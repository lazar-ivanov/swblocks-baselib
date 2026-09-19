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

#ifndef __UTEST_TESTHPACK_H_
#define __UTEST_TESTHPACK_H_

#include <baselib/http2/HpackDecoder.h>
#include <baselib/http2/HpackEncoder.h>
#include <baselib/http2/HpackHuffman.h>
#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/Globals.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * HPACK, against RFC 7541 rather than against the implementation
 *
 * Every vector below was transcribed from the RFC's own text - the hex dumps of Appendix C, the
 * dynamic table states the RFC prints after each of them down to the per-entry sizes, the 61 rows
 * of the static table in Appendix A, and all 257 rows of the Huffman code in Appendix B. None of
 * it was produced by running the encoder and writing down what came out, which is the failure
 * this kind of test usually has: a vector taken from the implementation agrees with it by
 * construction and can only ever catch a later change of behaviour, never the behaviour being
 * wrong in the first place
 *
 * Appendix C is a strong test of an ENCODER and not only of a decoder, although RFC 7541 defines
 * only decoding and leaves an encoder free. C.3 through C.6 are four chains of three blocks each,
 * on one connection and one table, and they only reproduce octet for octet if the encoder makes
 * the same choice at every field: indexed when the name and the value are both known, the static
 * table before the dynamic one, the lowest index when a name appears several times, and Huffman
 * when it is not longer. Each chain is therefore both a decode test and an encode test, and the
 * table is compared after every single block rather than at the end
 *
 * The Huffman rows here come from the "code as bits" column of Appendix B, while the table in
 * HpackHuffman.h was taken from the "code as hex" and "len" columns beside it. The RFC prints
 * both, and they have to agree - so this is a second reading of the same source rather than a
 * copy of the first, and it is what stops a later hand-edit of that table going unnoticed
 *
 * Nothing here opens a socket, takes a lock or allocates a thread pool
 */

namespace utest
{
    namespace hpack
    {
        typedef bl::http2::HpackDecoder                         decoder_t;
        typedef bl::http2::HpackEncoder                         encoder_t;
        typedef bl::http2::HpackField                           field_t;
        typedef bl::http2::HpackFieldList                       fields_t;

        /**
         * @brief Turns the RFC's hex dumps into octets, ignoring anything which is not a hex digit
         *
         * The dumps are copied in with their spacing, so that a reader can put the test and the
         * RFC side by side and see the same groups of four
         */

        inline auto octets( SAA_in const std::string& hex ) -> std::string
        {
            std::string result;

            unsigned int value = 0U;
            unsigned int nibbles = 0U;

            for( std::size_t i = 0U; i < hex.size(); ++i )
            {
                const auto ch = hex[ i ];

                unsigned int digit;

                if( ch >= '0' && ch <= '9' )
                {
                    digit = static_cast< unsigned int >( ch - '0' );
                }
                else if( ch >= 'a' && ch <= 'f' )
                {
                    digit = static_cast< unsigned int >( ch - 'a' ) + 10U;
                }
                else if( ch >= 'A' && ch <= 'F' )
                {
                    digit = static_cast< unsigned int >( ch - 'A' ) + 10U;
                }
                else
                {
                    continue;
                }

                value = ( value << 4 ) | digit;

                if( ++nibbles == 2U )
                {
                    result.push_back( static_cast< char >( value ) );

                    value = 0U;
                    nibbles = 0U;
                }
            }

            UTF_REQUIRE_EQUAL( nibbles, 0U );

            return result;
        }

        /**
         * @brief One expected decoded field
         */

        struct ExpectedField
        {
            const char*                                         name;
            const char*                                         value;
        };

        /**
         * @brief One expected dynamic table entry, with the size the RFC prints beside it
         *
         * The RFC writes those as "[  1] (s =  55) custom-key: custom-header", and the size is
         * worth comparing on its own: it is the only thing which shows the name-plus-value-plus-32
         * accounting of section 4.1 is being applied to the DECODED lengths rather than to the
         * coded ones, which is why C.5 and C.6 evict at exactly the same points
         */

        struct ExpectedEntry
        {
            const char*                                         name;
            const char*                                         value;
            std::size_t                                         size;
        };

        template
        <
            std::size_t COUNT
        >
        inline void requireFields(
            SAA_in          const fields_t&                     fields,
            SAA_in          const ExpectedField                 ( &expected )[ COUNT ]
            )
        {
            UTF_REQUIRE_EQUAL( fields.size(), COUNT );

            for( std::size_t i = 0U; i < COUNT; ++i )
            {
                UTF_REQUIRE_EQUAL( fields[ i ].name(), expected[ i ].name );
                UTF_REQUIRE_EQUAL( fields[ i ].value(), expected[ i ].value );
            }
        }

        template
        <
            std::size_t COUNT
        >
        inline void requireTable(
            SAA_in          const bl::http2::HpackDynamicTable& table,
            SAA_in          const ExpectedEntry                 ( &expected )[ COUNT ],
            SAA_in          const std::size_t                   totalSize
            )
        {
            UTF_REQUIRE_EQUAL( table.entryCount(), COUNT );
            UTF_REQUIRE_EQUAL( table.size(), totalSize );

            for( std::size_t i = 0U; i < COUNT; ++i )
            {
                UTF_REQUIRE_EQUAL( table.at( i ).name(), expected[ i ].name );
                UTF_REQUIRE_EQUAL( table.at( i ).value(), expected[ i ].value );
                UTF_REQUIRE_EQUAL( table.at( i ).hpackSize(), expected[ i ].size );
            }
        }

        /**
         * @brief Requires that the callable raises the connection error of RFC 9113 section 4.3
         *
         * The error CODE is what is checked and not the message: COMPRESSION_ERROR is what goes
         * into the GOAWAY, and a test which pinned the wording would have to be edited every time
         * a diagnostic improved
         */

        template
        <
            typename FUNC
        >
        inline void requireCompressionError( SAA_in const FUNC& callable )
        {
            bool caught = false;

            try
            {
                callable();
            }
            catch( bl::Http2ProtocolException& e )
            {
                caught = true;

                const auto* code =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_error_code >( e );

                UTF_REQUIRE( code != nullptr );

                UTF_REQUIRE_EQUAL(
                    *code,
                    static_cast< std::uint32_t >(
                        bl::http2::Globals::ERROR_CODE_COMPRESSION_ERROR
                        )
                    );
            }

            UTF_REQUIRE( caught );
        }

        /**
         * @brief Decodes a block and requires that it was complete rather than over the limit
         */

        inline auto decodeBlock(
            SAA_inout       decoder_t&                          decoder,
            SAA_in          const std::string&                  block
            )
            -> fields_t
        {
            fields_t fields;

            const auto outcome = decoder.decode(
                block.data(),
                block.size(),
                8192U,
                fields
                );

            UTF_REQUIRE( outcome == decoder_t::Outcome::Complete );

            return fields;
        }

        inline auto encodeBlock(
            SAA_inout       encoder_t&                          encoder,
            SAA_in          const fields_t&                     fields
            )
            -> std::string
        {
            std::string block;

            encoder.encode( fields, block );

            return block;
        }

        inline auto field(
            SAA_in          const char*                         name,
            SAA_in          const char*                         value
            )
            -> field_t
        {
            return field_t( std::string( name ), std::string( value ) );
        }

        /*
         * The four header lists of Appendix C.3 and C.4, which are the same lists in both, and
         * the three of C.5 and C.6, likewise. They are built once here so that the encode half
         * and the decode half of each case are demonstrably about the same fields
         */

        inline auto requestOne() -> fields_t
        {
            fields_t fields;

            fields.push_back( field( ":method", "GET" ) );
            fields.push_back( field( ":scheme", "http" ) );
            fields.push_back( field( ":path", "/" ) );
            fields.push_back( field( ":authority", "www.example.com" ) );

            return fields;
        }

        inline auto requestTwo() -> fields_t
        {
            auto fields = requestOne();

            fields.push_back( field( "cache-control", "no-cache" ) );

            return fields;
        }

        inline auto requestThree() -> fields_t
        {
            fields_t fields;

            fields.push_back( field( ":method", "GET" ) );
            fields.push_back( field( ":scheme", "https" ) );
            fields.push_back( field( ":path", "/index.html" ) );
            fields.push_back( field( ":authority", "www.example.com" ) );
            fields.push_back( field( "custom-key", "custom-value" ) );

            return fields;
        }

        inline auto responseOne() -> fields_t
        {
            fields_t fields;

            fields.push_back( field( ":status", "302" ) );
            fields.push_back( field( "cache-control", "private" ) );
            fields.push_back( field( "date", "Mon, 21 Oct 2013 20:13:21 GMT" ) );
            fields.push_back( field( "location", "https://www.example.com" ) );

            return fields;
        }

        inline auto responseTwo() -> fields_t
        {
            auto fields = responseOne();

            fields[ 0 ] = field( ":status", "307" );

            return fields;
        }

        inline auto responseThree() -> fields_t
        {
            fields_t fields;

            fields.push_back( field( ":status", "200" ) );
            fields.push_back( field( "cache-control", "private" ) );
            fields.push_back( field( "date", "Mon, 21 Oct 2013 20:13:22 GMT" ) );
            fields.push_back( field( "location", "https://www.example.com" ) );
            fields.push_back( field( "content-encoding", "gzip" ) );

            fields.push_back(
                field(
                    "set-cookie",
                    "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"
                    )
                );

            return fields;
        }

    } // hpack

} // utest

