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

#ifndef __BL_URI_H_
#define __BL_URI_H_

#include <baselib/core/OS.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <string>

namespace bl
{
    namespace net
    {
        /**
         * @brief class UriT - an RFC 3986 URI reference
         *
         * Parsing, reference resolution ( RFC 3986 section 5 ) and syntax-based normalization
         * ( RFC 3986 section 6.2.2 ). This complements, and does not replace,
         * str::uriEncode( ... ) / str::uriDecode( ... ) in StringUtils.h, which encode and
         * decode individual strings and know nothing about the structure of a URI.
         *
         * The parser is deliberately strict:
         *
         *  - control characters, whitespace and backslashes are rejected rather than stripped
         *    or re-interpreted, because the leniency the WHATWG URL specification requires of
         *    browsers is a well known source of parser differentials between the component
         *    which validates a URL and the component which fetches it
         *
         *  - there is no IDNA support, so a non-ASCII byte anywhere in the reference is an
         *    error and callers pass A-labels ( "xn--..." ) for an international host name
         *
         *  - reference resolution implements the strict algorithm of section 5.2.2, so a
         *    reference which carries the base scheme is NOT merged with the base - the
         *    backward compatibility rule which turns "http:g" into "http://a/b/c/g" is not
         *    applied
         *
         * Normalization applied by parse( ... ):
         *
         *  - the scheme and the host are lower-cased ( section 6.2.2.1 )
         *  - a percent-encoded octet which stands for an unreserved character is decoded, and
         *    the hexadecimal digits of the octets which remain encoded are upper-cased
         *    ( sections 6.2.2.1 and 6.2.2.2 )
         *  - dot segments are removed from the path, but only when the reference has a scheme
         *    or an authority ( section 6.2.2.3 ); in a relative reference the dot segments are
         *    meaningful and section 5.2.4 removes them at resolution time instead
         *  - an empty port normalizes to no port at all, and a port with leading zeros to its
         *    numeric value
         *
         * Deliberately NOT implemented: IDNA / punycode conversion, the WHATWG leniency rules,
         * the non-strict section 5.2.2 rule described above, RFC 5952 canonicalization of an
         * IPv6 literal ( zero compression is left exactly as written ), RFC 6874 zone
         * identifiers, and canonicalization of an IPv4 address ( RFC 3986 accepts an address
         * such as "010.1.1.1" as a reg-name, which is what this parser does with it ).
         *
         * The scheme-based normalization of section 6.2.3 is not applied to the parsed value
         * either - an empty path stays empty rather than becoming "/", and a port which spells
         * out the default for its scheme is kept. Both are folded in where they matter instead:
         * pathAndQuery( ... ) emits "/" for an empty path and origin( ... ) always renders the
         * effective port, so the two forms produce the same request target and the same
         * connection pool key without toString( ... ) rewriting what the caller passed in
         */

        template
        <
            typename E = void
        >
        class UriT FINAL
        {
        public:

            typedef UriT< E >                                       this_type;

        private:

            typedef bool ( *char_predicate_t )( const char );

            std::string                                             m_scheme;
            std::string                                             m_userInfo;
            std::string                                             m_host;
            std::string                                             m_path;
            std::string                                             m_query;
            std::string                                             m_fragment;

            cpp::ScalarTypeIniter< os::port_t >                     m_port;

            cpp::ScalarTypeIniter< bool >                           m_hasAuthority;
            cpp::ScalarTypeIniter< bool >                           m_hasUserInfo;
            cpp::ScalarTypeIniter< bool >                           m_hasPort;
            cpp::ScalarTypeIniter< bool >                           m_hasQuery;
            cpp::ScalarTypeIniter< bool >                           m_hasFragment;
            cpp::ScalarTypeIniter< bool >                           m_isIpLiteral;

            static const std::string                                g_schemeHttp;
            static const std::string                                g_schemeHttps;

            /*************************************************************************
             * The RFC 3986 character classes ( section 2 )
             */

            static bool isAlpha( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' );
            }

            static bool isDigit( SAA_in const char ch ) NOEXCEPT
            {
                return ch >= '0' && ch <= '9';
            }

            static bool isHexDigit( SAA_in const char ch ) NOEXCEPT
            {
                return isDigit( ch ) || ( ch >= 'a' && ch <= 'f' ) || ( ch >= 'A' && ch <= 'F' );
            }

