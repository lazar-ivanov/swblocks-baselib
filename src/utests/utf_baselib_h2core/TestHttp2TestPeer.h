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

#ifndef __UTEST_TESTHTTP2TESTPEER_H_
#define __UTEST_TESTHTTP2TESTPEER_H_

#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/RawFrameScriptPeer.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/SimpleTaskControlToken.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/TimeUtils.h>

#include <cstdint>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2 test peer of notes/plans/http2-design.md 8.2 (D8), slice S4.4 - its own self-test
 *
 * It lives in utf_baselib_h2core and not in utf_baselib_h2client because what it exercises is a
 * SERVER-ROLE http2::Session, which is this module's subject: S3.1 wrote the engine role-neutral
 * for D8 and until now nothing had ever constructed it with StreamRole::Server and driven it over
 * a socket. These cases are the first, so "the engine really is role-neutral" is a claim this
 * module now carries evidence for
 *
 * WHAT DRIVES THE PEER HERE IS NOT THE PRODUCT CLIENT, which does not exist yet - S4.1 and S4.2
 * build it. It is TestHttp2Client below: a CLIENT-role session over a blocking socket, on the
 * case's own thread. That makes each case a deterministic conversation with no second I/O thread
 * to reason about, and it is a second independent use of the same core
 *
 * The peer binds an ephemeral port, so these cases need no fixed port, do not take the machine
 * global test lock, and keep this module's property of running in parallel with every other
 */

namespace utest
{
    namespace h2peertest
    {
        using bl::http2::HpackField;
        using bl::http2::HpackFieldList;
        using bl::http2::SessionEvent;
        using bl::http2::SessionEventType;
        using bl::http2::SessionRequest;

        typedef bl::http2::Globals                                              Globals;

        inline auto now() -> bl::time::ptime
        {
            return bl::time::microsec_clock::universal_time();
        }

        inline auto fieldsOf(
            SAA_in              const std::string&                              name,
            SAA_in              const std::string&                              value
            )
            -> HpackFieldList
        {
            HpackFieldList fields;

            fields.push_back( HpackField( bl::cpp::copy( name ), bl::cpp::copy( value ) ) );

            return fields;
        }

        /**
         * @brief class TestHttp2Client - a client-role session over a blocking socket
         *
         * EVERY WAIT HERE IS BOUNDED. A synchronous read with no deadline is the one way a case in
         * this file could hang the suite rather than fail it, so the read is asynchronous against
         * a deadline timer and the io_service is run until one of the two completes - which is the
         * portable way to put a bound on a synchronous read
         */

        class TestHttp2Client
        {
            BL_NO_COPY_OR_MOVE( TestHttp2Client )

        public:

            enum : long
            {
                /*
                 * Generous, because it is only ever reached when something is already wrong
                 */

                DEFAULT_TIMEOUT_IN_MILLISECONDS = 15L * 1000L,
            };

            explicit TestHttp2Client(
                SAA_in              const unsigned short                        port,
                SAA_in_opt          const bl::http2::Http2Profile&              profile =
                                        bl::http2::Http2Profile()
                )
                :
                m_socket( m_ioService ),
                m_timer( m_ioService ),
                m_session( bl::http2::StreamRole::Client, now(), profile ),
                m_buffer( 16U * 1024U )
            {
                bl::eh::error_code ec;

                m_socket.connect(
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), port ),
                    ec
                    );

                UTF_REQUIRE( ! ec );
            }

            ~TestHttp2Client() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                close();

