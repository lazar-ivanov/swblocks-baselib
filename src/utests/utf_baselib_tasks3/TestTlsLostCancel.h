/*
 * This file is part of the swblocks-baselib library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __UTEST_TESTTLSLOSTCANCEL_H_
#define __UTEST_TESTTLSLOSTCANCEL_H_

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/HeldIoThreads.h>
#include <utests/baselib/Utf.h>

#include "TcpTeardownTestUtils.h"
#include "TestTcpForcedCancelLinger.h"

/************************************************************************
 * A FORCED CANCEL LOST BETWEEN TWO STEPS OF A TLS OPERATION (D-L3-1, change-set CS-6)
 *
 * WHAT IS UNDER TEST. A forced cancel shuts the socket's send side down and cancels what is
 * registered with the reactor at that instant. A TLS handshake or shutdown is a chain of socket
 * operations inside asio, and between one step's completion and the next step's start nothing of it
 * is registered: a cancel which lands there reaps nothing, the next step's read registers on a socket
 * whose receive side is still open, and it waits for the peer. requestCancelInternal( ) is
 * idempotent, so no deadline can issue the cancel again either
 * (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md). The decided shape: (c) a forced
 * cancel also shuts the receive side down while the task's own handshake is incomplete; (a2) the
 * deadlines which survive a cancel issue it again; and the connector does not retry a cancelled
 * establishment.
 *
 * THE CHARACTERIZATION, committed before the fixes and green on both sides of them:
 *
 *   - TlsLostCancel_ACancelWithTheHandshakeReadRegisteredEndsPromptlyTests - the cancel lands while
 *     the handshake's read is registered, which is the ordinary case: it ends the task at once, as a
 *     cancel, and the peer, which sends nothing, sees an orderly end of stream.
 *   - TlsLostCancel_APeerSeesAnOrderlyEndAfterAForcedCancelTests - past the handshake, a forced
 *     cancel of a registered read ends the task as a cancel, and the peer sees our FIN - a stream
 *     ended with no close_notify - over both TLS policies.
 *
 * AND THE REDS, committed before the fixes:
 *
 *   - TlsLostCancel_ACancelBeforeTheHandshakeIsNotLostTests - W1 on the stranded policy: the cancel
 *     is requested in the connect handler, on the strand, before the handshake starts. Its forced
 *     shutdown is posted to the strand first, and the ClientHello's completion reaches the strand only
 *     through the scheduler, behind it - so the shutdown reaps nothing, and the read for the
 *     ServerHello waits for a peer which never answers. Red today; (c) ends it at once.
 *   - TlsLostCancel_ACancelAfterTheHandshakeStartsIsNotLostTests - W1 on the plain policy: the I/O
 *     pool's other threads are held, and the cancel is requested in the connect handler right after
 *     the handshake starts, so the ClientHello's completion cannot run before it. Red today; (c).
 *   - TlsLostCancel_ACancelInsideTheTlsShutdownIsReissuedTests - W2 on the stranded policy: the
 *     cancel is requested in the finish continuation, on the strand, once the TLS shutdown has
 *     started, with the pool's other threads held, and the peer holds its answer. The 2 s protocol
 *     timer fires and does nothing today; (a2) makes it issue the cancel again.
 *   - TlsLostCancel_ACancelledHandshakeIsNotRetriedTests - W1's stranded order, against a peer which
 *     reads the ClientHello whole and then shuts its send side: the handshake fails with a
 *     truncation, which the connector counts as retryable, and today it restarts the cancelled
 *     establishment. The retry guard stops that.
 *
 * AND THE FIX'S OWN RECORD, committed with (c): TlsLostCancel_TheHandshakeFlagBelongsToTheOwnHandshakeTests
 * pins when the policy counts a handshake as this task's own - from its start until it completes, and
 * never across a retry's new stream, a detached stream or an attached one. It is what keeps the
 * receive shutdown off every application phase, and no ending can show it: a cancel reaps a registered
 * read before any receive shutdown would.
 *
 * Every order above is fixed by a posting order or by a single free I/O thread, never by a delay.
 * The bound only decides how long a case gives its task before it makes the peer act, which is what
 * ends a lost cancel today.
 */

