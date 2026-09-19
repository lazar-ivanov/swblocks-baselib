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

#include <baselib/http/HeaderList.h>
#include <baselib/http/Globals.h>

#include <baselib/core/BaseIncludes.h>

#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * http::HeadersMap loses the order of the fields, a repeated name and the distinction between
 * two spellings of one name. Those three losses are the whole reason bl::http::HeaderList
 * exists, so the cases below pin each of them from both sides - what the list keeps, and
 * exactly where the conversion to the map gives it up
 *
 * The name and value grammars are checked EXHAUSTIVELY, over all 256 octets, rather than by a
 * sample: the validation is a security boundary ( a CR or an LF in a value is how a caller
 * controlled string becomes extra header lines in an HTTP/1.1 stream ) and a sample would
 * leave the rest of the octet range unstated
 */

UTF_AUTO_TEST_CASE( HeaderList_OrderCaseAndRepeatsTests )
{
    const auto render = []( const bl::http::HeaderList& list ) -> std::string
    {
        std::string result;

        for( const auto& header : list )
        {
            result += header.name();
            result += ": ";
            result += header.value();
            result += "|";
        }

        return result;
    };

    bl::http::HeaderList list;

    UTF_CHECK( list.empty() );
    UTF_CHECK_EQUAL( list.size(), 0U );

    /*
     * The order below is a browser's, not an alphabetical one, and the casing is the casing
     * a browser puts on the wire under HTTP/1.1. Both have to come back out unchanged
     */

    list.append( "Host", "example.com" );
    list.append( "User-Agent", "Mozilla/5.0" );
    list.append( "Accept-Encoding", "gzip" );
    list.append( "accept-encoding", "br" );
    list.append( "X-Trace-Id", "42" );

    UTF_CHECK( ! list.empty() );
    UTF_CHECK_EQUAL( list.size(), 5U );

    UTF_CHECK_EQUAL(
        render( list ),
        std::string(
            "Host: example.com|"
            "User-Agent: Mozilla/5.0|"
            "Accept-Encoding: gzip|"
            "accept-encoding: br|"
            "X-Trace-Id: 42|"
            )
        );

    /*
     * The original case is kept per field line, so the two spellings of accept-encoding stay
     * distinguishable, and the repeat is a second entry rather than an overwrite
     */

    UTF_CHECK_EQUAL( list.at( 2U ).name(), std::string( "Accept-Encoding" ) );
    UTF_CHECK_EQUAL( list.at( 3U ).name(), std::string( "accept-encoding" ) );
    UTF_CHECK_EQUAL( list.at( 2U ).value(), std::string( "gzip" ) );
    UTF_CHECK_EQUAL( list.at( 3U ).value(), std::string( "br" ) );

    /*
     * Iteration is the same sequence as at( ... ), which is what a codec relies on
     */

    std::size_t visited = 0U;

    for( auto i = list.begin(); i != list.end(); ++i )
    {
        UTF_CHECK_EQUAL( i -> name(), list.at( visited ).name() );
        UTF_CHECK_EQUAL( i -> value(), list.at( visited ).value() );

        ++visited;
    }

    UTF_CHECK_EQUAL( visited, list.size() );

    /*
     * An empty value is a valid field value and keeps its place in the order
     */

    list.append( "X-Empty", "" );

    UTF_CHECK_EQUAL( list.size(), 6U );
    UTF_CHECK_EQUAL( list.at( 5U ).value(), std::string( "" ) );

    list.clear();

    UTF_CHECK( list.empty() );
    UTF_CHECK_EQUAL( list.size(), 0U );
}

