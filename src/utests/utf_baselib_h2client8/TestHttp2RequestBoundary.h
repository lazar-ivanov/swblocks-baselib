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

#ifndef __UTEST_TESTHTTP2REQUESTBOUNDARY_H_
#define __UTEST_TESTHTTP2REQUESTBOUNDARY_H_

#include <baselib/http2/Http2ConnectionTask.h>
#include <baselib/http2/HpackDecoder.h>
#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/Session.h>
#include <baselib/http2/Globals.h>

#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <baselib/core/Uri.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The HTTP/2 request boundary, read from the wire - astra's second review, D6 and D7
 * (notes/plans/issues/astra-second-review-decisions.md section 3)
 *
 * WHAT IS ASSERTED IS WHAT A PEER WOULD DECODE, NOT THE SessionRequest. Each case builds a
 * ClientRequest, runs it through toSessionRequest( ) - which is what submit( ) does on the
 * caller's thread - submits the result to a real client http2::Session, and decodes the HEADERS
 * block that session produces. H16's case, in utf_baselib_h2client2, asserts on the
 * SessionRequest, which says nothing about what the engine then emits from it.
 *
 * PURE. No socket, no strand, no peer, and a fixed ptime for a clock: every case is a statement
 * about a function of its input, so each is red or green deterministically.
 */

namespace utest
{
    namespace h2boundary
    {
        typedef bl::tasks::Http2ConnectionTaskT< bl::tasks::TcpSocketAsyncStrandedBase >  driver_t;

        inline auto makeRequest(
            SAA_in          const std::string&                                  url,
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( bl::cpp::copy( method ) );
            request.url( bl::net::Uri::parse( url ) );

            return request;
        }

        inline std::uint32_t octetAt(
            SAA_in          const std::string&                                  wire,
            SAA_in          const std::size_t                                   pos
            )
        {
            return static_cast< std::uint32_t >( static_cast< unsigned char >( wire[ pos ] ) );
        }

        /**
         * @brief The fields of the header block one ClientRequest puts on the wire, in wire
         * order, pseudo-headers included
         *
         * The opening write - the preface and our SETTINGS - is produced first and set aside, so
         * the second produce( ) holds this request's block and nothing else: a client may open a
         * stream before the peer's SETTINGS arrive (RFC 9113 3.4), and none of these requests
         * carries a body. The frames are walked by hand from their nine octet headers rather than
         * through FrameCodec, and the fragments are decoded by a fresh HpackDecoder, which is
         * exactly what a peer's decoder is for the first block of a connection.
         *
         * The session pads no HEADERS frame and adds priority fields only when a profile asks for
         * them, which the default profile used here does not - so the fragment is the whole
         * payload, and a frame carrying either flag fails the case rather than being decoded
         * wrongly.
         */

        inline auto emittedFields( SAA_in const bl::httpclient::ClientRequest& request )
            -> bl::http2::HpackFieldList
        {
            using namespace bl;
            using namespace bl::http2;

            const auto now = time::ptime( time::date( 2026, 9, 27 ) );

            Session session( StreamRole::Client, now );

            Session::wire_buffer_t opening;

            session.produce( opening, now );

            const auto streamId = session.submitRequest( driver_t::toSessionRequest( request ) );

            Session::wire_buffer_t out;

            session.produce( out, now );

            const auto wire = out.empty() ?
                std::string() :
                std::string( reinterpret_cast< const char* >( &out[ 0 ] ), out.size() );

            std::string block;
            std::size_t headersFrames = 0U;
            bool endHeaders = false;
            std::size_t offset = 0U;

            while( offset + 9U <= wire.size() )
            {
                const auto length =
                    ( octetAt( wire, offset ) << 16 ) |
                    ( octetAt( wire, offset + 1U ) << 8 ) |
                    octetAt( wire, offset + 2U );

                const auto type = octetAt( wire, offset + 3U );
                const auto flags = octetAt( wire, offset + 4U );

                const auto frameStreamId =
                    (
                        ( octetAt( wire, offset + 5U ) << 24 ) |
                        ( octetAt( wire, offset + 6U ) << 16 ) |
                        ( octetAt( wire, offset + 7U ) << 8 ) |
                        octetAt( wire, offset + 8U )
                    ) & 0x7FFFFFFFU;

                UTF_REQUIRE( offset + 9U + length <= wire.size() );
                UTF_REQUIRE_EQUAL( frameStreamId, streamId );

                if( type == Globals::FRAME_TYPE_HEADERS )
                {
                    UTF_REQUIRE_EQUAL(
                        flags & ( Globals::FRAME_FLAG_PADDED | Globals::FRAME_FLAG_PRIORITY ),
                        0U
                        );

                    ++headersFrames;
                }
                else
                {
                    UTF_REQUIRE_EQUAL( type, static_cast< std::uint32_t >( Globals::FRAME_TYPE_CONTINUATION ) );
                }

                block.append( wire, offset + 9U, length );

                endHeaders = 0U != ( flags & Globals::FRAME_FLAG_END_HEADERS );

                offset += 9U + length;
            }

            UTF_REQUIRE_EQUAL( offset, wire.size() );
            UTF_REQUIRE_EQUAL( headersFrames, 1U );
            UTF_REQUIRE( endHeaders );

            HpackDecoder decoder;
            HpackFieldList fields;

            UTF_REQUIRE(
                HpackDecoder::Outcome::Complete ==
                    decoder.decode(
                        block.data(),
                        block.size(),
                        ( std::numeric_limits< std::size_t >::max )(),
                        fields
                        )
                );

            return fields;
        }

