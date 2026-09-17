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
 * The out of line definitions of the heavy utest::TestMessagingUtilsT members, plus the explicit
 * instantiation which gives them one home per test module
 *
 * Defined inside the class these are implicitly inline, so every translation unit including
 * TestMessagingUtils.h instantiated them and the broker, client factory and backend machinery
 * underneath. Moving the bodies out removes the inline-ness
 *
 * This file is NOT compiled directly - it sits beside its header and is pulled in by a small
 * forwarding .cpp in each module which needs it, because projects/make/common.mk only globs .cpp
 * files inside a module directory. One forwarder per module, never two, or the explicit
 * instantiation below would be duplicated within a single binary
 *
 * See notes/plans/issues/test-instantiation-weight-deferral.md
 */

#include <utests/baselib/TestMessagingUtils.h>

namespace utest
{
    template < typename E >
    auto TestMessagingUtilsT< E >::createBrokerProtocolMessage(
            SAA_in                  const bl::messaging::MessageType::Enum          messageType,
            SAA_in                  const bl::uuid_t&                               conversationId,
            SAA_in_opt              const std::string&                              cookiesText,
            SAA_in_opt              const bl::uuid_t&                               messageId,
            SAA_in_opt              const std::string&                              tokenType
            )
            -> bl::om::ObjPtr< bl::messaging::BrokerProtocol >
    {
        /*
         * Note that g_tokenType must only ever be touched while g_tokenTypeLock is held,
         * so the cached value is copied into the local below inside the guard; reading it
         * after the guard has been released races with the lazy initialization above when
         * two threads call this on a cold cache
         */

        std::string effectiveTokenType;

        if( tokenType.empty() )
        {
            BL_MUTEX_GUARD( g_tokenTypeLock );

            if( g_tokenType.empty() )
            {
                using namespace bl::security;

                g_tokenType =
                    ( test::UtfArgsParser::path().empty() || test::UtfArgsParser::password().empty() ) ?
                        DummyAuthorizationCache::dummyTokenType()
                        :
                        cache_t::template createInstance< AuthorizationCache >(
                            AuthorizationServiceRest::create( test::UtfArgsParser::path() )
                            )
                            -> tokenType();
            }

            effectiveTokenType = g_tokenType;
        }
        else
        {
            effectiveTokenType = tokenType;
        }

        return bl::messaging::MessagingUtils::createBrokerProtocolMessage(
            messageType,
            conversationId,
            effectiveTokenType,
            cookiesText,
            messageId
            );
    }

    template < typename E >
    void TestMessagingUtilsT< E >::executeMessagingTests(
            SAA_in          const bl::om::ObjPtr< object_dispatch_t >&                      incomingObjectChannel,
            SAA_in          const callback_t&                                               callback
            )
    {
        using namespace bl;
        using namespace bl::messaging;

        tasks::scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepNone );

                const auto& cookiesText = getTokenData();

                const auto dataBlocksPool = datablocks_pool_t::createInstance();

                const auto incomingBlockChannel = om::lockDisposable(
                    MessagingClientBlockDispatchFromObject::createInstance< MessagingClientBlockDispatch >(
                        om::copy( incomingObjectChannel )
                        )
                    );

                const auto backend = om::lockDisposable(
                    client_factory_t::createClientBackendProcessingFromBlockDispatch(
                        om::copy( incomingBlockChannel )
                        )
                    );

                const auto asyncWrapper = om::lockDisposable(
                    client_factory_t::createAsyncWrapperFromBackend(
                        om::copy( backend ),
                        0U              /* threadsCount */,
                        0U              /* maxConcurrentTasks */,
                        om::copy( dataBlocksPool )
                        )
                    );

                callback( cookiesText, dataBlocksPool, eq, backend, asyncWrapper );

                eq -> flush();