namespace utest
{
    namespace lostcancel
    {
        using tcpteardown::WAIT_IN_MILLISECONDS;
        using tcpteardown::chkOrFail;
        using tcpteardown::describeCode;
        using tcpteardown::joinRecords;
        using tcpteardown::TlsEndingPeer;
        using forcedcancel::SilentListener;
        using heldiothreads::HeldIoThreads;

        enum : std::size_t
        {
            /**
             * @brief How long a case gives its task to end after its cancel, before it makes the
             * peer act - far above a prompt ending, and above the 2 s protocol timer the shutdown
             * case sets, so that a cancel issued again by that timer ends the task inside it
             */

            LOST_CANCEL_BOUND_IN_MILLISECONDS   = 5000U,
        };

        /**
         * @brief A loopback TCP peer which accepts one connection, reads one TLS record - the
         * ClientHello - whole and never sends a byte of TLS; it then reads until the client ends the
         * stream, or first shuts its own send side down, and it keeps its socket open until the case
         * releases it
         */

        class RawHandshakePeer
        {
            BL_NO_COPY_OR_MOVE( RawHandshakePeer )

        public:

            enum class Ending
            {
                /**
                 * @brief Reads until the client ends the stream, and records how it ended
                 */

                ReadToTheEnd,

                /**
                 * @brief Shuts its send side down once the ClientHello is in - an orderly close,
                 * which the client's handshake reads as a truncation - and then reads to the end
                 */

                ShutSendAfterTheClientHello,
            };