UTF_AUTO_TEST_CASE( HeaderList_CaseInsensitiveLookupTests )
{
    bl::http::HeaderList list;

    list.append( "Accept-Encoding", "gzip" );
    list.append( "Host", "example.com" );
    list.append( "accept-encoding", "br" );
    list.append( "ACCEPT-ENCODING", "zstd" );

    /*
     * Any spelling finds the field, and the multi-value accessors see every one of them in
     * the order the list holds them
     */

    UTF_CHECK( list.has( "accept-encoding" ) );
    UTF_CHECK( list.has( "Accept-Encoding" ) );
    UTF_CHECK( list.has( "AcCePt-EnCoDiNg" ) );
    UTF_CHECK( ! list.has( "accept" ) );
    UTF_CHECK( ! list.has( "accept-encodin" ) );
    UTF_CHECK( ! list.has( "" ) );

    UTF_CHECK_EQUAL( list.count( "accept-encoding" ), 3U );
    UTF_CHECK_EQUAL( list.count( "HOST" ), 1U );
    UTF_CHECK_EQUAL( list.count( "Connection" ), 0U );

    const auto values = list.getAll( "Accept-Encoding" );

    UTF_REQUIRE_EQUAL( values.size(), 3U );
    UTF_CHECK_EQUAL( values[ 0 ], std::string( "gzip" ) );
    UTF_CHECK_EQUAL( values[ 1 ], std::string( "br" ) );
    UTF_CHECK_EQUAL( values[ 2 ], std::string( "zstd" ) );

    UTF_CHECK( list.getAll( "Connection" ).empty() );

    /*
     * The single-value accessors answer with the FIRST field of that name, which is the one a
     * HeadersMap based caller would have kept
     */

    UTF_CHECK_EQUAL( list.get( "ACCEPT-ENCODING" ), std::string( "gzip" ) );
    UTF_CHECK_EQUAL( *list.tryGet( "accept-encoding" ), std::string( "gzip" ) );
    UTF_CHECK_EQUAL( *list.tryGet( "host" ), std::string( "example.com" ) );

    UTF_CHECK( list.tryGet( "Connection" ) == nullptr );
    UTF_CHECK_THROW( list.get( "Connection" ), bl::NotFoundException );

    /*
     * The fold is ASCII only, deliberately - see the note on the class. A non-ASCII octet is
     * never folded onto another one, whatever the process locale happens to be
     */

    UTF_CHECK( bl::http::HeaderList::equalsIgnoreCase( "Accept", "aCCEPT" ) );
    UTF_CHECK( bl::http::HeaderList::equalsIgnoreCase( "", "" ) );
    UTF_CHECK( ! bl::http::HeaderList::equalsIgnoreCase( "Accept", "Accepts" ) );
    UTF_CHECK( ! bl::http::HeaderList::equalsIgnoreCase( "\xc3\xa4", "\xc3\x84" ) );
}

UTF_AUTO_TEST_CASE( HeaderList_SetRemoveAndBoundsTests )
{
    const auto render = []( const bl::http::HeaderList& list ) -> std::string
    {
        std::string result;

        for( const auto& header : list )
        {
            result += header.name();
            result += ": ";
            result += header.value();
            result += "|";
        }

        return result;
    };

    bl::http::HeaderList list;

    list.append( "A", "1" );
    list.append( "B", "2" );
    list.append( "a", "3" );
    list.append( "C", "4" );

    /*
     * set( ... ) keeps the POSITION of the first field it replaces, so setting a value does
     * not move the header to the end of the block - which would change the bytes a stateful
     * HTTP/2 encoder produces. It takes the spelling the caller passed, and it drops the
     * later duplicates
     */

    list.set( "a", "9" );

    UTF_CHECK_EQUAL( render( list ), std::string( "a: 9|B: 2|C: 4|" ) );
    UTF_CHECK_EQUAL( list.count( "A" ), 1U );

    /*
     * A name which is not in the list is appended at the end
     */

    list.set( "D", "5" );

    UTF_CHECK_EQUAL( render( list ), std::string( "a: 9|B: 2|C: 4|D: 5|" ) );

    /*
     * "aA" is a different header from "a" - a name folds as a whole and never as a prefix
     */

    list.set( "aA", "0" );

    UTF_CHECK_EQUAL( render( list ), std::string( "a: 9|B: 2|C: 4|D: 5|aA: 0|" ) );

    /*
     * removeAll( ... ) removes every spelling and says how many it removed
     */

    bl::http::HeaderList other;

    other.append( "Set-Cookie", "a=1" );
    other.append( "Host", "example.com" );
    other.append( "set-cookie", "b=2" );
    other.append( "SET-COOKIE", "c=3" );

    UTF_CHECK_EQUAL( other.removeAll( "Set-Cookie" ), 3U );
    UTF_CHECK_EQUAL( render( other ), std::string( "Host: example.com|" ) );
    UTF_CHECK_EQUAL( other.removeAll( "Set-Cookie" ), 0U );
    UTF_CHECK_EQUAL( other.removeAll( "Connection" ), 0U );
    UTF_CHECK_EQUAL( other.size(), 1U );

    /*
     * at( ... ) is bounds checked
     */

    UTF_CHECK_EQUAL( other.at( 0U ).name(), std::string( "Host" ) );
    UTF_CHECK_THROW( other.at( 1U ), bl::ArgumentException );

    bl::http::HeaderList empty;

    UTF_CHECK_THROW( empty.at( 0U ), bl::ArgumentException );
}

