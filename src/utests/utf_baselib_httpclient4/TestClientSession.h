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

#ifndef __UTEST_TESTCLIENTSESSION_H_
#define __UTEST_TESTCLIENTSESSION_H_

#include <baselib/httpclient/ClientSession.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/HttpServerHelpers.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S6.1 - the client session (design 5.6 and 5.8)
 *
 * THE FIRST CASE IN THIS FILE IS THE MOST VALUABLE TEST IN THE FEATURE, and that is not a claim
 * about its assertions. Until it ran, the connection pool, the request task and a real driver had
 * never been composed in anything: S5.1 ran against a probe pool and a probe connection, S5.2
 * against stub connections, and S4.2's driver against a recording sink. Every one of the five
 * defects the L5 review found lived in that gap. So the order of work in this slice was the
 * end-to-end GET first and the session's surface afterwards
 *
 * WHAT IS REAL IN EVERY CASE BELOW: ClientSessionT over the stranded cleartext stream policy, the
 * ConnectionPoolImpl it builds, the Http2ConnectionTaskT its factory creates, the
 * HttpClientRequestTaskT its request task creates, and the in-process peer of design 8.2. Nothing
 * here stubs a layer of the client
 */

namespace utest
{
    namespace session
    {
        typedef bl::tasks::TcpSocketAsyncStrandedBase                           plain_stream_t;

        typedef bl::httpclient::ClientSessionImplT< plain_stream_t >            PlainSessionImpl;

        /**
         * @brief The peer of design 8.2, cleartext, on an ephemeral port
         */

        inline auto makePeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

        /**
         * @brief A session which speaks HTTP/2 over cleartext by prior knowledge (RFC 9113 3.3)
         *
         * Nothing negotiates a cleartext connection, so the protocol is the configuration's - the
         * same reason h2driver::cleartextHttp2Config( ) exists for the driver's own cases
         */

        inline auto makeHttp2SessionConfig() -> bl::httpclient::ClientSessionConfig
        {
            bl::httpclient::ClientSessionConfig config;

            config.connectionConfig = h2driver::cleartextHttp2Config();

            return config;
        }

        inline auto makeSession(
            SAA_in_opt      bl::httpclient::ClientSessionConfig                 config =
                                makeHttp2SessionConfig()
            )
            -> bl::om::ObjPtr< PlainSessionImpl >
        {
            return PlainSessionImpl::createInstance( BL_PARAM_FWD( config ) );
        }

        inline auto urlFor(
            SAA_in          const bl::os::port_t                                port,
            SAA_in_opt      const std::string&                                  target = "/"
            )
            -> std::string
        {
            return
                "http://127.0.0.1:" +
                bl::utils::lexical_cast< std::string >( port ) +
                target;
        }

        inline auto makeRequest(
            SAA_in          const bl::os::port_t                                port,
            SAA_in_opt      const std::string&                                  target = "/",
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( bl::cpp::copy( method ) );
            request.url( bl::net::Uri::parse( urlFor( port, target ) ) );

            return request;
        }

        /**
         * @brief Runs one session task to completion and hands the case the task back
         *
         * The queue keeps the task, so a case can look at how it ended; wait( ) is on the task
         * itself, which for a redirect chain is the whole chain - a continuation is the same queue
         * entry
         */

        inline void runSessionTask( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );
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

        /**
         * @brief The response of a request which was expected to succeed - and NOT an assertion
         * inside another assertion's argument
         *
         * Utf.h's own note says why that matters: UTF_FAIL( msg ) takes the globals lock and then
         * evaluates msg, and bl::os::mutex is not recursive
         */

        inline void requireTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            if( ! task -> isFailed() )
            {
                return;
            }

            UTF_FAIL(
                "the session request task failed: " +
                bl::eh::diagnostic_information( task -> exception() )
                );
        }

        /**
         * @brief Whether a failed task's exception is a TimeoutException
         *
         * A deadline which was reached is not a failure to be diagnosed, so the type is what a
         * case asserts on - the message is the request task's own and belongs to it
         */

        inline bool isTimeoutException( SAA_in const std::exception_ptr& eptr )
        {
            if( ! eptr )
            {
                return false;
            }

            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::TimeoutException& )
            {
                return true;
            }
            catch( std::exception& )
            {
            }

            return false;
        }

        /**
         * @brief Makes one request through the session, runs it to completion and requires it
         * succeeded - the shape most cases below want
         */

        inline auto runRequest(
            SAA_in          const bl::om::ObjPtr< PlainSessionImpl >&           session,
            SAA_in          const bl::httpclient::ClientRequest&                request,
            SAA_in_opt      const bl::om::ObjPtrCopyable< bl::httpclient::BodySink >& bodySink =
                                bl::om::ObjPtrCopyable< bl::httpclient::BodySink >()
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientRequestTask >
        {
            auto requestTask = session -> createRequestTask( request, bodySink );

            const auto task = bl::om::qi< bl::tasks::Task >( requestTask );

            runSessionTask( task );

            requireTaskSucceeded( task );

            return requestTask;
        }

        inline auto statsOf( SAA_in const bl::om::ObjPtr< PlainSessionImpl >& session )
            -> bl::httpclient::ConnectionPoolImpl::Stats
        {
            return bl::om::qi< bl::httpclient::ConnectionPoolImpl >( session -> pool() ) -> stats();
        }

        /**
         * @brief A peer which answers every request with its own path as the body
         */

        inline auto echoPathResponder() -> h2peer::responder_t
        {
            return []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
            {
                return h2peer::Http2ResponseScript()
                    .headers( 200U )
                    .data( request.path )
                    .endStream();
            };
        }

        /**
         * @brief What the peer received, as "name: value" lines in the order they arrived
         *
         * The pseudo-headers are left out because they are not what the session builds - the
         * driver derives them from the method and the URI (RFC 9113 8.3), and http::HeaderList
         * cannot even represent them
         */

        inline auto fieldsOf( SAA_in const h2peer::Http2TestRequest& request ) -> std::string
        {
            std::string result;

            for( std::size_t i = 0U; i < request.fields.size(); ++i )
            {
                const auto& name = request.fields[ i ].name();

                if( ! name.empty() && ':' == name[ 0 ] )
                {
                    continue;
                }

                result += name;
                result += ": ";
                result += request.fields[ i ].value();
                result += "\n";
            }

            return result;
        }

        /**
         * @brief A peer which answers every request with the request's own header fields
         */

        inline auto echoHeadersResponder() -> h2peer::responder_t
        {
            return []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
            {
                return h2peer::Http2ResponseScript()
                    .headers( 200U )
                    .data( fieldsOf( request ) )
                    .endStream();
            };
        }

        inline bool hasLine(
            SAA_in          const std::string&                                  text,
            SAA_in          const std::string&                                  line
            )
        {
            return std::string::npos != ( "\n" + text ).find( "\n" + line + "\n" );
        }

        inline std::size_t countLinesStartingWith(
            SAA_in          const std::string&                                  text,
            SAA_in          const std::string&                                  prefix
            )
        {
            std::size_t result = 0U;
            std::size_t pos = 0U;

            const auto padded = "\n" + text;
            const auto needle = "\n" + prefix;

            for( ; ; )
            {
                pos = padded.find( needle, pos );

                if( std::string::npos == pos )
                {
                    break;
                }

                ++result;
                ++pos;
            }

            return result;
        }

        /**
         * @brief A content decoder for the test-only coding "x-utest", which turns '~' into a space
         *
         * NOT A REAL COMPRESSOR, and it does not pretend to be one: no decoder ships with this
         * library (D9) and these cases are about the SEAM - that the REGISTERED codings decide
         * what accept-encoding says, and that a body in a registered coding is decoded before the
         * caller sees it
         */

        template
        <
            typename E = void
        >
        class UtestDecoderT : public bl::httpclient::ContentDecoder
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( UtestDecoderT, bl::httpclient::ContentDecoder )
            BL_CTR_DEFAULT( UtestDecoderT, protected )

        public:

            static const std::string& coding() NOEXCEPT
            {
                return g_coding;
            }

            virtual const std::string& contentCoding() const NOEXCEPT OVERRIDE
            {
                return g_coding;
            }

            virtual void write(
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&     input,
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                )
                OVERRIDE
            {
                std::string decoded(
                    input -> begin() + input -> offset1(),
                    input -> begin() + input -> size()
                    );

                for( std::size_t i = 0U; i < decoded.size(); ++i )
                {
                    if( '~' == decoded[ i ] )
                    {
                        decoded[ i ] = ' ';
                    }
                }

                output( h2driver::blockOf( decoded ) );
            }

            virtual void finish(
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                )
                OVERRIDE
            {
                BL_UNUSED( output );
            }

        private:

            static const std::string                                            g_coding;
        };

        BL_DEFINE_STATIC_CONST_STRING( UtestDecoderT, g_coding ) = "x-utest";

        typedef bl::om::ObjectImpl< UtestDecoderT<> >                           UtestDecoder;

        /**
         * @brief A header profile with a different default set per request kind
         */

        inline auto makeKindProfile() -> bl::httpclient::HeaderProfile
        {
            using namespace bl::httpclient;

            HeaderProfile profile;

            HeaderProfileForKind fetch;

            {
                ProfileHeader header;

                header.name = "x-kind";
                header.value = "fetch";
                fetch.defaultHeaders.push_back( header );

                header.name = "accept";
                header.value = "*/*";
                fetch.defaultHeaders.push_back( header );

                header.name = "accept-encoding";
                header.value = "<computed>";
                fetch.defaultHeaders.push_back( header );
            }

            HeaderProfileForKind navigation;

            {
                ProfileHeader header;

                header.name = "x-kind";
                header.value = "navigation";
                navigation.defaultHeaders.push_back( header );

                header.name = "accept";
                header.value = "text/html";
                navigation.defaultHeaders.push_back( header );
            }

            profile.byRequestKind[ HttpRequestKind::Fetch ] = fetch;
            profile.byRequestKind[ HttpRequestKind::Navigation ] = navigation;

            profile.acceptEncoding.push_back( "gzip" );
            profile.acceptEncoding.push_back( UtestDecoderT<>::coding() );

            return profile;
        }

        /**
         * @brief A caller's BodySink at the SESSION level, which counts its terminal callbacks
         *
         * NOTHING IN THIS SUITE INSTALLED ONE UNTIL S6R.3, which is why H08 was invisible: a sink
         * handed to createRequestTask( ) is carried to EVERY hop of the chain ( startHop( ) ), so
         * what a hop tells it is what the CALLER sees, and a chain of two hops used to tell it the
         * body was complete twice. The count is therefore the assertion, exactly as the request
         * task's own case counts credit
         *
         * It takes everything it is offered - the backpressure question is the request task's and
         * has its own cases there; what is under test here is which hop says what to it
         */

        template
        <
            typename E = void
        >
        class CountingBodySinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( CountingBodySinkT, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;

            std::string                                                         m_received;
            std::size_t                                                         m_completions;

            CountingBodySinkT() NOEXCEPT
                :
                m_completions( 0U )
            {
            }

        public:

            virtual std::size_t onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) OVERRIDE
            {
                const auto offered = data -> size() - data -> offset1();

                BL_MUTEX_GUARD( m_lock );

                m_received.append(
                    reinterpret_cast< const char* >( data -> pv() ) + data -> offset1(),
                    offered
                    );

                return offered;
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_completions;
            }

            auto received() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_received;
            }

            std::size_t completions() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_completions;
            }
        };

        typedef bl::om::ObjectImpl< CountingBodySinkT<> >                       CountingBodySink;

    } // session

} // utest

