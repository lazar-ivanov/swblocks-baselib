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
#include <baselib/crypto/TlsClientProfile.h>
#include <baselib/crypto/OpenSSLTypes.h>
#include <baselib/crypto/ErrorHandling.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

#include <openssl/pem.h>

/*
 * Tests for the per-profile TLS client contexts and the negotiated parameter floor of
 * notes/plans/http2-design.md 3.3, i.e. decisions D4 ("advertise, verify, refuse") and D22
 * (session tickets are advertised and never resumed)
 *
 * Handshakes are driven over an OpenSSL BIO pair rather than over a socket, which keeps every
 * assertion on the SSL_CTX policy with nothing else in the path and needs no port, no machine
 * global test lock and no timing assumption
 *
 * THE SECOND OPENSSL FLAVOR IS OWED, NOT COVERED. Everything here runs on whatever OpenSSL the
 * dist carries, which on the development machine is 3.5.4 only; 1.1.1w is deferred and the debt
 * is recorded in notes/plans/issues/openssl-1x-flavor-deferral.md. The one case which is about
 * that flavor - ClientProfilesRequireOpenSsl35 - therefore asserts the RULE at both version
 * numbers rather than pretending to have run the older branch
 */

namespace utest
{
    namespace tlsclientctx
    {
        /*
         * Minimal test-only RAII wrappers; the library does not expose smart pointer typedefs for
         * these OpenSSL types because it does not use them outside of Boost.Asio
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
         * @brief Loads the checked in test server key material into a raw context
         */

        inline void loadTestServerKeyMaterial( SAA_inout ::SSL_CTX* ctx )
        {
            const std::string certificatePem( test::UtfCrypto::getDefaultServerCertificate() );
            const std::string keyPem( test::UtfCrypto::getDefaultServerKey() );

            const auto certificateBuffer = bl::crypto::bio_ptr_t::attach(
                ::BIO_new_mem_buf(
                    const_cast< char* >( certificatePem.c_str() ),
                    static_cast< int >( certificatePem.size() )
                    )
                );

            BL_CHK_CRYPTO_API_NM( certificateBuffer );

            const auto certificate = bl::crypto::x509cert_ptr_t::attach(
                ::PEM_read_bio_X509( certificateBuffer.get(), nullptr, nullptr, nullptr )
                );

            BL_CHK_CRYPTO_API_NM( certificate );

            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_use_certificate( ctx, certificate.get() ) );

            const auto keyBuffer = bl::crypto::bio_ptr_t::attach(
                ::BIO_new_mem_buf(
                    const_cast< char* >( keyPem.c_str() ),
                    static_cast< int >( keyPem.size() )
                    )
                );

            BL_CHK_CRYPTO_API_NM( keyBuffer );

            const auto key = bl::crypto::evppkey_ptr_t::attach(
                ::PEM_read_bio_PrivateKey( keyBuffer.get(), nullptr, nullptr, nullptr )
                );