            /*
             * unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
             */

            static bool isUnreserved( SAA_in const char ch ) NOEXCEPT
            {
                return
                    isAlpha( ch ) || isDigit( ch ) ||
                    '-' == ch || '.' == ch || '_' == ch || '~' == ch;
            }

            /*
             * sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
             */

            static bool isSubDelim( SAA_in const char ch ) NOEXCEPT
            {
                return
                    '!' == ch || '$' == ch || '&' == ch || '\'' == ch ||
                    '(' == ch || ')' == ch || '*' == ch || '+'  == ch ||
                    ',' == ch || ';' == ch || '=' == ch;
            }

            /*
             * userinfo = *( unreserved / pct-encoded / sub-delims / ":" )
             */

            static bool isUserInfoChar( SAA_in const char ch ) NOEXCEPT
            {
                return isUnreserved( ch ) || isSubDelim( ch ) || ':' == ch;
            }

            /*
             * reg-name = *( unreserved / pct-encoded / sub-delims )
             */

            static bool isRegNameChar( SAA_in const char ch ) NOEXCEPT
            {
                return isUnreserved( ch ) || isSubDelim( ch );
            }

            /*
             * path = *( pchar / "/" ), where pchar = unreserved / pct-encoded / sub-delims /
             * ":" / "@"; the placement of the segment separators is a structural rule and is
             * checked by the parser rather than here
             */

            static bool isPathChar( SAA_in const char ch ) NOEXCEPT
            {
                return isUnreserved( ch ) || isSubDelim( ch ) || ':' == ch || '@' == ch || '/' == ch;
            }

            /*
             * query = fragment = *( pchar / "/" / "?" )
             */

            static bool isQueryOrFragmentChar( SAA_in const char ch ) NOEXCEPT
            {
                return isPathChar( ch ) || '?' == ch;
            }

            static unsigned hexValue( SAA_in const char ch ) NOEXCEPT
            {
                if( ch >= '0' && ch <= '9' )
                {
                    return static_cast< unsigned >( ch - '0' );
                }

                if( ch >= 'a' && ch <= 'f' )
                {
                    return static_cast< unsigned >( ch - 'a' ) + 10U;
                }

                return static_cast< unsigned >( ch - 'A' ) + 10U;
            }

            static char hexDigit( SAA_in const unsigned value ) NOEXCEPT
            {
                return static_cast< char >( value < 10U ? ( '0' + value ) : ( 'A' + ( value - 10U ) ) );
            }

            static char toLowerAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

            /*************************************************************************
             * Diagnostics
             *
             * Note that the offending text is deliberately never echoed into the message -
             * the userinfo of a URI can carry credentials and these messages end up in logs.
             * The component and the reason are enough to locate the defect
             */

            SAA_noreturn
            static void throwInvalidUri(
                SAA_in      const char*                             component,
                SAA_in      const char*                             reason
                )
            {
                BL_THROW(
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "Invalid URI reference - the "
                        << component
                        << " "
                        << reason
                    );
            }

            /*************************************************************************
             * Validation and normalization of a single component
             */

            /**
             * @brief Rejects everything which RFC 3986 does not allow anywhere in a URI, with
             * a message which names the class of the offending byte
             *
             * The per-component grammar below would reject all of these anyway; this exists so
             * that the four cases which matter most in practice - a control character, a raw
             * space, a backslash mistaken for a path separator and a non-ASCII byte in a host
             * name - are reported as themselves rather than as a generic grammar violation
             */

            static void chkStrictCharacters( SAA_in const std::string& text )
            {
                for( std::size_t pos = 0U; pos < text.size(); ++pos )
                {
                    const auto ch = static_cast< unsigned char >( text[ pos ] );

                    if( ch >= 0x80U )
                    {
                        throwInvalidUri(
                            "reference",
                            "contains a non-ASCII byte; RFC 3986 URIs are ASCII only and IDNA "
                            "is not supported, so an international host name must be passed as "
                            "its A-label form"
                            );
                    }

                    if( ch <= 0x1FU || 0x7FU == ch )
                    {
                        throwInvalidUri( "reference", "contains a control character" );
                    }

                    if( ' ' == ch )
                    {
                        throwInvalidUri(
                            "reference",
                            "contains a space, which must be percent-encoded as %20"
                            );
                    }

                    if( '\\' == ch )
                    {
                        throwInvalidUri(
                            "reference",
                            "contains a backslash, which RFC 3986 does not accept as a path separator"
                            );
                    }
                }
            }

