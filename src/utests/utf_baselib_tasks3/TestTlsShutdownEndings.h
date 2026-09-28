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

#ifndef __UTEST_TESTTLSSHUTDOWNENDINGS_H_
#define __UTEST_TESTTLSSHUTDOWNENDINGS_H_

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

#include "TcpTeardownTestUtils.h"

/************************************************************************
 * HOW A TLS TASK'S TEARDOWN ENDS, BY HOW THE STREAM ENDED (owed-list row I2, change-set CS-6)
 *
 * WHAT IS UNDER TEST. The TLS stream policy of tasks/TcpSslBaseTasks.h ends a task which asked for
 * it (isCloseStreamOnTaskFinish) with a TLS shutdown, run as the task's finish continuation: our
 * close_notify, and then a wait for the peer's. Row I2 is that wait, when the peer's transport has
 * already ended with no close_notify - the read which saw the truncation consumed the socket's end of
 * stream, and on Linux nothing wakes the shutdown's own read until the 60 second protocol timer. The
 * decision (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md) is that such a
 * task skips that wait, and that every other ending stays exactly as it is.
 *
 * THESE CASES ARE THE "EXACTLY AS IT IS" - CHARACTERIZATION, committed before the fix and green on
 * the tree it was written against:
 *
 *   - TlsShutdown_ThePeersCloseNotifyEndsTheTaskPromptlyTests - the peer ends with its close_notify.
 *     The task's read sees a clean end of stream, and its shutdown sends our close_notify and is done
 *     at once, because the peer's has already been read.
 *   - TlsShutdown_OurCloseNotifyIsAnsweredPromptlyTests - the task closes first and the peer answers
 *     our close_notify with its own.
 *   - TlsShutdown_OurCloseNotifyWaitsForTheAnswerTests - the task closes first and the peer HOLDS its
 *     answer: the task is still running while it is held - the wait for the peer's close_notify is
 *     kept whenever no truncation was seen - and ends once the answer is sent.
 *
 * Each runs over both TLS policies, with and without a strand. The task is the library's connection
 * establisher, which connects and handshakes over the policy exactly as every TLS client in the
 * library does, and then does what a consumer does: reads until the stream ends and asks the policy
 * what the ending was, or closes straight away.
 *
 * THE PEER is a raw asio::ssl::stream server of this module's own, on an ephemeral loopback port -
 * not an HTTP peer. The client connects to "localhost", the name the test server certificate
 * carries. The peer keeps its socket open until the case releases it, so that nothing but the
 * ending chosen can reach the task.
 */

namespace utest
{
    namespace tlsending
    {
        using tcpteardown::WAIT_IN_MILLISECONDS;
        using tcpteardown::chkOrFail;
        using tcpteardown::describeCode;
        using tcpteardown::joinRecords;
        using tcpteardown::TlsEndingPeer;

        enum : std::size_t
        {
            /**
             * @brief The bound a teardown must end within to count as prompt - well under the
             * 60 second protocol timer, and far above what a prompt teardown takes
             */

            TEARDOWN_BOUND_IN_MILLISECONDS      = 10000U,
        };

        /**
         * @brief A connection establisher over a TLS policy which handshakes, and then either reads
         * until the stream ends or closes straight away - with its TLS shutdown run as its finish
         * continuation, as every TLS consumer in the library which closes its stream has it
         */

        template
        <
            typename STREAM
        >
        class TlsEndingProbeT :
            public bl::tasks::TcpConnectionEstablisherConnector< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( TlsEndingProbeT )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< STREAM >      base_type;
            typedef TlsEndingProbeT< STREAM >                                   this_type;

            enum class Mode
            {
                /**
                 * @brief Reads until the stream ends, asks the policy what the ending was, and ends
                 * the task clean on a close_notify or a truncation - what both HTTP drivers do
                 */

                ReadUntilTheEnding,

                /**
                 * @brief Ends the task as soon as the handshake is done, so that our close_notify
                 * is the first word of the ending
                 */

                CloseFirst,
            };

        protected:

            const Mode                                                          m_mode;

            mutable bl::os::mutex                                               m_probeLock;
            mutable bl::os::condition_variable                                  m_probeCv;

            bool                                                                m_hasStopped;
            bl::eh::error_code                                                  m_readEnding;

            char                                                                m_buffer[ 4096 ];

            TlsEndingProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const Mode                              mode
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_mode( mode ),
                m_hasStopped( false )
            {
                base_type::isCloseStreamOnTaskFinish( true );
            }

            void armRead()
            {
                base_type::getStream().async_read_some(
                    bl::asio::buffer( m_buffer, sizeof( m_buffer ) ),
                    bl::cpp::bind(
                        &this_type::onRead,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error,
                        bl::asio::placeholders::bytes_transferred
                        )
                    );
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                if( Mode::CloseFirst == m_mode )
                {
                    return false;
                }

                armRead();

                return true;
            }

