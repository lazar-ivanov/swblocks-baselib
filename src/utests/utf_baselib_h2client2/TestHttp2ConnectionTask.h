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

#ifndef __UTEST_TESTHTTP2CONNECTIONTASK_H_
#define __UTEST_TESTHTTP2CONNECTIONTASK_H_

#include <baselib/tasks/TcpStrandedStreams.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/RawFrameScriptPeer.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.2 - the HTTP/2 driver over CLEARTEXT (design 5.1, 5.2 and 5.7)
 *
 * The task under test is tasks::Http2ConnectionTaskT< TcpSocketAsyncStrandedBase >, driven against
 * the in-process peer of design 8.2 and against the byte-exact raw peer beside it. The TLS half of
 * the same driver is utf_baselib_h2client3.
 *
 * WHAT THESE CASES ARE ABOUT. Not the protocol - the engine is S3.1's and is tested exhaustively in
 * utf_baselib_h2core against byte vectors. What this slice owns is the SHELL: that the opening
 * frames leave in one write, that a payload reaches a request as a pooled block, that the write
 * pump keeps exactly one write in flight while a read is outstanding, that the three connection
 * timers of design 5.7 do what the table says, and that the two ways out of the task stay
 * distinguishable - a deliberate close completes it SUCCESSFULLY and a cancel does not.
 *
 * The helpers are in utests/baselib/Http2DriverTestUtils.h, shared with the TLS module.
 */

namespace utest
{
    namespace h2driver
    {
        typedef bl::om::ObjectImpl
            <
                DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
            >
            PlainDriverImpl;

        inline auto makePeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

    } // h2driver

} // utest

/**
 * @brief The ordinary path - one request, one response, over cleartext h2
 *
 * It also pins the two things the S2.6 contract publishes and which only a driver can fill: the
 * status arrives BESIDE the header list, because http::HeaderList cannot hold ":status", and the
 * pseudo-headers are not in the list at all
 */

UTF_AUTO_TEST_CASE( H2Driver_RequestAndResponseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            /*
             * A responder runs on an I/O thread, where the Boost.Test macros are not safe, so it
             * states what it expects by throwing - which the peer records
             */

            BL_CHK(
                false,
                "GET" == request.method && "/hello" == request.path,
                BL_MSG()
                    << "The peer was asked for an unexpected request: "
                    << request.method
                    << " "
                    << request.path
                );

            http2::HpackFieldList fields;

            fields.push_back(
                http2::HpackField( std::string( "content-type" ), std::string( "text/plain" ) )
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U, fields )
                .data( "hello world" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    const auto handle = connection -> submit(
                        makeRequest( "http://127.0.0.1/hello" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            /*
             * The driver never went through the factory for itself - it IS the connection
             */

            UTF_REQUIRE_EQUAL( record -> creations, 0U );

            UTF_REQUIRE( HttpProtocol::Http2 == connection -> negotiated().protocol() );
            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "hello world" ) );
            UTF_REQUIRE_EQUAL( sink -> headerValue( "content-type" ), std::string( "text/plain" ) );

            /*
             * ":status" is NOT a header - the list refuses a colon by design, which is why the
             * status travels as its own parameter
             */

            UTF_REQUIRE( sink -> headerValue( ":status" ).empty() );

            UTF_REQUIRE( ! sink -> errorCode() );

            const auto records = sink -> records();

            UTF_REQUIRE_EQUAL( records.size(), 3U );
            UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "headers 200" ) );
            UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "data 11" ) );
            UTF_REQUIRE_EQUAL( records[ 2 ], std::string( "closed cleanly" ) );

            requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief The opening write is ONE write, and it carries the first request with it
 *
 * This is a fingerprint statement and not an efficiency one: a browser coalesces the preface, its
 * SETTINGS, its connection WINDOW_UPDATE and its first HEADERS into one segment, and a client which
 * sends them in four writes is distinguishable from one which does not however identical the bytes
 * are. Nothing on the wire can tell the two apart, so the assertion is made where the writes are
 */