        /**
         * @brief The regular fields of an emitted block, one 'name: value' line each, in wire
         * order
         *
         * A string rather than a comparison of lists, so that a failing check prints the whole
         * emitted list - which is the evidence a red run exists to show. The pseudo-headers are
         * left out: they come from the URL and the method, not from the caller's header list.
         */

        inline std::string regularFieldsOf( SAA_in const bl::http2::HpackFieldList& fields )
        {
            std::string result;

            for( std::size_t i = 0U; i < fields.size(); ++i )
            {
                const auto& name = fields[ i ].name();

                if( ! name.empty() && name[ 0 ] == ':' )
                {
                    continue;
                }

                result += name;
                result += ": ";
                result += fields[ i ].value();
                result += '\n';
            }

            return result;
        }

    } // h2boundary

} // utest

/**
 * @brief D6 (R05) - TE is canonicalized across EVERY field that carries it
 *
 * THE DEFECT. normalizeHeaders( ) read only the FIRST TE field - HeaderList::tryGet( ) returns the
 * first - and kept or removed every TE field on what that one said. 'TE: trailers' followed by
 * 'TE: gzip' therefore sent both, and RFC 9113 8.2.2 makes a request whose te field carries
 * anything but 'trailers' malformed; while 'TE: gzip' followed by 'TE: trailers', or the one field
 * 'TE: gzip, trailers', lost the trailers the caller had asked for.
 *
 * THE RULE, decided 2026-09-27: if any TE field lists 'trailers' among its codings, exactly one
 * 'te: trailers' is sent, and otherwise none. It is sent where the first TE field was, so the
 * caller's order - which a profile's fingerprint is made of - is kept. HeaderList itself keeps
 * repeated fields, as its contract says: the canonicalization belongs to the HTTP/2 boundary.
 *
 * Every script puts 'X-Kept' behind its TE fields, so each one also shows that nothing else in the
 * list moved.
 */

UTF_AUTO_TEST_CASE( H2Driver_TeIsCanonicalizedAcrossRepeatedFieldsTests )
{
    using namespace utest::h2boundary;

    struct Script
    {
        const char*     first;
        const char*     second;
        const char*     emitted;
    };

    static const Script scripts[] =
    {
        /*
         * Red before the fix: the second TE field was never read
         */

        { "trailers",               "gzip",             "te: trailers\nx-kept: yes\n"   },
        { "trailers",               "trailers",         "te: trailers\nx-kept: yes\n"   },
        { "gzip",                   "trailers",         "te: trailers\nx-kept: yes\n"   },

        /*
         * Red before the fix: one field listing several codings was compared whole. The second
         * carries OWS and a coding in another case, both of which RFC 9110 5.6.1 and 10.1.4 allow
         */

        { "gzip, trailers",         nullptr,            "te: trailers\nx-kept: yes\n"   },
        { "deflate ,Trailers",      nullptr,            "te: trailers\nx-kept: yes\n"   },

        /*
         * Controls, green on both sides: the ordinary single field, and TE fields none of which
         * lists trailers, which send no te at all
         */

        { "trailers",               nullptr,            "te: trailers\nx-kept: yes\n"   },
        { "gzip",                   "deflate",          "x-kept: yes\n"                 },
        { "gzip;q=0.5, deflate",    nullptr,            "x-kept: yes\n"                 },
    };

    for( std::size_t i = 0U; i < sizeof( scripts ) / sizeof( scripts[ 0 ] ); ++i )
    {
        const auto& script = scripts[ i ];

        UTF_MESSAGE(
            std::string( "TE: '" ) + script.first + "'" +
            ( script.second ? std::string( ", then TE: '" ) + script.second + "'" : std::string() )
            );

        auto request = makeRequest( "https://example.com/p" );

        request.headers().append( "TE", script.first );

        if( script.second )
        {
            request.headers().append( "te", script.second );
        }

        request.headers().append( "X-Kept", "yes" );

        UTF_CHECK_EQUAL( regularFieldsOf( emittedFields( request ) ), std::string( script.emitted ) );
    }
}

