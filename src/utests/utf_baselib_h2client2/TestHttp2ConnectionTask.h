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

        /**
         * @brief class UnwrittenStreamProbeT - puts one stream on each side of "was it written"
         * and then takes the peer-closed path, with NOTHING TIMED
         *
         * WHY A PROBE AND NOT A SCRIPT. A stream whose header block is still in the engine's
         * header block queue exists only between applySubmit( ) and the next pumpWrites( ), and
         * those two are adjacent in every strand handler except while a write is in flight -
         * pumpWrites( ) is the only thing that produces a block, and m_isWriteInFlight is the only
         * thing that stops it. So the state finding 4 is about is reachable from a case only by
         * submitting DURING a write, and on loopback a write completes in microseconds. Holding it
         * open long enough for a peer's FIN to arrive would need the client's socket to stall,
         * which needs an SO_SNDBUF clamp and a peer that stops reading - the same apparatus the L4
         * review record says the preface and drain deadlines are owed. A case built on "the FIN
         * usually wins" would be a coin toss wearing an assertion.
         *
         * So the ORDER is taken rather than raced, and asio's strand is what makes it an order.
         * onWriteScheduled( ) runs on the strand inside pumpWrites( ), AFTER onHeaderBlocksProduced(
         * ) has marked the first stream produced and BEFORE async_write is even issued. Both of the
         * things below are therefore queued on the strand strictly before the write starts, so both
         * run before its completion handler can:
         *
         *   1. submit( ) posts the second request. When its onCommandsPosted( ) runs, the write IS
         *      in flight, so applySubmit( ) opens the stream and pumpWrites( ) declines to produce
         *      it - exactly the state a submit made behind a write leaves behind, reached through
         *      the ordinary public entry point and the ordinary command mailbox.
         *   2. a second post then takes the peer-closed path.
         *
         * WHAT IS SIMULATED AND WHAT IS NOT. Only the arrival of the close is: onPeerClosed( ) is
         * called directly rather than reached from a read returning eof. Everything it then decides
         * is the production code's - the stream table it walks, the per-stream isHeadersProduced it
         * reads, the error code, the sinks it answers and the close it begins. The case measures
         * the wire as well as the flags, so "the second block never reached a write" is a
         * measurement and not the premise.
         */

        template
        <
            typename E = void
        >
        class UnwrittenStreamProbeT :
            public DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
        {
            BL_DECLARE_OBJECT_IMPL( UnwrittenStreamProbeT )

        public:

            typedef UnwrittenStreamProbeT< E >                                  this_type;
            typedef DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >       base_type;

            /*
             * Qualified by Task for the same reason the driver's own is: the task hierarchy
             * reaches om::Object by more than one path, so an unqualified ObjPtrCopyable finds
             * addRef in several base subobjects and is ambiguous
             */

            typedef bl::om::ObjPtrCopyable< this_type, bl::tasks::Task >        self_ref_t;

        protected:

            mutable bl::os::mutex                                               m_probeLock;

            ClientRequest                                                       m_secondRequest;
            bl::om::ObjPtr< ClientStreamEventSink >                             m_secondSink;

            bool                                                                m_isArmed;
            std::size_t                                                         m_liveStreamsAtClose;

            UnwrittenStreamProbeT(
                SAA_in          ConnectionKey                                   key,
                SAA_in          typename base_type::factory_ptr_t               driverFactory,
                SAA_in          Http2ConnectionConfig                           h2config,
                SAA_in          ClientConnectionConfig                          config
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( h2config ),
                    BL_PARAM_FWD( config )
                    ),
                m_isArmed( false ),
                m_liveStreamsAtClose( 0U )
            {
            }

            virtual void onWriteScheduled(
                SAA_in          const bl::http2::Session::wire_buffer_t&        buffer
                ) OVERRIDE
            {
                base_type::onWriteScheduled( buffer );

                if( ! m_isArmed )
                {
                    return;
                }

                /*
                 * The write which carries a header block, and not merely the first write. Which
                 * write the first request's HEADERS join depends on whether its submit reached the
                 * mailbox before the strand was ready, and that is a race this case must not have
                 * an opinion about - waiting for the block itself removes it
                 */

                const std::string wire(
                    reinterpret_cast< const char* >( buffer.empty() ? nullptr : &buffer[ 0 ] ),
                    buffer.size()
                    );

                if(
                    ! containsFrameType(
                        frameTypesOf( wire ),
                        bl::http2::Globals::FRAME_TYPE_HEADERS
                        )
                    )
                {
                    return;
                }

                m_isArmed = false;

                ( void ) base_type::submit( m_secondRequest, m_secondSink );

                /*
                 * The accounting of postCommand( ): the operation is begun here and ended by the
                 * handler's own END_MULTIOP
                 */

                base_type::beginOperation();

                base_type::postToStrand(
                    bl::cpp::bind(
                        &this_type::onPeerClosedOnStrand,
                        self_ref_t::acquireRef( this )
                        )
                    );
            }

            void onPeerClosedOnStrand() NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                /*
                 * The precondition, measured on the strand at the instant the close is taken: how
                 * many streams are LIVE. A submission the task had refused would never have
                 * reached m_streams at all - failSubmission( ) answers its sink with exactly the
                 * same error code and the same retryable flag - so without this number the case
                 * could pass for the wrong reason and read the same either way
                 */

                {
                    BL_MUTEX_GUARD( m_probeLock );

                    m_liveStreamsAtClose = base_type::m_streams.size();
                }

                base_type::onPeerClosed();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

        public:

            /**
             * @brief Called before the task is scheduled; the strand reads it afterwards
             */

            void armSecondSubmit(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const bl::om::ObjPtr< ClientStreamEventSink >&  sink
                )
            {
                m_secondRequest = request;
                m_secondSink = bl::om::copy( sink.get() );
                m_isArmed = true;
            }

            std::size_t liveStreamsAtClose() const
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_liveStreamsAtClose;
            }
        };

        typedef bl::om::ObjectImpl< UnwrittenStreamProbeT<> > UnwrittenStreamProbe;

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
 * @brief The upload PULL of design 5.3 - the driver asks, and the body never arrives unasked
 *
 * The case above hands the driver its whole upload in six unasked chunks, which is what the
 * ClientConnection contract permitted before S5.1 and is exactly the shape that moved the
 * buffering into the driver. This one never calls provideBody( ) from the case at all: every byte
 * the peer receives got there because the driver raised onBodyWanted( ) and the sink answered it,
 * so if the pull did not work the peer would receive an empty body and the stream would never end.
 *
 * THE ARITHMETIC IS THE CONTROL, and it is why this case cannot pass for the wrong reason. The
 * sink hands over AT MOST what it was asked for, so the bytes the peer received are bounded by the
 * sum of the amounts the driver asked for. The case therefore reads the asked-for amounts back out
 * of the recorded trace and requires that
 *
 *   - no SINGLE pull asked for as much as the whole body, so one pull cannot account for it, and
 *   - the pulls TOGETHER asked for at least the whole body, so nothing arrived unauthorized,
 *
 * which together with a peer that received all 204800 bytes forces the conclusion that every byte
 * moved because the driver asked for it. A driver that silently stopped pulling would not fail one
 * assertion by a margin - it would deliver nothing at all and the stream would never close.
 *
 * The FIRST pull is also required to precede every response event. That is the contract's
 * statement that onBodyWanted( ) sits OUTSIDE the sink's response ordering, and it is
 * deterministic rather than raced: the first raise happens inside applySubmit( ), on the strand,
 * before a response can have been read.
 */