UTF_AUTO_TEST_CASE( Hpack_IntegerRepresentationTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.1.1 - the value 10 in a 5-bit prefix. The three bits above the prefix
     * are "X X X" in the RFC's figure, so the encoder is asked for a zero pattern and the
     * decoder is given both a zero and a non-zero one, which is what shows the prefix is masked
     */

    std::string buffer;

    encoder_t::encodeInteger( 10U, 5U, 0x00U, buffer );

    UTF_REQUIRE_EQUAL( buffer, octets( "0a" ) );

    std::size_t pos = 0U;

    UTF_REQUIRE_EQUAL( decoder_t::decodeInteger( buffer.data(), buffer.size(), pos, 5U ), 10U );
    UTF_REQUIRE_EQUAL( pos, 1U );

    const auto withPattern = octets( "ea" );

    pos = 0U;

    UTF_REQUIRE_EQUAL(
        decoder_t::decodeInteger( withPattern.data(), withPattern.size(), pos, 5U ),
        10U
        );

    /*
     * C.1.2 - the value 1337 in a 5-bit prefix, which the RFC works through octet by octet:
     * the prefix is filled with 31, then 154 and then 10
     */

    buffer.clear();

    encoder_t::encodeInteger( 1337U, 5U, 0x00U, buffer );

    UTF_REQUIRE_EQUAL( buffer, octets( "1f 9a 0a" ) );

    pos = 0U;

    UTF_REQUIRE_EQUAL( decoder_t::decodeInteger( buffer.data(), buffer.size(), pos, 5U ), 1337U );
    UTF_REQUIRE_EQUAL( pos, 3U );

    /*
     * C.1.3 - the value 42 starting at an octet boundary, which is an 8-bit prefix
     */

    buffer.clear();

    encoder_t::encodeInteger( 42U, 8U, 0x00U, buffer );

    UTF_REQUIRE_EQUAL( buffer, octets( "2a" ) );

    pos = 0U;

    UTF_REQUIRE_EQUAL( decoder_t::decodeInteger( buffer.data(), buffer.size(), pos, 8U ), 42U );

    /*
     * Every prefix width round-trips, including the two values either side of the point where
     * the representation changes from "in the prefix" to "in the octets after it"
     */

    for( unsigned int prefixBits = 1U; prefixBits <= 8U; ++prefixBits )
    {
        const std::uint32_t prefixMax = ( 1U << prefixBits ) - 1U;

        const std::uint32_t values[] =
        {
            0U,
            prefixMax - 1U,
            prefixMax,
            prefixMax + 1U,
            1337U,
            0xFFFFFFFFU,
        };

        for( const auto value : values )
        {
            std::string encoded;

            encoder_t::encodeInteger( value, prefixBits, 0x00U, encoded );

            std::size_t at = 0U;

            UTF_REQUIRE_EQUAL(
                decoder_t::decodeInteger( encoded.data(), encoded.size(), at, prefixBits ),
                value
                );

            UTF_REQUIRE_EQUAL( at, encoded.size() );
        }
    }

    /*
     * Section 5.1 - "Integer encodings that exceed implementation limits -- in value or octet
     * length -- MUST be treated as decoding errors". Both halves of that sentence, and the two
     * ways a block can simply stop in the middle of one
     */

    requireCompressionError(
        []() -> void
        {
            std::size_t at = 0U;

            decoder_t::decodeInteger( nullptr, 0U, at, 5U );
        }
        );

    requireCompressionError(
        []() -> void
        {
            const auto truncated = utest::hpack::octets( "1f" );

            std::size_t at = 0U;

            decoder_t::decodeInteger( truncated.data(), truncated.size(), at, 5U );
        }
        );

    requireCompressionError(
        []() -> void
        {
            /*
             * Six continuation octets, every one of them carrying nothing - the cheap way to
             * make a decoder read forever
             */

            const auto padded = utest::hpack::octets( "7f 80 80 80 80 80 00" );

            std::size_t at = 0U;

            decoder_t::decodeInteger( padded.data(), padded.size(), at, 7U );
        }
        );

    requireCompressionError(
        []() -> void
        {
            /*
             * 4294967422, which is 127 past what a 32-bit integer can carry
             */

            const auto tooLarge = utest::hpack::octets( "7f ff ff ff ff 0f" );

            std::size_t at = 0U;

            decoder_t::decodeInteger( tooLarge.data(), tooLarge.size(), at, 7U );
        }
        );
}

