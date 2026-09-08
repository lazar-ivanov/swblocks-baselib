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
                        UTF_REQUIRE( ! errorJson -> result() -> exceptionFullDump().empty() );

                        const auto ecExpected = eh::errc::make_error_code( eh::errc::bad_file_descriptor );

                        const auto expectedPrefix = std::string( "System error has occurred: " ) + ecExpected.message();

                        UTF_REQUIRE( errorJson -> result() -> exceptionMessage().size() >= expectedPrefix.size() );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionMessage().substr( 0, expectedPrefix.size() ),
                            expectedPrefix
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionType(),
                            std::string( "bl::SystemException" )
                            );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> message(),
                            std::string( "An unexpected error has occurred" )
                            );

                        UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

                        const auto& exceptionProperties = errorJson -> result() -> exceptionProperties();

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

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> message(),
                            std::string( "System error has occurred" )
                            );
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
                        UTF_REQUIRE( ! errorJson -> result() -> exceptionFullDump().empty() );

                        const auto ecExpected = eh::errc::make_error_code( eh::errc::permission_denied );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionMessage(),
                            std::string( "Server error has occurred: " ) + ecExpected.message()
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

                        UTF_REQUIRE_EQUAL(
                            exceptionProperties -> message(),
                            std::string( "Server error has occurred: " ) + ecExpected.message()
                            );

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
 * The clamp on a peer supplied HTTP status code, the 'unset means 0' semantics and the
 * Content-Type consistency check
 *
 * httpStatusCode on the wire is a signed int chosen by a remote peer. The clamp is
 * deliberate hardening with an explanatory comment, and it is exactly the kind of check
 * which gets deleted during a refactor because nothing fails - without it a peer returning
 * -1 makes the gateway emit a status line with a nonsense code on remote command
 *
 * EchoServerProcessingContext only ever sets status codes drawn from http::Parameters, so
 * this is also the first test side implementation of BaseRestServerProcessingContext
 */