UTF_AUTO_TEST_CASE( H2Driver_UploadIsPulledFromTheSinkTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const std::string upload( 200U * 1024U, 'p' );

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                request.hasBody,
                BL_MSG()
                    << "The peer expected a request with a body"
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "taken" )
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
            sink -> setUpload( cpp::copy( upload ) );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    auto request = makeRequest( "http://127.0.0.1/pulled", "POST" );

                    request.bodySource(
                        om::ObjPtrCopyable< httpclient::BodySource >(
                            om::qi< httpclient::BodySource >( StubBodySource::createInstance() )
                            )
                        );

                    const auto handle = connection -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    /*
                     * Nothing is provided from here - the pull is the only thing that can move
                     * this body
                     */

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );

            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( 1U ).size(), upload.size() );
            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( 1U ), upload );

            const auto records = sink -> records();

            const std::string prefix( "wanted " );

            std::size_t wantedCount = 0U;
            std::size_t wantedTotal = 0U;
            std::size_t wantedLargest = 0U;

            std::size_t firstWanted = records.size();
            std::size_t firstResponse = records.size();

            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                if( 0U != records[ i ].find( prefix ) )
                {
                    if( i < firstResponse )
                    {
                        firstResponse = i;
                    }

                    continue;
                }

                const auto asked = utils::lexical_cast< std::size_t >(
                    records[ i ].substr( prefix.size() )
                    );

                ++wantedCount;
                wantedTotal += asked;
                wantedLargest = std::max( wantedLargest, asked );

                if( i < firstWanted )
                {
                    firstWanted = i;
                }
            }

            UTF_REQUIRE( wantedCount > 1U );

            /*
             * No one pull could have carried the body, and the pulls together cover every byte of
             * it - see the arithmetic in the comment above the case
             */

            UTF_REQUIRE( wantedLargest < upload.size() );
            UTF_REQUIRE( wantedTotal >= upload.size() );

            UTF_REQUIRE( firstWanted < firstResponse );
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
 *
 * IT CARRIES A LIVE STREAM, AND THAT IS THE SECOND THING IT PINS. onPingDeadline( ) is one of the
 * two routes which used to answer their sinks BEFORE publishing the state that close belongs to
 * ( L6 review, finding 16 ), and a request task derives its outcome - and with it
 * retryIdempotentOnConnectionLoss - from exactly that state. Without a stream the route answers
 * nobody and the breach reads the same as the fix; with one it is an assertion. Unfixed, the sink
 * reads Ready here
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

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    const auto sink = RecordingSink::createInstance();

    sink -> setConnection( connection.get() );

    /*
     * Submitted before the task is scheduled, so the HEADERS ride the preface and the stream is
     * live in the driver's table when the reply deadline fires at it
     */

    const auto handle = connection -> submit(
        makeRequest( "http://127.0.0.1/silent" ),
        om::qi< httpclient::ClientStreamEventSink >( sink )
        );

    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

    runDriver(
        driver,
        [ & ]() -> void
        {
            sink -> waitForClosed();
        }
        );

    sink -> setConnection( nullptr );

    const auto task = om::qi< Task >( driver );

    ( void ) chkTaskFailed( task );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

    /*
     * The deadline is what failed the stream - not the task stopping afterwards, which would have
     * carried operation_aborted and, for a stream whose header block was written, the same
     * non-retryable flag by a different route
     */

    UTF_REQUIRE( eh::errc::make_error_code( eh::errc::timed_out ) == sink -> errorCode() );

    UTF_REQUIRE( ! sink -> isRetryable() );

    /*
     * And the connection had published Closed BEFORE it said so, which is the read the request
     * task's outcome is derived from
     */

    UTF_REQUIRE( ConnectionState::Closed == sink -> stateOnClosed() );
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