UTF_AUTO_TEST_CASE( Hpack_HuffmanCodeTableTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix B, read out of the "code as bits" column - the column HpackHuffman.h did
     * NOT take its table from. Symbols 0 to 255 then EOS at 256
     */

    struct Row
    {
        std::uint32_t                                           code;
        std::uint32_t                                           lengthInBits;
    };

    static const Row g_rows[ HpackHuffman::CODE_COUNT ] =
    {
            /*   0 */ { 0x00001ff8U, 13U }, { 0x007fffd8U, 23U }, { 0x0fffffe2U, 28U }, { 0x0fffffe3U, 28U },
            /*   4 */ { 0x0fffffe4U, 28U }, { 0x0fffffe5U, 28U }, { 0x0fffffe6U, 28U }, { 0x0fffffe7U, 28U },
            /*   8 */ { 0x0fffffe8U, 28U }, { 0x00ffffeaU, 24U }, { 0x3ffffffcU, 30U }, { 0x0fffffe9U, 28U },
            /*  12 */ { 0x0fffffeaU, 28U }, { 0x3ffffffdU, 30U }, { 0x0fffffebU, 28U }, { 0x0fffffecU, 28U },
            /*  16 */ { 0x0fffffedU, 28U }, { 0x0fffffeeU, 28U }, { 0x0fffffefU, 28U }, { 0x0ffffff0U, 28U },
            /*  20 */ { 0x0ffffff1U, 28U }, { 0x0ffffff2U, 28U }, { 0x3ffffffeU, 30U }, { 0x0ffffff3U, 28U },
            /*  24 */ { 0x0ffffff4U, 28U }, { 0x0ffffff5U, 28U }, { 0x0ffffff6U, 28U }, { 0x0ffffff7U, 28U },
            /*  28 */ { 0x0ffffff8U, 28U }, { 0x0ffffff9U, 28U }, { 0x0ffffffaU, 28U }, { 0x0ffffffbU, 28U },
            /*  32 */ { 0x00000014U,  6U }, { 0x000003f8U, 10U }, { 0x000003f9U, 10U }, { 0x00000ffaU, 12U },
            /*  36 */ { 0x00001ff9U, 13U }, { 0x00000015U,  6U }, { 0x000000f8U,  8U }, { 0x000007faU, 11U },
            /*  40 */ { 0x000003faU, 10U }, { 0x000003fbU, 10U }, { 0x000000f9U,  8U }, { 0x000007fbU, 11U },
            /*  44 */ { 0x000000faU,  8U }, { 0x00000016U,  6U }, { 0x00000017U,  6U }, { 0x00000018U,  6U },
            /*  48 */ { 0x00000000U,  5U }, { 0x00000001U,  5U }, { 0x00000002U,  5U }, { 0x00000019U,  6U },
            /*  52 */ { 0x0000001aU,  6U }, { 0x0000001bU,  6U }, { 0x0000001cU,  6U }, { 0x0000001dU,  6U },
            /*  56 */ { 0x0000001eU,  6U }, { 0x0000001fU,  6U }, { 0x0000005cU,  7U }, { 0x000000fbU,  8U },
            /*  60 */ { 0x00007ffcU, 15U }, { 0x00000020U,  6U }, { 0x00000ffbU, 12U }, { 0x000003fcU, 10U },
            /*  64 */ { 0x00001ffaU, 13U }, { 0x00000021U,  6U }, { 0x0000005dU,  7U }, { 0x0000005eU,  7U },
            /*  68 */ { 0x0000005fU,  7U }, { 0x00000060U,  7U }, { 0x00000061U,  7U }, { 0x00000062U,  7U },
            /*  72 */ { 0x00000063U,  7U }, { 0x00000064U,  7U }, { 0x00000065U,  7U }, { 0x00000066U,  7U },
            /*  76 */ { 0x00000067U,  7U }, { 0x00000068U,  7U }, { 0x00000069U,  7U }, { 0x0000006aU,  7U },
            /*  80 */ { 0x0000006bU,  7U }, { 0x0000006cU,  7U }, { 0x0000006dU,  7U }, { 0x0000006eU,  7U },
            /*  84 */ { 0x0000006fU,  7U }, { 0x00000070U,  7U }, { 0x00000071U,  7U }, { 0x00000072U,  7U },
            /*  88 */ { 0x000000fcU,  8U }, { 0x00000073U,  7U }, { 0x000000fdU,  8U }, { 0x00001ffbU, 13U },
            /*  92 */ { 0x0007fff0U, 19U }, { 0x00001ffcU, 13U }, { 0x00003ffcU, 14U }, { 0x00000022U,  6U },
            /*  96 */ { 0x00007ffdU, 15U }, { 0x00000003U,  5U }, { 0x00000023U,  6U }, { 0x00000004U,  5U },
            /* 100 */ { 0x00000024U,  6U }, { 0x00000005U,  5U }, { 0x00000025U,  6U }, { 0x00000026U,  6U },
            /* 104 */ { 0x00000027U,  6U }, { 0x00000006U,  5U }, { 0x00000074U,  7U }, { 0x00000075U,  7U },
            /* 108 */ { 0x00000028U,  6U }, { 0x00000029U,  6U }, { 0x0000002aU,  6U }, { 0x00000007U,  5U },
            /* 112 */ { 0x0000002bU,  6U }, { 0x00000076U,  7U }, { 0x0000002cU,  6U }, { 0x00000008U,  5U },
            /* 116 */ { 0x00000009U,  5U }, { 0x0000002dU,  6U }, { 0x00000077U,  7U }, { 0x00000078U,  7U },
            /* 120 */ { 0x00000079U,  7U }, { 0x0000007aU,  7U }, { 0x0000007bU,  7U }, { 0x00007ffeU, 15U },
            /* 124 */ { 0x000007fcU, 11U }, { 0x00003ffdU, 14U }, { 0x00001ffdU, 13U }, { 0x0ffffffcU, 28U },
            /* 128 */ { 0x000fffe6U, 20U }, { 0x003fffd2U, 22U }, { 0x000fffe7U, 20U }, { 0x000fffe8U, 20U },
            /* 132 */ { 0x003fffd3U, 22U }, { 0x003fffd4U, 22U }, { 0x003fffd5U, 22U }, { 0x007fffd9U, 23U },
            /* 136 */ { 0x003fffd6U, 22U }, { 0x007fffdaU, 23U }, { 0x007fffdbU, 23U }, { 0x007fffdcU, 23U },
            /* 140 */ { 0x007fffddU, 23U }, { 0x007fffdeU, 23U }, { 0x00ffffebU, 24U }, { 0x007fffdfU, 23U },
            /* 144 */ { 0x00ffffecU, 24U }, { 0x00ffffedU, 24U }, { 0x003fffd7U, 22U }, { 0x007fffe0U, 23U },
            /* 148 */ { 0x00ffffeeU, 24U }, { 0x007fffe1U, 23U }, { 0x007fffe2U, 23U }, { 0x007fffe3U, 23U },
            /* 152 */ { 0x007fffe4U, 23U }, { 0x001fffdcU, 21U }, { 0x003fffd8U, 22U }, { 0x007fffe5U, 23U },
            /* 156 */ { 0x003fffd9U, 22U }, { 0x007fffe6U, 23U }, { 0x007fffe7U, 23U }, { 0x00ffffefU, 24U },
            /* 160 */ { 0x003fffdaU, 22U }, { 0x001fffddU, 21U }, { 0x000fffe9U, 20U }, { 0x003fffdbU, 22U },
            /* 164 */ { 0x003fffdcU, 22U }, { 0x007fffe8U, 23U }, { 0x007fffe9U, 23U }, { 0x001fffdeU, 21U },
            /* 168 */ { 0x007fffeaU, 23U }, { 0x003fffddU, 22U }, { 0x003fffdeU, 22U }, { 0x00fffff0U, 24U },
            /* 172 */ { 0x001fffdfU, 21U }, { 0x003fffdfU, 22U }, { 0x007fffebU, 23U }, { 0x007fffecU, 23U },
            /* 176 */ { 0x001fffe0U, 21U }, { 0x001fffe1U, 21U }, { 0x003fffe0U, 22U }, { 0x001fffe2U, 21U },
            /* 180 */ { 0x007fffedU, 23U }, { 0x003fffe1U, 22U }, { 0x007fffeeU, 23U }, { 0x007fffefU, 23U },
            /* 184 */ { 0x000fffeaU, 20U }, { 0x003fffe2U, 22U }, { 0x003fffe3U, 22U }, { 0x003fffe4U, 22U },
            /* 188 */ { 0x007ffff0U, 23U }, { 0x003fffe5U, 22U }, { 0x003fffe6U, 22U }, { 0x007ffff1U, 23U },
            /* 192 */ { 0x03ffffe0U, 26U }, { 0x03ffffe1U, 26U }, { 0x000fffebU, 20U }, { 0x0007fff1U, 19U },
            /* 196 */ { 0x003fffe7U, 22U }, { 0x007ffff2U, 23U }, { 0x003fffe8U, 22U }, { 0x01ffffecU, 25U },
            /* 200 */ { 0x03ffffe2U, 26U }, { 0x03ffffe3U, 26U }, { 0x03ffffe4U, 26U }, { 0x07ffffdeU, 27U },
            /* 204 */ { 0x07ffffdfU, 27U }, { 0x03ffffe5U, 26U }, { 0x00fffff1U, 24U }, { 0x01ffffedU, 25U },
            /* 208 */ { 0x0007fff2U, 19U }, { 0x001fffe3U, 21U }, { 0x03ffffe6U, 26U }, { 0x07ffffe0U, 27U },
            /* 212 */ { 0x07ffffe1U, 27U }, { 0x03ffffe7U, 26U }, { 0x07ffffe2U, 27U }, { 0x00fffff2U, 24U },
            /* 216 */ { 0x001fffe4U, 21U }, { 0x001fffe5U, 21U }, { 0x03ffffe8U, 26U }, { 0x03ffffe9U, 26U },
            /* 220 */ { 0x0ffffffdU, 28U }, { 0x07ffffe3U, 27U }, { 0x07ffffe4U, 27U }, { 0x07ffffe5U, 27U },
            /* 224 */ { 0x000fffecU, 20U }, { 0x00fffff3U, 24U }, { 0x000fffedU, 20U }, { 0x001fffe6U, 21U },
            /* 228 */ { 0x003fffe9U, 22U }, { 0x001fffe7U, 21U }, { 0x001fffe8U, 21U }, { 0x007ffff3U, 23U },
            /* 232 */ { 0x003fffeaU, 22U }, { 0x003fffebU, 22U }, { 0x01ffffeeU, 25U }, { 0x01ffffefU, 25U },
            /* 236 */ { 0x00fffff4U, 24U }, { 0x00fffff5U, 24U }, { 0x03ffffeaU, 26U }, { 0x007ffff4U, 23U },
            /* 240 */ { 0x03ffffebU, 26U }, { 0x07ffffe6U, 27U }, { 0x03ffffecU, 26U }, { 0x03ffffedU, 26U },
            /* 244 */ { 0x07ffffe7U, 27U }, { 0x07ffffe8U, 27U }, { 0x07ffffe9U, 27U }, { 0x07ffffeaU, 27U },
            /* 248 */ { 0x07ffffebU, 27U }, { 0x0ffffffeU, 28U }, { 0x07ffffecU, 27U }, { 0x07ffffedU, 27U },
            /* 252 */ { 0x07ffffeeU, 27U }, { 0x07ffffefU, 27U }, { 0x07fffff0U, 27U }, { 0x03ffffeeU, 26U },
            /* 256 */ { 0x3fffffffU, 30U },
    };

    for( unsigned int symbol = 0U; symbol < HpackHuffman::CODE_COUNT; ++symbol )
    {
        const auto& code = HpackHuffman::codeForSymbol( symbol );

        UTF_REQUIRE_EQUAL( code.code, g_rows[ symbol ].code );
        UTF_REQUIRE_EQUAL( code.lengthInBits, g_rows[ symbol ].lengthInBits );
    }

    /*
     * The example RFC 7541 section 5.2 works through in prose: "the code for the symbol 47
     * (corresponding to the ASCII character "/") consists in the 6 bits "0", "1", "1", "0", "0",
     * "0". This corresponds to the value 0x18 (in hexadecimal) encoded in 6 bits"
     */

    UTF_REQUIRE_EQUAL( HpackHuffman::codeForSymbol( 47U ).code, 0x18U );
    UTF_REQUIRE_EQUAL( HpackHuffman::codeForSymbol( 47U ).lengthInBits, 6U );

    /*
     * EOS, whose most significant bits are what padding has to be made of
     */

    UTF_REQUIRE_EQUAL( HpackHuffman::codeForSymbol( HpackHuffman::EOS_SYMBOL ).code, 0x3FFFFFFFU );
    UTF_REQUIRE_EQUAL( HpackHuffman::codeForSymbol( HpackHuffman::EOS_SYMBOL ).lengthInBits, 30U );

    UTF_REQUIRE_THROW( HpackHuffman::codeForSymbol( 257U ), ArgumentException );

    /*
     * THE CODE IS COMPLETE. Sum of 2 ^ -length over all 257 codes must be exactly 1, which is
     * the Kraft equality - scaled here by 2 ^ 30, the longest code, so that it is integer
     * arithmetic. A wrong LENGTH anywhere breaks this, whatever the code is
     */

    std::uint64_t kraft = 0U;

    for( unsigned int symbol = 0U; symbol < HpackHuffman::CODE_COUNT; ++symbol )
    {
        kraft += static_cast< std::uint64_t >( 1U ) <<
            ( HpackHuffman::MAX_CODE_LENGTH_IN_BITS - g_rows[ symbol ].lengthInBits );
    }

    UTF_REQUIRE_EQUAL(
        kraft,
        static_cast< std::uint64_t >( 1U ) << HpackHuffman::MAX_CODE_LENGTH_IN_BITS
        );

    /*
     * AND IT IS A PREFIX CODE. Completeness alone does not say the codes are distinguishable -
     * a wrong CODE of the right length passes the sum above and fails here
     */

    unsigned int clashes = 0U;

    for( unsigned int i = 0U; i < HpackHuffman::CODE_COUNT; ++i )
    {
        for( unsigned int j = i + 1U; j < HpackHuffman::CODE_COUNT; ++j )
        {
            const auto& shorter =
                g_rows[ i ].lengthInBits <= g_rows[ j ].lengthInBits ? g_rows[ i ] : g_rows[ j ];

            const auto& longer =
                g_rows[ i ].lengthInBits <= g_rows[ j ].lengthInBits ? g_rows[ j ] : g_rows[ i ];

            if( ( longer.code >> ( longer.lengthInBits - shorter.lengthInBits ) ) == shorter.code )
            {
                ++clashes;
            }
        }
    }

    UTF_REQUIRE_EQUAL( clashes, 0U );
}

