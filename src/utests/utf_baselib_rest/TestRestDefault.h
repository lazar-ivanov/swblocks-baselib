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

#include <utests/baselib/TestRestUtils.h>

/************************************************************************
 * RestUtils::updateHttpStatusFromException and the two error formatters
 *
 * A pure static class over std::exception& - no broker, no server, no network. The seven
 * arm mapping is what turns a broker error into the status a client acts on, and merging
 * two arms (EACCES / 401 and EPERM / 403 are one keystroke apart), dropping the
 * generic_category() guard or dropping the errinfo_errno fallback would otherwise pass the
 * entire suite
 */

UTF_AUTO_TEST_CASE( RestUtils_HttpStatusMappingTests )
{
    using namespace bl;
    using namespace bl::messaging;
    using namespace bl::rest;

    /*
     * This mirrors the catch( std::exception& ) in BaseRestServerProcessingContext::processingImpl
     */

    const auto mapStatus = [](
        SAA_in          const std::exception_ptr&                       eptr,
        SAA_in          const http::Parameters::HttpStatusCode          defaultStatus
        )
        -> http::Parameters::HttpStatusCode
    {
        auto status = defaultStatus;

        try
        {
            cpp::safeRethrowException( eptr );
        }
        catch( std::exception& e )
        {
            RestUtils::updateHttpStatusFromException( e, status );
        }

        return status;
    };

    const auto systemErrorEptr = []( SAA_in const eh::error_code& ec ) -> std::exception_ptr
    {
        return std::make_exception_ptr(
            SystemException::create( ec, BL_SYSTEM_ERROR_DEFAULT_MSG )
            );
    };

    const auto badRequest = http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST;

    const auto permissionDeniedEptr =
        systemErrorEptr( eh::errc::make_error_code( eh::errc::permission_denied ) );

    /*
     * The two broker codes are built from the BrokerErrorCodes constants, which are
     * hard-coded to 99 and 105 rather than taken from the platform enumerators
     */

    const auto targetPeerNotFoundEptr = systemErrorEptr(
        eh::error_code( BrokerErrorCodes::TargetPeerNotFound, eh::generic_category() )
        );

    const auto targetPeerQueueFullEptr = systemErrorEptr(
        eh::error_code( BrokerErrorCodes::TargetPeerQueueFull, eh::generic_category() )
        );

    UTF_REQUIRE_EQUAL(
        mapStatus( permissionDeniedEptr, badRequest ),
        http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED
        );

    UTF_REQUIRE_EQUAL(
        mapStatus( targetPeerNotFoundEptr, badRequest ),
        http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE
        );

    UTF_REQUIRE_EQUAL(
        mapStatus( targetPeerQueueFullEptr, badRequest ),
        http::Parameters::HTTP_SERVER_ERROR_INTERNAL
        );

    UTF_REQUIRE_EQUAL(
        mapStatus(
            systemErrorEptr( eh::errc::make_error_code( eh::errc::invalid_argument ) ),
            badRequest
            ),
        http::Parameters::HTTP_SERVER_ERROR_INTERNAL
        );

    UTF_REQUIRE_EQUAL(
        mapStatus(
            systemErrorEptr( eh::errc::make_error_code( eh::errc::no_such_file_or_directory ) ),
            badRequest
            ),
        http::Parameters::HTTP_CLIENT_ERROR_NOT_FOUND
        );

    UTF_REQUIRE_EQUAL(
        mapStatus(
            systemErrorEptr( eh::errc::make_error_code( eh::errc::operation_not_supported ) ),
            badRequest
            ),
        http::Parameters::HTTP_SERVER_ERROR_NOT_IMPLEMENTED
        );

    /*
     * EACCES and EPERM must produce different statuses - asserted next to each other so a
     * merge of the two arms fails right here
     */

    UTF_REQUIRE_EQUAL(
        mapStatus(
            systemErrorEptr( eh::errc::make_error_code( eh::errc::operation_not_permitted ) ),
            badRequest
            ),
        http::Parameters::HTTP_CLIENT_ERROR_FORBIDDEN
        );

    UTF_REQUIRE(
        mapStatus( permissionDeniedEptr, badRequest ) !=
            mapStatus(
                systemErrorEptr( eh::errc::make_error_code( eh::errc::operation_not_permitted ) ),
                badRequest
                )
        );

    /*
     * SystemException::create attaches both errinfo_errno and errinfo_error_code for a
     * generic code, so only a hand built exception isolates the errno fallback
     */

    {
        const auto errNoOnlyEptr = std::make_exception_ptr(
            eh::enable_current_exception( eh::enable_error_info( UnexpectedException() ) )
                << eh::errinfo_errno(
                    eh::errc::make_error_code( eh::errc::no_such_file_or_directory ).value()
                    )
            );

        UTF_REQUIRE_EQUAL(
            mapStatus( errNoOnlyEptr, badRequest ),
            http::Parameters::HTTP_CLIENT_ERROR_NOT_FOUND
            );
    }

    /*
     * A code whose category is not the generic one is left alone entirely - without that
     * guard a gateway timeout (asio::error::operation_aborted, system category) would be
     * re-mapped by whatever arm its numeric value happens to hit
     *
     * Asserted with two different defaults so that 'unchanged' is not accidentally true
     */

    {
        const auto abortedEptr = std::make_exception_ptr(
            SystemException::create( asio::error::operation_aborted, BL_SYSTEM_ERROR_DEFAULT_MSG )
            );

        UTF_REQUIRE_EQUAL(
            mapStatus( abortedEptr, http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT ),
            http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT
            );

        UTF_REQUIRE_EQUAL( mapStatus( abortedEptr, badRequest ), badRequest );
    }

    /*
     * ... and so is an exception which carries no error information at all
     */

    {
        const auto plainEptr =
            std::make_exception_ptr( BL_EXCEPTION( UnexpectedException(), "plain" ) );

        UTF_REQUIRE_EQUAL(
            mapStatus( plainEptr, http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT ),
            http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT
            );

        UTF_REQUIRE_EQUAL( mapStatus( plainEptr, badRequest ), badRequest );
    }

    /*
     * Both formatters bind the status by reference into the serializer's exception
     * callback, so serializing the error is what upgrades the status of the response
     */

    {
        const auto response =
            RestUtils::formatEhResponseSimpleJson( badRequest, permissionDeniedEptr );

        UTF_REQUIRE( response );

        UTF_REQUIRE_EQUAL(
            response -> status(),
            http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED
            );

        UTF_REQUIRE_EQUAL(
            response -> headers().at( http::HttpHeader::g_contentType ),
            http::HttpHeader::g_contentTypeJsonUtf8
            );

        const auto errorJson =
            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response -> content() );

        UTF_REQUIRE( errorJson );
        UTF_REQUIRE( errorJson -> result() );

        UTF_REQUIRE_EQUAL(
            errorJson -> result() -> exceptionType(),
            std::string( "bl::SystemException" )
            );

        /*
         * The response of a gateway goes to a client which may be untrusted, so the document
         * the default entry point produces is the redacted one
         */

        UTF_REQUIRE_EQUAL(
            errorJson -> result() -> exceptionFullDump(),
            std::string( "<redacted>" )
            );
    }

    /*
     * The explicit opt-out - an internal deployment which scrapes the full diagnostics over
     * HTTP keeps the unredacted document, and this is also the positive control which stops
     * the assertion above from passing against a formatter that produced no dump at all
     */

    {
        const auto response =
            RestUtils::formatEhResponseSimpleJsonUnredacted( badRequest, permissionDeniedEptr );

        UTF_REQUIRE( response );

        const auto errorJson =
            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response -> content() );

        UTF_REQUIRE( errorJson );
        UTF_REQUIRE( errorJson -> result() );

        UTF_REQUIRE( errorJson -> result() -> exceptionFullDump() != "<redacted>" );
        UTF_REQUIRE( ! errorJson -> result() -> exceptionFullDump().empty() );

        UTF_REQUIRE(
            errorJson -> result() -> exceptionMessage() != errorJson -> result() -> message()
            );
    }

    /*
     * The GraphQL formatter has no caller under test at all, and neither has the
     * BrokerErrorCodes::tryGetExpectedErrorMessage it renders the message with
     */

    {
        const auto response =
            RestUtils::formatEhResponseGraphQL( badRequest, targetPeerNotFoundEptr );

        UTF_REQUIRE( response );

        UTF_REQUIRE_EQUAL(
            response -> status(),
            http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE
            );

        const auto errorGraphQL =
            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorGraphQL >( response -> content() );

        UTF_REQUIRE( errorGraphQL );
        UTF_REQUIRE_EQUAL( errorGraphQL -> errors().size(), 1U );

        const auto& error = errorGraphQL -> errors().at( 0 );

        UTF_REQUIRE( error );

        UTF_REQUIRE_EQUAL( error -> errorType(), std::string( "bl::SystemException" ) );

        UTF_REQUIRE( cpp::contains( error -> message(), "The server is currently unavailable" ) );
        UTF_REQUIRE( cpp::contains( error -> message(), "(error code 99)" ) );
    }
}

