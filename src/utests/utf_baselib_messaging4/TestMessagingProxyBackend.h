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

#include <baselib/core/BuildInfo.h>

#include <utests/baselib/TestMessagingUtils.h>
#include <utests/baselib/UtfCrypto.h>

namespace
{
    void exceptionThrowHook2( SAA_in const bl::BaseException& exception ) NOEXCEPT
    {
        /*
         * This hooks is to catch the following issue (it happens on Windows only):
         *
         * std::exception::what: System error has occurred: The semaphore timeout period has expired
         * system:121
         */

        if( bl::os::onWindows() && std::string( "bl::SystemException" ) == exception.fullTypeName() )
        {
            const auto* errorCode = exception.errorCode();

            if( errorCode && bl::eh::error_code( 121, bl::eh::system_category() ) == *errorCode )
            {
                BL_RIP_MSG( exception.details() );
            }
        }
    }

    void exceptionThrowHook3( SAA_in const bl::BaseException& exception ) NOEXCEPT
    {
        /*
         * This hooks is to catch the following issue:
         *
         * std::exception::what: Server error has occurred: Cannot assign requested address
         * generic:99
         */

        if( std::string( "bl::ServerErrorException" ) == exception.fullTypeName() )
        {
            const auto* errorCode = exception.errorCode();

            if( errorCode && bl::eh::error_code( 99, bl::eh::generic_category() ) == *errorCode )
            {
                BL_RIP_MSG( exception.details() );
            }
        }
    }

} // __unnamed

