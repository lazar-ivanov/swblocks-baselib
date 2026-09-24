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

#ifndef __UTEST_TESTCLIENTSESSIONCANCEL_H_
#define __UTEST_TESTCLIENTSESSIONCANCEL_H_

#include <baselib/httpclient/ClientSession.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * H22 - a cancel which lands between two redirect hops
 *
 * WHAT WAS WRONG. SessionRequestTaskT::requestCancel( ) latches, and continuationTask( ) used to
 * read the latch before asking whether a next hop was due - returning nullptr without setting an
 * exception anywhere. The class derives from ForwarderTaskBaseT, which holds no state of its own,
 * so the logical request then reported the SUCCESSFUL 3xx hop's verdict: not failed, no
 * exception, status 302, redirectHops( ) accurate. That tuple is byte for byte the legitimate
 * "the policy did not follow this redirect" answer - redirects off, the hop limit, a cross-scheme
 * target, a BodySink - so a caller could not tell a chain its own cancel had truncated from a
 * chain the policy had ended on purpose
 *
 * THE BOUNDARY THESE TWO CASES PIN. The logical request is finished when the chain has decided it
 * has no more work to do. A cancel latched while a next hop was still due ends the chain as
 * operation_aborted; a cancel latched after the LAST hop lost the race and the answer stands.
 * Case 1 is the defect and case 2 is the other side of the same line - it is what stops a fix
 * which merely mirrors RetryableWrapperTaskT, failing the chain whenever the latch is set, from
 * replacing a complete 200 with operation_aborted for any caller which cancels on shutdown
 *
 * WHAT MAKES CASE 1 CERTAIN, AND IT IS NOT A SLEEP OR A RUN COUNT. The window is entered by
 * INTERCEPTING the decision point instead of racing it. The queue binds its ready observer to the
 * task it holds, so a test-local WrapperTaskBase pushed around the real session task has its own
 * continuationTask( ) called first - at which instant the hop has published its completion, the
 * session has not yet decided anything, and no lock of the session's is held. The real public
 * requestCancel( ) is called there. Same idiom as TestTcpPreHandshakeStage.h's ConnectOrderProbe
 *
 * WHAT THE PROBE DOES NOT REPRODUCE, stated rather than glossed. It makes the DECISION
 * deterministic, not the CONCURRENCY: a cancel arriving on another thread part-way through
 * absorbResponse( ) reaches the same latch and the same check, so it is the same case by
 * construction, but this does not execute that interleaving. No construction was found that would
 * - every in-window hook a black-box test can reach runs under the wrapper's own m_lock, where a
 * cancelling thread blocks with nothing observable in between
 *
 * WHICH ASSERTION IS THE CONTROL. In case 1, isFailed( ) and the three reads that follow from it.
 * Everything else there is green on both sides of the fix and must not be read as evidence: the
 * unfixed code also returns nullptr on the latch, so it also never asks the peer for /final, and
 * it also leaves redirectHops( ) at zero - the increment it would have made lives inside
 * chkPrepareNextHop( ), which it never reached. redirectHops( ) is asserted because the fix MOVED
 * that increment and it must still read zero for a hop which was decided and then abandoned
 */

namespace utest
{
    namespace sessioncancel
    {
        typedef bl::tasks::TcpSocketAsyncStrandedBase                           plain_stream_t;

        typedef bl::httpclient::ClientSessionImplT< plain_stream_t >            PlainSessionImpl;

