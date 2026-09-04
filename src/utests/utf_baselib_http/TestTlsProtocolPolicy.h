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

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/*
 * Tests for the TLS protocol version policy configured by
 * bl::crypto::detail::CryptoInitT::initNativeSslContext
 *
 * The policy is fixed and process wide - there is no opt-in which lowers it - so what is
 * asserted here is what every consumer of the library gets; see the decision record in
 * notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md
 *
 * The handshake is driven over an OpenSSL BIO pair rather than over a socket; that keeps the
 * assertion focused on the SSL_CTX policy with nothing else in the path and avoids needing a
 * port, the machine global test lock, or any timing assumptions
 */

namespace utest
{
    namespace tlspolicy
    {
        /*
         * Minimal test-only RAII wrappers; the library does not expose smart pointer typedefs
         * for these OpenSSL types because it does not use them outside of Boost.Asio
         */

        class SslCtxDeleter
        {
        public:

            void operator ()( SAA_in ::SSL_CTX* ctx ) const NOEXCEPT
            {
                ( void ) ::SSL_CTX_free( ctx );
            }
        };

        class SslDeleter
        {
        public:

            void operator ()( SAA_in ::SSL* ssl ) const NOEXCEPT
            {
                ( void ) ::SSL_free( ssl );
            }
        };

        typedef bl::cpp::SafeUniquePtr< ::SSL_CTX, SslCtxDeleter >      sslctx_ptr_t;
        typedef bl::cpp::SafeUniquePtr< ::SSL, SslDeleter >             ssl_ptr_t;

        /**
         * @brief Creates a client context which will only offer the requested protocol version
         *
         * The protocol is selected by denying every other version rather than by using the
         * version specific method functions (TLSv1_1_client_method and friends), which are
         * hidden by the OPENSSL_API_COMPAT level the library is built with; the SSL_OP_NO_*
         * bits exist on every version of OpenSSL we support
         *
         * Returns nullptr when the linked OpenSSL cannot offer the requested version at all,
         * which can happen when it was built without it or when the system configuration
         * raises the minimum protocol version
         */
        inline auto createSingleVersionClientContext( SAA_in const int protoVersion ) -> sslctx_ptr_t
        {
            auto ctx = sslctx_ptr_t::attach( ::SSL_CTX_new( ::SSLv23_client_method() ) );

            if( ! ctx )
            {
                return sslctx_ptr_t();
            }

            auto options = ::SSL_CTX_get_options( ctx.get() );

            options |= ( SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 );

            if( TLS1_VERSION != protoVersion )
            {
                options |= SSL_OP_NO_TLSv1;
            }

            if( TLS1_1_VERSION != protoVersion )
            {
                options |= SSL_OP_NO_TLSv1_1;
            }

            if( TLS1_2_VERSION != protoVersion )
            {
                options |= SSL_OP_NO_TLSv1_2;
            }

#ifdef SSL_OP_NO_TLSv1_3
            options |= SSL_OP_NO_TLSv1_3;
#endif

            ( void ) ::SSL_CTX_set_options( ctx.get(), options );

            /*
             * The server certificate is not the subject of these tests
             */

            ( void ) ::SSL_CTX_set_verify( ctx.get(), SSL_VERIFY_NONE, nullptr );

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
            if(
                1 != ::SSL_CTX_set_min_proto_version( ctx.get(), protoVersion ) ||
                1 != ::SSL_CTX_set_max_proto_version( ctx.get(), protoVersion )
                )
            {
                ( void ) ::ERR_clear_error();

                return sslctx_ptr_t();
            }
#endif

            /*
             * The pre-TLS 1.3 protocols need a cipher list which is not empty under the local
             * security level; the library policy is intentionally not used here because it is
             * the server side which is under test
             */

            if( 1 != ::SSL_CTX_set_cipher_list( ctx.get(), "DEFAULT:@SECLEVEL=0" ) )
            {
                ( void ) ::ERR_clear_error();
            }

            return ctx;
        }

