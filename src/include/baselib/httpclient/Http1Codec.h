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

#ifndef __BL_HTTPCLIENT_HTTP1CODEC_H_
#define __BL_HTTPCLIENT_HTTP1CODEC_H_

/*
 * The sans-I/O HTTP/1.1 codec - notes/plans/http2-design.md 5.5, decision D15
 *
 * A browser profile offers 'h2, http/1.1', so a server may answer http/1.1 - this library's own
 * HttpServer among them. This file is the facade the rest of the client speaks to, and it is
 * expressed entirely in the library's own terms: http::HeaderList, eh::error_code, std::string and
 * plain scalars. NO BEAST TYPE APPEARS IN ANY SIGNATURE HERE, which is the property that makes D15
 * reversible - see httpclient/detail/Http1CodecBeastImpl.h for the backend and for what the
 * isolation is worth
 *
 * SANS-I/O, AND IT DOES NOT THROW ON BAD INPUT. parse() takes bytes and returns how many it
 * consumed, setting an eh::error_code. A hostile response must not be able to unwind the stack of
 * a connection task through a parser, so every refusal below is a returned error and not an
 * exception. The request serializer is the other way round: its input is OUR OWN data, so a
 * caller-supplied header which would split the request is a programming error and does throw
 *
 *
 * THE SMUGGLING DEFENCES, AND WHERE EACH ONE LIVES
 *
 * Response splitting and request smuggling both come from one thing: two components reading the
 * same bytes two different ways. The rule this file follows throughout is the one S2.8 pinned for
 * a duplicated Location header - REFUSE RATHER THAN RESOLVE. Picking one of two readings is
 * precisely the differential an attacker needs
 *
 * The backend already refuses, measured rather than assumed (the S2.5 coverage probe):
 *
 *   - two Content-Length fields whose values differ
 *   - Content-Length together with Transfer-Encoding: chunked, in either order
 *   - a duplicated Transfer-Encoding
 *   - bare-LF line endings, a NUL in a value, a space before a colon, a non-token field name
 *   - a non-numeric, signed or hexadecimal Content-Length
 *   - a malformed chunk size, and a Content-Length arriving in the TRAILER section
 *
 * The facade adds these, because the backend accepts them and 5.5 does not:
 *
 *   1. OBSOLETE LINE FOLDING. The backend silently UNFOLDS - 'X-A: one\r\n  two' is delivered as
 *      'X-A: one two' - so a fold cannot be detected from the callbacks at all. The head is
 *      therefore scanned byte by byte as it is fed, and a CRLF followed by a space or a tab is a
 *      refusal. RFC 9112 section 5.2: a recipient of an obs-fold in a response MUST either reject
 *      the message or replace the fold with spaces before interpreting it; a fold is how a value
 *      is smuggled past a filter which looks at one line at a time
 *   2. TRANSFER-ENCODING WHICH IS NOT EXACTLY 'chunked'. The backend accepts
 *      'Transfer-Encoding: gzip' and, worse, 'Transfer-Encoding: chunked, gzip' - the second
 *      silently becoming a read-until-close body whose bytes are the raw chunk framing. It also
 *      accepts 'Transfer-Encoding: gzip' FOLLOWED BY a Content-Length while refusing the same two
 *      fields in the other order, and an asymmetry of that shape is itself a differential. Since
 *      D9 ships no transfer decoder there is nothing this client could do with another coding
 *      anyway, so the only accepted value is the single token 'chunked'
 *   3. A CONTENT-LENGTH WHICH IS NOT 1*DIGIT. The backend deliberately accepts the comma list
 *      form, treating 'Content-Length: 5, 5' as 5. RFC 9112 section 8.6 gives the field the
 *      grammar 1*DIGIT, so a list is invalid framing however consistent it is - and a list is
 *      what a proxy produces when it joins two separate Content-Length fields, which is the
 *      smuggling case with one layer of laundering on it
 *   4. A FORBIDDEN FIELD IN THE TRAILER SECTION. The backend refuses Content-Length and
 *      Transfer-Encoding there; RFC 9110 section 6.5.1 forbids a longer list, and a trailer which
 *      can set Location, Set-Cookie or Content-Type is a response-splitting primitive against any
 *      consumer which merges trailers into the header list
 *   5. A FIELD THE BACKEND ACCEPTS BUT http::HeaderList WOULD REJECT. HeaderList::append throws,
 *      and this parser does not throw, so the two validators are checked here and the field is
 *      refused as a parse error instead
 *
 * Two duplicated Content-Length fields with the SAME value are accepted, deliberately. RFC 9112
 * section 6.3 makes only differing values invalid framing, there is no second reading to pick
 * between, and refusing them would break responses that real servers emit. The case which pins it
 * sits next to the one for differing values so the boundary is visible
 *
 *
 * NO PART OF THIS CODEC FOLDS CASE OR TRIMS WHITESPACE THROUGH std::locale(). str::iequals,
 * str::to_lower_copy and str::trim_copy all take std::locale() and are therefore a global the
 * embedding process can change under a check which has already been written. Every fold and every
 * trim below is ASCII-only and spelled out here - http::HeaderList::equalsIgnoreCase, isOws and
 * trimOwsCopy, toLowerAsciiCopy - which is also the more correct rule, since a field name is a
 * token and OWS is SP and HTAB (RFC 9110 sections 5.1 and 5.6.3). The reasoning is the one at the
 * head of http::HeaderList, and 5.5 states it for the codec as a whole
 *
 *
 * THE 64 KB HEADER CAP IS SET HERE, NOT INHERITED. The backend's own default is 8 KB, which is
 * smaller than this design requires and would refuse ordinary responses; 5.5 asks for 64 KB, which
 * is what http::SimpleHttpTask's g_maxResponseHeadersSize already uses on the server side
 */

