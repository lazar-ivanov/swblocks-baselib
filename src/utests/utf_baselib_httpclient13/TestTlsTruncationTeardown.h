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

#ifndef __UTEST_TESTTLSTRUNCATIONTEARDOWN_H_
#define __UTEST_TESTTLSTRUNCATIONTEARDOWN_H_

#include <baselib/http/SimpleHttpSslTask.h>

#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * I2 ON THE REAL CONSUMERS - a truncated TLS stream's teardown must not wait for a close_notify
 * (owed-list row I2, change-set CS-6)
 *
 * WHAT IS UNDER TEST. After a peer ends a TLS stream with no close_notify, a TLS task's teardown
 * sends our close_notify and then waits for the peer's. The read which saw the truncation consumed
 * the socket's end of stream, so on Linux nothing wakes that wait but the protocol timer, which ends
 * the task as a cancel (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md, 1).
 * utf_baselib_tasks3 shows it on probes over the TLS policies; these cases show it on two of the
 * consumers the note names: an idle HTTP/1.1 driver, and SimpleHttpTask's complete response ended by
 * a truncation. The third, the HTTP/2 driver, is utf_baselib_h2client9's.
 *
 * I2'S REDS, committed before its fix. Each consumer's protocol timer is shortened to 3 s, so on
 * today's code each case observes the symptom: the timer cancels the task, and isCanceled( ) is
 * true. After the fix the teardown sends our close_notify, does not wait for the peer's, and the task
 * ends before the timer, uncancelled and clean.
 *
 *   - Http1DriverTls_ATruncatedIdleConnectionEndsWithoutWaitingTests - the driver reads the
 *     truncation on an idle connection and closes. The peer ends the stream only once the driver's
 *     first read is armed - registered with the reactor, since nothing else arrives - so the FIN's own
 *     event is what completes that read and no event is left to wake the shutdown's. A FIN which
 *     arrived first could have its event processed after the read had consumed the end of stream,
 *     and asio's epoll reactor would then let the shutdown's read complete at once
 *     (utf_baselib_tasks3's TestTlsShutdownEndings.h says the same of its probes).
 *   - SimpleHttpTls_ACompleteResponseEndedByATruncationSucceedsTests - a GET answered with a
 *     complete Content-Length body and then a truncation, which SimpleHttpTask counts as a success.
 *     It goes green only with the line of the note's 9 which makes SimpleHttpTask ask the policy's
 *     own predicate - the core fix alone leaves it waiting. ITS RED IS PROBABLE, NOT CERTAIN, and
 *     measured so: SimpleHttpTask offers nothing to wait for once it has armed its content read, so
 *     the peer truncates straight after its response, and on today's code the case ended at once in
 *     one run of the first twenty-one - the late event above. Its green is certain: with the line
 *     in, the shutdown reads nothing at all.
 *   - SimpleHttpTls_ATruncationIsRecordedBeforeTheShutdownTests - the certain red for that line,
 *     committed after the core fix: the same exchange, asserting that the stream has recorded the
 *     truncation when the shutdown begins. The core fix alone leaves it red, and the line makes it
 *     green, whatever the reactor does.
 *
 * WHY THE HTTP/1.1 DRIVER IS HANDED ITS STREAM BY A PLAIN CONNECTOR. The connection is established
 * by TcpConnectionEstablisherConnector over the stranded TLS policy - resolve, connect, handshake -
 * and the connected stream is detached and handed to the driver, which is what a session's
 * establisher (ClientConnectionTaskBaseT) does too, underneath. The establisher adds the ALPN offer,
 * the negotiated-parameter floor check, a connect deadline and a driver factory, which cost this
 * module 2.4 MB (39.1 against 36.7 MB, a64 clang debug) and all run before the driver exists. From
 * the driver's construction on, the path under test is a session's: the same driver type over a
 * stream the same policy connected, attached with no strand of the driver's own. What differs is
 * what the driver is given: HTTP/1.1 without an ALPN identifier - what a session's connection to a
 * server which selects none is given too - the default response limits, and no idle lifetime, where
 * a session passes its configured limits and its pool policy's idle lifetime (ClientSession.h,
 * makeDriverFactory( )). The limits bound what a response may be, and the idle lifetime arms a timer
 * which this teardown cancels in initiateClose( ); neither is on the path under test.
 *
 * THE PEER is utests/baselib/TlsEndingPeer.h: it truncates by shutting its transport's send side down
 * under the TLS session, then reads until the client ends the stream, and keeps its socket open and
 * silent until the case releases it - so nothing but the fix can end the teardown early.
 */