UTF_AUTO_TEST_CASE( RestServiceSslBackendTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        std::unordered_set< std::string > tokenCookieNames;
        tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

        /*
         * The expected security id is passed upper cased here, which makes this case a
         * free positive control for the case folding the gateway does on both sides of
         * the inbound security id comparison - the exact case match stays covered by
         * RestServiceSslBackendAssortedTests and RestServiceSslBackendPerfTests
         */

        utest::TestRestUtils::httpRestWithMessagingBackendTests(
            cpp::void_callback_t()                                          /* callback */,
            false                                                           /* waitOnServer */,
            false                                                           /* isQuietMode */,
            1U                                                              /* requestsCount */,
            uuids::create()                                                 /* gatewayPeerId */,
            uuids::create()                                                 /* serverPeerId */,
            om::copy( controlToken )                                        /* controlToken */,
            test::UtfArgsParser::host()                                     /* brokerHostName */,
            test::UtfArgsParser::port()                                     /* brokerInboundPort */,
            test::UtfArgsParser::connections()                              /* noOfConnections */,
            str::to_upper_copy( utest::DummyAuthorizationCache::dummySid() )
                                                                            /* expectedSecurityId */,
            std::move( tokenCookieNames )                                   /* tokenCookieNames */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}

UTF_AUTO_TEST_CASE( RestServiceSslBackendAssortedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto gatewayPeerId = uuids::create();
    const auto serverPeerId = uuids::create();

    /*
     * The fixture assigns a live view of the echo server's processed messages counter
     * before it invokes the callback below, which is the only way to sample it around
     * individual requests from a custom callback
     */

    utest::TestRestUtils::messages_processed_callback_t messagesProcessed;

    const auto callbackTests = [ & ]() -> void
    {
        std::unordered_set< std::string > tokenCookieNames;
        tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

        const auto callback = [ & ]() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                    const os::port_t httpPort =
                        utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() /* brokerInboundPort */ );

                    const auto payload = dm::DataModelUtils::loadFromFile< Payload >(
                        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    auto payloadDataString = dm::DataModelUtils::getDocAsPackedJsonString( payload );

                    /*
                     * Simple JSON echo request response
                     */

                    {
                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            cpp::copy( payloadDataString )              /* content */
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );
                        UTF_REQUIRE_EQUAL( payloadDataString, response );
                    }

                    /*
                     * Get the request metadata and verify it is what is expected
                     */

                    {
                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            "/requestMetadata"                          /* urlPath */
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto brokerProtocol =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( response );

                        UTF_REQUIRE( brokerProtocol );

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n"
                                << "\nBroker protocol message (request metadata):\n\n"
                                << dm::DataModelUtils::getDocAsPrettyJsonString( brokerProtocol )
                                << "\n\n"
                            );

                        /*
                         * Verify that the broker protocol message object has the expected content
                         */

                        UTF_REQUIRE( ! brokerProtocol -> conversationId().empty() );
                        ( void ) uuids::string2uuid( brokerProtocol -> conversationId() );

                        UTF_REQUIRE( ! brokerProtocol -> messageId().empty() );
                        ( void ) uuids::string2uuid( brokerProtocol -> messageId() );

                        UTF_REQUIRE_EQUAL( brokerProtocol -> messageType(), std::string( "AsyncRpcDispatch" ) );

                        {
                            UTF_REQUIRE( ! brokerProtocol -> sourcePeerId().empty() );
                            const auto sourcePeerId = uuids::string2uuid( brokerProtocol -> sourcePeerId() );
                            UTF_REQUIRE_EQUAL( sourcePeerId, gatewayPeerId );

                            UTF_REQUIRE( ! brokerProtocol -> targetPeerId().empty() );
                            const auto targetPeerId = uuids::string2uuid( brokerProtocol -> targetPeerId() );
                            UTF_REQUIRE_EQUAL( targetPeerId, serverPeerId );
                        }

                        {
                            const auto& passThroughUserData = brokerProtocol -> passThroughUserData();
                            UTF_REQUIRE( passThroughUserData );

                            const auto payload =
                                dm::DataModelUtils::castTo< dm::http::HttpRequestMetadataPayload >(
                                    passThroughUserData
                                    );

                            const auto& requestMetadata = payload -> httpRequestMetadata();
                            UTF_REQUIRE( requestMetadata );

                            const auto& cookies = utest::DummyAuthorizationCache::dummyTokenData();

                            UTF_REQUIRE_EQUAL( requestMetadata -> method(), "GET" );
                            UTF_REQUIRE_EQUAL( requestMetadata -> urlPath(), "/requestMetadata" );

                            /*
                             * Note that the HTTP server parser normalizes the request header
                             * names to lower case, so the metadata forwarded to the backend
                             * carries them in lower case too
                             */

                            UTF_REQUIRE( ! requestMetadata -> headers().at( "host" ).empty() );
                            UTF_REQUIRE_EQUAL( requestMetadata -> headers().at( "accept" ), "*/*" );
                            UTF_REQUIRE_EQUAL( requestMetadata -> headers().at( "connection" ), "close" );
                            UTF_REQUIRE_EQUAL( requestMetadata -> headers().at( "cookie" ), cookies );
                        }

                        {
                            const auto& principalIdentityInfo = brokerProtocol -> principalIdentityInfo();
                            UTF_REQUIRE( principalIdentityInfo );

                            const auto& securityPrincipal = principalIdentityInfo -> securityPrincipal();
                            UTF_REQUIRE( securityPrincipal );
                            UTF_REQUIRE( nullptr == principalIdentityInfo -> authenticationToken() );


                            const auto& sid = utest::DummyAuthorizationCache::dummySid();

                            UTF_REQUIRE_EQUAL( securityPrincipal -> email(), "john.smith@host.com" );
                            UTF_REQUIRE_EQUAL( securityPrincipal -> familyName(), "Smith" );
                            UTF_REQUIRE_EQUAL( securityPrincipal -> givenName(), "John" );
                            UTF_REQUIRE_EQUAL( securityPrincipal -> sid(), sid );
                        }
                    }

                    /*
                     * The same request, but carrying a Content-Type header and no body at all
                     *
                     * The HTTP server normalises every request header name to lower case in
                     * ParserHelpers.h ( str::to_lower( name ) - a request smuggling and duplicate
                     * Content-Length defence ) and prepareMessageDataBlock copies
                     * request -> headers() verbatim into the request metadata, whereas
                     * EchoServerProcessingContext.h looks the header up by the exact key
                     * http::HttpHeader::g_contentType ( "Content-Type" ) - so through the gateway
                     * that find() never succeeds and the whole block it guards, including the arm
                     * which turns 'a content type but no content' into a 400, is unreachable
                     *
                     * This request is therefore a 200 and NOT a 400 - do not 'fix' the assertion
                     * below. Whether the echo server ought to do a case insensitive lookup is a
                     * product decision; pinning today's behaviour is what makes that decision
                     * visible rather than accidental, and what makes a future change to the
                     * server side normalisation fail loudly here instead of silently activating
                     * a dormant 400
                     */

                    {
                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            std::string()                               /* content */,
                            "/requestMetadata"                          /* urlPath */,
                            "GET"                                       /* action */
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto brokerProtocol =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( response );

                        UTF_REQUIRE( brokerProtocol );

                        const auto& passThroughUserData = brokerProtocol -> passThroughUserData();
                        UTF_REQUIRE( passThroughUserData );

                        const auto payload =
                            dm::DataModelUtils::castTo< dm::http::HttpRequestMetadataPayload >(
                                passThroughUserData
                                );

                        const auto& requestMetadata = payload -> httpRequestMetadata();
                        UTF_REQUIRE( requestMetadata );

                        /*
                         * The direct, positive statement of the normalisation contract
                         */

                        UTF_REQUIRE_EQUAL( requestMetadata -> headers().count( "content-type" ), 1U );

                        UTF_REQUIRE_EQUAL(
                            requestMetadata -> headers().count( http::HttpHeader::g_contentType ),
                            0U
                            );
                    }

                    /*
                     * Get the response metadata and verify it is what is expected
                     */

                    const auto testResponseMetadata = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          const std::string&                  expectedCookie
                        )
                        -> void
                    {
                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            cpp::copy( urlPath )                        /* urlPath */
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto brokerProtocol =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( response );

                        UTF_REQUIRE( brokerProtocol );

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n"
                                << "\nBroker protocol message (response metadata):\n\n"
                                << dm::DataModelUtils::getDocAsPrettyJsonString( brokerProtocol )
                                << "\n\n"
                            );

                        /*
                         * Verify that the broker protocol message object has the expected content
                         */

                        UTF_REQUIRE( ! brokerProtocol -> conversationId().empty() );
                        ( void ) uuids::string2uuid( brokerProtocol -> conversationId() );

                        UTF_REQUIRE( ! brokerProtocol -> messageId().empty() );
                        ( void ) uuids::string2uuid( brokerProtocol -> messageId() );

                        UTF_REQUIRE_EQUAL( brokerProtocol -> messageType(), std::string( "AsyncRpcDispatch" ) );

                        UTF_REQUIRE( brokerProtocol -> sourcePeerId().empty() );
                        UTF_REQUIRE( brokerProtocol -> targetPeerId().empty() );

                        {
                            const auto& passThroughUserData = brokerProtocol -> passThroughUserData();
                            UTF_REQUIRE( passThroughUserData );

                            const auto payload =
                                dm::DataModelUtils::castTo< dm::http::HttpResponseMetadataPayload >(
                                    passThroughUserData
                                    );

                            const auto& responseMetadata = payload -> httpResponseMetadata();
                            UTF_REQUIRE( responseMetadata );

                            UTF_REQUIRE_EQUAL(
                                static_cast< http::Parameters::HttpStatusCode >(
                                    responseMetadata -> httpStatusCode()
                                    ),
                                http::Parameters::HTTP_SUCCESS_OK
                                );

                            UTF_REQUIRE_EQUAL(
                                responseMetadata -> contentType(),
                                http::HttpHeader::g_contentTypeJsonUtf8
                                );

                            UTF_REQUIRE_EQUAL(
                                responseMetadata -> headers().at( "Set-Cookie" ),
                                expectedCookie
                                );
                        }

                        {
                            const auto& principalIdentityInfo = brokerProtocol -> principalIdentityInfo();
                            UTF_REQUIRE( principalIdentityInfo );

                            const auto& authenticationToken = principalIdentityInfo -> authenticationToken();
                            UTF_REQUIRE( authenticationToken );
                            UTF_REQUIRE( nullptr == principalIdentityInfo -> securityPrincipal() );

                            UTF_REQUIRE_EQUAL(
                                authenticationToken -> type(),
                                utest::DummyAuthorizationCache::dummyTokenType()
                                );

                            UTF_REQUIRE_EQUAL(
                                authenticationToken -> data(),
                                utest::DummyAuthorizationCache::dummyTokenData()
                                );
                        }

                        /*
                         * All of the above reads the broker protocol document which the
                         * echo server placed in the response body - i.e. the server's own
                         * bookkeeping. The assertions below are on the HTTP response the
                         * gateway actually put on the wire, which is what getHttpResponse
                         * projects out of the very same HttpResponseMetadata: the status
                         * code, the content type and every custom header
                         */

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        /*
                         * The client lower cases the response header names
                         */

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getResponseHeaders().at( "content-type" ),
                            http::HttpHeader::g_contentTypeJsonUtf8
                            );

                        /*
                         * The only assertion in the repository that the gateway projects a
                         * peer supplied response header onto the wire
                         *
                         * It is starts_with rather than equality because the client folds
                         * the Set-Cookie values into a single 'cookie' entry and appends
                         * HttpHeader::g_cookieSeparator to each of them, while the echo
                         * server's own value already ends in ';'
                         */

                        const auto cookie = taskImpl -> tryGetResponseHeader( http::HttpHeader::g_cookie );

                        UTF_REQUIRE( cookie );
                        UTF_REQUIRE( str::starts_with( *cookie, expectedCookie ) );
                    };

                    testResponseMetadata(
                        "/responseMetadata"                             /* urlPath */,
                        "responseCookieName=responseCookieValue;"       /* expectedCookie */
                        );

                    testResponseMetadata(
                        "/cookie/foo/bar/baz"                           /* urlPath */,
                        "responseCookieName=/cookie/foo/bar/baz;"       /* expectedCookie */
                        );

                    /*
                     * Get HTTP status error response and verify it is what is expected
                     */

                    const auto testErrorScenario = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          const unsigned int                  expectedHttpStatus
                        )
                        -> void
                    {
                        http::StatusesList expectedHttpStatuses;
                        expectedHttpStatuses.insert( expectedHttpStatus );

                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            cpp::copy( urlPath )                        /* urlPath */,
                            "GET"                                       /* action */,
                            std::string()                               /* tokenData */,
                            expectedHttpStatuses                        /* expectedHttpStatuses */
                            );

                        UTF_REQUIRE( taskImpl -> isFailed() );
                        UTF_REQUIRE( taskImpl -> exception() );

                        UTF_REQUIRE_EQUAL( taskImpl -> getHttpStatus(), expectedHttpStatus );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto errorJson =
                            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

                        UTF_REQUIRE( errorJson );

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n"
                                << "\nError as JSON response:\n\n"
                                << dm::DataModelUtils::getDocAsPrettyJsonString( errorJson )
                                << "\n\n"
                            );

                        UTF_REQUIRE( errorJson -> result() );

                        /*
                         * The body a REST server behind the gateway produces reaches the
                         * gateway's client verbatim, so it is redacted: the exception dump and
                         * the raw exception text of an error which is not user friendly are
                         * gone, and the friendly message is what is left to diagnose with
                         */

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionFullDump(),
                            std::string( "<redacted>" )
                            );

                        const auto ecExpected = eh::errc::make_error_code( eh::errc::bad_file_descriptor );

                        UTF_REQUIRE(
                            std::string::npos ==
                                errorJson -> result() -> exceptionMessage().find( ecExpected.message() )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionType(),
                            std::string( "bl::SystemException" )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> message(),
                            std::string( "An unexpected error has occurred" )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionMessage(),
                            errorJson -> result() -> message()
                            );

                        UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

                        const auto& exceptionProperties = errorJson -> result() -> exceptionProperties();

                        /*
                         * The properties which say what the failure WAS are kept - only the ones
                         * which disclose the internals of the server are removed
                         */

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> categoryName(),
                            std::string( ecExpected.category().name() )
                            );

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> errNo(),
                            ecExpected.value()
                            );

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> errorCode(),
                            ecExpected.value()
                            );

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> errorCodeMessage(),
                            ecExpected.message()
                            );

                        UTF_REQUIRE( exceptionProperties -> message().empty() );

                        UTF_REQUIRE( exceptionProperties -> functionName().empty() );
                        UTF_REQUIRE( exceptionProperties -> hostName().empty() );
                    };

                    {
                        testErrorScenario(
                            "/error/HTTP_CLIENT_ERROR_UNAUTHORIZED"                         /* urlPath */,
                            http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED                /* expectedHttpStatus */
                            );

                        testErrorScenario(
                            "/error/HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE"                  /* urlPath */,
                            http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE         /* expectedHttpStatus */
                            );

                        testErrorScenario(
                            "/error/HTTP_SERVER_ERROR_INTERNAL"                             /* urlPath */,
                            http::Parameters::HTTP_SERVER_ERROR_INTERNAL                    /* expectedHttpStatus */
                            );

                        testErrorScenario(
                            "/error/HTTP_CLIENT_ERROR_NOT_FOUND"                            /* urlPath */,
                            http::Parameters::HTTP_CLIENT_ERROR_NOT_FOUND                   /* expectedHttpStatus */
                            );

                        testErrorScenario(
                            "/error/HTTP_SERVER_ERROR_NOT_IMPLEMENTED"                      /* urlPath */,
                            http::Parameters::HTTP_SERVER_ERROR_NOT_IMPLEMENTED             /* expectedHttpStatus */
                            );

                        testErrorScenario(
                            "/error/HTTP_CLIENT_ERROR_FORBIDDEN"                            /* urlPath */,
                            http::Parameters::HTTP_CLIENT_ERROR_FORBIDDEN                   /* expectedHttpStatus */
                            );
                    }

                    /*
                     * Test the case where the broker authorization fails
                     */

                    {
                        http::StatusesList expectedHttpStatuses;
                        expectedHttpStatuses.insert( http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED );

                        std::string tokenData = utest::DummyAuthorizationCache::dummyTokenDataUnauthorized();

                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            "/foo/bar"                                  /* urlPath */,
                            "GET"                                       /* action */,
                            std::move( tokenData )                      /* tokenData */,
                            expectedHttpStatuses                        /* expectedHttpStatuses */
                            );

                        UTF_REQUIRE( taskImpl -> isFailed() );
                        UTF_REQUIRE( taskImpl -> exception() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto errorJson =
                            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

                        UTF_REQUIRE( errorJson );

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n"
                                << "\nError as JSON response:\n\n"
                                << dm::DataModelUtils::getDocAsPrettyJsonString( errorJson )
                                << "\n\n"
                            );

                        UTF_REQUIRE( errorJson -> result() );

                        /*
                         * The gateway's own error response is redacted too - this is the body
                         * an internet facing HttpSslServer puts on the wire
                         */

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionFullDump(),
                            std::string( "<redacted>" )
                            );

                        const auto ecExpected = eh::errc::make_error_code( eh::errc::permission_denied );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionMessage(),
                            std::string( "An unexpected error has occurred" )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionType(),
                            std::string( "bl::ServerErrorException" )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> message(),
                            std::string( "An unexpected error has occurred" )
                            );

                        UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

                        const auto& exceptionProperties = errorJson -> result() -> exceptionProperties();

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> errNo(),
                            ecExpected.value()
                            );

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> errorCode(),
                            ecExpected.value()
                            );

                        UTF_REQUIRE( exceptionProperties -> message().empty() );

                        /*
                         * The broker's authorization failure IS user friendly at source
                         * (AuthorizationServiceRest.h throws it with the user friendly
                         * flag on), but errinfo_is_user_friendly does not survive the
                         * block transfer wire, so it arrives here unset - which is exactly
                         * why the result message asserted above is the generic one rather
                         * than the server's own text
                         *
                         * This pair of assertions is what would have to change if the flag
                         * were ever made to cross the messaging boundary
                         */

                        UTF_REQUIRE( ! exceptionProperties -> isUserFriendlyIsSet() );
                    }

                    /*
                     * TestRestUtils::executeHttpRequest replaces an empty tokenData with
                     * defaultToken(), so every request it sends carries a fully valid
                     * cookie - a genuinely credential free request has to be pushed on the
                     * queue directly
                     */

                    const auto executeRawRequest = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          http::HeadersMap&&                  headers,
                        SAA_in          const std::string&                  content
                        )
                        -> om::ObjPtr< SimpleHttpSslTaskImpl >
                    {
                        auto taskImpl = SimpleHttpSslTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() )    /* host */,
                            httpPort,
                            urlPath                                     /* path */,
                            "GET"                                       /* action */,
                            content                                     /* content */,
                            BL_PARAM_FWD( headers )
                            );

                        const auto task = om::qi< Task >( taskImpl );

                        eq -> push_back( task );
                        eq -> wait( task );

                        return taskImpl;
                    };

                    /*
                     * Ensure genuinely anonymous requests (no credential at all) can be
                     * handled correctly
                     *
                     * /requestMetadata returns the broker protocol document the backend
                     * received, which is what makes the trust boundary assertion below
                     * expressible from the client side
                     */

                    {
                        /*
                         * No Cookie header at all - the missing header arm of the gateway's
                         * cookie filter
                         */

                        const auto taskImpl =
                            executeRawRequest( "/requestMetadata", http::HeadersMap(), std::string() );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        const auto seen =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( taskImpl -> getResponse() );

                        /*
                         * The backend really did see an unauthenticated message - the empty
                         * token default made createBrokerProtocolMessage omit the principal
                         * identity info altogether, and the broker neither invented a
                         * principal nor rejected the message
                         */

                        UTF_REQUIRE( nullptr == seen -> principalIdentityInfo() );

                        /*
                         * A Cookie header which matches nothing in the configured token
                         * cookie names - the filter arm rather than the missing header arm.
                         * The two must be indistinguishable to the backend
                         */

                        http::HeadersMap otherCookieHeaders;
                        otherCookieHeaders[ http::HttpHeader::g_cookie ] = "someOtherCookieName=whatever";

                        const auto taskImplOther = executeRawRequest(
                            "/requestMetadata",
                            std::move( otherCookieHeaders ),
                            std::string()
                            );

                        UTF_REQUIRE( ! taskImplOther -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImplOther -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        const auto seenOther =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( taskImplOther -> getResponse() );

                        UTF_REQUIRE( nullptr == seenOther -> principalIdentityInfo() );

                        /*
                         * The contrast - the very same request with the default token
                         */

                        http::HeadersMap tokenHeaders;
                        tokenHeaders[ http::HttpHeader::g_cookie ] = utest::TestRestUtils::defaultToken();

                        const auto taskImpl2 = executeRawRequest(
                            "/requestMetadata",
                            std::move( tokenHeaders ),
                            std::string()
                            );

                        UTF_REQUIRE( ! taskImpl2 -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl2 -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        const auto seen2 =
                            dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( taskImpl2 -> getResponse() );

                        UTF_REQUIRE( seen2 -> principalIdentityInfo() );
                        UTF_REQUIRE( seen2 -> principalIdentityInfo() -> securityPrincipal() );

                        /*
                         * The broker strips the token once it has resolved the principal
                         */

                        UTF_REQUIRE( nullptr == seen2 -> principalIdentityInfo() -> authenticationToken() );

                        UTF_REQUIRE_EQUAL(
                            str::to_lower_copy(
                                seen2 -> principalIdentityInfo() -> securityPrincipal() -> sid()
                                ),
                            utest::DummyAuthorizationCache::dummySid()
                            );
                    }

                    /*
                     * Ensure gateway and backend server health check requests work, without
                     * any credential at all, and that they differ in exactly one respect -
                     * /health never leaves the gateway while /backendhealth traverses the
                     * whole pipe and is therefore counted by the backend
                     *
                     * Note that the content type cannot discriminate the two, as the stock
                     * response uses g_contentTypeDefault which is the same string as
                     * g_contentTypeJsonUtf8
                     */

                    const auto testHealthCheck = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          const bool                          expectedToReachTheBackend,
                        SAA_in          http::HeadersMap&&                  headers,
                        SAA_in          const std::string&                  content
                        )
                        -> om::ObjPtr< SimpleHttpSslTaskImpl >
                    {
                        const auto messagesProcessedBefore = messagesProcessed();

                        auto taskImpl = executeRawRequest( urlPath, BL_PARAM_FWD( headers ), content );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        UTF_REQUIRE( ! taskImpl -> getResponse().empty() );

                        UTF_REQUIRE_EQUAL(
                            messagesProcessed() - messagesProcessedBefore,
                            expectedToReachTheBackend ? 1U : 0U
                            );

                        return taskImpl;
                    };

                    const auto stockHealthCheckResponse =
                        httpserver::Response::createInstance( http::Parameters::HTTP_SUCCESS_OK ) -> content();

                    {
                        const auto taskImpl = testHealthCheck(
                            "/health",
                            false                                       /* expectedToReachTheBackend */,
                            http::HeadersMap(),
                            std::string()                               /* content */
                            );

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), stockHealthCheckResponse );
                    }

                    testHealthCheck(
                        "/backendhealth",
                        true                                            /* expectedToReachTheBackend */,
                        http::HeadersMap(),
                        std::string()                                   /* content */
                        );

                    /*
                     * A health check which also carries a body and a non JSON content type
                     * is answered exactly the same way
                     *
                     * Note that both backend gates are disabled in this case, so the
                     * ordering claim - that the early return in validateRequestBrokerProtocol
                     * precedes the content type gate - is pinned by the text/plain arm of
                     * RestServiceSslBackendRequestValidationTests instead
                     */

                    {
                        http::HeadersMap plainTextHeaders;

                        plainTextHeaders[ http::HttpHeader::g_contentType ] =
                            http::HttpHeader::g_contentTypePlainTextUtf8;

                        testHealthCheck(
                            "/backendhealth",
                            true                                        /* expectedToReachTheBackend */,
                            std::move( plainTextHeaders ),
                            "this is not JSON"                          /* content */
                            );
                    }

                    /*
                     * Both URIs are compared with str::iequals, which is otherwise only
                     * ever fed exact case input
                     */

                    {
                        const auto taskImpl = testHealthCheck(
                            "/HEALTH",
                            false                                       /* expectedToReachTheBackend */,
                            http::HeadersMap(),
                            std::string()                               /* content */
                            );

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), stockHealthCheckResponse );
                    }

                    testHealthCheck(
                        "/BackendHealth",
                        true                                            /* expectedToReachTheBackend */,
                        http::HeadersMap(),
                        std::string()                                   /* content */
                        );
                }
                );
        };

        utest::TestRestUtils::httpRestWithMessagingBackendTests(
            callback                                                        /* callback */,
            false                                                           /* waitOnServer */,
            false                                                           /* isQuietMode */,
            1U                                                              /* requestsCount */,
            gatewayPeerId                                                   /* gatewayPeerId */,
            serverPeerId                                                    /* serverPeerId */,
            om::copy( controlToken )                                        /* controlToken */,
            test::UtfArgsParser::host()                                     /* brokerHostName */,
            test::UtfArgsParser::port()                                     /* brokerInboundPort */,
            test::UtfArgsParser::connections()                              /* noOfConnections */,
            cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
            std::move( tokenCookieNames )                                   /* tokenCookieNames */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
            std::string()                                                   /* tokenDataDefault */,
            utest::TestRestUtils::defaultToken()                            /* tokenData */,
            time::neg_infin                                                 /* requestTimeout */,
            false                                                           /* isAuthnticationAlwaysRequired */,
            std::string()                                                   /* requiredContentType */,
            false                                                           /* isGraphQLServer */,
            utest::TestRestUtils::format_eh_response_callback_t()           /* ehFormatCallback */,
            utest::TestRestUtils::server_context_factory_t()                /* serverContextFactory */,
            &messagesProcessed                                              /* messagesProcessedOut */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}


