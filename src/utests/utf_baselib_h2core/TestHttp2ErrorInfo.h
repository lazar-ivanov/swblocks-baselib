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

#ifndef __UTEST_TESTHTTP2ERRORINFO_H_
#define __UTEST_TESTHTTP2ERRORINFO_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <exception>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2, ALPN and TLS error information added to core/ErrorHandling.h
 *
 * A smoke test, as the slice's acceptance describes it: the real coverage comes from the consumers
 * which attach these in anger. What is pinned here is that each one attaches and reads back, that
 * the two exceptions are reachable through the base every existing catch site already uses, and -
 * the failure this is really written for - that no two of them share a tag struct. Two typedefs
 * over one tag are the *same* type, so a copy-pasted tag would compile, would attach, and would
 * silently return the stream id when the GOAWAY last-stream-id was asked for
 */

namespace utest
{
    namespace http2errorinfo
    {
        /**
         * @brief Reads an errinfo which must be attached, and returns its value
         *
         * get_error_info returns a pointer which is null when the information is absent, so a
         * test which dereferenced it directly would crash rather than fail on a regression
         */

        template
        <
            typename INFO,
            typename EXCEPTION
        >
        auto requireErrorInfo( SAA_in const EXCEPTION& exception ) -> typename INFO::value_type
        {
            const auto* value = bl::eh::get_error_info< INFO >( exception );

            UTF_REQUIRE( value != nullptr );

            return *value;
        }

    } // http2errorinfo

} // utest

UTF_AUTO_TEST_CASE( Http2ErrorInfo_AttachAndReadTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::http2errorinfo;

    /*
     * A connection error, which is what Http2ProtocolException is for: everything the peer told
     * us in its GOAWAY, plus what the connection was negotiated as. The exception is caught as
     * bl::BaseException, the base every existing catch site in the library already names, so the
     * new types need no new catch site to be handled
     */

    bool caught = false;

    try
    {
        BL_THROW(
            Http2ProtocolException()
                << eh::errinfo_http2_error_code( Globals::ERROR_CODE_ENHANCE_YOUR_CALM )
                << eh::errinfo_http2_stream_id( 0U )
                << eh::errinfo_http2_goaway_last_stream_id( 4097U )
                << eh::errinfo_http2_debug_data( std::string( "h2-goaway-debug-payload" ) )
                << eh::errinfo_http2_is_retryable( true )
                << eh::errinfo_http_alpn_selected( std::string( "h2" ) )
                << eh::errinfo_tls_negotiated_cipher( std::string( "TLS_AES_128_GCM_SHA256" ) )
                << eh::errinfo_tls_negotiated_version( std::string( "TLSv1.3" ) ),
            BL_MSG()
                << "The peer has sent a GOAWAY"
            );
    }
    catch( BaseException& e )
    {
        caught = true;

        UTF_REQUIRE_EQUAL(
            requireErrorInfo< eh::errinfo_http2_error_code >( e ),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );

        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http2_stream_id >( e ), 0U );
        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http2_goaway_last_stream_id >( e ), 4097U );

        UTF_REQUIRE_EQUAL(
            requireErrorInfo< eh::errinfo_http2_debug_data >( e ),
            "h2-goaway-debug-payload"
            );

        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http2_is_retryable >( e ), true );
        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http_alpn_selected >( e ), "h2" );

        UTF_REQUIRE_EQUAL(
            requireErrorInfo< eh::errinfo_tls_negotiated_cipher >( e ),
            "TLS_AES_128_GCM_SHA256"
            );

        UTF_REQUIRE_EQUAL(
            requireErrorInfo< eh::errinfo_tls_negotiated_version >( e ),
            "TLSv1.3"
            );

        /*
         * The name which reaches a log and the server error document
         */

        UTF_REQUIRE_EQUAL( std::string( e.fullTypeName() ), "bl::Http2ProtocolException" );

        /*
         * And the values really do reach a diagnostic dump, which is the whole point of attaching
         * them rather than formatting them into the message
         */

        const auto dump = eh::diagnostic_information( e );

        UTF_REQUIRE( dump.find( "h2-goaway-debug-payload" ) != std::string::npos );
        UTF_REQUIRE( dump.find( "TLS_AES_128_GCM_SHA256" ) != std::string::npos );
    }

    UTF_REQUIRE( caught );

    /*
     * A stream error, which is what Http2StreamException is for. REFUSED_STREAM is the case D6
     * calls transparently retryable, since the peer has provably not processed the request
     */

    caught = false;

    try
    {
        BL_THROW(
            Http2StreamException()
                << eh::errinfo_http2_error_code( Globals::ERROR_CODE_REFUSED_STREAM )
                << eh::errinfo_http2_stream_id( 7U )
                << eh::errinfo_http2_is_retryable( true ),
            BL_MSG()
                << "The peer has refused the stream"
            );
    }
    catch( BaseException& e )
    {
        caught = true;

        UTF_REQUIRE_EQUAL(
            requireErrorInfo< eh::errinfo_http2_error_code >( e ),
            Globals::ERROR_CODE_REFUSED_STREAM
            );

        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http2_stream_id >( e ), 7U );
        UTF_REQUIRE_EQUAL( requireErrorInfo< eh::errinfo_http2_is_retryable >( e ), true );

        UTF_REQUIRE_EQUAL( std::string( e.fullTypeName() ), "bl::Http2StreamException" );

        /*
         * What was not attached reads back as absent rather than as a default value, so a
         * consumer can tell "no GOAWAY was involved" from "the GOAWAY named stream zero"
         */

        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_goaway_last_stream_id >( e ) == nullptr );
        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_debug_data >( e ) == nullptr );
    }

    UTF_REQUIRE( caught );
}