/**
 * @brief A GET over HTTP/2 through the session - the pool, the request task and a real driver,
 * composed for the first time
 *
 * WHAT THIS CASE IS FOR, beyond the two assertions on the body. Each of the following is a
 * property no module below could observe, because each of them needs two real components at once:
 *
 *   - the session's connection factory produces a connection the pool can use, and the pool's
 *     Connecting placeholder becomes a Ready connection without the establishment bound firing;
 *   - the pool dispatches the first request as the preface rider, so one request rides the
 *     opening write - the pool's UNCONFIRMED_MAX_CONCURRENT_STREAMS path against a driver which
 *     really does publish Ready before the peer's SETTINGS have arrived;
 *   - the request task's acquire( ) is answered with a real connection, its submit( ) reaches a
 *     real driver's mailbox, and the stream events come back over a real strand;
 *   - and the slot is released once, so the pool ends with nothing outstanding
 */

UTF_AUTO_TEST_CASE( ClientSession_GetOverHttp2Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
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
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_GetOverHttp2Tests"
                );

            const auto requestTask = runRequest( session, makeRequest( port, "/hello" ) );

            const auto& response = requestTask -> response();

            UTF_REQUIRE_EQUAL( response.status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( response ), std::string( "hello world" ) );
            UTF_REQUIRE_EQUAL(
                response.headers().get( "content-type" ),
                std::string( "text/plain" )
                );

            UTF_REQUIRE( httpclient::HttpProtocol::Http2 == response.protocol() );

            UTF_REQUIRE_EQUAL( requestTask -> redirectHops(), 0U );

            /*
             * The pool's own bookkeeping, which is the half of this case no driver test can see:
             * one dispatch, one release, and nothing outstanding on the connection afterwards
             */

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.failures.value(), 0U );

            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief Two requests, one connection - the pool learning the peer's limit from a real SETTINGS
 *
 * THE CONTROL FOR EVERY "A SECOND CONNECTION WAS OPENED" CASE BELOW. The pool dispatches the first
 * request as the preface rider, against UNCONFIRMED_MAX_CONCURRENT_STREAMS of one, and may
 * dispatch the second only once it has learnt the peer's real limit - which against a stub can
 * only be asserted, and here is the peer's own SETTINGS arriving over a socket
 */

UTF_AUTO_TEST_CASE( ClientSession_ConnectionIsReusedAcrossRequestsTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder( echoPathResponder() );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_ConnectionIsReusedAcrossRequestsTests"
                );

            const auto first = runRequest( session, makeRequest( port, "/one" ) );
            const auto second = runRequest( session, makeRequest( port, "/two" ) );

            UTF_REQUIRE_EQUAL( bodyOf( first -> response() ), std::string( "/one" ) );
            UTF_REQUIRE_EQUAL( bodyOf( second -> response() ), std::string( "/two" ) );

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 2U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 2U );

            /*
             * Both streams were opened on the SAME connection, which is what the peer sees as two
             * stream identifiers on one session - 1 and 3 (RFC 9113 5.1.1)
             */

            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 3U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A POST with a buffered body, and the peer measures what arrived
 */

UTF_AUTO_TEST_CASE( ClientSession_PostWithBodyOverHttp2Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const std::string upload( "{\"item\":\"value\"}" );

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                "POST" == request.method && request.hasBody,
                BL_MSG()
                    << "The peer expected a POST carrying a body and got "
                    << request.method
                );

            return h2peer::Http2ResponseScript()
                .headers( 201U )
                .data( "created" )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_PostWithBodyOverHttp2Tests"
                );

            auto request = makeRequest( port, "/items", "POST" );

            request.headers().append(
                std::string( "content-type" ),
                std::string( "application/json" )
                );

            request.body(
                om::ObjPtrCopyable< data::DataBlock >( h2driver::blockOf( upload ) )
                );

            const auto task = runRequest( session, request );

            UTF_REQUIRE_EQUAL( task -> response().status(), 201U );
            UTF_REQUIRE_EQUAL( bodyOf( task -> response() ), std::string( "created" ) );

            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( 1U ), upload );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A peer which says GOAWAY after one stream - the drained connection is retired, and the
 * next request opens a new one
 *
 * THIS IS THE CASE THE L5 SECOND PASS ASKED FOR. When the last stream of a draining connection
 * ends, the driver queues its GOAWAY in the SAME strand handler that posts the final onClosed, so
 * the pool's forget-cancel races that write. Against a stub it is a certainty in one direction;
 * this is the first time it is run as the race it is. What must hold either way is the pool's
 * verdict - the connection is gone and the next request does not go to it
 */

