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

#ifndef __UTEST_TESTCLIENTSESSIONTLSSINKFALLBACK_H_
#define __UTEST_TESTCLIENTSESSIONTLSSINKFALLBACK_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http1DriverTlsTestUtils.h>
#include <utests/baselib/HttpClientSessionTestUtils.h>
#include <utests/baselib/HttpClientSessionTlsTestUtils.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The TLS ALPN fallback exchange WITH A SINK - E3 of astra's second review's change-set CS-2
 *
 * WHAT IS OWED AND WHY IT IS HERE. The fallback is the one path on which a request is bounced: the
 * pool dispatches the first request of a key onto the connection still establishing, so that its
 * headers can ride an HTTP/2 preface; the peer's ALPN then selects "http/1.1", the rider is answered
 * connection_aborted and flagged retryable, and the session replays it onto the HTTP/1.1 driver the
 * fallback built. Since L6 finding 4a stopped the rider riding where the protocol is already
 * decided, that bounce happens only over TLS, and the one case which ran it with a BodySink -
 * utf_baselib_httpclient4's, over cleartext - has had no bounce in it since. So two properties had
 * no control anywhere ( notes/plans/issues/http2-l6-review-record.md, third pass, decision 1 ):
 *
 *   - the bounced rider tells the caller's sink NOTHING - no byte and no onComplete( ) - because
 *     applyClosed( ) says something to a sink only when the close is the answer;
 *   - and the session's refusal to replay onto a sink ( chkPrepareRetry( ) ) cannot fire on the
 *     bounce, because the rider was answered before a byte of its response existed.
 *
 * The second is the refusal D4 widens - a sink which threw is never replayed onto - which is why
 * this case landed, green, before D4's change, and has to stay green across it.
 *
 * WHY NOT IN utf_baselib_httpclient5, where the same exchange without a sink lives: that module is
 * over the 40 MB target ( its Main.cpp records 47.5 MB ), and src/utests/AGENTS.md says not to add
 * to one there. What the case needs is shared rather than copied, since a test header may never be
 * included across module directories: the peer is utests/baselib/Http1DriverTlsTestUtils.h's, the
 * session helpers utests/baselib/HttpClientSessionTlsTestUtils.h's, and the counting sink - once
 * utf_baselib_httpclient4's - utests/baselib/HttpClientSessionTestUtils.h's.
 *
 * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev root
 * CA for every test binary and the test server certificate is issued for that name.
 */

namespace utest
{
    namespace tlssession
    {
        /**
         * @brief The TLS session's helpers - utests/baselib/HttpClientSessionTlsTestUtils.h's, which
         * this module carried a copy of
         */

        using sessiontls::makeSession;
        using sessiontls::makeRequest;
        using sessiontls::runSessionTask;
        using sessiontls::requireTaskSucceeded;
        using sessiontls::bodyOf;
        using sessiontls::statsOf;

        /**
         * @brief The TLS HTTP/1.1 peer - utests/baselib/Http1DriverTlsTestUtils.h's, the one peer of
         * this kind in the tree, under the name this case was written against
         */

        typedef http1drivertls::TlsPeer                                         Http1TlsPeer;

    } // tlssession

} // utest

/**
 * @brief The ALPN fallback over TLS with a counting sink - the bounce tells the sink nothing, and the
 * replay is not refused because of it
 *
 * THE NAME IS THE ONE utf_baselib_httpclient4's case carried until it lost its bounce, and it is
 * accurate here: this chain has a fallback retry in it. The request is dispatched twice - the rider
 * onto the establishing task, and the replay onto the HTTP/1.1 driver - and the sink is installed
 * for the whole chain ( SessionRequestTaskT::startHop( ) hands it to every hop ).
 *
 * WHAT MAKES IT RED, which is the point of a control. A bounce which delivered anything to the sink
 * - onComplete( ) on the rider's close, as H08 once did - makes completions( ) two. A replay which
 * the sink refusal blocked - a refusal widened so that it also fired on a sink that had seen
 * nothing - fails the request with the rider's connection_aborted, and requireTaskSucceeded( )
 * says so. And a rider which no longer rode - ridePreface turned off where ALPN cannot be known in
 * advance - makes dispatched one, which is the must-not-move half of L6 finding 4a that
 * ClientSessionTls_Http11FallbackExchangeTests pins without a sink.
 */

UTF_AUTO_TEST_CASE( ClientSessionTls_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::tlssession;

    Http1TlsPeer peer(
        []( SAA_inout Http1TlsPeer& self, SAA_inout Http1TlsPeer::sslstream_t& stream ) -> void
        {
            const auto head = self.readRequestHead( stream );

            self.record( "head:" + Http1TlsPeer::requestLineOf( head ) );

            Http1TlsPeer::send(
                stream,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 6\r\n"
                "\r\n"
                "secure"
                );

            self.observeStreamEnd( stream );
        }
        );

    const auto session = makeSession();

    BL_SCOPE_EXIT_WARN_ON_FAILURE(
        {
            session -> dispose();
        },
        "utest::tlssession::ClientSessionTls_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests"
        );

    const auto sink = utest::session::CountingBodySink::createInstance();

    const auto requestTask = session -> createRequestTask(
        makeRequest( peer.port(), "/secure" ),
        om::ObjPtrCopyable< httpclient::BodySink >( om::qi< httpclient::BodySink >( sink ) )
        );

    const auto task = om::qi< Task >( requestTask );

    runSessionTask( task );

    requireTaskSucceeded( task );

    UTF_REQUIRE_EQUAL( requestTask -> response().status(), 200U );

    /*
     * THE FALLBACK, ASSERTED AND NOT ASSUMED: the identifier the peer selected says the answer came
     * over the HTTP/1.1 driver, which exists only because the rider's connection fell back
     */

    UTF_REQUIRE( httpclient::HttpProtocol::Http11 == requestTask -> response().protocol() );
    UTF_REQUIRE_EQUAL( requestTask -> response().negotiatedAlpn(), std::string( "http/1.1" ) );

    /*
     * The body went to the SINK and not into the response - design 5.3's streamed form - and the
     * sink holds it ONCE, and was told it was complete ONCE. The bounced rider said nothing to it
     */

    UTF_REQUIRE( ! requestTask -> response().body() );

    UTF_REQUIRE_EQUAL( sink -> received(), std::string( "secure" ) );

    UTF_REQUIRE_EQUAL( sink -> completions(), 1U );

    /*
     * AND THE BOUNCE REALLY HAPPENED - two dispatches for one request, the rider and its replay,
     * each given back to the pool - over ONE connection, carrying ONE request on the wire: the
     * rider was never written, so a second recorded head would mean the fallback had cost a request
     * rather than replayed it
     */

    const auto stats = statsOf( session );

    UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
    UTF_REQUIRE_EQUAL( stats.dispatched.value(), 2U );
    UTF_REQUIRE_EQUAL( stats.released.value(), 2U );

    UTF_REQUIRE_EQUAL( peer.records().size(), 1U );
    UTF_REQUIRE_EQUAL( peer.records()[ 0 ], std::string( "head:GET /secure HTTP/1.1" ) );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );
}

#endif /* __UTEST_TESTCLIENTSESSIONTLSSINKFALLBACK_H_ */