            BL_CHK_CRYPTO_API_NM( key );

            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_use_PrivateKey( ctx, key.get() ) );

            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_check_private_key( ctx ) );
        }

        /**
         * @brief A raw context which carries none of the library policy and offers exactly the
         * requested cipher list, pinned to TLS 1.2
         *
         * '@SECLEVEL=0' is appended so that a below-floor suite can be offered at all. This
         * context is built by the test and is never produced by the library - it is the peer a
         * below-floor connection needs in order to exist, not a context the library would accept
         */

        inline auto createBelowFloorContext(
            SAA_in          const char*                         cipherList,
            SAA_in          const bool                          isServer
            )
            -> sslctx_ptr_t
        {
            auto ctx = sslctx_ptr_t::attach(
                ::SSL_CTX_new( isServer ? ::SSLv23_server_method() : ::SSLv23_client_method() )
                );

            BL_CHK_CRYPTO_API_NM( ctx );

            ( void ) ::SSL_CTX_set_verify( ctx.get(), SSL_VERIFY_NONE, nullptr );

            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_set_min_proto_version( ctx.get(), TLS1_2_VERSION ) );
            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_set_max_proto_version( ctx.get(), TLS1_2_VERSION ) );

            const std::string list = std::string( cipherList ) + ":@SECLEVEL=0";

            BL_CHK_CRYPTO_API_NM( ::SSL_CTX_set_cipher_list( ctx.get(), list.c_str() ) );

            if( isServer )
            {
                loadTestServerKeyMaterial( ctx.get() );
            }

            return ctx;
        }

        /**
         * @brief The configured cipher of the given name, which is how a named suite is turned
         * into the ::SSL_CIPHER the floor predicate takes
         *
         * It is looked up by name rather than taken from the head of the stack, because a context
         * reports its TLS 1.3 suites in the same stack as its cipher list whatever cipher list was
         * set and whatever the maximum protocol version is
         *
         * Note that sk_SSL_CIPHER_num and sk_SSL_CIPHER_value are macros and therefore they must
         * not be qualified with the global namespace operator
         */

        inline auto findCipherByName(
            SAA_in          ::SSL_CTX*                          ctx,
            SAA_in          const std::string&                  name
            )
            -> const ::SSL_CIPHER*
        {
            const auto* const ciphers = ::SSL_CTX_get_ciphers( ctx );

            BL_CHK_CRYPTO_API_NM( ciphers );

            for( int i = 0, count = sk_SSL_CIPHER_num( ciphers ); i < count; ++i )
            {
                const auto* const cipher = sk_SSL_CIPHER_value( ciphers, i );

                const char* const cipherName = ::SSL_CIPHER_get_name( cipher );

                if( cipherName && name == cipherName )
                {
                    return cipher;
                }
            }

            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "The linked OpenSSL does not offer the cipher suite '"
                    << name
                    << "', which this test needs in order to assert the floor"
                );
        }

        /**
         * @brief Every cipher suite name a context has configured, in the order OpenSSL keeps them
         */

        inline auto getConfiguredCipherNames( SAA_in ::SSL_CTX* ctx ) -> std::vector< std::string >
        {
            const auto* const ciphers = ::SSL_CTX_get_ciphers( ctx );

            BL_CHK_CRYPTO_API_NM( ciphers );

            std::vector< std::string > names;

            for( int i = 0, count = sk_SSL_CIPHER_num( ciphers ); i < count; ++i )
            {
                const char* const name = ::SSL_CIPHER_get_name( sk_SSL_CIPHER_value( ciphers, i ) );

                BL_CHK_CRYPTO_API_NM( name );

                names.push_back( name );
            }

            return names;
        }

        /**
         * @brief The subsequence of 'names' which is present in 'wanted', in the order of 'names'
         *
         * This is how the configured list is compared against a profile's list without assuming
         * how OpenSSL interleaves the TLS 1.3 suites with the TLS 1.2 ones
         */

        inline auto filterNames(
            SAA_in          const std::vector< std::string >&   names,
            SAA_in          const std::vector< std::string >&   wanted
            )
            -> std::vector< std::string >
        {
            std::vector< std::string > result;

            for( const auto& name : names )
            {
                if( wanted.end() != std::find( wanted.begin(), wanted.end(), name ) )
                {
                    result.push_back( name );
                }
            }

            return result;
        }

        /**
         * @brief A completed handshake, with both peers kept alive so the negotiated parameters
         * can be inspected
         */

        struct HandshakeResult
        {
            ssl_ptr_t                                           client;
            ssl_ptr_t                                           server;
            bool                                                completed = false;
        };

        /**
         * @brief Drives a full handshake between two contexts over a BIO pair
         *
         * Modeled on the equivalent helper of TestTlsProtocolPolicy.h in utf_baselib_http2; it
         * differs in that it hands the two SSL objects back rather than only a verdict, because
         * what is under test here is what was negotiated and not merely that something was
         */

        inline auto tryHandshake(
            SAA_in          ::SSL_CTX*                          clientCtx,
            SAA_in          ::SSL_CTX*                          serverCtx
            )
            -> HandshakeResult
        {
            HandshakeResult result;

            result.client = ssl_ptr_t::attach( ::SSL_new( clientCtx ) );
            result.server = ssl_ptr_t::attach( ::SSL_new( serverCtx ) );

            if( ! result.client || ! result.server )
            {
                return result;
            }

            ::BIO* clientBio = nullptr;
            ::BIO* serverBio = nullptr;

            if( 1 != ::BIO_new_bio_pair( &clientBio, 0, &serverBio, 0 ) )
            {
                ( void ) ::ERR_clear_error();

                return result;
            }

            /*
             * ::SSL_set_bio takes ownership of both BIOs it is given, and the two SSL objects are
             * released by the smart pointers above
             */

            ::SSL_set_bio( result.client.get(), clientBio, clientBio );
            ::SSL_set_bio( result.server.get(), serverBio, serverBio );

            ::SSL_set_connect_state( result.client.get() );
            ::SSL_set_accept_state( result.server.get() );

            bool clientDone = false;
            bool serverDone = false;

            /*
             * The iteration cap is a guard against a protocol state machine which makes no
             * progress; a handshake needs a small number of round trips
             */

            for( int i = 0; i < 64 && ! ( clientDone && serverDone ); ++i )
            {
                ::SSL* const parties[] = { result.client.get(), result.server.get() };
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
                        ( void ) ::ERR_clear_error();

                        return result;
                    }
                }
            }

            ( void ) ::ERR_clear_error();

            result.completed = clientDone && serverDone;

            return result;
        }

        inline auto createTestServerContext() -> bl::cpp::SafeUniquePtr< bl::asio::ssl::context >
        {
            return bl::crypto::CryptoBase::createAsioSslServerContext(
                test::UtfCrypto::getDefaultServerKey(),
                test::UtfCrypto::getDefaultServerCertificate()
                );
        }

        /**
         * @brief A profile naming suites which exist on every OpenSSL the library supports and
         * which are all above the floor
         *
         * The content is not a browser's - captured browser ground truth is profile data and
         * belongs to a later slice (design 6.7) - it is only enough shape to prove the context
         * builder applies what it is given
         */

        inline auto createTestProfile() -> bl::crypto::TlsClientProfile
        {
            bl::crypto::TlsClientProfile profile;

            profile.cipherSuitesTls12.push_back( "ECDHE-ECDSA-AES128-GCM-SHA256" );
            profile.cipherSuitesTls12.push_back( "ECDHE-RSA-AES128-GCM-SHA256" );
            profile.cipherSuitesTls12.push_back( "ECDHE-RSA-AES256-GCM-SHA384" );

            profile.cipherSuitesTls13.push_back( "TLS_AES_128_GCM_SHA256" );
            profile.cipherSuitesTls13.push_back( "TLS_AES_256_GCM_SHA384" );

            profile.alpnProtocols.push_back( "h2" );
            profile.alpnProtocols.push_back( "http/1.1" );

            return profile;
        }

    } // tlsclientctx

} // utest