UTF_AUTO_TEST_CASE( RestServiceSslGatewayResponseMetadataTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const std::string statusPathPrefix( "/status/" );

    /*
     * Note that no assertion may be made here - this runs on a backend processing thread
     */

    const utest::TestRestServerProcessingContext::processing_callback_t processingCallback =
        [ statusPathPrefix ](
            SAA_in      const om::ObjPtr< dm::messaging::BrokerProtocol >&      brokerProtocol,
            SAA_in      const om::ObjPtrCopyable< data::DataBlock >&           dataBlock
            )
            -> om::ObjPtr< dm::http::HttpResponseMetadata >
        {
            BL_UNUSED( dataBlock );

            const auto metadataPayload =
                dm::DataModelUtils::castTo< dm::http::HttpRequestMetadataPayload >(
                    brokerProtocol -> passThroughUserData()
                    );

            const auto& urlPath = metadataPayload -> httpRequestMetadata() -> urlPath();

            if( "/noMetadata" == urlPath )
            {
                /*
                 * A null response metadata is what makes processingImpl fall through to the
                 * default metadata arm of getResponseBrokerProtocolString
                 */

                return nullptr;
            }

            auto responseMetadata = dm::http::HttpResponseMetadata::createInstance();

            responseMetadata -> contentType( http::HttpHeader::g_contentTypeJsonUtf8 );

            if( "/badcontenttype" == urlPath )
            {
                responseMetadata -> httpStatusCode( http::Parameters::HTTP_SUCCESS_OK );
                responseMetadata -> headersLvalue()[ "Content-Type" ] = "text/plain";

                return responseMetadata;
            }

            if( str::starts_with( urlPath, statusPathPrefix ) )
            {
                responseMetadata -> httpStatusCode(
                    utils::lexical_cast< int >( urlPath.substr( statusPathPrefix.size() ) )
                    );
            }

            return responseMetadata;
        };

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto serverContextFactory = [ & ](
        SAA_in      const om::ObjPtr< data::datablocks_pool_type >&         dataBlocksPool,
        SAA_in      const om::ObjPtr< om::Proxy >&                          backendReference
        )
        -> om::ObjPtrDisposable< AsyncBlockDispatcher >
    {
        return om::lockDisposable(
            om::qi< AsyncBlockDispatcher >(
                utest::TestRestServerProcessingContext::createInstance(
                    cpp::copy( processingCallback )                                 /* callback */,
                    false                                                           /* isGraphQLServer */,
                    false                                                           /* isAuthnticationAlwaysRequired */,
                    std::string()                                                   /* requiredContentType */,
                    om::copy( dataBlocksPool ),
                    om::copy( backendReference ),
                    cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenType */,
                    utest::TestRestUtils::defaultToken()                            /* tokenData */
                    )
                )
            );
    };

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
                        utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                    const auto payload = dm::DataModelUtils::loadFromFile< Payload >(
                        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    const auto payloadDataString =
                        dm::DataModelUtils::getDocAsPackedJsonString( payload );

                    const auto request = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          const unsigned int                  expectedStatus
                        )
                        -> om::ObjPtr< SimpleHttpSslTaskImpl >
                    {
                        http::StatusesList expectedHttpStatuses;
                        expectedHttpStatuses.insert( expectedStatus );

                        return utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            cpp::copy( payloadDataString )              /* content */,
                            cpp::copy( urlPath )                        /* urlPath */,
                            "GET"                                       /* action */,
                            utest::TestRestUtils::defaultToken()        /* tokenData */,
                            expectedHttpStatuses                        /* expectedHttpStatuses */
                            );
                    };

                    /*
                     * Out of range values in both directions and on both sides of the
                     * boundaries are replaced by a bad gateway status
                     */

                    const auto testOutOfRange = [ & ]( SAA_in const std::string& urlPath ) -> void
                    {
                        const auto taskImpl = request(
                            urlPath,
                            http::Parameters::HTTP_SERVER_ERROR_BAD_GATEWAY
                            );

                        UTF_REQUIRE( taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SERVER_ERROR_BAD_GATEWAY
                            );
                    };

                    testOutOfRange( "/status/-1" );
                    testOutOfRange( "/status/99" );
                    testOutOfRange( "/status/600" );
                    testOutOfRange( "/status/1000" );

                    /*
                     * A zero means 'unset' and never reaches the clamp at all - a separate
                     * and equally deletable behaviour
                     */

                    {
                        const auto taskImpl = request(
                            "/status/0",
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );
                    }

                    /*
                     * The clamp is inclusive at both ends, and an in range code which has no
                     * StatusStrings entry of its own still produces a well formed response
                     * through the generic reason phrase
                     */

                    const auto testForwardedVerbatim = [ & ](
                        SAA_in          const std::string&                  urlPath,
                        SAA_in          const unsigned int                  expectedStatus
                        )
                        -> void
                    {
                        const auto taskImpl = request( urlPath, expectedStatus );

                        UTF_REQUIRE( taskImpl -> isFailed() );
                        UTF_REQUIRE_EQUAL( taskImpl -> getHttpStatus(), expectedStatus );
                    };

                    testForwardedVerbatim( "/status/100", 100U );
                    testForwardedVerbatim( "/status/418", 418U );
                    testForwardedVerbatim( "/status/599", 599U );

                    /*
                     * A Content-Type header which disagrees with the metadata's content type
                     * makes the whole getResponse() call throw from inside the PROCESS arm of
                     * HttpServerConnection::continuationTask(), which is not the 'if( eptr )'
                     * path - so the current behaviour pinned here is a DROPPED CONNECTION and
                     * not an error response
                     */

                    {
                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            cpp::copy( payloadDataString )              /* content */,
                            "/badcontenttype"                           /* urlPath */
                            );

                        UTF_REQUIRE( taskImpl -> isFailed() );
                        UTF_REQUIRE( taskImpl -> getResponse().empty() );

                        /*
                         * No status line ever reached the client, which is what makes the
                         * assertion above a claim about a dropped connection rather than
                         * about some error response the gateway might have produced instead
                         *
                         * Verified by control: the very same request with a Content-Type
                         * header which agrees with the metadata's content type succeeds
                         */

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_STATUS_UNDEFINED
                            );
                    }

                    /*
                     * ... and a null response metadata falls through to the default one
                     */

                    {
                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                       /* allowFailure */,
                            http::HttpHeader::g_contentTypeJsonUtf8     /* contentType */,
                            cpp::copy( payloadDataString )              /* content */,
                            "/noMetadata"                               /* urlPath */
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getResponseHeaders().at( "content-type" ),
                            http::HttpHeader::g_contentTypeJsonUtf8
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
            std::move( tokenCookieNames )                                   /* tokenCookieNames */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */,
            std::string()                                                   /* tokenDataDefault */,
            utest::TestRestUtils::defaultToken()                            /* tokenData */,
            time::neg_infin                                                 /* requestTimeout */,
            false                                                           /* isAuthnticationAlwaysRequired */,
            std::string()                                                   /* requiredContentType */,
            false                                                           /* isGraphQLServer */,
            utest::TestRestUtils::format_eh_response_callback_t()           /* ehFormatCallback */,
            serverContextFactory                                            /* serverContextFactory */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}


