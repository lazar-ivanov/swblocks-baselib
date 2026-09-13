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

/*
 * Continuation of TestRestDefault.h, split so that no single test translation unit exhausts a
 * 32-bit compiler host - see notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * The file has almost no file scope structure - one anonymous namespace at what was line 2983,
 * used only by the case which follows it - so it can be cut between any two cases. This cut is
 * simply near the middle by case count: six stay, nine move.
 *
 * This file must be included after TestRestDefault.h - it is a continuation, not a standalone
 * header.
 */

#include <utests/baselib/TestRestUtils.h>
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
