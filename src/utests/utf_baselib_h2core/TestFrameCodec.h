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

#ifndef __UTEST_TESTFRAMECODEC_H_
#define __UTEST_TESTFRAMECODEC_H_

#include <baselib/http2/FrameCodec.h>
#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2 frame codec of RFC 9113 sections 4 and 6 - notes/plans/http2-design.md 4.1
 *
 * Every frame in this file is written as literal octets rather than being produced by the
 * serializer, because a test which builds its input with the code under test cannot see a byte
 * order defect: the encoder and the decoder would cancel out. The serializer is checked the same
 * way, against literal octets, in the other direction
 *
 * The incremental cases are the reason this file is as long as it is. A nine octet frame header
 * can arrive as nine separate reads, and so can a payload; feeding whole frames proves nothing
 * about either, so the same byte script is fed whole, one octet at a time, in a repeating chunk
 * pattern, and at every single one of the 133 places it can be cut in two
 */

namespace utest
{
    namespace framecodec
    {
        /**
         * @brief A frame taken out of the reader and copied, so it outlives the reader's buffer
         *
         * The view a reader hands out borrows its payload from the reader, and that is exactly
         * the lifetime rule which has to be respected here too
         */

        struct CapturedFrame
        {
            std::uint32_t                                       length;
            std::uint8_t                                        type;
            std::uint8_t                                        flags;
            std::uint32_t                                       streamId;
            std::uint32_t                                       streamErrorCode;
            std::vector< std::uint8_t >                         payload;
        };

        inline void appendBytes(
            SAA_inout       std::vector< std::uint8_t >&         out,
            SAA_in          std::initializer_list< int >         values
            )
        {
            for( const auto value : values )
            {
                out.push_back( static_cast< std::uint8_t >( value ) );
            }
        }

        inline std::vector< std::uint8_t > makeBytes( SAA_in std::initializer_list< int > values )
        {
            std::vector< std::uint8_t > result;

            appendBytes( result, values );

            return result;
        }

        inline std::vector< std::uint8_t > toVector(
            SAA_in_opt      const std::uint8_t*                  data,
            SAA_in          const std::size_t                    size
            )
        {
            return size != 0U ?
                std::vector< std::uint8_t >( data, data + size ) :
                std::vector< std::uint8_t >();
        }

        inline CapturedFrame capture( SAA_in const bl::http2::FrameView& frame )
        {
            CapturedFrame captured;

            captured.length = frame.header.length.value();
            captured.type = frame.header.type.value();
            captured.flags = frame.header.flags.value();
            captured.streamId = frame.header.streamId.value();
            captured.streamErrorCode = frame.streamErrorCode.value();
            captured.payload = toVector( frame.payload.value(), frame.payloadSize.value() );

            return captured;
        }

        inline bool sameFrame(
            SAA_in          const CapturedFrame&                 lhs,
            SAA_in          const CapturedFrame&                 rhs
            ) NOEXCEPT
        {
            return
                lhs.length == rhs.length &&
                lhs.type == rhs.type &&
                lhs.flags == rhs.flags &&
                lhs.streamId == rhs.streamId &&
                lhs.streamErrorCode == rhs.streamErrorCode &&
                lhs.payload == rhs.payload;
        }

        inline bool sameFrames(
            SAA_in          const std::vector< CapturedFrame >&  lhs,
            SAA_in          const std::vector< CapturedFrame >&  rhs
            ) NOEXCEPT
        {
            if( lhs.size() != rhs.size() )
            {
                return false;
            }

            for( std::size_t i = 0U; i < lhs.size(); ++i )
            {
                if( ! sameFrame( lhs[ i ], rhs[ i ] ) )
                {
                    return false;
                }
            }

            return true;
        }

        /**
         * @brief Feeds a sequence of blocks to a reader and collects every frame which comes out
         *
         * This is the drain loop of FrameReaderT's own documentation, and it is deliberately the
         * only one used here: a block boundary is a read boundary, and the whole point of the
         * incremental cases is that the loop does not care where they fall
         */

        inline std::vector< CapturedFrame > readBlocks(
            SAA_inout       bl::http2::FrameReader&                         reader,
            SAA_in          const std::vector< std::vector< std::uint8_t > >& blocks
            )
        {
            std::vector< CapturedFrame > frames;

            for( std::size_t block = 0U; block < blocks.size(); ++block )
            {
                const auto& bytes = blocks[ block ];

                std::size_t offset = 0U;

                while( offset < bytes.size() )
                {
                    offset += reader.feed( &bytes[ offset ], bytes.size() - offset );

                    if( reader.hasFrame() )
                    {
                        frames.push_back( capture( reader.frame() ) );

                        reader.consumeFrame();
                    }
                }
            }

            return frames;
        }

        inline std::vector< std::vector< std::uint8_t > > asOneBlock(
            SAA_in          const std::vector< std::uint8_t >&    wire
            )
        {
            return std::vector< std::vector< std::uint8_t > >( 1U, wire );
        }

        inline std::vector< std::vector< std::uint8_t > > asChunks(
            SAA_in          const std::vector< std::uint8_t >&    wire,
            SAA_in          const std::size_t                     chunkSize
            )
        {
            std::vector< std::vector< std::uint8_t > > blocks;

            std::size_t offset = 0U;

            while( offset < wire.size() )
            {
                const auto take = std::min( chunkSize, wire.size() - offset );

                blocks.push_back(
                    std::vector< std::uint8_t >(
                        wire.begin() + static_cast< std::ptrdiff_t >( offset ),
                        wire.begin() + static_cast< std::ptrdiff_t >( offset + take )
                        )
                    );

                offset += take;
            }

            return blocks;
        }

        inline std::vector< std::vector< std::uint8_t > > asSplit(
            SAA_in          const std::vector< std::uint8_t >&    wire,
            SAA_in          const std::size_t                     at
            )
        {
            std::vector< std::vector< std::uint8_t > > blocks;

            blocks.push_back(
                std::vector< std::uint8_t >(
                    wire.begin(),
                    wire.begin() + static_cast< std::ptrdiff_t >( at )
                    )
                );

            blocks.push_back(
                std::vector< std::uint8_t >(
                    wire.begin() + static_cast< std::ptrdiff_t >( at ),
                    wire.end()
                    )
                );

            return blocks;
        }

        template
        <
            typename CALLABLE
        >
        void requireConnectionError(
            SAA_in          const CALLABLE&                      callable,
            SAA_in          const std::uint32_t                  expectedErrorCode
            )
        {
            bool caught = false;

            try
            {
                callable();
            }
            catch( bl::Http2ProtocolException& e )
            {
                caught = true;

                const auto* const errorCode =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_error_code >( e );

                UTF_REQUIRE( errorCode != nullptr );
                UTF_REQUIRE_EQUAL( *errorCode, expectedErrorCode );
            }

            UTF_REQUIRE( caught );
        }

        template
        <
            typename CALLABLE
        >
        void requireStreamError(
            SAA_in          const CALLABLE&                      callable,
            SAA_in          const std::uint32_t                  expectedErrorCode,
            SAA_in          const std::uint32_t                  expectedStreamId
            )
        {
            bool caught = false;

            try
            {
                callable();
            }
            catch( bl::Http2StreamException& e )
            {
                caught = true;

                const auto* const errorCode =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_error_code >( e );

                const auto* const streamId =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_stream_id >( e );

                UTF_REQUIRE( errorCode != nullptr );
                UTF_REQUIRE_EQUAL( *errorCode, expectedErrorCode );
                UTF_REQUIRE( streamId != nullptr );
                UTF_REQUIRE_EQUAL( *streamId, expectedStreamId );
            }

            UTF_REQUIRE( caught );
        }