/************************************************************************
 * The HTTP to messaging request size envelope
 *
 * The gateway takes one pooled block of DataBlock::defaultCapacity() bytes, writes the
 * request body into it and then writes a broker protocol document which embeds the method,
 * the URL path and every single request header - so the largest body it can carry is that
 * capacity minus a JSON document whose size the caller controls through its own headers.
 * The HTTP parser advertises a g_maxContentSize ceiling and says nothing about this, and
 * the diagnostic a client receives when the block overflows is a bare 500
 *
 * Nothing else in the suite sends a large but legal body - the only bodies the REST fixture
 * has ever sent are async_rpc_request.json and empty strings - so both the success path at
 * scale and the whole envelope are untested
 */

UTF_AUTO_TEST_CASE( RestServiceSslRequestSizeEnvelopeTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

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
                        utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                    http::StatusesList expectedInternalError;
                    expectedInternalError.insert( http::Parameters::HTTP_SERVER_ERROR_INTERNAL );

                    /*
                     * The load bearing part of the two rejection assertions is the
                     * exception type - it names which of the three bounds fired, so a
                     * future change which moved the rejection to the broker
                     * (ProtocolValidationFailed, i.e. bl::ServerErrorException) or to the
                     * HTTP parser (a 400) is caught rather than absorbed by the shared 500
                     */

                    const auto requireGatewayBufferOverflow = [ & ](
                        SAA_in          const bool                          isFailed,
                        SAA_in          const unsigned int                  httpStatus,
                        SAA_in          const std::string&                  response
                        )
                        -> void
                    {
                        UTF_REQUIRE( isFailed );

                        UTF_REQUIRE_EQUAL( httpStatus, http::Parameters::HTTP_SERVER_ERROR_INTERNAL );

                        const auto errorJson =
                            dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( response );

                        UTF_REQUIRE( errorJson );
                        UTF_REQUIRE( errorJson -> result() );

                        UTF_REQUIRE_EQUAL(
                            errorJson -> result() -> exceptionType(),
                            std::string( "bl::BufferTooSmallException" )
                            );
                    };

                    /*
                     * Comfortably inside the envelope, and a byte identical echo of half a
                     * megabyte - the only proof in the suite that the response leg's
                     * reset / write / setOffset1 / write round trip works at scale
                     */

                    {
                        const std::string body( 512U * 1024U, 'a' );

                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            false                                           /* allowFailure */,
                            http::HttpHeader::g_contentTypePlainTextUtf8    /* contentType */,
                            cpp::copy( body )                               /* content */,
                            "/foo/bar"                                      /* urlPath */,
                            "PUT"                                           /* action */
                            );

                        UTF_REQUIRE( ! taskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            taskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), body );
                    }

                    UTF_REQUIRE_EQUAL( 1U, messagesProcessed() );

                    /*
                     * At the HTTP parser's ceiling - the body alone consumes the whole
                     * block, so the protocol write which follows it cannot fit
                     */

                    {
                        std::string body( bl::httpserver::Parser::g_maxContentSize, 'a' );

                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                            /* allowFailure */,
                            http::HttpHeader::g_contentTypePlainTextUtf8    /* contentType */,
                            std::move( body )                               /* content */,
                            "/foo/bar"                                      /* urlPath */,
                            "PUT"                                           /* action */,
                            utest::TestRestUtils::defaultToken()            /* tokenData */,
                            expectedInternalError                           /* expectedHttpStatuses */
                            );

                        requireGatewayBufferOverflow(
                            taskImpl -> isFailed(),
                            taskImpl -> getHttpStatus(),
                            taskImpl -> getResponse()
                            );
                    }

                    /*
                     * The request was refused by the gateway before any messaging, so it
                     * never reached the broker or the backend
                     */

                    UTF_REQUIRE_EQUAL( 1U, messagesProcessed() );

                    /*
                     * The header dependent flip - a body size which is otherwise accepted
                     * (see the probes below, which carry the same size class with ordinary
                     * headers) pushed over the edge with headers alone
                     *
                     * The invariant being pinned is
                     *
                     *   body.size() + sizeof( request protocol JSON incl. every request
                     *   header ) <= DataBlock::defaultCapacity()
                     *
                     * so the ceiling a caller actually observes depends on what that caller
                     * happens to send in its own headers
                     */

                    {
                        const std::string body( bl::httpserver::Parser::g_maxContentSize - 8192U, 'a' );

                        http::HeadersMap headers;

                        headers[ http::HttpHeader::g_cookie ] = utest::TestRestUtils::defaultToken();

                        headers[ http::HttpHeader::g_contentType ] =
                            http::HttpHeader::g_contentTypePlainTextUtf8;

                        /*
                         * About 48 KiB of padding, well under Parser::g_maxHeadersSize
                         */

                        const std::string padValue( 1000U, 'p' );

                        for( std::size_t i = 0U; i < 48U; ++i )
                        {
                            headers[ "x-pad-" + utils::lexical_cast< std::string >( i ) ] = padValue;
                        }

                        auto taskImpl = SimpleHttpSslPutTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() )        /* host */,
                            httpPort,
                            "/foo/bar"                                      /* path */,
                            body                                            /* content */,
                            std::move( headers )                            /* requestHeaders */
                            );

                        taskImpl -> addExpectedHttpStatuses( expectedInternalError );

                        const auto task = om::qi< Task >( taskImpl );

                        eq -> push_back( task );
                        eq -> wait( task );

                        requireGatewayBufferOverflow(
                            taskImpl -> isFailed(),
                            taskImpl -> getHttpStatus(),
                            taskImpl -> getResponse()
                            );
                    }

                    UTF_REQUIRE_EQUAL( 1U, messagesProcessed() );

                    /*
                     * Pin the headroom without a binary search - two further probes, each
                     * larger than the last, recording the status of each. No absolute
                     * status is asserted for them because the exact ceiling is a function
                     * of the header set; what is asserted is monotonicity
                     */

                    const auto probeAtBodySize = [ & ]( SAA_in const std::size_t bodySize ) -> unsigned int
                    {
                        const auto messagesProcessedBefore = messagesProcessed();

                        std::string body( bodySize, 'a' );

                        const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                            /* allowFailure */,
                            http::HttpHeader::g_contentTypePlainTextUtf8    /* contentType */,
                            std::move( body )                               /* content */,
                            "/foo/bar"                                      /* urlPath */,
                            "PUT"                                           /* action */
                            );

                        const auto httpStatus = taskImpl -> getHttpStatus();

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "A request body of "
                                << bodySize
                                << " bytes was answered with HTTP status "
                                << httpStatus
                            );

                        /*
                         * An internal server error here is the gateway refusing the request
                         * before any messaging, so the backend must not have seen it;
                         * anything else means the request did travel all the way through
                         */

                        UTF_REQUIRE_EQUAL(
                            messagesProcessed() - messagesProcessedBefore,
                            http::Parameters::HTTP_SERVER_ERROR_INTERNAL == httpStatus ? 0U : 1U
                            );

                        return httpStatus;
                    };

                    const auto statusAt4096 =
                        probeAtBodySize( bl::httpserver::Parser::g_maxContentSize - 4096U );

                    const auto statusAt1024 =
                        probeAtBodySize( bl::httpserver::Parser::g_maxContentSize - 1024U );

                    /*
                     * A larger body must never succeed where a smaller one failed
                     */

                    UTF_REQUIRE(
                        ! (
                            http::Parameters::HTTP_SUCCESS_OK == statusAt1024 &&
                            http::Parameters::HTTP_SERVER_ERROR_INTERNAL == statusAt4096
                            )
                        );
                }
                );
        };

        utest::TestRestUtils::httpRestWithMessagingBackendTests(
            callback                                                        /* callback */,
            false                                                           /* waitOnServer */,
            true                                                            /* isQuietMode */,
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

            /*
             * This case deliberately probes the edge of the block capacity, and a request
             * which the gateway accepts but whose response cannot fit in the same block
             * leaves the conversation in flight - so the request timeout is shortened from
             * the two minute default to keep the worst case bounded. Every request here
             * completes in well under a second
             */

            time::seconds( 30L )                                            /* requestTimeout */,
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
 * A gateway request for an unregistered target peer
 *
 * The broker raises a ServerErrorException carrying the hard coded numeric 99 in
 * generic_category(); chk4ServerErrors narrows it onto the 32 bit errorCode field of the
 * ack; the gateway's client reconstructs it; and updateHttpStatusFromException upgrades the
 * resulting 500 to a 503. Four components have to agree and no test observes the result -
 * yet 503 versus 500 is the difference between a load balancer draining a node and an
 * operator being paged, and between a client library retrying and giving up
 *
 * Every other test which could produce TargetPeerNotFound deliberately hides it:
 * TestMessagingUtils::flushQueueWithRetriesOnTargetPeerNotFound retries up to 2000 times
 * without reporting
 */

UTF_AUTO_TEST_CASE( RestServiceSslGatewayTargetPeerNotFoundTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

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
                        utest::TestRestUtils::getHttpPort( test::UtfArgsParser::port() );

                    http::StatusesList expectedHttpStatuses;
                    expectedHttpStatuses.insert( http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE );

                    const auto startTime = time::microsec_clock::universal_time();

                    const auto taskImpl = utest::TestRestUtils::executeHttpRequest(
                        eq,
                        httpPort,
                        true                                        /* allowFailure */,
                        str::empty()                                /* contentType */,
                        std::string()                               /* content */,
                        "/foo/bar"                                  /* urlPath */,
                        "GET"                                       /* action */,
                        utest::TestRestUtils::defaultToken()        /* tokenData */,
                        expectedHttpStatuses                        /* expectedHttpStatuses */
                        );

                    const auto elapsed = time::microsec_clock::universal_time() - startTime;

                    UTF_REQUIRE( taskImpl -> isFailed() );

                    UTF_REQUIRE_EQUAL(
                        taskImpl -> getHttpStatus(),
                        http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE
                        );

                    const auto errorJson =
                        dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( taskImpl -> getResponse() );

                    UTF_REQUIRE( errorJson );
                    UTF_REQUIRE( errorJson -> result() );

                    UTF_REQUIRE_EQUAL(
                        errorJson -> result() -> exceptionType(),
                        std::string( "bl::ServerErrorException" )
                        );

                    UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

                    /*
                     * Read back out of the HTTP body, this single assertion pins the
                     * constant, the wire field, the category re-attachment on the client
                     * side and the serializer in one shot
                     */

                    UTF_REQUIRE_EQUAL(
                        errorJson -> result() -> exceptionProperties() -> errorCode(),
                        static_cast< int >( BrokerErrorCodes::TargetPeerNotFound )
                        );

                    /*
                     * The errinfo_is_expected( true ) which the broker sets alongside the
                     * error code does NOT survive the block transfer wire -
                     * chk4ServerErrorsClient rebuilds the exception from a wire uint32
                     * which carries only errinfo_errno and errinfo_error_code. The same
                     * loss is pinned at the block transfer seam itself by the PKG-IO work
                     */

                    UTF_REQUIRE( ! errorJson -> result() -> exceptionProperties() -> isExpectedIsSet() );

                    /*
                     * No retry and no timeout - the gateway performs zero retries, unlike
                     * the async RPC conversation layer which would spend a five attempt
                     * budget on exactly this error code. The bound is far below the
                     * bridge's two minute DEFAULT_REQUEST_TIMEOUT_IN_SECONDS, which is what
                     * distinguishes a routing 503 from a timeout 504
                     */

                    UTF_REQUIRE( elapsed < time::seconds( 30L ) );

                    /*
                     * The backend never saw it
                     */

                    UTF_REQUIRE_EQUAL( 0U, messagesProcessed() );

                    /*
                     * Positive control - the gateway itself is otherwise serving, and
                     * /health never enters messaging at all
                     */

                    {
                        const auto healthTaskImpl = utest::TestRestUtils::executeHttpRequest(
                            eq,
                            httpPort,
                            true                                        /* allowFailure */,
                            str::empty()                                /* contentType */,
                            std::string()                               /* content */,
                            "/health"                                   /* urlPath */
                            );

                        UTF_REQUIRE( ! healthTaskImpl -> isFailed() );

                        UTF_REQUIRE_EQUAL(
                            healthTaskImpl -> getHttpStatus(),
                            http::Parameters::HTTP_SUCCESS_OK
                            );
                    }

                    UTF_REQUIRE_EQUAL( 0U, messagesProcessed() );
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
            utest::TestRestUtils::format_eh_response_callback_t()           /* ehFormatCallback */,
            utest::TestRestUtils::server_context_factory_t()                /* serverContextFactory */,
            &messagesProcessed                                              /* messagesProcessedOut */,

            /*
             * A peer which never registers with the broker
             */

            uuids::create()                                                 /* gatewayTargetPeerId */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}

namespace
{
    /**
     * @brief A messaging backend whose isConnected() is chosen by the test
     *
     * BackendProcessingBase already supplies validateParameters, setHostServices,
     * autoBlockDispatching and dispose, so all this needs is the switchable flag and a
     * processing task which does nothing
     */

    class DisconnectedBackend : public bl::messaging::BackendProcessingBase
    {
    protected:

        std::atomic< bool >                                                     m_connected;

        DisconnectedBackend()
            :
            m_connected( true )
        {
        }

    public:

        void setConnected( SAA_in const bool connected ) NOEXCEPT
        {
            m_connected = connected;
        }

        virtual bool isConnected() const NOEXCEPT OVERRIDE
        {
            return m_connected;
        }

        virtual auto createBackendProcessingTask(
            SAA_in                  const OperationId                            operationId,
            SAA_in                  const CommandId                              commandId,
            SAA_in                  const bl::uuid_t&                            sessionId,
            SAA_in                  const bl::uuid_t&                            chunkId,
            SAA_in_opt              const bl::uuid_t&                            sourcePeerId,
            SAA_in_opt              const bl::uuid_t&                            targetPeerId,
            SAA_in_opt              const bl::om::ObjPtr< bl::data::DataBlock >&  data
            )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            BL_UNUSED( operationId );
            BL_UNUSED( commandId );
            BL_UNUSED( sessionId );
            BL_UNUSED( chunkId );
            BL_UNUSED( sourcePeerId );
            BL_UNUSED( targetPeerId );
            BL_UNUSED( data );

            return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                bl::cpp::void_callback_t()
                );
        }
    };

    typedef bl::om::ObjectImpl< DisconnectedBackend > DisconnectedBackendImpl;

} // __unnamed