                BL_NOEXCEPT_END()
            }

            void close() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bl::eh::error_code ec;

                m_socket.close( ec );

                BL_NOEXCEPT_END()
            }

            auto session() NOEXCEPT -> bl::http2::Session&
            {
                return m_session;
            }

            auto events() const NOEXCEPT -> const std::vector< SessionEvent >&
            {
                return m_events;
            }

            /**
             * @brief Writes everything the session has to say right now
             *
             * IT ASSERTS ONLY ON THE FAILURE PATH, and that is not a style choice: how many
             * writes a body takes depends on when the peer's WINDOW_UPDATEs land, so a passing
             * assertion in this loop makes the case's assertion COUNT vary from run to run - 25,
             * 26 and 27 across three runs when this was UTF_REQUIRE - and the per-case counts are
             * exactly what verification tier 3 compares (src/utests/AGENTS.md)
             */

            void flush()
            {
                while( m_session.wantsWrite() )
                {
                    bl::http2::Session::wire_buffer_t out;

                    m_session.produce( out, now() );

                    if( out.empty() )
                    {
                        return;
                    }

                    bl::eh::error_code ec;

                    ( void ) bl::asio::write(
                        m_socket,
                        bl::asio::buffer( &out[ 0 ], out.size() ),
                        ec
                        );

                    if( ec )
                    {
                        UTF_FAIL(
                            BL_MSG()
                                << "The test client could not write "
                                << out.size()
                                << " octets: "
                                << ec.message()
                            );

                        return;
                    }
                }
            }

            /**
             * @brief One bounded read, fed to the session, with the events it produced drained
             *
             * @return false when the bound expired or the peer ended the stream
             */

            bool pump( SAA_in_opt const long timeoutInMilliseconds = DEFAULT_TIMEOUT_IN_MILLISECONDS )
            {
                using namespace bl;

                bool isTimedOut = false;
                eh::error_code readEc;
                std::size_t transferred = 0U;

                m_timer.expires_from_now( time::milliseconds( timeoutInMilliseconds ) );

                m_timer.async_wait(
                    [ this, &isTimedOut ]( SAA_in const eh::error_code& ec ) -> void
                    {
                        if( ! ec )
                        {
                            isTimedOut = true;

                            eh::error_code ignored;

                            m_socket.cancel( ignored );
                        }
                    }
                    );

                m_socket.async_read_some(
                    asio::buffer( &m_buffer[ 0 ], m_buffer.size() ),
                    [ this, &readEc, &transferred ](
                        SAA_in const eh::error_code& ec,
                        SAA_in const std::size_t bytes
                        ) -> void
                    {
                        readEc = ec;
                        transferred = bytes;

                        eh::error_code ignored;

                        m_timer.cancel( ignored );
                    }
                    );

                restartIoService();

                m_ioService.run();

                if( isTimedOut || readEc )
                {
                    return false;
                }

                m_session.feed( &m_buffer[ 0 ], transferred, now() );

                while( m_session.hasEvents() )
                {
                    m_events.push_back( m_session.frontEvent() );

                    m_session.popEvent();
                }

                return true;
            }

            std::uint32_t submit( SAA_in const SessionRequest& request )
            {
                const auto streamId = m_session.submitRequest( request );

                flush();

                return streamId;
            }

            std::uint32_t get(
                SAA_in              const std::string&                          path,
                SAA_in_opt          const bool                                  hasBody = false
                )
            {
                SessionRequest request;

                request.method = hasBody ? "POST" : "GET";
                request.scheme = "http";
                request.authority = "localhost";
                request.path = path;
                request.hasBody = hasBody;

                return submit( request );
            }

            /**
             * @brief Hands the session a whole body, a window's worth at a time
             *
             * THE STALL IS HANDLED HERE AND NOWHERE ELSE: when bodyBytesWanted( ) says zero the
             * windows are shut, and the only thing that reopens them is a WINDOW_UPDATE, so the
             * client reads until one arrives. A case never has to know
             */

            void sendBody(
                SAA_in              const std::uint32_t                         streamId,
                SAA_in              const std::string&                          body
                )
            {
                std::size_t offset = 0U;

                while( offset < body.size() )
                {
                    const auto wanted = m_session.bodyBytesWanted( streamId );

                    if( 0U == wanted )
                    {
                        if( ! pump() )
                        {
                            UTF_FAIL(
                                BL_MSG()
                                    << "The test client's send window stayed shut with "
                                    << ( body.size() - offset )
                                    << " octets of the body still to send - the peer never "
                                    << "credited it"
                                );

                            return;
                        }

                        continue;
                    }

                    const auto chunk = std::min< std::size_t >( wanted, body.size() - offset );

                    m_session.provideBody(
                        streamId,
                        reinterpret_cast< const std::uint8_t* >( body.data() + offset ),
                        chunk,
                        false /* endStream */
                        );

                    offset += chunk;

                    flush();
                }

                m_session.provideBody( streamId, nullptr, 0U, true /* endStream */ );

                flush();
            }

            /**
             * @brief Reads until this stream is reported closed, or the bound expires
             */

            bool waitForStreamClosed(
                SAA_in              const std::uint32_t                         streamId,
                SAA_in_opt          const long                                  timeoutInMilliseconds =
                                        DEFAULT_TIMEOUT_IN_MILLISECONDS
                )
            {
                const auto deadline =
                    now() + bl::time::milliseconds( timeoutInMilliseconds );

                for( ;; )
                {
                    if( hasEvent( SessionEventType::StreamClosed, streamId ) )
                    {
                        return true;
                    }

                    if( now() >= deadline || ! pump( timeoutInMilliseconds ) )
                    {
                        return hasEvent( SessionEventType::StreamClosed, streamId );
                    }
                }
            }

            bool waitForConnectionError(
                SAA_in_opt          const long                                  timeoutInMilliseconds =
                                        DEFAULT_TIMEOUT_IN_MILLISECONDS
                )
            {
                const auto deadline =
                    now() + bl::time::milliseconds( timeoutInMilliseconds );

                for( ;; )
                {
                    if( hasEvent( SessionEventType::ConnectionError, 0U ) )
                    {
                        return true;
                    }

                    if( now() >= deadline || ! pump( timeoutInMilliseconds ) )
                    {
                        return hasEvent( SessionEventType::ConnectionError, 0U );
                    }
                }
            }

            bool hasEvent(
                SAA_in              const SessionEventType                      type,
                SAA_in              const std::uint32_t                         streamId
                ) const
            {
                for( std::size_t i = 0U; i < m_events.size(); ++i )
                {
                    if(
                        m_events[ i ].type == type &&
                        ( 0U == streamId || m_events[ i ].streamId == streamId )
                        )
                    {
                        return true;
                    }
                }

                return false;
            }

            /**
             * @brief The events of one stream, in order - what a case asserts on
             */

            auto eventsOf( SAA_in const std::uint32_t streamId ) const -> std::vector< SessionEvent >
            {
                std::vector< SessionEvent > result;

                for( std::size_t i = 0U; i < m_events.size(); ++i )
                {
                    if( m_events[ i ].streamId == streamId )
                    {
                        result.push_back( m_events[ i ] );
                    }
                }

                return result;
            }

            /**
             * @brief Everything that arrived as a body on this stream
             */

            auto bodyOf( SAA_in const std::uint32_t streamId ) const -> std::string
            {
                std::string result;

                for( std::size_t i = 0U; i < m_events.size(); ++i )
                {
                    if(
                        m_events[ i ].type == SessionEventType::Data &&
                        m_events[ i ].streamId == streamId
                        )
                    {
                        result += m_events[ i ].data;
                    }
                }

                return result;
            }

        private:

            void restartIoService()
            {
                #if BOOST_VERSION >= 106600
                m_ioService.restart();
                #else
                m_ioService.reset();
                #endif
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::socket                                           m_socket;
            bl::asio::deadline_timer                                            m_timer;

            bl::http2::Session                                                  m_session;

            std::vector< std::uint8_t >                                         m_buffer;
            std::vector< SessionEvent >                                         m_events;
        };

        /**
         * @brief Schedules the peer, waits until it is listening, and runs the case against it
         *
         * The readiness wait is the peer's own waitForPort( ), which is signalled by the acceptor
         * when the base class has bound - so there is no probe connection, which would arrive at
         * this peer as a connection like any other, and no sleep
         */

        template
        <
            typename Callback
        >
        inline void runAgainstPeer(
            SAA_in              const bl::om::ObjPtr< h2peer::Http2TestServer >&     peer,
            SAA_in              const Callback&                                      callback
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    const auto taskAcceptor = om::qi< Task >( peer );

                    eq -> push_back( taskAcceptor );

                    BL_SCOPE_EXIT_WARN_ON_FAILURE(
                        {
                            cancelAndWaitForSuccess( eq, taskAcceptor );
                        },
                        "utest::h2peertest::runAgainstPeer"
                        );

                    callback( peer -> waitForPort() );
                }
                );
        }

        inline auto makePeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

        inline void requireRecords(
            SAA_in              const h2peer::Http2TestRecorder&                 recorder,
            SAA_in              const std::vector< std::string >&                expected
            )
        {
            h2peer::waitForRecordsOf( recorder, expected.size() );

            UTF_REQUIRE( recorder.failure().empty() );

            const auto records = recorder.records();

            UTF_REQUIRE_EQUAL( records.size(), expected.size() );

            for( std::size_t i = 0U; i < expected.size(); ++i )
            {
                UTF_REQUIRE_EQUAL( records[ i ], expected[ i ] );
            }
        }

    } // h2peertest

} // utest

