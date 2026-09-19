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

#ifndef __UTEST_TESTCLIENTCONNECTIONTASKBASE_H_
#define __UTEST_TESTCLIENTCONNECTIONTASKBASE_H_

#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpTunnelStage.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.1 - connection establishment and ALPN dispatch (design 5.1, 5.5, 5.7)
 *
 * The task under test is
 * tasks::ClientConnectionTaskBaseT< STREAM > =
 *     MultiOperationTaskT< TcpTunnelStageT< TcpConnectionEstablisherConnector< STREAM > > >,
 * run over both stranded stream policies of design 3.1 against loopback peers.
 *
 * WHAT THESE CASES ARE ABOUT, since it is easy to mistake them for TLS tests. The TLS rules
 * themselves belong to other slices and are tested there - the floor is S3.4's, the ALPN accessors
 * are S1.6's, the tunnel wire formats are S3.5's. What this task owns is the ORDER and the
 * DISPATCH: that the floor is checked before anything reads the connection, that an empty ALPN
 * selection is turned into a value through withoutAlpn and never through fromAlpn, that the whole
 * NegotiatedProtocol reaches the driver factory, and that the deadline which bounds a silent proxy
 * is armed before the tunnel stage rather than after it. Every case therefore asserts an ordered
 * event list as well as an outcome.
 *
 * The helpers here are deliberately SELF CONTAINED and do not reach into the S3.2 / S3.3 headers of
 * this directory, although they are the same module today. A test header which includes a sibling
 * cannot be moved to a numbered sibling module later without breaking (src/utests/AGENTS.md forbids
 * a cross-module test include), and this module is the one design 8.1 expects to grow most.
 */

namespace utest
{
    namespace clientconnect
    {
        /**
         * @brief The loopback machinery the three peers share
         *
         * Binding port zero is what keeps these cases free of the machine global test lock, and
         * every operation is deadline bounded so a client which never arrives fails the case on its
         * own deadline rather than hanging it
         */

        class ConnectPeerBase
        {
            BL_NO_COPY_OR_MOVE( ConnectPeerBase )

        public:

            enum : long
            {
                DEFAULT_TIMEOUT_IN_SECONDS = 30L,
            };

            ConnectPeerBase()
                :
                m_acceptor( m_ioService ),
                m_timer( m_ioService )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();
            }

            auto port() const -> bl::os::port_t
            {
                return m_acceptor.local_endpoint().port();
            }

        protected:

            template
            <
                typename CANCELABLE
            >
            void armDeadline(
                SAA_inout       CANCELABLE&                                     cancelable,
                SAA_in          const bl::time::time_duration&                  timeout =
                                    bl::time::seconds( DEFAULT_TIMEOUT_IN_SECONDS )
                )
            {
                m_timer.expires_from_now( timeout );

                m_timer.async_wait(
                    [ &cancelable ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        if( bl::asio::error::operation_aborted != ec )
                        {
                            bl::eh::error_code cancelEc;

                            cancelable.cancel( cancelEc );
                        }
                    }
                    );
            }

            void runService()
            {
                #if ( ( BOOST_VERSION / 100 ) >= 1066 )
                m_ioService.restart();
                #else
                m_ioService.reset();
                #endif

                m_ioService.run();
            }

            void acceptOne( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
                bl::eh::error_code acceptEc;
                bool acceptCompleted = false;

                m_acceptor.async_accept(
                    socket,
                    [ this, &acceptEc, &acceptCompleted ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        acceptEc = ec;
                        acceptCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( m_acceptor );

                runService();

                UTF_REQUIRE( acceptCompleted );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), acceptEc );
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::deadline_timer                                            m_timer;
        };

        /**
         * @brief The cleartext peer - it accepts and then does nothing
         *
         * Nothing more is needed: this slice writes no byte of any protocol, it hands the connected
         * stream to a driver and finishes
         */

