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

#include <utests/baselib/HttpServerHelpers.h>

UTF_AUTO_TEST_CASE( BaseLib_StatusStringsTest )
{
    using namespace bl;

    typedef http::Parameters::HttpStatusCode        StatusCode;
    typedef http::StatusStrings                     StatusStrings;

    const auto statusUndefined = "HTTP/1.0 0 Status Undefined\r\n";
    const auto ok = "HTTP/1.0 200 OK\r\n";
    const auto created = "HTTP/1.0 201 Created\r\n";
    const auto accepted = "HTTP/1.0 202 Accepted\r\n";
    const auto noContent = "HTTP/1.0 204 No Content\r\n";
    const auto multipleChoices = "HTTP/1.0 300 Multiple Choices\r\n";
    const auto movedPermanently = "HTTP/1.0 301 Moved Permanently\r\n";
    const auto movedTemporarily = "HTTP/1.0 302 Moved Temporarily\r\n";
    const auto notModified = "HTTP/1.0 304 Not Modified\r\n";
    const auto endRange = "HTTP/1.0 399 End Range\r\n";
    const auto badRequest = "HTTP/1.0 400 Bad Request\r\n";
    const auto unauthorized = "HTTP/1.0 401 Unauthorized\r\n";
    const auto forbidden = "HTTP/1.0 403 Forbidden\r\n";
    const auto notFound = "HTTP/1.0 404 Not Found\r\n";
    const auto conflict = "HTTP/1.0 409 Conflict\r\n";
    const auto tooManyRequests = "HTTP/1.0 429 Too Many Requests\r\n";
    const auto internalError = "HTTP/1.0 500 Internal Server Error\r\n";
    const auto notImplemented = "HTTP/1.0 501 Not Implemented\r\n";
    const auto badGateway = "HTTP/1.0 502 Bad Gateway\r\n";
    const auto serviceUnavailable = "HTTP/1.0 503 Service Unavailable\r\n";
    const auto gatewayTimeout = "HTTP/1.0 504 Gateway Timeout\r\n";

    std::vector< std::pair< std::string, StatusCode > > stringToCode;

    stringToCode.emplace_back( statusUndefined, StatusCode::HTTP_STATUS_UNDEFINED );
    stringToCode.emplace_back( ok, StatusCode::HTTP_SUCCESS_OK );
    stringToCode.emplace_back( created, StatusCode::HTTP_SUCCESS_CREATED );
    stringToCode.emplace_back( accepted, StatusCode::HTTP_SUCCESS_ACCEPTED );
    stringToCode.emplace_back( noContent, StatusCode::HTTP_SUCCESS_NO_CONTENT );
    stringToCode.emplace_back( multipleChoices, StatusCode::HTTP_REDIRECT_MULTIPLE_CHOICES );
    stringToCode.emplace_back( movedPermanently, StatusCode::HTTP_REDIRECT_PERMANENTLY );
    stringToCode.emplace_back( movedTemporarily, StatusCode::HTTP_REDIRECT_TEMPORARILY );
    stringToCode.emplace_back( notModified, StatusCode::HTTP_REDIRECT_NOT_MODIFIED );
    stringToCode.emplace_back( endRange, StatusCode::HTTP_REDIRECT_END_RANGE );
    stringToCode.emplace_back( badRequest, StatusCode::HTTP_CLIENT_ERROR_BAD_REQUEST );
    stringToCode.emplace_back( unauthorized, StatusCode::HTTP_CLIENT_ERROR_UNAUTHORIZED );
    stringToCode.emplace_back( forbidden, StatusCode::HTTP_CLIENT_ERROR_FORBIDDEN );
    stringToCode.emplace_back( notFound, StatusCode::HTTP_CLIENT_ERROR_NOT_FOUND );
    stringToCode.emplace_back( conflict, StatusCode::HTTP_CLIENT_ERROR_CONFLICT );
    stringToCode.emplace_back( tooManyRequests, StatusCode::HTTP_CLIENT_ERROR_TOO_MANY_REQUESTS );
    stringToCode.emplace_back( internalError, StatusCode::HTTP_SERVER_ERROR_INTERNAL );
    stringToCode.emplace_back( notImplemented, StatusCode::HTTP_SERVER_ERROR_NOT_IMPLEMENTED );
    stringToCode.emplace_back( badGateway, StatusCode::HTTP_SERVER_ERROR_BAD_GATEWAY );
    stringToCode.emplace_back( serviceUnavailable, StatusCode::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE );
    stringToCode.emplace_back( gatewayTimeout, StatusCode::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT );

    UTF_REQUIRE_EQUAL( stringToCode.size(), 21U );

    for( const auto& pair : stringToCode )
    {
        UTF_REQUIRE_EQUAL( StatusStrings::get( pair.second ), pair.first );

        /*
         * getStatusLine() delegates to get() for every code which has a canonical reason
         * phrase - the pass-through half of its switch, which nothing else calls directly.
         * A new HttpStatusCode enumerator added to get() but not to that list would fall
         * through to the synthetic path below and be caught here
         */

        UTF_REQUIRE_EQUAL( StatusStrings::getStatusLine( pair.second ), pair.first );

        /*
         * The invariant which ties the line to the code it was asked for
         */

        UTF_REQUIRE(
            0U == pair.first.find(
                "HTTP/1.0 " +
                    utils::lexical_cast< std::string >( static_cast< unsigned int >( pair.second ) )
                )
            );
    }

    {
        /*
         * A code which is not in the table above is emitted as it is by getStatusLine(),
         * with the generic reason phrase for its class - the whole point of the method,
         * which exists so the status line, the recorded status and the error body can
         * never disagree
         */

        std::vector< std::pair< unsigned int, std::string > > unknownCodeToLine;

        unknownCodeToLine.emplace_back( 100U, "HTTP/1.0 100 Informational\r\n" );
        unknownCodeToLine.emplace_back( 207U, "HTTP/1.0 207 Success\r\n" );
        unknownCodeToLine.emplace_back( 308U, "HTTP/1.0 308 Redirection\r\n" );
        unknownCodeToLine.emplace_back( 418U, "HTTP/1.0 418 Client Error\r\n" );
        unknownCodeToLine.emplace_back( 599U, "HTTP/1.0 599 Server Error\r\n" );
        unknownCodeToLine.emplace_back( 999U, "HTTP/1.0 999 Unknown Status\r\n" );
        unknownCodeToLine.emplace_back( 42U, "HTTP/1.0 42 Unknown Status\r\n" );

        for( const auto& pair : unknownCodeToLine )
        {
            UTF_REQUIRE_EQUAL(
                StatusStrings::getStatusLine( static_cast< StatusCode >( pair.first ) ),
                pair.second
                );
        }

        /*
         * get(), unlike getStatusLine(), deliberately substitutes the 500 line for anything
         * it does not know - pinned here as documented behavior rather than left looking
         * like a bug, and paired with the assertion that getStatusLine() does not do it
         */

        UTF_REQUIRE_EQUAL(
            StatusStrings::get( static_cast< StatusCode >( 418U ) ),
            "HTTP/1.0 500 Internal Server Error\r\n"
            );

        UTF_REQUIRE(
            StatusStrings::getStatusLine( static_cast< StatusCode >( 418U ) ) !=
                StatusStrings::get( static_cast< StatusCode >( 418U ) )
            );

        /*
         * One call per class of the integer division which picks the phrase
         */

        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 150U ) ), "Informational" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 250U ) ), "Success" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 350U ) ), "Redirection" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 450U ) ), "Client Error" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 550U ) ), "Server Error" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 650U ) ), "Unknown Status" );
        UTF_REQUIRE_EQUAL( StatusStrings::genericReasonPhrase( static_cast< StatusCode >( 42U ) ), "Unknown Status" );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ResponseTest )
{
    using namespace bl;

    typedef http::Parameters::HttpStatusCode        StatusCode;
    typedef httpserver::Response                    Response;
    typedef http::Parameters::HttpHeader            HttpHeader;

    /*
     * A Content-Length which disagrees with the body which follows it is the one response
     * defect which makes clients hang or truncate instead of failing loudly, and it is
     * exactly what a reorder of m_content / m_headers would produce - m_content is declared
     * first precisely so getRequiredHeaders() sees the real body. The three quantities are
     * therefore checked against each other rather than against a literal
     */

    const auto requireFraming = []( SAA_in const om::ObjPtr< Response >& r ) -> void
    {
        const auto& serialized = r -> getSerialized();

        const auto pos = serialized.find( "\r\n\r\n" );

        UTF_REQUIRE( pos != std::string::npos );

        UTF_REQUIRE_EQUAL( serialized.size() - ( pos + 4U ), r -> content().size() );

        UTF_REQUIRE_EQUAL(
            r -> headers().at( HttpHeader::g_contentLength ),
            bl::utils::lexical_cast< std::string >( r -> content().size() )
            );
    };

    {
        const auto response = Response::createInstance( StatusCode::HTTP_SUCCESS_OK);

        const auto& headers = response -> headers();

        for( const auto& header : headers )
        {
            UTF_REQUIRE_EQUAL(
                header.first == HttpHeader::g_contentType ||
                header.first == HttpHeader::g_contentLength ||
                header.first == HttpHeader::g_connection,
                true
                );
        }

        UTF_REQUIRE( response -> getSerialized().size() );

        requireFraming( response );
    }

    {
        http::HeadersMap headersIn;

        headersIn.emplace( "Custom-header1", "value1" );
        headersIn.emplace( "Custom-header2", "value2" );

        const std::string content = "abcdefgh";

        const auto response = Response::createInstance(
            StatusCode::HTTP_SUCCESS_CREATED,
            bl::cpp::copy( content ),
            std::string(),
            std::move( headersIn )
            );

        const auto& headers = response -> headers();

        UTF_REQUIRE_EQUAL( headers.size(), 5U );

        bool invalidHeader = false;

        for( const auto& header : headers )
        {
            if( header.first == "Custom-header1" )
            {
                UTF_REQUIRE_EQUAL( header.second, "value1" );
            }
            else if( header.first == "Custom-header2" )
            {
                UTF_REQUIRE_EQUAL( header.second, "value2" );
            }
            else if( header.first == HttpHeader::g_contentType )
            {
                UTF_REQUIRE_EQUAL( header.second, HttpHeader::g_contentTypeDefault );
            }
            else if( header.first == HttpHeader::g_contentLength )
            {
                UTF_REQUIRE_EQUAL( header.second, bl::utils::lexical_cast< std::string >( content.size() ) );
            }
            else if( header.first == HttpHeader::g_connection )
            {
                UTF_REQUIRE_EQUAL( header.second, HttpHeader::g_close );
            }
            else
            {
                invalidHeader = true;

                break;
            }
        }

        UTF_REQUIRE_EQUAL( invalidHeader, false );

        const auto responseString = response -> getSerialized();

        UTF_REQUIRE( responseString.find( content ) != std::string::npos );

        const auto value1Pos = responseString.find( "value1" );
        const auto value2Pos = responseString.find( "value2" );
        const auto contentPos = responseString.find( content );
        const auto responseLength = responseString.length();

        UTF_REQUIRE( value1Pos != std::string::npos && value2Pos != std::string::npos && contentPos != std::string::npos );
        UTF_REQUIRE( contentPos >= 4U && contentPos > std::max( value1Pos , value2Pos ) );

        UTF_REQUIRE( ( value1Pos + std::strlen( "value1" ) + 2U ) <= responseLength );
        UTF_REQUIRE( ( value2Pos + std::strlen( "value2" ) + 2U ) <= responseLength );

        UTF_REQUIRE_EQUAL( responseString.substr( value1Pos + std::strlen( "value1" ), 2U ), "\r\n" );
        UTF_REQUIRE_EQUAL( responseString.substr( value2Pos + std::strlen( "value2" ), 2U ), "\r\n" );
        UTF_REQUIRE_EQUAL( responseString.substr( contentPos - 4U, 4U ), "\r\n\r\n" );

        requireFraming( response );
    }

    {
        /*
         * The framing must hold for the bodies which would break a naive implementation -
         * an empty one, one which itself carries the header terminator and one which
         * carries an embedded NUL (so nothing may compute the length with strlen)
         */

        requireFraming(
            Response::createInstance( StatusCode::HTTP_SUCCESS_OK, std::string() )
            );

        requireFraming(
            Response::createInstance( StatusCode::HTTP_SUCCESS_OK, std::string( "a\r\n\r\nb" ) )
            );

        requireFraming(
            Response::createInstance( StatusCode::HTTP_SUCCESS_OK, std::string( "a\0b", 3U ) )
            );
    }

    {
        /*
         * The stock response is the body every error the server generates on its own
         * carries, so its exact shape is a contract with every client
         */

        const auto response = Response::createInstance( StatusCode::HTTP_CLIENT_ERROR_NOT_FOUND );

        UTF_REQUIRE_EQUAL( response -> content(), "{\"statusCode\" : 404}" );
        UTF_REQUIRE_EQUAL( response -> status(), StatusCode::HTTP_CLIENT_ERROR_NOT_FOUND );
        UTF_REQUIRE( 0U == response -> getSerialized().find( "HTTP/1.0 404 Not Found\r\n" ) );
        UTF_REQUIRE_EQUAL( response -> headers().size(), 3U );

        requireFraming( response );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_RequestTest )
{
    using namespace bl;

    typedef http::Parameters::HttpHeader            HttpHeader;

    const std::string method = "POST";

    const std::string uri = utest::http::g_requestUri;

    const std::string header1 = "Host";
    const std::string value1 = "";

    const std::string header2 = "Authorization";
    const std::string value2 = "AUTHZ token=\"ABC1234567-x____8B\"";

    const std::string header3 = "Accept";
    const std::string value3 = HttpHeader::g_contentTypeDefault;

    const std::string header4 = "Connection";
    const std::string value4 = "keep-alive";

    http::HeadersMap headers;

    headers.emplace( bl::cpp::copy( header1 ), bl::cpp::copy( value1 ) );
    headers.emplace( bl::cpp::copy( header2 ), bl::cpp::copy( value2 ) );
    headers.emplace( bl::cpp::copy( header3 ), bl::cpp::copy( value3 ) );
    headers.emplace( bl::cpp::copy( header4 ), bl::cpp::copy( value4 ) );

    const std::string body = "abcdefghi kl noq124";

    const auto r = httpserver::Request::createInstance(
        bl::cpp::copy( method ),
        bl::cpp::copy( uri ),
        std::move( headers ),
        bl::cpp::copy( body )
        );

    UTF_REQUIRE_EQUAL( r -> method(), method );
    UTF_REQUIRE_EQUAL( r -> uri(), uri );
    UTF_REQUIRE_EQUAL( r -> body(), body );
    UTF_REQUIRE_EQUAL( r -> headers().size(), 4U );

    for( const auto& header : r -> headers() )
    {
        UTF_REQUIRE(
            header.first == header1 ||
            header.first == header2 ||
            header.first == header3 ||
            header.first == header4
            );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ParserHelpersTestMethodURIProtocol )
{
    using namespace bl;

    typedef http::Parameters::HttpHeader                HttpHeader;

    typedef httpserver::detail::ParserHelpers           ParserHelpers;
    typedef httpserver::detail::HttpParserResult        HttpParserResult;

    const std::string method = "GET";

    const std::string uri = utest::http::g_requestUri;

    {
        /*
         * Test the case when all elements of the first line are present in the buffer
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << method
            << HttpHeader::g_space
            << uri
            << HttpHeader::g_space
            << HttpHeader::g_httpVersion1_1;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        UTF_REQUIRE_EQUAL( request, context.m_buffer );

        const auto result = ParserHelpers::parseMethodURIVersion( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );
        UTF_REQUIRE_EQUAL( context.m_method, method );
        UTF_REQUIRE_EQUAL( context.m_uri, uri );
    }

    {
        /*
         * Test the case when the data is invalid
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << method
            << HttpHeader::g_space
            << uri
            << HttpHeader::g_space
            << "HTTP/a.1";

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseMethodURIVersion( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    /*
     * The remaining arms of parseMethodURIVersion are all shaped the same way - append the
     * request line to a fresh context and check the outcome - so they are driven from one
     * helper rather than from another six copies of the block above. The context stays with
     * the caller because the arms which are expected to parse also assert on what was stored
     */

    const auto parseLine = [](
        SAA_in      const std::string&                      line,
        SAA_in      const bool                              expectOk,
        SAA_inout   httpserver::detail::Context&            context
        )
        -> void
    {
        context.m_buffer.append( line.c_str(), line.c_str() + line.size() );

        const auto result = ParserHelpers::parseMethodURIVersion( context.m_buffer, context );

        if( expectOk )
        {
            UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
            UTF_REQUIRE( result.second == nullptr );
        }
        else
        {
            UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
            UTF_REQUIRE( result.second != nullptr );
        }
    };

    {
        /*
         * A URI of exactly MAX_URI_SIZE characters is accepted - the guard rejects
         * '> MAX_URI_SIZE', so this is the boundary an off-by-one would move. The cap is
         * the only bound on the URI specifically; the request line as a whole is bounded
         * only by the 64 KB header cap of the parser
         */

        httpserver::detail::Context context;

        parseLine(
            "GET /" + std::string( ParserHelpers::MAX_URI_SIZE - 1U, 'a' ) + " HTTP/1.1",
            true                                            /* expectOk */,
            context
            );

        UTF_REQUIRE_EQUAL( context.m_uri.size(), static_cast< std::size_t >( ParserHelpers::MAX_URI_SIZE ) );
    }

    {
        httpserver::detail::Context context;

        parseLine(
            "GET /" + std::string( ParserHelpers::MAX_URI_SIZE, 'a' ) + " HTTP/1.1",
            false                                           /* expectOk */,
            context
            );
    }

    {
        /*
         * A control character in the URI must be rejected - it is what would otherwise
         * reach Request::uri() and be routed on by a backend
         */

        httpserver::detail::Context context;

        parseLine( std::string( "GET /pa\x01th HTTP/1.1" ), false /* expectOk */, context );
    }

    {
        /*
         * The method must be a valid token, so neither a tspecial nor a CTL may appear in
         * it - this is what stops a request line built from binary garbage from becoming a
         * Request::method()
         */

        httpserver::detail::Context context;

        parseLine( "GE:T /path HTTP/1.1", false /* expectOk */, context );
    }

    {
        httpserver::detail::Context context;

        parseLine( std::string( "GE\tT /path HTTP/1.1" ), false /* expectOk */, context );
    }

    {
        /*
         * The request line must split into exactly three non-empty elements; the split does
         * not compress adjacent separators, so a leading space yields an empty first element
         */

        httpserver::detail::Context context;

        parseLine( "GET /path", false /* expectOk */, context );
    }

    {
        httpserver::detail::Context context;

        parseLine( "GET /pa th HTTP/1.0", false /* expectOk */, context );
    }

    {
        httpserver::detail::Context context;

        parseLine( " GET /path HTTP/1.0", false /* expectOk */, context );
    }

    {
        /*
         * HTTP/1.1 is already covered by the first sub-block of this case, which builds its
         * valid line from HttpHeader::g_httpVersion1_1 - only the 1.0 row is added here
         */

        httpserver::detail::Context context;

        parseLine( "GET /path HTTP/1.0", true /* expectOk */, context );

        UTF_REQUIRE_EQUAL( context.m_method, "GET" );
        UTF_REQUIRE_EQUAL( context.m_uri, "/path" );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ParserHelpersParseHeader )
{
    using namespace bl;

    typedef http::Parameters::HttpHeader                HttpHeader;

    typedef httpserver::detail::ParserHelpers           ParserHelpers;
    typedef httpserver::detail::HttpParserResult        HttpParserResult;

    const std::string headerName    = "Authorization";
    const std::string value         = "AUTHZ token=\"ABC1234567-x____8B\"";

    /*
     * The parser normalizes the header names to lower case
     */

    const std::string normalizedName = bl::str::to_lower_copy( headerName );

    {
        /*
         * Test with a valid header (a space after the name-value separator)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << headerName
            << HttpHeader::g_nameSeparator
            << HttpHeader::g_space
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );
        UTF_REQUIRE_EQUAL( context.m_headers.size(), 1U );
        UTF_REQUIRE_EQUAL( context.m_headers.find( normalizedName ) -> second, value );
    }

    {
        /*
         * Test with a valid header (no space after the name-value separator)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << headerName
            << HttpHeader::g_nameSeparator
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );
        UTF_REQUIRE_EQUAL( context.m_headers.size(), 1U );
        UTF_REQUIRE_EQUAL( context.m_headers.find( normalizedName ) -> second, value );
    }

    {
        /*
         * Test duplicated header names
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << headerName
            << HttpHeader::g_nameSeparator
            << HttpHeader::g_space
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_headers[ normalizedName ] = value;

        UTF_REQUIRE_EQUAL( context.m_headers.size(), 1U );
        UTF_REQUIRE_EQUAL( context.m_headers.find( normalizedName ) -> second, value );

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test invalid header (no name value separator)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << headerName
            << HttpHeader::g_space
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test invalid header (spaces instead of a value)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << headerName
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test with an invalid header (no header name)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << HttpHeader::g_nameSeparator
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test with an invalid header (spaces instead of a header name)
         */

        bl::cpp::SafeOutputStringStream oss;

        oss
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_space
            << HttpHeader::g_nameSeparator
            << value;

        const auto request = oss.str();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        httpserver::detail::Context context;

        context.m_buffer.append( begin, end );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * HTAB is the only control character isValidValue() lets through, and it does so by
         * design (RFC 7230 section 3.2) - the carve-out reads like an oversight, so deleting
         * the 'continue' would look like a tightening while it silently started rejecting
         * legitimate headers. Only the leading white space is trimmed; the interior HTABs
         * are part of the value and must survive
         */

        const std::string request( "X-Tabbed:\tvalue\twith\ttabs" );

        httpserver::detail::Context context;

        context.m_buffer.append( request.c_str(), request.c_str() + request.length() );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        const auto pos = context.m_headers.find( "x-tabbed" );

        UTF_REQUIRE( pos != context.m_headers.end() );
        UTF_REQUIRE_EQUAL( pos -> second, "value\twith\ttabs" );
        UTF_REQUIRE( pos -> second.find( '\t' ) != std::string::npos );
    }

    {
        /*
         * isChar() tests 'ch >= 0' and char is signed on the supported platforms, so a byte
         * in the 0x80-0xFF range arrives as a negative int and is rejected. A change of the
         * parameter or of the comparison to unsigned char would silently start accepting
         * high bytes into both the request and the response header values
         */

        const std::string request = std::string( "X-High: caf" ) + static_cast< char >( 0xE9 );

        httpserver::detail::Context context;

        context.m_buffer.append( request.c_str(), request.c_str() + request.length() );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * The negative control for the carve-out above - a control character which is not
         * HTAB is still rejected
         */

        const std::string request( "X-Ctl: a\x01\x62" );

        httpserver::detail::Context context;

        context.m_buffer.append( request.c_str(), request.c_str() + request.length() );

        const auto result = ParserHelpers::parseHeader( context.m_buffer, context );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ParserHeaderStrictnessTest )
{
    using namespace bl;

    typedef httpserver::Parser                          Parser;

    typedef httpserver::detail::HttpParserResult        HttpParserResult;

    const auto parseRequest = []( SAA_in const std::string& request ) -> httpserver::detail::ServerResult
    {
        const auto parser = Parser::createInstance();

        const auto* begin = request.c_str();

        return parser -> parse( begin, begin + request.length() );
    };

    {
        /*
         * Two Content-Length headers which differ only in case must be rejected - which
         * of the two would win would otherwise depend on the iteration order of the map
         */

        const auto result = parseRequest(
            "POST /path HTTP/1.0\r\nContent-Length: 5\r\ncontent-length: 100\r\n\r\nabcde"
            );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Chunked transfer encoding is not implemented and must not be ignored
         */

        const auto result = parseRequest(
            "POST /path HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n"
            );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * White space before the header name or before the colon must be rejected
         */

        const auto result = parseRequest(
            "POST /path HTTP/1.0\r\n Content-Length: 0\r\n\r\n"
            );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        const auto result = parseRequest(
            "POST /path HTTP/1.0\r\nContent-Length : 0\r\n\r\n"
            );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * A control character in a header value must be rejected - an LF tolerant
         * intermediary would read it as two headers
         */

        const auto result = parseRequest(
            "POST /path HTTP/1.0\r\nX-Custom: a\rContent-Length: 10\r\n\r\n"
            );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * An empty header value is legal and the header names are normalized to lower case
         */

        const auto parser = Parser::createInstance();

        const std::string request = "GET /path HTTP/1.0\r\nX-Empty:\r\nX-Upper-Case: value\r\n\r\n";

        const auto* begin = request.c_str();

        const auto result = parser -> parse( begin, begin + request.length() );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        const auto httpRequest = parser -> buildRequest();

        const auto& headers = httpRequest -> headers();

        const auto posEmpty = headers.find( "x-empty" );

        UTF_REQUIRE( posEmpty != headers.end() );
        UTF_REQUIRE( posEmpty -> second.empty() );

        const auto posUpperCase = headers.find( "x-upper-case" );

        UTF_REQUIRE( posUpperCase != headers.end() );
        UTF_REQUIRE_EQUAL( posUpperCase -> second, "value" );
    }

    /*
     * While the header sentinel has not been seen the parser re-checks the very first byte
     * of the connection on every call and rejects immediately if it is not a valid token
     * character. That is what stops a stray TLS ClientHello on the plain HTTP port, or any
     * other binary protocol, from being buffered up to g_maxHeadersSize (64 KB per
     * connection) before being rejected, and it also refuses a leading space and a leading
     * CR - both request smuggling primitives against a lenient intermediary
     *
     * Note the incoming chunk is appended to the buffer before the check runs, so what the
     * guard bounds is accumulation across calls, not the first append
     */

    {
        /*
         * A TLS record header - the first byte 0x16 is a control character. The length is
         * explicit because the record carries an embedded NUL
         */

        const auto result = parseRequest( std::string( "\x16\x03\x01\x00\x2f", 5U ) );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        const auto result = parseRequest( " GET /path HTTP/1.0\r\n" );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        const auto result = parseRequest( "\r\nGET /path HTTP/1.0\r\n" );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * The negative control - a valid prefix must still ask for more data, otherwise the
         * assertions above would pass against a parser which rejected everything
         */

        const auto result = parseRequest( "GET /pa" );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );
    }

    {
        /*
         * The parser does not latch an error state, so each call re-evaluates the first byte
         * and a connection which opened with a non HTTP byte can never progress past it -
         * feeding a perfectly valid request line as the second chunk changes nothing
         */

        const auto parser = Parser::createInstance();

        const std::string first( "\x16\x03\x01\x00\x2f", 5U );

        const auto* begin = first.c_str();

        const auto firstResult = parser -> parse( begin, begin + first.length() );

        UTF_REQUIRE_EQUAL( firstResult.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( firstResult.second != nullptr );

        const std::string second( "GET /path HTTP/1.0\r\n" );

        begin = second.c_str();

        const auto secondResult = parser -> parse( begin, begin + second.length() );

        UTF_REQUIRE_EQUAL( secondResult.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( secondResult.second != nullptr );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ResponseHeaderValidationTest )
{
    using namespace bl;

    typedef http::Parameters::HttpStatusCode        StatusCode;
    typedef httpserver::Response                    Response;

    const auto createResponse = []( SAA_in http::HeadersMap&& customHeaders ) -> void
    {
        ( void ) Response::createInstance(
            StatusCode::HTTP_SUCCESS_OK,
            std::string( "content" ),
            std::string(),
            std::move( customHeaders )
            );
    };

    {
        /*
         * A header value which carries CRLF would split the response
         */

        http::HeadersMap headers;

        headers.emplace( "X-Custom", "value\r\nSet-Cookie: injected=1" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        /*
         * A framing header in a different case would be emitted next to the generated one
         */

        http::HeadersMap headers;

        headers.emplace( "content-length", "1000" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        http::HeadersMap headers;

        headers.emplace( "CONNECTION", "keep-alive" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        /*
         * Transfer-Encoding is the response counterpart of the parser's rejection of it -
         * emitting one next to the generated Content-Length is a response desync vector
         */

        http::HeadersMap headers;

        headers.emplace( "Transfer-Encoding", "chunked" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        http::HeadersMap headers;

        headers.emplace( "CONTENT-TYPE", "text/plain" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        /*
         * An invalid header name must be rejected as well
         */

        http::HeadersMap headers;

        headers.emplace( "X Custom", "value" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        /*
         * A high byte must be rejected on the response path too - chkCustomHeader and the
         * request side parseHeader share isValidValue(), and asserting both directions is
         * what pins that they cannot diverge
         */

        http::HeadersMap headers;

        headers.emplace( "X-High", std::string( "caf" ) + static_cast< char >( 0xE9 ) );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
    }

    {
        /*
         * ... while the HTAB carve-out is accepted on the response path as well and the tab
         * survives into the bytes which go on the wire. A test which asserted only the
         * rejection half would still let the '\t' 'continue' in isValidValue be deleted
         */

        http::HeadersMap headers;

        headers.emplace( "X-Tabbed", "a\tb" );

        UTF_REQUIRE_NO_THROW( createResponse( std::move( headers ) ) );

        http::HeadersMap headersAccepted;

        headersAccepted.emplace( "X-Tabbed", "a\tb" );

        const auto response = Response::createInstance(
            StatusCode::HTTP_SUCCESS_OK,
            std::string( "content" ),
            std::string(),
            std::move( headersAccepted )
            );

        UTF_REQUIRE( response -> getSerialized().find( "X-Tabbed: a\tb\r\n" ) != std::string::npos );
    }

    {
        /*
         * A status code which has no canonical reason phrase must still be emitted as is
         */

        const auto response = Response::createInstance(
            static_cast< StatusCode >( 418U ),
            std::string( "content" )
            );

        UTF_REQUIRE( 0U == response -> getSerialized().find( "HTTP/1.0 418 Client Error\r\n" ) );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerStdErrorResponseRedactionTest )
{
    using namespace bl;
    using namespace utest::http;

    /*
     * Every 400 / 500 / 504 the server emits is built by getStdErrorResponse and it goes out
     * on the wire to a client which may be untrusted, so what it carries must be the redacted
     * server error document - the source locations, the task information, the host and the
     * service names and the addresses of the server side endpoints must not leak
     *
     * Each "must not be there" assertion is paired with the same lookup against the unredacted
     * document, so that a needle which simply stopped being emitted at all can't make the
     * negative half of the pair pass vacuously
     */

    const auto eptr = std::make_exception_ptr(
        BL_EXCEPTION( HttpServerException(), "Bad client request" )
            << eh::errinfo_file_name        ( "/secret/path/Foo.cpp" )
            << eh::errinfo_function_name    ( "secretFunction" )
            << eh::errinfo_task_info        ( "secret task info" )
            << eh::errinfo_host_name        ( "internal-host-name" )
            << eh::errinfo_service_name     ( "internal-service" )
            << eh::errinfo_endpoint_address ( "10.1.2.3" )
            << eh::errinfo_endpoint_port    ( 65001 )
        );

    const auto backend =
        ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >();

    const auto response =
        backend -> getStdErrorResponse( http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST, eptr );

    const auto full = dm::ServerErrorHelpers::getServerErrorAsJson( eptr );

    const auto& content = response -> content();

    UTF_REQUIRE_EQUAL( response -> status(), http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST );
    UTF_REQUIRE( 0U == response -> getSerialized().find( "HTTP/1.0 400 Bad Request\r\n" ) );

    const char* const secrets[] =
    {
        "/secret/path/Foo.cpp",
        "secretFunction",
        "secret task info",
        "internal-host-name",
        "internal-service",
        "10.1.2.3",
        "65001",
    };

    for( const char* const secret : secrets )
    {
        UTF_REQUIRE( content.find( secret ) == std::string::npos );
        UTF_REQUIRE( full.find( secret ) != std::string::npos );
    }

    UTF_REQUIRE( content.find( "<redacted>" ) != std::string::npos );
    UTF_REQUIRE( content.find( "\"endpointPort\"" ) == std::string::npos );

    /*
     * The raw exception text goes the same way as the source locations - BL_MSG() text in this
     * library routinely carries paths - and the client is told what went wrong through the
     * friendly message the model computes instead
     */

    UTF_REQUIRE( content.find( "Bad client request" ) == std::string::npos );
    UTF_REQUIRE( full.find( "Bad client request" ) != std::string::npos );

    UTF_REQUIRE( content.find( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG ) != std::string::npos );

    UTF_REQUIRE_EQUAL(
        response -> headers().at( http::HttpHeader::g_contentType ),
        http::HttpHeader::g_contentTypeJsonUtf8
        );

    UTF_REQUIRE_EQUAL(
        response -> headers().at( http::HttpHeader::g_contentLength ),
        utils::lexical_cast< std::string >( content.size() )
        );

    /*
     * The user-friendly positive control: an error which was raised to be shown to the caller
     * keeps its text, so the redaction above cannot be satisfied by an implementation which
     * simply blanks every message
     */

    {
        const auto friendlyEptr = std::make_exception_ptr(
            BL_EXCEPTION( UserMessageException(), "The request payload is too large" )
            );

        const auto friendlyResponse = backend -> getStdErrorResponse(
            http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST,
            friendlyEptr
            );

        UTF_REQUIRE(
            friendlyResponse -> content().find( "The request payload is too large" ) != std::string::npos
            );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ParserTest )
{
    using namespace bl;

    typedef httpserver::Parser                          Parser;

    typedef httpserver::detail::HttpParserResult        HttpParserResult;

    {
        /*
         * Test the case when all elements of the first line are present in the buffer
         */

        const std::string firstLine = "PUT sign-request HTTP/1.0\r\n";

        const auto buffer = firstLine.c_str();

        const char* begin = buffer;
        const char* end = buffer + firstLine.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );
    }

    {
        /*
         * Test the case when not all elements of the first line are present in the buffer
         */

        const std::string firstLine = "PUT sign-request HTTP/1.";

        const auto buffer = firstLine.c_str();

        const char* begin = buffer;
        const char* end = buffer + firstLine.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );

        const std::string firstLineCompliment = "1\r\n";

        const char* beginCompliment = firstLineCompliment.c_str();
        const char* endCompliment = beginCompliment + firstLineCompliment.length();

        const auto nextResult = parser -> parse( beginCompliment, endCompliment );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );
    }

    {
        /*
         * Test the case when CRLFs are the only elements to parse
         */

        const std::string firstLine = "\r\n\r\n";

        const auto buffer = firstLine.c_str();

        const char* begin = buffer;
        const char* end = buffer + firstLine.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when there is one valid header but no content
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n"
            "Heade1: value1\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );
        UTF_REQUIRE( parser -> buildRequest() != nullptr );
    }

    {
        /*
         * Test the case when the header comes in parts
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n"
            "Heade1: value1\r";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );

        UTF_REQUIRE_THROW( parser -> buildRequest(), bl::UnexpectedException );

        const std::string request2 = "\n\r\n";

        const auto buffer2 = request2.c_str();

        const char* begin2 = buffer2;
        const char* end2 = buffer2 + request2.length();

        const auto result2 = parser -> parse( begin2, end2 );

        UTF_REQUIRE_EQUAL( result2.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result2.second == nullptr );
    }

    {
        /*
         * Test the case when the first header is present and there is no content
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );
    }

    {
        /*
         * Test the case when the content related headers are provided and define content with length == 0
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n"
            "Content-Type: text/xml\r\n"
            "Content-Length: 0\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        UTF_REQUIRE_EQUAL(
            ( parser -> parse( begin, end ) ).first,
            HttpParserResult::PARSING_ERROR );
    }

    {
        /*
         * Test the case when the content related headers are provided but Content-Length is invalid
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n"
            "Content-Type: text/xml\r\n"
            "Content-Length: 10d\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when only one of the the content related headers is provided
         */

        const std::string request =
            "PUT sign-request HTTP/1.0\r\n"
            "Content-Length: text/xml\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when the parser is called with too large headers
         */

        const std::string request = "PUT sign-request HTTP/1.0\r\n";

        auto bytesCount = request.length();

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + bytesCount;

        const auto parser = Parser::createInstance();

        auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );

        for( std::size_t i = 0U; result.first != HttpParserResult::PARSING_ERROR; ++i )
        {
            if( bytesCount > httpserver::Parser::g_maxHeadersSize )
            {
                break;
            }

            bl::cpp::SafeOutputStringStream oss;

            oss
                << "Header_"
                << i
                << ": "
                << i
                << "\r\n";

            const auto header = oss.str();

            const auto length = header.length();

            const auto headerBuffer = header.c_str();

            const char* beginHeader = headerBuffer;
            const char* endHeader = headerBuffer + length;

            bytesCount += length;

            result = parser -> parse( beginHeader, endHeader );
        }

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when the content is larger than the supported Content size
         */

        const std::string request =
            std::string( "PUT sign-request HTTP/1.0\r\n" ) +
            std::string( "Content-Length: " ) +
            std::to_string( Parser::g_maxContentSize + 1 ) +
            std::string( "\r\n\r\n" );

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );

        result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when the parser is called with an invalid buffer
         */

        const std::string request = "PUT sign-request HTTP/1.0\r\n\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer;

        const auto parser = Parser::createInstance();

        UTF_REQUIRE_THROW( parser -> parse( begin, end ), bl::UnexpectedException );
    }

    {
        /*
         * Test the case when the headers and the whole body arrive in a single call
         */

        const std::string request =
            "POST /path HTTP/1.0\r\n"
            "Content-Length: 10\r\n\r\n"
            "0123456789";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        const auto httpRequest = parser -> buildRequest();

        UTF_REQUIRE_EQUAL( httpRequest -> method(), "POST" );
        UTF_REQUIRE_EQUAL( httpRequest -> uri(), "/path" );
        UTF_REQUIRE_EQUAL( httpRequest -> body(), "0123456789" );
        UTF_REQUIRE_EQUAL( httpRequest -> headers().size(), 1U );
    }

    {
        /*
         * Test the case when the body arrives in parts - the parser must keep asking for more
         * data until exactly the declared number of body bytes has accumulated
         */

        const std::string request =
            "POST /path HTTP/1.0\r\n"
            "Content-Length: 10\r\n\r\n"
            "012";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result.second == nullptr );

        UTF_REQUIRE_THROW( parser -> buildRequest(), bl::UnexpectedException );

        const std::string request2 = "345";

        const auto buffer2 = request2.c_str();

        const char* begin2 = buffer2;
        const char* end2 = buffer2 + request2.length();

        const auto result2 = parser -> parse( begin2, end2 );

        UTF_REQUIRE_EQUAL( result2.first, HttpParserResult::MORE_DATA_REQUIRED );
        UTF_REQUIRE( result2.second == nullptr );

        const std::string request3 = "6789";

        const auto buffer3 = request3.c_str();

        const char* begin3 = buffer3;
        const char* end3 = buffer3 + request3.length();

        const auto result3 = parser -> parse( begin3, end3 );

        UTF_REQUIRE_EQUAL( result3.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result3.second == nullptr );

        UTF_REQUIRE_EQUAL( parser -> buildRequest() -> body(), "0123456789" );
    }

    {
        /*
         * Test the case when more body bytes arrive than the Content-Length header declares;
         * rejecting them is the only thing which stops a second, smuggled request from being
         * appended to the first one
         */

        const std::string request =
            "POST /path HTTP/1.0\r\n"
            "Content-Length: 5\r\n\r\n"
            "0123456789";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSING_ERROR );
        UTF_REQUIRE( result.second != nullptr );
    }

    {
        /*
         * Test the case when the body itself looks like a header - the sentinel terminates the
         * headers and everything past it must stay in the body
         */

        const std::string request =
            "GET /path HTTP/1.0\r\n"
            "Content-Length: 13\r\n\r\n"
            "X-Evil: bad\r\n";

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        const auto httpRequest = parser -> buildRequest();

        UTF_REQUIRE_EQUAL( httpRequest -> headers().size(), 1U );
        UTF_REQUIRE( httpRequest -> headers().find( "x-evil" ) == httpRequest -> headers().end() );
        UTF_REQUIRE_EQUAL( httpRequest -> body(), "X-Evil: bad\r\n" );
    }

    {
        /*
         * Test the case when the body carries a NUL - the framing is driven by Content-Length
         * and must never be computed from the C string length of the buffer
         */

        std::string request =
            "PUT /path HTTP/1.0\r\n"
            "Content-Length: 5\r\n\r\n";

        request.append( "a\0b\r\n", 5 );

        const auto buffer = request.c_str();

        const char* begin = buffer;
        const char* end = buffer + request.length();

        const auto parser = Parser::createInstance();

        const auto result = parser -> parse( begin, end );

        UTF_REQUIRE_EQUAL( result.first, HttpParserResult::PARSED );
        UTF_REQUIRE( result.second == nullptr );

        const auto httpRequest = parser -> buildRequest();

        UTF_REQUIRE_EQUAL( httpRequest -> body().size(), 5U );
        UTF_REQUIRE( httpRequest -> body() == std::string( "a\0b\r\n", 5 ) );
    }
}

namespace
{
    /**
     * @brief Opens a plain TCP connection to the test server and lets the test drive it
     */

    class RawTestConnection
    {
        BL_NO_COPY_OR_MOVE( RawTestConnection )

    private:

        bl::asio::io_service                    m_ioService;
        bl::asio::ip::tcp::socket               m_socket;

    public:

        RawTestConnection()
            :
            m_socket( m_ioService )
        {
        }

        void connect()
        {
            const bl::asio::ip::tcp::endpoint endpoint(
                #if ( ( BOOST_VERSION / 100 ) >= 1066 )
                bl::asio::ip::make_address( "127.0.0.1" ),
                #else
                bl::asio::ip::address::from_string( "127.0.0.1" ),
                #endif
                test::UtfArgsParser::port()
                );

            m_socket.connect( endpoint );
        }

        void write( SAA_in const std::string& data )
        {
            bl::asio::write( m_socket, bl::asio::buffer( data.c_str(), data.size() ) );
        }

        /**
         * @brief Reads until the server closes the connection or until the timeout expires
         *
         * The read itself is deadline bounded - an async_read_some raced against a timer
         * which cancels it - so a server which neither answers nor closes makes the caller
         * fail on its own deadline instead of blocking forever inside read_some()
         *
         * Returns true if the peer closed within the timeout; whatever was received before
         * that is appended to 'received' when it is not nullptr
         */

        bool readUntilClosedInternal(
            SAA_in          const bl::time::time_duration&      timeout,
            SAA_inout_opt   std::string*                        received
            )
        {
            const auto started = bl::time::microsec_clock::universal_time();

            for( ;; )
            {
                const auto elapsed = bl::time::microsec_clock::universal_time() - started;

                if( elapsed >= timeout )
                {
                    return false;
                }

                char buffer[ 1024 ];

                bl::eh::error_code readEc;
                std::size_t bytesRead = 0U;

                bl::asio::deadline_timer timer( m_ioService );

                m_socket.async_read_some(
                    bl::asio::buffer( buffer, sizeof( buffer ) ),
                    [ &readEc, &bytesRead, &timer ](
                        SAA_in      const bl::eh::error_code&   ec,
                        SAA_in      const std::size_t           transferred
                        ) -> void
                    {
                        readEc = ec;
                        bytesRead = transferred;

                        timer.cancel();
                    }
                    );

                timer.expires_from_now( timeout - elapsed );

                timer.async_wait(
                    [ this ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        if( bl::asio::error::operation_aborted != ec )
                        {
                            bl::eh::error_code cancelEc;

                            m_socket.cancel( cancelEc );
                        }
                    }
                    );

                #if ( ( BOOST_VERSION / 100 ) >= 1066 )
                m_ioService.restart();
                #else
                m_ioService.reset();
                #endif

                m_ioService.run();

                if( readEc )
                {
                    /*
                     * operation_aborted means the deadline fired and cancelled the read, so
                     * the connection is still open; anything else is the peer going away
                     */

                    return bl::asio::error::operation_aborted != readEc;
                }

                if( received )
                {
                    received -> append( buffer, bytesRead );
                }
            }
        }

        /**
         * @brief Waits until the server closes the connection and returns true if it did
         */

        bool waitUntilClosed( SAA_in const bl::time::time_duration& timeout )
        {
            return readUntilClosedInternal( timeout, nullptr /* received */ );
        }

        /**
         * @brief Reads whatever the server sends until it closes the connection
         */

        std::string readUntilClosed( SAA_in const bl::time::time_duration& timeout )
        {
            std::string received;

            ( void ) readUntilClosedInternal( timeout, &received );

            return received;
        }

        /**
         * @brief Returns true if the connection was still open after the whole duration
         *
         * This is the form a negative expectation must use - it never waits longer than the
         * duration it was given and it reports a peer which went away immediately
         */

        bool staysOpenFor( SAA_in const bl::time::time_duration& duration )
        {
            m_socket.non_blocking( true );

            BL_SCOPE_EXIT(
                {
                    bl::eh::error_code ec;

                    m_socket.non_blocking( false, ec );
                }
                );

            const auto started = bl::time::microsec_clock::universal_time();

            for( ;; )
            {
                bl::eh::error_code ec;

                char buffer[ 64 ];

                const auto size = m_socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                BL_UNUSED( size );

                if(
                    ec &&
                    bl::asio::error::would_block != ec &&
                    bl::asio::error::try_again != ec
                    )
                {
                    return false;
                }

                if( ( bl::time::microsec_clock::universal_time() - started ) > duration )
                {
                    return true;
                }

                bl::os::sleep( bl::time::milliseconds( 100 ) );
            }
        }

        bool isOpen() const NOEXCEPT
        {
            return m_socket.is_open();
        }
    };

    /**
     * @brief An HTTPS server whose connections get a short TLS handshake deadline
     *
     * setProtocolTimeout has no caller anywhere in the repository, so shortening the
     * deadline this way is also the only exercise the setter gets
     */

    template
    <
        typename E = void
    >
    class TimeoutHttpSslServerT :
        public bl::httpserver::HttpServerT< bl::tasks::TcpSslSocketAsyncBase >
    {
        BL_DECLARE_OBJECT_IMPL( TimeoutHttpSslServerT )

    protected:

        typedef bl::httpserver::HttpServerT< bl::tasks::TcpSslSocketAsyncBase >         base_type;

        TimeoutHttpSslServerT(
            SAA_in      bl::om::ObjPtr< bl::httpserver::ServerBackendProcessing >&&      backend,
            SAA_in      const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&           controlToken,
            SAA_in      std::string&&                                                    host,
            SAA_in      const unsigned short                                             port,
            SAA_in      const std::string&                                               privateKeyPem,
            SAA_in      const std::string&                                               certificatePem
            )
            :
            base_type(
                BL_PARAM_FWD( backend ),
                controlToken,
                BL_PARAM_FWD( host ),
                port,
                privateKeyPem,
                certificatePem
                )
        {
        }

        virtual bl::om::ObjPtr< bl::tasks::Task > createProtocolHandshakeTask(
            SAA_inout   bl::tasks::TcpSslSocketAsyncBase::stream_ref&&                   connectedStream
            ) OVERRIDE
        {
            auto task = base_type::createProtocolHandshakeTask( BL_PARAM_FWD( connectedStream ) );

            const auto handshakeTask =
                bl::om::qi< bl::om::ObjectImpl< bl::tasks::TcpSslSocketAsyncBase > >( task );

            handshakeTask -> setProtocolTimeout( bl::time::seconds( 3 ) );

            return task;
        }
    };

    typedef bl::om::ObjectImpl< TimeoutHttpSslServerT<> > TimeoutHttpSslServerImpl;

    /**
     * @brief A backend whose processing task fails in the four interesting ways
     *
     * The shared utest::http::TestHttpServerProcessingTask never throws - it always sets a
     * status and returns - so the whole "the wrapped processing task failed" half of
     * HttpServerConnection::continuationTask() is otherwise dead in this module
     */

    template
    <
        typename BACKENDSTATE
    >
    class TestFailingProcessingTask :
        public bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >
    {
        BL_DECLARE_OBJECT_IMPL( TestFailingProcessingTask )

    protected:

        typedef bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >         base_type;

        using base_type::m_request;

        TestFailingProcessingTask(
            SAA_in          bl::om::ObjPtr< bl::httpserver::Request >&&                  request,
            SAA_in_opt      bl::om::ObjPtr< BACKENDSTATE >&&                             backendState = nullptr
            )
            :
            base_type( BL_PARAM_FWD( request ), BL_PARAM_FWD( backendState ) )
        {
        }

        virtual void requestProcessing() OVERRIDE
        {
            const auto& uri = m_request -> uri();

            if( "/fail-aborted" == uri )
            {
                /*
                 * This is exactly the shape rest::HttpServerBackendMessagingBridge produces
                 * when it fails a conversation on its request timeout
                 */

                throw BL_EXCEPTION(
                    bl::SystemException::create(
                        bl::asio::error::operation_aborted,
                        "Simulated backend cancellation"
                        ),
                    "Simulated backend cancellation"
                    );
            }

            if( "/fail-timedout" == uri )
            {
                throw BL_EXCEPTION(
                    bl::SystemException::create(
                        bl::asio::error::timed_out,
                        "Simulated backend timeout"
                        ),
                    "Simulated backend timeout"
                    );
            }

            if( "/fail-eperm" == uri )
            {
                /*
                 * A system_error carrying an unrelated code must NOT be mapped to a gateway
                 * timeout - this is the sub-case which stops the mapper from being written
                 * as "any system_error is a timeout"
                 */

                throw BL_EXCEPTION(
                    bl::SystemException::create(
                        bl::eh::errc::make_error_code( bl::eh::errc::permission_denied ),
                        "Simulated backend permission failure"
                        ),
                    "Simulated backend permission failure"
                    );
            }

            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "Simulated backend failure"
                );
        }
    };

    typedef bl::om::ObjectImpl
    <
        bl::httpserver::ServerBackendProcessingImplDefault
        <
            utest::http::DummyBackendStateImpl,
            TestFailingProcessingTask
        >
    >
    FailingBackendImpl;

    /**
     * @brief A backend which tries to smuggle a second header through a custom header value
     */

    template
    <
        typename BACKENDSTATE
    >
    class TestCustomHeaderProcessingTask :
        public bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >
    {
        BL_DECLARE_OBJECT_IMPL( TestCustomHeaderProcessingTask )

    protected:

        typedef bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >         base_type;
        typedef bl::http::Parameters::HttpStatusCode                                    HttpStatusCode;

        using base_type::m_statusCode;
        using base_type::m_request;
        using base_type::m_response;
        using base_type::m_responseHeaders;

        TestCustomHeaderProcessingTask(
            SAA_in          bl::om::ObjPtr< bl::httpserver::Request >&&                  request,
            SAA_in_opt      bl::om::ObjPtr< BACKENDSTATE >&&                             backendState = nullptr
            )
            :
            base_type( BL_PARAM_FWD( request ), BL_PARAM_FWD( backendState ) )
        {
        }

        virtual void requestProcessing() OVERRIDE
        {
            m_responseHeaders.clear();

            if( "/inject" == m_request -> uri() )
            {
                m_responseHeaders[ "X-Custom" ] = "value\r\nSet-Cookie: injected=1";
                m_statusCode = HttpStatusCode::HTTP_SUCCESS_OK;

                return;
            }

            m_responseHeaders[ "X-Custom" ] = "plain-value";
            m_response = "ok";
            m_statusCode = HttpStatusCode::HTTP_SUCCESS_OK;
        }
    };

    typedef bl::om::ObjectImpl
    <
        bl::httpserver::ServerBackendProcessingImplDefault
        <
            utest::http::DummyBackendStateImpl,
            TestCustomHeaderProcessingTask
        >
    >
    CustomHeaderBackendImpl;

    /**
     * @brief Reaches TcpServerPolicySmooth's protected constructor
     *
     * isLogOnConnect / isLogOnDisconnect / isLogNoConnections are already public and forward
     * to isLogInternal unchanged, so only the constructor needs widening - the protected
     * predicate itself is exercised through them rather than by relaxing its visibility.
     * BL_NO_COPY_OR_MOVE on the base means the probe has to be constructed in place
     */

    class SmoothPolicyProbe : public bl::tasks::TcpServerPolicySmooth
    {
    public:

        SmoothPolicyProbe() = default;

        explicit SmoothPolicyProbe( SAA_in const std::size_t delta )
            :
            bl::tasks::TcpServerPolicySmooth( delta )
        {
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_TcpServerPolicySmoothHysteresisTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * TcpServerPolicySmooth is TcpServerPolicyDefault - the policy every TcpServerBase gets
     * unless it is told otherwise - and nothing exercises it at all, while its trivial
     * sibling getDerivedMaxConnections has a dedicated case below
     *
     * The rows are evaluated in order on a single probe instance: the accumulated state is
     * the point, and a rejected call must not advance the last logged value
     */

    UTF_REQUIRE( ( std::is_same< TcpServerPolicyDefault, TcpServerPolicySmooth >::value ) );

    UTF_REQUIRE( TcpServerPolicySmooth::isLogNoConnections() );

    {
        SmoothPolicyProbe probe;

        UTF_REQUIRE( probe.isLogOnConnect( 50U ) );             /* the '0 == last' term */
        UTF_REQUIRE( ! probe.isLogOnConnect( 60U ) );           /* delta 10, below the default of 100 */

        /*
         * Measured from 50 rather than from the rejected 60 - which is what proves that a
         * rejected call leaves m_noOfConnectionsLastLogged where it was
         */

        UTF_REQUIRE( probe.isLogOnConnect( 150U ) );            /* delta exactly 100 */

        /*
         * The unsigned underflow row: 149 is below the last logged 150, so the delta is 1
         * and the call must be rejected. A naive 'noOfConnections - last' would wrap to a
         * huge unsigned value, pass the minimum and make every downward transition log -
         * a log flood at exactly the moment a server is shedding load
         */

        UTF_REQUIRE( ! probe.isLogOnDisconnect( 149U ) );

        UTF_REQUIRE( probe.isLogOnDisconnect( 0U ) );           /* the '0 == noOfConnections' term */
    }

    {
        /*
         * The same table against a custom minimum delta
         */

        SmoothPolicyProbe probe( 5U );

        UTF_REQUIRE( probe.isLogOnConnect( 3U ) );
        UTF_REQUIRE( ! probe.isLogOnConnect( 5U ) );
        UTF_REQUIRE( probe.isLogOnConnect( 8U ) );              /* delta 5 measured from 3, not from 5 */
        UTF_REQUIRE( ! probe.isLogOnDisconnect( 4U ) );         /* downward, delta 4 */
        UTF_REQUIRE( probe.isLogOnDisconnect( 3U ) );           /* downward, delta 5 */
        UTF_REQUIRE( probe.isLogOnDisconnect( 0U ) );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerDerivedConnectionCapTest )
{
    using namespace bl;
    using namespace bl::tasks;

    typedef httpserver::HttpServer                                          server_t;

    /*
     * The derived cap is the minimum of the fixed ceiling, half of the descriptor soft
     * limit and the number of connections which fit in 80% of the physical memory
     */

    UTF_REQUIRE_EQUAL(
        server_t::getDerivedMaxConnections(
            1024U * 1024U       /* memoryFootprint */,
            64ULL * 1024U * 1024U * 1024U /* physicalMemorySize */,
            1048576U            /* fileDescriptorSoftLimit */
            ),
        4096U
        );

    UTF_REQUIRE_EQUAL(
        server_t::getDerivedMaxConnections(
            1024U * 1024U       /* memoryFootprint */,
            64ULL * 1024U * 1024U * 1024U /* physicalMemorySize */,
            1024U               /* fileDescriptorSoftLimit */
            ),
        512U
        );

    UTF_REQUIRE_EQUAL(
        server_t::getDerivedMaxConnections(
            16ULL * 1024U * 1024U /* memoryFootprint */,
            1024ULL * 1024U * 1024U /* physicalMemorySize */,
            1048576U            /* fileDescriptorSoftLimit */
            ),
        51U
        );

    /*
     * Unknown terms are simply skipped and the result is never zero
     */

    UTF_REQUIRE_EQUAL(
        server_t::getDerivedMaxConnections( 1024U * 1024U, 0U, 0U ),
        4096U
        );

    UTF_REQUIRE_EQUAL(
        server_t::getDerivedMaxConnections( 1024ULL * 1024U * 1024U * 1024U, 1024U, 0U ),
        1U
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerConnectionTimeoutAndCapTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    test::MachineGlobalTestLock lock;

    const auto backend =
        ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >();

    const om::ObjPtr< TaskControlTokenRW > controlToken;

    const auto acceptor = httpserver::HttpServer::createInstance<>(
        om::copy( backend ),
        controlToken,
        "0.0.0.0"                                       /* host */,
        test::UtfArgsParser::port(),
        test::UtfCrypto::getDefaultServerKey(),
        test::UtfCrypto::getDefaultServerCertificate()
        );

    /*
     * A short inactivity timeout and a cap of two connections
     */

    acceptor -> setConnectionTimeout( time::seconds( 3 ) );
    acceptor -> setMaxConnections( 2U );

    utest::TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            UTF_REQUIRE_EQUAL( acceptor -> getMaxConnections(), 2U );
            UTF_REQUIRE_EQUAL( acceptor -> getConnectionTimeout(), time::seconds( 3 ) );

            {
                /*
                 * The deadline is an *inactivity* deadline, not a total request deadline -
                 * scheduleRead() re-arms it before every read, so a client which keeps
                 * sending is never interrupted however long its request takes. The drip
                 * below feeds one body byte per second for eight seconds against a three
                 * second deadline, so a regression which armed the timer once in
                 * scheduleTask() instead would kill every slow or large upload at 60 s in
                 * production while leaving the expiry sub-block below green
                 */

                RawTestConnection connection;

                connection.connect();
                connection.write( "PUT /request-uri HTTP/1.0\r\nContent-Length: 8\r\n\r\n" );

                for( int i = 0; i < 8; ++i )
                {
                    connection.write( std::string( 1, static_cast< char >( '0' + i ) ) );

                    os::sleep( time::seconds( 1 ) );
                }

                const auto response = connection.readUntilClosed( time::seconds( 30 ) );

                UTF_REQUIRE( 0U == response.find( "HTTP/1.0 200 OK\r\n" ) );

                /*
                 * Which also proves end to end that the eight drip fed body bytes were all
                 * accumulated before PARSED was reported
                 */

                UTF_REQUIRE( response.find( g_desiredResult ) != std::string::npos );
            }

            {
                /*
                 * A client which sends half a request and then stops must have its
                 * connection cancelled once the inactivity timeout expires
                 */

                RawTestConnection connection;

                connection.connect();
                connection.write( "GET /path HTTP/1.0\r\nHost: localhost\r\n" );

                UTF_REQUIRE( connection.waitUntilClosed( time::seconds( 30 ) ) );
            }

            {
                /*
                 * The third concurrent connection must be refused (closed immediately)
                 * while the first two stay open
                 */

                RawTestConnection connection1;
                RawTestConnection connection2;
                RawTestConnection connection3;

                connection1.connect();
                connection1.write( "GET /path HTTP/1.0\r\n" );

                connection2.connect();
                connection2.write( "GET /path HTTP/1.0\r\n" );

                os::sleep( time::milliseconds( 500 ) );

                connection3.connect();

                UTF_REQUIRE( connection3.waitUntilClosed( time::seconds( 10 ) ) );
            }
        },
        acceptor,
        test::UtfArgsParser::host()                     /* readinessHost */,
        test::UtfArgsParser::port()                     /* readinessPort */
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerConnectionTimeoutDisabledTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    test::MachineGlobalTestLock lock;

    const auto backend =
        ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >();

    const om::ObjPtr< TaskControlTokenRW > controlToken;

    const auto acceptor = httpserver::HttpServer::createInstance<>(
        om::copy( backend ),
        controlToken,
        "0.0.0.0"                                       /* host */,
        test::UtfArgsParser::port(),
        test::UtfCrypto::getDefaultServerKey(),
        test::UtfCrypto::getDefaultServerCertificate()
        );

    /*
     * scheduleTimer() returns before it arms anything when the duration is special or is not
     * positive, so a zero timeout disables the deadline entirely. That is a documented API
     * contract with no test at all, and dropping the guard would turn a zero duration - which
     * is what every caller leaving the timeout on its time_duration() default has - into an
     * immediate cancel of the connection
     */

    acceptor -> setConnectionTimeout( time::seconds( 0 ) );

    UTF_REQUIRE_EQUAL( acceptor -> getConnectionTimeout(), time::seconds( 0 ) );

    utest::TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            RawTestConnection connection;

            connection.connect();
            connection.write( "GET /path HTTP/1.0\r\nHost: localhost\r\n" );

            /*
             * A negative expectation must be expressed with staysOpenFor, which never waits
             * longer than the duration it was given
             */

            UTF_REQUIRE( connection.staysOpenFor( time::seconds( 8 ) ) );

            UTF_REQUIRE( ! connection.waitUntilClosed( time::milliseconds( 500 ) ) );
            UTF_REQUIRE( connection.isOpen() );
        },
        acceptor,
        test::UtfArgsParser::host()                     /* readinessHost */,
        test::UtfArgsParser::port()                     /* readinessPort */
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpSslServerProtocolHandshakeTimeoutTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * A peer which completes the TCP connect and then never sends its ClientHello would park
     * a handshake task, an execution queue slot and a file descriptor for the lifetime of the
     * server if the handshake deadline were not armed. The whole guard is dark today -
     * setProtocolTimeout and getProtocolTimeout have zero callers repo wide
     */

    {
        const auto probe = om::ObjectImpl< tasks::TcpSslSocketAsyncBase >::createInstance(
            std::string( "t" )
            );

        UTF_REQUIRE_EQUAL(
            probe -> getProtocolTimeout(),
            time::seconds( tasks::TcpSslSocketAsyncBase::DEFAULT_PROTOCOL_TIMEOUT_IN_SECONDS )
            );

        probe -> setProtocolTimeout( time::seconds( 7 ) );

        UTF_REQUIRE_EQUAL( probe -> getProtocolTimeout(), time::seconds( 7 ) );
    }

    test::MachineGlobalTestLock lock;

    const auto backend =
        ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >();

    const om::ObjPtr< TaskControlTokenRW > controlToken;

    const auto acceptor = TimeoutHttpSslServerImpl::createInstance<>(
        om::copy( backend ),
        controlToken,
        "0.0.0.0"                                       /* host */,
        test::UtfArgsParser::port(),
        test::UtfCrypto::getDefaultServerKey(),
        test::UtfCrypto::getDefaultServerCertificate()
        );

    utest::TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            RawTestConnection connection;

            connection.connect();

            /*
             * Nothing is written, so the server side handshake never gets its ClientHello and
             * only the deadline can tear it down. The acceptor shortened it to three seconds,
             * so being closed well inside the thirty second ceiling is the assertion
             */

            UTF_REQUIRE( connection.waitUntilClosed( time::seconds( 30 ) ) );

            const auto waitForEndpointsToDrain = [ &acceptor ]() -> void
            {
                for( std::size_t i = 0U; i < 50U && ! acceptor -> activeEndpoints().empty(); ++i )
                {
                    os::sleep( time::milliseconds( 200 ) );
                }

                UTF_REQUIRE_EQUAL( acceptor -> activeEndpoints().size(), 0U );
            };

            /*
             * The timed out connection must also leave the server bookkeeping clean. Note a
             * TLS connection which never completed its handshake was only ever a handshake
             * task, so what has to be erased for it is m_handshakeTasks, which has no
             * accessor - the assertion below is what would catch a regression in which a
             * failed handshake was still pushed on as a connection and then never drained
             */

            waitForEndpointsToDrain();

            /*
             * The shutdown arm of the same protocol timer: a client which completes the
             * handshake, exchanges one request and then goes away makes the server's
             * connection task run its finish continuation, which arms m_protocolTimer
             * immediately before beginProtocolShutdown and relies on onTaskStoppedNothrow to
             * cancel it - the timer must be cancelled there and not when a protocol operation
             * completes, because the shutdown runs while the task is still Running
             *
             * Unlike the timed out connection above this one does reach m_activeEndpoints, so
             * the drain afterwards is the assertion that the connection task ran all the way
             * through onTaskStoppedNothrow and that TcpServerBase::onEvent erased it
             */

            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    const auto taskImpl = SimpleHttpSslGetTaskImpl::createInstance(
                        cpp::copy( test::UtfArgsParser::host() ),
                        test::UtfArgsParser::port(),
                        utest::http::g_requestUri,
                        std::string()                   /* content */
                        );

                    const auto task = om::qi< Task >( taskImpl );

                    eq -> push_back( task );

                    UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                    UTF_REQUIRE_EQUAL( http::Parameters::HTTP_SUCCESS_OK, taskImpl -> getHttpStatus() );
                    UTF_REQUIRE( ! taskImpl -> getResponse().empty() );

                    UTF_REQUIRE( eq -> isEmpty() );
                });

            waitForEndpointsToDrain();
        },
        acceptor,
        test::UtfArgsParser::host()                     /* readinessHost */,
        test::UtfArgsParser::port()                     /* readinessPort */
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpSslServerConnectionCapTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * For a plain TCP server the objects counted against the connection cap are the
     * connection tasks; for an SSL server they are the in flight handshake tasks, which
     * processIncomingConnection inserts into and erases from m_handshakeTasks, and the
     * refusal path shuts down the lowest layer of the SSL wrapper rather than the socket
     *
     * BaseLib_HttpServerConnectionTimeoutAndCapTest covers the cap for plain TCP only, and
     * the m_handshakeTasks half of the bookkeeping is the part of the server accounting with
     * no coverage anywhere - an erase which never ran would make the server stop accepting
     * after maxConnections half open TLS connections, which is a trivial denial of service
     */

    test::MachineGlobalTestLock lock;

    const auto backend =
        ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >();

    const om::ObjPtr< TaskControlTokenRW > controlToken;

    /*
     * The stock SSL server is used rather than TimeoutHttpSslServerImpl so that the default
     * sixty second handshake deadline - comfortably longer than this case - cannot close the
     * held connections underneath the cap assertions
     */

    const auto acceptor = httpserver::HttpSslServer::createInstance<>(
        om::copy( backend ),
        controlToken,
        "0.0.0.0"                                       /* host */,
        test::UtfArgsParser::port(),
        test::UtfCrypto::getDefaultServerKey(),
        test::UtfCrypto::getDefaultServerCertificate()
        );

    acceptor -> setMaxConnections( 2U );

    utest::TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            UTF_REQUIRE_EQUAL( acceptor -> getMaxConnections(), 2U );

            const auto& connectionsQueue = acceptor -> executionQueue();

            UTF_REQUIRE( connectionsQueue );

            const auto waitForConnectionsToDrain = [ &connectionsQueue ]() -> void
            {
                for( std::size_t i = 0U; i < 50U && 0U != connectionsQueue -> size(); ++i )
                {
                    os::sleep( time::milliseconds( 200 ) );
                }

                UTF_REQUIRE_EQUAL( connectionsQueue -> size(), 0U );
            };

            /*
             * The readiness probe of startAcceptorAndExecuteCallback connects and disconnects
             * once, which leaves a handshake task of its own behind, so the queue is drained
             * before anything is measured against the cap
             */

            waitForConnectionsToDrain();

            {
                RawTestConnection connection1;
                RawTestConnection connection2;
                RawTestConnection connection3;

                /*
                 * Only the TCP connect is completed and no ClientHello is ever sent, so each
                 * of these is parked as an in flight handshake task and counts against the cap
                 */

                connection1.connect();
                connection2.connect();

                os::sleep( time::milliseconds( 500 ) );

                connection3.connect();

                UTF_REQUIRE( connection3.waitUntilClosed( time::seconds( 30 ) ) );

                UTF_REQUIRE( connection1.staysOpenFor( time::seconds( 2 ) ) );
                UTF_REQUIRE( connection2.staysOpenFor( time::seconds( 2 ) ) );
            }

            /*
             * Both connections are gone now, so the erase half of the bookkeeping must give
             * the slots back - the size of the connections queue is the quantity the cap is
             * measured against
             */

            waitForConnectionsToDrain();

            {
                RawTestConnection connection4;

                connection4.connect();

                UTF_REQUIRE( connection4.staysOpenFor( time::seconds( 2 ) ) );
            }
        },
        acceptor,
        test::UtfArgsParser::host()                     /* readinessHost */,
        test::UtfArgsParser::port()                     /* readinessPort */
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerBackendFailureStatusTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * A failure of the processing stage is a failure of the server or of the backend behind
     * it, not a bad request: continuationTask() maps a cancelled or a timed out backend to a
     * gateway timeout and everything else to an internal server error. The shared test
     * processing task never throws, so that whole half of continuationTask() is dead in this
     * module - and collapsing the mapper to a single status, or losing its
     * catch( eh::system_error& ) arm, would leave a REST gateway client unable to tell "the
     * backend is broken" from "the backend was slow"
     *
     * All four requests share one server start so the acceptor warm up is paid once
     */

    HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    /*
                     * The body is the redacted server error document, so the backend's own
                     * "Simulated backend failure" text is not in it - what a client gets is
                     * the friendly message. The status code is what carries the distinction
                     * this case is about
                     */

                    HttpServerHelpers::sendHttpRequestAndVerifyTheResult(
                        eq,
                        "/fail-plain"                                       /* uri */,
                        "0123456789"                                        /* content */,
                        true                                                /* exceptionExpected */,
                        http::Parameters::HTTP_SERVER_ERROR_INTERNAL        /* statusCodeExpected */,
                        BL_GENERIC_FRIENDLY_UNEXPECTED_MSG                  /* contentExpected */
                        );

                    HttpServerHelpers::sendHttpRequestAndVerifyTheResult(
                        eq,
                        "/fail-aborted"                                     /* uri */,
                        "0123456789"                                        /* content */,
                        true                                                /* exceptionExpected */,
                        http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT /* statusCodeExpected */
                        );

                    HttpServerHelpers::sendHttpRequestAndVerifyTheResult(
                        eq,
                        "/fail-timedout"                                    /* uri */,
                        "0123456789"                                        /* content */,
                        true                                                /* exceptionExpected */,
                        http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT /* statusCodeExpected */
                        );

                    HttpServerHelpers::sendHttpRequestAndVerifyTheResult(
                        eq,
                        "/fail-eperm"                                       /* uri */,
                        "0123456789"                                        /* content */,
                        true                                                /* exceptionExpected */,
                        http::Parameters::HTTP_SERVER_ERROR_INTERNAL        /* statusCodeExpected */
                        );
                });
        },
        FailingBackendImpl::createInstance< httpserver::ServerBackendProcessing >()
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerBackendResponseHeadersTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * chkCustomHeader is the only validation between a header a backend fills in through
     * getResponseHeadersLvalue() and the socket. BaseLib_ResponseHeaderValidationTest proves
     * it rejects a CRLF value on a directly constructed Response, but nothing connected that
     * to a running server and nothing asserted what the client actually receives
     *
     * Both halves share one server start
     */

    HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            /*
             * (a) The injected bytes must never reach the wire. Today the rejection
             *     propagates out of continuationTask(), the execution queue attaches it to
             *     the connection task as a normal task failure and no response is written at
             *     all - so this asserts on the absence of the injected bytes rather than on a
             *     status code, which is a product decision this test must not freeze
             */

            {
                RawTestConnection connection;

                connection.connect();
                connection.write( "GET /inject HTTP/1.0\r\nHost: localhost\r\n\r\n" );

                const auto started = time::microsec_clock::universal_time();

                const auto bytes = connection.readUntilClosed( time::seconds( 30 ) );

                const auto elapsed = time::microsec_clock::universal_time() - started;

                UTF_REQUIRE( bytes.find( "injected=1" ) == std::string::npos );
                UTF_REQUIRE( bytes.find( "Set-Cookie" ) == std::string::npos );

                /*
                 * The connection is dropped rather than left hanging - a hang here is the
                 * regression, so returning only when the thirty second bound expired is a
                 * failure and not a pass
                 */

                UTF_REQUIRE( elapsed < time::seconds( 20 ) );
            }

            /*
             * (b) The acceptor was not wedged by the rejection and the connection slot was
             *     released, and a legitimate backend header survives validation, serialization
             *     and the client parse
             */

            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto taskImpl = SimpleHttpPutTaskImpl::createInstance(
                        cpp::copy( test::UtfArgsParser::host() ),
                        test::UtfArgsParser::port(),
                        "/benign"                                       /* URI */,
                        "0123456789"                                    /* content */
                        );

                    const auto task = om::qi< Task >( taskImpl );

                    eq -> push_back( task );

                    UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task, false /* cancel */ ) );

                    UTF_REQUIRE_EQUAL( http::Parameters::HTTP_SUCCESS_OK, taskImpl -> getHttpStatus() );

                    /*
                     * doReadHeaders lower cases every non Set-Cookie header name before it
                     * stores it, so the map must never be looked up with the original casing -
                     * tryGetResponseHeader lower cases on behalf of the caller
                     */

                    const auto value = taskImpl -> tryGetResponseHeader( "X-Custom" );

                    UTF_REQUIRE( value );
                    UTF_REQUIRE_EQUAL( *value, "plain-value" );

                    UTF_REQUIRE( eq -> isEmpty() );
                });
        },
        CustomHeaderBackendImpl::createInstance< httpserver::ServerBackendProcessing >()
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerDefaultBackendTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * None of the repository instantiations of ServerBackendProcessingImplDefault uses the
     * default TASKIMPL, so the default template argument is never instantiated and never
     * compiled anywhere - a change to HttpServerProcessingTaskDefault which broke the default
     * path (a signature change, a missing OVERRIDE, a member which no longer initialises)
     * would compile cleanly across the whole repository
     *
     * Note this asserts the object level contract only. On the wire the 404 the task starts
     * with is not what a client sees: requestProcessing() throws, so the connection takes the
     * PROCESS failure path and the no_such_file_or_directory SystemException is mapped to a
     * 500 - that mapping belongs to BaseLib_HttpServerBackendFailureStatusTest
     */

    typedef om::ObjectImpl
    <
        httpserver::ServerBackendProcessingImplDefault< DummyBackendStateImpl >
    >
    default_backend_t;

    const auto backend = default_backend_t::createInstance< httpserver::ServerBackendProcessing >();

    auto request = httpserver::Request::createInstance( std::string( "GET" ), std::string( "/anything" ) );

    const auto task = backend -> getProcessingTask( std::move( request ) );

    UTF_REQUIRE( task );

    /*
     * getResponse() moves the content, the content type and the header map out of the task,
     * so it is read here before the task is executed
     */

    const auto response = backend -> getResponse( task );

    UTF_REQUIRE_EQUAL( response -> status(), http::Parameters::HTTP_CLIENT_ERROR_NOT_FOUND );
    UTF_REQUIRE( response -> content().empty() );

    UTF_REQUIRE_EQUAL(
        response -> headers().at( http::HttpHeader::g_contentType ),
        http::HttpHeader::g_contentTypeJsonUtf8
        );

    UTF_REQUIRE_EQUAL( response -> headers().at( http::HttpHeader::g_contentLength ), "0" );

    /*
     * The override is what stops the continuation of the wrapped SimpleTaskImpl from being
     * forwarded to the server
     */

    UTF_REQUIRE( task -> continuationTask() == nullptr );

    scheduleAndExecuteInParallel(
        [ &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> push_back( task );

            eq -> flush(
                false                                           /* discardPending */,
                true                                            /* nothrowIfFailed */
                );

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE( nullptr != task -> exception() );

            try
            {
                cpp::safeRethrowException( task -> exception() );

                UTF_FAIL( "The default backend processing task must fail" );
            }
            catch( eh::system_error& e )
            {
                UTF_REQUIRE(
                    e.code() == eh::errc::make_error_code( eh::errc::no_such_file_or_directory )
                    );
            }

            /*
             * The queue is configured to keep the failed tasks, so the task has to be
             * popped here to leave the queue empty
             */

            const auto failedTask = eq -> pop( false /* wait */ );

            UTF_REQUIRE( om::areEqual( task, failedTask ) );
            UTF_REQUIRE( eq -> isEmpty() );
        });
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerCancelBeforeStartTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    /*
     * A server task which is cancelled before it has started executing unwinds through
     * onTaskStoppedNothrow() before its execution queues and its notification callback
     * have been created, so the unwind must tolerate all of them being null
     */

    scheduleAndExecuteInParallel(
        []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            const om::ObjPtr< TaskControlTokenRW > controlToken;

            const auto acceptor = httpserver::HttpServer::createInstance<>(
                ServerBackendProcessingImplTest::createInstance< httpserver::ServerBackendProcessing >(),
                controlToken,
                "0.0.0.0"                                       /* host */,
                test::UtfArgsParser::port(),
                test::UtfCrypto::getDefaultServerKey(),
                test::UtfCrypto::getDefaultServerCertificate()
                );

            const auto task = om::qi< Task >( acceptor );

            task -> requestCancel();

            eq -> push_back( task );

            eq -> flush(
                false                                           /* discardPending */,
                true                                            /* nothrowIfFailed */
                );

            UTF_REQUIRE( task -> isFailed() );

            /*
             * The queue is configured to keep the failed tasks, so the task has to be
             * popped here to leave the queue empty
             */

            const auto failedTask = eq -> pop( false /* wait */ );

            UTF_REQUIRE( om::areEqual( task, failedTask ) );
            UTF_REQUIRE( eq -> isEmpty() );
        });
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerImplTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    /*
                     * Run an assorted set of HTTP requests
                     */

                    HttpServerHelpers::sendAndVerifyAssortedHttpRequests( eq );
                });
        }
        );
}

