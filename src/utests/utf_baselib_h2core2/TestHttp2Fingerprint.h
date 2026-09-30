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

#ifndef __UTEST_TESTHTTP2FINGERPRINT_H_
#define __UTEST_TESTHTTP2FINGERPRINT_H_

#include <baselib/http2/Fingerprint.h>
#include <baselib/http2/Session.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeUtils.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * http2/Fingerprint.h - the HTTP/2 fingerprint of a client's opening, notes/plans/http2-design.md
 * 6.4 and 6.6
 *
 * THE BYTES ARE BUILT BY HAND, frames and HPACK alike, and not through FrameCodec or
 * HpackEncoder: a script built with the code the fingerprint reads with would prove only that the
 * two agree. The exception is the point of two cases - that a Session's own output renders as its
 * profile says - where the Session writes the bytes and the expected string is written out by hand
 * from the profile
 *
 * THE EXPECTED STRINGS ARE THE PUBLISHED CONVENTION, and the first case pins it against the
 * strings its source published: Segal, Fridman and Shuster, "Passive Fingerprinting of HTTP/2
 * Clients", Akamai, Black Hat Europe 2017. Fingerprint.h's class note cites it, and records each
 * choice the paper leaves open and where each detector parts from it
 */

namespace utest
{
    namespace h2fingerprint
    {
        typedef bl::http2::Session::wire_buffer_t                   wire_buffer_t;

        enum : std::uint32_t
        {
            /*
             * What requireRefused( ) is told when a refusal is the fingerprint's own and carries
             * no RFC 9113 section 7 code
             */

            NO_ERROR_CODE                                           = 0xFFFFFFFFU,
        };

        inline std::string toText( SAA_in const wire_buffer_t& buffer )
        {
            return buffer.empty() ?
                std::string() :
                std::string( reinterpret_cast< const char* >( &buffer[ 0 ] ), buffer.size() );
        }

        inline std::string octet( SAA_in const std::uint32_t value )
        {
            return std::string( 1U, static_cast< char >( value & 0xFFU ) );
        }

        inline std::string uint32Octets( SAA_in const std::uint32_t value )
        {
            return octet( value >> 24 ) + octet( value >> 16 ) + octet( value >> 8 ) + octet( value );
        }

        /**
         * @brief The nine octet frame header of RFC 9113 4.1 and a payload
         */

        inline std::string makeFrame(
            SAA_in          const std::uint32_t                  type,
            SAA_in          const std::uint32_t                  flags,
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   payload
            )
        {
            const auto length = static_cast< std::uint32_t >( payload.size() );

            return
                octet( length >> 16 ) +
                octet( length >> 8 ) +
                octet( length ) +
                octet( type ) +
                octet( flags ) +
                uint32Octets( streamId ) +
                payload;
        }

        inline bl::http2::Http2Setting setting(
            SAA_in          const std::uint16_t                  id,
            SAA_in          const std::uint32_t                  value
            )
        {
            bl::http2::Http2Setting result;

            result.id = id;
            result.value = value;

            return result;
        }

        inline std::string settingsFrame(
            SAA_in          const std::vector< bl::http2::Http2Setting >&    settings
            )
        {
            std::string payload;

            for( std::size_t i = 0U; i < settings.size(); ++i )
            {
                payload += octet( settings[ i ].id.value() >> 8 );
                payload += octet( settings[ i ].id.value() );
                payload += uint32Octets( settings[ i ].value.value() );
            }

            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_SETTINGS,
                bl::http2::Globals::FRAME_FLAG_NONE,
                0U,
                payload
                );
        }