/************************************************************************
 * The gateway's isConnected() self shutdown wire, in both polarities
 *
 * This is the only automatic shutdown mechanism in the shipped servers and it is load
 * bearing in both directions. Over-reporting leaves a gateway whose broker has vanished up
 * and answering 504s forever instead of being restarted by its orchestrator;
 * under-reporting terminates the whole gateway process on a transient reconnect blip. Both
 * are silent today - the only isConnected() anywhere in the test tree is a mock which hard
 * codes true, so the real chain is never invoked
 *
 * The token is the observable; nothing is asserted about the bridge itself. No broker, no
 * machine global lock and no port are needed
 */

UTF_AUTO_TEST_CASE( RestServiceGatewayShutsDownWhenBackendDisconnectedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto stub = om::lockDisposable( DisconnectedBackendImpl::createInstance() );

        const auto httpBackend = om::lockDisposable(
            rest::HttpServerBackendMessagingBridge::createInstance< bl::httpserver::ServerBackendProcessing >(
                om::copy( controlToken ),
                om::qi< BackendProcessing >( stub )                  /* messagingBackend */,
                uuids::create()                                     /* sourcePeerId */,
                uuids::create()                                     /* targetPeerId */,
                data::datablocks_pool_type::createInstance(),
                std::unordered_set< std::string >()                 /* tokenCookieNames */,
                true                                                /* serverAuthenticationRequired */,
                std::string()                                       /* expectedSecurityId */
                )
            );

        /*
         * The bridge starts its prune timer with initDelay = 0 and a 5 s period, so by the
         * time this wait is over the wire has been exercised repeatedly against a healthy
         * backend
         *
         * This is the arm which a '!' inversion would break, and it is what makes the
         * negative arm below mean something
         */

        os::sleep( time::seconds( 8L ) );

        UTF_REQUIRE( ! controlToken -> isCanceled() );

        /*
         * The negative polarity - the very same wire must request cancellation once the
         * backend starts reporting disconnected
         */

        stub -> setConnected( false );

        {
            const std::size_t maxRetries = 15U;
            std::size_t retries = 0U;

            for( ;; )
            {
                if( controlToken -> isCanceled() )
                {
                    break;
                }

                if( retries >= maxRetries )
                {
                    UTF_FAIL(
                        "The gateway did not request shutdown within 15 seconds of the"
                        " messaging backend reporting disconnected"
                        );

                    break;
                }

                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        }

        UTF_REQUIRE( controlToken -> isCanceled() );
    }

    /*
     * The identical timer exists a second time on the backend server side, inside
     * waitOnForwardingBackend, where it additionally cancels the shutdown watcher the
     * function blocks on - so there the observable is that the call returns at all
     *
     * It is driven from its own thread, and the callable is self contained, because a
     * regression which broke that wire would otherwise hang the whole test module rather
     * than failing it
     */

    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto stub = om::lockDisposable( DisconnectedBackendImpl::createInstance() );

        stub -> setConnected( false );

        const auto completed = std::make_shared< std::atomic< bool > >( false );
        const auto failed = std::make_shared< std::atomic< bool > >( false );

        const auto backend = om::qi< BackendProcessing >( stub );

        const auto controlTokenRef =
            om::ObjPtrCopyable< TaskControlTokenRW >::acquireRef( controlToken.get() );

        const auto backendRef =
            om::ObjPtrCopyable< BackendProcessing >::acquireRef( backend.get() );

        os::thread waiter(
            [ completed, failed, controlTokenRef, backendRef ]() -> void
            {
                try
                {
                    echo::EchoServerProcessingContext::waitOnForwardingBackend(
                        controlTokenRef,
                        backendRef
                        );
                }
                catch( std::exception& )
                {
                    /*
                     * Nothing may be asserted from a thread which is not the one the test
                     * framework runs on - the flag is checked below instead
                     */

                    *failed = true;
                }

                *completed = true;
            }
            );

        {
            const std::size_t maxRetries = 15U;
            std::size_t retries = 0U;

            while( ! completed -> load() && retries < maxRetries )
            {
                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        }

        if( completed -> load() )
        {
            waiter.join();
        }
        else
        {
            /*
             * Leave the stuck thread behind rather than blocking the whole module on it
             */

            waiter.detach();

            UTF_FAIL(
                "waitOnForwardingBackend did not return within 15 seconds of the"
                " forwarding backend reporting disconnected"
                );
        }

        UTF_REQUIRE( ! failed -> load() );
        UTF_REQUIRE( controlToken -> isCanceled() );
    }
}

