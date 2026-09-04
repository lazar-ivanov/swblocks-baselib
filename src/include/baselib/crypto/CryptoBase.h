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

#ifndef __BL_CRYPTO_CRYPTOBASE_H_
#define __BL_CRYPTO_CRYPTOBASE_H_

#include <baselib/crypto/OpenSSLTypes.h>
#include <baselib/crypto/ErrorHandling.h>
#include <baselib/crypto/TrustedRoots.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/Random.h>
#include <baselib/core/BaseIncludes.h>

#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

namespace bl
{
    namespace crypto
    {
        namespace detail
        {
            /**
             * @brief class CryptoInit
             *
             * OpenSSL is initialized from many places already (e.g. boost asio, thrift),
             * but we need to also provide an initializer for the case where it is used
             * without any of these external libraries
             */

            template
            <
                typename E = void
            >
            class CryptoInitT
            {
                BL_DECLARE_STATIC( CryptoInitT )

            private:

                static asio::ssl::context*                      g_sslContext;
                static os::mutex*                               g_locks;
                static int                                      g_lockCount;
                static int                                      g_sessionIdContext;

                static std::map< std::string, std::string >     g_untrustedEndpointsInfo;
                static os::mutex                                g_untrustedEndpointsInfoLock;
                static bool                                     g_allowUntrustedCertificates;

                static void initRandomEngine()
                {
                    /*
                     * SSL would use /dev/urandom to seed its RNG. We want to avoid this due
                     * to reasons mentioned in RandomBoostImports.h hence do our own seeding here
                     */

                    unsigned char buffer[ 2048 ];

                    random::getRandomBytes( buffer, sizeof( buffer ) );

                    ( void ) ::RAND_seed( buffer, sizeof( buffer ) );

                    BL_CHK_CRYPTO_API_NM( ::RAND_status() );
                }

#if OPENSSL_VERSION_NUMBER < 0x30000000L
                /*
                 * Locking callback is only needed for OpenSSL < 3.x
                 * OpenSSL 3.x+ handles threading internally
                 */
                static void callbackLocking(
                    SAA_in              int                                     mode,
                    SAA_in              int                                     lockId,
                    SAA_in              const char*                             file,
                    SAA_in              int                                     line
                    )
                {
                    BL_UNUSED( file );
                    BL_UNUSED( line );

                    BL_NOEXCEPT_BEGIN()

                    if( ! g_locks )
                    {
                        BL_RIP_MSG( "OpenSSL library was not initialized properly" );
                    }

                    if( lockId < 0 || lockId >= g_lockCount )
                    {
                        const auto msg = resolveMessage(
                            BL_MSG()
                                << "Invalid lockId "
                                << lockId
                                << " passed to callbackLocking(); [g_lockCount="
                                << g_lockCount
                                << "]"
                            );

                        BL_RIP_MSG( msg.c_str() );
                    }

                    if( mode & CRYPTO_LOCK )
                    {
                        g_locks[ lockId ].lock();
                    }
                    else
                    {
                        g_locks[ lockId ].unlock();
                    }

                    BL_NOEXCEPT_END()
                }
#endif

            public:

                static void loadTrustedRootFromPem(
                    SAA_inout           ::SSL_CTX*                              nativeSslContext,
                    SAA_in              const std::string&                      pemKeyText
                    )
                {
                    BL_ASSERT( nativeSslContext );

                    X509_STORE* store = ::SSL_CTX_get_cert_store( nativeSslContext );

                    BL_CHK(
                        nullptr,
                        store,
                        BL_MSG()
                           << "Unable to obtain certificate store from SSL context"
                        );

                    const auto buffer = bio_ptr_t::attach(
                        ::BIO_new_mem_buf(
                            const_cast< char* >( pemKeyText.c_str() ),
                            static_cast< int >( pemKeyText.size() )
                            )
                        );

                    BL_CHK_CRYPTO_API_NM( buffer );

                    const auto x509cert = x509cert_ptr_t::attach(
                        ::PEM_read_bio_X509_AUX(
                            buffer.get(),
                            nullptr                 /* X509 certificate out pointer (**) */,
                            nullptr                 /* Password callback */,
                            nullptr                 /* Password bytes */
                            )
                        );

                    BL_CHK_CRYPTO_API_NM( x509cert );

                    BL_CHK_CRYPTO_API_NM( ::X509_STORE_add_cert( store, x509cert.get() ) );
                }

