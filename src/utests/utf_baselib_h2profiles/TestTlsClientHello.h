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

#include <baselib/crypto/TlsClientHello.h>
#include <baselib/crypto/CryptoBase.h>

#include <baselib/tasks/AsioSslStreamWrapper.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/*
 * Tests for the ClientHello parser and the JA3 and JA4 fingerprints of notes/plans/http2-design.md
 * 3.3 and 6.3
 *
 * HOW THE EXPECTED VALUES WERE OBTAINED, because it matters that they are not this code's own
 * output played back:
 *
 *   The two hellos below are built here, field by field, from values written out in the source.
 *   The JA3 string and the three inputs a JA4 is assembled from were then worked out BY HAND from
 *   those same values, following the two definitions, and are written below as literals with the
 *   working shown beside them.
 *
 *   The digests are not hand computable, so they were produced by tools which have nothing to do
 *   with this library, and the exact commands are recorded beside each literal:
 *
 *     printf '%s' '<the JA3 string>'      | md5sum
 *     printf '%s' '<the JA4 hash input>'  | sha256sum
 *
 *   Nothing here asserts a value that this parser produced.
 *
 * The third case takes the opposite direction: it captures a ClientHello which OpenSSL really
 * emitted, through the capture hook of S1.6 that the library ships, and asserts what the parser
 * makes of genuine wire bytes. Hand-worked values are impossible there - the random and the
 * extension set are OpenSSL's - so what it asserts is the semantics
 *
 * This header uses createTestProfile() from TestTlsClientContext.h, which is included before it in
 * the module's Main.cpp. It is deliberately not copied: a helper duplicated across two headers of
 * one module is an ODR violation and invariant C6 catches it (src/utests/AGENTS.md)
 */

namespace utest
{
    namespace tlsclienthello
    {
        typedef std::vector< unsigned char > bytes_t;

        inline void appendUint8( SAA_inout bytes_t& out, SAA_in const unsigned value )
        {
            out.push_back( static_cast< unsigned char >( value & 0xFFU ) );
        }

        inline void appendUint16( SAA_inout bytes_t& out, SAA_in const unsigned value )
        {
            out.push_back( static_cast< unsigned char >( ( value >> 8 ) & 0xFFU ) );
            out.push_back( static_cast< unsigned char >( value & 0xFFU ) );
        }

        inline void appendUint24( SAA_inout bytes_t& out, SAA_in const unsigned value )
        {
            out.push_back( static_cast< unsigned char >( ( value >> 16 ) & 0xFFU ) );
            out.push_back( static_cast< unsigned char >( ( value >> 8 ) & 0xFFU ) );
            out.push_back( static_cast< unsigned char >( value & 0xFFU ) );
        }

        inline void appendBytes( SAA_inout bytes_t& out, SAA_in const bytes_t& tail )
        {
            out.insert( out.end(), tail.cbegin(), tail.cend() );
        }

        inline void appendText( SAA_inout bytes_t& out, SAA_in const std::string& text )
        {
            out.insert( out.end(), text.cbegin(), text.cend() );
        }

        inline auto withUint8Length( SAA_in const bytes_t& body ) -> bytes_t
        {
            bytes_t result;

            appendUint8( result, static_cast< unsigned >( body.size() ) );
            appendBytes( result, body );

            return result;
        }

        inline auto withUint16Length( SAA_in const bytes_t& body ) -> bytes_t
        {
            bytes_t result;

            appendUint16( result, static_cast< unsigned >( body.size() ) );
            appendBytes( result, body );

            return result;
        }

        inline auto makeExtension(
            SAA_in          const unsigned                       type,
            SAA_in          const bytes_t&                       body
            )
            -> bytes_t
        {
            bytes_t result;

            appendUint16( result, type );
            appendBytes( result, withUint16Length( body ) );

            return result;
        }

        /**
         * @brief Assembles a ClientHello handshake message exactly as the capture hook hands one
         * over - the one byte type, the three byte length, then the body
         *
         * The lengths are computed from the content rather than written out, so that the vectors
         * below read as the list of suites and extensions they are meant to be and a hand-counted
         * length cannot be the thing that is wrong
         */

