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
#include <baselib/crypto/TlsClientProfile.h>
#include <baselib/crypto/TrustedRoots.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/Random.h>
#include <baselib/core/BaseIncludes.h>

#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <atomic>

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
                static std::atomic< bool >                      g_allowUntrustedCertificates;

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

#if OPENSSL_VERSION_NUMBER < 0x10100000L
                /*
                 * The locking callback is only needed for OpenSSL 1.0.x (devenv2): from 1.1.0
                 * onwards OpenSSL locks internally, CRYPTO_num_locks() is a compatibility macro
                 * which expands to 1 and CRYPTO_set_locking_callback() to nothing, so
                 * installing one there would allocate a mutex nothing ever uses
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
                            toIntSize( pemKeyText.size() )
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
                 * value is checked at the call site. From OpenSSL 1.1.1 onwards a cipher list
                 * and the TLS 1.3 suites are configured separately, and this check is defence in
                 * depth against a version which reports success as long as any TLS 1.3 suite is
                 * configured even when the list selected no TLS 1.2 suite at all: the 1.1.1w and
                 * 3.x sources already fail the call in that case (they count the TLS 1.2 suites
                 * separately), but that could not be established for every 1.1.1 letter release
                 *
                 * Without this check such an OpenSSL, built without the relevant algorithms,
                 * would silently become TLS 1.3 only and would then fail every TLS 1.2 peer at
                 * handshake time rather than failing loudly here at configuration time
                 *
                 * Before OpenSSL 1.1.0 there is no ::SSL_CTX_get_ciphers and no TLS 1.3, so
                 * ::SSL_CTX_set_cipher_list returning zero when nothing matched, which the call
                 * site checks, is the whole check there
                 */

                static void chkUsableCipherSuitesAvailable( SAA_inout ::SSL_CTX* nativeSslContext )
                {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
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
#else
                    BL_UNUSED( nativeSslContext );
#endif
                }

                /**
                 * @brief Step 1 of the context configuration - the protocol floor, the option
                 * bits which harden the context, and the security level
                 *
                 * This step is common to every context the library builds and it is never
                 * parameterized: there is no caller supplied input which can lower the floor,
                 * clear one of the hardening bits or move the security level
                 */

                static void initNativeSslProtocolPolicy( SAA_inout ::SSL_CTX* nativeSslContext )
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
                     *
                     * TLS compression is refused explicitly (CRIME): OpenSSL 1.1.0+ refuses it by
                     * default and the security level below refuses it as well, so on those
                     * versions this only pins the policy, but a 1.0.2 build with zlib support
                     * would otherwise still offer it
                     *
                     * The server's own preference order is made authoritative, so that the cipher
                     * list below (AEAD suites first) decides the suite rather than the order in
                     * which the client happened to list them; the bit has no effect on the client
                     * role or on TLS 1.3, where the server always chooses
                     */

                    options |= (
                        SSL_OP_NO_SSLv2 |
                        SSL_OP_NO_SSLv3 |
                        SSL_OP_NO_TLSv1 |
                        SSL_OP_NO_TLSv1_1 |
                        SSL_OP_ALL |
                        SSL_OP_NO_TICKET |
                        SSL_OP_NO_COMPRESSION |
                        SSL_OP_CIPHER_SERVER_PREFERENCE
                        );

#ifdef SSL_OP_NO_RENEGOTIATION
                    /*
                     * Neither this library nor Boost.Asio ever initiates a renegotiation and
                     * TLS 1.3 has none, so refusing it costs nothing: on the server role it
                     * removes client initiated renegotiation storms as a denial of service
                     * vector and on the client role it turns a server's HelloRequest into a
                     * no_renegotiation alert (a TLS 1.2 server which renegotiates in order to
                     * demand a client certificate would fail, but this library never presents
                     * one, so such a server fails today as well). The option exists from OpenSSL
                     * 1.1.0h onwards, which is why it is tested by name rather than by version
                     */

                    options |= SSL_OP_NO_RENEGOTIATION;