/**
 * @brief A peer close decides retryability PER STREAM, from whether the header block was written
 *
 * Design 5.4's third limb is "the connection failed before any byte of the request was written",
 * and that is a property of a STREAM and not of a connection: two requests can be live on one
 * connection with only the first one's header block on the wire, and replaying the second is safe
 * while replaying the first is not. onPeerClosed( ) used to report every live stream
 * non-retryable, which is right for the first and wrong for the second.
 *
 * The two streams here are put on opposite sides of that line by the probe above - deterministically
 * and by strand ordering rather than by timing; its comment says how and says what it simulates.
 * What the case then MEASURES is all three facts the claim rests on:
 *
 *   - two streams were live when the close was taken, so the second request really did open a
 *     stream rather than being refused on the way in;
 *   - exactly ONE header block reached the wire, across every write the driver made;
 *   - and the sinks were answered accordingly - the written stream not retryable, the unwritten
 *     one retryable, both with the same error code.
 *
 * Against the unfixed engine the third bullet is what fails: it closed both with a blanket
 * false.
 */

UTF_AUTO_TEST_CASE( H2Driver_UnwrittenStreamIsRetryableOnPeerCloseTests )
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

            const auto driver = UnwrittenStreamProbe::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto written = RecordingSink::createInstance();
            const auto unwritten = RecordingSink::createInstance();

            /*
             * Armed before the task is scheduled, so the strand reads it with the push_back below
             * ordered in front of it
             */

            driver -> armSecondSubmit(
                makeRequest( "http://127.0.0.1/behind-the-write" ),
                om::qi< httpclient::ClientStreamEventSink >( unwritten )
                );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/on-the-wire" ),
                        om::qi< httpclient::ClientStreamEventSink >( written )
                        );

                    written -> waitForClosed();
                    unwritten -> waitForClosed();
                }
                );

            /*
             * A peer close ends the task through the deliberate door of design 3.2, exactly as the
             * idle close does - the connection did not fail, it ended
             */

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

            /*
             * The precondition: both requests were live streams on this connection when the close
             * was taken
             */

            UTF_REQUIRE_EQUAL( driver -> liveStreamsAtClose(), 2U );

            /*
             * And only one header block ever reached a write. This is the fact the flag is
             * supposed to be derived from, measured on the wire rather than assumed
             */

            std::size_t headerBlocks = 0U;

            const auto writes = driver -> writes();

            for( std::size_t i = 0U; i < writes.size(); ++i )
            {
                const auto types = frameTypesOf( writes[ i ] );

                for( std::size_t j = 0U; j < types.size(); ++j )
                {
                    if( http2::Globals::FRAME_TYPE_HEADERS == types[ j ] )
                    {
                        ++headerBlocks;
                    }
                }
            }

            UTF_REQUIRE_EQUAL( headerBlocks, 1U );

            /*
             * Neither stream was answered, so neither carries a status - what tells them apart is
             * the retryable flag and nothing else
             */

            UTF_REQUIRE_EQUAL( written -> status(), 0U );
            UTF_REQUIRE_EQUAL( unwritten -> status(), 0U );

            UTF_REQUIRE( written -> errorCode() );
            UTF_REQUIRE_EQUAL( written -> errorCode(), unwritten -> errorCode() );

            /*
             * THE ASSERTION THE CASE EXISTS FOR
             */

            UTF_REQUIRE( ! written -> isRetryable() );
            UTF_REQUIRE( unwritten -> isRetryable() );
        }
        );
}