        inline std::string settingsAckFrame()
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_SETTINGS,
                bl::http2::Globals::FRAME_FLAG_ACK,
                0U,
                std::string()
                );
        }

        inline std::string windowUpdateFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::uint32_t                  increment
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_WINDOW_UPDATE,
                bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                uint32Octets( increment )
                );
        }

        /**
         * @brief A PRIORITY frame - 'weight' is the OCTET, one less than RFC 7540 6.3's weight
         */

        inline std::string priorityFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::uint32_t                  dependency,
            SAA_in          const std::uint32_t                  weight,
            SAA_in          const bool                           exclusive
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_PRIORITY,
                bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                uint32Octets( dependency | ( exclusive ? 0x80000000U : 0U ) ) + octet( weight )
                );
        }

        inline bl::http2::Http2PriorityFrame priority(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::uint32_t                  dependency,
            SAA_in          const std::uint32_t                  weight,
            SAA_in          const bool                           exclusive
            )
        {
            bl::http2::Http2PriorityFrame result;

            result.streamId = streamId;
            result.streamDependency = dependency;
            result.weight = static_cast< std::uint8_t >( weight );
            result.exclusive = exclusive;

            return result;
        }

        /**
         * @brief A HEADERS frame carrying END_STREAM, with neither PADDED nor PRIORITY
         */

        inline std::string headersFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   fragment,
            SAA_in          const bool                           endHeaders
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_HEADERS,
                bl::http2::Globals::FRAME_FLAG_END_STREAM |
                    ( endHeaders ? bl::http2::Globals::FRAME_FLAG_END_HEADERS : 0U ),
                streamId,
                fragment
                );
        }

        /**
         * @brief A HEADERS frame with PADDED, PRIORITY or both - RFC 9113 6.2: the Pad Length
         * octet, the E bit and Stream Dependency, the Weight octet, the fragment, the padding
         */

        inline std::string flaggedHeadersFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   fragment,
            SAA_in          const bool                           endHeaders,
            SAA_in          const bool                           padded,
            SAA_in          const std::uint32_t                  padLength,
            SAA_in          const bool                           hasPriority,
            SAA_in          const std::uint32_t                  dependency,
            SAA_in          const std::uint32_t                  weight,
            SAA_in          const bool                           exclusive
            )
        {
            std::uint32_t flags =
                bl::http2::Globals::FRAME_FLAG_END_STREAM |
                ( endHeaders ? bl::http2::Globals::FRAME_FLAG_END_HEADERS : 0U );

            std::string payload;

            if( padded )
            {
                flags |= bl::http2::Globals::FRAME_FLAG_PADDED;

                payload += octet( padLength );
            }

            if( hasPriority )
            {
                flags |= bl::http2::Globals::FRAME_FLAG_PRIORITY;

                payload += uint32Octets( dependency | ( exclusive ? 0x80000000U : 0U ) );
                payload += octet( weight );
            }

            payload += fragment;

            if( padded )
            {
                payload += std::string( padLength, '\0' );
            }

            return makeFrame( bl::http2::Globals::FRAME_TYPE_HEADERS, flags, streamId, payload );
        }

        inline std::string continuationFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   fragment,
            SAA_in          const bool                           endHeaders
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_CONTINUATION,
                endHeaders ?
                    bl::http2::Globals::FRAME_FLAG_END_HEADERS :
                    bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                fragment
                );
        }

        /*****************************************************************************************
         * HPACK, by hand - RFC 7541 sections 5 and 6
         */

        /**
         * @brief An integer of section 5.1, with 'highBits' already in place above its prefix
         */

        inline std::string hpackInteger(
            SAA_in          const std::uint32_t                  value,
            SAA_in          const std::uint32_t                  prefixBits,
            SAA_in          const std::uint32_t                  highBits
            )
        {
            const std::uint32_t prefixMax = ( 1U << prefixBits ) - 1U;

            if( value < prefixMax )
            {
                return octet( highBits | value );
            }

            std::string out = octet( highBits | prefixMax );

            std::uint32_t rest = value - prefixMax;

            while( rest >= 128U )
            {
                out += octet( 0x80U | ( rest & 0x7FU ) );

                rest >>= 7;
            }

            return out + octet( rest );
        }

        /**
         * @brief A string literal of section 5.2, never Huffman coded
         */

        inline std::string hpackString( SAA_in const std::string& value )
        {
            return hpackInteger( static_cast< std::uint32_t >( value.size() ), 7U, 0x00U ) + value;
        }

        inline std::string indexedField( SAA_in const std::uint32_t index )
        {
            return hpackInteger( index, 7U, 0x80U );                                /* 6.1 */
        }

        inline std::string literalWithIndexedName(
            SAA_in          const std::uint32_t                  nameIndex,
            SAA_in          const std::string&                   value
            )
        {
            return hpackInteger( nameIndex, 4U, 0x00U ) + hpackString( value );     /* 6.2.2 */
        }

        inline std::string literalWithNewName(
            SAA_in          const std::string&                   name,
            SAA_in          const std::string&                   value
            )
        {
            return octet( 0x00U ) + hpackString( name ) + hpackString( value );     /* 6.2.2 */
        }

        inline std::string indexingLiteralWithNewName(
            SAA_in          const std::string&                   name,
            SAA_in          const std::string&                   value
            )
        {
            return octet( 0x40U ) + hpackString( name ) + hpackString( value );     /* 6.2.1 */
        }

        inline std::string sizeUpdate( SAA_in const std::uint32_t size )
        {
            return hpackInteger( size, 5U, 0x20U );                                 /* 6.3 */
        }

        /**
         * @brief A request's pseudo-headers in the order of the letters, the fingerprint's own
         * spelling of them
         *
         * RFC 7541 Appendix A has ":method: GET" at 2, ":path: /" at 4 and ":scheme: https" at 7;
         * ":authority" is a literal on the table's name at 1
         */

        inline std::string requestBlock( SAA_in const std::string& order )
        {
            std::string block;

            for( std::size_t i = 0U; i < order.size(); ++i )
            {
                switch( order[ i ] )
                {
                    case 'm':
                        block += indexedField( 2U );
                        break;

                    case 'a':
                        block += literalWithIndexedName( 1U, "example.com" );
                        break;

                    case 's':
                        block += indexedField( 7U );
                        break;

                    case 'p':
                        block += indexedField( 4U );
                        break;

                    default:
                        UTF_FAIL( "requestBlock( ) knows the letters m, a, s and p only" );
                }
            }

            return block;
        }

        inline std::string preface()
        {
            return bl::http2::Globals::g_connectionPreface;
        }

        /**
         * @brief An opening with every part, both HEADERS flags, and a CONTINUATION whose boundary
         * falls inside a representation - for the cases which cut it into pieces
         */

        inline std::string sampleOpening()
        {
            std::vector< bl::http2::Http2Setting > settings;

            settings.push_back( setting( 1U, 65536U ) );
            settings.push_back( setting( 4U, 131072U ) );
            settings.push_back( setting( 5U, 16384U ) );

            const auto block =
                requestBlock( "mpas" ) + literalWithNewName( "user-agent", "probe/1.0" );

            return
                preface() +
                settingsFrame( settings ) +
                windowUpdateFrame( 0U, 12517377U ) +
                priorityFrame( 3U, 0U, 200U, false ) +
                priorityFrame( 5U, 3U, 100U, true ) +
                flaggedHeadersFrame(
                    13U,
                    block.substr( 0U, 5U ),
                    false /* endHeaders */,
                    true /* padded */,
                    7U,
                    true /* hasPriority */,
                    0U,
                    41U,
                    true /* exclusive */
                    ) +
                continuationFrame( 13U, block.substr( 5U ), true );
        }

        inline std::string sampleFingerprint()
        {
            return "1:65536;4:131072;5:16384|12517377|3:0:0:201,5:1:3:101|m,p,a,s";
        }

        /*****************************************************************************************
         * Driving the fingerprint
         */

        inline const std::uint8_t* octetsOf( SAA_in const std::string& bytes )
        {
            return bytes.empty() ? nullptr : reinterpret_cast< const std::uint8_t* >( bytes.data() );
        }

        inline bl::http2::Http2FingerprintParts parseText( SAA_in const std::string& bytes )
        {
            return bl::http2::Http2Fingerprint::parse( octetsOf( bytes ), bytes.size() );
        }

        inline std::string fingerprintOf( SAA_in const std::string& bytes )
        {
            return bl::http2::Http2Fingerprint::render( parseText( bytes ) );
        }

        inline void feedText(
            SAA_inout       bl::http2::Http2Fingerprint&         fingerprint,
            SAA_in          const std::string&                   bytes
            )
        {
            fingerprint.feed( octetsOf( bytes ), bytes.size() );
        }

        inline void requireSameSettings(
            SAA_in          const std::vector< bl::http2::Http2Setting >&        actual,
            SAA_in          const std::vector< bl::http2::Http2Setting >&        expected
            )
        {
            UTF_REQUIRE_EQUAL( actual.size(), expected.size() );

            for( std::size_t i = 0U; i < actual.size(); ++i )
            {
                UTF_REQUIRE_EQUAL( actual[ i ].id.value(), expected[ i ].id.value() );
                UTF_REQUIRE_EQUAL( actual[ i ].value.value(), expected[ i ].value.value() );
            }
        }

        inline void requireSamePriorities(
            SAA_in          const std::vector< bl::http2::Http2PriorityFrame >&  actual,
            SAA_in          const std::vector< bl::http2::Http2PriorityFrame >&  expected
            )
        {
            UTF_REQUIRE_EQUAL( actual.size(), expected.size() );

            for( std::size_t i = 0U; i < actual.size(); ++i )
            {
                UTF_REQUIRE_EQUAL( actual[ i ].streamId.value(), expected[ i ].streamId.value() );

                UTF_REQUIRE_EQUAL(
                    actual[ i ].streamDependency.value(),
                    expected[ i ].streamDependency.value()
                    );

                UTF_REQUIRE_EQUAL(
                    static_cast< unsigned >( actual[ i ].weight.value() ),
                    static_cast< unsigned >( expected[ i ].weight.value() )
                    );

                UTF_REQUIRE_EQUAL( actual[ i ].exclusive.value(), expected[ i ].exclusive.value() );
            }
        }

        inline void requireSameHeadersPriority(
            SAA_in          const bl::http2::Http2HeadersPriority&               actual,
            SAA_in          const bl::http2::Http2HeadersPriority&               expected
            )
        {
            UTF_REQUIRE_EQUAL( actual.isSet.value(), expected.isSet.value() );
            UTF_REQUIRE_EQUAL( actual.streamDependency.value(), expected.streamDependency.value() );

            UTF_REQUIRE_EQUAL(
                static_cast< unsigned >( actual.weight.value() ),
                static_cast< unsigned >( expected.weight.value() )
                );

            UTF_REQUIRE_EQUAL( actual.exclusive.value(), expected.exclusive.value() );
        }

        inline void requireSameParts(
            SAA_in          const bl::http2::Http2FingerprintParts&              actual,
            SAA_in          const bl::http2::Http2FingerprintParts&              expected
            )
        {
            requireSameSettings( actual.settings, expected.settings );

            UTF_REQUIRE_EQUAL(
                actual.connectionWindowUpdateIncrement.value(),
                expected.connectionWindowUpdateIncrement.value()
                );

            requireSamePriorities( actual.idleStreamPriorities, expected.idleStreamPriorities );

            UTF_REQUIRE( actual.pseudoHeaderOrder == expected.pseudoHeaderOrder );

            requireSameHeadersPriority( actual.headersPriority, expected.headersPriority );
        }

        /**
         * @brief That the bytes are refused, naming the rule and - when the frame codec or HPACK
         * raised it - with the RFC 9113 section 7 code; and refused just the same when they arrive
         * one octet at a time, by the incremental reader, which is then left incomplete
         */

        inline void requireRefused(
            SAA_in          const std::string&                   bytes,
            SAA_in          const std::string&                   reason,
            SAA_in_opt      const std::uint32_t                  expectedCode = NO_ERROR_CODE
            )
        {
            bool refused = false;

            try
            {
                ( void ) parseText( bytes );
            }
            catch( bl::InvalidDataFormatException& e )
            {
                refused = true;

                const std::string message( e.what() );

                if( ! bl::cpp::contains( message, reason ) )
                {
                    UTF_FAIL(
                        BL_MSG()
                            << "The refusal reads '"
                            << message
                            << "' and does not name '"
                            << reason
                            << "'"
                        );
                }

                const auto* const code =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_error_code >( e );

                if( expectedCode == NO_ERROR_CODE )
                {
                    UTF_REQUIRE( code == nullptr );
                }
                else
                {
                    UTF_REQUIRE( code != nullptr );
                    UTF_REQUIRE_EQUAL( *code, expectedCode );
                }
            }

            UTF_REQUIRE( refused );

            bl::http2::Http2Fingerprint byOctet;

            bool refusedByOctet = false;

            for( std::size_t i = 0U; i < bytes.size() && ! refusedByOctet; ++i )
            {
                try
                {
                    byOctet.feed( octetsOf( bytes ) + i, 1U );
                }
                catch( bl::InvalidDataFormatException& e )
                {
                    refusedByOctet = true;

                    UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), reason ) );
                }
            }

            UTF_REQUIRE( refusedByOctet );
            UTF_REQUIRE( ! byOctet.isComplete() );
        }

        /*****************************************************************************************
         * Driving a Session
         */

        inline bl::time::ptime baseTime()
        {
            return bl::time::ptime( bl::time::date( 2026, 9, 30 ) );
        }

        inline bl::http2::SessionRequest makeRequest()
        {
            bl::http2::SessionRequest request;

            request.method = "GET";
            request.scheme = "https";
            request.authority = "example.com";
            request.path = "/";

            return request;
        }

        inline std::string produceText(
            SAA_inout       bl::http2::Session&                  session,
            SAA_in          const bl::time::ptime&               now
            )
        {
            wire_buffer_t out;

            session.produce( out, now );

            return toText( out );
        }

        inline void drainEvents( SAA_inout bl::http2::Session& session )
        {
            while( session.hasEvents() )
            {
                session.popEvent();
            }
        }

        /**
         * @brief The nine octet header of every frame in a buffer, walked by hand - so that a
         * Session case can show which shape of opening the session actually wrote
         */

        struct FrameSummary
        {
            std::uint32_t                                           length;
            std::uint32_t                                           type;
            std::uint32_t                                           flags;
            std::uint32_t                                           streamId;
        };

        inline std::uint32_t octetAt(
            SAA_in          const std::string&                   bytes,
            SAA_in          const std::size_t                    offset
            )
        {
            return static_cast< unsigned char >( bytes[ offset ] );
        }

        inline std::vector< FrameSummary > framesOf( SAA_in const std::string& bytes )
        {
            std::vector< FrameSummary > frames;

            std::size_t offset = 0U;

            while( offset + 9U <= bytes.size() )
            {
                FrameSummary frame = FrameSummary();

                frame.length =
                    ( octetAt( bytes, offset ) << 16 ) |
                    ( octetAt( bytes, offset + 1U ) << 8 ) |
                    octetAt( bytes, offset + 2U );

                frame.type = octetAt( bytes, offset + 3U );
                frame.flags = octetAt( bytes, offset + 4U );

                frame.streamId =
                    (
                        ( octetAt( bytes, offset + 5U ) << 24 ) |
                        ( octetAt( bytes, offset + 6U ) << 16 ) |
                        ( octetAt( bytes, offset + 7U ) << 8 ) |
                        octetAt( bytes, offset + 8U )
                    ) & 0x7FFFFFFFU;

                frames.push_back( frame );

                offset += 9U + frame.length;
            }

            return frames;
        }

        /**
         * @brief A Session configured with the profile writes its opening and its first request,
         * each from a produce( ) call of its own; the fingerprint of those bytes must be the
         * string the profile implies, and each part the profile's own field read back
         *
         * 'settingsSent' and 'orderSent' are what the session puts on the wire for the profile,
         * which is the profile's own list with the session's two defaults applied - D11's 2:0
         * appended when the profile does not name SETTINGS_ENABLE_PUSH, and RFC 9113 8.3.1's
         * order when the profile names none (Session.h, queueOpeningFrames and
         * appendPseudoHeaders)
         */

        inline void requireSessionOpeningRendersAs(
            SAA_in          const bl::http2::Http2Profile&                           profile,
            SAA_in          const std::vector< bl::http2::Http2Setting >&            settingsSent,
            SAA_in          const std::vector< bl::http2::Http2PseudoHeader >&       orderSent,
            SAA_in          const std::string&                                       expected
            )
        {
            const auto now = baseTime();

            bl::http2::Session session( bl::http2::StreamRole::Client, now, profile );

            const auto opening = produceText( session, now );

            ( void ) session.submitRequest( makeRequest() );

            const auto firstRequest = produceText( session, now );

            bl::http2::Http2Fingerprint fingerprint;

            feedText( fingerprint, opening );

            UTF_REQUIRE( ! fingerprint.isComplete() );

            feedText( fingerprint, firstRequest );

            UTF_REQUIRE( fingerprint.isComplete() );

            UTF_REQUIRE_EQUAL( bl::http2::Http2Fingerprint::render( fingerprint.parts() ), expected );

            bl::http2::Http2FingerprintParts fromProfile;

            fromProfile.settings = settingsSent;
            fromProfile.connectionWindowUpdateIncrement = profile.connectionWindowUpdateIncrement;
            fromProfile.idleStreamPriorities = profile.idleStreamPriorities;
            fromProfile.pseudoHeaderOrder = orderSent;
            fromProfile.headersPriority = profile.headersPriority;

            requireSameParts( fingerprint.parts(), fromProfile );

            UTF_REQUIRE_EQUAL( fingerprintOf( opening + firstRequest ), expected );
        }

    } // h2fingerprint

} // utest

