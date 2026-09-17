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

        /**
         * @brief A live view of the echo server context's processed messages counter
         *
         * The echo context is created and owned by httpRestWithMessagingBackendTests, so a
         * caller supplied callback which needs to sample the counter around individual
         * requests has to be handed a getter rather than a value; the getter holds its own
         * reference to the context, so it also stays valid after the fixture has returned
         *
         * Note that it always reports the echo context - a caller which replaces it via
         * serverContextFactory will see the counter stay at zero
         */

        typedef bl::cpp::function< std::size_t () > messages_processed_callback_t;

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
            -> bl::om::ObjPtr< bl::tasks::SimpleHttpSslTaskImpl >;

        static void httpRunSimpleRequest(
            SAA_in          const unsigned short                                            httpPort,
            SAA_in_opt      const std::string&                                              tokenData = defaultToken(),
            SAA_in_opt      const std::size_t                                               requestsCount = 1U,
            SAA_in_opt      const bool                                                      isQuietMode = false
            );

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
            SAA_in_opt      const server_context_factory_t&                                 serverContextFactory = server_context_factory_t(),
            SAA_inout_opt   messages_processed_callback_t*                                  messagesProcessedOut = nullptr,
            SAA_in_opt      const bl::uuid_t&                                               gatewayTargetPeerId = bl::uuids::nil()
            );

        static void startBrokerAndRunTests(
            SAA_in_opt      const bl::cpp::void_callback_t&                                 callbackTests,
            SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&          controlToken
            );
    };

    typedef TestRestUtilsT<> TestRestUtils;

} // utest

#endif /* __UTEST_TESTRESTUTILS_H_ */
