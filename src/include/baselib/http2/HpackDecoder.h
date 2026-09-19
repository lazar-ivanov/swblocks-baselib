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

#ifndef __BL_HTTP2_HPACKDECODER_H_
#define __BL_HTTP2_HPACKDECODER_H_

#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/HpackHuffman.h>
#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <string>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class HpackDecoderT - the HPACK decoder of RFC 7541 sections 3, 5 and 6
         *
         * One of these per connection and per direction: RFC 7541 section 2.2 says the encoding
         * and decoding contexts of an endpoint are completely independent, so a client's decoder
         * is fed only the blocks the server sent it
         *
         * WHAT IT PRODUCES is an HpackFieldList - see the long note on HpackFieldT for why that
         * is not an http::HeaderList and cannot be. In short: a decoded response carries
         * pseudo-headers, which a HeaderList rejects by design, and a decoded field which breaks
         * an HTTP/2 message rule has to reach the session intact so that the session can reset
         * the STREAM, where a validating container could only have thrown and taken the whole
         * connection with it
         *
         * THE TWO FAILURE MODES ARE DIFFERENT AND ARE REPORTED DIFFERENTLY, which is the thing
         * to understand before changing anything here:
         *
         *  - a block which cannot be decoded - a bad index, a truncated representation, a size
         *    update in the wrong place, an ill-formed Huffman string - THROWS. RFC 9113 section
         *    4.3 makes that a connection error of type COMPRESSION_ERROR, and there is no
         *    lesser answer, because the dynamic table is shared by every stream
         *  - a block which decodes but comes to more than the caller's decoded header list size
         *    RETURNS Outcome::ExceededDecodedSizeLimit with an empty list. The block is still
         *    consumed to the last octet so that the dynamic table tracks the peer's encoder, and
         *    the caller resets that one stream and leaves the connection alone (design 4.6,
         *    first row). Checking it only at the end would mean buffering a list the peer chose
         *    the size of, so it is checked as each field is decoded
         *
         * THE DECODED SIZE LIMIT IS A PARAMETER OF decode( ), not a constant of this library.
         * RFC 9113 section 6.5.2 gives SETTINGS_MAX_HEADER_LIST_SIZE no numeric initial value at
         * all - "the initial value of this setting is unlimited" - so what to pass is the caller's
         * to decide: SessionT passes the larger of its own limits row and what the active profile
         * advertises, from construction. A caller with no limit passes the largest std::size_t,
         * which bounds nothing - the frame layer's compressed-block cap does not bound this one,
         * because HPACK expands
         *
         * A MID-BLOCK FAILURE LEAVES THE DYNAMIC TABLE EXACTLY AS THE BLOCK FOUND IT. Every
         * decode( ) runs inside an HpackDynamicTable::Transaction, so a throw from anywhere -
         * including from inside the Huffman decoder, three calls down - unwinds through its
         * destructor and rolls the table back. See the class note on HpackDynamicTableT for what
         * that does and does not claim
         */

        template
        <
            typename E = void
        >
        class HpackDecoderT FINAL
        {
            BL_NO_COPY_OR_MOVE( HpackDecoderT )

        public:

            /**
             * @brief What a block came to, when it did not throw
             */

            enum class Outcome : std::uint8_t
            {
                Complete,
                ExceededDecodedSizeLimit,
            };

            enum : std::size_t
            {
                /*
                 * RFC 7541 section 5.1 - "Integer encodings that exceed implementation limits --
                 * in value or octet length -- MUST be treated as decoding errors". Five
                 * continuation octets carry 35 bits, which is every value a 32-bit integer can
                 * take; a sixth can only be a padded encoding, which is the cheap way to make a
                 * decoder do unbounded work
                 */

                MAX_INTEGER_CONTINUATION_OCTETS             = 5U,
            };

            explicit HpackDecoderT(
                SAA_in_opt      const std::size_t               maxDynamicTableSize =
                    static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
                )
                :
                m_dynamicTable( maxDynamicTableSize ),
                m_maxDynamicTableSize( maxDynamicTableSize )
            {
            }

            /**
             * @brief The largest a dynamic table size update may ask for - what we advertised as
             * SETTINGS_HEADER_TABLE_SIZE, and also the capacity the table starts at
             *
             * RFC 7541 section 4.2 requires the peer's encoder to signal a change with a size
             * update at the start of the first block after our SETTINGS is acknowledged. Between
             * our SETTINGS going out and that acknowledgement the peer is still entitled to the
             * protocol's initial 4096, so a session which advertises LESS than that should not
             * construct this decoder with the smaller value until its own SETTINGS is
             * acknowledged. Advertising more is safe at any time: our table then merely evicts
             * later than the peer's, and the peer only ever names indices its own table holds,
             * which are the same indices in ours
             */

            std::size_t maxDynamicTableSize() const NOEXCEPT
            {
                return m_maxDynamicTableSize;
            }

            /**
             * @brief Lowers or raises the ceiling above, WITHOUT touching the table's capacity
             *
             * This is what the session calls when the peer acknowledges our SETTINGS (RFC 9113
             * section 6.5.3), and it is the answer to the window the comment above describes: the
             * decoder is constructed at the larger of what we advertise and the protocol's 4096,
             * so that a size update the peer was still entitled to send cannot be refused, and the
             * ceiling drops to what we advertised only once the acknowledgement proves the peer
             * has applied it
             *
             * IT DOES NOT SHRINK THE TABLE, deliberately. The table's capacity follows the peer's
             * own dynamic table size update, which RFC 7541 section 4.2 requires at the start of
             * the first block after the change - and that update is what evicts, on both sides, at
             * the same point in the stream of blocks. Evicting here instead would NOT break a
             * conforming peer: section 4.3 has its encoder evict whenever its maximum is reduced,
             * and section 4.2 has it signal that reduction at the start of its next block, so a
             * conforming peer has itself stopped naming exactly what an eviction here would drop.
             * What not evicting buys is tolerance of a peer which reduced late or not at all - it
             * goes on naming entries we still hold - and the table is bounded by what we once
             * advertised either way
             */

            void setMaxDynamicTableSize( SAA_in const std::size_t maxDynamicTableSize ) NOEXCEPT
            {
                m_maxDynamicTableSize = maxDynamicTableSize;
            }

            const HpackDynamicTable& dynamicTable() const NOEXCEPT
            {
                return m_dynamicTable;
            }

            /**
             * @brief Decodes one complete header block - the concatenation of a HEADERS frame's
             * field block fragment with those of the CONTINUATION frames which follow it
             *
             * 'fields' is cleared first and is left empty if the outcome is not Complete
             */

            Outcome decode(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_in          const std::size_t               maxDecodedHeaderListSize,
                SAA_out         HpackFieldList&                 fields
                )
            {
                BL_CHK_ARG( data != nullptr || size == 0U, data );

                fields.clear();

                BlockState state;

                state.fields = &fields;
                state.decodedSize = 0U;
                state.maxDecodedSize = maxDecodedHeaderListSize;
                state.exceeded = false;
                state.sawField = false;

                typename HpackDynamicTable::Transaction transaction( m_dynamicTable );

                std::size_t pos = 0U;

                while( pos < size )
                {
                    const auto first = static_cast< unsigned char >( data[ pos ] );

                    if( ( first & 0x80U ) != 0U )
                    {
                        decodeIndexed( data, size, pos, state );
                    }
                    else if( ( first & 0xC0U ) == 0x40U )
                    {
                        decodeLiteral( data, size, pos, 6U, INDEXING_INCREMENTAL, state );
                    }
                    else if( ( first & 0xE0U ) == 0x20U )
                    {
                        decodeSizeUpdate( data, size, pos, state );
                    }
                    else
                    {
                        /*
                         * 0000xxxx is "without indexing" (6.2.2) and 0001xxxx is "never indexed"
                         * (6.2.3) - the same encoding, and the difference is what an intermediary
                         * which re-encodes the field is allowed to do with it (7.1.3)
                         */

                        decodeLiteral(
                            data,
                            size,
                            pos,
                            4U,
                            ( first & 0x10U ) != 0U ? INDEXING_NEVER : INDEXING_NONE,
                            state
                            );
                    }
                }

                transaction.commit();

                if( state.exceeded )
                {
                    fields.clear();

                    return Outcome::ExceededDecodedSizeLimit;
                }

                return Outcome::Complete;
            }

            /*************************************************************************
             * The primitive decoders of RFC 7541 section 5, exposed because they are
             * worth testing against Appendix C.1 on their own
             */

            /**
             * @brief Decodes the integer whose prefix occupies the low 'prefixBits' bits of the
             * octet at 'pos', leaving 'pos' on the octet after it - section 5.1
             */

            static std::uint32_t decodeInteger(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::size_t&                    pos,
                SAA_in          const unsigned int              prefixBits
                )
            {
                BL_CHK_ARG( prefixBits >= 1U && prefixBits <= 8U, prefixBits );

                if( pos >= size )
                {
                    HpackError::throwCompressionError(
                        "a header block ends in the middle of an integer"
                        );
                }

                const std::uint32_t prefixMax = ( 1U << prefixBits ) - 1U;

                const std::uint32_t prefix =
                    static_cast< unsigned char >( data[ pos ] ) & prefixMax;

                ++pos;

                if( prefix < prefixMax )
                {
                    return prefix;
                }

                std::uint64_t value = prefixMax;
                unsigned int shift = 0U;
                std::size_t octets = 0U;

                for( ;; )
                {
                    if( pos >= size )
                    {
                        HpackError::throwCompressionError(
                            "a header block ends in the middle of a multi-octet integer"
                            );
                    }

                    if( octets >= MAX_INTEGER_CONTINUATION_OCTETS )
                    {
                        HpackError::throwCompressionError(
                            "an integer is encoded on more octets than any value it could carry"
                            );
                    }

                    const auto octet = static_cast< unsigned char >( data[ pos ] );

                    ++pos;
                    ++octets;

                    value += static_cast< std::uint64_t >( octet & 0x7FU ) << shift;

                    if( value > 0xFFFFFFFFULL )
                    {
                        HpackError::throwCompressionError(
                            "an integer exceeds the largest value this decoder represents"
                            );
                    }

                    shift += 7U;

                    if( ( octet & 0x80U ) == 0U )
                    {
                        break;
                    }
                }

                return static_cast< std::uint32_t >( value );
            }

            /**
             * @brief Decodes the string literal at 'pos' and appends it to 'value' - section 5.2
             *
             * The length limit is the rest of the block, which is the only bound that does not
             * invent a number: the block itself is capped by the frame layer before it reaches
             * here (design 4.6, compressed header block bytes)
             */

            static void decodeString(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::size_t&                    pos,
                SAA_inout       std::string&                    value
                )
            {
                if( pos >= size )
                {
                    HpackError::throwCompressionError(
                        "a header block ends where a string literal was expected"
                        );
                }

                const bool huffman =
                    ( static_cast< unsigned char >( data[ pos ] ) & 0x80U ) != 0U;

                const auto length = decodeInteger( data, size, pos, 7U );

                if( static_cast< std::size_t >( length ) > size - pos )
                {
                    HpackError::throwCompressionError(
                        "a string literal runs past the end of the header block"
                        );
                }

                const auto lengthAsSize = static_cast< std::size_t >( length );

                if( huffman )
                {
                    HpackHuffman::decode( data + pos, lengthAsSize, value );
                }
                else
                {
                    value.append( data + pos, lengthAsSize );
                }

                pos += lengthAsSize;
            }

        private:

            /*
             * Which of the three literal representations of section 6.2 is being decoded - it
             * decides whether the field enters the dynamic table and whether the decoded field
             * remembers that it must never do so
             */

            enum LiteralIndexing
            {
                INDEXING_INCREMENTAL,
                INDEXING_NONE,
                INDEXING_NEVER,
            };

            /*
             * Everything which belongs to one block rather than to the decoder. It is passed
             * along rather than held as members so that a decoder is never left carrying half a
             * block's worth of accounting after a failure
             */

            struct BlockState
            {
                HpackFieldList*                                 fields;
                std::size_t                                     decodedSize;
                std::size_t                                     maxDecodedSize;
                bool                                            exceeded;
                bool                                            sawField;
            };

            HpackDynamicTable                                   m_dynamicTable;
            std::size_t                                         m_maxDynamicTableSize;

            /**
             * @brief Adds a decoded field to the list, unless the block has already gone past
             * the caller's decoded header list size
             *
             * Once the bound is broken nothing more is kept - but the caller of this keeps
             * decoding, because the dynamic table has to end the block where the peer's encoder
             * ended it
             */

            static void emit( SAA_inout BlockState& state, SAA_in HpackField&& field )
            {
                state.sawField = true;

                if( state.exceeded )
                {
                    return;
                }

                const auto fieldSize = field.hpackSize();

                if( fieldSize > state.maxDecodedSize - state.decodedSize )
                {
                    state.exceeded = true;

                    state.fields -> clear();

                    return;
                }

                state.decodedSize += fieldSize;

                state.fields -> push_back( BL_PARAM_FWD( field ) );
            }

            void decodeIndexed(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::size_t&                    pos,
                SAA_inout       BlockState&                     state
                )
            {
                const auto index = decodeInteger( data, size, pos, 7U );

                /*
                 * Section 6.1 - "The index value of 0 is not used. It MUST be treated as a
                 * decoding error if found in an indexed header field representation"
                 */

                if( index == 0U )
                {
                    HpackError::throwCompressionError(
                        "an indexed header field representation carries the index 0"
                        );
                }

                if( ! m_dynamicTable.isValidIndex( index ) )
                {
                    HpackError::throwCompressionError(
                        "an index is beyond the end of the static and dynamic tables"
                        );
                }

                const auto& entry = m_dynamicTable.fieldAtIndex( index );

                emit(
                    state,
                    HpackField( cpp::copy( entry.name() ), cpp::copy( entry.value() ) )
                    );
            }

            void decodeLiteral(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::size_t&                    pos,
                SAA_in          const unsigned int              prefixBits,
                SAA_in          const LiteralIndexing           indexing,
                SAA_inout       BlockState&                     state
                )
            {
                const auto nameIndex = decodeInteger( data, size, pos, prefixBits );

                std::string name;

                if( nameIndex == 0U )
                {
                    decodeString( data, size, pos, name );
                }
                else
                {
                    if( ! m_dynamicTable.isValidIndex( nameIndex ) )
                    {
                        HpackError::throwCompressionError(
                            "the name index of a literal representation is beyond the end of the "
                            "static and dynamic tables"
                            );
                    }

                    /*
                     * Copied, not referenced - section 4.4 warns that the entry this name comes
                     * from may be the one evicted to make room for the field being built
                     */

                    name = m_dynamicTable.fieldAtIndex( nameIndex ).name();
                }

                std::string value;

                decodeString( data, size, pos, value );

                if( indexing == INDEXING_INCREMENTAL )
                {
                    m_dynamicTable.insert(
                        HpackField( cpp::copy( name ), cpp::copy( value ) )
                        );
                }

                emit(
                    state,
                    HpackField(
                        BL_PARAM_FWD( name ),
                        BL_PARAM_FWD( value ),
                        indexing == INDEXING_NEVER
                        )
                    );
            }

            void decodeSizeUpdate(
                SAA_in          const char*                     data,
                SAA_in          const std::size_t               size,
                SAA_inout       std::size_t&                    pos,
                SAA_inout       BlockState&                     state
                )
            {
                /*
                 * RFC 7541 section 4.2 - a size update "MUST occur at the beginning of the first
                 * header block following the change". So one may precede the first field
                 * representation of a block and nowhere else. The RFC also bounds the ENCODER to
                 * at most two of them; it asks nothing of the decoder about the count, and the
                 * design asks only for the position and the ceiling, so neither is invented here
                 */

                if( state.sawField )
                {
                    HpackError::throwCompressionError(
                        "a dynamic table size update follows a header field representation "
                        "rather than opening the block"
                        );
                }

                const auto newSize = decodeInteger( data, size, pos, 5U );

                if( static_cast< std::size_t >( newSize ) > m_maxDynamicTableSize )
                {
                    HpackError::throwCompressionError(
                        "a dynamic table size update asks for more than was advertised as "
                        "SETTINGS_HEADER_TABLE_SIZE"
                        );
                }

                m_dynamicTable.setCapacity( static_cast< std::size_t >( newSize ) );
            }
        };

        typedef HpackDecoderT<> HpackDecoder;

    } // http2

} // bl

#endif /* __BL_HTTP2_HPACKDECODER_H_ */