UTF_AUTO_TEST_CASE( IO_MessagingProxyBackendTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    BL_EXCEPTION_HOOKS_THROW_GUARD( &exceptionThrowHook2 )

    typedef utest::TestMessagingUtils utils_t;

    const os::port_t brokerInboundPort = test::UtfArgsParser::port() + 2U;

    const auto heartbeatInterval =  time::seconds( 3L );

    om::Object* proxyBackendRef = nullptr;

    const auto callbackTests = [ & ]() -> void
    {
        const auto sendSingleMessageTests = [](
            SAA_in          const unsigned short                                        port1,
            SAA_in          const unsigned short                                        port2,
            SAA_in          const bl::uuid_t&                                           peerId1,
            SAA_in          const bl::uuid_t&                                           peerId2
            )
            -> void
        {
            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "\n************************* sendSingleMessageTests [begin] *************************\n"
                    << "\n*** port1: "
                    << port1
                    << "\n*** port2: "
                    << port2
                    << "\n*** peerId1: "
                    << peerId1
                    << "\n*** peerId2: "
                    << peerId2
                );

            BL_SCOPE_EXIT(
                {
                    BL_LOG_MULTILINE(
                        Logging::debug(),
                        BL_MSG()
                            << "\n************************* sendSingleMessageTests [end] *************************\n"
                            << "*** Exited with exception: "
                            << ( std::current_exception() ? "true" : "false" )
                            << "\n"
                        );
                }
                );

            const auto incomingSink1 = om::lockDisposable(
                MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                    [ = ](
                        SAA_in              const bl::uuid_t&                               targetPeerId,
                        SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                        SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                        )
                        -> void
                    {
                        BL_UNUSED( payload );

                        UTF_REQUIRE_EQUAL( peerId1, uuids::string2uuid( brokerProtocol -> sourcePeerId() ) );
                        UTF_REQUIRE_EQUAL( peerId2, uuids::string2uuid( brokerProtocol -> targetPeerId() ) );
                        UTF_REQUIRE_EQUAL( peerId2, targetPeerId );
                    }
                    )
                );

            const auto incomingSink2 = om::lockDisposable(
                MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                    [ = ](
                        SAA_in              const bl::uuid_t&                               targetPeerId,
                        SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                        SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                        )
                        -> void
                    {
                        BL_UNUSED( payload );

                        UTF_REQUIRE_EQUAL( peerId2, uuids::string2uuid( brokerProtocol -> sourcePeerId() ) );
                        UTF_REQUIRE_EQUAL( peerId1, uuids::string2uuid( brokerProtocol -> targetPeerId() ) );
                        UTF_REQUIRE_EQUAL( peerId1, targetPeerId );
                    }
                    )
                );

            const om::ObjPtrCopyable< om::Proxy > clientSink =
                om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

            const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

            const auto incomingObjectChannel = om::lockDisposable(
                MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                    cpp::bind(
                        &utest::TestMessagingUtils::dispatchCallback,
                        clientSink,
                        uuids::nil()    /* targetPeerIdExpected */,
                        dispatchAssertions,
                        _1              /* targetPeerId */,
                        _2              /* brokerProtocol */,
                        _3              /* payload */
                        )
                    )
                );

            /*
             * The number of TargetPeerNotFound retries each of the two directions required
             *
             * When the proxy is asked to forward a message over an outbound channel it has not
             * announced to the real broker yet it prefixes the dispatch task with an associate
             * message task, precisely so the real broker's routing cache learns the target peer
             * before the message arrives. Without that prefix the messages would still all be
             * delivered - the proxy re-announces the peer on a 5s timer anyway - and the only
             * observable difference would be the retries these counters record
             */

            std::size_t retriesFirst = 0U;
            std::size_t retriesSecond = 0U;

            utils_t::executeMessagingTests(
                incomingObjectChannel,
                [ & ](
                    SAA_in          const std::string&                                      cookiesText,
                    SAA_in          const bl::om::ObjPtr< datablocks_pool_type >&           dataBlocksPool,
                    SAA_in          const bl::om::ObjPtr< ExecutionQueue >&                 eq,
                    SAA_in          const bl::om::ObjPtr< BackendProcessing >&              backend,
                    SAA_in          const bl::om::ObjPtr< utils_t::async_wrapper_t >&       asyncWrapper
                    ) -> void
                {
                    const auto conversationId = uuids::create();

                    const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        conversationId,
                        cookiesText
                        );

                    const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
                        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    /*
                     * First create a client for the proxy and then create such client for the
                     * real broker
                     *
                     * The peer id is either fixed or a unique one is generated for each connection
                     */

                    om::ObjPtrDisposable< MessagingClientObject > client1;
                    om::ObjPtrDisposable< MessagingClientObject > client2;

                    {
                        BL_EXCEPTION_HOOKS_THROW_GUARD( &exceptionThrowHook3 )

                        /*
                         * Create two messaging clients - one to the real broker and one to the proxy -
                         * send messages between them and verify that the source and target peer ids
                         * are preserved correctly in the broker protocol message
                         *
                         * Note that these don't own the backend and the queue, so when they get
                         * disposed they will not actually dispose the backend, but just tear down
                         * the connections
                         */

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Creating connections for client1..."
                            );

                        auto connections1 = utils_t::createNoOfConnections(
                            1U /* noOfConnections */,
                            test::UtfArgsParser::host(),
                            port1
                            );
                        UTF_REQUIRE_EQUAL( connections1.size(), 1U /* noOfConnections */ );

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Creating connections for client2..."
                            );

                        auto connections2 = utils_t::createNoOfConnections(
                            1U /* noOfConnections */,
                            test::UtfArgsParser::host(),
                            port2
                            );
                        UTF_REQUIRE_EQUAL( connections2.size(), 1U /* noOfConnections */ );

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Creating client1..."
                            );

                        auto blockDispatch1 = om::lockDisposable(
                            utils_t::client_factory_t::createWithSmartDefaults(
                                om::copy( eq ),
                                peerId1,
                                om::copy( backend ),
                                om::copy( asyncWrapper ),
                                test::UtfArgsParser::host()                         /* host */,
                                port1                                               /* inboundPort */,
                                port1 + 1U                                          /* outboundPort */,
                                std::move( connections1[ 0U ].first )               /* inboundConnection */,
                                std::move( connections1[ 0U ].second )              /* outboundConnection */,
                                om::copy( dataBlocksPool )
                                )
                            );

                        client1 = om::lockDisposable(
                            MessagingClientObjectImplDefault::createInstance< MessagingClientObject >(
                                om::qi< MessagingClientBlockDispatch >( blockDispatch1 ),
                                dataBlocksPool
                                )
                            );

                        blockDispatch1.detachAsObjPtr();

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Creating client2..."
                            );

                        auto blockDispatch2 = om::lockDisposable(
                            utils_t::client_factory_t::createWithSmartDefaults(
                                om::copy( eq ),
                                peerId2,
                                om::copy( backend ),
                                om::copy( asyncWrapper ),
                                test::UtfArgsParser::host()                         /* host */,
                                port2                                               /* inboundPort */,
                                port2 + 1U                                          /* outboundPort */,
                                std::move( connections2[ 0U ].first )               /* inboundConnection */,
                                std::move( connections2[ 0U ].second )              /* outboundConnection */,
                                om::copy( dataBlocksPool )
                                )
                            );

                        client2 = om::lockDisposable(
                            MessagingClientObjectImplDefault::createInstance< MessagingClientObject >(
                                om::qi< MessagingClientBlockDispatch >( blockDispatch2 ),
                                dataBlocksPool
                                )
                            );

                        blockDispatch2.detachAsObjPtr();
                    }

                    /*
                     * Just execute a bunch of messages at random and then verify that they have arrived
                     * and that all channels were used fairly
                     */

                    {
                        BL_SCOPE_EXIT(
                            {
                                clientSink -> disconnect();
                            }
                            );

                        scheduleAndExecuteInParallel(
                            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eqLocal ) -> void
                            {
                                eqLocal -> setOptions( ExecutionQueue::OptionKeepFailed );
                                eqLocal -> setThrottleLimit( utils_t::sender_connection_t::BLOCK_QUEUE_SIZE / 2U );

                                /*
                                 * Send two messages - first client1 -> client2 and then client2 -> client1
                                 * and before sending each message we connect the respective sink that knows
                                 * how to validate the message
                                 *
                                 * Note also that the ordering is important and that we always have to send
                                 * client1 -> client2 message first as if we are sending messages between
                                 * clients connected to proxy and real broker we need to make sure that the
                                 * proxy -> broker message is sent first to ensure the proxy client has
                                 * registered and will receive the reverse message (otherwise we have to do
                                 * an arbitrary wait as we don't know how long it would take for the proxy
                                 * client to register)
                                 */

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message was being scheduled to be sent to incomingSink1..."
                                    );

                                clientSink -> connect( incomingSink1.get() );

                                eqLocal -> push_back(
                                    ExternalCompletionTaskImpl::createInstance< Task >(
                                        cpp::bind(
                                            &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                            om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                client1 -> outgoingObjectChannel().get()
                                                ),
                                            peerId2,
                                            om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                            om::ObjPtrCopyable< Payload >( payload ),
                                            _1 /* onReady - the completion callback */
                                            )
                                        )
                                    );

                                utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal, &retriesFirst );

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message send to incomingSink1 successfully"
                                    );

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message was being scheduled to be sent to incomingSink2..."
                                    );


                                clientSink -> connect( incomingSink2.get() );

                                eqLocal -> push_back(
                                    ExternalCompletionTaskImpl::createInstance< Task >(
                                        cpp::bind(
                                            &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                            om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                client2 -> outgoingObjectChannel().get()
                                                ),
                                            peerId1,
                                            om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                            om::ObjPtrCopyable< Payload >( payload ),
                                            _1 /* onReady - the completion callback */
                                            )
                                        )
                                    );

                                utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal, &retriesSecond );

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message send to incomingSink2 successfully"
                                    );

                                clientSink -> disconnect();
                            }
                            );
                    }
                }
                );

            dispatchAssertions -> requireNone();

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "TargetPeerNotFound retries: client1 -> client2: "
                    << retriesFirst
                    << "; client2 -> client1: "
                    << retriesSecond
                );

            /*
             * The reverse direction message is the one the associate message prefix guarantees -
             * by the time client2 sends to client1 the proxy has already associated client1 with
             * the proxy peer id on the real broker, so it must never need a retry
             */

            UTF_REQUIRE_EQUAL( 0U, retriesSecond );

            /*
             * The very first message may legitimately race the initial connection handshake, so
             * this one is a check rather than a requirement - the counts are logged above so that
             * any drift is visible even when it stays within the bound
             */

            UTF_CHECK( retriesFirst <= 1U );
        };

        /*
         * Note that runPruneProbes is an EXPLICIT parameter rather than a defaulted one: a
         * default argument on a lambda parameter is a C++14 extension which gcc rejects at
         * -Werror, and the probes must run only while the last batch of clients is still alive
         */

        const auto executeTests = [ brokerInboundPort, &proxyBackendRef ](
            SAA_in          const std::size_t                                           noOfConnections,
            SAA_in_opt      const bl::uuid_t                                            peerId,
            SAA_in          const bool                                                  runPruneProbes
            )
            -> void
        {
            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "\n************************* executeTests [begin] *************************\n"
                    << "\n*** noOfConnections: "
                    << noOfConnections
                    << "\n*** peerId: "
                    << peerId
                );

            BL_SCOPE_EXIT(
                {
                    BL_LOG_MULTILINE(
                        Logging::debug(),
                        BL_MSG()
                            << "\n************************* executeTests [end] *************************\n"
                            << "*** Exited with exception: "
                            << ( std::current_exception() ? "true" : "false" )
                            << "\n"
                        );
                }
                );

            UTF_REQUIRE( noOfConnections >= 1 );

            std::atomic< std::size_t > noOfMessagesDelivered( 0U );

            const auto incomingSink = om::lockDisposable(
                MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                    [ & ](
                        SAA_in              const bl::uuid_t&                               targetPeerId,
                        SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                        SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                        )
                        -> void
                    {
                        BL_UNUSED( targetPeerId );
                        BL_UNUSED( brokerProtocol );
                        BL_UNUSED( payload );

                        ++noOfMessagesDelivered;
                    }
                    )
                );

            const om::ObjPtrCopyable< om::Proxy > clientSink =
                om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

            const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

            const auto incomingObjectChannel = om::lockDisposable(
                MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                    cpp::bind(
                        &utest::TestMessagingUtils::dispatchCallback,
                        clientSink,
                        uuids::nil()    /* targetPeerIdExpected */,
                        dispatchAssertions,
                        _1              /* targetPeerId */,
                        _2              /* brokerProtocol */,
                        _3              /* payload */
                        )
                    )
                );

            utils_t::executeMessagingTests(
                incomingObjectChannel,
                [ & ](
                    SAA_in          const std::string&                                      cookiesText,
                    SAA_in          const bl::om::ObjPtr< datablocks_pool_type >&           dataBlocksPool,
                    SAA_in          const bl::om::ObjPtr< ExecutionQueue >&                 eq,
                    SAA_in          const bl::om::ObjPtr< BackendProcessing >&              backend,
                    SAA_in          const bl::om::ObjPtr< utils_t::async_wrapper_t >&       asyncWrapper
                    ) -> void
                {
                    const auto conversationId = uuids::create();

                    const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        conversationId,
                        cookiesText
                        );

                    const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
                        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    /*
                     * Create noOfConnections messaging clients backed by the same async wrapper
                     *
                     * Note that these don't own the backend and the queue, so when they get
                     * disposed they will not actually dispose the backend, but just tear down
                     * the connections
                     */

                    auto proxyConnections = utils_t::createNoOfConnections( noOfConnections );
                    UTF_REQUIRE_EQUAL( proxyConnections.size(), noOfConnections );

                    auto brokerConnections = utils_t::createNoOfConnections(
                        noOfConnections,
                        test::UtfArgsParser::host(),
                        brokerInboundPort
                        );
                    UTF_REQUIRE_EQUAL( brokerConnections.size(), noOfConnections );

                    utils_t::clients_list_t proxyClients;
                    utils_t::clients_list_t brokerClients;

                    proxyClients.reserve( noOfConnections );
                    brokerClients.reserve( noOfConnections );

                    for( std::size_t i = 0; i < noOfConnections; ++i )
                    {
                        /*
                         * First create a client for the proxy and then create such client for the
                         * real broker
                         *
                         * The peer id is either fixed or a unique one is generated for each connection
                         */

                        {
                            const auto clientPeerId = uuids::nil() == peerId ? uuids::create() : peerId;

                            auto blockDispatch = om::lockDisposable(
                                utils_t::client_factory_t::createWithSmartDefaults(
                                    om::copy( eq ),
                                    clientPeerId,
                                    om::copy( backend ),
                                    om::copy( asyncWrapper ),
                                    test::UtfArgsParser::host()                         /* host */,
                                    test::UtfArgsParser::port()                         /* inboundPort */,
                                    test::UtfArgsParser::port() + 1U                    /* outboundPort */,
                                    std::move( proxyConnections[ i ].first )            /* inboundConnection */,
                                    std::move( proxyConnections[ i ].second )           /* outboundConnection */,
                                    om::copy( dataBlocksPool )
                                    )
                                );

                            auto client = om::lockDisposable(
                                MessagingClientObjectImplDefault::createInstance< MessagingClientObject >(
                                    om::qi< MessagingClientBlockDispatch >( blockDispatch ),
                                    dataBlocksPool
                                    )
                                );

                            blockDispatch.detachAsObjPtr();

                            proxyClients.emplace_back( std::make_pair( clientPeerId, std::move( client ) ) );
                        }

                        {
                            const auto clientPeerId = uuids::nil() == peerId ? uuids::create() : peerId;

                            auto blockDispatch = om::lockDisposable(
                                utils_t::client_factory_t::createWithSmartDefaults(
                                    om::copy( eq ),
                                    clientPeerId,
                                    om::copy( backend ),
                                    om::copy( asyncWrapper ),
                                    test::UtfArgsParser::host()                         /* host */,
                                    brokerInboundPort                                   /* inboundPort */,
                                    brokerInboundPort + 1U                              /* outboundPort */,
                                    std::move( brokerConnections[ i ].first )           /* inboundConnection */,
                                    std::move( brokerConnections[ i ].second )          /* outboundConnection */,
                                    om::copy( dataBlocksPool )
                                    )
                                );

                            auto client = om::lockDisposable(
                                MessagingClientObjectImplDefault::createInstance< MessagingClientObject >(
                                    om::qi< MessagingClientBlockDispatch >( blockDispatch ),
                                    dataBlocksPool
                                    )
                                );

                            blockDispatch.detachAsObjPtr();

                            brokerClients.emplace_back( std::make_pair( clientPeerId, std::move( client ) ) );
                        }
                    }

                    /*
                     * Just execute a bunch of messages at random and then verify that they have arrived
                     * and that all channels were used fairly
                     */

                    {
                        BL_SCOPE_EXIT(
                            {
                                clientSink -> disconnect();
                            }
                            );

                        clientSink -> connect( incomingSink.get() );

                        scheduleAndExecuteInParallel(
                            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eqLocal ) -> void
                            {
                                eqLocal -> setOptions( ExecutionQueue::OptionKeepFailed );
                                eqLocal -> setThrottleLimit( utils_t::sender_connection_t::BLOCK_QUEUE_SIZE / 2U );

                                UTF_REQUIRE_EQUAL( proxyClients.size(), brokerClients.size() );

                                const auto vectorsSize = proxyClients.size();

                                /*
                                 * The TargetPeerNotFound retries accumulated over both bulk
                                 * blocks below - see the comment on the counters in
                                 * sendSingleMessageTests for what these are pinning
                                 */

                                std::size_t retriesBulk = 0U;

                                {
                                    BL_LOG(
                                        Logging::debug(),
                                        BL_MSG()
                                            << "\n**** Send some messages to the clients .... [begin]\n"
                                        );

                                    BL_SCOPE_EXIT(
                                        {
                                            BL_LOG(
                                                Logging::debug(),
                                                BL_MSG()
                                                    << "\n**** Send some messages to the clients .... [end]\n"
                                                    << "*** Exited with exception: "
                                                    << ( std::current_exception() ? "true" : "false" )
                                                    << "\n"
                                                );
                                        }
                                        );
                                    /*
                                     * Send some messages to the clients and verify that these are also delivered
                                     */

                                    noOfMessagesDelivered = 0;

                                    const std::size_t noOfBlocks = 10 * vectorsSize;

                                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                                    {
                                        const auto pos1 = i % vectorsSize;
                                        const auto pos2 = ( i + 1 ) % vectorsSize;

                                        const auto& sourceClient = proxyClients[ pos1 ].second;
                                        const auto& targetPeerId = proxyClients[ pos2 ].first;

                                        eqLocal -> push_back(
                                            ExternalCompletionTaskImpl::createInstance< Task >(
                                                cpp::bind(
                                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                        sourceClient -> outgoingObjectChannel().get()
                                                        ),
                                                    targetPeerId,
                                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                                    om::ObjPtrCopyable< Payload >( payload ),
                                                    _1 /* onReady - the completion callback */
                                                    )
                                                )
                                            );

                                        /*
                                         * Send at least one request successfully before we start parallelizing
                                         * the rest of the requests to avoid multiple unnecessary request to the
                                         * authorization service (we only need one to populate the cache)
                                         */

                                        if( 0U == i )
                                        {
                                            utils_t::flushQueueWithRetriesOnTargetPeerNotFound(
                                                eqLocal,
                                                &retriesBulk
                                                );
                                        }
                                    }

                                    utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal, &retriesBulk );

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );

                                    BL_LOG(
                                        Logging::debug(),
                                        BL_MSG()
                                            << "TargetPeerNotFound retries so far: "
                                            << retriesBulk
                                        );

                                    /*
                                     * A loose bound which still fails loudly if the associate
                                     * message prefix disappears - a regression yields roughly one
                                     * retry per message, i.e. 10 * vectorsSize of them
                                     */

                                    UTF_CHECK( retriesBulk <= vectorsSize );
                                }

                                {
                                    BL_LOG(
                                        Logging::debug(),
                                        BL_MSG()
                                            << "\n**** Send messages from proxy to real brokers.... [begin]\n"
                                        );

                                    BL_SCOPE_EXIT(
                                        {
                                            BL_LOG(
                                                Logging::debug(),
                                                BL_MSG()
                                                    << "\n**** Send messages from proxy to real brokers.... [end]\n"
                                                    << "*** Exited with exception: "
                                                    << ( std::current_exception() ? "true" : "false" )
                                                    << "\n"
                                                );
                                        }
                                        );

                                    /*
                                     * Send some messages to between the proxy clients and the real broker
                                     * clients and vice versa
                                     */

                                    const auto getRandomPos = [ vectorsSize ]() -> std::size_t
                                    {
                                        return random::getUniformRandomUnsignedValue< std::size_t >(
                                            vectorsSize - 1
                                            );
                                    };

                                    const auto getCoinToss = []() -> bool
                                    {
                                        return 0 == random::getUniformRandomUnsignedValue< int >( 1 );
                                    };

                                    noOfMessagesDelivered = 0;

                                    const std::size_t noOfBlocks = 10 * vectorsSize;

                                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                                    {
                                        /*
                                         * Indexes of the clients are chosen at random and also the source
                                         * vs. the target are chosen to be from proxy to broker client or
                                         * vice versa (again at random)
                                         */

                                        const auto pos1 = getRandomPos();
                                        const auto pos2 = getRandomPos();

                                        const auto coinToss = getCoinToss();

                                        const auto& sourceClient =
                                            coinToss ? proxyClients[ pos1 ].second : brokerClients[ pos1 ].second ;

                                        const auto& targetPeerId =
                                            coinToss ? brokerClients[ pos2 ].first : proxyClients[ pos2 ].first;

                                        eqLocal -> push_back(
                                            ExternalCompletionTaskImpl::createInstance< Task >(
                                                cpp::bind(
                                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                        sourceClient -> outgoingObjectChannel().get()
                                                        ),
                                                    targetPeerId,
                                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                                    om::ObjPtrCopyable< Payload >( payload ),
                                                    _1 /* onReady - the completion callback */
                                                    )
                                                )
                                            );

                                        /*
                                         * Send at least one request successfully before we start parallelizing
                                         * the rest of the requests to avoid multiple unnecessary request to the
                                         * authorization service (we only need one to populate the cache)
                                         */

                                        if( 0U == i )
                                        {
                                            utils_t::flushQueueWithRetriesOnTargetPeerNotFound(
                                                eqLocal,
                                                &retriesBulk
                                                );
                                        }
                                    }

                                    utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal, &retriesBulk );

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );

                                    BL_LOG(
                                        Logging::debug(),
                                        BL_MSG()
                                            << "TargetPeerNotFound retries so far: "
                                            << retriesBulk
                                        );

                                    /*
                                     * A loose bound which still fails loudly if the associate
                                     * message prefix disappears - a regression yields roughly one
                                     * retry per message, i.e. 10 * vectorsSize of them
                                     */

                                    UTF_CHECK( retriesBulk <= vectorsSize );
                                }
                            }
                            );
                    }

                    if( runPruneProbes )
                    {
                        /*
                         * The prune arms which the polling loops at the end of this case cannot
                         * reach, because by the time those run every client has already
                         * disconnected
                         *
                         * (b) is the resurrection arm - every currently active peer id is
                         * erased from m_clientsPruneState on each check, which is what stops a
                         * live, reconnecting client from being pruned mid-session. A regression
                         * there makes the proxy forget a connected peer's channel associations,
                         * which then shows up as TargetPeerNotFound storms that the retry
                         * helper hides
                         *
                         * (d) is the self-healing arm - an active peer id missing from
                         * m_clientsState is re-inserted rather than left out forever
                         *
                         * The intervals are made very short here and restored below, before the
                         * existing polling loops, so their assertions are unchanged
                         */

                        UTF_REQUIRE( proxyBackendRef );

                        const auto proxyBackend =
                            om::qi< ProxyBrokerBackendProcessingFactorySsl::proxy_backend_t >( proxyBackendRef );

                        BL_SCOPE_EXIT(
                            {
                                proxyBackend -> setClientsPruneIntervals(
                                    time::seconds( 3L )         /* clientsPruneCheckInterval */,
                                    time::seconds( 12L )        /* clientsPruneInterval */
                                    );
                            }
                            );

                        proxyBackend -> setClientsPruneIntervals(
                            time::seconds( 1L )                 /* clientsPruneCheckInterval */,
                            time::seconds( 3L )                 /* clientsPruneInterval */
                            );

                        /*
                         * setClientsPruneIntervals() calls m_timer.runNow(), so the new
                         * intervals take effect immediately rather than at the next natural
                         * tick; six seconds is several prune checks at the interval above
                         */

                        os::sleep( time::seconds( 6L ) );

                        {
                            std::unordered_set< bl::uuid_t > activeClients;
                            std::unordered_set< bl::uuid_t > pendingPrune;

                            proxyBackend -> getCurrentState( &activeClients, &pendingPrune );

                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Prune probes: active clients "
                                    << activeClients.size()
                                    << "; pending prune "
                                    << pendingPrune.size()
                                );

                            UTF_REQUIRE( ! activeClients.empty() );

                            for( const auto& pair : proxyClients )
                            {
                                const auto& clientPeerId = pair.first;

                                /*
                                 * Arm (d) seen from the outside - a live client is registered
                                 */

                                UTF_REQUIRE( cpp::contains( activeClients, clientPeerId ) );

                                /*
                                 * Arm (b) - with live clients, no live peer may be pending
                                 * prune even though the intervals are now very short
                                 */

                                UTF_REQUIRE( ! cpp::contains( pendingPrune, clientPeerId ) );
                            }

                            /*
                             * Documentation only: a bare requirement that the whole set is
                             * empty is not safe on a live 24 connection fan-out
                             */

                            UTF_CHECK( pendingPrune.empty() );
                        }

                        {
                            /*
                             * Arm (c), the full ladder, for a peer id which never had a real
                             * connection: registered by peerConnectedNotify(), pending prune on
                             * the SECOND sighting, and gone once the prune interval elapses
                             */

                            const auto freshPeerId = uuids::create();

                            UTF_REQUIRE(
                                ! om::qi< AcceptorNotify >( proxyBackendRef ) -> peerConnectedNotify(
                                    freshPeerId,
                                    tasks::CompletionCallback()
                                    )
                                );

                            {
                                std::unordered_set< bl::uuid_t > activeClients;

                                proxyBackend -> getCurrentState( &activeClients, nullptr /* pendingPrune */ );

                                UTF_REQUIRE( cpp::contains( activeClients, freshPeerId ) );
                            }

                            const std::size_t maxProbeRetries = 60U;

                            bool wasPendingPrune = false;
                            bool wasPruned = false;

                            for( std::size_t retries = 0U; retries < maxProbeRetries; ++retries )
                            {
                                std::unordered_set< bl::uuid_t > activeClients;
                                std::unordered_set< bl::uuid_t > pendingPrune;

                                proxyBackend -> getCurrentState( &activeClients, &pendingPrune );

                                if( cpp::contains( pendingPrune, freshPeerId ) )
                                {
                                    wasPendingPrune = true;
                                }

                                if( ! cpp::contains( activeClients, freshPeerId ) )
                                {
                                    wasPruned = true;

                                    break;
                                }

                                /*
                                 * Polled twice per second: the peer sits in pendingPrune for
                                 * roughly the three second prune interval, and the snapshot in
                                 * which it is pruned has it in NEITHER set, so the intermediate
                                 * state has to be caught while it lasts
                                 */

                                os::sleep( time::milliseconds( 500 ) );
                            }

                            UTF_REQUIRE( wasPendingPrune );
                            UTF_REQUIRE( wasPruned );
                        }
                    }
                }
                );

            dispatchAssertions -> requireNone();
        };

        {
            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                brokerInboundPort                       /* port2 */,
                uuids::create()                         /* peerId1 */,
                uuids::create()                         /* peerId2 */
                );

            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                test::UtfArgsParser::port()             /* port2 */,
                uuids::create()                         /* peerId1 */,
                uuids::create()                         /* peerId2 */
                );

            sendSingleMessageTests(
                brokerInboundPort                       /* port1 */,
                brokerInboundPort                       /* port2 */,
                uuids::create()                         /* peerId1 */,
                uuids::create()                         /* peerId2 */
                );

            const auto stickyPeerId1 = uuids::create();
            const auto stickyPeerId2 = uuids::create();

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Sticky peer id 1 is "
                    << str::quoteString( uuids::uuid2string( stickyPeerId1 ) )
                );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Sticky peer id 2 is "
                    << str::quoteString( uuids::uuid2string( stickyPeerId2 ) )
                );

            /*
             * A peer id that has been connected to the proxy (stickyPeerId1) can disconnect and
             * then immediately connect to the broker directly (that scenario should work)
             *
             * However a peer id that was connected to the broker directly (stickyPeerId2)
             * cannot disconnect and then immediately connect to the proxy because the broker will
             * detect the connection was torn down only after the heartbeatInterval
             *
             * Therefore we need to wait for at least heartbeatInterval before we try to reuse
             * the peer id that was connected to the broker directly (stickyPeerId2) with the
             * proxy
             */

            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                brokerInboundPort                       /* port2 */,
                stickyPeerId1                           /* peerId1 */,
                stickyPeerId2                           /* peerId2 */
                );

            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                brokerInboundPort                       /* port2 */,
                uuids::create()                         /* peerId1 */,
                stickyPeerId1                           /* peerId2 */
                );

            /*
             * Sleep is necessary here before we do the next two tests - see comment above
             */

            os::sleep( heartbeatInterval + time::seconds( 2L ) );

            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                brokerInboundPort                       /* port2 */,
                stickyPeerId2                           /* peerId1 */,
                stickyPeerId1                           /* peerId2 */
                );

            sendSingleMessageTests(
                test::UtfArgsParser::port()             /* port1 */,
                brokerInboundPort                       /* port2 */,
                stickyPeerId2                           /* peerId1 */,
                uuids::create()                         /* peerId2 */
                );
        }

        {
            const auto peerId = uuids::create();

            executeTests( 1U /* noOfConnections */, peerId, false /* runPruneProbes */ );
        }

        {
            const auto peerId = uuids::create();

            executeTests( 21U /* noOfConnections */, peerId, false /* runPruneProbes */ );
        }

        /*
         * Execute the tests with peerId=nil() which will generate unique physical peerId
         * for each connection
         */

        executeTests( 24 /* noOfConnections */, uuids::nil() /* peerId */, true /* runPruneProbes */ );

        /*
         * Now test if the client pruning logic works correctly
         */

        UTF_REQUIRE( proxyBackendRef );

        const auto proxyBackend =
            om::qi< ProxyBrokerBackendProcessingFactorySsl::proxy_backend_t >( proxyBackendRef );

        /*
         * The proxy backend delegates isConnected() to its outgoing block channel the same way
         * the forwarding backend does, rather than inheriting the always connected default of
         * BackendProcessingBase - both REST consumers gate request admission on it, so a
         * request to a proxy which has lost the actual backend fails fast
         *
         * The proxy is connected to the actual backend at this point in the case
         *
         * KNOWN GAP: this asserts only the 'true' half, which also held before the override was
         * added, so it does not on its own discriminate the delegation from the old default. The
         * 'false' half is not cheaply reachable - ProxyBrokerBackendProcessingFactorySsl::create()
         * THROWS when no endpoint connects (see ForwardingBackendConnectFailureTests) rather than
         * returning a live but disconnected backend, so reaching it needs either the actual
         * backend torn down underneath a running proxy or direct construction of the detail::
         * type with a stub outgoing channel. The delegated expression itself is the same one
         * ForwardingBackendProcessing has used since before this change
         */

        UTF_REQUIRE( om::qi< BackendProcessing >( proxyBackendRef ) -> isConnected() );

        UTF_REQUIRE( ! om::qi< BackendProcessing >( proxyBackendRef ) -> autoBlockDispatching() );

        {
            std::unordered_set< bl::uuid_t > activeClients;
            std::unordered_set< bl::uuid_t > pendingPrune;

            proxyBackend -> getCurrentState( &activeClients, &pendingPrune );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Active clients count is "
                    << activeClients.size()
                    << "; client pending prune is "
                    << pendingPrune.size()
                );

            UTF_REQUIRE( activeClients.size() );
        }

        proxyBackend -> setClientsPruneIntervals(
            time::seconds( 3L )         /* clientsPruneCheckInterval */,
            time::seconds( 12L )        /* clientsPruneInterval */
            );

        const std::size_t maxRetries = 60U;
        std::size_t retries = 0U;

        for( ;; )
        {
            if( retries > maxRetries )
            {
                UTF_FAIL( "All clients did not go in pending prune mode in 2 minutes" );

                break;
            }

            os::sleep( time::seconds( 2L ) );

            std::unordered_set< bl::uuid_t > activeClients;
            std::unordered_set< bl::uuid_t > pendingPrune;

            proxyBackend -> getCurrentState( &activeClients, &pendingPrune );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Active clients count is "
                    << activeClients.size()
                    << "; client pending prune is "
                    << pendingPrune.size()
                );

            for( const auto& peerId : pendingPrune )
            {
                activeClients.erase( peerId );
            }

            if( activeClients.size() )
            {
                ++retries;
                continue;
            }

            UTF_REQUIRE_EQUAL( 0U, activeClients.size() );
            UTF_REQUIRE( pendingPrune.size() );

            break;
        }

        retries = 0U;

        for( ;; )
        {
            if( retries > maxRetries )
            {
                UTF_FAIL( "All clients did get pruned after in pending prune mode for 2 minutes" );

                break;
            }

            os::sleep( time::seconds( 2L ) );

            std::unordered_set< bl::uuid_t > activeClients;
            std::unordered_set< bl::uuid_t > pendingPrune;

            proxyBackend -> getCurrentState( &activeClients, &pendingPrune );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Active clients count is "
                    << activeClients.size()
                    << "; client pending prune is "
                    << pendingPrune.size()
                );

            if( activeClients.size() || pendingPrune.size() )
            {
                ++retries;
                continue;
            }

            UTF_REQUIRE_EQUAL( 0U, activeClients.size() );
            UTF_REQUIRE_EQUAL( 0U, pendingPrune.size() );

            break;
        }
    };

    test::MachineGlobalTestLock lock;

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto processingBackend = om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()                      /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()              /* certificatePem */,
        brokerInboundPort                                           /* inboundPort */,
        brokerInboundPort + 1U                                      /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                          /* maxConcurrentTasks */,
        cpp::bind(
            &utils_t::startBrokerProxy,
            om::ObjPtrCopyable< TaskControlTokenRW >( controlToken ),
            callbackTests,
            test::UtfArgsParser::port()                             /* proxyInboundPort */,
            test::UtfArgsParser::connections()                      /* noOfConnections */,
            test::UtfArgsParser::host()                             /* brokerHostName */,
            brokerInboundPort,
            om::ObjPtrCopyable< data::datablocks_pool_type >( dataBlocksPool ),
            heartbeatInterval,
            &proxyBackendRef
            ),
        om::copy( controlToken ),
        dataBlocksPool,
        cpp::copy( heartbeatInterval )
        );
}