/************************************************************************
 * The backend's authentication and content type gates, and the gateway's cookie filter
 *
 * All three construction sites pass false / std::string() for the two gates, so
 * validateRequestBrokerProtocol only ever reaches its early return or falls straight
 * through and the catch block in processingImpl never runs. On the gateway side the cookie
 * filter has only ever seen one shape of input - a single element header which matches the
 * one configured token cookie name - and the filter is only observable at all through the
 * authentication gate this case arms
 */

UTF_AUTO_TEST_CASE( RestServiceSslBackendRequestValidationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto makeTokenCookieNames = []() -> std::unordered_set< std::string >
    {
        std::unordered_set< std::string > names;

        names.emplace( utest::DummyAuthorizationCache::dummyCookieName() );
        names.emplace( "secondToken" );

        return names;
    };

    /*
     * The error responses the backend produces are framed by processingImpl's catch block,
     * so they are loaded back as the data model the gateway forwarded
     */

    const auto loadErrorJson = []( SAA_in const std::string& response )
        -> om::ObjPtr< dm::ServerErrorJson >
    {
        UTF_REQUIRE( ! response.empty() );

        auto errorJson = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

        UTF_REQUIRE( errorJson );
        UTF_REQUIRE( errorJson -> result() );
        UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

        return errorJson;
    };

    /*
     * Run 1 - the token data default is empty, so a request whose cookies match none of the
     * configured token cookie names reaches the backend unauthenticated
     */

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto callbackTests = [ & ]() -> void
        {
            const auto callback = [ & ]() -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                        const os::port_t httpPort =
                            utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                        const auto payload = dm::DataModelUtils::loadFromFile< Payload >(
                            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                            );

                        const auto payloadDataString =
                            dm::DataModelUtils::getDocAsPackedJsonString( payload );

                        http::StatusesList badRequestStatuses;
                        badRequestStatuses.insert( http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST );

                        http::StatusesList unauthorizedStatuses;
                        unauthorizedStatuses.insert( http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED );

                        /*
                         * (1) The positive control - the echo still works when both gates are
                         * armed and passed
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                false                                       /* allowFailure */,
                                http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                                cpp::copy( payloadDataString )              /* content */,
                                "/foo/bar"                                  /* urlPath */
                                );

                            UTF_REQUIRE( ! taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_SUCCESS_OK
                                );

                            UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), payloadDataString );
                        }

                        /*
                         * (2) A missing Content-Type header is a BL_CHK_USER, i.e. a
                         * UserMessageException with no error code at all - which is precisely
                         * why the status stays 400
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                true                                        /* allowFailure */,
                                str::empty()                                /* contentType */,
                                std::string()                               /* content */,
                                "/foo/bar"                                  /* urlPath */,
                                "GET"                                       /* action */,
                                utest::TestRestUtils::defaultToken()        /* tokenData */,
                                badRequestStatuses                          /* expectedHttpStatuses */
                                );

                            UTF_REQUIRE( taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST
                                );

                            const auto errorJson = loadErrorJson( taskImpl -> getResponse() );

                            UTF_REQUIRE_EQUAL(
                                errorJson -> result() -> exceptionType(),
                                std::string( "bl::UserMessageException" )
                                );

                            UTF_REQUIRE(
                                ! errorJson -> result() -> exceptionProperties() -> errorCodeIsSet()
                                );

                            const auto& message = errorJson -> result() -> message();

                            UTF_REQUIRE( cpp::contains( message, "Content-Type" ) );

                            UTF_REQUIRE(
                                cpp::contains(
                                    message,
                                    "not found in messaging broker HTTP request metadata"
                                    )
                                );
                        }

                        /*
                         * (3) ... and so is a Content-Type which is not a JSON one
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                true                                        /* allowFailure */,
                                http::HttpHeader::g_contentTypePlainText    /* contentType */,
                                cpp::copy( payloadDataString )              /* content */,
                                "/foo/bar"                                  /* urlPath */,
                                "GET"                                       /* action */,
                                utest::TestRestUtils::defaultToken()        /* tokenData */,
                                badRequestStatuses                          /* expectedHttpStatuses */
                                );

                            UTF_REQUIRE( taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST
                                );

                            const auto errorJson = loadErrorJson( taskImpl -> getResponse() );

                            UTF_REQUIRE_EQUAL(
                                errorJson -> result() -> exceptionType(),
                                std::string( "bl::UserMessageException" )
                                );

                            UTF_REQUIRE(
                                ! errorJson -> result() -> exceptionProperties() -> errorCodeIsSet()
                                );

                            const auto& message = errorJson -> result() -> message();

                            UTF_REQUIRE( cpp::contains( message, "Content-Type" ) );
                            UTF_REQUIRE( cpp::contains( message, "Unsupported" ) );
                        }

                        /*
                         * (4) A cookie header which matches none of the configured token cookie
                         * names is filtered away entirely, and with an empty token data default
                         * the message reaches the backend with no principal at all
                         *
                         * The JSON content type is passed so that only the authentication gate
                         * can be the one which fires, and the status is 401 rather than 400
                         * because updateHttpStatusFromException maps the permission denied
                         * error code of the exception
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                true                                        /* allowFailure */,
                                http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                                cpp::copy( payloadDataString )              /* content */,
                                "/foo/bar"                                  /* urlPath */,
                                "GET"                                       /* action */,
                                "someOtherCookieName=whatever"              /* tokenData */,
                                unauthorizedStatuses                        /* expectedHttpStatuses */
                                );

                            UTF_REQUIRE( taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED
                                );

                            const auto errorJson = loadErrorJson( taskImpl -> getResponse() );

                            UTF_REQUIRE_EQUAL(
                                errorJson -> result() -> exceptionType(),
                                std::string( "bl::SystemException" )
                                );

                            UTF_REQUIRE(
                                errorJson -> result() -> exceptionProperties() -> errorCodeIsSet()
                                );

                            UTF_REQUIRE_EQUAL(
                                errorJson -> result() -> exceptionProperties() -> errorCode(),
                                eh::errc::make_error_code( eh::errc::permission_denied ).value()
                                );

                            UTF_REQUIRE(
                                cpp::contains(
                                    errorJson -> result() -> message(),
                                    "Authentication information is required for all requests"
                                    )
                                );
                        }

                        /*
                         * (5) and (6) Noise elements are dropped by the filter while the real
                         * token survives, so the request is still authorised - a leading '='
                         * disqualifies an element (the pos != 0U guard) and an element with no
                         * '=' at all only survives if the whole element is a configured name
                         */

                        const auto testFilteredCookies = [ & ](
                            SAA_in          const std::string&              tokenData
                            )
                            -> void
                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                false                                       /* allowFailure */,
                                http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                                cpp::copy( payloadDataString )              /* content */,
                                "/requestMetadata"                          /* urlPath */,
                                "GET"                                       /* action */,
                                cpp::copy( tokenData )                      /* tokenData */
                                );

                            UTF_REQUIRE( ! taskImpl -> isFailed() );

                            const auto& response = taskImpl -> getResponse();

                            UTF_REQUIRE( ! response.empty() );

                            const auto brokerProtocol =
                                dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( response );

                            UTF_REQUIRE( brokerProtocol );

                            const auto& principalIdentityInfo =
                                brokerProtocol -> principalIdentityInfo();

                            UTF_REQUIRE( principalIdentityInfo );
                            UTF_REQUIRE( principalIdentityInfo -> securityPrincipal() );

                            UTF_REQUIRE_EQUAL(
                                principalIdentityInfo -> securityPrincipal() -> sid(),
                                utest::DummyAuthorizationCache::dummySid()
                                );

                            /*
                             * The raw Cookie header is forwarded verbatim in the request
                             * metadata, which is a separate contract from the token
                             */

                            const auto& passThroughUserData =
                                brokerProtocol -> passThroughUserData();

                            UTF_REQUIRE( passThroughUserData );

                            const auto metadataPayload =
                                dm::DataModelUtils::castTo< dm::http::HttpRequestMetadataPayload >(
                                    passThroughUserData
                                    );

                            const auto& requestMetadata = metadataPayload -> httpRequestMetadata();

                            UTF_REQUIRE( requestMetadata );

                            UTF_REQUIRE_EQUAL(
                                requestMetadata -> headers().at( "cookie" ),
                                tokenData
                                );
                        };

                        testFilteredCookies( "=authorized; dummyCookieName=authorized" );
                        testFilteredCookies( "justnoise; dummyCookieName=authorized" );

                        /*
                         * (7) The health check URI bypasses both gates - its early return in
                         * validateRequestBrokerProtocol is before either of them
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                true                                        /* allowFailure */,
                                str::empty()                                /* contentType */,
                                std::string()                               /* content */,
                                "/backendhealth"                            /* urlPath */,
                                "GET"                                       /* action */,
                                "someOtherCookieName=whatever"              /* tokenData */
                                );

                            UTF_REQUIRE( ! taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_SUCCESS_OK
                                );
                        }

                        /*
                         * The same URI with a body and a content type which the armed
                         * content type gate would otherwise reject outright - this is what
                         * makes the ordering claim above a claim about the content type
                         * gate and not only about the authentication one
                         */

                        {
                            const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                                eq,
                                httpPort,
                                true                                            /* allowFailure */,
                                http::HttpHeader::g_contentTypePlainTextUtf8    /* contentType */,
                                std::string( "this is not JSON" )               /* content */,
                                "/backendhealth"                                /* urlPath */,
                                "GET"                                           /* action */,
                                "someOtherCookieName=whatever"                  /* tokenData */
                                );

                            UTF_REQUIRE( ! taskImpl -> isFailed() );

                            UTF_REQUIRE_EQUAL(
                                taskImpl -> getHttpStatus(),
                                http::Parameters::HTTP_SUCCESS_OK
                                );
                        }
                    }
                    );
            };

            utest::TestRestUtils::httpRestWithMessagingBackendTests(
                callback                                                        /* callback */,
                false                                                           /* waitOnServer */,
                false                                                           /* isQuietMode */,
                1U                                                              /* requestsCount */,
                uuids::create()                                                 /* gatewayPeerId */,
                uuids::create()                                                 /* serverPeerId */,
                om::copy( controlToken )                                        /* controlToken */,
                test::UtfArgsParser::host()                                     /* brokerHostName */,
                test::UtfArgsParser::port()                                     /* brokerInboundPort */,
                test::UtfArgsParser::connections()                              /* noOfConnections */,
                cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
                makeTokenCookieNames()                                          /* tokenCookieNames */,
                cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
                std::string()                                                   /* tokenDataDefault */,
                utest::TestRestUtils::defaultToken()                            /* tokenData */,
                time::neg_infin                                                 /* requestTimeout */,
                true                                                            /* isAuthnticationAlwaysRequired */,
                cpp::copy( http::HttpHeader::g_contentTypeJson )                /* requiredContentType */
                );
        };

        utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
    }

    /*
     * Run 2 - the same configuration except that the token data default is non-empty, so the
     * no-match request is authenticated after all
     *
     * Together with (4) above, where the default was empty and the same shape of request was
     * rejected with 401, this pair is what makes the m_tokenDataDefault branch observable
     */

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto callbackTests = [ & ]() -> void
        {
            const auto callback = [ & ]() -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                        const os::port_t httpPort =
                            utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                        const auto payload = dm::DataModelUtils::loadFromFile< Payload >(
                            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                            );

                        const auto payloadDataString =
                            dm::DataModelUtils::getDocAsPackedJsonString( payload );

                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            cpp::copy( payloadDataString )              /* content */,
                            "/foo/bar"                                  /* urlPath */,
                            "GET"                                       /* action */,
                            "onlyNoise=1"                               /* tokenData */
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );
                    }
                    );
            };

            utest::TestRestUtils::httpRestWithMessagingBackendTests(
                callback                                                        /* callback */,
                false                                                           /* waitOnServer */,
                false                                                           /* isQuietMode */,
                1U                                                              /* requestsCount */,
                uuids::create()                                                 /* gatewayPeerId */,
                uuids::create()                                                 /* serverPeerId */,
                om::copy( controlToken )                                        /* controlToken */,
                test::UtfArgsParser::host()                                     /* brokerHostName */,
                test::UtfArgsParser::port()                                     /* brokerInboundPort */,
                test::UtfArgsParser::connections()                              /* noOfConnections */,
                cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
                makeTokenCookieNames()                                          /* tokenCookieNames */,
                cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
                cpp::copy( utest::DummyAuthorizationCache::dummyTokenData() )   /* tokenDataDefault */,
                utest::TestRestUtils::defaultToken()                            /* tokenData */,
                time::neg_infin                                                 /* requestTimeout */,
                true                                                            /* isAuthnticationAlwaysRequired */,
                cpp::copy( http::HttpHeader::g_contentTypeJson )                /* requiredContentType */
                );
        };

        utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
    }
}

