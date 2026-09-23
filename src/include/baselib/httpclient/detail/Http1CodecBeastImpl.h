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

#ifndef __BL_HTTPCLIENT_DETAIL_HTTP1CODECBEASTIMPL_H_
#define __BL_HTTPCLIENT_DETAIL_HTTP1CODECBEASTIMPL_H_

/*
 * The Boost.Beast backend of the HTTP/1.1 response parser - decision D15 and
 * notes/plans/http2-design.md 5.5
 *
 * This is the THIRD isolation layer of 5.5 seen from below, and the only file in the library which
 * names a bl::beast name. Everything above it - httpclient/Http1Codec.h and its callers - speaks
 * http::HeaderList, data::DataBlock, eh::error_code and plain scalars, which is what makes D15
 * reversible: an in-house backend is a sibling header here presenting the same two types,
 * Http1ParserEvents and Http1ResponseBackend, and nothing above this directory changes
 *
 * THE PUBLIC SURFACE OF THIS FILE CONTAINS NO BEAST TYPE. That is a property, not an aspiration:
 *
 *   - Http1ParserEvents, the sink the facade implements, passes text as ( const char*, size ) and
 *     lengths as std::uint64_t. Beast's string_view and boost::optional stop here
 *   - Http1ResponseBackend derives from bl::beast::http::basic_parser PRIVATELY, so not one
 *     inherited member leaks, and re-exports only wrappers in library terms
 *   - the four static predicates at the bottom are what lets the facade classify a refusal without
 *     naming Beast's error enumeration
 *
 * WHAT WAS MEASURED BEFORE THIS FILE WAS WRITTEN, and which an in-house backend would have to
 * match - see the S2.5 coverage probe recorded with the slice:
 *
 *   - chunked bodies with trailers, read-until-close, 204, 304 carrying a Content-Length, interim
 *     1xx and a HEAD response are all covered by basic_parser used sans-I/O
 *   - Beast itself refuses two Content-Length which disagree, Content-Length together with
 *     Transfer-Encoding: chunked in EITHER order, a duplicated Transfer-Encoding, bare-LF line
 *     endings, a NUL in a value, a space before a colon, a non-numeric length, a bad chunk size,
 *     a four-digit status, and a Content-Length arriving in the TRAILER section
 *   - Beast is more lenient than 5.5 requires in three places, and the facade closes all three.
 *     They are documented where they are closed, in Http1Codec.h
 */

#include <baselib/core/detail/BeastBoostImports.h>

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>

namespace bl
{
    namespace httpclient
    {
        namespace detail
        {
            /**
             * @brief The sink the HTTP/1.1 response backend delivers into
             *
             * Deliberately in the crudest possible terms - pointer and length rather than
             * std::string - for two reasons. It is the backend's own buffer being handed over,
             * so a sink which does not keep the bytes copies nothing; and a type which cannot
             * express a Beast concept cannot accidentally carry one
             *
             * Every callback takes an eh::error_code the sink may set, and the backend then
             * stops and reports it. That is how the facade's own checks - which Beast does not
             * make - become ordinary parse failures rather than exceptions thrown through the
             * middle of a parser
             */

            class Http1ParserEvents
            {
            public:

                virtual ~Http1ParserEvents() NOEXCEPT
                {
                }