        /**
         * @brief Reads one frame out of a complete, well formed block of octets
         */
        inline CapturedFrame readOneFrame( SAA_in const std::vector< std::uint8_t >& wire )
        {
            bl::http2::FrameReader reader;

            const auto frames = readBlocks( reader, asOneBlock( wire ) );

            UTF_REQUIRE_EQUAL( frames.size(), 1U );

            return frames[ 0 ];
        }

        /**
         * @brief A frame view over a caller owned buffer, for the payload accessors
         */

        inline bl::http2::FrameView viewOf( SAA_in const std::vector< std::uint8_t >& wire )
        {
            UTF_REQUIRE( wire.size() >= bl::http2::Globals::FRAME_HEADER_SIZE );

            bl::http2::FrameView view;

            view.header = bl::http2::FrameCodec::parseFrameHeader( &wire[ 0 ] );

            const auto payloadSize = wire.size() - bl::http2::Globals::FRAME_HEADER_SIZE;

            view.payloadSize = payloadSize;
            view.payload = payloadSize != 0U ?
                &wire[ bl::http2::Globals::FRAME_HEADER_SIZE ] : nullptr;

            return view;
        }

        /**
         * @brief The nine frames of the incremental byte script, written out as octets
         *
         * Chosen for what it puts on a read boundary rather than for what it means: two frames of
         * length zero - a SETTINGS acknowledgement and an empty DATA frame with END_STREAM - are
         * in it precisely because a frame whose payload never arrives is where an incremental
         * parser which waits for "some payload" hangs forever
         */