        class CleartextConnectPeer :
            public ConnectPeerBase
        {
            BL_NO_COPY_OR_MOVE( CleartextConnectPeer )

        public:

            CleartextConnectPeer()
                :
                m_socket( m_ioService )
            {
            }

            void acceptAndIdle()
            {
                acceptOne( m_socket );
            }

        private:

            bl::asio::ip::tcp::socket                                           m_socket;
        };

        /**
         * @brief The TLS peer, with the ALPN selection under the case's control
         *
         * The server side of the handshake is driven synchronously from the test thread, as the TLS
         * peers of this suite's other headers do it, and the handshaken stream is kept alive by the
         * peer so the client is never raced by the socket going away underneath it
         *
         * The selection is made by an ::SSL_CTX_set_alpn_select_cb callback rather than by OpenSSL's
         * own preference helper, because two of these cases need a selection OpenSSL would not make
         * on its own: none at all, and an identifier this build does not speak
         */

        class TlsConnectPeer :
            public ConnectPeerBase
        {
            BL_NO_COPY_OR_MOVE( TlsConnectPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            /**
             * @brief An empty selection means the peer acknowledges no protocol at all, which is
             * what a server does when it speaks none of what was offered
             */

            TlsConnectPeer( SAA_in std::string alpnSelection = std::string() )
                :
                m_alpnSelection( BL_PARAM_FWD( alpnSelection ) ),
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    )
            {
                ::SSL_CTX_set_alpn_select_cb(
                    m_serverContext -> native_handle(),
                    &TlsConnectPeer::onAlpnSelect,
                    &m_alpnSelection
                    );
            }

            void acceptAndHandshake()
            {
                m_stream = bl::cpp::SafeUniquePtr< sslstream_t >::attach(
                    new sslstream_t( m_ioService, *m_serverContext )
                    );

                acceptOne( m_stream -> next_layer() );

                bl::eh::error_code ec;

                m_stream -> handshake( bl::asio::ssl::stream_base::server, ec );

                UTF_REQUIRE_EQUAL( bl::eh::error_code(), ec );
            }

        private:

            /**
             * @brief The OpenSSL ALPN selection callback - a plain function, since it is a C
             * callback and cannot capture
             *
             * The selected name is returned by POINTER and OpenSSL copies it only later, so it has
             * to outlive the callback. It is the peer's own member, handed over through the opaque
             * argument, which is what keeps that lifetime obvious
             */

            static int onAlpnSelect(
                SAA_inout       ::SSL*                                          ssl,
                SAA_out         const unsigned char**                           out,
                SAA_out         unsigned char*                                  outLen,
                SAA_in          const unsigned char*                            in,
                SAA_in          const unsigned int                              inLen,
                SAA_inout_opt   void*                                           arg
                )
            {
                BL_UNUSED( ssl );
                BL_UNUSED( in );
                BL_UNUSED( inLen );

                const auto* const selection = static_cast< const std::string* >( arg );

                if( nullptr == selection || selection -> empty() )
                {
                    return SSL_TLSEXT_ERR_NOACK;
                }

                *out = reinterpret_cast< const unsigned char* >( selection -> c_str() );
                *outLen = static_cast< unsigned char >( selection -> size() );

                return SSL_TLSEXT_ERR_OK;
            }

            /*
             * Not const: ::SSL_CTX_set_alpn_select_cb takes its opaque argument as void*, so a
             * const member could not be handed to it at all. Nothing writes it after the
             * constructor
             */

            std::string                                                         m_alpnSelection;

            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;
            bl::cpp::SafeUniquePtr< sslstream_t >                               m_stream;
        };

        /**
         * @brief A proxy which accepts, records what was asked of it, and then never answers
         *
         * This is the peer design 5.7's connect deadline exists for. The tunnel stage owns no timer
         * - a timer inside it would acquire obligation 2 of beginPreHandshakeStage with it - so a
         * proxy which behaves like this holds the task until something outside the stage gives up
         */

