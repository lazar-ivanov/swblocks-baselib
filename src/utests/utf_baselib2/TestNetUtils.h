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

    /*
     * DOCUMENTED DEFECT - the port is parsed with std::stoul( ... ) and the consumed
     * length out-parameter is never requested, so std::stoul's permissive grammar leaks
     * straight through: leading whitespace is skipped, a sign is accepted and everything
     * after the digits is silently ignored
     *
     * tryParseEndpoint( ... ) is the parser for operator supplied endpoints
     * ( MessagingUtils.h and ForwardingBackendProcessingFactory.h feed it configuration
     * strings ), so each of the four shapes below is a configuration validation hole which
     * surfaces as a connection to the wrong port rather than as an error
     *
     * The one line fix is to pass a std::size_t out-parameter to std::stoul and require
     * that it consumed the whole port substring. The block below pins what the code does
     * TODAY so that, when the parser is tightened, this is the single place where the
     * expectations flip - and until then it proves nobody widened the acceptance further
     *
     * Note also that std::out_of_range is NOT caught by the catch( std::invalid_argument& )
     * below the std::stoul call - it is unreachable today only because the port substring
     * is capped at 5 characters ( the "host:123456" guard above ), i.e. at most 99999,
     * which always fits an unsigned long. Removing that length gate without adding an
     * out_of_range catch would turn a malformed configuration into an uncaught exception
     */

    UTF_CHECK( bl::net::tryParseEndpoint( "host:12ab", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 12 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:+80", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 80 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host: 80", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 80 );

    UTF_CHECK( bl::net::tryParseEndpoint( "host:0x10", host, port ) );
    UTF_CHECK_EQUAL( host, "host" );
    UTF_CHECK_EQUAL( port, 0 );
}

UTF_AUTO_TEST_CASE( NetUtils_RemoteEndpointIdTests )
{
    /*
     * The "host:port" shape is what every endpoint id in the logs and in MessagingUtils /
     * ForwardingBackendProcessingFactory is built from
     */

    UTF_CHECK_EQUAL( bl::net::formatEndpointId( "host", 8080 ), std::string( "host:8080" ) );

    bl::asio::io_service ioService;

    /*
     * remoteEndpointIdNoWait( ... ) exists precisely so it can be called from a context
     * which must not block or throw - e.g. a task continuation, which runs while the
     * execution queue lock is held - so the "never throws on a bad socket" contract is the
     * load bearing assertion here; a socket which was never opened is the cheapest way to
     * make socket.remote_endpoint( ec ) fail deterministically
     */

    bl::net::tcp::socket unopened( ioService );

    std::string unopenedId;

    UTF_REQUIRE_NO_THROW( unopenedId = bl::net::remoteEndpointIdNoWait( unopened ) );

    UTF_CHECK_EQUAL( std::string( "<unknown>" ), unopenedId );

    /*
     * A loopback round trip - no external DNS, no sleeps, no fixed ports and nothing
     * asynchronous, so no io_service::run() is required. The templated
     * formatEndpointId( endpoint ) overload is exercised through the call below
     */

    bl::net::tcp::acceptor acceptor(
        ioService,
        bl::net::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral port */ )
        );

    const auto port = acceptor.local_endpoint().port();

    bl::net::tcp::socket client( ioService );

    client.connect( bl::net::tcp::endpoint( bl::asio::ip::address_v4::loopback(), port ) );

    bl::net::tcp::socket peer( ioService );

    acceptor.accept( peer );

    UTF_CHECK_EQUAL(
        bl::net::remoteEndpointIdNoWait( client ),
        bl::net::formatEndpointId( "127.0.0.1", port )
        );

    client.close();
    peer.close();
    acceptor.close();
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

        /*
         * computeChecksum( ... ) zeroes the field before summing, so recomputing it on a
         * header which already carries one - i.e. a header which is reused rather than
         * built fresh, which is what every caller other than the pinger would do - yields
         * exactly the same value
         */

        header.computeChecksum( body.begin(), body.end() );

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