UTF_AUTO_TEST_CASE( ClientSession_GoAwayRetiresTheConnectionTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder( echoPathResponder() );
    peer -> setGoAwayAfterStreams( 1U );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_GoAwayRetiresTheConnectionTests"
                );

            const auto first = runRequest( session, makeRequest( port, "/one" ) );

            UTF_REQUIRE_EQUAL( bodyOf( first -> response() ), std::string( "/one" ) );

            const auto second = runRequest( session, makeRequest( port, "/two" ) );

            UTF_REQUIRE_EQUAL( bodyOf( second -> response() ), std::string( "/two" ) );

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 2U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 2U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 2U );

            /*
             * AT LEAST ONE, and not exactly one, for a reason the first run of this case showed:
             * setGoAwayAfterStreams( ) is a property of the PEER and applies to every connection
             * it accepts, so the second connection drains too. Whether its retirement has been
             * observed by the time the second response completes depends on the maintenance tick,
             * so the count here is 1 or 2 and an equality would be a coin toss. What IS
             * deterministic is connectionsCreated: the second request could not have used the
             * first connection
             */

            UTF_REQUIRE( stats.connectionsRetired.value() >= 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief ConnectionPoolPolicy::drainingReserve reaches the driver - obligation 1a
 *
 * A KNOB WHICH REACHES NOTHING IS THE SAME DEFECT ONE LAYER UP, and the reserve was exactly that
 * until the session's connection factory existed: ConnectionPoolPolicy::drainingReserve was read
 * by nothing at all. The assertion is therefore a BEHAVIOUR and not a configuration read-back -
 * with a reserve of "everything but one" the connection may open exactly one stream, publishes
 * Draining, is retired, and the second request has to open a second connection
 *
 * ClientSession_ConnectionIsReusedAcrossRequestsTests is the control: the same two requests under
 * the default reserve share one connection
 */

UTF_AUTO_TEST_CASE( ClientSession_DrainingReserveReachesTheDriverTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder( echoPathResponder() );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            auto config = makeHttp2SessionConfig();

            /*
             * A registry starts with ( MAX_STREAM_ID - 1 ) / 2 + 1 identifiers in hand, so a
             * reserve of ( MAX_STREAM_ID - 1 ) / 2 leaves exactly one above the margin
             */

            config.poolPolicy.drainingReserve = ( http2::Globals::MAX_STREAM_ID - 1U ) / 2U;

            const auto session = makeSession( std::move( config ) );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_DrainingReserveReachesTheDriverTests"
                );

            const auto first = runRequest( session, makeRequest( port, "/one" ) );
            const auto second = runRequest( session, makeRequest( port, "/two" ) );

            UTF_REQUIRE_EQUAL( bodyOf( first -> response() ), std::string( "/one" ) );
            UTF_REQUIRE_EQUAL( bodyOf( second -> response() ), std::string( "/two" ) );

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 2U );

            /*
             * The reserve applies to every connection the SESSION opens, so the second one is
             * draining too - see the note in the GOAWAY case above for why this is not an equality
             */

            UTF_REQUIRE( stats.connectionsRetired.value() >= 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief ConnectionPoolPolicy::idleTimeout reaches the driver - obligation 1b
 *
 * The other knob which reached nothing, and its owner is the same factory. Design 5.7 names the
 * POOL as the owner of the connection idle lifetime and the DRIVER as what enforces it, so the
 * proof is that an idle connection closes ITSELF: nothing in this case cancels it, and the peer
 * records the client closing the connection
 */

UTF_AUTO_TEST_CASE( ClientSession_IdleTimeoutReachesTheDriverTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder( echoPathResponder() );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            auto config = makeHttp2SessionConfig();

            config.poolPolicy.idleTimeout = time::milliseconds( 300 );

            const auto session = makeSession( std::move( config ) );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_IdleTimeoutReachesTheDriverTests"
                );

            const auto task = runRequest( session, makeRequest( port, "/one" ) );

            UTF_REQUIRE_EQUAL( bodyOf( task -> response() ), std::string( "/one" ) );

            /*
             * A rendezvous on the record and NOT a sleep: requireRecorded( ) waits on the peer's
             * own condition variable for the close it is about
             */

            h2driver::requireRecorded(
                peer -> recorder(),
                std::string( "the client closed the connection" )
                );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief The BodySource rule - obligation 2
 *
 * THE HTTP/1.1 DRIVER REFUSES EVERY REQUEST CARRYING A BodySource, and until this slice the
 * request was still dispatched there: it burned an attempt against maxRetriesPerRequest and could
 * land on another HTTP/1.1 connection next time. The session is the only layer which knows both
 * the request and what a connection for a key will speak, so the rule is in two halves and both
 * are here
 */

UTF_AUTO_TEST_CASE( ClientSession_BodySourceIsNeverCarriedByHttp11Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    /*
     * HALF ONE - a session whose transport can never produce an HTTP/2 connection refuses such a
     * request outright, before an attempt is spent. A cleartext session speaks what its
     * configuration says and nothing negotiates it, so this is decidable at the moment the
     * request is made
     */

    {
        bl::httpclient::ClientSessionConfig config;

        UTF_REQUIRE(
            httpclient::HttpProtocol::Http11 == config.connectionConfig.cleartextProtocol.value()
            );

        const auto session = makeSession( std::move( config ) );

        BL_SCOPE_EXIT_WARN_ON_FAILURE(
            {
                session -> dispose();
            },
            "utest::session::ClientSession_BodySourceIsNeverCarriedByHttp11Tests"
            );

        UTF_REQUIRE( ! session -> canCarryBodySource() );

        auto request = makeRequest( 8080U, "/upload", "PUT" );

        request.bodySource(
            om::ObjPtrCopyable< httpclient::BodySource >(
                om::qi< httpclient::BodySource >( h2driver::StubBodySource::createInstance() )
                )
            );

        UTF_REQUIRE_THROW( session -> createRequestTask( request ), NotSupportedException );

        /*
         * The same session takes the same request without the source
         */

        request.body( om::ObjPtrCopyable< data::DataBlock >( h2driver::blockOf( "payload" ) ) );

        UTF_REQUIRE( session -> createRequestTask( request ) );
    }

    /*
     * HALF TWO - a session whose transport NEGOTIATES routes such a request to a key of its own,
     * whose connections do not offer http/1.1 at all. The decision and the narrowing are pinned
     * here as what they are: two pure functions the connection factory joins
     */

    {
        httpclient::ConnectionKey templateKey;

        templateKey.http2ProfileId = "chrome";

        auto plain = makeRequest( 8080U, "/upload", "PUT" );

        auto streaming = plain;

        streaming.bodySource(
            om::ObjPtrCopyable< httpclient::BodySource >(
                om::qi< httpclient::BodySource >( h2driver::StubBodySource::createInstance() )
                )
            );

        const auto plainKey = httpclient::SessionHeaders::keyFor( plain, templateKey, true );
        const auto streamingKey = httpclient::SessionHeaders::keyFor( streaming, templateKey, true );

        UTF_REQUIRE( ! httpclient::SessionHeaders::isH2OnlyKey( plainKey ) );
        UTF_REQUIRE( httpclient::SessionHeaders::isH2OnlyKey( streamingKey ) );

        UTF_REQUIRE( plainKey != streamingKey );
        UTF_REQUIRE_EQUAL( plainKey.http2ProfileId, std::string( "chrome" ) );

        /*
         * And a session which cannot produce HTTP/1.1 at all does not split the key, because the
         * routing would buy nothing and a second connection is not free
         */

        UTF_REQUIRE(
            ! httpclient::SessionHeaders::isH2OnlyKey(
                httpclient::SessionHeaders::keyFor( streaming, templateKey, false )
                )
            );

        /*
         * What the factory does with such a key: a peer may select only from what it was offered
         * (RFC 7301 3.1), so an offer of "h2" alone settles the protocol before a byte is spoken
         */

        tasks::ClientConnectionConfig config;

        UTF_REQUIRE_EQUAL( config.alpnOffer.size(), 2U );

        PlainSessionImpl::narrowToHttp2( config );

        UTF_REQUIRE_EQUAL( config.alpnOffer.size(), 1U );
        UTF_REQUIRE_EQUAL( config.alpnOffer[ 0 ], std::string( "h2" ) );
        UTF_REQUIRE(
            httpclient::HttpProtocol::Http2 == config.cleartextProtocol.value()
            );
    }
}

/**
 * @brief One session speaks one scheme, and says so rather than connecting cleartext to a TLS port
 */

UTF_AUTO_TEST_CASE( ClientSession_SpeaksOneSchemeTests )
{
    using namespace bl;
    using namespace utest;
    using namespace utest::session;

    const auto session = makeSession();

    BL_SCOPE_EXIT_WARN_ON_FAILURE(
        {
            session -> dispose();
        },
        "utest::session::ClientSession_SpeaksOneSchemeTests"
        );

    UTF_REQUIRE_EQUAL( session -> transportScheme(), std::string( "http" ) );

    auto request = makeRequest( 8080U );

    request.url( net::Uri::parse( "https://example.com/resource" ) );

    UTF_REQUIRE_THROW( session -> createRequestTask( request ), NotSupportedException );

    /*
     * And a relative reference has no host, so it has no key either - the refusal
     * ConnectionKey::fromUri( ) exists for
     */

    request.url( net::Uri::parse( "/resource" ) );

    UTF_REQUIRE_THROW( session -> createRequestTask( request ), ArgumentException );
}

/**
 * @brief ... AND AT THE OTHER ENTRY POINT A URL HAS, which is the redirect target
 *
 * THE CASE ABOVE IS ONLY HALF THE RULE. A session is given a URL twice: once by its caller, which
 * createRequestTask( ) checks, and once by the SERVER, in a Location. RedirectPolicy refuses only
 * the https-to-http downgrade, so http to https - the commonest redirect on the web - arrives at
 * the session as something it must decide about, and a session whose only stream policy is
 * cleartext would build an https key, connect a CLEARTEXT socket to port 443 and write the request
 * head in the clear. The head carries the Cookie field the jar computed for the https target, so
 * the credential the Secure attribute exists to protect is what goes out in the clear
 *
 * TWO PEERS, and the second one exists to NOT BE CONTACTED: the https target is a peer of its own,
 * so "no cleartext went to the https origin" is an assertion about that peer and not an inference.
 * The Secure cookie is real too - the origin sets it on the 3xx, and the jar is asked, at the end,
 * for what it WOULD have sent to the https target. Against the unfixed session every one of the
 * four assertions below flips: the chain follows, the target peer records the request, the cookie
 * is in it, and the pool has made a second connection
 */

UTF_AUTO_TEST_CASE( ClientSession_CrossSchemeRedirectIsRefusedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto target = makePeer();

    target -> setResponder( echoHeadersResponder() );

    h2driver::withPeer(
        target,
        [ & ]( SAA_in const unsigned short targetPort ) -> void
        {
            const auto httpsTarget =
                "https://127.0.0.1:" +
                utils::lexical_cast< std::string >( targetPort ) +
                "/final";

            const auto origin = makePeer();

            origin -> setResponder(
                [ httpsTarget ]( SAA_in const h2peer::Http2TestRequest& )
                    -> h2peer::Http2ResponseScript
                {
                    http2::HpackFieldList fields;

                    fields.push_back(
                        http2::HpackField(
                            std::string( "location" ),
                            cpp::copy( httpsTarget )
                            )
                        );

                    /*
                     * An http response CAN set a Secure cookie in this jar - the 6265bis rule
                     * which forbids it is not implemented, and design 5.6 records that
                     */

                    fields.push_back(
                        http2::HpackField(
                            std::string( "set-cookie" ),
                            std::string( "sid=secret; Path=/; Secure" )
                            )
                        );

                    return h2peer::Http2ResponseScript()
                        .headers( 302U, fields, true /* endStream */ );
                }
                );

            h2driver::withPeer(
                origin,
                [ & ]( SAA_in const unsigned short originPort ) -> void
                {
                    const auto session = makeSession();

                    BL_SCOPE_EXIT_WARN_ON_FAILURE(
                        {
                            session -> dispose();
                        },
                        "utest::session::ClientSession_CrossSchemeRedirectIsRefusedTests"
                        );

                    session -> redirectPolicy().isEnabled( true );

                    const auto task = runRequest( session, makeRequest( originPort, "/start" ) );

                    /*
                     * Refused and REPORTED, which is what every other refusal in the policy does
                     * and what this client does with a 3xx it does not follow: the caller keeps
                     * the status and the Location, and can re-issue it on a session of the right
                     * scheme. A Location is the server's, so it may not raise
                     */

                    UTF_REQUIRE_EQUAL( task -> response().status(), 302U );
                    UTF_REQUIRE_EQUAL( task -> redirectHops(), 0U );

                    UTF_REQUIRE_EQUAL(
                        task -> response().headers().get( "location" ),
                        httpsTarget
                        );

                    /*
                     * Nothing was connected to the https origin, by either measure
                     */

                    UTF_REQUIRE( target -> recorder().records().empty() );

                    UTF_REQUIRE_EQUAL( statsOf( session ).connectionsCreated.value(), 1U );

                    /*
                     * And the credential this is about was in the jar and in scope for the target
                     * all along - so what did not happen is the only reason it stayed in
                     */

                    UTF_REQUIRE_EQUAL( session -> cookieJar().size(), 1U );

                    UTF_REQUIRE_EQUAL(
                        session -> cookieJar().cookieHeaderValue( net::Uri::parse( httpsTarget ) ),
                        std::string( "sid=secret" )
                        );

                    UTF_REQUIRE(
                        session -> cookieJar()
                            .cookieHeaderValue( net::Uri::parse( urlFor( targetPort, "/final" ) ) )
                            .empty()
                        );

                    UTF_REQUIRE( origin -> recorder().failure().empty() );
                    UTF_REQUIRE( target -> recorder().failure().empty() );
                }
                );
        }
        );
}

/**
 * @brief A same-origin redirect, followed - and the second hop is a second request task
 *
 * The chain is a WrapperTaskBase continuation, so what the caller scheduled is one task and what
 * ran is two HttpClientRequestTaskT of design 5.3 - which is what keeps that class one request
 * over one connection
 */

UTF_AUTO_TEST_CASE( ClientSession_RedirectIsFollowedWhenEnabledTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( "/start" == request.path )
            {
                http2::HpackFieldList fields;

                fields.push_back(
                    http2::HpackField( std::string( "location" ), std::string( "/final" ) )
                    );

                return h2peer::Http2ResponseScript()
                    .headers( 302U, fields, true /* endStream */ );
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( request.path )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_RedirectIsFollowedWhenEnabledTests"
                );

            /*
             * OFF BY DEFAULT, as design 5.6 requires and as the existing client behaves: the 3xx
             * is reported to the caller rather than followed
             */

            const auto reported = runRequest( session, makeRequest( port, "/start" ) );

            UTF_REQUIRE_EQUAL( reported -> response().status(), 302U );
            UTF_REQUIRE_EQUAL( reported -> redirectHops(), 0U );

            session -> redirectPolicy().isEnabled( true );

            const auto followed = runRequest( session, makeRequest( port, "/start" ) );

            UTF_REQUIRE_EQUAL( followed -> response().status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( followed -> response() ), std::string( "/final" ) );
            UTF_REQUIRE_EQUAL( followed -> redirectHops(), 1U );

            /*
             * request( ) is the request as it was FINALLY sent, which is the second hop's
             */

            UTF_REQUIRE_EQUAL( followed -> request().url().path(), std::string( "/final" ) );

            /*
             * Both hops went over the one connection the pool already had
             */

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 3U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 3U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief THE BUDGET IS THE REQUEST'S, not each hop's - design 5.7's request-total row
 *
 * Every hop and every retry is a fresh HttpClientRequestTaskT which arms its own FULL total timer,
 * so left alone a session request has no deadline at all: four attempts of twenty hops is thirty
 * minutes times eighty. The session therefore computes the deadline once and gives each hop what
 * is left of it
 *
 * HOW THE NUMBERS DISCRIMINATE, which is the whole of this case. The peer takes HOP_DELAY to
 * answer EITHER path, and the caller budgets the request at BUDGET, with HOP_DELAY < BUDGET <
 * 2 * HOP_DELAY. So the first hop fits with a second to spare and the second hop cannot fit at
 * all - unless it is handed a fresh budget of its own, which is exactly what the unfixed session
 * does and what makes this case go green against it, with a 200 and no failure
 *
 * AND WHY IT ASSERTS THE HOP COUNT AS WELL AS THE FAILURE: a machine slow enough to make the
 * FIRST hop miss the budget would fail the request too, for the wrong reason. redirectHops( ) is
 * one exactly when the chain got past the redirect, so that run fails the case loudly instead of
 * passing it vacuously
 *
 * It pins the per-request override on the way past: the session default is thirty minutes, so
 * nothing here would be bounded by anything if ClientRequest::totalTimeout( ) did not reach the hop
 */

UTF_AUTO_TEST_CASE( ClientSession_RequestBudgetIsChainedAcrossHopsTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    static constexpr long HOP_DELAY_IN_MILLISECONDS = 2000;
    const long BUDGET_IN_MILLISECONDS = 3000;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request )
            -> h2peer::Http2ResponseScript
        {
            if( "/start" == request.path )
            {
                http2::HpackFieldList fields;

                fields.push_back(
                    http2::HpackField( std::string( "location" ), std::string( "/final" ) )
                    );

                return h2peer::Http2ResponseScript()
                    .delay( HOP_DELAY_IN_MILLISECONDS )
                    .headers( 302U, fields, true /* endStream */ );
            }

            return h2peer::Http2ResponseScript()
                .delay( HOP_DELAY_IN_MILLISECONDS )
                .headers( 200U )
                .data( request.path )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_RequestBudgetIsChainedAcrossHopsTests"
                );

            session -> redirectPolicy().isEnabled( true );

            auto request = makeRequest( port, "/start" );

            request.totalTimeout( time::milliseconds( BUDGET_IN_MILLISECONDS ) );

            const auto requestTask = session -> createRequestTask( request );

            const auto task = om::qi< tasks::Task >( requestTask );

            runSessionTask( task );

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE( isTimeoutException( task -> exception() ) );

            /*
             * The chain DID follow the redirect - so what ran out of budget is the second hop and
             * not the first
             */

            UTF_REQUIRE_EQUAL( requestTask -> redirectHops(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A cross-origin redirect drops the credentials, and a POST is rewritten to a bodiless GET
 *
 * TWO PEERS AND NOT ONE, because an origin is scheme, host AND port (net::Uri::origin), so two
 * loopback peers on two ephemeral ports are two origins - which is what makes this a real
 * cross-origin hop rather than a simulated one
 */

UTF_AUTO_TEST_CASE( ClientSession_CrossOriginRedirectDropsCredentialsTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto target = makePeer();

    target -> setResponder( echoHeadersResponder() );

    h2driver::withPeer(
        target,
        [ & ]( SAA_in const unsigned short targetPort ) -> void
        {
            const auto origin = makePeer();

            origin -> setResponder(
                [ targetPort ]( SAA_in const h2peer::Http2TestRequest& request )
                    -> h2peer::Http2ResponseScript
                {
                    BL_CHK(
                        false,
                        "POST" == request.method,
                        BL_MSG()
                            << "The first hop was expected to be a POST and was "
                            << request.method
                        );

                    http2::HpackFieldList fields;

                    fields.push_back(
                        http2::HpackField(
                            std::string( "location" ),
                            urlFor( targetPort, "/moved" )
                            )
                        );

                    return h2peer::Http2ResponseScript()
                        .headers( 303U, fields, true /* endStream */ );
                }
                );

            h2driver::withPeer(
                origin,
                [ & ]( SAA_in const unsigned short originPort ) -> void
                {
                    const auto session = makeSession();

                    BL_SCOPE_EXIT_WARN_ON_FAILURE(
                        {
                            session -> dispose();
                        },
                        "utest::session::ClientSession_CrossOriginRedirectDropsCredentialsTests"
                        );

                    session -> redirectPolicy().isEnabled( true );

                    auto request = makeRequest( originPort, "/start", "POST" );

                    request.headers().append(
                        std::string( "authorization" ),
                        std::string( "Bearer secret" )
                        );

                    request.headers().append(
                        std::string( "cookie" ),
                        std::string( "sid=fromcaller" )
                        );

                    request.headers().append(
                        std::string( "x-kept" ),
                        std::string( "yes" )
                        );

                    request.body(
                        om::ObjPtrCopyable< data::DataBlock >( h2driver::blockOf( "payload" ) )
                        );

                    const auto task = runRequest( session, request );

                    UTF_REQUIRE_EQUAL( task -> redirectHops(), 1U );
                    UTF_REQUIRE_EQUAL( task -> response().status(), 200U );

                    /*
                     * 303 rewrites to GET and drops the body, for every method but HEAD
                     */

                    UTF_REQUIRE_EQUAL( task -> request().method(), std::string( "GET" ) );
                    UTF_REQUIRE( ! task -> request().hasBody() );

                    const auto received = bodyOf( task -> response() );

                    UTF_REQUIRE( hasLine( received, "x-kept: yes" ) );

                    UTF_REQUIRE_EQUAL( countLinesStartingWith( received, "authorization:" ), 0U );
                    UTF_REQUIRE_EQUAL( countLinesStartingWith( received, "cookie:" ), 0U );

                    /*
                     * Two origins are two keys, so two connections - the property that makes this
                     * hop cross-origin in the first place
                     */

                    UTF_REQUIRE_EQUAL( statsOf( session ).connectionsCreated.value(), 2U );

                    UTF_REQUIRE( origin -> recorder().failure().empty() );
                    UTF_REQUIRE( target -> recorder().failure().empty() );
                }
                );
        }
        );
}

/**
 * @brief The cookie jar round trip, and THE ONE Cookie FIELD - RFC 6265 5.4
 *
 * THE MERGE IS THE POINT AND THE REDIRECT IS WHERE THE TWO MEET. On a SAME-ORIGIN hop the caller's
 * own Cookie header survives dropCredentialHeaders( ) while the jar recomputes for the new target,
 * so both are in scope at once - and a request which carried both as two fields would be
 * malformed. So the second half of this case is a redirect, and the assertion is a COUNT
 */

UTF_AUTO_TEST_CASE( ClientSession_CookiesAreStoredMergedAndSentOnceTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( "/set" == request.path )
            {
                http2::HpackFieldList fields;

                fields.push_back(
                    http2::HpackField(
                        std::string( "set-cookie" ),
                        std::string( "sid=fromserver; Path=/" )
                        )
                    );

                fields.push_back(
                    http2::HpackField(
                        std::string( "set-cookie" ),
                        std::string( "theme=dark; Path=/" )
                        )
                    );

                return h2peer::Http2ResponseScript()
                    .headers( 200U, fields )
                    .data( "stored" )
                    .endStream();
            }

            if( "/start" == request.path )
            {
                http2::HpackFieldList fields;

                fields.push_back(
                    http2::HpackField( std::string( "location" ), std::string( "/final" ) )
                    );

                return h2peer::Http2ResponseScript()
                    .headers( 302U, fields, true /* endStream */ );
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( fieldsOf( request ) )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_CookiesAreStoredMergedAndSentOnceTests"
                );

            UTF_REQUIRE_EQUAL( session -> cookieJar().size(), 0U );

            ( void ) runRequest( session, makeRequest( port, "/set" ) );

            UTF_REQUIRE_EQUAL( session -> cookieJar().size(), 2U );

            /*
             * The jar's own cookies, on a request the caller added nothing to
             */

            const auto plain = runRequest( session, makeRequest( port, "/echo" ) );

            const auto plainFields = bodyOf( plain -> response() );

            UTF_REQUIRE_EQUAL( countLinesStartingWith( plainFields, "cookie:" ), 1U );
            UTF_REQUIRE( hasLine( plainFields, "cookie: sid=fromserver; theme=dark" ) );

            /*
             * And the merge, across a same-origin hop: the caller's own pair wins for the name
             * they share, the jar's other cookie follows, and there is still exactly ONE field
             */

            session -> redirectPolicy().isEnabled( true );

            auto request = makeRequest( port, "/start" );

            request.headers().append( std::string( "cookie" ), std::string( "sid=fromcaller" ) );

            const auto merged = runRequest( session, request );

            const auto mergedFields = bodyOf( merged -> response() );

            UTF_REQUIRE_EQUAL( merged -> redirectHops(), 1U );
            UTF_REQUIRE_EQUAL( countLinesStartingWith( mergedFields, "cookie:" ), 1U );
            UTF_REQUIRE( hasLine( mergedFields, "cookie: sid=fromcaller; theme=dark" ) );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A cookie name is CASE SENSITIVE, so the merge may only drop a jar cookie the caller
 * named byte for byte - RFC 6265 section 4.1.1, and the jar's own identity comparison
 */

UTF_AUTO_TEST_CASE( ClientSession_CookieMergeComparesNamesExactlyTests )
{
    using namespace bl;
    using namespace bl::httpclient;

    /*
     * THE JAR ALREADY STORES sid AND SID AS TWO COOKIES and emits both, because its own identity
     * comparison is byte-exact - so a case-insensitive merge is not a second opinion about
     * cookie identity, it is the merge losing one of two cookies the jar deliberately kept. A
     * caller's "sid" would suppress a session cookie named "SID" and the request would go out
     * without it, which reads to the origin as a signed-out client
     */

    http::HeaderList caller;

    caller.append( std::string( "cookie" ), std::string( "sid=fromcaller" ) );

    const auto merged = SessionHeaders::buildRequestHeaders(
        caller,
        nullptr /* kindProfile */,
        std::vector< std::string >() /* profileCodings */,
        std::vector< std::string >() /* registeredCodings */,
        false /* isStrict */,
        "SID=fromjar; theme=dark" /* jarCookieValue */
        );

    const auto* const value = merged.tryGet( std::string( "cookie" ) );

    UTF_REQUIRE( value );
    UTF_REQUIRE_EQUAL( *value, std::string( "sid=fromcaller; SID=fromjar; theme=dark" ) );

    /*
     * ... and a name they really do share is still the caller's, which is the behaviour the
     * exact comparison must not lose
     */

    const auto overridden = SessionHeaders::buildRequestHeaders(
        caller,
        nullptr /* kindProfile */,
        std::vector< std::string >() /* profileCodings */,
        std::vector< std::string >() /* registeredCodings */,
        false /* isStrict */,
        "sid=fromjar; theme=dark" /* jarCookieValue */
        );

    const auto* const overriddenValue = overridden.tryGet( std::string( "cookie" ) );

    UTF_REQUIRE( overriddenValue );
    UTF_REQUIRE_EQUAL( *overriddenValue, std::string( "sid=fromcaller; theme=dark" ) );
}

/**
 * @brief accept-encoding is the profile's list intersected with the REGISTERED decoders, and
 * strict mode sends the profile's list and hands back the raw body - design 6.5
 */

UTF_AUTO_TEST_CASE( ClientSession_AcceptEncodingAndStrictDecodeTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( "/coded" == request.path )
            {
                http2::HpackFieldList fields;

                fields.push_back(
                    http2::HpackField(
                        std::string( "content-encoding" ),
                        std::string( "x-utest" )
                        )
                    );

                return h2peer::Http2ResponseScript()
                    .headers( 200U, fields )
                    .data( "hello~world" )
                    .endStream();
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( fieldsOf( request ) )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            /*
             * WITH NO PROFILE AND NO DECODER the header is not sent at all, which is what every
             * request of this library does today (SimpleHttpTask sends no accept-encoding either)
             */

            {
                const auto session = makeSession();

                BL_SCOPE_EXIT_WARN_ON_FAILURE(
                    { session -> dispose(); },
                    "utest::session::ClientSession_AcceptEncodingAndStrictDecodeTests"
                    );

                const auto task = runRequest( session, makeRequest( port, "/echo" ) );

                UTF_REQUIRE_EQUAL(
                    countLinesStartingWith( bodyOf( task -> response() ), "accept-encoding:" ),
                    0U
                    );
            }

            /*
             * WITH A PROFILE AND NO DECODER the header is still omitted - the intersection is
             * empty, and design 6.5 says the deviation is reported rather than the header faked
             */

            {
                auto config = makeHttp2SessionConfig();

                config.headerProfile = makeKindProfile();

                const auto session = makeSession( std::move( config ) );

                BL_SCOPE_EXIT_WARN_ON_FAILURE(
                    { session -> dispose(); },
                    "utest::session::ClientSession_AcceptEncodingAndStrictDecodeTests"
                    );

                const auto task = runRequest( session, makeRequest( port, "/echo" ) );

                UTF_REQUIRE_EQUAL(
                    countLinesStartingWith( bodyOf( task -> response() ), "accept-encoding:" ),
                    0U
                    );
            }

            /*
             * WITH A DECODER REGISTERED the header carries exactly what can be decoded, in the
             * PROFILE's order - the order is the fingerprint, and the registry's order is an
             * artefact of who registered first - and a body in that coding is decoded before the
             * caller sees it, with its content-encoding removed because it is no longer true
             */

            {
                auto config = makeHttp2SessionConfig();

                config.headerProfile = makeKindProfile();

                const auto session = makeSession( std::move( config ) );

                BL_SCOPE_EXIT_WARN_ON_FAILURE(
                    { session -> dispose(); },
                    "utest::session::ClientSession_AcceptEncodingAndStrictDecodeTests"
                    );

                session -> decoders().registerDecoder(
                    UtestDecoderT<>::coding(),
                    []() -> om::ObjPtr< httpclient::ContentDecoder >
                    {
                        return om::qi< httpclient::ContentDecoder >(
                            UtestDecoder::createInstance()
                            );
                    }
                    );

                const auto echoed = runRequest( session, makeRequest( port, "/echo" ) );

                UTF_REQUIRE(
                    hasLine( bodyOf( echoed -> response() ), "accept-encoding: x-utest" )
                    );

                const auto coded = runRequest( session, makeRequest( port, "/coded" ) );

                UTF_REQUIRE_EQUAL(
                    bodyOf( coded -> response() ),
                    std::string( "hello world" )
                    );

                UTF_REQUIRE( ! coded -> response().headers().has( "content-encoding" ) );
            }

            /*
             * STRICT MODE sends the profile's list EXACTLY - including the coding no decoder
             * exists for - and hands the body back in whatever coding the server chose, which is
             * what a caller who decodes it themselves needs
             */

            {
                auto config = makeHttp2SessionConfig();

                config.headerProfile = makeKindProfile();
                config.isStrictContentEncoding = true;

                const auto session = makeSession( std::move( config ) );

                BL_SCOPE_EXIT_WARN_ON_FAILURE(
                    { session -> dispose(); },
                    "utest::session::ClientSession_AcceptEncodingAndStrictDecodeTests"
                    );

                session -> decoders().registerDecoder(
                    UtestDecoderT<>::coding(),
                    []() -> om::ObjPtr< httpclient::ContentDecoder >
                    {
                        return om::qi< httpclient::ContentDecoder >(
                            UtestDecoder::createInstance()
                            );
                    }
                    );

                const auto echoed = runRequest( session, makeRequest( port, "/echo" ) );

                UTF_REQUIRE(
                    hasLine( bodyOf( echoed -> response() ), "accept-encoding: gzip, x-utest" )
                    );

                const auto coded = runRequest( session, makeRequest( port, "/coded" ) );

                UTF_REQUIRE_EQUAL(
                    bodyOf( coded -> response() ),
                    std::string( "hello~world" )
                    );

                UTF_REQUIRE_EQUAL(
                    coded -> response().headers().get( "content-encoding" ),
                    std::string( "x-utest" )
                    );
            }

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief The profile's header set is applied BY REQUEST KIND, and a caller's header keeps the
 * profile's position - design 6.5
 *
 * PLACEMENT IS PART OF THE FINGERPRINT, so a caller header which the browser also sends belongs
 * where the browser sends it, and one it does not send goes where the profile says caller headers
 * go. Both are asserted on the bytes the peer received, in order
 */

UTF_AUTO_TEST_CASE( ClientSession_ProfileHeaderSetIsByRequestKindTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    peer -> setResponder( echoHeadersResponder() );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            auto config = makeHttp2SessionConfig();

            config.headerProfile = makeKindProfile();

            const auto session = makeSession( std::move( config ) );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_ProfileHeaderSetIsByRequestKindTests"
                );

            /*
             * Fetch is the default kind of a ClientRequest
             */

            auto fetch = makeRequest( port, "/fetch" );

            fetch.headers().append( std::string( "x-caller" ), std::string( "1" ) );

            const auto fetched = runRequest( session, fetch );

            UTF_REQUIRE_EQUAL(
                bodyOf( fetched -> response() ),
                std::string( "x-kind: fetch\naccept: */*\nx-caller: 1\n" )
                );

            /*
             * The same session, the same caller header, a different kind - a different set
             */

            auto navigation = makeRequest( port, "/navigate" );

            navigation.kind( httpclient::HttpRequestKind::Navigation );
            navigation.headers().append( std::string( "x-caller" ), std::string( "1" ) );

            const auto navigated = runRequest( session, navigation );

            UTF_REQUIRE_EQUAL(
                bodyOf( navigated -> response() ),
                std::string( "x-kind: navigation\naccept: text/html\nx-caller: 1\n" )
                );

            /*
             * A caller header the PROFILE also sends takes the caller's value in the PROFILE's
             * position, and is not sent twice
             */

            auto override = makeRequest( port, "/override" );

            override.headers().append( std::string( "accept" ), std::string( "application/json" ) );

            const auto overridden = runRequest( session, override );

            UTF_REQUIRE_EQUAL(
                bodyOf( overridden -> response() ),
                std::string( "x-kind: fetch\naccept: application/json\n" )
                );

            /*
             * PROXY CREDENTIALS ARE SESSION CONFIGURATION AND NEVER A REQUEST HEADER. RFC 9110
             * 11.7.1 makes Proxy-Authorization hop-by-hop, so a caller-supplied one on a request
             * which goes through a tunnel would be a credential sent to the ORIGIN. The session
             * drops it, which is also what makes RedirectPolicy::dropCredentialHeaders( )'s
             * removal of the same field a no-op by construction
             */

            auto proxied = makeRequest( port, "/proxied" );

            proxied.headers().append(
                std::string( "proxy-authorization" ),
                std::string( "Basic c2VjcmV0" )
                );

            const auto sent = runRequest( session, proxied );

            UTF_REQUIRE_EQUAL(
                countLinesStartingWith( bodyOf( sent -> response() ), "proxy-authorization:" ),
                0U
                );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief GET and POST over HTTP/1.1 through the session, against the library's own HttpServer
 *
 * THE OTHER HALF OF THE ACCEPT CRITERION, and a second composition nobody had run: this is the
 * ALPN FALLBACK reached over cleartext. The session's connection factory builds an
 * Http2ConnectionTaskT as it always does; the connection is configured HTTP/1.1, so the task hands
 * the connected stream to the driver factory the session registered, which builds
 * Http1ConnectionTaskT - and from that moment the DRIVER is the connection while the task's own
 * state( ) reads Closed. The pool has to prefer the accessor's driver over the task it scheduled,
 * which is design 5.4's rule and cost the L4 review a defect to learn
 *
 * HttpServer puts 'Connection: close' on every response it builds and does not implement
 * persistent connections (Response.h says so), so it proves the request/response path and the
 * NEGATIVE half of reuse against something which is not a fake: each request costs a connection
 */

UTF_AUTO_TEST_CASE( ClientSession_AgainstTheLibraryHttpServerTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            const auto port = static_cast< os::port_t >( test::UtfArgsParser::port() );

            const auto url =
                "http://" +
                test::UtfArgsParser::host() +
                ":" +
                utils::lexical_cast< std::string >( port ) +
                utest::http::g_requestUri;

            /*
             * A DEFAULT configuration, which is HTTP/1.1 over cleartext - nothing negotiates a
             * cleartext connection, so the protocol is the configuration's
             */

            const auto session = makeSession( httpclient::ClientSessionConfig() );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_AgainstTheLibraryHttpServerTests"
                );

            UTF_REQUIRE( ! session -> canCarryBodySource() );

            httpclient::ClientRequest request;

            request.method( "GET" );
            request.url( net::Uri::parse( url ) );

            request.headers().append(
                std::string( "user-agent" ),
                std::string( "utf-baselib-httpclient4" )
                );

            const auto fetched = runRequest( session, request );

            UTF_REQUIRE_EQUAL( fetched -> response().status(), 200U );
            UTF_REQUIRE_EQUAL(
                bodyOf( fetched -> response() ),
                utest::http::g_desiredResult
                );

            UTF_REQUIRE(
                httpclient::HttpProtocol::Http11 == fetched -> response().protocol()
                );

            /*
             * The server echoes the user-agent it received, so this is the session's own header
             * set arriving at a real HTTP/1.1 parser rather than at a script
             */

            UTF_REQUIRE_EQUAL(
                fetched -> response().headers().get( "request-user-agent-id" ),
                std::string( "utf-baselib-httpclient4" )
                );

            /*
             * And a POST with a body over the same path
             */

            httpclient::ClientRequest posted;

            posted.method( "POST" );
            posted.url( net::Uri::parse( url ) );
            posted.body(
                om::ObjPtrCopyable< data::DataBlock >( h2driver::blockOf( "payload" ) )
                );

            const auto created = runRequest( session, posted );

            UTF_REQUIRE_EQUAL( created -> response().status(), 200U );
            UTF_REQUIRE_EQUAL(
                bodyOf( created -> response() ),
                utest::http::g_desiredResult
                );

            /*
             * THE NEGATIVE HALF OF REUSE. Two requests, two connections, because the server said
             * 'Connection: close' both times - against the scripted HTTP/2 peer the same two
             * requests shared one
             */

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 2U );

            /*
             * ONE DISPATCH PER REQUEST, AND THIS NUMBER IS THE FINDING THIS CASE MADE TWICE OVER.
             * It was FOUR for these two requests until L6 finding 4a landed: the pool dispatched
             * the first request of a key onto the Connecting placeholder so that its HEADERS could
             * ride the preface (design 5.4), that placeholder was the HTTP/2 task, and on a
             * connection which turns out to speak HTTP/1.1 it handed the connected stream to the
             * HTTP/1.1 driver and completed - answering the rider it was holding with
             * connection_aborted, correctly flagged retryable because not a byte of it was
             * written. The session then replayed it onto the driver, and that replay is a RETRY,
             * so every first request to an HTTP/1.1 origin spent one from a budget meant for
             * network faults (astra H21)
             *
             * THE RIDER NOW RIDES ONLY WHERE THE PROTOCOL IS GENUINELY UNDECIDED, which is ALPN
             * and nothing else: a cleartext connection speaks what its configuration says, this
             * session's says HTTP/1.1, so ConnectionPoolPolicy::ridePreface is off here and the
             * request waits the few milliseconds for the driver rather than being thrown at a
             * placeholder which cannot carry it. The bounce and the replay themselves are
             * UNCHANGED and are exercised where they are real -
             * ClientSessionTls_Http11FallbackExchangeTests, over TLS, in utf_baselib_httpclient5
             */

            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 2U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 2U );
        }
        );
}

/**
 * @brief The control for the case above, INVERTED BY L6 FINDING 4a: with the retry budget at zero
 * the request SUCCEEDS, because nothing spends that budget on a protocol decision any more
 *
 * THE NAME RECORDS WHAT THIS CASE WAS FOR AND IS DELIBERATELY KEPT. It pinned the consequence of
 * the rider being dispatched onto a placeholder which can never speak HTTP/2: the bounce is a
 * retry, so maxRetriesPerRequest of zero made a cleartext HTTP/1.1 session unable to make any
 * request at all, and it failed as "connection aborted" with nothing chained to explain it,
 * because the placeholder itself completed successfully. A knob whose value zero disables a
 * protocol - and this case is what made that a measurement rather than an argument. Three places
 * cite it by name ( ConnectionPool.h at maxRetriesPerRequest, L6 finding 4a, astra H21 ), so a
 * rename would break the citations that explain why it exists
 *
 * WHY THE INVERSION IS EVIDENCE AND NOT A CONCESSION. The request, the server and the zero budget
 * are the same; only the expected outcome moved. So this case is RED against every revision before
 * ConnectionPoolPolicy::ridePreface and GREEN after it, which is the discrimination a control is
 * for - and it is a stronger assertion inverted than it was before, because the old form could be
 * satisfied by ANY failure whatever while this one can only be satisfied by a request which really
 * was carried with no attempt to spare
 *
 * THE COUNTS DO NOT MOVE, AND THAT IS WHY THE OUTCOME IS ASSERTED WITH THEM. One connection, one
 * dispatch, one release, before and after: before, that single dispatch was the rider and it was
 * thrown away; now it is the request itself, onto the HTTP/1.1 driver the fallback built, and
 * nothing is thrown away. A case which asserted only the counts would stay green across the fix
 * and would be telling nobody anything
 */

UTF_AUTO_TEST_CASE( ClientSession_FallbackRiderNeedsTheDispatchedRetryTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            httpclient::ClientSessionConfig config;

            config.poolPolicy.maxRetriesPerRequest = 0U;

            const auto session = makeSession( std::move( config ) );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_FallbackRiderNeedsTheDispatchedRetryTests"
                );

            httpclient::ClientRequest request;

            request.method( "GET" );

            request.url(
                net::Uri::parse(
                    "http://" +
                    test::UtfArgsParser::host() +
                    ":" +
                    utils::lexical_cast< std::string >(
                        static_cast< os::port_t >( test::UtfArgsParser::port() )
                        ) +
                    utest::http::g_requestUri
                    )
                );

            const auto requestTask = session -> createRequestTask( request );

            const auto task = om::qi< Task >( requestTask );

            runSessionTask( task );

            UTF_REQUIRE( ! task -> isFailed() );

            UTF_REQUIRE_EQUAL( requestTask -> response().status(), 200U );

            /*
             * AND IT REALLY WENT OVER HTTP/1.1, which is what makes the 200 above mean what the
             * case says. The server is the library's own HttpServer and speaks nothing else
             */

            UTF_REQUIRE(
                httpclient::HttpProtocol::Http11 == requestTask -> response().protocol()
                );

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );

            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 1U );
        }
        );
}

