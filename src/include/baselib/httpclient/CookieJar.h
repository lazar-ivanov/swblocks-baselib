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

#ifndef __BL_HTTPCLIENT_COOKIEJAR_H_
#define __BL_HTTPCLIENT_COOKIEJAR_H_

#include <baselib/core/Uri.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief One stored cookie - RFC 6265 section 5.3's storage model
         */

        struct Cookie
        {
            std::string                                                         name;
            std::string                                                         value;

            /**
             * The scope, canonicalized: lower case, with any leading dot removed. A host-only
             * cookie carries the request host here and matches only that exact host
             */

            std::string                                                         domain;

            std::string                                                         path;

            /**
             * time::pos_infin for a session cookie, which is what a cookie with neither Expires
             * nor Max-Age is
             */

            time::ptime                                                         expiryTime;

            time::ptime                                                         creationTime;
            time::ptime                                                         lastAccessTime;

            cpp::ScalarTypeIniter< bool >                                       isSecure;
            cpp::ScalarTypeIniter< bool >                                       isHttpOnly;
            cpp::ScalarTypeIniter< bool >                                       isHostOnly;

            Cookie()
                :
                expiryTime( time::pos_infin )
            {
            }

            bool isSessionCookie() const NOEXCEPT
            {
                return expiryTime.is_pos_infinity();
            }
        };

        /**
         * @brief What became of a Set-Cookie header
         *
         * A bool would not do here. Which rule rejected a cookie is the interesting part - the
         * domain rules below are a security boundary, and a test which can only see "not stored"
         * cannot tell a cookie rejected for crossing a domain from one rejected for a typo
         */

        enum class CookieStoreResult : std::uint8_t
        {
            /**
             * Stored, or replaced an existing cookie of the same name, domain and path
             */

            Stored,

            /**
             * Accepted and then removed, because it was already expired - which is how a server
             * deletes a cookie (Max-Age=0, or an Expires in the past)
             */

            Removed,

            /**
             * Not a well formed set-cookie-string: no '=' in the name-value pair, or an empty name
             */

            RejectedMalformed,

            /**
             * The Domain attribute is one this client will not accept for this request - it does
             * not domain-match the request host, or it is an IP address, a bare TLD or has no
             * embedded dot, and is not the request host itself
             */

            RejectedDomain,

            /**
             * The request was not made by an HTTP API and the cookie is HttpOnly
             * (RFC 6265 section 5.3 step 10)
             */

            RejectedHttpOnly,
        };

        /**
         * @brief class CookieJarT - an RFC 6265 cookie store, per session and thread safe
         *
         * WHAT THIS IS FOR. Design 5.6: the client session holds one of these, hands every
         * Set-Cookie of a response to setCookie( ... ) and asks cookieHeaderValue( ... ) for what
         * to send. It is deliberately a plain object rather than a task or an om interface -
         * cookie handling is not I/O, and design 5.2 runs it on the general purpose pool for
         * exactly that reason.
         *
         * SCOPE COMES FROM net::Uri AND ITS REFUSAL, NOT FROM COMPONENTS. Every entry point takes
         * the request URI and calls net::Uri::origin() on it first. origin() throws for a
         * reference with no scheme and for one with no host, and those are precisely the two cases
         * which would otherwise scope a cookie to the empty host - which matches every other
         * empty-host entry, i.e. leaks one site's cookies to another. The doc block of origin()
         * names cookie scoping as its purpose; this is the caller that means.
         *
         * THERE IS NO PUBLIC SUFFIX LIST, AND THE RESIDUAL RISK IS STATED RATHER THAN HIDDEN. The
         * attack a public suffix list exists to stop is a "supercookie": a page on
         * evil.example.co.uk setting Domain=co.uk, which would then be sent to every site under
         * that suffix. Without a list we apply the two checks which need no data:
         *
         *  - a Domain attribute must domain-match the request host (RFC 6265 section 5.3 step 6),
         *    so a page cannot set a cookie for an unrelated site; and
         *  - a Domain attribute with no embedded dot, or which is a single label, is rejected, so
         *    "Domain=com" from a page under com, and "Domain=localhost" from a page under
         *    localhost, are both refused
         *
         * A third rule sits beside them and is not about suffixes at all: an IP address is refused
         * as a scope, because an address has no hierarchy for a domain cookie to span.
         *
         * ... and all three carry the one exception RFC 6265 section 5.3 step 5 states: an
         * attribute they refuse, but which is IDENTICAL to the canonicalized request host, becomes
         * a HOST-ONLY cookie instead of a rejection. That is what makes "Domain=localhost" on
         * localhost and "Domain=1.2.3.4" on 1.2.3.4 work, which are the ordinary local-development
         * and bare-address cases, and it grants nothing: a host-only cookie goes back to that
         * exact host and to no other, so the same exception applied to the pathological
         * "Domain=com" on a host literally named com scopes the cookie to com alone rather than to
         * everything beneath it.
         *
         * WHAT REMAINS: a multi-label public suffix - co.uk, com.au, github.io - has an embedded
         * dot and domain-matches a host beneath it, so a page at a.co.uk CAN still set
         * Domain=co.uk here and a later request to b.co.uk WILL receive it. That is the documented
         * residual risk of design 5.6, and closing it needs the Mozilla list as data, which is a
         * dependency decision rather than code. It is recorded here, next to the checks which do
         * hold, so that a reader of this file cannot mistake the two rules above for full
         * protection.
         *
         * ASCII ONLY, DELIBERATELY. The case folding and the whitespace trimming below are written
         * out rather than taken from str::to_lower_copy or boost::trim, for the reason the header
         * of http::HeaderList gives: those take std::locale(), so a caller which installs a
         * different global locale can change what a security check compares. A host name is an
         * A-label and a cookie name is a token, so both are ASCII by construction.
         *
         * THE LOOKUPS ARE LINEAR. A jar holds tens to a few hundred cookies and every operation
         * has to consider all of them anyway - a match is over the domain suffix and the path
         * prefix, not over an equal key - so an index would buy nothing and would have to be kept
         * consistent with the caps and the eviction order.
         */

        template
        <
            typename E = void
        >
        class CookieJarT FINAL
        {
        public:

            typedef CookieJarT< E >                                             this_type;

            enum : std::size_t
            {
                /**
                 * RFC 6265 section 6.1's minimum capability is at least 50 cookies per domain and
                 * at least 3000 in total; these are those numbers as the caps
                 */

                DEFAULT_MAX_PER_DOMAIN              = 50U,
                DEFAULT_MAX_TOTAL                   = 3000U,
            };

            enum : std::int64_t
            {
                /**
                 * The largest Max-Age which is turned into a duration rather than clamped - a
                 * hundred years, which no cookie outlives and which a ptime holds comfortably
                 */

                MAX_MAX_AGE_SECONDS                 = 3155760000LL,
            };

        private:

            mutable os::mutex                                                   m_lock;

            std::vector< Cookie >                                               m_cookies;

            cpp::ScalarTypeIniter< std::size_t >                                m_maxPerDomain;
            cpp::ScalarTypeIniter< std::size_t >                                m_maxTotal;

            /*************************************************************************************
             * ASCII helpers - see the header note on why these are not the str:: ones
             */

            static char toLowerAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

            static std::string toLowerAsciiCopy( SAA_in const std::string& text )
            {
                std::string result( text );

                for( std::size_t pos = 0U; pos < result.size(); ++pos )
                {
                    result[ pos ] = toLowerAscii( result[ pos ] );
                }

                return result;
            }

            static bool equalsIgnoreCaseAscii(
                SAA_in          const std::string&                              lhs,
                SAA_in          const std::string&                              rhs
                ) NOEXCEPT
            {
                if( lhs.size() != rhs.size() )
                {
                    return false;
                }

                for( std::size_t pos = 0U; pos < lhs.size(); ++pos )
                {
                    if( toLowerAscii( lhs[ pos ] ) != toLowerAscii( rhs[ pos ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            /**
             * @brief Removes leading and trailing SP and HTAB - the OWS of RFC 6265 section 5.2
             */

            static std::string trimOws( SAA_in const std::string& text )
            {
                std::size_t first = 0U;

                while( first < text.size() && ( ' ' == text[ first ] || '\t' == text[ first ] ) )
                {
                    ++first;
                }

                std::size_t last = text.size();

                while( last > first && ( ' ' == text[ last - 1U ] || '\t' == text[ last - 1U ] ) )
                {
                    --last;
                }

                return text.substr( first, last - first );
            }

            /*************************************************************************************
             * The cookie-date algorithm of RFC 6265 section 5.1.1
             *
             * Written out rather than matched against a format string, because the RFC's algorithm
             * is deliberately delimiter based: it accepts the IMF-fixdate a modern server sends,
             * the two obsolete formats RFC 9110 still lists, and the several things real servers
             * emit which are none of the three. A format matcher would reject dates browsers
             * accept, and a cookie whose expiry does not parse is treated as a session cookie,
             * which means it would silently outlive its intended lifetime in one direction or die
             * early in the other
             */

            static bool isDateDelimiter( SAA_in const char ch ) NOEXCEPT
            {
                const auto value = static_cast< unsigned char >( ch );

                return
                    0x09U == value ||
                    ( value >= 0x20U && value <= 0x2FU ) ||
                    ( value >= 0x3BU && value <= 0x40U ) ||
                    ( value >= 0x5BU && value <= 0x60U ) ||
                    ( value >= 0x7BU && value <= 0x7EU );
            }

            static bool isDigit( SAA_in const char ch ) NOEXCEPT
            {
                return ch >= '0' && ch <= '9';
            }

            /**
             * @brief Reads the 1*2DIGIT day-of-month, or the 2*4DIGIT year, prefix of a token
             *
             * The minimum is a parameter and not a convenience: a year is 2*4DIGIT in the RFC's
             * grammar, so a one digit token is a day and never a year, and accepting it as one
             * would be leniency the specification does not have
             */

            static bool readNumberPrefix(
                SAA_in          const std::string&                              token,
                SAA_in          const std::size_t                               minDigits,
                SAA_in          const std::size_t                               maxDigits,
                SAA_out         unsigned&                                       value
                ) NOEXCEPT
            {
                std::size_t digits = 0U;

                value = 0U;

                while( digits < token.size() && digits < maxDigits && isDigit( token[ digits ] ) )
                {
                    value = ( value * 10U ) + static_cast< unsigned >( token[ digits ] - '0' );
                    ++digits;
                }

                return digits >= minDigits;
            }

            static bool tryParseTimeToken(
                SAA_in          const std::string&                              token,
                SAA_out         unsigned&                                       hour,
                SAA_out         unsigned&                                       minute,
                SAA_out         unsigned&                                       second
                ) NOEXCEPT
            {
                std::size_t pos = 0U;

                unsigned parts[ 3 ] = { 0U, 0U, 0U };

                for( std::size_t part = 0U; part < 3U; ++part )
                {
                    if( part )
                    {
                        if( pos >= token.size() || ':' != token[ pos ] )
                        {
                            return false;
                        }

                        ++pos;
                    }

                    std::size_t digits = 0U;

                    while( pos < token.size() && digits < 2U && isDigit( token[ pos ] ) )
                    {
                        parts[ part ] = ( parts[ part ] * 10U ) +
                            static_cast< unsigned >( token[ pos ] - '0' );

                        ++pos;
                        ++digits;
                    }

                    if( 0U == digits )
                    {
                        return false;
                    }
                }

                hour = parts[ 0 ];
                minute = parts[ 1 ];
                second = parts[ 2 ];

                return true;
            }

            static bool tryParseMonthToken(
                SAA_in          const std::string&                              token,
                SAA_out         unsigned&                                       month
                ) NOEXCEPT
            {
                static const char* const names[ 12 ] =
                {
                    "jan", "feb", "mar", "apr", "may", "jun",
                    "jul", "aug", "sep", "oct", "nov", "dec",
                };

                if( token.size() < 3U )
                {
                    return false;
                }

                for( unsigned index = 0U; index < 12U; ++index )
                {
                    if(
                        toLowerAscii( token[ 0 ] ) == names[ index ][ 0 ] &&
                        toLowerAscii( token[ 1 ] ) == names[ index ][ 1 ] &&
                        toLowerAscii( token[ 2 ] ) == names[ index ][ 2 ]
                        )
                    {
                        month = index + 1U;

                        return true;
                    }
                }

                return false;
            }

        public:

            /**
             * @brief Parses a cookie-date per RFC 6265 section 5.1.1
             *
             * @return false, leaving 'result' untouched, when the date is not one this algorithm
             * accepts - which the caller treats as "no Expires attribute" rather than as an error,
             * per section 5.2.1
             */

            static bool tryParseCookieDate(
                SAA_in          const std::string&                              text,
                SAA_inout       time::ptime&                                    result
                )
            {
                bool haveTime = false;
                bool haveDay = false;
                bool haveMonth = false;
                bool haveYear = false;

                unsigned hour = 0U;
                unsigned minute = 0U;
                unsigned second = 0U;
                unsigned day = 0U;
                unsigned month = 0U;
                unsigned year = 0U;

                std::size_t pos = 0U;

                while( pos < text.size() )
                {
                    while( pos < text.size() && isDateDelimiter( text[ pos ] ) )
                    {
                        ++pos;
                    }

                    const auto start = pos;

                    while( pos < text.size() && ! isDateDelimiter( text[ pos ] ) )
                    {
                        ++pos;
                    }

                    if( start == pos )
                    {
                        continue;
                    }

                    const auto token = text.substr( start, pos - start );

                    if( ! haveTime && tryParseTimeToken( token, hour, minute, second ) )
                    {
                        haveTime = true;

                        continue;
                    }

                    if( ! haveDay && readNumberPrefix( token, 1U /* minDigits */, 2U /* maxDigits */, day ) )
                    {
                        haveDay = true;

                        continue;
                    }

                    if( ! haveMonth && tryParseMonthToken( token, month ) )
                    {
                        haveMonth = true;

                        continue;
                    }

                    if( ! haveYear && readNumberPrefix( token, 2U /* minDigits */, 4U /* maxDigits */, year ) )
                    {
                        haveYear = true;

                        continue;
                    }
                }

                if( ! ( haveTime && haveDay && haveMonth && haveYear ) )
                {
                    return false;
                }

                /*
                 * The two digit year rules of section 5.1.1
                 */

                if( year >= 70U && year <= 99U )
                {
                    year += 1900U;
                }
                else if( year <= 69U )
                {
                    year += 2000U;
                }

                if(
                    day < 1U || day > 31U ||
                    year < 1601U ||
                    hour > 23U || minute > 59U || second > 59U
                    )
                {
                    return false;
                }

                try
                {
                    result = time::ptime(
                        time::date(
                            static_cast< unsigned short >( year ),
                            static_cast< unsigned short >( month ),
                            static_cast< unsigned short >( day )
                            ),
                        time::hours( static_cast< long >( hour ) ) +
                            time::minutes( static_cast< long >( minute ) ) +
                            time::seconds( static_cast< long >( second ) )
                        );
                }
                catch( std::exception& )
                {
                    /*
                     * A day which does not exist in that month - 31 February - reaches here rather
                     * than being range checked above, because the number of days in a month is not
                     * a constant
                     */

                    return false;
                }

                return true;
            }

            /**
             * @brief The domain-match rule of RFC 6265 section 5.1.3
             *
             * True when the strings are identical, or when all three hold: the domain is a suffix
             * of the host, the character of the host immediately before that suffix is a '.', and
             * the host is not an IP address
             *
             * The last condition is the one which is easy to miss and expensive to miss: without
             * it "1.2.3.4" would domain-match "2.3.4", so a cookie set with Domain=2.3.4 would be
             * sent to the address 1.2.3.4
             */

            static bool domainMatches(
                SAA_in          const std::string&                              host,
                SAA_in          const std::string&                              domain
                )
            {
                const auto hostLower = toLowerAsciiCopy( host );
                const auto domainLower = toLowerAsciiCopy( domain );

                if( hostLower == domainLower )
                {
                    return true;
                }

                if( domainLower.empty() || hostLower.size() <= domainLower.size() )
                {
                    return false;
                }

                if(
                    hostLower.compare(
                        hostLower.size() - domainLower.size(),
                        domainLower.size(),
                        domainLower
                        ) != 0
                    )
                {
                    return false;
                }

                if( '.' != hostLower[ hostLower.size() - domainLower.size() - 1U ] )
                {
                    return false;
                }

                return ! isIpAddressLike( hostLower );
            }

            /**
             * @brief Whether a host is an IP address rather than a name, for the third condition
             * of the domain-match rule
             *
             * An IPv6 literal reaches here without its brackets, because net::Uri::host() strips
             * them, so a colon is the tell. For IPv4 it is enough that every label is numeric:
             * net::Uri has already validated the syntax, and a name whose every label is numeric
             * cannot be registered anyway
             */

            static bool isIpAddressLike( SAA_in const std::string& host ) NOEXCEPT
            {
                if( host.empty() )
                {
                    return false;
                }

                if( std::string::npos != host.find( ':' ) )
                {
                    return true;
                }

                for( std::size_t pos = 0U; pos < host.size(); ++pos )
                {
                    if( ! isDigit( host[ pos ] ) && '.' != host[ pos ] )
                    {
                        return false;
                    }
                }

                return true;
            }

            /**
             * @brief The path-match rule of RFC 6265 section 5.1.4
             *
             * The third condition - that the cookie path which does not end in '/' must be
             * followed in the request path by a '/' - is what stops a cookie scoped to "/admin"
             * being sent to "/administrator"
             */

            static bool pathMatches(
                SAA_in          const std::string&                              requestPath,
                SAA_in          const std::string&                              cookiePath
                )
            {
                if( requestPath == cookiePath )
                {
                    return true;
                }

                if( cookiePath.empty() || requestPath.size() <= cookiePath.size() )
                {
                    return false;
                }

                if( requestPath.compare( 0U, cookiePath.size(), cookiePath ) != 0 )
                {
                    return false;
                }

                if( '/' == cookiePath[ cookiePath.size() - 1U ] )
                {
                    return true;
                }

                return '/' == requestPath[ cookiePath.size() ];
            }

            /**
             * @brief The default-path of RFC 6265 section 5.1.4 - the request path up to but not
             * including the rightmost '/'
             */

            static std::string defaultPath( SAA_in const std::string& uriPath )
            {
                if( uriPath.empty() || '/' != uriPath[ 0 ] )
                {
                    return "/";
                }

                const auto pos = uriPath.rfind( '/' );

                if( 0U == pos )
                {
                    return "/";
                }

                return uriPath.substr( 0U, pos );
            }

            CookieJarT()
            {
                m_maxPerDomain = DEFAULT_MAX_PER_DOMAIN;
                m_maxTotal = DEFAULT_MAX_TOTAL;
            }

            std::size_t maxCookiesPerDomain() const NOEXCEPT
            {
                return m_maxPerDomain;
            }

            void maxCookiesPerDomain( SAA_in const std::size_t value ) NOEXCEPT
            {
                m_maxPerDomain = value;
            }

            std::size_t maxCookiesTotal() const NOEXCEPT
            {
                return m_maxTotal;
            }

            void maxCookiesTotal( SAA_in const std::size_t value ) NOEXCEPT
            {
                m_maxTotal = value;
            }

            std::size_t size() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_cookies.size();
            }

            void clear()
            {
                BL_MUTEX_GUARD( m_lock );

                m_cookies.clear();
            }

            /**
             * @brief A snapshot of the jar, for a caller which persists or inspects it
             */

            std::vector< Cookie > allCookies() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_cookies;
            }

            /**
             * @brief Stores one Set-Cookie header value against the request URI
             *
             * 'isHttpApi' is false for a caller which is not itself the HTTP stack - the
             * "non-HTTP API" of RFC 6265 section 5.3 step 10 - and an HttpOnly cookie is then
             * refused rather than stored
             *
             * 'now' exists so that expiry is testable without a clock; a default constructed
             * ptime means "ask the clock"
             *
             * @throw ArgumentException when the request URI is not an absolute URI with a host
             */

            CookieStoreResult setCookie(
                SAA_in          const net::Uri&                                 requestUri,
                SAA_in          const std::string&                              setCookieValue,
                SAA_in_opt      const bool                                      isHttpApi = true,
                SAA_in_opt      const time::ptime&                              now = time::ptime()
                )
            {
                /*
                 * Called for the refusal, not for the value - see the header note
                 */

                ( void ) requestUri.origin();

                const auto when = effectiveNow( now );

                const auto host = toLowerAsciiCopy( requestUri.host() );

                Cookie cookie;

                std::string domainAttribute;
                std::string pathAttribute;

                bool haveMaxAge = false;
                bool haveExpires = false;

                time::ptime expires( time::pos_infin );
                std::int64_t maxAge = 0;

                if(
                    ! parseSetCookie(
                        setCookieValue,
                        cookie,
                        domainAttribute,
                        pathAttribute,
                        haveExpires,
                        expires,
                        haveMaxAge,
                        maxAge
                        )
                    )
                {
                    return CookieStoreResult::RejectedMalformed;
                }

                if( cookie.isHttpOnly && ! isHttpApi )
                {
                    return CookieStoreResult::RejectedHttpOnly;
                }

                /*
                 * Max-Age takes precedence over Expires when both are present
                 * ( RFC 6265 section 5.3 step 3 ). Getting this the wrong way round is how a
                 * cookie a server meant to delete survives, because the deletion idiom is
                 * "Max-Age=0" alongside an Expires the server left in place
                 */

                if( haveMaxAge )
                {
                    /*
                     * Clamped before it becomes a duration. A Max-Age of 10^18 is "forever" by any
                     * reading, and adding that many seconds to a ptime overflows its tick count -
                     * which would wrap the expiry into the PAST and delete the cookie instead
                     */

                    cookie.expiryTime = ( maxAge <= 0 )
                        ? time::ptime( time::neg_infin )
                        : when + time::seconds(
                            static_cast< long >( std::min< std::int64_t >( maxAge, MAX_MAX_AGE_SECONDS ) )
                            );
                }
                else if( haveExpires )
                {
                    cookie.expiryTime = expires;
                }

                if( domainAttribute.empty() )
                {
                    cookie.domain = host;
                    cookie.isHostOnly = true;
                }
                else
                {
                    const auto domain = toLowerAsciiCopy(
                        ( '.' == domainAttribute[ 0 ] )
                            ? domainAttribute.substr( 1U )
                            : domainAttribute
                        );

                    bool isHostOnly = false;

                    if( ! isAcceptableDomainAttribute( host, domain, isHostOnly ) )
                    {
                        return CookieStoreResult::RejectedDomain;
                    }

                    /*
                     * 'isHostOnly' is set only by RFC 6265 section 5.3 step 5's exception, and in
                     * that case 'domain' IS the request host - so this branch and the empty
                     * attribute branch above store the same thing, which is the point of the step
                     */

                    cookie.domain = domain;
                    cookie.isHostOnly = isHostOnly;
                }

                cookie.path = ( pathAttribute.empty() || '/' != pathAttribute[ 0 ] )
                    ? defaultPath( requestUri.path() )
                    : pathAttribute;

                cookie.creationTime = when;
                cookie.lastAccessTime = when;

                BL_MUTEX_GUARD( m_lock );

                const auto existing = indexOf(
                    cookie.name,
                    cookie.domain,
                    cookie.path,
                    cookie.isHostOnly.value()
                    );

                const auto isExpired =
                    ! cookie.expiryTime.is_pos_infinity() && cookie.expiryTime <= when;

                if( existing != m_cookies.size() )
                {
                    if( isExpired )
                    {
                        m_cookies.erase( m_cookies.begin() + static_cast< std::ptrdiff_t >( existing ) );

                        return CookieStoreResult::Removed;
                    }

                    /*
                     * A replacement keeps the ORIGINAL creation time ( section 5.3 step 11 ),
                     * which is what keeps the Cookie header's order stable across a refresh
                     */

                    cookie.creationTime = m_cookies[ existing ].creationTime;

                    m_cookies[ existing ] = cookie;

                    return CookieStoreResult::Stored;
                }

                if( isExpired )
                {
                    return CookieStoreResult::Removed;
                }

                m_cookies.push_back( cookie );

                enforceCaps( cookie.domain, when );

                return CookieStoreResult::Stored;
            }

            /**
             * @brief Every unexpired cookie in scope for the request, in the order RFC 6265
             * section 5.4 step 2 puts them in - longer paths first, then by creation time
             *
             * Not const: it updates each returned cookie's last access time, which is what the
             * eviction order is over
             *
             * @throw ArgumentException when the request URI is not an absolute URI with a host
             */

            std::vector< Cookie > cookiesForRequest(
                SAA_in          const net::Uri&                                 requestUri,
                SAA_in_opt      const bool                                      isHttpApi = true,
                SAA_in_opt      const time::ptime&                              now = time::ptime()
                )
            {
                ( void ) requestUri.origin();

                const auto when = effectiveNow( now );

                const auto host = toLowerAsciiCopy( requestUri.host() );
                const auto path = requestUri.path().empty()
                    ? std::string( "/" )
                    : requestUri.path();

                /*
                 * "Secure" is about the SCHEME and not about whether a TLS handshake happened to
                 * take place, because the scheme is what the origin is defined over and what the
                 * cookie was scoped by
                 */

                const auto isSecureTransport = ( std::string( "https" ) == requestUri.scheme() );

                std::vector< Cookie > result;

                BL_MUTEX_GUARD( m_lock );

                removeExpiredLocked( when );

                for( std::size_t pos = 0U; pos < m_cookies.size(); ++pos )
                {
                    auto& cookie = m_cookies[ pos ];

                    if( cookie.isHostOnly )
                    {
                        if( host != cookie.domain )
                        {
                            continue;
                        }
                    }
                    else if( ! domainMatches( host, cookie.domain ) )
                    {
                        continue;
                    }

                    if( ! pathMatches( path, cookie.path ) )
                    {
                        continue;
                    }

                    /*
                     * A Secure cookie is never sent over a scheme which is not secure. This is the
                     * check which stops an active network attacker on the plaintext channel from
                     * learning a session cookie the site only ever set over TLS
                     */

                    if( cookie.isSecure && ! isSecureTransport )
                    {
                        continue;
                    }

                    if( cookie.isHttpOnly && ! isHttpApi )
                    {
                        continue;
                    }

                    cookie.lastAccessTime = when;

                    result.push_back( cookie );
                }

                std::stable_sort( result.begin(), result.end(), byPathThenCreation );

                return result;
            }

            /**
             * @brief The Cookie header value for the request, or the empty string when nothing is
             * in scope - in which case no Cookie header is sent at all
             */

            std::string cookieHeaderValue(
                SAA_in          const net::Uri&                                 requestUri,
                SAA_in_opt      const bool                                      isHttpApi = true,
                SAA_in_opt      const time::ptime&                              now = time::ptime()
                )
            {
                const auto cookies = cookiesForRequest( requestUri, isHttpApi, now );

                std::string result;

                for( std::size_t pos = 0U; pos < cookies.size(); ++pos )
                {
                    if( pos )
                    {
                        result += "; ";
                    }

                    result += cookies[ pos ].name;
                    result += '=';
                    result += cookies[ pos ].value;
                }

                return result;
            }

            /**
             * @brief Drops every cookie which has expired, and reports how many went
             */

            std::size_t removeExpired( SAA_in_opt const time::ptime& now = time::ptime() )
            {
                BL_MUTEX_GUARD( m_lock );

                return removeExpiredLocked( effectiveNow( now ) );
            }

        private:

            static time::ptime effectiveNow( SAA_in const time::ptime& now )
            {
                return now.is_not_a_date_time() ? time::second_clock::universal_time() : now;
            }

            static bool byPathThenCreation(
                SAA_in          const Cookie&                                   lhs,
                SAA_in          const Cookie&                                   rhs
                )
            {
                if( lhs.path.size() != rhs.path.size() )
                {
                    return lhs.path.size() > rhs.path.size();
                }

                return lhs.creationTime < rhs.creationTime;
            }

            /**
             * @brief The two checks which stand in for a public suffix list - see the header note
             *
             * 'isHostOnly' is an output: it is set for the single case in which the dot test below
             * refuses an attribute and RFC 6265 section 5.3 step 5 keeps the cookie anyway. It is
             * meaningless when this returns false
             */

            static bool isAcceptableDomainAttribute(
                SAA_in          const std::string&                              host,
                SAA_in          const std::string&                              domain,
                SAA_out         bool&                                           isHostOnly
                )
            {
                isHostOnly = false;

                if( domain.empty() )
                {
                    return false;
                }

                /*
                 * A Domain attribute which does not domain-match the request host is refused
                 * outright ( RFC 6265 section 5.3 step 6 ): this is what stops a page on one site
                 * setting a cookie for another
                 */

                if( ! domainMatches( host, domain ) )
                {
                    return false;
                }

                /*
                 * THE TWO ATTRIBUTES WHICH ARE NOT A USABLE SCOPE, AND THE ONE ANSWER THEY SHARE
                 *
                 * An IP address is refused because an address has no hierarchy at all, so
                 * "Domain=1.2.3.4" as a DOMAIN cookie is meaningless - it would otherwise be
                 * accepted as the degenerate identical-strings case of the domain-match rule
                 * above. No embedded dot, or a single label - "com", "localhost" - is refused
                 * because it is a registry suffix or near enough to one: this is the stand-in for
                 * a public suffix list, incomplete and knowingly so, and the header note carries
                 * the residual risk for a multi-label suffix such as co.uk
                 */

                const auto dot = domain.find( '.' );

                const bool isNotAUsableScope =
                    isIpAddressLike( domain ) ||
                    std::string::npos == dot ||
                    0U == dot ||
                    domain.size() - 1U == dot;

                if( isNotAUsableScope )
                {
                    /*
                     * RFC 6265 section 5.3 step 5, which answers both of the above the same way,
                     * and whose answer is NOT a flat rejection - an attribute identical to the
                     * canonicalized request host is stored HOST-ONLY, i.e. exactly as if the
                     * Set-Cookie had carried no Domain attribute at all. Without this,
                     * Domain=localhost on localhost - every local test server there is - and
                     * Domain=1.2.3.4 from a client talking to a bare address both silently lose
                     * their cookies, and for the address the refusal contradicted its own reason:
                     * "only ever a host-only cookie" is what this now stores.
                     *
                     * The test is equality with the request host and nothing weaker, so what these
                     * rules exist to stop is untouched. A page on app.localhost sending
                     * Domain=localhost is not identical to its host and is still refused, the same
                     * shape as a page under com sending Domain=com. An address is refused against
                     * every host but itself, and mostly before it gets here, because domainMatches
                     * never lets one address be a suffix of another. What the exception does admit
                     * is Domain=com from a host literally NAMED com - and there it grants nothing,
                     * because host-only is matched by string equality on the request host ( see
                     * cookiesForRequest ), so the cookie returns to com alone and never to
                     * anything beneath it.
                     *
                     * ON SPELLING, because equality over addresses is where this could be doing
                     * less work than it looks. The comparison is between two ASCII-lowercased
                     * SPELLINGS and not between two parsed addresses, so 1.2.3.4 and 01.2.3.4
                     * differ here, and so do ::1 and 0:0:0:0:0:0:0:1. That is the safe direction:
                     * an unfamiliar spelling costs a cookie, it can never widen one. The store and
                     * the match sides cannot drift apart either, because both compare
                     * toLowerAsciiCopy( net::Uri::host() ) against the same stored string - so
                     * whatever net::Uri does or does not canonicalize is applied identically to
                     * both. Two spellings ARE reconciled, by that shared fold and by the parser:
                     * hex case in an IPv6 literal, and a percent-encoded unreserved octet in the
                     * host, which net::Uri decodes ( Uri.h normalizeComponent ). And a ':' reaches
                     * host() ONLY through the bracketed IP-literal branch, which chkIpLiteral has
                     * validated - so although isIpAddressLike calls any colon-bearing string an
                     * address, equality can only ever admit one net::Uri itself accepted as an
                     * IPv6 literal, unbracketed
                     */

                    if( host != domain )
                    {
                        return false;
                    }

                    isHostOnly = true;
                }

                return true;
            }

            /**
             * @brief The set-cookie-string grammar of RFC 6265 section 5.2
             */

            static bool parseSetCookie(
                SAA_in          const std::string&                              text,
                SAA_inout       Cookie&                                         cookie,
                SAA_inout       std::string&                                    domainAttribute,
                SAA_inout       std::string&                                    pathAttribute,
                SAA_inout       bool&                                           haveExpires,
                SAA_inout       time::ptime&                                    expires,
                SAA_inout       bool&                                           haveMaxAge,
                SAA_inout       std::int64_t&                                   maxAge
                )
            {
                const auto semicolon = text.find( ';' );

                const auto pair = ( std::string::npos == semicolon )
                    ? text
                    : text.substr( 0U, semicolon );

                const auto equals = pair.find( '=' );

                /*
                 * A name-value pair with no '=' is ignored ENTIRELY ( section 5.2 step 2 ), rather
                 * than being taken as a valueless cookie
                 */

                if( std::string::npos == equals )
                {
                    return false;
                }

                cookie.name = trimOws( pair.substr( 0U, equals ) );
                cookie.value = trimOws( pair.substr( equals + 1U ) );

                if( cookie.name.empty() )
                {
                    return false;
                }

                if( std::string::npos == semicolon )
                {
                    return true;
                }

                auto pos = semicolon + 1U;

                while( pos <= text.size() )
                {
                    const auto next = text.find( ';', pos );

                    const auto attribute = ( std::string::npos == next )
                        ? text.substr( pos )
                        : text.substr( pos, next - pos );

                    const auto attributeEquals = attribute.find( '=' );

                    const auto name = trimOws(
                        ( std::string::npos == attributeEquals )
                            ? attribute
                            : attribute.substr( 0U, attributeEquals )
                        );

                    const auto value = ( std::string::npos == attributeEquals )
                        ? std::string()
                        : trimOws( attribute.substr( attributeEquals + 1U ) );

                    applyAttribute(
                        name,
                        value,
                        cookie,
                        domainAttribute,
                        pathAttribute,
                        haveExpires,
                        expires,
                        haveMaxAge,
                        maxAge
                        );

                    if( std::string::npos == next )
                    {
                        break;
                    }

                    pos = next + 1U;
                }

                return true;
            }

            static void applyAttribute(
                SAA_in          const std::string&                              name,
                SAA_in          const std::string&                              value,
                SAA_inout       Cookie&                                         cookie,
                SAA_inout       std::string&                                    domainAttribute,
                SAA_inout       std::string&                                    pathAttribute,
                SAA_inout       bool&                                           haveExpires,
                SAA_inout       time::ptime&                                    expires,
                SAA_inout       bool&                                           haveMaxAge,
                SAA_inout       std::int64_t&                                   maxAge
                )
            {
                if( equalsIgnoreCaseAscii( name, "Expires" ) )
                {
                    time::ptime parsed;

                    if( tryParseCookieDate( value, parsed ) )
                    {
                        expires = parsed;
                        haveExpires = true;
                    }

                    return;
                }

                if( equalsIgnoreCaseAscii( name, "Max-Age" ) )
                {
                    /*
                     * A Max-Age which is not a number is ignored, per section 5.2.2, rather than
                     * making the whole cookie invalid
                     */

                    std::int64_t parsed = 0;

                    if( tryParseSignedInteger( value, parsed ) )
                    {
                        maxAge = parsed;
                        haveMaxAge = true;
                    }

                    return;
                }

                if( equalsIgnoreCaseAscii( name, "Domain" ) )
                {
                    domainAttribute = value;

                    return;
                }

                if( equalsIgnoreCaseAscii( name, "Path" ) )
                {
                    pathAttribute = value;

                    return;
                }

                if( equalsIgnoreCaseAscii( name, "Secure" ) )
                {
                    cookie.isSecure = true;

                    return;
                }

                if( equalsIgnoreCaseAscii( name, "HttpOnly" ) )
                {
                    cookie.isHttpOnly = true;
                }

                /*
                 * Any other attribute is ignored, which is what section 5.2 requires - an unknown
                 * attribute never invalidates the cookie
                 */
            }

            static bool tryParseSignedInteger(
                SAA_in          const std::string&                              text,
                SAA_out         std::int64_t&                                   result
                ) NOEXCEPT
            {
                if( text.empty() )
                {
                    return false;
                }

                std::size_t pos = 0U;
                bool isNegative = false;

                if( '-' == text[ 0 ] )
                {
                    isNegative = true;
                    pos = 1U;
                }

                if( pos >= text.size() )
                {
                    return false;
                }

                std::int64_t value = 0;

                for( ; pos < text.size(); ++pos )
                {
                    if( ! isDigit( text[ pos ] ) )
                    {
                        return false;
                    }

                    if( value > ( ( std::numeric_limits< std::int64_t >::max() - 9 ) / 10 ) )
                    {
                        /*
                         * Saturate rather than overflow; a Max-Age this large is "forever" by any
                         * reading and an overflow would wrap it into the past
                         */

                        value = MAX_MAX_AGE_SECONDS;

                        break;
                    }

                    value = ( value * 10 ) + static_cast< std::int64_t >( text[ pos ] - '0' );
                }

                result = isNegative ? -value : value;

                return true;
            }

            /**
             * @brief The stored cookie one Set-Cookie replaces, if any
             *
             * The identity is name, domain, path AND the host-only flag. RFC 6265 section 5.3
             * step 11 lists only the first three; RFC 6265bis section 5.6 adds the fourth, and
             * that correction is followed here for two reasons. A host-only cookie and a domain
             * cookie of the same name are different cookies - the first goes to one host, the
             * second to every host beneath it - so keying without the flag lets a page WIDEN the
             * scope of its own host-only cookie by re-setting it with a Domain attribute, and the
             * old cookie disappears with no trace that its scope changed. And it is what browsers
             * do, which for a client whose purpose is to be indistinguishable from one is the
             * whole point
             */

            std::size_t indexOf(
                SAA_in          const std::string&                              name,
                SAA_in          const std::string&                              domain,
                SAA_in          const std::string&                              path,
                SAA_in          const bool                                      isHostOnly
                ) const NOEXCEPT
            {
                for( std::size_t pos = 0U; pos < m_cookies.size(); ++pos )
                {
                    if(
                        m_cookies[ pos ].name == name &&
                        m_cookies[ pos ].domain == domain &&
                        m_cookies[ pos ].path == path &&
                        m_cookies[ pos ].isHostOnly.value() == isHostOnly
                        )
                    {
                        return pos;
                    }
                }

                return m_cookies.size();
            }

            std::size_t removeExpiredLocked( SAA_in const time::ptime& when )
            {
                const auto before = m_cookies.size();

                std::size_t kept = 0U;

                for( std::size_t pos = 0U; pos < m_cookies.size(); ++pos )
                {
                    const auto& cookie = m_cookies[ pos ];

                    if( cookie.expiryTime.is_pos_infinity() || cookie.expiryTime > when )
                    {
                        if( kept != pos )
                        {
                            m_cookies[ kept ] = m_cookies[ pos ];
                        }

                        ++kept;
                    }
                }

                m_cookies.resize( kept );

                return before - kept;
            }

            /**
             * @brief The per-domain and total caps of RFC 6265 section 5.3 step 12
             *
             * Expired cookies go first, then the least recently used - which is the RFC's own
             * order, and which matters because the alternative, evicting the newest, would let a
             * flood of junk cookies from one path push out the session cookie that was just set
             */

            void enforceCaps(
                SAA_in          const std::string&                              domain,
                SAA_in          const time::ptime&                              when
                )
            {
                ( void ) removeExpiredLocked( when );

                while( countForDomain( domain ) > m_maxPerDomain )
                {
                    if( ! evictLeastRecentlyUsed( &domain ) )
                    {
                        break;
                    }
                }

                while( m_cookies.size() > m_maxTotal )
                {
                    if( ! evictLeastRecentlyUsed( nullptr ) )
                    {
                        break;
                    }
                }
            }

            std::size_t countForDomain( SAA_in const std::string& domain ) const NOEXCEPT
            {
                std::size_t count = 0U;

                for( std::size_t pos = 0U; pos < m_cookies.size(); ++pos )
                {
                    if( m_cookies[ pos ].domain == domain )
                    {
                        ++count;
                    }
                }

                return count;
            }

            bool evictLeastRecentlyUsed( SAA_in_opt const std::string* domain )
            {
                auto victim = m_cookies.size();

                for( std::size_t pos = 0U; pos < m_cookies.size(); ++pos )
                {
                    if( domain && m_cookies[ pos ].domain != *domain )
                    {
                        continue;
                    }

                    if(
                        victim == m_cookies.size() ||
                        m_cookies[ pos ].lastAccessTime < m_cookies[ victim ].lastAccessTime
                        )
                    {
                        victim = pos;
                    }
                }

                if( victim == m_cookies.size() )
                {
                    return false;
                }

                m_cookies.erase( m_cookies.begin() + static_cast< std::ptrdiff_t >( victim ) );

                return true;
            }
        };

        typedef CookieJarT<> CookieJar;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_COOKIEJAR_H_ */