            explicit RawHandshakePeer( SAA_in const Ending ending )
                :
                m_acceptor( m_ioService ),
                m_port( 0U ),
                m_ending( ending ),
                m_isReleased( false )
            {
                const bl::asio::ip::tcp::endpoint endpoint(
                    bl::asio::ip::address_v4::loopback(),
                    0 /* ephemeral */
                    );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                m_port = m_acceptor.local_endpoint().port();

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &RawHandshakePeer::run, this ) ) );
            }

            ~RawHandshakePeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                release();

                {
                    /*
                     * One throwaway connection wakes a worker still blocked in accept( ) - harmless
                     * when the connection under test has already been accepted
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                        ec
                        );

                    socket.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            bl::os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            /**
             * @brief Lets the peer close its socket once its script has ended
             */

            void release()
            {
                BL_MUTEX_GUARD( m_lock );

                m_isReleased = true;

                m_cv.notify_all();
            }

            /**
             * @brief Blocks until a record which starts with 'prefix' has been made, or the bound
             * expires
             */

            bool waitForRecord( SAA_in const std::string& prefix ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this, &prefix ]() -> bool
                    {
                        for( const auto& record : m_records )
                        {
                            if( 0U == record.compare( 0U, prefix.size(), prefix ) )
                            {
                                return true;
                            }
                        }

                        return false;
                    }
                    );
            }

        private:

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );

                m_cv.notify_all();
            }

            void waitForRelease()
            {
                bl::os::mutex_unique_lock guard( m_lock );

                ( void ) m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_isReleased;
                    }
                    );
            }

            /**
             * @brief Reads one TLS record whole: the five octet header, then the length it gives
             */

            static auto readRecord( SAA_inout bl::asio::ip::tcp::socket& socket ) -> bl::eh::error_code
            {
                unsigned char header[ 5 ];

                bl::eh::error_code ec;

                ( void ) bl::asio::read( socket, bl::asio::buffer( header, sizeof( header ) ), ec );

                if( ec )
                {
                    return ec;
                }

                const std::size_t length =
                    ( static_cast< std::size_t >( header[ 3 ] ) << 8 ) | static_cast< std::size_t >( header[ 4 ] );

                std::vector< unsigned char > body( length );

                ( void ) bl::asio::read( socket, bl::asio::buffer( body ), ec );

                return ec;
            }

            static auto readToTheEnd( SAA_inout bl::asio::ip::tcp::socket& socket ) -> bl::eh::error_code
            {
                char buffer[ 4096 ];

                for( ;; )
                {
                    bl::eh::error_code ec;

                    ( void ) socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec )
                    {
                        return ec;
                    }
                }
            }

            void run()
            {
                bl::asio::ip::tcp::socket socket( m_ioService );

                bl::eh::error_code ec;

                m_acceptor.accept( socket, ec );

                if( ec )
                {
                    record( "accept-failed:" + describeCode( ec ) );
                }
                else
                {
                    ec = readRecord( socket );

                    record( "client-hello:" + describeCode( ec ) );

                    if( ! ec && Ending::ShutSendAfterTheClientHello == m_ending )
                    {
                        socket.shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );

                        record( "shut-send:" + describeCode( ec ) );
                    }

                    record( "client-ended:" + describeCode( readToTheEnd( socket ) ) );
                }

                record( "script-ended" );

                /*
                 * THE SOCKET IS KEPT OPEN UNTIL THE CASE RELEASES IT, so that the peer's side going
                 * away can never be what ends the client's task
                 */

                waitForRelease();

                socket.close( ec );
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::os::port_t                                                      m_port;
            const Ending                                                        m_ending;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::vector< std::string >                                          m_records;
            bool                                                                m_isReleased;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /**
         * @brief Where the probe requests its cancel
         */

        enum class LostCancelMode
        {
            /**
             * @brief In the connect handler, before the handshake starts
             */

            CancelBeforeTheHandshake,

            /**
             * @brief In the connect handler, right after the handshake starts
             */

            CancelAfterTheHandshakeStarts,

            /**
             * @brief From a handler queued two hops behind the handshake's start, which with one
             * free I/O thread runs only once the ClientHello's completion has registered the read
             * for the ServerHello
             */

            CancelOnceTheReadIsRegistered,

            /**
             * @brief In the finish continuation, right after the TLS shutdown starts
             */

            CancelInsideTheTlsShutdown,

            /**
             * @brief Past the handshake a read is armed, and the case cancels it
             */

            CancelAReadAfterTheHandshake,
        };

        /**
         * @brief A connection establisher over a TLS policy which requests its cancel at one chosen
         * point of the handshake or of the TLS shutdown, and records whether the connector restarted
         * the establishment afterwards
         */

        template
        <
            typename STREAM
        >
        class LostCancelProbeT :
            public bl::tasks::TcpConnectionEstablisherConnector< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( LostCancelProbeT )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< STREAM >      base_type;
            typedef LostCancelProbeT< STREAM >                                  this_type;

        protected:

            const LostCancelMode                                                m_mode;

            mutable bl::os::mutex                                               m_probeLock;
            mutable bl::os::condition_variable                                  m_probeCv;

            bool                                                                m_isStarted;
            bool                                                                m_hasStopped;
            bool                                                                m_wasRestarted;
            bool                                                                m_isShutdownCancelRequested;

            char                                                                m_buffer[ 64 ];

            LostCancelProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const LostCancelMode                    mode
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_mode( mode ),
                m_isStarted( false ),
                m_hasStopped( false ),
                m_wasRestarted( false ),
                m_isShutdownCancelRequested( false )
            {
                if( LostCancelMode::CancelInsideTheTlsShutdown == mode )
                {
                    base_type::isCloseStreamOnTaskFinish( true );

                    base_type::setProtocolTimeout( bl::time::seconds( 2 ) );
                }
            }

            void signalStarted()
            {
                BL_MUTEX_GUARD( m_probeLock );

                m_isStarted = true;

                m_probeCv.notify_all();
            }

            /**
             * @brief Queues the cancel two hops behind everything queued so far
             *
             * The ClientHello's completion is queued when the handshake starts - on the scheduler's
             * queue, or on the running thread's own, which the scheduler appends to its queue once
             * the running handler returns. Either way the second hop is queued behind it, and with
             * one free I/O thread the queue runs in order
             */

            void postCancelTwoHopsLater()
            {
                /*
                 * The default pool outlives every task, so its service may be held by address
                 */

                auto* const ioService =
                    &bl::ThreadPoolDefault::getDefault( base_type::getThreadPoolId() ) -> aioService();

                const auto ref = bl::om::ObjPtrCopyable< this_type >::acquireRef( this );

                ioService -> post(
                    [ ref, ioService ]() -> void
                    {
                        ioService -> post(
                            [ ref ]() -> void
                            {
                                ref -> requestCancel();
                            }
                            );
                    }
                    );
            }

            virtual bool beginPreHandshakeStage( SAA_in const bl::cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                /*
                 * Entered from the connect handler, under the task lock, which
                 * requestCancelInternal( ) requires; continueCallback( ) starts the handshake
                 */

                switch( m_mode )
                {
                    case LostCancelMode::CancelBeforeTheHandshake:
                        {
                            base_type::requestCancelInternal();

                            const bool result = continueCallback();

                            signalStarted();

                            return result;
                        }

                    case LostCancelMode::CancelAfterTheHandshakeStarts:
                        {
                            const bool result = continueCallback();

                            base_type::requestCancelInternal();

                            signalStarted();

                            return result;
                        }

                    case LostCancelMode::CancelOnceTheReadIsRegistered:
                        {
                            const bool result = continueCallback();

                            postCancelTwoHopsLater();

                            signalStarted();

                            return result;
                        }

                    case LostCancelMode::CancelInsideTheTlsShutdown:
                    case LostCancelMode::CancelAReadAfterTheHandshake:
                        break;
                }

                return continueCallback();
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                if( LostCancelMode::CancelAReadAfterTheHandshake != m_mode )
                {
                    /*
                     * The shutdown mode ends at once, so that the finish continuation begins the TLS
                     * shutdown; the handshake modes never get here
                     */

                    return false;
                }

                base_type::getStream().async_read_some(
                    bl::asio::buffer( m_buffer, sizeof( m_buffer ) ),
                    bl::cpp::bind(
                        &this_type::onRead,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error,
                        bl::asio::placeholders::bytes_transferred
                        )
                    );

                /*
                 * The read's first engine step runs inline and registers the socket read, and the
                 * peer sends nothing, so the case cancels a read which is registered
                 */

                signalStarted();

                return true;
            }

            void onRead(
                SAA_in                  const bl::eh::error_code&               ec,
                SAA_in                  const std::size_t                       bytesTransferred
                ) NOEXCEPT
            {
                BL_UNUSED( bytesTransferred );

                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                BL_TASKS_HANDLER_END()
            }

            virtual bool scheduleTaskFinishContinuation( SAA_in_opt const std::exception_ptr& eptrIn = nullptr ) OVERRIDE
            {
                const std::size_t retriesBefore = base_type::m_retries.value();

                const bool result = base_type::scheduleTaskFinishContinuation( eptrIn );

                if( base_type::m_retries.value() != retriesBefore )
                {
                    /*
                     * The connector took a failed handshake up again - resolve, connect and
                     * handshake from the start
                     */

                    BL_MUTEX_GUARD( m_probeLock );

                    m_wasRestarted = true;
                }

                if(
                    result &&
                    LostCancelMode::CancelInsideTheTlsShutdown == m_mode &&
                    ! m_isShutdownCancelRequested
                    )
                {
                    /*
                     * On the strand, under the task lock, with our close_notify just written and the
                     * completion of that write queued on the scheduler
                     */

                    m_isShutdownCancelRequested = true;

                    base_type::requestCancelInternal();

                    signalStarted();
                }

                return result;
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( m_probeLock );

                m_hasStopped = true;

                m_probeCv.notify_all();

                BL_NOEXCEPT_END()

                return result;
            }

            bool waitFor(
                SAA_in                  const bool this_type::*                 flag,
                SAA_in                  const std::size_t                       timeoutInMilliseconds
                ) const
            {
                bl::os::mutex_unique_lock guard( m_probeLock );

                return m_probeCv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this, flag ]() -> bool
                    {
                        return this ->* flag;
                    }
                    );
            }

        public:

            /**
             * @brief Blocks until the cancel has been requested - or, past the handshake, until the
             * read the case cancels is registered
             */

            bool waitForStart() const
            {
                return waitFor( &this_type::m_isStarted, static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            /**
             * @brief Blocks until the task has stopped - onTaskStoppedNothrow( ) is reached only
             * once the policy's finish continuation has returned false
             */

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return waitFor( &this_type::m_hasStopped, timeoutInMilliseconds );
            }

            bool wasRestarted() const
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_wasRestarted;
            }
        };

        template
        <
            typename STREAM
        >
        using LostCancelProbeImpl = bl::om::ObjectImpl< LostCancelProbeT< STREAM > >;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct LostCancelResult
        {
            bool                                                                isStarted;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
            bool                                                                hasStopped;
            bool                                                                wasRestarted;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;

            LostCancelResult()
                :
                isStarted( false ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
                hasStopped( false ),
                wasRestarted( false ),
                isFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "started " ) +
                    ( isStarted ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", stopped once the peer acted " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", establishment restarted " +
                    ( wasRestarted ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) );
            }
        };

        /**
         * @brief Runs one probe: waits until its cancel is requested - or, past the handshake,
         * requests it - gives the task the bound to stop, then makes the peer act and waits for the
         * task to end
         */

        template
        <
            typename STREAM
        >
        inline auto runLostCancel(
            SAA_in          const LostCancelMode                                mode,
            SAA_in          const std::string&                                  host,
            SAA_in          const bl::os::port_t                                port,
            SAA_in          const bl::cpp::void_callback_t&                     makeThePeerAct
            )
            -> LostCancelResult
        {
            using namespace bl;
            using namespace bl::tasks;

            LostCancelResult result;

            const auto probe = LostCancelProbeImpl< STREAM >::createInstance( std::string( host ), port, mode );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.isStarted = probe -> waitForStart();

                    if( result.isStarted && LostCancelMode::CancelAReadAfterTheHandshake == mode )
                    {
                        task -> requestCancel();
                    }

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( LOST_CANCEL_BOUND_IN_MILLISECONDS ) );

                    result.isCanceled = probe -> isCanceled();

                    makeThePeerAct();

                    result.hasStopped =
                        probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( task );
                }
                );

            result.wasRestarted = probe -> wasRestarted();
            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            return result;
        }

        inline bool hasRecordStartingWith(
            SAA_in          const std::vector< std::string >&                   records,
            SAA_in          const std::string&                                  prefix
            )
        {
            for( const auto& record : records )
            {
                if( 0U == record.compare( 0U, prefix.size(), prefix ) )
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief A cancel which worked: the task stopped inside the bound, as a cancel
         *
         * A task whose socket was shut down forcefully reports operation_aborted whatever the
         * operation it was in failed with - TcpSocketCommonBase::onTaskStoppedNothrow( ) chains
         * that failure onto it
         */

        inline void chkEndedPromptlyAsACancel(
            SAA_in          const LostCancelResult&                             result,
            SAA_in          const std::string&                                  which,
            SAA_in          const std::string&                                  readings
            )
        {
            chkOrFail( result.isStarted, which + ": the cancel was never requested; " + readings );

            chkOrFail(
                result.hasStoppedWithinBound && result.isCanceled,
                which + ": the task did not end within the bound after its cancel; " + readings
                );

            chkOrFail(
                result.isFailed && bl::asio::error::operation_aborted == result.taskCode,
                which + ": the task did not end as a cancel; " + readings
                );
        }

        /**
         * @brief A connection establisher over the plain TLS policy which records, under the task
         * lock, whether the policy counts a handshake as this task's own - before its handshake
         * starts, once it has started, and once it has completed - and which fails its second
         * attempt, if the connector makes one, before that attempt's handshake starts
         */

        class HandshakeFlagProbe :
            public bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( HandshakeFlagProbe )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSslSocketAsyncBase >
                base_type;

        protected:

            std::vector< std::string >                                          m_flags;
            std::size_t                                                         m_attempts;

            HandshakeFlagProbe(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const bool                              isRetried
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_attempts( 0U )
            {
                if( ! isRetried )
                {
                    base_type::m_maxRetryCount = 0U;
                }
            }

            void recordFlag( SAA_in const std::string& where )
            {
                m_flags.push_back( where + ( base_type::m_isOwnHandshakeRunning ? ": set" : ": clear" ) );
            }

            virtual bool beginPreHandshakeStage( SAA_in const bl::cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                ++m_attempts;

                if( m_attempts > 1U )
                {
                    recordFlag( "the second attempt, before its handshake" );

                    BL_THROW(
                        bl::UnexpectedException(),
                        BL_MSG()
                            << "The probe ends the task at its second attempt"
                        );
                }

                recordFlag( "before the handshake" );

                const bool result = continueCallback();

                /*
                 * Still under the lock of the connect handler, which the handshake's own completion
                 * needs too, so the handshake has started here and cannot have completed
                 */

                recordFlag( "the handshake started" );

                return result;
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                recordFlag( "the handshake completed" );

                return false;
            }

        public:

            auto flags() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( base_type::m_lock );

                return m_flags;
            }

            bool isOwnHandshakeRunning() const
            {
                BL_MUTEX_GUARD( base_type::m_lock );

                return base_type::m_isOwnHandshakeRunning;
            }
        };

        typedef bl::om::ObjectImpl< HandshakeFlagProbe >                        HandshakeFlagProbeImpl;

        /**
         * @brief Runs a flag probe to its end
         */

        inline void runFlagProbe( SAA_in const bl::om::ObjPtr< HandshakeFlagProbeImpl >& probe )
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );
        }

    } // lostcancel

} // utest

