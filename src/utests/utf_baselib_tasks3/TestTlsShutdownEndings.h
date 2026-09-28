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
 * AND I2'S RED, committed before its fix, beside one more truncated ending:
 *
 *   - TlsShutdown_ATruncationDoesNotWaitForTheCloseNotifyTests - the peer truncates: it shuts its
 *     transport's send side down with no close_notify, and then keeps its socket open and silent.
 *     Each probe's protocol timer is shortened to 3 s, so today the case sees the symptom I2 names:
 *     the timer cancels the task, and isCanceled( ) is true. After the fix the teardown sends our
 *     close_notify, does not wait for the peer's, and ends before the timer with isCanceled( )
 *     false. The probe that swallows the ending then ends clean, and the one that fails with the
 *     code keeps its truncation - both before and after, which is why isCanceled( ) is the assertion
 *     that makes both red.
 *   - TlsShutdown_ATruncationThenACloseEndsCleanTests - the peer truncates and closes its socket
 *     at once, so our close_notify draws a reset. On Linux this is a CHARACTERIZATION, green before
 *     and after the fix: once the peer's FIN has been received, a read reports end of stream even
 *     after a reset arrives - the error is only left pending (measured, logs/astra2/cs6/
 *     c2-reset-after-fin-probe.txt in the run's state directory) - so today's shutdown reads a
 *     truncation, which the policy counts as expected, and ends clean. Where the reset does reach
 *     the read - INFERRED possible on Windows, not measured - today's probe that swallows the ending
 *     fails with it, and the fix, which reads nothing, makes it clean. The probe that fails with the
 *     code keeps its truncation everywhere.
 *
 * IN BOTH TRUNCATED ENDINGS THE PEER ENDS THE STREAM ONLY ONCE THE PROBE'S READ IS ARMED - registered
 * with the reactor, since nothing else arrives. The FIN's own event then completes that read, and no
 * event is left to wake the shutdown's. A FIN which arrived first could have its event processed after
 * the probe's read had already consumed the end of stream, and asio's epoll reactor would then let the
 * shutdown's read complete at once (INFERRED, epoll_reactor.ipp, perform_io( )) - the red would be
 * probable rather than certain.
 *
 * Each runs over both TLS policies, with and without a strand. The task is the library's connection
 * establisher, which connects and handshakes over the policy exactly as every TLS client in the
 * library does, and then does what a consumer does: reads until the stream ends and asks the policy
 * what the ending was, or closes straight away.
 *
 * THE PEER is a raw asio::ssl::stream server - utests/baselib/TlsEndingPeer.h, shared - on an
 * ephemeral loopback port, not an HTTP peer. The client connects to "localhost", the name the test
 * server certificate carries. The peer keeps its socket open until the case releases it, so that
 * nothing but the ending chosen can reach the task.
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

                /**
                 * @brief Reads until the stream ends and fails the task with whatever code ended
                 * it, through the handler macros - what the block transfer tasks do. It does not
                 * ask the policy what the ending was: the macros tell the policy through
                 * isExpectedException( )
                 */

                FailOnTheEnding,
            };

        protected:

            const Mode                                                          m_mode;

            mutable bl::os::mutex                                               m_probeLock;
            mutable bl::os::condition_variable                                  m_probeCv;

            bool                                                                m_hasStopped;
            bool                                                                m_isReadArmed;
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
                m_hasStopped( false ),
                m_isReadArmed( false )
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

                /*
                 * The read is initiated inline, and with nothing to read it is left registered
                 * with the reactor by the time armRead( ) returns
                 */

                {
                    BL_MUTEX_GUARD( m_probeLock );

                    m_isReadArmed = true;

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

                /*
                 * THE ENDING IS ASKED OF THE POLICY, as the HTTP/1.1 driver asks it of every read
                 * that ends (Http1ConnectionTask.h, onReadCompleted( )) - before the prolog, and
                 * whatever the ending turns out to be. The probe which fails with the code does not
                 * ask: it reaches the policy only through the handler macros
                 */

                const bool isTruncation =
                    Mode::FailOnTheEnding != m_mode && base_type::isStreamTruncationError( ec );

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

                if( Mode::FailOnTheEnding == m_mode || ( bl::asio::error::eof != ec && ! isTruncation ) )
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

            /**
             * @brief Blocks until the probe has armed its first read or the bound expires
             */

            bool waitForReadArmed( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                bl::os::mutex_unique_lock guard( m_probeLock );

                return m_probeCv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isReadArmed;
                    }
                    );
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
            bool                                                                wasReadArmedBeforeTheEnding;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
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
                wasReadArmedBeforeTheEnding( true ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
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
                    std::string( "read armed before the ending " ) +
                    ( wasReadArmedBeforeTheEnding ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled when it stopped " +
                    ( isCanceled ? "yes" : "no" ) +
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
            SAA_in_opt      const bool                                          isAnswerHeld = false,
            SAA_in_opt      const bl::time::time_duration&                      protocolTimeout =
                                bl::time::neg_infin
            )
            -> EndingResult
        {
            using namespace bl;
            using namespace bl::tasks;

            EndingResult result;

            const bool isEndingHeld =
                TlsEndingPeer::Ending::Truncate == ending ||
                TlsEndingPeer::Ending::TruncateAndClose == ending;

            auto script = TlsEndingPeer::makeScript( ending, isAnswerHeld );

            script.isEndingHeld = isEndingHeld;

            TlsEndingPeer peer( script );

            const auto probe = TlsEndingProbeImpl< STREAM >::createInstance(
                std::string( "localhost" ),
                peer.port(),
                mode
                );

            if( ! protocolTimeout.is_special() )
            {
                probe -> setProtocolTimeout( protocolTimeout );
            }

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    if( isEndingHeld )
                    {
                        /*
                         * THE PEER ENDS THE STREAM ONLY ONCE THE PROBE'S READ IS ARMED - see the
                         * header's comment for why that is what makes the red certain
                         */

                        result.wasReadArmedBeforeTheEnding =
                            probe -> waitForReadArmed( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                        peer.releaseEnding();
                    }

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

                    /*
                     * READ BEFORE THE CASE'S OWN SAFETY CANCEL BELOW, so that a cancel it reports is
                     * one the task met on its own - the protocol timer's
                     */

                    result.isCanceled = probe -> isCanceled();

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

namespace utest
{
    namespace tlsending
    {
        /**
         * @brief Whether a code is the truncated TLS stream - asked of the stream policy's own predicate,
         * as AGENTS.md's networking rule asks, and not spelled here
         */

        inline bool isTruncationCode( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
        {
            return bl::tasks::TcpSslSocketAsyncBase::isExpectedSslErrorCode( ec );
        }

        /**
         * @brief One ending over both TLS policies and both kinds of consumer - every run is made
         * before any is asserted, so a failure message carries all four readings
         */

        struct FourEndings
        {
            EndingResult                                                        results[ 4 ];
            std::string                                                         names[ 4 ];
            std::string                                                         readings;
        };

        inline auto runFourEndings(
            SAA_in          const TlsEndingPeer::Ending                         ending,
            SAA_in          const bl::time::time_duration&                      protocolTimeout
            )
            -> FourEndings
        {
            using namespace bl::tasks;

            typedef TlsEndingProbeT< TcpSslSocketAsyncBase >                    plain_t;
            typedef TlsEndingProbeT< TcpSslSocketAsyncStrandedBase >            stranded_t;

            FourEndings four;

            four.results[ 0 ] = runEnding< TcpSslSocketAsyncBase >(
                ending, plain_t::Mode::ReadUntilTheEnding, false, protocolTimeout );
            four.names[ 0 ] = "TLS, the ending swallowed";

            four.results[ 1 ] = runEnding< TcpSslSocketAsyncBase >(
                ending, plain_t::Mode::FailOnTheEnding, false, protocolTimeout );
            four.names[ 1 ] = "TLS, failed with the ending";

            four.results[ 2 ] = runEnding< TcpSslSocketAsyncStrandedBase >(
                ending, stranded_t::Mode::ReadUntilTheEnding, false, protocolTimeout );
            four.names[ 2 ] = "TLS stranded, the ending swallowed";

            four.results[ 3 ] = runEnding< TcpSslSocketAsyncStrandedBase >(
                ending, stranded_t::Mode::FailOnTheEnding, false, protocolTimeout );
            four.names[ 3 ] = "TLS stranded, failed with the ending";

            for( std::size_t i = 0U; i < 4U; ++i )
            {
                four.readings += four.names[ i ] + ": " + four.results[ i ].describe() + "; ";
            }

            return four;
        }

        /**
         * @brief What every truncated ending asserts, before the fix and after: the teardown ended
         * inside the bound; the probe that swallowed the ending did not fail, and the one that
         * failed with it kept the truncation - both only once the fix is in, for the first - and
         * the TLS shutdown ran but did not complete as a closure, because the peer's close_notify
         * never came
         */

        inline void chkTruncatedEnding(
            SAA_in          const EndingResult&                                 result,
            SAA_in          const std::string&                                  which,
            SAA_in          const bool                                          isFailedWithTheCode,
            SAA_in          const std::string&                                  readings
            )
        {
            chkOrFail(
                result.wasReadArmedBeforeTheEnding,
                which + ": the probe did not arm its read before the peer's ending; all four: " + readings
                );

            chkOrFail(
                result.hasStoppedWithinBound,
                which + ": the teardown did not end within the bound; all four: " + readings
                );

            chkOrFail(
                ! result.isCanceled,
                which + ": the teardown ended only because it was cancelled; all four: " + readings
                );

            if( isFailedWithTheCode )
            {
                chkOrFail(
                    result.isFailed && isTruncationCode( result.taskCode ),
                    which + ": the task did not keep its truncation; all four: " + readings
                    );
            }
            else
            {
                chkOrFail(
                    ! result.isFailed,
                    which + ": the task failed; all four: " + readings
                    );
            }

            chkOrFail(
                result.wasShutdownInvoked && ! result.hasShutdownCompletedSuccessfully,
                which + ": the TLS shutdown did not run, or read as a completed closure; all four: " + readings
                );
        }

    } // tlsending

} // utest

/**
 * @brief I2'S RED - a truncation does not make the teardown wait for the peer's close_notify
 *
 * The peer truncates and then keeps its socket open and silent. With the protocol timer shortened to
 * 3 s, today's code waits for the peer's close_notify until the timer cancels the task; after the
 * fix it sends ours and ends at once. The peer reads our close_notify both times
 */

UTF_AUTO_TEST_CASE( TlsShutdown_ATruncationDoesNotWaitForTheCloseNotifyTests )
{
    using namespace utest::tlsending;

    const auto four = runFourEndings( TlsEndingPeer::Ending::Truncate, bl::time::seconds( 3 ) );

    for( std::size_t i = 0U; i < 4U; ++i )
    {
        chkTruncatedEnding( four.results[ i ], four.names[ i ], 1U == i % 2U, four.readings );

        chkOrFail(
            four.results[ i ].hasPeerRecord( "client-ended:" + describeCode( bl::asio::error::eof ) ),
            four.names[ i ] + ": the peer did not read our close_notify; all four: " + four.readings
            );
    }
}

/**
 * @brief A truncation followed by the peer's close ends the teardown clean, before the fix and after
 *
 * The peer truncates and closes its socket at once, and our close_notify draws a reset. On Linux the
 * shutdown's read reports the end of stream the FIN left behind rather than the reset, so today's
 * teardown ends clean as well; after the fix it reads nothing. Where a platform hands the read the
 * reset instead, this case is the red that platform shows today
 */

UTF_AUTO_TEST_CASE( TlsShutdown_ATruncationThenACloseEndsCleanTests )
{
    using namespace utest::tlsending;

    const auto four = runFourEndings( TlsEndingPeer::Ending::TruncateAndClose, bl::time::neg_infin );

    for( std::size_t i = 0U; i < 4U; ++i )
    {
        chkTruncatedEnding( four.results[ i ], four.names[ i ], 1U == i % 2U, four.readings );
    }
}

/**
 * @brief The truncation record I2 added to the stream wrapper - clear on a fresh stream, set by
 * recordTruncation( ), and cleared again by a new handshake
 *
 * The record is the wrapper's, so it travels with the stream and dies with it; a stream which begins
 * a handshake starts clean, as its handshake and shutdown state do (utf_baselib_http2's
 * AsioSslStreamWrapper_FreshStateTests). The handshake is begun on a socket which was never opened,
 * over an io_service which is never run: nothing is read, and all the case observes is the reset
 * beginProtocolHandshake( ) makes before it starts
 */

UTF_AUTO_TEST_CASE( TlsShutdown_TheTruncationRecordBelongsToOneHandshakeTests )
{
    using namespace bl;

    asio::io_service ioService;

    tasks::AsioSslStreamWrapper wrapper(
        ioService,
        std::string( "localhost" ),
        std::string( "443" ),
        nullptr /* sslServerContextPtr - the client role */
        );

    UTF_REQUIRE( ! wrapper.hasSeenTruncation() );

    wrapper.recordTruncation();

    UTF_REQUIRE( wrapper.hasSeenTruncation() );

    wrapper.beginProtocolHandshake(
        []( SAA_in const eh::error_code& ec ) -> void
        {
            BL_UNUSED( ec );
        }
        );

    UTF_REQUIRE( ! wrapper.hasSeenTruncation() );
}

#endif /* __UTEST_TESTTLSSHUTDOWNENDINGS_H_ */