UTF_AUTO_TEST_CASE( RestServiceSslBackendPerfTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        std::unordered_set< std::string > tokenCookieNames;
        tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

        utest::TestRestUtils::httpRestWithMessagingBackendTests(
            cpp::void_callback_t()                                          /* callback */,
            false                                                           /* waitOnServer */,
            true                                                            /* isQuietMode */,
            50U                                                             /* requestsCount */,
            uuids::create()                                                 /* gatewayPeerId */,
            uuids::create()                                                 /* serverPeerId */,
            om::copy( controlToken )                                        /* controlToken */,
            test::UtfArgsParser::host()                                     /* brokerHostName */,
            test::UtfArgsParser::port()                                     /* brokerInboundPort */,
            test::UtfArgsParser::connections()                              /* noOfConnections */,
            cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
            std::move( tokenCookieNames )                                   /* tokenCookieNames */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */
            );
    };

    utest::TestRestUtils::startBrokerAndRunTests( callbackTests, controlToken );
}

UTF_AUTO_TEST_CASE( RestServiceSslHttpGatewayOnlyTests )
{
    using namespace bl;
    using namespace bl::tasks;

    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual run test)" );

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    std::unordered_set< std::string > tokenCookieNames;
    tokenCookieNames.emplace( utest::DummyAuthorizationCache::dummyCookieName() );

    utest::TestRestUtils::httpRestWithMessagingBackendTests(
        cpp::void_callback_t()                                          /* callback */,
        true                                                            /* waitOnServer */,
        true                                                            /* isQuietMode */,
        0U                                                              /* requestsCount */,
        uuids::create()                                                 /* gatewayPeerId */,
        uuids::create()                                                 /* serverPeerId */,
        om::copy( controlToken )                                        /* controlToken */,
        test::UtfArgsParser::host()                                     /* brokerHostName */,
        test::UtfArgsParser::port()                                     /* brokerInboundPort */,
        test::UtfArgsParser::connections()                              /* noOfConnections */,
        cpp::copy( utest::DummyAuthorizationCache::dummySid() )         /* expectedSecurityId */,
        std::move( tokenCookieNames )                                   /* tokenCookieNames */,
        cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenTypeDefault */
        );
}

