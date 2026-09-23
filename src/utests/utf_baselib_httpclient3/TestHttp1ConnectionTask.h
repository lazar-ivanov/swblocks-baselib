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

#ifndef __UTEST_TESTHTTP1CONNECTIONTASK_H_
#define __UTEST_TESTHTTP1CONNECTIONTASK_H_

#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/httpserver/HttpServer.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/HttpServerHelpers.h>
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.3 - the HTTP/1.1 driver (design 5.5)
 *
 * The driver under test is tasks::Http1ConnectionTaskT< STREAM >, reached the way the pool will
 * reach it: an S4.1 ClientConnectionTaskBaseT establishes the connection and hands the connected
 * stream to the driver factory, which is the fallback path the plan's S4.1 entry corrected - the
 * h2 task does NOT go through the factory, this driver is what does.
 *
 * WHAT THESE CASES ARE ABOUT. Two things the L2 review singled out as easy to get wrong silently:
 *
 *   - REUSE IS DERIVED HERE. The codec publishes httpVersion(), needsEof() and the header list and
 *     no keep-alive verdict at all, so each input of that derivation gets a case of its own, and
 *     the positive verdict is proved by actually putting a second request down the same connection
 *     rather than by reading a flag.
 *   - INTERIM RESPONSES ARRIVE AFTER THE FACT. The parser files a 1xx and restarts with no
 *     per-interim callback, so the driver delivers them when the final header section completes.
 *     The case pins the ORDER the sink sees, which is what that arrangement has to preserve.
 *
 * THE PEERS, AND WHY THERE ARE TWO. Http1Driver_AgainstTheLibraryHttpServerTests runs against
 * bl::httpserver::HttpServer, a real peer with a real parser - but that server cannot keep a
 * connection alive: Response.h puts 'Connection: close' on every response it builds and says why,
 * and HttpServerConnection is finished after one. So it proves the request/response path and the
 * NEGATIVE half of the derivation against something that is not a fake, and the scripted peer
 * below proves the half it cannot - reuse, HTTP/1.0, read-until-close, interim responses and
 * chunked trailers, none of which HttpServer can produce.
 *
 * THE HELPERS MOVED OUT IN S6R.2 and are now utests/baselib/Http1DriverTestUtils.h - unchanged,
 * only relocated, because utf_baselib_httpclient7 needs them too and a test header may never be
 * included across module directories (src/utests/AGENTS.md). What stayed here is what only this
 * module wants: the library's own HttpServer behind the real-peer case, and the explicit
 * instantiation over the TLS stranded policy below.
 */

/*
 * S4.3 DELIVERS A TEMPLATE OVER A STREAM POLICY, AND A TEMPLATE NOTHING INSTANTIATES IS NOT
 * COMPILED. That has bitten this project twice - BeastBoostImports.h, which no module includes, and
 * TcpTunnelStageT over a TLS policy, which S3.5's release validation passed straight over because
 * nothing had ever named the combination. The cases above exercise the driver over the cleartext
 * stranded policy; this explicit instantiation is what turns "it also works over the TLS one" from
 * a claim into a fact, by compiling every member of it.
 */

template class bl::om::ObjectImpl
<
    bl::tasks::Http1ConnectionTaskT< bl::tasks::TcpSslSocketAsyncStrandedBase >
>;