UTF_AUTO_TEST_CASE( Http2ErrorInfo_DistinctTagsTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * The three pairs which share a value type, and would therefore be indistinguishable if they
     * shared a tag struct. Each is attached alone and its sibling must read back absent
     */

    bool caught = false;

    try
    {
        BL_THROW(
            Http2ProtocolException()
                << eh::errinfo_http2_stream_id( 11U )
                << eh::errinfo_http2_debug_data( std::string( "debug" ) )
                << eh::errinfo_tls_negotiated_cipher( std::string( "cipher" ) ),
            BL_MSG()
                << "Only one of each pair is attached"
            );
    }
    catch( BaseException& e )
    {
        caught = true;

        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_stream_id >( e ) != nullptr );
        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_goaway_last_stream_id >( e ) == nullptr );

        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_debug_data >( e ) != nullptr );
        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http_alpn_selected >( e ) == nullptr );

        UTF_REQUIRE( eh::get_error_info< eh::errinfo_tls_negotiated_cipher >( e ) != nullptr );
        UTF_REQUIRE( eh::get_error_info< eh::errinfo_tls_negotiated_version >( e ) == nullptr );
    }

    UTF_REQUIRE( caught );

    /*
     * And the errinfo this slice did not touch behaves exactly as it did before it. An HTTP
     * status failure still uses HttpException with errinfo_http_status_code - design 3.7 keeps
     * that deliberately, so addExpectedHttpStatuses semantics and the existing catch sites carry
     * over - and none of the new information appears on it unless somebody attaches it
     */

    caught = false;

    try
    {
        BL_THROW(
            HttpException()
                << eh::errinfo_http_status_code( 503 )
                << eh::errinfo_http_url( std::string( "https://localhost/probe" ) ),
            BL_MSG()
                << "The server is unavailable"
            );
    }
    catch( BaseException& e )
    {
        caught = true;

        const auto* status = eh::get_error_info< eh::errinfo_http_status_code >( e );

        UTF_REQUIRE( status != nullptr );
        UTF_REQUIRE_EQUAL( *status, 503 );

        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http2_error_code >( e ) == nullptr );
        UTF_REQUIRE( eh::get_error_info< eh::errinfo_http_alpn_selected >( e ) == nullptr );

        UTF_REQUIRE_EQUAL( std::string( e.fullTypeName() ), "bl::HttpException" );
    }

    UTF_REQUIRE( caught );
}

#endif /* __UTEST_TESTHTTP2ERRORINFO_H_ */