UTF_AUTO_TEST_CASE( Hpack_HuffmanCodingAndPaddingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * Every octet value survives a round trip on its own, and then all 256 of them together in
     * one string - which is the only way the generated decoding machine is walked over every
     * one of its states
     */

    std::string everyOctet;

    for( unsigned int symbol = 0U; symbol < HpackHuffman::SYMBOL_COUNT; ++symbol )
    {
        const std::string one( 1U, static_cast< char >( symbol ) );

        std::string coded;

        HpackHuffman::encode( one, coded );

        UTF_REQUIRE_EQUAL( coded.size(), HpackHuffman::encodedSizeInOctets( one ) );

        std::string decoded;

        HpackHuffman::decode( coded.data(), coded.size(), decoded );

        UTF_REQUIRE_EQUAL( decoded, one );

        everyOctet.push_back( static_cast< char >( symbol ) );
    }

    std::string codedAll;

    HpackHuffman::encode( everyOctet, codedAll );

    std::string decodedAll;

    HpackHuffman::decode( codedAll.data(), codedAll.size(), decodedAll );

    UTF_REQUIRE_EQUAL( decodedAll, everyOctet );

    /*
     * Against the RFC rather than against ourselves - the Huffman coded authority of C.4.1 and
     * the coded value of C.6.2, decoded from the RFC's octets
     */

    const auto authority = octets( "f1e3 c2e5 f23a 6ba0 ab90 f4ff" );

    std::string value;

    HpackHuffman::decode( authority.data(), authority.size(), value );

    UTF_REQUIRE_EQUAL( value, "www.example.com" );

    const auto status = octets( "640e ff" );

    value.clear();

    HpackHuffman::decode( status.data(), status.size(), value );

    UTF_REQUIRE_EQUAL( value, "307" );

    /*
     * The three padding rules of section 5.2, each on the smallest octet string that can break
     * it. The symbol '0' is the five bits 00000, so one octet holds it with three bits left over
     */

    {
        const auto good = octets( "07" );

        std::string result;

        HpackHuffman::decode( good.data(), good.size(), result );

        UTF_REQUIRE_EQUAL( result, "0" );
    }

    requireCompressionError(
        []() -> void
        {
            /*
             * "A padding not corresponding to the most significant bits of the code for the EOS
             * symbol MUST be treated as a decoding error" - 00000 then 000
             */

            const auto zeroPadding = utest::hpack::octets( "00" );

            std::string result;

            HpackHuffman::decode( zeroPadding.data(), zeroPadding.size(), result );
        }
        );

    requireCompressionError(
        []() -> void
        {
            /*
             * "A padding strictly longer than 7 bits MUST be treated as a decoding error" -
             * 00000 then eleven ones, and no code in Appendix B is all ones before EOS at
             * thirty bits, so nothing consumes them
             */

            const auto longPadding = utest::hpack::octets( "07 ff" );

            std::string result;

            HpackHuffman::decode( longPadding.data(), longPadding.size(), result );
        }
        );

    requireCompressionError(
        []() -> void
        {
            /*
             * "A Huffman-encoded string literal containing the EOS symbol MUST be treated as a
             * decoding error" - thirty-two ones, of which the first thirty are EOS itself
             */

            const auto withEos = utest::hpack::octets( "ff ff ff ff" );

            std::string result;

            HpackHuffman::decode( withEos.data(), withEos.size(), result );
        }
        );

    /*
     * An empty string codes to nothing at all, which is a length of zero and no padding
     */

    std::string empty;

    HpackHuffman::encode( std::string(), empty );

    UTF_REQUIRE( empty.empty() );
    UTF_REQUIRE_EQUAL( HpackHuffman::encodedSizeInOctets( std::string() ), 0U );
}

UTF_AUTO_TEST_CASE( Hpack_StaticTableTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix A, Table 1, all 61 rows in index order
     */

    static const ExpectedField g_expected[] =
    {
        { ":authority",                     ""                          },
        { ":method",                        "GET"                       },
        { ":method",                        "POST"                      },
        { ":path",                          "/"                         },
        { ":path",                          "/index.html"               },
        { ":scheme",                        "http"                      },
        { ":scheme",                        "https"                     },
        { ":status",                        "200"                       },
        { ":status",                        "204"                       },
        { ":status",                        "206"                       },
        { ":status",                        "304"                       },
        { ":status",                        "400"                       },
        { ":status",                        "404"                       },
        { ":status",                        "500"                       },
        { "accept-charset",                 ""                          },
        { "accept-encoding",                "gzip, deflate"             },
        { "accept-language",                ""                          },
        { "accept-ranges",                  ""                          },
        { "accept",                         ""                          },
        { "access-control-allow-origin",    ""                          },
        { "age",                            ""                          },
        { "allow",                          ""                          },
        { "authorization",                  ""                          },
        { "cache-control",                  ""                          },
        { "content-disposition",            ""                          },
        { "content-encoding",               ""                          },
        { "content-language",               ""                          },
        { "content-length",                 ""                          },
        { "content-location",               ""                          },
        { "content-range",                  ""                          },
        { "content-type",                   ""                          },
        { "cookie",                         ""                          },
        { "date",                           ""                          },
        { "etag",                           ""                          },
        { "expect",                         ""                          },
        { "expires",                        ""                          },
        { "from",                           ""                          },
        { "host",                           ""                          },
        { "if-match",                       ""                          },
        { "if-modified-since",              ""                          },
        { "if-none-match",                  ""                          },
        { "if-range",                       ""                          },
        { "if-unmodified-since",            ""                          },
        { "last-modified",                  ""                          },
        { "link",                           ""                          },
        { "location",                       ""                          },
        { "max-forwards",                   ""                          },
        { "proxy-authenticate",             ""                          },
        { "proxy-authorization",            ""                          },
        { "range",                          ""                          },
        { "referer",                        ""                          },
        { "refresh",                        ""                          },
        { "retry-after",                    ""                          },
        { "server",                         ""                          },
        { "set-cookie",                     ""                          },
        { "strict-transport-security",      ""                          },
        { "transfer-encoding",              ""                          },
        { "user-agent",                     ""                          },
        { "vary",                           ""                          },
        { "via",                            ""                          },
        { "www-authenticate",               ""                          },
    };

    UTF_REQUIRE_EQUAL( sizeof( g_expected ) / sizeof( g_expected[ 0 ] ), 61U );

    UTF_REQUIRE_EQUAL(
        static_cast< std::uint32_t >( HpackStaticTable::ENTRY_COUNT ),
        61U
        );

    for( std::uint32_t index = 1U; index <= HpackStaticTable::ENTRY_COUNT; ++index )
    {
        const auto& entry = HpackStaticTable::at( index );

        UTF_REQUIRE_EQUAL( entry.name(), g_expected[ index - 1U ].name );
        UTF_REQUIRE_EQUAL( entry.value(), g_expected[ index - 1U ].value );
    }

    UTF_REQUIRE_THROW( HpackStaticTable::at( 0U ), ArgumentException );
    UTF_REQUIRE_THROW( HpackStaticTable::at( 62U ), ArgumentException );

    /*
     * A lookup answers with the LOWEST matching index, which is what the Appendix C vectors
     * depend on - ":status" is seven entries and its name index has to be 8, not 14
     */

    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( ":status" ), 8U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( ":method" ), 2U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "cache-control" ), 24U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "date" ), 33U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "location" ), 46U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "content-encoding" ), 26U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "set-cookie" ), 55U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "x-not-in-the-table" ), 0U );

    UTF_REQUIRE_EQUAL( HpackStaticTable::findFieldIndex( ":status", "200" ), 8U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findFieldIndex( ":status", "500" ), 14U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findFieldIndex( ":method", "POST" ), 3U );
    UTF_REQUIRE_EQUAL( HpackStaticTable::findFieldIndex( "cache-control", "" ), 24U );

    /*
     * ":status: 302" is the case which makes Appendix C.5 what it is: the name is in the table
     * and the value is not, so it cannot be an indexed representation
     */

    UTF_REQUIRE_EQUAL( HpackStaticTable::findFieldIndex( ":status", "302" ), 0U );

    /*
     * The lookup is exact and not case-insensitive. RFC 9113 section 8.2.1 makes an uppercase
     * name malformed rather than equivalent, so folding here would hide it from the session
     */

    UTF_REQUIRE_EQUAL( HpackStaticTable::findNameIndex( "Date" ), 0U );
}