UTF_AUTO_TEST_CASE( Http1Driver_RequestResponseAndKeepAliveReuseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * THE POSITIVE HALF OF THE REUSE DERIVATION, PROVED BY REUSING. A first response which says
     * nothing about the connection leaves an HTTP/1.1 connection persistent, so the driver must put
     * it back in the Ready state with its slot free - and the thing which proves it is not the
     * state but the second request going down the same connection and being answered on it
     *
     * The second request is a POST with a buffered body, which also pins the framing this driver
     * owns: it SETS Content-Length from the body it will actually write, and the peer reads exactly
     * that many bytes rather than blocking for a body which never comes
     */

    const std::string body( "posted-body" );

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto first = ScriptedPeer::readRequest( socket );

            /*
             * RECORDED RATHER THAN ASSERTED, because this runs on the peer's worker thread and a
             * UTF_REQUIRE which fails there throws boost::execution_aborted, which is not an
             * std::exception and would leave the process rather than the case
             */

            self.record(
                ScriptedPeer::requestHasField( first, "Host" ) ? "host:yes" : "host:no"
                );

            self.record( "first:" + ScriptedPeer::requestLineOf( first ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "hello"
                );

            const auto second = ScriptedPeer::readRequest( socket );

            self.record( "second:" + ScriptedPeer::requestLineOf( second ) );
            self.record( "body:" + ScriptedPeer::requestBodyOf( second ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 201 Created\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "ok"
                );

            self.waitForRelease();
        }
        );

    const auto firstSink = RecordingSinkImpl::createInstance();
    const auto secondSink = RecordingSinkImpl::createInstance();

    auto stateAfterFirst = httpclient::ConnectionState::Closed;
    auto stateAfterSecond = httpclient::ConnectionState::Ready;

    std::size_t slotsAfterFirst = 0U;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            const auto driver = establishDriver( eq, peer.port() );
            const auto driverTask = om::qi< Task >( driver );

            eq -> push_back( driverTask );

            UTF_REQUIRE_EQUAL( driver -> negotiated().protocol(), httpclient::HttpProtocol::Http11 );

            /*
             * A cleartext connection was not decided by ALPN, so it reports no identifier - the
             * value S4.1 constructs through withoutAlpn, travelling unchanged through the factory
             */

            UTF_REQUIRE( ! driver -> negotiated().hasAlpn() );

            const auto firstHandle = driver -> submit(
                makeRequest( peer.port(), "/first" ),
                om::qi< httpclient::ClientStreamEventSink >( firstSink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != firstHandle );

            chkOrFail(
                firstSink -> waitForClosed(),
                "the first stream never ended; events so far: " + joinEvents( firstSink -> events() )
                );

            stateAfterFirst = driver -> state();
            slotsAfterFirst = driver -> freeStreamSlots();

            auto request = makeRequest( peer.port(), "/second", "POST" );

            const auto block = data::DataBlock::createInstance( body.size() );

            std::memcpy( block -> pv(), body.c_str(), body.size() );

            block -> setSize( body.size() );

            request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );

            const auto secondHandle = driver -> submit(
                request,
                om::qi< httpclient::ClientStreamEventSink >( secondSink )
                );

            chkOrFail(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE != secondHandle,
                "the connection refused a second request, so it was not reused"
                );

            UTF_REQUIRE( secondHandle != firstHandle );

            chkOrFail(
                secondSink -> waitForClosed(),
                "the second stream never ended; events so far: " + joinEvents( secondSink -> events() )
                );

            stateAfterSecond = driver -> state();

            peer.release();

            eq -> wait( driverTask );

            chkTaskSucceeded( driverTask );
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The first exchange - persistent, so the connection went back to Ready with its slot free
     */

    UTF_REQUIRE_EQUAL(
        joinEvents( firstSink -> events() ),
        std::string( "headers:200:final|data:5|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( firstSink -> body(), std::string( "hello" ) );
    UTF_REQUIRE_EQUAL( firstSink -> finalStatus(), 200U );
    UTF_REQUIRE_EQUAL( stateAfterFirst, httpclient::ConnectionState::Ready );
    UTF_REQUIRE_EQUAL( slotsAfterFirst, 1U );

    const auto blocks = firstSink -> blocks();

    UTF_REQUIRE_EQUAL( blocks.size(), 1U );
    UTF_REQUIRE( ! blocks[ 0 ].isInterim );
    UTF_REQUIRE( nullptr != blocks[ 0 ].headers.tryGet( "content-type" ) );

    /*
     * The second exchange - the server said close, so the connection did not go back to the pool
     */

    UTF_REQUIRE_EQUAL(
        joinEvents( secondSink -> events() ),
        std::string( "headers:201:final|data:2|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( secondSink -> body(), std::string( "ok" ) );
    UTF_REQUIRE( httpclient::ConnectionState::Ready != stateAfterSecond );

    UTF_REQUIRE( peer.waitForRecords( 4U ) );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 4U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "host:yes" ) );
    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "first:GET /first HTTP/1.1" ) );
    UTF_REQUIRE_EQUAL( records[ 2 ], std::string( "second:POST /second HTTP/1.1" ) );
    UTF_REQUIRE_EQUAL( records[ 3 ], std::string( "body:" ) + body );
}