                static void loadAllKnownCertificateAuthorities( SAA_inout ::SSL_CTX* nativeSslContext )
                {
                    BL_ASSERT( nativeSslContext );

                    /*
                     * Load certificate authorities used for verification
                     *
                     * In the new versions of boost we can simply call
                     * asio::ssl::context::add_certificate_authority() method, but since in the one
                     * we use (1.52) it is not provided, so we will have to call the relevant
                     * OpenSSL functions directly (the code is wrapped in loadTrustedRootFromPem)
                     */

                    for( const auto& certificatePemText : trustedRoots() )
                    {
                        loadTrustedRootFromPem( nativeSslContext, certificatePemText );
                    }
                }

                /**
                 * @brief Verifies that the configured cipher policy left at least one cipher
                 * suite which can be negotiated below TLS 1.3
                 *
                 * ::SSL_CTX_set_cipher_list returns zero when nothing matched and its return
                 * value is checked at the call site, but from OpenSSL 1.1.1 onwards it returns
                 * success as long as any TLS 1.3 cipher suite is configured, even when the list
                 * selected no TLS 1.2 suite at all
                 *
                 * Without this check an OpenSSL built without the relevant algorithms would
                 * silently become TLS 1.3 only and would then fail every TLS 1.2 peer at
                 * handshake time rather than failing loudly here at configuration time
                 */

                static void chkUsableCipherSuitesAvailable( SAA_inout ::SSL_CTX* nativeSslContext )
                {
                    const auto* ciphers = ::SSL_CTX_get_ciphers( nativeSslContext );

                    BL_CHK_CRYPTO_API_NM( ciphers );

                    int usableCount = 0;

                    /*
                     * Note that sk_SSL_CIPHER_num and sk_SSL_CIPHER_value are macros and
                     * therefore they must not be qualified with the global namespace operator
                     *
                     * ::SSL_CIPHER_get_version returns the name of the lowest protocol version
                     * in which the cipher suite can be negotiated; it is available on all the
                     * versions of OpenSSL we support and on the versions which predate TLS 1.3
                     * it simply never returns the TLS 1.3 name, so the loop below degenerates
                     * into a plain non-empty check there, which is the correct behavior
                     */

                    for( int i = 0, count = sk_SSL_CIPHER_num( ciphers ); i < count; ++i )
                    {
                        const char* version = ::SSL_CIPHER_get_version( sk_SSL_CIPHER_value( ciphers, i ) );

                        if( version && std::string( "TLSv1.3" ) == version )
                        {
                            continue;
                        }

                        ++usableCount;
                    }

                    BL_CHK_CRYPTO_API(
                        usableCount > 0,
                        "No usable TLS cipher suites are configured"
                        );
                }