UTF_AUTO_TEST_CASE( HeaderList_NameValidationTests )
{
    /*
     * RFC 9110 section 5.1: field-name = token, and token = 1*tchar. The check below walks
     * all 256 octets and collects the ones accepted as a single character name, so what it
     * asserts is the whole rule rather than a sample of it - anything accepted which is not
     * a tchar, and anything rejected which is, shows up as a difference in this one string
     */

    std::string accepted;

    for( int octet = 0; octet <= 255; ++octet )
    {
        const std::string candidate( 1U, static_cast< char >( octet ) );

        if( bl::http::HeaderList::isValidHeaderName( candidate ) )
        {
            accepted += candidate;
        }
    }

    UTF_CHECK_EQUAL(
        accepted,
        std::string(
            "!#$%&'*+-."
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "^_`"
            "abcdefghijklmnopqrstuvwxyz"
            "|~"
            )
        );

    /*
     * A name is 1*tchar, so it is never empty
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderName( "X" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderName( "Content-Length" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderName( "content-length" ) );

    /*
     * The RFC 9110 section 5.6.2 delimiters are the characters a name is most often wrongly
     * allowed to carry, and the ones which do the damage - a ':' or a space ends the name in
     * an HTTP/1.1 field line
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content-Length:" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content,Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content;Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content/Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content=Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content@Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "(Content-Length)" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "[Content-Length]" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "{Content-Length}" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "\"Content-Length\"" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content\\Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content?Length" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "Content<Length>" ) );

    /*
     * A non-ASCII octet is not a tchar either, so there is no equivalent of an IDNA question
     * here to answer
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( "X-Caf\xc3\xa9" ) );

    /*
     * HTTP/2 pseudo-headers are deliberately not representable - ':' is not a tchar. They are
     * derived by the session engine from the method and the URI, and their order comes from
     * the profile's pseudo-header order field rather than from this list
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( ":method" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( ":authority" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderName( ":status" ) );

    /*
     * And the rejection is enforced on the way in, not only by the predicate
     */

    bl::http::HeaderList list;

    UTF_CHECK_THROW( list.append( ":method", "GET" ), bl::InvalidDataFormatException );
    UTF_CHECK_THROW( list.append( "", "v" ), bl::InvalidDataFormatException );
    UTF_CHECK_THROW( list.set( "Bad Name", "v" ), bl::InvalidDataFormatException );
    UTF_CHECK( list.empty() );
}