            /**
             * @brief Checks a component against its character set and applies the percent-encoding
             * normalization of RFC 3986 sections 6.2.2.1 and 6.2.2.2
             */

            static std::string normalizeComponent(
                SAA_in      const std::string&                      value,
                SAA_in      const char_predicate_t                  isAllowed,
                SAA_in      const char*                             component,
                SAA_in      const bool                              toLowerCase
                )
            {
                std::string result;
                result.reserve( value.size() );

                std::size_t pos = 0U;

                while( pos < value.size() )
                {
                    const char ch = value[ pos ];

                    if( '%' != ch )
                    {
                        if( ! isAllowed( ch ) )
                        {
                            throwInvalidUri( component, "contains a character which is not allowed in it" );
                        }

                        result += toLowerCase ? toLowerAscii( ch ) : ch;

                        ++pos;

                        continue;
                    }

                    if(
                        pos + 2U >= value.size() ||
                        ! isHexDigit( value[ pos + 1U ] ) ||
                        ! isHexDigit( value[ pos + 2U ] )
                        )
                    {
                        throwInvalidUri( component, "contains a malformed percent-encoded octet" );
                    }

                    const auto octet =
                        ( hexValue( value[ pos + 1U ] ) << 4 ) | hexValue( value[ pos + 2U ] );

                    const auto decoded = static_cast< char >( octet );

                    if( isUnreserved( decoded ) )
                    {
                        /*
                         * Section 2.3 - a percent-encoded unreserved character is equivalent to
                         * the character itself, so decoding it is a normalization and not a
                         * change of meaning. Note that this is what makes "%2E%2E" a dot segment
                         * for the purposes of the removal below, which is also what the WHATWG
                         * URL specification requires of a browser
                         */

                        result += toLowerCase ? toLowerAscii( decoded ) : decoded;
                    }
                    else
                    {
                        result += '%';
                        result += hexDigit( ( octet >> 4 ) & 0x0FU );
                        result += hexDigit( octet & 0x0FU );
                    }

                    pos += 3U;
                }

                return result;
            }

            /*
             * scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
             */