        class SilentProxyPeer :
            public ConnectPeerBase
        {
            BL_NO_COPY_OR_MOVE( SilentProxyPeer )

        public:

            enum : std::size_t
            {
                READ_BUFFER_SIZE = 1024U,
            };

            SilentProxyPeer()
                :
                m_socket( m_ioService )
            {
            }

            /**
             * @brief Accepts one connection and reads whatever the client asks for, once
             */

            void acceptAndRecordRequest()
            {
                acceptOne( m_socket );

                std::vector< char > buffer( static_cast< std::size_t >( READ_BUFFER_SIZE ) );

                bl::eh::error_code readEc;
                std::size_t bytesRead = 0U;
                bool readCompleted = false;

                m_socket.async_read_some(
                    bl::asio::buffer( buffer.data(), buffer.size() ),
                    [ this, &readEc, &bytesRead, &readCompleted ](
                        SAA_in      const bl::eh::error_code&                   ec,
                        SAA_in      const std::size_t                           transferred
                        ) -> void
                    {
                        readEc = ec;
                        bytesRead = transferred;
                        readCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( m_socket );

                runService();

                UTF_REQUIRE( readCompleted );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), readEc );

                m_request.assign( buffer.data(), bytesRead );
            }

            const std::string& request() const NOEXCEPT
            {
                return m_request;
            }

        private:

            bl::asio::ip::tcp::socket                                           m_socket;
            std::string                                                         m_request;
        };

        /**
         * @brief The driver the factory builds - the smallest honest implementation of the S2.6
         * contract
         *
         * It holds the connected stream, because that is the whole point of the hand-off: after
         * createDriver the driver owns it and the establisher does not. The negotiated value is a
         * CONST member, which is what makes the reference-returning negotiated() safe to read off
         * the strand - a driver which ever reassigned it would be a data race on an std::string
         */

        template
        <
            typename STREAM
        >
        class StubDriverT : public bl::httpclient::ClientConnection
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubDriverT, bl::httpclient::ClientConnection )

        protected:

            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

            const bl::httpclient::NegotiatedProtocol                            m_negotiated;
            const typename STREAM::stream_ref                                   m_connectedStream;