UTF_AUTO_TEST_CASE( H2Driver_OpeningWriteIsOneWriteTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            return h2peer::Http2ResponseScript()
                .headers( 204U, http2::HpackFieldList(), true /* endStream */ )
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            /*
             * The profile's connection WINDOW_UPDATE has to be non-zero for the frame to be in the
             * opening write at all - a zero increment would be an invalid frame, so the engine
             * emits none
             */

            Http2ConnectionConfig h2config;

            h2config.profile.connectionWindowUpdateIncrement = 15663105U;

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                h2config,
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            /*
             * SUBMITTED BEFORE THE TASK IS EVEN SCHEDULED, and that is what makes this case a
             * statement rather than a race. The mailbox takes a command with no strand to post it
             * to and onProtocolNegotiated( ) drains it as part of the opening write; submitting
             * from inside the run instead leaves it a race with the loopback connect, which loses
             * often enough to be seen - it lost under ThreadSanitizer the first time it was run
             * there. It is also what the pool does: the request which caused the connection to
             * exist is already waiting when the handshake completes
             */

            UTF_REQUIRE(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE !=
                    connection -> submit(
                        makeRequest( "http://127.0.0.1/first" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        )
                );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 204U );

            const auto writes = driver -> writes();

            UTF_REQUIRE( ! writes.empty() );

            const auto& opening = writes[ 0 ];

            /*
             * The preface first, verbatim
             */

            UTF_REQUIRE( opening.size() > http2::Globals::g_connectionPreface.size() );

            UTF_REQUIRE_EQUAL(
                opening.substr( 0U, http2::Globals::g_connectionPreface.size() ),
                http2::Globals::g_connectionPreface
                );

            const auto types = frameTypesOf( opening );

            UTF_REQUIRE( types.size() >= 3U );

            UTF_REQUIRE( http2::Globals::FRAME_TYPE_SETTINGS == types[ 0 ] );

            UTF_REQUIRE(
                containsFrameType( types, http2::Globals::FRAME_TYPE_WINDOW_UPDATE )
                );

            /*
             * And the first request's HEADERS, in the SAME write. This is the assertion the case
             * exists for - everything above would also hold if the HEADERS had gone out second
             */

            UTF_REQUIRE( containsFrameType( types, http2::Globals::FRAME_TYPE_HEADERS ) );
        }
        );
}

/**
 * @brief Full duplex - a body going out while a body is coming in, both larger than one window
 *
 * The upload is handed over by provideBody( ) in chunks, as a streaming request task would do it,
 * and the peer withholds its WINDOW_UPDATEs until the client has actually run out of window. The
 * stall is an EVENT and not a duration: awaitWindowStall( ) fires at the exact octet the client
 * cannot go past, so the case is deterministic rather than a race with a timer
 *
 * THE SCRIPT'S SECOND RENDEZVOUS IS THE ONE THIS CASE WAS FLAKY WITHOUT. awaitStreamClosed( )
 * between endStream( ) and closeConnection( ) is what stops the peer tearing the connection down
 * while the client's upload half is still open - the peer's own END_STREAM closes only the
 * response half, and full duplex is precisely the case where the request half outlives it. Without
 * it the close raced the upload and won about once in forty under load, and the assertions below
 * about what the peer RECEIVED are exactly what is lost when it does
 */

UTF_AUTO_TEST_CASE( H2Driver_FullDuplexUploadAndDownloadTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const std::string chunk( 16U * 1024U, 'u' );
    const std::size_t chunks = 6U;

    const std::string download( 200U * 1024U, 'd' );

    const auto peer = makePeer();

    peer -> setWithholdWindowUpdates( true );

    peer -> setResponder(
        [ &download ]( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                request.hasBody,
                BL_MSG()
                    << "The peer expected a request with a body"
                );

            return h2peer::Http2ResponseScript()
                .awaitWindowStall()
                .creditWindow()
                .headers( 200U )
                .data( download )
                .endStream()
                .awaitStreamClosed()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    auto request = makeRequest( "http://127.0.0.1/upload", "POST" );

                    request.bodySource(
                        om::ObjPtrCopyable< httpclient::BodySource >(
                            om::qi< httpclient::BodySource >( StubBodySource::createInstance() )
                            )
                        );

                    UTF_REQUIRE( request.hasBody() );

                    const auto handle = connection -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    for( std::size_t i = 0U; i < chunks; ++i )
                    {
                        connection -> provideBody(
                            handle,
                            blockOf( chunk ),
                            i + 1U == chunks /* endStream */
                            );
                    }

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body().size(), download.size() );
            UTF_REQUIRE_EQUAL( sink -> body(), download );

            /*
             * The whole upload arrived, which it could not have without the WINDOW_UPDATE the
             * stall released and without the driver holding back what the windows would not take
             */

            requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );

            UTF_REQUIRE_EQUAL(
                peer -> recorder().bodyOf( 1U ).size(),
                chunk.size() * chunks
                );

            /*
             * More than one write was needed for a body this size, and exactly one was ever in
             * flight - which is what the pump is
             */

            UTF_REQUIRE( driver -> writes().size() > 1U );
        }
        );
}

/**
 * @brief The keepalive PING of design 5.7, and the connection idle close
 *
 * Both are observed rather than inferred: the PING is counted in the writes the driver made, and
 * the idle close is the GOAWAY the peer records plus a task which completed SUCCESSFULLY - the
 * deliberate door of design 3.2, which is what stops the pool counting a clean shutdown as a
 * failed connection
 */