UTF_AUTO_TEST_CASE( Hpack_DynamicTableAccountingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * Section 4.1 - the size of an entry is its name plus its value plus 32. The first of these
     * is the one the RFC itself prints in C.2.1
     */

    UTF_REQUIRE_EQUAL( field( "custom-key", "custom-header" ).hpackSize(), 55U );
    UTF_REQUIRE_EQUAL( field( ":authority", "www.example.com" ).hpackSize(), 57U );
    UTF_REQUIRE_EQUAL( field( "", "" ).hpackSize(), 32U );

    HpackDynamicTable table( 100U );

    UTF_REQUIRE_EQUAL( table.capacity(), 100U );
    UTF_REQUIRE_EQUAL( table.size(), 0U );
    UTF_REQUIRE_EQUAL( table.entryCount(), 0U );

    /*
     * Newest first, and the combined address space of section 2.3.3 begins at 62
     */

    table.insert( field( "aa", "1" ) );

    UTF_REQUIRE_EQUAL( table.size(), 35U );
    UTF_REQUIRE_EQUAL( table.fieldAtIndex( 62U ).name(), "aa" );

    table.insert( field( "bb", "2" ) );

    UTF_REQUIRE_EQUAL( table.size(), 70U );
    UTF_REQUIRE_EQUAL( table.entryCount(), 2U );
    UTF_REQUIRE_EQUAL( table.at( 0U ).name(), "bb" );
    UTF_REQUIRE_EQUAL( table.at( 1U ).name(), "aa" );
    UTF_REQUIRE_EQUAL( table.fieldAtIndex( 62U ).name(), "bb" );
    UTF_REQUIRE_EQUAL( table.fieldAtIndex( 63U ).name(), "aa" );
    UTF_REQUIRE_EQUAL( table.fieldAtIndex( 61U ).name(), "www-authenticate" );

    UTF_REQUIRE( table.isValidIndex( 63U ) );
    UTF_REQUIRE( ! table.isValidIndex( 64U ) );
    UTF_REQUIRE( ! table.isValidIndex( 0U ) );

    UTF_REQUIRE_EQUAL( table.findFieldIndex( "bb", "2" ), 62U );
    UTF_REQUIRE_EQUAL( table.findNameIndex( "aa" ), 63U );

    /*
     * Section 4.4 - entries go from the oldest end until the new one fits
     */

    table.insert( field( "cc", "3" ) );

    UTF_REQUIRE_EQUAL( table.entryCount(), 2U );
    UTF_REQUIRE_EQUAL( table.size(), 70U );
    UTF_REQUIRE_EQUAL( table.at( 0U ).name(), "cc" );
    UTF_REQUIRE_EQUAL( table.at( 1U ).name(), "bb" );

    /*
     * Section 4.4 again - "It is not an error to attempt to add an entry that is larger than the
     * maximum size; an attempt to add an entry larger than the maximum size causes the table to
     * be emptied of all existing entries and results in an empty table"
     */

    table.insert( field_t( std::string( "n" ), std::string( 80U, 'x' ) ) );

    UTF_REQUIRE_EQUAL( table.entryCount(), 0U );
    UTF_REQUIRE_EQUAL( table.size(), 0U );
    UTF_REQUIRE_EQUAL( table.capacity(), 100U );

    /*
     * Section 4.3 - reducing the maximum evicts from the oldest end until the rest fit, and a
     * maximum of zero clears the table and can then be raised again
     */

    table.insert( field( "dd", "4" ) );
    table.insert( field( "ee", "5" ) );

    UTF_REQUIRE_EQUAL( table.entryCount(), 2U );

    table.setCapacity( 40U );

    UTF_REQUIRE_EQUAL( table.entryCount(), 1U );
    UTF_REQUIRE_EQUAL( table.at( 0U ).name(), "ee" );
    UTF_REQUIRE_EQUAL( table.size(), 35U );

    table.setCapacity( 0U );

    UTF_REQUIRE_EQUAL( table.entryCount(), 0U );
    UTF_REQUIRE_EQUAL( table.size(), 0U );

    table.setCapacity( 4096U );

    table.insert( field( "ff", "6" ) );

    UTF_REQUIRE_EQUAL( table.entryCount(), 1U );

    /*
     * Duplicates are explicitly legal - section 2.3.2 says a decoder must not treat them as an
     * error - and the lookup answers with the newest, which is the lowest index
     */

    table.insert( field( "ff", "6" ) );

    UTF_REQUIRE_EQUAL( table.entryCount(), 2U );
    UTF_REQUIRE_EQUAL( table.findFieldIndex( "ff", "6" ), 62U );

    UTF_REQUIRE_THROW( table.at( 2U ), ArgumentException );
    UTF_REQUIRE_THROW( table.fieldAtIndex( 64U ), ArgumentException );
}

UTF_AUTO_TEST_CASE( Hpack_RepresentationVectorsTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.2 - the four representations, one block each, each on a fresh context
     */

    {
        /*
         * C.2.1 - a literal header field with incremental indexing, literal name and value
         */

        const auto block = octets( "400a 6375 7374 6f6d 2d6b 6579 0d63 7573 746f 6d2d 6865 6164 6572" );

        decoder_t decoder;

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { "custom-key", "custom-header" },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "custom-key", "custom-header", 55U },
        };

        requireTable( decoder.dynamicTable(), g_table, 55U );

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.huffman = false;

        encoder.setPolicy( policy );

        fields_t toEncode;

        toEncode.push_back( field( "custom-key", "custom-header" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, toEncode ), block );

        requireTable( encoder.dynamicTable(), g_table, 55U );
    }

    {
        /*
         * C.2.2 - a literal header field without indexing, indexed name. The dynamic table is
         * left empty, which is the whole point of the representation
         */

        const auto block = octets( "040c 2f73 616d 706c 652f 7061 7468" );

        decoder_t decoder;

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":path", "/sample/path" },
        };

        requireFields( fields, g_expected );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 0U );
        UTF_REQUIRE( ! fields[ 0 ].neverIndexed() );

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.indexingPolicy = HpackIndexingPolicy::WithoutIndexing;
        policy.huffman = false;

        encoder.setPolicy( policy );

        fields_t toEncode;

        toEncode.push_back( field( ":path", "/sample/path" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, toEncode ), block );

        UTF_REQUIRE_EQUAL( encoder.dynamicTable().entryCount(), 0U );
    }

    {
        /*
         * C.2.3 - a literal header field never indexed, literal name. The decoded field has to
         * remember that it arrived that way: section 7.1.3 makes that a property of the field
         * and not of the block
         */

        const auto block = octets( "1008 7061 7373 776f 7264 0673 6563 7265 74" );

        decoder_t decoder;

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { "password", "secret" },
        };

        requireFields( fields, g_expected );

        UTF_REQUIRE( fields[ 0 ].neverIndexed() );
        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 0U );

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.huffman = false;

        encoder.setPolicy( policy );

        fields_t toEncode;

        toEncode.push_back( field_t( "password", "secret", true ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, toEncode ), block );

        UTF_REQUIRE_EQUAL( encoder.dynamicTable().entryCount(), 0U );
    }

    {
        /*
         * C.2.4 - an indexed header field from the static table, one octet
         */

        const auto block = octets( "82" );

        decoder_t decoder;

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method", "GET" },
        };

        requireFields( fields, g_expected );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 0U );

        encoder_t encoder;

        fields_t toEncode;

        toEncode.push_back( field( ":method", "GET" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, toEncode ), block );
    }
}