#endif

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
                }

                /**
                 * @brief Step 2 of the context configuration - the cipher policy
                 *
                 * This is the hardened default list, which every context the library builds
                 * receives; it is a step of its own because it is the one part of the policy a
                 * TLS client profile is meant to be able to provide for itself, whereas steps 1
                 * and 3 are not negotiable
                 */

                static void initNativeSslDefaultCipherPolicy( SAA_inout ::SSL_CTX* nativeSslContext )
                {
                    /*
                     * Enable only forward-secret AEAD suites (ephemeral ECDH or DH key exchange
                     * with AES-GCM) for the protocols up to and including TLS 1.2; the CBC
                     * suites with an HMAC are deliberately not offered, so a peer cannot steer a
                     * connection to a MAC-then-encrypt construction. EECDH and EDH are the
                     * aliases which spell that on every supported version of OpenSSL
                     *
                     * Note that the 3DES suites which used to be part of this list have been
                     * removed and are now also denied explicitly, so that no future alias can
                     * reintroduce them silently; the same is asserted for every remaining suite
                     * by TlsProtocolPolicy_CipherSuitesAreAeadOnly in utf_baselib_http
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
                            "EECDH+AESGCM:EDH+AESGCM:"
                            "!aNULL:!eNULL:!kRSA:!PSK:!SRP:!MD5:!RC4:!3DES:!DES:!EXPORT"
                            )
                        );

                    chkUsableCipherSuitesAvailable( nativeSslContext );
                }

                /**
                 * @brief Configures a native SSL context with the TLS policy of the library
                 *
                 * The three steps are composable on purpose - step 3 is the trust anchors, i.e.
                 * loadAllKnownCertificateAuthorities above. A per-profile client context is
                 * step 1, then the profile's own cipher policy in place of step 2, then the
                 * same trust anchors; every context the library builds today - the process
                 * global client context and every server context - is all three of them in this
                 * order, exactly as when this was a single function
                 */

                static void initNativeSslContext( SAA_inout ::SSL_CTX* nativeSslContext )
                {
                    initNativeSslProtocolPolicy( nativeSslContext );

                    initNativeSslDefaultCipherPolicy( nativeSslContext );

                    ( void ) loadAllKnownCertificateAuthorities( nativeSslContext );
                }

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

                /*
                 * Everything below is the TLS client profile support of
                 * notes/plans/http2-design.md 3.3 and it is declared only from OpenSSL 1.1.0
                 * onwards, which is where the APIs it is built on exist:
                 * ::SSL_CIPHER_get_kx_nid, ::SSL_CIPHER_get_auth_nid and ::SSL_CIPHER_is_aead for
                 * the floor check and ::SSL_CTX_set_ciphersuites for the TLS 1.3 suite list
                 *
                 * The feature itself is devenv7 and later (D1), i.e. OpenSSL 3.5.4 or 1.1.1w, so
                 * nothing which can reach these names is compiled on an older branch; declaring
                 * them there would only add a runtime failure where a build failure is clearer
                 */

                /**
                 * @brief The OpenSSL version from which a TLS client profile is honored (D2)
                 *
                 * Below it a profile is refused rather than approximated. The knobs which shape a
                 * current browser's ClientHello - the hybrid key exchange group and more than one
                 * key share in particular - do not exist on the older branch, so a context built
                 * there would be a materially different client rather than a slightly different
                 * one; see notes/plans/http2-design.md 6.3, which tabulates what each branch has
                 *
                 * The rule is a function of a version number rather than a bare #if so that both
                 * of its answers can be asserted on whichever flavor happens to be linked; the
                 * entry point below is what applies it to OPENSSL_VERSION_NUMBER
                 */

                static bool isTlsClientProfileSupportedOnOpenSslVersion(
                    SAA_in              const unsigned long                     openSslVersionNumber
                    ) NOEXCEPT
                {
                    return openSslVersionNumber >= 0x30500000UL;
                }

                static void chkTlsClientProfileSupportedOnOpenSslVersion(
                    SAA_in              const unsigned long                     openSslVersionNumber
                    )
                {
                    if( isTlsClientProfileSupportedOnOpenSslVersion( openSslVersionNumber ) )
                    {
                        return;
                    }

                    BL_THROW(
                        NotSupportedException(),
                        BL_MSG()
                            << "A TLS client profile requires OpenSSL 3.5 or later"
                        );
                }

                /**
                 * @brief Whether a string is a plain cipher suite name, i.e. safe to place in an
                 * OpenSSL cipher list
                 *
                 * Profiles are loaded from JSON (design 6.2), so every name in one is untrusted
                 * input, and an OpenSSL cipher list is a small language rather than a list of
                 * names: ':', ',' and ' ' separate tokens, a leading '!', '-' or '+' deletes or
                 * reorders, and '@' introduces a control token. '@SECLEVEL=0' is one of those and
                 * it silently overrides ::SSL_CTX_set_security_level - see the "Cipher lists"
                 * section of notes/plans/issues/tls-legacy-protocol-opt-in-removal-decision.md -
                 * which is the injection this exists to stop
                 *
                 * The allowlist is a non-empty string of ASCII letters, digits, '_' and '-',
                 * beginning with a letter or a digit
                 *
                 * '-' is accepted inside a name and refused as the first character, which is a
                 * deliberate departure from the literal wording of design 3.3 ("no @, !, +, -, :
                 * inside a name"). Every TLS 1.2 suite name OpenSSL knows carries hyphens -
                 * ECDHE-RSA-AES128-GCM-SHA256 - so refusing the character outright would leave the
                 * TLS 1.2 list of every profile empty and the feature unable to express anything.
                 * Only a leading '-' is an operator, so that is what is refused. ',' and ' ' are
                 * refused as well although 3.3 does not name them; they separate tokens exactly as
                 * ':' does
                 *
                 * This is the outermost of three layers and not the one which is relied upon: the
                 * context builder asserts the security level is still 2 after the list has been
                 * applied, and the post-handshake floor check refuses a below-floor suite whatever
                 * was advertised
                 */

                static bool isCipherSuiteNameSafe( SAA_in const std::string& name ) NOEXCEPT
                {
                    if( name.empty() )
                    {
                        return false;
                    }

                    for( std::size_t i = 0U; i < name.size(); ++i )
                    {
                        const char ch = name[ i ];

                        if(
                            ( ch >= 'A' && ch <= 'Z' ) ||
                            ( ch >= 'a' && ch <= 'z' ) ||
                            ( ch >= '0' && ch <= '9' )
                            )
                        {
                            continue;
                        }

                        if( 0U == i || ( '_' != ch && '-' != ch ) )
                        {
                            return false;
                        }
                    }

                    return true;
                }

                /**
                 * @brief Validates every name and joins them into an OpenSSL cipher list
                 *
                 * The empty list is a valid answer and means the profile named no suites for that
                 * protocol, which the caller reads as "keep the library default"
                 *
                 * appendExclusions asks for the library's own exclusion tokens to be appended
                 * after the profile's names, so a profile cannot offer an unauthenticated or a
                 * NULL suite in the first place - the same '!aNULL:!eNULL' the hardened default
                 * list above already carries. They are the builder's own text rather than a
                 * profile's, so the name allowlist is untouched by them; and they are the outer
                 * of the two layers which keep an anonymous suite out, the inner being the
                 * authentication axis of the floor check below
                 *
                 * Only the TLS 1.2 list asks for them. ::SSL_CTX_set_ciphersuites does not speak
                 * the cipher list language at all - it splits the string on ':' and looks up each
                 * element as a suite name, silently ignoring one it does not know - so the tokens
                 * would be dead text in the TLS 1.3 list, and there is nothing there for them to
                 * remove either, every TLS 1.3 suite reporting NID_auth_any
                 *
                 * Note that the tokens are appended only to a non-empty list, because an empty
                 * one is what tells the caller to keep the library default, and a list of nothing
                 * but exclusions would not be empty
                 */

                static auto buildCipherListFromNames(
                    SAA_in              const std::vector< std::string >&       names,
                    SAA_in              const bool                              appendExclusions
                    )
                    -> std::string
                {
                    std::string result;

                    for( const auto& name : names )
                    {
                        if( ! isCipherSuiteNameSafe( name ) )
                        {
                            BL_THROW(
                                SecurityException()
                                    << eh::errinfo_string_value( name ),
                                BL_MSG()
                                    << "A TLS client profile carries a cipher suite name which is not a plain suite name"
                                );
                        }

                        if( ! result.empty() )
                        {
                            result += ':';
                        }

                        result += name;
                    }

                    if( appendExclusions && ! result.empty() )
                    {
                        result += ":!aNULL:!eNULL";
                    }

                    return result;
                }

                /**
                 * @brief Whether a negotiated protocol version and cipher suite meet the library
                 * floor (D4)
                 *
                 * The floor is TLS 1.2 or better, with an ephemeral key exchange, certificate
                 * authentication and an AEAD cipher - three axes, all of which a TLS 1.3 suite
                 * satisfies. It is strictly stronger than the cipher blocklist of RFC 9113
                 * Appendix A, which is why this library never has to raise INADEQUATE_SECURITY of
                 * its own accord
                 *
                 * The version comparison is guarded by the major version byte, which is the idiom
                 * OpenSSL's own tls1.h uses (SSL_get_secure_renegotiation_support and the macros
                 * beside it): a DTLS version is numerically far larger than any TLS version -
                 * DTLS 1.0 is 0xFEFF - and would sail through a bare >= comparison
                 *
                 * A TLS 1.3 suite is not special cased. OpenSSL reports a key exchange of
                 * NID_kx_any and an authentication of NID_auth_any for one, because TLS 1.3
                 * settles both outside the suite, and every TLS 1.3 suite is AEAD - which is
                 * asserted by a handshake rather than assumed here
                 *
                 * The authentication axis is what makes the Appendix A claim above true rather
                 * than nearly true: the anonymous AEAD suites that appendix names - among them
                 * TLS_DH_anon_WITH_AES_128_GCM_SHA256 and its 256-bit sibling, which OpenSSL
                 * spells ADH-AES128-GCM-SHA256 and ADH-AES256-GCM-SHA384 - are ephemeral and AEAD
                 * and pass the other two axes. OpenSSL's security level 2 refuses an
                 * unauthenticated suite as well, whatever its strength, and every context this
                 * library builds pins that level - but the level is not where D4 says the check
                 * lives, and a floor which asked nothing about authentication would be resting on
                 * a behaviour of OpenSSL rather than on itself
                 *
                 * The axis is written as the set of authentications which are accepted rather
                 * than as a refusal of NID_auth_null, so that it does not depend on which NID a
                 * given OpenSSL maps an unauthenticated suite to, and so that an authentication
                 * method this library has never seen fails closed. The four accepted values are
                 * the certificate ones: NID_auth_rsa, NID_auth_ecdsa, NID_auth_dss - which the
                 * hardened default list above really does offer, as DHE-DSS-AES128-GCM-SHA256 -
                 * and NID_auth_any for TLS 1.3. The PSK and SRP families never reach this test:
                 * their key exchanges are NID_kx_psk, NID_kx_dhe_psk, NID_kx_ecdhe_psk,
                 * NID_kx_rsa_psk and NID_kx_srp, and none of those is accepted above
                 */

                static bool doNegotiatedParametersMeetFloor(
                    SAA_in              const int                               protocolVersion,
                    SAA_in_opt          const ::SSL_CIPHER*                     cipher
                    ) NOEXCEPT
                {
                    if( nullptr == cipher )
                    {
                        return false;
                    }

                    if(
                        TLS1_VERSION_MAJOR != ( protocolVersion >> 8 ) ||
                        protocolVersion < TLS1_2_VERSION
                        )
                    {
                        return false;
                    }

                    const int keyExchange = ::SSL_CIPHER_get_kx_nid( cipher );

                    const bool isEphemeralKeyExchange =
                        NID_kx_ecdhe == keyExchange ||
                        NID_kx_dhe == keyExchange ||
                        NID_kx_any == keyExchange;

                    const int authentication = ::SSL_CIPHER_get_auth_nid( cipher );

                    const bool isCertificateAuthentication =
                        NID_auth_rsa == authentication ||
                        NID_auth_ecdsa == authentication ||
                        NID_auth_dss == authentication ||
                        NID_auth_any == authentication;

                    return
                        isEphemeralKeyExchange &&
                        isCertificateAuthentication &&
                        0 != ::SSL_CIPHER_is_aead( cipher );
                }

                /**
                 * @brief Refuses a connection whose negotiated parameters are below the floor (D4)
                 *
                 * This runs after the handshake and before the first byte of HTTP is written or
                 * read, so a server which steered the connection below the floor never sees a
                 * request. It is what makes "advertise, verify, refuse" the whole of D4: a profile
                 * may advertise a browser's suite list, but what was actually negotiated is
                 * checked here against the library's own floor rather than against the profile's
                 */

                static void chkNegotiatedParametersMeetFloor( SAA_inout ::SSL* ssl )
                {
                    BL_ASSERT( ssl );

                    const ::SSL_CIPHER* const cipher = ::SSL_get_current_cipher( ssl );

                    if( doNegotiatedParametersMeetFloor( ::SSL_version( ssl ), cipher ) )
                    {
                        return;
                    }

                    const char* const cipherName = cipher ? ::SSL_CIPHER_get_name( cipher ) : nullptr;
                    const char* const versionName = ::SSL_get_version( ssl );

                    BL_THROW(
                        SecurityException()
                            << eh::errinfo_tls_negotiated_cipher( cipherName ? cipherName : "<none>" )
                            << eh::errinfo_tls_negotiated_version( versionName ? versionName : "<none>" ),
                        BL_MSG()
                            << "The TLS parameters negotiated with the peer are below the security floor of the library"
                        );
                }

                /**
                 * @brief Creates a client context which advertises a TLS client profile (D4, D22)
                 *
                 * The context is step 1 of initNativeSslContext above - the protocol floor, the
                 * hardening options and security level 2, none of which a profile can move - then
                 * the profile's own cipher policy in place of step 2, then the same trust anchors
                 *
                 * Trust is shared rather than copied: ::SSL_CTX_set1_cert_store takes a reference
                 * to the store of the process global client context, so a root registered after a
                 * profile context was built is visible through it and the default path and the
                 * profiles can never diverge
                 *
                 * Session tickets are advertised when the profile asks for them, because a
                 * browser's first ClientHello carries the extension, and nothing is ever resumed:
                 * the session cache is off, as it is on the global client context. A real
                 * browser's later connections carry pre_shared_key and ours will not - that is a
                 * known and accepted fidelity gap (D22)
                 *
                 * What this deliberately does NOT yet apply, although TlsClientProfile carries it:
                 * the group list and its key share marks, the signature algorithms, and the
                 * status_request, SCT and padding switches. Wiring a profile across the layers is
                 * S7.3 in notes/plans/http2-implementation-plan.md, after the measured fidelity
                 * spike of 6.3 has established what each knob really does on this OpenSSL. A
                 * context from here is therefore shaped by its cipher lists alone
                 */

                static auto createAsioSslClientContext(
                    SAA_in              const TlsClientProfile&                 profile
                    )
                    -> cpp::SafeUniquePtr< asio::ssl::context >
                {
                    chkTlsClientProfileSupportedOnOpenSslVersion( OPENSSL_VERSION_NUMBER );

#if OPENSSL_VERSION_NUMBER >= 0x30500000L
                    auto context = cpp::SafeUniquePtr< asio::ssl::context >::attach(
                        new asio::ssl::context( asio::ssl::context::sslv23 )
                        );

                    ::SSL_CTX* const nativeSslContext = context -> native_handle();

                    /*
                     * Step 1 - common, and never parameterized by the profile
                     */

                    initNativeSslProtocolPolicy( nativeSslContext );

                    /*
                     * Step 2 - the profile's cipher policy, or the hardened library default when
                     * the profile names no TLS 1.2 suites
                     *
                     * Both lists are validated before either is applied, so a bad name in the
                     * TLS 1.3 list cannot leave a half configured context behind
                     *
                     * The TLS 1.2 list carries the library's exclusion tokens after the profile's
                     * names and the TLS 1.3 one does not; buildCipherListFromNames says why
                     */

                    const auto cipherListTls12 =
                        buildCipherListFromNames( profile.cipherSuitesTls12, true /* appendExclusions */ );

                    const auto cipherListTls13 =
                        buildCipherListFromNames( profile.cipherSuitesTls13, false /* appendExclusions */ );

                    if( cipherListTls12.empty() )
                    {
                        initNativeSslDefaultCipherPolicy( nativeSslContext );
                    }
                    else
                    {
                        BL_CHK_CRYPTO_API_NM(
                            ::SSL_CTX_set_cipher_list( nativeSslContext, cipherListTls12.c_str() )
                            );

                        chkUsableCipherSuitesAvailable( nativeSslContext );
                    }

                    if( ! cipherListTls13.empty() )
                    {
                        BL_CHK_CRYPTO_API_NM(
                            ::SSL_CTX_set_ciphersuites( nativeSslContext, cipherListTls13.c_str() )
                            );
                    }

                    /*
                     * The check which actually catches a '@SECLEVEL' token that got past the name
                     * allowlist: a cipher list carrying one moves the level, and the level is
                     * readable, so the only thing which has to be true is that it is still what
                     * step 1 set it to
                     */

                    BL_CHK_CRYPTO_API(
                        2 == ::SSL_CTX_get_security_level( nativeSslContext ),
                        "The cipher policy of a TLS client profile moved the OpenSSL security level"
                        );

                    /*
                     * Step 3 - the trust anchors, shared with the process global client context
                     */

                    ::X509_STORE* const trustStore =
                        ::SSL_CTX_get_cert_store( getAsioSslContext().native_handle() );

                    BL_CHK_CRYPTO_API_NM( trustStore );

                    ( void ) ::SSL_CTX_set1_cert_store( nativeSslContext, trustStore );

                    if( profile.sessionTicket )
                    {
                        ( void ) ::SSL_CTX_clear_options( nativeSslContext, SSL_OP_NO_TICKET );
                    }

                    ( void ) ::SSL_CTX_set_session_cache_mode( nativeSslContext, SSL_SESS_CACHE_OFF );

                    return context;
#else
                    BL_UNUSED( profile );

                    /*
                     * Unreachable - the check above throws on every version below the threshold.
                     * It is spelled out rather than left to fall off the end of the function so
                     * that the compiler on that flavor sees a terminating path
                     */

                    BL_RIP_MSG( "A TLS client profile requires OpenSSL 3.5 or later" );
#endif
                }

#endif // OPENSSL_VERSION_NUMBER >= 0x10100000L

                static void initSsl()
                {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                    /*
                     * OpenSSL 3.x+: SSL_library_init() is deprecated and becomes a no-op.
                     * Threading is handled automatically; no manual locking callbacks needed.
                     * Use OPENSSL_init_ssl() if explicit initialization is required.
                     *
                     * ::OPENSSL_init_ssl() returns 0 on failure and must be checked, or a failed
                     * initialization proceeds silently into initRandomEngine() and into context
                     * creation, where the eventual error is far from its cause
                     */

                    BL_CHK_CRYPTO_API_NM( ::OPENSSL_init_ssl( 0, nullptr ) );

                    initRandomEngine();
#elif OPENSSL_VERSION_NUMBER >= 0x10100000L
                    /*
                     * OpenSSL 1.1.x: SSL_library_init() is a compatibility macro over
                     * ::OPENSSL_init_ssl( 0, NULL ), which returns 0 on failure exactly as on the
                     * 3.x branch above, so its result is checked for the same reason; the
                     * "always returns 1" documentation applies to 1.0.x only
                     */

                    BL_CHK_CRYPTO_API_NM( ::SSL_library_init() );

                    initRandomEngine();
#else
                    /*
                     * OpenSSL 1.0.x: According to the OpenSSL docs (https://www.openssl.org/docs/ssl/SSL_library_init.html)
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

                    /*
                     * The private key must never be loaded through the default password
                     * callback of asio - it prompts on the terminal, which makes a daemon
                     * either block on /dev/tty or fail with an unrelated error; an encrypted
                     * key is rejected right here instead
                     */

                    context -> set_password_callback(
                        []( SAA_in const std::size_t /* size */, SAA_in const asio::ssl::context::password_purpose /* purpose */ )
                            -> std::string
                        {
                            BL_THROW(
                                SecurityException(),
                                BL_MSG()
                                    << "The private key of the server is encrypted, which is not supported"
                                );
                        }
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
            BL_DEFINE_STATIC_MEMBER( CryptoInitT, std::atomic< bool >, g_allowUntrustedCertificates )( false );

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

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

            /**
             * @brief Whether this build of OpenSSL can honor a TLS client profile (D2)
             *
             * A caller which offers impersonation as an option asks this rather than catching the
             * NotSupportedException which createAsioSslClientContext below throws
             */

            static bool isTlsClientProfileSupported() NOEXCEPT
            {
                return detail::CryptoInit::isTlsClientProfileSupportedOnOpenSslVersion( OPENSSL_VERSION_NUMBER );
            }

            /**
             * @brief Whether a name is a plain cipher suite name and can be placed in a profile
             *
             * This is the validator a profile loader applies to untrusted input; see the comment
             * on the implementation for what the allowlist is and why
             */

            static bool isCipherSuiteNameSafe( SAA_in const std::string& name ) NOEXCEPT
            {
                return detail::CryptoInit::isCipherSuiteNameSafe( name );
            }

            /**
             * @brief Creates a client context shaped by a TLS client profile (design 3.3)
             *
             * Throws NotSupportedException on an OpenSSL below 3.5 and SecurityException when the
             * profile carries a cipher suite name which is not a plain name
             */

            static auto createAsioSslClientContext( SAA_in const TlsClientProfile& profile )
                -> cpp::SafeUniquePtr< asio::ssl::context >
            {
                init();

                return detail::CryptoInit::createAsioSslClientContext( profile );
            }

            /**
             * @brief Refuses a connection whose negotiated TLS parameters are below the floor (D4)
             *
             * Called after the handshake and before any HTTP byte; throws SecurityException
             * carrying the negotiated suite and version
             */

            static void chkNegotiatedParametersMeetFloor( SAA_inout ::SSL* ssl )
            {
                detail::CryptoInit::chkNegotiatedParametersMeetFloor( ssl );
            }

#endif // OPENSSL_VERSION_NUMBER >= 0x10100000L

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
