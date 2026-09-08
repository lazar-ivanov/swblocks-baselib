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

#include <baselib/core/NetUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <string>

#include <utests/baselib/Utf.h>

UTF_AUTO_TEST_CASE( NetUtils_tryParseEndpointTest )
{
    bl::os::port_t port;
    std::string host;

    UTF_CHECK( ! bl::net::tryParseEndpoint( ":", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "host:", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( ":port", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "hostport", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "host:65536", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "host:123456", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "host:-123", host, port ) );
    UTF_CHECK( ! bl::net::tryParseEndpoint( "host:port", host, port ) );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:6", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 6 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:65", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 65 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:655", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 655 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:6553", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 6553 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:65535", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 65535 );
}

UTF_AUTO_TEST_CASE( NetUtils_IcmpHeaderTests )
{
    /*
     * The ICMP header is the only structure in this workstream which is written straight
     * onto the wire - tasks/utils/Pinger.h builds an echo request with it and matches the
     * reply against it - so the big endian layout and the RFC 1071 checksum are pinned
     * here byte by byte
     *
     * The body is exercised at both parities because computeChecksum( ... ) walks it two
     * bytes at a time and zero pads a trailing odd byte on the right
     */

    const std::string bodies[] =
    {
        std::string( "abcde" )      /* odd length */,
        std::string( "abcdef" )     /* even length */
    };

    for( const auto& body : bodies )
    {
        bl::net::IcmpHeader header;

        header.type( bl::net::IcmpHeader::ICMP_ECHO_REQUEST );
        header.code( 0 );
        header.identifier( 0x1234 );
        header.sequenceNumber( 0x5678 );

        header.computeChecksum( body.begin(), body.end() );

        bl::cpp::SafeOutputStringStream os;

        os << header;
        os << body;

        const std::string buffer = os.str();

        UTF_REQUIRE_EQUAL( 8U + body.size(), buffer.size() );

        /*
         * The RFC 1071 property is verified independently of the production code - the
         * one's complement sum of the whole message, checksum field included, must fold
         * down to 0xFFFF
         */

        std::uint32_t sum = 0U;

        for( std::size_t pos = 0U; pos < buffer.size(); pos += 2U )
        {
            sum += static_cast< std::uint32_t >( static_cast< std::uint8_t >( buffer[ pos ] ) ) << 8;

            if( pos + 1U < buffer.size() )
            {
                sum += static_cast< std::uint8_t >( buffer[ pos + 1U ] );
            }
        }

        sum = ( sum >> 16 ) + ( sum & 0xFFFFU );
        sum += ( sum >> 16 );

        UTF_REQUIRE_EQUAL( 0xFFFFU, sum & 0xFFFFU );

        /*
         * The raw layout at fixed offsets - this is what pins the big endian encode( ... )
         */

        UTF_REQUIRE_EQUAL( 8U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 0 ] ) ) );
        UTF_REQUIRE_EQUAL( 0U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 1 ] ) ) );
        UTF_REQUIRE_EQUAL( 0x12U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 4 ] ) ) );
        UTF_REQUIRE_EQUAL( 0x34U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 5 ] ) ) );
        UTF_REQUIRE_EQUAL( 0x56U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 6 ] ) ) );
        UTF_REQUIRE_EQUAL( 0x78U, static_cast< unsigned >( static_cast< std::uint8_t >( buffer[ 7 ] ) ) );

        /*
         * The extraction operator must read back exactly what the insertion operator wrote
         */

        bl::cpp::SafeInputStringStream is( buffer );

        bl::net::IcmpHeader header2;

        is >> header2;

        UTF_REQUIRE( is.good() || is.eof() );

        UTF_REQUIRE_EQUAL(
            static_cast< unsigned >( header.type() ),
            static_cast< unsigned >( header2.type() )
            );

        UTF_REQUIRE_EQUAL(
            static_cast< unsigned >( header.code() ),
            static_cast< unsigned >( header2.code() )
            );

        UTF_REQUIRE_EQUAL( 0x1234U, static_cast< unsigned >( header2.identifier() ) );
        UTF_REQUIRE_EQUAL( 0x5678U, static_cast< unsigned >( header2.sequenceNumber() ) );

        UTF_REQUIRE_EQUAL(
            static_cast< unsigned >( header.checksum() ),
            static_cast< unsigned >( header2.checksum() )
            );
    }
}