/**
 * @brief The peer answers a request, and says exactly what it did
 *
 * The record list is asserted in full and in order rather than by sampling, because the ORDER is
 * the contract S4.2's cases will be written against: what the peer received, then what it sent
 */

UTF_AUTO_TEST_CASE( Http2TestPeer_RequestAndResponseTests )
{
    using namespace utest;
    using namespace utest::h2peertest;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            /*
             * A responder runs on an I/O thread, where the Boost.Test macros are not safe, so it
             * states what it expects by throwing - which the peer records and requireRecords( )
             * asserts on
             */

            BL_CHK(
                false,
                "/hello" == request.path,
                BL_MSG()
                    << "The peer was asked for an unexpected path: "
                    << request.path
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U, fieldsOf( "content-type", "text/plain" ) )
                .data( "hello" )
                .endStream();
        }
        );

    runAgainstPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            TestHttp2Client client( port );

            const auto streamId = client.get( "/hello" );

            UTF_REQUIRE_EQUAL( streamId, 1U );

            UTF_REQUIRE( client.waitForStreamClosed( streamId ) );

            /*
             * What the client saw - a final header section, a body, and a closed stream
             */

            const auto events = client.eventsOf( streamId );

            UTF_REQUIRE_EQUAL( events.size(), 3U );

            UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
            UTF_REQUIRE_EQUAL( events[ 0 ].status.value(), 200U );
            UTF_REQUIRE( ! events[ 0 ].isInformational );
            UTF_REQUIRE( events[ 1 ].type == SessionEventType::Data );
            UTF_REQUIRE_EQUAL( events[ 1 ].data, std::string( "hello" ) );
            UTF_REQUIRE( events[ 2 ].type == SessionEventType::StreamClosed );
            UTF_REQUIRE( events[ 2 ].isMessageComplete );

            UTF_REQUIRE_EQUAL( client.bodyOf( streamId ), std::string( "hello" ) );

            /*
             * What the peer says it did. THE WAIT IS THE RENDEZVOUS: the records are made on the
             * peer's I/O threads and waitForStreamClosed waits only for the CLIENT
             */

            std::vector< std::string > expected;

            expected.push_back( "connected" );
            expected.push_back( "request GET /hello on stream 1" );
            expected.push_back( "end of request body on stream 1, 0 bytes" );
            expected.push_back( "responded 200 on stream 1" );
            expected.push_back( "sent 5 body bytes on stream 1" );
            expected.push_back( "ended stream 1" );
            expected.push_back( "stream 1 closed with error 0" );

            requireRecords( peer -> recorder(), expected );

            /*
             * And the last record of all, which only the client's own close can produce
             */

            client.close();

            expected.push_back( "the client closed the connection" );

            requireRecords( peer -> recorder(), expected );
        }
        );
}