UTF_AUTO_TEST_CASE( TlsClientContext_ProfileContextKeepsTheLibraryFloorTests )
{
    using namespace bl;
    using namespace utest::tlsclientctx;

    const auto profile = createTestProfile();

    const auto context = crypto::CryptoBase::createAsioSslClientContext( profile );

    UTF_REQUIRE( context );

    ::SSL_CTX* const native = context -> native_handle();

    /*
     * Step 1 is common and a profile cannot move any of it
     */

    UTF_REQUIRE_EQUAL( 2, ::SSL_CTX_get_security_level( native ) );

    UTF_REQUIRE_EQUAL(
        static_cast< int >( TLS1_2_VERSION ),
        static_cast< int >( ::SSL_CTX_get_min_proto_version( native ) )
        );

    const auto options = ::SSL_CTX_get_options( native );

    UTF_REQUIRE( 0 != ( options & SSL_OP_NO_TLSv1 ) );
    UTF_REQUIRE( 0 != ( options & SSL_OP_NO_TLSv1_1 ) );
    UTF_REQUIRE( 0 != ( options & SSL_OP_NO_COMPRESSION ) );

    /*
     * Step 3 is the same trust anchors as the process global client context, shared and not
     * copied: the two contexts hold the very same X509_STORE, so a root registered after this
     * context was built is visible through it and the two paths cannot diverge
     */

    UTF_REQUIRE(
        ::SSL_CTX_get_cert_store( native ) ==
            ::SSL_CTX_get_cert_store( crypto::CryptoBase::getAsioSslContext().native_handle() )
        );

    /*
     * D22 - the cache is off, exactly as it is on the global client context, so nothing is ever
     * resumed however the ticket extension is advertised
     */

    UTF_REQUIRE_EQUAL(
        static_cast< long >( SSL_SESS_CACHE_OFF ),
        ::SSL_CTX_get_session_cache_mode( native )
        );

    /*
     * Step 2 came from the profile. The comparison is per list rather than against one
     * concatenation, because how OpenSSL interleaves the TLS 1.3 suites with the TLS 1.2 ones in
     * the stack it reports is its business and not a property worth pinning
     */

    const auto configured = getConfiguredCipherNames( native );

    UTF_REQUIRE(
        filterNames( configured, profile.cipherSuitesTls12 ) == profile.cipherSuitesTls12
        );

    UTF_REQUIRE(
        filterNames( configured, profile.cipherSuitesTls13 ) == profile.cipherSuitesTls13
        );

    UTF_REQUIRE_EQUAL(
        profile.cipherSuitesTls12.size() + profile.cipherSuitesTls13.size(),
        configured.size()
        );

    /*
     * The profile's names are not the whole of the TLS 1.2 list: the library's own exclusion
     * tokens follow them, which is the layer that keeps an anonymous or a NULL suite from being
     * offered at all - the floor check being the layer that would refuse one if it were. They go
     * on the TLS 1.2 list only, because SSL_CTX_set_ciphersuites reads its argument as a list of
     * suite names and would silently ignore them
     *
     * The empty list must stay empty, because that is what tells the builder to keep the hardened
     * library default rather than apply a list of nothing but exclusions - which is what the two
     * assertions on the emptyProfile context below would no longer be testing if it did not
     *
     * The names asserted against are the assertion's own rather than the test profile's, so that
     * editing that profile cannot silently change what this pins
     */

    const std::vector< std::string > twoNames( { "ECDHE-RSA-AES128-GCM-SHA256", "ECDHE-RSA-AES256-GCM-SHA384" } );

    UTF_REQUIRE_EQUAL(
        std::string( "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:!aNULL:!eNULL" ),
        crypto::detail::CryptoInit::buildCipherListFromNames( twoNames, true /* appendExclusions */ )
        );

    UTF_REQUIRE_EQUAL(
        std::string( "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384" ),
        crypto::detail::CryptoInit::buildCipherListFromNames( twoNames, false /* appendExclusions */ )
        );

    UTF_REQUIRE(
        crypto::detail::CryptoInit::buildCipherListFromNames(
            std::vector< std::string >(),
            true /* appendExclusions */
            ).empty()
        );

    /*
     * The ticket extension is off unless the profile asks for it, and asking for it clears
     * exactly one option bit and leaves the cache alone
     */

    UTF_REQUIRE( 0 != ( options & SSL_OP_NO_TICKET ) );

    auto ticketProfile = createTestProfile();

    ticketProfile.sessionTicket = true;

    const auto ticketContext = crypto::CryptoBase::createAsioSslClientContext( ticketProfile );

    UTF_REQUIRE( 0 == ( ::SSL_CTX_get_options( ticketContext -> native_handle() ) & SSL_OP_NO_TICKET ) );

    UTF_REQUIRE_EQUAL(
        static_cast< long >( SSL_SESS_CACHE_OFF ),
        ::SSL_CTX_get_session_cache_mode( ticketContext -> native_handle() )
        );

    /*
     * A profile which names no suites keeps the hardened library default rather than ending up
     * with an empty or an unconstrained cipher policy
     */

    const crypto::TlsClientProfile emptyProfile;

    const auto defaultContext = crypto::CryptoBase::createAsioSslClientContext( emptyProfile );

    UTF_REQUIRE( ! getConfiguredCipherNames( defaultContext -> native_handle() ).empty() );

    UTF_REQUIRE_EQUAL( 2, ::SSL_CTX_get_security_level( defaultContext -> native_handle() ) );

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( TlsClientContext_UnsafeCipherSuiteNamesAreRefusedTests )
{
    using namespace bl;
    using namespace utest::tlsclientctx;

    /*
     * The premise the security level assertion exists for, pinned first: a '@SECLEVEL' token in a
     * cipher list really does override ::SSL_CTX_set_security_level. This is asserted against a
     * raw context built by the test, because the library refuses to build one this way - without
     * it, the assertion in the context builder would be guarding against a hazard nobody has
     * shown to be real
     */

    auto rawContext = sslctx_ptr_t::attach( ::SSL_CTX_new( ::SSLv23_client_method() ) );

    UTF_REQUIRE( rawContext );

    ::SSL_CTX_set_security_level( rawContext.get(), 2 );

    UTF_REQUIRE_EQUAL( 2, ::SSL_CTX_get_security_level( rawContext.get() ) );

    UTF_REQUIRE( 1 == ::SSL_CTX_set_cipher_list( rawContext.get(), "DEFAULT:@SECLEVEL=0" ) );

    UTF_REQUIRE_EQUAL( 0, ::SSL_CTX_get_security_level( rawContext.get() ) );

    rawContext.reset();

    /*
     * Every one of these is refused by the name allowlist, before OpenSSL sees any of it
     */

    const std::vector< std::string > unsafeNames =
    {
        "@SECLEVEL=0",                                  /* the control token itself */
        "DEFAULT:@SECLEVEL=0",                          /* smuggled through a ':' separator */
        "ECDHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0",      /* appended to a name which is genuine */
        "ALL,@SECLEVEL=0",                              /* ',' separates tokens exactly as ':' does */
        "ALL @STRENGTH",                                /* and so does a space */
        "!aNULL",                                       /* a deletion operator */
        "-ALL",                                         /* a leading '-' deletes rather than names */
        "+RC4",                                         /* a leading '+' reorders */
        "",                                             /* the empty name */
        "ECDHE_RSA/AES128",                             /* a character which is in no suite name */
    };

    for( const auto& name : unsafeNames )
    {
        UTF_CHECK( ! crypto::CryptoBase::isCipherSuiteNameSafe( name ) );

        crypto::TlsClientProfile profile;

        profile.cipherSuitesTls12.push_back( name );

        UTF_CHECK_THROW(
            crypto::CryptoBase::createAsioSslClientContext( profile ),
            SecurityException
            );

        /*
         * The same name is refused in the TLS 1.3 list, which is validated before either list is
         * applied so that a bad name there cannot leave a half configured context behind
         */

        crypto::TlsClientProfile tls13Profile;

        tls13Profile.cipherSuitesTls13.push_back( name );

        UTF_CHECK_THROW(
            crypto::CryptoBase::createAsioSslClientContext( tls13Profile ),
            SecurityException
            );
    }

    /*
     * A refusal which left the process global OpenSSL error queue dirty would surface as an
     * unrelated failure in whatever runs next
     */

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );

    /*
     * And the allowlist is not simply refusing everything: a hyphen inside a name is what every
     * TLS 1.2 suite name is spelled with, and an underscore is what every TLS 1.3 one uses
     */

    UTF_CHECK( crypto::CryptoBase::isCipherSuiteNameSafe( "ECDHE-RSA-AES128-GCM-SHA256" ) );
    UTF_CHECK( crypto::CryptoBase::isCipherSuiteNameSafe( "TLS_AES_128_GCM_SHA256" ) );
    UTF_CHECK( crypto::CryptoBase::isCipherSuiteNameSafe( "AES128-SHA" ) );
}

