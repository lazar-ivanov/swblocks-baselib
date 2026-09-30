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

#ifndef __BL_CRYPTO_TLSNAMERULES_H_
#define __BL_CRYPTO_TLSNAMERULES_H_

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace bl
{
    namespace crypto
    {
        /*
         * The rules a name in a TLS client profile must satisfy - the cipher suite, group and
         * signature algorithm names of crypto::TlsClientProfile (notes/plans/http2-design.md 3.3
         * and 6.2)
         *
         * WHY A HEADER OF ITS OWN. Two layers judge the same untrusted names and must judge them by
         * ONE rule: the browser profile loader, httpclient/BrowserProfiles.h, which refuses a bad
         * profile when it is loaded and can say why, and the context builder in
         * crypto/CryptoBase.h, which is the last layer before OpenSSL. The loader must not need
         * OpenSSL to apply it and CryptoBase.h is an OpenSSL header, so the rule lives here, beside
         * TlsClientProfile.h whose names it judges and, like it, free of any OpenSSL header. It is
         * in crypto/ and not in httpclient/ because CryptoBase.h includes it, and nothing in
         * crypto/ may depend on httpclient/
         *
         * WHY A FAKE TEMPLATE. Once CryptoBase.h includes this header every translation unit which
         * uses OpenSSL compiles it, utf_baselib_io among them, and that module sits at 74.34 of its
         * 75 MB ceiling on win-x86-ccl16-debug. A member of a class template costs a translation
         * unit nothing unless the unit uses it, so every rule is a static member of TlsNameRulesT
         * and there is no table and no function at namespace scope
         *
         * This header carries no content: no suite, group or algorithm list. The name components
         * named below are OpenSSL's and IANA's spelling conventions, not an allowlist
         *
         * WHAT THESE RULES DO NOT DO. They stop the OpenSSL list LANGUAGE - an operator, a
         * separator, a control token - and the anonymous and NULL suites. They do not tell a single
         * suite or group from a well-formed word which is not one: an OpenSSL cipher alias such as
         * ALL or HIGH, or a group pseudo-name such as DEFAULT, passes them. The security of a
         * context does not rest on that - the level 2 pin, the builder's own exclusion tokens and
         * the negotiated-parameter floor all hold whatever a profile names
         */

        template
        <
            typename E = void
        >
        class TlsNameRulesT FINAL
        {
            BL_DECLARE_STATIC( TlsNameRulesT )

        private:

            static char toLowerAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

            static bool isComponentSeparator( SAA_in const char ch ) NOEXCEPT
            {
                return '-' == ch || '_' == ch;
            }

            /*
             * The position one past the end of the component beginning at 'begin', i.e. of the
             * next '-' or '_', or the end of the name
             */

            static std::size_t componentEnd(
                SAA_in          const std::string&                              name,
                SAA_in          const std::size_t                               begin
                ) NOEXCEPT
            {
                std::size_t end = begin;

                while( end < name.size() && ! isComponentSeparator( name[ end ] ) )
                {
                    ++end;
                }

                return end;
            }

            /*
             * Whether name[ begin, end ) is the ASCII word given, without regard to case
             */

            static bool isWord(
                SAA_in          const std::string&                              name,
                SAA_in          const std::size_t                               begin,
                SAA_in          const std::size_t                               end,
                SAA_in          const char*                                     word
                ) NOEXCEPT
            {
                const auto size = std::strlen( word );

                if( end - begin != size )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < size; ++i )
                {
                    if( toLowerAscii( name[ begin + i ] ) != toLowerAscii( word[ i ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*
             * Whether any component of the name - a run between '-' and '_' - is the word given,
             * without regard to case. Components and not substrings, so that 'NULL' is found in
             * NULL-SHA and in TLS_RSA_WITH_NULL_SHA but not in a name which merely contains the
             * letters
             */

            static bool hasComponent(
                SAA_in          const std::string&                              name,
                SAA_in          const char*                                     word
                ) NOEXCEPT
            {
                std::size_t begin = 0U;

                for( ;; )
                {
                    const auto end = componentEnd( name, begin );

                    if( isWord( name, begin, end, word ) )
                    {
                        return true;
                    }

                    if( end == name.size() )
                    {
                        return false;
                    }

                    begin = end + 1U;
                }
            }

            /*
             * "SHA" followed by one or more decimal digits, e.g. SHA256
             */

            static bool isHashComponent(
                SAA_in          const std::string&                              name,
                SAA_in          const std::size_t                               begin,
                SAA_in          const std::size_t                               end
                ) NOEXCEPT
            {
                if( end - begin < 4U || ! isWord( name, begin, begin + 3U, "SHA" ) )
                {
                    return false;
                }

                for( std::size_t i = begin + 3U; i < end; ++i )
                {
                    if( name[ i ] < '0' || name[ i ] > '9' )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*
             * A TLS 1.3 integrity-only suite of RFC 9150 - TLS_SHA256_SHA256, TLS_SHA384_SHA384 -
             * which authenticates and encrypts nothing. OpenSSL 3.5 carries both with a NULL cipher
             * (ssl/s3_lib.c, SSL_eNULL), and neither name has a NULL component, which is why they are
             * recognised by shape: exactly three components, TLS and then two hashes
             */

            static bool isIntegrityOnlySuiteName( SAA_in const std::string& name ) NOEXCEPT
            {
                const auto end1 = componentEnd( name, 0U );

                if( end1 == name.size() || ! isWord( name, 0U, end1, "TLS" ) )
                {
                    return false;
                }

                const auto begin2 = end1 + 1U;
                const auto end2 = componentEnd( name, begin2 );

                if( end2 == name.size() || ! isHashComponent( name, begin2, end2 ) )
                {
                    return false;
                }

                const auto begin3 = end2 + 1U;
                const auto end3 = componentEnd( name, begin3 );

                return end3 == name.size() && isHashComponent( name, begin3, end3 );
            }

        public:

            /**
             * @brief Whether a string is a plain name, i.e. safe to place in an OpenSSL list
             *
             * This is the rule crypto::CryptoBase::isCipherSuiteNameSafe has applied to cipher
             * suite names since S3.4, exactly - the context builder moves to this one, and the
             * loader applies it from the start, so that the two cannot drift
             *
             * A name is a non-empty string of ASCII letters, digits, '_' and '-', beginning with a
             * letter or a digit. An OpenSSL list is a small language rather than a list of names:
             * ':', ',' and ' ' separate tokens, a leading '!', '-' or '+' deletes or reorders, and
             * '@' introduces a control token - '@SECLEVEL=0' silently overrides
             * ::SSL_CTX_set_security_level (design 3.3). None of those can appear, and '-' is
             * refused only in first position, the one place OpenSSL reads it as an operator; every
             * TLS 1.2 suite name OpenSSL knows carries hyphens inside it
             *
             * The group and signature algorithm lists of OpenSSL 3.5 are the same kind of language
             * - ':' separates, a leading '*' marks a key share, '?' ignores an unknown name, '-'
             * removes and '/' separates a tuple; 'RSA+SHA256' spells an algorithm pair - and the
             * same rule refuses all of it. A key share is the profile's own mark on a group, never
             * a character in its name, and a signature algorithm is named by its TLS 1.3 name, e.g.
             * rsa_pss_rsae_sha256, which is what a code point in a captured ClientHello maps to
             */

            static bool isNameSafe( SAA_in const std::string& name ) NOEXCEPT
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
             * @brief Whether a cipher suite name names an anonymous suite - one which
             * authenticates nobody
             *
             * By its components, without regard to case: OpenSSL spells every anonymous suite
             * ADH-... or AECDH-... and IANA spells it ..._anon_...; and the OpenSSL aliases for the
             * class, ADH, AECDH and aNULL, are refused with it. Checked exhaustively against every
             * suite of OpenSSL 3.5.4 in both spellings, and against the linked OpenSSL's own
             * classification by utf_baselib_h2profiles
             */

            static bool isAnonymousCipherSuiteName( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    hasComponent( name, "ADH" ) ||
                    hasComponent( name, "AECDH" ) ||
                    hasComponent( name, "anon" ) ||
                    hasComponent( name, "aNULL" );
            }

            /**
             * @brief Whether a cipher suite name names a NULL suite - one which encrypts nothing
             *
             * By its components, without regard to case - NULL-SHA, ECDHE-RSA-NULL-SHA,
             * TLS_RSA_WITH_NULL_SHA256 and the OpenSSL aliases NULL and eNULL - and by shape for
             * the TLS 1.3 integrity-only suites, whose names carry no NULL at all. Checked as
             * isAnonymousCipherSuiteName( ) is
             */

            static bool isNullCipherSuiteName( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    hasComponent( name, "NULL" ) ||
                    hasComponent( name, "eNULL" ) ||
                    isIntegrityOnlySuiteName( name );
            }

            /**
             * @brief The whole rule for a cipher suite name, in either list: a plain name, and
             * neither an anonymous nor a NULL suite
             */

            static bool isCipherSuiteNameAllowed( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    isNameSafe( name ) &&
                    ! isAnonymousCipherSuiteName( name ) &&
                    ! isNullCipherSuiteName( name );
            }

            /**
             * @brief The whole rule for a group name - see isNameSafe( )
             */

            static bool isGroupNameAllowed( SAA_in const std::string& name ) NOEXCEPT
            {
                return isNameSafe( name );
            }

            /**
             * @brief The whole rule for a signature algorithm name - see isNameSafe( )
             */

            static bool isSignatureAlgorithmNameAllowed( SAA_in const std::string& name ) NOEXCEPT
            {
                return isNameSafe( name );
            }
        };

        typedef TlsNameRulesT<> TlsNameRules;

    } // crypto

} // bl

#endif /* __BL_CRYPTO_TLSNAMERULES_H_ */
