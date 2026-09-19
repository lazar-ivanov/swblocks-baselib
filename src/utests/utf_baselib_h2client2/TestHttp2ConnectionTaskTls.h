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

#ifndef __UTEST_TESTHTTP2CONNECTIONTASKTLS_H_
#define __UTEST_TESTHTTP2CONNECTIONTASKTLS_H_

#include <baselib/tasks/TcpSslStrandedStreams.h>

#include <baselib/crypto/CryptoBase.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.2 - the HTTP/2 driver over TLS (design 5.1, 5.5 and 3.3)
 *
 * The task under test is tasks::Http2ConnectionTaskT< TcpSslSocketAsyncStrandedBase >, driven
 * against the peer of design 8.2 over TLS. The cleartext half of the same driver, and everything
 * about the shell which does not need a handshake, is utf_baselib_h2client2.
 *
 * WHY THIS IS A MODULE OF ITS OWN. Measured: the TLS half costs 9.8 MB of clang debug object, and
 * carrying it and the cleartext half in one module put that module at 46.8 MB - over the 40 MB
 * target of src/utests/AGENTS.md. The two peers and the two driver instantiations are four
 * http2::Session instantiations in one translation unit, and the seam between them is exactly the
 * stream policy, so that is where the cut is.
 *
 * It also carries the first instantiation of Http2TestConnectionT over a TLS stream policy. A
 * template nothing instantiates is not compiled and "builds clean" says nothing about it; this
 * project has been bitten by that twice.
 *
 * The helpers are in utests/baselib/Http2DriverTestUtils.h, shared with the cleartext module.
 */

namespace utest
{
    namespace h2driver
    {
        typedef bl::om::ObjectImpl
            <
                DriverProbeT< bl::tasks::TcpSslSocketAsyncStrandedBase >
            >
            TlsDriverImpl;

        /**
         * @brief class TlsHttp2TestServerT - the peer of design 8.2 over TLS
         *
         * S4.4 could not build this: choosing "h2" is the SERVER half of ALPN and no entry point
         * for it existed in src/include until S4.2 added
         * crypto::CryptoBase::setAlpnServerPreference. Everything else is the cleartext peer -
         * which is the point, since TcpServerBase is what performs the server side handshake and
         * Http2TestConnectionT is already written against a stream policy
         *
         * It is also the first instantiation of Http2TestConnectionT over a TLS policy. A template
         * nothing instantiates is not compiled, and this project has now been bitten by that twice
         */

        template
        <
            typename E = void
        >
        class TlsHttp2TestServerT :
            public h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( TlsHttp2TestServerT )

        public:

            typedef h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase > base_type;

        protected:

            TlsHttp2TestServerT(
                SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >& controlToken,
                SAA_in          const std::vector< std::string >&                preference
                )
                :
                base_type(
                    controlToken,
                    "localhost",
                    0U /* ephemeral */,
                    test::UtfCrypto::getDefaultServerKey(),
                    test::UtfCrypto::getDefaultServerCertificate()
                    )
            {
                /*
                 * The base constructor has built the server context by now - initServerContext( )
                 * runs from TcpServerBase's own constructor when the policy needs a handshake
                 */

                UTF_REQUIRE( nullptr != base_type::m_serverContext.get() );

                bl::crypto::CryptoBase::setAlpnServerPreference(
                    *base_type::m_serverContext,
                    preference
                    );
            }
        };

        typedef bl::om::ObjectImpl< TlsHttp2TestServerT<> > TlsHttp2TestServer;

        inline auto makeTlsPeer( SAA_in const std::vector< std::string >& preference )
            -> bl::om::ObjPtr< TlsHttp2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return TlsHttp2TestServer::createInstance<>( controlToken, preference );
        }

    } // h2driver

} // utest

/**
 * @brief h2 over TLS, on the library's own client path, against a TLS peer which selects "h2"
 *
 * THE SERVER HALF OF ALPN IS WHAT MADE THIS POSSIBLE. Until S4.2 added
 * crypto::CryptoBase::setAlpnServerPreference the tree had the client half only, so no TLS peer
 * could choose h2 and S4.4's peer was cleartext by necessity. This is also the first instantiation
 * of the peer's connection task over a TLS stream policy
 *
 * The host is "localhost" because the client verifies the peer name - UtfMain registers the dev
 * root CA for every test binary and the test server certificate is issued for that name
 *
 * WHAT IS OWED: the same case on OpenSSL 1.1.1w. No dist on this machine carries that flavor and
 * BL_USE_OPENSSL_1X=1 does not build for pre-existing reasons - see
 * notes/plans/issues/openssl-1x-flavor-deferral.md
 */

UTF_AUTO_TEST_CASE( H2Driver_TlsAlpnSelectsHttp2Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    std::vector< std::string > preference;

    preference.push_back( "h2" );
    preference.push_back( "http/1.1" );

    const auto peer = makeTlsPeer( preference );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                "https" == request.scheme,
                BL_MSG()
                    << "The peer expected an https scheme, got: "
                    << request.scheme
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "secure" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = TlsDriverImpl::createInstance(
                makeKey( "https", "localhost", port ),
                makeFallbackFactory< TcpSslSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                ClientConnectionConfig()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest(
                            "https://localhost:" +
                            utils::lexical_cast< std::string >( port ) +
                            "/secure"
                            ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            /*
             * The identifier the peer selected, verbatim - not one derived from the protocol
             */

            UTF_REQUIRE( HttpProtocol::Http2 == connection -> negotiated().protocol() );
            UTF_REQUIRE_EQUAL( connection -> negotiated().alpn(), std::string( "h2" ) );

            UTF_REQUIRE_EQUAL( record -> creations, 0U );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "secure" ) );
        }
        );
}

/**
 * @brief The ALPN selection order is the SERVER's, not the client's
 *
 * The client offers "h2, http/1.1" and the server prefers "http/1.1, h2"; the server's order wins,
 * so the connection is HTTP/1.1 and this h2 task hands the connected stream to the factory - which
 * is design 5.5's fallback and the only thing the factory is for on this path
 */

UTF_AUTO_TEST_CASE( H2Driver_TlsAlpnServerPreferenceWinsTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    std::vector< std::string > preference;

    preference.push_back( "http/1.1" );
    preference.push_back( "h2" );

    const auto peer = makeTlsPeer( preference );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = TlsDriverImpl::createInstance(
                makeKey( "https", "localhost", port ),
                makeFallbackFactory< TcpSslSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                ClientConnectionConfig()
                );

            runDriver( driver, []() -> void {} );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            UTF_REQUIRE( HttpProtocol::Http11 == connection -> negotiated().protocol() );
            UTF_REQUIRE_EQUAL( connection -> negotiated().alpn(), std::string( "http/1.1" ) );

            /*
             * The stream went to the HTTP/1.1 driver, once, carrying the identifier the peer chose
             */

            UTF_REQUIRE_EQUAL( record -> creations, 1U );
            UTF_REQUIRE_EQUAL( record -> alpn, std::string( "http/1.1" ) );
        }
        );
}

#endif /* __UTEST_TESTHTTP2CONNECTIONTASKTLS_H_ */
