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

#ifndef __BL_CRYPTO_TLSPEERVERIFICATION_H_
#define __BL_CRYPTO_TLSPEERVERIFICATION_H_

#include <baselib/crypto/ErrorHandling.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/BaseIncludes.h>

#include <openssl/x509v3.h>

namespace bl
{
    namespace crypto
    {
        namespace detail
        {
            /**
             * @brief class TlsPeerVerification - matches a peer name against the certificate it
             * presented, for both DNS names and IP address literals
             *
             * WHY THIS EXISTS
             *
             * Boost 1.89 removed asio::ssl::rfc2818_verification and replaced it with
             * asio::ssl::host_name_verification, and baselib::core::detail::AsioSslCompat.h
             * typedefs the old name to the new one so existing code keeps compiling.
             *
             * The two are NOT the same implementation. host_name_verification delegates to
             * OpenSSL's ::X509_check_host(), which is a genuine improvement - it handles
             * subjectAltName correctly, ignores the common name when a SAN is present as RFC 6125
             * requires, and rejects embedded NUL bytes in names.
             *
             * But ::X509_check_host() does NOT match IP addresses; that needs ::X509_check_ip().
             * So a peer addressed by IP literal against a certificate carrying that IP in an
             * iPAddress SAN verified under the old implementation and stopped verifying under the
             * new one, silently, with nothing but a handshake failure to show for it. This class
             * restores that case without giving up any of the improvement.
             *
             * HOW THE DISPATCH WORKS
             *
             * ::X509_check_ip_asc() parses the supplied string before it compares anything, and
             * returns -2 when the string is not a valid address literal. That makes OpenSSL's own
             * parser the arbiter of whether a name is an address, so no separate literal test - and
             * no dependency on a particular Boost address parser - is needed here.
             *
             * Note that when the peer name IS an address literal the IP result is FINAL: a literal
             * which does not match the certificate's iPAddress entries must not then be matched
             * against its DNS names. RFC 6125 section 6.4 is explicit that an address literal is
             * not a domain name, and falling through would let a certificate for the DNS name
             * "10.0.0.1" authenticate the host at 10.0.0.1.
             */

            template
            <
                typename E = void
            >
            class TlsPeerVerificationT
            {
                BL_DECLARE_STATIC( TlsPeerVerificationT )

            public:

                enum : int
                {
                    /**
                     * @brief What ::X509_check_ip_asc() returns when the name is not an address
                     */

                    NotAnAddressLiteral = -2,
                };

                /**
                 * @brief Does 'peerName' identify the peer which presented 'certificate'?
                 *
                 * Dispatches to ::X509_check_ip_asc() for an address literal and to
                 * ::X509_check_host() for everything else. This is the whole matching decision and
                 * it is deliberately free of any Asio or handshake state so it can be tested
                 * directly against a certificate.
                 */

                static bool certificateMatchesPeerName(
                    SAA_in          ::X509*                                     certificate,
                    SAA_in          const std::string&                          peerName
                    ) NOEXCEPT
                {
                    if( nullptr == certificate || peerName.empty() )
                    {
                        return false;
                    }

                    const int ipResult = ::X509_check_ip_asc( certificate, peerName.c_str(), 0U );

                    if( NotAnAddressLiteral != ipResult )
                    {
                        /*
                         * The name is an address literal, so the address comparison is the answer -
                         * see the note on RFC 6125 above for why this does not fall through
                         */

                        return 1 == ipResult;
                    }

                    /*
                     * X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS refuses the 'f*.example.com' shapes,
                     * where the wildcard is only part of the leftmost label; OpenSSL already
                     * refuses a wildcard which is not in the leftmost label, or which has text on
                     * both sides. RFC 6125 section 6.4.3 tolerates the partial form, but the
                     * CA/Browser Forum baseline requirements define a wildcard as a whole leftmost
                     * label and public CAs do not issue anything else, so nothing legitimate is
                     * lost and the matching rule becomes the same one BoringSSL and Go apply
                     */

                    return 1 == ::X509_check_host(
                        certificate,
                        peerName.c_str(),
                        peerName.size(),
                        X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS    /* flags */,
                        nullptr                                 /* peername */
                        );
                }

                /**
                 * @brief The verify-callback form, for use from an Asio verify callback
                 *
                 * Matches the semantics of asio::ssl::host_name_verification, including its
                 * order of checks: a certificate which failed the chain verification which
                 * produced 'preVerified' is refused before its name is even looked at, so the
                 * name match can never turn an untrusted or expired chain into success. Then,
                 * because only the certificate at the end of the chain carries the peer's
                 * identity, anything above depth zero is passed through.
                 *
                 * This form is safe to install directly as the verify callback (e.g. through
                 * the rfc2818 verify callback hook in AsioSslStreamWrapper.h); a caller which
                 * gates on 'preVerified' itself may still pass it, which changes nothing.
                 */

                static bool verifyPeerName(
                    SAA_in          const bool                                  preVerified,
                    SAA_in          const std::string&                          peerName,
                    SAA_inout       asio::ssl::verify_context&                  verifyContext
                    ) NOEXCEPT
                {
                    if( ! preVerified )
                    {
                        return false;
                    }

                    if( ::X509_STORE_CTX_get_error_depth( verifyContext.native_handle() ) > 0 )
                    {
                        return true;
                    }

                    return certificateMatchesPeerName(
                        ::X509_STORE_CTX_get_current_cert( verifyContext.native_handle() ),
                        peerName
                        );
                }
            };

            typedef TlsPeerVerificationT<> TlsPeerVerification;

        } // detail

        using detail::TlsPeerVerification;

    } // crypto

} // bl

#endif /* __BL_CRYPTO_TLSPEERVERIFICATION_H_ */