        inline auto makeClientHello(
            SAA_in          const unsigned                       legacyVersion,
            SAA_in          const std::vector< unsigned >&       cipherSuites,
            SAA_in          const std::vector< bytes_t >&        extensions,
            SAA_in          const bool                           includeExtensionBlock = true
            )
            -> bytes_t
        {
            bytes_t body;

            appendUint16( body, legacyVersion );

            for( unsigned i = 0U; i < 32U; ++i )
            {
                appendUint8( body, i );                                 /* random */
            }

            appendUint8( body, 0U );                                    /* legacy_session_id */

            {
                bytes_t suites;

                for( const auto suite : cipherSuites )
                {
                    appendUint16( suites, suite );
                }

                appendBytes( body, withUint16Length( suites ) );
            }

            {
                bytes_t compression;

                appendUint8( compression, 0U );                         /* null */

                appendBytes( body, withUint8Length( compression ) );
            }

            if( includeExtensionBlock )
            {
                bytes_t block;

                for( const auto& extension : extensions )
                {
                    appendBytes( block, extension );
                }

                appendBytes( body, withUint16Length( block ) );
            }

            bytes_t message;

            appendUint8( message, 0x01U );                              /* client_hello */
            appendUint24( message, static_cast< unsigned >( body.size() ) );
            appendBytes( message, body );

            return message;
        }

        inline auto makeServerNameExtension( SAA_in const std::string& hostName ) -> bytes_t
        {
            bytes_t hostBytes;

            appendText( hostBytes, hostName );

            bytes_t entry;

            appendUint8( entry, 0x00U );                                /* name_type = host_name */
            appendBytes( entry, withUint16Length( hostBytes ) );

            return makeExtension( 0x0000U, withUint16Length( entry ) );
        }

        inline auto makeAlpnExtension( SAA_in const std::vector< std::string >& protocols ) -> bytes_t
        {
            bytes_t list;

            for( const auto& protocol : protocols )
            {
                bytes_t one;

                appendText( one, protocol );

                appendBytes( list, withUint8Length( one ) );
            }

            return makeExtension( 0x0010U, withUint16Length( list ) );
        }

        inline auto makeUint16ListExtension(
            SAA_in          const unsigned                       type,
            SAA_in          const std::vector< unsigned >&       values
            )
            -> bytes_t
        {
            bytes_t list;

            for( const auto value : values )
            {
                appendUint16( list, value );
            }

            return makeExtension( type, withUint16Length( list ) );
        }

        inline auto makeSupportedVersionsExtension( SAA_in const std::vector< unsigned >& versions )
            -> bytes_t
        {
            bytes_t list;

            for( const auto version : versions )
            {
                appendUint16( list, version );
            }

            /*
             * The supported_versions extension of a ClientHello is length prefixed with a single
             * byte, unlike the other lists here, which is exactly the kind of detail a parser gets
             * wrong and a hand-built vector catches
             */

            return makeExtension( 0x002BU, withUint8Length( list ) );
        }

        inline auto makeEcPointFormatsExtension( SAA_in const std::vector< unsigned >& formats )
            -> bytes_t
        {
            bytes_t list;

            for( const auto format : formats )
            {
                appendUint8( list, format );
            }

            return makeExtension( 0x000BU, withUint8Length( list ) );
        }

    } // tlsclienthello

} // utest

