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

#ifndef __UTEST_TESTRESTUTILS_H_
#define __UTEST_TESTRESTUTILS_H_

#include <baselib/core/BuildInfo.h>

#include <baselib/examples/echoserver/EchoServerProcessingContext.h>

#include <baselib/rest/HttpServerBackendMessagingBridge.h>

#include <baselib/messaging/BrokerFacade.h>

#include <utests/baselib/TestMessagingUtils.h>
#include <utests/baselib/HttpServerHelpers.h>
#include <utests/baselib/UtfCrypto.h>

namespace utest
{
    /**
     * @brief A BaseRestServerProcessingContext implementation whose response metadata is
     * chosen by the test
     *
     * EchoServerProcessingContext is the only implementation of the base in the repository
     * and it only ever sets status codes drawn from http::Parameters and exactly one
     * header, so without this there is no way to feed the gateway a hostile or merely
     * unusual HttpResponseMetadata
     */

    template
    <
        typename E = void
    >
    class TestRestServerProcessingContextT :
        public bl::rest::BaseRestServerProcessingContext< TestRestServerProcessingContextT< E > >
    {
        BL_DECLARE_OBJECT_IMPL( TestRestServerProcessingContextT )

    public:

        typedef bl::dm::messaging::BrokerProtocol                                   BrokerProtocol;

        typedef bl::cpp::function
        <
            bl::om::ObjPtr< bl::dm::http::HttpResponseMetadata > (
                SAA_in      const bl::om::ObjPtr< BrokerProtocol >&                     brokerProtocol,
                SAA_in      const bl::om::ObjPtrCopyable< bl::data::DataBlock >&        dataBlock
                )
        >
        processing_callback_t;

    protected:

        typedef bl::rest::BaseRestServerProcessingContext
        <
            TestRestServerProcessingContextT< E >
        >
        base_type;

        const processing_callback_t                                                 m_callback;

        TestRestServerProcessingContextT(
            SAA_in      processing_callback_t&&                                     callback,
            SAA_in      const bool                                                  isGraphQLServer,
            SAA_in      const bool                                                  isAuthnticationAlwaysRequired,
            SAA_in      std::string&&                                               requiredContentType,
            SAA_in      bl::om::ObjPtr< bl::data::datablocks_pool_type >&&          dataBlocksPool,
            SAA_in      bl::om::ObjPtr< bl::om::Proxy >&&                           backendReference,
            SAA_in      std::string&&                                               tokenType,
            SAA_in_opt  std::string&&                                               tokenData = std::string()
            )
            :
            base_type(
                isGraphQLServer,
                isAuthnticationAlwaysRequired,
                BL_PARAM_FWD( requiredContentType ),
                BL_PARAM_FWD( dataBlocksPool ),
                BL_PARAM_FWD( backendReference ),
                BL_PARAM_FWD( tokenType ),
                BL_PARAM_FWD( tokenData )
                ),
            m_callback( BL_PARAM_FWD( callback ) )
        {
        }

    public:

        auto processingSync(
            SAA_in      const bl::om::ObjPtr< BrokerProtocol >&                     brokerProtocolIn,
            SAA_in      const bl::om::ObjPtrCopyable< bl::data::DataBlock >&        dataBlock
            )
            -> bl::om::ObjPtr< bl::dm::http::HttpResponseMetadata >
        {
            return m_callback( brokerProtocolIn, dataBlock );
        }
    };

    typedef bl::om::ObjectImpl< TestRestServerProcessingContextT<> > TestRestServerProcessingContext;

    /**
     * @brief Helpers for aiding implementation of REST tests
     */

    template
    <
        typename E = void
    >
    class TestRestUtilsT
    {
        BL_DECLARE_STATIC( TestRestUtilsT )

    public:

        typedef bl::httpserver::ServerBackendProcessing::format_eh_response_callback_t
            format_eh_response_callback_t;

        /**
         * @brief Creates the server side processing context which replaces the echo context
         *
         * This is a factory rather than a ready made context because the context has to be
         * constructed with the backend reference proxy which httpRestWithMessagingBackendTests
         * itself creates and connects to the server side forwarding backend
         */

        typedef bl::cpp::function
        <
            bl::om::ObjPtrDisposable< bl::messaging::AsyncBlockDispatcher > (
                SAA_in      const bl::om::ObjPtr< bl::data::datablocks_pool_type >&     dataBlocksPool,
                SAA_in      const bl::om::ObjPtr< bl::om::Proxy >&                      backendReference
                )
        >
        server_context_factory_t;

        static auto defaultToken() -> std::string
        {
            return TestMessagingUtils::getTokenData();
        }

        static auto defaultTokenType() -> std::string
        {
            return TestMessagingUtils::getTokenType();
        }

