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

#ifndef __UTEST_TESTTCPTUNNELSTAGE_H_
#define __UTEST_TESTTCPTUNNELSTAGE_H_

#include <baselib/tasks/TcpTunnelStage.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <initializer_list>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * The tunnel stage of design 3.6 (D5, D18, D19), slice S3.5 - HTTP CONNECT and SOCKS5
 *
 * Two kinds of case, and the split is the one the design draws. The negotiation is sans-I/O, so
 * what goes on the wire and what is refused coming back is pinned byte for byte with no socket at
 * all - deterministically, and without a fake proxy having to be wrong on purpose in a dozen
 * different ways. The stage itself is then driven against in-process fake proxies on loopback,
 * which is what proves the bytes reach a socket in the right order, that a failure fails the task
 * before any handshake, that a cancel is heard, and that the origin and the proxy stay apart
 *
 * The proxies bind port zero, so these cases need no fixed port and do not take the machine global
 * test lock
 */

namespace utest
{
    namespace tunnel
    {
        /**
         * @brief A byte string written the way the RFC writes it
         */

        inline auto bytes( SAA_in const std::initializer_list< int >& values ) -> std::string
        {
            std::string result;

            for( const int value : values )
            {
                result.push_back( static_cast< char >( static_cast< unsigned char >( value ) ) );
            }

            return result;
        }

        /**
         * @brief An in-process proxy which runs a scripted conversation on a loopback port
         *
         * Synchronous socket calls on a worker thread, which is what makes a script read as a
         * script. A blocking read cannot hang the suite: the only way the client stops talking is
         * by ending the stream, which the peer sees as EOF - including on the cancel path, where
         * cancelTask() shuts the socket down. The one case which could hang is an accept nobody
         * ever connects to, and the destructor unblocks that with a throwaway connection before
         * joining, exactly as RawHttpResponder does in utf_baselib_http
         *
         * NOTHING IS ASSERTED ON THE WORKER THREAD. The Boost.Test assertion macros are not safe
         * to call from two threads, so the handler records what it saw and the case asserts on the
         * records after the task has finished - which is also the rendezvous that makes them
         * visible
         */

        class FakeProxy
        {
            BL_NO_COPY_OR_MOVE( FakeProxy )

        public:

            typedef bl::cpp::function
                <
                    void (
                        SAA_inout   FakeProxy&                                  proxy,
                        SAA_inout   bl::asio::ip::tcp::socket&                  socket
                        )
                >
                handler_t;

            FakeProxy(
                SAA_in              handler_t&&                                 handler,
                SAA_in_opt          const std::size_t                           connections = 1U
                )
                :
                m_acceptor(
                    m_ioService,
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    ),
                m_port( m_acceptor.local_endpoint().port() ),
                m_handler( BL_PARAM_FWD( handler ) ),
                m_connections( connections ),
                m_stopRequested( false )
            {
                m_thread.reset( new bl::os::thread( bl::cpp::bind( &FakeProxy::run, this ) ) );
            }

            ~FakeProxy() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_stopRequested = true;

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept(), so one throwaway connection does it while the acceptor is still
                     * open; the flag above makes the worker drop it rather than run the script
                     */

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

            /**
             * @brief What a script threw, if anything; empty when every connection ran to the end
             */

            auto failure() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_failure;
            }

            /*
             * The script vocabulary - all synchronous, all on the worker thread
             */

            static auto readExactly(
                SAA_inout           bl::asio::ip::tcp::socket&                  socket,
                SAA_in              const std::size_t                           count
                )
                -> std::string
            {
                std::string buffer( count, '\0' );

                bl::eh::error_code ec;

                const auto transferred =
                    bl::asio::read( socket, bl::asio::buffer( &buffer[ 0 ], count ), ec );

                buffer.resize( transferred );

                return buffer;
            }

            static auto readHeaders( SAA_inout bl::asio::ip::tcp::socket& socket ) -> std::string
            {
                bl::asio::streambuf streamBuffer( 64U * 1024U );

                bl::eh::error_code ec;

                bl::asio::read_until( socket, streamBuffer, "\r\n\r\n", ec );

                if( 0U == streamBuffer.size() )
                {
                    return std::string();
                }

                /*
                 * The capture idiom and the non-empty guard above are the production ones from
                 * SimpleHttpTask.h - inserting an empty streambuf sets failbit, which
                 * cpp::SafeOutputStringStream turns into an exception
                 */

                bl::cpp::SafeOutputStringStream oss;

                oss << &streamBuffer;

                return oss.str();
            }

            static void send(
                SAA_inout           bl::asio::ip::tcp::socket&                  socket,
                SAA_in              const std::string&                          data
                )
            {
                bl::eh::error_code ec;

                ( void ) bl::asio::write( socket, bl::asio::buffer( data ), ec );
            }

            /**
             * @brief Blocks until the peer ends the stream; true when it did rather than speaking
             */

            static bool waitForPeerToClose( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
                char buffer[ 64 ];

                bl::eh::error_code ec;

                const auto transferred =
                    socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                return 0U == transferred && ec;
            }

        private:

            void run()
            {
                BL_NOEXCEPT_BEGIN()

                for( std::size_t i = 0U; i < m_connections; ++i )
                {
                    bl::eh::error_code ec;

                    bl::asio::ip::tcp::socket socket( m_ioService );

                    m_acceptor.accept( socket, ec );

                    if( ec || m_stopRequested )
                    {
                        break;
                    }

                    try
                    {
                        m_handler( *this, socket );
                    }
                    catch( std::exception& e )
                    {
                        BL_MUTEX_GUARD( m_lock );

                        if( m_failure.empty() )
                        {
                            m_failure = e.what();
                        }
                    }

                    socket.close( ec );
                }

                BL_NOEXCEPT_END()
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            const unsigned short                                                m_port;
            const handler_t                                                     m_handler;
            const std::size_t                                                   m_connections;
            std::atomic< bool >                                                 m_stopRequested;
            mutable bl::os::mutex                                               m_lock;
            std::vector< std::string >                                          m_records;
            std::string                                                         m_failure;
            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /**
         * @brief A plain stream policy which records what createSocket was given
         *
         * THIS IS HOW THE ORIGIN/PROXY SPLIT IS OBSERVED, and it observes it at the only point
         * where the stage has any say in it. Under the TLS policy the host name handed to
         * createSocket becomes the SNI - TcpSslSocketAsyncBaseT::configureClientStream calls
         * SSL_set_tlsext_host_name with it - and the name the peer certificate is verified
         * against, since AsioSslStreamWrapperT stores the same string as m_hostName and
         * TlsPeerVerification::verifyPeerName is given that member. Both of those are covered
         * where they live; what is new in this slice, and what these cases pin, is WHICH name
         * arrives there when a proxy is in the way
         *
         * Hiding rather than overriding is the right mechanism because a stream policy's static
         * interface is resolved by template composition and not virtually - the same mechanism
         * design 3.1 names for the stranded policies
         */

        class CreateSocketRecordingStream : public bl::tasks::TcpSocketAsyncBase
        {
        public:

            typedef bl::tasks::TcpSocketAsyncBase                               base_type;

            std::string                                                         m_createSocketHostName;
            std::string                                                         m_createSocketServiceName;

        protected:

            void createSocket(
                SAA_inout           bl::asio::io_service&                       aioService,
                SAA_in              const std::string&                          hostName,
                SAA_in              const std::string&                          serviceName
                )
            {
                m_createSocketHostName = hostName;
                m_createSocketServiceName = serviceName;

                base_type::createSocket( aioService, hostName, serviceName );
            }
        };

        /**
         * @brief The tunnel stage under test, with the order it was driven in recorded
         *
         * continueAfterConnected is what the connection task of S4.1 will override; here it only
         * records that the handshake path was reached and returns false, which completes the task.
         * That makes "the tunnel succeeded" and "the task completed" one observation, and makes
         * "the tunnel failed" observable as the absence of the record
         */

        template
        <
            typename STREAM
        >
        class TunnelProbeT :
            public bl::tasks::TcpTunnelStageT
                <
                    bl::tasks::TcpConnectionEstablisherConnector< STREAM >
                >
        {
            BL_DECLARE_OBJECT_IMPL( TunnelProbeT )

        public:

            typedef bl::tasks::TcpTunnelStageT
                <
                    bl::tasks::TcpConnectionEstablisherConnector< STREAM >
                >
                base_type;

        protected:

            typedef TunnelProbeT< STREAM >                                      this_type;

            mutable bl::os::mutex                                               m_eventsLock;
            std::vector< std::string >                                          m_events;

            bl::cpp::ScalarTypeIniter< std::size_t >                            m_retriesAllowed;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_retriesTaken;
            bl::cpp::ScalarTypeIniter< bool >                                   m_wasExpectedExceptionAtStop;

            TunnelProbeT(
                SAA_in                  std::string                             originHost,
                SAA_in                  const bl::os::port_t                    originPort,
                SAA_in                  bl::tasks::ProxyConfig                  proxyConfig
                )
                :
                base_type(
                    BL_PARAM_FWD( originHost ),
                    originPort,
                    BL_PARAM_FWD( proxyConfig ),
                    false /* logExceptions */
                    )
            {
                /*
                 * Every real consumer of a connection establisher owns its socket
                 * (SimpleHttpTask.h, TcpBlockTransferServer.h, HttpServer.h all set this), and it
                 * is what makes a failed or cancelled tunnel end the stream the peer is reading
                 */

                base_type::isCloseStreamOnTaskFinish( true );
            }

            void record( SAA_in const char* event )
            {
                BL_MUTEX_GUARD( m_eventsLock );

                m_events.push_back( std::string( event ) );
            }

            virtual bool beginPreHandshakeStage(
                SAA_in                  const bl::cpp::bool_callback_t&         continueCallback
                )
                OVERRIDE
            {
                record( "stageEntered" );

                return base_type::beginPreHandshakeStage( continueCallback );
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                record( "handshakePathReached" );

                return false;
            }

            /**
             * @brief Restarts the whole transaction once, the way the handshake retry does
             *
             * THE THREE CALLS ARE THE PRODUCTION ONES, copied from the retry branch of
             * TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation: reset the stream
             * state, drop the resolver, start connection establishing again - which re-enters
             * beginPreHandshakeStage IN PLACE, without going back through scheduleNothrow. What is
             * not copied is the condition, which asks whether a protocol handshake failed with a
             * retryable error; a plain TCP policy has no protocol handshake, so the retry could
             * never be reached with one and the condition is replaced by a counter
             *
             * This is deliberately a reproduction of the mechanism rather than a use of it. The
             * real retry needs a TLS stream and a peer which truncates a handshake, and it is
             * exercised that way in utf_baselib_http2; what is under test here is whether THE
             * STAGE is re-entrant against it, which is a property of the stage alone
             */

            virtual bool scheduleTaskFinishContinuation(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr
                )
                OVERRIDE
            {
                if( eptrIn && m_retriesTaken < m_retriesAllowed )
                {
                    ++m_retriesTaken.lvalue();

                    record( "transactionRestarted" );

                    base_type::resetStreamState();
                    base_type::m_resolver.reset();

                    base_type::startConnectionEstablishingInternal();

                    return true;
                }

                return base_type::scheduleTaskFinishContinuation( eptrIn );
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                if( isExpectedException )
                {
                    m_wasExpectedExceptionAtStop = *isExpectedException;
                }

                return result;
            }

        public:

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_eventsLock );

                return m_events;
            }

            auto countOf( SAA_in const char* event ) const -> std::size_t
            {
                BL_MUTEX_GUARD( m_eventsLock );

                std::size_t count = 0U;

                for( const auto& recorded : m_events )
                {
                    if( recorded == event )
                    {
                        ++count;
                    }
                }

                return count;
            }