/**
 * @brief CHARACTERIZATION - a cancel which lands while the handshake's read is registered ends the
 * task at once, as a cancel, and the peer sees an orderly end of stream
 *
 * The plain policy's cancel runs on the thread which requests it, and the I/O pool's other threads
 * are held: the connect handler starts the handshake and queues the cancel two hops later, so the
 * ClientHello's completion - which registers the read for the ServerHello - runs first. The peer
 * reads the ClientHello and sends nothing
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_ACancelWithTheHandshakeReadRegisteredEndsPromptlyTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    RawHandshakePeer peer( RawHandshakePeer::Ending::ReadToTheEnd );

    LostCancelResult result;

    {
        HeldIoThreads held;

        chkOrFail( held.isHeld(), "the I/O pool's other threads could not be held" );

        result = runLostCancel< TcpSslSocketAsyncBase >(
            LostCancelMode::CancelOnceTheReadIsRegistered,
            "127.0.0.1",
            peer.port(),
            [ &peer ]() -> void
            {
                peer.release();
            }
            );
    }

    ( void ) peer.waitForRecord( "script-ended" );

    const auto records = peer.records();

    const std::string readings = result.describe() + "; peer " + joinRecords( records );

    chkEndedPromptlyAsACancel( result, "TLS, the handshake's read registered", readings );

    chkOrFail(
        hasRecordStartingWith( records, "client-hello:success" ) &&
            hasRecordStartingWith( records, "client-ended:" + describeCode( bl::asio::error::eof ) ),
        "the peer did not read the ClientHello and then an orderly end of stream; " + readings
        );
}

/**
 * @brief CHARACTERIZATION - past the handshake, a forced cancel ends the task as a cancel and the
 * peer sees our FIN: a stream ended with no close_notify, and no reset
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_APeerSeesAnOrderlyEndAfterAForcedCancelTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    const auto chk = []( SAA_in const std::string& which, SAA_in const LostCancelResult& result, SAA_in const TlsEndingPeer& peer ) -> void
    {
        ( void ) peer.waitForRecord( "script-ended" );

        const auto records = peer.records();

        const std::string readings = result.describe() + "; peer " + joinRecords( records );

        chkEndedPromptlyAsACancel( result, which, readings );

        chkOrFail(
            hasRecordStartingWith( records, "client-ended:asio.ssl.stream:1" ),
            which + ": the peer did not see the stream end with no close_notify; " + readings
            );
    };

    {
        TlsEndingPeer peer( TlsEndingPeer::Ending::AwaitTheClient );

        const auto result = runLostCancel< TcpSslSocketAsyncBase >(
            LostCancelMode::CancelAReadAfterTheHandshake,
            "localhost",
            peer.port(),
            [ &peer ]() -> void
            {
                peer.release();
            }
            );

        chk( "TLS", result, peer );
    }

    {
        TlsEndingPeer peer( TlsEndingPeer::Ending::AwaitTheClient );

        const auto result = runLostCancel< TcpSslSocketAsyncStrandedBase >(
            LostCancelMode::CancelAReadAfterTheHandshake,
            "localhost",
            peer.port(),
            [ &peer ]() -> void
            {
                peer.release();
            }
            );

        chk( "TLS, stranded", result, peer );
    }
}

/**
 * @brief D-L3-1's RED, W1 STRANDED - a cancel requested before the handshake starts is not lost
 *
 * The peer is a listener which never accepts: the ClientHello sits in its backlog and nothing ever
 * answers it. Once the bound is over the case closes the listener, which resets the connection
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_ACancelBeforeTheHandshakeIsNotLostTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    auto listener = bl::cpp::SafeUniquePtr< SilentListener >::attach( new SilentListener() );

    const auto result = runLostCancel< TcpSslSocketAsyncStrandedBase >(
        LostCancelMode::CancelBeforeTheHandshake,
        "127.0.0.1",
        listener -> port(),
        [ &listener ]() -> void
        {
            listener.reset();
        }
        );

    chkEndedPromptlyAsACancel( result, "TLS, stranded, the cancel before the handshake", result.describe() );
}

/**
 * @brief D-L3-1's RED, W1 PLAIN - a cancel requested right after the handshake starts is not lost
 *
 * With the I/O pool's other threads held, the ClientHello's completion cannot run until the connect
 * handler which requests the cancel has returned, so the cancel finds nothing registered
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_ACancelAfterTheHandshakeStartsIsNotLostTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    auto listener = bl::cpp::SafeUniquePtr< SilentListener >::attach( new SilentListener() );

    LostCancelResult result;

    {
        HeldIoThreads held;

        chkOrFail( held.isHeld(), "the I/O pool's other threads could not be held" );

        result = runLostCancel< TcpSslSocketAsyncBase >(
            LostCancelMode::CancelAfterTheHandshakeStarts,
            "127.0.0.1",
            listener -> port(),
            [ &listener ]() -> void
            {
                listener.reset();
            }
            );
    }

    chkEndedPromptlyAsACancel( result, "TLS, the cancel right after the handshake starts", result.describe() );
}

/**
 * @brief D-L3-1's RED, W2 STRANDED - a cancel lost inside the TLS shutdown is issued again by the
 * protocol timer
 *
 * The I/O pool's other threads are held, so the completion of our close_notify's write cannot reach
 * the strand before the forced shutdown the cancel posts there. The peer reads our close_notify and
 * holds its own answer, its socket open, until the case releases it; the probe's 2 s protocol timer
 * is the one deadline left
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_ACancelInsideTheTlsShutdownIsReissuedTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    TlsEndingPeer peer( TlsEndingPeer::Ending::AwaitTheClient, true /* isAnswerHeld */ );

    LostCancelResult result;

    {
        HeldIoThreads held;

        chkOrFail( held.isHeld(), "the I/O pool's other threads could not be held" );

        result = runLostCancel< TcpSslSocketAsyncStrandedBase >(
            LostCancelMode::CancelInsideTheTlsShutdown,
            "localhost",
            peer.port(),
            [ &peer ]() -> void
            {
                peer.release();
            }
            );
    }

    ( void ) peer.waitForRecord( "script-ended" );

    const auto records = peer.records();

    const std::string readings = result.describe() + "; peer " + joinRecords( records );

    chkOrFail(
        hasRecordStartingWith( records, "client-ended:" + describeCode( bl::asio::error::eof ) ),
        "the peer did not read our close_notify, so the TLS shutdown never started; " + readings
        );

    chkOrFail( result.isStarted, "the cancel was never requested; " + readings );

    chkOrFail(
        result.hasStoppedWithinBound && result.isCanceled,
        "the protocol timer did not issue the TLS shutdown's lost cancel again; " + readings
        );
}

