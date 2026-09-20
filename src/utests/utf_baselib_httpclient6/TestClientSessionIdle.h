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

#ifndef __UTEST_TESTCLIENTSESSIONIDLE_H_
#define __UTEST_TESTCLIENTSESSIONIDLE_H_

#include <baselib/httpclient/ClientSession.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * L6 finding 5 - the connection idle lifetime, over HTTP/1.1
 *
 * WHAT WAS WRONG. ConnectionPoolPolicy::idleTimeout reached the HTTP/2 driver only, through the
 * session's connection factory, and the HTTP/1.1 driver had no idle timer at all - its own header
 * said the lifetime was the pool's, and the pool has never had a reaper. So an idle keep-alive
 * HTTP/1.1 connection obtained through a session was closed by nothing of ours: it stayed open
 * until the peer gave up on it or the session was disposed. The knob was pinned as behaviour for
 * one of the two protocols and silently unmet for the other
 *
 * WHAT THIS CASE PINS, AND WHY IT IS AT SESSION LEVEL. A driver-level case would prove the timer
 * works and would prove nothing about the defect, which was the WIRING: the number is the pool's
 * and the timer is the driver's, and the session is the only thing which puts them together. So
 * what runs here is a real ClientSessionT over the cleartext stream policy, its real pool, its
 * real HTTP/1.1 driver and a real socket - and the close is observed AT THE PEER rather than
 * inferred from a state or a stat. The rendezvous is the peer's own record, not a sleep: the peer
 * blocks in a read and the client's FIN is what wakes it
 */

namespace utest
{
    namespace sessionidle
    {
        typedef bl::tasks::TcpSocketAsyncStrandedBase                           plain_stream_t;

        typedef bl::httpclient::ClientSessionImplT< plain_stream_t >            PlainSessionImpl;

        /**
         * @brief An HTTP/1.1 peer which answers one request and keeps the connection
         *
         * ITS ONLY SUBJECT IS WHAT HAPPENS AFTER THE RESPONSE. It answers with no Connection
         * header at all, which by RFC 9112 section 9.3 is a persistent connection and is what
         * makes the driver report Ready and the pool keep the entry - the library's own HttpServer
         * cannot be used here, since it says 'Connection: close' on every response. Then it blocks
         * in one read: the client's close arrives as end of stream and anything else means the
         * client did not close
         *
         * THE RECORDER IS THE HTTP/2 PEER'S, deliberately - a second implementation of the
         * rendezvous is a second chance to get it wrong, and the two records this peer makes are
         * then waited for with the same waitForRecordsOf( ) every other peer in the suite uses
         *
         * The port is ephemeral, so this case takes no machine global test lock. The shape of the
         * worker - a blocking script on its own thread, a throwaway connection to unblock an
         * accept nobody came to, nothing asserted off the test thread - is RawFrameScriptPeer's
         * and FakeProxy's before it
         */

        class KeepAliveHttp1Peer
        {
            BL_NO_COPY_OR_MOVE( KeepAliveHttp1Peer )

        public:

            KeepAliveHttp1Peer( SAA_in std::string&& body )
                :
                m_recorder( std::make_shared< h2peer::Http2TestRecorder >() ),
                m_acceptor(
                    m_ioService,
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    ),
                m_port( m_acceptor.local_endpoint().port() ),
                m_body( BL_PARAM_FWD( body ) ),
                m_stopRequested( false )
            {
                m_thread.reset(
                    new bl::os::thread( bl::cpp::bind( &KeepAliveHttp1Peer::run, this ) )
                    );
            }

            ~KeepAliveHttp1Peer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_stopRequested = true;

                {
                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                        ec
                        );

                    socket.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            unsigned short port() const NOEXCEPT
            {
                return m_port;
            }

            auto recorder() const NOEXCEPT -> const h2peer::Http2TestRecorder&
            {
                return *m_recorder;
            }

        private:

            /**
             * @brief Reads until the end of the request head, which is the only framing this peer
             * needs - the requests it is given carry no body
             */

            void readRequestHead( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
                std::string head;

                char buffer[ 512 ];

                for( ;; )
                {
                    bl::eh::error_code ec;

                    const auto transferred =
                        socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    BL_CHK(
                        false,
                        0U != transferred,
                        BL_MSG()
                            << "The peer expected an HTTP/1.1 request head and the client ended "
                            << "the stream after "
                            << head.size()
                            << " octets"
                        );

                    head.append( buffer, transferred );

                    if( std::string::npos != head.find( "\r\n\r\n" ) )
                    {
                        break;
                    }
                }
            }