/**
 * @brief The convention, pinned against the strings its source published
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_PaperExamplesRenderExactlyTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    /*
     * Firefox's priority tree as the paper lists it (section 3.0) and as Firefox wrote it: the
     * OCTETS 200, 100 and 0, which the paper renders as 201, 101 and 1
     */

    const std::string firefoxTree =
        priorityFrame( 3U, 0U, 200U, false ) +
        priorityFrame( 5U, 0U, 100U, false ) +
        priorityFrame( 7U, 0U, 0U, false ) +
        priorityFrame( 9U, 7U, 0U, false ) +
        priorityFrame( 11U, 3U, 0U, false );

    /*
     * Section 4.0's worked example - Firefox 53 on Mac OS X, the flow of its Image 2 - and the one
     * string the paper gives with all four parts
     */

    {
        std::vector< Http2Setting > settings;

        settings.push_back( setting( 1U, 65536U ) );
        settings.push_back( setting( 4U, 131072U ) );
        settings.push_back( setting( 5U, 16384U ) );

        const auto opening =
            preface() +
            settingsFrame( settings ) +
            windowUpdateFrame( 0U, 12517377U ) +
            firefoxTree +
            headersFrame( 13U, requestBlock( "mpas" ), true );

        UTF_REQUIRE_EQUAL(
            fingerprintOf( opening ),
            std::string(
                "1:65536;4:131072;5:16384|12517377|"
                "3:0:0:201,5:0:0:101,7:0:0:1,9:0:7:1,11:0:3:1|m,p,a,s"
                )
            );
    }

    /*
     * Its sample fingerprints (section 4.0, Examples 1 to 9) have three parts - the pseudo-header
     * order comes after them. Each is built from the values it states and must render to exactly
     * its string, followed by the order the frames were built with: the paper's own table's order
     * for Chrome, curl and Go, and m,a,s,p for the others, of which the table says nothing.
     * Example 2 is Example 1's string again, and Example 5 is the worked example above
     */

    struct Sample
    {
        std::vector< Http2Setting >                         settings;
        std::uint32_t                                       increment;
        bool                                                firefoxPriorities;
        const char*                                         order;
        const char*                                         published;
    };

    std::vector< Sample > samples;

    {
        Sample sample;

        sample.settings.push_back( setting( 1U, 65536U ) );
        sample.settings.push_back( setting( 3U, 1000U ) );
        sample.settings.push_back( setting( 4U, 6291456U ) );
        sample.increment = 15663105U;
        sample.firefoxPriorities = false;
        sample.order = "masp";
        sample.published = "1:65536;3:1000;4:6291456|15663105|0";

        samples.push_back( sample );                            /* 1 - Chrome 58 */
    }

    {
        Sample sample;

        sample.settings.push_back( setting( 3U, 1024U ) );
        sample.settings.push_back( setting( 4U, 10485760U ) );
        sample.increment = 10420225U;
        sample.firefoxPriorities = false;
        sample.order = "masp";
        sample.published = "3:1024;4:10485760|10420225|0";

        samples.push_back( sample );                            /* 3 - Edge 14 */
    }

    {
        Sample sample;

        sample.settings.push_back( setting( 4U, 16777216U ) );
        sample.increment = 16711681U;
        sample.firefoxPriorities = false;
        sample.order = "masp";
        sample.published = "4:16777216|16711681|0";

        samples.push_back( sample );                            /* 4 - OkHttp 3.6.0 */
    }

    {
        Sample sample;

        sample.settings.push_back( setting( 1U, 4096U ) );
        sample.settings.push_back( setting( 4U, 32768U ) );
        sample.settings.push_back( setting( 5U, 16384U ) );
        sample.increment = 12517377U;
        sample.firefoxPriorities = true;
        sample.order = "masp";
        sample.published =
            "1:4096;4:32768;5:16384|12517377|3:0:0:201,5:0:0:101,7:0:0:1,9:0:7:1,11:0:3:1";

        samples.push_back( sample );                            /* 6 - Firefox 53 Android */
    }

    {
        Sample sample;

        sample.settings.push_back( setting( 2U, 0U ) );
        sample.settings.push_back( setting( 4U, 4194304U ) );
        sample.settings.push_back( setting( 6U, 10485760U ) );
        sample.increment = 1073741824U;
        sample.firefoxPriorities = false;
        sample.order = "amps";
        sample.published = "2:0;4:4194304;6:10485760|1073741824|0";

        samples.push_back( sample );                            /* 7 - Go-http-client/2.0 */
    }

    {
        Sample sample;

        sample.settings.push_back( setting( 3U, 100U ) );
        sample.settings.push_back( setting( 4U, 1073741824U ) );
        sample.settings.push_back( setting( 2U, 0U ) );
        sample.increment = 1073676289U;
        sample.firefoxPriorities = false;
        sample.order = "mpsa";
        sample.published = "3:100;4:1073741824;2:0|1073676289|0";

        samples.push_back( sample );                            /* 8 - curl 7.54.0 */
    }

    {
        /*
         * No WINDOW_UPDATE at all - the paper's own '00'
         */

        Sample sample;

        sample.settings.push_back( setting( 3U, 100U ) );
        sample.settings.push_back( setting( 4U, 65535U ) );
        sample.increment = 0U;
        sample.firefoxPriorities = true;
        sample.order = "masp";
        sample.published = "3:100;4:65535|00|3:0:0:201,5:0:0:101,7:0:0:1,9:0:7:1,11:0:3:1";

        samples.push_back( sample );                            /* 9 - nghttp2 1.22.0 */
    }

    for( std::size_t i = 0U; i < samples.size(); ++i )
    {
        const auto& sample = samples[ i ];

        std::string opening = preface() + settingsFrame( sample.settings );

        if( sample.increment != 0U )
        {
            opening += windowUpdateFrame( 0U, sample.increment );
        }

        if( sample.firefoxPriorities )
        {
            opening += firefoxTree;
        }

        opening += headersFrame( 1U, requestBlock( sample.order ), true );

        std::string order;

        for( std::size_t j = 0U; sample.order[ j ] != '\0'; ++j )
        {
            if( j != 0U )
            {
                order += ',';
            }

            order += sample.order[ j ];
        }

        UTF_REQUIRE_EQUAL(
            fingerprintOf( opening ),
            std::string( sample.published ) + "|" + order
            );
    }
}