        /**
         * @brief A wrapper around the real session task which cancels it between two named hops
         *
         * IT MUST STAY IN THE CHAIN, which is the one thing about this construction that is easy
         * to get wrong. base_type::continuationTask( ) would resolve to the FORWARDER's, which
         * returns whatever the session returns - the session task - and onReady( ) would then see
         * a continuation which is not the task it called, push the session as a fresh queue entry
         * and complete the probe. The second hop's ready callback would be bound to the session
         * and this override would never run again, so a case armed on hop 2 would be green for
         * the wrong reason. handleContinuationForward( ) is the spelling that keeps it in: it
         * swaps m_wrappedTask and returns THIS, which is onReady( )'s continuationIsSelf branch,
         * so the probe is re-scheduled and the next hop's ready callback is bound to it again.
         * That is also what SessionRequestTaskT and RetryableWrapperTaskT themselves do
         *
         * hopsObserved( ) is what says it stayed: it counts the calls this override actually
         * received, and case 2 asserts on it rather than trusting the construction
         */

        class RedirectCancelProbe : public bl::tasks::WrapperTaskBase
        {
            BL_DECLARE_OBJECT_IMPL( RedirectCancelProbe )

        protected:

            typedef RedirectCancelProbe                                         this_type;
            typedef bl::tasks::WrapperTaskBase                                  base_type;

            const bl::om::ObjPtr< bl::tasks::Task >                             m_session;
            const std::size_t                                                   m_cancelAfterHop;

            std::atomic< std::size_t >                                          m_hopsObserved;
            std::atomic< std::size_t >                                          m_cancelsIssued;

            RedirectCancelProbe(
                SAA_in          bl::om::ObjPtr< bl::tasks::Task >&&             session,
                SAA_in          const std::size_t                               cancelAfterHop
                )
                :
                m_session( BL_PARAM_FWD( session ) ),
                m_cancelAfterHop( cancelAfterHop ),
                m_hopsObserved( 0U ),
                m_cancelsIssued( 0U )
            {
                m_wrappedTask = bl::om::copy( m_session );
            }

        public:

            std::size_t hopsObserved() const NOEXCEPT
            {
                return m_hopsObserved.load();
            }

            std::size_t cancelsIssued() const NOEXCEPT
            {
                return m_cancelsIssued.load();
            }

            virtual auto continuationTask() -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
            {
                const auto hop = ++m_hopsObserved;

                if( hop == m_cancelAfterHop )
                {
                    /*
                     * THREE FACTS HOLD HERE BY CONSTRUCTION AND NOT BY TIMING. The hop has
                     * finished - this is only reached from onReady( ), which is only reached from
                     * the hop's own cbReady( ) after notifyReadyImpl( ) published its completion.
                     * The session has not decided anything yet - its continuationTask( ) is
                     * reached only through the forward on the next line. And no lock of the
                     * session's is held, so the real cancel runs to completion: it sets the latch
                     * and forwards to the hop, where applyStopped( )'s m_isCompleted guard will
                     * discard it - asynchronously, on the pool, since the forward only posts to
                     * the hop's mailbox
                     */

                    ++m_cancelsIssued;

                    m_session -> requestCancel();
                }

                return base_type::handleContinuationForward();
            }
        };

        typedef bl::om::ObjectImpl< RedirectCancelProbe >                       RedirectCancelProbeImpl;

        /**
         * @brief The peer of design 8.2, cleartext, answering /start with a 302 to /final
         *
         * finalRequests counts what actually reached it, which is how a case says the chain did
         * not continue - green on both sides of the fix, and asserted for the property and not as
         * a control
         */

        inline auto makeRedirectingPeer( SAA_inout std::atomic< std::size_t >& finalRequests )
            -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            auto peer = h2peer::Http2TestServer::createInstance<>( controlToken );

            peer -> setResponder(
                [ &finalRequests ]( SAA_in const h2peer::Http2TestRequest& request )
                    -> h2peer::Http2ResponseScript
                {
                    if( "/start" == request.path )
                    {
                        bl::http2::HpackFieldList fields;

                        fields.push_back(
                            bl::http2::HpackField( std::string( "location" ), std::string( "/final" ) )
                            );

                        return h2peer::Http2ResponseScript()
                            .headers( 302U, fields, true /* endStream */ );
                    }

                    ++finalRequests;

                    return h2peer::Http2ResponseScript()
                        .headers( 200U )
                        .data( request.path )
                        .endStream();
                }
                );

