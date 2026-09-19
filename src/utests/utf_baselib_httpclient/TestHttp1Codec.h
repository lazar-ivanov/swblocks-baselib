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

#include <baselib/httpclient/Http1Codec.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * Slice S2.5 - the HTTP/1.1 codec (design 5.5, decision D15)
 *
 * WHAT THE SMUGGLING CASES BELOW ARE FOR. A response whose framing can be read two ways is how a
 * client and whatever sits in front of it are made to disagree about where one message ends and
 * the next begins. Every such case here is asserted from BOTH sides - the spelling which must be
 * refused next to the one which must still be accepted - because a list of refusals says nothing
 * about where the boundary is, and a check which refuses everything passes it just as well
 *
 * Several of these are refused by the Beast backend and several by the facade, and the cases do
 * not care which: they assert the codec's own Http1CodecError, which is the point of the facade
 */

namespace utest
{
    namespace http1codec
    {
        namespace ht = bl::httpclient;

        /**
         * @brief Feeds the whole of a response in one go and reports what came back
         */

        struct ParseOutcome
        {
            bl::cpp::ScalarTypeIniter< std::size_t >                            consumed;
            bl::cpp::ScalarTypeIniter< bool >                                   hasError;
            bl::cpp::ScalarTypeIniter< ht::Http1CodecError >                    codecError;

            ParseOutcome() NOEXCEPT
            {
                codecError = ht::Http1CodecError::None;
            }
        };

        inline ParseOutcome feed(
            bl::httpclient::Http1ResponseParser&                                parser,
            const std::string&                                                  wire,
            const bool                                                          eofAfter = false
            )
        {
            ParseOutcome outcome;

            bl::eh::error_code ec;

            outcome.consumed = parser.parse( wire, ec );

            if( ! ec && eofAfter && ! parser.isComplete() )
            {
                parser.parseEof( ec );
            }

            outcome.hasError = !! ec;
            outcome.codecError = parser.codecError();

            return outcome;
        }

        /**
         * @brief The refusal half of a both-sides assertion
         *
         * Named rather than inlined because every smuggling case below is this same shape, and a
         * bare UTF_CHECK inside a loop names nothing when it fails - this library's Utf.h has no
         * UTF_CHECK_MESSAGE, so the description has to travel inside the compared values
         */

        inline void checkRefused(
            const std::string&                                                  what,
            const std::string&                                                  wire,
            const ht::Http1CodecError                                           expected
            )
        {
            bl::httpclient::Http1ResponseParser parser;

            const auto outcome = feed( parser, wire, true /* eofAfter */ );

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":refused" : ":ACCEPTED" ), what + ":refused" );

            UTF_CHECK_EQUAL(
                what + ( expected == outcome.codecError.value() ? ":right-reason" : ":WRONG-REASON" ),
                what + ":right-reason"
                );

            UTF_CHECK_EQUAL(
                what + ( parser.isComplete() ? ":COMPLETED" : ":not-complete" ),
                what + ":not-complete"
                );
        }

        inline void checkAccepted(
            const std::string&                                                  what,
            const std::string&                                                  wire
            )
        {
            bl::httpclient::Http1ResponseParser parser;

            const auto outcome = feed( parser, wire, true /* eofAfter */ );

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":REFUSED" : ":accepted" ), what + ":accepted" );

            UTF_CHECK_EQUAL(
                what + ( parser.isComplete() ? ":complete" : ":INCOMPLETE" ),
                what + ":complete"
                );
        }

        inline std::string headersAsText( const bl::http::HeaderList& headers )
        {
            std::string result;

            for( auto it = headers.begin(); it != headers.end(); ++it )
            {
                result += it -> name();
                result += "=";
                result += it -> value();
                result += ";";
            }

            return result;
        }

        /**
         * @brief A small profile whose only content is synthetic - no browser is described here
         */

        inline ht::HeaderProfileForKind sampleProfile()
        {
            ht::HeaderProfileForKind profile;

            const char* names[] = { "host", "user-agent", "accept", "accept-language" };
            const char* values[] = { "", "ProfileUA", "*/*", "en" };
            const char* cased[] = { "Host", "User-Agent", "Accept", "Accept-Language" };

            for( std::size_t i = 0U; i < 4U; ++i )
            {
                ht::ProfileHeader header;

                header.name = names[ i ];
                header.value = values[ i ];

                profile.defaultHeaders.push_back( header );

                profile.http1CaseMap[ names[ i ] ] = cased[ i ];
            }

            profile.http1CaseMap[ "x-caller" ] = "X-Caller";

            profile.callerHeaderPlacement = ht::CallerHeaderPlacement::Appended;

            return profile;
        }

    } // http1codec

} // utest