UTF_AUTO_TEST_CASE( RestServiceSslBackendHttpOnlyTests )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    if( test::UtfArgsParser::tokenData().empty() )
    {
        UTF_FAIL( "--token-data is a required parameter for this test" );

        return;
    }

    utest::TestRestUtils::httpRunSimpleRequest(
        test::UtfArgsParser::port()                 /* httpPort */,
        test::UtfArgsParser::tokenData()            /* tokenData */,
        test::UtfArgsParser::connections()          /* requestsCount */,
        true                                        /* isQuietMode */
        );

}

UTF_AUTO_TEST_CASE( RestServerProcessingContextDisposeTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * The context is an om::Disposable which a forwarding backend holds through an om::Proxy
     * and which may still be handed blocks while shutdown runs; 'fail fast instead of
     * touching a disposed queue' is the contract which makes that safe
     *
     * Every existing test wraps the context in om::lockDisposable, so dispose() runs exactly
     * once, at scope exit, when nothing is in flight - so neither the idempotency of
     * disposeInternal() nor either chkIfDisposed() call site has ever been asserted. A
     * regression which moved the flag assignment after the flush, or which dropped
     * chkIfDisposed() from createDispatchTask(), would push a task onto a disposed
     * ExecutionQueue during shutdown - an assert in debug and undefined behaviour in release,
     * on a path which only fires under load
     *
     * No broker, no HTTP server and no machine global lock are involved
     */

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    const auto backendReference = om::ProxyImpl::createInstance< om::Proxy >( false /* strongRef */ );

    const auto context = echo::EchoServerProcessingContext::createInstance(
        true                                                            /* isQuietMode */,
        0UL                                                             /* maxProcessingDelayInMicroseconds */,
        false                                                           /* isGraphQLServer */,
        false                                                           /* isAuthnticationAlwaysRequired */,
        std::string()                                                   /* requiredContentType */,
        om::copy( dataBlocksPool ),
        om::copy( backendReference ),
        cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenType */,
        std::string()                                                   /* tokenData */
        );

    const auto block = data::DataBlock::get( dataBlocksPool );

    {
        /*
         * Before disposal a dispatch task is created normally; it is deliberately not run,
         * because the proxy has no backend connected
         */

        const auto task = context -> createDispatchTask( uuids::create(), block );

        UTF_REQUIRE( task );
    }

    UTF_REQUIRE_EQUAL( 0UL, context -> messagesProcessed() );

    /*
     * ... and disposal is idempotent
     */

    context -> dispose();
    context -> dispose();

    UTF_REQUIRE_THROW_MESSAGE(
        context -> createDispatchTask( uuids::create(), block ),
        UnexpectedException,
        "The server processing context was disposed already"
        );

    /*
     * Nothing was ever processed - which is what distinguishes 'the task was created' from
     * 'the task ran'
     */

    UTF_REQUIRE_EQUAL( 0UL, context -> messagesProcessed() );
}