            StubDriverT(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_inout       typename STREAM::stream_ref&&                   connectedStream
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) ),
                m_connectedStream( BL_PARAM_FWD( connectedStream ) )
            {
            }

        public:

            bool hasStream() const NOEXCEPT
            {
                return nullptr != m_connectedStream.get();
            }

            virtual auto submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                )
                -> stream_handle_t OVERRIDE
            {
                BL_UNUSED( request );
                BL_UNUSED( eventSink );

                return bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE;
            }

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( errorCode );
            }

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( bytes );
            }

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const bl::om::ObjPtr< bl::data::DataBlock >&    data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( data );
                BL_UNUSED( endStream );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                return 1U;
            }

            virtual auto state() const NOEXCEPT -> bl::httpclient::ConnectionState OVERRIDE
            {
                return bl::httpclient::ConnectionState::Ready;
            }

            virtual auto negotiated() const NOEXCEPT -> const bl::httpclient::NegotiatedProtocol& OVERRIDE
            {
                return m_negotiated;
            }
        };

        /**
         * @brief What the factory saw, so a case can assert on the hand-off itself and not only on
         * what the driver reports afterwards
         */

        struct DriverRecord
        {
            std::size_t                                                         creations;
            bool                                                                gotStream;
            std::string                                                         host;
            bl::os::port_t                                                      port;

            DriverRecord()
                :
                creations( 0U ),
                gotStream( false ),
                port( 0U )
            {
            }
        };

        /**
         * @brief A factory with a stub driver registered for each of the given protocols
         */

        template
        <
            typename STREAM
        >
        auto makeDriverFactory(
            SAA_in          const std::shared_ptr< DriverRecord >&              record,
            SAA_in          const std::vector< bl::httpclient::HttpProtocol >&  protocols
            )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< STREAM > >
        {
            typedef bl::httpclient::ClientDriverFactoryT< STREAM >              factory_t;
            typedef bl::om::ObjectImpl< StubDriverT< STREAM > >                 driver_impl_t;

            auto factory = std::make_shared< factory_t >();

            for( const auto protocol : protocols )
            {
                factory -> registerDriver(
                    protocol,
                    [ record ](
                        SAA_in      const bl::httpclient::NegotiatedProtocol&   negotiated,
                        SAA_inout   typename STREAM::stream_ref&&               connectedStream,
                        SAA_in      const bl::httpclient::ConnectionKey&        key
                        )
                        -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                    {
                        record -> creations += 1U;
                        record -> gotStream = ( nullptr != connectedStream.get() );
                        record -> host = key.host;
                        record -> port = key.port;

                        auto driver = driver_impl_t::createInstance(
                            bl::cpp::copy( negotiated ),
                            BL_PARAM_FWD( connectedStream )
                            );

                        return bl::om::qi< bl::httpclient::ClientConnection >( driver );
                    }
                    );
            }

            return factory;
        }

        inline auto makeKey(
            SAA_in          std::string                                         host,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::httpclient::ConnectionKey
        {
            bl::httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = BL_PARAM_FWD( host );
            key.port = port;

            return key;
        }

        /**
         * @brief The task under test, with the three points a case needs to see recorded
         *
         * It adds no behavior of its own beyond the optional floor failure. The order of the three
         * events is the property these cases are written for: the floor is checked first, the
         * protocol is read second, and only then does a driver exist
         */

        template
        <
            typename STREAM
        >
        class ConnectProbeT :
            public bl::tasks::ClientConnectionTaskBaseT< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( ConnectProbeT )

        public:

            typedef ConnectProbeT< STREAM >                                     this_type;
            typedef bl::tasks::ClientConnectionTaskBaseT< STREAM >              base_type;

        protected:

            mutable bl::os::mutex                                               m_eventsLock;
            std::vector< std::string >                                          m_events;

            bl::cpp::ScalarTypeIniter< bool >                                   m_failFloor;

            ConnectProbeT(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          typename base_type::factory_ptr_t               driverFactory,
                SAA_in          bl::tasks::ProxyConfig                          proxyConfig,
                SAA_in          bl::tasks::ClientConnectionConfig               config,
                SAA_in          const bool                                      failFloor
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( proxyConfig ),
                    BL_PARAM_FWD( config ),
                    false /* logExceptions */
                    )
            {
                m_failFloor = failFloor;
            }

            void record( SAA_in const char* event )
            {
                BL_MUTEX_GUARD( m_eventsLock );

                m_events.push_back( std::string( event ) );
            }

            virtual void chkNegotiatedParametersMeetFloor() OVERRIDE
            {
                record( "floor" );

                if( m_failFloor )
                {
                    /*
                     * What a below-floor negotiated suite would do, without a peer which can
                     * produce one. A client context at security level 2 refuses every such suite
                     * during the handshake, so the connection would never reach this point at all
                     * and the ORDER this case is about could not be observed
                     */

                    BL_THROW(
                        bl::SecurityException(),
                        BL_MSG()
                            << "The negotiated TLS parameters are below the floor"
                        );
                }

                base_type::chkNegotiatedParametersMeetFloor();
            }

            virtual bool onProtocolNegotiated() OVERRIDE
            {
                record( "negotiated" );

                const bool continueTask = base_type::onProtocolNegotiated();

                record( "driver" );

                return continueTask;
            }

        public:

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_eventsLock );

                return m_events;
            }
        };

        /**
         * @brief Runs the probe to completion while the peer does its part on the test thread
         */

        template
        <
            typename PROBE,
            typename PEERWORK
        >
        inline void runProbe(
            SAA_in          const bl::om::ObjPtr< PROBE >&                      probe,
            SAA_in          const PEERWORK&                                     peerWork
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ &task, &peerWork ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    peerWork();

                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );
                }
                );
        }

        /**
         * @brief Fails the case with the message the task failed with, rather than with a bare
         * "isFailed() has failed" which says nothing about why
         */

        inline void chkTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;

            if( ! task -> isFailed() )
            {
                UTF_REQUIRE( ! task -> exception() );

                return;
            }

            std::string message( "<no exception>" );

            if( task -> exception() )
            {
                try
                {
                    cpp::safeRethrowException( task -> exception() );
                }
                catch( std::exception& e )
                {
                    message = e.what();
                }
            }

            UTF_FAIL( "the connection probe task failed: " + message );
        }

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

            return std::string();
        }

        /**
         * @brief The events of a run which reached a driver
         */

        inline void chkEstablishedEvents( SAA_in const std::vector< std::string >& events )
        {
            UTF_REQUIRE_EQUAL( events.size(), 3U );
            UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "floor" ) );
            UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "negotiated" ) );
            UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "driver" ) );
        }

        typedef bl::om::ObjectImpl< ConnectProbeT< bl::tasks::TcpSocketAsyncStrandedBase > >
            PlainConnectProbeImpl;

        typedef bl::om::ObjectImpl< ConnectProbeT< bl::tasks::TcpSslSocketAsyncStrandedBase > >
            TlsConnectProbeImpl;

    } // clientconnect

} // utest

