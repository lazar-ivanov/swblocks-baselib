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

#ifndef __BL_HTTP2_HPACKENCODER_H_
#define __BL_HTTP2_HPACKENCODER_H_

#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/HpackHuffman.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class HpackEncoderT - the HPACK encoder of RFC 7541 sections 4, 5 and 6
         *
         * One per connection and per direction, independent of the decoder's table (section 2.2).
         * RFC 7541 defines only how a DECODER must behave, so an encoder is free within the
         * format; what it chooses is nevertheless visible on the wire, and for an impersonating
         * client the choices are part of the fingerprint, which is why they are a policy object
         * rather than constants (design 4.2 and 6.4)
         *
         * THE REPRESENTATION CHOSEN FOR A FIELD, in the order the questions are asked:
         *
         *  1. a field marked neverIndexed( ) always takes the literal never-indexed form of
         *     section 6.2.3, whatever the policy says and even when the very same name and value
         *     are already in a table. Section 7.1.3 is explicit that the point of that form is
         *     that the value must not enter a compression context, and an indexed representation
         *     would put it there
         *  2. otherwise, a name AND value which are both already in the static or the dynamic
         *     table are sent as the one-octet indexed form of section 6.1. The static table is
         *     searched first: its index is smaller, and it does not move when an entry is
         *     inserted
         *  3. otherwise, the policy's HpackIndexingPolicy decides which of the three literal
         *     forms of section 6.2 is used, and the name is sent as an index when one is known
         *     and as a string literal when it is not
         *
         * HUFFMAN IS USED WHEN IT IS NOT LONGER, not when it is strictly shorter. That is a
         * deliberate choice with evidence behind it: RFC 7541 Appendix C.6.2 Huffman-codes the
         * value "307" although the coded form is also three octets, so the strict rule cannot
         * reproduce the RFC's own vector. It is also what browsers do, which is what an
         * impersonating client needs
         *
         * NAMES ARE LOWERCASED HERE. http::HeaderList keeps the spelling a caller gave, because
         * HTTP/1.1 sends it that way and a browser's casing is part of what is reproduced; RFC
         * 9113 section 8.2.1 requires HTTP/2 names to be lowercase. Its class comment names this
         * class as the place that converts, so the conversion is here and not in the bridge -
         * that way no field list, however it was built, can carry an uppercase name onto the wire
         */

        template
        <
            typename E = void
        >
        class HpackEncoderT FINAL
        {
            BL_NO_COPY_OR_MOVE( HpackEncoderT )

        public:

            /**
             * @brief What the encoder does with a field it is not required to send literally
             *
             * These are the knobs design 6.4 keeps in a profile - Http2Profile carries
             * hpackIndexingPolicy and cookieCrumbling already - held here as a plain value so
             * that this header does not depend on a profile it has no other use for
             */

            struct Policy
            {
                HpackIndexingPolicy                 indexingPolicy = HpackIndexingPolicy::Incremental;

                bool                                huffman = true;

                /*
                 * RFC 9113 section 8.2.3 - a cookie header field may be split into one field per
                 * crumb, so that changing one cookie does not invalidate the table entry holding
                 * all of them. Browsers do it, so a profile turns it on. The split is on the
                 * exact two-octet delimiter "; " the RFC names for putting them back together,
                 * which makes crumbling reversible rather than merely plausible
                 */

                bool                                crumbleCookies = false;
            };

            explicit HpackEncoderT(
                SAA_in_opt      const std::size_t               dynamicTableCapacity =
                    static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
                )
                :
                m_dynamicTable( dynamicTableCapacity ),
                m_sizeUpdatePending( false ),
                m_pendingSmallest( 0U ),
                m_pendingFinal( 0U )
            {
            }

            const Policy& policy() const NOEXCEPT
            {
                return m_policy;
            }

            void setPolicy( SAA_in const Policy& policy ) NOEXCEPT
            {
                m_policy = policy;
            }

            const HpackDynamicTable& dynamicTable() const NOEXCEPT
            {
                return m_dynamicTable;
            }

            /**
             * @brief Changes the size this encoder will use for its dynamic table, signalling it
             * to the peer at the start of the next block - RFC 7541 section 4.2
             *
             * The caller is the one which read the peer's SETTINGS_HEADER_TABLE_SIZE, so the
             * caller is what keeps this at or below it; nothing here can know that value. The
             * RFC's other requirement is met here rather than by the caller: if the size changes
             * more than once before the next block is encoded, the SMALLEST of the values is
             * signalled first and the final one after it, so that a decoder which has to evict
             * on the way down does so
             */

            void setDynamicTableCapacity( SAA_in const std::size_t capacity ) NOEXCEPT
            {
                if( ! m_sizeUpdatePending )
                {
                    if( capacity == m_dynamicTable.capacity() )
                    {
                        return;
                    }

                    m_sizeUpdatePending = true;
                    m_pendingSmallest = capacity;
                    m_pendingFinal = capacity;

                    return;
                }

                m_pendingSmallest = std::min( m_pendingSmallest, capacity );
                m_pendingFinal = capacity;
            }

            /**
             * @brief Appends the header block for these fields to the buffer
             *
             * Either the whole block is appended and the dynamic table has moved with it, or
             * neither happened: the block is built aside and the table's own transaction rolls
             * back if anything throws. A half-written block with a table that moved would be
             * undecodable at the peer and there would be no way back from it
             */

            void encode(
                SAA_in          const HpackFieldList&           fields,
                SAA_inout       std::string&                    buffer
                )
            {
                std::string block;

                {
                    typename HpackDynamicTable::Transaction transaction( m_dynamicTable );

                    if( m_sizeUpdatePending )
                    {
                        if( m_pendingSmallest != m_pendingFinal )
                        {
                            emitSizeUpdate( m_pendingSmallest, block );
                        }

                        emitSizeUpdate( m_pendingFinal, block );
                    }

                    for( std::size_t i = 0U; i < fields.size(); ++i )
                    {
                        encodeField( fields[ i ], block );
                    }

                    /*
                     * Reserving before the commit is what makes the two states move together:
                     * after this line the append below cannot allocate and so cannot throw
                     */

                    buffer.reserve( buffer.size() + block.size() );

                    transaction.commit();
                }

                m_sizeUpdatePending = false;

                buffer.append( block );
            }

            /*************************************************************************
             * The primitive encoders of RFC 7541 section 5, exposed because they are
             * worth testing against Appendix C.1 on their own
             */

            /**
             * @brief Encodes an integer into a prefix of 'prefixBits' bits - section 5.1
             *
             * 'highBits' is the pattern which occupies the 8 - prefixBits bits above the prefix,
             * already in place: 0x80 for an indexed field, 0x40 for a literal with incremental
             * indexing, 0x20 for a size update, 0x10 for never-indexed and 0x00 for the rest
             */

            static void encodeInteger(
                SAA_in          const std::uint32_t             value,
                SAA_in          const unsigned int              prefixBits,
                SAA_in          const unsigned char             highBits,
                SAA_inout       std::string&                    buffer
                )
            {
                BL_CHK_ARG( prefixBits >= 1U && prefixBits <= 8U, prefixBits );

                const std::uint32_t prefixMax = ( 1U << prefixBits ) - 1U;

                if( value < prefixMax )
                {
                    buffer.push_back(
                        static_cast< char >( highBits | static_cast< unsigned char >( value ) )
                        );

                    return;
                }

                buffer.push_back(
                    static_cast< char >( highBits | static_cast< unsigned char >( prefixMax ) )
                    );

                std::uint32_t rest = value - prefixMax;

                while( rest >= 128U )
                {
                    buffer.push_back( static_cast< char >( ( rest % 128U ) + 128U ) );

                    rest /= 128U;
                }

                buffer.push_back( static_cast< char >( rest ) );
            }

            /**
             * @brief Encodes a string literal, Huffman coded when that is not longer - section 5.2
             */

            static void encodeString(
                SAA_in          const std::string&              value,
                SAA_in          const bool                      huffman,
                SAA_inout       std::string&                    buffer
                )
            {
                if( huffman )
                {
                    const auto huffmanSize = HpackHuffman::encodedSizeInOctets( value );

                    if( huffmanSize <= value.size() )
                    {
                        encodeInteger(
                            static_cast< std::uint32_t >( huffmanSize ),
                            7U,
                            0x80U,
                            buffer
                            );

                        HpackHuffman::encode( value, buffer );

                        return;
                    }
                }

                encodeInteger( static_cast< std::uint32_t >( value.size() ), 7U, 0x00U, buffer );

                buffer.append( value );
            }

            /**
             * @brief The ASCII-only lowercase fold RFC 9113 section 8.2.1 asks for
             *
             * ASCII only and not str::to_lower_copy, for the reason http::HeaderList gives for
             * its own: a field name is a token and so is ASCII by construction, and a fold which
             * takes std::locale( ) can be changed out from under it by any caller which installs
             * a different global locale
             */

            static std::string toLowerAsciiCopy( SAA_in const std::string& value )
            {
                std::string result( value );

                for( std::size_t i = 0U; i < result.size(); ++i )
                {
                    const auto octet = static_cast< unsigned char >( result[ i ] );

                    if( octet >= 'A' && octet <= 'Z' )
                    {
                        result[ i ] = static_cast< char >( octet - 'A' + 'a' );
                    }
                }

                return result;
            }

        private:

            static const std::string                            g_cookieName;
            static const std::string                            g_cookieCrumbSeparator;

            HpackDynamicTable                                   m_dynamicTable;
            Policy                                              m_policy;

            bool                                                m_sizeUpdatePending;
            std::size_t                                         m_pendingSmallest;
            std::size_t                                         m_pendingFinal;

            static bool hasUpperAscii( SAA_in const std::string& value ) NOEXCEPT
            {
                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    const auto octet = static_cast< unsigned char >( value[ i ] );

                    if( octet >= 'A' && octet <= 'Z' )
                    {
                        return true;
                    }
                }

                return false;
            }

            void emitSizeUpdate(
                SAA_in          const std::size_t               capacity,
                SAA_inout       std::string&                    block
                )
            {
                encodeInteger(
                    static_cast< std::uint32_t >( capacity ),
                    5U,
                    0x20U,
                    block
                    );

                m_dynamicTable.setCapacity( capacity );
            }

            void encodeField(
                SAA_in          const HpackField&               field,
                SAA_inout       std::string&                    block
                )
            {
                std::string lowered;

                const std::string* name = &field.name();

                if( hasUpperAscii( field.name() ) )
                {
                    lowered = toLowerAsciiCopy( field.name() );

                    name = &lowered;
                }

                if(
                    m_policy.crumbleCookies &&
                    ! field.neverIndexed() &&
                    *name == g_cookieName
                    )
                {
                    encodeCookieCrumbs( *name, field.value(), block );

                    return;
                }

                encodeOneField( *name, field.value(), field.neverIndexed(), block );
            }

            /**
             * @brief Splits a cookie value on the RFC 9113 section 8.2.3 delimiter and encodes
             * one field per crumb
             *
             * A value which does not carry the delimiter is one crumb, which is the same field,
             * so this is never lossy
             */

            void encodeCookieCrumbs(
                SAA_in          const std::string&              name,
                SAA_in          const std::string&              value,
                SAA_inout       std::string&                    block
                )
            {
                std::size_t begin = 0U;

                for( ;; )
                {
                    const auto found = value.find( g_cookieCrumbSeparator, begin );

                    if( found == std::string::npos )
                    {
                        encodeOneField( name, value.substr( begin ), false, block );

                        return;
                    }

                    encodeOneField( name, value.substr( begin, found - begin ), false, block );

                    begin = found + g_cookieCrumbSeparator.size();
                }
            }

            void encodeOneField(
                SAA_in          const std::string&              name,
                SAA_in          const std::string&              value,
                SAA_in          const bool                      neverIndexed,
                SAA_inout       std::string&                    block
                )
            {
                const auto indexing =
                    neverIndexed ? HpackIndexingPolicy::NeverIndexed : m_policy.indexingPolicy;

                if( indexing != HpackIndexingPolicy::NeverIndexed )
                {
                    const auto fieldIndex = m_dynamicTable.findFieldIndex( name, value );

                    if( fieldIndex != 0U )
                    {
                        encodeInteger( fieldIndex, 7U, 0x80U, block );

                        return;
                    }
                }

                unsigned int prefixBits = 4U;
                unsigned char highBits = 0x00U;

                if( indexing == HpackIndexingPolicy::Incremental )
                {
                    prefixBits = 6U;
                    highBits = 0x40U;
                }
                else if( indexing == HpackIndexingPolicy::NeverIndexed )
                {
                    highBits = 0x10U;
                }

                const auto nameIndex = m_dynamicTable.findNameIndex( name );

                encodeInteger( nameIndex, prefixBits, highBits, block );

                if( nameIndex == 0U )
                {
                    encodeString( name, m_policy.huffman, block );
                }

                encodeString( value, m_policy.huffman, block );

                if( indexing == HpackIndexingPolicy::Incremental )
                {
                    m_dynamicTable.insert(
                        HpackField( cpp::copy( name ), cpp::copy( value ) )
                        );
                }
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( HpackEncoderT, g_cookieName )                = "cookie";
        BL_DEFINE_STATIC_CONST_STRING( HpackEncoderT, g_cookieCrumbSeparator )      = "; ";

        typedef HpackEncoderT<> HpackEncoder;

    } // http2

} // bl

#endif /* __BL_HTTP2_HPACKENCODER_H_ */