UTF_AUTO_TEST_CASE( TlsClientHello_Ja3AndJa4MatchHandWorkedValuesTests )
{
    using namespace bl;
    using namespace utest::tlsclienthello;

    /*
     * The vector, written out as what it is:
     *
     *   legacy_version   0x0303
     *   cipher suites    0x1301 0x1302 0xc02b 0xc02f
     *   extensions, in this order:
     *     0x0000 server_name         "example.com"
     *     0x000b ec_point_formats    0x00
     *     0x000a supported_groups    0x001d 0x0017
     *     0x000d signature_algs      0x0403 0x0804
     *     0x0010 alpn                "h2", "http/1.1"
     *     0x002b supported_versions  0x0304 0x0303
     */

    const auto message = makeClientHello(
        0x0303U,
        { 0x1301U, 0x1302U, 0xC02BU, 0xC02FU },
        {
            makeServerNameExtension( "example.com" ),
            makeEcPointFormatsExtension( { 0x00U } ),
            makeUint16ListExtension( 0x000AU, { 0x001DU, 0x0017U } ),
            makeUint16ListExtension( 0x000DU, { 0x0403U, 0x0804U } ),
            makeAlpnExtension( { "h2", "http/1.1" } ),
            makeSupportedVersionsExtension( { 0x0304U, 0x0303U } ),
        }
        );

    const auto info = crypto::TlsClientHello::parse( message );

    /*
     * What the parser found, against what was written above
     */

    UTF_REQUIRE_EQUAL( 0x0303, static_cast< int >( info.legacyVersion.value() ) );
    UTF_REQUIRE_EQUAL( 0x0304, static_cast< int >( info.highestSupportedVersion.value() ) );
    UTF_REQUIRE( info.hasServerName );

    UTF_REQUIRE( info.cipherSuites == std::vector< std::uint16_t >( { 0x1301, 0x1302, 0xC02B, 0xC02F } ) );
    UTF_REQUIRE( info.extensions == std::vector< std::uint16_t >( { 0x0000, 0x000B, 0x000A, 0x000D, 0x0010, 0x002B } ) );
    UTF_REQUIRE( info.supportedGroups == std::vector< std::uint16_t >( { 0x001D, 0x0017 } ) );
    UTF_REQUIRE( info.ecPointFormats == std::vector< std::uint8_t >( { 0x00 } ) );
    UTF_REQUIRE( info.signatureAlgorithms == std::vector< std::uint16_t >( { 0x0403, 0x0804 } ) );
    UTF_REQUIRE( info.alpnProtocols == std::vector< std::string >( { "h2", "http/1.1" } ) );

    /*
     * JA3, worked by hand from the vector above:
     *
     *   version     0x0303                                  -> 771
     *   ciphers     0x1301 0x1302 0xc02b 0xc02f             -> 4865-4866-49195-49199
     *   extensions  0x0000 0x000b 0x000a 0x000d 0x0010 0x002b
     *                                                       -> 0-11-10-13-16-43
     *   groups      0x001d 0x0017                           -> 29-23
     *   formats     0x00                                    -> 0
     *
     * and the digest, from outside this library:
     *
     *   printf '%s' '771,4865-4866-49195-49199,0-11-10-13-16-43,29-23,0' | md5sum
     */

    const std::string expectedJa3( "771,4865-4866-49195-49199,0-11-10-13-16-43,29-23,0" );
    const std::string expectedJa3Hash( "2990dac6608f7ee3d85866e604ab2d2b" );

    UTF_REQUIRE_EQUAL( expectedJa3, crypto::TlsClientHello::computeJa3String( info ) );
    UTF_REQUIRE_EQUAL( expectedJa3Hash, crypto::TlsClientHello::computeJa3Hash( expectedJa3 ) );

    /*
     * JA4, worked by hand from the same vector:
     *
     *   t            over TCP
     *   13           supported_versions carries 0x0304, which wins over legacy_version
     *   d            a server name was sent
     *   04           four cipher suites, none of them GREASE
     *   06           six extensions - server_name and ALPN ARE counted here, although the hash
     *                below leaves them out
     *   h2           first and last character of the first ALPN protocol, "h2"
     *
     *   ciphers, sorted:      1301,1302,c02b,c02f
     *   extensions, sorted, without server_name (0x0000) and ALPN (0x0010):
     *                         000a,000b,000d,002b
     *   signature algorithms, NOT sorted, appended after an underscore:
     *                         0403,0804
     *
     * and the two digests, from outside this library:
     *
     *   printf '%s' '1301,1302,c02b,c02f'            | sha256sum
     *   printf '%s' '000a,000b,000d,002b_0403,0804'  | sha256sum
     *
     * of which JA4 keeps the first twelve characters each.
     */

    UTF_REQUIRE_EQUAL( std::string( "t13d0406h2" ), crypto::TlsClientHello::computeJa4Prefix( info ) );

    UTF_REQUIRE_EQUAL(
        std::string( "1301,1302,c02b,c02f" ),
        crypto::TlsClientHello::computeJa4CipherInput( info )
        );

    UTF_REQUIRE_EQUAL(
        std::string( "000a,000b,000d,002b_0403,0804" ),
        crypto::TlsClientHello::computeJa4ExtensionInput( info )
        );

    UTF_REQUIRE_EQUAL(
        std::string( "t13d0406h2_e00fd9ffaebd_fb71836bce29" ),
        crypto::TlsClientHello::computeJa4( info )
        );

    /*
     * And the one step entry point agrees with the three of them
     */

    const auto fingerprint = crypto::TlsClientHello::computeFingerprint( message );

    UTF_REQUIRE_EQUAL( expectedJa3, fingerprint.ja3 );
    UTF_REQUIRE_EQUAL( expectedJa3Hash, fingerprint.ja3Hash );
    UTF_REQUIRE_EQUAL( std::string( "t13d0406h2_e00fd9ffaebd_fb71836bce29" ), fingerprint.ja4 );
}