                static void initNativeSslContext( SAA_inout ::SSL_CTX* nativeSslContext )
                {
                    auto options = ::SSL_CTX_get_options( nativeSslContext );

                    /*
                     * Disable the non-secure protocols; the minimum protocol version which will
                     * be negotiated is TLS 1.2 on every version of OpenSSL we support and there
                     * is deliberately no way to lower it - TLS 1.0 and TLS 1.1 have known
                     * weaknesses and a peer which cannot speak TLS 1.2 needs to be upgraded
                     * rather than accommodated; see the decision record in
                     * notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md
                     *
                     * Note that we also allow for all bug workarounds via SSL_OP_ALL; on the
                     * OpenSSL versions we support this only enables interoperability workarounds
                     * and the historically dangerous members of it have become no-ops
                     */

                    options |= (
                        SSL_OP_NO_SSLv2 |
                        SSL_OP_NO_SSLv3 |
                        SSL_OP_NO_TLSv1 |
                        SSL_OP_NO_TLSv1_1 |
                        SSL_OP_ALL |
                        SSL_OP_NO_TICKET
                        );

                    /*
                     * Ignore the return value because it is the new bitmask
                     */

                    ( void ) ::SSL_CTX_set_options( nativeSslContext, options );

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
                    /*
                     * ::SSL_CTX_set_min_proto_version is the API which OpenSSL recommends for
                     * protocol selection, but it does not exist before OpenSSL 1.1.0, which is
                     * why the SSL_OP_NO_TLSv1* bits above are set as well; on 1.1.0+ both
                     * mechanisms are in force and they agree
                     *
                     * Unlike the option bits this is a real floor rather than an enumeration of
                     * the denied versions and it can be queried back, which makes the policy
                     * verifiable
                     */

                    BL_CHK_CRYPTO_API_NM(
                        ::SSL_CTX_set_min_proto_version( nativeSslContext, TLS1_2_VERSION )
                        );

                    /*
                     * The security level is pinned explicitly rather than left at whatever the
                     * linked OpenSSL was compiled with (OPENSSL_TLS_SECURITY_LEVEL, which is 1
                     * upstream but which some distributions raise), so that the floor is a
                     * property of this library rather than of the particular build of OpenSSL
                     *
                     * Level 2 requires 112 bits of security: RSA, DSA and DH keys below 2048
                     * bits and ECC keys below 224 bits are refused, both in the handshake and
                     * in certificate chain verification (the level is copied into the X.509
                     * auth level), as are RC4 and SSL 3.0, and compression is disabled; on
                     * OpenSSL 3.x it also refuses SHA-1 signatures. Level 3 would refuse
                     * 2048-bit RSA keys, which are still the deployed norm, so level 2 is the
                     * highest one which is usable
                     *
                     * Note that the level is applied before the server's own key and certificate
                     * are loaded, so a server certificate which is below the floor is refused
                     * when the context is created (i.e. at server startup) rather than at the
                     * first handshake
                     *
                     * Note also that a '@SECLEVEL=' token in a cipher list overrides this call,
                     * so the cipher list below must never carry one
                     */

                    ::SSL_CTX_set_security_level( nativeSslContext, 2 );

                    BL_CHK_CRYPTO_API(
                        2 == ::SSL_CTX_get_security_level( nativeSslContext ),
                        "The OpenSSL security level could not be set"
                        );
#endif

                    /*
                     * Enable only forward-secret ciphers with a modern bulk cipher for the
                     * protocols up to and including TLS 1.2
                     *
                     * Note that the 3DES suites which used to be part of this list have been
                     * removed and are now also denied explicitly, so that no future alias can
                     * reintroduce them silently
                     *
                     * Note also that only cipher aliases are used here and never individual
                     * cipher names, so that the policy resolves identically on every version of
                     * OpenSSL we support; OpenSSL ignores unrecognized tokens silently, so an
                     * alias which does not exist on the older versions (CHACHA20 for example)
                     * would make the effective policy differ between builds for a reason which
                     * is not visible in the source
                     *
                     * Note that ::SSL_CTX_set_cipher_list configures the protocols up to and
                     * including TLS 1.2 only and it does not affect the TLS 1.3 cipher suites,
                     * which are configured via ::SSL_CTX_set_ciphersuites; that API is
                     * deliberately not called because the OpenSSL default TLS 1.3 suite list is
                     * already exactly the set we would ask for - TLS 1.3 has no non-AEAD,
                     * non-forward-secret or NULL suites to remove - and because pinning it would
                     * make this library rather than the platform the owner of the decision of
                     * which TLS 1.3 suite to drop when one is found to be weak
                     */

                    BL_CHK_CRYPTO_API_NM(
                        ::SSL_CTX_set_cipher_list(
                            nativeSslContext,
                            "ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:"
                            "!aNULL:!eNULL:!kRSA:!PSK:!SRP:!MD5:!RC4:!3DES:!DES:!EXPORT"
                            )
                        );

                    chkUsableCipherSuitesAvailable( nativeSslContext );

                    ( void ) loadAllKnownCertificateAuthorities( nativeSslContext );
                }

