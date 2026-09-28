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

#ifndef __UTEST_TESTTCPFORCEDCANCELLINGER_H_
#define __UTEST_TESTTCPFORCEDCANCELLINGER_H_

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Utf.h>

#include "TcpTeardownTestUtils.h"

/************************************************************************
 * THE FORCED CANCEL AND THE SOCKET'S LINGER OPTION (owed-list row I13, change-set CS-6)
 *
 * WHAT THE FORCED PATH IS. A stream task's cancelTask( ) shuts its socket down through
 * TcpSocketCommonBase::shutdownSocket( ) with force set - directly for the two plain policies, and
 * posted to the stream's strand for the two stranded ones. That is shutdown_send and cancel, and it
 * used to set SO_LINGER to off first.
 *
 * WHY THAT WRITE WAS A DEFECT. asio records a linger the application sets in the socket's own state
 * byte (socket_ops::setsockopt, user_set_linger), and every operation started on the socket reads
 * that byte as it is built. The plain policies cancel from whichever thread asks, so the write
 * raced an operation which an I/O thread was starting on the same socket - ThreadSanitizer caught a
 * TLS handshake's next read doing exactly that, in utf_baselib_h2client3 (B6 of astra's second
 * review). The write bought nothing: linger is off by default, the acceptor sets it off explicitly,
 * and asio consults user_set_linger only when a socket is destroyed.
 *
 * WHAT THESE CASES PIN. A forced cancel of each of the four policies, woken from a read the task
 * armed itself, and then the socket's linger option read back. For the cleartext pair the peer is a
 * listener which never accepts - the kernel completes the connect and nothing else happens on the
 * wire; for the TLS pair it is a peer which completes the handshake and then says nothing. Either way
 * the cancel is the only thing that can end the task.
 *
 * THE CANCEL IS AIMED AT THAT READ, AND NEVER AT A HANDSHAKE. A TLS handshake is a chain of steps
 * inside asio, and between two of them nothing is registered with the reactor: a cancel landing there
 * reaps nothing, and the step that follows starts a read on a socket whose receive side the forced
 * path leaves open. The read here is registered before the case is told it is in flight, so the cancel
 * always has something to wake.
 *
 *   - Tcp_ForcedCancelLeavesTheLingerOptionOffTests - CHARACTERIZATION. After a forced cancel the
 *     option reads off (l_onoff = 0). It did before the fix because the forced path wrote it, and it
 *     does after because nothing ever turned it on; the case is green on both sides and is what
 *     says the graceful close a cancel makes did not change.
 *
 *   - Tcp_ForcedCancelDoesNotWriteTheLingerOptionTests - I13's RED. The socket's owner sets
 *     linger( on, 0 ) before the cancel, and the forced cancel must leave it as it found it. Before
 *     the fix it reads off - the forced path wrote it, which is the very write that raced - and after
 *     it reads on. It is certain rather than probable: a pure input, with no timing in it.
 *
 * See notes/plans/issues/astra-remediation-owed-work.md, row I13.
 */

namespace utest
{
    namespace forcedcancel
    {
        using tcpteardown::WAIT_IN_MILLISECONDS;
        using tcpteardown::chkOrFail;
        using tcpteardown::describeCode;
        using tcpteardown::TlsEndingPeer;

        /**
         * @brief A listening socket on an ephemeral loopback port which never accepts
         *
         * The kernel completes a connect out of the listen backlog, so a cleartext client connects
         * and then waits on a read for bytes which never come. Binding port zero keeps the cases free
         * of the machine global test lock
         */

        class SilentListener
        {
            BL_NO_COPY_OR_MOVE( SilentListener )

        public:

            SilentListener()
                :
                m_acceptor( m_ioService )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();
            }

            auto port() const -> unsigned short
            {
                return m_acceptor.local_endpoint().port();
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
        };

        /**
         * @brief A connection establisher over STREAM which connects, starts the one operation the
         * cancel has to wake, and then waits to be cancelled
         *
         * The socket is the policy's own - created by the establisher through STREAM::createSocket -
         * so each stranded policy has its strand and its cancelTask( ) takes the posting branch
         */

        template
        <
            typename STREAM
        >
        class ForcedCancelProbeT :
            public bl::tasks::TcpConnectionEstablisherConnector< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( ForcedCancelProbeT )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< STREAM >      base_type;
            typedef ForcedCancelProbeT< STREAM >                                this_type;

        protected:

            mutable bl::os::mutex                                               m_probeLock;
            mutable bl::os::condition_variable                                  m_probeCv;

            const bool                                                          m_isLingerSetFirst;

            bool                                                                m_isInFlight;
            bool                                                                m_hasStopped;
            bl::eh::error_code                                                  m_setOptionCode;

            char                                                                m_buffer[ 64 ];

            ForcedCancelProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const bool                              isLingerSetFirst
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_isLingerSetFirst( isLingerSetFirst ),
                m_isInFlight( false ),
                m_hasStopped( false )
            {
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                /*
                 * Reached from the connect handler for a cleartext policy, and from the handshake's
                 * completion for a TLS one. The read is registered with the reactor before
                 * async_read_some( ) returns - over TLS its first engine step runs inline and starts
                 * the socket read - so a case told it is in flight cancels an operation which exists
                 */

                if( m_isLingerSetFirst )
                {
                    /*
                     * THE OWNER OF THE SOCKET SETS A LINGER OF ITS OWN, here on the thread which is
                     * about to start the read and before the case is told of it - so nothing else
                     * touches the socket while it is written
                     */

                    bl::eh::error_code ec;

                    base_type::getSocket().set_option( bl::asio::socket_base::linger( true, 0 ), ec );

                    BL_MUTEX_GUARD( m_probeLock );

                    m_setOptionCode = ec;
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

                {
                    BL_MUTEX_GUARD( m_probeLock );

                    m_isInFlight = true;

                    m_probeCv.notify_all();
                }

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

            bool waitFor( SAA_in const bool this_type::* flag ) const
            {
                bl::os::mutex_unique_lock guard( m_probeLock );

                return m_probeCv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this, flag ]() -> bool
                    {
                        return this ->* flag;
                    }
                    );
            }

        public:

            /**
             * @brief Blocks until the operation the cancel has to wake has been started
             */

            bool waitForInFlight() const
            {
                return waitFor( &this_type::m_isInFlight );
            }

            /**
             * @brief Blocks until the task has stopped - onTaskStoppedNothrow( ) is reached only
             * once the task is ending, after any finish continuation of the policy has run
             */

            bool waitForStop() const
            {
                return waitFor( &this_type::m_hasStopped );
            }

            /**
             * @brief The socket's SO_LINGER, read back from the kernel
             *
             * A forced cancel shuts the socket down and cancels it but never closes it - the socket
             * lives as long as this object - so the option can be read after the task has ended
             */

            auto setOptionCode() const -> bl::eh::error_code
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_setOptionCode;
            }

            auto readLinger( SAA_inout bl::eh::error_code& ec ) const -> bl::asio::socket_base::linger
            {
                bl::asio::socket_base::linger option;

                base_type::getSocket().get_option( option, ec );

                return option;
            }
        };

        template
        <
            typename STREAM
        >
        using ForcedCancelProbeImpl = bl::om::ObjectImpl< ForcedCancelProbeT< STREAM > >;

        /**
         * @brief What one forced cancel came to, read on the test thread
         */

        struct ForcedCancelResult
        {
            bool                                                                isInFlight;
            bool                                                                hasStopped;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            bl::eh::error_code                                                  setOptionCode;
            bl::eh::error_code                                                  getOptionCode;
            bool                                                                isLingerEnabled;
            int                                                                 lingerTimeout;

            ForcedCancelResult()
                :
                isInFlight( false ),
                hasStopped( false ),
                isFailed( false ),
                isLingerEnabled( false ),
                lingerTimeout( -1 )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "in flight " ) +
                    ( isInFlight ? "yes" : "no" ) +
                    ", stopped " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
                    ", the owner's linger( on, 0 ) " +
                    describeCode( setOptionCode ) +
                    ", SO_LINGER " +
                    (
                        getOptionCode ?
                            "unreadable " + describeCode( getOptionCode )
                            :
                            "( " +
                                std::string( isLingerEnabled ? "on" : "off" ) +
                                ", " +
                                bl::utils::lexical_cast< std::string >( lingerTimeout ) +
                                " )"
                    );
            }
        };

        /**
         * @brief Connects a probe over STREAM to the given port, cancels it once its read is in
         * flight, and reads its socket's linger option back after the task has ended
         */

        template
        <
            typename STREAM
        >
        inline auto runForcedCancelAgainst(
            SAA_in          std::string&&                                       host,
            SAA_in          const unsigned short                                port,
            SAA_in          const bool                                          isLingerSetFirst
            )
            -> ForcedCancelResult
        {
            using namespace bl;
            using namespace bl::tasks;

            ForcedCancelResult result;

            const auto probe = ForcedCancelProbeImpl< STREAM >::createInstance(
                BL_PARAM_FWD( host ),
                port,
                isLingerSetFirst
                );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ &probe, &task, &result ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.isInFlight = probe -> waitForInFlight();

                    task -> requestCancel();

                    result.hasStopped = probe -> waitForStop();

                    eq -> wait( task );
                }
                );

            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            result.setOptionCode = probe -> setOptionCode();

            const auto option = probe -> readLinger( result.getOptionCode );

            result.isLingerEnabled = option.enabled();
            result.lingerTimeout = option.timeout();

            return result;
        }

        /**
         * @brief The same, against the peer STREAM needs: a listener which never accepts for a
         * cleartext policy, and for a TLS one a peer which completes the handshake and then says
         * nothing until the client ends the stream
         */

        template
        <
            typename STREAM
        >
        inline auto runForcedCancel( SAA_in_opt const bool isLingerSetFirst = false ) -> ForcedCancelResult
        {
            if( STREAM::isProtocolHandshakeNeeded )
            {
                TlsEndingPeer peer( TlsEndingPeer::Ending::AwaitTheClient );

                return runForcedCancelAgainst< STREAM >( std::string( "localhost" ), peer.port(), isLingerSetFirst );
            }

            SilentListener listener;

            return runForcedCancelAgainst< STREAM >( std::string( "127.0.0.1" ), listener.port(), isLingerSetFirst );
        }

        /**
         * @brief What every forced cancel asserts: it woke the operation it was aimed at, and the
         * task ended as a cancel with its socket still there to be asked
         */

        inline void chkEndedByTheForcedCancel(
            SAA_in          const ForcedCancelResult&                           result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.isInFlight,
                which + ": the operation the cancel had to wake was never started; " + result.describe()
                );

            chkOrFail(
                result.hasStopped,
                which + ": the forced cancel did not end the task; " + result.describe()
                );

            chkOrFail(
                result.isFailed && bl::asio::error::operation_aborted == result.taskCode,
                which + ": the task did not end as a cancel; " + result.describe()
                );

            chkOrFail(
                ! result.getOptionCode,
                which + ": the socket's linger option could not be read back; " + result.describe()
                );
        }

    } // forcedcancel

} // utest