UTF_AUTO_TEST_CASE( Hpack_RequestVectorsWithoutHuffmanTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.3 - three requests on one connection, so one decoder and one encoder
     * carry their tables from block to block. The table is checked after every block, against
     * what the RFC prints under it
     */

    decoder_t decoder;
    encoder_t encoder;

    encoder_t::Policy policy;

    policy.huffman = false;

    encoder.setPolicy( policy );

    {
        const auto block = octets( "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",    "GET"               },
            { ":scheme",    "http"              },
            { ":path",      "/"                 },
            { ":authority", "www.example.com"   },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { ":authority", "www.example.com", 57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 57U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestOne() ), block );

        requireTable( encoder.dynamicTable(), g_table, 57U );
    }

    {
        /*
         * The authority is now in the dynamic table at index 62, which is the 0xbe
         */

        const auto block = octets( "8286 84be 5808 6e6f 2d63 6163 6865" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",        "GET"               },
            { ":scheme",        "http"              },
            { ":path",          "/"                 },
            { ":authority",     "www.example.com"   },
            { "cache-control",  "no-cache"          },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 110U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestTwo() ), block );

        requireTable( encoder.dynamicTable(), g_table, 110U );
    }

    {
        const auto block =
            octets( "8287 85bf 400a 6375 7374 6f6d 2d6b 6579 0c63 7573 746f 6d2d 7661 6c75 65" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",    "GET"               },
            { ":scheme",    "https"             },
            { ":path",      "/index.html"       },
            { ":authority", "www.example.com"   },
            { "custom-key", "custom-value"      },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "custom-key",     "custom-value",     54U },
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 164U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestThree() ), block );

        requireTable( encoder.dynamicTable(), g_table, 164U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_RequestVectorsWithHuffmanTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.4 - the same three requests with the literals Huffman coded. The
     * tables end each block exactly where C.3's did, because section 4.1 accounts for the
     * decoded lengths
     */

    decoder_t decoder;
    encoder_t encoder;

    {
        const auto block = octets( "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",    "GET"               },
            { ":scheme",    "http"              },
            { ":path",      "/"                 },
            { ":authority", "www.example.com"   },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { ":authority", "www.example.com", 57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 57U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestOne() ), block );

        requireTable( encoder.dynamicTable(), g_table, 57U );
    }

    {
        const auto block = octets( "8286 84be 5886 a8eb 1064 9cbf" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",        "GET"               },
            { ":scheme",        "http"              },
            { ":path",          "/"                 },
            { ":authority",     "www.example.com"   },
            { "cache-control",  "no-cache"          },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 110U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestTwo() ), block );

        requireTable( encoder.dynamicTable(), g_table, 110U );
    }

    {
        const auto block =
            octets( "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925 a849 e95b b8e8 b4bf" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method",    "GET"               },
            { ":scheme",    "https"             },
            { ":path",      "/index.html"       },
            { ":authority", "www.example.com"   },
            { "custom-key", "custom-value"      },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "custom-key",     "custom-value",     54U },
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 164U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestThree() ), block );

        requireTable( encoder.dynamicTable(), g_table, 164U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_ResponseVectorsWithoutHuffmanTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.5 - three responses with SETTINGS_HEADER_TABLE_SIZE at 256 octets,
     * which is what makes them evict. This is the chain that exercises eviction from both
     * directions: C.5.2 evicts one entry to make room for one, and C.5.3 evicts three across
     * three separate insertions inside a single block
     */

    decoder_t decoder( 256U );
    encoder_t encoder( 256U );

    encoder_t::Policy policy;

    policy.huffman = false;

    encoder.setPolicy( policy );

    UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), 256U );

    static const ExpectedField g_responseOne[] =
    {
        { ":status",        "302"                               },
        { "cache-control",  "private"                           },
        { "date",           "Mon, 21 Oct 2013 20:13:21 GMT"     },
        { "location",       "https://www.example.com"           },
    };

    {
        const auto block = octets(
            "4803 3330 3258 0770 7269 7661 7465 611d"
            "4d6f 6e2c 2032 3120 4f63 7420 3230 3133"
            "2032 303a 3133 3a32 3120 474d 546e 1768"
            "7474 7073 3a2f 2f77 7777 2e65 7861 6d70"
            "6c65 2e63 6f6d"
            );

        const auto fields = decodeBlock( decoder, block );

        requireFields( fields, g_responseOne );

        static const ExpectedEntry g_table[] =
        {
            { "location",       "https://www.example.com",          63U },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT",     65U },
            { "cache-control",  "private",                           52U },
            { ":status",        "302",                               42U },
        };

        requireTable( decoder.dynamicTable(), g_table, 222U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseOne() ), block );

        requireTable( encoder.dynamicTable(), g_table, 222U );
    }

    {
        /*
         * The RFC's own note: "The (":status", "302") header field is evicted from the dynamic
         * table to free space to allow adding the (":status", "307") header field"
         */

        const auto block = octets( "4803 3330 37c1 c0bf" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":status",        "307"                               },
            { "cache-control",  "private"                           },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT"     },
            { "location",       "https://www.example.com"           },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { ":status",        "307",                               42U },
            { "location",       "https://www.example.com",          63U },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT",     65U },
            { "cache-control",  "private",                           52U },
        };

        requireTable( decoder.dynamicTable(), g_table, 222U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseTwo() ), block );

        requireTable( encoder.dynamicTable(), g_table, 222U );
    }

    {
        const auto block = octets(
            "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420"
            "3230 3133 2032 303a 3133 3a32 3220 474d"
            "54c0 5a04 677a 6970 7738 666f 6f3d 4153"
            "444a 4b48 514b 425a 584f 5157 454f 5049"
            "5541 5851 5745 4f49 553b 206d 6178 2d61"
            "6765 3d33 3630 303b 2076 6572 7369 6f6e"
            "3d31"
            );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":status",            "200"                               },
            { "cache-control",      "private"                           },
            { "date",               "Mon, 21 Oct 2013 20:13:22 GMT"     },
            { "location",           "https://www.example.com"           },
            { "content-encoding",   "gzip"                              },
            {
                "set-cookie",
                "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"
            },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            {
                "set-cookie",
                "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
                98U
            },
            { "content-encoding",   "gzip",                              52U },
            { "date",               "Mon, 21 Oct 2013 20:13:22 GMT",     65U },
        };

        requireTable( decoder.dynamicTable(), g_table, 215U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseThree() ), block );

        requireTable( encoder.dynamicTable(), g_table, 215U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_ResponseVectorsWithHuffmanTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 Appendix C.6 - the same three responses Huffman coded, again with the table at
     * 256 octets. The RFC says it explicitly: "The eviction mechanism uses the length of the
     * decoded literal values, so the same evictions occur as in the previous section", and the
     * table states below are byte for byte the ones in C.5
     *
     * C.6.2 is the vector which fixes the encoder's Huffman rule: the value "307" codes to
     * exactly three octets and is three octets long, and the RFC codes it anyway - so the rule
     * has to be "when it is not longer", not "when it is shorter"
     */

    decoder_t decoder( 256U );
    encoder_t encoder( 256U );

    {
        const auto block = octets(
            "4882 6402 5885 aec3 771a 4b61 96d0 7abe"
            "9410 54d4 44a8 2005 9504 0b81 66e0 82a6"
            "2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8"
            "e9ae 82ae 43d3"
            );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":status",        "302"                               },
            { "cache-control",  "private"                           },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT"     },
            { "location",       "https://www.example.com"           },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { "location",       "https://www.example.com",          63U },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT",     65U },
            { "cache-control",  "private",                           52U },
            { ":status",        "302",                               42U },
        };

        requireTable( decoder.dynamicTable(), g_table, 222U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseOne() ), block );

        requireTable( encoder.dynamicTable(), g_table, 222U );
    }

    {
        const auto block = octets( "4883 640e ffc1 c0bf" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":status",        "307"                               },
            { "cache-control",  "private"                           },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT"     },
            { "location",       "https://www.example.com"           },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            { ":status",        "307",                               42U },
            { "location",       "https://www.example.com",          63U },
            { "date",           "Mon, 21 Oct 2013 20:13:21 GMT",     65U },
            { "cache-control",  "private",                           52U },
        };

        requireTable( decoder.dynamicTable(), g_table, 222U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseTwo() ), block );

        requireTable( encoder.dynamicTable(), g_table, 222U );
    }

    {
        const auto block = octets(
            "88c1 6196 d07a be94 1054 d444 a820 0595"
            "040b 8166 e084 a62d 1bff c05a 839b d9ab"
            "77ad 94e7 821d d7f2 e6c7 b335 dfdf cd5b"
            "3960 d5af 2708 7f36 72c1 ab27 0fb5 291f"
            "9587 3160 65c0 03ed 4ee5 b106 3d50 07"
            );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":status",            "200"                               },
            { "cache-control",      "private"                           },
            { "date",               "Mon, 21 Oct 2013 20:13:22 GMT"     },
            { "location",           "https://www.example.com"           },
            { "content-encoding",   "gzip"                              },
            {
                "set-cookie",
                "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"
            },
        };

        requireFields( fields, g_expected );

        static const ExpectedEntry g_table[] =
        {
            {
                "set-cookie",
                "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
                98U
            },
            { "content-encoding",   "gzip",                              52U },
            { "date",               "Mon, 21 Oct 2013 20:13:22 GMT",     65U },
        };

        requireTable( decoder.dynamicTable(), g_table, 215U );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, responseThree() ), block );

        requireTable( encoder.dynamicTable(), g_table, 215U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_CompressionErrorTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * Everything RFC 7541 calls a decoding error, which RFC 9113 section 4.3 makes a connection
     * error of type COMPRESSION_ERROR. Each of these is the smallest block which breaks its rule
     */

    const char* const g_malformed[] =
    {
        /*
         * Section 6.1 - "The index value of 0 is not used. It MUST be treated as a decoding
         * error if found in an indexed header field representation"
         */

        "80",

        /*
         * Section 2.3.3 - "Indices strictly greater than the sum of the lengths of both tables
         * MUST be treated as a decoding error". 62 with an empty dynamic table
         */

        "be",

        /*
         * The same, as the NAME index of a literal representation
         */

        "7e",

        /*
         * A string literal whose length runs past the end of the block
         */

        "400a 6162 63",

        /*
         * A block which stops in the middle of a multi-octet integer
         */

        "40 7f",

        /*
         * A block which stops where a value was expected
         */

        "400a 6375 7374 6f6d 2d6b 6579",

        /*
         * Section 4.2 - a dynamic table size update has to open the block. Here an indexed
         * representation precedes it
         */

        "82 20",

        /*
         * A size update asking for 4097 when 4096 was advertised
         */

        "3f e2 1f",

        /*
         * A Huffman coded name carrying the EOS symbol
         */

        "40 84 ffff ffff 0161",
    };

    for( const auto malformed : g_malformed )
    {
        requireCompressionError(
            [ malformed ]() -> void
            {
                const auto block = utest::hpack::octets( malformed );

                decoder_t decoder;

                fields_t fields;

                decoder.decode( block.data(), block.size(), 8192U, fields );
            }
            );
    }

    /*
     * The two which are NOT errors, and would be easy to make into one. A name index of zero in
     * a literal representation means the name follows as a string literal - it is only in an
     * INDEXED representation that zero is forbidden. And an empty block decodes to no fields at
     * all rather than failing: a HEADERS frame carrying an empty field block is legal
     */

    {
        decoder_t decoder;

        const auto block = octets( "400a 6375 7374 6f6d 2d6b 6579 0161" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { "custom-key", "a" },
        };

        requireFields( fields, g_expected );
    }

    {
        decoder_t decoder;

        fields_t fields;

        const auto outcome = decoder.decode( nullptr, 0U, 8192U, fields );

        UTF_REQUIRE( outcome == decoder_t::Outcome::Complete );
        UTF_REQUIRE( fields.empty() );
    }
}