        inline std::vector< std::uint8_t > byteScript()
        {
            std::vector< std::uint8_t > wire;

            /*
             * SETTINGS, two entries - MAX_CONCURRENT_STREAMS = 100, INITIAL_WINDOW_SIZE = 65535
             */

            appendBytes( wire, { 0x00, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            appendBytes( wire, { 0x00, 0x03, 0x00, 0x00, 0x00, 0x64 } );
            appendBytes( wire, { 0x00, 0x04, 0x00, 0x00, 0xff, 0xff } );

            /*
             * SETTINGS with the ACK flag - a frame of length zero
             */

            appendBytes( wire, { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } );

            /*
             * HEADERS on stream 1, END_STREAM | END_HEADERS, field block "abcde"
             */

            appendBytes( wire, { 0x00, 0x00, 0x05, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01 } );
            appendBytes( wire, { 0x61, 0x62, 0x63, 0x64, 0x65 } );

            /*
             * DATA on stream 1, END_STREAM | PADDED, pad length 4, data "xyz"
             */

            appendBytes( wire, { 0x00, 0x00, 0x08, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01 } );
            appendBytes( wire, { 0x04, 0x78, 0x79, 0x7a, 0x00, 0x00, 0x00, 0x00 } );

            /*
             * PING, eight opaque octets
             */

            appendBytes( wire, { 0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            appendBytes( wire, { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 } );

            /*
             * WINDOW_UPDATE on the connection, increment 65535
             */

            appendBytes( wire, { 0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            appendBytes( wire, { 0x00, 0x00, 0xff, 0xff } );

            /*
             * DATA on stream 3 with END_STREAM and no payload at all
             */

            appendBytes( wire, { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03 } );

            /*
             * GOAWAY, last stream 3, ENHANCE_YOUR_CALM, debug data "no"
             */

            appendBytes( wire, { 0x00, 0x00, 0x0a, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 } );
            appendBytes( wire, { 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0b, 0x6e, 0x6f } );

            /*
             * RST_STREAM on stream 1, CANCEL
             */

            appendBytes( wire, { 0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01 } );
            appendBytes( wire, { 0x00, 0x00, 0x00, 0x08 } );

            return wire;
        }

        inline std::vector< CapturedFrame > byteScriptFrames()
        {
            using namespace bl::http2;

            std::vector< CapturedFrame > frames;

            const std::uint32_t lengths[] = { 12U, 0U, 5U, 8U, 8U, 4U, 0U, 10U, 4U };
            const std::uint8_t types[] =
            {
                Globals::FRAME_TYPE_SETTINGS,
                Globals::FRAME_TYPE_SETTINGS,
                Globals::FRAME_TYPE_HEADERS,
                Globals::FRAME_TYPE_DATA,
                Globals::FRAME_TYPE_PING,
                Globals::FRAME_TYPE_WINDOW_UPDATE,
                Globals::FRAME_TYPE_DATA,
                Globals::FRAME_TYPE_GOAWAY,
                Globals::FRAME_TYPE_RST_STREAM,
            };
            const std::uint8_t flags[] = { 0x00, 0x01, 0x05, 0x09, 0x00, 0x00, 0x01, 0x00, 0x00 };
            const std::uint32_t streamIds[] = { 0U, 0U, 1U, 1U, 0U, 0U, 3U, 0U, 1U };

            const std::vector< std::uint8_t > payloads[] =
            {
                makeBytes( { 0x00, 0x03, 0x00, 0x00, 0x00, 0x64,
                             0x00, 0x04, 0x00, 0x00, 0xff, 0xff } ),
                makeBytes( {} ),
                makeBytes( { 0x61, 0x62, 0x63, 0x64, 0x65 } ),
                makeBytes( { 0x04, 0x78, 0x79, 0x7a, 0x00, 0x00, 0x00, 0x00 } ),
                makeBytes( { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 } ),
                makeBytes( { 0x00, 0x00, 0xff, 0xff } ),
                makeBytes( {} ),
                makeBytes( { 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0b, 0x6e, 0x6f } ),
                makeBytes( { 0x00, 0x00, 0x00, 0x08 } ),
            };

            for( std::size_t i = 0U; i < 9U; ++i )
            {
                CapturedFrame frame;

                frame.length = lengths[ i ];
                frame.type = types[ i ];
                frame.flags = flags[ i ];
                frame.streamId = streamIds[ i ];
                frame.streamErrorCode = Globals::ERROR_CODE_NO_ERROR;
                frame.payload = payloads[ i ];

                frames.push_back( frame );
            }

            return frames;
        }

    } // framecodec

} // utest

UTF_AUTO_TEST_CASE( FrameCodec_FrameHeaderTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * RFC 9113 4.1 - Length (24), Type (8), Flags (8), Reserved (1), Stream Identifier (31)
     */

    const auto wire = makeBytes( { 0x00, 0x40, 0x01, 0x01, 0x25, 0x00, 0x00, 0x00, 0x0f } );

    const auto header = FrameCodec::parseFrameHeader( &wire[ 0 ] );

    UTF_REQUIRE_EQUAL( header.length.value(), 16385U );
    UTF_REQUIRE_EQUAL( header.type.value(), Globals::FRAME_TYPE_HEADERS );
    UTF_REQUIRE_EQUAL( header.flags.value(), 0x25U );
    UTF_REQUIRE_EQUAL( header.streamId.value(), 15U );

    /*
     * The reserved bit is ignored on receipt - the same nine octets with the high bit of the
     * stream identifier set must parse to the same header
     */

    const auto withReservedBit =
        makeBytes( { 0x00, 0x40, 0x01, 0x01, 0x25, 0x80, 0x00, 0x00, 0x0f } );

    const auto ignored = FrameCodec::parseFrameHeader( &withReservedBit[ 0 ] );

    UTF_REQUIRE_EQUAL( ignored.streamId.value(), 15U );
    UTF_REQUIRE_EQUAL( ignored.length.value(), header.length.value() );

    /*
     * The largest header which is representable at all
     */

    const auto largest = makeBytes( { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } );

    const auto extremes = FrameCodec::parseFrameHeader( &largest[ 0 ] );

    UTF_REQUIRE_EQUAL( extremes.length.value(), Globals::MAX_FRAME_SIZE_UPPER_BOUND );
    UTF_REQUIRE_EQUAL( extremes.type.value(), 0xffU );
    UTF_REQUIRE_EQUAL( extremes.flags.value(), 0xffU );
    UTF_REQUIRE_EQUAL( extremes.streamId.value(), Globals::MAX_STREAM_ID );

    /*
     * Serialization is the same nine octets back, with the reserved bit left unset - which is
     * what makes the second of the three round trips below differ from the first
     */

    std::vector< std::uint8_t > out;

    FrameCodec::serializeFrameHeader( header, out );

    UTF_REQUIRE( out == wire );

    out.clear();
    FrameCodec::serializeFrameHeader( ignored, out );

    UTF_REQUIRE( out == wire );

    /*
     * A payload which does not fit the 24 bit Length field, and a stream identifier which does
     * not fit 31 bits, are both programming errors rather than anything the wire can express
     */

    FrameHeader tooLong;
    tooLong.length = Globals::MAX_FRAME_SIZE_UPPER_BOUND + 1U;

    UTF_REQUIRE_THROW( FrameCodec::serializeFrameHeader( tooLong, out ), UnexpectedException );

    FrameHeader tooHigh;
    tooHigh.streamId = Globals::MAX_STREAM_ID + 1U;

    UTF_REQUIRE_THROW( FrameCodec::serializeFrameHeader( tooHigh, out ), UnexpectedException );

    /*
     * The ten types of section 6 are known; everything above them is not, and an unknown type is
     * ignored rather than rejected (4.1)
     */

    UTF_REQUIRE( FrameCodec::isKnownFrameType( Globals::FRAME_TYPE_DATA ) );
    UTF_REQUIRE( FrameCodec::isKnownFrameType( Globals::FRAME_TYPE_CONTINUATION ) );
    UTF_REQUIRE( ! FrameCodec::isKnownFrameType( 0x0aU ) );
    UTF_REQUIRE( ! FrameCodec::isKnownFrameType( 0xffU ) );
}

UTF_AUTO_TEST_CASE( FrameCodec_IncrementalParseAcrossReadBoundariesTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    const auto wire = byteScript();
    const auto expected = byteScriptFrames();

    UTF_REQUIRE_EQUAL( wire.size(), 132U );
    UTF_REQUIRE_EQUAL( expected.size(), 9U );

    /*
     * One block, which is the easy case and the only one most codecs are tested on
     */

    {
        FrameReader reader;

        const auto frames = readBlocks( reader, asOneBlock( wire ) );

        UTF_REQUIRE_EQUAL( frames.size(), expected.size() );

        for( std::size_t i = 0U; i < frames.size(); ++i )
        {
            UTF_REQUIRE_EQUAL( frames[ i ].length, expected[ i ].length );
            UTF_REQUIRE_EQUAL( frames[ i ].type, expected[ i ].type );
            UTF_REQUIRE_EQUAL( frames[ i ].flags, expected[ i ].flags );
            UTF_REQUIRE_EQUAL( frames[ i ].streamId, expected[ i ].streamId );
            UTF_REQUIRE( frames[ i ].payload == expected[ i ].payload );
        }

        UTF_REQUIRE( ! reader.hasFrame() );
        UTF_REQUIRE( ! reader.isInHeaderBlock() );
    }

    /*
     * One octet at a time. Every nine octet header and every payload is now split at every
     * internal boundary there is, which is the case the design calls out
     */

    {
        FrameReader reader;

        const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

        UTF_REQUIRE_EQUAL( frames.size(), expected.size() );

        for( std::size_t i = 0U; i < frames.size(); ++i )
        {
            UTF_REQUIRE_EQUAL( frames[ i ].length, expected[ i ].length );
            UTF_REQUIRE_EQUAL( frames[ i ].type, expected[ i ].type );
            UTF_REQUIRE_EQUAL( frames[ i ].flags, expected[ i ].flags );
            UTF_REQUIRE_EQUAL( frames[ i ].streamId, expected[ i ].streamId );
            UTF_REQUIRE( frames[ i ].payload == expected[ i ].payload );
        }
    }

    /*
     * Chunk sizes which are coprime with the frame sizes, so the boundaries walk through the
     * script rather than landing in the same relative place each time
     */

    {
        const std::size_t chunkSizes[] = { 2U, 3U, 5U, 7U, 8U, 9U, 10U, 11U, 13U, 17U, 131U, 200U };

        for( const auto chunkSize : chunkSizes )
        {
            FrameReader reader;

            const auto frames = readBlocks( reader, asChunks( wire, chunkSize ) );

            UTF_REQUIRE( sameFrames( frames, expected ) );
        }
    }

    /*
     * And every place the script can be cut in two, including before the first octet and after
     * the last. 133 of them for 132 octets
     */

    {
        for( std::size_t at = 0U; at <= wire.size(); ++at )
        {
            FrameReader reader;

            const auto frames = readBlocks( reader, asSplit( wire, at ) );

            UTF_REQUIRE( sameFrames( frames, expected ) );
        }
    }

    /*
     * feed( ) stops at the end of a frame, whatever it was given. Handed the whole script it
     * takes exactly the first frame - nine octets of header and twelve of payload - and no more
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.feed( &wire[ 0 ], wire.size() ), 21U );
        UTF_REQUIRE( reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.frame().header.type.value(), Globals::FRAME_TYPE_SETTINGS );

        /*
         * And it refuses to go on until that frame is taken, rather than dropping it
         */

        UTF_REQUIRE_THROW( reader.feed( &wire[ 21 ], wire.size() - 21U ), UnexpectedException );

        reader.consumeFrame();

        UTF_REQUIRE( ! reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.feed( &wire[ 21 ], wire.size() - 21U ), 9U );
        UTF_REQUIRE( reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.frame().header.length.value(), 0U );
    }

    /*
     * A header which is fed in full but whose payload never arrives leaves nothing available,
     * and an empty block changes nothing
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.feed( &wire[ 0 ], 9U ), 9U );
        UTF_REQUIRE( ! reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.feed( nullptr, 0U ), 0U );
        UTF_REQUIRE( ! reader.hasFrame() );
        UTF_REQUIRE_THROW( reader.frame(), UnexpectedException );
        UTF_REQUIRE_THROW( reader.consumeFrame(), UnexpectedException );

        UTF_REQUIRE_EQUAL( reader.feed( &wire[ 9 ], 12U ), 12U );
        UTF_REQUIRE( reader.hasFrame() );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_PayloadParseVectorsTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * DATA with padding - RFC 9113 6.1. Pad length 4, data "xyz", four zero octets of padding
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x08, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01,
                0x04, 0x78, 0x79, 0x7a, 0x00, 0x00, 0x00, 0x00
            } );

        const auto payload = FrameCodec::parseData( viewOf( wire ) );

        UTF_REQUIRE( payload.isPadded.value() );
        UTF_REQUIRE_EQUAL( payload.padLength.value(), 4U );
        UTF_REQUIRE( payload.endStream.value() );
        UTF_REQUIRE_EQUAL( payload.size.value(), 3U );
        UTF_REQUIRE( toVector( payload.data.value(), payload.size.value() ) ==
            makeBytes( { 0x78, 0x79, 0x7a } ) );
    }

    /*
     * DATA of length zero, which is how a request body ends when it ended on a frame boundary
     */

    {
        const auto wire =
            makeBytes( { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03 } );

        const auto payload = FrameCodec::parseData( viewOf( wire ) );

        UTF_REQUIRE( ! payload.isPadded.value() );
        UTF_REQUIRE( payload.endStream.value() );
        UTF_REQUIRE_EQUAL( payload.size.value(), 0U );
        UTF_REQUIRE( payload.data.value() == nullptr );
    }

    /*
     * The legal extreme of padding - the pad length octet plus padding which fills the payload,
     * so the frame carries no data at all. 6.1 rejects padding which is the length of the payload
     * or GREATER, so this one is still valid
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                0x04, 0x00, 0x00, 0x00, 0x00
            } );

        const auto payload = FrameCodec::parseData( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( payload.padLength.value(), 4U );
        UTF_REQUIRE_EQUAL( payload.size.value(), 0U );
        UTF_REQUIRE( payload.data.value() == nullptr );
    }

    /*
     * HEADERS with both the PADDED and the PRIORITY fields - 6.2. Pad length 2, exclusive
     * dependency on stream 3, weight 200, field block "hi", two octets of padding
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x0a, 0x01, 0x2c, 0x00, 0x00, 0x00, 0x05,
                0x02,
                0x80, 0x00, 0x00, 0x03, 0xc8,
                0x68, 0x69,
                0x00, 0x00
            } );

        const auto payload = FrameCodec::parseHeaders( viewOf( wire ) );

        UTF_REQUIRE( payload.isPadded.value() );
        UTF_REQUIRE_EQUAL( payload.padLength.value(), 2U );
        UTF_REQUIRE( ! payload.endStream.value() );
        UTF_REQUIRE( payload.endHeaders.value() );
        UTF_REQUIRE( payload.priority.isSet.value() );
        UTF_REQUIRE( payload.priority.exclusive.value() );
        UTF_REQUIRE_EQUAL( payload.priority.streamDependency.value(), 3U );
        UTF_REQUIRE_EQUAL( payload.priority.weight.value(), 200U );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 2U );
        UTF_REQUIRE( toVector( payload.fieldBlock.value(), payload.fieldBlockSize.value() ) ==
            makeBytes( { 0x68, 0x69 } ) );
    }

    /*
     * HEADERS with neither, which is the ordinary shape
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x05, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01,
                0x61, 0x62, 0x63, 0x64, 0x65
            } );

        const auto payload = FrameCodec::parseHeaders( viewOf( wire ) );

        UTF_REQUIRE( ! payload.isPadded.value() );
        UTF_REQUIRE( ! payload.priority.isSet.value() );
        UTF_REQUIRE( payload.endStream.value() );
        UTF_REQUIRE( payload.endHeaders.value() );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 5U );
    }

    /*
     * PRIORITY - 6.3. The weight octet is kept as it appears on the wire and the RFC's actual
     * weight is one higher; a profile records what the browser sent, so this must round trip
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
                0x00, 0x00, 0x00, 0x00, 0xff
            } );

        const auto priority = FrameCodec::parsePriority( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( priority.streamId.value(), 3U );
        UTF_REQUIRE( ! priority.exclusive.value() );
        UTF_REQUIRE_EQUAL( priority.streamDependency.value(), 0U );
        UTF_REQUIRE_EQUAL( priority.weight.value(), 255U );
    }

    /*
     * RST_STREAM - 6.4
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x08
            } );

        UTF_REQUIRE_EQUAL(
            FrameCodec::parseRstStream( viewOf( wire ) ),
            Globals::ERROR_CODE_CANCEL
            );
    }

    /*
     * SETTINGS - 6.5. Three entries, of which the middle one carries an identifier this library
     * does not interpret at all: 6.5.2 requires that it be ignored, and it must survive being
     * read so that a profile can pass it through (design 6.4). The third repeats the first, which
     * 6.5.3 says is processed in order and therefore must not be collapsed here
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x12, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x01, 0x00, 0x00, 0x10, 0x00,
                0x00, 0x09, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x01, 0x00, 0x00, 0x00, 0x00
            } );

        const auto settings = FrameCodec::parseSettings( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( settings.size(), 3U );

        UTF_REQUIRE_EQUAL( settings[ 0 ].id.value(), Globals::SETTINGS_HEADER_TABLE_SIZE );
        UTF_REQUIRE_EQUAL( settings[ 0 ].value.value(), 4096U );
        UTF_REQUIRE_EQUAL( settings[ 1 ].id.value(), 9U );
        UTF_REQUIRE_EQUAL( settings[ 1 ].value.value(), 1U );
        UTF_REQUIRE_EQUAL( settings[ 2 ].id.value(), Globals::SETTINGS_HEADER_TABLE_SIZE );
        UTF_REQUIRE_EQUAL( settings[ 2 ].value.value(), 0U );
    }

    /*
     * A SETTINGS acknowledgement carries none
     */

    {
        const auto wire =
            makeBytes( { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } );

        UTF_REQUIRE( FrameCodec::parseSettings( viewOf( wire ) ).empty() );
    }

    /*
     * PUSH_PROMISE - 6.6. Never sent by this client (D11), and parsed so that the session can
     * say why it is closing the connection
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x07, 0x05, 0x04, 0x00, 0x00, 0x00, 0x01,
                0x80, 0x00, 0x00, 0x02,
                0x41, 0x42, 0x43
            } );

        const auto payload = FrameCodec::parsePushPromise( viewOf( wire ) );

        UTF_REQUIRE( ! payload.isPadded.value() );
        UTF_REQUIRE( payload.endHeaders.value() );
        UTF_REQUIRE_EQUAL( payload.promisedStreamId.value(), 2U );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 3U );
        UTF_REQUIRE( toVector( payload.fieldBlock.value(), payload.fieldBlockSize.value() ) ==
            makeBytes( { 0x41, 0x42, 0x43 } ) );
    }

    /*
     * PING - 6.7, eight opaque octets which are never interpreted
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22, 0x33
            } );

        const auto* const opaque = FrameCodec::parsePing( viewOf( wire ) );

        UTF_REQUIRE( toVector( opaque, 8U ) ==
            makeBytes( { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22, 0x33 } ) );
    }

    /*
     * GOAWAY - 6.8, with and without debug data
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x0a, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x80, 0x00, 0x00, 0x03,
                0x00, 0x00, 0x00, 0x0b,
                0x6e, 0x6f
            } );

        const auto payload = FrameCodec::parseGoAway( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( payload.lastStreamId.value(), 3U );
        UTF_REQUIRE_EQUAL( payload.errorCode.value(), Globals::ERROR_CODE_ENHANCE_YOUR_CALM );
        UTF_REQUIRE_EQUAL( payload.debugDataSize.value(), 2U );
        UTF_REQUIRE( toVector( payload.debugData.value(), payload.debugDataSize.value() ) ==
            makeBytes( { 0x6e, 0x6f } ) );
    }

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            } );

        const auto payload = FrameCodec::parseGoAway( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( payload.lastStreamId.value(), 0U );
        UTF_REQUIRE_EQUAL( payload.errorCode.value(), Globals::ERROR_CODE_NO_ERROR );
        UTF_REQUIRE_EQUAL( payload.debugDataSize.value(), 0U );
        UTF_REQUIRE( payload.debugData.value() == nullptr );
    }

    /*
     * WINDOW_UPDATE - 6.9. The reserved bit of the increment is ignored exactly as the reserved
     * bit of a stream identifier is, and an increment of zero parses here because it is flow
     * control's error to raise, not the codec's
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x80, 0x00, 0x00, 0x01
            } );

        UTF_REQUIRE_EQUAL( FrameCodec::parseWindowUpdate( viewOf( wire ) ), 1U );
    }

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x00
            } );

        UTF_REQUIRE_EQUAL( FrameCodec::parseWindowUpdate( viewOf( wire ) ), 0U );
    }

    /*
     * CONTINUATION - 6.10, and one of length zero, which is legal
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x02, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01,
                0x7f, 0x80
            } );

        const auto payload = FrameCodec::parseContinuation( viewOf( wire ) );

        UTF_REQUIRE( payload.endHeaders.value() );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 2U );
    }

    {
        const auto wire =
            makeBytes( { 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x01 } );

        const auto payload = FrameCodec::parseContinuation( viewOf( wire ) );

        UTF_REQUIRE( ! payload.endHeaders.value() );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 0U );
        UTF_REQUIRE( payload.fieldBlock.value() == nullptr );
    }

    /*
     * An accessor called for the wrong type is a programming error, not a protocol error
     */

    {
        const auto wire =
            makeBytes( { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } );

        UTF_REQUIRE_THROW( FrameCodec::parseData( viewOf( wire ) ), UnexpectedException );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_SerializeVectorsTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    std::vector< std::uint8_t > out;

    /*
     * DATA with no padding, and then the same data padded - which is one octet of pad length plus
     * the padding itself longer, and carries the PADDED flag
     */

    {
        const auto data = makeBytes( { 0x78, 0x79, 0x7a } );

        FramePadding none;

        out.clear();
        FrameCodec::serializeData( 1U, &data[ 0 ], data.size(), true, none, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                0x78, 0x79, 0x7a
            } ) );

        FramePadding padded;
        padded.isSet = true;
        padded.padLength = 4U;

        out.clear();
        FrameCodec::serializeData( 1U, &data[ 0 ], data.size(), true, padded, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x08, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01,
                0x04, 0x78, 0x79, 0x7a, 0x00, 0x00, 0x00, 0x00
            } ) );

        /*
         * Padding of length zero is not the same frame as no padding at all - it still carries
         * the flag and the pad length octet
         */

        FramePadding empty;
        empty.isSet = true;

        out.clear();
        FrameCodec::serializeData( 1U, &data[ 0 ], data.size(), false, empty, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x78, 0x79, 0x7a
            } ) );
    }