/**
 * @brief A Session's own opening renders as its profile says, for several shapes of profile
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_SessionOpeningsRenderAsTheirProfilesSayTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    /*
     * The expected strings are written out by hand from each profile. The shapes' values are the
     * design's illustrative ones (6.4), used here as SHAPES and not as any browser's truth
     */

    std::vector< Http2PseudoHeader > rfcOrder;

    rfcOrder.push_back( Http2PseudoHeader::Method );
    rfcOrder.push_back( Http2PseudoHeader::Authority );
    rfcOrder.push_back( Http2PseudoHeader::Scheme );
    rfcOrder.push_back( Http2PseudoHeader::Path );

    /*
     * No profile at all: the session's own 2:0 and nothing else, and RFC 9113 8.3.1's order
     */

    {
        std::vector< Http2Setting > sent;

        sent.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );

        requireSessionOpeningRendersAs( Http2Profile(), sent, rfcOrder, "2:0|00|0|m,a,s,p" );
    }

    /*
     * Chrome's shape: its own 2:0 in its own place, a WINDOW_UPDATE, no PRIORITY frames, and
     * priority fields on HEADERS - weight 256, exclusive - which the parts carry and the string
     * leaves out
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 6291456U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_MAX_HEADER_LIST_SIZE, 262144U ) );

        profile.connectionWindowUpdateIncrement = 15663105U;

        profile.headersPriority.isSet = true;
        profile.headersPriority.streamDependency = 0U;
        profile.headersPriority.weight = 255U;
        profile.headersPriority.exclusive = true;

        profile.pseudoHeaderOrder = rfcOrder;

        requireSessionOpeningRendersAs(
            profile,
            profile.settings,
            rfcOrder,
            "1:65536;2:0;4:6291456;6:262144|15663105|0|m,a,s,p"
            );
    }

    /*
     * PRIORITY frames on idle streams - the old Firefox tree, as octets - and a profile which does
     * not name SETTINGS_ENABLE_PUSH, so that the session's 2:0 goes after its settings: the one
     * place this string differs from the paper's Firefox 53, and D11 is why
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 131072U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_MAX_FRAME_SIZE, 16384U ) );

        profile.connectionWindowUpdateIncrement = 12517377U;

        profile.idleStreamPriorities.push_back( priority( 3U, 0U, 200U, false ) );
        profile.idleStreamPriorities.push_back( priority( 5U, 0U, 100U, false ) );
        profile.idleStreamPriorities.push_back( priority( 7U, 0U, 0U, false ) );
        profile.idleStreamPriorities.push_back( priority( 9U, 7U, 0U, false ) );
        profile.idleStreamPriorities.push_back( priority( 11U, 3U, 0U, false ) );

        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Method );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Path );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Authority );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Scheme );

        auto sent = profile.settings;

        sent.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );

        requireSessionOpeningRendersAs(
            profile,
            sent,
            profile.pseudoHeaderOrder,
            "1:65536;4:131072;5:16384;2:0|12517377|"
                "3:0:0:201,5:0:0:101,7:0:0:1,9:0:7:1,11:0:3:1|m,p,a,s"
            );
    }

    /*
     * No WINDOW_UPDATE: Safari's shape with its increment left at zero, which is how a profile
     * says to send none. Identifiers 8 and 9 are ones the engine only passes through
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_MAX_CONCURRENT_STREAMS, 100U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 2097152U ) );
        profile.settings.push_back( setting( 8U, 1U ) );
        profile.settings.push_back( setting( 9U, 1U ) );

        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Method );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Scheme );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Authority );
        profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Path );

        requireSessionOpeningRendersAs(
            profile,
            profile.settings,
            profile.pseudoHeaderOrder,
            "2:0;3:100;4:2097152;8:1;9:1|00|0|m,s,a,p"
            );
    }
}

/**
 * @brief The openings a Session writes when the first request does not follow its opening at once
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_SessionOpeningEdgeShapesTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    const auto now = baseTime();

    /*
     * THE PEER'S SETTINGS ARRIVE BEFORE THE FIRST REQUEST, as on a real connection they often do.
     * Three things follow, and each is a way to read the opening wrongly:
     *
     *  - our acknowledgement of them is a SETTINGS frame written ahead of the first HEADERS, and
     *    it is not the fingerprint's S part
     *  - the peer's larger SETTINGS_HEADER_TABLE_SIZE lets the encoder raise its table to the
     *    profile's 65536, so the first block OPENS WITH A DYNAMIC TABLE SIZE UPDATE (RFC 7541 4.2)
     *    which the fingerprint must accept - what HPACK_TABLE_SIZE_CEILING is for
     *  - the peer's larger SETTINGS_MAX_FRAME_SIZE lets a large first block go out as ONE HEADERS
     *    frame of more than 16384 octets, which the fingerprint must not refuse for its size
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );
        profile.hpackEncoderTableSize = 65536U;
        profile.connectionWindowUpdateIncrement = 15663105U;

        Session session( StreamRole::Client, now, profile );

        const auto opening = produceText( session, now );

        std::vector< Http2Setting > peerSettings;

        peerSettings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );
        peerSettings.push_back( setting( Globals::SETTINGS_MAX_FRAME_SIZE, 65536U ) );

        session.feed( settingsFrame( peerSettings ), now );

        drainEvents( session );

        UTF_REQUIRE( ! session.isClosed() );

        auto request = makeRequest();

        request.headers.append( "x-large", std::string( 30000U, 'a' ) );

        ( void ) session.submitRequest( request );

        const auto rest = produceText( session, now );

        const auto frames = framesOf( rest );

        UTF_REQUIRE_EQUAL( frames.size(), 2U );

        UTF_REQUIRE_EQUAL(
            frames[ 0 ].type,
            static_cast< std::uint32_t >( Globals::FRAME_TYPE_SETTINGS )
            );

        UTF_REQUIRE_EQUAL( frames[ 0 ].flags, static_cast< std::uint32_t >( Globals::FRAME_FLAG_ACK ) );

        UTF_REQUIRE_EQUAL(
            frames[ 1 ].type,
            static_cast< std::uint32_t >( Globals::FRAME_TYPE_HEADERS )
            );

        UTF_REQUIRE( frames[ 1 ].length > Globals::MAX_FRAME_SIZE_DEFAULT );
        UTF_REQUIRE( 0U != ( frames[ 1 ].flags & Globals::FRAME_FLAG_END_HEADERS ) );

        /*
         * The block's first octet is 001xxxxx, a size update - read past the acknowledgement's
         * nine octets and the HEADERS frame's own nine
         */

        UTF_REQUIRE_EQUAL( octetAt( rest, 9U + 9U ) & 0xE0U, 0x20U );

        const auto parts = parseText( opening + rest );

        UTF_REQUIRE_EQUAL(
            Http2Fingerprint::render( parts ),
            std::string( "1:65536;2:0|15663105|0|m,a,s,p" )
            );

        /*
         * And one octet at a time, the whole of it
         */

        const auto all = opening + rest;

        Http2Fingerprint byOctet;

        for( std::size_t i = 0U; i < all.size(); ++i )
        {
            UTF_REQUIRE( ! byOctet.isComplete() );

            byOctet.feed( octetsOf( all ) + i, 1U );
        }

        UTF_REQUIRE( byOctet.isComplete() );

        requireSameParts( byOctet.parts(), parts );
    }

    /*
     * A SECOND SETTINGS FRAME OF OURS, and a first block larger than the peer's 16384: the
     * session writes the later SETTINGS ahead of the HEADERS, splits the block into HEADERS and
     * CONTINUATION, and takes the HEADERS priority fields out of the first fragment's budget
     */

    {
        Http2Profile profile;

        profile.headersPriority.isSet = true;
        profile.headersPriority.streamDependency = 0U;
        profile.headersPriority.weight = 255U;
        profile.headersPriority.exclusive = true;

        Session session( StreamRole::Client, now, profile );

        const auto opening = produceText( session, now );

        std::vector< Http2Setting > later;

        later.push_back( setting( Globals::SETTINGS_MAX_CONCURRENT_STREAMS, 50U ) );

        session.applyLocalSettings( later );

        auto request = makeRequest();

        request.headers.append( "x-large", std::string( 30000U, 'a' ) );

        ( void ) session.submitRequest( request );

        const auto rest = produceText( session, now );

        const auto frames = framesOf( rest );

        UTF_REQUIRE_EQUAL( frames.size(), 3U );

        UTF_REQUIRE_EQUAL(
            frames[ 0 ].type,
            static_cast< std::uint32_t >( Globals::FRAME_TYPE_SETTINGS )
            );

        UTF_REQUIRE_EQUAL( frames[ 0 ].flags, static_cast< std::uint32_t >( Globals::FRAME_FLAG_NONE ) );

        UTF_REQUIRE_EQUAL(
            frames[ 1 ].type,
            static_cast< std::uint32_t >( Globals::FRAME_TYPE_HEADERS )
            );

        UTF_REQUIRE( 0U != ( frames[ 1 ].flags & Globals::FRAME_FLAG_PRIORITY ) );
        UTF_REQUIRE( 0U == ( frames[ 1 ].flags & Globals::FRAME_FLAG_END_HEADERS ) );

        UTF_REQUIRE_EQUAL(
            frames[ 2 ].type,
            static_cast< std::uint32_t >( Globals::FRAME_TYPE_CONTINUATION )
            );

        UTF_REQUIRE( 0U != ( frames[ 2 ].flags & Globals::FRAME_FLAG_END_HEADERS ) );

        const auto parts = parseText( opening + rest );

        UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), std::string( "2:0|00|0|m,a,s,p" ) );

        requireSameHeadersPriority( parts.headersPriority, profile.headersPriority );
    }
}