UTF_AUTO_TEST_CASE( H2Driver_KeepAliveAndIdleCloseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            Http2ConnectionConfig h2config;

            h2config.keepAliveInterval = time::milliseconds( 50 );
            h2config.keepAlivePingReplyTimeout = time::seconds( 10 );
            h2config.idleTimeout = time::milliseconds( 400 );

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                h2config,
                cleartextHttp2Config()
                );

            runDriver( driver, []() -> void {} );

            /*
             * Nothing was ever submitted, so the idle timer is what ended this connection - and it
             * ended it the deliberate way
             */

            chkTaskSucceeded( om::qi< Task >( driver ) );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
            UTF_REQUIRE_EQUAL( connection -> freeStreamSlots(), 0U );

            std::size_t pings = 0U;
            bool sawGoAway = false;

            const auto writes = driver -> writes();

            for( std::size_t i = 0U; i < writes.size(); ++i )
            {
                const auto types = frameTypesOf( writes[ i ] );

                for( std::size_t j = 0U; j < types.size(); ++j )
                {
                    if( http2::Globals::FRAME_TYPE_PING == types[ j ] )
                    {
                        ++pings;
                    }

                    if( http2::Globals::FRAME_TYPE_GOAWAY == types[ j ] )
                    {
                        sawGoAway = true;
                    }
                }
            }

            /*
             * 400ms of idling at a 50ms interval - several PINGs, and every one of them was
             * answered or the reply deadline would have cancelled the task instead
             */

            UTF_REQUIRE( pings >= 2U );

            UTF_REQUIRE( sawGoAway );

            requireRecorded( peer -> recorder(), "the client sent GOAWAY with error 0" );
        }
        );
}

/**
 * @brief A peer which stops answering PINGs loses the connection, and every stream on it
 *
 * The raw peer reads the opening write, answers the SETTINGS so the connection is established, and
 * then says nothing at all. The reply deadline is the only thing which ends this - and it ends it
 * the OTHER way, failed, because a connection which stopped answering is not one that was done
 */

UTF_AUTO_TEST_CASE( H2Driver_KeepAlivePingDeadlineTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto settingsAck = h2peer::frameOctets(
        static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
        static_cast< std::uint8_t >( http2::Globals::FRAME_FLAG_ACK ),
        0U
        );

    h2peer::RawFrameScriptPeer rawPeer(
        h2peer::RawFrameScript()
            .expectPreface()
            .send(
                h2peer::frameOctets(
                    static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
                    0U,
                    0U
                    )
                )
            .send( settingsAck )
            .delay( 3000 )
        );

    const auto record = std::make_shared< FallbackRecord >();

    Http2ConnectionConfig h2config;

    h2config.keepAliveInterval = time::milliseconds( 50 );
    h2config.keepAlivePingReplyTimeout = time::milliseconds( 300 );

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", rawPeer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        h2config,
        cleartextHttp2Config()
        );

    runDriver( driver, []() -> void {} );

    const auto task = om::qi< Task >( driver );

    ( void ) chkTaskFailed( task );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
}

/**
 * @brief A peer which never acknowledges our SETTINGS is dropped on the SETTINGS timeout
 *
 * THE VALUE IS SessionLimits::settingsTimeoutInSeconds AND THERE IS ONLY ONE OF IT. Design 5.7 used
 * to quote 30 seconds beside the engine's 10; S4.2 reconciled that in favour of 10 and amended the
 * design, and this case is what pins the driver to the engine's number rather than to a second one
 * of its own - it sets the limit and the connection dies on it
 */

UTF_AUTO_TEST_CASE( H2Driver_SettingsAcknowledgementTimeoutTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    /*
     * The peer sends its own SETTINGS - so the connection is up and the client acknowledges it -
     * and then withholds the acknowledgement of ours forever
     */

    h2peer::RawFrameScriptPeer rawPeer(
        h2peer::RawFrameScript()
            .expectPreface()
            .send(
                h2peer::frameOctets(
                    static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
                    0U,
                    0U
                    )
                )
            .delay( 5000 )
        );

    const auto record = std::make_shared< FallbackRecord >();

    Http2ConnectionConfig h2config;

    h2config.limits.settingsTimeoutInSeconds = 1U;

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", rawPeer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        h2config,
        cleartextHttp2Config()
        );

    runDriver( driver, []() -> void {} );

    const auto task = om::qi< Task >( driver );

    /*
     * A connection error is neither a clean end nor an external cancel, and the pool must not be
     * told it was either - so the task fails, with the reason
     */

    const auto message = chkTaskFailed( task );

    UTF_REQUIRE( message.find( "SETTINGS" ) != std::string::npos );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

    /*
     * The GOAWAY went out BEFORE the task failed - RFC 9113 5.4.1 asks for it, and throwing at the
     * point the error was detected would have taken the task down with it still queued
     */

    bool sawGoAway = false;

    const auto writes = driver -> writes();

    for( std::size_t i = 0U; i < writes.size(); ++i )
    {
        const auto types = frameTypesOf( writes[ i ] );

        for( std::size_t j = 0U; j < types.size(); ++j )
        {
            if( http2::Globals::FRAME_TYPE_GOAWAY == types[ j ] )
            {
                sawGoAway = true;
            }
        }
    }

    UTF_REQUIRE( sawGoAway );
}

