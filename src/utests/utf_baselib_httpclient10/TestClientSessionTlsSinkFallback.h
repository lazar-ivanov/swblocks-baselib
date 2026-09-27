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

#include <utests/baselib/HttpClientSessionTestUtils.h>
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
 * over the 40 MB target ( its Main.cpp records 46.9 MB ), and src/utests/AGENTS.md says not to add
 * to one there. Its peer is not shared either, because a test header may never be included across
 * module directories; the peer below is this module's own, a smaller one written for the one
 * script these cases run. The counting sink IS shared - it was utf_baselib_httpclient4's, and moved
 * to utests/baselib/HttpClientSessionTestUtils.h for this case.
 *
 * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev root
 * CA for every test binary and the test server certificate is issued for that name.
 */

namespace utest
{
    namespace tlssession
    {
        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        typedef bl::httpclient::ClientSessionImplT< tls_stream_t >              TlsSessionImpl;

        inline auto makeSession(
            SAA_in_opt      bl::httpclient::ClientSessionConfig                 config =
                                bl::httpclient::ClientSessionConfig()
            )
            -> bl::om::ObjPtr< TlsSessionImpl >
        {
            return TlsSessionImpl::createInstance( BL_PARAM_FWD( config ) );
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

            request.url(
                bl::net::Uri::parse(
                    "https://localhost:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        /**
         * @brief Runs one session task to completion; the queue keeps it, so a case can look at how
         * it ended
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

        /**
         * @brief Fails with the reason the task failed - a function and not a UTF macro argument,
         * because UTF_FAIL( msg ) takes the globals lock before it evaluates msg and bl::os::mutex
         * is not recursive ( Utf.h )
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

        inline auto statsOf( SAA_in const bl::om::ObjPtr< TlsSessionImpl >& session )
            -> bl::httpclient::ConnectionPoolImpl::Stats
        {
            return bl::om::qi< bl::httpclient::ConnectionPoolImpl >( session -> pool() ) -> stats();
        }

        /**
         * @brief class Http1TlsPeer - a loopback HTTP/1.1 peer over TLS which selects "http/1.1"
         * and runs one canned script on one connection
         *
         * THE STEERING IS THE ALPN PREFERENCE AND NOTHING ELSE. The client offers { "h2", "http/1.1" }
         * by default, a peer may select only from what it was offered ( RFC 7301 3.1 ), and this
         * context prefers "http/1.1" alone - so the connection the pool dispatched the rider onto
         * turns out to speak HTTP/1.1, which is the fallback.
         *
         * Port zero, so no machine global test lock is needed, and the script runs on a worker
         * thread so that the test thread is free to run the session. The address is IPv4 loopback
         * while the client connects to "localhost"; asio::async_connect( ) walks every resolved
         * endpoint, the arrangement utf_baselib_httpclient5's peer of the same name runs green.
         * It is that peer's shape with everything its close_notify cases needed taken out.
         */

        class Http1TlsPeer
        {
            BL_NO_COPY_OR_MOVE( Http1TlsPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            typedef bl::cpp::function
            <
                void (
                    SAA_inout       Http1TlsPeer&                               peer,
                    SAA_inout       sslstream_t&                                stream
                    )
            >
            script_t;

            Http1TlsPeer( SAA_in script_t&& script )
                :
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    ),
                m_acceptor( m_ioService ),
                m_port( 0U ),
                m_script( BL_PARAM_FWD( script ) )
            {
                std::vector< std::string > preference;

                preference.push_back( "http/1.1" );

                bl::crypto::CryptoBase::setAlpnServerPreference( *m_serverContext, preference );

                const bl::asio::ip::tcp::endpoint endpoint(
                    bl::asio::ip::address_v4::loopback(),
                    0 /* ephemeral */
                    );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                m_port = m_acceptor.local_endpoint().port();

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &Http1TlsPeer::run, this ) ) );
            }

            ~Http1TlsPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept( ), so one throwaway connection does it - harmless when the script
                     * has already run
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint(
                            bl::asio::ip::address_v4::loopback(),
                            m_port
                            ),
                        ec
                        );

                    socket.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            bl::os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            auto failure() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_failure;
            }

            /*
             * The script vocabulary - all synchronous, all on the worker thread
             */

            /**
             * @brief Reads to the end of the request head and stops there
             */

            static auto readRequestHead( SAA_inout sslstream_t& stream ) -> std::string
            {
                std::string data;

                char buffer[ 1024 ];

                while( std::string::npos == data.find( "\r\n\r\n" ) )
                {
                    bl::eh::error_code ec;

                    const auto transferred =
                        stream.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec || 0U == transferred )
                    {
                        break;
                    }

                    data.append( buffer, transferred );
                }

                return data;
            }

            static void send(
                SAA_inout       sslstream_t&                                    stream,
                SAA_in          const std::string&                              data
                )
            {
                bl::eh::error_code ec;

                ( void ) bl::asio::write( stream, bl::asio::buffer( data ), ec );
            }

            /**
             * @brief Reads until the client lets the connection go, which is what lets this
             * worker finish when the session is disposed. Nothing asserts on how it ended
             */

            static void readUntilTheEnd( SAA_inout sslstream_t& stream )
            {
                char buffer[ 16U * 1024U ];

                for( ;; )
                {
                    bl::eh::error_code ec;

                    ( void ) stream.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec )
                    {
                        return;
                    }
                }
            }

            /**
             * @brief The request line of a recorded request, for a readable assertion
             */

            static auto requestLineOf( SAA_in const std::string& request ) -> std::string
            {
                const auto pos = request.find( "\r\n" );

                return std::string::npos == pos ? request : request.substr( 0U, pos );
            }

        private:

            void run()
            {
                sslstream_t stream( m_ioService, *m_serverContext );

                try
                {
                    bl::eh::error_code ec;

                    m_acceptor.accept( stream.next_layer(), ec );

                    if( ec )
                    {
                        record( "accept-failed" );

                        return;
                    }

                    stream.handshake( bl::asio::ssl::stream_base::server, ec );

                    if( ec )
                    {
                        record( "handshake-failed:" + ec.message() );

                        return;
                    }

                    m_script( *this, stream );
                }
                catch( std::exception& e )
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_failure = e.what();
                }

                bl::eh::error_code ec;

                stream.next_layer().close( ec );
            }

            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::os::port_t                                                      m_port;
            const script_t                                                      m_script;

            mutable bl::os::mutex                                               m_lock;
            std::vector< std::string >                                          m_records;
            std::string                                                         m_failure;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

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
            const auto head = Http1TlsPeer::readRequestHead( stream );

            self.record( "head:" + Http1TlsPeer::requestLineOf( head ) );

            Http1TlsPeer::send(
                stream,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 6\r\n"
                "\r\n"
                "secure"
                );

            Http1TlsPeer::readUntilTheEnd( stream );
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