/************************************************************************
 * The gateway's two inbound message drop arms, the request timeout prune path and the
 * pluggable error formatter
 *
 * The two 'return' statements in processIncomingMessage are the gateway's entire inbound
 * trust boundary - without them any peer on the broker can answer any conversation the
 * gateway is waiting on - and every existing case only ever exercises their positive half.
 * A dropped response leaves the conversation in flight until the prune timer hands it to
 * cancelRequestsNoThrow, which is what turns it into a clean 504 rather than a connection
 * which hangs until the HTTP server's own inactivity timeout
 */

UTF_AUTO_TEST_CASE( RestServiceSslGatewayInboundFilterTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * Issues one request which is expected to time out, and returns its response body
     */

    const auto runTimingOutRequest = []() -> std::string
    {
        std::string response;

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                const os::port_t httpPort =
                    utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                const auto payload = dm::DataModelUtils::loadFromFile< Payload >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                const auto payloadDataString =
                    dm::DataModelUtils::getDocAsPackedJsonString( payload );

                http::StatusesList gatewayTimeoutStatuses;
                gatewayTimeoutStatuses.insert( http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT );

                const auto timeBefore = time::microsec_clock::universal_time();

                const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                    eq,
                    httpPort,
                    true                                        /* allowFailure */,
                    http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                    cpp::copy( payloadDataString )              /* content */,
                    "/foo/bar"                                  /* urlPath */,
                    "GET"                                       /* action */,
                    utest::TestRestUtils::defaultToken()        /* tokenData */,
                    gatewayTimeoutStatuses                      /* expectedHttpStatuses */
                    );

                const auto elapsed = time::microsec_clock::universal_time() - timeBefore;

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( taskImpl -> exception() );

                UTF_REQUIRE_EQUAL(
                    taskImpl -> getHttpStatus(),
                    http::Parameters::HTTP_SERVER_ERROR_GATEWAY_TIMEOUT
                    );

                UTF_REQUIRE( ! taskImpl -> getResponse().empty() );

                /*
                 * This is what distinguishes 'the prune timer cancelled it in 3 to 8 s' from
                 * 'the connection died of old age after 60 s', which would also report 504
                 */

                UTF_REQUIRE( elapsed < time::seconds( 30L ) );

                response = taskImpl -> getResponse();
            }
            );

        return response;
    };

    /*
     * Run 1 - the security id of the response does not match the configured one, so the
     * gateway drops the reply at the second arm; the GraphQL formatter is supplied here so
     * that the shipped, command line selectable output format is produced by a running
     * gateway for the first time
     */

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        std::string response;

        const auto callbackTests = [ & ]() -> void
        {
            std::unordered_set< std::string > tokenCookieNames;
            tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

            const auto callback = [ & ]() -> void
            {
                response = runTimingOutRequest();
            };

            utest::TestRestUtils::httpRestWithMessagingBackendTests(
                callback                                                        /* callback */,
                false                                                           /* waitOnServer */,
                false                                                           /* isQuietMode */,
                1U                                                              /* requestsCount */,
                uuids::create()                                                 /* gatewayPeerId */,
                uuids::create()                                                 /* serverPeerId */,
                om::copy( controlToken )                                        /* controlToken */,
                test::UtfArgsParser::host()                                     /* brokerHostName */,
                test::UtfArgsParser::port()                                     /* brokerInboundPort */,
                test::UtfArgsParser::connections()                              /* noOfConnections */,
                std::string( "some-other-sid" )                                 /* expectedSecurityId */,
                std::move( tokenCookieNames )                                   /* tokenCookieNames */,
                cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
                std::string()                                                   /* tokenDataDefault */,
                utest::TestRestUtils::defaultToken()                            /* tokenData */,
                time::seconds( 3L )                                             /* requestTimeout */,
                false                                                           /* isAuthnticationAlwaysRequired */,
                std::string()                                                   /* requiredContentType */,
                false                                                           /* isGraphQLServer */,
                utest::TestRestUtils::format_eh_response_callback_t(
                    &rest::RestUtils::formatEhResponseGraphQL
                    )                                                           /* ehFormatCallback */
                );
        };

        utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );

        UTF_REQUIRE( ! response.empty() );

        const auto errorGraphQL =
            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorGraphQL >( response );

        UTF_REQUIRE( errorGraphQL );
        UTF_REQUIRE_EQUAL( errorGraphQL -> errors().size(), 1U );

        UTF_REQUIRE_EQUAL(
            errorGraphQL -> errors().at( 0 ) -> errorType(),
            std::string( "bl::SystemException" )
            );

        /*
         * ... and the very same body must not be a simple JSON server error, otherwise the
         * assertion above could pass with the default formatter still in effect
         */

        bool loadedAsSimpleJson = false;

        try
        {
            const auto errorJson =
                dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

            loadedAsSimpleJson = ( nullptr != errorJson -> result() );
        }
        catch( std::exception& )
        {
            loadedAsSimpleJson = false;
        }

        UTF_REQUIRE( ! loadedAsSimpleJson );
    }

    /*
     * Run 2 - the echo context is given no token data at all, so the response carries no
     * principal identity information and the gateway drops it at the first arm; no error
     * formatter is supplied here, which keeps the default formatter arm covered
     */

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        std::string response;

        const auto callbackTests = [ & ]() -> void
        {
            std::unordered_set< std::string > tokenCookieNames;
            tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

            const auto callback = [ & ]() -> void
            {
                response = runTimingOutRequest();
            };

            utest::TestRestUtils::httpRestWithMessagingBackendTests(
                callback                                                        /* callback */,
                false                                                           /* waitOnServer */,
                false                                                           /* isQuietMode */,
                1U                                                              /* requestsCount */,
                uuids::create()                                                 /* gatewayPeerId */,
                uuids::create()                                                 /* serverPeerId */,
                om::copy( controlToken )                                        /* controlToken */,
                test::UtfArgsParser::host()                                     /* brokerHostName */,
                test::UtfArgsParser::port()                                     /* brokerInboundPort */,
                test::UtfArgsParser::connections()                              /* noOfConnections */,
                cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
                std::move( tokenCookieNames )                                   /* tokenCookieNames */,
                cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
                std::string()                                                   /* tokenDataDefault */,
                std::string()                                                   /* tokenData */,
                time::seconds( 3L )                                             /* requestTimeout */
                );
        };

        utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );

        UTF_REQUIRE( ! response.empty() );

        const auto errorJson =
            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

        UTF_REQUIRE( errorJson );
        UTF_REQUIRE( errorJson -> result() );

        /*
         * The type rather than the platform's ECANCELED number, so the case stays portable
         */

        UTF_REQUIRE_EQUAL(
            errorJson -> result() -> exceptionType(),
            std::string( "bl::SystemException" )
            );
    }
}