UTF_AUTO_TEST_CASE( TlsClientContext_NegotiatedParametersFloorTests )
{
    using namespace bl;
    using namespace utest::tlsclientctx;

    /*
     * The three arms of the floor, each refused on its own. All of these suites exist and can be
     * negotiated; what disqualifies them is the key exchange in the first case, the cipher in the
     * second and the authentication in the third, so a floor which had lost any one of the three
     * would let one of them through
     *
     * The anonymous one is the arm the floor gained last, and it is the one that matters most:
     * it is ephemeral and it is AEAD, so the other two arms pass it, and RFC 9113 Appendix A
     * names it - TLS_DH_anon_WITH_AES_128_GCM_SHA256 - as a suite an HTTP/2 implementation may
     * treat as INADEQUATE_SECURITY. Its absence from this case is what let the floor go on
     * admitting it while the design claimed the floor was stronger than that appendix. OpenSSL's
     * own security level 2 refuses it too, which is why nothing was ever exposed - but the level
     * is not what D4 names as the check, and this is
     */

    const auto staticRsaAead = createBelowFloorContext( "AES128-GCM-SHA256", false /* isServer */ );
    const auto ephemeralCbc = createBelowFloorContext( "ECDHE-RSA-AES128-SHA", false /* isServer */ );
    const auto anonymousAead = createBelowFloorContext( "ADH-AES128-GCM-SHA256", false /* isServer */ );
    const auto ephemeralAead = createBelowFloorContext( "ECDHE-RSA-AES128-GCM-SHA256", false /* isServer */ );

    UTF_REQUIRE(
        ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor(
            TLS1_2_VERSION,
            findCipherByName( staticRsaAead.get(), "AES128-GCM-SHA256" )
            )
        );

    UTF_REQUIRE(
        ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor(
            TLS1_2_VERSION,
            findCipherByName( ephemeralCbc.get(), "ECDHE-RSA-AES128-SHA" )
            )
        );

    {
        const auto* const anonymous = findCipherByName( anonymousAead.get(), "ADH-AES128-GCM-SHA256" );

        UTF_REQUIRE(
            ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor( TLS1_2_VERSION, anonymous )
            );

        /*
         * And it is refused for the stated reason rather than incidentally: the suite really does
         * satisfy the other two arms, so the assertion above cannot be passing because the linked
         * OpenSSL resolved 'ADH-AES128-GCM-SHA256' to something other than an ephemeral AEAD
         * suite. This says nothing about which NID an unauthenticated suite maps to, which is
         * deliberate - the predicate accepts a set of authentications rather than refusing one
         */

        UTF_REQUIRE_EQUAL( NID_kx_dhe, ::SSL_CIPHER_get_kx_nid( anonymous ) );

        UTF_REQUIRE( 0 != ::SSL_CIPHER_is_aead( anonymous ) );
    }

    UTF_REQUIRE(
        crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor(
            TLS1_2_VERSION,
            findCipherByName( ephemeralAead.get(), "ECDHE-RSA-AES128-GCM-SHA256" )
            )
        );

    /*
     * An acceptable suite at an unacceptable version is still refused, and so is a connection
     * which has negotiated nothing at all
     */

    UTF_REQUIRE(
        ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor(
            TLS1_1_VERSION,
            findCipherByName( ephemeralAead.get(), "ECDHE-RSA-AES128-GCM-SHA256" )
            )
        );

    UTF_REQUIRE(
        ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor( TLS1_2_VERSION, nullptr )
        );

    /*
     * DTLS 1.2 is 0xFEFD, which is numerically far above TLS 1.2 and would pass a bare >=
     * comparison; the major version byte is what keeps it out
     */

    UTF_REQUIRE(
        ! crypto::detail::CryptoInit::doNegotiatedParametersMeetFloor(
            0xFEFD,
            findCipherByName( ephemeralAead.get(), "ECDHE-RSA-AES128-GCM-SHA256" )
            )
        );

    /*
     * End to end. A profile context completes a handshake and what it negotiates is above the
     * floor - which is also what pins the claim that a TLS 1.3 suite satisfies the check without
     * being special cased, since OpenSSL reports NID_kx_any for one
     */

    const auto serverContext = createTestServerContext();

    const auto profileContext = crypto::CryptoBase::createAsioSslClientContext( createTestProfile() );

    {
        const auto handshake = tryHandshake(
            profileContext -> native_handle(),
            serverContext -> native_handle()
            );

        UTF_REQUIRE( handshake.completed );

        UTF_REQUIRE_EQUAL(
            static_cast< int >( TLS1_3_VERSION ),
            ::SSL_version( handshake.client.get() )
            );

        UTF_REQUIRE_NO_THROW(
            crypto::CryptoBase::chkNegotiatedParametersMeetFloor( handshake.client.get() )
            );
    }

    /*
     * And the TLS 1.2 arm positively, over the profile's own suite list. The maximum version is
     * lowered on this test's copy of the context rather than by the profile, because a profile
     * deliberately cannot move the protocol policy - that is step 1
     */

    {
        const auto tls12Context = crypto::CryptoBase::createAsioSslClientContext( createTestProfile() );

        BL_CHK_CRYPTO_API_NM(
            ::SSL_CTX_set_max_proto_version( tls12Context -> native_handle(), TLS1_2_VERSION )
            );

        const auto handshake = tryHandshake(
            tls12Context -> native_handle(),
            serverContext -> native_handle()
            );

        UTF_REQUIRE( handshake.completed );

        UTF_REQUIRE_EQUAL(
            static_cast< int >( TLS1_2_VERSION ),
            ::SSL_version( handshake.client.get() )
            );

        UTF_REQUIRE_NO_THROW(
            crypto::CryptoBase::chkNegotiatedParametersMeetFloor( handshake.client.get() )
            );
    }

    /*
     * The refusal, end to end: two peers which agree on a below-floor suite complete a handshake
     * and the check refuses the result before any HTTP byte is written
     */

    {
        const auto belowFloorServer =
            createBelowFloorContext( "AES128-GCM-SHA256", true /* isServer */ );

        const auto handshake = tryHandshake( staticRsaAead.get(), belowFloorServer.get() );

        UTF_REQUIRE( handshake.completed );

        const auto* const negotiated = ::SSL_get_current_cipher( handshake.client.get() );

        UTF_REQUIRE( nullptr != negotiated );

        UTF_REQUIRE_EQUAL( std::string( "AES128-GCM-SHA256" ), std::string( ::SSL_CIPHER_get_name( negotiated ) ) );

        UTF_REQUIRE_THROW(
            crypto::CryptoBase::chkNegotiatedParametersMeetFloor( handshake.client.get() ),
            SecurityException
            );

        /*
         * The exception says what was negotiated, which is the whole of its diagnostic value
         */

        try
        {
            crypto::CryptoBase::chkNegotiatedParametersMeetFloor( handshake.client.get() );

            UTF_FAIL( BL_MSG() << "A below-floor connection was accepted" );
        }
        catch( SecurityException& e )
        {
            const auto* const cipherName = eh::get_error_info< eh::errinfo_tls_negotiated_cipher >( e );
            const auto* const versionName = eh::get_error_info< eh::errinfo_tls_negotiated_version >( e );

            UTF_REQUIRE( nullptr != cipherName );
            UTF_REQUIRE( nullptr != versionName );

            UTF_REQUIRE_EQUAL( std::string( "AES128-GCM-SHA256" ), *cipherName );
            UTF_REQUIRE_EQUAL( std::string( "TLSv1.2" ), *versionName );
        }
    }

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( TlsClientContext_ClientProfilesRequireOpenSsl35Tests )
{
    using namespace bl;
    using namespace utest::tlsclientctx;

    /*
     * D2 draws the line at OpenSSL 3.5: below it a current browser's ClientHello cannot be
     * approximated - there is no hybrid group and no second key share - so an impersonation
     * profile is refused rather than silently delivered as something else
     *
     * THE 1.1.1w FLAVOR IS NOT RUN HERE AND THIS CASE DOES NOT CLAIM IT WAS. No dist on the
     * development machine carries it and BL_USE_OPENSSL_1X=1 does not build for reasons which
     * predate this work - notes/plans/issues/openssl-1x-flavor-deferral.md. What is asserted is
     * the RULE, at a version number on each side of the threshold, which is why the rule is a
     * function of a version rather than a bare #if. What is owed is a run of this module on that
     * flavor; on it the entry point compiles to the throw below and nothing else, because the
     * whole body is behind OPENSSL_VERSION_NUMBER >= 0x30500000L
     *
     * The version numbers below are chosen as points either side of the threshold and are not a
     * claim about the value any particular release's header carries
     */

    typedef crypto::detail::CryptoInit CryptoInit;

    UTF_REQUIRE( ! CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( 0x10100000UL ) );
    UTF_REQUIRE( ! CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( 0x30000000UL ) );
    UTF_REQUIRE( ! CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( 0x304FFFFFUL ) );

    UTF_REQUIRE( CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( 0x30500000UL ) );
    UTF_REQUIRE( CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( 0x40000000UL ) );

    UTF_REQUIRE_THROW(
        CryptoInit::chkTlsClientProfileSupportedOnOpenSslVersion( 0x10100000UL ),
        NotSupportedException
        );

    UTF_REQUIRE_NO_THROW(
        CryptoInit::chkTlsClientProfileSupportedOnOpenSslVersion( 0x30500000UL )
        );

    /*
     * And the entry point applies that rule to the OpenSSL which is actually linked, rather than
     * carrying a second copy of the threshold
     */

    UTF_REQUIRE_EQUAL(
        crypto::CryptoBase::isTlsClientProfileSupported(),
        CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( OPENSSL_VERSION_NUMBER )
        );

    if( crypto::CryptoBase::isTlsClientProfileSupported() )
    {
        UTF_REQUIRE_NO_THROW(
            crypto::CryptoBase::createAsioSslClientContext( createTestProfile() )
            );
    }
    else
    {
        UTF_REQUIRE_THROW(
            crypto::CryptoBase::createAsioSslClientContext( createTestProfile() ),
            NotSupportedException
            );
    }

    UTF_MESSAGE(
        BL_MSG()
            << "TLS client profiles are "
            << ( crypto::CryptoBase::isTlsClientProfileSupported() ? "supported" : "refused" )
            << " on the linked OpenSSL ["
            << ::OpenSSL_version( OPENSSL_VERSION )
            << "]"
        );
}

/**
 * @brief An ALPN server preference may be set ONCE on a context, and a second call is refused
 *
 * THE REFUSAL IS THE WHOLE POINT AND IT IS NOT ABOUT LEAKING. ::SSL_CTX_set_ex_data would simply
 * overwrite the slot, so a second list would indeed leak - but freeing the first one is worse than
 * leaking it, because alpnSelectCallback hands OpenSSL a pointer INTO that list and OpenSSL reads
 * it again after the callback has returned (tls_handle_alpn duplicates and compares the selected
 * pointer at ssl/statem/statem_srvr.c:2258, :2273 and :2290 on the openssl-3.5 branch). A
 * handshake on another thread may therefore be reading the very list a second call would delete,
 * and nothing in OpenSSL synchronises a context's ex_data against its own handshakes. There is no
 * safe replacement, so there is no replacement
 *
 * This is the pin for that refusal. It is a SERVER entry point on a bare context, which is why it
 * is here rather than beside the TLS driver: utf_baselib_h2client3 is the only other caller of
 * setAlpnServerPreference and that module is closed at 39.7 MB (src/utests/AGENTS.md), and nothing
 * in this case needs a socket, a port or a handshake - the whole behaviour is on the SSL_CTX
 *
 * WHAT IT DOES NOT PIN. The check reads the ex_data slot WITHOUT the global crypto lock, so two
 * threads making the FIRST call on one fresh context concurrently could both pass it and one list
 * would leak. That is a misuse of a misuse - a context is configured by whoever built it, before
 * it is handed to anything which handshakes - and it is recorded in the L4 review record rather
 * than fixed here. No case below is concurrent and none should be read as excluding it
 */

UTF_AUTO_TEST_CASE( TlsClientContext_AlpnServerPreferenceIsSetOnceTests )
{
    using namespace bl;

    const std::vector< std::string > preference =
    {
        std::string( "h2" ),
        std::string( "http/1.1" ),
    };

    asio::ssl::context context( asio::ssl::context::sslv23 );

    /*
     * The first call takes, and it is the call the TLS driver suite already drives end to end
     */

    UTF_REQUIRE_NO_THROW(
        crypto::CryptoBase::setAlpnServerPreference( context, preference )
        );

    /*
     * The second is refused. This is the assertion the whole case exists for: without the check
     * the call below simply succeeds, overwrites the slot and leaks the first list
     */

    UTF_REQUIRE_THROW_MESSAGE(
        crypto::CryptoBase::setAlpnServerPreference( context, preference ),
        UnexpectedException,
        "An ALPN server preference has already been set on this context"
        );

    /*
     * And it is refused whatever the second list says, so what is refused is the STATE of the
     * context rather than a repeat of a value - "the same list again" would be the one case a
     * leak-driven check could have been tempted to allow
     */

    const std::vector< std::string > other =
    {
        std::string( "http/1.1" ),
    };

    UTF_REQUIRE_THROW_MESSAGE(
        crypto::CryptoBase::setAlpnServerPreference( context, other ),
        UnexpectedException,
        "An ALPN server preference has already been set on this context"
        );

    /*
     * The refusal is PER CONTEXT and not a process-wide latch: a fresh context still takes its own
     * first call. Without this the case would pass just as well against a global "already set"
     * flag, which would have made every server context after the first unconfigurable
     */

    {
        asio::ssl::context fresh( asio::ssl::context::sslv23 );

        UTF_REQUIRE_NO_THROW(
            crypto::CryptoBase::setAlpnServerPreference( fresh, other )
            );
    }

    /*
     * The refusals are checks and not OpenSSL failures, so nothing was pushed onto the error queue
     * for the next handshake on this thread to trip over
     */

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
}