namespace utest
{
    namespace truncteardown
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::TlsEndingPeer;
        using tlsteardown::chkEndedWithoutWaiting;
        using tlsteardown::chkOrFail;
        using tlsteardown::makeTlsKey;
        using tlsteardown::runToTheEnd;
        using tlsteardown::shortProtocolTimeout;

        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        /**
         * @brief The HTTP/1.1 driver over the stranded TLS policy, with its stop signalled - and
         * the moment its first read is armed
         */

        class Http1DriverProbe : public bl::tasks::Http1ConnectionTaskT< tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( Http1DriverProbe )

        public:

            typedef bl::tasks::Http1ConnectionTaskT< tls_stream_t >             base_type;

        protected:

            OneShotSignal                                                       m_stop;
            OneShotSignal                                                       m_readArmed;

            Http1DriverProbe(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_inout       tls_stream_t::stream_ref&&                      connectedStream,
                SAA_in          bl::httpclient::ConnectionKey                   key
                )
                :
                base_type(
                    BL_PARAM_FWD( negotiated ),
                    BL_PARAM_FWD( connectedStream ),
                    BL_PARAM_FWD( key )
                    )
            {
            }

            /**
             * @brief The base posts the start handler, which arms the first read inline; a marker
             * posted behind it to the same strand runs only after that handler has returned
             */

            virtual void scheduleTask( SAA_in const std::shared_ptr< bl::tasks::ExecutionQueue >& eq ) OVERRIDE
            {
                base_type::scheduleTask( eq );

                const auto ref = base_type::selfRef();

                base_type::postToStreamExecutor(
                    [ this, ref ]() -> void
                    {
                        BL_NOEXCEPT_BEGIN()

                        m_readArmed.signal();

                        BL_NOEXCEPT_END()
                    }
                    );
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stop.signal();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            bool waitForReadArmed( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_readArmed.waitFor( timeoutInMilliseconds );
            }
        };

        typedef bl::om::ObjectImpl< Http1DriverProbe >                          Http1DriverProbeImpl;

        /**
         * @brief SimpleHttpTask's GET over TLS, with its stop signalled - and whether its stream had
         * recorded a truncation when its finish continuation, which begins the TLS shutdown, was
         * first entered
         */

        class SimpleHttpsGetProbe : public bl::tasks::SimpleHttpSslGetTaskT<>
        {
            BL_DECLARE_OBJECT_IMPL( SimpleHttpsGetProbe )

        public:

            typedef bl::tasks::SimpleHttpSslGetTaskT<>                          base_type;

        protected:

            OneShotSignal                                                       m_stop;

            mutable bl::os::mutex                                               m_sampleLock;
            bool                                                                m_isContinuationEntered;
            bool                                                                m_wasTruncationRecorded;

            SimpleHttpsGetProbe(
                SAA_in          std::string&&                                   host,
                SAA_in          const unsigned short                            port
                )
                :
                base_type( BL_PARAM_FWD( host ), port, std::string( "/truncated" ), std::string() /* content */ ),
                m_isContinuationEntered( false ),
                m_wasTruncationRecorded( false )
            {
            }

            /**
             * @brief Entered under the task lock once the response is complete, before the TLS
             * shutdown begins; entered again once it has ended, which is not sampled
             */

            virtual bool scheduleTaskFinishContinuation( SAA_in_opt const std::exception_ptr& eptrIn = nullptr ) OVERRIDE
            {
                {
                    BL_MUTEX_GUARD( m_sampleLock );

                    if( ! m_isContinuationEntered )
                    {
                        m_isContinuationEntered = true;

                        m_wasTruncationRecorded =
                            base_type::m_sslStream && base_type::m_sslStream -> hasSeenTruncation();
                    }
                }

                return base_type::scheduleTaskFinishContinuation( eptrIn );
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stop.signal();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            bool isContinuationEntered() const
            {
                BL_MUTEX_GUARD( m_sampleLock );

                return m_isContinuationEntered;
            }

            bool wasTruncationRecordedAtShutdown() const
            {
                BL_MUTEX_GUARD( m_sampleLock );

                return m_wasTruncationRecorded;
            }
        };

        typedef bl::om::ObjectImpl< SimpleHttpsGetProbe >                       SimpleHttpsGetProbeImpl;

        /**
         * @brief A peer which answers one GET with a complete Content-Length response and then
         * truncates - SimpleHttpTask's one success path after a truncation
         */

        inline auto makeTruncatedResponseScript() -> TlsEndingPeer::Script
        {
            TlsEndingPeer::Script script( TlsEndingPeer::Ending::Truncate );

            script.response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";

            return script;
        }

        inline void chkCompleteResponse(
            SAA_in          const bl::om::ObjPtr< SimpleHttpsGetProbeImpl >&    task,
            SAA_in          const std::string&                                  readings
            )
        {
            chkOrFail(
                200U == task -> getHttpStatus() && "hello" == task -> getResponse(),
                "SimpleHttpTask did not deliver the complete response: status " +
                    bl::utils::lexical_cast< std::string >( task -> getHttpStatus() ) +
                    ", body '" +
                    task -> getResponse() +
                    "'; " +
                    readings
                );
        }