/**
 * @brief CONTINUATION, and the PADDED and PRIORITY flags of HEADERS
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_ContinuationPaddingAndPriorityFlagsTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    std::vector< Http2Setting > settings;

    settings.push_back( setting( 1U, 65536U ) );

    const auto head = preface() + settingsFrame( settings ) + windowUpdateFrame( 0U, 15663105U );

    const auto block = requestBlock( "mpas" ) + literalWithNewName( "user-agent", "probe/1.0" );

    const std::string expected( "1:65536|15663105|0|m,p,a,s" );

    UTF_REQUIRE_EQUAL( fingerprintOf( head + headersFrame( 1U, block, true ) ), expected );

    /*
     * The block over a HEADERS and three CONTINUATION frames: a cut inside the :authority
     * literal's value, an empty fragment, which RFC 9113 6.10 permits, and the rest
     */

    {
        const auto parts = parseText(
            head +
            headersFrame( 1U, block.substr( 0U, 5U ), false ) +
            continuationFrame( 1U, block.substr( 5U, 7U ), false ) +
            continuationFrame( 1U, std::string(), false ) +
            continuationFrame( 1U, block.substr( 12U ), true )
            );

        UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), expected );
        UTF_REQUIRE( ! parts.headersPriority.isSet );
    }

    /*
     * PADDED: neither the Pad Length octet nor the padding is part of the block - with ten octets
     * of padding, and with none at all but the flag still set
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf(
            head + flaggedHeadersFrame( 1U, block, true, true, 10U, false, 0U, 0U, false )
            ),
        expected
        );

    UTF_REQUIRE_EQUAL(
        fingerprintOf(
            head + flaggedHeadersFrame( 1U, block, true, true, 0U, false, 0U, 0U, false )
            ),
        expected
        );

    /*
     * PRIORITY: the fields are kept in the parts - the octet as it went, as the profile holds it -
     * and they change nothing in the string
     */

    {
        const auto parts = parseText(
            head + flaggedHeadersFrame( 1U, block, true, false, 0U, true, 0U, 255U, true )
            );

        UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), expected );

        Http2HeadersPriority sent;

        sent.isSet = true;
        sent.streamDependency = 0U;
        sent.weight = 255U;
        sent.exclusive = true;

        requireSameHeadersPriority( parts.headersPriority, sent );
    }

    {
        const auto parts = parseText(
            head + flaggedHeadersFrame( 1U, block, true, false, 0U, true, 3U, 41U, false )
            );

        UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), expected );

        Http2HeadersPriority sent;

        sent.isSet = true;
        sent.streamDependency = 3U;
        sent.weight = 41U;
        sent.exclusive = false;

        requireSameHeadersPriority( parts.headersPriority, sent );
    }

    /*
     * Both flags on a HEADERS frame whose block continues: the fields and the padding are the
     * HEADERS frame's alone, and the CONTINUATION carries fragment only
     */

    {
        const auto parts = parseText(
            head +
            flaggedHeadersFrame( 1U, block.substr( 0U, 3U ), false, true, 4U, true, 0U, 219U, true ) +
            continuationFrame( 1U, block.substr( 3U ), true )
            );

        UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), expected );

        Http2HeadersPriority sent;

        sent.isSet = true;
        sent.streamDependency = 0U;
        sent.weight = 219U;
        sent.exclusive = true;

        requireSameHeadersPriority( parts.headersPriority, sent );
    }
}

/**
 * @brief An opening split anywhere between reads is read the same
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_FramesSplitAcrossReadsTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    const auto opening = sampleOpening();

    const auto whole = parseText( opening );

    UTF_REQUIRE_EQUAL( Http2Fingerprint::render( whole ), sampleFingerprint() );

    /*
     * Every cut into two reads - through the preface, through each nine octet header, through
     * each payload and each HPACK representation
     */

    for( std::size_t cut = 0U; cut <= opening.size(); ++cut )
    {
        Http2Fingerprint fingerprint;

        feedText( fingerprint, opening.substr( 0U, cut ) );
        feedText( fingerprint, opening.substr( cut ) );

        UTF_REQUIRE( fingerprint.isComplete() );

        requireSameParts( fingerprint.parts(), whole );
    }

    /*
     * One octet at a time, complete exactly when the last octet of the first block arrives
     */

    Http2Fingerprint byOctet;

    for( std::size_t i = 0U; i < opening.size(); ++i )
    {
        UTF_REQUIRE( ! byOctet.isComplete() );

        byOctet.feed( octetsOf( opening ) + i, 1U );
    }

    UTF_REQUIRE( byOctet.isComplete() );

    requireSameParts( byOctet.parts(), whole );
}