            void allowRetries( SAA_in const std::size_t count ) NOEXCEPT
            {
                m_retriesAllowed = count;
            }

            bool wasExpectedExceptionAtStop() const NOEXCEPT
            {
                return m_wasExpectedExceptionAtStop;
            }

            /**
             * @brief What the resolver was given, which with a proxy is the proxy
             */

            auto resolvedHostName() const -> std::string
            {
                return base_type::m_query.host_name();
            }

            auto resolvedServiceName() const -> std::string
            {
                return base_type::m_query.service_name();
            }

            /**
             * @brief The endpoint actually connected to
             */

            unsigned short connectedPort() const NOEXCEPT
            {
                return base_type::m_endpoint.port();
            }
        };

        /**
         * @brief The one probe instantiation every case below uses
         *
         * ONE, deliberately. A second STREAM policy instantiates the whole establisher chain
         * again, and a test module is a single translation unit whose size is policed
         * (src/utests/AGENTS.md) - having both a plain and a recording policy measured 1.7MB more
         * on clang debug than having only this one. The recording policy does nothing but store
         * two strings on its way to the base, so the cases which never look at them are driving
         * the plain policy's behaviour in every respect that matters
         */

        typedef bl::om::ObjectImpl< TunnelProbeT< CreateSocketRecordingStream > >
            TunnelProbeImpl;

        /**
         * @brief Runs a probe to completion; the wait is what makes everything it recorded
         * visible to the test thread
         */

        template
        <
            typename PROBE
        >
        inline void runProbeToCompletion( SAA_in const bl::om::ObjPtr< PROBE >& probe )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ &probe ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto task = om::qi< Task >( probe );

                    eq -> push_back( task );
                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );
                }
                );
        }

        /**
         * @brief The exception a failed probe task carries, as a message
         */

        inline auto exceptionMessageOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> std::string
        {
            using namespace bl;

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE( task -> exception() );

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( std::exception& e )
            {
                return std::string( e.what() );
            }

            UTF_FAIL( "cpp::safeRethrowException must throw" );

            return std::string();
        }

        /**
         * @brief Runs a callable and returns the message of the exception it threw
         */

        template
        <
            typename CALLABLE
        >
        inline auto messageOfThrown( SAA_in const CALLABLE& callable ) -> std::string
        {
            try
            {
                callable();
            }
            catch( std::exception& e )
            {
                return std::string( e.what() );
            }

            UTF_FAIL( "The callable must have thrown" );

            return std::string();
        }

    } // tunnel

} // utest