                static void initSsl()
                {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                    /*
                     * OpenSSL 3.x+: SSL_library_init() is deprecated and becomes a no-op.
                     * Threading is handled automatically; no manual locking callbacks needed.
                     * Use OPENSSL_init_ssl() if explicit initialization is required.
                     *
                     * Note that unlike ::SSL_library_init(), which is documented to always return 1
                     * and whose return value is therefore correctly discarded on the 1.1.x branch
                     * below, ::OPENSSL_init_ssl() returns 0 on failure. It must be checked, or a
                     * failed initialization proceeds silently into initRandomEngine() and into
                     * context creation, where the eventual error is far from its cause
                     */

                    BL_CHK_CRYPTO_API_NM( ::OPENSSL_init_ssl( 0, nullptr ) );

                    initRandomEngine();
#else
                    /*
                     * OpenSSL 1.1.x: According to the OpenSSL docs (https://www.openssl.org/docs/ssl/SSL_library_init.html)
                     * ::SSL_library_init() always returns 1, so the return value should not be checked
                     */

                    ( void ) ::SSL_library_init();

                    /*
                     * First register the lock callbacks and then initialize the global state
                     * (e.g. the random engine, context, etc)
                     */

                    const int lockCount = CRYPTO_num_locks();
                    BL_CHK_CRYPTO_API_NM( lockCount > 0 );

                    g_locks = new os::mutex[ lockCount ];
                    g_lockCount = lockCount;

                    CRYPTO_set_locking_callback( &callbackLocking );

                    initRandomEngine();
#endif

                    /*
                     * TODO: we need to load the root certificates here
                     * calling m_sslContext -> set_default_verify_paths() does not
                     * work because OpenSSL doesn't work natively with the MSFT CERT
                     * store on Windows
                     */

                    g_sslContext = new asio::ssl::context( asio::ssl::context::sslv23 );

                    initNativeSslContext( g_sslContext -> native_handle() );

                    /*
                     * This is the default / client context and thus we want to disable session caching
                     */

                    ( void ) ::SSL_CTX_set_session_cache_mode( g_sslContext -> native_handle(), SSL_SESS_CACHE_OFF );
                }

                static auto getAsioSslContext() NOEXCEPT -> asio::ssl::context&
                {
                    if( g_sslContext )
                    {
                        return *g_sslContext;
                    }

                    BL_RIP_MSG( "OpenSSL was not initialized properly" );
                }

                static auto createAsioSslServerContext(
                    SAA_in              const std::string&                  privateKeyPem,
                    SAA_in              const std::string&                  certificatePem
                    ) -> cpp::SafeUniquePtr< asio::ssl::context >
                {
                    auto context = cpp::SafeUniquePtr< asio::ssl::context >::attach(
                        new asio::ssl::context( asio::ssl::context::sslv23 )
                        );

                    initNativeSslContext( context -> native_handle() );

                    /*
                     * This is a server context and thus we want to enable session caching explicitly
                     * (it is the default mode, but enabling it explicitly is better)
                     */

                    ( void ) ::SSL_CTX_set_session_cache_mode( context -> native_handle(), SSL_SESS_CACHE_SERVER );

                    /*
                     * To make sure that server side session caching works properly (see
                     * SSL_CTX_set_session_cache_mode in links below) a session id context
                     * must be set with SSL_CTX_set_session_id_context which should be a random
                     * static data with length no bigger than SSL_MAX_SSL_SESSION_ID_LENGTH
                     * (which is 32)
                     *
                     * The default session caching mode is SSL_SESS_CACHE_SERVER
                     *
                     * For more details see the following links:
                     *
                     * https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_session_cache_mode.html
                     * https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_session_id_context.html
                     */

                    static_assert(
                        sizeof( g_sessionIdContext ) < SSL_MAX_SSL_SESSION_ID_LENGTH,
                        "sizeof( g_sessionIdContext ) must be less than SSL_MAX_SSL_SESSION_ID_LENGTH"
                        );

                    BL_CHK_CRYPTO_API_NM(
                        ::SSL_CTX_set_session_id_context(
                            context -> native_handle(),
                            reinterpret_cast< const unsigned char * >( &g_sessionIdContext ),
                            sizeof( g_sessionIdContext )
                            )
                        );

                    context -> use_private_key(
                        asio::const_buffer( privateKeyPem.data(), privateKeyPem.size() ),
                        boost::asio::ssl::context::pem
                        );

                    context -> use_certificate_chain(
                        asio::const_buffer( certificatePem.data(), certificatePem.size() )
                        );

                    /*
                     * The private key and the leaf certificate must be a matching pair
                     *
                     * Note that this check must come after the certificate has been loaded
                     * because it compares the private key against the certificate which is
                     * currently in the context
                     *
                     * Without it a mismatched pair is only discovered when the first client
                     * attempts a handshake and it is then reported as a per-connection error
                     * rather than as the server misconfiguration which it is
                     */

                    BL_CHK_CRYPTO_API_NM( ::SSL_CTX_check_private_key( context -> native_handle() ) );

                    return context;
                }

                static bool hasUntrustedEndpoints() NOEXCEPT
                {
                    BL_MUTEX_GUARD( g_untrustedEndpointsInfoLock );

                    return ! g_untrustedEndpointsInfo.empty();
                }