UTF_AUTO_TEST_CASE( Http1Driver_ReuseVerdictInputsTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    /*
     * EACH INPUT OF THE DERIVATION, ON ITS OWN. The codec answers none of this - it publishes
     * httpVersion(), needsEof() and the header list and no verdict - so every rule the driver
     * applies is a rule only these cases hold it to
     */

    {
        /*
         * Connection: close on an HTTP/1.1 response
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: close\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * The same, spelled as one token of a list and in a case the wire is free to choose. The
         * fold is ASCII only and never through std::locale(), which is the rule the whole codec
         * follows
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: Keep-Alive, CLOSE\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * HTTP/1.0 without keep-alive - persistence is opt-in below HTTP/1.1
         */

        const auto result = runExchange(
            "HTTP/1.0 200 OK\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * HTTP/1.0 WITH keep-alive - persistent, and the one case where the version alone would
         * have given the wrong answer
         */

        const auto result = runExchange(
            "HTTP/1.0 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }

    {
        /*
         * A body framed by the close: it is delivered in full and the connection cannot be reused,
         * because there is no end to such a message other than the close which just happened
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "until-the-close",
            true /* closeImmediately */
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "until-the-close" ) );
        UTF_REQUIRE_EQUAL( result.blocks.size(), 1U );
        UTF_REQUIRE_EQUAL( result.blocks[ 0 ].status, 200U );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * THE ONE INPUT WHICH IS NOT THE RESPONSE'S, AND THE ONLY WAY TO TELL IT APART IS A SERVER
         * WHICH DOES NOT ECHO. The request said close; the response says nothing about the
         * connection, is HTTP/1.1, is framed by a Content-Length and leaves no bytes over - so
         * every response-side input says reusable and only the request says otherwise. RFC 9112
         * section 9.6 puts the rule on the sender: a client which sent close MUST NOT send another
         * request on that connection, and the server MUST close after its final response while
         * only SHOULD echoing the token. A driver which derived this verdict from the response
         * alone would report Ready here, the pool would hand the connection to the next request,
         * and that request would reach a socket the server was already closing - and fail
         * NON-retryably, because its bytes did go out
         */

        http::HeaderList requestHeaders;

        requestHeaders.append( "Connection", "close" );

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "abc",
            false /* closeImmediately */,
            "GET",
            "/resource",
            requestHeaders
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );

        /*
         * The token reached the wire untouched - serializeRequestHead( ) passes the caller's fields
         * through - which is what puts the server under the rule in the first place. Without this
         * the case could pass on a driver which refused the header instead of honouring it
         */

        UTF_REQUIRE( ScriptedPeer::requestHasField( result.request, "Connection" ) );

        UTF_REQUIRE(
            std::string::npos !=
                ScriptedPeer::toLowerAscii( result.request ).find( "\r\nconnection: close\r\n" )
            );

        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }
}