UTF_AUTO_TEST_CASE( H2Connect_CleartextDispatchesWithoutAlpnTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * A cleartext connection is never decided by ALPN, so it goes through withoutAlpn and reports
     * no identifier at all. What it speaks is the configuration's - HTTP/1.1 by default, and
     * HTTP/2 for a peer known to speak it by prior knowledge (RFC 9113 section 3.3)
     *
     * The ALPN offer is left at its default in both halves on purpose: a cleartext connection
     * carries no ALPN extension, so an offer says nothing about it and must not be mistaken for
     * one. If it were honored here the first half would report "h2"
     */

    {
        CleartextConnectPeer peer;

        const auto record = std::make_shared< DriverRecord >();

        const auto probe = PlainConnectProbeImpl::createInstance(
            makeKey( std::string( "127.0.0.1" ), peer.port() ),
            makeDriverFactory< TcpSocketAsyncStrandedBase >(
                record,
                { HttpProtocol::Http11, HttpProtocol::Http2 }
                ),
            ProxyConfig::none(),
            ClientConnectionConfig(),
            false /* failFloor */
            );

        runProbe( probe, [ &peer ]() -> void { peer.acceptAndIdle(); } );

        chkTaskSucceeded( om::qi< Task >( probe ) );

        chkEstablishedEvents( probe -> events() );

        UTF_REQUIRE( HttpProtocol::Http11 == probe -> negotiated().protocol() );
        UTF_REQUIRE( ! probe -> negotiated().hasAlpn() );
        UTF_REQUIRE( probe -> negotiated().alpn().empty() );

        UTF_REQUIRE( nullptr != probe -> connection().get() );
        UTF_REQUIRE( HttpProtocol::Http11 == probe -> connection() -> negotiated().protocol() );

        /*
         * The hand-off itself: exactly one driver was built, it was given the connected stream,
         * and it was told which origin it belongs to
         */

        UTF_REQUIRE_EQUAL( record -> creations, 1U );
        UTF_REQUIRE( record -> gotStream );
        UTF_REQUIRE_EQUAL( record -> host, std::string( "127.0.0.1" ) );
        UTF_REQUIRE_EQUAL( record -> port, peer.port() );
    }

    {
        CleartextConnectPeer peer;

        const auto record = std::make_shared< DriverRecord >();

        auto config = ClientConnectionConfig();
        config.cleartextProtocol = HttpProtocol::Http2;

        const auto probe = PlainConnectProbeImpl::createInstance(
            makeKey( std::string( "127.0.0.1" ), peer.port() ),
            makeDriverFactory< TcpSocketAsyncStrandedBase >(
                record,
                { HttpProtocol::Http11, HttpProtocol::Http2 }
                ),
            ProxyConfig::none(),
            config,
            false /* failFloor */
            );

        runProbe( probe, [ &peer ]() -> void { peer.acceptAndIdle(); } );

        chkTaskSucceeded( om::qi< Task >( probe ) );

        UTF_REQUIRE( HttpProtocol::Http2 == probe -> negotiated().protocol() );
        UTF_REQUIRE( ! probe -> negotiated().hasAlpn() );

        UTF_REQUIRE_EQUAL( record -> creations, 1U );
    }
}

