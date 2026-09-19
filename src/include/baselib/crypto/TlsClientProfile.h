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

#ifndef __BL_CRYPTO_TLSCLIENTPROFILE_H_
#define __BL_CRYPTO_TLSCLIENTPROFILE_H_

#include <baselib/core/BaseIncludes.h>

#include <string>
#include <vector>

namespace bl
{
    namespace crypto
    {
        /*
         * The shape of a TLS client handshake, as data
         *
         * This is the "TLS layer" of a browser profile (notes/plans/http2-design.md 3.3 and 6.1),
         * but it is not HTTP-specific and carries no HTTP type. It is also deliberately free of
         * any OpenSSL header: it is the input to the context builder, not part of it, so a caller
         * may name a profile without OpenSSL being in the translation unit
         *
         * Everything here is a *name*, never a cipher string. Profiles load from JSON and are
         * therefore untrusted input, and an OpenSSL cipher string can carry @SECLEVEL=0 which
         * silently overrides SSL_CTX_set_security_level (3.3). The loader which fills these
         * vectors validates each name against an allowlist; this header only fixes the shape
         *
         * Content - the actual suite, group and signature-algorithm names of a given browser - is
         * captured ground truth and belongs to the profile data, not to this header (6.7)
         */

        /**
         * @brief A named TLS group (supported curve), and whether a key share is offered for it
         *
         * A browser offers key shares for only the first group or two of the list it supports, so
         * the mark is per group rather than a count
         */

        struct TlsGroup
        {
            std::string                                                         name;
            cpp::ScalarTypeIniter< bool >                                       keyShare;
        };

        /**
         * @brief The shaping knobs of a TLS client handshake
         */

        struct TlsClientProfile
        {
            /*
             * Ordered, most preferred first. The two suite lists are separate because TLS 1.3
             * suites are configured through a different OpenSSL entry point than TLS 1.2 ones
             */

            std::vector< std::string >                                          cipherSuitesTls12;
            std::vector< std::string >                                          cipherSuitesTls13;

            std::vector< TlsGroup >                                             groups;
            std::vector< std::string >                                          signatureAlgorithms;

            /*
             * The ALPN protocol ids offered, in order - e.g. h2 before http/1.1 (3.3)
             */

            std::vector< std::string >                                          alpnProtocols;

            /*
             * The ClientHello extension switches of 3.3. Session tickets are off unless the
             * profile asks for them, which is what clears SSL_OP_NO_TICKET for that profile only
             */

            cpp::ScalarTypeIniter< bool >                                       sessionTicket;
            cpp::ScalarTypeIniter< bool >                                       statusRequest;
            cpp::ScalarTypeIniter< bool >                                       signedCertificateTimestamp;
            cpp::ScalarTypeIniter< bool >                                       padding;
        };

    } // crypto

} // bl

#endif /* __BL_CRYPTO_TLSCLIENTPROFILE_H_ */
