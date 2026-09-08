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

    const auto ok = "HTTP/1.0 200 OK\r\n";
    const auto created = "HTTP/1.0 201 Created\r\n";
    const auto accepted = "HTTP/1.0 202 Accepted\r\n";
    const auto noContent = "HTTP/1.0 204 No Content\r\n";
    const auto multipleChoices = "HTTP/1.0 300 Multiple Choices\r\n";
    const auto movedPermanently = "HTTP/1.0 301 Moved Permanently\r\n";
    const auto movedTemporarily = "HTTP/1.0 302 Moved Temporarily\r\n";
    const auto notModified = "HTTP/1.0 304 Not Modified\r\n";
    const auto badRequest = "HTTP/1.0 400 Bad Request\r\n";
    const auto unauthorized = "HTTP/1.0 401 Unauthorized\r\n";
    const auto forbidden = "HTTP/1.0 403 Forbidden\r\n";
    const auto notFound = "HTTP/1.0 404 Not Found\r\n";
    const auto internalError = "HTTP/1.0 500 Internal Server Error\r\n";
    const auto notImplemented = "HTTP/1.0 501 Not Implemented\r\n";
    const auto badGateway = "HTTP/1.0 502 Bad Gateway\r\n";
    const auto serviceUnavailable = "HTTP/1.0 503 Service Unavailable\r\n";

    std::vector< std::pair< std::string, StatusCode > > stringToCode;

    stringToCode.emplace_back( ok, StatusCode::HTTP_SUCCESS_OK );
    stringToCode.emplace_back( created, StatusCode::HTTP_SUCCESS_CREATED );
    stringToCode.emplace_back( accepted, StatusCode::HTTP_SUCCESS_ACCEPTED );
    stringToCode.emplace_back( noContent, StatusCode::HTTP_SUCCESS_NO_CONTENT );
    stringToCode.emplace_back( multipleChoices, StatusCode::HTTP_REDIRECT_MULTIPLE_CHOICES );
    stringToCode.emplace_back( movedPermanently, StatusCode::HTTP_REDIRECT_PERMANENTLY );
    stringToCode.emplace_back( movedTemporarily, StatusCode::HTTP_REDIRECT_TEMPORARILY );
    stringToCode.emplace_back( notModified, StatusCode::HTTP_REDIRECT_NOT_MODIFIED );
    stringToCode.emplace_back( badRequest, StatusCode::HTTP_CLIENT_ERROR_BAD_REQUEST );
    stringToCode.emplace_back( unauthorized, StatusCode::HTTP_CLIENT_ERROR_UNAUTHORIZED );
    stringToCode.emplace_back( forbidden, StatusCode::HTTP_CLIENT_ERROR_FORBIDDEN );
    stringToCode.emplace_back( notFound, StatusCode::HTTP_CLIENT_ERROR_NOT_FOUND );
    stringToCode.emplace_back( internalError, StatusCode::HTTP_SERVER_ERROR_INTERNAL );
    stringToCode.emplace_back( notImplemented, StatusCode::HTTP_SERVER_ERROR_NOT_IMPLEMENTED );
    stringToCode.emplace_back( badGateway, StatusCode::HTTP_SERVER_ERROR_BAD_GATEWAY );
    stringToCode.emplace_back( serviceUnavailable, StatusCode::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE );

    for( const auto& pair : stringToCode )
    {
        UTF_REQUIRE_EQUAL( StatusStrings::get( pair.second ), pair.first );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_ResponseTest )
{
    using namespace bl;

    typedef http::Parameters::HttpStatusCode        StatusCode;
    typedef httpserver::Response                    Response;
    typedef http::Parameters::HttpHeader            HttpHeader;

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
         * An invalid header name must be rejected as well
         */

        http::HeadersMap headers;

        headers.emplace( "X Custom", "value" );

        UTF_REQUIRE_THROW( createResponse( std::move( headers ) ), bl::UnexpectedException );
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
     * The client must still be told what went wrong, so the message is deliberately kept
     */

    UTF_REQUIRE( content.find( "Bad client request" ) != std::string::npos );

    UTF_REQUIRE_EQUAL(
        response -> headers().at( http::HttpHeader::g_contentType ),
        http::HttpHeader::g_contentTypeJsonUtf8
        );

    UTF_REQUIRE_EQUAL(
        response -> headers().at( http::HttpHeader::g_contentLength ),
        utils::lexical_cast< std::string >( content.size() )
        );
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
         * @brief Waits until the server closes the connection and returns true if it did
         */

        bool waitUntilClosed( SAA_in const bl::time::time_duration& timeout )
        {
            const auto started = bl::time::microsec_clock::universal_time();

            for( ;; )
            {
                bl::eh::error_code ec;

                char buffer[ 64 ];

                const auto size = m_socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                BL_UNUSED( size );

                if( ec )
                {
                    return true;
                }

                if( ( bl::time::microsec_clock::universal_time() - started ) > timeout )
                {
                    return false;
                }
            }
        }

        bool isOpen() const NOEXCEPT
        {
            return m_socket.is_open();
        }
    };

} // __unnamed

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
        acceptor
        );
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