UTF_AUTO_TEST_CASE( HeaderList_ValueInjectionRejectionTests )
{
    /*
     * RFC 9110 section 5.5: field-value = *field-content, field-vchar = VCHAR / obs-text. The
     * exhaustive walk states which single octets stand as a value at all: everything from
     * 0x21 up except DEL, plus obs-text. Everything at or below SP is out, which is where the
     * CR, the LF and the NUL the work order names live, and so is DEL
     */

    std::vector< int > rejected;

    for( int octet = 0; octet <= 255; ++octet )
    {
        const std::string candidate( 1U, static_cast< char >( octet ) );

        if( ! bl::http::HeaderList::isValidHeaderValue( candidate ) )
        {
            rejected.push_back( octet );
        }
    }

    std::vector< int > expected;

    for( int octet = 0x00; octet <= 0x20; ++octet )
    {
        expected.push_back( octet );
    }

    expected.push_back( 0x7F );

    UTF_CHECK_EQUAL_COLLECTIONS( rejected.begin(), rejected.end(), expected.begin(), expected.end() );

    /*
     * The injection payloads themselves. Each of these, accepted, would end the field line
     * early and start one the caller did not authorise - or, with the doubled CRLF, an entire
     * second response
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\r\nX-Injected: 1" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\nX-Injected: 1" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\rX-Injected: 1" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\r\n\r\nHTTP/1.1 200 OK" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( std::string( "text/\0html", 10U ) ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\x0b" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "text/html\x7f" ) );

    /*
     * What a value IS allowed to carry. An empty value is valid, a space or a horizontal tab
     * between two field-vchars is valid, and obs-text ( 0x80-0xFF ) is valid - a legacy field
     * value is not required to be ASCII, unlike a name
     */

    UTF_CHECK( bl::http::HeaderList::isValidHeaderValue( "" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderValue( "gzip, deflate, br, zstd" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderValue( "a\tb" ) );
    UTF_CHECK( bl::http::HeaderList::isValidHeaderValue( "Caf\xc3\xa9" ) );

    /*
     * Leading and trailing whitespace is not part of a field value - RFC 9110 section 5.5 has
     * the recipient strip the OWS, and RFC 9113 section 8.2.1 makes a field which still
     * carries it malformed
     */

    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( " gzip" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "gzip " ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "\tgzip" ) );
    UTF_CHECK( ! bl::http::HeaderList::isValidHeaderValue( "gzip\t" ) );

    /*
     * There is no unchecked way into the list: append, set and fromMap all validate, and a
     * rejected append leaves the list exactly as it was
     */

    bl::http::HeaderList list;

    list.append( "Content-Type", "text/html" );

    UTF_CHECK_THROW(
        list.append( "Content-Type", "text/html\r\nX-Injected: 1" ),
        bl::InvalidDataFormatException
        );

    UTF_CHECK_THROW(
        list.set( "Content-Type", "text/html\r\nX-Injected: 1" ),
        bl::InvalidDataFormatException
        );

    UTF_CHECK_EQUAL( list.size(), 1U );
    UTF_CHECK_EQUAL( list.at( 0U ).value(), std::string( "text/html" ) );

    bl::http::HeadersMap poisoned;

    poisoned[ "Content-Type" ] = "text/html\r\nX-Injected: 1";

    UTF_CHECK_THROW( bl::http::HeaderList::fromMap( poisoned ), bl::InvalidDataFormatException );

    bl::http::HeadersMap poisonedName;

    poisonedName[ "Content-Type\r\nX-Injected" ] = "text/html";

    UTF_CHECK_THROW( bl::http::HeaderList::fromMap( poisonedName ), bl::InvalidDataFormatException );
}

UTF_AUTO_TEST_CASE( HeaderList_ErrorMessageRedactionTests )
{
    const auto messageOfAppend = []( const std::string& name, const std::string& value ) -> std::string
    {
        bl::http::HeaderList list;

        try
        {
            list.append( bl::cpp::copy( name ), bl::cpp::copy( value ) );
        }
        catch( bl::InvalidDataFormatException& e )
        {
            return std::string( e.what() );
        }

        return std::string();
    };

    /*
     * A value which failed is never echoed - a header value carries credentials ( an
     * Authorization or a Cookie ), and these messages reach logs. The name, having passed the
     * name check, is a token and is safe to name, which is what makes the message useful
     */

    const auto valueMessage = messageOfAppend( "Authorization", "Bearer s3cretvalue\r\nX-Injected: 1" );

    UTF_CHECK( ! valueMessage.empty() );
    UTF_CHECK( bl::cpp::contains( valueMessage, std::string( "Authorization" ) ) );
    UTF_CHECK( ! bl::cpp::contains( valueMessage, std::string( "s3cretvalue" ) ) );
    UTF_CHECK( ! bl::cpp::contains( valueMessage, std::string( "X-Injected" ) ) );

    /*
     * A name which failed is not echoed either, and for a sharper reason: a rejected name can
     * carry the very CR and LF the check exists to stop, and a log line is exactly as
     * injectable as a header block
     */

    const auto nameMessage = messageOfAppend( "X-Bad\r\nX-Injected: 1", "v" );

    UTF_CHECK( ! nameMessage.empty() );
    UTF_CHECK( ! bl::cpp::contains( nameMessage, std::string( "X-Bad" ) ) );
    UTF_CHECK( ! bl::cpp::contains( nameMessage, std::string( "X-Injected" ) ) );
    UTF_CHECK( nameMessage.find( '\r' ) == std::string::npos );
    UTF_CHECK( nameMessage.find( '\n' ) == std::string::npos );
}

UTF_AUTO_TEST_CASE( HeaderList_HeadersMapRoundTripTests )
{
    const auto render = []( const bl::http::HeaderList& list ) -> std::string
    {
        std::string result;

        for( const auto& header : list )
        {
            result += header.name();
            result += ": ";
            result += header.value();
            result += "|";
        }

        return result;
    };

    bl::http::HeaderList list;

    list.append( "Host", "example.com" );
    list.append( "Accept-Encoding", "gzip" );
    list.append( "accept-encoding", "br" );
    list.append( "X-Trace-Id", "42" );

    const auto map = list.toMap();

    /*
     * WHAT THE MAP CANNOT HOLD. The two spellings of accept-encoding fold into one entry,
     * keyed by the spelling of the first of them, and their values are joined with ", " -
     * the equivalent single-field form of RFC 9110 section 5.3
     */

    UTF_REQUIRE_EQUAL( map.size(), 3U );
    UTF_CHECK_EQUAL( map.at( "Accept-Encoding" ), std::string( "gzip, br" ) );
    UTF_CHECK_EQUAL( map.at( "Host" ), std::string( "example.com" ) );
    UTF_CHECK_EQUAL( map.at( "X-Trace-Id" ), std::string( "42" ) );
    UTF_CHECK( map.find( "accept-encoding" ) == map.end() );

    /*
     * ... and coming back, the order is gone. A HeadersMap has none, so fromMap produces a
     * deterministic one instead of the hash order - by name, ASCII case-insensitively, then
     * by value - and the repeat stays collapsed
     */

    const auto back = bl::http::HeaderList::fromMap( map );

    UTF_CHECK_EQUAL(
        render( back ),
        std::string( "Accept-Encoding: gzip, br|Host: example.com|X-Trace-Id: 42|" )
        );

    UTF_CHECK_EQUAL( back.size(), 3U );
    UTF_CHECK_EQUAL( back.count( "accept-encoding" ), 1U );

    /*
     * So list -> map -> list keeps every name and every byte of every value, and loses the
     * order, the repetition and the second spelling. Nothing else
     */

    UTF_CHECK( back.has( "host" ) );
    UTF_CHECK( back.has( "x-trace-id" ) );
    UTF_CHECK_EQUAL( back.get( "Host" ), std::string( "example.com" ) );

    /*
     * The other direction is an identity, for a map whose names fold apart from each other
     */

    bl::http::HeadersMap original;

    original[ "Host" ] = "example.com";
    original[ "Accept" ] = "*/*";
    original[ "X-Empty" ] = "";

    const auto roundTripped = bl::http::HeaderList::fromMap( original ).toMap();

    UTF_REQUIRE_EQUAL( roundTripped.size(), original.size() );
    UTF_CHECK( roundTripped == original );

    /*
     * ... and that is exactly where it stops being one. Two spellings of one name are two
     * entries in a map and one entry after the trip
     */

    bl::http::HeadersMap twoSpellings;

    twoSpellings[ "Accept" ] = "a";
    twoSpellings[ "accept" ] = "b";

    const auto fromTwoSpellings = bl::http::HeaderList::fromMap( twoSpellings );

    UTF_CHECK_EQUAL( render( fromTwoSpellings ), std::string( "Accept: a|accept: b|" ) );
    UTF_CHECK_EQUAL( fromTwoSpellings.toMap().size(), 1U );

    /*
     * fromMap is deterministic: the same map always gives the same list, whatever order the
     * hash table happened to be in. Built the other way round, the list is the same
     */

    bl::http::HeadersMap reordered;

    reordered[ "X-Empty" ] = "";
    reordered[ "Accept" ] = "*/*";
    reordered[ "Host" ] = "example.com";

    UTF_CHECK_EQUAL(
        render( bl::http::HeaderList::fromMap( reordered ) ),
        render( bl::http::HeaderList::fromMap( original ) )
        );

    UTF_CHECK_EQUAL(
        render( bl::http::HeaderList::fromMap( original ) ),
        std::string( "Accept: */*|Host: example.com|X-Empty: |" )
        );

    /*
     * THE ONE FIELD THE COMBINE RULE DOES NOT HOLD FOR. RFC 9110 section 5.3 and RFC 6265
     * section 5.2: two Set-Cookie lines joined by a comma cannot be split apart again, and a
     * HeadersMap cannot represent two of them at all. A caller which needs the cookies reads
     * them from the list
     */

    bl::http::HeaderList cookies;

    cookies.append( "Set-Cookie", "sid=1; Path=/; Expires=Thu, 01 Jan 2099 00:00:00 GMT" );
    cookies.append( "Set-Cookie", "pref=2; Path=/" );

    UTF_CHECK_EQUAL( cookies.getAll( "set-cookie" ).size(), 2U );

    const auto cookieMap = cookies.toMap();

    UTF_REQUIRE_EQUAL( cookieMap.size(), 1U );
    UTF_CHECK_EQUAL(
        cookieMap.at( "Set-Cookie" ),
        std::string( "sid=1; Path=/; Expires=Thu, 01 Jan 2099 00:00:00 GMT, pref=2; Path=/" )
        );

    /*
     * An empty list maps to an empty map and back
     */

    const bl::http::HeaderList emptyList;

    UTF_CHECK( emptyList.toMap().empty() );
    UTF_CHECK( bl::http::HeaderList::fromMap( bl::http::HeadersMap() ).empty() );
}