                UTF_REQUIRE( eq -> isEmpty() );
            }
            );
    }

    template < typename E >
    void TestMessagingUtilsT< E >::dispatchCallback(
            SAA_in              const bl::om::ObjPtrCopyable< bl::om::Proxy >&  clientSink,
            SAA_in_opt          const bl::uuid_t&                               targetPeerIdExpected,
            SAA_in              const std::shared_ptr< DeferredAssertions >&    assertions,
            SAA_in              const bl::uuid_t&                               targetPeerId,
            SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
            SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
            )
    {
        if( targetPeerIdExpected != bl::uuids::nil() )
        {
            UTF_RECORD( *assertions, targetPeerId == targetPeerIdExpected );
        }

        bl::os::mutex_unique_lock guard;

        {
            const auto target = clientSink -> tryAcquireRef< object_dispatch_t >(
                object_dispatch_t::iid(),
                &guard
                );

            if( target )
            {
                target -> pushMessage( targetPeerId, brokerProtocol, payload );
            }
        }
    }

    template < typename E >
    void TestMessagingUtilsT< E >::verifyUniformMessageDistribution(
            SAA_in                  const clients_list_t&                           clients,
            SAA_in_opt              const std::uint64_t                             expectedPerClient,
            SAA_in_opt              const std::uint64_t                             tolerance,
            SAA_in_opt              const std::uint64_t                             maxSkewFactor
            )
    {
        using namespace bl;

        /*
         * An empty clients list would leave all four bounds at their initial values and
         * make every assertion below vacuously true
         */

        UTF_REQUIRE( ! clients.empty() );

        /*
         * Verify that all channels dispatched at least 2 messages or more
         * including the incoming channels
         */

        std::uint64_t receivedLower = std::numeric_limits< std::uint64_t >::max();
        std::uint64_t receivedUpper = std::numeric_limits< std::uint64_t >::min();

        std::uint64_t sentLower = std::numeric_limits< std::uint64_t >::max();
        std::uint64_t sentUpper = std::numeric_limits< std::uint64_t >::min();

        for( std::size_t i = 0U, count = clients.size(); i < count; ++i )
        {
            const auto& client = clients[ i ].second;

            const auto clientImpl = om::qi< client_t >( client -> outgoingBlockChannel() );

            UTF_REQUIRE( clientImpl -> noOfBlocksReceived() > 2U );
            UTF_REQUIRE( clientImpl -> noOfBlocksSent() > 2U );

            if( clientImpl -> noOfBlocksReceived() < receivedLower )
            {
                receivedLower = clientImpl -> noOfBlocksReceived();
            }

            if( clientImpl -> noOfBlocksReceived() > receivedUpper )
            {
                receivedUpper = clientImpl -> noOfBlocksReceived();
            }

            if( clientImpl -> noOfBlocksSent() < sentLower )
            {
                sentLower = clientImpl -> noOfBlocksSent();
            }

            if( clientImpl -> noOfBlocksSent() > sentUpper )
            {
                sentUpper = clientImpl -> noOfBlocksSent();
            }
        }

        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "Message bounds: "
                << "receivedLower="
                << receivedLower
                << "; receivedUpper="
                << receivedUpper
                << "; sentLower="
                << sentLower
                << "; sentUpper="
                << sentUpper
            );

        /*
         * The assertions are placed after the log line above, so a failure is always
         * preceded by the four printed bounds
         */

        UTF_REQUIRE( receivedLower > 0U && sentLower > 0U );

        if( clients.size() >= 2U )
        {
            /*
             * The bounded skew half - a single client has no spread at all
             */

            UTF_REQUIRE( receivedUpper <= receivedLower * maxSkewFactor );
            UTF_REQUIRE( sentUpper <= sentLower * maxSkewFactor );

            if( expectedPerClient )
            {
                /*
                 * ... and the spread, which is what 'uniform' actually means; the
                 * tolerance absorbs the association and heartbeat blocks which ride on
                 * the same connections
                 */

                UTF_REQUIRE( sentUpper - sentLower <= tolerance );
                UTF_REQUIRE( receivedUpper - receivedLower <= tolerance );
            }
        }

        if( expectedPerClient )
        {
            /*
             * The absolute floor half
             */

            UTF_REQUIRE( sentLower >= expectedPerClient );
            UTF_REQUIRE( receivedLower >= expectedPerClient );
        }
    }

    template < typename E >
    void TestMessagingUtilsT< E >::flushQueueWithRetriesOnTargetPeerNotFound(
            SAA_in              const bl::om::ObjPtr< ExecutionQueue >&         eq,
            SAA_inout_opt       std::size_t*                                    totalRetries
            )
    {
        using namespace bl;
        using namespace bl::tasks;
        using namespace bl::messaging;

        const std::size_t maxRetries = 2000U;
        std::map< Task*, std::size_t > retryCountsMap;

        for( ;; )
        {
            const auto task = eq -> top( true /* wait */ );

            if( ! task )
            {
                BL_ASSERT( eq -> isEmpty() );

                break;
            }

            if( ! task -> isFailed() )
            {
                eq -> pop( true /* wait */ );

                continue;
            }

            /*
             * The task has failed - check if we need to do a retry
             */

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( ServerErrorException& e )
            {
                const auto* ec = e.errorCode();

                if( ec && eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound ) == *ec )
                {
                    auto& retryCount = retryCountsMap[ task.get() ];

                    if( retryCount >= maxRetries )
                    {
                        throw;
                    }

                    ++retryCount;

                    if( totalRetries )
                    {
                        ++( *totalRetries );
                    }

                    os::sleep( time::milliseconds( 200L ) );

                    eq -> push_back( task );

                    continue;
                }

                throw;
            }
        }
    }

    template < typename E >
    void TestMessagingUtilsT< E >::startBrokerProxy(
            SAA_in_opt          const token_ptr_t&                  controlToken,
            SAA_in_opt          const bl::cpp::void_callback_t&     callback,
            SAA_in_opt          const unsigned short                proxyInboundPort,
            SAA_in_opt          const std::size_t                   noOfConnections,
            SAA_in_opt          const std::string&                  brokerHostName,
            SAA_in_opt          const unsigned short                brokerInboundPort,
            SAA_in_opt          const pool_ptr_t&                   dataBlocksPool,
            SAA_in_opt          const bl::time::time_duration&      heartbeatInterval,
            SAA_inout_opt       bl::om::Object**                    backendRef
            )
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::messaging;

        const auto controlTokenLocal =
            controlToken ?
                bl::om::copy( controlToken )
                :
                tasks::SimpleTaskControlTokenImpl::createInstance< tasks::TaskControlTokenRW >();

        const auto peerId = uuids::create();

        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "Proxy clients peerId: "
                << peerId
            );

        /*
         * For the unit test case we want to pass waitAllToConnect=true
         *
         * Also for testing purpose we want to set maxNoOfSmallBlocks and minSmallBlocksDeltaToLog
         * to some small values, so we can test correctly all the code paths and the logging, etc
         */

        const auto maxNoOfSmallBlocks = test::UtfArgsParser::isServer() ?
            50 * test::UtfArgsParser::connections() : test::UtfArgsParser::connections();

        const auto minSmallBlocksDeltaToLogDefault = maxNoOfSmallBlocks / 10U;

        const auto proxyBackend = bl::om::lockDisposable(
            ProxyBrokerBackendProcessingFactorySsl::create(
                test::UtfArgsParser::port()                     /* defaultInboundPort */,
                bl::om::copy( controlTokenLocal ),
                peerId,
                noOfConnections,
                getTestEndpointsList( brokerHostName, brokerInboundPort, 3U /* noOfEndpoints */ ),
                dataBlocksPool,
                0U                                              /* threadsCount */,
                0U                                              /* maxConcurrentTasks */,
                true                                            /* waitAllToConnect */,
                maxNoOfSmallBlocks,
                minSmallBlocksDeltaToLogDefault < 5U ?
                    5U : minSmallBlocksDeltaToLogDefault        /* minSmallBlocksDeltaToLog */
                )
            );

        if( backendRef )
        {
            *backendRef = proxyBackend.get();
        }

        bl::messaging::BrokerFacade::execute(
            proxyBackend,
            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
            proxyInboundPort                                    /* inboundPort */,
            proxyInboundPort + 1U                               /* outboundPort */,
            test::UtfArgsParser::threadsCount(),
            0U                                                  /* maxConcurrentTasks */,
            callback,
            om::copy( controlTokenLocal ),
            dataBlocksPool,
            cpp::copy( heartbeatInterval )
            );
    }

    template < typename E >
    auto TestMessagingUtilsT< E >::createTestMessagingBackend(
            SAA_in_opt      const bl::om::ObjPtr< bl::security::AuthorizationCache >&   authorizationCache
            )
            -> bl::om::ObjPtr< bl::messaging::BackendProcessing >
    {
        using namespace bl;
        using namespace bl::security;

        auto cache = om::copy( authorizationCache );

        if( ! cache )
        {
            cache =
                ( test::UtfArgsParser::path().empty() || test::UtfArgsParser::password().empty() ) ?
                    DummyAuthorizationCache::createInstance< AuthorizationCache >()
                    :
                    cache_t::template createInstance< AuthorizationCache >(
                        AuthorizationServiceRest::create( test::UtfArgsParser::path() )
                        );
        }

        return messaging::BrokerBackendProcessing::createInstance< messaging::BackendProcessing >(
            std::move( cache )
            );
    }

    template < typename E >
    void TestMessagingUtilsT< E >::forwardingBackendTests(
            SAA_in_opt      bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&&       controlToken,
            SAA_in_opt      const std::string&                                      cookiesText,
            SAA_in_opt      const std::string&                                      tokenType,
            SAA_in_opt      const std::string&                                      brokerHostName,
            SAA_in_opt      const unsigned short                                    brokerInboundPort,
            SAA_in          const std::size_t                                       noOfConnections
            )
    {
        using namespace bl;
        using namespace bl::tasks;
        using namespace bl::messaging;

        const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                const auto peerId1 = uuids::create();
                const auto peerId2 = uuids::create();

                BL_LOG_MULTILINE(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Peer id 1: "
                        << uuids::uuid2string( peerId1 )
                        << "\nPeer id 2: "
                        << uuids::uuid2string( peerId2 )
                    );

                const auto backendReference = om::ProxyImpl::createInstance< om::Proxy >( false /* strongRef*/ );

                const auto echoContext = om::lockDisposable(
                    echo::EchoServerProcessingContext::createInstance(
                        false                               /* isQuietMode */,
                        0UL                                 /* maxProcessingDelayInMicroseconds */,
                        false                               /* isGraphQLServer */,
                        false                               /* isAuthnticationAlwaysRequired */,
                        std::string()                       /* requiredContentType */,
                        om::copy( dataBlocksPool ),
                        om::copy( backendReference ),
                        cpp::copy( tokenType ),
                        cpp::copy( cookiesText )            /* tokenData */
                        )
                    );

                {

                    const auto loggingContext = logging_context_t::createInstance();

                    {
                        const auto backend1 = om::lockDisposable(
                            ForwardingBackendProcessingFactoryDefaultSsl::create(
                                brokerInboundPort       /* defaultInboundPort */,
                                om::copy( controlToken ),
                                peerId1,
                                noOfConnections,
                                getTestEndpointsList( brokerHostName, brokerInboundPort ),
                                dataBlocksPool,
                                0U                      /* threadsCount */,
                                0U                      /* maxConcurrentTasks */,
                                true                    /* waitAllToConnect */
                                )
                            );

                        {
                            auto proxy = om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );
                            proxy -> connect( loggingContext.get() );
                            backend1 -> setHostServices( std::move( proxy ) );
                        }

                        const auto backend2 = om::lockDisposable(
                            ForwardingBackendProcessingFactoryDefaultSsl::create(
                                brokerInboundPort       /* defaultInboundPort */,
                                om::copy( controlToken ),
                                peerId2,
                                noOfConnections,
                                getTestEndpointsList( brokerHostName, brokerInboundPort ),
                                dataBlocksPool,
                                0U                      /* threadsCount */,
                                0U                      /* maxConcurrentTasks */,
                                true                    /* waitAllToConnect */
                                )
                            );

                        {
                            auto proxy = om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );
                            proxy -> connect(
                                static_cast< messaging::AsyncBlockDispatcher* >( echoContext.get() )
                                );
                            backend2 -> setHostServices( std::move( proxy ) );
                        }

                        os::sleep( time::seconds( 2L ) );

                        {
                            BL_SCOPE_EXIT(
                                {
                                    backendReference -> disconnect();
                                }
                                );

                            backendReference -> connect( backend2.get() );

                            const auto conversationId = uuids::create();

                            const auto brokerProtocol = createBrokerProtocolMessage(
                                MessageType::AsyncRpcDispatch,
                                conversationId,
                                cookiesText,
                                uuids::create() /* messageId */,
                                tokenType
                                );

                            const auto requestMetadata = bl::dm::http::HttpRequestMetadata::createInstance();

                            requestMetadata -> method( "GET" );
                            requestMetadata -> urlPath( "/foo/bar" );

                            const auto requestMetadataPayload =
                                bl::dm::http::HttpRequestMetadataPayload::createInstance();

                            requestMetadataPayload -> httpRequestMetadata( std::move( requestMetadata ) );

                            brokerProtocol -> passThroughUserData(
                                bl::dm::DataModelUtils::castTo< bl::dm::Payload >( requestMetadataPayload )
                                );

                            const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
                                TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                                );

                            const auto dataBlock = MessagingUtils::serializeObjectsToBlock(
                                brokerProtocol,
                                payload,
                                dataBlocksPool
                                );

                            const auto messageTask = backend1 -> createBackendProcessingTask(
                                BackendProcessing::OperationId::Put,
                                BackendProcessing::CommandId::None,
                                uuids::nil()                                    /* sessionId */,
                                BlockTransferDefs::chunkIdDefault(),
                                peerId1                                         /* sourcePeerId */,
                                peerId2                                         /* targetPeerId */,
                                dataBlock
                                );

                            eq -> push_back( messageTask );
                            eq -> waitForSuccess( messageTask );

                            os::sleep( time::seconds( 2L ) );

                            UTF_REQUIRE( loggingContext -> messageLogged() );
                            UTF_REQUIRE_EQUAL( 1UL, echoContext -> messagesProcessed() );
                        }
                    }
                }

                controlToken -> requestCancel();
            }
            );
    }
    template class TestMessagingUtilsT< void >;

} // utest
