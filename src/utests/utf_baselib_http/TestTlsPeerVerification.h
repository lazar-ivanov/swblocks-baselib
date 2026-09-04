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

#ifndef __UTEST_TESTTLSPEERVERIFICATION_H_
#define __UTEST_TESTTLSPEERVERIFICATION_H_

#include <baselib/crypto/TlsPeerVerification.h>
#include <baselib/crypto/OpenSSLTypes.h>
#include <baselib/crypto/ErrorHandling.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>

#include <openssl/pem.h>

/*
 * Tests for bl::crypto::TlsPeerVerification
 *
 * Boost 1.89 removed asio::ssl::rfc2818_verification, and AsioSslCompat.h typedefs the old name to
 * host_name_verification so existing code keeps compiling. That substitution is a real improvement
 * in every respect except one: host_name_verification delegates to ::X509_check_host(), which does
 * not match IP addresses, so a peer addressed by IP literal stopped verifying with no diagnostic
 * beyond a handshake failure.
 *
 * These cases pin the restored behaviour. They act on the matching decision directly, against a
 * certificate loaded from PEM, so they need no handshake, no socket and no server - the matching
 * decision is the whole of what changed.
 */

namespace utest
{
    namespace tlspeer
    {
        inline bl::crypto::x509cert_ptr_t loadCertificate( SAA_in const char* pemText )
        {
            const std::string pem( pemText );

            const auto buffer = bl::crypto::bio_ptr_t::attach(
                ::BIO_new_mem_buf(
                    const_cast< char* >( pem.c_str() ),
                    static_cast< int >( pem.size() )
                    )
                );

            BL_CHK_CRYPTO_API_NM( buffer );

            auto certificate = bl::crypto::x509cert_ptr_t::attach(
                ::PEM_read_bio_X509(
                    buffer.get(),
                    nullptr     /* X509 certificate out pointer */,
                    nullptr     /* password callback */,
                    nullptr     /* password bytes */
                    )
                );

            BL_CHK_CRYPTO_API_NM( certificate );

            return certificate;
        }

        inline bool matches(
            SAA_in          const bl::crypto::x509cert_ptr_t&   certificate,
            SAA_in          const std::string&                  peerName
            )
        {
            return bl::crypto::TlsPeerVerification::certificateMatchesPeerName(
                certificate.get(),
                peerName
                );
        }

    } // tlspeer

} // utest

UTF_AUTO_TEST_CASE( TlsPeerVerification_IpAddressLiteralsAreMatched )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    /*
     * This is the case which regressed: the certificate carries IP:127.0.0.1 and IP:::1, and a
     * client connecting to either literal must verify. ::X509_check_host() returns 0 for both, so
     * host_name_verification on its own would refuse them
     */

    UTF_REQUIRE( utest::tlspeer::matches( certificate, "127.0.0.1" ) );
    UTF_REQUIRE( utest::tlspeer::matches( certificate, "::1" ) );

    /*
     * IPv6 is compared as an address rather than as text, so an equivalent spelling of the same
     * address matches too
     */

    UTF_REQUIRE( utest::tlspeer::matches( certificate, "0:0:0:0:0:0:0:1" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_UnlistedIpAddressIsRefused )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "192.168.1.1" ) );
    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "::2" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_AddressLiteralIsNotMatchedAgainstDnsNames )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    /*
     * The sharp edge, and the reason the IP result is treated as final rather than as a first
     * attempt which falls through.
     *
     * The certificate carries DNS:10.11.12.13 - a DNS entry which happens to look like an address -
     * and no IP:10.11.12.13. So for the peer name "10.11.12.13":
     *
     *   ::X509_check_ip_asc() returns 0  - it IS a valid literal, and it does not match
     *   ::X509_check_host()   returns 1  - the DNS entry matches the text
     *
     * A fall-through implementation would therefore accept this certificate as authenticating the
     * host at 10.11.12.13, which RFC 6125 section 6.4 forbids: an address literal is not a domain
     * name. The match must be refused
     */

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "10.11.12.13" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_DnsNamesStillMatch )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    UTF_REQUIRE( utest::tlspeer::matches( certificate, "example.test" ) );
    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "other.test" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_ExistingServerCertificateBehaviourIsUnchanged )
{
    /*
     * A regression guard on the certificate the rest of the suite uses, which carries only DNS
     * entries: *.*.mycompany.com and localhost
     */

    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getDefaultServerCertificate() );

    UTF_REQUIRE( utest::tlspeer::matches( certificate, "localhost" ) );

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "mycompany.com" ) );

    /*
     * An address literal does not match a certificate which has no iPAddress SAN at all
     */

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "127.0.0.1" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_MultiLabelWildcardsDoNotMatch )
{
    /*
     * A SECOND behaviour change from the rfc2818_verification -> host_name_verification switch,
     * separate from the IP address one and not restored here.
     *
     * The test certificate's primary SAN is *.*.mycompany.com - a wildcard in more than one label.
     * Asio's own RFC 2818 matcher accepted that shape; ::X509_check_host() does not, because
     * RFC 6125 section 6.4.3 permits a wildcard only as the complete leftmost label. So the SAN
     * matches NOTHING now - not a.b.mycompany.com, not x.mycompany.com - and only the certificate's
     * second SAN, localhost, is usable.
     *
     * This is asserted rather than fixed. Restoring multi-label wildcard matching would mean
     * loosening name verification below what RFC 6125 allows, which is the opposite of what the
     * switch to ::X509_check_host() bought. A deployment relying on a *.*.example.com certificate
     * needs a reissued certificate, and the breakage belongs in the release notes - see
     * notes/plans/issues/devenv7-breaking-changes-release-notes.md
     */

    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getDefaultServerCertificate() );

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "a.b.mycompany.com" ) );
    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, "x.mycompany.com" ) );
}