/**
 * @brief S6R.3 H08 - a streamed response says "the body is complete" ONCE
 *
 * WHAT IT ESTABLISHED WHEN IT WAS WRITTEN, and the name keeps: this was the case above with a sink
 * installed, the one thing no case in this suite did until then. The pool dispatched the first
 * request of a key onto the Connecting placeholder; over a connection which turns out to speak
 * HTTP/1.1 the HTTP/2 task bounced that rider with connection_aborted, and applyClosed( ) used to
 * queue the caller's onComplete( ) on ANY close whatever its outcome. So the caller's sink was
 * told the body was complete, with nothing in it, and was then handed the whole body by the
 * retried hop - twice wrong on every first request to an origin which does not speak h2
 *
 * WHAT IT ESTABLISHES NOW, AND THE HALF IT LOST. L6 finding 4a stopped the rider being dispatched
 * where the protocol is already decided, so this chain is ONE hop and there is no bounce on it any
 * more: what survives is the streamed form itself - the body reaches the sink and not the response,
 * and the single hop which carried it says "complete" exactly once. The discrimination that made
 * the count worth its green - a first hop which must say nothing at all - no longer runs HERE, and
 * saying so is the point: it is not that the defect was re-checked and found gone, it is that this
 * path no longer reaches it. The combination of a BOUNCED rider and an installed sink is now only
 * reachable over TLS ALPN fallback, where ClientSessionTls_Http11FallbackExchangeTests
 * ( utf_baselib_httpclient5 ) runs the bounce with no sink; that case with a sink is OWED and is
 * recorded as such against finding 4a
 *
 * AND IT IS STILL THE CONTROL FOR H08's OTHER HALF, which did not depend on the bounce:
 * chkPrepareRetry( ) refuses a retry once the sink has seen bytes, and a plain streamed response
 * must not trip that refusal
 */