UTF_AUTO_TEST_CASE( Http1Driver_InterimResponsesPrecedeTheFinalBlockTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    /*
     * INTERIM RESPONSES ARRIVE AFTER THE FACT, AND THE ORDER THE SINK SEES IS WHAT MUST SURVIVE IT.
     * The parser files a 1xx and restarts on the same buffer with no per-interim callback, so the
     * driver cannot deliver a 103 when it reaches the wire - it delivers every filed interim, in
     * order, immediately before the final block. What this case pins is that ordering and the
     * sink's status/flag invariant, and that the hints themselves are not lost on the way
     */

    const auto result = runExchange(
        "HTTP/1.1 100 Continue\r\n"
        "\r\n"
        "HTTP/1.1 103 Early Hints\r\n"
        "Link: </style.css>; rel=preload; as=style\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "done"
        );

    UTF_REQUIRE_EQUAL(
        joinEvents( result.events ),
        std::string( "headers:100:interim|headers:103:interim|headers:200:final|data:4|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( result.blocks.size(), 3U );

    UTF_REQUIRE( result.blocks[ 0 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 0 ].status, 100U );

    UTF_REQUIRE( result.blocks[ 1 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 1 ].status, 103U );

    const auto* const link = result.blocks[ 1 ].headers.tryGet( "link" );

    UTF_REQUIRE( nullptr != link );
    UTF_REQUIRE_EQUAL( *link, std::string( "</style.css>; rel=preload; as=style" ) );

    UTF_REQUIRE( ! result.blocks[ 2 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 2 ].status, 200U );

    UTF_REQUIRE_EQUAL( result.body, std::string( "done" ) );

    /*
     * An interim response says nothing about the connection, so the final one decides - and this
     * one leaves it persistent
     */

    UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
}

UTF_AUTO_TEST_CASE( Http1Driver_ChunkedTrailersAndBodilessTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    {
        /*
         * A chunked body reaches the sink de-chunked, and the trailer section reaches it as
         * trailers rather than as headers - keeping them apart is what stops a trailer becoming a
         * header injection, which is the codec's rule and is preserved here
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6\r\n world\r\n"
            "0\r\n"
            "X-Checksum: 9f2b\r\n"
            "\r\n"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "hello world" ) );
        UTF_REQUIRE_EQUAL( result.trailers.size(), 1U );

        const auto* const checksum = result.trailers.tryGet( "x-checksum" );

        UTF_REQUIRE( nullptr != checksum );
        UTF_REQUIRE_EQUAL( *checksum, std::string( "9f2b" ) );

        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );

        /*
         * The ORDER is the point of the event list: the trailers arrive after the last body chunk
         * and before onClosed, which is the sink's contract
         */

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:200:final|data:11|trailers:1|closed:ok" )
            );
    }

    {
        /*
         * A trailer section carrying a field RFC 9110 section 6.5.1 forbids there. The rule is the
         * codec's and is tested with it; what is this driver's, and what this pins, is that the
         * refusal reaches the request as a FAILED stream and takes the connection with it - there
         * are bytes on it which this client has decided not to interpret, so there is nothing safe
         * to do with it but end it
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "0\r\n"
            "Expires: Wed, 21 Oct 2026 07:28:00 GMT\r\n"
            "\r\n"
            );

        UTF_REQUIRE( result.trailers.empty() );
        UTF_REQUIRE( std::string::npos != joinEvents( result.events ).find( "closed:error" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * 204 carries no body whatever it says, so nothing follows the header block
         */

        const auto result = runExchange(
            "HTTP/1.1 204 No Content\r\n"
            "\r\n"
            );

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:204:final|closed:ok" )
            );

        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }

    {
        /*
         * A response to HEAD carries a Content-Length and no body, which only a parser TOLD the
         * request was a HEAD can frame. The driver is what tells it, from the method it is about to
         * write - so this case fails outright if that never reaches the parser
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 1024\r\n"
            "\r\n",
            false /* closeImmediately */,
            "HEAD"
            );

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:200:final|closed:ok" )
            );

        UTF_REQUIRE( result.body.empty() );
        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }
}