/**
 * @brief D6 (R05) - every field a Connection field names is removed, reading EVERY Connection field
 *
 * THE DEFECT. normalizeHeaders( ) removed 'connection' in its fixed-name loop without reading it,
 * so the fields its tokens name - fields the caller had marked as meant for the first hop only,
 * RFC 9110 7.6.1 - travelled on past it.
 *
 * THE RULE, decided 2026-09-27: the tokens of every Connection field are collected before
 * anything is removed, the fields they name are removed, and then Connection itself and the fixed
 * connection-specific names of RFC 9113 8.2.2 go. A field no token names stays, repeats and order
 * included.
 *
 * EXCEPT 'te', which is what the second block is for. RFC 9110 10.1.4 has a sender of TE also send
 * a TE connection option, so 'Connection: TE' beside 'TE: trailers' is what a CORRECT caller writes
 * - and trailers is the one TE value HTTP/2 permits. A fix which removed everything Connection
 * names would strip it; te is governed by its own rule instead. That first request is green before
 * the fix, because the unfixed code read no token at all: it is the control against the obvious
 * fix, not against the defect.
 */

UTF_AUTO_TEST_CASE( H2Driver_ConnectionTokensRemoveTheFieldsTheyNameTests )
{
    using namespace utest::h2boundary;

    /*
     * (1) Two Connection fields, with a list, OWS, empty elements and a token in another case.
     * Red before the fix: every field they name was emitted
     */

    {
        auto request = makeRequest( "https://example.com/p" );

        request.headers().append( "Connection", "keep-alive, X-Hop-One" );
        request.headers().append( "X-Hop-One", "1" );
        request.headers().append( "X-End-To-End", "kept" );
        request.headers().append( "connection", ",x-hop-two ,, X-HOP-THREE" );
        request.headers().append( "X-Hop-Two", "2" );
        request.headers().append( "X-Repeat", "a" );
        request.headers().append( "x-hop-three", "3" );
        request.headers().append( "X-Repeat", "b" );
        request.headers().append( "Keep-Alive", "timeout=5" );

        UTF_CHECK_EQUAL(
            regularFieldsOf( emittedFields( request ) ),
            std::string( "x-end-to-end: kept\nx-repeat: a\nx-repeat: b\n" )
            );
    }

    /*
     * (2) te survives being named, and its own rule still decides it
     */

    {
        auto kept = makeRequest( "https://example.com/p" );

        kept.headers().append( "Connection", "TE" );
        kept.headers().append( "TE", "trailers" );
        kept.headers().append( "X-Kept", "yes" );

        UTF_CHECK_EQUAL(
            regularFieldsOf( emittedFields( kept ) ),
            std::string( "te: trailers\nx-kept: yes\n" )
            );

        /*
         * Red before the fix, for X-Hop: te goes by its own rule here, and X-Hop by the token
         */

        auto dropped = makeRequest( "https://example.com/p" );

        dropped.headers().append( "Connection", "te, X-Hop" );
        dropped.headers().append( "TE", "gzip" );
        dropped.headers().append( "X-Hop", "1" );
        dropped.headers().append( "X-Kept", "yes" );

        UTF_CHECK_EQUAL(
            regularFieldsOf( emittedFields( dropped ) ),
            std::string( "x-kept: yes\n" )
            );
    }
}

#endif /* __UTEST_TESTHTTP2REQUESTBOUNDARY_H_ */