UTF_AUTO_TEST_CASE( BaseLib_HttpServerPerfTest )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http;

    HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    typedef SimpleHttpPutTaskImpl task_impl_t;

                    {
                        /*
                         * Run an assorted set of HTTP requests (multiple sequential)
                         */

                        Logging::LevelPusher level( Logging::LL_INFO, true /* global */ );

                        const std::size_t count = 10;

                        utils::ExecutionTimer timer(
                            resolveMessage(
                                BL_MSG()
                                    << "Execute "
                                    << count
                                    << " tasks sequentially"
                                )
                            );

                        for( std::size_t i = 0; i < count; ++i )
                        {
                            HttpServerHelpers::sendAndVerifyAssortedHttpRequests< task_impl_t >( eq );
                        }
                    }

                    HttpServerHelpers::completion_results_map_t results;

                    {
                        /*
                         * Run an assorted set of HTTP requests (multiple parallel)
                         */

                        Logging::LevelPusher level( Logging::LL_INFO, true /* global */ );

                        eq -> setOptions( ExecutionQueue::OptionKeepNone );

                        const std::size_t count = 10;

                        utils::ExecutionTimer timer(
                            resolveMessage(
                                BL_MSG()
                                    << "Execute "
                                    << count
                                    << " tasks in parallel"
                                )
                            );

                        for( std::size_t i = 0; i < count; ++i )
                        {
                            HttpServerHelpers::sendAndVerifyAssortedHttpRequests< task_impl_t >( eq, &results );
                        }

                        eq -> flush(
                            false   /* discardPending */,
                            true    /* nothrowIfFailed */,
                            false   /* discardReady */,
                            false   /* cancelExecuting */
                            );
                    }

                    for( const auto& pair : results )
                    {
                        const auto taskImpl = om::qi< task_impl_t >( pair.first );
                        const auto& result = pair.second;

                        const auto status = taskImpl -> getHttpStatus();
                        const auto& response = taskImpl -> getResponse();

                        const bool& exceptionExpected = std::get< 0 >( result );
                        const bl::http::Parameters::HttpStatusCode& statusCodeExpected = std::get< 1 >( result );
                        const std::string& contentExpected = std::get< 2 >( result );

                        if( taskImpl -> isFailed() != exceptionExpected )
                        {
                            if( exceptionExpected )
                            {
                                UTF_FAIL( "Task has succeeded even though exception was expected" );
                            }
                            else
                            {
                                UTF_REQUIRE( taskImpl -> isFailed() );
                                UTF_REQUIRE( taskImpl -> exception() );

                                cpp::safeRethrowException( taskImpl -> exception() );
                            }
                        }

                        if( statusCodeExpected != status )
                        {
                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "HTTP status code "
                                    << status
                                    << " is different than the expected HTTP status code "
                                    << statusCodeExpected
                                );

                            UTF_FAIL( "Invariant broken - see message above" );
                        }

                        if( ! contentExpected.empty() )
                        {
                            UTF_REQUIRE( response.find( contentExpected ) != std::string::npos );
                        }
                    }
                });
        }
        );
}