UTF_AUTO_TEST_CASE( Http1Driver_RequestsThisDriverRefusesTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * TWO REFUSALS, AND NEITHER OF THEM PUTS A BYTE ON THE WIRE. A streaming body is refused at
     * submit( ... ), because HTTP/1.1 would need request-side chunked framing for a body of unknown
     * length and S2.5's serializer has none - a driver which took the request and wrote only its
     * head would leave the server waiting for a body forever. A caller supplied Transfer-Encoding
     * is refused for the same reason, and refusing it is a framing defense: writing the head as
     * though this driver honoured the coding is how a request gets smuggled
     *
     * In both cases the connection itself is untouched and stays usable, which the final exchange
     * on the same connection proves
     */

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( "served:" + ScriptedPeer::requestLineOf( request ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "ok"
                );

            self.waitForRelease();
        }
        );

    const auto refusedSink = RecordingSinkImpl::createInstance();
    const auto acceptedSink = RecordingSinkImpl::createInstance();

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            const auto driver = establishDriver( eq, peer.port() );
            const auto driverTask = om::qi< Task >( driver );

            eq -> push_back( driverTask );

            /*
             * A streaming body source, refused synchronously
             */

            {
                auto request = makeRequest( peer.port(), "/streamed", "POST" );

                request.bodySource(
                    om::ObjPtrCopyable< httpclient::BodySource >(
                        om::qi< httpclient::BodySource >( RefusingBodySourceImpl::createInstance() )
                        )
                    );

                UTF_REQUIRE_EQUAL(
                    driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( refusedSink )
                        ),
                    static_cast< httpclient::stream_handle_t >(
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE
                        )
                    );
            }

            /*
             * A caller supplied transfer coding - accepted as a handle and then failed as a
             * stream, because nothing of it was written
             */

            {
                auto request = makeRequest( peer.port(), "/chunked", "POST" );

                request.headers().append( "Transfer-Encoding", "chunked" );

                const auto handle = driver -> submit(
                    request,
                    om::qi< httpclient::ClientStreamEventSink >( refusedSink )
                    );

                UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                chkOrFail(
                    refusedSink -> waitForClosed(),
                    "the refused stream never ended"
                    );

                UTF_REQUIRE( refusedSink -> errorCode() );

                /*
                 * Nothing was written, so the request is provably unprocessed
                 */

                UTF_REQUIRE( refusedSink -> isRetryable() );
            }

            /*
             * ... and the connection is still good
             */

            UTF_REQUIRE_EQUAL( driver -> state(), httpclient::ConnectionState::Ready );

            const auto handle = driver -> submit(
                makeRequest( peer.port(), "/after" ),
                om::qi< httpclient::ClientStreamEventSink >( acceptedSink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

            chkOrFail(
                acceptedSink -> waitForClosed(),
                "the stream after the refusals never ended"
                );

            peer.release();

            eq -> wait( driverTask );

            chkTaskSucceeded( driverTask );
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    UTF_REQUIRE_EQUAL( refusedSink -> events().size(), 1U );
    UTF_REQUIRE_EQUAL( refusedSink -> events()[ 0 ], std::string( "closed:error" ) );

    UTF_REQUIRE_EQUAL(
        joinEvents( acceptedSink -> events() ),
        std::string( "headers:200:final|data:2|closed:ok" )
        );

    UTF_REQUIRE( peer.waitForRecords( 1U ) );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 1U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "served:GET /after HTTP/1.1" ) );
}