/**
 * @brief D-L3-1's RED, THE RETRY GUARD - a cancelled establishment is not restarted
 *
 * W1's stranded order, against a peer which reads the ClientHello whole and then shuts its send
 * side: the handshake fails with a truncation, which the connector counts as retryable
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_ACancelledHandshakeIsNotRetriedTests )
{
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    RawHandshakePeer peer( RawHandshakePeer::Ending::ShutSendAfterTheClientHello );

    const auto result = runLostCancel< TcpSslSocketAsyncStrandedBase >(
        LostCancelMode::CancelBeforeTheHandshake,
        "127.0.0.1",
        peer.port(),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    ( void ) peer.waitForRecord( "script-ended" );

    const auto records = peer.records();

    const std::string readings = result.describe() + "; peer " + joinRecords( records );

    chkOrFail(
        hasRecordStartingWith( records, "client-hello:success" ) &&
            hasRecordStartingWith( records, "shut-send:success" ),
        "the peer did not read the ClientHello and shut its send side; " + readings
        );

    chkOrFail( ! result.wasRestarted, "the connector restarted a cancelled establishment; " + readings );

    chkEndedPromptlyAsACancel( result, "TLS, stranded, the cancelled handshake", readings );
}

/**
 * @brief (c)'s RECORD - a handshake counts as this task's own from its start until it completes, and
 * never across a retry's new stream, a detached stream or an attached one
 *
 * Four runs over the plain TLS policy, each a pure input. A handshake which completes clears the
 * record before the continuation runs; one which fails leaves it set, which is harmless on a stream
 * that never carries application data - so the retry's new stream, a detached stream and an attached
 * one each have to clear it
 */