UTF_AUTO_TEST_CASE( EchoServerContentTypeHeaderLookupTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * The other half of the finding which RestServiceSslBackendAssortedTests pins at the
     * gateway level: the exact key find() in EchoServerProcessingContext *can* succeed, but
     * only for a producer which is not the HTTP gateway, because the gateway lower cases every
     * request header name
     *
     * The contrast between the two blocks below is the finding - the same request, spelled
     * two ways, takes two different paths and yields two different HTTP statuses
     *
     * Note that both blocks depend on isQuietMode == false: the whole block is nested inside
     * 'if( ! m_isQuietMode )', so moving the context to quiet mode would turn the first one
     * into a 200 as well - a logging flag deciding an HTTP status
     */

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    const auto backendReference = om::ProxyImpl::createInstance< om::Proxy >( false /* strongRef */ );

    const auto context = om::lockDisposable(
        echo::EchoServerProcessingContext::createInstance(
            false                                                           /* isQuietMode */,
            0UL                                                             /* maxProcessingDelayInMicroseconds */,
            false                                                           /* isGraphQLServer */,
            false                                                           /* isAuthnticationAlwaysRequired */,
            std::string()                                                   /* requiredContentType */,
            om::copy( dataBlocksPool ),
            om::copy( backendReference ),
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenType */,
            std::string()                                                   /* tokenData */
            )
        );

    /*
     * HttpResponseMetadata::httpStatusCode is an int property, so the status is compared as
     * an int rather than as http::Parameters::HttpStatusCode
     */

    const auto processWithContentTypeKey =
        [ & ]( SAA_in const std::string& contentTypeHeaderName ) -> int
    {
        const auto requestMetadata = dm::http::HttpRequestMetadata::createInstance();

        requestMetadata -> method( "GET" );
        requestMetadata -> urlPath( "/foo/bar" );

        requestMetadata -> headersLvalue()[ contentTypeHeaderName ] =
            http::HttpHeader::g_contentTypeJsonUtf8;

        const auto requestMetadataPayload = dm::http::HttpRequestMetadataPayload::createInstance();

        requestMetadataPayload -> httpRequestMetadata( om::copy( requestMetadata ) );

        const auto brokerProtocol = MessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                                                 /* conversationId */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenType() )   /* tokenType */,
            cpp::copy( utest::DummyAuthorizationCache::dummyTokenData() )   /* tokenData */
            );

        brokerProtocol -> targetPeerId( uuids::uuid2string( uuids::create() ) );

        brokerProtocol -> passThroughUserData(
            dm::DataModelUtils::castTo< bl::dm::Payload >( requestMetadataPayload )
            );

        /*
         * offset1() stays at zero - i.e. a content type header was announced, but no content
         * was sent at all
         */

        const auto block = data::DataBlock::get( dataBlocksPool );

        UTF_REQUIRE_EQUAL( 0U, block -> offset1() );

        const auto responseMetadata = context -> processingSync(
            brokerProtocol,
            om::ObjPtrCopyable< data::DataBlock >( block )
            );

        UTF_REQUIRE( responseMetadata );

        return responseMetadata -> httpStatusCode();
    };

    /*
     * The exact, non normalised key - which only a non gateway producer can create. This is
     * the dormant arm being executed for the first time
     */

    UTF_REQUIRE_EQUAL(
        processWithContentTypeKey( http::HttpHeader::g_contentType ),
        static_cast< int >( http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST )
        );

    /*
     * ... and the shape the gateway actually produces takes the other path
     */

    UTF_REQUIRE_EQUAL(
        processWithContentTypeKey( "content-type" ),
        static_cast< int >( http::Parameters::HTTP_SUCCESS_OK )
        );
}