UTF_AUTO_TEST_CASE( TcpTunnelStage_ProxyConfigurationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * A configuration which cannot be put on the wire is refused where it is written, so the three
     * factories are the whole validation surface
     */

    const auto direct = ProxyConfig::none();

    UTF_REQUIRE( ! direct.isEnabled() );
    UTF_REQUIRE( ! direct.hasCredentials() );
    UTF_REQUIRE( ProxyProtocol::None == direct.protocol() );
    UTF_REQUIRE( direct.proxyId().empty() );

    const auto anonymous = ProxyConfig::httpConnect( "proxy.example.com", 3128U );

    UTF_REQUIRE( anonymous.isEnabled() );
    UTF_REQUIRE( ! anonymous.hasCredentials() );
    UTF_REQUIRE_EQUAL( anonymous.proxyId(), std::string( "http-connect://proxy.example.com:3128" ) );

    /*
     * The user name identifies the connection and the password does not: two principals must not
     * share a tunnel, and a key is a thing which gets logged
     */

    const auto authenticated = ProxyConfig::socks5( "proxy.example.com", 1080U, "alice", "secret" );

    UTF_REQUIRE( authenticated.hasCredentials() );
    UTF_REQUIRE_EQUAL( authenticated.proxyId(), std::string( "socks5://alice@proxy.example.com:1080" ) );
    UTF_REQUIRE( ! cpp::contains( authenticated.proxyId(), std::string( "secret" ) ) );

    /*
     * A CR or an LF in a host would split the CONNECT request line; the check is stated positively
     * as printable ASCII, so everything below is refused by the same rule
     */

    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "", 3128U ), ArgumentException );
    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "proxy\r\nX-Evil: 1", 3128U ), ArgumentException );
    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "proxy host", 3128U ), ArgumentException );
    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "http://proxy", 3128U ), ArgumentException );
    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "user@proxy", 3128U ), ArgumentException );
    UTF_REQUIRE_THROW( ProxyConfig::httpConnect( "proxy.example.com", 0U ), ArgumentException );

    /*
     * Half a credential is a configuration mistake rather than an anonymous proxy
     */

    UTF_REQUIRE_THROW(
        ProxyConfig::socks5( "proxy.example.com", 1080U, "alice", "" ),
        ArgumentException
        );

    UTF_REQUIRE_THROW(
        ProxyConfig::socks5( "proxy.example.com", 1080U, "", "secret" ),
        ArgumentException
        );

    /*
     * RFC 1929 gives ULEN and PLEN one octet each, so 255 is the bound - and it is applied to the
     * HTTP credentials too, where base64 would otherwise have hidden the difference
     */

    UTF_REQUIRE_THROW(
        ProxyConfig::socks5( "proxy.example.com", 1080U, std::string( 256U, 'a' ), "secret" ),
        ArgumentException
        );

    UTF_REQUIRE_THROW(
        ProxyConfig::httpConnect( "proxy.example.com", 3128U, "alice", std::string( 256U, 'a' ) ),
        ArgumentException
        );

    /*
     * The colon separates the user-id from the password in RFC 7617 and separates nothing in
     * RFC 1929, so it is refused in an HTTP user name and accepted in a SOCKS5 one. A user name
     * carrying one would otherwise move the split and authenticate as a different principal
     */

    UTF_REQUIRE_THROW(
        ProxyConfig::httpConnect( "proxy.example.com", 3128U, "ali:ce", "secret" ),
        ArgumentException
        );

    UTF_REQUIRE_NO_THROW(
        ( void ) ProxyConfig::socks5( "proxy.example.com", 1080U, "ali:ce", "secret" )
        );

    /*
     * A control character in a password would end the Basic credentials early and become a header
     * of its own once the proxy decodes them
     */

    UTF_REQUIRE_THROW(
        ProxyConfig::httpConnect( "proxy.example.com", 3128U, "alice", "sec\r\nret" ),
        ArgumentException
        );
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_HttpConnectNegotiationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * The exact request bytes. Authority form in the request line and the same authority in Host,
     * per RFC 9110 section 9.3.6, and nothing else - a CONNECT has no body and needs no other
     * field, and every field added here is one the proxy sees which no caller asked for
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        UTF_REQUIRE_EQUAL(
            negotiation.outgoing(),
            std::string(
                "CONNECT origin.example.com:443 HTTP/1.1\r\n"
                "Host: origin.example.com:443\r\n"
                "\r\n"
                )
            );

        UTF_REQUIRE( TunnelStep::Action::Write == negotiation.start().action() );

        const auto afterWrite = negotiation.onWriteCompleted();

        UTF_REQUIRE( TunnelStep::Action::ReadSome == afterWrite.action() );
        UTF_REQUIRE( afterWrite.length() > 0U );

        const std::string response( "HTTP/1.1 200 Connection established\r\n\r\n" );

        UTF_REQUIRE(
            TunnelStep::Action::Done ==
                negotiation.onDataRead( response.c_str(), response.size() ).action()
            );
    }

    /*
     * RFC 7617: the user-id and the password joined by a colon, base64 of the result. The literal
     * below was produced with
     *
     *     printf '%s' 'user:pass' | base64
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U, "user", "pass" ),
            "origin.example.com",
            8443U
            );

        UTF_REQUIRE_EQUAL(
            negotiation.outgoing(),
            std::string(
                "CONNECT origin.example.com:8443 HTTP/1.1\r\n"
                "Host: origin.example.com:8443\r\n"
                "Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
                "\r\n"
                )
            );
    }

    /*
     * A response which arrives one byte at a time is the same response: the reader keeps asking
     * for more until it sees CRLF CRLF
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response( "HTTP/1.1 200 OK\r\nX-Proxy: yes\r\n\r\n" );

        for( std::size_t i = 0U; i + 1U < response.size(); ++i )
        {
            UTF_REQUIRE(
                TunnelStep::Action::ReadSome ==
                    negotiation.onDataRead( response.c_str() + i, 1U ).action()
                );
        }

        UTF_REQUIRE(
            TunnelStep::Action::Done ==
                negotiation.onDataRead( response.c_str() + response.size() - 1U, 1U ).action()
            );
    }

    /*
     * A non-2xx is an HTTP status failure, so it is an HttpException carrying the code - which is
     * how design 3.7 says an HTTP status failure is reported, and what lets a caller tell a 407
     * asking for credentials from a 403 refusing them
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response(
            "HTTP/1.1 407 Proxy Authentication Required\r\n"
            "Proxy-Authenticate: Basic realm=\"x\"\r\n"
            "\r\n"
            );

        bool thrown = false;

        try
        {
            ( void ) negotiation.onDataRead( response.c_str(), response.size() );
        }
        catch( HttpException& e )
        {
            thrown = true;

            const auto* statusCode = eh::get_error_info< eh::errinfo_http_status_code >( e );

            UTF_REQUIRE( statusCode );
            UTF_REQUIRE_EQUAL( *statusCode, 407 );

            UTF_REQUIRE(
                cpp::contains(
                    std::string( e.what() ),
                    std::string( "Proxy Authentication Required" )
                    )
                );
        }

        UTF_REQUIRE( thrown );
    }

    /*
     * A 1xx is a failure and not an interim response to wait past: a CONNECT has no body to be
     * continued, and guessing what a proxy meant by one is how a reader ends up disagreeing with
     * its peer about where the response ended
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response( "HTTP/1.1 100 Continue\r\n\r\n" );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( response.c_str(), response.size() ),
            HttpException
            );
    }

    /*
     * The header section ends at CRLF CRLF and at nothing else. A reader which also accepted a
     * bare LF is a reader two peers can disagree with about where the response ended, which is the
     * shape of every request smuggling defect - so the LF-terminated response below is not a
     * response at all, it is an unterminated one and the reader asks for more
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response( "HTTP/1.1 200 OK\n\n" );

        UTF_REQUIRE(
            TunnelStep::Action::ReadSome ==
                negotiation.onDataRead( response.c_str(), response.size() ).action()
            );
    }

    /*
     * Everything past CRLF CRLF on an established tunnel belongs to the ORIGIN, and those bytes
     * are already off the socket with nowhere to be put back for the handshake about to read them.
     * They also cannot be legitimate, since the client has said nothing to the origin yet
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response( "HTTP/1.1 200 OK\r\n\r\n\x16\x03\x03" );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( response.c_str(), response.size() ),
            InvalidDataFormatException
            );
    }

    /*
     * ...but that refusal is for a 2xx only. A proxy which says no is entitled to a body, and
     * because the reader asks for at most READ_CHUNK_SIZE octets at a time the read which
     * completes the header section normally carries the first bytes of that HTML explanation with
     * it. None of the reasoning above applies: no tunnel was established, nothing is about to
     * handshake. So what must come out is the HttpException carrying 407 - the thing which lets a
     * caller ask for credentials - and not the framing error the trailing body would otherwise
     * produce
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response(
            "HTTP/1.1 407 Proxy Authentication Required\r\n"
            "Proxy-Authenticate: Basic realm=\"x\"\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 43\r\n"
            "\r\n"
            "<html><body>Proxy credentials</body></html>"
            );

        bool thrown = false;

        try
        {
            ( void ) negotiation.onDataRead( response.c_str(), response.size() );
        }
        catch( HttpException& e )
        {
            thrown = true;

            const auto* statusCode = eh::get_error_info< eh::errinfo_http_status_code >( e );

            UTF_REQUIRE( statusCode );
            UTF_REQUIRE_EQUAL( *statusCode, 407 );

            UTF_REQUIRE(
                cpp::contains(
                    std::string( e.what() ),
                    std::string( "The proxy refused a CONNECT request with status 407" )
                    )
                );
        }
        catch( InvalidDataFormatException& )
        {
            UTF_FAIL(
                "A refused CONNECT which carries a body must fail with its status, not as a "
                "framing error"
                );
        }

        UTF_REQUIRE( thrown );
    }

    /*
     * Something which is not a status line is refused before its middle three characters are read
     * as a status code
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string response( "HTTP/1.1 2OO OK\r\n\r\n" );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( response.c_str(), response.size() ),
            InvalidDataFormatException
            );
    }

    /*
     * The 64KB bound of design 3.6, and the read length shrinking as the budget is consumed, so a
     * proxy which never terminates its header section cannot make the client allocate without
     * limit
     */

    {
        HttpConnectNegotiation negotiation(
            ProxyConfig::httpConnect( "proxy.example.com", 3128U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const std::string filler( 1024U, 'x' );

        const auto maxResponseSize =
            static_cast< std::size_t >( HttpConnectNegotiation::MAX_RESPONSE_SIZE );

        std::size_t sent = 0U;
        std::size_t lastLength = 0U;

        while( sent + filler.size() < maxResponseSize )
        {
            const auto step = negotiation.onDataRead( filler.c_str(), filler.size() );

            UTF_REQUIRE( TunnelStep::Action::ReadSome == step.action() );

            lastLength = step.length();
            sent += filler.size();
        }

        UTF_REQUIRE( lastLength > 0U );
        UTF_REQUIRE( lastLength <= maxResponseSize - sent );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( filler.c_str(), maxResponseSize - sent ),
            InvalidDataFormatException
            );
    }
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_Socks5NegotiationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * RFC 1928 section 3: VER, NMETHODS, METHODS. Exactly the methods which can be honoured are
     * offered - NO AUTHENTICATION alone without credentials, and NO AUTHENTICATION plus
     * USERNAME/PASSWORD with them
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        UTF_REQUIRE_EQUAL( negotiation.outgoing(), bytes( { 0x05, 0x01, 0x00 } ) );
        UTF_REQUIRE( TunnelStep::Action::Write == negotiation.start().action() );

        const auto afterWrite = negotiation.onWriteCompleted();

        UTF_REQUIRE( TunnelStep::Action::ReadExactly == afterWrite.action() );
        UTF_REQUIRE_EQUAL( afterWrite.length(), 2U );

        /*
         * RFC 1928 section 4: VER, CMD, RSV, ATYP, then a length-prefixed DOMAINNAME and the port
         * in network byte order. 443 is 0x01BB, and the name is sent for the PROXY to resolve -
         * resolving it here would leak to the local resolver exactly what the proxy was there to
         * hide, and would resolve in the wrong network
         */

        const auto methodReply = bytes( { 0x05, 0x00 } );

        UTF_REQUIRE(
            TunnelStep::Action::Write ==
                negotiation.onDataRead( methodReply.c_str(), methodReply.size() ).action()
            );

        UTF_REQUIRE_EQUAL(
            negotiation.outgoing(),
            bytes( { 0x05, 0x01, 0x00, 0x03, 0x12 } ) + "origin.example.com" + bytes( { 0x01, 0xBB } )
            );

        const auto afterRequest = negotiation.onWriteCompleted();

        UTF_REQUIRE( TunnelStep::Action::ReadExactly == afterRequest.action() );
        UTF_REQUIRE_EQUAL( afterRequest.length(), 4U );

        /*
         * A reply whose BND.ADDR is an IPv4 address: four bytes plus the two of BND.PORT
         */

        const auto replyHeader = bytes( { 0x05, 0x00, 0x00, 0x01 } );

        const auto afterHeader = negotiation.onDataRead( replyHeader.c_str(), replyHeader.size() );

        UTF_REQUIRE( TunnelStep::Action::ReadExactly == afterHeader.action() );
        UTF_REQUIRE_EQUAL( afterHeader.length(), 6U );

        const auto bound = bytes( { 0x7F, 0x00, 0x00, 0x01, 0x01, 0xBB } );

        UTF_REQUIRE(
            TunnelStep::Action::Done ==
                negotiation.onDataRead( bound.c_str(), bound.size() ).action()
            );
    }

    /*
     * With credentials the greeting offers both methods, and the subnegotiation of RFC 1929 is
     * VER=1, ULEN, UNAME, PLEN, PASSWD followed by a two byte status
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U, "user", "pass" ),
            "origin.example.com",
            80U
            );

        UTF_REQUIRE_EQUAL( negotiation.outgoing(), bytes( { 0x05, 0x02, 0x00, 0x02 } ) );

        ( void ) negotiation.onWriteCompleted();

        const auto methodReply = bytes( { 0x05, 0x02 } );

        UTF_REQUIRE(
            TunnelStep::Action::Write ==
                negotiation.onDataRead( methodReply.c_str(), methodReply.size() ).action()
            );

        UTF_REQUIRE_EQUAL(
            negotiation.outgoing(),
            bytes( { 0x01, 0x04 } ) + "user" + bytes( { 0x04 } ) + "pass"
            );

        UTF_REQUIRE_EQUAL( negotiation.onWriteCompleted().length(), 2U );

        const auto authReply = bytes( { 0x01, 0x00 } );

        UTF_REQUIRE(
            TunnelStep::Action::Write ==
                negotiation.onDataRead( authReply.c_str(), authReply.size() ).action()
            );

        /*
         * Port 80 is 0x0050, which is where a byte order mistake would show
         */

        UTF_REQUIRE_EQUAL(
            negotiation.outgoing(),
            bytes( { 0x05, 0x01, 0x00, 0x03, 0x12 } ) + "origin.example.com" + bytes( { 0x00, 0x50 } )
            );
    }

    /*
     * A DOMAINNAME reply reads its length first and then that many bytes plus the port, and an
     * IPv6 one reads sixteen plus two. Neither is ever SENT by this client - ATYP is always
     * DOMAINNAME outbound - but both have to be understood coming back
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const auto methodReply = bytes( { 0x05, 0x00 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto replyHeader = bytes( { 0x05, 0x00, 0x00, 0x03 } );

        UTF_REQUIRE_EQUAL(
            negotiation.onDataRead( replyHeader.c_str(), replyHeader.size() ).length(),
            1U
            );

        const auto addressLength = bytes( { 0x03 } );

        const auto afterLength =
            negotiation.onDataRead( addressLength.c_str(), addressLength.size() );

        UTF_REQUIRE( TunnelStep::Action::ReadExactly == afterLength.action() );
        UTF_REQUIRE_EQUAL( afterLength.length(), 5U );

        const auto address = std::string( "abc" ) + bytes( { 0x01, 0xBB } );

        UTF_REQUIRE(
            TunnelStep::Action::Done ==
                negotiation.onDataRead( address.c_str(), address.size() ).action()
            );
    }

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        ( void ) negotiation.onWriteCompleted();

        const auto methodReply = bytes( { 0x05, 0x00 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto replyHeader = bytes( { 0x05, 0x00, 0x00, 0x04 } );

        UTF_REQUIRE_EQUAL(
            negotiation.onDataRead( replyHeader.c_str(), replyHeader.size() ).length(),
            18U
            );
    }

    /*
     * A server which selects a method that was not offered is refused rather than followed into a
     * subnegotiation this client never agreed to - including the USERNAME/PASSWORD method when
     * there are no credentials to answer it with
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0x02 } );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() ),
            SecurityException
            );
    }

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0xFF } );

        const auto message =
            messageOfThrown(
                [ &negotiation, &methodReply ]() -> void
                {
                    ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
                }
                );

        UTF_REQUIRE(
            cpp::contains( message, std::string( "accepts none of the authentication methods" ) )
            );
    }

    /*
     * RFC 1929 section 2 puts the version of the SUBNEGOTIATION in the status reply, which is 1
     * and not the 5 of the protocol itself; and a non-zero status is a rejection of the
     * credentials rather than a transport failure
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U, "user", "pass" ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0x02 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto authReply = bytes( { 0x05, 0x00 } );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( authReply.c_str(), authReply.size() ),
            InvalidDataFormatException
            );
    }

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U, "user", "pass" ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0x02 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto authReply = bytes( { 0x01, 0x01 } );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( authReply.c_str(), authReply.size() ),
            SecurityException
            );
    }

    /*
     * A non-zero REP is the proxy refusing the connection, and its meaning is spelled out from
     * RFC 1928 section 6 rather than left as a number
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0x00 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto replyHeader = bytes( { 0x05, 0x05, 0x00, 0x01 } );

        const auto message =
            messageOfThrown(
                [ &negotiation, &replyHeader ]() -> void
                {
                    ( void ) negotiation.onDataRead( replyHeader.c_str(), replyHeader.size() );
                }
                );

        UTF_REQUIRE( cpp::contains( message, std::string( "connection refused" ) ) );
    }

    /*
     * An unknown ATYP has no length, so the rest of the reply cannot be read at all
     */

    {
        Socks5Negotiation negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            "origin.example.com",
            443U
            );

        const auto methodReply = bytes( { 0x05, 0x00 } );
        ( void ) negotiation.onDataRead( methodReply.c_str(), methodReply.size() );
        ( void ) negotiation.onWriteCompleted();

        const auto replyHeader = bytes( { 0x05, 0x00, 0x00, 0x09 } );

        UTF_REQUIRE_THROW(
            ( void ) negotiation.onDataRead( replyHeader.c_str(), replyHeader.size() ),
            InvalidDataFormatException
            );
    }

    /*
     * A name longer than 255 bytes has no representation in a DOMAINNAME field at all, so it is
     * refused at construction rather than truncated onto the wire
     */

    UTF_REQUIRE_THROW(
        Socks5Negotiation(
            ProxyConfig::socks5( "proxy.example.com", 1080U ),
            std::string( 256U, 'a' ),
            443U
            ),
        ArgumentException
        );
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_HttpConnectTunnelTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * The CONNECT tunnel end to end against a proxy which accepts it. The proxy records the
     * request it was sent, so the bytes are checked where they actually arrived rather than only
     * where they were built
     */

    FakeProxy proxy(
        []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
        {
            self.record( FakeProxy::readHeaders( socket ) );

            FakeProxy::send( socket, "HTTP/1.1 200 Connection established\r\n\r\n" );

            self.record( FakeProxy::waitForPeerToClose( socket ) ? "closed" : "spoke" );
        }
        );

    const auto probe = TunnelProbeImpl::createInstance(
        std::string( "origin.example.com" ),
        static_cast< os::port_t >( 443U ),
        ProxyConfig::httpConnect( "127.0.0.1", proxy.port(), "tunneluser", "tunnelpass" )
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "handshakePathReached" ) );

    UTF_REQUIRE( proxy.failure().empty() );

    const auto records = proxy.records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );

    /*
     *     printf '%s' 'tunneluser:tunnelpass' | base64
     */

    UTF_REQUIRE_EQUAL(
        records[ 0 ],
        std::string(
            "CONNECT origin.example.com:443 HTTP/1.1\r\n"
            "Host: origin.example.com:443\r\n"
            "Proxy-Authorization: Basic dHVubmVsdXNlcjp0dW5uZWxwYXNz\r\n"
            "\r\n"
            )
        );

    /*
     * The client said nothing to the origin after the tunnel came up, which is what says the
     * handshake - a no-op returning false for a plain stream - ran after the stage rather than
     * beside it
     */

    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "closed" ) );
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_Socks5TunnelTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * The SOCKS5 tunnel end to end, through the username/password subnegotiation. The proxy reads
     * each message at its exact length, so a client which framed one wrongly would stall here
     * rather than pass
     */

    FakeProxy proxy(
        []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
        {
            self.record( FakeProxy::readExactly( socket, 4U ) );

            FakeProxy::send( socket, bytes( { 0x05, 0x02 } ) );

            self.record( FakeProxy::readExactly( socket, 2U + 4U + 1U + 4U ) );

            FakeProxy::send( socket, bytes( { 0x01, 0x00 } ) );

            self.record( FakeProxy::readExactly( socket, 5U + 18U + 2U ) );

            FakeProxy::send(
                socket,
                bytes( { 0x05, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x00, 0x01, 0x01, 0xBB } )
                );

            self.record( FakeProxy::waitForPeerToClose( socket ) ? "closed" : "spoke" );
        }
        );

    const auto probe = TunnelProbeImpl::createInstance(
        std::string( "origin.example.com" ),
        static_cast< os::port_t >( 443U ),
        ProxyConfig::socks5( "127.0.0.1", proxy.port(), "user", "pass" )
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    UTF_REQUIRE_EQUAL( probe -> countOf( "handshakePathReached" ), 1U );

    UTF_REQUIRE( proxy.failure().empty() );

    const auto records = proxy.records();

    UTF_REQUIRE_EQUAL( records.size(), 4U );
    UTF_REQUIRE_EQUAL( records[ 0 ], bytes( { 0x05, 0x02, 0x00, 0x02 } ) );
    UTF_REQUIRE_EQUAL( records[ 1 ], bytes( { 0x01, 0x04 } ) + "user" + bytes( { 0x04 } ) + "pass" );

    UTF_REQUIRE_EQUAL(
        records[ 2 ],
        bytes( { 0x05, 0x01, 0x00, 0x03, 0x12 } ) + "origin.example.com" + bytes( { 0x01, 0xBB } )
        );

    UTF_REQUIRE_EQUAL( records[ 3 ], std::string( "closed" ) );
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_TunnelRefusedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * A stage which fails fails the task BEFORE any handshake is attempted - the contract of
     * design 3.6 - so handshakePathReached is never recorded, on either protocol
     */

    {
        FakeProxy proxy(
            []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
            {
                self.record( FakeProxy::readHeaders( socket ) );

                FakeProxy::send( socket, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n" );

                ( void ) FakeProxy::waitForPeerToClose( socket );
            }
            );

        const auto probe = TunnelProbeImpl::createInstance(
            std::string( "origin.example.com" ),
            static_cast< os::port_t >( 443U ),
            ProxyConfig::httpConnect( "127.0.0.1", proxy.port() )
            );

        runProbeToCompletion( probe );

        const auto task = om::qi< Task >( probe );

        UTF_REQUIRE( task -> isFailed() );

        UTF_REQUIRE(
            cpp::contains(
                exceptionMessageOf( task ),
                std::string( "The proxy refused a CONNECT request with status 403" )
                )
            );

        UTF_REQUIRE_EQUAL( probe -> countOf( "stageEntered" ), 1U );
        UTF_REQUIRE_EQUAL( probe -> countOf( "handshakePathReached" ), 0U );
    }

    {
        FakeProxy proxy(
            []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
            {
                self.record( FakeProxy::readExactly( socket, 3U ) );

                FakeProxy::send( socket, bytes( { 0x05, 0x00 } ) );

                self.record( FakeProxy::readExactly( socket, 5U + 18U + 2U ) );

                FakeProxy::send( socket, bytes( { 0x05, 0x05, 0x00, 0x01 } ) );

                ( void ) FakeProxy::waitForPeerToClose( socket );
            }
            );

        const auto probe = TunnelProbeImpl::createInstance(
            std::string( "origin.example.com" ),
            static_cast< os::port_t >( 443U ),
            ProxyConfig::socks5( "127.0.0.1", proxy.port() )
            );

        runProbeToCompletion( probe );

        const auto task = om::qi< Task >( probe );

        UTF_REQUIRE( task -> isFailed() );

        UTF_REQUIRE(
            cpp::contains( exceptionMessageOf( task ), std::string( "connection refused" ) )
            );

        UTF_REQUIRE_EQUAL( probe -> countOf( "handshakePathReached" ), 0U );
    }
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_CancelDuringTunnelTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * A cancel which lands while the stage is waiting on the proxy ends the task with
     * operation_aborted classified as an expected exception, and no handshake is attempted
     *
     * THIS IS ALSO WHERE THE TWO LEAK OBLIGATIONS OF THE HOOK ARE EXERCISED. The stage is parked
     * across an asynchronous read with the continuation - which holds a reference to the task -
     * in a member, which is the reference cycle the hook's doc comment warns about; if
     * onTaskStoppedNothrow did not release it the task would outlive the run, and the suite's
     * exit time leak check would say so long after this case had passed. And the stage is woken
     * only because its outstanding operation is on the SOCKET, which the base cancelTask() shuts
     * down - a stage parked on an async object of its own would hang here instead
     *
     * The proxy never answers, so the cancel is the only thing which can end the task and the
     * bounded poll below is a rendezvous rather than a sleep
     */

    FakeProxy proxy(
        []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
        {
            self.record( FakeProxy::readHeaders( socket ) );

            /*
             * Deliberately no response
             */

            self.record( FakeProxy::waitForPeerToClose( socket ) ? "closed" : "spoke" );
        }
        );

    const auto probe = TunnelProbeImpl::createInstance(
        std::string( "origin.example.com" ),
        static_cast< os::port_t >( 443U ),
        ProxyConfig::httpConnect( "127.0.0.1", proxy.port() )
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &task, &proxy ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            for( std::size_t i = 0U; i < 300U && proxy.records().empty(); ++i )
            {
                os::sleep( time::milliseconds( 10 ) );
            }

            UTF_REQUIRE_EQUAL( proxy.records().size(), 1U );

            task -> requestCancel();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE( probe -> wasExpectedExceptionAtStop() );

    UTF_REQUIRE_EQUAL( probe -> countOf( "stageEntered" ), 1U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "handshakePathReached" ), 0U );

    const auto records = proxy.records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "closed" ) );
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_OriginAndProxyStaySeparateTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * THE POINT OF THE SLICE, IN TWO ASSERTIONS. The resolver and the TCP connection target the
     * PROXY, while the name and service handed to createSocket - which under the TLS policy become
     * the SNI and the verified peer name - are the ORIGIN's
     *
     * The origin here is a name which does not resolve and a port nothing listens on, which is
     * what makes the assertion load bearing: if the connection had gone anywhere near the origin
     * the case could not pass at all
     */

    {
        FakeProxy proxy(
            []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
            {
                self.record( FakeProxy::readHeaders( socket ) );

                FakeProxy::send( socket, "HTTP/1.1 200 Connection established\r\n\r\n" );

                ( void ) FakeProxy::waitForPeerToClose( socket );
            }
            );

        const auto probe = TunnelProbeImpl::createInstance(
            std::string( "origin.invalid" ),
            static_cast< os::port_t >( 8443U ),
            ProxyConfig::httpConnect( "127.0.0.1", proxy.port() )
            );

        runProbeToCompletion( probe );

        const auto task = om::qi< Task >( probe );

        UTF_REQUIRE( ! task -> isFailed() );

        UTF_REQUIRE_EQUAL( probe -> resolvedHostName(), std::string( "127.0.0.1" ) );

        UTF_REQUIRE_EQUAL(
            probe -> resolvedServiceName(),
            utils::lexical_cast< std::string >( proxy.port() )
            );

        UTF_REQUIRE_EQUAL( probe -> connectedPort(), proxy.port() );

        UTF_REQUIRE_EQUAL( probe -> m_createSocketHostName, std::string( "origin.invalid" ) );
        UTF_REQUIRE_EQUAL( probe -> m_createSocketServiceName, std::string( "8443" ) );
    }

    /*
     * With no proxy the stage is transparent: the establisher's host and port are the origin's,
     * continueAfterResolved is the base's, and the same name reaches createSocket - so nothing
     * about a direct connection changes because this class is in the chain
     */

    {
        FakeProxy origin(
            []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
            {
                self.record( FakeProxy::waitForPeerToClose( socket ) ? "closed" : "spoke" );
            }
            );

        const auto probe = TunnelProbeImpl::createInstance(
            std::string( "127.0.0.1" ),
            static_cast< os::port_t >( origin.port() ),
            ProxyConfig::none()
            );

        runProbeToCompletion( probe );

        const auto task = om::qi< Task >( probe );

        UTF_REQUIRE( ! task -> isFailed() );

        UTF_REQUIRE_EQUAL( probe -> resolvedHostName(), std::string( "127.0.0.1" ) );
        UTF_REQUIRE_EQUAL( probe -> connectedPort(), origin.port() );
        UTF_REQUIRE_EQUAL( probe -> m_createSocketHostName, std::string( "127.0.0.1" ) );

        UTF_REQUIRE_EQUAL(
            probe -> m_createSocketServiceName,
            utils::lexical_cast< std::string >( origin.port() )
            );

        /*
         * The stage was entered and completed synchronously, inside the connect handler, which is
         * what the default hook does and what the transparent path must go on doing
         */

        const auto events = probe -> events();

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "stageEntered" ) );
        UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "handshakePathReached" ) );
    }
}