    /*
     * HEADERS with the optional priority fields, taken straight from the profile's own type
     */

    {
        const auto block = makeBytes( { 0x68, 0x69 } );

        Http2HeadersPriority priority;
        priority.isSet = true;
        priority.exclusive = true;
        priority.streamDependency = 3U;
        priority.weight = 200U;

        FramePadding padding;
        padding.isSet = true;
        padding.padLength = 2U;

        out.clear();
        FrameCodec::serializeHeaders(
            5U, &block[ 0 ], block.size(), false, true, priority, padding, out
            );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x0a, 0x01, 0x2c, 0x00, 0x00, 0x00, 0x05,
                0x02,
                0x80, 0x00, 0x00, 0x03, 0xc8,
                0x68, 0x69,
                0x00, 0x00
            } ) );

        Http2HeadersPriority noPriority;
        FramePadding noPadding;

        out.clear();
        FrameCodec::serializeHeaders(
            1U, &block[ 0 ], block.size(), true, true, noPriority, noPadding, out
            );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x02, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01,
                0x68, 0x69
            } ) );
    }

    /*
     * PRIORITY, again from the profile's own type - this is the frame browsers send on idle
     * streams right after SETTINGS (design 6.4)
     */

    {
        Http2PriorityFrame priority;
        priority.streamId = 3U;
        priority.streamDependency = 0U;
        priority.weight = 200U;
        priority.exclusive = false;

        out.clear();
        FrameCodec::serializePriority( priority, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
                0x00, 0x00, 0x00, 0x00, 0xc8
            } ) );
    }

    /*
     * RST_STREAM, WINDOW_UPDATE and GOAWAY
     */

    {
        out.clear();
        FrameCodec::serializeRstStream( 1U, Globals::ERROR_CODE_CANCEL, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x08
            } ) );

        out.clear();
        FrameCodec::serializeWindowUpdate( 0U, 65535U, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0xff, 0xff
            } ) );

        const auto debug = makeBytes( { 0x6e, 0x6f } );

        out.clear();
        FrameCodec::serializeGoAway(
            3U, Globals::ERROR_CODE_ENHANCE_YOUR_CALM, &debug[ 0 ], debug.size(), out
            );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x0a, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x03,
                0x00, 0x00, 0x00, 0x0b,
                0x6e, 0x6f
            } ) );

        out.clear();
        FrameCodec::serializeGoAway( 0U, Globals::ERROR_CODE_NO_ERROR, nullptr, 0U, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            } ) );
    }

    /*
     * SETTINGS in the order the profile gives them, because the order is part of a fingerprint,
     * and its acknowledgement, which is a frame of length zero with the ACK flag
     */

    {
        std::vector< Http2Setting > settings;

        Http2Setting first;
        first.id = Globals::SETTINGS_HEADER_TABLE_SIZE;
        first.value = 65536U;

        Http2Setting second;
        second.id = Globals::SETTINGS_ENABLE_PUSH;
        second.value = 0U;

        settings.push_back( first );
        settings.push_back( second );

        out.clear();
        FrameCodec::serializeSettings( settings, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                0x00, 0x02, 0x00, 0x00, 0x00, 0x00
            } ) );

        out.clear();
        FrameCodec::serializeSettings( std::vector< Http2Setting >(), out );

        UTF_REQUIRE( out ==
            makeBytes( { 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 } ) );

        out.clear();
        FrameCodec::serializeSettingsAck( out );

        UTF_REQUIRE( out ==
            makeBytes( { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } ) );
    }

    /*
     * PING and its acknowledgement, which is the same eight octets echoed back
     */

    {
        const auto opaque = makeBytes( { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 } );

        out.clear();
        FrameCodec::serializePing( &opaque[ 0 ], false, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
            } ) );

        out.clear();
        FrameCodec::serializePing( &opaque[ 0 ], true, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x08, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
            } ) );
    }

    /*
     * CONTINUATION, and PUSH_PROMISE which exists for the role neutrality of D8
     */

    {
        const auto block = makeBytes( { 0x7f, 0x80 } );

        out.clear();
        FrameCodec::serializeContinuation( &block[ 0 ], block.size(), 1U, true, out );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x02, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01,
                0x7f, 0x80
            } ) );

        FramePadding noPadding;

        out.clear();
        FrameCodec::serializePushPromise(
            1U, 2U, &block[ 0 ], block.size(), true, noPadding, out
            );

        UTF_REQUIRE( out == makeBytes(
            {
                0x00, 0x00, 0x06, 0x05, 0x04, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x02,
                0x7f, 0x80
            } ) );
    }

    /*
     * Composing several frames into one buffer appends rather than replaces, which is what the
     * session engine's write path does
     */

    {
        out.clear();
        FrameCodec::serializeSettingsAck( out );
        FrameCodec::serializeWindowUpdate( 0U, 1U, out );

        UTF_REQUIRE_EQUAL( out.size(), 9U + 13U );
        UTF_REQUIRE_EQUAL( out[ 3 ], Globals::FRAME_TYPE_SETTINGS );
        UTF_REQUIRE_EQUAL( out[ 12 ], Globals::FRAME_TYPE_WINDOW_UPDATE );
    }

    /*
     * Everything the wire cannot express is a programming error
     */

    {
        UTF_REQUIRE_THROW(
            FrameCodec::serializeWindowUpdate( 0U, Globals::MAX_STREAM_ID + 1U, out ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW(
            FrameCodec::serializeGoAway(
                Globals::MAX_STREAM_ID + 1U, 0U, nullptr, 0U, out
                ),
            UnexpectedException
            );

        Http2PriorityFrame priority;
        priority.streamDependency = Globals::MAX_STREAM_ID + 1U;

        UTF_REQUIRE_THROW( FrameCodec::serializePriority( priority, out ), UnexpectedException );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_SerializeAndParseRoundTripTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * Everything the serializer produces must read back through the incremental reader as the
     * same frame. This is the only place in the file where the two halves are allowed to meet,
     * and it is a round trip rather than a vector - the literal octets above are what pins the
     * absolute encoding
     */

    std::vector< std::uint8_t > wire;

    const auto block = makeBytes( { 0x88, 0x76, 0x54 } );
    const auto opaque = makeBytes( { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 } );

    Http2HeadersPriority priority;
    priority.isSet = true;
    priority.exclusive = true;
    priority.streamDependency = 7U;
    priority.weight = 110U;

    FramePadding padding;
    padding.isSet = true;
    padding.padLength = 6U;

    FramePadding noPadding;

    std::vector< Http2Setting > settings;

    Http2Setting setting;
    setting.id = Globals::SETTINGS_MAX_FRAME_SIZE;
    setting.value = Globals::MAX_FRAME_SIZE_UPPER_BOUND;
    settings.push_back( setting );

    FrameCodec::serializeSettings( settings, wire );
    FrameCodec::serializeSettingsAck( wire );
    FrameCodec::serializeHeaders(
        1U, &block[ 0 ], block.size(), false, false, priority, padding, wire
        );
    FrameCodec::serializeContinuation( &block[ 0 ], block.size(), 1U, true, wire );
    FrameCodec::serializeData( 1U, &block[ 0 ], block.size(), true, noPadding, wire );
    FrameCodec::serializePing( &opaque[ 0 ], true, wire );
    FrameCodec::serializeWindowUpdate( 1U, 1024U, wire );
    FrameCodec::serializeRstStream( 1U, Globals::ERROR_CODE_NO_ERROR, wire );
    FrameCodec::serializeGoAway( 1U, Globals::ERROR_CODE_NO_ERROR, &block[ 0 ], block.size(), wire );

    FrameReader reader;

    const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

    UTF_REQUIRE_EQUAL( frames.size(), 9U );
    UTF_REQUIRE( ! reader.isInHeaderBlock() );

    /*
     * And the same frames again out of one block, which must agree octet for octet
     */

    FrameReader second;

    UTF_REQUIRE( sameFrames( frames, readBlocks( second, asOneBlock( wire ) ) ) );

    /*
     * The round trip of the two frames whose fields are the least obvious
     */

    {
        FrameReader reparse;

        UTF_REQUIRE_EQUAL( reparse.feed( &wire[ 0 ], wire.size() ), 9U + 6U );

        const auto parsed = FrameCodec::parseSettings( reparse.frame() );

        UTF_REQUIRE_EQUAL( parsed.size(), 1U );
        UTF_REQUIRE_EQUAL( parsed[ 0 ].id.value(), Globals::SETTINGS_MAX_FRAME_SIZE );
        UTF_REQUIRE_EQUAL( parsed[ 0 ].value.value(), Globals::MAX_FRAME_SIZE_UPPER_BOUND );
    }

    {
        const auto headersOffset = 9U + 6U + 9U;

        FrameReader reparse;

        UTF_REQUIRE_EQUAL(
            reparse.feed( &wire[ headersOffset ], wire.size() - headersOffset ),
            9U + 1U + 5U + 3U + 6U
            );

        const auto parsed = FrameCodec::parseHeaders( reparse.frame() );

        UTF_REQUIRE( parsed.priority.isSet.value() );
        UTF_REQUIRE( parsed.priority.exclusive.value() );
        UTF_REQUIRE_EQUAL( parsed.priority.streamDependency.value(), 7U );
        UTF_REQUIRE_EQUAL( parsed.priority.weight.value(), 110U );
        UTF_REQUIRE_EQUAL( parsed.padLength.value(), 6U );
        UTF_REQUIRE_EQUAL( parsed.fieldBlockSize.value(), 3U );
        UTF_REQUIRE( toVector( parsed.fieldBlock.value(), parsed.fieldBlockSize.value() ) == block );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_OversizeAndLengthRejectionTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * A frame larger than the SETTINGS_MAX_FRAME_SIZE we advertised is a connection error, and it
     * is raised BEFORE the payload is buffered (design 4.1) - which is what this first block
     * shows: nine octets are fed, the payload never arrives at all, and the error is already out
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.maxFrameSize(), Globals::MAX_FRAME_SIZE_DEFAULT );

        const auto header =
            makeBytes( { 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } );

        requireConnectionError(
            [ &reader, &header ]() -> void
            {
                reader.feed( &header[ 0 ], header.size() );
            },
            Globals::ERROR_CODE_FRAME_SIZE_ERROR
            );

        /*
         * And the reader stays unusable, because the frame boundaries cannot be recovered
         */

        UTF_REQUIRE_THROW( reader.feed( &header[ 0 ], header.size() ), UnexpectedException );

        reader.reset();

        /*
         * Raising the advertised limit makes the same frame acceptable - the limit is ours and
         * not a property of the wire
         */

        reader.setMaxFrameSize( 16385U );

        UTF_REQUIRE_EQUAL( reader.feed( &header[ 0 ], header.size() ), 9U );
        UTF_REQUIRE( ! reader.hasFrame() );
    }

    /*
     * A frame of exactly the advertised size is not oversize
     */

    {
        FrameReader reader;

        std::vector< std::uint8_t > wire =
            makeBytes( { 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } );

        wire.resize( wire.size() + Globals::MAX_FRAME_SIZE_DEFAULT, 0x5aU );

        const auto frames = readBlocks( reader, asChunks( wire, 1000U ) );

        UTF_REQUIRE_EQUAL( frames.size(), 1U );
        UTF_REQUIRE_EQUAL( frames[ 0 ].length, Globals::MAX_FRAME_SIZE_DEFAULT );
        UTF_REQUIRE_EQUAL( frames[ 0 ].payload.size(), Globals::MAX_FRAME_SIZE_DEFAULT );
    }

    /*
     * The fixed and minimum lengths of section 6. Every one of these is a CONNECTION error: the
     * frame either carries a field block, or is a SETTINGS, or is on stream zero, which is
     * exactly the list RFC 9113 4.2 gives
     */

    {
        const std::vector< std::uint8_t > tooShortOrWrong[] =
        {
            /* RST_STREAM of five octets - 6.4 requires exactly four */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01,
                  0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* PING of seven - 6.7 requires exactly eight */
            makeBytes(
                { 0x00, 0x00, 0x07, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* WINDOW_UPDATE of five - 6.9 requires exactly four */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x01, 0x00 } ),

            /* SETTINGS of seven - 6.5 requires a multiple of six */
            makeBytes(
                { 0x00, 0x00, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* SETTINGS with the ACK flag and a payload - 6.5 requires none */
            makeBytes(
                { 0x00, 0x00, 0x06, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x01, 0x00, 0x00, 0x00, 0x00 } ),

            /* GOAWAY of seven - 6.8 needs eight before its debug data */
            makeBytes(
                { 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* HEADERS with PADDED and PRIORITY but only five octets for their six */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x01, 0x28, 0x00, 0x00, 0x00, 0x01,
                  0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* PUSH_PROMISE of three, too short for the promised stream identifier */
            makeBytes(
                { 0x00, 0x00, 0x03, 0x05, 0x04, 0x00, 0x00, 0x00, 0x01,
                  0x00, 0x00, 0x00 } ),
        };

        for( const auto& wire : tooShortOrWrong )
        {
            requireConnectionError(
                [ &wire ]() -> void
                {
                    FrameReader reader;

                    readBlocks( reader, asOneBlock( wire ) );
                },
                Globals::ERROR_CODE_FRAME_SIZE_ERROR
                );
        }
    }

    /*
     * PRIORITY is the one length rule in section 6 which is a STREAM error - 6.3 says so
     * explicitly. It must therefore NOT stop the connection: the frame is consumed in full, it is
     * surfaced carrying the code to answer with, and the frame behind it parses normally
     */

    {
        std::vector< std::uint8_t > wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x00
            } );

        appendBytes( wire,
            {
                0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
            } );

        FrameReader reader;

        const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

        UTF_REQUIRE_EQUAL( frames.size(), 2U );
        UTF_REQUIRE_EQUAL( frames[ 0 ].type, Globals::FRAME_TYPE_PRIORITY );
        UTF_REQUIRE_EQUAL( frames[ 0 ].streamErrorCode, Globals::ERROR_CODE_FRAME_SIZE_ERROR );
        UTF_REQUIRE_EQUAL( frames[ 0 ].payload.size(), 4U );
        UTF_REQUIRE_EQUAL( frames[ 1 ].type, Globals::FRAME_TYPE_PING );
        UTF_REQUIRE_EQUAL( frames[ 1 ].streamErrorCode, Globals::ERROR_CODE_NO_ERROR );

        /*
         * And asking such a frame for its fields raises the stream error rather than reading
         * whatever happens to be there
         */

        const auto bad = makeBytes(
            {
                0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x00
            } );

        requireStreamError(
            [ &bad ]() -> void
            {
                FrameCodec::parsePriority( viewOf( bad ) );
            },
            Globals::ERROR_CODE_FRAME_SIZE_ERROR,
            1U
            );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_PaddingRejectionTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * RFC 9113 6.1 - "If the length of the padding is the length of the frame payload or greater,
     * the recipient MUST treat this as a connection error of type PROTOCOL_ERROR" - and 6.2 and
     * 6.6 say the same for padding which exceeds what is left for the field block fragment
     */

    {
        const std::vector< std::uint8_t > badPadding[] =
        {
            /* DATA, payload of five, pad length five - the padding IS the payload */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                  0x05, 0x00, 0x00, 0x00, 0x00 } ),

            /* DATA, payload of five, pad length 255 */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                  0xff, 0x00, 0x00, 0x00, 0x00 } ),

            /* HEADERS, payload of three, pad length three */
            makeBytes(
                { 0x00, 0x00, 0x03, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x01,
                  0x03, 0x00, 0x00 } ),

            /* HEADERS with PRIORITY, payload of six, pad length one - nothing left to pad */
            makeBytes(
                { 0x00, 0x00, 0x06, 0x01, 0x2c, 0x00, 0x00, 0x00, 0x01,
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* PUSH_PROMISE, payload of five, pad length one */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x05, 0x0c, 0x00, 0x00, 0x00, 0x01,
                  0x01, 0x00, 0x00, 0x00, 0x02 } ),
        };

        for( const auto& wire : badPadding )
        {
            requireConnectionError(
                [ &wire ]() -> void
                {
                    FrameReader reader;

                    readBlocks( reader, asOneBlock( wire ) );
                },
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );

            /*
             * The payload accessor reaches the same verdict on its own, which is what makes it
             * safe on a view the reader never produced
             */

            requireConnectionError(
                [ &wire ]() -> void
                {
                    const auto view = viewOf( wire );

                    FrameCodec::validateFramePayload( view );
                },
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );
        }
    }

    /*
     * A padded DATA frame with no payload at all has no room for the Pad Length octet its own
     * flags promise. RFC 9113 4.2 makes that a FRAME_SIZE_ERROR, and because DATA carries no
     * field block and is never on stream zero, a STREAM error - so the frame is consumed and the
     * connection carries on, which is what the frame behind it shows
     */

    {
        std::vector< std::uint8_t > wire =
            makeBytes( { 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01 } );

        appendBytes( wire, { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } );

        FrameReader reader;

        const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

        UTF_REQUIRE_EQUAL( frames.size(), 2U );
        UTF_REQUIRE_EQUAL( frames[ 0 ].type, Globals::FRAME_TYPE_DATA );
        UTF_REQUIRE_EQUAL( frames[ 0 ].streamErrorCode, Globals::ERROR_CODE_FRAME_SIZE_ERROR );
        UTF_REQUIRE_EQUAL( frames[ 1 ].type, Globals::FRAME_TYPE_SETTINGS );
        UTF_REQUIRE_EQUAL( frames[ 1 ].streamErrorCode, Globals::ERROR_CODE_NO_ERROR );
    }

    /*
     * The boundary on the other side: padding one octet shorter than the payload leaves an empty
     * field block, and that is legal
     */

    {
        const auto wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x01,
                0x03, 0x00, 0x00, 0x00
            } );

        const auto payload = FrameCodec::parseHeaders( viewOf( wire ) );

        UTF_REQUIRE_EQUAL( payload.padLength.value(), 3U );
        UTF_REQUIRE_EQUAL( payload.fieldBlockSize.value(), 0U );
        UTF_REQUIRE( payload.fieldBlock.value() == nullptr );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_StreamIdentifierRuleTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * The types which must be associated with a stream, each one on stream zero
     */

    {
        const std::vector< std::uint8_t > onStreamZero[] =
        {
            /* DATA */
            makeBytes( { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* HEADERS */
            makeBytes( { 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00 } ),

            /* PRIORITY */
            makeBytes(
                { 0x00, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x10 } ),

            /* RST_STREAM */
            makeBytes(
                { 0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00 } ),

            /* PUSH_PROMISE */
            makeBytes(
                { 0x00, 0x00, 0x04, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x02 } ),
        };

        for( const auto& wire : onStreamZero )
        {
            requireConnectionError(
                [ &wire ]() -> void
                {
                    FrameReader reader;

                    readBlocks( reader, asOneBlock( wire ) );
                },
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );
        }
    }

    /*
     * And the types which apply to the connection as a whole, each one on a stream
     */

    {
        const std::vector< std::uint8_t > onAStream[] =
        {
            /* SETTINGS */
            makeBytes( { 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 } ),

            /* PING */
            makeBytes(
                { 0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* GOAWAY */
            makeBytes(
                { 0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x01,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),
        };

        for( const auto& wire : onAStream )
        {
            requireConnectionError(
                [ &wire ]() -> void
                {
                    FrameReader reader;

                    readBlocks( reader, asOneBlock( wire ) );
                },
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );
        }
    }

    /*
     * WINDOW_UPDATE is the one type which is legal in both places - on the connection and on a
     * stream - so it has no stream identifier rule at all
     */

    {
        std::vector< std::uint8_t > wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x01
            } );

        appendBytes( wire,
            {
                0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x01
            } );

        FrameReader reader;

        const auto frames = readBlocks( reader, asOneBlock( wire ) );

        UTF_REQUIRE_EQUAL( frames.size(), 2U );
        UTF_REQUIRE_EQUAL( frames[ 0 ].streamId, 0U );
        UTF_REQUIRE_EQUAL( frames[ 1 ].streamId, 1U );
    }

    /*
     * An unknown frame type is ignored rather than rejected (4.1), which here means that it is
     * handed over with its payload intact for the session to discard - including one on a stream
     * identifier which would be illegal for any type we do know
     */

    {
        std::vector< std::uint8_t > wire = makeBytes(
            {
                0x00, 0x00, 0x03, 0x0b, 0xff, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x02, 0x03
            } );

        appendBytes( wire,
            {
                0x00, 0x00, 0x00, 0xff, 0x00, 0x7f, 0xff, 0xff, 0xff
            } );

        FrameReader reader;

        const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

        UTF_REQUIRE_EQUAL( frames.size(), 2U );
        UTF_REQUIRE_EQUAL( frames[ 0 ].type, 0x0bU );
        UTF_REQUIRE( frames[ 0 ].payload == makeBytes( { 0x01, 0x02, 0x03 } ) );
        UTF_REQUIRE_EQUAL( frames[ 0 ].streamErrorCode, Globals::ERROR_CODE_NO_ERROR );
        UTF_REQUIRE_EQUAL( frames[ 1 ].type, 0xffU );
        UTF_REQUIRE_EQUAL( frames[ 1 ].streamId, Globals::MAX_STREAM_ID );

        /*
         * The size limit still applies to it, though - being unknown does not make it free
         */

        const auto oversize =
            makeBytes( { 0x00, 0x40, 0x01, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00 } );

        requireConnectionError(
            [ &oversize ]() -> void
            {
                FrameReader reader2;

                reader2.feed( &oversize[ 0 ], oversize.size() );
            },
            Globals::ERROR_CODE_FRAME_SIZE_ERROR
            );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_HeaderBlockContinuityTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * RFC 9113 6.10 - a HEADERS frame without END_HEADERS must be followed by a CONTINUATION
     * frame for the same stream, and anything else is a connection error of type PROTOCOL_ERROR.
     * This is a rule about the frame SEQUENCE, which is why the reader is the only part of the
     * codec that has to remember anything
     */

    const auto headersNoEnd =
        makeBytes( { 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x41 } );

    /*
     * The legal sequence: HEADERS, CONTINUATION, CONTINUATION with END_HEADERS
     */

    {
        std::vector< std::uint8_t > wire = headersNoEnd;

        appendBytes( wire, { 0x00, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00, 0x00, 0x01, 0x42 } );
        appendBytes( wire, { 0x00, 0x00, 0x01, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x43 } );
        appendBytes( wire, { 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00 } );

        FrameReader reader;

        UTF_REQUIRE( ! reader.isInHeaderBlock() );

        const auto frames = readBlocks( reader, asChunks( wire, 1U ) );

        UTF_REQUIRE_EQUAL( frames.size(), 4U );
        UTF_REQUIRE_EQUAL( frames[ 1 ].type, Globals::FRAME_TYPE_CONTINUATION );
        UTF_REQUIRE_EQUAL( frames[ 3 ].type, Globals::FRAME_TYPE_SETTINGS );
        UTF_REQUIRE( ! reader.isInHeaderBlock() );
    }

    /*
     * A header block is open between the HEADERS frame and the CONTINUATION which closes it, and
     * the reader says so - the session engine needs the same fact for the per-block limits of 4.6
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.feed( &headersNoEnd[ 0 ], headersNoEnd.size() ), 10U );
        UTF_REQUIRE( reader.isInHeaderBlock() );
        UTF_REQUIRE_EQUAL( reader.headerBlockStreamId(), 1U );

        reader.consumeFrame();

        UTF_REQUIRE( reader.isInHeaderBlock() );

        const auto continuation =
            makeBytes( { 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01 } );

        UTF_REQUIRE_EQUAL( reader.feed( &continuation[ 0 ], continuation.size() ), 9U );
        UTF_REQUIRE( ! reader.isInHeaderBlock() );
        UTF_REQUIRE_EQUAL( reader.headerBlockStreamId(), Globals::STREAM_ID_CONNECTION );
    }

    /*
     * Everything which may not interrupt an open header block. The last of them is the one the
     * design calls out by name: an unknown type is ignored everywhere EXCEPT inside a header
     * block, where it is a connection error
     */

    {
        const std::vector< std::uint8_t > interruptions[] =
        {
            /* DATA on the same stream */
            makeBytes( { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } ),

            /* a PING, which is otherwise legal at any time */
            makeBytes(
                { 0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } ),

            /* a second HEADERS frame */
            makeBytes( { 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01 } ),

            /* a CONTINUATION on a different stream */
            makeBytes( { 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x03 } ),

            /* an unknown type */
            makeBytes( { 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x01 } ),
        };

        for( const auto& interruption : interruptions )
        {
            std::vector< std::uint8_t > wire = headersNoEnd;

            wire.insert( wire.end(), interruption.begin(), interruption.end() );

            requireConnectionError(
                [ &wire ]() -> void
                {
                    FrameReader reader;

                    readBlocks( reader, asOneBlock( wire ) );
                },
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );
        }
    }

    /*
     * And a CONTINUATION with no block open at all
     */

    {
        const auto orphan =
            makeBytes( { 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x01 } );

        requireConnectionError(
            [ &orphan ]() -> void
            {
                FrameReader reader;

                readBlocks( reader, asOneBlock( orphan ) );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );
    }

    /*
     * A HEADERS frame WITH END_HEADERS opens no block, so an ordinary frame may follow it
     */

    {
        std::vector< std::uint8_t > wire =
            makeBytes( { 0x00, 0x00, 0x01, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x41 } );

        appendBytes( wire, { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01 } );

        FrameReader reader;

        UTF_REQUIRE_EQUAL( readBlocks( reader, asOneBlock( wire ) ).size(), 2U );
        UTF_REQUIRE( ! reader.isInHeaderBlock() );
    }

    /*
     * A PUSH_PROMISE without END_HEADERS opens one exactly as a HEADERS frame does (6.6)
     */

    {
        std::vector< std::uint8_t > wire = makeBytes(
            {
                0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x02
            } );

        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.feed( &wire[ 0 ], wire.size() ), wire.size() );
        UTF_REQUIRE( reader.isInHeaderBlock() );
        UTF_REQUIRE_EQUAL( reader.headerBlockStreamId(), 1U );
    }
}

UTF_AUTO_TEST_CASE( FrameCodec_ReaderContractTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::framecodec;

    /*
     * The advertised maximum frame size is ours, so a value outside the range of RFC 9113 4.2 is
     * a programming error here - not the PROTOCOL_ERROR it would be arriving from a peer
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.maxFrameSize(), 16384U );

        UTF_REQUIRE_THROW( reader.setMaxFrameSize( 16383U ), UnexpectedException );
        UTF_REQUIRE_THROW( reader.setMaxFrameSize( 0U ), UnexpectedException );
        UTF_REQUIRE_THROW(
            reader.setMaxFrameSize( Globals::MAX_FRAME_SIZE_UPPER_BOUND + 1U ),
            UnexpectedException
            );

        reader.setMaxFrameSize( Globals::MAX_FRAME_SIZE_DEFAULT );
        UTF_REQUIRE_EQUAL( reader.maxFrameSize(), 16384U );

        reader.setMaxFrameSize( Globals::MAX_FRAME_SIZE_UPPER_BOUND );
        UTF_REQUIRE_EQUAL( reader.maxFrameSize(), Globals::MAX_FRAME_SIZE_UPPER_BOUND );
    }

    /*
     * A block of no bytes at all is not the same as a block of none
     */

    {
        FrameReader reader;

        UTF_REQUIRE_EQUAL( reader.feed( nullptr, 0U ), 0U );
        UTF_REQUIRE_THROW( reader.feed( nullptr, 5U ), UnexpectedException );
    }

    /*
     * reset( ) puts back everything a connection error left behind, including the open header
     * block, and keeps the advertised frame size - which is a property of this endpoint rather
     * than of the connection
     */

    {
        FrameReader reader;

        reader.setMaxFrameSize( 32768U );

        const auto headersNoEnd =
            makeBytes( { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01 } );

        UTF_REQUIRE_EQUAL( reader.feed( &headersNoEnd[ 0 ], headersNoEnd.size() ), 9U );
        UTF_REQUIRE( reader.hasFrame() );

        reader.consumeFrame();

        UTF_REQUIRE( reader.isInHeaderBlock() );

        const auto ping = makeBytes(
            {
                0x00, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
            } );

        requireConnectionError(
            [ &reader, &ping ]() -> void
            {
                reader.feed( &ping[ 0 ], ping.size() );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );

        reader.reset();

        UTF_REQUIRE( ! reader.isInHeaderBlock() );
        UTF_REQUIRE( ! reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.maxFrameSize(), 32768U );

        UTF_REQUIRE_EQUAL( reader.feed( &ping[ 0 ], ping.size() ), ping.size() );
        UTF_REQUIRE( reader.hasFrame() );
        UTF_REQUIRE_EQUAL( reader.frame().header.type.value(), Globals::FRAME_TYPE_PING );
    }

    /*
     * A view which does not describe itself consistently is refused rather than read - the reader
     * never produces one, but a peer built by hand can
     */

    {
        FrameView inconsistent;

        inconsistent.header.length = 4U;
        inconsistent.header.type = Globals::FRAME_TYPE_RST_STREAM;
        inconsistent.header.streamId = 1U;

        UTF_REQUIRE_THROW(
            FrameCodec::validateFramePayload( inconsistent ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW( FrameCodec::parseRstStream( inconsistent ), UnexpectedException );

        inconsistent.payloadSize = 4U;

        UTF_REQUIRE_THROW( FrameCodec::parseRstStream( inconsistent ), UnexpectedException );
    }
}

#endif /* __UTEST_TESTFRAMECODEC_H_ */