UTF_AUTO_TEST_CASE( TlsClientHello_GreaseAndAbsentExtensionsAreHandledTests )
{
    using namespace bl;
    using namespace utest::tlsclienthello;

    /*
     * The second vector is the first one's opposite in every respect that changes an answer:
     *
     *   legacy_version   0x0303, and NO supported_versions extension, so it is what JA4 reports
     *   cipher suites    0x0a0a (GREASE) 0x1301 0xc02b
     *   extensions       0x1a1a (GREASE, empty)
     *                    0x000a supported_groups   0x2a2a (GREASE) 0x001d
     *                    0x000b ec_point_formats   0x00
     *   and no server_name, no ALPN, no signature_algorithms
     */

    const auto message = makeClientHello(
        0x0303U,
        { 0x0A0AU, 0x1301U, 0xC02BU },
        {
            makeExtension( 0x1A1AU, {} ),
            makeUint16ListExtension( 0x000AU, { 0x2A2AU, 0x001DU } ),
            makeEcPointFormatsExtension( { 0x00U } ),
        }
        );

    const auto info = crypto::TlsClientHello::parse( message );

    /*
     * The parse keeps everything as it was sent, GREASE included - it is the fingerprints which
     * drop it, each where its own definition says to
     */

    UTF_REQUIRE( info.cipherSuites == std::vector< std::uint16_t >( { 0x0A0A, 0x1301, 0xC02B } ) );
    UTF_REQUIRE( info.extensions == std::vector< std::uint16_t >( { 0x1A1A, 0x000A, 0x000B } ) );
    UTF_REQUIRE( info.supportedGroups == std::vector< std::uint16_t >( { 0x2A2A, 0x001D } ) );
    UTF_REQUIRE( ! info.hasServerName );
    UTF_REQUIRE( info.alpnProtocols.empty() );
    UTF_REQUIRE( info.signatureAlgorithms.empty() );
    UTF_REQUIRE_EQUAL( 0x0303, static_cast< int >( info.highestSupportedVersion.value() ) );

    /*
     * All sixteen GREASE code points, and the near misses which are not any of them
     */

    UTF_REQUIRE( crypto::TlsClientHello::isGreaseValue( 0x0A0A ) );
    UTF_REQUIRE( crypto::TlsClientHello::isGreaseValue( 0x1A1A ) );
    UTF_REQUIRE( crypto::TlsClientHello::isGreaseValue( 0x2A2A ) );
    UTF_REQUIRE( crypto::TlsClientHello::isGreaseValue( 0xFAFA ) );
    UTF_REQUIRE( ! crypto::TlsClientHello::isGreaseValue( 0x0A0B ) );
    UTF_REQUIRE( ! crypto::TlsClientHello::isGreaseValue( 0x1A2A ) );
    UTF_REQUIRE( ! crypto::TlsClientHello::isGreaseValue( 0x1301 ) );
    UTF_REQUIRE( ! crypto::TlsClientHello::isGreaseValue( 0x0000 ) );

    /*
     * JA3, worked by hand:
     *
     *   version     771
     *   ciphers     0x0a0a dropped; 0x1301 0xc02b   -> 4865-49195
     *   extensions  0x1a1a dropped; 0x000a 0x000b   -> 10-11
     *   groups      0x2a2a dropped; 0x001d          -> 29
     *   formats     0x00                            -> 0
     *
     *   printf '%s' '771,4865-49195,10-11,29,0' | md5sum
     */

    const std::string expectedJa3( "771,4865-49195,10-11,29,0" );

    UTF_REQUIRE_EQUAL( expectedJa3, crypto::TlsClientHello::computeJa3String( info ) );

    UTF_REQUIRE_EQUAL(
        std::string( "d19247c417933029394ec08e89b4ac96" ),
        crypto::TlsClientHello::computeJa3Hash( expectedJa3 )
        );

    /*
     * JA4, worked by hand:
     *
     *   t            over TCP
     *   12           no supported_versions, so legacy_version 0x0303 decides
     *   i            no server name
     *   02           two cipher suites after GREASE is dropped
     *   02           two extensions after GREASE is dropped
     *   00           no ALPN at all
     *
     *   ciphers, sorted:     1301,c02b
     *   extensions, sorted:  000a,000b - and with NO trailing underscore, because there is no
     *                        signature_algorithms extension to append
     *
     *   printf '%s' '1301,c02b' | sha256sum
     *   printf '%s' '000a,000b' | sha256sum
     */

    UTF_REQUIRE_EQUAL( std::string( "t12i020200" ), crypto::TlsClientHello::computeJa4Prefix( info ) );

    UTF_REQUIRE_EQUAL( std::string( "1301,c02b" ), crypto::TlsClientHello::computeJa4CipherInput( info ) );
    UTF_REQUIRE_EQUAL( std::string( "000a,000b" ), crypto::TlsClientHello::computeJa4ExtensionInput( info ) );

    UTF_REQUIRE_EQUAL(
        std::string( "t12i020200_777cda164f4b_33a13ba74d1c" ),
        crypto::TlsClientHello::computeJa4( info )
        );

    /*
     * A hello with no extension block at all - which is what a client predating TLS 1.2 sends,
     * and which is a different encoding from an empty block rather than the same thing written
     * twice. Its empty extension section hashes to twelve zeros rather than to the hash of the
     * empty string, so that "nothing there" reads as nothing
     *
     *   legacy_version 0x0301 -> 769, and "10" to JA4
     *   one suite 0x002f -> 47
     *
     *   printf '%s' '002f' | sha256sum
     */

    const auto bare = makeClientHello( 0x0301U, { 0x002FU }, {}, false /* includeExtensionBlock */ );

    const auto bareInfo = crypto::TlsClientHello::parse( bare );

    UTF_REQUIRE( bareInfo.extensions.empty() );
    UTF_REQUIRE_EQUAL( std::string( "769,47,,," ), crypto::TlsClientHello::computeJa3String( bareInfo ) );
    UTF_REQUIRE_EQUAL(
        std::string( "t10i010000_ba72b8082249_000000000000" ),
        crypto::TlsClientHello::computeJa4( bareInfo )
        );
}