UTF_AUTO_TEST_CASE( ClientSession_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            const auto url =
                "http://" +
                test::UtfArgsParser::host() +
                ":" +
                utils::lexical_cast< std::string >(
                    static_cast< os::port_t >( test::UtfArgsParser::port() )
                    ) +
                utest::http::g_requestUri;

            const auto session = makeSession( httpclient::ClientSessionConfig() );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_SinkIsToldCompleteOnceAcrossTheFallbackRetryTests"
                );

            httpclient::ClientRequest request;

            request.method( "GET" );
            request.url( net::Uri::parse( url ) );

            const auto sink = CountingBodySink::createInstance();

            const auto fetched = runRequest(
                session,
                request,
                om::ObjPtrCopyable< httpclient::BodySink >(
                    om::qi< httpclient::BodySink >( sink )
                    )
                );

            UTF_REQUIRE_EQUAL( fetched -> response().status(), 200U );

            /*
             * The body went to the SINK and not into the response, which is design 5.3's streamed
             * form - so the sink's tally is the only place the body exists
             */

            UTF_REQUIRE( ! fetched -> response().body() );

            UTF_REQUIRE_EQUAL( sink -> received(), utest::http::g_desiredResult );

            UTF_REQUIRE_EQUAL( sink -> completions(), 1U );

            /*
             * ONE HOP, which is what the chain is once the rider no longer rides a cleartext
             * HTTP/1.1 key - and it is asserted rather than left implicit precisely because the
             * completion count above USED to be discriminating and now is not
             */

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 1U );
        }
        );
}