        static auto noCookieNames() -> std::unordered_set< std::string >
        {
            return std::unordered_set< std::string >();
        }

        static auto getHttpPort( SAA_in const bl::os::port_t brokerInboundPort ) NOEXCEPT
            -> bl::os::port_t
        {
            return brokerInboundPort + 100U;
        }

        static auto executeHttpRequest(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&              eq,
            SAA_in          const unsigned short                                            httpPort,
            SAA_in          const bool                                                      allowFailure,
            SAA_in_opt      const std::string&                                              contentType,
            SAA_in_opt      std::string&&                                                   content = std::string(),
            SAA_in_opt      std::string&&                                                   urlPath = "/default",
            SAA_in_opt      std::string&&                                                   action = "GET",
            SAA_in_opt      std::string&&                                                   tokenData = defaultToken(),
            SAA_in_opt      const bl::http::StatusesList&                                   expectedHttpStatuses = bl::http::StatusesList()
            )
            -> bl::om::ObjPtr< bl::tasks::SimpleHttpSslTaskImpl >
        {
            using namespace bl;
            using namespace bl::tasks;

            bl::http::HeadersMap headers;

            if( tokenData.empty() )
            {
                tokenData = defaultToken();
            }

            headers[ http::HttpHeader::g_cookie ] = std::move( tokenData );

            if( ! contentType.empty() )
            {
                headers[ http::HttpHeader::g_contentType ] = contentType;
            }

            auto taskImpl = SimpleHttpSslTaskImpl::createInstance(
                cpp::copy( test::UtfArgsParser::host() ),
                httpPort,
                BL_PARAM_FWD( urlPath ),
                BL_PARAM_FWD( action ),
                BL_PARAM_FWD( content ),
                std::move( headers )
                );

            if( ! expectedHttpStatuses.empty() )
            {
                taskImpl -> addExpectedHttpStatuses( expectedHttpStatuses );
            }

            const auto task = om::qi< Task >( taskImpl );

            eq -> push_back( task );

            if( allowFailure )
            {
                eq -> wait( task );
            }
            else
            {
                eq -> waitForSuccess( task );
            }

            return taskImpl;
        }

        static void httpRunSimpleRequest(
            SAA_in          const unsigned short                                            httpPort,
            SAA_in_opt      const std::string&                                              tokenData = defaultToken(),
            SAA_in_opt      const std::size_t                                               requestsCount = 1U,
            SAA_in_opt      const bool                                                      isQuietMode = false
            )
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace bl::messaging;
            using namespace utest;
            using namespace utest::http;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                    /*
                     * Run HTTP requests for testing
                     */

                    const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
                        TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    const auto payloadDataString = bl::dm::DataModelUtils::getDocAsPackedJsonString( payload );

                    std::vector< om::ObjPtr< SimpleHttpSslPutTaskImpl > > tasks;

                    {
                        utils::ExecutionTimer timer(
                            resolveMessage(
                                BL_MSG()
                                    << "Executing "
                                    << requestsCount
                                    << " requests"
                                )
                            );

                        for( std::size_t i = 0U; i < requestsCount; ++i )
                        {
                            bl::http::HeadersMap headers;

                            if( ! tokenData.empty() )
                            {
                                headers[ bl::http::HttpHeader::g_cookie ] = tokenData;
                            }

                            headers[ bl::http::HttpHeader::g_contentType ] =
                                bl::http::HttpHeader::g_contentTypeJsonUtf8;

                            auto taskImpl = SimpleHttpSslPutTaskImpl::createInstance(
                                cpp::copy( test::UtfArgsParser::host() ),
                                httpPort,
                                "/foo/bar",                         /* URI */
                                cpp::copy( payloadDataString )      /* content */,
                                std::move( headers )                /* headers */
                                );

                            eq -> push_back( om::qi< Task >( taskImpl ) );
                            tasks.push_back( std::move( taskImpl ) );
                        }

                        eq -> flush();
                    }