#include <baselib/httpclient/detail/Http1CodecBeastImpl.h>
#include <baselib/httpclient/HeaderProfile.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/Uri.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief Why the codec refused a response
         *
         * A named reason rather than only an eh::error_code, because every value below is a
         * security property somebody will want to assert on directly, and because the backend's
         * own error enumeration must not reach a caller - that is the whole point of the facade
         */

        enum class Http1CodecError : std::uint8_t
        {
            None,

            /**
             * The message is not well formed - a bad status line, a bad field name, a control
             * character in a value, bare-LF line endings, a truncated message at end of stream
             */

            MalformedMessage,

            /**
             * The message's FRAMING is invalid - the smuggling-relevant class. Two Content-Length
             * values which disagree, Content-Length together with a chunked Transfer-Encoding, a
             * duplicated Transfer-Encoding, a malformed chunk
             */

            InvalidFraming,

            /**
             * The status line and fields exceeded the cap
             */

            HeadersTooLarge,

            /**
             * The body exceeded the cap
             */

            BodyTooLarge,

            /**
             * A CRLF in the head was followed by a space or a tab - see the file note
             */

            ObsoleteLineFolding,

            /**
             * Content-Length and Transfer-Encoding arrived together, in either order
             */

            ConflictingFraming,

            /**
             * A Transfer-Encoding naming any coding other than the single token 'chunked'
             */

            UnsupportedTransferCoding,

            /**
             * A Content-Length whose value is not 1*DIGIT, the comma list form included
             */

            InvalidContentLength,

            /**
             * A field which RFC 9110 section 6.5.1 forbids in the trailer section
             */

            ForbiddenTrailerField,

            /**
             * A field the backend accepted which http::HeaderList will not carry
             */

            InvalidFieldSyntax,
        };

        /**
         * @brief The caps a response parser enforces
         */

        struct Http1ResponseLimits
        {
            enum : std::uint32_t
            {
                /**
                 * Design 5.5, matching http::SimpleHttpTask's g_maxResponseHeadersSize
                 */

                DEFAULT_MAX_HEADERS_SIZE            = 1U << 16,
            };

            enum : std::uint64_t
            {
                DEFAULT_MAX_BODY_SIZE               = 1ULL << 26,
            };

            cpp::ScalarTypeIniter< std::uint32_t >                              maxHeadersSize;
            cpp::ScalarTypeIniter< std::uint64_t >                              maxBodySize;

            Http1ResponseLimits() NOEXCEPT
            {
                maxHeadersSize = DEFAULT_MAX_HEADERS_SIZE;
                maxBodySize = DEFAULT_MAX_BODY_SIZE;
            }
        };

        /**
         * @brief An interim 1xx response which preceded the final one
         *
         * Kept rather than discarded because 103 Early Hints carries Link fields a client acts on,
         * and because a caller which never looks still gets the final response unchanged
         */

        struct Http1InterimResponse
        {
            cpp::ScalarTypeIniter< int >                                        statusCode;
            http::HeaderList                                                    headers;
        };

        /**
         * @brief class Http1ResponseParserT - the sans-I/O HTTP/1.1 response parser
         *
         * Feed it bytes with parse(); when the peer closes without framing the message, call
         * parseEof(). isComplete() says the final response has been fully parsed
         *
         * INTERIM RESPONSES ARE HANDLED HERE AND NOT BY THE CALLER. The backend parses one message
         * per instance and a 1xx IS a complete message to it, so a buffer holding '100 Continue'
         * followed by the real response is consumed only as far as the interim. This class notices
         * that, files the interim and continues on the same buffer, which is why parse() is a loop
         */

        template
        <
            typename E = void
        >
        class Http1ResponseParserT FINAL : private detail::Http1ParserEvents
        {
            BL_NO_COPY_OR_MOVE( Http1ResponseParserT )

        public:

            typedef Http1ResponseParserT< E >                                   this_type;

            /**
             * @brief Receives body bytes as they are parsed; returns how many it consumed
             *
             * Chunked and identity bodies both arrive here, already de-chunked
             */

            typedef cpp::function
                <
                    std::size_t ( SAA_in const char* data, SAA_in const std::size_t size )
                >
                body_callback_t;

        private:

            typedef detail::Http1ResponseBackend                                backend_t;

            const Http1ResponseLimits                                           m_limits;
            const bool                                                          m_isResponseToHead;

            cpp::SafeUniquePtr< backend_t >                                     m_backend;

            body_callback_t                                                     m_bodyCallback;

            cpp::ScalarTypeIniter< int >                                        m_statusCode;
            cpp::ScalarTypeIniter< int >                                        m_httpVersion;
            std::string                                                         m_reasonPhrase;

            http::HeaderList                                                    m_headers;
            http::HeaderList                                                    m_trailers;
            std::string                                                         m_body;

            std::vector< Http1InterimResponse >                                 m_interimResponses;

            cpp::ScalarTypeIniter< Http1CodecError >                            m_codecError;
            cpp::ScalarTypeIniter< bool >                                       m_isComplete;

            /*
             * The framing observed in the CURRENT field section, reset for every message
             */

            cpp::ScalarTypeIniter< std::size_t >                                m_contentLengthCount;
            cpp::ScalarTypeIniter< bool >                                       m_sawTransferEncoding;

            /**
             * @brief The obsolete-folding scanner - see the file note
             *
             * A plain value with no reference to anything, which is what makes the look-ahead
             * below possible: a COPY of it can be run forward over bytes the backend has not seen
             * yet, and thrown away, leaving the real one exactly where it was
             */

            struct ObsFoldScanner
            {
                cpp::ScalarTypeIniter< bool >                                   inHead;
                cpp::ScalarTypeIniter< bool >                                   atLineStart;
                cpp::ScalarTypeIniter< bool >                                   lastWasCr;
                cpp::ScalarTypeIniter< std::size_t >                            lineSize;

                ObsFoldScanner() NOEXCEPT
                {
                    inHead = true;
                }

                /**
                 * @brief Walks the bytes, halting at the blank line which ends the field section
                 *
                 * @return true when a CRLF in the head was followed by a space or a tab
                 */

                bool scan(
                    SAA_in          const char*                                 data,
                    SAA_in          const std::size_t                           size
                    ) NOEXCEPT
                {
                    for( std::size_t i = 0U; i < size && inHead.value(); ++i )
                    {
                        const auto ch = data[ i ];

                        if( atLineStart.value() )
                        {
                            atLineStart = false;

                            if( ' ' == ch || '\t' == ch )
                            {
                                return true;
                            }
                        }

                        if( '\n' == ch )
                        {
                            const bool isBlankLine =
                                0U == lineSize.value() ||
                                ( 1U == lineSize.value() && lastWasCr.value() );

                            if( isBlankLine )
                            {
                                /*
                                 * The blank line ends the field section; nothing after it is head
                                 */

                                inHead = false;

                                return false;
                            }

                            lineSize = 0U;
                            lastWasCr = false;
                            atLineStart = true;

                            continue;
                        }

                        lastWasCr = ( '\r' == ch );

                        lineSize = lineSize.value() + 1U;
                    }

                    return false;
                }
            };

            ObsFoldScanner                                                      m_foldScanner;

        public:

            Http1ResponseParserT()
                :
                m_isResponseToHead( false )
            {
                initState();
            }

            explicit Http1ResponseParserT(
                SAA_in          const Http1ResponseLimits&                      limits,
                SAA_in          const bool                                      isResponseToHead = false
                )
                :
                m_limits( limits ),
                m_isResponseToHead( isResponseToHead )
            {
                initState();
            }

            /**
             * @brief Streams the body out instead of buffering it into body()
             */

            void bodyCallback( SAA_in body_callback_t callback )
            {
                m_bodyCallback = BL_PARAM_FWD( callback );
            }

            /*************************************************************************
             * Feeding it
             */

            /**
             * @brief Parses what it can of the buffer, returning how many bytes it consumed
             *
             * THIS PARSER DOES NOT BUFFER. A short return with no error means either that the
             * message is incomplete and the rest of the buffer is an incomplete token the parser
             * needs more of, or that the message finished and what is left belongs to the next
             * response on this connection. In the first case the caller MUST present the
             * unconsumed bytes again, in front of the next ones it reads - a caller which advances
             * past them loses them, and the message never completes
             */

            std::size_t parse(
                SAA_in          const char*                                     data,
                SAA_in          const std::size_t                               size,
                SAA_inout       eh::error_code&                                 ec
                )
            {
                ec = eh::error_code();

                std::size_t consumed = 0U;

                if( Http1CodecError::None != m_codecError.value() )
                {
                    /*
                     * A refused message is refused for good. Feeding the backend again after it
                     * has reported an error is not defined, and a caller which ignores the first
                     * refusal must not be able to get a second, cleaner answer out of the same
                     * bytes - that is the differential all over again
                     */

                    ec = eh::errc::make_error_code( eh::errc::protocol_error );

                    return 0U;
                }

                while( consumed < size && ! m_isComplete )
                {
                    /*
                     * The fold check runs on a COPY of the scanner, ahead of the backend, so a
                     * folded head is refused BEFORE one byte of its body is handed to a body
                     * callback. Checking afterwards would leave a streaming consumer holding data
                     * from a message this parser has decided to reject
                     */

                    ObsFoldScanner lookAhead = m_foldScanner;

                    if( lookAhead.scan( data + consumed, size - consumed ) )
                    {
                        fail( Http1CodecError::ObsoleteLineFolding, ec );

                        return consumed;
                    }

                    const auto produced = m_backend -> put( data + consumed, size - consumed, ec );

                    /*
                     * ... and the real scanner then advances over the bytes the backend actually
                     * consumed, which is the only prefix this message is known to own
                     */

                    ( void ) m_foldScanner.scan( data + consumed, produced );

                    consumed += produced;

                    if( ec )
                    {
                        if( backend_t::isNeedMore( ec ) )
                        {
                            /*
                             * Not a refusal - the message simply continues in a later buffer
                             */

                            ec = eh::error_code();

                            break;
                        }

                        classifyBackendError( ec );

                        return consumed;
                    }

                    if( m_backend -> isDone() )
                    {
                        if( isInterimStatus( m_statusCode ) )
                        {
                            fileInterimAndRestart();

                            continue;
                        }

                        m_isComplete = true;

                        break;
                    }

                    if( 0U == produced )
                    {
                        break;
                    }
                }

                return consumed;
            }

            std::size_t parse(
                SAA_in          const std::string&                              data,
                SAA_inout       eh::error_code&                                 ec
                )
            {
                return parse( data.c_str(), data.size(), ec );
            }

            /**
             * @brief Tells the parser the peer closed the connection
             *
             * This is what completes a read-until-close body, and what turns a truncated message
             * into a refusal rather than a hang
             */

            void parseEof( SAA_inout eh::error_code& ec )
            {
                ec = eh::error_code();

                if( Http1CodecError::None != m_codecError.value() )
                {
                    ec = eh::errc::make_error_code( eh::errc::protocol_error );

                    return;
                }

                if( m_isComplete )
                {
                    return;
                }

                m_backend -> putEof( ec );

                if( ec )
                {
                    classifyBackendError( ec );

                    return;
                }

                if( m_backend -> isDone() && ! isInterimStatus( m_statusCode ) )
                {
                    m_isComplete = true;
                }
            }

            /*************************************************************************
             * What it parsed
             */

            bool isHeaderComplete() const NOEXCEPT
            {
                return m_backend -> isHeaderDone();
            }

            bool isComplete() const NOEXCEPT
            {
                return m_isComplete;
            }

            /**
             * @brief True when the message is framed by the connection closing
             */

            bool needsEof() const NOEXCEPT
            {
                return m_backend -> needEof();
            }

            bool isChunked() const NOEXCEPT
            {
                return m_backend -> isChunked();
            }

            int statusCode() const NOEXCEPT
            {
                return m_statusCode;
            }

            /**
             * @brief The HTTP minor version doubled - 10 for HTTP/1.0 and 11 for HTTP/1.1
             */

            int httpVersion() const NOEXCEPT
            {
                return m_httpVersion;
            }

            const std::string& reasonPhrase() const NOEXCEPT
            {
                return m_reasonPhrase;
            }

            const http::HeaderList& headers() const NOEXCEPT
            {
                return m_headers;
            }

            /**
             * @brief The trailer section of a chunked response; empty for every other framing
             *
             * Kept SEPARATE from headers() on purpose. Merging a trailer into the header list is
             * how a trailer becomes a header injection, and a caller which wants them merged can
             * do it knowing that is what it is doing
             */

            const http::HeaderList& trailers() const NOEXCEPT
            {
                return m_trailers;
            }

            /**
             * @brief The buffered body; empty when a bodyCallback() took it instead
             */

            const std::string& body() const NOEXCEPT
            {
                return m_body;
            }

            const std::vector< Http1InterimResponse >& interimResponses() const NOEXCEPT
            {
                return m_interimResponses;
            }

            Http1CodecError codecError() const NOEXCEPT
            {
                return m_codecError;
            }

        private:

            void initState()
            {
                m_codecError = Http1CodecError::None;

                resetPerMessageState();

                makeBackend();
            }

            void makeBackend()
            {
                m_backend = cpp::SafeUniquePtr< backend_t >::attach(
                    new backend_t(
                        *this,
                        m_limits.maxHeadersSize,
                        m_limits.maxBodySize,
                        m_isResponseToHead
                        )
                    );
            }

            void resetPerMessageState() NOEXCEPT
            {
                m_contentLengthCount = 0U;
                m_sawTransferEncoding = false;

                m_foldScanner = ObsFoldScanner();
            }

            static bool isInterimStatus( SAA_in const int statusCode ) NOEXCEPT
            {
                /*
                 * 101 is excluded: Switching Protocols is a FINAL response, and treating it as
                 * interim would have the parser look for another response after it
                 */

                return statusCode >= 100 && statusCode <= 199 && statusCode != 101;
            }

            void fileInterimAndRestart()
            {
                Http1InterimResponse interim;

                interim.statusCode = m_statusCode;
                interim.headers = m_headers;

                m_interimResponses.push_back( std::move( interim ) );

                m_headers.clear();
                m_reasonPhrase.clear();
                m_statusCode = 0;
                m_httpVersion = 0;

                resetPerMessageState();

                /*
                 * The backend parses one message and offers no reset, so the interim's parser is
                 * discarded and the final response gets a new one
                 */

                makeBackend();
            }

            void fail(
                SAA_in          const Http1CodecError                           error,
                SAA_inout       eh::error_code&                                 ec
                ) NOEXCEPT
            {
                if( Http1CodecError::None == m_codecError.value() )
                {
                    m_codecError = error;
                }

                ec = eh::errc::make_error_code( eh::errc::protocol_error );
            }

            void classifyBackendError( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                if( Http1CodecError::None != m_codecError.value() )
                {
                    /*
                     * One of this file's own checks already refused and set the reason; what the
                     * backend reports is that refusal travelling back out of it
                     */

                    return;
                }

                if( backend_t::isHeaderLimitError( ec ) )
                {
                    m_codecError = Http1CodecError::HeadersTooLarge;
                }
                else if( backend_t::isBodyLimitError( ec ) )
                {
                    m_codecError = Http1CodecError::BodyTooLarge;
                }
                else if( backend_t::isFramingError( ec ) )
                {
                    m_codecError = Http1CodecError::InvalidFraming;
                }
                else
                {
                    m_codecError = Http1CodecError::MalformedMessage;
                }
            }

            /*************************************************************************
             * The framing checks this file adds - see the file note
             */

            static bool isDigitsOnly( SAA_in const std::string& value ) NOEXCEPT
            {
                if( value.empty() )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    if( value[ i ] < '0' || value[ i ] > '9' )
                    {
                        return false;
                    }
                }

                return true;
            }

            /**
             * @brief RFC 9110 section 6.5.1 - what a trailer section may not carry
             */

            static bool isForbiddenTrailerField( SAA_in const std::string& name ) NOEXCEPT
            {
                static const char* g_forbidden[] =
                {
                    /*
                     * Message framing
                     */

                    "transfer-encoding", "content-length", "host",

                    /*
                     * Routing and authentication
                     */

                    "location", "authorization", "proxy-authorization", "www-authenticate",
                    "proxy-authenticate", "set-cookie", "cookie",

                    /*
                     * Request modifiers and response control data
                     */

                    "cache-control", "expect", "max-forwards", "pragma", "range", "te",
                    "age", "expires", "date", "retry-after", "vary",

                    /*
                     * Payload processing
                     */

                    "content-encoding", "content-type", "content-range", "trailer",
                };

                for( std::size_t i = 0U; i < ( sizeof( g_forbidden ) / sizeof( g_forbidden[ 0 ] ) ); ++i )
                {
                    if( http::HeaderList::equalsIgnoreCase( name, g_forbidden[ i ] ) )
                    {
                        return true;
                    }
                }

                return false;
            }

            /**
             * @brief RFC 9110 section 5.6.3 - OWS is SP and HTAB, and nothing else
             */

            static bool isOws( SAA_in const char ch ) NOEXCEPT
            {
                return ' ' == ch || '\t' == ch;
            }

            /**
             * @brief Strips leading and trailing OWS - and only OWS - from a field value
             *
             * DELIBERATELY NOT str::trim_copy, which is boost::algorithm::trim_copy and therefore
             * takes std::locale(): what counted as whitespace would be whatever global locale the
             * embedding process last installed, and the one caller below is the gate which decides
             * that a Transfer-Encoding is exactly 'chunked'. The perturbation runs toward
             * LENIENCY - in a locale whose ctype calls some other octet a space, 'chunked' followed
             * by that octet passes a gate the C locale refuses, and the backend then frames the
             * message its own way, which is the two-readings differential this file exists to
             * close. The case which pins it uses 'chunked,', because a comma is what a front end
             * leaves behind when it joins two Transfer-Encoding fields. A security check must not
             * depend on an embedder's std::locale::global
             *
             * This is the rule http::HeaderList states at the head of its own file, for the same
             * reason; the fold here is also the more correct one, since OWS is SP and HTAB
             */

            static std::string trimOwsCopy( SAA_in const std::string& value )
            {
                std::size_t begin = 0U;
                std::size_t end = value.size();

                while( begin < end && isOws( value[ begin ] ) )
                {
                    ++begin;
                }

                while( end > begin && isOws( value[ end - 1U ] ) )
                {
                    --end;
                }

                return value.substr( begin, end - begin );
            }

            bool chkFieldSyntax(
                SAA_in          const std::string&                              name,
                SAA_in          const std::string&                              value,
                SAA_inout       eh::error_code&                                 ec
                )
            {
                if(
                    ! http::HeaderList::isValidHeaderName( name ) ||
                    ! http::HeaderList::isValidHeaderValue( value )
                    )
                {
                    fail( Http1CodecError::InvalidFieldSyntax, ec );

                    return false;
                }

                return true;
            }

            /*************************************************************************
             * detail::Http1ParserEvents
             */

            void onStatusLine(
                SAA_in          const int                                       statusCode,
                SAA_in          const char*                                     reason,
                SAA_in          const std::size_t                               reasonSize,
                SAA_in          const int                                       httpVersion,
                SAA_inout       eh::error_code&
                ) OVERRIDE
            {
                m_statusCode = statusCode;
                m_httpVersion = httpVersion;
                m_reasonPhrase.assign( reason, reasonSize );
            }

            void onField(
                SAA_in          const char*                                     name,
                SAA_in          const std::size_t                               nameSize,
                SAA_in          const char*                                     value,
                SAA_in          const std::size_t                               valueSize,
                SAA_inout       eh::error_code&                                 ec
                ) OVERRIDE
            {
                std::string fieldName( name, nameSize );
                std::string fieldValue( value, valueSize );

                if( ! chkFieldSyntax( fieldName, fieldValue, ec ) )
                {
                    return;
                }

                if( http::HeaderList::equalsIgnoreCase( fieldName, "content-length" ) )
                {
                    if( ! isDigitsOnly( fieldValue ) )
                    {
                        fail( Http1CodecError::InvalidContentLength, ec );

                        return;
                    }

                    m_contentLengthCount = m_contentLengthCount + 1U;
                }
                else if( http::HeaderList::equalsIgnoreCase( fieldName, "transfer-encoding" ) )
                {
                    if( ! http::HeaderList::equalsIgnoreCase( trimOwsCopy( fieldValue ), "chunked" ) )
                    {
                        fail( Http1CodecError::UnsupportedTransferCoding, ec );

                        return;
                    }

                    m_sawTransferEncoding = true;
                }

                if( m_sawTransferEncoding && 0U != m_contentLengthCount.value() )
                {
                    /*
                     * RFC 9112 section 6.3. The backend catches this pair only when the coding is
                     * chunked, so the check is repeated here where the order of the two fields
                     * cannot change the answer
                     */

                    fail( Http1CodecError::ConflictingFraming, ec );

                    return;
                }

                m_headers.append( std::move( fieldName ), std::move( fieldValue ) );
            }

            void onTrailerField(
                SAA_in          const char*                                     name,
                SAA_in          const std::size_t                               nameSize,
                SAA_in          const char*                                     value,
                SAA_in          const std::size_t                               valueSize,
                SAA_inout       eh::error_code&                                 ec
                ) OVERRIDE
            {
                std::string fieldName( name, nameSize );
                std::string fieldValue( value, valueSize );

                if( ! chkFieldSyntax( fieldName, fieldValue, ec ) )
                {
                    return;
                }

                if( isForbiddenTrailerField( fieldName ) )
                {
                    fail( Http1CodecError::ForbiddenTrailerField, ec );

                    return;
                }

                m_trailers.append( std::move( fieldName ), std::move( fieldValue ) );
            }

            void onFieldSectionComplete( SAA_inout eh::error_code& ) OVERRIDE
            {
            }

            void onBodyInit(
                SAA_in          const bool,
                SAA_in          const std::uint64_t,
                SAA_inout       eh::error_code&
                ) OVERRIDE
            {
            }

            std::size_t onBodyData(
                SAA_in          const char*                                     data,
                SAA_in          const std::size_t                               size,
                SAA_inout       eh::error_code&
                ) OVERRIDE
            {
                if( m_bodyCallback )
                {
                    return m_bodyCallback( data, size );
                }

                m_body.append( data, size );

                return size;
            }

            void onChunkHeader(
                SAA_in          const std::uint64_t,
                SAA_in          const char*,
                SAA_in          const std::size_t,
                SAA_inout       eh::error_code&
                ) OVERRIDE
            {
            }

            void onMessageComplete( SAA_inout eh::error_code& ) OVERRIDE
            {
            }
        };

        typedef Http1ResponseParserT<> Http1ResponseParser;

        /**
         * @brief class Http1RequestSerializerT - the request side, in house
         *
         * In house and not the backend's, deliberately (design 5.5): the exact order and the exact
         * casing of the request headers ARE the fingerprint this library exists to reproduce, so
         * they have to be ours byte for byte. There is nothing subtle here for a parser library to
         * get right on our behalf - the whole of it is a request line, a list in the order it is
         * given, and a blank line
         *
         * It THROWS on bad input, unlike the parser. Its input is this process's own request, so a
         * method or a target carrying a CR is either a bug here or an injection through a caller's
         * API, and both are worth an exception rather than an error code somebody may not read
         */

        template
        <
            typename E = void
        >
        class Http1RequestSerializerT FINAL
        {
            BL_DECLARE_STATIC( Http1RequestSerializerT )

        public:

            /**
             * @brief The origin-form request target of a URI - RFC 9112 section 3.2.1
             *
             * The fragment is never sent, and an empty path is '/'
             */

            static std::string requestTarget( SAA_in const net::Uri& url )
            {
                std::string result = url.path();

                if( result.empty() )
                {
                    result = "/";
                }

                if( url.hasQuery() )
                {
                    result += "?";
                    result += url.query();
                }

                return result;
            }

            /**
             * @brief The Host header value - host[":" port], the default port omitted
             *
             * net::Uri::authority() renders host[":" port] with the port only when the reference
             * carried one, which is exactly the rule here: a browser sends 'example.com' and not
             * 'example.com:443'
             */

            static std::string hostHeaderValue( SAA_in const net::Uri& url )
            {
                return url.authority();
            }

            /**
             * @brief Puts the caller's headers into the profile's order, with the profile's casing
             *
             * The rules, which are the fingerprint (design 6.5):
             *
             *   - the profile's default headers come in the profile's order, and a caller who
             *     supplied one of those names changes its VALUE and not its POSITION
             *   - every other caller header is placed as callerHeaderPlacement says
             *   - each name is rendered through http1CaseMap, which maps the lower-case name to
             *     the casing this profile puts on the wire. HTTP/2 lower-cases every name
             *     (RFC 9113 section 8.2.1), so this table is consulted on this path only
             */

            static http::HeaderList orderHeaders(
                SAA_in          const http::HeaderList&                         callerHeaders,
                SAA_in          const HeaderProfileForKind&                     profile
                )
            {
                http::HeaderList defaults;
                http::HeaderList extras;

                for( std::size_t i = 0U; i < profile.defaultHeaders.size(); ++i )
                {
                    const auto& profileHeader = profile.defaultHeaders[ i ];

                    const auto* supplied = callerHeaders.tryGet( profileHeader.name );

                    defaults.append(
                        renderName( profileHeader.name, profile ),
                        supplied ? cpp::copy( *supplied ) : cpp::copy( profileHeader.value )
                        );
                }

                for( auto it = callerHeaders.begin(); it != callerHeaders.end(); ++it )
                {
                    if( defaults.has( it -> name() ) )
                    {
                        continue;
                    }

                    extras.append( renderName( it -> name(), profile ), cpp::copy( it -> value() ) );
                }

                return merge( defaults, extras, profile );
            }

            /**
             * @brief Renders the request line, the fields in the order given, and the blank line
             *
             * @throw InvalidDataFormatException when anything in it could split the request, or
             * when the mandatory Host field is missing
             */

            static std::string serialize(
                SAA_in          const std::string&                              method,
                SAA_in          const std::string&                              target,
                SAA_in          const http::HeaderList&                         headers
                )
            {
                chkToken( method, "method" );
                chkTarget( target );

                BL_CHK_T(
                    false,
                    headers.has( "host" ),
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "An HTTP/1.1 request must carry a Host header"
                    );

                std::string result;

                result += method;
                result += " ";
                result += target;
                result += " HTTP/1.1\r\n";

                for( auto it = headers.begin(); it != headers.end(); ++it )
                {
                    /*
                     * http::HeaderList validates on the way in, so a field which reached here is
                     * already free of CR, LF and NUL. It is checked again rather than trusted,
                     * because this is the one place where the bytes become a message and the cost
                     * of being wrong is a smuggled request
                     */

                    http::HeaderList::validateHeader( it -> name(), it -> value() );

                    result += it -> name();
                    result += ": ";
                    result += it -> value();
                    result += "\r\n";
                }

                result += "\r\n";

                return result;
            }

        private:

            /**
             * @brief The ASCII-only fold the case-map lookup uses - never str::to_lower_copy
             *
             * A field name is a token, so it is ASCII by construction, and http1CaseMap is keyed
             * on the lower-case spelling. str::to_lower_copy is boost::to_lower_copy and takes
             * std::locale(), so the key this lookup builds would be whatever global locale the
             * embedding process last installed - in a Turkish locale 'If-Modified-Since' lowers
             * to a key which is not in the table at all, and the request silently goes out with
             * the caller's casing instead of the profile's, which is a fingerprint the profile
             * exists to reproduce exactly. The rule is http::HeaderList's, see the head of that
             * file, and it is the rule the whole codec follows (design 5.5)
             */

            static std::string toLowerAsciiCopy( SAA_in const std::string& value )
            {
                std::string result = value;

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

            static std::string renderName(
                SAA_in          const std::string&                              name,
                SAA_in          const HeaderProfileForKind&                     profile
                )
            {
                const auto pos = profile.http1CaseMap.find( toLowerAsciiCopy( name ) );

                return pos == profile.http1CaseMap.end() ? cpp::copy( name ) : cpp::copy( pos -> second );
            }

            static http::HeaderList merge(
                SAA_in          const http::HeaderList&                         defaults,
                SAA_in          const http::HeaderList&                         extras,
                SAA_in          const HeaderProfileForKind&                     profile
                )
            {
                http::HeaderList result;

                const auto placement = profile.callerHeaderPlacement.value();

                if( CallerHeaderPlacement::Prepended == placement )
                {
                    appendAll( result, extras );
                    appendAll( result, defaults );

                    return result;
                }

                if(
                    CallerHeaderPlacement::BeforeAnchor == placement &&
                    defaults.has( profile.callerHeaderAnchor )
                    )
                {
                    for( auto it = defaults.begin(); it != defaults.end(); ++it )
                    {
                        if( http::HeaderList::equalsIgnoreCase( it -> name(), profile.callerHeaderAnchor ) )
                        {
                            appendAll( result, extras );
                        }

                        result.append( cpp::copy( it -> name() ), cpp::copy( it -> value() ) );
                    }

                    return result;
                }

                /*
                 * Appended, and also BeforeAnchor when this request kind does not send the anchor
                 */

                appendAll( result, defaults );
                appendAll( result, extras );

                return result;
            }

            static void appendAll(
                SAA_inout       http::HeaderList&                               target,
                SAA_in          const http::HeaderList&                         source
                )
            {
                for( auto it = source.begin(); it != source.end(); ++it )
                {
                    target.append( cpp::copy( it -> name() ), cpp::copy( it -> value() ) );
                }
            }

            static void chkToken(
                SAA_in          const std::string&                              value,
                SAA_in          const char*                                     what
                )
            {
                BL_CHK_T(
                    false,
                    http::HeaderList::isValidHeaderName( value ),
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "The HTTP request "
                        << what
                        << " is empty or carries a character which is not allowed in a token"
                    );
            }

            static void chkTarget( SAA_in const std::string& target )
            {
                BL_CHK_T(
                    true,
                    target.empty(),
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "The HTTP request target is empty"
                    );

                for( std::size_t i = 0U; i < target.size(); ++i )
                {
                    const auto ch = static_cast< unsigned char >( target[ i ] );

                    /*
                     * A space would end the target and start the version; a CR or an LF would end
                     * the line. Both are request splitting, so the whole CTL range plus space and
                     * DEL is refused rather than the three bytes which happen to be exploitable
                     */

                    BL_CHK_T(
                        false,
                        ch > 0x20U && ch < 0x7FU,
                        InvalidDataFormatException()
                            << eh::errinfo_is_user_friendly( true ),
                        BL_MSG()
                            << "The HTTP request target carries a character which is not allowed "
                            << "in a request target"
                        );
                }
            }
        };

        typedef Http1RequestSerializerT<> Http1RequestSerializer;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_HTTP1CODEC_H_ */