UTF_AUTO_TEST_CASE( TlsClientHello_CapturedFromRealHandshakeTests )
{
    using namespace bl;
    using namespace utest::tlsclienthello;

    /*
     * The other direction: a ClientHello OpenSSL really produced, captured through the hook the
     * library ships rather than through anything this test installed
     *
     * No peer is needed. ::SSL_do_handshake on a client which has never been connected writes its
     * ClientHello into the stream's own memory BIO and then asks for a read it will never get; the
     * message callback has already fired by then, which is the whole point of capturing on write
     */

    const auto clientContext =
        crypto::CryptoBase::createAsioSslClientContext( utest::tlsclientctx::createTestProfile() );

    asio::io_service ioService;

    tasks::AsioSslStreamWrapper wrapper( ioService, "example.com", "443", *clientContext );

    wrapper.enableClientHelloCapture();

    wrapper.setAlpnProtocolOffer( { "h2", "http/1.1" } );

    ::SSL* const ssl = wrapper.getStream().native_handle();

    BL_CHK_CRYPTO_API_NM( ::SSL_set_tlsext_host_name( ssl, "example.com" ) );

    ::SSL_set_connect_state( ssl );

    ( void ) ::SSL_do_handshake( ssl );

    ( void ) ::ERR_clear_error();

    const auto& captured = wrapper.getCapturedClientHello();

    UTF_REQUIRE( ! captured.empty() );

    /*
     * The hook is supposed to keep the ClientHello and nothing else
     */

    UTF_REQUIRE_EQUAL( 0x01, static_cast< int >( captured.front() ) );

    const auto info = crypto::TlsClientHello::parse( captured );

    /*
     * What a TLS 1.3 capable OpenSSL client sends: legacy_version pinned at 0x0303 with the real
     * one in supported_versions, the server name we set, and the ALPN offer in the order it was
     * given
     */

    UTF_REQUIRE_EQUAL( 0x0303, static_cast< int >( info.legacyVersion.value() ) );
    UTF_REQUIRE_EQUAL( 0x0304, static_cast< int >( info.highestSupportedVersion.value() ) );
    UTF_REQUIRE( info.hasServerName );
    UTF_REQUIRE( info.alpnProtocols == std::vector< std::string >( { "h2", "http/1.1" } ) );
    UTF_REQUIRE( ! info.supportedGroups.empty() );
    UTF_REQUIRE( ! info.signatureAlgorithms.empty() );

    /*
     * Every suite the profile named is offered, in the profile's own order within each family.
     * The list is not required to be exactly those five, because whether OpenSSL also offers the
     * empty renegotiation SCSV is its business and not this test's
     */

    const auto profile = utest::tlsclientctx::createTestProfile();

    UTF_REQUIRE(
        info.cipherSuites.size() >= profile.cipherSuitesTls12.size() + profile.cipherSuitesTls13.size()
        );

    /*
     * And the fingerprints of it have the shape both definitions give them: a JA3 which starts at
     * the ClientHello version, a 32 character MD5, and a JA4 of ten characters and two twelve
     * character sections
     */

    const auto fingerprint = crypto::TlsClientHello::computeFingerprint( captured );

    UTF_REQUIRE( 0U == fingerprint.ja3.find( "771," ) );
    UTF_REQUIRE_EQUAL( 32U, fingerprint.ja3Hash.size() );
    UTF_REQUIRE_EQUAL( fingerprint.ja3Hash, crypto::TlsClientHello::computeJa3Hash( fingerprint.ja3 ) );

    UTF_REQUIRE_EQUAL( 36U, fingerprint.ja4.size() );
    UTF_REQUIRE_EQUAL( std::string( "t13d" ), fingerprint.ja4.substr( 0U, 4U ) );
    UTF_REQUIRE_EQUAL( std::string( "h2" ), fingerprint.ja4.substr( 8U, 2U ) );
    UTF_REQUIRE_EQUAL( '_', fingerprint.ja4[ 10 ] );
    UTF_REQUIRE_EQUAL( '_', fingerprint.ja4[ 23 ] );

    /*
     * Printed because the fidelity spike of design 6.3 is going to want them, and a run of this
     * module is the cheapest place to read them off
     */

    UTF_MESSAGE( BL_MSG() << "captured ClientHello: " << captured.size() << " bytes" );
    UTF_MESSAGE( BL_MSG() << "JA3:      " << fingerprint.ja3 );
    UTF_MESSAGE( BL_MSG() << "JA3 hash: " << fingerprint.ja3Hash );
    UTF_MESSAGE( BL_MSG() << "JA4:      " << fingerprint.ja4 );
}