/**
 * @brief Which frames are the fingerprint's, and how an absent part reads
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_WhichFramesAreReadTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    std::vector< Http2Setting > settings;

    settings.push_back( setting( 1U, 65536U ) );

    const auto head = preface() + settingsFrame( settings );
    const auto request = headersFrame( 1U, requestBlock( "masp" ), true );

    /*
     * S is the first SETTINGS frame, a repeated identifier kept as sent; a later SETTINGS frame
     * and an acknowledgement are not the fingerprint's
     */

    {
        std::vector< Http2Setting > first;

        first.push_back( setting( 1U, 65536U ) );
        first.push_back( setting( 1U, 4096U ) );

        std::vector< Http2Setting > later;

        later.push_back( setting( 3U, 100U ) );

        UTF_REQUIRE_EQUAL(
            fingerprintOf(
                preface() + settingsFrame( first ) + settingsAckFrame() + settingsFrame( later ) +
                request
                ),
            std::string( "1:65536;1:4096|00|0|m,a,s,p" )
            );
    }

    /*
     * An empty SETTINGS frame is an empty S part, and the string then opens with its '|'
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf( preface() + settingsFrame( std::vector< Http2Setting >() ) + request ),
        std::string( "|00|0|m,a,s,p" )
        );

    /*
     * An identifier nothing interprets is an entry like any other - here 43690, 0xAAAA, the one
     * TrackMe's own test pins
     */

    {
        std::vector< Http2Setting > unknown;

        unknown.push_back( setting( 1U, 65536U ) );
        unknown.push_back( setting( 43690U, 0U ) );
        unknown.push_back( setting( 4U, 6291456U ) );

        UTF_REQUIRE_EQUAL(
            fingerprintOf( preface() + settingsFrame( unknown ) + request ),
            std::string( "1:65536;43690:0;4:6291456|00|0|m,a,s,p" )
            );
    }

    /*
     * WU is the first WINDOW_UPDATE on stream 0; one on a stream is not the connection's, and
     * alone it leaves WU absent
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf(
            head +
            windowUpdateFrame( 3U, 300U ) +
            windowUpdateFrame( 0U, 100U ) +
            windowUpdateFrame( 0U, 200U ) +
            request
            ),
        std::string( "1:65536|100|0|m,a,s,p" )
        );

    UTF_REQUIRE_EQUAL(
        fingerprintOf( head + windowUpdateFrame( 3U, 300U ) + request ),
        std::string( "1:65536|00|0|m,a,s,p" )
        );

    /*
     * P is every PRIORITY frame ahead of the first HEADERS frame, in order, whatever lies between
     * them - and what lies between is skipped: PING, a type no RFC defines, an acknowledgement,
     * RST_STREAM, and a PUSH_PROMISE whose block a CONTINUATION ends, which must not be taken for
     * the first header block. A PRIORITY frame after the first block is not read
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf(
            head +
            priorityFrame( 3U, 0U, 200U, false ) +
            makeFrame( Globals::FRAME_TYPE_PING, Globals::FRAME_FLAG_NONE, 0U, std::string( 8U, 'p' ) ) +
            makeFrame( 0xFAU, 0x00U, 0U, "an extension frame" ) +
            settingsAckFrame() +
            priorityFrame( 5U, 3U, 0U, true ) +
            makeFrame( Globals::FRAME_TYPE_RST_STREAM, Globals::FRAME_FLAG_NONE, 7U, uint32Octets( 8U ) ) +
            makeFrame( Globals::FRAME_TYPE_PUSH_PROMISE, Globals::FRAME_FLAG_NONE, 1U, uint32Octets( 2U ) + "x" ) +
            continuationFrame( 1U, "y", true ) +
            priorityFrame( 7U, 0U, 0U, false ) +
            request +
            priorityFrame( 15U, 0U, 0U, false )
            ),
        std::string( "1:65536|00|3:0:0:201,5:1:3:1,7:0:0:1|m,a,s,p" )
        );

    /*
     * PS is the order of what the first block carries. With no :authority the order is still
     * one; a regular field after the pseudo-headers changes nothing; and a block with no field
     * at all leaves PS empty
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf( head + headersFrame( 1U, requestBlock( "msp" ), true ) ),
        std::string( "1:65536|00|0|m,s,p" )
        );

    UTF_REQUIRE_EQUAL(
        fingerprintOf(
            head +
            headersFrame(
                1U,
                requestBlock( "masp" ) + literalWithNewName( "user-agent", "probe/1.0" ),
                true
                )
            ),
        std::string( "1:65536|00|0|m,a,s,p" )
        );

    UTF_REQUIRE_EQUAL(
        fingerprintOf( head + headersFrame( 1U, std::string(), true ) ),
        std::string( "1:65536|00|0|" )
        );

    /*
     * Only the FIRST header block: a second request is not read
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf( head + request + headersFrame( 3U, requestBlock( "pasm" ), true ) ),
        std::string( "1:65536|00|0|m,a,s,p" )
        );

    /*
     * And nothing after it is examined - not even octets no frame reader would accept, fed in
     * the same call or in a later one
     */

    {
        const std::string garbage( 64U, '\xFF' );

        UTF_REQUIRE_EQUAL(
            fingerprintOf( head + request + garbage ),
            std::string( "1:65536|00|0|m,a,s,p" )
            );

        Http2Fingerprint fingerprint;

        feedText( fingerprint, head + request );

        UTF_REQUIRE( fingerprint.isComplete() );

        feedText( fingerprint, garbage );

        UTF_REQUIRE_EQUAL(
            Http2Fingerprint::render( fingerprint.parts() ),
            std::string( "1:65536|00|0|m,a,s,p" )
            );
    }
}

