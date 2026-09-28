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

#ifndef __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_
#define __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * What reaches a request after it has FAILED - D3's guard, applied to its siblings ( owed-list
 * row I8 of notes/plans/issues/astra-remediation-owed-work.md )
 *
 * A timeout or a cancel fails the request in the drain's apply phase, and the connection learns of
 * it only afterwards, from the cancel( ) the deferred phase makes. Whatever the connection had
 * already sent up is still in the mailbox then, and whatever it sends before it sees the cancel
 * follows. D3 made a late Data block change nothing; this is the same question for the header
 * blocks and the trailers, which used to write the response the caller holds - after its task had
 * completed and while the caller could be reading it - and re-arm the stream idle timer.
 *
 * THE FAILURE IS PINNED BEFORE ANYTHING LATE IS DELIVERED, with the same rendezvous D3's case uses:
 * the probe's cancel record is made in the deferred phase of the batch which failed the request, and
 * runTask( ) returns only once the task has completed. The close delivered last is the second
 * rendezvous - its release is applied behind everything delivered ahead of it.
 *
 * AND WHAT THE SESSION DOES WITH A REQUEST WHICH FAILED ON A BODY CAP ( owed-list row I5 ): it does
 * not replay it, whatever connection loss lands in the same batch - the refusal D4 made for a sink
 * which threw. The batch is arranged the way D4's session case arranges it, with its gated probe.
 *
 * The probes are TestHttpClientRequestTask.h's, the gated connection
 * TestHttpClientRequestTaskSinkAccounting.h's and the counting sink
 * TestHttpClientRequestTaskOutstandingCap.h's - all of which this module's Main.cpp includes ahead
 * of this file.
 */

namespace utest
{
    namespace afterthefailure
    {
        /**
         * @brief How a request is made to fail before the late events reach it
         */

        enum class FailBy
        {
            Cancel,
            HeadersTimeout,
        };

        /**
         * @brief Fails one request, then delivers an interim block, a final block and trailers,
         * then the close - and checks that none of it reached the response the caller holds
         */

        inline void requireLateBlocksChangeNothing( SAA_in const FailBy failBy )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;

            const bool isTimeout = FailBy::HeadersTimeout == failBy;

            const auto connection = ProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                false /* isSubmitRefused */
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            /*
             * THE IDLE TIMEOUT IS ON, so that a late block which re-armed the idle timer would
             * really arm one; it is long enough that it never fires inside the case
             */

            HttpClientRequestConfig config;

            config.streamIdleTimeout = time::seconds( 60 );

            if( isTimeout )
            {
                config.responseHeadersTimeout = time::milliseconds( 250 );
            }

            const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
                makeRequest(),
                makeKey(),
                om::qi< ConnectionPool >( pool ),
                config
                );

            const auto task = om::qi< tasks::Task >( taskImpl );

            runTask(
                task,
                [ & ]() -> void
                {
                    connection -> waitFor( "submit" );

                    if( ! isTimeout )
                    {
                        task -> requestCancel();
                    }

                    connection -> waitFor( "cancel:42" );
                }
                );

            requireTrue( task -> isFailed(), "the request should have failed before the late blocks" );

            const std::string expected = isTimeout ? "has timed out" : "was cancelled";

            requireTrue(
                std::string::npos != messageOf( task ).find( expected ),
                "the request should have failed with '" + expected + "', and it reports: " +
                    messageOf( task )
                );

            http::HeaderList hints;

            hints.append( "link", "</style.css>; rel=preload" );

            connection -> deliverHeaders( 103U, std::move( hints ), true /* isInterim */ );

            http::HeaderList headers;

            headers.append( "x-late", "headers" );

            connection -> deliverHeaders( 200U, std::move( headers ), false /* isInterim */ );

            http::HeaderList trailers;

            trailers.append( "x-late-trailer", "trailers" );

            connection -> deliverTrailers( std::move( trailers ) );

            connection -> deliverClosed(
                eh::errc::make_error_code( eh::errc::operation_canceled ),
                false /* isRetryable */
                );

            requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

            const auto& response = taskImpl -> response();

            UTF_CHECK_EQUAL( response.status(), 0U );
            UTF_CHECK( ! response.headers().has( "x-late" ) );
            UTF_CHECK( ! response.trailers().has( "x-late-trailer" ) );
            UTF_CHECK_EQUAL( taskImpl -> interimResponses().size(), 0U );

            UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
            UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:failed" ) );
        }

        /**
         * @brief Which of the two body caps a request is made to overflow
         */

        enum class Cap
        {
            Buffered,
            Outstanding,
        };

        /**
         * @brief Runs one GET through a session with retryIdempotentOnConnectionLoss on, and has its
         * first stream overflow a body cap and lose its connection in ONE batch
         *
         * The batch is [ headers, a ten byte block, a connection loss published as Draining ], all
         * delivered while the gated probe holds the drain inside its first submit( ): the block
         * overflows the cap, and the close behind it reads ConnectionUnusable - which is what feeds
         * the knob. Nothing else about the request forbids a replay: a GET, no body, and a sink
         * which is offered nothing, since the block which crosses a cap is never offered.
         *
         * A REPLAY WOULD BE ANSWERED ON ITS OWN - the gated probe answers a second submit( ) from a
         * pool thread with the same ten bytes, which overflow the cap again on a clean close - so
         * the chain completes either way, and the number of submits is the answer
         */

        inline void requireAnOverflowIsNotReplayed( SAA_in const Cap cap )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;
            using namespace utest::sinkaccounting;

            const auto connection = GatedProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                std::string( "0123456789" )
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            SessionRequestPlan plan;

            plan.pool = om::ObjPtrCopyable< ConnectionPool >( om::qi< ConnectionPool >( pool ) );
            plan.state = om::ObjPtrCopyable< SessionState >(
                SessionStateImpl::createInstance< SessionState >()
                );
            plan.transportScheme = "https";

            plan.policy.retryIdempotentOnConnectionLoss = true;

            om::ObjPtrCopyable< BodySink > sink;

            if( Cap::Buffered == cap )
            {
                plan.requestConfig.maxResponseBodySize = 4U;
            }
            else
            {
                /*
                 * One byte: a block of any size, with its allowance, is over it at post( )
                 */

                plan.requestConfig.maxOutstandingResponseBodySize = 1U;

                sink = om::ObjPtrCopyable< BodySink >(
                    om::qi< BodySink >(
                        utest::outstandingcap::ParkingSink::createInstance( false /* isParking */ )
                        )
                    );
            }

            const auto chain = om::qi< tasks::Task >(
                SessionRequestTaskImpl::createInstance( std::move( plan ), makeRequest(), sink )
                );

            runTask(
                chain,
                [ & ]() -> void
                {
                    connection -> waitFor( "submit" );

                    connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

                    connection -> deliverData( "0123456789" );

                    connection -> publishState( ConnectionState::Draining );

                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_reset ),
                        false /* isRetryable */
                        );

                    connection -> openTheGate();
                }
                );

            /*
             * ONE NETWORK ATTEMPT, and one slot given back - checked and not required. The submit
             * count is the discriminator: a replay's submit( ) precedes its hop's end, which the
             * chain waits for. The slot count is exact only without a replay, since a replay's
             * close may be applied after its hop has already failed on the cap
             */

            UTF_CHECK_EQUAL( connection -> submits(), 1U );
            UTF_CHECK_EQUAL( pool -> releases().size(), 1U );

            requireTrue( chain -> isFailed(), "a request which overflowed a body cap should have failed" );

            requireTrue(
                std::string::npos != messageOf( chain ).find( "exceeded the maximum of" ),
                "the chain should have failed on the body cap, and it reports: " + messageOf( chain )
                );
        }

    } // afterthefailure

} // utest

/**
 * @brief Header blocks and trailers which arrive after a cancel or a timeout change nothing
 *
 * The same guard D3 put at the top of applyData( ), at the top of applyHeaders( ) and of the
 * Trailers arm: a request whose completion is decided takes nothing more into its response and arms
 * no timer. Run twice, once for each way a request fails with its stream still open - a cancel and
 * a response-headers timeout.
 *
 * RED BEFORE: the late 200 became the failed request's status, its field and the trailer's field
 * were written into the response, and the 103 was filed as an interim - all after the caller's task
 * had completed. GREEN AFTER: the response is as the failure left it
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_HeadersAndTrailersAfterTheFailureChangeNothingTests )
{
    using namespace utest::afterthefailure;

    requireLateBlocksChangeNothing( FailBy::Cancel );
    requireLateBlocksChangeNothing( FailBy::HeadersTimeout );
}

/**
 * @brief An upload pull which arrives after a cancel is not answered - the caller's BodySource is
 * not read again
 *
 * The third sibling of applyData( )'s guard, found while folding in I8. applyBodyWanted( ) asked
 * only whether the stream had closed, so a pull the connection sent before it saw the cancel read
 * the caller's source and handed the bytes to a stream this task had already reset - after the
 * caller's task had completed. Every failure which leaves the stream open resets it first, so no
 * answer is owed to the connection once the completion is decided.
 *
 * StubBodySource is TestClientContracts.h's, which this module's Main.cpp includes ahead of this
 * file. RED BEFORE: the late pull read four bytes from the source and provided them. GREEN AFTER:
 * nothing read and nothing provided
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_AnUploadPullAfterTheFailureIsNotAnsweredTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto source = utest::clientcontracts::StubBodySource::createInstance(
        std::string( "12345678" ),
        true /* canRewind */,
        4U /* chunkSize */
        );

    auto request = makeRequest( "POST" );

    request.bodySource(
        om::ObjPtrCopyable< BodySource >( om::qi< BodySource >( source ) )
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        std::move( request ),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            task -> requestCancel();

            connection -> waitFor( "cancel:42" );
        }
        );

    requireTrue( task -> isFailed(), "the request should have failed before the late pull" );

    connection -> deliverBodyWanted( 4096U );

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    /*
     * The close's release is applied behind the pull, and a pull's answer is a deferred action of
     * the batch which applied it - so once the release is in, an answer would have been made
     */

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    UTF_CHECK_EQUAL( connection -> uploaded(), std::string() );
    UTF_CHECK( ! connection -> has( "body:4:more" ) );
}

/**
 * @brief I5 - a request which failed on a body cap is not replayed, whatever connection loss lands
 * in the same batch
 *
 * Run once for each cap: the buffered maxResponseBodySize, and D3's maxOutstandingResponseBodySize,
 * whose overflow marker is made at post( ). Each time the overflow is the request's verdict - it is
 * applied first - and the close behind it only reports the connection gone.
 *
 * RED BEFORE: the session read ConnectionUnusable off the hop and, with the knob on and a GET, made
 * a second attempt, which overflowed again - two submits and two slots. GREEN AFTER: one of each, and
 * the chain failed on the cap
 */

UTF_AUTO_TEST_CASE( ClientSession_AnOverflowedRequestIsNotReplayedTests )
{
    using namespace utest::afterthefailure;

    requireAnOverflowIsNotReplayed( Cap::Buffered );
    requireAnOverflowIsNotReplayed( Cap::Outstanding );
}

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_ */