UTF_AUTO_TEST_CASE( TlsClientHello_MalformedMessagesAreRefusedTests )
{
    using namespace bl;
    using namespace utest::tlsclienthello;

    typedef crypto::TlsClientHello TlsClientHello;

    UTF_REQUIRE_THROW( TlsClientHello::parse( bytes_t() ), InvalidDataFormatException );

    /*
     * A handshake message which is not a ClientHello is refused rather than parsed as one; the
     * capture hook already filters by type, so this is the parser refusing to trust that
     */

    {
        auto message = makeClientHello( 0x0303U, { 0x1301U }, {} );

        message[ 0 ] = 0x02;                                    /* server_hello */

        UTF_REQUIRE_THROW( TlsClientHello::parse( message ), InvalidDataFormatException );
    }

    /*
     * Every proper prefix of a well formed message is refused. This is the whole bounds check in
     * one assertion: a length field which promises more than is there must stop the parse at the
     * field which promised it, wherever in the message that field happens to be
     */

    const auto message = makeClientHello(
        0x0303U,
        { 0x1301U, 0xC02BU },
        {
            makeServerNameExtension( "example.com" ),
            makeUint16ListExtension( 0x000AU, { 0x001DU } ),
            makeAlpnExtension( { "h2" } ),
            makeSupportedVersionsExtension( { 0x0304U } ),
        }
        );

    UTF_REQUIRE_NO_THROW( TlsClientHello::parse( message ) );

    for( std::size_t length = 0U; length < message.size(); ++length )
    {
        const bytes_t prefix( message.cbegin(), message.cbegin() + static_cast< std::ptrdiff_t >( length ) );

        UTF_CHECK_THROW( TlsClientHello::parse( prefix ), InvalidDataFormatException );
    }

    /*
     * And a length field which promises less than is there does not let the remainder be read as
     * something it is not: the body length is shortened by one, which cuts the last extension in
     * half, and the parse stops rather than running into the bytes beyond it
     */

    {
        auto shortened = message;

        shortened[ 3 ] = static_cast< unsigned char >( shortened[ 3 ] - 1U );

        UTF_REQUIRE_THROW( TlsClientHello::parse( shortened ), InvalidDataFormatException );
    }
}
