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

#ifndef __UTEST_TESTCLIENTSESSIONTLS_H_
#define __UTEST_TESTCLIENTSESSIONTLS_H_

#include <baselib/httpclient/ClientSession.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>

#include <baselib/crypto/CryptoBase.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S6.1 - the client session over TLS (design 5.6, 5.8 and 3.3)
 *
 * The type under test is httpclient::ClientSessionT< tasks::TcpSslSocketAsyncStrandedBase >, which
 * until this module existed was a template nothing instantiated - and so a claim rather than a
 * fact. The cleartext half is utf_baselib_httpclient5's sibling, utf_baselib_httpclient4.
 *
 * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev root
 * CA for every test binary and the test server certificate is issued for that name.
 */

namespace utest
{
    namespace sessiontls
    {
        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        typedef bl::httpclient::ClientSessionImplT< tls_stream_t >              TlsSessionImpl;

        /**
         * @brief class TlsPeerT - the peer of design 8.2 over TLS, choosing from an ALPN preference
         *
         * Self contained rather than reaching into utf_baselib_h2client3's own TLS peer: a test
         * header which includes a sibling module's header silently duplicates its cases into two
         * binaries (src/utests/AGENTS.md)
         */

        template
        <
            typename E = void
        >
        class TlsPeerT :
            public h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( TlsPeerT )

        public:

            typedef h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase > base_type;

        protected:

            TlsPeerT(
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

        typedef bl::om::ObjectImpl< TlsPeerT<> >                                TlsPeer;

        inline auto makeTlsPeer( SAA_in const std::vector< std::string >& preference )
            -> bl::om::ObjPtr< TlsPeer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return TlsPeer::createInstance<>( controlToken, preference );
        }

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

        inline auto runRequest(
            SAA_in          const bl::om::ObjPtr< TlsSessionImpl >&             session,
            SAA_in          const bl::httpclient::ClientRequest&                request
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientRequestTask >
        {
            auto requestTask = session -> createRequestTask( request );

            const auto task = bl::om::qi< bl::tasks::Task >( requestTask );

            runSessionTask( task );

            if( task -> isFailed() )
            {
                UTF_FAIL(
                    "the session request task failed: " +
                    bl::eh::diagnostic_information( task -> exception() )
                    );
            }

            return requestTask;
        }

        inline auto statsOf( SAA_in const bl::om::ObjPtr< TlsSessionImpl >& session )
            -> bl::httpclient::ConnectionPoolImpl::Stats
        {
            return bl::om::qi< bl::httpclient::ConnectionPoolImpl >( session -> pool() ) -> stats();
        }

        /**
         * @brief class StringBodySourceT - a streaming upload which really does produce bytes and
         * really can rewind
         *
         * Http2DriverTestUtils' StubBodySource exists to make a request LOOK streaming for the
         * driver's own cases and never produces anything, which is right there and useless here:
         * what this module needs is a source the request task can drain to completion
         */

        template
        <
            typename E = void
        >
        class StringBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StringBodySourceT, bl::httpclient::BodySource )

        protected:

            const std::string                                                   m_payload;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_offset;

            StringBodySourceT( SAA_in std::string payload )
                :
                m_payload( BL_PARAM_FWD( payload ) )
            {
            }

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                bl::httpclient::BodyReadResult result;

                const auto room = target.capacity() - target.size();
                const auto left = m_payload.size() - m_offset;
                const auto count = std::min< std::size_t >( room, left );

                if( count )
                {
                    std::memcpy( target.begin() + target.size(), m_payload.c_str() + m_offset, count );

                    target.setSize( target.size() + count );

                    m_offset = m_offset.value() + count;
                }

                result.size = count;
                result.isEndOfStream = ( m_offset == m_payload.size() );

                return result;
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return true;
            }

            virtual void rewind() OVERRIDE
            {
                m_offset = 0U;
            }
        };

        typedef bl::om::ObjectImpl< StringBodySourceT<> >                       StringBodySource;

    } // sessiontls

} // utest

/**
 * @brief A GET over h2 and TLS through the session, with ALPN choosing "h2"
 *
 * THE FIRST INSTANTIATION OF ClientSessionT OVER A TLS STREAM POLICY, and therefore the first
 * compilation of it: everything in utf_baselib_httpclient4 is the cleartext policy. What it then
 * exercises is the whole establishment the cleartext half cannot - the handshake, the negotiated
 * parameter floor of design 3.3, and ALPN actually choosing - with the pool's establishment bound
 * armed across all of it
 */