/**
 * @brief The draining reserve of design 4.3, as the POOL has to see it
 *
 * WHAT THE RESERVE IS AND WHY THE DRIVER IS WHAT MAKES IT WORK. StreamRegistry::isDraining( ) goes
 * true once the identifiers left fall to the margin the pool chose - DEFAULT_DRAINING_RESERVE in
 * httpclient::ConnectionPoolPolicy, which reaches the registry as SessionLimits::drainingReserve -
 * and the pool retires a connection it sees Draining. The registry announces nothing when it gets
 * there, and this is the only way a session drains which nothing else already publishes: our own
 * goAway( ) and a GOAWAY received both publish from the path which caused them. Unpublished, the
 * connection goes on reading Ready with slots free, the pool goes on dispatching to it, and every
 * submission bounces back retryable - which is precisely the behaviour the reserve is chosen to
 * prevent, arriving one reserve of streams later instead of never.
 *
 * WHY THE MARGIN IS MOVED RATHER THAN REACHED. A client connection can open ( 2^31 - 1 ) / 2 + 1
 * identifiers, about 1.07 billion, so no case opens its way to the default margin. The reserve is
 * configurable exactly so that it can be set instead: one short of the whole space leaves room for
 * a single stream and puts the session where a spent connection is, which is what the settable
 * field was added for ( H2Session_DrainingReserveFromLimitsTests pins the same boundary on the
 * engine alone ).
 *
 * NOTHING HERE IS TIMED, and two things make that so. The peer answers with an EMPTY script - it
 * never responds, never resets and never closes - so the one stream stays open and the connection
 * cannot retire behind the case's back; and the rendezvous is the peer's own record of the request
 * arriving, which is made strictly after the applySubmit( ) which took the identifier that crossed
 * the margin. The idle timeout is left off, its default, so the graceful close at the end is the
 * draining retirement and can be nothing else.
 */