            void onRead(
                SAA_in                  const bl::eh::error_code&               ec,
                SAA_in                  const std::size_t                       bytesTransferred
                ) NOEXCEPT
            {
                BL_UNUSED( bytesTransferred );

                /*
                 * THE ENDING IS ASKED OF THE POLICY, as the HTTP/1.1 driver asks it of every read
                 * that ends (Http1ConnectionTask.h, onReadCompleted( )) - before the prolog, and
                 * whatever the ending turns out to be
                 */

                const bool isTruncation = base_type::isStreamTruncationError( ec );

                BL_TASKS_HANDLER_BEGIN()

                if( ! ec )
                {
                    armRead();

                    return;
                }

                {
                    BL_MUTEX_GUARD( m_probeLock );

                    m_readEnding = ec;
                }

                if( bl::asio::error::eof != ec && ! isTruncation )
                {
                    BL_TASKS_HANDLER_CHK_EC( ec );
                }

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

        public:

            /**
             * @brief Blocks until the task has stopped or the bound expires
             *
             * onTaskStoppedNothrow( ) is reached only once the policy's finish continuation - the
             * TLS shutdown - has returned false, so a stopped task is a task whose teardown ended
             */

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                bl::os::mutex_unique_lock guard( m_probeLock );

                return m_probeCv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_hasStopped;
                    }
                    );
            }

            bool hasStopped() const
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_hasStopped;
            }

            auto readEnding() const -> bl::eh::error_code
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_readEnding;
            }
        };

        template
        <
            typename STREAM
        >
        using TlsEndingProbeImpl = bl::om::ObjectImpl< TlsEndingProbeT< STREAM > >;

        /**
         * @brief What one ending came to, read on the test thread
         */

        struct EndingResult
        {
            bool                                                                wasHeldWhileRunning;
            bool                                                                hasStoppedWithinBound;
            bool                                                                hasStopped;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            bl::eh::error_code                                                  readEnding;
            bool                                                                hasShutdownCompletedSuccessfully;
            bool                                                                wasShutdownInvoked;
            std::vector< std::string >                                          peerRecords;

            EndingResult()
                :
                wasHeldWhileRunning( false ),
                hasStoppedWithinBound( false ),
                hasStopped( false ),
                isFailed( false ),
                hasShutdownCompletedSuccessfully( false ),
                wasShutdownInvoked( false )
            {
            }

            bool hasPeerRecord( SAA_in const std::string& record ) const
            {
                for( const auto& recorded : peerRecords )
                {
                    if( recorded == record )
                    {
                        return true;
                    }
                }

                return false;
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "stopped within the bound " ) +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", stopped " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
                    ", the read ended " +
                    describeCode( readEnding ) +
                    ", shutdown invoked " +
                    ( wasShutdownInvoked ? "yes" : "no" ) +
                    ", shutdown completed successfully " +
                    ( hasShutdownCompletedSuccessfully ? "yes" : "no" ) +
                    ", peer " +
                    joinRecords( peerRecords );
            }
        };

        /**
         * @brief One connection over a TLS policy, ended the way 'ending' and 'mode' say
         *
         * With the peer's answer held, the case checks the task is still running once the peer has
         * seen our close_notify, and only then releases the answer. A task which does not stop
         * within the bound is cancelled, so that a regression fails the case instead of hanging it
         */

        template
        <
            typename STREAM
        >
        inline auto runEnding(
            SAA_in          const TlsEndingPeer::Ending                         ending,
            SAA_in          const typename TlsEndingProbeT< STREAM >::Mode      mode,
            SAA_in_opt      const bool                                          isAnswerHeld = false
            )
            -> EndingResult
        {
            using namespace bl;
            using namespace bl::tasks;

            EndingResult result;

            TlsEndingPeer peer( ending, isAnswerHeld );

            const auto probe = TlsEndingProbeImpl< STREAM >::createInstance(
                std::string( "localhost" ),
                peer.port(),
                mode
                );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    if( isAnswerHeld )
                    {
                        /*
                         * THE PEER HAS READ OUR close_notify, AND HOLDS ITS OWN. Nothing else can
                         * reach the task now - the peer sends nothing and keeps its socket open - so
                         * a task still running here is a task waiting for the peer's close_notify
                         */

                        if( peer.waitForRecord( "client-ended:" ) )
                        {
                            result.wasHeldWhileRunning = ! probe -> hasStopped();
                        }

                        peer.release();
                    }

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( TEARDOWN_BOUND_IN_MILLISECONDS ) );

                    if( ! result.hasStoppedWithinBound )
                    {
                        task -> requestCancel();
                    }

                    result.hasStopped =
                        probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    peer.release();

                    eq -> wait( task );
                }
                );

            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            result.readEnding = probe -> readEnding();
            result.hasShutdownCompletedSuccessfully = probe -> hasShutdownCompletedSuccessfully();
            result.wasShutdownInvoked = probe -> wasShutdownInvoked();

            ( void ) peer.waitForRecord( "script-ended" );

            result.peerRecords = peer.records();

            return result;
        }

        /**
         * @brief What every prompt, clean teardown asserts
         */

        inline void chkEndedPromptlyAndClean(
            SAA_in          const EndingResult&                                 result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.hasStoppedWithinBound,
                which + ": the teardown did not end within the bound; " + result.describe()
                );

            chkOrFail(
                ! result.isFailed,
                which + ": the task failed; " + result.describe()
                );

            chkOrFail(
                result.wasShutdownInvoked && result.hasShutdownCompletedSuccessfully,
                which + ": the TLS shutdown did not complete as a closure; " + result.describe()
                );
        }

    } // tlsending

} // utest