/**
 * @brief Every scripted step of a response, in one conversation
 *
 * The interesting one is the trailer section: Session::produce( ) writes header blocks BEFORE
 * data, so a peer which queued the trailers as soon as the script reached them would put them on
 * the wire in front of the body. The client would then see a trailer section before the body and
 * the case would fail on the event ORDER, which is why the order is what is asserted
 *
 * The opening delay is here rather than in a case of its own because it is the OTHER timer path
 * of the peer - the one which holds its SETTINGS back, for the connect-through-preface deadline
 * S4.1 owns - and what it must not do is change any of this. So the conversation and the record
 * list below are the ones a peer with no opening delay produces, and that is the assertion
 */

UTF_AUTO_TEST_CASE( Http2TestPeer_ScriptedResponseStepsTests )
{
    using namespace utest;
    using namespace utest::h2peertest;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            return h2peer::Http2ResponseScript()
                .interim( 103U, fieldsOf( "link", "</style.css>; rel=preload" ) )
                .headers( 200U )
                .delay( 40L )
                .data( "the body" )
                .trailers( fieldsOf( "x-checksum", "7" ) );
        }
        );

    peer -> setOpeningDelayInMilliseconds( 60L );

    runAgainstPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            TestHttp2Client client( port );

            const auto streamId = client.get( "/scripted" );

            UTF_REQUIRE( client.waitForStreamClosed( streamId ) );

            const auto events = client.eventsOf( streamId );

            UTF_REQUIRE_EQUAL( events.size(), 5U );

            UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
            UTF_REQUIRE( events[ 0 ].isInformational );
            UTF_REQUIRE_EQUAL( events[ 0 ].status.value(), 103U );

            UTF_REQUIRE( events[ 1 ].type == SessionEventType::Headers );
            UTF_REQUIRE( ! events[ 1 ].isInformational );
            UTF_REQUIRE_EQUAL( events[ 1 ].status.value(), 200U );

            UTF_REQUIRE( events[ 2 ].type == SessionEventType::Data );
            UTF_REQUIRE_EQUAL( events[ 2 ].data, std::string( "the body" ) );

            UTF_REQUIRE( events[ 3 ].type == SessionEventType::Headers );
            UTF_REQUIRE( events[ 3 ].isTrailers );

            UTF_REQUIRE( events[ 4 ].type == SessionEventType::StreamClosed );

            std::vector< std::string > expected;

            expected.push_back( "connected" );
            expected.push_back( "request GET /scripted on stream 1" );
            expected.push_back( "end of request body on stream 1, 0 bytes" );
            expected.push_back( "interim 103 on stream 1" );
            expected.push_back( "responded 200 on stream 1" );
            expected.push_back( "sent 8 body bytes on stream 1" );
            expected.push_back( "trailers on stream 1" );
            expected.push_back( "stream 1 closed with error 0" );

            requireRecords( peer -> recorder(), expected );
        }
        );
}