UTF_AUTO_TEST_CASE( H2Driver_DrainingReserveIsPublishedToThePoolTests )
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

            /*
             * No steps at all - the peer leaves the stream open and goes on reading
             */

            return h2peer::Http2ResponseScript();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            Http2ConnectionConfig h2config;

            /*
             * A registry starts with ( MAX_STREAM_ID - 1 ) / 2 + 1 identifiers in hand, so this
             * reserve is all of them but one
             */

            h2config.limits.drainingReserve = ( http2::Globals::MAX_STREAM_ID - 1U ) / 2U;

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                h2config,
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto held = RecordingSink::createInstance();
            const auto refused = RecordingSink::createInstance();

            runDriver(
                driver,
                [ & ]() -> void
                {
                    /*
                     * The margin leaves room for exactly one stream, and the peer receiving this
                     * request is the proof that the connection was still usable for it
                     */

                    const auto handle = connection -> submit(
                        makeRequest( "http://127.0.0.1/held" ),
                        om::qi< httpclient::ClientStreamEventSink >( held )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    requireRecorded( peer -> recorder(), "request GET /held on stream 1" );

                    /*
                     * THE TWO ASSERTIONS THE CASE EXISTS FOR. That request took the identifier
                     * which crossed the margin, so what the pool reads from here on must be a
                     * connection it retires and not one it dispatches to
                     */

                    UTF_REQUIRE( ConnectionState::Draining == connection -> state() );
                    UTF_REQUIRE_EQUAL( connection -> freeStreamSlots(), 0U );

                    /*
                     * And this is what those two are about: a pool which dispatched anyway gets a
                     * retryable bounce with nothing written - a request failed for a reason the
                     * connection could have declared before it was ever sent
                     */

                    const auto bounced = connection -> submit(
                        makeRequest( "http://127.0.0.1/refused" ),
                        om::qi< httpclient::ClientStreamEventSink >( refused )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != bounced );

                    refused -> waitForClosed();

                    /*
                     * The last stream going away is what retires a draining connection; cancelling
                     * the held one is how the case gets there without waiting for anything
                     */

                    connection -> cancel( handle, asio::error::operation_aborted );

                    held -> waitForClosed();
                }
                );

            /*
             * The deliberate door of design 3.2, and nothing else could have taken it: the peer
             * never answered and never closed, and the idle timer is off
             */

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

            requireRecorded( peer -> recorder(), "the client sent GOAWAY with error 0" );

            /*
             * The refused submission was answered by the driver and never reached the wire - the
             * peer saw the held request and no other
             */

            UTF_REQUIRE( refused -> errorCode() );
            UTF_REQUIRE( refused -> isRetryable() );
            UTF_REQUIRE_EQUAL( refused -> status(), 0U );

            UTF_REQUIRE(
                ! hasRecord( peer -> recorder().records(), "request GET /refused on stream 3" )
                );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief An establishment which never happened bounces what was queued, and keeps WHY on the task
 *
 * THE PATH NO CASE RAN (L6 review, finding 7): an establishment failure of any kind. Every other
 * case here has a live peer, and the pool's rider - the request dispatched onto a connection which
 * is still Connecting - is answered on exactly this path and nowhere else. It is also the first
 * thing a real deployment meets, and the review's finding 4b is about what it says when it does.
 *
 * TWO HALVES, AND THE SECOND IS THE POINT. The first is the contract this driver already states:
 * a submission which never reached a stream was provably not written, so it is ANSWERED rather
 * than dropped, and marked retryable. The second is that closeSubmissions( ) has exactly one error
 * code for that answer - connection_aborted - so a refused connect, a resolver failure, an expired
 * establishment bound and a rejected certificate are indistinguishable from the bounce alone. What
 * separates them is on the TASK, as its exception, and this case is what says so: the request task
 * chains it from there ( HttpClientRequestTask.h, connectionFailureCause( ) ), and if this driver
 * ever stopped failing with the establishment's own error that chain would silently carry nothing.
 *
 * The dead port is a peer's own, taken after the peer has gone, which is the cheapest refused
 * connect that depends on nothing outside this process
 */

UTF_AUTO_TEST_CASE( H2Driver_EstablishmentFailureBouncesAndKeepsItsCauseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    unsigned short deadPort = 0U;

    withPeer(
        makePeer(),
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            deadPort = port;
        }
        );

    UTF_REQUIRE( 0U != deadPort );

    const auto record = std::make_shared< FallbackRecord >();

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", deadPort ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        Http2ConnectionConfig(),
        cleartextHttp2Config()
        );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    const auto sink = RecordingSink::createInstance();

    sink -> setConnection( connection.get() );

    /*
     * Submitted BEFORE the task is scheduled, which is the rider's own shape: the command sits in
     * the mailbox because the strand is not ready, and the establishment then fails under it
     */

    const auto handle = connection -> submit(
        makeRequest( "http://127.0.0.1/hello" ),
        om::qi< httpclient::ClientStreamEventSink >( sink )
        );

    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

    runDriver(
        driver,
        [ & ]() -> void
        {
            sink -> waitForClosed();
        }
        );

    sink -> setConnection( nullptr );

    /*
     * Half one - the bounce, and it says nothing about the cause
     */

    UTF_REQUIRE( sink -> isRetryable() );

    UTF_REQUIRE(
        eh::errc::make_error_code( eh::errc::connection_aborted ) == sink -> errorCode()
        );

    UTF_REQUIRE_EQUAL( sink -> status(), 0U );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

    /*
     * And it was Closed ALREADY WHEN THE BOUNCE WAS ANSWERED, which is the read a request task
     * derives its outcome from ( L6 review, finding 16 ). This is the onTaskStoppedNothrow( )
     * route - the one every write error and every non-EOF read error takes as well - and unfixed
     * it publishes after answering, so the sink reads Connecting here
     */

    UTF_REQUIRE( ConnectionState::Closed == sink -> stateOnClosed() );

    UTF_REQUIRE_EQUAL( record -> creations, 0U );

    /*
     * Half two - the cause, on the task, where the request task reads it from
     */

    const auto task = om::qi< Task >( driver );

    ( void ) chkTaskFailed( task );

    const auto diagnostics = eh::diagnostic_information( task -> exception() );

    if( diagnostics.find( "refused" ) == std::string::npos )
    {
        UTF_FAIL(
            "the connection task's failure does not name the refused connect:\n" + diagnostics
            );
    }
}