UTF_AUTO_TEST_CASE( H2Connect_TlsAlpnSelectsHttp2Tests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * The ordinary path: the browser style offer is "h2, http/1.1", the peer selects h2, and the
     * value which reaches the driver carries the identifier the peer selected VERBATIM rather than
     * one derived from the protocol
     *
     * The host is "localhost" because the client verifies the peer name - UtfMain registers the dev
     * root CA for every test binary and the test server certificate is issued for that name
     */

    TlsConnectPeer peer( std::string( "h2" ) );

    const auto record = std::make_shared< DriverRecord >();

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::none(),
        ClientConnectionConfig(),
        false /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    chkTaskSucceeded( om::qi< Task >( probe ) );

    chkEstablishedEvents( probe -> events() );

    UTF_REQUIRE( HttpProtocol::Http2 == probe -> negotiated().protocol() );
    UTF_REQUIRE( probe -> negotiated().hasAlpn() );
    UTF_REQUIRE_EQUAL( probe -> negotiated().alpn(), std::string( "h2" ) );

    UTF_REQUIRE( nullptr != probe -> connection().get() );
    UTF_REQUIRE_EQUAL( probe -> connection() -> negotiated().alpn(), std::string( "h2" ) );

    UTF_REQUIRE_EQUAL( record -> creations, 1U );
    UTF_REQUIRE( record -> gotStream );
}

UTF_AUTO_TEST_CASE( H2Connect_TlsForcedHttp11Tests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * Forcing HTTP/1.1 is done by not offering "h2" at all, so a peer which would have selected it
     * cannot: a peer may only select from what it was offered (RFC 7301 section 3.1), and OpenSSL
     * validates the answer against the offer on the client side besides. Refusing an "h2" the peer
     * legitimately selected would be a connection failure instead of a fallback, which is why the
     * forcing is done in the offer and not after the handshake
     */

    TlsConnectPeer peer( std::string( "http/1.1" ) );

    const auto record = std::make_shared< DriverRecord >();

    const auto config = ClientConnectionConfig::forcedHttp11();

    UTF_REQUIRE_EQUAL( config.alpnOffer.size(), 1U );
    UTF_REQUIRE_EQUAL( config.alpnOffer[ 0 ], std::string( "http/1.1" ) );

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::none(),
        config,
        false /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    chkTaskSucceeded( om::qi< Task >( probe ) );

    chkEstablishedEvents( probe -> events() );

    UTF_REQUIRE( HttpProtocol::Http11 == probe -> negotiated().protocol() );
    UTF_REQUIRE( probe -> negotiated().hasAlpn() );
    UTF_REQUIRE_EQUAL( probe -> negotiated().alpn(), std::string( "http/1.1" ) );

    UTF_REQUIRE_EQUAL( record -> creations, 1U );
}