/**
 * @brief S6R.3 H08's other half - a hop which reached the sink may not be replayed onto it
 *
 * A BodySink CANNOT BE REWOUND - ClientTypes.h has no such method, and that is a decision rather
 * than an oversight ( the reset-capable sink is a recorded deferral ). So a retry after the sink
 * has seen bytes does not repeat a request, it CORRUPTS one: the caller's sink keeps the prefix
 * the lost connection delivered and the replayed hop appends a second, complete copy behind it
 *
 * THE CONFIGURATION IS THE ONE WHERE THIS BITES: retryIdempotentOnConnectionLoss, which is off by
 * default and which exists precisely so that an idempotent request survives a connection dying
 * under it. The knob is not wrong - what was wrong is that nothing asked whether response bytes
 * had already escaped to the caller
 *
 * THE PEER SERVES THE FIRST REQUEST HALF AND THEN DROPS THE CONNECTION, and serves the second in
 * full - so a client which replays is visibly rewarded with a body, and the assertion below can
 * tell "refused the replay" from "the peer simply failed twice"
 */

UTF_AUTO_TEST_CASE( ClientSession_RetryIsRefusedOnceTheSinkHasSeenBytesTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::session;

    const auto peer = makePeer();

    const auto served = std::make_shared< std::atomic< unsigned > >( 0U );

    peer -> setResponder(
        [ served ]( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            if( 0U == ( *served )++ )
            {
                /*
                 * A status, half a body, and then the connection out from under it - no
                 * END_STREAM, so the stream is still open when the close arrives
                 */

                return h2peer::Http2ResponseScript()
                    .headers( 200U )
                    .data( "first-half" )
                    .closeConnection();
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "the whole body" )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            auto config = makeHttp2SessionConfig();

            config.poolPolicy.retryIdempotentOnConnectionLoss = true;

            const auto session = makeSession( std::move( config ) );

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::session::ClientSession_RetryIsRefusedOnceTheSinkHasSeenBytesTests"
                );

            const auto sink = CountingBodySink::createInstance();

            const auto requestTask = session -> createRequestTask(
                makeRequest( port, "/half" ),
                om::ObjPtrCopyable< httpclient::BodySink >(
                    om::qi< httpclient::BodySink >( sink )
                    )
                );

            const auto task = om::qi< Task >( requestTask );

            runSessionTask( task );

            UTF_REQUIRE( task -> isFailed() );

            /*
             * WHAT THE CALLER'S SINK HOLDS IS THE PREFIX AND NOTHING ELSE. This is the assertion
             * the fix is for: without it the replayed hop appends "the whole body" behind
             * "first-half" and the caller is handed a body which was never sent
             */

            UTF_REQUIRE_EQUAL( sink -> received(), std::string( "first-half" ) );

            /*
             * And it is never told the body is complete, because it never was - the terminal
             * callback for a request which failed is a contract change and a recorded deferral;
             * the task's own failure is how the caller learns of this one
             */

            UTF_REQUIRE_EQUAL( sink -> completions(), 0U );

            /*
             * The peer was asked ONCE. A second request arriving here is the replay this refuses,
             * and it would have been served in full
             */

            UTF_REQUIRE_EQUAL( served -> load(), 1U );
        }
        );
}

#endif /* __UTEST_TESTCLIENTSESSION_H_ */