            static bool isValidScheme( SAA_in const std::string& scheme ) NOEXCEPT
            {
                if( scheme.empty() || ! isAlpha( scheme[ 0 ] ) )
                {
                    return false;
                }

                for( std::size_t pos = 1U; pos < scheme.size(); ++pos )
                {
                    const char ch = scheme[ pos ];

                    if( ! isAlpha( ch ) && ! isDigit( ch ) && '+' != ch && '-' != ch && '.' != ch )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*************************************************************************
             * The IP literal grammar ( RFC 3986 section 3.2.2 )
             */

            /*
             * dec-octet = DIGIT / %x31-39 DIGIT / "1" 2DIGIT / "2" %x30-34 DIGIT / "25" %x30-35
             *
             * i.e. one to three digits, no leading zero, and at most 255
             */

            static bool isDecOctet( SAA_in const std::string& token ) NOEXCEPT
            {
                if( token.empty() || token.size() > 3U )
                {
                    return false;
                }

                if( token.size() > 1U && '0' == token[ 0 ] )
                {
                    return false;
                }

                unsigned value = 0U;

                for( std::size_t pos = 0U; pos < token.size(); ++pos )
                {
                    if( ! isDigit( token[ pos ] ) )
                    {
                        return false;
                    }

                    value = value * 10U + static_cast< unsigned >( token[ pos ] - '0' );
                }

                return value <= 255U;
            }

            static bool isIpv4Address( SAA_in const std::string& token )
            {
                std::size_t count = 0U;
                std::size_t pos = 0U;

                for( ;; )
                {
                    const auto next = token.find( '.', pos );

                    const auto octet = ( std::string::npos == next )
                        ? token.substr( pos )
                        : token.substr( pos, next - pos );

                    if( ! isDecOctet( octet ) )
                    {
                        return false;
                    }

                    ++count;

                    if( std::string::npos == next )
                    {
                        break;
                    }

                    pos = next + 1U;
                }

                return 4U == count;
            }

            /*
             * h16 = 1*4HEXDIG
             */

            static bool isIpv6Group( SAA_in const std::string& token ) NOEXCEPT
            {
                if( token.empty() || token.size() > 4U )
                {
                    return false;
                }

                for( std::size_t pos = 0U; pos < token.size(); ++pos )
                {
                    if( ! isHexDigit( token[ pos ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            /**
             * @brief Counts the 16 bit groups in one side of an IPv6 address, where a trailing
             * dotted-quad counts as the two groups it stands for
             *
             * @return npos when the text is not a valid sequence of groups
             */

            static std::size_t countIpv6Groups(
                SAA_in      const std::string&                      piece,
                SAA_in      const bool                              allowIpv4Tail
                )
            {
                if( piece.empty() )
                {
                    return 0U;
                }

                std::size_t count = 0U;
                std::size_t pos = 0U;

                for( ;; )
                {
                    const auto next = piece.find( ':', pos );

                    const auto token = ( std::string::npos == next )
                        ? piece.substr( pos )
                        : piece.substr( pos, next - pos );

                    if(
                        std::string::npos == next &&
                        allowIpv4Tail &&
                        std::string::npos != token.find( '.' )
                        )
                    {
                        if( ! isIpv4Address( token ) )
                        {
                            return std::string::npos;
                        }

                        count += 2U;
                    }
                    else
                    {
                        if( ! isIpv6Group( token ) )
                        {
                            return std::string::npos;
                        }

                        ++count;
                    }

                    if( std::string::npos == next )
                    {
                        break;
                    }

                    pos = next + 1U;
                }

                return count;
            }

            /**
             * @brief The IPv6address rule, expressed as the equivalent constraint: at most one
             * "::", eight groups in total, and an optional trailing dotted-quad standing for
             * the last two of them
             */

            static void chkIpv6Address( SAA_in const std::string& address )
            {
                const auto doubleColon = address.find( "::" );

                if( std::string::npos == doubleColon )
                {
                    if( 8U != countIpv6Groups( address, true /* allowIpv4Tail */ ) )
                    {
                        throwInvalidUri( "host", "is not a valid IPv6 address" );
                    }

                    return;
                }

                if( std::string::npos != address.find( "::", doubleColon + 1U ) )
                {
                    throwInvalidUri( "host", "is not a valid IPv6 address - it has more than one '::'" );
                }

                /*
                 * The head is never the end of the address, so a dotted-quad is only accepted
                 * in the tail
                 */

                const auto head = countIpv6Groups(
                    address.substr( 0, doubleColon ),
                    false /* allowIpv4Tail */
                    );

                const auto tail = countIpv6Groups(
                    address.substr( doubleColon + 2U ),
                    true /* allowIpv4Tail */
                    );

                if( std::string::npos == head || std::string::npos == tail || ( head + tail ) > 7U )
                {
                    throwInvalidUri( "host", "is not a valid IPv6 address" );
                }
            }

            /*
             * IPvFuture = "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
             */

            static void chkIpvFuture( SAA_in const std::string& literal )
            {
                std::size_t pos = 1U;

                while( pos < literal.size() && isHexDigit( literal[ pos ] ) )
                {
                    ++pos;
                }

                if( 1U == pos || pos >= literal.size() || '.' != literal[ pos ] )
                {
                    throwInvalidUri( "host", "is not a valid IPvFuture literal" );
                }

                ++pos;

                if( pos >= literal.size() )
                {
                    throwInvalidUri( "host", "is not a valid IPvFuture literal" );
                }

                for( ; pos < literal.size(); ++pos )
                {
                    const char ch = literal[ pos ];

                    if( ! isUnreserved( ch ) && ! isSubDelim( ch ) && ':' != ch )
                    {
                        throwInvalidUri( "host", "is not a valid IPvFuture literal" );
                    }
                }
            }

            /*
             * IP-literal = "[" ( IPv6address / IPvFuture ) "]"
             */

            static void chkIpLiteral( SAA_in const std::string& literal )
            {
                if( literal.empty() )
                {
                    throwInvalidUri( "host", "is an empty IP literal" );
                }

                /*
                 * 'v' is not a hexadecimal digit, so it can only introduce an IPvFuture
                 */

                if( 'v' == literal[ 0 ] || 'V' == literal[ 0 ] )
                {
                    chkIpvFuture( literal );
                }
                else
                {
                    chkIpv6Address( literal );
                }
            }

            /*************************************************************************
             * Parsing
             */

            void parsePort( SAA_in const std::string& port )
            {
                /*
                 * port = *DIGIT - an empty port is grammatical, and section 6.2.3 makes it
                 * equivalent to no port at all, so it normalizes away here
                 */

                if( port.empty() )
                {
                    return;
                }

                std::uint32_t value = 0U;

                for( std::size_t pos = 0U; pos < port.size(); ++pos )
                {
                    if( ! isDigit( port[ pos ] ) )
                    {
                        throwInvalidUri( "port", "contains a character which is not a digit" );
                    }

                    value = value * 10U + static_cast< unsigned >( port[ pos ] - '0' );

                    if( value > 65535U )
                    {
                        throwInvalidUri( "port", "is greater than 65535" );
                    }
                }

                m_port = static_cast< os::port_t >( value );
                m_hasPort = true;
            }

            /*
             * authority = [ userinfo "@" ] host [ ":" port ]
             */

            void parseAuthority( SAA_in const std::string& authority )
            {
                std::string hostAndPort( authority );

                /*
                 * "@" is not in the userinfo character set, so the first one is the separator
                 * and a second one is left in the host, where the reg-name rule rejects it
                 */

                const auto atPos = hostAndPort.find( '@' );

                if( std::string::npos != atPos )
                {
                    m_userInfo = normalizeComponent(
                        hostAndPort.substr( 0, atPos ),
                        &isUserInfoChar,
                        "userinfo",
                        false /* toLowerCase */
                        );

                    m_hasUserInfo = true;

                    hostAndPort.erase( 0, atPos + 1U );
                }

                std::string port;
                bool hasPortSeparator = false;

                if( ! hostAndPort.empty() && '[' == hostAndPort[ 0 ] )
                {
                    const auto closePos = hostAndPort.find( ']' );

                    if( std::string::npos == closePos )
                    {
                        throwInvalidUri( "host", "starts an IP literal which is never closed" );
                    }

                    const auto literal = hostAndPort.substr( 1U, closePos - 1U );

                    chkIpLiteral( literal );

                    m_host = str::to_lower_copy( literal );
                    m_isIpLiteral = true;

                    const auto remainder = hostAndPort.substr( closePos + 1U );

                    if( ! remainder.empty() )
                    {
                        if( ':' != remainder[ 0 ] )
                        {
                            throwInvalidUri( "authority", "has text between the IP literal and the port" );
                        }

                        port = remainder.substr( 1U );
                        hasPortSeparator = true;
                    }
                }
                else
                {
                    /*
                     * ":" is not in the reg-name character set, so the first one starts the
                     * port and a second one is left in the port, where the digit rule rejects it
                     */

                    const auto colonPos = hostAndPort.find( ':' );

                    if( std::string::npos != colonPos )
                    {
                        port = hostAndPort.substr( colonPos + 1U );
                        hasPortSeparator = true;

                        hostAndPort.erase( colonPos );
                    }

                    m_host = normalizeComponent(
                        hostAndPort,
                        &isRegNameChar,
                        "host",
                        true /* toLowerCase */
                        );
                }

                if( hasPortSeparator )
                {
                    parsePort( port );
                }
            }

            void parseImpl( SAA_in const std::string& text )
            {
                chkStrictCharacters( text );

                std::string rest( text );

                /*
                 * RFC 3986 appendix B - the fragment is everything after the first "#", and the
                 * query everything after the first "?" in what is left
                 */

                const auto hashPos = rest.find( '#' );

                if( std::string::npos != hashPos )
                {
                    m_fragment = normalizeComponent(
                        rest.substr( hashPos + 1U ),
                        &isQueryOrFragmentChar,
                        "fragment",
                        false /* toLowerCase */
                        );

                    m_hasFragment = true;

                    rest.erase( hashPos );
                }

                const auto questionPos = rest.find( '?' );

                if( std::string::npos != questionPos )
                {
                    m_query = normalizeComponent(
                        rest.substr( questionPos + 1U ),
                        &isQueryOrFragmentChar,
                        "query",
                        false /* toLowerCase */
                        );

                    m_hasQuery = true;

                    rest.erase( questionPos );
                }

                /*
                 * Only a ":" which comes before the first "/" can introduce a scheme; when it
                 * does not spell one the reference is a relative one, and then its first path
                 * segment may not contain a ":" at all ( the path-noscheme rule )
                 */

                const auto colonPos = rest.find( ':' );
                const auto slashPos = rest.find( '/' );

                if(
                    std::string::npos != colonPos &&
                    ( std::string::npos == slashPos || colonPos < slashPos )
                    )
                {
                    const auto candidate = rest.substr( 0, colonPos );

                    if( ! isValidScheme( candidate ) )
                    {
                        throwInvalidUri(
                            "scheme",
                            "is not well formed, and a relative reference may not carry a ':' "
                            "in its first path segment"
                            );
                    }

                    m_scheme = str::to_lower_copy( candidate );

                    rest.erase( 0, colonPos + 1U );
                }

                if( rest.size() >= 2U && '/' == rest[ 0 ] && '/' == rest[ 1 ] )
                {
                    m_hasAuthority = true;

                    const auto authorityEnd = rest.find( '/', 2U );

                    if( std::string::npos == authorityEnd )
                    {
                        parseAuthority( rest.substr( 2U ) );

                        rest.clear();
                    }
                    else
                    {
                        parseAuthority( rest.substr( 2U, authorityEnd - 2U ) );

                        rest.erase( 0, authorityEnd );
                    }
                }

                m_path = normalizeComponent( rest, &isPathChar, "path", false /* toLowerCase */ );

                /*
                 * Section 6.2.2.3 - the complete path segments "." and ".." are only meaningful
                 * inside a relative reference, so they are removed here from everything else and
                 * left alone in a relative reference for section 5.2.4 to deal with
                 */

                if( ! m_scheme.empty() || m_hasAuthority )
                {
                    m_path = removeDotSegments( m_path );
                }
            }

            /*************************************************************************
             * Reference resolution ( RFC 3986 section 5 )
             */

            /**
             * @brief remove_dot_segments, transcribed from RFC 3986 section 5.2.4
             */

            static std::string removeDotSegments( SAA_in const std::string& path )
            {
                std::string input( path );
                std::string output;

                while( ! input.empty() )
                {
                    if( str::starts_with( input, "../" ) )
                    {
                        input.erase( 0, 3U );
                    }
                    else if( str::starts_with( input, "./" ) )
                    {
                        input.erase( 0, 2U );
                    }
                    else if( str::starts_with( input, "/./" ) )
                    {
                        input.erase( 0, 2U );
                    }
                    else if( "/." == input )
                    {
                        input = "/";
                    }
                    else if( str::starts_with( input, "/../" ) )
                    {
                        input.erase( 0, 3U );

                        removeLastSegment( output );
                    }
                    else if( "/.." == input )
                    {
                        input = "/";

                        removeLastSegment( output );
                    }
                    else if( "." == input || ".." == input )
                    {
                        input.clear();
                    }
                    else
                    {
                        const auto next = input.find( '/', ( '/' == input[ 0 ] ) ? 1U : 0U );

                        if( std::string::npos == next )
                        {
                            output += input;

                            input.clear();
                        }
                        else
                        {
                            output.append( input, 0, next );

                            input.erase( 0, next );
                        }
                    }
                }

                return output;
            }

            static void removeLastSegment( SAA_inout std::string& output )
            {
                const auto pos = output.rfind( '/' );

                if( std::string::npos == pos )
                {
                    output.clear();
                }
                else
                {
                    output.erase( pos );
                }
            }

            /**
             * @brief merge, from RFC 3986 section 5.2.3
             */

            static std::string mergePaths(
                SAA_in      const this_type&                        base,
                SAA_in      const std::string&                      refPath
                )
            {
                if( base.m_hasAuthority && base.m_path.empty() )
                {
                    return "/" + refPath;
                }

                const auto pos = base.m_path.rfind( '/' );

                if( std::string::npos == pos )
                {
                    return refPath;
                }

                return base.m_path.substr( 0, pos + 1U ) + refPath;
            }

            void copyAuthorityFrom( SAA_in const this_type& other )
            {
                m_hasAuthority = other.m_hasAuthority;
                m_userInfo = other.m_userInfo;
                m_hasUserInfo = other.m_hasUserInfo;
                m_host = other.m_host;
                m_isIpLiteral = other.m_isIpLiteral;
                m_port = other.m_port;
                m_hasPort = other.m_hasPort;
            }

            void copyQueryFrom( SAA_in const this_type& other )
            {
                m_query = other.m_query;
                m_hasQuery = other.m_hasQuery;
            }

            std::string hostForAuthority() const
            {
                return m_isIpLiteral ? ( "[" + m_host + "]" ) : m_host;
            }

        public:

            /**
             * @brief Parses a URI reference - an absolute URI or a relative reference
             *
             * @throw InvalidDataFormatException when the text is not a well formed RFC 3986
             * URI reference
             */

            static this_type parse( SAA_in const std::string& text )
            {
                this_type result;

                result.parseImpl( text );

                return result;
            }

            /**
             * @brief The non-throwing form, for the callers which treat a malformed reference
             * as data rather than as a defect - e.g. a redirect whose Location header cannot
             * be parsed and which is therefore not followed
             */

            static bool tryParse(
                SAA_in      const std::string&                      text,
                SAA_inout   this_type&                              result
                )
            {
                try
                {
                    result = parse( text );

                    return true;
                }
                catch( InvalidDataFormatException& )
                {
                    return false;
                }
            }

            const std::string& scheme() const NOEXCEPT
            {
                return m_scheme;
            }

            const std::string& userInfo() const NOEXCEPT
            {
                return m_userInfo;
            }

            /**
             * @brief The host, with the brackets of an IP literal removed - i.e. the form which
             * a resolver or asio::ip::address::from_string( ... ) expects
             */

            const std::string& host() const NOEXCEPT
            {
                return m_host;
            }

            /**
             * @brief The port as written, or zero when the reference does not carry one
             */

            os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            const std::string& path() const NOEXCEPT
            {
                return m_path;
            }

            const std::string& query() const NOEXCEPT
            {
                return m_query;
            }

            const std::string& fragment() const NOEXCEPT
            {
                return m_fragment;
            }

            bool hasScheme() const NOEXCEPT
            {
                return ! m_scheme.empty();
            }

            bool hasAuthority() const NOEXCEPT
            {
                return m_hasAuthority;
            }

            bool hasUserInfo() const NOEXCEPT
            {
                return m_hasUserInfo;
            }

            bool hasPort() const NOEXCEPT
            {
                return m_hasPort;
            }

            bool hasQuery() const NOEXCEPT
            {
                return m_hasQuery;
            }

            bool hasFragment() const NOEXCEPT
            {
                return m_hasFragment;
            }

            /**
             * @brief True when the reference is an absolute URI rather than a relative one -
             * i.e. when it carries a scheme; note that this says nothing about the fragment,
             * unlike the RFC's "absolute-URI" rule
             */

            bool isAbsolute() const NOEXCEPT
            {
                return hasScheme();
            }

            /**
             * @brief True when the host is an IP literal, i.e. when it needs its brackets back
             * to appear in an authority
             */

            bool isIpLiteral() const NOEXCEPT
            {
                return m_isIpLiteral;
            }

            /**
             * @brief The port a scheme defaults to, or zero when it is not one this library
             * speaks
             */

            static os::port_t defaultPort( SAA_in const std::string& scheme ) NOEXCEPT
            {
                if( scheme == g_schemeHttp )
                {
                    return 80U;
                }

                if( scheme == g_schemeHttps )
                {
                    return 443U;
                }

                return 0U;
            }

            /**
             * @brief The port to connect to - the one the reference carries, otherwise the
             * default for its scheme, otherwise zero
             */

            os::port_t effectivePort() const NOEXCEPT
            {
                return m_hasPort ? static_cast< os::port_t >( m_port ) : defaultPort( m_scheme );
            }

            /**
             * @brief host[":" port] - the value of the HTTP/2 ":authority" pseudo-header and of
             * the HTTP/1.1 Host header
             *
             * The userinfo is deliberately excluded; RFC 9113 section 8.3.1 forbids it in
             * ":authority", and hasUserInfo() is how a caller detects a URI which carries one
             */

            std::string authority() const
            {
                std::string result = hostForAuthority();

                if( m_hasPort )
                {
                    result += ':';
                    result += std::to_string( static_cast< unsigned >( m_port ) );
                }

                return result;
            }

            /**
             * @brief scheme "://" host [":" effective port] - the canonical key for a
             * connection pool, for cookie scoping and for a cross-origin check
             *
             * The effective port is always rendered when it is known, so that a reference which
             * spells out the default port for its scheme and one which leaves it out produce
             * the same origin
             */

            std::string origin() const
            {
                std::string result = m_scheme;

                result += "://";
                result += hostForAuthority();

                const auto port = effectivePort();

                if( 0U != port )
                {
                    result += ':';
                    result += std::to_string( static_cast< unsigned >( port ) );
                }

                return result;
            }

            /**
             * @brief The origin-form request target - the value of the HTTP/2 ":path"
             * pseudo-header and of the HTTP/1.1 request line
             *
             * An empty path becomes "/", as RFC 9113 section 8.3.1 requires; the fragment is
             * never part of a request target
             */

            std::string pathAndQuery() const
            {
                std::string result = m_path.empty() ? std::string( "/" ) : m_path;

                if( m_hasQuery )
                {
                    result += '?';
                    result += m_query;
                }

                return result;
            }

            /**
             * @brief Recomposes the reference, per RFC 3986 section 5.3
             */

            std::string toString() const
            {
                std::string result;

                if( ! m_scheme.empty() )
                {
                    result += m_scheme;
                    result += ':';
                }

                if( m_hasAuthority )
                {
                    result += "//";

                    if( m_hasUserInfo )
                    {
                        result += m_userInfo;
                        result += '@';
                    }

                    result += authority();
                }

                result += m_path;

                if( m_hasQuery )
                {
                    result += '?';
                    result += m_query;
                }

                if( m_hasFragment )
                {
                    result += '#';
                    result += m_fragment;
                }

                return result;
            }

            /**
             * @brief Resolves a reference against a base URI, per the strict algorithm of
             * RFC 3986 section 5.2.2
             */

            static this_type resolve(
                SAA_in      const this_type&                        base,
                SAA_in      const this_type&                        reference
                )
            {
                BL_CHK_T(
                    true,
                    base.m_scheme.empty(),
                    ArgumentException(),
                    BL_MSG()
                        << "The base of a URI reference resolution must be an absolute URI"
                    );

                this_type result;

                if( ! reference.m_scheme.empty() )
                {
                    result.m_scheme = reference.m_scheme;
                    result.copyAuthorityFrom( reference );
                    result.m_path = removeDotSegments( reference.m_path );
                    result.copyQueryFrom( reference );
                }
                else
                {
                    if( reference.m_hasAuthority )
                    {
                        result.copyAuthorityFrom( reference );
                        result.m_path = removeDotSegments( reference.m_path );
                        result.copyQueryFrom( reference );
                    }
                    else
                    {
                        if( reference.m_path.empty() )
                        {
                            result.m_path = base.m_path;

                            result.copyQueryFrom( reference.m_hasQuery ? reference : base );
                        }
                        else
                        {
                            result.m_path = ( '/' == reference.m_path[ 0 ] )
                                ? removeDotSegments( reference.m_path )
                                : removeDotSegments( mergePaths( base, reference.m_path ) );

                            result.copyQueryFrom( reference );
                        }

                        result.copyAuthorityFrom( base );
                    }

                    result.m_scheme = base.m_scheme;
                }

                result.m_fragment = reference.m_fragment;
                result.m_hasFragment = reference.m_hasFragment;

                return result;
            }

            /**
             * @brief Parses a reference and resolves it against this URI - the shape a redirect
             * needs, where this is the request URI and the text is the Location header
             */

            this_type resolve( SAA_in const std::string& reference ) const
            {
                return resolve( *this, parse( reference ) );
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( UriT, g_schemeHttp )         = "http";
        BL_DEFINE_STATIC_CONST_STRING( UriT, g_schemeHttps )        = "https";

        typedef UriT<> Uri;

    } // net

} // bl

#endif /* __BL_URI_H_ */
