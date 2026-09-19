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

/*
 * S0.4 mechanical proof - dumps the full observable configuration of the two kinds of TLS context
 * the library builds, so that the refactor of initNativeSslContext can be shown to change nothing.
 *
 * This is a one-off probe (design 3.8 commit 4, plan S0.4 "Probe"); it is deliberately NOT a test
 * case and NOT a makefile target - see tls_context_dump.sh for how it is built.
 *
 * What it dumps, for the process global client context and for a server context:
 *   - the option bits
 *   - the minimum and maximum protocol version
 *   - the security level
 *   - the ordered list of cipher suites; SSL_CTX_get_ciphers returns the TLS 1.3 suites and the
 *     suites for TLS 1.2 and below in one ordered stack, and each entry carries the protocol it
 *     belongs to, so this covers "the ordered cipher list and the TLS 1.3 suites" in one place
 *   - the session cache mode
 *   - the verify mode and depth
 *   - the number of trust anchors in the certificate store
 */

#include <baselib/crypto/CryptoBase.h>
#include <baselib/crypto/TrustedRoots.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfCrypto.h>

#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <openssl/crypto.h>

#include <iomanip>
#include <iostream>
#include <string>

namespace
{
    std::string toHex( const unsigned long long value )
    {
        std::ostringstream oss;

        oss << "0x" << std::hex << std::setw( 16 ) << std::setfill( '0' ) << value;

        return oss.str();
    }

    std::size_t countTrustAnchors( ::SSL_CTX* const ctx )
    {
        std::size_t count = 0U;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

        ::X509_STORE* const store = ::SSL_CTX_get_cert_store( ctx );

        if( ! store )
        {
            return 0U;
        }

        STACK_OF( X509_OBJECT )* const objects = ::X509_STORE_get0_objects( store );

        if( ! objects )
        {
            return 0U;
        }

        for( int i = 0, size = sk_X509_OBJECT_num( objects ); i < size; ++i )
        {
            if( X509_LU_X509 == ::X509_OBJECT_get_type( sk_X509_OBJECT_value( objects, i ) ) )
            {
                ++count;
            }
        }

#else
        ( void ) ctx;
#endif

        return count;
    }

    void dumpContext(
        const char* const                                   label,
        ::SSL_CTX* const                                    ctx,
        std::ostream&                                       os
        )
    {
        os << "[" << label << "]" << std::endl;

        os << "  options                 = "
           << toHex( static_cast< unsigned long long >( SSL_CTX_get_options( ctx ) ) ) << std::endl;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

        os << "  min_proto_version       = "
           << toHex( static_cast< unsigned long long >( SSL_CTX_get_min_proto_version( ctx ) ) ) << std::endl;

        os << "  max_proto_version       = "
           << toHex( static_cast< unsigned long long >( SSL_CTX_get_max_proto_version( ctx ) ) ) << std::endl;

        os << "  security_level          = " << ::SSL_CTX_get_security_level( ctx ) << std::endl;

#endif

        os << "  session_cache_mode      = "
           << toHex( static_cast< unsigned long long >( SSL_CTX_get_session_cache_mode( ctx ) ) ) << std::endl;

        os << "  verify_mode             = "
           << toHex( static_cast< unsigned long long >( ::SSL_CTX_get_verify_mode( ctx ) ) ) << std::endl;

        os << "  verify_depth            = " << ::SSL_CTX_get_verify_depth( ctx ) << std::endl;

        os << "  trust_anchor_count      = " << countTrustAnchors( ctx ) << std::endl;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

        STACK_OF( SSL_CIPHER )* const ciphers = ::SSL_CTX_get_ciphers( ctx );

        const int count = ciphers ? sk_SSL_CIPHER_num( ciphers ) : -1;

        os << "  cipher_count            = " << count << std::endl;

        for( int i = 0; i < count; ++i )
        {
            const ::SSL_CIPHER* const cipher = sk_SSL_CIPHER_value( ciphers, i );

            os << "  cipher[" << std::setw( 2 ) << std::setfill( '0' ) << i << "]              = "
               << ::SSL_CIPHER_get_name( cipher )
               << " proto=" << ::SSL_CIPHER_get_version( cipher )
               << " id=" << toHex( static_cast< unsigned long long >( ::SSL_CIPHER_get_id( cipher ) ) )
               << " aead=" << ::SSL_CIPHER_is_aead( cipher )
               << " kx=" << ::SSL_CIPHER_get_kx_nid( cipher )
               << " auth=" << ::SSL_CIPHER_get_auth_nid( cipher )
               << std::endl;
        }

#endif
    }

} // __unnamed

int main()
{
    try
    {
        bl::crypto::CryptoBase::init();

        const auto serverContext = bl::crypto::CryptoBase::createAsioSslServerContext(
            test::UtfCrypto::getDefaultServerKey(),
            test::UtfCrypto::getDefaultServerCertificate()
            );

        auto& clientContext = bl::crypto::CryptoBase::getAsioSslContext();

        std::ostream& os = std::cout;

        os << "openssl_version_number    = "
           << toHex( static_cast< unsigned long long >( OPENSSL_VERSION_NUMBER ) ) << std::endl;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        os << "openssl_version_text      = " << ::OpenSSL_version( OPENSSL_VERSION ) << std::endl;
#endif

        os << "registered_trusted_roots  = " << bl::crypto::trustedRoots().size() << std::endl;

        dumpContext( "client", clientContext.native_handle(), os );
        dumpContext( "server", serverContext -> native_handle(), os );

        return 0;
    }
    catch( std::exception& e )
    {
        std::cerr << "tls-context-dump failed: " << e.what() << std::endl;

        return 1;
    }
}