UTF_AUTO_TEST_CASE( Http1Codec_StatusLineHeadersAndBodyTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * The status line, the fields in the order and the CASE they arrived in, and an identity body
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "X-Mixed-CASE: Value\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello"
            );

        UTF_REQUIRE( ! outcome.hasError );

        UTF_CHECK_EQUAL( parser.statusCode(), 200 );
        UTF_CHECK_EQUAL( parser.reasonPhrase(), std::string( "OK" ) );
        UTF_CHECK_EQUAL( parser.httpVersion(), 11 );
        UTF_CHECK( parser.isHeaderComplete() );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK( ! parser.isChunked() );
        UTF_CHECK( ! parser.needsEof() );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hello" ) );

        /*
         * The exact spelling is kept, which is what an impersonating client needs to see
         */

        UTF_CHECK_EQUAL(
            headersAsText( parser.headers() ),
            std::string( "Content-Type=text/plain;X-Mixed-CASE=Value;Content-Length=5;" )
            );
    }

    /*
     * A reason phrase may be empty, and HTTP/1.0 is reported as 10
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed( parser, "HTTP/1.0 404 \r\nContent-Length: 0\r\n\r\n" );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK_EQUAL( parser.statusCode(), 404 );
        UTF_CHECK_EQUAL( parser.httpVersion(), 10 );
        UTF_CHECK( parser.reasonPhrase().empty() );
        UTF_CHECK( parser.isComplete() );
    }

    /*
     * Fed one byte at a time, which is the shape a socket actually delivers
     *
     * THE PARSER DOES NOT BUFFER, and this case is what pins that. A feed which does not consume
     * everything it was given leaves the rest to the caller, who must present it again with the
     * next bytes - so the loop below grows a pending buffer and erases only what was consumed. A
     * caller which instead advanced past unconsumed bytes would silently lose them, and the one
     * byte the parser needed to finish the status line would never come back
     */

    {
        const std::string wire =
            "HTTP/1.1 201 Created\r\nX-A: b\r\nContent-Length: 11\r\n\r\nhello world";

        httpclient::Http1ResponseParser parser;

        std::string pending;

        for( std::size_t i = 0U; i < wire.size() && ! parser.isComplete(); ++i )
        {
            pending += wire[ i ];

            eh::error_code ec;

            const auto consumed = parser.parse( pending, ec );

            UTF_REQUIRE( ! ec );

            pending.erase( 0U, consumed );
        }

        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( parser.statusCode(), 201 );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hello world" ) );
        UTF_CHECK( pending.empty() );
    }

    /*
     * A body callback takes the bytes instead of body() buffering them - the streaming path design
     * 5.3 needs, and the one which must never see data from a message that was refused
     */

    {
        httpclient::Http1ResponseParser parser;

        std::string streamed;

        parser.bodyCallback(
            [ &streamed ]( SAA_in const char* data, SAA_in const std::size_t size ) -> std::size_t
            {
                streamed.append( data, size );

                return size;
            }
            );

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nstreamed-out"
            );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK_EQUAL( streamed, std::string( "streamed-out" ) );
        UTF_CHECK( parser.body().empty() );
    }

    /*
     * The tail of the buffer beyond one message is NOT consumed - it is the next response on a
     * kept-alive connection and belongs to the next parser
     */

    {
        httpclient::Http1ResponseParser parser;

        const std::string first = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
        const std::string wire = first + "HTTP/1.1 500 Oops\r\nContent-Length: 0\r\n\r\n";

        const auto outcome = feed( parser, wire );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( outcome.consumed.value(), first.size() );
    }
}