UTF_AUTO_TEST_CASE( TcpTunnelStage_StageRunsOncePerAttemptTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tunnel;

    /*
     * THE STAGE CARRIES NOTHING FROM ONE ATTEMPT TO THE NEXT, which is what
     * beginPreHandshakeStage requires of every override and what the handshake retry of
     * scheduleTaskFinishContinuation makes necessary: that retry restarts the whole resolve and
     * connect transaction IN PLACE, so a second attempt re-enters the stage without anything
     * having gone through scheduleNothrow in between
     *
     * The proxy refuses the first connection and accepts the second, and the assertion carrying
     * the weight is that THE TWO REQUESTS ARE IDENTICAL BYTE FOR BYTE. A stage which had kept a
     * response buffer, a parse position or a state machine across the restart would send something
     * different the second time, or read the first attempt's leftovers as the second attempt's
     * reply
     *
     * The restart is reproduced by the probe rather than driven by the real retry, because the
     * real one needs a TLS handshake to fail with a retryable error; the three calls it makes are
     * the production ones. See the probe for the whole of that reasoning
     */

    FakeProxy proxy(
        []( SAA_inout FakeProxy& self, SAA_inout bl::asio::ip::tcp::socket& socket ) -> void
        {
            self.record( FakeProxy::readHeaders( socket ) );

            if( 1U == self.records().size() )
            {
                FakeProxy::send( socket, "HTTP/1.1 503 Service Unavailable\r\n\r\n" );
            }
            else
            {
                FakeProxy::send( socket, "HTTP/1.1 200 Connection established\r\n\r\n" );
            }

            ( void ) FakeProxy::waitForPeerToClose( socket );
        },
        2U /* connections */
        );

    const auto probe = TunnelProbeImpl::createInstance(
        std::string( "origin.example.com" ),
        static_cast< os::port_t >( 443U ),
        ProxyConfig::httpConnect( "127.0.0.1", proxy.port() )
        );

    probe -> allowRetries( 1U );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 4U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "transactionRestarted" ) );
    UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 3 ], std::string( "handshakePathReached" ) );

    UTF_REQUIRE( proxy.failure().empty() );

    const auto records = proxy.records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 0 ], records[ 1 ] );

    UTF_REQUIRE_EQUAL(
        records[ 0 ],
        std::string(
            "CONNECT origin.example.com:443 HTTP/1.1\r\n"
            "Host: origin.example.com:443\r\n"
            "\r\n"
            )
        );
}

#endif /* __UTEST_TESTTCPTUNNELSTAGE_H_ */