        /**
         * @brief Drives a full handshake between the two contexts over a BIO pair
         *
         * Returns true only when both sides complete the handshake
         */
        inline bool tryHandshake(
            SAA_in          ::SSL_CTX*                          clientCtx,
            SAA_in          ::SSL_CTX*                          serverCtx
            )
        {
            auto client = ssl_ptr_t::attach( ::SSL_new( clientCtx ) );
            auto server = ssl_ptr_t::attach( ::SSL_new( serverCtx ) );

            if( ! client || ! server )
            {
                return false;
            }

            ::BIO* clientBio = nullptr;
            ::BIO* serverBio = nullptr;

            if( 1 != ::BIO_new_bio_pair( &clientBio, 0, &serverBio, 0 ) )
            {
                ( void ) ::ERR_clear_error();

                return false;
            }

            /*
             * ::SSL_set_bio takes ownership of both BIOs it is given, and the two SSL objects
             * are released by the smart pointers above
             */

            ::SSL_set_bio( client.get(), clientBio, clientBio );
            ::SSL_set_bio( server.get(), serverBio, serverBio );

            ::SSL_set_connect_state( client.get() );
            ::SSL_set_accept_state( server.get() );

            bool clientDone = false;
            bool serverDone = false;

            /*
             * The iteration cap is a guard against a protocol state machine which makes no
             * progress; a handshake needs a small number of round trips
             */

            for( int i = 0; i < 64 && ! ( clientDone && serverDone ); ++i )
            {
                ::SSL* const parties[] = { client.get(), server.get() };
                bool* const flags[] = { &clientDone, &serverDone };

                for( std::size_t j = 0U; j < 2U; ++j )
                {
                    if( *flags[ j ] )
                    {
                        continue;
                    }

                    const int rc = ::SSL_do_handshake( parties[ j ] );

                    if( 1 == rc )
                    {
                        *flags[ j ] = true;

                        continue;
                    }

                    const int error = ::SSL_get_error( parties[ j ], rc );

                    if( SSL_ERROR_WANT_READ != error && SSL_ERROR_WANT_WRITE != error )
                    {
                        /*
                         * The specific reason code is deliberately not asserted on by the
                         * callers; OpenSSL reports a version mismatch inconsistently across
                         * versions and between the two sides of the connection
                         */

                        ( void ) ::ERR_clear_error();

                        return false;
                    }
                }
            }

            ( void ) ::ERR_clear_error();

            return clientDone && serverDone;
        }

        inline auto createServerContext() -> bl::cpp::SafeUniquePtr< bl::asio::ssl::context >
        {
            return bl::crypto::CryptoBase::createAsioSslServerContext(
                test::UtfCrypto::getDefaultServerKey(),
                test::UtfCrypto::getDefaultServerCertificate()
                );
        }

    } // tlspolicy

} // utest

UTF_AUTO_TEST_CASE( TlsProtocolPolicy_Tls11IsRefusedUnderTheDefaultPolicy )
{
    using namespace utest::tlspolicy;

    const auto serverContext = createServerContext();

    const auto tls12Client = createSingleVersionClientContext( TLS1_2_VERSION );
    UTF_REQUIRE( tls12Client );

    /*
     * A TLS 1.2 client must succeed against this very context
     *
     * This assertion is what makes the negative one below meaningful - without it a refused
     * TLS 1.1 handshake could equally well be explained by a broken key/certificate pair or by
     * a mistake in the BIO plumbing rather than by the protocol policy
     */

    UTF_REQUIRE( tryHandshake( tls12Client.get(), serverContext -> native_handle() ) );

    const auto tls11Client = createSingleVersionClientContext( TLS1_1_VERSION );

    if( ! tls11Client )
    {
        /*
         * The linked OpenSSL cannot offer TLS 1.1 at all, so the server policy is not what
         * would be under test here
         */

        UTF_WARNING_MESSAGE(
            BL_MSG()
                << "The linked OpenSSL cannot offer TLS 1.1; the negative case is vacuous"
            );

        return;
    }

    UTF_REQUIRE( ! tryHandshake( tls11Client.get(), serverContext -> native_handle() ) );
}

UTF_AUTO_TEST_CASE( TlsProtocolPolicy_Tls10IsRefusedUnderTheDefaultPolicy )
{
    using namespace utest::tlspolicy;

    const auto serverContext = createServerContext();

    const auto tls10Client = createSingleVersionClientContext( TLS1_VERSION );

    if( ! tls10Client )
    {
        UTF_WARNING_MESSAGE(
            BL_MSG()
                << "The linked OpenSSL cannot offer TLS 1.0; the negative case is vacuous"
            );

        return;
    }

    UTF_REQUIRE( ! tryHandshake( tls10Client.get(), serverContext -> native_handle() ) );
}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

UTF_AUTO_TEST_CASE( TlsProtocolPolicy_FloorAndSecurityLevelArePinned )
{
    using namespace utest::tlspolicy;

    /*
     * The protocol floor and the security level are the two settings which decide what the
     * library will negotiate and which certificates it will accept; both are queried back from
     * the contexts rather than inferred from handshakes, so a future change which lowers either
     * of them fails here directly rather than through some handshake which unexpectedly succeeds
     *
     * Both the process global client context and a freshly created server context are checked,
     * because they are initialized through the same code path and a regression in either one
     * is a regression for every connection of that role
     *
     * Note that SSL_CTX_get_min_proto_version is a macro over SSL_CTX_ctrl and therefore it
     * must not be qualified with the global namespace operator
     */

    const auto serverContext = createServerContext();

    auto& clientContext = bl::crypto::CryptoBase::getAsioSslContext();

    ::SSL_CTX* const contexts[] = { clientContext.native_handle(), serverContext -> native_handle() };

    for( ::SSL_CTX* const ctx : contexts )
    {
        UTF_REQUIRE_EQUAL( static_cast< int >( SSL_CTX_get_min_proto_version( ctx ) ), TLS1_2_VERSION );
        UTF_REQUIRE_EQUAL( ::SSL_CTX_get_security_level( ctx ), 2 );
    }
}

#endif // OPENSSL_VERSION_NUMBER >= 0x10100000L