                virtual void onStatusLine(
                    SAA_in          const int                       statusCode,
                    SAA_in          const char*                     reason,
                    SAA_in          const std::size_t               reasonSize,
                    SAA_in          const int                       httpVersion,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual void onField(
                    SAA_in          const char*                     name,
                    SAA_in          const std::size_t               nameSize,
                    SAA_in          const char*                     value,
                    SAA_in          const std::size_t               valueSize,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual void onTrailerField(
                    SAA_in          const char*                     name,
                    SAA_in          const std::size_t               nameSize,
                    SAA_in          const char*                     value,
                    SAA_in          const std::size_t               valueSize,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual void onFieldSectionComplete( SAA_inout eh::error_code& ec ) = 0;

                virtual void onBodyInit(
                    SAA_in          const bool                      hasContentLength,
                    SAA_in          const std::uint64_t             contentLength,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual std::size_t onBodyData(
                    SAA_in          const char*                     data,
                    SAA_in          const std::size_t               size,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual void onChunkHeader(
                    SAA_in          const std::uint64_t             chunkSize,
                    SAA_in          const char*                     extensions,
                    SAA_in          const std::size_t               extensionsSize,
                    SAA_inout       eh::error_code&                 ec
                    ) = 0;

                virtual void onMessageComplete( SAA_inout eh::error_code& ec ) = 0;
            };

            /**
             * @brief class Http1BeastResponseBackendT - basic_parser< false >, wrapped
             *
             * One instance parses ONE message. Beast's put() asserts the parser is not already
             * done and the class offers no public reset, so an interim 1xx - which IS a complete
             * message - is handled above by discarding this object and making another. The facade
             * holds it by pointer for exactly that reason
             */

            template
            <
                typename E = void
            >
            class Http1BeastResponseBackendT FINAL : private bl::beast::http::basic_parser< false >
            {
                BL_NO_COPY_OR_MOVE( Http1BeastResponseBackendT )

            private:

                typedef bl::beast::http::basic_parser< false >                  base_type;

                Http1ParserEvents&                                              m_events;

            public:

                Http1BeastResponseBackendT(
                    SAA_in          Http1ParserEvents&                          events,
                    SAA_in          const std::uint32_t                         headerLimit,
                    SAA_in          const std::uint64_t                         bodyLimit,
                    SAA_in          const bool                                  skipBody
                    )
                    :
                    m_events( events )
                {
                    /*
                     * eager( true ) is not an optimization, it is required. Without it put()
                     * returns as soon as the header is complete and leaves the body for a later
                     * call - measured by the S1.5 probe, which read an empty body until it was set
                     */

                    base_type::eager( true );

                    base_type::header_limit( headerLimit );
                    base_type::body_limit( bodyLimit );

                    if( skipBody )
                    {
                        /*
                         * The response to a HEAD carries the Content-Length of the body it would
                         * have had, and without this the parser waits forever for bytes which are
                         * never sent. base_type::skip() asserts nothing has been parsed yet, which
                         * is why this is a constructor argument and not a setter
                         */

                        base_type::skip( true );
                    }
                }

                std::size_t put(
                    SAA_in          const char*                                 data,
                    SAA_in          const std::size_t                           size,
                    SAA_inout       eh::error_code&                             ec
                    )
                {
                    if( base_type::is_done() )
                    {
                        return 0U;
                    }

                    return base_type::put( asio::const_buffer( data, size ), ec );
                }

                void putEof( SAA_inout eh::error_code& ec )
                {
                    if( base_type::is_done() )
                    {
                        return;
                    }

                    base_type::put_eof( ec );
                }

                /**
                 * @brief Whether this backend has been handed a single octet
                 *
                 * RE-EXPORTED BECAUSE putEof( ) MAY NOT BE CALLED WITHOUT ASKING IT FIRST.
                 * Beast's put_eof( ) opens with BOOST_ASSERT( got_some( ) ), and this library
                 * defines NDEBUG only in the release toolchain files - so a parser which has seen
                 * nothing aborts a debug build there, and in a release build falls past BOTH of
                 * put_eof( )'s guards, which test for the start_line and fields states and for
                 * the framing flags, and reports a COMPLETE message with no status line at all.
                 * The facade asks this before putEof( ) - see Http1ResponseParserT::parseEof( )
                 */

                bool gotSome() const NOEXCEPT
                {
                    return base_type::got_some();
                }

                bool isHeaderDone() const NOEXCEPT
                {
                    return base_type::is_header_done();
                }

                bool isDone() const NOEXCEPT
                {
                    return base_type::is_done();
                }

                bool isChunked() const NOEXCEPT
                {
                    return base_type::is_header_done() && base_type::chunked();
                }

                bool needEof() const NOEXCEPT
                {
                    return base_type::is_header_done() && base_type::need_eof();
                }

                /*************************************************************************
                 * Classifying a refusal without naming the backend's error enumeration
                 */

                static bool isNeedMore( SAA_in const eh::error_code& ec ) NOEXCEPT
                {
                    return ec == bl::beast::http::error::need_more;
                }

                /**
                 * @brief A refusal about message FRAMING - the smuggling-relevant class
                 */

                static bool isFramingError( SAA_in const eh::error_code& ec ) NOEXCEPT
                {
                    return
                        ec == bl::beast::http::error::multiple_content_length ||
                        ec == bl::beast::http::error::bad_content_length ||
                        ec == bl::beast::http::error::bad_transfer_encoding ||
                        ec == bl::beast::http::error::bad_chunk ||
                        ec == bl::beast::http::error::bad_chunk_extension;
                }

                static bool isHeaderLimitError( SAA_in const eh::error_code& ec ) NOEXCEPT
                {
                    return ec == bl::beast::http::error::header_limit;
                }

                static bool isBodyLimitError( SAA_in const eh::error_code& ec ) NOEXCEPT
                {
                    return ec == bl::beast::http::error::body_limit;
                }

            protected:

                /*************************************************************************
                 * basic_parser< false >'s ten virtuals. Every one of them is declared on the
                 * primary template, so the request-side callback must be overridden here too even
                 * though this parser can never receive one
                 */

                void on_request_impl(
                    bl::beast::http::verb,
                    bl::beast::string_view,
                    bl::beast::string_view,
                    int,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    /*
                     * Unreachable on basic_parser< false >; present because the virtual is
                     * declared unconditionally on the primary template
                     */

                    ec = eh::errc::make_error_code( eh::errc::protocol_error );
                }

                void on_response_impl(
                    const int                                                   statusCode,
                    bl::beast::string_view                                      reason,
                    const int                                                   httpVersion,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    m_events.onStatusLine( statusCode, reason.data(), reason.size(), httpVersion, ec );
                }

                void on_field_impl(
                    bl::beast::http::field,
                    bl::beast::string_view                                      name,
                    bl::beast::string_view                                      value,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    m_events.onField( name.data(), name.size(), value.data(), value.size(), ec );
                }

                void on_trailer_field_impl(
                    bl::beast::http::field,
                    bl::beast::string_view                                      name,
                    bl::beast::string_view                                      value,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    m_events.onTrailerField( name.data(), name.size(), value.data(), value.size(), ec );
                }

                void on_header_impl( eh::error_code& ec ) OVERRIDE
                {
                    m_events.onFieldSectionComplete( ec );
                }

                void on_body_init_impl(
                    const bl::beast::optional< std::uint64_t >&                 contentLength,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    m_events.onBodyInit(
                        !! contentLength,
                        contentLength ? *contentLength : 0U,
                        ec
                        );
                }

                std::size_t on_body_impl(
                    bl::beast::string_view                                      body,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    return m_events.onBodyData( body.data(), body.size(), ec );
                }

                void on_chunk_header_impl(
                    const std::uint64_t                                         chunkSize,
                    bl::beast::string_view                                      extensions,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    m_events.onChunkHeader( chunkSize, extensions.data(), extensions.size(), ec );
                }

                std::size_t on_chunk_body_impl(
                    const std::uint64_t,
                    bl::beast::string_view                                      body,
                    eh::error_code&                                             ec
                    ) OVERRIDE
                {
                    return m_events.onBodyData( body.data(), body.size(), ec );
                }

                void on_finish_impl( eh::error_code& ec ) OVERRIDE
                {
                    m_events.onMessageComplete( ec );
                }
            };

            typedef Http1BeastResponseBackendT<> Http1ResponseBackend;

        } // detail

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_DETAIL_HTTP1CODECBEASTIMPL_H_ */