/**
 * @brief A refusal, then a response, then GOAWAY - the shape D6's retry budget is driven with
 *
 * The RETRYABLE flag on the closed stream is what the pool of design 5.4 will read, so it is
 * asserted here: a REFUSED_STREAM is a stream the peer never began processing and is retryable on
 * a fresh connection, which is exactly what makes a GOAWAY after it safe
 */

UTF_AUTO_TEST_CASE( Http2TestPeer_RefusedStreamAndGoAwayTests )
{
    using namespace utest;
    using namespace utest::h2peertest;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( request.streamIndex <= 2U )
            {
                return h2peer::Http2ResponseScript().refuse();
            }

            return h2peer::Http2ResponseScript().headers( 200U, HpackFieldList(), true );
        }
        );

    peer -> setGoAwayAfterStreams( 3U );

    runAgainstPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            TestHttp2Client client( port );

            for( std::size_t i = 0U; i < 3U; ++i )
            {
                const auto streamId = client.get( "/retry" );

                UTF_REQUIRE( client.waitForStreamClosed( streamId ) );
            }

            const auto first = client.eventsOf( 1U );

            UTF_REQUIRE_EQUAL( first.size(), 1U );
            UTF_REQUIRE( first[ 0 ].type == SessionEventType::StreamClosed );
            UTF_REQUIRE_EQUAL(
                first[ 0 ].errorCode.value(),
                static_cast< std::uint32_t >( Globals::ERROR_CODE_REFUSED_STREAM )
                );
            UTF_REQUIRE( first[ 0 ].isRetryable );

            const auto third = client.eventsOf( 5U );

            UTF_REQUIRE_EQUAL( third.size(), 2U );
            UTF_REQUIRE( third[ 0 ].type == SessionEventType::Headers );
            UTF_REQUIRE_EQUAL( third[ 0 ].status.value(), 200U );

            /*
             * The GOAWAY follows the response to the third request rather than replacing it -
             * 6.8's graceful shutdown, which is what makes a retrying client able to finish what
             * it already had in flight
             */

            UTF_REQUIRE( client.hasEvent( SessionEventType::GoAwayReceived, 0U ) );
            UTF_REQUIRE( client.session().goAwayReceived() );

            std::vector< std::string > expected;

            expected.push_back( "connected" );
            expected.push_back( "request GET /retry on stream 1" );
            expected.push_back( "end of request body on stream 1, 0 bytes" );
            expected.push_back( "refused stream 1 with error 7" );
            expected.push_back( "stream 1 closed with error 7" );
            expected.push_back( "request GET /retry on stream 3" );
            expected.push_back( "end of request body on stream 3, 0 bytes" );
            expected.push_back( "refused stream 3 with error 7" );
            expected.push_back( "stream 3 closed with error 7" );
            expected.push_back( "request GET /retry on stream 5" );
            expected.push_back( "end of request body on stream 5, 0 bytes" );
            expected.push_back( "responded 200 on stream 5" );
            expected.push_back( "sent GOAWAY with error 0" );
            expected.push_back( "stream 5 closed with error 0" );

            requireRecords( peer -> recorder(), expected );
        }
        );
}