UTF_AUTO_TEST_CASE( Hpack_SizeUpdateTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * RFC 7541 section 6.3 and 4.2 on the decoding side
     */

    decoder_t decoder;

    UTF_REQUIRE_EQUAL( decoder.maxDynamicTableSize(), 4096U );
    UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), 4096U );

    /*
     * Fill the table, then a block which opens with a size update of zero - which section 4.2
     * describes as the way to clear the table completely - followed by the field it then has
     * room for again once the size goes back up
     */

    {
        const auto block = octets( "400a 6375 7374 6f6d 2d6b 6579 0161" );

        decodeBlock( decoder, block );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 1U );
    }

    {
        const auto block = octets( "20 82" );

        const auto fields = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { ":method", "GET" },
        };

        requireFields( fields, g_expected );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), 0U );
        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 0U );
    }

    {
        /*
         * Two size updates opening one block, which is the maximum an encoder may send and the
         * shape section 4.2 prescribes when the size changed twice: the smallest first, then the
         * final one. 0x20 is zero and 0x3f e1 1f is 4096
         */

        const auto block = octets( "20 3f e1 1f 82" );

        decodeBlock( decoder, block );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), 4096U );
    }

    /*
     * And the encoder's half - a change signalled at the start of the next block, once
     */

    {
        encoder_t encoder;

        encoder.setDynamicTableCapacity( 0U );

        fields_t fields;

        fields.push_back( field( ":method", "GET" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, fields ), octets( "20 82" ) );
        UTF_REQUIRE_EQUAL( encoder.dynamicTable().capacity(), 0U );

        /*
         * It is signalled once and not on every block after it
         */

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, fields ), octets( "82" ) );
    }

    {
        /*
         * Two changes before a block is encoded: section 4.2 requires the SMALLEST to be
         * signalled and then the final size, so that a decoder evicts on the way down
         */

        encoder_t encoder;

        encoder.setDynamicTableCapacity( 100U );
        encoder.setDynamicTableCapacity( 0U );
        encoder.setDynamicTableCapacity( 2048U );

        fields_t fields;

        fields.push_back( field( ":method", "GET" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, fields ), octets( "20 3f e1 0f 82" ) );
        UTF_REQUIRE_EQUAL( encoder.dynamicTable().capacity(), 2048U );
    }

    {
        /*
         * A change back to what it already is signals nothing
         */

        encoder_t encoder;

        encoder.setDynamicTableCapacity( 4096U );

        fields_t fields;

        fields.push_back( field( ":method", "GET" ) );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, fields ), octets( "82" ) );
    }
}

UTF_AUTO_TEST_CASE( Hpack_DecodedSizeLimitTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * Design 4.6, first row - a block which decodes to more than our
     * SETTINGS_MAX_HEADER_LIST_SIZE resets the STREAM, and the block is still consumed so that
     * the dynamic table follows the peer's encoder. That is what makes this different from every
     * other failure here: the connection carries on afterwards, so the table has to be right
     *
     * The limit is a parameter of decode( ) and not a constant anywhere, because it is whatever
     * the active profile advertises and RFC 9113 gives the setting no initial value
     */

    const auto block =
        octets( "8286 84be 5808 6e6f 2d63 6163 6865" );

    /*
     * The same block, decoded by a decoder primed exactly as C.3.2's was
     */

    const auto prime = octets( "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d" );

    /*
     * RFC 9113 section 6.5.2 sizes a decoded list as the name plus the value plus 32 per field,
     * which is the same accounting as the dynamic table's. ":method: GET" is 7 + 3 + 32,
     * ":scheme: http" 7 + 4 + 32, ":path: /" 5 + 1 + 32, ":authority: www.example.com"
     * 10 + 15 + 32 and "cache-control: no-cache" 13 + 8 + 32 - so 42 + 43 + 38 + 57 + 53, 233
     */

    const std::size_t exactSize = 233U;

    {
        decoder_t decoder;

        decodeBlock( decoder, prime );

        fields_t fields;

        UTF_REQUIRE(
            decoder.decode( block.data(), block.size(), exactSize, fields ) ==
                decoder_t::Outcome::Complete
            );

        UTF_REQUIRE_EQUAL( fields.size(), 5U );

        std::size_t total = 0U;

        for( std::size_t i = 0U; i < fields.size(); ++i )
        {
            total += fields[ i ].hpackSize();
        }

        UTF_REQUIRE_EQUAL( total, exactSize );

        static const ExpectedEntry g_table[] =
        {
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 110U );
    }

    {
        /*
         * One octet less than the block needs. The fields are dropped, the outcome says so, and
         * - the point of the case - the dynamic table ends exactly where it ended above
         */

        decoder_t decoder;

        decodeBlock( decoder, prime );

        fields_t fields;

        UTF_REQUIRE(
            decoder.decode( block.data(), block.size(), exactSize - 1U, fields ) ==
                decoder_t::Outcome::ExceededDecodedSizeLimit
            );

        UTF_REQUIRE( fields.empty() );

        static const ExpectedEntry g_table[] =
        {
            { "cache-control",  "no-cache",         53U },
            { ":authority",     "www.example.com",  57U },
        };

        requireTable( decoder.dynamicTable(), g_table, 110U );

        /*
         * And the connection carries on: the next block decodes against that table, which is
         * only possible because the block above was consumed to the end
         */

        const auto next = octets( "82 be bf" );

        const auto fields2 = decodeBlock( decoder, next );

        static const ExpectedField g_expected[] =
        {
            { ":method",        "GET"               },
            { "cache-control",  "no-cache"          },
            { ":authority",     "www.example.com"   },
        };

        requireFields( fields2, g_expected );
    }

    {
        /*
         * A limit of zero rejects even one field, and a field is never truncated to fit. The
         * decoder is primed first because the block names index 62, which only exists once the
         * first request's authority is in the table
         */

        decoder_t decoder;

        decodeBlock( decoder, prime );

        fields_t fields;

        UTF_REQUIRE(
            decoder.decode( block.data(), block.size(), 0U, fields ) ==
                decoder_t::Outcome::ExceededDecodedSizeLimit
            );

        UTF_REQUIRE( fields.empty() );

        /*
         * And the block was still consumed - the table carries the entry it inserted
         */

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 2U );
        UTF_REQUIRE_EQUAL( decoder.dynamicTable().size(), 110U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_DynamicTableAfterMidBlockFailureTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * THE PROPERTY THIS CASE EXISTS FOR. A header block is decoded one representation at a time
     * and each may mutate the dynamic table. If a later one is malformed, a decoder which simply
     * gives up leaves the table half-updated - and every index into it afterwards is then wrong
     * in a way nothing detects
     *
     * What is pinned is the strongest form: after a failed block the table is not merely "some
     * consistent state" but EXACTLY the state the block found, proved by decoding the same good
     * block before and after and requiring the identical result
     */

    static const ExpectedEntry g_primed[] =
    {
        { "cache-control",  "no-cache",         53U },
        { ":authority",     "www.example.com",  57U },
    };

    /*
     * Three blocks that each fail after doing real work to the table first. The first inserts
     * one entry and then names an index that does not exist; the second opens with a size update
     * small enough to evict everything before failing; the third fails inside the Huffman
     * decoder, three calls down from decode( )
     */

    const char* const g_failing[] =
    {
        "400a 6375 7374 6f6d 2d6b 6579 0161 ff00",
        "20 400a 6375 7374 6f6d 2d6b 6579 0161 be",
        "400a 6375 7374 6f6d 2d6b 6579 0161 4084 ffff ffff 0162",
    };

    for( const auto failing : g_failing )
    {
        decoder_t decoder;

        decodeBlock( decoder, octets( "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d" ) );
        decodeBlock( decoder, octets( "8286 84be 5808 6e6f 2d63 6163 6865" ) );

        requireTable( decoder.dynamicTable(), g_primed, 110U );

        const auto capacityBefore = decoder.dynamicTable().capacity();

        const auto block = octets( failing );

        requireCompressionError(
            [ &decoder, &block ]() -> void
            {
                fields_t fields;

                decoder.decode( block.data(), block.size(), 8192U, fields );
            }
            );

        /*
         * Every entry, every entry's size, the total and the capacity - unchanged
         */

        requireTable( decoder.dynamicTable(), g_primed, 110U );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), capacityBefore );

        /*
         * And the table is not just the right shape but the right contents. The two entries are
         * read back by INDEX, which is the thing a half-rolled-back table would get wrong while
         * still looking plausible: 62 is the newest entry and 63 the one under it
         */

        const auto fields = decodeBlock( decoder, octets( "be bf" ) );

        static const ExpectedField g_expected[] =
        {
            { "cache-control",  "no-cache"          },
            { ":authority",     "www.example.com"   },
        };

        requireFields( fields, g_expected );

        requireTable( decoder.dynamicTable(), g_primed, 110U );
    }

    /*
     * The same for a failure on the very first representation of a block, where there is nothing
     * to roll back but the bookkeeping still has to be right
     */

    {
        decoder_t decoder;

        const auto bad = octets( "be" );

        requireCompressionError(
            [ &decoder, &bad ]() -> void
            {
                fields_t fields;

                decoder.decode( bad.data(), bad.size(), 8192U, fields );
            }
            );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 0U );
        UTF_REQUIRE_EQUAL( decoder.dynamicTable().size(), 0U );
        UTF_REQUIRE_EQUAL( decoder.dynamicTable().capacity(), 4096U );

        const auto fields = decodeBlock( decoder, octets( "400a 6375 7374 6f6d 2d6b 6579 0161" ) );

        static const ExpectedField g_expected[] =
        {
            { "custom-key", "a" },
        };

        requireFields( fields, g_expected );

        UTF_REQUIRE_EQUAL( decoder.dynamicTable().entryCount(), 1U );
    }
}