                static auto getUntrustedEndpointsInfo() -> std::map< std::string, std::string >
                {
                    std::map< std::string, std::string > result;

                    {
                        BL_MUTEX_GUARD( g_untrustedEndpointsInfoLock );

                        result = g_untrustedEndpointsInfo;
                    }

                    return result;
                }

                static void setUntrustedEndpointInfo(
                    SAA_in              std::string&&                       endpointId,
                    SAA_in              std::string&&                       info
                    )
                {
                    BL_MUTEX_GUARD( g_untrustedEndpointsInfoLock );

                    const auto pair =
                        g_untrustedEndpointsInfo.emplace( BL_PARAM_FWD( endpointId ), BL_PARAM_FWD( info ) );

                    if( pair.second /* true if added */ )
                    {
                        /*
                         * Note we can't use endpointId and info in the log message below
                         * because they have already been moved into the container
                         *
                         * We can use 'pair.first -> first/second' instead is an iterator
                         * to the inserted element (which is a pair since the container
                         * is a map)
                         */

                        BL_LOG(
                            Logging::warning(),
                            BL_MSG()
                                << "An SSL certificate sent from endpoint '"
                                << pair.first -> first /* endpointId */
                                << "' cannot be verified: "
                                << pair.first -> second /* error info */
                            );
                    }
                }

                static void clearUntrustedEndpointInfo( SAA_in const std::string& endpointId )
                {
                    BL_MUTEX_GUARD( g_untrustedEndpointsInfoLock );

                    if( g_untrustedEndpointsInfo.empty() )
                    {
                        return;
                    }

                    /*
                     * Erase returns the # of elements which were deleted
                     *
                     * For a map container this can only be zero or one of course
                     */

                    if( g_untrustedEndpointsInfo.erase( endpointId ) )
                    {
                        BL_LOG(
                            Logging::info(),
                            BL_MSG()
                                << "A valid SSL certificate was obtained successfully from '"
                                << endpointId
                                << "'"
                            );
                    }
                }

                static bool allowUntrustedCertificates() NOEXCEPT
                {
                    return g_allowUntrustedCertificates;
                }

                static void allowUntrustedCertificates( SAA_in const bool allowUntrusted ) NOEXCEPT
                {
                    g_allowUntrustedCertificates = allowUntrusted;
                }
            };

            BL_DEFINE_STATIC_MEMBER( CryptoInitT, asio::ssl::context*, g_sslContext ) = nullptr;
            BL_DEFINE_STATIC_MEMBER( CryptoInitT, os::mutex*, g_locks ) = nullptr;
            BL_DEFINE_STATIC_MEMBER( CryptoInitT, int, g_lockCount ) = 0;
            BL_DEFINE_STATIC_MEMBER( CryptoInitT, int, g_sessionIdContext ) = 42;
            BL_DEFINE_STATIC_MEMBER( CryptoInitT, bool, g_allowUntrustedCertificates ) = false;

            template
            <
                typename E
            >
            std::map< std::string, std::string >
            CryptoInitT< E >::g_untrustedEndpointsInfo;

            BL_DEFINE_STATIC_MEMBER( CryptoInitT, os::mutex, g_untrustedEndpointsInfoLock );

            typedef CryptoInitT<> CryptoInit;

        } // detail

        template
        <
            typename E = void
        >
        class CryptoBaseT
        {
        protected:

            static os::mutex                                            g_lock;
            static bool                                                 g_initialized;

            static bool                                                 g_dllsPinned;

            static void chk2InitCrypto()
            {
                BL_MUTEX_GUARD( g_lock );

                if( g_initialized )
                {
                    return;
                }

                detail::TrustedRoots::initGlobalTrustedRoots();

                detail::CryptoInit::initSsl();

                g_initialized = true;
            }

            CryptoBaseT()
            {
                init();
            }

            ~CryptoBaseT() NOEXCEPT
            {
                /*
                 * The destructor is declared protected just to make sure this base
                 * cannot be used as virtual base
                 */
            }

        public:

            static void init()
            {
                #if defined( _WIN32 )

                /*
                 * OpenSSL triggers app verifier leak issue where it tries to
                 * unload some DLLs which have made memory allocations, but
                 * have not freed them
                 *
                 * These DLLs are likely not designed to be loaded and unloaded
                 * dynamically as OpenSSL tries to do
                 *
                 * The fix is to pin these DLLs before OpenSSL attempts to load
                 * and unload, so they're never unloaded
                 */

                if( ! g_dllsPinned )
                {
                    const auto cb = []( SAA_in const std::string& dllName ) -> void
                    {
                        std::wstring wname( dllName.begin(), dllName.end() );

                        if( ! ::LoadLibraryW( wname.c_str() ) )
                        {
                            eh::error_code ec( ::GetLastError(), eh::system_category() );

                            BL_CHK_EC(
                                ec,
                                BL_MSG()
                                    << "Could not load the following DLL: '"
                                    << dllName
                                    << "'"
                                );
                        }
                    };

                    cb( "wkscli.dll" );
                    cb( "netapi32.dll" );

                    g_dllsPinned = true;
                }

                #endif // defined( _WIN32 )

                chk2InitCrypto();
            }

            static auto getAsioSslContext() NOEXCEPT -> asio::ssl::context&
            {
                return detail::CryptoInit::getAsioSslContext();
            }

            static auto createAsioSslServerContext(
                SAA_in              const std::string&                  privateKeyPem,
                SAA_in              const std::string&                  certificatePem
                )
                -> cpp::SafeUniquePtr< asio::ssl::context >
            {
                init();

                return detail::CryptoInit::createAsioSslServerContext( privateKeyPem, certificatePem );
            }

            /**
             * @brief Whether a client connection whose peer certificate could not be verified
             * is nevertheless allowed to complete the handshake
             *
             * The default is false - i.e. the connection fails closed
             *
             * This used to be hard-coded to true, on the reasoning that an expired or otherwise
             * unverifiable certificate should be a soft error which the application reports to
             * the user and lets them continue, in the way a browser does; the reasoning is sound
             * but the second half of it was never implemented - the failure is only recorded in
             * the untrusted endpoints map and logged as a warning, and nothing consumes it - so
             * in practice it disabled certificate verification altogether
             *
             * An application which genuinely implements the prompt-the-user behavior, or which
             * connects to endpoints with self-signed or otherwise unverifiable certificates on
             * purpose, can restore the previous behavior by calling the setter below before it
             * establishes any connection, and can then use hasUntrustedEndpoints() and
             * getUntrustedEndpointsInfo() to report what was accepted
             *
             * Note that the trust anchors are the roots bundled in TrustedRoots.h plus whatever
             * was passed to registerTrustedRoot(); the platform certificate store is
             * deliberately not consulted (see the comment on set_default_verify_paths in
             * initSsl above), so an endpoint whose root is only in the platform store must be
             * registered explicitly rather than handled by allowing untrusted certificates
             */

            static bool allowUntrustedCertificates() NOEXCEPT
            {
                return detail::CryptoInit::allowUntrustedCertificates();
            }

            static void allowUntrustedCertificates( SAA_in const bool allowUntrusted ) NOEXCEPT
            {
                detail::CryptoInit::allowUntrustedCertificates( allowUntrusted );
            }

            static bool hasUntrustedEndpoints() NOEXCEPT
            {
                return detail::CryptoInit::hasUntrustedEndpoints();
            }

            static auto getUntrustedEndpointsInfo() -> std::map< std::string, std::string >
            {
                return detail::CryptoInit::getUntrustedEndpointsInfo();
            }

            static void setUntrustedEndpointInfo(
                SAA_in              std::string&&                      endpointId,
                SAA_in              std::string&&                      info
                )
            {
                detail::CryptoInit::setUntrustedEndpointInfo( BL_PARAM_FWD( endpointId ), BL_PARAM_FWD( info ) );
            }

            static void clearUntrustedEndpointInfo( SAA_in const std::string& endpointId )
            {
                detail::CryptoInit::clearUntrustedEndpointInfo( endpointId );
            }
        };

        BL_DEFINE_STATIC_MEMBER( CryptoBaseT, bool, g_dllsPinned ) = false;

        template
        <
            typename E
        >
        os::mutex
        CryptoBaseT< E >::g_lock;

        template
        <
            typename E
        >
        bool
        CryptoBaseT< E >::g_initialized = false;

        typedef CryptoBaseT<> CryptoBase;

    } // crypto

} // bl

#endif /* __BL_CRYPTO_CRYPTOBASE_H_ */
