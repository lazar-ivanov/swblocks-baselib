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

#ifndef __BL_HTTP_HEADERLIST_H_
#define __BL_HTTP_HEADERLIST_H_

#include <baselib/http/Globals.h>

#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace bl
{
    namespace http
    {
        /**
         * @brief class HeaderListT - an ordered, multi-value list of HTTP header fields
         *
         * http::HeadersMap ( Globals.h ) is an std::unordered_map< std::string, std::string >
         * and it therefore loses three things which a header list has to keep:
         *
         *  - the ORDER of the fields, because the map is unordered
         *  - REPEATED names, because a map holds exactly one value per key
         *  - the ORIGINAL CASE, in the sense that two spellings of one name ( "Accept" and
         *    "accept" ) are two unrelated keys, while a lookup which compensates with
         *    str::iequals - the way Response.h and Parser.h do - will find only one of them
         *
         * All three matter. HTTP/2 header compression is stateful, so the bytes which go on
         * the wire depend on the order the fields are emitted in, and the browser
         * impersonation work reproduces a browser's header order and its HTTP/1.1 casing
         * exactly. This type is what carries that; http::HeadersMap stays the compatibility
         * facade for the existing SimpleHttpTask API.
         *
         * The list stores a name exactly as it was given and never lowercases it: the HTTP/2
         * encoder lowercases as it encodes, because RFC 9113 section 8.2.1 requires it, and
         * the HTTP/1.1 serializer keeps the case given. Lookup is case-insensitive over
         * ASCII only - a field name is a token, so it is ASCII by construction, and an
         * ASCII-only fold cannot be changed out from under the lookup by a caller which
         * installs a different global locale ( which str::iequals and str::to_lower_copy are
         * subject to, as they take std::locale() ).
         *
         * VALIDATION HERE IS A SECURITY BOUNDARY, NOT A CONVENIENCE. append( ... ),
         * set( ... ) and fromMap( ... ) validate and throw, and there is no unchecked way in.
         * A value which carries CR or LF is how a caller-controlled string becomes extra
         * header lines - or an entire extra response - in an HTTP/1.1 stream, and a name
         * which is not a token is the same attack through the name. RFC 9113 section 8.2.1
         * additionally makes NUL, CR or LF in a field value a malformed message on HTTP/2.
         *
         *  - a name is 1*tchar ( RFC 9110 section 5.1 ): never empty, and never carrying a
         *    space, a colon, a control character or a non-ASCII byte
         *  - a value is *field-content ( RFC 9110 section 5.5 ): it may be empty, it may
         *    carry a space or a horizontal tab between two field-vchars and it may carry
         *    obs-text ( 0x80-0xFF ), but it may not begin or end with a space or a horizontal
         *    tab, and it may not carry NUL, CR, LF, any other control character or DEL
         *
         * An error message never echoes a name which failed the name check - a rejected name
         * can carry the very CR and LF the check exists to stop, and a log line is exactly as
         * injectable as a header block. Once a name has passed it is a token, so it is then
         * safe to name it in the message for a value which failed.
         *
         * HTTP/2 pseudo-headers ( ":method", ":authority" and the rest ) are deliberately NOT
         * representable here, because ':' is not a token character. They are derived from the
         * method and the URI by the session engine, and their order comes from the profile's
         * own pseudo-header order field rather than from this list.
         *
         * CONVERSION, AND EXACTLY WHERE IT LOSES. toMap( ... ) folds the list into an
         * http::HeadersMap, which can hold neither order nor repeats:
         *
         *  - fields whose names match case-insensitively become one entry, keyed by the
         *    spelling of the FIRST of them
         *  - their values are joined with ", ", which is the equivalent single-field form
         *    RFC 9110 section 5.3 defines for a repeated list-valued field
         *  - "Set-Cookie" is the field that rule does not hold for ( RFC 9110 section 5.3,
         *    RFC 6265 section 5.2 ): two cookies joined by a comma cannot be split apart
         *    again. An http::HeadersMap cannot represent two of them at all, so a caller
         *    which needs the cookies reads them from the list and not from the map
         *
         * fromMap( ... ) is the other direction and cannot invent an order, because the map
         * has none. Rather than hand back whatever order the hash table happened to be in -
         * which varies with the platform, the contents and the standard library version, and
         * would make the bytes on the wire vary with it - it produces a deterministic one: by
         * name, ASCII-case-insensitively, then by value. A caller which cares about the order
         * builds the list directly.
         *
         * So, for a map whose names and values are valid, map -> list -> map round-trips
         * exactly unless the map holds two spellings of one name; and list -> map -> list
         * keeps every name and every byte of every value, but re-orders the list and collapses
         * repeats into one comma-joined field.
         *
         * The lookups are linear scans and toMap( ... ) is quadratic in the number of distinct
         * names. A header list is tens of entries, so this is deliberate - the constant factor
         * of a small vector beats an index, and an index would have to be kept consistent with
         * the order, which is the one thing this type exists to preserve.
         */

        template
        <
            typename E = void
        >
        class HeaderListT FINAL
        {
        public:

            typedef HeaderListT< E >                                this_type;

            /**
             * @brief class Header - one field line: a name exactly as it was given and its value
             */

            class Header FINAL
            {
            public:

                Header(
                    SAA_in          std::string&&                   name,
                    SAA_in          std::string&&                   value
                    )
                    :
                    m_name( BL_PARAM_FWD( name ) ),
                    m_value( BL_PARAM_FWD( value ) )
                {
                }

                const std::string& name() const NOEXCEPT
                {
                    return m_name;
                }

                const std::string& value() const NOEXCEPT
                {
                    return m_value;
                }

            private:

                std::string                                         m_name;
                std::string                                         m_value;
            };

            typedef std::vector< Header >                           headers_t;
            typedef typename headers_t::const_iterator              const_iterator;

        private:

            headers_t                                               m_headers;

            static const std::string                                g_valueSeparator;

            /*************************************************************************
             * The RFC 9110 character classes and the ASCII-only case fold
             */

            static bool isTokenChar( SAA_in const char ch ) NOEXCEPT
            {
                const auto octet = static_cast< unsigned char >( ch );

                if(
                    ( octet >= 'a' && octet <= 'z' ) ||
                    ( octet >= 'A' && octet <= 'Z' ) ||
                    ( octet >= '0' && octet <= '9' )
                    )
                {
                    return true;
                }

                switch( octet )
                {
                    case '!':
                    case '#':
                    case '$':
                    case '%':
                    case '&':
                    case '\'':
                    case '*':
                    case '+':
                    case '-':
                    case '.':
                    case '^':
                    case '_':
                    case '`':
                    case '|':
                    case '~':
                        return true;

                    default:
                        return false;
                }
            }

            /*
             * field-vchar = VCHAR / obs-text, i.e. 0x21-0x7E or 0x80-0xFF ( RFC 9110 5.5 )
             */

            static bool isFieldVChar( SAA_in const char ch ) NOEXCEPT
            {
                const auto octet = static_cast< unsigned char >( ch );

                return octet >= 0x21U && octet != 0x7FU;
            }

            static bool isSpaceOrTab( SAA_in const char ch ) NOEXCEPT
            {
                return ch == ' ' || ch == '\t';
            }

            static char toLowerAsciiChar( SAA_in const char ch ) NOEXCEPT
            {
                const auto octet = static_cast< unsigned char >( ch );

                return ( octet >= 'A' && octet <= 'Z' ) ?
                    static_cast< char >( octet - 'A' + 'a' ) : ch;
            }

            static bool lessIgnoreCase(
                SAA_in          const std::string&                  lhs,
                SAA_in          const std::string&                  rhs
                ) NOEXCEPT
            {
                const auto size = std::min( lhs.size(), rhs.size() );

                for( std::size_t i = 0U; i < size; ++i )
                {
                    const auto left = static_cast< unsigned char >( toLowerAsciiChar( lhs[ i ] ) );
                    const auto right = static_cast< unsigned char >( toLowerAsciiChar( rhs[ i ] ) );

                    if( left != right )
                    {
                        return left < right;
                    }
                }

                return lhs.size() < rhs.size();
            }

            static bool hasLeadingOrTrailingWhitespace( SAA_in const std::string& value ) NOEXCEPT
            {
                return
                    ! value.empty() &&
                    ( isSpaceOrTab( value.front() ) || isSpaceOrTab( value.back() ) );
            }

            /*
             * Returns the position of the first field whose name matches the one given, or
             * size() if there is none
             */

            std::size_t indexOf( SAA_in const std::string& name ) const NOEXCEPT
            {
                for( std::size_t i = 0U; i < m_headers.size(); ++i )
                {
                    if( equalsIgnoreCase( m_headers[ i ].name(), name ) )
                    {
                        return i;
                    }
                }

                return m_headers.size();
            }

        public:

            /*************************************************************************
             * Validation - see the security note on the class
             */

            static bool isValidHeaderName( SAA_in const std::string& name ) NOEXCEPT
            {
                if( name.empty() )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < name.size(); ++i )
                {
                    if( ! isTokenChar( name[ i ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            static bool isValidHeaderValue( SAA_in const std::string& value ) NOEXCEPT
            {
                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    const auto ch = value[ i ];

                    if( ! isFieldVChar( ch ) && ! isSpaceOrTab( ch ) )
                    {
                        return false;
                    }
                }

                return ! hasLeadingOrTrailingWhitespace( value );
            }

            static void validateHeader(
                SAA_in          const std::string&                  name,
                SAA_in          const std::string&                  value
                )
            {
                if( ! isValidHeaderName( name ) )
                {
                    /*
                     * The name is deliberately not echoed here - a name which was rejected can
                     * carry the CR and the LF this check exists to stop, and a log line is as
                     * injectable as a header block
                     */

                    BL_THROW(
                        InvalidDataFormatException()
                            << eh::errinfo_is_user_friendly( true ),
                        BL_MSG()
                            << "Invalid HTTP header - the name is empty or contains a character "
                            << "which is not allowed in a header name"
                        );
                }

                /*
                 * The name has passed, so it is a token and it is safe to name it below
                 */

                if( hasLeadingOrTrailingWhitespace( value ) )
                {
                    BL_THROW(
                        InvalidDataFormatException()
                            << eh::errinfo_is_user_friendly( true ),
                        BL_MSG()
                            << "Invalid HTTP header '"
                            << name
                            << "' - the value begins or ends with a space or a horizontal tab"
                        );
                }

                if( ! isValidHeaderValue( value ) )
                {
                    BL_THROW(
                        InvalidDataFormatException()
                            << eh::errinfo_is_user_friendly( true ),
                        BL_MSG()
                            << "Invalid HTTP header '"
                            << name
                            << "' - the value contains a character which is not allowed in a "
                            << "header value"
                        );
                }
            }

            /*
             * The case rule every lookup on this type uses - ASCII only, see the class note
             */

            static bool equalsIgnoreCase(
                SAA_in          const std::string&                  lhs,
                SAA_in          const std::string&                  rhs
                ) NOEXCEPT
            {
                if( lhs.size() != rhs.size() )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < lhs.size(); ++i )
                {
                    if( toLowerAsciiChar( lhs[ i ] ) != toLowerAsciiChar( rhs[ i ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*************************************************************************
             * The list itself
             */

            bool empty() const NOEXCEPT
            {
                return m_headers.empty();
            }

            std::size_t size() const NOEXCEPT
            {
                return m_headers.size();
            }

            const_iterator begin() const NOEXCEPT
            {
                return m_headers.begin();
            }

            const_iterator end() const NOEXCEPT
            {
                return m_headers.end();
            }

            const Header& at( SAA_in const std::size_t pos ) const
            {
                BL_CHK_ARG( pos < m_headers.size(), pos );

                return m_headers[ pos ];
            }

            void clear() NOEXCEPT
            {
                m_headers.clear();
            }

            /*
             * Appends a field, keeping the name exactly as given. A name which is already in
             * the list is appended again rather than replaced - that is what makes the list
             * multi-value
             */

            void append(
                SAA_in          std::string&&                       name,
                SAA_in          std::string&&                       value
                )
            {
                validateHeader( name, value );

                m_headers.push_back( Header( BL_PARAM_FWD( name ), BL_PARAM_FWD( value ) ) );
            }

            /*
             * Replaces every field with this name by the one given. The field keeps the
             * POSITION of the first one it replaces, so setting a value does not move the
             * header, and it takes the spelling the caller passed. A name which is not in the
             * list is appended at the end
             */

            void set(
                SAA_in          std::string&&                       name,
                SAA_in          std::string&&                       value
                )
            {
                validateHeader( name, value );

                const auto pos = indexOf( name );

                if( pos == m_headers.size() )
                {
                    m_headers.push_back( Header( BL_PARAM_FWD( name ), BL_PARAM_FWD( value ) ) );

                    return;
                }

                m_headers[ pos ] = Header( BL_PARAM_FWD( name ), BL_PARAM_FWD( value ) );

                for( std::size_t i = m_headers.size(); i > pos + 1U; --i )
                {
                    const auto current = i - 1U;

                    if( equalsIgnoreCase( m_headers[ current ].name(), m_headers[ pos ].name() ) )
                    {
                        m_headers.erase( m_headers.begin() + static_cast< std::ptrdiff_t >( current ) );
                    }
                }
            }

            /*
             * Removes every field with this name and returns how many were removed
             */

            std::size_t removeAll( SAA_in const std::string& name )
            {
                const auto before = m_headers.size();

                for( std::size_t i = m_headers.size(); i > 0U; --i )
                {
                    const auto current = i - 1U;

                    if( equalsIgnoreCase( m_headers[ current ].name(), name ) )
                    {
                        m_headers.erase( m_headers.begin() + static_cast< std::ptrdiff_t >( current ) );
                    }
                }

                return before - m_headers.size();
            }

            /*************************************************************************
             * Lookup - case-insensitive, and never throwing for a name which is simply
             * not there
             */

            bool has( SAA_in const std::string& name ) const NOEXCEPT
            {
                return indexOf( name ) != m_headers.size();
            }

            std::size_t count( SAA_in const std::string& name ) const NOEXCEPT
            {
                std::size_t result = 0U;

                for( std::size_t i = 0U; i < m_headers.size(); ++i )
                {
                    if( equalsIgnoreCase( m_headers[ i ].name(), name ) )
                    {
                        ++result;
                    }
                }

                return result;
            }

            /*
             * The value of the first field with this name, or nullptr if there is none. The
             * pointer is invalidated by the next modification of the list
             */

            const std::string* tryGet( SAA_in const std::string& name ) const NOEXCEPT
            {
                const auto pos = indexOf( name );

                return pos == m_headers.size() ? nullptr : &m_headers[ pos ].value();
            }

            const std::string& get( SAA_in const std::string& name ) const
            {
                const auto* value = tryGet( name );

                if( value == nullptr )
                {
                    /*
                     * The name is not echoed, for the reason given on validateHeader( ... ) -
                     * a caller which reached here already knows which name it asked for
                     */

                    BL_THROW(
                        NotFoundException(),
                        BL_MSG()
                            << "The HTTP header requested is not present in the header list"
                        );
                }

                return *value;
            }

            /*
             * Every value with this name, in the order the fields appear in the list
             */

            std::vector< std::string > getAll( SAA_in const std::string& name ) const
            {
                std::vector< std::string > result;

                for( std::size_t i = 0U; i < m_headers.size(); ++i )
                {
                    if( equalsIgnoreCase( m_headers[ i ].name(), name ) )
                    {
                        result.push_back( m_headers[ i ].value() );
                    }
                }

                return result;
            }

            /*************************************************************************
             * Conversion to and from the http::HeadersMap compatibility facade - see the
             * class note for what each direction loses
             */

            HeadersMap toMap() const
            {
                HeadersMap result;

                for( std::size_t i = 0U; i < m_headers.size(); ++i )
                {
                    const auto& header = m_headers[ i ];

                    auto pos = result.end();

                    for( auto current = result.begin(); current != result.end(); ++current )
                    {
                        if( equalsIgnoreCase( current -> first, header.name() ) )
                        {
                            pos = current;

                            break;
                        }
                    }

                    if( pos == result.end() )
                    {
                        result[ header.name() ] = header.value();
                    }
                    else
                    {
                        pos -> second += g_valueSeparator;
                        pos -> second += header.value();
                    }
                }

                return result;
            }

            static this_type fromMap( SAA_in const HeadersMap& headers )
            {
                this_type result;

                for( const auto& pair : headers )
                {
                    result.append( cpp::copy( pair.first ), cpp::copy( pair.second ) );
                }

                std::sort(
                    result.m_headers.begin(),
                    result.m_headers.end(),
                    []( SAA_in const Header& lhs, SAA_in const Header& rhs ) -> bool
                    {
                        if( ! equalsIgnoreCase( lhs.name(), rhs.name() ) )
                        {
                            return lessIgnoreCase( lhs.name(), rhs.name() );
                        }

                        return lhs.value() < rhs.value();
                    }
                    );

                return result;
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( HeaderListT, g_valueSeparator )              = ", ";

        typedef HeaderListT<> HeaderList;

    } // http

} // bl

#endif /* __BL_HTTP_HEADERLIST_H_ */