UTF_AUTO_TEST_CASE( Http1Codec_ChunkedAndTrailersTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * Several chunks, a chunk extension, and a trailer section - the whole of it in one feed
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Trailer: X-Checksum\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6;name=value\r\n world\r\n"
            "0\r\n"
            "X-Checksum: 42\r\n"
            "X-Second: b\r\n"
            "\r\n"
            );

        UTF_REQUIRE( ! outcome.hasError );

        UTF_CHECK( parser.isChunked() );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK( ! parser.needsEof() );

        /*
         * De-chunked, and the extension is not part of the body
         */

        UTF_CHECK_EQUAL( parser.body(), std::string( "hello world" ) );

        /*
         * The trailers are kept SEPARATE from the headers. Merging them is how a trailer becomes
         * a header injection, so the case asserts the separation in both directions
         */

        UTF_CHECK_EQUAL( headersAsText( parser.trailers() ), std::string( "X-Checksum=42;X-Second=b;" ) );

        UTF_CHECK( ! parser.headers().has( "X-Checksum" ) );
        UTF_CHECK( parser.headers().has( "Trailer" ) );
        UTF_CHECK( ! parser.trailers().has( "Trailer" ) );
    }

    /*
     * A chunked body with no trailer section at all, and an empty chunked body
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n"
            );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hello" ) );
        UTF_CHECK( parser.trailers().empty() );
    }

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
            );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK( parser.body().empty() );
    }

    /*
     * Chunked delivered in pieces which split a chunk header, a chunk body and the trailer
     * section, because that is where a chunk parser goes wrong
     */

    {
        const char* pieces[] =
        {
            "HTTP/1.1 200 OK\r\nTransfer-En",
            "coding: chunked\r\n\r\n",
            "5\r\nhel",
            "lo\r\n",
            "3\r\n th\r\n0\r\nX-T: ",
            "1\r\n\r\n",
        };

        httpclient::Http1ResponseParser parser;

        std::string pending;

        for( std::size_t i = 0U; i < 6U; ++i )
        {
            pending += pieces[ i ];

            eh::error_code ec;

            const auto consumed = parser.parse( pending, ec );

            UTF_REQUIRE( ! ec );

            pending.erase( 0U, consumed );
        }

        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hello th" ) );
        UTF_CHECK_EQUAL( headersAsText( parser.trailers() ), std::string( "X-T=1;" ) );
    }

    /*
     * The transfer coding is recognised however it is spelled, which is what makes the refusal of
     * every OTHER coding in the smuggling case a statement about the coding and not about parsing
     */

    checkAccepted(
        "TE-uppercase",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: CHUNKED\r\n\r\n0\r\n\r\n"
        );

    checkAccepted(
        "TE-surrounded-by-OWS",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding:  chunked \r\n\r\n0\r\n\r\n"
        );
}