                    if( ! isQuietMode && ! tasks.empty() )
                    {
                        const auto& taskImpl = tasks.at( 0 );

                        const auto responsePayload = bl::dm::DataModelUtils::loadFromJsonText< Payload >(
                            taskImpl -> getResponse()
                            );

                        const auto response = bl::dm::DataModelUtils::getDocAsPrettyJsonString( responsePayload );

                        const auto& responseHeaders = taskImpl -> getResponseHeaders();

                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\n**********************************************\n\n"
                                << "Response headers:\n"
                                << str::mapToString( responseHeaders )
                                << "\nResponse message:\n"
                                << response
                                << "\n\n"
                            );
                    }
                });
        }

        static void httpRestWithMessagingBackendTests(
            SAA_in_opt      bl::cpp::void_callback_t&&                                      callback,
            SAA_in          const bool                                                      waitOnServer,
            SAA_in          const bool                                                      isQuietMode,
            SAA_in          const std::size_t                                               requestsCount,
            SAA_in          const bl::uuid_t&                                               gatewayPeerId,
            SAA_in          const bl::uuid_t&                                               serverPeerId,
            SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&          controlToken,
            SAA_in          const std::string&                                              brokerHostName,
            SAA_in          const unsigned short                                            brokerInboundPort,
            SAA_in          const std::size_t                                               noOfConnections,
            SAA_in          std::string&&                                                   expectedSecurityId,
            SAA_in_opt      std::unordered_set< std::string >&&                             tokenCookieNames = noCookieNames(),
            SAA_in_opt      std::string&&                                                   tokenTypeDefault = defaultTokenType(),
            SAA_in_opt      std::string&&                                                   tokenDataDefault = std::string(),
            SAA_in_opt      std::string&&                                                   tokenData = defaultToken(),
            SAA_in_opt      const bl::time::time_duration&                                  requestTimeout = bl::time::neg_infin,
            SAA_in_opt      const bool                                                      isAuthnticationAlwaysRequired = false,
            SAA_in_opt      std::string&&                                                   requiredContentType = std::string(),
            SAA_in_opt      const bool                                                      isGraphQLServer = false,
            SAA_in_opt      format_eh_response_callback_t&&                                 ehFormatCallback = format_eh_response_callback_t(),
            SAA_in_opt      const server_context_factory_t&                                 serverContextFactory = server_context_factory_t()
            )
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace bl::messaging;
            using namespace utest;
            using namespace utest::http;

            const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "The HTTP gateway peer id: "
                    << uuids::uuid2string( gatewayPeerId )
                    << "\nServer peer id: "
                    << uuids::uuid2string( serverPeerId )
                );

            const auto backendReference = om::ProxyImpl::createInstance< om::Proxy >( false /* strongRef*/ );

            const auto echoContext = om::lockDisposable(
                echo::EchoServerProcessingContext::createInstance(
                    isQuietMode || waitOnServer,
                    0UL                                 /* maxProcessingDelayInMicroseconds */,
                    isGraphQLServer                     /* isGraphQLServer */,
                    isAuthnticationAlwaysRequired       /* isAuthnticationAlwaysRequired */,
                    BL_PARAM_FWD( requiredContentType ) /* requiredContentType */,
                    om::copy( dataBlocksPool ),
                    om::copy( backendReference ),
                    cpp::copy( tokenTypeDefault )       /* tokenType */,
                    cpp::copy( tokenData )              /* tokenData */
                    )
                );

            {
                const auto backend1 = om::lockDisposable(
                    ForwardingBackendProcessingFactoryDefaultSsl::create(
                        brokerInboundPort       /* defaultInboundPort */,
                        om::copy( controlToken ),
                        gatewayPeerId,
                        noOfConnections,
                        TestMessagingUtils::getTestEndpointsList( brokerHostName, brokerInboundPort ),
                        dataBlocksPool,
                        0U                      /* threadsCount */,
                        0U                      /* maxConcurrentTasks */,
                        true                    /* waitAllToConnect */
                        )
                    );

                const auto backend2 = om::lockDisposable(
                    ForwardingBackendProcessingFactoryDefaultSsl::create(
                        brokerInboundPort       /* defaultInboundPort */,
                        om::copy( controlToken ),
                        serverPeerId,
                        noOfConnections,
                        TestMessagingUtils::getTestEndpointsList( brokerHostName, brokerInboundPort ),
                        dataBlocksPool,
                        0U                      /* threadsCount */,
                        0U                      /* maxConcurrentTasks */,
                        true                    /* waitAllToConnect */
                        )
                    );

                /*
                 * The caller supplied server context, when there is one, replaces the echo
                 * context as the implementation behind the server side forwarding backend
                 */

                const auto serverContext = serverContextFactory ?
                    serverContextFactory( dataBlocksPool, backendReference ) :
                    om::ObjPtrDisposable< messaging::AsyncBlockDispatcher >();

                {
                    auto proxy = om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                    proxy -> connect(
                        serverContext ?
                            serverContext.get() :
                            static_cast< messaging::AsyncBlockDispatcher* >( echoContext.get() )
                        );

                    backend2 -> setHostServices( std::move( proxy ) );
                }

                /*
                 * The composed isConnected() is a four level chain and the gateway bridge
                 * polls it every 5 s, cancelling the control token the moment it reads
                 * false - so in a healthy deployment it must be true continuously, and a
                 * regression which makes it under-report would otherwise surface only as a
                 * mysteriously cancelled token deep inside an unrelated case
                 */

                UTF_REQUIRE( backend1 -> isConnected() );
                UTF_REQUIRE( backend2 -> isConnected() );
                UTF_REQUIRE( ! controlToken -> isCanceled() );

                {
                    BL_SCOPE_EXIT(
                        {
                            backendReference -> disconnect();
                        }
                        );

                    backendReference -> connect( backend2.get() );

                    const auto httpBackend = om::lockDisposable(
                        rest::HttpServerBackendMessagingBridge::createInstance< bl::httpserver::ServerBackendProcessing >(
                            om::copy( controlToken ),
                            om::copy( backend1 )                                    /* messagingBackend */,
                            gatewayPeerId                                           /* sourcePeerId */,
                            serverPeerId                                            /* targetPeerId */,
                            om::copy( dataBlocksPool ),
                            BL_PARAM_FWD( tokenCookieNames ),
                            true                                                    /* serverAuthenticationRequired */,
                            BL_PARAM_FWD( expectedSecurityId ),
                            true                                                    /* logUnauthorizedMessages */,
                            BL_PARAM_FWD( tokenTypeDefault ),
                            BL_PARAM_FWD( tokenDataDefault ),
                            requestTimeout,
                            BL_PARAM_FWD( ehFormatCallback )
                            )
                        );

                    {
                        const os::port_t httpPort = getHttpPort( brokerInboundPort );

                        const auto acceptor = bl::httpserver::HttpSslServer::createInstance(
                            om::copy( httpBackend ),
                            controlToken,
                            "0.0.0.0"                                           /* host */,
                            httpPort,
                            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
                            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */
                            );

                        const bl::cpp::void_callback_t waitOnBackendCallback =
                            [ & ]() -> void
                            {
                                echo::EchoServerProcessingContext::waitOnForwardingBackend(
                                    controlToken,
                                    backend2            /* forwardingBackend */
                                    );
                            };

                        const bl::cpp::void_callback_t executeHttpRequestCallback =
                            bl::cpp::bind(
                                &httpRunSimpleRequest,
                                httpPort,
                                tokenData,
                                requestsCount,
                                isQuietMode
                                );

                        const bool isCustomCallback = static_cast< bool >( callback );

                        if( ! callback )
                        {
                            callback = waitOnServer ? waitOnBackendCallback : executeHttpRequestCallback;
                        }

                        /*
                         * The identical assertion block again, immediately after the
                         * caller's callback has returned - the gateway must not have self
                         * cancelled during the run
                         *
                         * It has to run inside the callback rather than after
                         * startAcceptorAndExecuteCallback returns, because shutting the
                         * acceptor down cancels the shared control token by design
                         * (TcpBaseTasks.h:2292), and it is skipped in the wait on server
                         * mode, where the caller's callback is itself the one which blocks
                         * until that cancellation
                         */

                        const bl::cpp::void_callback_t innerCallback = callback;

                        const bl::cpp::void_callback_t callbackWithAssertions =
                            [ & ]() -> void
                            {
                                innerCallback();

                                if( ! waitOnServer )
                                {
                                    UTF_REQUIRE( backend1 -> isConnected() );
                                    UTF_REQUIRE( backend2 -> isConnected() );
                                    UTF_REQUIRE( ! controlToken -> isCanceled() );
                                }
                            };

                        /*
                         * The acceptor binds "0.0.0.0", so the readiness probe must target the
                         * regular test host rather than the bind address
                         */

                        TestTaskUtils::startAcceptorAndExecuteCallback(
                            callbackWithAssertions,
                            acceptor,
                            test::UtfArgsParser::host()                      /* readinessHost */,
                            httpPort                                        /* readinessPort */
                            );

                        if( ! waitOnServer && ! isCustomCallback && ! serverContext )
                        {
                            UTF_REQUIRE_EQUAL( requestsCount, echoContext -> messagesProcessed() );
                        }
                    }
                }
            }

            controlToken -> requestCancel();
        }

        static void startBrokerAndRunTests(
            SAA_in_opt      const bl::cpp::void_callback_t&                                 callbackTests,
            SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&          controlToken
            )
        {
            test::MachineGlobalTestLock lock;

            const auto processingBackend = bl::om::lockDisposable(
                utest::TestMessagingUtils::createTestMessagingBackend()
                );

            bl::messaging::BrokerFacade::execute(
                processingBackend,
                test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
                test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
                test::UtfArgsParser::port()                         /* inboundPort */,
                test::UtfArgsParser::port() + 1U                    /* outboundPort */,
                test::UtfArgsParser::threadsCount(),
                0U                                                  /* maxConcurrentTasks */,
                callbackTests,
                bl::om::copy( controlToken )
                );
        }
    };

    typedef TestRestUtilsT<> TestRestUtils;

} // utest

#endif /* __UTEST_TESTRESTUTILS_H_ */