/**
 * @brief Malformed openings are refused with InvalidDataFormatException, naming the rule
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_MalformedOpeningsAreRefusedTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    std::vector< Http2Setting > settings;

    settings.push_back( setting( 1U, 65536U ) );

    const auto S = settingsFrame( settings );
    const auto block = requestBlock( "masp" );
    const auto request = headersFrame( 1U, block, true );

    /*
     * The preface and the SETTINGS frame after it - RFC 9113 3.4. An HTTP/1.1 request and a
     * server's opening, which has no preface, fail the first rule
     */

    {
        auto altered = preface();

        altered[ altered.size() - 1U ] = 'X';

        requireRefused(
            altered + S + request,
            "the opening does not begin with the client connection preface (RFC 9113 3.4)"
            );

        requireRefused(
            "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n",
            "the opening does not begin with the client connection preface"
            );

        requireRefused( S + request, "the opening does not begin with the client connection preface" );
    }

    requireRefused(
        preface() + windowUpdateFrame( 0U, 100U ) + S + request,
        "the opening does not follow its preface with a SETTINGS frame (RFC 9113 3.4)"
        );

    requireRefused(
        preface() + settingsAckFrame() + S + request,
        "the opening does not follow its preface with a SETTINGS frame"
        );

    /*
     * What the frame codec refuses - the rule in its own words and the RFC 9113 section 7 code -
     * including in frames which are no part of the fingerprint, since the codec judges every one
     */

    requireRefused(
        preface() +
        makeFrame( Globals::FRAME_TYPE_SETTINGS, Globals::FRAME_FLAG_NONE, 0U, std::string( 5U, '\0' ) ) +
        request,
        "the payload of a SETTINGS frame must be a multiple of six octets",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    requireRefused(
        preface() + makeFrame( Globals::FRAME_TYPE_SETTINGS, Globals::FRAME_FLAG_NONE, 1U, "" ),
        "a SETTINGS frame applies to the connection as a whole",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    requireRefused(
        preface() + S +
        makeFrame( Globals::FRAME_TYPE_SETTINGS, Globals::FRAME_FLAG_ACK, 0U, std::string( 6U, '\0' ) ) +
        request,
        "a SETTINGS with the ACK flag set frame must carry exactly 0 octets",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    requireRefused(
        preface() + S + priorityFrame( 0U, 0U, 15U, false ) + request,
        "a PRIORITY frame must be associated with a stream",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    requireRefused(
        preface() + S +
        makeFrame( Globals::FRAME_TYPE_WINDOW_UPDATE, Globals::FRAME_FLAG_NONE, 0U, std::string( 3U, '\0' ) ) +
        request,
        "a WINDOW_UPDATE frame must carry exactly 4 octets",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    requireRefused(
        preface() + S +
        makeFrame( Globals::FRAME_TYPE_DATA, Globals::FRAME_FLAG_PADDED, 1U, octet( 5U ) + "ab" ) +
        request,
        "the padding of a DATA frame is longer than the payload which is left to hold it",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    /*
     * A frame of the wrong size for its type is a STREAM error to the codec - consumed and
     * flagged, not thrown - and a refusal here like any other, with the codec's code
     */

    requireRefused(
        preface() + S +
        makeFrame( Globals::FRAME_TYPE_PRIORITY, Globals::FRAME_FLAG_NONE, 3U, std::string( 4U, '\0' ) ) +
        request,
        "the opening carries a frame which is not of the size its type requires",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    requireRefused(
        preface() + S +
        makeFrame( Globals::FRAME_TYPE_DATA, Globals::FRAME_FLAG_PADDED, 1U, std::string() ) +
        request,
        "the opening carries a frame which is not of the size its type requires",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    /*
     * RFC 9113 6.9 - a zero increment on the connection is a connection error, and it would make
     * "none was sent" ambiguous
     */

    requireRefused(
        preface() + S + windowUpdateFrame( 0U, 0U ) + request,
        "the opening sends a connection WINDOW_UPDATE with an increment of zero (RFC 9113 6.9)"
        );

    /*
     * The header block's continuity - RFC 9113 6.10
     */

    requireRefused(
        preface() + S + continuationFrame( 1U, block, true ),
        "a CONTINUATION frame arrived while no header block was open",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    requireRefused(
        preface() + S +
        headersFrame( 1U, block.substr( 0U, 3U ), false ) +
        priorityFrame( 3U, 0U, 15U, false ) +
        continuationFrame( 1U, block.substr( 3U ), true ),
        "a header block is open and what followed it is not a CONTINUATION frame on the same stream",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    requireRefused(
        preface() + S +
        headersFrame( 1U, block.substr( 0U, 3U ), false ) +
        continuationFrame( 3U, block.substr( 3U ), true ),
        "a header block is open and what followed it is not a CONTINUATION frame on the same stream",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    /*
     * The HEADERS frame's own layout - RFC 9113 6.2
     */

    requireRefused(
        preface() + S +
        makeFrame(
            Globals::FRAME_TYPE_HEADERS,
            Globals::FRAME_FLAG_END_HEADERS | Globals::FRAME_FLAG_PADDED,
            1U,
            octet( 200U ) + block
            ),
        "the padding of a HEADERS frame is longer than the payload which is left to hold it",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    requireRefused(
        preface() + S +
        makeFrame(
            Globals::FRAME_TYPE_HEADERS,
            Globals::FRAME_FLAG_END_HEADERS | Globals::FRAME_FLAG_PRIORITY,
            1U,
            std::string( 4U, '\0' )
            ),
        "a HEADERS frame is too small to carry the fields its flags say are present",
        Globals::ERROR_CODE_FRAME_SIZE_ERROR
        );

    requireRefused(
        preface() + S + headersFrame( 0U, block, true ),
        "a HEADERS frame must be associated with a stream",
        Globals::ERROR_CODE_PROTOCOL_ERROR
        );

    /*
     * What HPACK refuses - RFC 7541, and RFC 9113 4.3's COMPRESSION_ERROR
     */

    requireRefused(
        preface() + S + headersFrame( 1U, indexedField( 0U ), true ),
        "an indexed header field representation carries the index 0",
        Globals::ERROR_CODE_COMPRESSION_ERROR
        );

    requireRefused(
        preface() + S + headersFrame( 1U, requestBlock( "m" ) + indexedField( 62U ), true ),
        "an index is beyond the end of the static and dynamic tables",
        Globals::ERROR_CODE_COMPRESSION_ERROR
        );

    requireRefused(
        preface() + S + headersFrame( 1U, requestBlock( "m" ) + octet( 0x01U ) + octet( 11U ) + "exam", true ),
        "a string literal runs past the end of the header block",
        Globals::ERROR_CODE_COMPRESSION_ERROR
        );

    requireRefused(
        preface() + S + headersFrame( 1U, requestBlock( "m" ) + sizeUpdate( 0U ) + requestBlock( "asp" ), true ),
        "a dynamic table size update follows a header field representation",
        Globals::ERROR_CODE_COMPRESSION_ERROR
        );

    /*
     * The pseudo-headers - the rules the session applies to a request it reads (RFC 9113 8.3 and
     * 8.3.1). Only the four have a letter: not ":protocol", not a response's ":status", and not
     * ":Method", which is not ":method"
     */

    requireRefused(
        preface() + S +
        headersFrame( 1U, block + literalWithNewName( ":protocol", "websocket" ), true ),
        "the opening's first header block carries a pseudo-header which is not defined for a "
            "request (RFC 9113 8.3.1)"
        );

    requireRefused(
        preface() + S + headersFrame( 1U, indexedField( 8U ) + block, true ),
        "carries a pseudo-header which is not defined for a request"
        );

    requireRefused(
        preface() + S +
        headersFrame( 1U, literalWithNewName( ":Method", "GET" ) + requestBlock( "asp" ), true ),
        "carries a pseudo-header which is not defined for a request"
        );

    requireRefused(
        preface() + S + headersFrame( 1U, block + indexedField( 3U ), true ),
        "the opening's first header block carries a pseudo-header more than once (RFC 9113 8.3)"
        );

    requireRefused(
        preface() + S +
        headersFrame(
            1U,
            requestBlock( "mas" ) + literalWithNewName( "user-agent", "probe/1.0" ) + requestBlock( "p" ),
            true
            ),
        "the opening's first header block carries a pseudo-header after a regular field "
            "(RFC 9113 8.3)"
        );

    /*
     * A refusal the codec raised carries the codec's own exception, nested, with its code
     */

    {
        bool checked = false;

        try
        {
            ( void ) parseText(
                preface() +
                makeFrame( Globals::FRAME_TYPE_SETTINGS, Globals::FRAME_FLAG_NONE, 0U, std::string( 5U, '\0' ) )
                );
        }
        catch( InvalidDataFormatException& e )
        {
            const auto* const cause = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( cause != nullptr );

            try
            {
                std::rethrow_exception( *cause );
            }
            catch( Http2ProtocolException& codec )
            {
                const auto* const code = eh::get_error_info< eh::errinfo_http2_error_code >( codec );

                UTF_REQUIRE( code != nullptr );

                UTF_REQUIRE_EQUAL(
                    *code,
                    static_cast< std::uint32_t >( Globals::ERROR_CODE_FRAME_SIZE_ERROR )
                    );

                checked = true;
            }
        }

        UTF_REQUIRE( checked );
    }
}

/**
 * @brief Each bound on what is accumulated, one step either side of it
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_BoundsAreEnforcedAtTheirEdgesTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    const auto head = preface() + settingsFrame( std::vector< Http2Setting >() );
    const auto request = headersFrame( 1U, requestBlock( "masp" ), true );

    /*
     * The first block, compressed: 262144 octets are read and one more is refused. The block is
     * dynamic table size updates to zero - one octet each, decoding to nothing, which RFC 7541
     * 4.2 lets open a block - ahead of the pseudo-headers, so that the block at the bound still
     * decodes, carried in frames of 16384 octets
     */

    {
        const std::size_t bound = Http2Fingerprint::MAX_FIRST_HEADER_BLOCK_SIZE;

        UTF_REQUIRE_EQUAL( bound, static_cast< std::size_t >( 262144U ) );

        const auto fields = requestBlock( "masp" );
        const auto block = std::string( bound - fields.size(), '\x20' ) + fields;

        std::string atBound;
        std::string overBound;

        for( std::size_t offset = 0U; offset < block.size(); offset += 16384U )
        {
            const auto fragment = block.substr( offset, 16384U );
            const bool last = offset + 16384U >= block.size();

            atBound += offset == 0U ?
                headersFrame( 1U, fragment, last ) :
                continuationFrame( 1U, fragment, last );

            overBound += offset == 0U ?
                headersFrame( 1U, fragment, false ) :
                continuationFrame( 1U, fragment, false );
        }

        overBound += continuationFrame( 1U, std::string( 1U, '\x20' ), true );

        UTF_REQUIRE_EQUAL( fingerprintOf( head + atBound ), std::string( "|00|0|m,a,s,p" ) );

        requireRefused(
            head + overBound,
            "the opening's first header block is larger than 262144 octets"
            );
    }

    /*
     * The CONTINUATION frames of the first block: 64 are read and a 65th is refused
     */

    {
        const std::size_t bound = Http2Fingerprint::MAX_FIRST_HEADER_BLOCK_CONTINUATIONS;

        std::string atBound = headersFrame( 1U, requestBlock( "masp" ), false );
        std::string overBound = atBound;

        for( std::size_t i = 0U; i < bound; ++i )
        {
            atBound += continuationFrame( 1U, std::string(), i + 1U == bound );
            overBound += continuationFrame( 1U, std::string(), false );
        }

        overBound += continuationFrame( 1U, std::string(), true );

        UTF_REQUIRE_EQUAL( fingerprintOf( head + atBound ), std::string( "|00|0|m,a,s,p" ) );

        requireRefused(
            head + overBound,
            "the opening's first header block is spread over more than 64 CONTINUATION frames"
            );
    }

    /*
     * The PRIORITY frames: as many as the session's own 64 KB control-frame bound admits in an
     * opening, 65536 / 14 = 4681, and one more is refused
     */

    {
        const std::size_t bound = Http2Fingerprint::MAX_IDLE_STREAM_PRIORITIES;

        UTF_REQUIRE_EQUAL( bound, static_cast< std::size_t >( 4681U ) );

        std::string priorities;

        for( std::size_t i = 0U; i < bound; ++i )
        {
            priorities += priorityFrame( static_cast< std::uint32_t >( 2U * i + 3U ), 0U, 15U, false );
        }

        const auto parts = parseText( head + priorities + request );

        UTF_REQUIRE_EQUAL( parts.idleStreamPriorities.size(), bound );
        UTF_REQUIRE_EQUAL( parts.idleStreamPriorities.back().streamId.value(), 2U * 4680U + 3U );

        requireRefused(
            head + priorities + priorityFrame( 1U, 0U, 15U, false ) + request,
            "the opening sends more than 4681 PRIORITY frames ahead of its first HEADERS frame"
            );
    }

    /*
     * The first block, decoded: the shape of an HPACK bomb - one field indexed once, then named
     * by its index 62 an octet at a time. RFC 7541 4.1 counts each field as its name, its value
     * and 32, and 16 of these fields fit in 65536 octets with the pseudo-headers where 17 do not
     */

    {
        const std::string name( "x-bomb" );
        const std::string value( 4000U, 'a' );

        const std::size_t perField = name.size() + value.size() + 32U;

        const std::size_t pseudoHeaders =
            ( 7U + 3U + 32U ) +                                 /* :method GET */
            ( 10U + 11U + 32U ) +                               /* :authority example.com */
            ( 7U + 5U + 32U ) +                                 /* :scheme https */
            ( 5U + 1U + 32U );                                  /* :path / */

        const std::size_t bound = Http2Fingerprint::MAX_DECODED_FIRST_HEADER_BLOCK_SIZE;
        const std::size_t fit = ( bound - pseudoHeaders ) / perField;

        UTF_REQUIRE_EQUAL( fit, static_cast< std::size_t >( 16U ) );

        std::string block = requestBlock( "masp" ) + indexingLiteralWithNewName( name, value );

        for( std::size_t i = 1U; i < fit; ++i )
        {
            block += indexedField( 62U );
        }

        UTF_REQUIRE_EQUAL(
            fingerprintOf( head + headersFrame( 1U, block, true ) ),
            std::string( "|00|0|m,a,s,p" )
            );

        block += indexedField( 62U );

        requireRefused(
            head + headersFrame( 1U, block, true ),
            "the opening's first header block decodes to more than 65536 octets"
            );
    }

    /*
     * The size update the first block may open with: 65536 is accepted and 65537 refused, in
     * HPACK's own words - for which HPACK_TABLE_SIZE_CEILING plays the advertised size
     */

    {
        UTF_REQUIRE_EQUAL(
            fingerprintOf( head + headersFrame( 1U, sizeUpdate( 65536U ) + requestBlock( "masp" ), true ) ),
            std::string( "|00|0|m,a,s,p" )
            );

        requireRefused(
            head + headersFrame( 1U, sizeUpdate( 65537U ) + requestBlock( "masp" ), true ),
            "a dynamic table size update asks for more than was advertised as "
                "SETTINGS_HEADER_TABLE_SIZE",
            Globals::ERROR_CODE_COMPRESSION_ERROR
            );
    }

    /*
     * No frame is refused for its size alone: a frame over RFC 9113's initial 16384, which a
     * client may send once the server allows it, is read past
     */

    UTF_REQUIRE_EQUAL(
        fingerprintOf( head + makeFrame( 0xFAU, 0x00U, 0U, std::string( 100000U, 'e' ) ) + request ),
        std::string( "|00|0|m,a,s,p" )
        );
}

/**
 * @brief Every proper prefix of an opening is truncated - refused by parse( ), incomplete to the
 * incremental reader, which may still be given the rest
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_TruncatedOpeningsAreRefusedTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    const auto opening = sampleOpening();

    UTF_REQUIRE_EQUAL( fingerprintOf( opening ), sampleFingerprint() );

    for( std::size_t length = 0U; length < opening.size(); ++length )
    {
        const auto prefix = opening.substr( 0U, length );

        UTF_REQUIRE_THROW_MESSAGE(
            parseText( prefix ),
            bl::InvalidDataFormatException,
            "The HTTP/2 fingerprint cannot be taken - the opening is truncated: it ends before its "
                "first header block is complete"
            );

        Http2Fingerprint fingerprint;

        feedText( fingerprint, prefix );

        UTF_REQUIRE( ! fingerprint.isComplete() );
    }

    UTF_REQUIRE_THROW_MESSAGE(
        Http2Fingerprint::parse( wire_buffer_t() ),
        bl::InvalidDataFormatException,
        "the opening is truncated"
        );
}

/**
 * @brief The reader's contract, and render( ) over parts built by hand
 */

UTF_AUTO_TEST_CASE( Http2Fingerprint_ReaderContractAndRenderingTests )
{
    using namespace bl::http2;
    using namespace utest::h2fingerprint;

    /*
     * Asking before the first block has been read is a programming error
     */

    {
        Http2Fingerprint fingerprint;

        UTF_REQUIRE( ! fingerprint.isComplete() );

        UTF_REQUIRE_THROW_MESSAGE(
            fingerprint.parts(),
            bl::UnexpectedException,
            "An HTTP/2 fingerprint was asked for before the first header block of the opening had "
                "been read"
            );

        feedText( fingerprint, preface() );

        UTF_REQUIRE_THROW_MESSAGE(
            fingerprint.parts(),
            bl::UnexpectedException,
            "was asked for before the first header block"
            );
    }

    /*
     * A block of no bytes is nothing at all; a non-empty block of none is a programming error
     */

    {
        Http2Fingerprint fingerprint;

        fingerprint.feed( nullptr, 0U );

        UTF_REQUIRE( ! fingerprint.isComplete() );

        UTF_REQUIRE_THROW_MESSAGE(
            fingerprint.feed( nullptr, 1U ),
            bl::UnexpectedException,
            "An HTTP/2 fingerprint was fed a non-empty block of no bytes"
            );
    }

    /*
     * A reader which has refused its input is spent
     */

    {
        Http2Fingerprint fingerprint;

        UTF_REQUIRE_THROW_MESSAGE(
            feedText( fingerprint, "GET / HTTP/1.1\r\n\r\n" ),
            bl::InvalidDataFormatException,
            "the opening does not begin with the client connection preface"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            feedText( fingerprint, preface() ),
            bl::UnexpectedException,
            "An HTTP/2 fingerprint was fed again after it had refused its input"
            );

        UTF_REQUIRE( ! fingerprint.isComplete() );
    }

    /*
     * render( ) over parts built by hand. Nothing at all is every absent part in the paper's
     * spelling, S empty, '00' and '0', PS empty
     */

    Http2FingerprintParts parts;

    UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), std::string( "|00|0|" ) );

    /*
     * An increment is plain decimal whatever its size - where fingerproxy's "%02d" writes "05"
     */

    parts.connectionWindowUpdateIncrement = 5U;

    UTF_REQUIRE_EQUAL( Http2Fingerprint::render( parts ), std::string( "|5|0|" ) );

    /*
     * Every field at its widest: a 16 bit identifier and a 32 bit value, a 31 bit increment, the
     * exclusive bit, 31 bit stream identifiers, and the weight octets 255 and 0 as 256 and 1
     */

    parts.settings.push_back( setting( 65535U, 4294967295U ) );
    parts.connectionWindowUpdateIncrement = 2147483647U;
    parts.idleStreamPriorities.push_back( priority( 2147483647U, 2147483647U, 255U, true ) );
    parts.idleStreamPriorities.push_back( priority( 1U, 0U, 0U, false ) );

    parts.pseudoHeaderOrder.push_back( Http2PseudoHeader::Scheme );
    parts.pseudoHeaderOrder.push_back( Http2PseudoHeader::Path );
    parts.pseudoHeaderOrder.push_back( Http2PseudoHeader::Authority );
    parts.pseudoHeaderOrder.push_back( Http2PseudoHeader::Method );

    UTF_REQUIRE_EQUAL(
        Http2Fingerprint::render( parts ),
        std::string( "65535:4294967295|2147483647|2147483647:1:2147483647:256,1:0:0:1|s,p,a,m" )
        );

    /*
     * A pseudo-header with no letter can only come from parts built by hand, never from the
     * reader, so rendering one is a programming error
     */

    parts.pseudoHeaderOrder.push_back( static_cast< Http2PseudoHeader >( 7U ) );

    UTF_REQUIRE_THROW_MESSAGE(
        Http2Fingerprint::render( parts ),
        bl::UnexpectedException,
        "An HTTP/2 fingerprint was asked to render a pseudo-header it has no letter for"
        );

    /*
     * parse( ) over the wire buffer a Session's produce( ) appends to
     */

    {
        const auto opening = sampleOpening();

        const wire_buffer_t buffer( opening.begin(), opening.end() );

        UTF_REQUIRE_EQUAL(
            Http2Fingerprint::render( Http2Fingerprint::parse( buffer ) ),
            sampleFingerprint()
            );
    }
}

#endif /* __UTEST_TESTHTTP2FINGERPRINT_H_ */