UTF_AUTO_TEST_CASE( Http1Codec_ReadUntilCloseAndBodilessTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * No Content-Length and no Transfer-Encoding: the body runs to the close, which the parser
     * cannot know has happened until it is told
     */

    {
        httpclient::Http1ResponseParser parser;

        eh::error_code ec;

        parser.parse(
            std::string( "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbytes-to-the-end" ),
            ec
            );

        UTF_REQUIRE( ! ec );

        UTF_CHECK( parser.isHeaderComplete() );
        UTF_CHECK( parser.needsEof() );
        UTF_CHECK( ! parser.isComplete() );

        parser.parseEof( ec );

        UTF_REQUIRE( ! ec );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( parser.body(), std::string( "bytes-to-the-end" ) );
    }

    /*
     * 204 and 304 carry no body. The 304 carries a Content-Length of a body it does NOT have -
     * RFC 9112 6.3 - and a parser which waited for those bytes would hang the connection
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed( parser, "HTTP/1.1 204 No Content\r\nX-A: b\r\n\r\n" );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK( ! parser.needsEof() );
        UTF_CHECK( parser.body().empty() );
    }

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed( parser, "HTTP/1.1 304 Not Modified\r\nContent-Length: 100\r\n\r\n" );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK( parser.body().empty() );

        /*
         * The field is still reported; it is the framing which ignores it
         */

        UTF_CHECK( parser.headers().has( "content-length" ) );
    }

    /*
     * A HEAD response, both ways round. The parser must be TOLD, before the first byte, that the
     * request was a HEAD - and the second half of this is the point: without being told it waits
     */

    {
        const std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n";

        {
            httpclient::Http1ResponseParser toHead(
                httpclient::Http1ResponseLimits(),
                true    /* isResponseToHead */
                );

            const auto outcome = feed( toHead, wire );

            UTF_REQUIRE( ! outcome.hasError );
            UTF_CHECK( toHead.isComplete() );
            UTF_CHECK( toHead.body().empty() );
        }

        {
            httpclient::Http1ResponseParser notToHead;

            const auto outcome = feed( notToHead, wire );

            UTF_REQUIRE( ! outcome.hasError );
            UTF_CHECK( notToHead.isHeaderComplete() );
            UTF_CHECK( ! notToHead.isComplete() );
        }
    }

    /*
     * An interim 1xx is filed and the parse continues through it on the SAME buffer. The backend
     * sees a 1xx as a complete message and stops there, so this is the codec's own work
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 100 Continue\r\n\r\n"
            "HTTP/1.1 103 Early Hints\r\nLink: </s.css>; rel=preload\r\n\r\n"
            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"
            );

        UTF_REQUIRE( ! outcome.hasError );

        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( parser.statusCode(), 200 );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hi" ) );

        UTF_REQUIRE_EQUAL( parser.interimResponses().size(), 2U );
        UTF_CHECK_EQUAL( parser.interimResponses()[ 0 ].statusCode.value(), 100 );
        UTF_CHECK_EQUAL( parser.interimResponses()[ 1 ].statusCode.value(), 103 );
        UTF_CHECK( parser.interimResponses()[ 1 ].headers.has( "link" ) );

        /*
         * The interim's fields are NOT in the final response's header list
         */

        UTF_CHECK( ! parser.headers().has( "link" ) );
    }

    /*
     * 101 is a FINAL response, not an interim one. Treating it as interim would leave the parser
     * waiting for a response which is never coming, because the bytes after it are not HTTP
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n"
            );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
        UTF_CHECK_EQUAL( parser.statusCode(), 101 );
        UTF_CHECK( parser.interimResponses().empty() );
    }

    /*
     * A message cut short by the close is a refusal and not a short body - otherwise a truncated
     * response is indistinguishable from a complete one
     */

    checkRefused(
        "truncated-at-close",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel",
        httpclient::Http1CodecError::MalformedMessage
        );
}