UTF_AUTO_TEST_CASE( Hpack_EncoderPolicyTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::hpack;

    /*
     * The encoder's own choices, which RFC 7541 leaves open and design 4.2 and 6.4 make a policy
     */

    {
        /*
         * The defaults - incremental indexing, Huffman on, cookies left whole
         */

        encoder_t encoder;

        UTF_REQUIRE( encoder.policy().indexingPolicy == HpackIndexingPolicy::Incremental );
        UTF_REQUIRE( encoder.policy().huffman );
        UTF_REQUIRE( ! encoder.policy().crumbleCookies );
    }

    {
        /*
         * RFC 9113 section 8.2.1 - names go on the wire lowercase, whatever spelling the caller
         * used. http::HeaderList keeps that spelling on purpose, so this is where it is folded,
         * and the bridge from a HeaderList is exercised on the same fields
         */

        encoder_t encoder;

        fields_t fields;

        http::HeaderList headers;

        headers.append( "Accept-Encoding", "gzip, deflate" );
        headers.append( "X-Custom-Header", "Value-With-Caps" );

        HpackFields::appendAll( headers, fields );

        UTF_REQUIRE_EQUAL( fields.size(), 2U );
        UTF_REQUIRE_EQUAL( fields[ 0 ].name(), "Accept-Encoding" );

        std::string block;

        encoder.encode( fields, block );

        decoder_t decoder;

        const auto decoded = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { "accept-encoding",    "gzip, deflate"     },
            { "x-custom-header",    "Value-With-Caps"   },
        };

        requireFields( decoded, g_expected );

        /*
         * "accept-encoding: gzip, deflate" is static entry 16, so the fold happened before the
         * lookup rather than after it - a one-octet block is the proof
         */

        UTF_REQUIRE_EQUAL( static_cast< unsigned char >( block[ 0 ] ), 0x90U );
    }

    {
        /*
         * A field marked never-indexed keeps that representation even when the very same name
         * and value are sitting in the dynamic table, which is what section 7.1.3 is for. An
         * encoder which asked the table first would answer with one indexed octet and put the
         * value into a compression context
         */

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.huffman = false;

        encoder.setPolicy( policy );

        fields_t indexed;

        indexed.push_back( field( "password", "secret" ) );

        UTF_REQUIRE_EQUAL(
            encodeBlock( encoder, indexed ),
            octets( "40 08 7061 7373 776f 7264 0673 6563 7265 74" )
            );

        UTF_REQUIRE_EQUAL( encoder.dynamicTable().entryCount(), 1U );
        UTF_REQUIRE_EQUAL( encoder.dynamicTable().findFieldIndex( "password", "secret" ), 62U );

        /*
         * The same field again is now one octet, the indexed representation of index 62
         */

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, indexed ), octets( "be" ) );

        /*
         * And marked never-indexed it is not, although the full match is still there. The name
         * is still sent as an index - section 6.2.3 allows that and only the VALUE has to be
         * literal - so this is 0x1f 0x2f, which is the four-bit never-indexed prefix carrying
         * 15 + 47 = 62, followed by the value as a string literal
         */

        fields_t sensitive;

        sensitive.push_back( field_t( "password", "secret", true ) );

        UTF_REQUIRE_EQUAL(
            encodeBlock( encoder, sensitive ),
            octets( "1f 2f 06 7365 6372 6574" )
            );

        /*
         * And it did not enter the table a second time
         */

        UTF_REQUIRE_EQUAL( encoder.dynamicTable().entryCount(), 1U );
    }

    {
        /*
         * RFC 9113 section 8.2.3 - a cookie split into one field per crumb. The RFC's own
         * example is that "cookie: a=b; c=d; e=f" and three separate cookie fields are
         * semantically equivalent, and the split is on the two-octet delimiter it names for
         * putting them back together, so it is reversible
         */

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.crumbleCookies = true;
        policy.huffman = false;

        encoder.setPolicy( policy );

        fields_t fields;

        fields.push_back( field( "cookie", "a=b; c=d; e=f" ) );

        std::string block;

        encoder.encode( fields, block );

        decoder_t decoder;

        const auto decoded = decodeBlock( decoder, block );

        static const ExpectedField g_expected[] =
        {
            { "cookie", "a=b" },
            { "cookie", "c=d" },
            { "cookie", "e=f" },
        };

        requireFields( decoded, g_expected );

        std::string rejoined;

        for( std::size_t i = 0U; i < decoded.size(); ++i )
        {
            if( i != 0U )
            {
                rejoined += "; ";
            }

            rejoined += decoded[ i ].value();
        }

        UTF_REQUIRE_EQUAL( rejoined, "a=b; c=d; e=f" );
    }

    {
        /*
         * With crumbling off the same value is one field, and a cookie which carries no
         * delimiter is one field either way - the split is never lossy
         */

        encoder_t encoder;

        fields_t fields;

        fields.push_back( field( "cookie", "a=b; c=d" ) );

        decoder_t decoder;

        const auto decoded = decodeBlock( decoder, encodeBlock( encoder, fields ) );

        static const ExpectedField g_expected[] =
        {
            { "cookie", "a=b; c=d" },
        };

        requireFields( decoded, g_expected );

        encoder_t crumbling;

        encoder_t::Policy policy;

        policy.crumbleCookies = true;

        crumbling.setPolicy( policy );

        fields_t single;

        single.push_back( field( "cookie", "a=b" ) );

        decoder_t decoder2;

        const auto decodedSingle =
            decodeBlock( decoder2, encodeBlock( crumbling, single ) );

        static const ExpectedField g_expectedSingle[] =
        {
            { "cookie", "a=b" },
        };

        requireFields( decodedSingle, g_expectedSingle );
    }

    {
        /*
         * Huffman off is a switch and not a suggestion - "www.example.com" then goes out as its
         * own fifteen octets, which is the C.3.1 form rather than the C.4.1 one
         */

        encoder_t encoder;

        encoder_t::Policy policy;

        policy.huffman = false;

        encoder.setPolicy( policy );

        UTF_REQUIRE_EQUAL( encodeBlock( encoder, requestOne() ),
            octets( "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d" ) );
    }

    {
        /*
         * WithoutIndexing and NeverIndexed as a policy rather than per field - neither touches
         * the table, and the two differ only in the fourth bit of the first octet
         */

        encoder_t withoutIndexing;
        encoder_t neverIndexed;

        encoder_t::Policy policy;

        policy.huffman = false;
        policy.indexingPolicy = HpackIndexingPolicy::WithoutIndexing;

        withoutIndexing.setPolicy( policy );

        policy.indexingPolicy = HpackIndexingPolicy::NeverIndexed;

        neverIndexed.setPolicy( policy );

        fields_t fields;

        fields.push_back( field( ":path", "/sample/path" ) );

        const auto a = encodeBlock( withoutIndexing, fields );
        const auto b = encodeBlock( neverIndexed, fields );

        /*
         * The first is C.2.2 exactly; the second differs from it in one bit, the 0x10 which
         * separates section 6.2.2 from section 6.2.3
         */

        UTF_REQUIRE_EQUAL( a, octets( "040c 2f73 616d 706c 652f 7061 7468" ) );
        UTF_REQUIRE_EQUAL( b, octets( "140c 2f73 616d 706c 652f 7061 7468" ) );

        UTF_REQUIRE_EQUAL( withoutIndexing.dynamicTable().entryCount(), 0U );
        UTF_REQUIRE_EQUAL( neverIndexed.dynamicTable().entryCount(), 0U );

        decoder_t decoderA;
        decoder_t decoderB;

        UTF_REQUIRE( ! decodeBlock( decoderA, a )[ 0 ].neverIndexed() );
        UTF_REQUIRE( decodeBlock( decoderB, b )[ 0 ].neverIndexed() );
    }
}

#endif /* __UTEST_TESTHPACK_H_ */