/**
 * @brief A window stall on an upload, and the credit which releases it
 *
 * The peer is told to report nothing it receives as consumed, so no WINDOW_UPDATE is ever sent and
 * the client's send windows shut at the 65535 octets RFC 9113 6.9.2 starts them at. The script
 * then credits what arrived, which is what reopens them. WITHOUT THE CREDIT THIS CASE WOULD HANG,
 * so it is also the proof that the stall is real rather than merely configured
 *
 * awaitWindowStall( ) and not a delay, and the first shape of this case is why. A peer which
 * waited 40 ms and then credited whatever had arrived by then credited a number nobody could
 * predict, and when that number was short of a whole window the client ran out again with the
 * one-shot credit already spent - a 15 second hang, seen on the first run. Waiting for the
 * window itself to reach zero makes both the stall and the number certain
 */

UTF_AUTO_TEST_CASE( Http2TestPeer_WindowStallTests )
{
    using namespace utest;
    using namespace utest::h2peertest;

    const std::string body( 96U * 1024U, 'u' );

    const auto peer = makePeer();

    peer -> setWithholdWindowUpdates( true );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                request.hasBody,
                BL_MSG()
                    << "The upload request carried no body"
                );

            return h2peer::Http2ResponseScript()
                .awaitWindowStall()
                .creditWindow()
                .headers( 200U, HpackFieldList(), true );
        }
        );

    runAgainstPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            TestHttp2Client client( port );

            const auto streamId = client.get( "/upload", true /* hasBody */ );

            /*
             * The window is shut for most of this, and sendBody( ) is what reads until it opens
             */

            client.sendBody( streamId, body );

            UTF_REQUIRE( client.waitForStreamClosed( streamId ) );

            const auto events = client.eventsOf( streamId );

            UTF_REQUIRE_EQUAL( events.size(), 2U );
            UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
            UTF_REQUIRE_EQUAL( events[ 0 ].status.value(), 200U );
            UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );

            /*
             * THE CREDITED NUMBER IS THE PROOF AND IT IS EXACT. 65535 is where both of the
             * client's send windows start (6.9.2) and the peer credited nothing before the
             * script reached creditWindow, so the client had placed exactly one window's worth
             * and could place no more - it was stalled. The body is larger than that, so the rest
             * of it can only have been sent after the credit
             */

            std::vector< std::string > expected;

            expected.push_back( "connected" );
            expected.push_back( "request POST /upload on stream 1" );
            expected.push_back( "credited 65535 bytes on stream 1" );
            expected.push_back( "responded 200 on stream 1" );
            expected.push_back( "end of request body on stream 1, 98304 bytes" );
            expected.push_back( "stream 1 closed with error 0" );

            requireRecords( peer -> recorder(), expected );

            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( streamId ), body );
        }
        );
}

