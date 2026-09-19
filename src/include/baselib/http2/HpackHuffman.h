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

#ifndef __BL_HTTP2_HPACKHUFFMAN_H_
#define __BL_HTTP2_HPACKHUFFMAN_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class HpackErrorT - how HPACK reports that a header block cannot be decoded
         *
         * RFC 7541 calls these "decoding errors" and leaves what to do about them to the
         * protocol using HPACK. RFC 9113 section 4.3 is that protocol, and it is unambiguous:
         * a decoding error in a field block MUST be treated as a CONNECTION error of type
         * COMPRESSION_ERROR. There is no stream-level variant, because HPACK state is shared by
         * every stream on the connection and a decoder which has lost its place cannot recover
         * it from a later block
         *
         * This is deliberately NOT the way the decoder reports that a block exceeded the
         * decoded header list size - that one is a stream reset with the block still consumed
         * (design 4.6, first row), so it is an outcome and not an exception
         *
         * It lives in this header, rather than in one of its own, because the two files that
         * raise it - this one and HpackDecoder.h - both already include it, and a fifth header
         * carrying twelve lines would be harder to find than this comment
         */

        template
        <
            typename E = void
        >
        class HpackErrorT
        {
            BL_DECLARE_STATIC( HpackErrorT )

        public:

            /**
             * @brief Throws the connection error of RFC 9113 section 4.3
             *
             * The reason is a fixed description of which rule was broken, never a copy of what
             * arrived on the wire: a header block is attacker-controlled and this message reaches
             * logs, which is the same argument http::HeaderList makes for not echoing a rejected
             * name. Positions and lengths are safe and are what is actually useful
             */

            static void throwCompressionError( SAA_in const std::string& reason )
            {
                BL_THROW(
                    Http2ProtocolException()
                        << eh::errinfo_http2_error_code(
                            static_cast< std::uint32_t >( Globals::ERROR_CODE_COMPRESSION_ERROR )
                            ),
                    BL_MSG()
                        << "HPACK decoding error - "
                        << reason
                    );
            }
        };

        typedef HpackErrorT<> HpackError;

        /**
         * @brief class HpackHuffmanT - the static Huffman code of RFC 7541 Appendix B
         *
         * The code table below is the RFC's, transcribed from the "code as hex" and "len"
         * columns of Appendix B, one row per symbol, symbols 0 to 255 followed by EOS at 256.
         * It is the ONLY table in this file. The decoder does not carry a second, hand-written
         * state machine beside it: the state machine is GENERATED from this table the first time
         * a string is decoded (design 4.2), so the two cannot disagree - a second copy that
         * differs in one row is the kind of defect which survives review and then corrupts one
         * character in one header, months later, against one peer
         *
         * The generated machine is a binary trie over the 257 codes, walked one bit at a time.
         * That is a state machine whose states are its nodes and whose input alphabet is one
         * bit, and it is the form which can be derived from the table with no further tables:
         * a nibble- or octet-at-a-time machine would be faster and would need its own generation
         * step, which is a cost this client does not need to pay. A response header block is a
         * few hundred octets, so this is a few thousand steps per response
         *
         * Building the trie is also a self-check. The code is complete - its Kraft sum is
         * exactly one - so every node of a correctly transcribed table ends up with both of its
         * children assigned. The builder verifies that, which means a transposed row or a wrong
         * length fails loudly at first use rather than silently decoding one symbol wrongly
         *
         * PADDING, which is where implementations get this wrong (RFC 7541 section 5.2):
         *
         *  - an incomplete code at the end of the data is padding and is discarded
         *  - padding STRICTLY LONGER than seven bits is a decoding error - so a decoder must not
         *    simply stop at the end of the octets and call whatever is left over "padding"
         *  - padding which is not the most significant bits of the EOS code is a decoding error.
         *    EOS is thirty one-bits, so this is exactly "the leftover bits must all be ones"
         *  - a string containing the EOS symbol itself is a decoding error, even though the
         *    symbol is in the table and has a code
         */

        template
        <
            typename E = void
        >
        class HpackHuffmanT
        {
            BL_DECLARE_STATIC( HpackHuffmanT )

        public:

            enum : unsigned int
            {
                /*
                 * 256 octet symbols plus EOS, which is symbol 256
                 */

                SYMBOL_COUNT                        = 256U,
                EOS_SYMBOL                          = 256U,
                CODE_COUNT                          = 257U,

                /*
                 * The longest code in Appendix B is thirty bits (EOS and four other symbols),
                 * and padding is at most seven bits - anything longer is a decoding error
                 */

                MAX_CODE_LENGTH_IN_BITS             = 30U,
                MAX_PADDING_LENGTH_IN_BITS          = 7U,
            };

            /**
             * @brief One row of Appendix B - the code aligned on its least significant bit, and
             * how many bits of it are significant
             *
             * Both fields are 32-bit although the length never exceeds 30, because a narrower
             * length field would make every row of the table below a narrowing conversion in a
             * braced initializer, and the two kilobytes this costs are not worth that
             */

            struct Code
            {
                std::uint32_t                       code;
                std::uint32_t                       lengthInBits;
            };

            static const Code& codeForSymbol( SAA_in const unsigned int symbol )
            {
                BL_CHK_ARG( symbol < CODE_COUNT, symbol );

                return codeTable()[ symbol ];
            }

            /**
             * @brief How many octets the Huffman form of this string would take
             *
             * The encoder needs this before it commits to a representation, because RFC 7541
             * leaves the choice to it and the only sensible rule is "Huffman when it is shorter"
             */

            static std::size_t encodedSizeInOctets( SAA_in const std::string& value ) NOEXCEPT
            {
                std::uint64_t bits = 0U;

                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    bits += codeTable()[ static_cast< unsigned char >( value[ i ] ) ].lengthInBits;
                }

                return static_cast< std::size_t >( ( bits + 7U ) / 8U );
            }

            /**
             * @brief Appends the Huffman form of the string to the buffer
             *
             * The encoded data is the bitwise concatenation of the codes, padded to the next
             * octet boundary with the most significant bits of the EOS code - which are ones
             */

            static void encode(
                SAA_in          const std::string&              value,
                SAA_inout       std::string&                    buffer
                )
            {
                /*
                 * At most 30 bits enter the accumulator at a time and at most 7 are ever left
                 * in it, so 64 bits is more than the 37 this can hold
                 */

                std::uint64_t accumulator = 0U;
                unsigned int accumulatedBits = 0U;

                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    const auto& entry = codeTable()[ static_cast< unsigned char >( value[ i ] ) ];

                    accumulator = ( accumulator << entry.lengthInBits ) | entry.code;
                    accumulatedBits += entry.lengthInBits;

                    while( accumulatedBits >= 8U )
                    {
                        accumulatedBits -= 8U;

                        buffer.push_back(
                            static_cast< char >( ( accumulator >> accumulatedBits ) & 0xFFU )
                            );
                    }
                }

                if( accumulatedBits != 0U )
                {
                    const auto shift = 8U - accumulatedBits;

                    buffer.push_back(
                        static_cast< char >(
                            ( ( accumulator << shift ) | ( ( 1ULL << shift ) - 1ULL ) ) & 0xFFU
                            )
                        );
                }
            }

            /**
             * @brief Appends the decoded form of Huffman-encoded data to the string
             *
             * Throws the connection error of RFC 9113 section 4.3 on an encoded EOS symbol or on
             * padding which breaks either of the two rules of RFC 7541 section 5.2
             */

            static void decode(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::string&                    value
                )
            {
                BL_CHK_ARG( data != nullptr || size == 0U, data );

                const auto& nodes = decodeStateMachine();

                std::size_t node = 0U;
                unsigned int bitsSinceSymbol = 0U;
                bool allOnesSinceSymbol = true;

                for( std::size_t i = 0U; i < size; ++i )
                {
                    const auto octet = static_cast< unsigned char >( data[ i ] );

                    for( unsigned int bitIndex = 8U; bitIndex > 0U; --bitIndex )
                    {
                        const auto bit = ( octet >> ( bitIndex - 1U ) ) & 1U;

                        ++bitsSinceSymbol;
                        allOnesSinceSymbol = allOnesSinceSymbol && ( bit == 1U );

                        const auto child = nodes[ node ].children[ bit ];

                        if( child < 0 )
                        {
                            const auto symbol =
                                static_cast< unsigned int >( -( child + 1 ) );

                            if( symbol == EOS_SYMBOL )
                            {
                                HpackError::throwCompressionError(
                                    "a Huffman encoded string literal contains the EOS symbol"
                                    );
                            }

                            value.push_back( static_cast< char >( symbol ) );

                            node = 0U;
                            bitsSinceSymbol = 0U;
                            allOnesSinceSymbol = true;
                        }
                        else
                        {
                            node = static_cast< std::size_t >( child );
                        }
                    }
                }

                if( bitsSinceSymbol > MAX_PADDING_LENGTH_IN_BITS )
                {
                    HpackError::throwCompressionError(
                        "the padding of a Huffman encoded string literal is longer than seven bits"
                        );
                }

                if( bitsSinceSymbol != 0U && ! allOnesSinceSymbol )
                {
                    HpackError::throwCompressionError(
                        "the padding of a Huffman encoded string literal is not the most "
                        "significant bits of the EOS code"
                        );
                }
            }

        private:

            /**
             * @brief One state of the generated decoding machine
             *
             * A child which is negative is a leaf and carries its symbol as -( symbol + 1 );
             * a child which is positive is the index of the next state. Zero means "not
             * assigned", which cannot survive a complete code - the root is never the target of
             * a transition - so the builder uses it as the sentinel and then checks that none
             * is left
             */

            struct Node
            {
                std::int32_t                        children[ 2 ];
            };

            static const Code* codeTable() NOEXCEPT
            {
                /*
                 * RFC 7541 Appendix B, the "code as hex" (aligned on the LSB) and "len" columns.
                 * The comment on each line is the symbol the row begins at
                 */

                static const Code g_codes[ CODE_COUNT ] =
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

                return g_codes;
            }

            static const std::vector< Node >& decodeStateMachine()
            {
                /*
                 * C++11 guarantees this is initialized exactly once even if several threads
                 * reach it at the same time, which is what lets the machine be generated at
                 * first use rather than written out as a second table
                 */

                static const std::vector< Node > g_nodes = buildDecodeStateMachine();

                return g_nodes;
            }

            static std::vector< Node > buildDecodeStateMachine()
            {
                std::vector< Node > nodes;

                /*
                 * A binary trie over 257 leaves has at most 256 internal nodes beside its root
                 */

                nodes.reserve( CODE_COUNT );
                nodes.push_back( Node() );

                for( unsigned int symbol = 0U; symbol < CODE_COUNT; ++symbol )
                {
                    const auto& entry = codeTable()[ symbol ];

                    std::size_t node = 0U;

                    for( unsigned int i = 0U; i < entry.lengthInBits; ++i )
                    {
                        const auto bit =
                            ( entry.code >> ( entry.lengthInBits - 1U - i ) ) & 1U;

                        if( i + 1U == entry.lengthInBits )
                        {
                            nodes[ node ].children[ bit ] =
                                -( static_cast< std::int32_t >( symbol ) + 1 );

                            break;
                        }

                        if( nodes[ node ].children[ bit ] == 0 )
                        {
                            nodes.push_back( Node() );

                            nodes[ node ].children[ bit ] =
                                static_cast< std::int32_t >( nodes.size() - 1U );
                        }

                        node = static_cast< std::size_t >( nodes[ node ].children[ bit ] );
                    }
                }

                /*
                 * The code of Appendix B is complete, so a correctly transcribed table leaves no
                 * transition unassigned. If one is, the table above is wrong and every decode
                 * after this point would be wrong with it - so this fails at first use instead
                 */

                for( std::size_t i = 0U; i < nodes.size(); ++i )
                {
                    BL_CHK_T(
                        false,
                        nodes[ i ].children[ 0 ] != 0 && nodes[ i ].children[ 1 ] != 0,
                        UnexpectedException(),
                        BL_MSG()
                            << "The HPACK Huffman code table is incomplete - state "
                            << i
                            << " of the generated decoder has an unassigned transition"
                        );
                }

                return nodes;
            }
        };

        typedef HpackHuffmanT<> HpackHuffman;

    } // http2

} // bl

#endif /* __BL_HTTP2_HPACKHUFFMAN_H_ */