/************************************************************************
 * GraphQL vs simple JSON error rendering, at the HTTP level
 *
 * --graphql-error-formatting is a one flag switch on a shipped binary which changes the
 * disclosure level, the message text and the visibility of the error code for every error
 * response the gateway produces
 *
 * RestServiceSslGatewayInboundFilterTests already runs formatEhResponseGraphQL inside a
 * server, but only for a response the gateway itself dropped, where the status is the
 * default the caller passed in. This case drives the same renderer through a broker
 * authorization failure instead, which is the path where the exception callback the
 * renderer binds by reference has to UPGRADE the status - the by reference
 * cpp::ref( httpStatusCodeActual ) binding of formatEhResponseGraphQL is exercised
 * nowhere else - and it is the only place where the two renderers are compared on one and
 * the same input
 */

UTF_AUTO_TEST_CASE( RestServiceSslGatewayGraphQLErrorRenderingTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        std::unordered_set< std::string > tokenCookieNames;
        tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

        const auto callback = [ & ]() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                    const os::port_t httpPort =
                        utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() /* brokerInboundPort */ );

                    /*
                     * The very same unauthorized token request which
                     * RestServiceSslBackendAssortedTests issues against the simple JSON
                     * renderer - it is known to drive getStdErrorResponse with a
                     * permission_denied ServerErrorException
                     */

                    {
                        http::StatusesList expectedHttpStatuses;
                        expectedHttpStatuses.insert( http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED );

                        std::string tokenData = utest::DummyAuthorizationCache::dummyTokenDataUnauthorized();

                        auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            "/foo/bar"                                  /* urlPath */,
                            "GET"                                       /* action */,
                            std::move( tokenData )                      /* tokenData */,
                            expectedHttpStatuses                        /* expectedHttpStatuses */
                            );

                        UTF_REQUIRE( taskImpl -> isFailed() );
                        UTF_REQUIRE( taskImpl -> exception() );

                        /*
                         * Status parity with the simple JSON case: the status the client
                         * sees is 401 and not the 500 the gateway started from, i.e. the
                         * by reference status binding works through this renderer too
                         */

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED
                            );

                        UTF_REQUIRE_EQUAL( 1U, taskImpl -> getResponseHeaders().count( "content-type" ) );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getResponseHeaders().at( "content-type" ),
                            http::HttpHeader::g_contentTypeJsonUtf8
                            );

                        const auto& response = taskImpl -> getResponse();

                        UTF_REQUIRE( ! response.empty() );

                        const auto errorGraphQL =
                            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorGraphQL >( response );

                        UTF_REQUIRE( errorGraphQL );

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n"
                                << "\nError as GraphQL response:\n\n"
                                << dm::DataModelUtils::getDocAsPrettyJsonString( errorGraphQL )
                                << "\n\n"
                            );

                        UTF_REQUIRE_EQUAL( 1U, errorGraphQL -> errors().size() );

                        const auto& error = errorGraphQL -> errors().at( 0 );

                        UTF_REQUIRE( error );

                        UTF_REQUIRE_EQUAL(
                            error -> errorType(),
                            std::string( "bl::ServerErrorException" )
                            );

                        const auto ecExpected = eh::errc::make_error_code( eh::errc::permission_denied );

                        /*
                         * The message is split into two platform independent halves - the
                         * text comes from BrokerErrorCodes::tryGetExpectedErrorMessage and
                         * the suffix from the non zero error code
                         */

                        const auto& message = error -> message();

                        UTF_REQUIRE_EQUAL( 0U, message.find( ecExpected.message() ) );
                        UTF_REQUIRE( std::string::npos != message.find( "(error code " ) );

                        /*
                         * Disclosure parity - the assertion only a cross renderer test can
                         * make. RestServiceSslBackendAssortedTests requires the simple JSON
                         * body of this very same failure to carry a NON EMPTY
                         * exceptionFullDump(), i.e. the whole eh::diagnostic_information
                         * dump with the source file, the function, the host name and the
                         * task information; none of it may appear here
                         */

                        UTF_REQUIRE( std::string::npos == response.find( "exceptionFullDump" ) );
                        UTF_REQUIRE( std::string::npos == response.find( "exceptionMessage" ) );
                        UTF_REQUIRE( std::string::npos == response.find( net::getShortHostName() ) );
                        UTF_REQUIRE( std::string::npos == response.find( "function_name" ) );

                        /*
                         * The two envelopes are not interchangeable
                         *
                         * Note that loading a GraphQL body as a ServerErrorJson does NOT
                         * throw: 'result' is an optional complex property, so it simply
                         * stays null and the 'errors' array lands in the object's unmapped
                         * property bag. What makes them non interchangeable is that a
                         * client reading this body through the simple JSON envelope gets
                         * no error information at all
                         */

                        bool loadedAsSimpleJson = false;

                        try
                        {
                            const auto errorJson =
                                dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

                            loadedAsSimpleJson = ( nullptr != errorJson -> result() );
                        }
                        catch( std::exception& )
                        {
                            loadedAsSimpleJson = false;
                        }

                        UTF_REQUIRE( ! loadedAsSimpleJson );
                    }

                    /*
                     * The renderer is only on the error path - a success response is
                     * untouched by it
                     */

                    {
                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            "/backendhealth"                            /* urlPath */
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        const auto stockHealthCheckResponse =
                            httpserver::Response::createInstance( http::Parameters::HTTP_SUCCESS_OK ) -> content();

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), stockHealthCheckResponse );
                    }
                }
                );
        };

        utest::TestRestUtils::httpRestWithMessagingBackendTests(
            callback                                                        /* callback */,
            false                                                           /* waitOnServer */,
            false                                                           /* isQuietMode */,
            1U                                                              /* requestsCount */,
            uuids::create()                                                 /* gatewayPeerId */,
            uuids::create()                                                 /* serverPeerId */,
            om::copy( controlToken )                                        /* controlToken */,
            test::UtfArgsParser::host()                                     /* brokerHostName */,
            test::UtfArgsParser::port()                                     /* brokerInboundPort */,
            test::UtfArgsParser::connections()                              /* noOfConnections */,
            cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
            std::move( tokenCookieNames )                                   /* tokenCookieNames */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
            std::string()                                                   /* tokenDataDefault */,
            utest::TestRestUtils::defaultToken()                            /* tokenData */,
            time::neg_infin                                                 /* requestTimeout */,
            false                                                           /* isAuthnticationAlwaysRequired */,
            std::string()                                                   /* requiredContentType */,
            false                                                           /* isGraphQLServer */,
            utest::TestRestUtils::format_eh_response_callback_t(
                &rest::RestUtils::formatEhResponseGraphQL
                )                                                           /* ehFormatCallback */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}