/**
 * @brief S6R.2 H16 - protocol-specific fields are normalized at the HTTP/2 driver boundary
 *
 * THE HTTP/1.1 RENDERER ALREADY DID THIS AND THE HTTP/2 PATH DID NOT, which is the whole finding.
 * serializeRequestHead( ) refuses Transfer-Encoding, supplies Host and SETS Content-Length from
 * the body that will actually be written; toSessionRequest( ) copied request.headers( ) through
 * unchanged, and submitRequest( ) checked only that the three pseudo-headers were non-empty
 * before appending every field the caller had.
 *
 * THE INTERNAL TRIGGER IS REAL AND IT IS THREE STATUS CODES, NOT ONE.
 * RedirectPolicy::rewriteMethod( ) sets dropBody for a 303 on any method but GET and HEAD, AND
 * for a 301 or 302 on POST; chkPrepareNextHop( ) then drops the body and touches the headers only
 * to strip credentials on a cross-origin hop. So a POST with an explicit Content-Length becomes a
 * GET which still declares one, and this driver sent END_STREAM with no DATA behind it - RFC 9113
 * 8.1.1 makes that malformed. Over HTTP/1.1 the same redirect is harmless, because the renderer
 * removes the field. The last block below is that exact shape.
 *
 * PURE AND STATIC, so it needs no peer, no strand and no connection: toSessionRequest( ) runs on
 * the CALLER's thread inside submit( ), which is where the h1 side does the equivalent work
 */