            return peer;
        }

        /**
         * @brief A session which speaks HTTP/2 over cleartext by prior knowledge (RFC 9113 3.3)
         */

        inline auto makeHttp2Session() -> bl::om::ObjPtr< PlainSessionImpl >
        {
            bl::httpclient::ClientSessionConfig config;

            config.connectionConfig = h2driver::cleartextHttp2Config();

            return PlainSessionImpl::createInstance( std::move( config ) );
        }

        inline auto makeRequest(
            SAA_in          const unsigned short                                port,
            SAA_in          const std::string&                                  target
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( std::string( "GET" ) );

            request.url(
                bl::net::Uri::parse(
                    "http://127.0.0.1:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        inline auto bodyOf( SAA_in const bl::httpclient::ClientResponse& response ) -> std::string
        {
            const auto& block = response.body();

            if( ! block )
            {
                return std::string();
            }

            return std::string(
                block -> begin() + block -> offset1(),
                block -> begin() + block -> size()
                );
        }

        inline auto messageOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> std::string
        {
            using namespace bl;

            if( ! task -> exception() )
            {
                return std::string( "<no exception>" );
            }

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( std::exception& e )
            {
                return std::string( e.what() );
            }

            return std::string( "<unknown exception>" );
        }

        /**
         * @brief Whether the chain ended as an operation_aborted marked expected
         *
         * BOTH PROPERTIES ARE READ, because each says something the other does not. The error code
         * is what a caller discriminates a cancel on, and the expected mark is the shape
         * applyStopped( ) produces for the same event - the one CmdLineAppBase and
         * BackendProcessingBase read
         */

        inline bool isExpectedOperationAborted( SAA_in const std::exception_ptr& eptr )
        {
            using namespace bl;

            if( ! eptr )
            {
                return false;
            }

            try
            {
                cpp::safeRethrowException( eptr );
            }
            catch( eh::exception& e )
            {
                const auto* const code = eh::get_error_info< eh::errinfo_error_code >( e );
                const auto* const expected = eh::get_error_info< eh::errinfo_is_expected >( e );

                return
                    nullptr != code &&
                    asio::error::operation_aborted == *code &&
                    nullptr != expected &&
                    *expected;
            }
            catch( std::exception& )
            {
            }

            return false;
        }

        /**
         * @brief Runs one session request inside a probe which cancels it after the named hop
         *
         * The queue keeps the probe so the case can read how the chain ended; wait( ) is on the
         * probe, which is the whole chain, because every continuation is the same queue entry
         */

        inline auto runCancelledAfterHop(
            SAA_in          const bl::om::ObjPtr< PlainSessionImpl >&           session,
            SAA_in          const bl::httpclient::ClientRequest&                request,
            SAA_in          const std::size_t                                   cancelAfterHop
            )
            -> bl::om::ObjPtr< RedirectCancelProbeImpl >
        {
            using namespace bl;
            using namespace bl::tasks;

            auto requestTask = session -> createRequestTask( request );

            auto probe =
                RedirectCancelProbeImpl::createInstance(
                    om::qi< Task >( requestTask ),
                    cancelAfterHop
                    );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );

            return probe;
        }

    } // sessioncancel

} // utest

/**
 * @brief A cancel latched while the next hop was still due ends the chain as cancelled - H22
 *
 * THE FIRST ASSERTION IS THE NEGATIVE CONTROL. Against the unfixed continuationTask( ) it fails,
 * and it fails for the stated reason rather than by hanging or timing out: isFailed( ) is false
 * because the latch returned nullptr before anything was set and the forwarder handed back the
 * 302 hop's own verdict
 */

UTF_AUTO_TEST_CASE( ClientSession_CancelBetweenHopsIsReportedAsCancelledTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::sessioncancel;

    std::atomic< std::size_t > finalRequests( 0U );

    const auto peer = makeRedirectingPeer( finalRequests );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeHttp2Session();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::sessioncancel::ClientSession_CancelBetweenHopsIsReportedAsCancelledTests"
                );