UTF_AUTO_TEST_CASE( Http1Codec_SmugglingDefencesTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * ---- Content-Length against Content-Length ------------------------------------------------
     *
     * Two lengths which disagree is the original desync: whichever one a component picks, some
     * other component picks the other
     */

    checkRefused(
        "two-CL-differing",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello",
        httpclient::Http1CodecError::InvalidFraming
        );

    /*
     * ... and the other side of that boundary. Two IDENTICAL lengths are valid framing (RFC 9112
     * 6.3 makes only differing values invalid) - there is no second reading to pick between, and
     * refusing them would refuse responses real servers send
     */

    checkAccepted(
        "two-CL-identical",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello"
        );

    /*
     * The comma list form is what a proxy produces when it JOINS two separate Content-Length
     * fields, so '5, 5' is the same attack with one layer of laundering on it. RFC 9112 8.6 gives
     * the field the grammar 1*DIGIT, so the list is invalid framing however consistent it is -
     * and the backend accepts it, which is why the facade checks
     */

    checkRefused(
        "CL-list-consistent",
        "HTTP/1.1 200 OK\r\nContent-Length: 5, 5\r\n\r\nhello",
        httpclient::Http1CodecError::InvalidContentLength
        );

    checkRefused(
        "CL-list-inconsistent",
        "HTTP/1.1 200 OK\r\nContent-Length: 5, 6\r\n\r\nhello",
        httpclient::Http1CodecError::InvalidFraming
        );

    /*
     * A length which is not a plain decimal number, each spelling on its own
     */

    {
        const char* badLengths[] = { "+5", "-5", "0x5", "5.0", "five", "" };

        for( std::size_t i = 0U; i < 6U; ++i )
        {
            httpclient::Http1ResponseParser parser;

            const auto outcome = feed(
                parser,
                std::string( "HTTP/1.1 200 OK\r\nContent-Length: " ) + badLengths[ i ] + "\r\n\r\nhello",
                true /* eofAfter */
                );

            const std::string what = std::string( "CL-'" ) + badLengths[ i ] + "'";

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":refused" : ":ACCEPTED" ), what + ":refused" );
        }
    }

    /*
     * ---- Content-Length against Transfer-Encoding ---------------------------------------------
     *
     * BOTH ORDERS, deliberately. The backend refuses the pair only when the coding is chunked, and
     * an asymmetry where the same two fields are a refusal one way round and an accept the other
     * is itself the differential
     */

    checkRefused(
        "CL-then-TE-chunked",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        httpclient::Http1CodecError::InvalidFraming
        );

    checkRefused(
        "TE-chunked-then-CL",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        httpclient::Http1CodecError::InvalidFraming
        );

    checkRefused(
        "CL-then-TE-gzip",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: gzip\r\n\r\nhello",
        httpclient::Http1CodecError::InvalidFraming
        );

    /*
     * This one is the reason the facade has a check of its own: the backend ACCEPTS it
     */

    checkRefused(
        "TE-gzip-then-CL",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\nContent-Length: 5\r\n\r\nhello",
        httpclient::Http1CodecError::UnsupportedTransferCoding
        );

    /*
     * ---- Transfer-Encoding which is not exactly 'chunked' -------------------------------------
     *
     * 'chunked, gzip' is the dangerous one: the backend accepts it and silently turns the message
     * into a read-until-close body whose bytes are the raw chunk framing, which is exactly a
     * component reading the same octets a different way. D9 ships no transfer decoder, so there is
     * nothing this client could do with any other coding in any case
     */

    {
        const char* codings[] = { "gzip", "chunked, gzip", "gzip, chunked", "identity", "deflate", "chunked, chunked" };

        for( std::size_t i = 0U; i < 6U; ++i )
        {
            checkRefused(
                std::string( "TE-'" ) + codings[ i ] + "'",
                std::string( "HTTP/1.1 200 OK\r\nTransfer-Encoding: " ) + codings[ i ] +
                    "\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
                httpclient::Http1CodecError::UnsupportedTransferCoding
                );
        }
    }

    /*
     * ---- Obsolete line folding ----------------------------------------------------------------
     *
     * The backend UNFOLDS silently - the folded value arrives indistinguishable from an unfolded
     * one - so this is detected on the raw bytes or not at all. A fold is how a value is smuggled
     * past a filter which inspects one line at a time
     */

    checkRefused(
        "obs-fold-space",
        "HTTP/1.1 200 OK\r\nX-Fold: one\r\n  two\r\nContent-Length: 2\r\n\r\nhi",
        httpclient::Http1CodecError::ObsoleteLineFolding
        );

    checkRefused(
        "obs-fold-tab",
        "HTTP/1.1 200 OK\r\nX-Fold: one\r\n\ttwo\r\nContent-Length: 2\r\n\r\nhi",
        httpclient::Http1CodecError::ObsoleteLineFolding
        );

    checkRefused(
        "obs-fold-of-the-last-field",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n \r\n\r\nhi",
        httpclient::Http1CodecError::ObsoleteLineFolding
        );

    /*
     * ... and the three things which must NOT be mistaken for a fold. The last is the one which
     * proves the scanner stops at the end of the head rather than running over the whole stream
     */

    checkAccepted(
        "value-containing-a-space",
        "HTTP/1.1 200 OK\r\nX-Fold: one two\r\nContent-Length: 2\r\n\r\nhi"
        );

    checkAccepted(
        "value-containing-an-inner-tab",
        "HTTP/1.1 200 OK\r\nX-Fold: one\ttwo\r\nContent-Length: 2\r\n\r\nhi"
        );

    checkAccepted(
        "body-containing-LF-then-space",
        "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\na\n b"
        );

    /*
     * Nothing of a folded message reaches a body callback, because the check runs ahead of the
     * backend. A streaming consumer must not be handed data from a message the parser is about to
     * reject
     */

    {
        httpclient::Http1ResponseParser parser;

        std::string streamed;

        parser.bodyCallback(
            [ &streamed ]( SAA_in const char* data, SAA_in const std::size_t size ) -> std::size_t
            {
                streamed.append( data, size );

                return size;
            }
            );

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nX-Fold: one\r\n  two\r\nContent-Length: 2\r\n\r\nhi"
            );

        UTF_CHECK( outcome.hasError );
        UTF_CHECK( streamed.empty() );
        UTF_CHECK_EQUAL( outcome.consumed.value(), 0U );
        UTF_CHECK( parser.headers().empty() );
    }

    /*
     * ---- The trailer section --------------------------------------------------------------------
     *
     * A trailer which can set Location, Set-Cookie or Content-Type is a response-splitting
     * primitive against anything which merges trailers into the header list. RFC 9110 6.5.1
     */

    {
        const char* forbidden[] =
        {
            "Content-Length: 9", "Transfer-Encoding: chunked", "Location: /elsewhere",
            "Set-Cookie: a=b", "Content-Type: text/html", "Host: other.test",
            "Authorization: Basic x", "Cache-Control: no-store",
        };

        for( std::size_t i = 0U; i < 8U; ++i )
        {
            httpclient::Http1ResponseParser parser;

            const auto outcome = feed(
                parser,
                std::string( "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n" ) +
                    forbidden[ i ] + "\r\n\r\n",
                true /* eofAfter */
                );

            const std::string what = std::string( "trailer-'" ) + forbidden[ i ] + "'";

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":refused" : ":ACCEPTED" ), what + ":refused" );
            UTF_CHECK( parser.trailers().empty() );
        }
    }

    /*
     * ... and an ordinary trailer still arrives, which is what makes the list above a boundary
     */

    checkAccepted(
        "trailer-ordinary",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Checksum: 42\r\n\r\n"
        );

    /*
     * ---- Malformed at the lexical level ---------------------------------------------------------
     */

    {
        const char* malformed[] =
        {
            "HTTP/1.1 200 OK\nContent-Length: 2\n\nhi",
            "HTTP/1.1 2000 OK\r\nContent-Length: 2\r\n\r\nhi",
            "HTTP/1.1 200 OK\r\nContent-Length : 5\r\n\r\nhello",
            "HTTP/1.1 200 OK\r\nX Bad: 1\r\nContent-Length: 0\r\n\r\n",
            "NOT-HTTP 200 OK\r\nContent-Length: 0\r\n\r\n",
        };

        for( std::size_t i = 0U; i < 5U; ++i )
        {
            httpclient::Http1ResponseParser parser;

            const auto outcome = feed( parser, malformed[ i ], true /* eofAfter */ );

            const std::string what = "malformed-" + std::to_string( i );

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":refused" : ":ACCEPTED" ), what + ":refused" );
        }
    }

    /*
     * ---- A refusal is final -----------------------------------------------------------------
     *
     * A caller which ignores the first refusal must not be able to get a cleaner answer out of the
     * same parser, which would be the differential all over again - this time between two readings
     * by the SAME component
     */

    {
        httpclient::Http1ResponseParser parser;

        const auto first = feed(
            parser,
            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello"
            );

        UTF_REQUIRE( first.hasError );

        eh::error_code ec;

        const auto consumed = parser.parse( std::string( "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" ), ec );

        UTF_CHECK( !! ec );
        UTF_CHECK_EQUAL( consumed, 0U );
        UTF_CHECK( ! parser.isComplete() );
        UTF_CHECK( httpclient::Http1CodecError::InvalidFraming == parser.codecError() );

        parser.parseEof( ec );

        UTF_CHECK( !! ec );
    }
}