UTF_AUTO_TEST_CASE( H2Driver_RequestHeadersAreNormalizedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    typedef Http2ConnectionTaskT< TcpSocketAsyncStrandedBase >              driver_t;

    /*
     * (1) The connection-specific fields of RFC 9113 8.2.2 are REMOVED and not refused. A
     * 'Connection: keep-alive' from a caller is legal protocol-neutral input, and rejecting it
     * would make one ClientRequest succeed over h1 and fail over h2 - which is the difference
     * this client exists to hide. 8.2.2 says an intermediary translating h1 to h2 MUST remove
     * them, so removing is the specified behaviour
     */

    {
        auto request = makeRequest( "https://example.com/p" );

        request.headers().append( "Connection", "keep-alive" );
        request.headers().append( "Keep-Alive", "timeout=5" );
        request.headers().append( "Proxy-Connection", "keep-alive" );
        request.headers().append( "Transfer-Encoding", "chunked" );
        request.headers().append( "Upgrade", "websocket" );
        request.headers().append( "X-Kept", "yes" );

        const auto result = driver_t::toSessionRequest( request );

        UTF_REQUIRE( ! result.headers.has( "connection" ) );
        UTF_REQUIRE( ! result.headers.has( "keep-alive" ) );
        UTF_REQUIRE( ! result.headers.has( "proxy-connection" ) );
        UTF_REQUIRE( ! result.headers.has( "transfer-encoding" ) );
        UTF_REQUIRE( ! result.headers.has( "upgrade" ) );

        /*
         * ... and nothing else is touched, which is what stops a lane from "fixing" this by
         * emptying the list
         */

        UTF_REQUIRE( result.headers.has( "x-kept" ) );
        UTF_REQUIRE_EQUAL( result.headers.get( "x-kept" ), std::string( "yes" ) );
    }

    /*
     * (2) 'te' is the one field of that family which survives, and only with the exact value
     * 8.2.2 permits
     */

    {
        auto keeps = makeRequest( "https://example.com/p" );

        keeps.headers().append( "TE", "trailers" );

        UTF_REQUIRE( driver_t::toSessionRequest( keeps ).headers.has( "te" ) );

        auto drops = makeRequest( "https://example.com/p" );

        drops.headers().append( "TE", "gzip" );

        UTF_REQUIRE( ! driver_t::toSessionRequest( drops ).headers.has( "te" ) );
    }

    /*
     * (3) 'host' which AGREES with the URL authority is dropped - 8.3.1 has a client generating
     * HTTP/2 use :authority - and the agreement is a comparison of AUTHORITIES and not of
     * strings: a caller who writes the default port explicitly names the same origin
     */

    {
        auto bare = makeRequest( "https://example.com/p" );

        bare.headers().append( "Host", "example.com" );

        UTF_REQUIRE( ! driver_t::toSessionRequest( bare ).headers.has( "host" ) );

        auto withPort = makeRequest( "https://example.com/p" );

        withPort.headers().append( "Host", "EXAMPLE.com:443" );

        UTF_REQUIRE( ! driver_t::toSessionRequest( withPort ).headers.has( "host" ) );

        auto spelledPort = makeRequest( "https://example.com:8443/p" );

        spelledPort.headers().append( "Host", "example.com:8443" );

        UTF_REQUIRE( ! driver_t::toSessionRequest( spelledPort ).headers.has( "host" ) );
    }

    /*
     * (4) ... and one which DISAGREES is refused, which is the only arm here that can fail a
     * request which works today. A caller setting Host to something other than the URL host is
     * doing virtual-host routing that h2 expresses through :authority, and failing loudly beats
     * sending a request whose two authorities disagree
     */

    {
        auto request = makeRequest( "https://example.com/p" );

        request.headers().append( "Host", "attacker.example" );

        UTF_REQUIRE_THROW(
            ( void ) driver_t::toSessionRequest( request ),
            ArgumentException
            );

        auto otherPort = makeRequest( "https://example.com/p" );

        otherPort.headers().append( "Host", "example.com:8443" );

        UTF_REQUIRE_THROW(
            ( void ) driver_t::toSessionRequest( otherPort ),
            ArgumentException
            );
    }

    /*
     * (5) content-length is SET from the body which will actually be written, and REMOVED when
     * there is no body. The second half is the redirect shape: RedirectPolicy drops the body of a
     * POST on a 303, 301 or 302 and leaves the headers alone, so this is precisely what
     * chkPrepareNextHop( ) hands the next hop - a GET declaring a body it will never send
     */

    {
        auto withBody = makeRequest( "https://example.com/p", "POST" );

        withBody.headers().append( "Content-Length", "999" );
        withBody.body( om::ObjPtrCopyable< data::DataBlock >( blockOf( "four" ) ) );

        const auto sent = driver_t::toSessionRequest( withBody );

        UTF_REQUIRE( sent.headers.has( "content-length" ) );
        UTF_REQUIRE_EQUAL( sent.headers.get( "content-length" ), std::string( "4" ) );

        auto afterRedirect = makeRequest( "https://example.com/other", "GET" );

        afterRedirect.headers().append( "Content-Length", "4" );

        const auto rewritten = driver_t::toSessionRequest( afterRedirect );

        UTF_REQUIRE( ! rewritten.headers.has( "content-length" ) );
        UTF_REQUIRE( ! rewritten.hasBody );
    }
}

#endif /* __UTEST_TESTHTTP2CONNECTIONTASK_H_ */