/**
 * @brief CHARACTERIZATION - the peer ends with its close_notify, and the task's teardown is prompt
 *
 * The task's read sees the peer's alert as a clean end of stream (eof), and its TLS shutdown sends
 * our close_notify and completes without reading, because the peer's is already in - the peer reads
 * ours in answer to its own
 */

UTF_AUTO_TEST_CASE( TlsShutdown_ThePeersCloseNotifyEndsTheTaskPromptlyTests )
{
    using namespace bl::tasks;
    using namespace utest::tlsending;

    const auto chk = []( SAA_in const EndingResult& result, SAA_in const std::string& which ) -> void
    {
        chkEndedPromptlyAndClean( result, which );

        chkOrFail(
            bl::asio::error::eof == result.readEnding,
            which + ": the read did not see a clean end of stream; " + result.describe()
            );

        chkOrFail(
            result.hasPeerRecord( "ended-with-close-notify:success" ),
            which + ": the peer did not read our close_notify in answer to its own; " + result.describe()
            );
    };

    chk(
        runEnding< TcpSslSocketAsyncBase >(
            TlsEndingPeer::Ending::CloseNotify,
            TlsEndingProbeT< TcpSslSocketAsyncBase >::Mode::ReadUntilTheEnding
            ),
        "TLS"
        );

    chk(
        runEnding< TcpSslSocketAsyncStrandedBase >(
            TlsEndingPeer::Ending::CloseNotify,
            TlsEndingProbeT< TcpSslSocketAsyncStrandedBase >::Mode::ReadUntilTheEnding
            ),
        "TLS, stranded"
        );
}

/**
 * @brief CHARACTERIZATION - the task closes first, and the peer's answer ends its teardown promptly
 */

UTF_AUTO_TEST_CASE( TlsShutdown_OurCloseNotifyIsAnsweredPromptlyTests )
{
    using namespace bl::tasks;
    using namespace utest::tlsending;

    const auto chk = []( SAA_in const EndingResult& result, SAA_in const std::string& which ) -> void
    {
        chkEndedPromptlyAndClean( result, which );

        chkOrFail(
            result.hasPeerRecord( "client-ended:" + describeCode( bl::asio::error::eof ) ) &&
                result.hasPeerRecord( "answered:success" ),
            which + ": the peer did not read our close_notify and answer it; " + result.describe()
            );
    };

    chk(
        runEnding< TcpSslSocketAsyncBase >(
            TlsEndingPeer::Ending::AwaitTheClient,
            TlsEndingProbeT< TcpSslSocketAsyncBase >::Mode::CloseFirst
            ),
        "TLS"
        );

    chk(
        runEnding< TcpSslSocketAsyncStrandedBase >(
            TlsEndingPeer::Ending::AwaitTheClient,
            TlsEndingProbeT< TcpSslSocketAsyncStrandedBase >::Mode::CloseFirst
            ),
        "TLS, stranded"
        );
}

/**
 * @brief CHARACTERIZATION - the task closes first and the peer holds its answer: the teardown waits
 * for the peer's close_notify, and ends once it is sent
 *
 * This is the wait I2's fix must keep. No read has seen a truncation here, so the task has no reason
 * not to wait - the 60 second protocol timer is what would bound it, far beyond this case's timeline
 */

UTF_AUTO_TEST_CASE( TlsShutdown_OurCloseNotifyWaitsForTheAnswerTests )
{
    using namespace bl::tasks;
    using namespace utest::tlsending;

    const auto chk = []( SAA_in const EndingResult& result, SAA_in const std::string& which ) -> void
    {
        chkOrFail(
            result.wasHeldWhileRunning,
            which + ": the task did not wait for the peer's close_notify; " + result.describe()
            );

        chkEndedPromptlyAndClean( result, which );

        chkOrFail(
            result.hasPeerRecord( "answered:success" ),
            which + ": the peer's held answer was not what ended the teardown; " + result.describe()
            );
    };

    chk(
        runEnding< TcpSslSocketAsyncBase >(
            TlsEndingPeer::Ending::AwaitTheClient,
            TlsEndingProbeT< TcpSslSocketAsyncBase >::Mode::CloseFirst,
            true /* isAnswerHeld */
            ),
        "TLS"
        );

    chk(
        runEnding< TcpSslSocketAsyncStrandedBase >(
            TlsEndingPeer::Ending::AwaitTheClient,
            TlsEndingProbeT< TcpSslSocketAsyncStrandedBase >::Mode::CloseFirst,
            true /* isAnswerHeld */
            ),
        "TLS, stranded"
        );
}

#endif /* __UTEST_TESTTLSSHUTDOWNENDINGS_H_ */