/**
 * @brief A refused stream is answered, and is reported RETRYABLE
 *
 * REFUSED_STREAM is the peer saying "I did not process this", which is the first limb of the
 * retry rule of design 5.4. The connection stays up and takes the next request, which is the other
 * half of what makes the answer useful
 */

UTF_AUTO_TEST_CASE( H2Driver_RefusedStreamIsRetryableTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( 1U == request.streamIndex )
            {
                return h2peer::Http2ResponseScript().refuse();
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "second" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto refused = RecordingSink::createInstance();
            const auto served = RecordingSink::createInstance();

            served -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/first" ),
                        om::qi< httpclient::ClientStreamEventSink >( refused )
                        );

                    refused -> waitForClosed();

                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/second" ),
                        om::qi< httpclient::ClientStreamEventSink >( served )
                        );

                    served -> waitForClosed();
                }
                );

            served -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( refused -> isRetryable() );
            UTF_REQUIRE( refused -> errorCode() );
            UTF_REQUIRE_EQUAL( refused -> status(), 0U );

            UTF_REQUIRE( ! served -> isRetryable() );
            UTF_REQUIRE_EQUAL( served -> status(), 200U );
            UTF_REQUIRE_EQUAL( served -> body(), std::string( "second" ) );
        }
        );
}

/**
 * @brief Cancelling a request resets ONE stream and leaves the connection up (design 5.7)
 *
 * The cancelled stream is answered with operation_aborted and the other one completes on the same
 * connection - which is the property the sentence in 5.7 is actually about
 */

UTF_AUTO_TEST_CASE( H2Driver_CancelResetsOneStreamOnlyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( "/slow" == request.path )
            {
                /*
                 * Long enough that the cancel is certain to reach the peer first, and bounded so a
                 * peer left behind by a failing case still finishes
                 */

                return h2peer::Http2ResponseScript()
                    .delay( 5000 )
                    .headers( 200U, http2::HpackFieldList(), true /* endStream */ );
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "fast" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto slow = RecordingSink::createInstance();
            const auto fast = RecordingSink::createInstance();

            fast -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    const auto slowHandle = connection -> submit(
                        makeRequest( "http://127.0.0.1/slow" ),
                        om::qi< httpclient::ClientStreamEventSink >( slow )
                        );

                    connection -> cancel( slowHandle, asio::error::operation_aborted );

                    slow -> waitForClosed();

                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/fast" ),
                        om::qi< httpclient::ClientStreamEventSink >( fast )
                        );

                    fast -> waitForClosed();
                }
                );

            fast -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( slow -> errorCode() );
            UTF_REQUIRE_EQUAL( slow -> status(), 0U );

            /*
             * The connection was never closed by the cancel - the second request went out on it
             */

            UTF_REQUIRE_EQUAL( fast -> status(), 200U );
            UTF_REQUIRE_EQUAL( fast -> body(), std::string( "fast" ) );
        }
        );
}

/**
 * @brief A GOAWAY drains what is in flight and then closes, cleanly
 *
 * The peer answers the request and then says GOAWAY, which is the ordinary graceful shutdown of
 * RFC 9113 6.8. The connection reports Draining from the moment it arrives and finishes as a
 * DELIBERATE close - the task succeeds and the pool is not told a connection failed
 */

UTF_AUTO_TEST_CASE( H2Driver_GoAwayDrainsAndClosesCleanlyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setGoAwayAfterStreams( 1U );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "bye" )
                .endStream();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/last" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "bye" ) );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
            UTF_REQUIRE_EQUAL( connection -> freeStreamSlots(), 0U );

            /*
             * Nothing further may be submitted, and a request which arrives too late is answered
             * rather than dropped - and answered as RETRYABLE, since not a byte of it was written
             */

            const auto late = RecordingSink::createInstance();

            UTF_REQUIRE(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE ==
                    connection -> submit(
                        makeRequest( "http://127.0.0.1/toolate" ),
                        om::qi< httpclient::ClientStreamEventSink >( late )
                        )
                );
        }
        );
}

#endif /* __UTEST_TESTHTTP2CONNECTIONTASK_H_ */