UTF_AUTO_TEST_CASE( ClientSessionTls_GetOverHttp2Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::sessiontls;

    std::vector< std::string > preference;

    preference.push_back( "h2" );
    preference.push_back( "http/1.1" );

    const auto peer = makeTlsPeer( preference );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                "https" == request.scheme && "/secure" == request.path,
                BL_MSG()
                    << "The peer was asked for an unexpected request: "
                    << request.scheme
                    << " "
                    << request.path
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "secure" )
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
                "utest::sessiontls::ClientSessionTls_GetOverHttp2Tests"
                );

            UTF_REQUIRE_EQUAL( session -> transportScheme(), std::string( "https" ) );

            /*
             * A TLS session NEGOTIATES, so it may produce either protocol - which is what makes
             * the h2-only routing below do anything at all
             */

            UTF_REQUIRE( session -> canCarryBodySource() );

            const auto task = runRequest( session, makeRequest( port, "/secure" ) );

            UTF_REQUIRE_EQUAL( task -> response().status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( task -> response() ), std::string( "secure" ) );

            /*
             * The identifier the peer selected, verbatim - not one derived from the protocol
             */

            UTF_REQUIRE( httpclient::HttpProtocol::Http2 == task -> response().protocol() );
            UTF_REQUIRE_EQUAL( task -> response().negotiatedAlpn(), std::string( "h2" ) );

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 1U );

            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief A streaming upload is routed to a connection which does not offer http/1.1 - obligation 2
 *
 * THE JOIN utf_baselib_httpclient4 CANNOT RUN. The HTTP/1.1 driver refuses every request carrying
 * a BodySource, so the session routes such a request to a key of its own whose connections offer
 * "h2" alone - a peer may select only from what it was offered (RFC 7301 3.1), so it cannot be
 * HTTP/1.1. That routing does anything only on a transport which negotiates, which a cleartext
 * session never is, so the cleartext module pins the decision and the narrowing as the two pure
 * functions they are and this case runs them joined
 *
 * The assertion is the SECOND CONNECTION: the ordinary GET and the streaming PUT go to the same
 * origin and do not share a connection, which is the whole of what the separate key buys
 */

UTF_AUTO_TEST_CASE( ClientSessionTls_StreamingUploadTakesAnHttp2OnlyConnectionTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::sessiontls;

    const std::string upload( "streamed-payload" );

    std::vector< std::string > preference;

    preference.push_back( "h2" );
    preference.push_back( "http/1.1" );

    const auto peer = makeTlsPeer( preference );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
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
                "utest::sessiontls::ClientSessionTls_StreamingUploadTakesAnHttp2OnlyConnectionTests"
                );

            const auto plain = runRequest( session, makeRequest( port, "/plain" ) );

            UTF_REQUIRE_EQUAL( bodyOf( plain -> response() ), std::string( "/plain" ) );
            UTF_REQUIRE_EQUAL( statsOf( session ).connectionsCreated.value(), 1U );

            auto streaming = makeRequest( port, "/streamed", "PUT" );

            streaming.bodySource(
                om::ObjPtrCopyable< httpclient::BodySource >(
                    om::qi< httpclient::BodySource >(
                        StringBodySource::createInstance( cpp::copy( upload ) )
                        )
                    )
                );

            const auto streamed = runRequest( session, streaming );

            UTF_REQUIRE_EQUAL( streamed -> response().status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( streamed -> response() ), std::string( "/streamed" ) );

            /*
             * A SECOND CONNECTION, because the streaming upload got a key of its own. Its stream
             * is therefore stream 1 of the second connection rather than stream 3 of the first,
             * which is also what the peer recorded the body against
             */

            UTF_REQUIRE_EQUAL( statsOf( session ).connectionsCreated.value(), 2U );

            /*
             * THE RENDEZVOUS, and without it this assertion is a race the case loses about once
             * in eight runs, with an EMPTY body. The peer's responder runs on the request HEADERS
             * and not on its END_STREAM, so the peer answers 200 and ends its half of the stream
             * before it has necessarily processed a single DATA frame of the upload; the client's
             * task then completes, and appendBody( ) is still to run on the peer's strand. The
             * recorder's own note says to read a body only after a wait, and "end of request body"
             * is the record onRequestBodyEnd( ) makes for exactly this - so the wait is on the
             * thing being asserted, which is what requireRecorded( ) is for
             */

            h2driver::requireRecorded(
                peer -> recorder(),
                "end of request body on stream 1, " +
                    utils::lexical_cast< std::string >( upload.size() ) +
                    " bytes"
                );

            /*
             * The recorder keys bodies by the WIRE stream identifier and not by connection, so
             * two connections which both use stream 1 would append into one string - which is
             * safe here and not luck: the first request is a GET and appends nothing
             */

            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( 1U ), upload );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

#endif /* __UTEST_TESTCLIENTSESSIONTLS_H_ */