UTF_AUTO_TEST_CASE( TlsPeerVerification_DegenerateInputsAreRefused )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    UTF_REQUIRE( ! utest::tlspeer::matches( certificate, bl::str::empty() ) );

    UTF_REQUIRE(
        ! bl::crypto::TlsPeerVerification::certificateMatchesPeerName( nullptr, "127.0.0.1" )
        );
}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

namespace utest
{
    namespace tlspeer
    {
        /*
         * Drives the verify-callback form through a hand built X509_STORE_CTX, so the gate on
         * the chain verification result and the depth pass-through can be asserted without a
         * handshake. The two setters used exist from OpenSSL 1.1.0 on
         */

        inline bool verifyCallback(
            SAA_in          const bool                          preVerified,
            SAA_in          const bl::crypto::x509cert_ptr_t&   certificate,
            SAA_in          const std::string&                  peerName,
            SAA_in          const int                           depth
            )
        {
            ::X509_STORE_CTX* storeContext = ::X509_STORE_CTX_new();
            BL_CHK_CRYPTO_API_NM( storeContext );

            BL_SCOPE_EXIT( { ::X509_STORE_CTX_free( storeContext ); } );

            BL_CHK_CRYPTO_API_NM(
                ::X509_STORE_CTX_init(
                    storeContext,
                    nullptr             /* trust store */,
                    certificate.get(),
                    nullptr             /* untrusted chain */
                    )
                );

            ::X509_STORE_CTX_set_current_cert( storeContext, certificate.get() );
            ::X509_STORE_CTX_set_error_depth( storeContext, depth );

            bl::asio::ssl::verify_context verifyContext( storeContext );

            return bl::crypto::TlsPeerVerification::verifyPeerName(
                preVerified,
                peerName,
                verifyContext
                );
        }

    } // tlspeer

} // utest

UTF_AUTO_TEST_CASE( TlsPeerVerification_CallbackFormFailsClosedWithoutPreverification )
{
    const auto certificate =
        utest::tlspeer::loadCertificate( test::UtfCrypto::getIpAddressServerCertificate() );

    /*
     * The order of checks is the one asio::ssl::host_name_verification uses: a chain which
     * failed verification is refused before the name is looked at, so a matching name can
     * never turn an untrusted or expired chain into success. This matters because the
     * function is public and can be installed directly as the verify callback
     */

    UTF_REQUIRE( ! utest::tlspeer::verifyCallback( false, certificate, "127.0.0.1", 0 ) );
    UTF_REQUIRE( utest::tlspeer::verifyCallback( true, certificate, "127.0.0.1", 0 ) );
    UTF_REQUIRE( ! utest::tlspeer::verifyCallback( true, certificate, "192.168.1.1", 0 ) );

    /*
     * Above depth zero the certificate is not the peer's and is passed through - but only
     * once the chain check has passed
     */

    UTF_REQUIRE( utest::tlspeer::verifyCallback( true, certificate, "192.168.1.1", 1 ) );
    UTF_REQUIRE( ! utest::tlspeer::verifyCallback( false, certificate, "127.0.0.1", 1 ) );
}

#endif /* OPENSSL_VERSION_NUMBER >= 0x10100000L */

#endif /* __UTEST_TESTTLSPEERVERIFICATION_H_ */