UTF_AUTO_TEST_CASE( NetUtils_Ipv4HeaderTests )
{
    /*
     * A well formed 20 byte IPv4 header, followed by the four option bytes which only the
     * IHL 6 case at the bottom consumes
     */

    const std::uint8_t raw[ 24 ] =
    {
        0x45                        /* version 4, IHL 5 */,
        0x10                        /* type of service */,
        0x00, 0x14                  /* total length - 20 */,
        0xAB, 0xCD                  /* identification */,
        0x41                        /* DF set, MF clear, fragment offset high bits */,
        0x23                        /* fragment offset low bits */,
        64                          /* time to live */,
        1                           /* protocol - ICMP */,
        0x00, 0x00                  /* header checksum - operator>> does not validate it */,
        10, 1, 2, 3                 /* source address */,
        10, 4, 5, 6                 /* destination address */,
        0x01, 0x02, 0x03, 0x04      /* options */
    };

    const std::string wellFormed( reinterpret_cast< const char* >( raw ), 20U );

    {
        bl::cpp::SafeInputStringStream is( wellFormed );

        bl::net::Ipv4Header header;

        is >> header;

        UTF_REQUIRE( ! is.fail() );

        UTF_REQUIRE_EQUAL( 4U, static_cast< unsigned >( header.version() ) );
        UTF_REQUIRE_EQUAL( 20U, static_cast< unsigned >( header.headerLength() ) );
        UTF_REQUIRE_EQUAL( 0x10U, static_cast< unsigned >( header.typeOfService() ) );
        UTF_REQUIRE_EQUAL( 20U, static_cast< unsigned >( header.totalLength() ) );
        UTF_REQUIRE_EQUAL( 0xABCDU, static_cast< unsigned >( header.identification() ) );
        UTF_REQUIRE( header.dontFragment() );
        UTF_REQUIRE( ! header.moreFragments() );
        UTF_REQUIRE_EQUAL( 0x0123U, static_cast< unsigned >( header.fragmentOffset() ) );
        UTF_REQUIRE_EQUAL( 64U, static_cast< unsigned >( header.timeToLive() ) );
        UTF_REQUIRE_EQUAL( 1U, static_cast< unsigned >( header.protocol() ) );

        UTF_CHECK_EQUAL( header.sourceAddress().to_string(), std::string( "10.1.2.3" ) );
        UTF_CHECK_EQUAL( header.destinationAddress().to_string(), std::string( "10.4.5.6" ) );
    }

    /*
     * A header which is not IPv4 must be rejected - without this branch Pinger would
     * happily decode a malformed reply
     */

    {
        std::string wrongVersion( wellFormed );
        wrongVersion[ 0 ] = static_cast< char >( 0x65 );

        bl::cpp::SafeInputStringStream is( wrongVersion );

        bl::net::Ipv4Header header;

        is >> header;

        UTF_REQUIRE( ! is );
    }

    /*
     * IHL 4 means a header length of 16, i.e. a negative options length - the branch which
     * keeps operator>> from reading a wrapped, enormous count
     */

    {
        std::string shortHeaderLength( wellFormed );
        shortHeaderLength[ 0 ] = static_cast< char >( 0x44 );

        bl::cpp::SafeInputStringStream is( shortHeaderLength );

        bl::net::Ipv4Header header;

        is >> header;

        UTF_REQUIRE( ! is );
    }

    /*
     * IHL 6 means four bytes of options, which must be consumed as part of the header
     */

    {
        std::string withOptions( reinterpret_cast< const char* >( raw ), 24U );
        withOptions[ 0 ] = static_cast< char >( 0x46 );

        bl::cpp::SafeInputStringStream is( withOptions );

        bl::net::Ipv4Header header;

        is >> header;

        UTF_REQUIRE( ! is.fail() );
        UTF_REQUIRE_EQUAL( 24U, static_cast< unsigned >( header.headerLength() ) );
    }
}