UTF_AUTO_TEST_CASE( Http1Codec_LimitsTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * The 64 KB cap of design 5.5, which is set by the facade. The backend's own default is 8 KB,
     * so a 9 KB head is exactly the size which tells the two apart
     */

    {
        httpclient::Http1ResponseLimits defaults;

        UTF_CHECK_EQUAL( defaults.maxHeadersSize.value(), 65536U );

        httpclient::Http1ResponseParser parser;

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nX-Long: " + std::string( 9000U, 'a' ) + "\r\nContent-Length: 2\r\n\r\nhi"
            );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK( parser.isComplete() );
    }

    /*
     * ... and a head over the cap is refused BEFORE the header section completes, so an oversized
     * head is never assembled in memory
     */

    {
        httpclient::Http1ResponseLimits limits;

        limits.maxHeadersSize = 128U;

        httpclient::Http1ResponseParser parser( limits );

        const auto outcome = feed(
            parser,
            "HTTP/1.1 200 OK\r\nX-Long: " + std::string( 4000U, 'a' ) + "\r\nContent-Length: 2\r\n\r\nhi"
            );

        UTF_CHECK( outcome.hasError );
        UTF_CHECK( httpclient::Http1CodecError::HeadersTooLarge == parser.codecError() );
        UTF_CHECK( ! parser.isHeaderComplete() );
    }

    /*
     * The body cap, on BOTH framings - a cap which only counted identity bodies would leave the
     * chunked path, which is the one with no declared length, uncapped
     */

    {
        const char* wires[] =
        {
            "HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\n0123456789012345678901234567890123456789",
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n28\r\n0123456789012345678901234567890123456789\r\n0\r\n\r\n",
            "HTTP/1.1 200 OK\r\nX-A: b\r\n\r\n0123456789012345678901234567890123456789",
        };

        for( std::size_t i = 0U; i < 3U; ++i )
        {
            httpclient::Http1ResponseLimits limits;

            limits.maxBodySize = 8U;

            httpclient::Http1ResponseParser parser( limits );

            const auto outcome = feed( parser, wires[ i ], true /* eofAfter */ );

            const std::string what = "body-cap-" + std::to_string( i );

            UTF_CHECK_EQUAL( what + ( outcome.hasError ? ":refused" : ":ACCEPTED" ), what + ":refused" );

            UTF_CHECK_EQUAL(
                what + (
                    httpclient::Http1CodecError::BodyTooLarge == parser.codecError()
                        ? ":body-too-large"
                        : ":WRONG-REASON"
                    ),
                what + ":body-too-large"
                );
        }
    }

    /*
     * ... and a body at the cap is not over it. Without this the cases above would pass just as
     * well against a parser which refused every body
     */

    {
        httpclient::Http1ResponseLimits limits;

        limits.maxBodySize = 5U;

        httpclient::Http1ResponseParser parser( limits );

        const auto outcome = feed( parser, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello" );

        UTF_REQUIRE( ! outcome.hasError );
        UTF_CHECK_EQUAL( parser.body(), std::string( "hello" ) );
    }
}