/**
 * @brief The raw peer sends what a conforming one cannot
 *
 * A connection-level WINDOW_UPDATE with an increment of zero is a PROTOCOL_ERROR (RFC 9113 6.9)
 * and there is no way to ask Http2TestServer for one - its session refuses to build it. That is
 * the whole reason RawFrameScriptPeer exists, and this case is the proof that it reaches the
 * client's parser as the exact octets it was given
 */

UTF_AUTO_TEST_CASE( Http2TestPeer_RawFrameScriptTests )
{
    using namespace utest;
    using namespace utest::h2peertest;

    h2peer::RawFrameScriptPeer peer(
        h2peer::RawFrameScript()
            .expectPreface()
            .readFrames( 2U )                   /* the client's SETTINGS, then its HEADERS */
            .send( h2peer::frameOctets( Globals::FRAME_TYPE_SETTINGS, 0U, 0U ) )
            .send(
                h2peer::frameOctets(
                    Globals::FRAME_TYPE_WINDOW_UPDATE,
                    0U                          /* flags */,
                    0U                          /* the connection */,
                    h2peer::uint32Octets( 0U )  /* an increment of zero - 6.9 forbids it */
                    )
                )
            .waitForClose()
        );

    {
        TestHttp2Client client( peer.port() );

        const auto streamId = client.get( "/raw" );

        UTF_REQUIRE( client.waitForConnectionError() );

        const auto& events = client.events();

        bool sawSettings = false;
        std::uint32_t errorCode = 0U;

        for( std::size_t i = 0U; i < events.size(); ++i )
        {
            if( events[ i ].type == SessionEventType::SettingsReceived )
            {
                sawSettings = true;
            }

            if( events[ i ].type == SessionEventType::ConnectionError )
            {
                errorCode = events[ i ].errorCode;
            }
        }

        UTF_REQUIRE( sawSettings );

        UTF_REQUIRE_EQUAL(
            errorCode,
            static_cast< std::uint32_t >( Globals::ERROR_CODE_PROTOCOL_ERROR )
            );

        /*
         * The stream the request opened is closed by the connection error, and the session stops
         * parsing - 4.5's contract that an error never escapes feed( )
         */

        UTF_REQUIRE( client.hasEvent( SessionEventType::StreamClosed, streamId ) );
        UTF_REQUIRE( client.session().isClosed() );

        client.close();
    }

    h2peer::waitForRecordsOf( peer.recorder(), 6U );

    UTF_REQUIRE( peer.recorder().failure().empty() );

    const auto records = peer.recorder().records();

    UTF_REQUIRE_EQUAL( records.size(), 6U );

    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "the client sent the connection preface" ) );

    /*
     * The SETTINGS frame carries one entry - SETTINGS_ENABLE_PUSH = 0, which D11 makes this
     * client always send - so its length is the six octets of one entry and is named exactly
     */

    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "the client sent frame type 4 of 6 octets" ) );

    /*
     * The HEADERS length is whatever the HPACK encoder made of this request, and pinning it here
     * would make an unrelated change to the encoder's defaults fail this case for no reason -
     * what this case is about is that the octets arrive, which receivedOctets( ) below asserts
     */

    UTF_REQUIRE_EQUAL(
        records[ 2 ].substr( 0U, std::string( "the client sent frame type 1 of " ).size() ),
        std::string( "the client sent frame type 1 of " )
        );

    UTF_REQUIRE_EQUAL( records[ 3 ], std::string( "sent 9 octets" ) );
    UTF_REQUIRE_EQUAL( records[ 4 ], std::string( "sent 13 octets" ) );
    UTF_REQUIRE_EQUAL( records[ 5 ], std::string( "the client closed the connection" ) );

    /*
     * And the octets the peer read back are the client's, byte for byte - the preface first
     */

    const auto received = peer.receivedOctets();

    UTF_REQUIRE(
        received.size() > bl::http2::Globals::g_connectionPreface.size()
        );

    UTF_REQUIRE_EQUAL(
        received.substr( 0U, bl::http2::Globals::g_connectionPreface.size() ),
        bl::http2::Globals::g_connectionPreface
        );
}

#endif /* __UTEST_TESTHTTP2TESTPEER_H_ */