UTF_AUTO_TEST_CASE( Http1Driver_RequestOnTheWireIsNotRetryableTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * A REQUEST WHOSE WRITE HAS BEEN ISSUED IS NOT PROVABLY UNPROCESSED. The peer-close path
     * reports retryability as the negation of "this request may have been sent", and the driver
     * used to set that only when the WRITE COMPLETION ran - so a connection which died between
     * the write going out and its completion was reported as retryable. The pool's
     * chkRequestMayBeReplayed( ) answers true from that limb BEFORE it reaches the idempotency
     * gate, which makes this a silently duplicated POST rather than a failed one: the octets are
     * on the wire and the origin may act on them either way.
     *
     * The probe is what makes the moment reachable at all - see Http1DriverProbe. The peer here
     * reads the request and says nothing, so nothing but the probe ends this stream, and the
     * REFUSED cases above are the other side of the same assertion: a request which never
     * reached the socket is still retryable
     */

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( "read:" + ScriptedPeer::requestLineOf( request ) );

            self.waitForRelease();
        }
        );

    const auto sink = RecordingSinkImpl::createInstance();

    bool probeParsed = false;

    scheduleAndExecuteInParallel(
        [ &peer, &sink, &probeParsed ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            const auto probe = establishProbeDriver( eq, peer.port() );

            probe -> probeWith(
                makeRequest( peer.port(), "/inflight", "POST" ),
                om::qi< httpclient::ClientStreamEventSink >( sink )
                );

            const auto driverTask = om::qi< Task >( probe );

            eq -> push_back( driverTask );

            chkOrFail(
                sink -> waitForClosed(),
                "the probed stream never ended; events so far: " + joinEvents( sink -> events() )
                );

            probeParsed = probe -> probeParsed();

            peer.release();

            eq -> wait( driverTask );
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The premise of the probe: the parser really did see the partial response, so what ended
     * this stream is the peer-close path and not a codec refusal
     */

    UTF_REQUIRE( probeParsed );

    /*
     * The stream failed, which is not the subject - THIS is: the request went out, so nothing
     * downstream may replay it on another connection
     */

    UTF_REQUIRE( sink -> errorCode() );
    UTF_REQUIRE( ! sink -> isRetryable() );

    UTF_REQUIRE( peer.waitForRecords( 1U ) );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 1U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "read:POST /inflight HTTP/1.1" ) );
}

UTF_AUTO_TEST_CASE( Http1Driver_AgainstTheLibraryHttpServerTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * THE REAL PEER. Everything above is scripted bytes; this is the library's own HttpServer, with
     * its own parser and its own response builder, and what it proves is that the request this
     * driver renders is one a real server accepts and that the response a real server writes is one
     * this driver reads
     *
     * It also proves the NEGATIVE half of the reuse derivation against something that is not a
     * fake, and it is the only peer here which can: HttpServer puts 'Connection: close' on every
     * response it builds - Response.h says so, and says it does not implement persistent
     * connections - so the driver must notice and not return the connection to the pool. That the
     * scripted peer could say the same word is not the same as a real server saying it
     */

    const auto sink = RecordingSinkImpl::createInstance();

    auto stateAfterResponse = httpclient::ConnectionState::Ready;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        [ & ]() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto port = static_cast< os::port_t >( test::UtfArgsParser::port() );

                    const auto driver = establishDriver( eq, port, test::UtfArgsParser::host() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    httpclient::ClientRequest request;

                    request.method( "GET" );

                    request.url(
                        net::Uri::parse(
                            "http://" +
                            test::UtfArgsParser::host() +
                            ":" +
                            utils::lexical_cast< std::string >( port ) +
                            utest::http::g_requestUri
                            )
                        );

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE(
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle
                        );

                    chkOrFail(
                        sink -> waitForClosed(),
                        "the stream against HttpServer never ended; events so far: " +
                            joinEvents( sink -> events() )
                        );

                    stateAfterResponse = driver -> state();

                    eq -> wait( driverTask );

                    chkTaskSucceeded( driverTask );
                }
                );
        }
        );

    UTF_REQUIRE_EQUAL( sink -> finalStatus(), 200U );
    UTF_REQUIRE_EQUAL( sink -> body(), utest::http::g_desiredResult );
    UTF_REQUIRE( ! sink -> errorCode() );

    chkOrFail(
        httpclient::ConnectionState::Ready != stateAfterResponse,
        "HttpServer said 'Connection: close' and the driver returned the connection to the pool"
        );
}

#endif /* __UTEST_TESTHTTP1CONNECTIONTASK_H_ */