        /**
         * @brief Connects and handshakes over the stranded TLS policy with the plain connection
         * establisher, and hands the connected stream to an HTTP/1.1 driver which is not yet running
         * - see the header's comment for why not a session's establisher, and what that changes
         */

        inline auto establishHttp1Driver( SAA_in const bl::os::port_t port ) -> bl::om::ObjPtr< Http1DriverProbeImpl >
        {
            using namespace bl;
            using namespace bl::tasks;

            typedef om::ObjectImpl< TcpConnectionEstablisherConnector< tls_stream_t > > connector_t;

            const auto connector = connector_t::createInstance(
                std::string( "localhost" ),
                port,
                false /* logExceptions */
                );

            const auto connectorTask = om::qi< Task >( connector );

            scheduleAndExecuteInParallel(
                [ &connectorTask ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( connectorTask );

                    eq -> wait( connectorTask );
                }
                );

            chkOrFail(
                ! connectorTask -> isFailed(),
                "the TLS connection to the peer could not be established"
                );

            return Http1DriverProbeImpl::createInstance(
                httpclient::NegotiatedProtocol::withoutAlpn( httpclient::HttpProtocol::Http11 ),
                connector -> detachStream(),
                makeTlsKey( port )
                );
        }

    } // truncteardown

} // utest

/**
 * @brief I2'S RED - an idle HTTP/1.1 driver whose peer truncates ends without waiting
 *
 * The peer's ending is held until the driver's first read is armed - see the header's comment
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_ATruncatedIdleConnectionEndsWithoutWaitingTests )
{
    using namespace bl;
    using namespace utest::truncteardown;

    TlsEndingPeer::Script script( TlsEndingPeer::Ending::Truncate );

    script.isEndingHeld = true;

    TlsEndingPeer peer( script );

    const auto driver = establishHttp1Driver( peer.port() );

    driver -> setProtocolTimeout( shortProtocolTimeout() );

    const auto releaseOnceArmed = [ &driver, &peer ]() -> bool
    {
        const bool isArmed = driver -> waitForReadArmed( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

        peer.releaseEnding();

        return isArmed;
    };

    chkEndedWithoutWaiting( runToTheEnd( driver, peer, releaseOnceArmed ), "the HTTP/1.1 driver" );
}

/**
 * @brief I2'S RED, on SimpleHttpTask's one success path after a truncation - probable before the
 * fix, see the header's comment, and certain after it
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_ACompleteResponseEndedByATruncationSucceedsTests )
{
    using namespace bl;
    using namespace utest::truncteardown;

    TlsEndingPeer peer( makeTruncatedResponseScript() );

    const auto task = SimpleHttpsGetProbeImpl::createInstance( std::string( "localhost" ), peer.port() );

    task -> setProtocolTimeout( shortProtocolTimeout() );

    const auto result = runToTheEnd( task, peer );

    chkEndedWithoutWaiting( result, "SimpleHttpTask" );

    chkCompleteResponse( task, result.describe() );
}

/**
 * @brief I2'S CERTAIN RED FOR THE SimpleHttpTask.h LINE - when SimpleHttpTask's shutdown begins after
 * a complete response ended by a truncation, its stream has recorded the truncation
 *
 * Committed after the core fix and before the line. With the core fix alone it is red: the success
 * path asks the static isExpectedProtocolException( ), which records nothing, and nothing else on that
 * path classifies the ending - so the shutdown still waits for the peer's close_notify. With the line
 * it is green: the success path asks the policy's own predicate, which records. Unlike the case
 * above it does not depend on the reactor: it reads the record when the finish continuation is
 * entered, which is before the shutdown reads anything
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_ATruncationIsRecordedBeforeTheShutdownTests )
{
    using namespace bl;
    using namespace utest::truncteardown;

    TlsEndingPeer peer( makeTruncatedResponseScript() );

    const auto task = SimpleHttpsGetProbeImpl::createInstance( std::string( "localhost" ), peer.port() );

    task -> setProtocolTimeout( shortProtocolTimeout() );

    const auto result = runToTheEnd( task, peer );

    chkCompleteResponse( task, result.describe() );

    chkOrFail(
        task -> isContinuationEntered() && task -> wasTruncationRecordedAtShutdown(),
        std::string( "SimpleHttpTask's stream had not recorded the truncation when its shutdown began" ) +
            " (finish continuation entered " +
            ( task -> isContinuationEntered() ? "yes" : "no" ) +
            "); " +
            result.describe()
        );
}

#endif /* __UTEST_TESTTLSTRUNCATIONTEARDOWN_H_ */