UTF_AUTO_TEST_CASE( TlsLostCancel_TheHandshakeFlagBelongsToTheOwnHandshakeTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::lostcancel;

    const auto describeFlags = []( SAA_in const std::vector< std::string >& flags ) -> std::string
    {
        return joinRecords( flags );
    };

    {
        /*
         * A handshake which completes
         */

        TlsEndingPeer peer( TlsEndingPeer::Ending::AwaitTheClient );

        const auto probe = HandshakeFlagProbeImpl::createInstance( std::string( "localhost" ), peer.port(), true );

        runFlagProbe( probe );

        const auto flags = probe -> flags();

        const std::vector< std::string > expected =
        {
            "before the handshake: clear",
            "the handshake started: set",
            "the handshake completed: clear",
        };

        chkOrFail(
            expected == flags && ! probe -> isOwnHandshakeRunning(),
            "a completed handshake: " + describeFlags( flags )
            );
    }

    {
        /*
         * A handshake which fails with a truncation, which the connector retries on a new stream
         */

        RawHandshakePeer peer( RawHandshakePeer::Ending::ShutSendAfterTheClientHello );

        const auto probe = HandshakeFlagProbeImpl::createInstance( std::string( "127.0.0.1" ), peer.port(), true );

        runFlagProbe( probe );

        const auto flags = probe -> flags();

        const std::vector< std::string > expected =
        {
            "before the handshake: clear",
            "the handshake started: set",
            "the second attempt, before its handshake: clear",
        };

        chkOrFail( expected == flags, "a retried handshake: " + describeFlags( flags ) );
    }

    {
        /*
         * A handshake which fails with no retry leaves the record set, and a detached stream clears it
         */

        RawHandshakePeer peer( RawHandshakePeer::Ending::ShutSendAfterTheClientHello );

        const auto probe = HandshakeFlagProbeImpl::createInstance( std::string( "127.0.0.1" ), peer.port(), false );

        runFlagProbe( probe );

        const bool isSetAtTheEnd = probe -> isOwnHandshakeRunning();

        const auto stream = probe -> detachStream();

        chkOrFail(
            isSetAtTheEnd && nullptr != stream && ! probe -> isOwnHandshakeRunning(),
            "a failed handshake, then its stream detached: " + describeFlags( probe -> flags() ) +
                "; set at the end " + ( isSetAtTheEnd ? "yes" : "no" )
            );
    }

    {
        /*
         * The same, and a stream attached in its place clears it
         */

        RawHandshakePeer peer( RawHandshakePeer::Ending::ShutSendAfterTheClientHello );

        const auto probe = HandshakeFlagProbeImpl::createInstance( std::string( "127.0.0.1" ), peer.port(), false );

        runFlagProbe( probe );

        const bool isSetAtTheEnd = probe -> isOwnHandshakeRunning();

        probe -> attachStream( TcpSslSocketAsyncBase::stream_ref() );

        chkOrFail(
            isSetAtTheEnd && ! probe -> isOwnHandshakeRunning(),
            "a failed handshake, then a stream attached: " + describeFlags( probe -> flags() ) +
                "; set at the end " + ( isSetAtTheEnd ? "yes" : "no" )
            );
    }
}

#endif /* __UTEST_TESTTLSLOSTCANCEL_H_ */