UTF_AUTO_TEST_CASE( Http1Codec_RequestSerializerTests )
{
    using namespace bl;
    using namespace utest::http1codec;

    /*
     * The request target is origin-form, the fragment is never sent, and an empty path is '/'
     */

    {
        UTF_CHECK_EQUAL(
            httpclient::Http1RequestSerializer::requestTarget(
                net::Uri::parse( "https://example.com/p/q?x=1#frag" )
                ),
            std::string( "/p/q?x=1" )
            );

        UTF_CHECK_EQUAL(
            httpclient::Http1RequestSerializer::requestTarget( net::Uri::parse( "https://example.com" ) ),
            std::string( "/" )
            );

        /*
         * The default port is not spelled out and a non-default one is - a browser sends
         * 'example.com', never 'example.com:443'
         */

        UTF_CHECK_EQUAL(
            httpclient::Http1RequestSerializer::hostHeaderValue(
                net::Uri::parse( "https://example.com/p" )
                ),
            std::string( "example.com" )
            );

        UTF_CHECK_EQUAL(
            httpclient::Http1RequestSerializer::hostHeaderValue(
                net::Uri::parse( "https://example.com:8443/p" )
                ),
            std::string( "example.com:8443" )
            );
    }

    /*
     * The profile's order and the profile's CASING, which together are the fingerprint. A caller
     * who supplies a name the profile already sends changes its VALUE and not its POSITION
     */

    {
        auto profile = sampleProfile();

        http::HeaderList caller;

        caller.append( "host", "example.com" );
        caller.append( "x-caller", "1" );

        const auto ordered = httpclient::Http1RequestSerializer::orderHeaders( caller, profile );

        UTF_CHECK_EQUAL(
            headersAsText( ordered ),
            std::string( "Host=example.com;User-Agent=ProfileUA;Accept=*/*;Accept-Language=en;X-Caller=1;" )
            );
    }

    /*
     * The three placements, each changing where the caller's own header lands and nothing else
     */

    {
        auto profile = sampleProfile();

        http::HeaderList caller;

        caller.append( "host", "example.com" );
        caller.append( "x-caller", "1" );

        profile.callerHeaderPlacement = httpclient::CallerHeaderPlacement::Prepended;

        UTF_CHECK_EQUAL(
            headersAsText( httpclient::Http1RequestSerializer::orderHeaders( caller, profile ) ),
            std::string( "X-Caller=1;Host=example.com;User-Agent=ProfileUA;Accept=*/*;Accept-Language=en;" )
            );

        profile.callerHeaderPlacement = httpclient::CallerHeaderPlacement::BeforeAnchor;
        profile.callerHeaderAnchor = "accept-language";

        UTF_CHECK_EQUAL(
            headersAsText( httpclient::Http1RequestSerializer::orderHeaders( caller, profile ) ),
            std::string( "Host=example.com;User-Agent=ProfileUA;Accept=*/*;X-Caller=1;Accept-Language=en;" )
            );

        /*
         * An anchor this request kind does not send falls back to appending, rather than dropping
         * the caller's header on the floor
         */

        profile.callerHeaderAnchor = "x-not-sent-for-this-kind";

        UTF_CHECK_EQUAL(
            headersAsText( httpclient::Http1RequestSerializer::orderHeaders( caller, profile ) ),
            std::string( "Host=example.com;User-Agent=ProfileUA;Accept=*/*;Accept-Language=en;X-Caller=1;" )
            );
    }

    /*
     * The rendered request, byte for byte
     */

    {
        http::HeaderList headers;

        headers.append( "Host", "example.com" );
        headers.append( "User-Agent", "ProfileUA" );
        headers.append( "Accept", "*/*" );

        UTF_CHECK_EQUAL(
            httpclient::Http1RequestSerializer::serialize( "GET", "/p?x=1", headers ),
            std::string(
                "GET /p?x=1 HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "User-Agent: ProfileUA\r\n"
                "Accept: */*\r\n"
                "\r\n"
                )
            );
    }

    /*
     * Request splitting: a target or a method carrying a space, a CR or an LF would end the
     * request line and start a second request inside the first. Refused, each spelling on its own
     */

    {
        http::HeaderList headers;

        headers.append( "Host", "example.com" );

        const char* badTargets[] =
        {
            "/p q", "/p\r\nX-Injected: 1", "/p\n", "/p\r", "", "/p\x7f", "/p\x01",
        };

        for( std::size_t i = 0U; i < 7U; ++i )
        {
            UTF_CHECK_THROW(
                httpclient::Http1RequestSerializer::serialize( "GET", badTargets[ i ], headers ),
                InvalidDataFormatException
                );
        }

        const char* badMethods[] = { "GET\r\nX: y", "GET ", "", "GE T", "GET\n" };

        for( std::size_t i = 0U; i < 5U; ++i )
        {
            UTF_CHECK_THROW(
                httpclient::Http1RequestSerializer::serialize( badMethods[ i ], "/p", headers ),
                InvalidDataFormatException
                );
        }
    }

    /*
     * An HTTP/1.1 request without a Host is malformed, and sending one is how a request ends up
     * routed somewhere nobody intended
     */

    {
        http::HeaderList noHost;

        noHost.append( "Accept", "*/*" );

        UTF_CHECK_THROW(
            httpclient::Http1RequestSerializer::serialize( "GET", "/p", noHost ),
            InvalidDataFormatException
            );

        /*
         * ... and the name is matched case-insensitively, so a profile spelling it 'host' is fine
         */

        http::HeaderList lowerHost;

        lowerHost.append( "host", "example.com" );

        UTF_CHECK_NO_THROW( httpclient::Http1RequestSerializer::serialize( "GET", "/p", lowerHost ) );
    }
}