/**
 * @brief CHARACTERIZATION - after a forced cancel the socket's linger option reads off
 *
 * The same before and after I13's fix, for all four stream policies: before it, because the forced
 * path wrote linger( false, 0 ); after it, because nothing turns the option on - it is off by
 * default. Off is the graceful close: a close( ) which does not block and does not reset the peer
 */

UTF_AUTO_TEST_CASE( Tcp_ForcedCancelLeavesTheLingerOptionOffTests )
{
    using namespace bl::tasks;
    using namespace utest::forcedcancel;

    const auto chkLingerOff = []( SAA_in const ForcedCancelResult& result, SAA_in const std::string& which ) -> void
    {
        chkEndedByTheForcedCancel( result, which );

        chkOrFail(
            ! result.isLingerEnabled,
            which + ": the socket lingers after a forced cancel; " + result.describe()
            );
    };

    chkLingerOff( runForcedCancel< TcpSocketAsyncBase >(), "cleartext" );
    chkLingerOff( runForcedCancel< TcpSocketAsyncStrandedBase >(), "cleartext, stranded" );
    chkLingerOff( runForcedCancel< TcpSslSocketAsyncBase >(), "TLS" );
    chkLingerOff( runForcedCancel< TcpSslSocketAsyncStrandedBase >(), "TLS, stranded" );
}

/**
 * @brief I13 - a forced cancel does not write the socket's linger option
 *
 * The owner of the socket sets linger( on, 0 ) before the cancel, and after a forced cancel the
 * option still reads on, for all four stream policies. All four are run before any is asserted, so a
 * failure message carries every reading
 */

UTF_AUTO_TEST_CASE( Tcp_ForcedCancelDoesNotWriteTheLingerOptionTests )
{
    using namespace bl::tasks;
    using namespace utest::forcedcancel;

    const ForcedCancelResult results[] =
    {
        runForcedCancel< TcpSocketAsyncBase >( true /* isLingerSetFirst */ ),
        runForcedCancel< TcpSocketAsyncStrandedBase >( true /* isLingerSetFirst */ ),
        runForcedCancel< TcpSslSocketAsyncBase >( true /* isLingerSetFirst */ ),
        runForcedCancel< TcpSslSocketAsyncStrandedBase >( true /* isLingerSetFirst */ ),
    };

    const char* const names[] =
    {
        "cleartext",
        "cleartext, stranded",
        "TLS",
        "TLS, stranded",
    };

    std::string readings;

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        readings += std::string( names[ i ] ) + ": " + results[ i ].describe() + "; ";
    }

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        const std::string which( names[ i ] );

        chkEndedByTheForcedCancel( results[ i ], which );

        chkOrFail(
            ! results[ i ].setOptionCode,
            which + ": the owner could not set its linger; all four: " + readings
            );

        chkOrFail(
            results[ i ].isLingerEnabled,
            which + ": the forced cancel wrote the socket's linger option; all four: " + readings
            );
    }
}

#endif /* __UTEST_TESTTCPFORCEDCANCELLINGER_H_ */