UTF_AUTO_TEST_CASE( H2Connect_TlsWithNoAlpnSelectionFallsBackTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * The peer completed the handshake and selected NOTHING. NegotiatedProtocol::fromAlpn( "" )
     * throws by design, so this connection must go through withoutAlpn instead - and it must report
     * an EMPTY identifier rather than "http/1.1", because a client which fell back did not have
     * that chosen for it. That distinction is the whole reason
     * ClientResponse::negotiatedAlpn() says "verbatim", and it is the one case a value derived from
     * the protocol alone gets wrong
     *
     * A task which passed the empty selection to fromAlpn would fail here rather than fall back,
     * which is what makes this a real check and not a restatement of the previous case
     */

    TlsConnectPeer peer;

    const auto record = std::make_shared< DriverRecord >();

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::none(),
        ClientConnectionConfig(),
        false /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    chkTaskSucceeded( om::qi< Task >( probe ) );

    chkEstablishedEvents( probe -> events() );

    UTF_REQUIRE( HttpProtocol::Http11 == probe -> negotiated().protocol() );
    UTF_REQUIRE( ! probe -> negotiated().hasAlpn() );
    UTF_REQUIRE( probe -> negotiated().alpn().empty() );

    UTF_REQUIRE( nullptr != probe -> connection().get() );
    UTF_REQUIRE( probe -> connection() -> negotiated().alpn().empty() );

    UTF_REQUIRE_EQUAL( record -> creations, 1U );
}

UTF_AUTO_TEST_CASE( H2Connect_TlsUnknownAlpnIsRefusedTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * A NON-empty selection always goes through fromAlpn, which refuses an identifier this build
     * does not speak rather than falling back to HTTP/1.1. Quietly speaking something other than
     * what was negotiated is how a client ends up sending HTTP/1.1 into an h2 connection
     *
     * The offer has to contain the identifier for the peer to be able to select it - OpenSSL
     * validates the server's answer against what the client offered - so this is what a build which
     * offers a protocol it has no driver for looks like
     */

    TlsConnectPeer peer( std::string( "spdy/3.1" ) );

    const auto record = std::make_shared< DriverRecord >();

    auto config = ClientConnectionConfig();
    config.alpnOffer.clear();
    config.alpnOffer.push_back( "spdy/3.1" );

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::none(),
        config,
        false /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        cpp::contains(
            exceptionMessageOf( task ),
            std::string( "ALPN protocol which this build does not speak" )
            )
        );

    /*
     * The refusal happened while the protocol was being read, so nothing was ever handed over
     */

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 1U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "floor" ) );

    UTF_REQUIRE( HttpProtocol::Unknown == probe -> negotiated().protocol() );
    UTF_REQUIRE( nullptr == probe -> connection().get() );
    UTF_REQUIRE_EQUAL( record -> creations, 0U );
}

UTF_AUTO_TEST_CASE( H2Connect_NoDriverForNegotiatedProtocolIsRefusedTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * The same refusal one layer down: the peer selected a protocol this build speaks, but no
     * driver is registered for it. The factory refuses rather than substituting the other one, and
     * the connection fails after the protocol was read and before any driver existed
     */

    TlsConnectPeer peer( std::string( "h2" ) );

    const auto record = std::make_shared< DriverRecord >();

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >( record, { HttpProtocol::Http11 } ),
        ProxyConfig::none(),
        ClientConnectionConfig(),
        false /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        cpp::contains(
            exceptionMessageOf( task ),
            std::string( "No HTTP client driver is registered" )
            )
        );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "floor" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "negotiated" ) );

    UTF_REQUIRE( HttpProtocol::Http2 == probe -> negotiated().protocol() );
    UTF_REQUIRE( nullptr == probe -> connection().get() );
    UTF_REQUIRE_EQUAL( record -> creations, 0U );
}