            void run()
            {
                BL_NOEXCEPT_BEGIN()

                bl::eh::error_code ec;

                bl::asio::ip::tcp::socket socket( m_ioService );

                m_acceptor.accept( socket, ec );

                if( ec || m_stopRequested )
                {
                    return;
                }

                try
                {
                    readRequestHead( socket );

                    const auto response =
                        std::string( "HTTP/1.1 200 OK\r\ncontent-length: " ) +
                        bl::utils::lexical_cast< std::string >( m_body.size() ) +
                        "\r\n\r\n" +
                        m_body;

                    bl::asio::write( socket, bl::asio::buffer( response ), ec );

                    m_recorder -> record( "answered one request and kept the connection" );

                    /*
                     * Nothing below this line is a poll or a sleep: the read blocks until the
                     * client does something, and the something this case is about is a close
                     */

                    char buffer[ 64 ];

                    const auto transferred =
                        socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    m_recorder -> record(
                        0U == transferred && ec ?
                            std::string( "the client closed the connection" )
                            :
                            std::string( "the client sent more instead of closing" )
                        );
                }
                catch( std::exception& e )
                {
                    m_recorder -> recordFailure( std::string( e.what() ) );
                }

                socket.close( ec );

                BL_NOEXCEPT_END()
            }

            const std::shared_ptr< h2peer::Http2TestRecorder >                  m_recorder;

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            const unsigned short                                                m_port;
            const std::string                                                   m_body;
            std::atomic< bool >                                                 m_stopRequested;
            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        inline auto makeRequest( SAA_in const unsigned short port )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( std::string( "GET" ) );

            request.url(
                bl::net::Uri::parse(
                    "http://127.0.0.1:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    "/one"
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

        /**
         * @brief The response of a request which was expected to succeed - and NOT an assertion
         * inside another assertion's argument, for the reason Utf.h gives
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

        inline auto runRequest(
            SAA_in          const bl::om::ObjPtr< PlainSessionImpl >&           session,
            SAA_in          const bl::httpclient::ClientRequest&                request
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientRequestTask >
        {
            using namespace bl;
            using namespace bl::tasks;

            auto requestTask = session -> createRequestTask( request );

            const auto task = om::qi< Task >( requestTask );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );

            requireTaskSucceeded( task );

            return requestTask;
        }

    } // sessionidle

} // utest

/**
 * @brief ConnectionPoolPolicy::idleTimeout reaches the HTTP/1.1 driver - L6 finding 5
 *
 * A DEFAULT ClientSessionConfig SPEAKS HTTP/1.1 over cleartext, so this is the ordinary session
 * against an ordinary keep-alive server, and the only thing set on it is the knob. The request
 * itself is incidental - what it is there for is to leave a pooled connection behind, which is the
 * state the idle lifetime measures
 *
 * The proof that the knob arrived is that the connection closes ITSELF: nothing in this case
 * cancels it, the pool has no reaper to do it, the peer is blocked in a read and does not close
 * it, and the request has long since completed. Degrade the session so that it hands the driver
 * factory time::neg_infin instead of the policy's value and this case fails, with the peer
 * recording one entry where two are expected
 */

UTF_AUTO_TEST_CASE( ClientSession_IdleTimeoutReachesTheHttp11DriverTests )
{
    using namespace bl;
    using namespace utest;
    using namespace utest::sessionidle;

    KeepAliveHttp1Peer peer( std::string( "/one" ) );

    httpclient::ClientSessionConfig config;

    config.poolPolicy.idleTimeout = time::milliseconds( 300 );

    const auto session = PlainSessionImpl::createInstance( std::move( config ) );

    BL_SCOPE_EXIT_WARN_ON_FAILURE(
        {
            session -> dispose();
        },
        "utest::sessionidle::ClientSession_IdleTimeoutReachesTheHttp11DriverTests"
        );

    const auto task = runRequest( session, makeRequest( peer.port() ) );

    UTF_REQUIRE_EQUAL( bodyOf( task -> response() ), std::string( "/one" ) );

    /*
     * The connection this request left in the pool speaks HTTP/1.1 and nothing else could have
     * carried it - a peer which never sent a preface and a driver which answered a status line
     */

    UTF_REQUIRE( httpclient::HttpProtocol::Http11 == task -> response().protocol() );

    h2peer::waitForRecordsOf( peer.recorder(), 2U );

    const auto records = peer.recorder().records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "the client closed the connection" ) );

    UTF_REQUIRE( peer.recorder().failure().empty() );
}

#endif /* __UTEST_TESTCLIENTSESSIONIDLE_H_ */
