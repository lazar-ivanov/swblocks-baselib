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

#include <baselib/httpclient/HttpClientRequestTask.h>
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
 * The probes are TestHttpClientRequestTask.h's, which this module's Main.cpp includes ahead of this
 * file.
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

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_ */