UTF_AUTO_TEST_CASE( H2Connect_FloorCheckPrecedesEverythingTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * Design 3.3 (D4) requires the floor check before any HTTP byte. What this task owns is that
     * ORDER, which the event list states exactly: the floor is the FIRST thing that happens after
     * the handshake, so a connection which fails it has neither read its ALPN nor built a driver,
     * and nothing above the task ever sees it
     *
     * The floor itself is refused here by the probe rather than by a peer, because a peer which
     * negotiates a below-floor suite cannot be built: the client context is at security level 2 and
     * the handshake would fail first, at which point this order could not be observed at all. The
     * rule the real check applies is crypto::CryptoBase's and is tested in utf_baselib_h2profiles
     */

    TlsConnectPeer peer( std::string( "h2" ) );

    const auto record = std::make_shared< DriverRecord >();

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), peer.port() ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::none(),
        ClientConnectionConfig(),
        true /* failFloor */
        );

    runProbe( probe, [ &peer ]() -> void { peer.acceptAndHandshake(); } );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        cpp::contains(
            exceptionMessageOf( task ),
            std::string( "below the floor" )
            )
        );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 1U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "floor" ) );

    UTF_REQUIRE( HttpProtocol::Unknown == probe -> negotiated().protocol() );
    UTF_REQUIRE( nullptr == probe -> connection().get() );
    UTF_REQUIRE_EQUAL( record -> creations, 0U );
}

UTF_AUTO_TEST_CASE( H2Connect_SilentProxyHitsTheConnectDeadlineTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace bl::tasks;
    using namespace utest::clientconnect;

    /*
     * A proxy which accepts the TCP connection and then never answers is what design 5.7's connect
     * deadline exists to catch, and it is the only thing which catches it: the tunnel stage owns no
     * timer of its own - a timer inside it would acquire obligation 2 of beginPreHandshakeStage
     * with it - and the establisher has no connect deadline either. A deadline armed AFTER the
     * stage returned would never be reached on this path at all
     *
     * The case is run over the TLS stream policy deliberately. The CONNECT it asserts on is written
     * in CLEARTEXT on the socket underneath the TLS engine, which is what design 3.6 requires and
     * what the stage did not even compile for before this slice: a write through the ssl stream
     * would have put a ClientHello on the wire instead, and a write through getSocket() would not
     * have built
     *
     * The deadline is 500ms and not 60s for the obvious reason; what the case shows is that the
     * configured value is what bounds the task, not that 60 is the right number
     */

    SilentProxyPeer proxy;

    const auto record = std::make_shared< DriverRecord >();

    auto config = ClientConnectionConfig();
    config.connectTimeout = time::milliseconds( 500 );

    const auto probe = TlsConnectProbeImpl::createInstance(
        makeKey( std::string( "localhost" ), static_cast< os::port_t >( 443 ) ),
        makeDriverFactory< TcpSslSocketAsyncStrandedBase >(
            record,
            { HttpProtocol::Http11, HttpProtocol::Http2 }
            ),
        ProxyConfig::httpConnect( std::string( "127.0.0.1" ), proxy.port() ),
        config,
        false /* failFloor */
        );

    const auto before = time::microsec_clock::universal_time();

    runProbe( probe, [ &proxy ]() -> void { proxy.acceptAndRecordRequest(); } );

    const auto elapsed = time::microsec_clock::universal_time() - before;

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    /*
     * The tunnel really was entered, in cleartext, and it asked for the origin this task was
     * created for
     */

    UTF_REQUIRE( cpp::contains( proxy.request(), std::string( "CONNECT localhost:443 HTTP/1.1" ) ) );

    /*
     * Nothing beyond the tunnel ever ran - no handshake, so no floor check, no protocol and no
     * driver
     */

    UTF_REQUIRE( probe -> events().empty() );
    UTF_REQUIRE( HttpProtocol::Unknown == probe -> negotiated().protocol() );
    UTF_REQUIRE( nullptr == probe -> connection().get() );
    UTF_REQUIRE_EQUAL( record -> creations, 0U );

    /*
     * And it was the deadline which ended it rather than the case's patience. The bound is generous
     * because a loaded machine is not a fast one; what would fail here is a deadline which was
     * never armed, which is a run that does not end at all
     */

    UTF_REQUIRE( elapsed < time::seconds( 30 ) );
}

#endif /* __UTEST_TESTCLIENTCONNECTIONTASKBASE_H_ */
