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

#ifndef __BL_HTTP2_HPACKDYNAMICTABLE_H_
#define __BL_HTTP2_HPACKDYNAMICTABLE_H_

#include <baselib/http2/Globals.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class HpackFieldT - one header field as HPACK carries it, and the element of
         * what the decoder produces
         *
         * WHY THIS TYPE EXISTS AT ALL, which is the decision S2.2 was asked to make and to state.
         * The obvious candidate for the decoder's output was http::HeaderList, and it cannot be
         * it, for two independent reasons - the second of which is the one that would have hurt:
         *
         *  1. A PSEUDO-HEADER IS NOT REPRESENTABLE IN A HeaderList. ':' is not a token character
         *     (RFC 9110 section 5.1), so HeaderList::append rejects ":status" by design - and
         *     correctly, because on the request side the session derives the pseudo-headers and
         *     their order comes from the profile (design 6.4), so nothing ever needs to put one
         *     in a HeaderList. A decoded RESPONSE, though, is required to carry exactly one
         *     ":status" (design 4.5), and it arrives in wire order with its pseudo-headers
         *     first. A type which cannot hold it is not the type the decoder returns
         *
         *  2. VALIDATION HERE WOULD RAISE THE WRONG ERROR. HeaderList validates on the way in
         *     and throws - that is deliberate, it is the security boundary of its own class
         *     comment. But HPACK decoding failures and MESSAGE validation failures are different
         *     errors at different levels: a block which cannot be decoded is a CONNECTION error
         *     of type COMPRESSION_ERROR (RFC 9113 section 4.3), while an uppercase field name or
         *     a CR in a value is a malformed message and a STREAM error of type PROTOCOL_ERROR
         *     (RFC 9113 sections 8.1 and 8.2.1, design 4.5). A decoder built on a validating
         *     container could only report the first, so a single malformed response would take
         *     the whole connection down with every other request on it. This type therefore does
         *     NOT validate names or values: it represents faithfully what the peer sent, and the
         *     session engine of design 4.5 judges it
         *
         * So the decoder's output is a SEQUENCE OF THESE, HpackFieldList below - ordered,
         * repeats allowed, pseudo-headers allowed, no validation. It is what an S3.1 event
         * carrying a decoded block holds. It is deliberately not a class of its own: the session
         * walks it once in order, which is what a vector is for, and a wrapper would have to
         * re-offer the same lookups http::HeaderList already has without being able to share
         * them
         *
         * The ENCODER takes the same type, which is what makes an encode/decode round trip
         * meaningful and is why there is one type rather than two. A request's pseudo-headers
         * are built into it by the session in the profile's order, and its regular fields come
         * from the caller's http::HeaderList through appendAll( ) below - which is where this
         * slice "consumes the encoder side" of S1.2
         */

        template
        <
            typename E = void
        >
        class HpackFieldT FINAL
        {
        public:

            enum : std::size_t
            {
                /*
                 * RFC 7541 section 4.1 - the size of an entry is its name's length plus its
                 * value's length plus 32, the estimated per-entry overhead. The lengths are the
                 * DECODED ones: Huffman coding does not change what an entry costs in the table,
                 * which is why Appendix C.5 and C.6 evict at exactly the same points
                 */

                ENTRY_SIZE_OVERHEAD                     = 32U,
            };

            HpackFieldT()
                :
                m_neverIndexed( false )
            {
            }

            HpackFieldT(
                SAA_in          std::string&&                   name,
                SAA_in          std::string&&                   value,
                SAA_in_opt      const bool                      neverIndexed = false
                )
                :
                m_name( BL_PARAM_FWD( name ) ),
                m_value( BL_PARAM_FWD( value ) ),
                m_neverIndexed( neverIndexed )
            {
            }

            const std::string& name() const NOEXCEPT
            {
                return m_name;
            }

            const std::string& value() const NOEXCEPT
            {
                return m_value;
            }

            /**
             * @brief Whether this field must use the never-indexed representation
             *
             * On a field the DECODER produced it records that the peer sent it as RFC 7541
             * section 6.2.3, which section 7.1.3 asks an implementation to preserve if it ever
             * re-encodes the field. On a field handed to the ENCODER it is the caller saying
             * that this value must never enter a compression context whatever the profile's
             * HpackIndexingPolicy says - which is what section 7.1.3 exists for
             */

            bool neverIndexed() const NOEXCEPT
            {
                return m_neverIndexed;
            }

            std::size_t hpackSize() const NOEXCEPT
            {
                return m_name.size() + m_value.size() + ENTRY_SIZE_OVERHEAD;
            }

            bool operator ==( SAA_in const HpackFieldT& other ) const NOEXCEPT
            {
                return
                    m_name == other.m_name &&
                    m_value == other.m_value &&
                    m_neverIndexed == other.m_neverIndexed;
            }

            bool operator !=( SAA_in const HpackFieldT& other ) const NOEXCEPT
            {
                return ! ( *this == other );
            }

        private:

            std::string                                         m_name;
            std::string                                         m_value;
            bool                                                m_neverIndexed;
        };

        typedef HpackFieldT<> HpackField;
        typedef std::vector< HpackField > HpackFieldList;

        /**
         * @brief class HpackFieldsT - the bridge from the library's ordered header list
         *
         * http::HeaderList keeps a name exactly as the caller spelled it, because HTTP/1.1
         * sends it that way and a browser's casing is part of what the impersonation work
         * reproduces. RFC 9113 section 8.2.1 requires HTTP/2 field names to be lowercase, and
         * HeaderList's own class comment says where that conversion belongs: "the HTTP/2 encoder
         * lowercases as it encodes". It is done there and not here, so that a field list built
         * by any other route cannot skip it
         */

        template
        <
            typename E = void
        >
        class HpackFieldsT
        {
            BL_DECLARE_STATIC( HpackFieldsT )

        public:

            /**
             * @brief Appends every field of the header list, in the list's order, keeping the
             * spelling it was given
             */

            static void appendAll(
                SAA_in          const http::HeaderList&         headers,
                SAA_inout       HpackFieldList&                 fields
                )
            {
                for( auto it = headers.begin(); it != headers.end(); ++it )
                {
                    fields.push_back(
                        HpackField( cpp::copy( it -> name() ), cpp::copy( it -> value() ) )
                        );
                }
            }
        };

        typedef HpackFieldsT<> HpackFields;

        /**
         * @brief class HpackStaticTableT - the 61 predefined entries of RFC 7541 Appendix A
         *
         * Index 1 through 61 of the combined address space of section 2.3.3. Unchangeable, and
         * shared by the encoder and the decoder and by every connection - there is one of it in
         * the process
         *
         * The lookups return the LOWEST matching index, which matters: ":status" appears seven
         * times (8 through 14) and ":method" twice, and the lowest index is both what Appendix C
         * expects and the one that encodes in fewer bits
         */

        template
        <
            typename E = void
        >
        class HpackStaticTableT
        {
            BL_DECLARE_STATIC( HpackStaticTableT )

        public:

            enum : std::uint32_t
            {
                ENTRY_COUNT                             = 61U,
            };

            static const HpackField& at( SAA_in const std::uint32_t index )
            {
                BL_CHK_ARG( index >= 1U && index <= ENTRY_COUNT, index );

                return entries()[ index - 1U ];
            }

            /**
             * @brief The index of an entry whose name and value both match, or 0 if there is none
             */

            static std::uint32_t findFieldIndex(
                SAA_in          const std::string&              name,
                SAA_in          const std::string&              value
                ) NOEXCEPT
            {
                const auto& table = entries();

                for( std::uint32_t i = 0U; i < ENTRY_COUNT; ++i )
                {
                    if( table[ i ].name() == name && table[ i ].value() == value )
                    {
                        return i + 1U;
                    }
                }

                return 0U;
            }

            /**
             * @brief The index of the first entry whose name matches, or 0 if there is none
             */

            static std::uint32_t findNameIndex( SAA_in const std::string& name ) NOEXCEPT
            {
                const auto& table = entries();

                for( std::uint32_t i = 0U; i < ENTRY_COUNT; ++i )
                {
                    if( table[ i ].name() == name )
                    {
                        return i + 1U;
                    }
                }

                return 0U;
            }

        private:

            static const std::vector< HpackField >& entries()
            {
                static const std::vector< HpackField > g_entries = buildEntries();

                return g_entries;
            }

            static std::vector< HpackField > buildEntries()
            {
                struct Row
                {
                    const char*                     name;
                    const char*                     value;
                };

                /*
                 * RFC 7541 Appendix A, Table 1, in index order. An entry with no value in the
                 * table has an empty one here
                 */

                static const Row g_rows[ ENTRY_COUNT ] =
                {
                    { ":authority",                     ""                          },  /*  1 */
                    { ":method",                        "GET"                       },  /*  2 */
                    { ":method",                        "POST"                      },  /*  3 */
                    { ":path",                          "/"                         },  /*  4 */
                    { ":path",                          "/index.html"               },  /*  5 */
                    { ":scheme",                        "http"                      },  /*  6 */
                    { ":scheme",                        "https"                     },  /*  7 */
                    { ":status",                        "200"                       },  /*  8 */
                    { ":status",                        "204"                       },  /*  9 */
                    { ":status",                        "206"                       },  /* 10 */
                    { ":status",                        "304"                       },  /* 11 */
                    { ":status",                        "400"                       },  /* 12 */
                    { ":status",                        "404"                       },  /* 13 */
                    { ":status",                        "500"                       },  /* 14 */
                    { "accept-charset",                 ""                          },  /* 15 */
                    { "accept-encoding",                "gzip, deflate"             },  /* 16 */
                    { "accept-language",                ""                          },  /* 17 */
                    { "accept-ranges",                  ""                          },  /* 18 */
                    { "accept",                         ""                          },  /* 19 */
                    { "access-control-allow-origin",    ""                          },  /* 20 */
                    { "age",                            ""                          },  /* 21 */
                    { "allow",                          ""                          },  /* 22 */
                    { "authorization",                  ""                          },  /* 23 */
                    { "cache-control",                  ""                          },  /* 24 */
                    { "content-disposition",            ""                          },  /* 25 */
                    { "content-encoding",               ""                          },  /* 26 */
                    { "content-language",               ""                          },  /* 27 */
                    { "content-length",                 ""                          },  /* 28 */
                    { "content-location",               ""                          },  /* 29 */
                    { "content-range",                  ""                          },  /* 30 */
                    { "content-type",                   ""                          },  /* 31 */
                    { "cookie",                         ""                          },  /* 32 */
                    { "date",                           ""                          },  /* 33 */
                    { "etag",                           ""                          },  /* 34 */
                    { "expect",                         ""                          },  /* 35 */
                    { "expires",                        ""                          },  /* 36 */
                    { "from",                           ""                          },  /* 37 */
                    { "host",                           ""                          },  /* 38 */
                    { "if-match",                       ""                          },  /* 39 */
                    { "if-modified-since",              ""                          },  /* 40 */
                    { "if-none-match",                  ""                          },  /* 41 */
                    { "if-range",                       ""                          },  /* 42 */
                    { "if-unmodified-since",            ""                          },  /* 43 */
                    { "last-modified",                  ""                          },  /* 44 */
                    { "link",                           ""                          },  /* 45 */
                    { "location",                       ""                          },  /* 46 */
                    { "max-forwards",                   ""                          },  /* 47 */
                    { "proxy-authenticate",             ""                          },  /* 48 */
                    { "proxy-authorization",            ""                          },  /* 49 */
                    { "range",                          ""                          },  /* 50 */
                    { "referer",                        ""                          },  /* 51 */
                    { "refresh",                        ""                          },  /* 52 */
                    { "retry-after",                    ""                          },  /* 53 */
                    { "server",                         ""                          },  /* 54 */
                    { "set-cookie",                     ""                          },  /* 55 */
                    { "strict-transport-security",      ""                          },  /* 56 */
                    { "transfer-encoding",              ""                          },  /* 57 */
                    { "user-agent",                     ""                          },  /* 58 */
                    { "vary",                           ""                          },  /* 59 */
                    { "via",                            ""                          },  /* 60 */
                    { "www-authenticate",               ""                          },  /* 61 */
                };

                std::vector< HpackField > result;

                result.reserve( ENTRY_COUNT );

                for( std::uint32_t i = 0U; i < ENTRY_COUNT; ++i )
                {
                    result.push_back(
                        HpackField( std::string( g_rows[ i ].name ), std::string( g_rows[ i ].value ) )
                        );
                }

                return result;
            }
        };

        typedef HpackStaticTableT<> HpackStaticTable;

        /**
         * @brief class HpackDynamicTableT - the first-in first-out table of RFC 7541 section 2.3.2
         *
         * It also owns the COMBINED ADDRESS SPACE of section 2.3.3, because the static half of
         * that space is a constant and there is nothing else for a separate type to hold:
         * indices 1 to 61 are HpackStaticTable, and anything above that is this table with 61
         * subtracted. An index beyond both is a decoding error, and it is the caller which turns
         * that into one - isValidIndex( ) answers the question, and fieldAtIndex( ) is a
         * programming-error check, because which protocol error a bad index maps to is the
         * decoder's business and not this container's
         *
         * MID-BLOCK FAILURE, which is the property this class is shaped around. A header block
         * is decoded one representation at a time and each one may mutate this table. If a later
         * representation is malformed, the earlier mutations have already happened, and a table
         * left half-updated - a size which no longer matches its entries, an entry inserted whose
         * value never finished decoding - is worse than a lost connection: every index into it is
         * then wrong in a way nothing detects. So mutation during a block is TRANSACTIONAL. The
         * nested Transaction opens one, commit( ) keeps it, and anything else - a throw, an early
         * return, a caller that simply forgets - rolls the table back to exactly the state it had
         * when the block started
         *
         * Rollback cannot itself fail, and that is why eviction inside a block is LOGICAL: an
         * evicted entry stays physically in the deque past the live region and is only dropped at
         * commit. Undoing a block is then popping the entries it pushed and restoring four
         * scalars, all of which are noexcept, and no allocation is needed on the failure path
         *
         * To be clear about what this does NOT claim: a COMPRESSION_ERROR is a connection error
         * (RFC 9113 section 4.3), so rolling back does not resynchronize us with the peer's
         * encoder - nothing can, since the peer applied the whole block. What it guarantees is
         * that this object stays internally consistent and usable, which is what stops one
         * malformed block from turning into an out-of-range index or an eviction loop later
         */

        template
        <
            typename E = void
        >
        class HpackDynamicTableT FINAL
        {
        public:

            explicit HpackDynamicTableT(
                SAA_in_opt      const std::size_t               capacity =
                    static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
                )
                :
                m_liveCount( 0U ),
                m_size( 0U ),
                m_capacity( capacity ),
                m_blockOpen( false ),
                m_savedLiveCount( 0U ),
                m_savedSize( 0U ),
                m_savedCapacity( 0U ),
                m_insertedInBlock( 0U )
            {
            }

            /**
             * @brief The maximum the entries may sum to before eviction begins
             */

            std::size_t capacity() const NOEXCEPT
            {
                return m_capacity;
            }

            /**
             * @brief The sum of the sizes of the live entries - section 4.1
             */

            std::size_t size() const NOEXCEPT
            {
                return m_size;
            }

            std::size_t entryCount() const NOEXCEPT
            {
                return m_liveCount;
            }

            /**
             * @brief The entry at a zero-based position, newest first
             *
             * Position 0 is what index 62 of the combined address space names
             */

            const HpackField& at( SAA_in const std::size_t pos ) const
            {
                BL_CHK_ARG( pos < m_liveCount, pos );

                return m_entries[ pos ];
            }

            /*************************************************************************
             * The combined address space - section 2.3.3
             */

            bool isValidIndex( SAA_in const std::uint32_t index ) const NOEXCEPT
            {
                if( index < 1U )
                {
                    return false;
                }

                return
                    static_cast< std::size_t >( index ) <=
                    static_cast< std::size_t >( HpackStaticTable::ENTRY_COUNT ) + m_liveCount;
            }

            const HpackField& fieldAtIndex( SAA_in const std::uint32_t index ) const
            {
                BL_CHK_ARG( isValidIndex( index ), index );

                if( index <= HpackStaticTable::ENTRY_COUNT )
                {
                    return HpackStaticTable::at( index );
                }

                return m_entries[ static_cast< std::size_t >( index - HpackStaticTable::ENTRY_COUNT ) - 1U ];
            }

            /**
             * @brief The lowest combined index of an entry matching both name and value, or 0
             *
             * The static table is searched first, so a field which is in both is encoded with
             * the smaller index - and the static index never moves, while a dynamic one shifts
             * with every insertion
             */

            std::uint32_t findFieldIndex(
                SAA_in          const std::string&              name,
                SAA_in          const std::string&              value
                ) const NOEXCEPT
            {
                const auto staticIndex = HpackStaticTable::findFieldIndex( name, value );

                if( staticIndex != 0U )
                {
                    return staticIndex;
                }

                for( std::size_t i = 0U; i < m_liveCount; ++i )
                {
                    if( m_entries[ i ].name() == name && m_entries[ i ].value() == value )
                    {
                        return toCombinedIndex( i );
                    }
                }

                return 0U;
            }

            std::uint32_t findNameIndex( SAA_in const std::string& name ) const NOEXCEPT
            {
                const auto staticIndex = HpackStaticTable::findNameIndex( name );

                if( staticIndex != 0U )
                {
                    return staticIndex;
                }

                for( std::size_t i = 0U; i < m_liveCount; ++i )
                {
                    if( m_entries[ i ].name() == name )
                    {
                        return toCombinedIndex( i );
                    }
                }

                return 0U;
            }

            /*************************************************************************
             * Mutation - sections 4.2, 4.3 and 4.4
             */

            /**
             * @brief Changes the maximum size, evicting from the oldest end until the entries
             * fit - section 4.3
             */

            void setCapacity( SAA_in const std::size_t capacity ) NOEXCEPT
            {
                m_capacity = capacity;

                evictUntilAtMost( m_capacity );
            }

            /**
             * @brief Inserts a field at the newest end - sections 3.2 and 4.4
             *
             * Entries are evicted from the oldest end until the table would hold the new entry.
             * An entry larger than the whole table is NOT an error: it empties the table and is
             * then not added, which is what section 4.4 requires and what a decoder must do or
             * lose its place
             */

            void insert( SAA_in HpackField&& field )
            {
                const auto entrySize = field.hpackSize();

                if( entrySize > m_capacity )
                {
                    evictUntilAtMost( 0U );

                    return;
                }

                evictUntilAtMost( m_capacity - entrySize );

                m_entries.push_front( BL_PARAM_FWD( field ) );

                ++m_liveCount;
                m_size += entrySize;

                if( m_blockOpen )
                {
                    ++m_insertedInBlock;
                }
            }

            void clear() NOEXCEPT
            {
                evictUntilAtMost( 0U );
            }

            /*************************************************************************
             * The block transaction - see the class note
             */

            class Transaction FINAL
            {
                BL_NO_COPY_OR_MOVE( Transaction )

            public:

                explicit Transaction( SAA_inout HpackDynamicTableT& table ) NOEXCEPT
                    :
                    m_table( &table )
                {
                    m_table -> beginBlock();
                }

                ~Transaction() NOEXCEPT
                {
                    if( m_table != nullptr )
                    {
                        m_table -> rollbackBlock();
                    }
                }

                void commit() NOEXCEPT
                {
                    if( m_table != nullptr )
                    {
                        m_table -> commitBlock();

                        m_table = nullptr;
                    }
                }

            private:

                HpackDynamicTableT*                             m_table;
            };

        private:

            /*
             * The entries, newest at the front. While a block is open the deque may also hold
             * entries past m_liveCount which the block has logically evicted - see the class note
             */

            std::deque< HpackField >                            m_entries;

            std::size_t                                         m_liveCount;
            std::size_t                                         m_size;
            std::size_t                                         m_capacity;

            bool                                                m_blockOpen;
            std::size_t                                         m_savedLiveCount;
            std::size_t                                         m_savedSize;
            std::size_t                                         m_savedCapacity;
            std::size_t                                         m_insertedInBlock;

            std::uint32_t toCombinedIndex( SAA_in const std::size_t pos ) const NOEXCEPT
            {
                return
                    HpackStaticTable::ENTRY_COUNT + static_cast< std::uint32_t >( pos ) + 1U;
            }

            void evictUntilAtMost( SAA_in const std::size_t target ) NOEXCEPT
            {
                while( m_size > target && m_liveCount != 0U )
                {
                    m_size -= m_entries[ m_liveCount - 1U ].hpackSize();

                    --m_liveCount;

                    if( ! m_blockOpen )
                    {
                        m_entries.pop_back();
                    }
                }
            }

            void beginBlock() NOEXCEPT
            {
                /*
                 * Nesting is not a thing a header block can do, and a second open would lose the
                 * first one's saved state - so the state is taken only on the outermost open
                 */

                if( m_blockOpen )
                {
                    return;
                }

                m_blockOpen = true;
                m_savedLiveCount = m_liveCount;
                m_savedSize = m_size;
                m_savedCapacity = m_capacity;
                m_insertedInBlock = 0U;
            }

            /**
             * @brief Drops the entries the block evicted, which were kept only so that it could
             * be rolled back
             *
             * resize( ) to a smaller size only destroys elements from the back of the deque and
             * frees whole nodes, so nothing here can throw
             */

            void commitBlock() NOEXCEPT
            {
                if( ! m_blockOpen )
                {
                    return;
                }

                m_entries.resize( m_liveCount );

                m_blockOpen = false;
                m_insertedInBlock = 0U;
            }

            /**
             * @brief Restores exactly the state the table had when the block opened
             *
             * Every step is noexcept: pop_front destroys one element, and the rest is four
             * scalars. That is the whole reason eviction inside a block is logical rather than
             * physical - nothing has to be allocated to put an evicted entry back
             */

            void rollbackBlock() NOEXCEPT
            {
                if( ! m_blockOpen )
                {
                    return;
                }

                while( m_insertedInBlock != 0U )
                {
                    m_entries.pop_front();

                    --m_insertedInBlock;
                }

                m_liveCount = m_savedLiveCount;
                m_size = m_savedSize;
                m_capacity = m_savedCapacity;

                m_blockOpen = false;
            }
        };

        typedef HpackDynamicTableT<> HpackDynamicTable;

    } // http2

} // bl

#endif /* __BL_HTTP2_HPACKDYNAMICTABLE_H_ */