            session -> redirectPolicy().isEnabled( true );

            const auto probe =
                runCancelledAfterHop(
                    session,
                    makeRequest( port, "/start" ),
                    1U /* cancelAfterHop */
                    );

            const auto task = om::qi< Task >( probe );

            UTF_REQUIRE( task -> isFailed() );

            UTF_REQUIRE(
                messageOf( task ).find( "was cancelled" ) != std::string::npos
                );

            UTF_REQUIRE( isExpectedOperationAborted( task -> exception() ) );

            const auto requestTask = probe -> getWrappedTaskT< httpclient::ClientRequestTask >();

            /*
             * The 3xx stays readable, deliberately - with isFailed( ) true and an explicit
             * operation_aborted the answer is no longer ambiguous, and a failed chain already
             * leaves the failing hop's partial response readable
             */

            UTF_REQUIRE_EQUAL( requestTask -> response().status(), 302U );

            /*
             * Zero and not one: the redirect was decided and then abandoned, and the fix moved
             * the increment out of chkPrepareNextHop( ) so that it stays zero here
             */

            UTF_REQUIRE_EQUAL( requestTask -> redirectHops(), 0U );

            /*
             * The chain stopped: one hop was observed, one cancel was issued, and /final was
             * never asked for. All three are green on both sides of the fix
             */

            UTF_REQUIRE_EQUAL( probe -> hopsObserved(), 1U );
            UTF_REQUIRE_EQUAL( probe -> cancelsIssued(), 1U );
            UTF_REQUIRE_EQUAL( finalRequests.load(), 0U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A cancel latched after the FINAL hop is a no-op - the other side of the same boundary
 *
 * GREEN BEFORE AND AFTER, and that is what it is for. It is the guard against a fix which fails
 * the chain whenever the latch is set: that shape would replace the complete 200 this case asks
 * for with operation_aborted, and the caller it would do it to is any caller whose shutdown
 * cancels a request that had already finished
 *
 * hopsObserved( ) == 2 is a claim about the CONSTRUCTION and not about the session: it says the
 * probe was still in the chain when the second hop completed, which is the thing that would
 * silently make this case guard nothing if handleContinuationForward( ) were spelled as the
 * forwarder's continuationTask( )
 */

UTF_AUTO_TEST_CASE( ClientSession_CancelAfterTheFinalHopIsANoOpTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::sessioncancel;

    std::atomic< std::size_t > finalRequests( 0U );

    const auto peer = makeRedirectingPeer( finalRequests );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeHttp2Session();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::sessioncancel::ClientSession_CancelAfterTheFinalHopIsANoOpTests"
                );

            session -> redirectPolicy().isEnabled( true );

            const auto probe =
                runCancelledAfterHop(
                    session,
                    makeRequest( port, "/start" ),
                    2U /* cancelAfterHop */
                    );

            const auto task = om::qi< Task >( probe );

            UTF_REQUIRE_EQUAL( probe -> hopsObserved(), 2U );
            UTF_REQUIRE_EQUAL( probe -> cancelsIssued(), 1U );

            if( task -> isFailed() )
            {
                UTF_FAIL( "the cancelled chain should have kept its answer: " + messageOf( task ) );
            }

            const auto requestTask = probe -> getWrappedTaskT< httpclient::ClientRequestTask >();

            UTF_REQUIRE_EQUAL( requestTask -> response().status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( requestTask -> response() ), std::string( "/final" ) );
            UTF_REQUIRE_EQUAL( requestTask -> redirectHops(), 1U );

            UTF_REQUIRE_EQUAL( finalRequests.load(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

#endif /* __UTEST_TESTCLIENTSESSIONCANCEL_H_ */
