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
    /**
     * @brief A minimal message block completion queue stub which is handed out for peer ids
     * that the test wants to present as directly connected to the backend
     */

    template
    <
        typename E = void
    >
    class TestBlockCompletionQueueT : public bl::messaging::MessageBlockCompletionQueue
    {
        BL_CTR_DEFAULT( TestBlockCompletionQueueT, protected )

        BL_DECLARE_OBJECT_IMPL_ONEIFACE( TestBlockCompletionQueueT, bl::messaging::MessageBlockCompletionQueue )

    public:

        virtual void requestHeartbeat() OVERRIDE
        {
        }

        virtual bool tryScheduleBlock(
            SAA_in                  const bl::uuid_t&                                   targetPeerId,
            SAA_in                  bl::om::ObjPtr< bl::data::DataBlock >&&             dataBlock,
            SAA_in                  CompletionCallback&&                                callback
            ) OVERRIDE
        {
            BL_UNUSED( targetPeerId );
            BL_UNUSED( dataBlock );
            BL_UNUSED( callback );

            return true;
        }
    };

    typedef bl::om::ObjectImpl< TestBlockCompletionQueueT<> > TestBlockCompletionQueue;

    template
    <
        typename E = void
    >
    class TestHostServicesContextT : public bl::messaging::AsyncBlockDispatcher
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( TestHostServicesContextT, bl::messaging::AsyncBlockDispatcher )

    protected:

        typedef TestHostServicesContextT< E >                                   this_type;

        bl::cpp::ScalarTypeIniter< bool >                                       m_wasBlockDispatched;
        bl::uuid_t                                                              m_targetPeerId;
        bl::uuid_t                                                              m_resolvedTargetPeerId;
        bl::uuid_t                                                              m_directlyConnectedPeerId;
        bl::om::ObjPtr< bl::messaging::MessageBlockCompletionQueue >            m_queue;

        TestHostServicesContextT() NOEXCEPT
            :
            m_targetPeerId( bl::uuids::nil() ),
            m_resolvedTargetPeerId( bl::uuids::nil() ),
            m_directlyConnectedPeerId( bl::uuids::nil() )
        {
        }

        void dispatchCallback( SAA_in const bl::uuid_t& targetPeerId )
        {
            if( targetPeerId != m_targetPeerId )
            {
                m_resolvedTargetPeerId = targetPeerId;
            }

            m_wasBlockDispatched = true;
        }

    public:

        void targetPeerId( SAA_in const bl::uuid_t& targetPeerId ) NOEXCEPT
        {
            m_targetPeerId = targetPeerId;
        }

        /*
         * Note that the completion queue is created here rather than lazily in the virtual
         * below because the latter is invoked on a task thread while this setter is only
         * ever called from the test thread before the backend task is scheduled
         */

        void directlyConnectedPeerId( SAA_in const bl::uuid_t& directlyConnectedPeerId )
        {
            m_directlyConnectedPeerId = directlyConnectedPeerId;

            if( ! m_queue )
            {
                m_queue = TestBlockCompletionQueue::createInstance<
                    bl::messaging::MessageBlockCompletionQueue
                    >();
            }
        }

        auto wasMessageForBackend() const NOEXCEPT -> bool
        {
            return ! m_wasBlockDispatched;
        }

        auto resolvedTargetPeerId() const NOEXCEPT -> const bl::uuid_t&
        {
            return m_resolvedTargetPeerId;
        }

        virtual auto getAllActiveQueuesIds() -> std::unordered_set< bl::uuid_t > OVERRIDE
        {
            return std::unordered_set< bl::uuid_t >();
        }

        virtual auto tryGetMessageBlockCompletionQueue( SAA_in const bl::uuid_t& targetPeerId )
            -> bl::om::ObjPtr< bl::messaging::MessageBlockCompletionQueue > OVERRIDE
        {
            /*
             * m_directlyConnectedPeerId defaults to nil() and m_queue is only created when
             * directlyConnectedPeerId() is called, so unless a test opts in explicitly this
             * keeps returning nullptr - i.e. no peer id is directly connected
             */

            if( m_queue && targetPeerId == m_directlyConnectedPeerId )
            {
                return bl::om::copy( m_queue );
            }

            return nullptr;
        }

        virtual auto createDispatchTask(
            SAA_in                  const bl::uuid_t&                                   targetPeerId,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&        data
            )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            BL_UNUSED( data );

            return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                bl::cpp::bind(
                    &this_type::dispatchCallback,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    targetPeerId
                    )
                );
        }
    };

    typedef bl::om::ObjectImpl< TestHostServicesContextT<> > TestHostServicesContext;
    typedef TestHostServicesContext context_t;

    auto createTestSecurityPrincipal() -> bl::om::ObjPtr< bl::messaging::SecurityPrincipal >
    {
        auto principal = bl::messaging::SecurityPrincipal::createInstance();

        principal -> sidLvalue() = "e123456";
        principal -> emailLvalue() = "user@host.com";
        principal -> givenNameLvalue() = "First";
        principal -> familyNameLvalue() = "Last";

        return principal;
    }

    auto createProtocolMessage( SAA_in_opt const std::string& cookiesText = bl::str::empty() )
        -> bl::om::ObjPtr< bl::messaging::BrokerProtocol >
    {
        using namespace bl::messaging;

        return utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            bl::uuids::create() /* conversationId */,
            cookiesText
            );
    }

    void testBackendProcessingTaskJson(
        SAA_in      const std::string&                                              testName,
        SAA_in      const bl::om::ObjPtr< bl::messaging::BackendProcessing >&       backendProcessing,
        SAA_in      const bl::om::ObjPtr< bl::messaging::BrokerProtocol >&          brokerProtocol,
        SAA_in      const std::string&                                              protocolDataString,
        SAA_in      const bl::om::ObjPtr< context_t >&                              context,
        SAA_in_opt  const bl::uuid_t                                                sourcePeerId = bl::uuids::create(),
        SAA_in_opt  const bl::uuid_t                                                targetPeerId = bl::uuids::create()
        )
    {
        using namespace bl::messaging;

        MessageType::Enum messageType;

        bool isAssociateDissociateMessage = false;

        if(
            MessageType::tryToEnum( brokerProtocol -> messageType(), messageType ) &&
            (
                MessageType::BackendAssociateTargetPeerId == messageType ||
                MessageType::BackendDissociateTargetPeerId == messageType
            )
            )
        {
            isAssociateDissociateMessage = true;
        }
        else
        {
            UTF_REQUIRE( brokerProtocol -> sourcePeerId().empty() );
            UTF_REQUIRE( brokerProtocol -> targetPeerId().empty() );
        }

        BL_LOG_MULTILINE(
            bl::Logging::debug(),
            BL_MSG()
                << "\n********** "
                << testName
                << " **********\n"
            );

        using BackendProcessing = bl::messaging::BackendProcessing;

        const auto operationId = BackendProcessing::OperationId::Put;
        const auto commandId = BackendProcessing::CommandId::None;

        const auto sessionId = bl::uuids::create();
        const auto chunkId = bl::uuids::create();

        const std::size_t payloadSize = 1024U;
        const std::size_t dataBlockSize = 4 * 1024U;

        const auto data = bl::data::DataBlock::createInstance( dataBlockSize );
        data -> setSize( payloadSize );

        UTF_REQUIRE( data -> capacity() >= payloadSize + protocolDataString.size() );

        data -> setOffset1( data -> size() );

        std::copy_n(
            protocolDataString.data(),
            protocolDataString.size(),
            data -> begin() + data -> offset1()
            );

        data -> setSize( data -> size() + protocolDataString.size() );

        const auto hostServices = bl::om::ProxyImpl::createInstance< bl::om::Proxy >( true /* strongRef */ );

        hostServices -> connect( context.get() );

        {
            BL_SCOPE_EXIT(
                {
                    hostServices -> disconnect();
                }
                );

            context -> targetPeerId( targetPeerId );
            backendProcessing -> setHostServices( bl::om::copy( hostServices ) );

            const auto task = backendProcessing -> createBackendProcessingTask(
                operationId,
                commandId,
                sessionId,
                chunkId,
                sourcePeerId,
                targetPeerId,
                data
                );

            bl::tasks::scheduleAndExecuteInParallel(
                [ & ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
                {
                    eq -> push_back( task );
                }
                );

            backendProcessing -> setHostServices( nullptr );
        }

        const auto protocolDataOffset = data -> offset1();

        UTF_REQUIRE( data -> size() > protocolDataOffset );

        const std::string newProtocolData(
            data -> begin() + protocolDataOffset,
            data -> size() - protocolDataOffset
            );

        const auto newBrokerProtocol =
            bl::dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( newProtocolData );

        UTF_REQUIRE_EQUAL( brokerProtocol -> messageType(), newBrokerProtocol -> messageType() );
        UTF_REQUIRE_EQUAL( brokerProtocol -> messageId(), newBrokerProtocol -> messageId() );
        UTF_REQUIRE_EQUAL( brokerProtocol -> conversationId(), newBrokerProtocol -> conversationId() );

        if( isAssociateDissociateMessage )
        {
            UTF_REQUIRE_EQUAL( brokerProtocol -> sourcePeerId(), newBrokerProtocol -> sourcePeerId() );
            UTF_REQUIRE_EQUAL( brokerProtocol -> targetPeerId(), newBrokerProtocol -> targetPeerId() );
        }
        else
        {
            UTF_REQUIRE_EQUAL( bl::uuids::uuid2string( sourcePeerId ), newBrokerProtocol -> sourcePeerId() );
            UTF_REQUIRE_EQUAL( bl::uuids::uuid2string( targetPeerId ), newBrokerProtocol -> targetPeerId() );
        }

        if( newBrokerProtocol -> principalIdentityInfo() )
        {
            UTF_REQUIRE( ! newBrokerProtocol -> principalIdentityInfo() -> authenticationToken() );
            UTF_REQUIRE( newBrokerProtocol -> principalIdentityInfo() -> securityPrincipal() );

            const auto& securityPrincipal =
                newBrokerProtocol -> principalIdentityInfo() -> securityPrincipal();

            const auto principalExpected = utest::DummyAuthorizationCache::getTestSecurityPrincipal();

            const auto sidLower = bl::str::to_lower_copy( securityPrincipal -> sid() );

            UTF_REQUIRE_EQUAL( sidLower, principalExpected -> secureIdentity() );
            UTF_REQUIRE_EQUAL( securityPrincipal -> givenName(), principalExpected -> givenName() );
            UTF_REQUIRE_EQUAL( securityPrincipal -> familyName(), principalExpected -> familyName() );
            UTF_REQUIRE_EQUAL( securityPrincipal -> email(), principalExpected -> email() );
        }
    }

    void testBackendProcessingTask(
        SAA_in      const std::string&                                              testName,
        SAA_in      const bl::om::ObjPtr< bl::messaging::BackendProcessing >&       backendProcessing,
        SAA_in      const bl::om::ObjPtr< bl::messaging::BrokerProtocol >&          brokerProtocol,
        SAA_in_opt  const bl::om::ObjPtr< context_t >&                              context = nullptr,
        SAA_in_opt  const bl::uuid_t                                                sourcePeerId = bl::uuids::create(),
        SAA_in_opt  const bl::uuid_t                                                targetPeerId = bl::uuids::create()
        )
    {
        const auto protocolDataString = bl::dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );

        const auto contextNonNull = context ? bl::om::copy( context ) : context_t::createInstance();

        testBackendProcessingTaskJson(
            testName,
            backendProcessing,
            brokerProtocol,
            protocolDataString,
            contextNonNull,
            sourcePeerId,
            targetPeerId
            );

        if( ! context )
        {
            /*
             * If nullptr was passed as context then we pass a freshly created context and
             * expect that properties are set as wasMessageForBackend()=true and no target
             * peer id re-mapping
             */

            UTF_REQUIRE( ! contextNonNull -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == contextNonNull -> resolvedTargetPeerId() );
        }
    }

    void testBackendProcessingTask(
        SAA_in      const std::string&                                              testName,
        SAA_in      const bl::om::ObjPtr< bl::messaging::BackendProcessing >&       backendProcessing,
        SAA_in      const std::string&                                              cookiesText,
        SAA_in_opt  const bl::om::ObjPtr< context_t >&                              context = nullptr,
        SAA_in_opt  const bl::uuid_t                                                sourcePeerId = bl::uuids::create(),
        SAA_in_opt  const bl::uuid_t                                                targetPeerId = bl::uuids::create()
        )
    {
        testBackendProcessingTask(
            testName,
            backendProcessing,
            createProtocolMessage( cookiesText ),
            context,
            sourcePeerId,
            targetPeerId
            );
    }

    /**
     * @brief An authorization cache mock which can fail the authorization service refresh -
     * i.e. the update() call the broker makes in postAuthorization() after the authorization
     * task created by the cache miss arm has completed
     *
     * Everything else, including the opt-in cache miss mode and the call counters, is
     * inherited from utest::DummyAuthorizationCacheT
     */

    template
    <
        typename E = void
    >
    class FailingUpdateAuthorizationCacheT : public utest::DummyAuthorizationCacheT<>
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( FailingUpdateAuthorizationCacheT, bl::security::AuthorizationCache )

    protected:

        typedef utest::DummyAuthorizationCacheT<>                               base_type;

        std::atomic< bool >                                                     m_failUpdate;

        /*
         * The state of the authorization task is captured here at the moment update() is
         * called - i.e. this is what actually pins the ordering, as the state can only
         * change afterwards
         */

        std::atomic< bl::tasks::Task::State >                                   m_updateTaskState;

        FailingUpdateAuthorizationCacheT()
            :
            m_failUpdate( false ),
            m_updateTaskState( bl::tasks::Task::Created )
        {
        }

    public:

        void failUpdate( SAA_in const bool failUpdate ) NOEXCEPT
        {
            m_failUpdate = failUpdate;
        }

        auto updateTaskState() const NOEXCEPT -> bl::tasks::Task::State
        {
            return m_updateTaskState;
        }

        virtual auto update(
            SAA_in              const bl::om::ObjPtr< bl::data::DataBlock >&        authenticationToken,
            SAA_in_opt          const bl::om::ObjPtr< bl::tasks::Task >&            authorizationTask = nullptr
            )
            -> bl::om::ObjPtr< bl::security::SecurityPrincipal > OVERRIDE
        {
            m_updateTaskState =
                authorizationTask ? authorizationTask -> getState() : bl::tasks::Task::Created;

            /*
             * The base is called first on purpose, so the call is counted and the task which
             * was handed to it is recorded even when the refresh is configured to fail
             */

            auto principal = base_type::update( authenticationToken, authorizationTask );

            if( m_failUpdate )
            {
                BL_THROW(
                    bl::SecurityException()
                        << bl::eh::errinfo_error_code(
                            bl::eh::errc::make_error_code( bl::eh::errc::permission_denied )
                            ),
                    BL_MSG()
                        << "Authorization service refresh has failed"
                    );
            }

            return principal;
        }
    };

    typedef bl::om::ObjectImpl< FailingUpdateAuthorizationCacheT<> > FailingUpdateAuthorizationCache;

    /*
     * Use this macro to enable the hook in the tests where necessary:
     * (and also uncomment the function currently commented with the if 0)
     *
     * BL_EXCEPTION_HOOKS_THROW_GUARD( &exceptionThrowHook1 )
     */

    #if 0
    void exceptionThrowHook1( SAA_in const bl::BaseException& exception ) NOEXCEPT
    {
        if( std::string( "bl::SystemException" ) == exception.fullTypeName() )
        {
            const auto* errorCode = exception.errorCode();

            if( errorCode && bl::asio::error::operation_aborted == *errorCode )
            {
                const auto throwFunctionName =
                    std::string( *bl::eh::get_error_info< bl::eh::throw_function >( exception ) );

                if(
                    std::string::npos !=
                        throwFunctionName.find( "bl::tasks::TaskBaseT<E>::scheduleNothrow" ) ||
                    std::string::npos !=
                        throwFunctionName.find( "bl::tasks::TaskBaseT<void>::scheduleNothrow" )
                    )
                {
                    BL_RIP_MSG( exception.details() );
                }
            }
        }
    }
    #endif

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

UTF_AUTO_TEST_CASE( BackendTests )
{
    using namespace bl::messaging;

    const auto brokerBackendProcessing = utest::TestMessagingUtils::createTestMessagingBackend();

    /*
     * Test the expected context defaults
     */

    {
        const auto context = context_t::createInstance();

        UTF_REQUIRE( context -> wasMessageForBackend() );
        UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
    }

    /*
     * No cookies test
     */

    testBackendProcessingTask( "no cookies test", brokerBackendProcessing, "" /* cookiesText */ );

    /*
     * Fresh cookies test
     */

    const auto freshCookiesText = utest::TestMessagingUtils::getTokenData();

    testBackendProcessingTask( "fresh cookies test", brokerBackendProcessing, freshCookiesText );

    /*
     * Cached cookies test
     *
     * Verify that the cache was actually used by doing a small loop expecting these to be
     * processed very fast now
     */

    for( std::size_t i = 0U; i < 20; ++i )
    {
        testBackendProcessingTask( "cached cookies test", brokerBackendProcessing, freshCookiesText );
    }

    /*
     * Acknowledgment message test
     */

    {
        const auto brokerProtocol = createProtocolMessage();
        brokerProtocol -> messageType( MessageType::toString( MessageType::AsyncRpcAcknowledgment ) );
        UTF_REQUIRE( ! brokerProtocol -> principalIdentityInfo() );
        testBackendProcessingTask( "acknowledgment cookies test", brokerBackendProcessing, brokerProtocol );
    }

    {
        /*
         * Test associate and dissociate messages
         */

        const auto sourcePeerId = bl::uuids::create();
        const auto targetPeerId = bl::uuids::create();

        const auto brokerProtocol = createProtocolMessage();
        UTF_REQUIRE( ! brokerProtocol -> principalIdentityInfo() );

        /*
         * An attempt to associate it would fail because sourcePeerId and targetPeerId were not specified
         */

        try
        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendAssociateTargetPeerId ) );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (invalid source & target peer id)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_FAIL( "This code must throw" );
        }
        catch( bl::ServerErrorException& e )
        {
            const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

            UTF_REQUIRE( ec );
            UTF_REQUIRE_EQUAL( *ec, bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) );
        }

        /*
         * An associate message should be processed by the broker and it should succeed
         *
         * The wasMessageForBackend() in the context should return true, but the resolvedTargetPeerId
         * in the context should still be nil()
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendAssociateTargetPeerId ) );

            brokerProtocol -> sourcePeerId( bl::uuids::uuid2string( sourcePeerId ) );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (success for associate)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_REQUIRE( context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        /*
         * Another attempt to associate it should succeed because an association message / command is
         * idempotent and overrides the association if one already exists
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendAssociateTargetPeerId ) );

            brokerProtocol -> sourcePeerId( bl::uuids::uuid2string( sourcePeerId ) );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (already associated)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_REQUIRE( context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        /*
         * Now we will try to process a message for that targetPeerId and verify that it gets
         * resolved to the original sourcePeerId which it is associated with
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::AsyncRpcAcknowledgment ) );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( "" );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (resolve test for dispatch)",
                brokerBackendProcessing,
                brokerProtocol,
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE( sourcePeerId == context -> resolvedTargetPeerId() );
        }

        /*
         * An attempt to dissociate it would fail because targetPeerId was not specified
         */

        try
        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendDissociateTargetPeerId ) );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( "" );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (invalid source & target peer id for dissociate)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_FAIL( "This code must throw" );
        }
        catch( bl::ServerErrorException& e )
        {
            const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

            UTF_REQUIRE( ec );
            UTF_REQUIRE_EQUAL( *ec, bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) );
        }

        /*
         * An dissociate message should be processed by the broker and it should succeed
         *
         * The wasMessageForBackend() in the context should return true, but the resolvedTargetPeerId
         * in the context should still be nil()
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendDissociateTargetPeerId ) );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (success for dissociate)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_REQUIRE( context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        /*
         * Another attempt to dissociate it should succeed because the dissociate command / message
         * is idempotent and does nothing if there is no association
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::BackendDissociateTargetPeerId ) );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (does not exist for dissociate)",
                brokerBackendProcessing,
                brokerProtocol,
                context
                );

            UTF_REQUIRE( context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        /*
         * Now we will try to process a message for that targetPeerId and verify that it gets
         * resolved to nil() as it is no longer associated
         */

        {
            brokerProtocol -> messageType( MessageType::toString( MessageType::AsyncRpcAcknowledgment ) );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( "" );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "broker only messages test (resolve for non-existing association)",
                brokerBackendProcessing,
                brokerProtocol,
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }
    }

    {
        /*
         * Test that an associate message is *skipped* when the target peer id is already
         * directly connected to the backend - i.e. when the block dispatcher hands out a
         * message block completion queue for it
         *
         * This is the routing security invariant of the broker - if the association was
         * recorded in that case then any client which is able to send an associate message
         * would silently re-route the traffic of a directly connected peer to itself
         *
         * Note that the message must still succeed, as a proxy is expected to keep trying
         * to associate peer ids which have since moved and connected directly
         */

        const auto sourcePeerId = bl::uuids::create();
        const auto targetPeerId = bl::uuids::create();

        const auto createAssociateMessage = [ & ]() -> bl::om::ObjPtr< BrokerProtocol >
        {
            auto brokerProtocol = createProtocolMessage();

            brokerProtocol -> messageType(
                MessageType::toString( MessageType::BackendAssociateTargetPeerId )
                );

            brokerProtocol -> sourcePeerId( bl::uuids::uuid2string( sourcePeerId ) );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            return brokerProtocol;
        };

        const auto createResolveMessage = [ & ]() -> bl::om::ObjPtr< BrokerProtocol >
        {
            auto brokerProtocol = createProtocolMessage();

            brokerProtocol -> messageType(
                MessageType::toString( MessageType::AsyncRpcAcknowledgment )
                );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( "" );

            return brokerProtocol;
        };

        /*
         * The skip arm - the target peer id is reported as directly connected
         */

        {
            const auto contextWithDirectPeer = context_t::createInstance();

            contextWithDirectPeer -> directlyConnectedPeerId( targetPeerId );

            testBackendProcessingTask(
                "associate ignored for directly connected peer",
                brokerBackendProcessing,
                createAssociateMessage(),
                contextWithDirectPeer
                );

            UTF_REQUIRE( contextWithDirectPeer -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == contextWithDirectPeer -> resolvedTargetPeerId() );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "resolve test for dispatch after the associate was ignored",
                brokerBackendProcessing,
                createResolveMessage(),
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        /*
         * The control arm - the only difference from the arm above is that the block
         * dispatcher does not hand out a queue for the target peer id, in which case the
         * association is recorded and the message is re-addressed to the source peer id
         */

        {
            const auto contextWithoutDirectPeer = context_t::createInstance();

            testBackendProcessingTask(
                "associate recorded for a peer which is not directly connected",
                brokerBackendProcessing,
                createAssociateMessage(),
                contextWithoutDirectPeer
                );

            UTF_REQUIRE( contextWithoutDirectPeer -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == contextWithoutDirectPeer -> resolvedTargetPeerId() );

            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "resolve test for dispatch after the associate was recorded",
                brokerBackendProcessing,
                createResolveMessage(),
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE( sourcePeerId == context -> resolvedTargetPeerId() );
        }
    }

    {
        /*
         * Test the AcceptorNotify implementation of the broker backend
         *
         * This is the second half of the routing invariant tested above - a peer which was
         * reachable only through a proxy and then connects directly to the backend must have
         * its stale route dropped by peerConnectedNotify(), otherwise every message for that
         * peer would keep being sent to the proxy's physical connection and be lost
         *
         * Note that peerDisconnectedNotify() is deliberately a no-op - a disconnect must not
         * invalidate the route, as the peer is expected to remain reachable via the proxy
         *
         * Both notifications must return 'false' to indicate that the call was completed
         * synchronously and that the completion callback will not be invoked - the acceptor
         * relies on that return value to decide whether it has to wait for a callback
         */

        const auto acceptorNotify = bl::om::qi< AcceptorNotify >( brokerBackendProcessing );

        bool callbackInvoked = false;

        bl::tasks::CompletionCallback cb =
            [ &callbackInvoked ]( SAA_in_opt const std::exception_ptr& ) -> void
            {
                callbackInvoked = true;
            };

        const auto sourcePeerId = bl::uuids::create();
        const auto targetPeerId = bl::uuids::create();

        const auto createAssociateMessage = [ & ]() -> bl::om::ObjPtr< BrokerProtocol >
        {
            auto brokerProtocol = createProtocolMessage();

            brokerProtocol -> messageType(
                MessageType::toString( MessageType::BackendAssociateTargetPeerId )
                );

            brokerProtocol -> sourcePeerId( bl::uuids::uuid2string( sourcePeerId ) );
            brokerProtocol -> targetPeerId( bl::uuids::uuid2string( targetPeerId ) );

            return brokerProtocol;
        };

        const auto createResolveMessage = [ & ]() -> bl::om::ObjPtr< BrokerProtocol >
        {
            auto brokerProtocol = createProtocolMessage();

            brokerProtocol -> messageType(
                MessageType::toString( MessageType::AsyncRpcAcknowledgment )
                );

            brokerProtocol -> sourcePeerId( "" );
            brokerProtocol -> targetPeerId( "" );

            return brokerProtocol;
        };

        /*
         * Establish the route which the notifications below are expected to act upon
         *
         * Note that each resolve probe below uses a freshly created context, so that the
         * resolved target peer id it reports cannot be carrying state from a previous probe
         */

        {
            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "associate before the acceptor notifications",
                brokerBackendProcessing,
                createAssociateMessage(),
                context
                );

            UTF_REQUIRE( context -> wasMessageForBackend() );
            UTF_REQUIRE( bl::uuids::nil() == context -> resolvedTargetPeerId() );
        }

        {
            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "resolve test for dispatch before the acceptor notifications",
                brokerBackendProcessing,
                createResolveMessage(),
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE_EQUAL( sourcePeerId, context -> resolvedTargetPeerId() );
        }

        /*
         * A disconnect notification must leave the route intact
         */

        UTF_REQUIRE( ! acceptorNotify -> peerDisconnectedNotify( targetPeerId, bl::cpp::copy( cb ) ) );

        {
            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "resolve test for dispatch after peerDisconnectedNotify",
                brokerBackendProcessing,
                createResolveMessage(),
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE_EQUAL( sourcePeerId, context -> resolvedTargetPeerId() );
        }

        /*
         * A connect notification must drop it
         */

        UTF_REQUIRE( ! acceptorNotify -> peerConnectedNotify( targetPeerId, bl::cpp::copy( cb ) ) );

        {
            const auto context = context_t::createInstance();

            testBackendProcessingTask(
                "resolve test for dispatch after peerConnectedNotify",
                brokerBackendProcessing,
                createResolveMessage(),
                context,
                bl::uuids::create()     /* sourcePeerId */,
                targetPeerId
                );

            UTF_REQUIRE( ! context -> wasMessageForBackend() );
            UTF_REQUIRE_EQUAL( bl::uuids::nil(), context -> resolvedTargetPeerId() );
        }

        /*
         * An unknown peer id - dissociateTargetPeerId() returns false and must not throw -
         * and an empty completion callback must both be tolerated
         */

        UTF_REQUIRE( ! acceptorNotify -> peerConnectedNotify( bl::uuids::create(), bl::cpp::copy( cb ) ) );
        UTF_REQUIRE( ! acceptorNotify -> peerConnectedNotify( targetPeerId, bl::tasks::CompletionCallback() ) );

        /*
         * None of the five notifications above is allowed to invoke the completion callback
         */

        UTF_REQUIRE( ! callbackInvoked );
    }

    const auto testPermissionDeniedFailure = [ & ](
        SAA_in          const std::string&                                                  testName,
        SAA_in          const std::string&                                                  cookiesText,
        SAA_in          const std::string&                                                  messageText
        ) -> void
    {
        try
        {
            testBackendProcessingTask( testName, brokerBackendProcessing, cookiesText );
            UTF_FAIL( "The code above is expected to throw" );
        }
        catch( bl::ServerErrorException& e )
        {
            const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

            UTF_REQUIRE( ec );
            UTF_REQUIRE_EQUAL( *ec, bl::eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed ) );

            const auto* nestedExceptionPtr = bl::eh::get_error_info< bl::eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE_THROW_MESSAGE(
                bl::cpp::safeRethrowException( *nestedExceptionPtr ),
                bl::SecurityException,
                messageText
                );
        }
    };

    /*
     * Bad cookies test
     *
     * Note the cookiesText must be 'dummyCookieName=unauthorized' for the dummy authorization cache to throw
     */

    testPermissionDeniedFailure(
        "bad cookies test"                      /* testName */,
        "dummyCookieName=unauthorized"          /* cookiesText */,
        "Authorization request has failed"      /* messageText */
        );

    /*
     * Invalid parameter tests
     */

    try
    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );

        const auto context = context_t::createInstance();

        testBackendProcessingTaskJson(
            "invalid json test",
            brokerBackendProcessing,
            brokerProtocol,
            "<invalid json>",
            context
            );
    }
    catch( bl::ServerErrorException& e )
    {
        const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

        UTF_REQUIRE( ec );
        UTF_REQUIRE_EQUAL( *ec, bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) );
    }

    const auto testInvalidArgumentFailure = [ & ]( SAA_in const bl::om::ObjPtr< BrokerProtocol >& brokerProtocol )
        -> void
    {
        try
        {
            testBackendProcessingTask( "invalid argument test", brokerBackendProcessing, brokerProtocol );
        }
        catch( bl::ServerErrorException& e )
        {
            const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

            UTF_REQUIRE( ec );
            UTF_REQUIRE_EQUAL( *ec, bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) );
        }
    };

    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        brokerProtocol -> messageType( "foo" );

        testInvalidArgumentFailure( brokerProtocol );
    }

    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        brokerProtocol -> messageId( "foo" );

        testInvalidArgumentFailure( brokerProtocol );
    }

    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        brokerProtocol -> conversationId( "foo" );

        testInvalidArgumentFailure( brokerProtocol );
    }

    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        brokerProtocol -> principalIdentityInfo() -> authenticationToken( nullptr );

        testInvalidArgumentFailure( brokerProtocol );
    }

    {
        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        brokerProtocol -> principalIdentityInfo() -> authenticationToken() -> type( "foo" );

        testInvalidArgumentFailure( brokerProtocol );
    }

    {
        /*
         * Cover the 4 main cases:
         *
         * -- both authentication token & security principal is invalid combination
         * -- no authentication token & security principal is invalid combination
         * -- no authentication token & no security principal is invalid combination
         * -- no principal identity info is valid combination for any message
         */

        const auto brokerProtocol = createProtocolMessage( freshCookiesText );
        UTF_REQUIRE( brokerProtocol -> principalIdentityInfo() -> authenticationToken() );

        brokerProtocol -> principalIdentityInfo() -> securityPrincipal( createTestSecurityPrincipal() );
        testInvalidArgumentFailure( brokerProtocol );

        brokerProtocol -> principalIdentityInfo() -> authenticationToken( nullptr );
        testInvalidArgumentFailure( brokerProtocol );

        brokerProtocol -> principalIdentityInfo() -> securityPrincipal( nullptr );
        testInvalidArgumentFailure( brokerProtocol );

        brokerProtocol -> principalIdentityInfo( nullptr );
        testBackendProcessingTask( "no principal identity info", brokerBackendProcessing, brokerProtocol );
    }

    {
        /*
         * The broker must reject a client supplied security principal
         *
         * This is the one trust boundary rule the broker enforces against a hostile client -
         * the security principal is what the backend treats as the authenticated identity,
         * and the broker is the only party allowed to write it (authorizeProtocolMessage).
         * If the guard were dropped a client could assert any identity it liked and the
         * broker would forward it to the backend as authenticated
         *
         * The scaffolding below is a local variant of testBackendProcessingTaskJson which
         * keeps the data block, so the negative arm can assert that the forwarded block was
         * not rewritten at all
         */

        typedef bl::messaging::BackendProcessing BackendProcessing;

        const auto cbCreateBlock = []( SAA_in const bl::om::ObjPtr< BrokerProtocol >& brokerProtocol )
            -> bl::om::ObjPtr< bl::data::DataBlock >
        {
            const auto protocolDataString =
                bl::dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );

            const std::size_t payloadSize = 1024U;
            const std::size_t dataBlockSize = 4 * 1024U;

            auto data = bl::data::DataBlock::createInstance( dataBlockSize );
            data -> setSize( payloadSize );

            UTF_REQUIRE( data -> capacity() >= payloadSize + protocolDataString.size() );

            data -> setOffset1( data -> size() );

            std::copy_n(
                protocolDataString.data(),
                protocolDataString.size(),
                data -> begin() + data -> offset1()
                );

            data -> setSize( data -> size() + protocolDataString.size() );

            return data;
        };

        const auto cbReadProtocol = []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data )
            -> bl::om::ObjPtr< BrokerProtocol >
        {
            const auto protocolDataOffset = data -> offset1();

            UTF_REQUIRE( data -> size() > protocolDataOffset );

            const std::string protocolData(
                data -> begin() + protocolDataOffset,
                data -> size() - protocolDataOffset
                );

            return bl::dm::DataModelUtils::loadFromJsonText< BrokerProtocol >( protocolData );
        };

        const auto cbDriveBackendTask = [ & ](
            SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&            data,
            SAA_in          const bl::om::ObjPtr< context_t >&                      context,
            SAA_in          const bl::uuid_t&                                       sourcePeerId,
            SAA_in          const bl::uuid_t&                                       targetPeerId
            )
            -> void
        {
            const auto hostServices =
                bl::om::ProxyImpl::createInstance< bl::om::Proxy >( true /* strongRef */ );

            hostServices -> connect( context.get() );

            context -> targetPeerId( targetPeerId );
            brokerBackendProcessing -> setHostServices( bl::om::copy( hostServices ) );

            BL_SCOPE_EXIT(
                {
                    brokerBackendProcessing -> setHostServices( nullptr );

                    hostServices -> disconnect();
                }
                );

            const auto task = brokerBackendProcessing -> createBackendProcessingTask(
                BackendProcessing::OperationId::Put,
                BackendProcessing::CommandId::None,
                bl::uuids::create()                                 /* sessionId */,
                bl::uuids::create()                                 /* chunkId */,
                sourcePeerId,
                targetPeerId,
                data
                );

            bl::tasks::scheduleAndExecuteInParallel(
                [ & ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
                {
                    eq -> push_back( task );
                }
                );
        };

        /*
         * The negative arm - a forged security principal on an otherwise perfectly valid
         * and authorizable message
         */

        {
            const auto brokerProtocol = createProtocolMessage( freshCookiesText );

            UTF_REQUIRE( brokerProtocol -> principalIdentityInfo() );
            UTF_REQUIRE( brokerProtocol -> principalIdentityInfo() -> authenticationToken() );
            UTF_REQUIRE( ! brokerProtocol -> principalIdentityInfo() -> securityPrincipal() );

            const auto forgedPrincipal = createTestSecurityPrincipal();

            brokerProtocol -> principalIdentityInfo() -> securityPrincipal( bl::om::copy( forgedPrincipal ) );

            const auto data = cbCreateBlock( brokerProtocol );
            const auto context = context_t::createInstance();

            try
            {
                cbDriveBackendTask(
                    data,
                    context,
                    bl::uuids::create()     /* sourcePeerId */,
                    bl::uuids::create()     /* targetPeerId */
                    );

                UTF_FAIL( "The broker must reject a client supplied security principal" );
            }
            catch( bl::ServerErrorException& e )
            {
                const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

                UTF_REQUIRE( ec );

                UTF_REQUIRE_EQUAL(
                    *ec,
                    bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed )
                    );

                UTF_REQUIRE(
                    bl::cpp::contains(
                        std::string( e.what() ),
                        "Security principal info cannot be provided as input"
                        )
                    );
            }

            /*
             * The block must not have been rewritten - the broker rejects before it
             * authorizes, so the forged principal is still there untouched, the
             * authentication token was not stripped, the peer ids were never stamped and
             * the message was never dispatched to any peer
             */

            const auto newBrokerProtocol = cbReadProtocol( data );

            UTF_REQUIRE( newBrokerProtocol -> principalIdentityInfo() );
            UTF_REQUIRE( newBrokerProtocol -> principalIdentityInfo() -> authenticationToken() );

            const auto& newPrincipal = newBrokerProtocol -> principalIdentityInfo() -> securityPrincipal();

            UTF_REQUIRE( newPrincipal );
            UTF_REQUIRE_EQUAL( newPrincipal -> sid(), forgedPrincipal -> sid() );

            UTF_REQUIRE( newBrokerProtocol -> sourcePeerId().empty() );
            UTF_REQUIRE( newBrokerProtocol -> targetPeerId().empty() );

            UTF_REQUIRE( context -> wasMessageForBackend() );
        }

        /*
         * The control arm - the very same message without the forged principal must be
         * authorized successfully and the broker's own principal must be stamped on it, so
         * the negative arm above cannot pass simply because the whole path is broken
         */

        {
            const auto brokerProtocol = createProtocolMessage( freshCookiesText );

            UTF_REQUIRE( ! brokerProtocol -> principalIdentityInfo() -> securityPrincipal() );

            const auto data = cbCreateBlock( brokerProtocol );
            const auto context = context_t::createInstance();

            const auto sourcePeerId = bl::uuids::create();
            const auto targetPeerId = bl::uuids::create();

            UTF_REQUIRE_NO_THROW( cbDriveBackendTask( data, context, sourcePeerId, targetPeerId ) );

            const auto newBrokerProtocol = cbReadProtocol( data );

            UTF_REQUIRE( newBrokerProtocol -> principalIdentityInfo() );
            UTF_REQUIRE( ! newBrokerProtocol -> principalIdentityInfo() -> authenticationToken() );

            const auto& newPrincipal = newBrokerProtocol -> principalIdentityInfo() -> securityPrincipal();

            UTF_REQUIRE( newPrincipal );

            UTF_REQUIRE_EQUAL(
                bl::str::to_lower_copy( newPrincipal -> sid() ),
                utest::DummyAuthorizationCache::dummySid()
                );

            UTF_REQUIRE_EQUAL(
                newBrokerProtocol -> sourcePeerId(),
                bl::uuids::uuid2string( sourcePeerId )
                );

            UTF_REQUIRE_EQUAL(
                newBrokerProtocol -> targetPeerId(),
                bl::uuids::uuid2string( targetPeerId )
                );
        }

        /*
         * The mirror guard one line below - an authentication token is required whenever
         * principal identity info is present at all; only the error code of this arm is
         * covered elsewhere, never the message
         */

        {
            const auto brokerProtocol = createProtocolMessage( freshCookiesText );

            brokerProtocol -> principalIdentityInfo() -> authenticationToken( nullptr );

            const auto data = cbCreateBlock( brokerProtocol );
            const auto context = context_t::createInstance();

            try
            {
                cbDriveBackendTask(
                    data,
                    context,
                    bl::uuids::create()     /* sourcePeerId */,
                    bl::uuids::create()     /* targetPeerId */
                    );

                UTF_FAIL( "The broker must reject a message without an authentication token" );
            }
            catch( bl::ServerErrorException& e )
            {
                const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

                UTF_REQUIRE( ec );

                UTF_REQUIRE_EQUAL(
                    *ec,
                    bl::eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed )
                    );

                UTF_REQUIRE(
                    bl::cpp::contains(
                        std::string( e.what() ),
                        "Authentication token information is required"
                        )
                    );
            }

            UTF_REQUIRE( context -> wasMessageForBackend() );
        }
    }
}

UTF_AUTO_TEST_CASE( BrokerErrorCodesTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * The four constants are copied into locals before they are compared - they are declared
     * in-class with an initializer and have no out-of-class definition, so binding them
     * directly to the const references the check macros take would odr-use them
     */

    const int targetPeerNotFound = BrokerErrorCodes::TargetPeerNotFound;
    const int targetPeerQueueFull = BrokerErrorCodes::TargetPeerQueueFull;
    const int authorizationFailed = BrokerErrorCodes::AuthorizationFailed;
    const int protocolValidationFailed = BrokerErrorCodes::ProtocolValidationFailed;

    /*
     * The two broker specific codes are part of the wire contract between a broker and its
     * clients and are deliberately hard-coded to the Linux numeric values, because the
     * corresponding eh::errc names do not have stable values across platforms - replacing
     * them with the names would silently change the wire values on Windows and macOS
     */

    UTF_REQUIRE_EQUAL( 99, targetPeerNotFound );
    UTF_REQUIRE_EQUAL( 105, targetPeerQueueFull );

    UTF_REQUIRE_EQUAL( static_cast< int >( eh::errc::permission_denied ), authorizationFailed );
    UTF_REQUIRE_EQUAL( static_cast< int >( eh::errc::invalid_argument ), protocolValidationFailed );

    /*
     * isExpectedErrorCode() - the four accepted values, a non-broker generic value, the two
     * wrong category values and a default constructed code
     *
     * The category guard is what stops an asio / system_category error whose value happens
     * to be 99 or 105 from being mistaken for a broker error
     */

    UTF_REQUIRE(
        BrokerErrorCodes::isExpectedErrorCode(
            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
            )
        );

    UTF_REQUIRE(
        BrokerErrorCodes::isExpectedErrorCode(
            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull )
            )
        );

    UTF_REQUIRE(
        BrokerErrorCodes::isExpectedErrorCode(
            eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed )
            )
        );

    UTF_REQUIRE(
        BrokerErrorCodes::isExpectedErrorCode(
            eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed )
            )
        );

    UTF_REQUIRE(
        ! BrokerErrorCodes::isExpectedErrorCode( eh::errc::make_error_code( eh::errc::address_in_use ) )
        );

    UTF_REQUIRE( ! BrokerErrorCodes::isExpectedErrorCode( eh::error_code( 99, eh::system_category() ) ) );
    UTF_REQUIRE( ! BrokerErrorCodes::isExpectedErrorCode( eh::error_code( 105, eh::system_category() ) ) );

    UTF_REQUIRE( ! BrokerErrorCodes::isExpectedErrorCode( eh::error_code() ) );

    /*
     * tryGetExpectedErrorMessage() - the two POSIX-ish codes map to the standard message of
     * the error code itself while the two broker codes map to fixed user-facing strings,
     * which are what reaches the GraphQL clients
     */

    {
        const auto ecAuthorizationFailed =
            eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed );

        const auto ecProtocolValidationFailed =
            eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed );

        UTF_REQUIRE( ! BrokerErrorCodes::tryGetExpectedErrorMessage( ecAuthorizationFailed ).empty() );

        UTF_REQUIRE_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage( ecAuthorizationFailed ),
            ecAuthorizationFailed.message()
            );

        UTF_REQUIRE( ! BrokerErrorCodes::tryGetExpectedErrorMessage( ecProtocolValidationFailed ).empty() );

        UTF_REQUIRE_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage( ecProtocolValidationFailed ),
            ecProtocolValidationFailed.message()
            );

        UTF_REQUIRE_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage(
                eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
                ),
            std::string( "The server is currently unavailable" )
            );

        UTF_REQUIRE_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage(
                eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull )
                ),
            std::string( "The server is too busy" )
            );

        UTF_REQUIRE_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage(
                eh::errc::make_error_code( eh::errc::address_in_use )
                ),
            str::empty()
            );

        UTF_REQUIRE_EQUAL( BrokerErrorCodes::tryGetExpectedErrorMessage( eh::error_code() ), str::empty() );

        /*
         * The check below pins the *current* behavior rather than endorsing it - unlike
         * isExpectedErrorCode() above, tryGetExpectedErrorMessage() switches on the numeric
         * value without checking the category at all, so an unrelated system_category errno
         * 99 is given the broker's user-facing message
         *
         * It is a UTF_CHECK so that the asymmetry between the two functions is visible in
         * the suite rather than only in the source
         */

        UTF_CHECK_EQUAL(
            BrokerErrorCodes::tryGetExpectedErrorMessage( eh::error_code( 99, eh::system_category() ) ),
            std::string( "The server is currently unavailable" )
            );
    }

    /*
     * rethrowIfNotExpectedException() / isExpectedException() - only a ServerErrorException
     * which carries an expected error code is swallowed, anything else must propagate
     *
     * Note that a null exception_ptr must never be passed to either of them - that would be
     * a BL_RIP_MSG which terminates the process - so every exception pointer below is
     * obtained from an exception which was actually thrown
     */

    const auto makeEptr = []( SAA_in const cpp::void_callback_t& callback ) -> std::exception_ptr
    {
        try
        {
            callback();
        }
        catch( std::exception& )
        {
            return std::current_exception();
        }

        UTF_FAIL( "The callback above is expected to throw" );

        return std::exception_ptr();
    };

    {
        const auto eptr = makeEptr(
            []() -> void
            {
                BL_THROW(
                    ServerErrorException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
                            ),
                    BL_MSG()
                        << "Expected broker error"
                    );
            }
            );

        UTF_REQUIRE_NO_THROW( BrokerErrorCodes::rethrowIfNotExpectedException( eptr ) );
        UTF_REQUIRE( BrokerErrorCodes::isExpectedException( eptr ) );
    }

    {
        const auto eptr = makeEptr(
            []() -> void
            {
                BL_THROW(
                    ServerErrorException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( eh::errc::address_in_use )
                            ),
                    BL_MSG()
                        << "Unexpected error code"
                    );
            }
            );

        UTF_REQUIRE_THROW( BrokerErrorCodes::rethrowIfNotExpectedException( eptr ), ServerErrorException );
        UTF_REQUIRE( ! BrokerErrorCodes::isExpectedException( eptr ) );
    }

    {
        const auto eptr = makeEptr(
            []() -> void
            {
                BL_THROW(
                    ServerErrorException(),
                    BL_MSG()
                        << "No error code at all"
                    );
            }
            );

        UTF_REQUIRE_THROW( BrokerErrorCodes::rethrowIfNotExpectedException( eptr ), ServerErrorException );
        UTF_REQUIRE( ! BrokerErrorCodes::isExpectedException( eptr ) );
    }

    {
        /*
         * The type gate - an expected error code attached to an exception which is not a
         * ServerErrorException must not be swallowed either
         */

        const auto eptr = makeEptr(
            []() -> void
            {
                BL_THROW(
                    ArgumentException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
                            ),
                    BL_MSG()
                        << "Expected error code on a non-server exception"
                    );
            }
            );

        UTF_REQUIRE_THROW( BrokerErrorCodes::rethrowIfNotExpectedException( eptr ), ArgumentException );
        UTF_REQUIRE( ! BrokerErrorCodes::isExpectedException( eptr ) );
    }
}

UTF_AUTO_TEST_CASE( IO_BrokerAuthorizationCacheMissTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef utest::TestMessagingUtils utils_t;

    /*
     * utest::DummyAuthorizationCache never returns nullptr from tryGetAuthorizedPrinciplal()
     * and every broker in the suite is handed one of those whenever --path / --password are
     * not provided, i.e. on every automated run, so the broker's cache miss arm - which is
     * the arm that talks to the authorization service - is otherwise never taken
     *
     * The mock below opts into the cache miss mode and can also fail the refresh
     */

    const auto authorizationCache = FailingUpdateAuthorizationCache::createInstance();

    authorizationCache -> forceCacheMiss( true );

    /*
     * A distinct authentication token, so the failing refresh arm below misses the cache too
     */

    const std::string cookiesTextToFailRefresh =
        utest::DummyAuthorizationCache::dummyTokenData() + ";refreshFails=true";

    const auto callbackTests = [ & ]() -> void
    {
        os::mutex messagesLock;
        std::vector< om::ObjPtr< BrokerProtocol > > messagesReceived;

        const auto incomingObjectChannel = om::lockDisposable(
            MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                [ & ](
                    SAA_in              const bl::uuid_t&                               targetPeerId,
                    SAA_in              const om::ObjPtr< BrokerProtocol >&             brokerProtocol,
                    SAA_in_opt          const om::ObjPtr< Payload >&                    payload
                    )
                    -> void
                {
                    BL_UNUSED( targetPeerId );
                    BL_UNUSED( payload );

                    BL_MUTEX_GUARD( messagesLock );

                    messagesReceived.push_back( om::copy( brokerProtocol ) );
                }
                )
            );

        const auto noOfMessagesReceived = [ & ]() -> std::size_t
        {
            BL_MUTEX_GUARD( messagesLock );

            return messagesReceived.size();
        };

        const auto waitForMessages = [ & ]( SAA_in const std::size_t expected ) -> void
        {
            const std::size_t maxRetries = 60U;
            std::size_t retries = 0U;

            for( ;; )
            {
                if( noOfMessagesReceived() >= expected )
                {
                    break;
                }

                if( retries >= maxRetries )
                {
                    UTF_FAIL( "The message was not delivered within 60 seconds" );

                    break;
                }

                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        };

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
                const bl::uuid_t peerIds[ 2 ] = { uuids::create(), uuids::create() };

                const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                auto connections = utils_t::createNoOfConnections( 2U );

                UTF_REQUIRE_EQUAL( 2U, connections.size() );

                std::vector< om::ObjPtrDisposable< MessagingClientObject > > clients;

                clients.reserve( 2U );

                for( std::size_t i = 0U; i < 2U; ++i )
                {
                    auto blockDispatch = om::lockDisposable(
                        utils_t::client_factory_t::createWithSmartDefaults(
                            om::copy( eq ),
                            peerIds[ i ],
                            om::copy( backend ),
                            om::copy( asyncWrapper ),
                            test::UtfArgsParser::host()                         /* host */,
                            test::UtfArgsParser::port()                         /* inboundPort */,
                            test::UtfArgsParser::port() + 1                     /* outboundPort */,
                            std::move( connections[ i ].first )                 /* inboundConnection */,
                            std::move( connections[ i ].second )                /* outboundConnection */,
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

                    clients.push_back( std::move( client ) );
                }

                const auto sendOneMessage = [ & ]( SAA_in const std::string& cookies ) -> void
                {
                    const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        uuids::create()                                         /* conversationId */,
                        cookies
                        );

                    scheduleAndExecuteInParallel(
                        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eqLocal ) -> void
                        {
                            eqLocal -> push_back(
                                ExternalCompletionTaskImpl::createInstance< Task >(
                                    cpp::bind(
                                        &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                        om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                            clients[ 0U ] -> outgoingObjectChannel().get()
                                            ),
                                        peerIds[ 1U ]                           /* targetPeerId */,
                                        om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                        om::ObjPtrCopyable< Payload >( payload ),
                                        _1 /* onReady - the completion callback */
                                        )
                                    )
                                );
                        }
                        );
                };

                /*
                 * The first message misses the cache, so the broker must create the
                 * authorization task, run it as its continuation and only then call update()
                 */

                sendOneMessage( cookiesText );

                waitForMessages( 1U );

                UTF_REQUIRE_EQUAL( 1U, authorizationCache -> createTaskCalls() );
                UTF_REQUIRE_EQUAL( 1U, authorizationCache -> updateCalls() );

                /*
                 * postAuthorization() must run strictly after the authorization task has
                 * completed - this is the ordering nothing else in the suite pins
                 */

                UTF_REQUIRE( authorizationCache -> lastTaskHandedToUpdate() );

                /*
                 * Note that Task::Completed is only ever set by the execution queue and only
                 * on the task it owns, so a task which is executed as a continuation of the
                 * broker backend wrapper task settles at Task::PendingCompletion instead
                 *
                 * Task::PendingCompletion is exactly what AuthorizationCacheImpl requires in
                 * tryGetRefreshedPrincipal(), i.e. any state other than Created or Running,
                 * so that check below is the production contract and it is captured at the
                 * moment update() was called - had postAuthorization() run before (or
                 * instead of) the authorization task, it would have been Created or Running
                 */

                const auto taskStateAtUpdate = authorizationCache -> updateTaskState();

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "The authorization task state observed by update() was "
                        << static_cast< int >( taskStateAtUpdate )
                    );

                UTF_REQUIRE( bl::tasks::Task::Created != taskStateAtUpdate );
                UTF_REQUIRE( bl::tasks::Task::Running != taskStateAtUpdate );

                {
                    BL_MUTEX_GUARD( messagesLock );

                    UTF_REQUIRE_EQUAL( 1U, messagesReceived.size() );

                    const auto& principalIdentityInfo =
                        messagesReceived[ 0U ] -> principalIdentityInfo();

                    UTF_REQUIRE( principalIdentityInfo );
                    UTF_REQUIRE( ! principalIdentityInfo -> authenticationToken() );
                    UTF_REQUIRE( principalIdentityInfo -> securityPrincipal() );

                    UTF_REQUIRE_EQUAL(
                        bl::str::to_lower_copy( principalIdentityInfo -> securityPrincipal() -> sid() ),
                        bl::str::to_lower_copy( utest::DummyAuthorizationCache::dummySid() )
                        );
                }

                /*
                 * The second message on the same connection carries the same token, which is
                 * in the cache now, so the hit arm must be taken instead
                 */

                sendOneMessage( cookiesText );

                waitForMessages( 2U );

                UTF_REQUIRE_EQUAL( 1U, authorizationCache -> createTaskCalls() );
                UTF_REQUIRE_EQUAL( 1U, authorizationCache -> updateCalls() );

                /*
                 * The third message carries a token which is not in the cache and the refresh
                 * is configured to fail, so the message must be rejected with
                 * BrokerErrorCodes::AuthorizationFailed and must not be forwarded at all
                 */

                authorizationCache -> failUpdate( true );

                try
                {
                    sendOneMessage( cookiesTextToFailRefresh );

                    UTF_FAIL( "Sending the message must fail when the authorization refresh fails" );
                }
                catch( ServerErrorException& e )
                {
                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

                    UTF_REQUIRE( ec );
                    UTF_REQUIRE_EQUAL(
                        *ec,
                        eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed )
                        );
                }

                UTF_REQUIRE_EQUAL( 2U, authorizationCache -> createTaskCalls() );
                UTF_REQUIRE_EQUAL( 2U, authorizationCache -> updateCalls() );

                /*
                 * Nothing must have been forwarded, i.e. the message was not delivered
                 * half-authorized
                 */

                os::sleep( time::seconds( 2L ) );

                UTF_REQUIRE_EQUAL( 2U, noOfMessagesReceived() );
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend(
            bl::om::qi< bl::security::AuthorizationCache >( authorizationCache )
            )
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingClientReconnectAndChannelIdTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef utest::TestMessagingUtils utils_t;

    /*
     * A messaging client which is created without pre-established connections starts out
     * disconnected and only the reconnect timer can bring it up - the first tick creates the
     * connection establisher task and only the next one, RECONNECT_TIMER_IN_SECONDS later,
     * observes it as completed and builds the sender and the receiver connections
     *
     * Every other case in the suite either hands the client pre-established connections or
     * tears the client down before that second tick, so this is the only place where the
     * timer driven connect / disconnect detection and the channel id regeneration which goes
     * with it are actually exercised
     */

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        const auto target = om::lockDisposable(
            MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
                [](
                    SAA_in              const bl::uuid_t&                               targetPeerId,
                    SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                    ) -> void
                {
                    BL_UNUSED( targetPeerId );
                    BL_UNUSED( dataBlock );
                }
                )
            );

        const auto peerId = uuids::create();

        const auto client = om::lockDisposable(
            MessagingClientFactorySsl::createWithSmartDefaults(
                peerId,
                om::copy( target ),
                test::UtfArgsParser::host(),
                test::UtfArgsParser::port()                         /* inboundPort */
                )
            );

        /*
         * The client has no connections at all yet, so it must report itself as disconnected
         * while still having a well defined (i.e. non-nil) channel id
         */

        const auto channelIdInitial = client -> channelId();

        UTF_REQUIRE( ! client -> isConnected() );
        UTF_REQUIRE( channelIdInitial != uuids::nil() );

        {
            const std::size_t maxRetries = 180U;
            std::size_t retries = 0U;

            for( ;; )
            {
                if( client -> isConnected() )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "The messaging client has connected to the broker in about "
                            << retries
                            << " seconds"
                        );

                    break;
                }

                if( retries >= maxRetries )
                {
                    UTF_FAIL( "The messaging client did not connect to the broker within 180 seconds" );

                    break;
                }

                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        }

        const auto channelIdConnected = client -> channelId();

        UTF_REQUIRE( client -> isConnected() );
        UTF_REQUIRE( channelIdConnected != uuids::nil() );
        UTF_REQUIRE( channelIdConnected != channelIdInitial );

        /*
         * The block counters are maintained by the sender and the receiver connections which
         * only exist once the reconnect timer has established them
         */

        {
            const auto clientImpl = om::tryQI< utils_t::client_t >( client );

            UTF_REQUIRE( clientImpl );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "The messaging client has sent "
                    << clientImpl -> noOfBlocksSent()
                    << " and received "
                    << clientImpl -> noOfBlocksReceived()
                    << " blocks"
                );
        }

        /*
         * Now stop the broker while the client is still alive and verify that the client
         * notices the disconnect and regenerates the channel id again
         */

        controlToken -> requestCancel();

        {
            const std::size_t maxRetries = 120U;
            std::size_t retries = 0U;

            for( ;; )
            {
                if( ! client -> isConnected() )
                {
                    break;
                }

                if( retries >= maxRetries )
                {
                    UTF_FAIL( "The messaging client did not detect the broker shutdown within 120 seconds" );

                    break;
                }

                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        }

        /*
         * Wait for at least one more reconnect timer tick, so the connected flags are flipped
         * back and the channel id is regenerated
         */

        os::sleep( time::seconds( 7L ) );

        const auto channelIdDisconnected = client -> channelId();

        UTF_REQUIRE( ! client -> isConnected() );
        UTF_REQUIRE( channelIdDisconnected != channelIdConnected );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests,
        om::copy( controlToken )
        );
}

UTF_AUTO_TEST_CASE( BrokerFacadeTests )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual run test)" );

    /*
     * This global lock needed to avoid conflicts with the default ports used below
     */

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */
        );
}

UTF_AUTO_TEST_CASE( ProxyBrokerFacadeTests )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual run test)" );

    typedef utest::TestMessagingUtils utils_t;

    utils_t::startBrokerProxy( /* ... ; use the defaults */ );
}

UTF_AUTO_TEST_CASE( ProxyBrokerClientBasicTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    typedef utest::TestMessagingUtils utils_t;

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
            BL_UNUSED( cookiesText );
            BL_UNUSED( dataBlocksPool );

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
             * Create UtfArgsParser::connections() messaging clients backed by the same
             * async wrapper
             *
             * Note that these don't own the backend and the queue, so when they get
             * disposed they will not actually dispose the backend, but just tear down
             * the connections
             */

            const auto noOfConnections = test::UtfArgsParser::connections();

            auto connections = utils_t::createNoOfConnections(
                noOfConnections,
                test::UtfArgsParser::host()                     /* brokerHostName */,
                utils_t::getDefaultProxyInboundPort()           /* brokerInboundPort */
                );

            UTF_REQUIRE_EQUAL( connections.size(), noOfConnections );

            utils_t::clients_list_t clients;

            for( std::size_t i = 0; i < test::UtfArgsParser::connections(); ++i )
            {
                const auto clientPeerId = uuids::create();

                auto blockDispatch = om::lockDisposable(
                    utils_t::client_factory_t::createWithSmartDefaults(
                        om::copy( eq ),
                        clientPeerId,
                        om::copy( backend ),
                        om::copy( asyncWrapper ),
                        test::UtfArgsParser::host()                         /* host */,
                        utils_t::getDefaultProxyInboundPort()               /* inboundPort */,
                        utils_t::getDefaultProxyInboundPort() + 1U          /* outboundPort */,
                        std::move( connections[ i ].first )                 /* inboundConnection */,
                        std::move( connections[ i ].second )                /* outboundConnection */,
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

                clients.emplace_back( std::make_pair( clientPeerId, std::move( client ) ) );
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
                        eqLocal -> setOptions( ExecutionQueue::OptionKeepAll );

                        const auto defaultDuration = time::seconds( 5L );

                        const auto timerCallback = [ & ]() -> time::time_duration
                        {
                            const auto task = eqLocal -> pop( false /* wait */ );

                            if( task )
                            {
                                if( task -> isFailed() )
                                {
                                    BL_LOG_MULTILINE(
                                        bl::Logging::debug(),
                                        BL_MSG()
                                            << "An exception occurred while trying to send a message:\n"
                                            << bl::eh::diagnostic_information( task -> exception() )
                                        );
                                }
                                else
                                {
                                    BL_LOG_MULTILINE(
                                        bl::Logging::debug(),
                                        BL_MSG()
                                            << "Last message was sent successfully!"
                                        );

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, 1U );
                                }
                            }

                            if( eqLocal -> isEmpty() )
                            {
                                const auto getRandomPos = [ & ]() -> std::size_t
                                {
                                    return random::getUniformRandomUnsignedValue< std::size_t >(
                                        clients.size() - 1
                                        );
                                };

                                const auto pos1 = getRandomPos();
                                const auto pos2 = getRandomPos();

                                const auto& sourceClient = clients[ pos1 ].second;
                                const auto& targetPeerId = clients[ pos2 ].first;

                                noOfMessagesDelivered = 0;

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
                            }

                            return defaultDuration;
                        };

                        SimpleTimer timer( timerCallback, cpp::copy( defaultDuration ) );

                        utils_t::waitForKeyOrTimeout();

                        eqLocal -> forceFlushNoThrow();
                    }
                    );
            }
        }
        );

    dispatchAssertions -> requireNone();
}

UTF_AUTO_TEST_CASE( BrokerClientTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    typedef utest::TestMessagingUtils utils_t;

    const auto incomingObjectChannel = om::lockDisposable(
        MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
            [](
                SAA_in              const bl::uuid_t&                               targetPeerId,
                SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                )
                -> void
            {
                BL_UNUSED( targetPeerId );
                BL_UNUSED( brokerProtocol );
                BL_UNUSED( payload );

                UTF_FAIL( "This should not be called from this test" );
            }
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
            BL_UNUSED( cookiesText );
            BL_UNUSED( dataBlocksPool );

            /*
             * Create UtfArgsParser::connections() messaging clients backed by the same
             * async wrapper
             *
             * Note that these don't own the backend and the queue, so when they get
             * disposed they will not actually dispose the backend, but just tear down
             * the connections
             */

            auto connections = utils_t::createConnections();

            UTF_REQUIRE_EQUAL( connections.size(), test::UtfArgsParser::connections() );

            std::vector< om::ObjPtrDisposable< MessagingClientBlockDispatch > > clients;

            for( std::size_t i = 0; i < test::UtfArgsParser::connections(); ++i )
            {
                const auto peerId = uuids::create();

                clients.emplace_back(
                    om::lockDisposable(
                        MessagingClientFactorySsl::createWithSmartDefaults(
                            om::copy( eq ),
                            peerId,
                            om::copy( backend ),
                            om::copy( asyncWrapper ),
                            test::UtfArgsParser::host(),
                            test::UtfArgsParser::port()                             /* inboundPort */,
                            test::UtfArgsParser::port() + 1                         /* outboundPort */,
                            std::move( connections[ i ].first )                     /* inboundConnection */,
                            std::move( connections[ i ].second )                    /* outboundConnection */
                            )
                        )
                    );
            }

            utils_t::waitForKeyOrTimeout();
        }
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingUtilsTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * Test message verification helpers
     */

    typedef bl::cpp::function < std::string& ( SAA_in BrokerProtocol& ) > string_lvalue_getter_t;

    const auto testInvalidPropertyValue = [](
        SAA_in              const string_lvalue_getter_t&                       getter,
        SAA_in              const char*                                         exceptionMessage,
        SAA_in              std::string&&                                       invalidValue
        ) -> void
    {
        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        getter( *brokerProtocol ) = BL_PARAM_FWD( invalidValue );

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol ),
            InvalidDataFormatException,
            exceptionMessage
            );
    };

    testInvalidPropertyValue(
        bl::cpp::mem_fn( &BrokerProtocol::messageTypeLvalue )           /* getter */,
        "The message type specified is invalid ''"                      /* exceptionMessage */,
        ""                                                              /* invalidValue */
        );

    testInvalidPropertyValue(
        bl::cpp::mem_fn( &BrokerProtocol::messageTypeLvalue )           /* getter */,
        "The message type specified is invalid 'foo'"                   /* exceptionMessage */,
        "foo"                                                           /* invalidValue */
        );

    testInvalidPropertyValue(
        bl::cpp::mem_fn( &BrokerProtocol::messageIdLvalue )             /* getter */,
        "The 'messageId' property cannot be empty"                      /* exceptionMessage */,
        ""                                                              /* invalidValue */
        );

    testInvalidPropertyValue(
        bl::cpp::mem_fn( &BrokerProtocol::conversationIdLvalue )        /* getter */,
        "The 'conversationId' property cannot be empty"                 /* exceptionMessage */,
        ""                                                              /* invalidValue */
        );

    {
        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        brokerProtocol -> principalIdentityInfoLvalue() = nullptr;

        MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol );
    }

    {
        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        const auto& principalIdentityInfo = brokerProtocol -> principalIdentityInfo();
        UTF_REQUIRE( principalIdentityInfo );

        UTF_REQUIRE( principalIdentityInfo -> authenticationTokenLvalue() );
        principalIdentityInfo -> securityPrincipalLvalue() = createTestSecurityPrincipal();

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol ),
            InvalidDataFormatException,
            "Principal identity info is invalid: either security principal or authentication token must be provided"
            );

        principalIdentityInfo -> authenticationTokenLvalue() = nullptr;
        principalIdentityInfo -> securityPrincipalLvalue() = nullptr;

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol ),
            InvalidDataFormatException,
            "Principal identity info is invalid: either security principal or authentication token must be provided"
            );
    }

    typedef bl::cpp::function < std::string& ( SAA_in AuthenticationToken& ) > token_lvalue_getter_t;

    const auto testInvalidTokenPropertyValue = [](
        SAA_in              const token_lvalue_getter_t&                        getter,
        SAA_in              const char*                                         exceptionMessage,
        SAA_in              std::string&&                                       invalidValue
        ) -> void
    {
        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        const auto& principalIdentityInfo = brokerProtocol -> principalIdentityInfo();
        UTF_REQUIRE( principalIdentityInfo );

        const auto& authenticationToken = principalIdentityInfo -> authenticationToken();
        UTF_REQUIRE( authenticationToken );

        getter( *authenticationToken ) = BL_PARAM_FWD( invalidValue );

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol ),
            InvalidDataFormatException,
            exceptionMessage
            );
    };

    testInvalidTokenPropertyValue(
        bl::cpp::mem_fn( &AuthenticationToken::typeLvalue )          /* getter */,
        "The authentication token type specified is invalid"         /* exceptionMessage */,
        ""                                                           /* invalidValue */
        );

    testInvalidTokenPropertyValue(
        bl::cpp::mem_fn( &AuthenticationToken::dataLvalue )          /* getter */,
        "The authentication token data is invalid or unavailable"    /* exceptionMessage */,
        ""                                                           /* invalidValue */
        );

    typedef bl::cpp::function < std::string& ( SAA_in SecurityPrincipal& ) > principal_lvalue_getter_t;

    const auto testInvalidPrincipalPropertyValue = [](
        SAA_in              const principal_lvalue_getter_t&                    getter,
        SAA_in              const char*                                         exceptionMessage,
        SAA_in              std::string&&                                       invalidValue
        ) -> void
    {
        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        const auto& principalIdentityInfo = brokerProtocol -> principalIdentityInfo();
        UTF_REQUIRE( principalIdentityInfo );
        principalIdentityInfo -> authenticationTokenLvalue() = nullptr;

        auto& securityPrincipal = principalIdentityInfo -> securityPrincipalLvalue();
        UTF_REQUIRE( ! securityPrincipal );

        securityPrincipal = createTestSecurityPrincipal();

        getter( *securityPrincipal ) = BL_PARAM_FWD( invalidValue );

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyBrokerProtocolMessage( brokerProtocol ),
            InvalidDataFormatException,
            exceptionMessage
            );
    };

    const char* exceptionMessageExpected =
        "The security principal information specified is invalid as one of the required fields is empty";

    testInvalidPrincipalPropertyValue(
        bl::cpp::mem_fn( &SecurityPrincipal::sidLvalue )            /* getter */,
        exceptionMessageExpected                                    /* exceptionMessage */,
        ""                                                          /* invalidValue */
        );

    /*
     * Test serialization & deserialization helpers
     */

    {
        const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

        const auto conversationId = uuids::create();
        const auto messageId = uuids::create();

        const std::string cookiesText( "<test cookies>" );

        const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            conversationId,
            cookiesText,
            messageId
            );

        UTF_REQUIRE_EQUAL( brokerProtocol -> messageType(), MessageType::toString( MessageType::AsyncRpcDispatch ) );
        UTF_REQUIRE_EQUAL( brokerProtocol -> messageId(), uuids::uuid2string( messageId ) );
        UTF_REQUIRE_EQUAL( brokerProtocol -> conversationId(), uuids::uuid2string( conversationId ) );

        UTF_REQUIRE( brokerProtocol -> principalIdentityInfo() );
        UTF_REQUIRE( brokerProtocol -> principalIdentityInfo() -> authenticationToken() );
        UTF_REQUIRE( ! brokerProtocol -> principalIdentityInfo() -> securityPrincipal() );

        const auto& authenticationToken = brokerProtocol -> principalIdentityInfo() -> authenticationToken();

        UTF_REQUIRE_EQUAL(
            authenticationToken -> type(),
            utest::DummyAuthorizationCache::dummyTokenType()
            );
        UTF_REQUIRE_EQUAL( authenticationToken -> data(), cookiesText );

        const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
            );

        const auto dataBlock = MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload, dataBlocksPool );

        const auto dataBlockCopy = MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload );

        UTF_REQUIRE_EQUAL( dataBlock -> size(), dataBlockCopy -> size() );
        UTF_REQUIRE_EQUAL( dataBlock -> offset1(), dataBlockCopy -> offset1() );

        UTF_REQUIRE_EQUAL(
            0,
            std::memcmp(
                dataBlock -> begin(),
                dataBlockCopy -> begin(),
                dataBlock -> size() )
            );

        const auto pair = MessagingUtils::deserializeBlockToObjects( dataBlock );

        UTF_REQUIRE( pair.second );

        utest::DataModelTestUtils::requireObjectsEqual( brokerProtocol, pair.first /* brokerProtocol */ );
        utest::DataModelTestUtils::requireObjectsEqual( payload, pair.second /* payload */ );
    }

    /*
     * Test the 'brokerProtocolOnly' flag of deserializeBlockToObjects() and the
     * verifyPayloadMessage() gate it skips
     *
     * The proxy hot path parses every forwarded block with brokerProtocolOnly=true
     * specifically to avoid paying for payload parsing and validation, so a regression
     * which ignores the flag would both cost throughput and start rejecting perfectly
     * valid forwarded messages whose payload is not an async RPC document
     */

    {
        const auto emptyPayload = Payload::createInstance();

        const auto bpDispatch = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        const auto blockDispatch = MessagingUtils::serializeObjectsToBlock( bpDispatch, emptyPayload );

        const auto bpNotification = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncNotification,
            uuids::create()                 /* conversationId */,
            "<test cookies>"                /* cookiesText */
            );

        const auto blockNotification =
            MessagingUtils::serializeObjectsToBlock( bpNotification, emptyPayload );

        /*
         * An empty payload serializes to '{}', so the payload region is present and the
         * branch under test is actually reachable
         */

        UTF_REQUIRE( blockDispatch -> offset1() > 0U );

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::deserializeBlockToObjects( blockDispatch ),
            InvalidDataFormatException,
            "Payload message has to contain either a request or a response"
            );

        const auto pairOnly =
            MessagingUtils::deserializeBlockToObjects( blockDispatch, true /* brokerProtocolOnly */ );

        UTF_REQUIRE( pairOnly.first );
        UTF_REQUIRE( ! pairOnly.second );

        utest::DataModelTestUtils::requireObjectsEqual( pairOnly.first, bpDispatch );

        /*
         * The non-AsyncRpcDispatch skip arm of verifyPayloadMessage() returns the payload
         * without validating it
         */

        UTF_REQUIRE_NO_THROW( MessagingUtils::deserializeBlockToObjects( blockNotification ) );
        UTF_REQUIRE( MessagingUtils::deserializeBlockToObjects( blockNotification ).second );

        /*
         * On a block carrying a perfectly valid async RPC request payload the flag is the
         * only difference between a parsed payload and no payload at all
         */

        const auto validPayload = bl::dm::DataModelUtils::loadFromFile< Payload >(
            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
            );

        const auto blockValid = MessagingUtils::serializeObjectsToBlock( bpDispatch, validPayload );

        const auto pairFull =
            MessagingUtils::deserializeBlockToObjects( blockValid, true /* brokerProtocolOnly */ );

        UTF_REQUIRE( ! pairFull.second );
        UTF_REQUIRE( MessagingUtils::deserializeBlockToObjects( blockValid ).second );

        /*
         * The null payload arm of verifyPayloadMessage() which deserializeBlockToObjects()
         * can never reach, as it only calls it when the payload region is non-empty
         */

        UTF_REQUIRE_NO_THROW( MessagingUtils::verifyPayloadMessage( bpNotification, nullptr ) );

        UTF_REQUIRE_THROW_MESSAGE(
            MessagingUtils::verifyPayloadMessage( bpDispatch, nullptr ),
            InvalidDataFormatException,
            "Payload message has to contain either a request or a response"
            );
    }

    /*
     * Test the endpoints expansion helper
     */

    {
        std::vector< std::string > endpoints;

        /*
         * Test various scenarios when the input is even number (2)
         */

        endpoints.push_back( "ep1:1234" );
        endpoints.push_back( "ep2:5678" );

        const auto testExpandEndpoints = [ & ](
            SAA_in          const std::size_t                                       noOfRequestedEndpoints,
            SAA_in          const std::size_t                                       expectedMultiplier
            )
            -> void
        {
            const auto expandedEndpoints =
                MessagingUtils::expandEndpoints( noOfRequestedEndpoints, cpp::copy( endpoints ) );

            UTF_REQUIRE( expandedEndpoints.size() >= endpoints.size() );
            UTF_REQUIRE_EQUAL( expandedEndpoints.size(), expectedMultiplier * endpoints.size() );

            for( std::size_t i = 0U, count = expandedEndpoints.size(); i < count; ++i )
            {
                UTF_REQUIRE_EQUAL( expandedEndpoints[ i ], endpoints[ i % endpoints.size() ] );
            }
        };

        const auto testAllScenarios = [ & ]() -> void
        {
            /*
             * Test various scenarios
             */

            testExpandEndpoints( 0U /* noOfRequestedEndpoints */, 1U /* expectedMultiplier */ );
            testExpandEndpoints( 1U /* noOfRequestedEndpoints */, 1U /* expectedMultiplier */ );
            testExpandEndpoints( endpoints.size() /* noOfRequestedEndpoints */, 1U /* expectedMultiplier */ );
            testExpandEndpoints( endpoints.size() + 1U /* noOfRequestedEndpoints */, 2U /* expectedMultiplier */ );
            testExpandEndpoints( endpoints.size() * 2U /* noOfRequestedEndpoints */, 2U /* expectedMultiplier */ );
            testExpandEndpoints( endpoints.size() * 2U + 1 /* noOfRequestedEndpoints */, 3U /* expectedMultiplier */ );
        };

        /*
         * Test various scenarios when the input is even number (2)
         */

        testAllScenarios();

        /*
         * Test various scenarios when the input is odd number (3)
         */

        endpoints.push_back( "ep3:9012" );

        testAllScenarios();
    }

    /*
     * Test updateBrokerProtocolMessageInBlock() - the skipUpdateIfUnchanged short-circuit and
     * the two capacity guards
     *
     * Note that the sibling 'offset1 <= size' BL_CHK_T guard is deliberately not covered here;
     * constructing a block which violates it requires DataBlock::setOffset1() / setSize(), both
     * of which carry a BL_ASSERT( offset1 <= size ) which aborts a debug build
     */

    {
        const auto payload = bl::dm::DataModelUtils::loadFromFile< Payload >(
            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
            );

        const auto sourcePeerId = uuids::create();
        const auto targetPeerId = uuids::create();

        const std::string cookiesText( "<test cookies>" );

        /*
         * The skipUpdateIfUnchanged early return must leave an already fully addressed block
         * byte identical - ProxyBrokerBackendProcessingFactory relies on that to avoid
         * re-packing every single forwarded block
         */

        {
            const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                MessageType::AsyncRpcDispatch,
                uuids::create()                                     /* conversationId */,
                cookiesText
                );

            const auto originalSourcePeerId = uuids::uuid2string( uuids::create() );
            const auto originalTargetPeerId = uuids::uuid2string( uuids::create() );

            brokerProtocol -> sourcePeerId( originalSourcePeerId );
            brokerProtocol -> targetPeerId( originalTargetPeerId );

            const auto dataBlock = MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload );

            const std::string before( dataBlock -> begin(), dataBlock -> begin() + dataBlock -> size() );

            const auto offset1Before = dataBlock -> offset1();

            MessagingUtils::updateBrokerProtocolMessageInBlock(
                brokerProtocol,
                dataBlock,
                sourcePeerId,
                targetPeerId,
                true                                                /* skipUpdateIfUnchanged */
                );

            UTF_REQUIRE_EQUAL( dataBlock -> size(), before.size() );
            UTF_REQUIRE_EQUAL( dataBlock -> offset1(), offset1Before );

            UTF_REQUIRE_EQUAL( 0, std::memcmp( dataBlock -> begin(), before.data(), before.size() ) );

            /*
             * The peer ids which were passed in must not have been applied
             */

            UTF_REQUIRE_EQUAL( brokerProtocol -> sourcePeerId(), originalSourcePeerId );
            UTF_REQUIRE_EQUAL( brokerProtocol -> targetPeerId(), originalTargetPeerId );
        }

        /*
         * With skipUpdateIfUnchanged left at its default the block is rewritten and must still
         * round trip, and the payload region in front of offset1 must be left alone
         */

        {
            const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                MessageType::AsyncRpcDispatch,
                uuids::create()                                     /* conversationId */,
                cookiesText
                );

            brokerProtocol -> sourcePeerId( uuids::uuid2string( uuids::create() ) );
            brokerProtocol -> targetPeerId( uuids::uuid2string( uuids::create() ) );

            const auto dataBlock = MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload );

            const auto offset1Before = dataBlock -> offset1();

            MessagingUtils::updateBrokerProtocolMessageInBlock(
                brokerProtocol,
                dataBlock,
                sourcePeerId,
                targetPeerId,
                false                                               /* skipUpdateIfUnchanged */
                );

            const auto pair = MessagingUtils::deserializeBlockToObjects( dataBlock );

            utest::DataModelTestUtils::requireObjectsEqual( pair.first /* brokerProtocol */, brokerProtocol );
            utest::DataModelTestUtils::requireObjectsEqual( pair.second /* payload */, payload );

            UTF_REQUIRE_EQUAL( dataBlock -> offset1(), offset1Before );
        }

        /*
         * The same again, but starting from a document whose peer ids are empty on entry - the
         * function mutates the caller's document, which
         * BrokerBackendProcessing::serializeBrokerProtocolMessage() depends on
         */

        {
            const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                MessageType::AsyncRpcDispatch,
                uuids::create()                                     /* conversationId */,
                cookiesText
                );

            UTF_REQUIRE( brokerProtocol -> sourcePeerId().empty() );
            UTF_REQUIRE( brokerProtocol -> targetPeerId().empty() );

            const auto dataBlock = MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload );

            const auto offset1Before = dataBlock -> offset1();

            MessagingUtils::updateBrokerProtocolMessageInBlock(
                brokerProtocol,
                dataBlock,
                sourcePeerId,
                targetPeerId,
                false                                               /* skipUpdateIfUnchanged */
                );

            UTF_REQUIRE_EQUAL( brokerProtocol -> sourcePeerId(), uuids::uuid2string( sourcePeerId ) );
            UTF_REQUIRE_EQUAL( brokerProtocol -> targetPeerId(), uuids::uuid2string( targetPeerId ) );

            const auto pair = MessagingUtils::deserializeBlockToObjects( dataBlock );

            utest::DataModelTestUtils::requireObjectsEqual( pair.first /* brokerProtocol */, brokerProtocol );
            utest::DataModelTestUtils::requireObjectsEqual( pair.second /* payload */, payload );

            UTF_REQUIRE_EQUAL( dataBlock -> offset1(), offset1Before );
        }

        /*
         * A small helper which drives the function into one of the two capacity guards and
         * verifies both the message and the broker error code it carries, plus the fact that
         * the block was not partially overwritten before the throw
         */

        const auto requireCapacityTooSmall = [ & ](
            SAA_in              const om::ObjPtr< BrokerProtocol >&                 brokerProtocol,
            SAA_in              const om::ObjPtr< data::DataBlock >&                dataBlock
            )
            -> void
        {
            const auto sizeBeforeTheCall = dataBlock -> size();

            UTF_REQUIRE_THROW_MESSAGE(
                MessagingUtils::updateBrokerProtocolMessageInBlock(
                    brokerProtocol,
                    dataBlock,
                    sourcePeerId,
                    targetPeerId
                    ),
                ServerErrorException,
                "DataBlock capacity is too small"
                );

            UTF_REQUIRE_EQUAL( dataBlock -> size(), sizeBeforeTheCall );

            bool didThrow = false;

            try
            {
                MessagingUtils::updateBrokerProtocolMessageInBlock(
                    brokerProtocol,
                    dataBlock,
                    sourcePeerId,
                    targetPeerId
                    );
            }
            catch( ServerErrorException& e )
            {
                didThrow = true;

                const auto* ec = e.errorCode();

                UTF_REQUIRE(
                    ec && eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) == *ec
                    );
            }

            UTF_REQUIRE( didThrow );

            UTF_REQUIRE_EQUAL( dataBlock -> size(), sizeBeforeTheCall );
        };

        /*
         * The 'jsonString.size() > capacity' half of the guard - the block capacity is exactly
         * the size of the old protocol JSON, so it cannot possibly hold the longer one which
         * carries the two peer ids
         */

        {
            const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                MessageType::AsyncRpcDispatch,
                uuids::create()                                     /* conversationId */,
                cookiesText
                );

            const auto protocolJson = bl::dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );

            const auto dataBlock = data::DataBlock::createInstance( protocolJson.size() );

            dataBlock -> setOffset1( 0U );
            dataBlock -> setSize( 0U );
            dataBlock -> write( protocolJson.c_str(), protocolJson.size() );

            UTF_REQUIRE_EQUAL( dataBlock -> capacity(), protocolJson.size() );

            requireCapacityTooSmall( brokerProtocol, dataBlock );
        }

        /*
         * The wrap safe 'protocolDataOffset > capacity - jsonString.size()' half of the guard -
         * the capacity is one byte short of what the updated protocol data needs at offset1
         * while still being large enough for the updated JSON on its own, so this is the only
         * half of the check which can reject the block
         *
         * The pre-cb431f0 form of the check, 'protocolDataOffset + jsonString.size() > capacity',
         * is the one which can wrap for a large offset or a large JSON string
         */

        {
            const auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                MessageType::AsyncRpcDispatch,
                uuids::create()                                     /* conversationId */,
                cookiesText
                );

            /*
             * Compute the JSON the function is going to produce once it has filled in both peer
             * ids, then put the document back into its original state; an empty string property
             * is omitted altogether by the packed serializer
             */

            brokerProtocol -> sourcePeerId( uuids::uuid2string( sourcePeerId ) );
            brokerProtocol -> targetPeerId( uuids::uuid2string( targetPeerId ) );

            const auto updatedJson = bl::dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );

            brokerProtocol -> sourcePeerId( std::string() );
            brokerProtocol -> targetPeerId( std::string() );

            UTF_REQUIRE( brokerProtocol -> sourcePeerId().empty() );
            UTF_REQUIRE( brokerProtocol -> targetPeerId().empty() );

            const auto protocolJson = bl::dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );
            const auto payloadJson = bl::dm::DataModelUtils::getDocAsPackedJsonString( payload );

            UTF_REQUIRE( updatedJson.size() > protocolJson.size() );

            const auto dataBlock =
                data::DataBlock::createInstance( payloadJson.size() + updatedJson.size() - 1U );

            dataBlock -> setOffset1( 0U );
            dataBlock -> setSize( 0U );
            dataBlock -> write( payloadJson.c_str(), payloadJson.size() );
            dataBlock -> setOffset1( payloadJson.size() );
            dataBlock -> write( protocolJson.c_str(), protocolJson.size() );

            /*
             * The updated protocol JSON on its own fits, so the first half of the guard cannot
             * be what rejects this block
             */

            UTF_REQUIRE( updatedJson.size() <= dataBlock -> capacity() );

            requireCapacityTooSmall( brokerProtocol, dataBlock );
        }
    }
}

UTF_AUTO_TEST_CASE( MessagingUtils_TokenTypeConcurrencyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * utest::TestMessagingUtils::createBrokerProtocolMessage() lazily initializes the static
     * g_tokenType under g_tokenTypeLock, and the cached value must never be read outside of
     * that guard - otherwise a thread copying the string races the thread assigning to it
     *
     * The window is only open while the cache is cold, which is what makes the race a rare
     * and unreproducible failure rather than a reliable one, so this pins the concurrent
     * path with 16 tasks constructing 50 messages each
     */

    const std::size_t noOfTasks = 16U;
    const std::size_t noOfMessagesPerTask = 50U;

    const auto& cookiesText = utest::TestMessagingUtils::getTokenData();

    utest::DeferredAssertions assertions;

    std::atomic< std::size_t > noOfMessagesCreated( 0U );

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepNone );

            for( std::size_t i = 0U; i < noOfTasks; ++i )
            {
                eq -> push_back(
                    SimpleTaskImpl::createInstance< Task >(
                        [ & ]() -> void
                        {
                            for( std::size_t j = 0U; j < noOfMessagesPerTask; ++j )
                            {
                                const auto brokerProtocol =
                                    utest::TestMessagingUtils::createBrokerProtocolMessage(
                                        MessageType::AsyncRpcDispatch,
                                        uuids::create()                 /* conversationId */,
                                        cookiesText
                                        );

                                const auto& principalIdentityInfo =
                                    brokerProtocol -> principalIdentityInfo();

                                UTF_RECORD( assertions, nullptr != principalIdentityInfo );

                                if( principalIdentityInfo )
                                {
                                    UTF_RECORD(
                                        assertions,
                                        principalIdentityInfo -> authenticationToken() -> type() ==
                                            utest::DummyAuthorizationCache::dummyTokenType()
                                        );
                                }

                                ++noOfMessagesCreated;
                            }
                        }
                        )
                    );
            }
        }
        );

    assertions.requireNone();

    UTF_REQUIRE_EQUAL( noOfMessagesCreated.load(), noOfTasks * noOfMessagesPerTask );
}

UTF_AUTO_TEST_CASE( MessagingUtils_RetryableBrokerErrorTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * isRetryableMessagingBrokerError() is the single policy function which decides whether the
     * messaging layer retries a failed conversation or fails it, and it is built out of three
     * ordered catch arms - a ServerErrorException arm gated on BrokerErrorCodes::isExpectedErrorCode(),
     * a SystemException arm gated on isExpectedSocketException() with isCancelExpected false plus
     * isExpectedSslException(), and a final std::exception arm which only accepts the
     * ErrorUuidNotConnectedToBroker decoration
     *
     * Note that ServerErrorException derives from BaseExceptionDefault and not from
     * SystemException, so a non-retryable server error is never re-examined by the later arms
     *
     * The table below deliberately contains no null row - cpp::safeRethrowException( nullptr )
     * is a BL_RIP_MSG which would terminate the process
     */

    const auto check = [](
        SAA_in              const std::exception_ptr&                               eptr,
        SAA_in              const bool                                              expected
        )
        -> void
    {
        UTF_REQUIRE_EQUAL( MessagingUtils::isRetryableMessagingBrokerError( eptr ), expected );
    };

    /*
     * The two retryable broker error codes
     */

    check(
        BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound ) ),
            "target peer not found"
            ),
        true
        );

    check(
        BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) ),
            "target peer queue full"
            ),
        true
        );

    /*
     * The two broker error codes which are expected, but are not transient
     */

    check(
        BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed ) ),
            "authorization failed"
            ),
        false
        );

    check(
        BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) ),
            "protocol validation failed"
            ),
        false
        );

    /*
     * A server error with no error code at all
     */

    check( BL_MAKE_EXCEPTION_PTR( ServerErrorException(), "no error code" ), false );

    /*
     * The same numeric value in the wrong category - this is the eh::generic_category() guard
     * inside BrokerErrorCodes::isExpectedErrorCode(), which is what stops an unrelated platform
     * errno 99 from being mistaken for a broker error
     */

    {
        const int targetPeerNotFoundValue = BrokerErrorCodes::TargetPeerNotFound;

        check(
            BL_MAKE_EXCEPTION_PTR(
                ServerErrorException()
                    << eh::errinfo_error_code( eh::error_code( targetPeerNotFoundValue, eh::system_category() ) ),
                "wrong category"
                ),
            false
            );
    }

    /*
     * The socket errors which isExpectedSocketException() accepts
     */

    check(
        std::make_exception_ptr(
            SystemException::create( asio::error::make_error_code( asio::error::eof ), "eof" )
            ),
        true
        );

    check(
        std::make_exception_ptr(
            SystemException::create( eh::errc::make_error_code( eh::errc::connection_refused ), "refused" )
            ),
        true
        );

    check(
        std::make_exception_ptr(
            SystemException::create( eh::errc::make_error_code( eh::errc::broken_pipe ), "pipe" )
            ),
        true
        );

    /*
     * A cancelled operation must NOT look retryable - isRetryableMessagingBrokerError() passes
     * isCancelExpected as false deliberately, and flipping it would turn shutdown into a retry
     * storm
     */

    check(
        std::make_exception_ptr(
            SystemException::create( asio::error::make_error_code( asio::error::operation_aborted ), "cancel" )
            ),
        false
        );

    check(
        std::make_exception_ptr(
            SystemException::create(
                eh::errc::make_error_code( eh::errc::no_such_file_or_directory ),
                "enoent"
                )
            ),
        false
        );

    /*
     * The final catch arm - only the not-connected-to-broker decoration is retryable
     */

    check(
        BL_MAKE_EXCEPTION_PTR(
            NotSupportedException()
                << eh::errinfo_error_uuid( uuiddefs::ErrorUuidNotConnectedToBroker() ),
            "not connected"
            ),
        true
        );

    check(
        BL_MAKE_EXCEPTION_PTR(
            NotSupportedException()
                << eh::errinfo_error_uuid( uuiddefs::ErrorUuidResponseTimeout() ),
            "response timeout"
            ),
        false
        );

    check( BL_MAKE_EXCEPTION_PTR( UnexpectedException(), "plain unexpected" ), false );
}

UTF_AUTO_TEST_CASE( BackendProcessingDefaultsTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * isConnected() is the signal which turns "the broker is unreachable" into a clean rejection
     * instead of a hung request; BackendProcessingBase defaults it to true, the forwarding
     * backend overrides it to delegate to its rotating outgoing channel, and the proxy backend
     * simply inherits the default
     *
     * autoBlockDispatching() is what decides whether the dispatching backend chains a
     * DispatchingTask on top - BrokerBackendProcessingT overrides it to false, and flipping it
     * on any of these backends would silently double dispatch every message
     */

    const auto brokerBackend = om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    UTF_REQUIRE( brokerBackend -> isConnected() );

    UTF_REQUIRE( ! brokerBackend -> autoBlockDispatching() );

    /*
     * This pins the QI table entry which BrokerDispatchingBackendProcessing's constructor
     * depends on
     */

    const auto acceptorNotify = om::tryQI< AcceptorNotify >( brokerBackend );

    UTF_REQUIRE( acceptorNotify );
}

UTF_AUTO_TEST_CASE( ForwardingBackendConnectFailureTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * ForwardingBackendProcessingFactory::create() races an inbound and an outbound connection
     * establisher per expanded endpoint and, when no pair has both halves connected, throws a
     * user friendly UnexpectedException carrying the first captured socket failure as a nested
     * exception
     *
     * This is the error a mis-configured deployment hits first and the exception an operator
     * actually sees, so the nested cause, the user friendly marking and the fact that the throw
     * happens early - rather than falling into the 60 second connectivity poll - all need pinning
     */

    test::MachineGlobalTestLock lock;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    /*
     * Nothing ever listens on this port - the suite itself only uses the 28100 - 28103 range and
     * the machine global lock keeps a concurrently running server test out of the way
     */

    const auto deadPort = static_cast< unsigned short >( test::UtfArgsParser::port() + 40U );

    const auto testAllEndpointsDead = [ & ]( SAA_in const bool waitAllToConnect ) -> void
    {
        const auto createBackend = [ & ]() -> void
        {
            const auto backend = om::lockDisposable(
                ForwardingBackendProcessingFactoryDefaultSsl::create(
                    deadPort                        /* defaultInboundPort */,
                    om::copy( controlToken ),
                    uuids::create()                 /* peerId */,
                    2U                              /* noOfConnections */,
                    utest::TestMessagingUtils::getTestEndpointsList(
                        test::UtfArgsParser::host(),
                        deadPort,
                        1U                          /* noOfEndpoints */
                        ),
                    dataBlocksPool,
                    0U                              /* threadsCount */,
                    0U                              /* maxConcurrentTasks */,
                    waitAllToConnect
                    )
                );

            BL_UNUSED( backend );
        };

        const auto startTime = time::microsec_clock::universal_time();

        UTF_REQUIRE_THROW_MESSAGE(
            createBackend(),
            UnexpectedException,
            "The backend can't connect to any of the endpoints provided"
            );

        const auto elapsed = time::microsec_clock::universal_time() - startTime;

        /*
         * A connect refused on loopback resolves within a few MAX_RETRY_COUNT attempts; this
         * bound is what catches a regression which made the failure path fall into the 60 second
         * connectivity poll instead of throwing early
         */

        UTF_REQUIRE( elapsed < time::seconds( 30L ) );

        /*
         * Run the same call once more, this time to inspect the exception itself
         */

        bool createDidThrow = false;

        try
        {
            createBackend();
        }
        catch( UnexpectedException& e )
        {
            createDidThrow = true;

            const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( nullptr != nested );

            /*
             * The underlying socket failure must survive as the nested cause; its concrete type
             * differs per platform, so only the fact that it is a std::exception is pinned here
             */

            bool nestedWasRethrown = false;

            try
            {
                cpp::safeRethrowException( *nested );
            }
            catch( std::exception& nestedException )
            {
                nestedWasRethrown = true;

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "The nested connect failure is: "
                        << nestedException.what()
                    );
            }

            UTF_REQUIRE( nestedWasRethrown );

            UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_is_user_friendly >( e ) );
        }

        UTF_REQUIRE( createDidThrow );
    };

    testAllEndpointsDead( true /* waitAllToConnect */ );

    /*
     * The "no endpoint connected" throw happens before the connectivity poll loop, so the
     * waitAllToConnect flag must not change the outcome at all
     */

    testAllEndpointsDead( false /* waitAllToConnect */ );
}

UTF_AUTO_TEST_CASE( IO_MessagingClientDisposedContractTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * The messaging client accessors are polled from timers while dispose() can run
     * concurrently, so they copy the state out under the lock and return safe defaults once the
     * state has been released - isConnected() false, channelId() nil and isNoCopyDataBlocks()
     * false, with the setter becoming a no-op
     *
     * The two pushBlock() failure shapes are equally load bearing: the not-connected one is
     * decorated with ErrorUuidNotConnectedToBroker and is therefore retryable, while the disposed
     * one deliberately carries no error uuid and must never be retried
     */

    test::MachineGlobalTestLock lock;

    /*
     * Nothing is listening on this port - it is outside the 28100 - 28103 range the suite uses -
     * so no connection is ever established and the sink below can never be invoked; a stray
     * listener makes this case fail loudly rather than pass silently
     */

    const auto deadPort = static_cast< unsigned short >( test::UtfArgsParser::port() + 4U );

    const auto sink = om::lockDisposable(
        MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
            [](
                SAA_in              const bl::uuid_t&                               targetPeerId,
                SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                ) -> void
            {
                BL_UNUSED( targetPeerId );
                BL_UNUSED( dataBlock );

                UTF_FAIL( "The block dispatch sink must not be called" );
            }
            )
        );

    const auto client = om::lockDisposable(
        MessagingClientFactorySsl::createWithSmartDefaults(
            uuids::create()                                     /* peerId */,
            om::copy( sink ),
            test::UtfArgsParser::host(),
            deadPort                                            /* inboundPort */
            )
        );

    /*
     * The alive but not yet connected state
     */

    UTF_REQUIRE( ! client -> isConnected() );

    const auto channelId = client -> channelId();

    UTF_REQUIRE( channelId != uuids::nil() );

    UTF_REQUIRE( ! client -> isNoCopyDataBlocks() );

    client -> isNoCopyDataBlocks( true );

    UTF_REQUIRE( client -> isNoCopyDataBlocks() );

    /*
     * The channel id is only regenerated when the connection status flips, so it is stable while
     * nothing changes
     */

    UTF_REQUIRE_EQUAL( channelId, client -> channelId() );

    /*
     * Pushing a block while not connected must fail with the decorated NotSupportedException
     */

    {
        const auto dataBlock = data::DataBlock::createInstance( 1024U );

        UTF_REQUIRE_THROW_MESSAGE(
            client -> pushBlock( uuids::create() /* targetPeerId */, dataBlock ),
            NotSupportedException,
            "Messaging client is not connected to messaging broker"
            );

        bool pushDidThrow = false;

        try
        {
            client -> pushBlock( uuids::create() /* targetPeerId */, dataBlock );
        }
        catch( NotSupportedException& e )
        {
            pushDidThrow = true;

            const auto* uuid = eh::get_error_info< eh::errinfo_error_uuid >( e );

            UTF_REQUIRE( uuid && *uuid == uuiddefs::ErrorUuidNotConnectedToBroker() );

            /*
             * This decoration is the only thing which makes the final catch arm of
             * isRetryableMessagingBrokerError() return true - dropping it silently turns
             * "retry after a transient disconnect" into "fail the conversation"
             */

            UTF_REQUIRE( MessagingUtils::isRetryableMessagingBrokerError( std::current_exception() ) );
        }

        UTF_REQUIRE( pushDidThrow );
    }

    /*
     * After dispose() every accessor must return its safe default; before commit cb431f0 these
     * dereferenced an already released state
     */

    client -> dispose();

    UTF_REQUIRE( ! client -> isConnected() );

    UTF_REQUIRE_EQUAL( client -> channelId(), uuids::nil() );

    UTF_REQUIRE( ! client -> isNoCopyDataBlocks() );

    UTF_REQUIRE_NO_THROW( client -> isNoCopyDataBlocks( true ) );

    UTF_REQUIRE( ! client -> isNoCopyDataBlocks() );

    /*
     * The disposed push carries no error uuid, so a disposed client is deliberately not
     * retryable - otherwise the async-RPC layer would retry it forever
     */

    {
        UTF_REQUIRE_THROW_MESSAGE(
            client -> pushBlock(
                uuids::create()                                 /* targetPeerId */,
                data::DataBlock::createInstance( 128U )
                ),
            UnexpectedException,
            "Messaging client has been disposed already"
            );

        bool pushDidThrow = false;

        try
        {
            client -> pushBlock(
                uuids::create()                                 /* targetPeerId */,
                data::DataBlock::createInstance( 128U )
                );
        }
        catch( UnexpectedException& )
        {
            pushDidThrow = true;

            UTF_REQUIRE( ! MessagingUtils::isRetryableMessagingBrokerError( std::current_exception() ) );
        }

        UTF_REQUIRE( pushDidThrow );
    }

    /*
     * dispose() is idempotent and the accessors keep returning the same safe defaults
     */

    UTF_REQUIRE_NO_THROW( client -> dispose() );

    UTF_REQUIRE( ! client -> isConnected() );

    UTF_REQUIRE_EQUAL( client -> channelId(), uuids::nil() );

    UTF_REQUIRE( ! client -> isNoCopyDataBlocks() );
}

UTF_AUTO_TEST_CASE( IO_MessagingClientObjectDispatchLocalTests )
{
    using namespace bl;
    using namespace bl::messaging;

    std::atomic< std::size_t > callsCount( 0U );

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    const auto targetPeerIdExpected = uuids::create();

    const auto brokerProtocolExpected = utest::TestMessagingUtils::createBrokerProtocolMessage(
        MessageType::AsyncRpcDispatch,
        uuids::create()                     /* conversationId */,
        "<test cookies>"                    /* cookiesText */
        );

    const auto payloadExpected = bl::dm::DataModelUtils::loadFromFile< Payload >(
        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
        );

    const auto incomingObjectChannel = bl::om::lockDisposable(
        MessagingClientObjectDispatchFromCallback::createInstance(
            [ & ](
                SAA_in              const bl::uuid_t&                               targetPeerId,
                SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                ) -> void
            {
                UTF_REQUIRE_EQUAL( targetPeerIdExpected, targetPeerId );
                ++callsCount;

                UTF_REQUIRE( payload );

                utest::DataModelTestUtils::requireObjectsEqual( brokerProtocol, brokerProtocolExpected );
                utest::DataModelTestUtils::requireObjectsEqual( payload, payloadExpected );
            }
            )
        );

    {
        const auto client = bl::om::lockDisposable(
            MessagingClientObjectFactory::createFromObjectDispatchLocal< MessagingClientBlockDispatchLocal >(
                om::qi< MessagingClientObjectDispatch >( incomingObjectChannel ),
                om::copy( dataBlocksPool )
                )
            );

        const auto& objectDispatcher = client -> outgoingObjectChannel();

        const auto& blockDispatcher = client -> outgoingBlockChannel();

        const std::size_t noOfBlocks = 1024U;

        for( std::size_t i = 0U; i < noOfBlocks; ++i )
        {
            objectDispatcher -> pushMessage( targetPeerIdExpected, brokerProtocolExpected, payloadExpected );
        }

        om::qi< MessagingClientBlockDispatchLocal >( blockDispatcher ) -> flush();

        UTF_REQUIRE_EQUAL( callsCount.load(), noOfBlocks );
    }
}

UTF_AUTO_TEST_CASE( IO_MessagingClientObjectDispatchTcpDispatcherTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto callbackTests = []() -> void
    {
        std::atomic< std::size_t > callsCount( 0U );

        const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

        const auto targetPeerIdExpected = uuids::create();
        const auto sourcePeerId = uuids::create();
        const auto conversationId = uuids::create();
        const auto messageId = uuids::create();

        /*
         * Obtain fresh cookies and create a valid broker protocol message
         */

        const auto cookiesText = utest::TestMessagingUtils::getTokenData();

        const auto brokerProtocolToSend = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            conversationId,
            cookiesText,
            messageId
            );

        const auto brokerProtocolExpected = utest::TestMessagingUtils::createBrokerProtocolMessage(
            MessageType::AsyncRpcDispatch,
            conversationId,
            "<fake cookies text>",
            messageId
            );

        const auto principal = utest::DummyAuthorizationCache::getTestSecurityPrincipal();

        BrokerBackendTask::authorizeProtocolMessage( brokerProtocolExpected, principal );

        brokerProtocolExpected -> sourcePeerId( uuids::uuid2string( sourcePeerId ) );
        brokerProtocolExpected -> targetPeerId( uuids::uuid2string( targetPeerIdExpected ) );

        const auto payloadExpected = bl::dm::DataModelUtils::loadFromFile< Payload >(
            utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
            );

        const auto incomingObjectChannel1 = bl::om::lockDisposable(
            MessagingClientObjectDispatchFromCallback::createInstance(
                [ & ](
                    SAA_in              const bl::uuid_t&                               targetPeerId,
                    SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                    SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                    ) -> void
                {
                    BL_UNUSED( targetPeerId );
                    BL_UNUSED( brokerProtocol );
                    BL_UNUSED( payload );

                    UTF_FAIL( "This one should not be called" );
                }
                )
            );

        const auto incomingObjectChannel2 = bl::om::lockDisposable(
            MessagingClientObjectDispatchFromCallback::createInstance(
                [ & ](
                    SAA_in              const bl::uuid_t&                               targetPeerId,
                    SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                    SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                    ) -> void
                {
                    UTF_REQUIRE_EQUAL( targetPeerIdExpected, targetPeerId );
                    UTF_REQUIRE_EQUAL( uuids::uuid2string( targetPeerId ), brokerProtocol -> targetPeerId() );
                    UTF_REQUIRE_EQUAL( uuids::uuid2string( sourcePeerId ), brokerProtocol -> sourcePeerId() );

                    ++callsCount;

                    UTF_REQUIRE( payload );

                    utest::DataModelTestUtils::requireObjectsEqual( brokerProtocol, brokerProtocolExpected );
                    utest::DataModelTestUtils::requireObjectsEqual( payload, payloadExpected );
                }
                )
            );

        {
            auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                "localhost"                                             /* host */,
                test::UtfArgsParser::port()                             /* inboundPort */,
                test::UtfArgsParser::port() + 1                         /* outboundPort */
                );

            auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                "localhost"                                             /* host */,
                test::UtfArgsParser::port()                             /* inboundPort */,
                test::UtfArgsParser::port() + 1                         /* outboundPort */
                );

            const auto client1 = bl::om::lockDisposable(
                MessagingClientObjectFactory::createFromObjectDispatchTcp(
                    om::qi< MessagingClientObjectDispatch >( incomingObjectChannel1 ),
                    dataBlocksPool,
                    sourcePeerId,
                    "localhost"                                         /* host */,
                    test::UtfArgsParser::port()                         /* inboundPort */,
                    test::UtfArgsParser::port() + 1                     /* outboundPort */,
                    std::move( connections1.first )                     /* inboundConnection */,
                    std::move( connections1.second )                    /* outboundConnection */
                    )
                );

            const auto client2 = bl::om::lockDisposable(
                MessagingClientObjectFactory::createFromObjectDispatchTcp(
                    om::qi< MessagingClientObjectDispatch >( incomingObjectChannel2 ),
                    dataBlocksPool,
                    targetPeerIdExpected,
                    "localhost"                                         /* host */,
                    test::UtfArgsParser::port()                         /* inboundPort */,
                    test::UtfArgsParser::port() + 1                     /* outboundPort */,
                    std::move( connections2.first )                     /* inboundConnection */,
                    std::move( connections2.second )                    /* outboundConnection */
                    )
                );

            const auto& objectDispatcher = client1 -> outgoingObjectChannel();

            const std::size_t noOfBlocks = 1024U;

            /*
             * Test the completion callback interface which allows us to wait for message
             * delivery and do proper error handling (as necessary)
             *
             * 1) First send messages in batches of less than half the queue size (
             *    utils_t::sender_connection_t::BLOCK_QUEUE_SIZE / 2) which should always
             *    succeed
             *
             * 2) Send a quick storm of messages and expect many of these to fail with
             *    BrokerErrorCodes::TargetPeerQueueFull error code
             *
             * 3) Send invalid peer id and expect BrokerErrorCodes::TargetPeerNotFound
             *
             * 4) Send a message which is expected to be authenticated, but don't provide an
             *    authentication token (expect BrokerErrorCodes::AuthorizationFailed)
             *
             * 5) Send invalid message and expect BrokerErrorCodes::ProtocolValidationFailed
             *
             * 6) Send noOfBlocks messages in async mode and wait for all of them to finish
             */

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    typedef utest::TestMessagingUtils utils_t;

                    eq -> setOptions( ExecutionQueue::OptionKeepFailed );

                    /*
                     * Step #1 from the comment above...
                     */

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Step #1: testing sync execution of "
                            << noOfBlocks
                            << " calls"
                        );

                    eq -> setThrottleLimit( utils_t::sender_connection_t::BLOCK_QUEUE_SIZE / 2U );

                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                    {
                        eq -> push_back(
                            ExternalCompletionTaskImpl::createInstance< Task >(
                                cpp::bind(
                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                        objectDispatcher.get()
                                        ),
                                    targetPeerIdExpected,
                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocolToSend ),
                                    om::ObjPtrCopyable< Payload >( payloadExpected ),
                                    _1 /* onReady - the completion callback */
                                    )
                                )
                            );
                    }

                    utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eq );

                    /*
                     * Step #2 from the comment above...
                     */

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Step #2: testing sync execution of "
                            << noOfBlocks
                            << " calls without throttle (many should fail)"
                        );

                    eq -> setThrottleLimit( 0U /* no throttle limit */ );

                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                    {
                        eq -> push_back(
                            ExternalCompletionTaskImpl::createInstance< Task >(
                                cpp::bind(
                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                        objectDispatcher.get()
                                        ),
                                    targetPeerIdExpected,
                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocolToSend ),
                                    om::ObjPtrCopyable< Payload >( payloadExpected ),
                                    _1 /* onReady - the completion callback */
                                    )
                                )
                            );
                    }

                    std::size_t noOfFailedCalls = 0U;

                    BL_SCOPE_EXIT(
                        {
                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Number of failed calls is "
                                    << noOfFailedCalls
                                );
                        }
                        );

                    while( ! eq -> isEmpty() )
                    {
                        const auto task = eq -> pop();

                        /*
                         * Because of the inherent race condition with the isEmpty() check
                         * returning 'false' right before it becomes 'true' we need to check
                         * for the case where task is nullptr
                         */

                        if( ! task )
                        {
                            continue;
                        }

                        UTF_REQUIRE( task -> isFailed() );

                        UTF_REQUIRE_THROW_ERROR_CODE(
                            cpp::safeRethrowException( task -> exception() ),
                            ServerErrorException,
                            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull )
                            );

                        ++noOfFailedCalls;
                    }

                    UTF_REQUIRE( noOfFailedCalls );

                    /*
                     * Step #3 from the comment above...
                     *
                     * Sending a message to a non-existing target (targetPeerId is random uuid)
                     */

                    eq -> forceFlushNoThrow();

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Step #3: testing sync execution of a message which is expected to fail with"
                            << " BrokerErrorCodes::TargetPeerNotFound"
                        );

                    eq -> push_back(
                        ExternalCompletionTaskImpl::createInstance< Task >(
                            cpp::bind(
                                &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                    objectDispatcher.get()
                                    ),
                                uuids::create() /* random non-existing targetPeerId */,
                                om::ObjPtrCopyable< BrokerProtocol >( brokerProtocolToSend ),
                                om::ObjPtrCopyable< Payload >( payloadExpected ),
                                _1 /* onReady - the completion callback */
                                )
                            )
                        );

                    noOfFailedCalls = 0U;

                    UTF_REQUIRE_THROW_ERROR_CODE(
                        eq -> flush(),
                        ServerErrorException,
                        eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
                        );

                    ++noOfFailedCalls;

                    UTF_REQUIRE( noOfFailedCalls );

                    /*
                     * Step #4 from the comment above...
                     */

                    eq -> forceFlushNoThrow();

                    const auto brokerProtocolNoCookies = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        conversationId,
                        "dummyCookieName=unauthorized",
                        messageId
                        );

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Step #4: testing sync execution of a message which is expected to fail with"
                            << " BrokerErrorCodes::AuthorizationFailed"
                        );

                    eq -> push_back(
                        ExternalCompletionTaskImpl::createInstance< Task >(
                            cpp::bind(
                                &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                    objectDispatcher.get()
                                    ),
                                targetPeerIdExpected,
                                om::ObjPtrCopyable< BrokerProtocol >( brokerProtocolNoCookies ),
                                om::ObjPtrCopyable< Payload >( payloadExpected ),
                                _1 /* onReady - the completion callback */
                                )
                            )
                        );

                    noOfFailedCalls = 0U;

                    try
                    {
                        eq -> flush();

                        UTF_FAIL( "This must throw" );
                    }
                    catch( ServerErrorException& e )
                    {
                        const auto* ec = e.errorCode();

                        UTF_REQUIRE(
                            ec && eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed ) == *ec
                            );

                        ++noOfFailedCalls;
                    }

                    UTF_REQUIRE( noOfFailedCalls );

                    /*
                     * Step #5 from the comment above...
                     */

                    eq -> forceFlushNoThrow();

                    const auto brokerProtocolInvalid = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        conversationId,
                        cookiesText,
                        messageId
                        );

                    brokerProtocolInvalid -> messageId( "<invalid uuid>" );

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Step #5: testing sync execution of a message which is expected to fail with"
                            << " BrokerErrorCodes::ProtocolValidationFailed"
                        );

                    eq -> push_back(
                        ExternalCompletionTaskImpl::createInstance< Task >(
                            cpp::bind(
                                &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                    objectDispatcher.get()
                                    ),
                                targetPeerIdExpected,
                                om::ObjPtrCopyable< BrokerProtocol >( brokerProtocolInvalid ),
                                om::ObjPtrCopyable< Payload >( payloadExpected ),
                                _1 /* onReady - the completion callback */
                                )
                            )
                        );

                    noOfFailedCalls = 0U;

                    try
                    {
                        eq -> flush();

                        UTF_FAIL( "This must throw" );
                    }
                    catch( ServerErrorException& e )
                    {
                        const auto* ec = e.errorCode();

                        UTF_REQUIRE(
                            ec && eh::errc::make_error_code( BrokerErrorCodes::ProtocolValidationFailed ) == *ec
                            );

                        ++noOfFailedCalls;
                    }

                    UTF_REQUIRE( noOfFailedCalls );
                }
                );

            /*
             * Step #6 from the comment above...
             */

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Step #6: testing async execution of "
                    << noOfBlocks
                    << " calls"
                );

            /*
             * Note that at this point we need to reset callsCount to satisfy the
             * wait loop below which expects all messages to be delivered and
             * callsCount to be exactly noOfBlocks
             *
             * We will also so a small sleep and check that callsCount is stable
             * as we don't expect any messages to be delivered at this point
             */

            callsCount = 0U;
            os::sleep( time::seconds( 1L ) );
            UTF_REQUIRE( callsCount.load() == 0U );

            for( std::size_t i = 0U; i < noOfBlocks; ++i )
            {
                for( ;; )
                {
                    try
                    {
                        objectDispatcher -> pushMessage(
                            targetPeerIdExpected,
                            brokerProtocolToSend,
                            payloadExpected
                            );

                        break;
                    }
                    catch( ServerErrorException& e )
                    {
                        const auto* ec = e.errorCode();

                        if( ec && eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) == *ec )
                        {
                            os::sleep( time::milliseconds( 100 ) );

                            continue;
                        }

                        throw;
                    }
                }
            }

            std::size_t retries = 0U;
            const std::size_t retryCount = 180U;

            for( ;; )
            {
                if( retries >= retryCount )
                {
                    UTF_FAIL( "Messages were not delivered within 180 seconds" );
                }

                if( callsCount.load() == noOfBlocks )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "All messages were delivered approximately within less than "
                            << retries
                            << " seconds"
                        );

                    break;
                }

                os::sleep( time::seconds( 1L ) );
                ++retries;
            }
        }
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

namespace utest
{
    namespace dm
    {
        /*
         * TestAsyncRequest
         */

        BL_DM_DEFINE_CLASS_BEGIN( TestAsyncRequest )

            BL_DM_DECLARE_STRING_PROPERTY               ( inputPath )
            BL_DM_DECLARE_BOOL_PROPERTY                 ( shouldStart )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( inputPath )
                BL_DM_IMPL_PROPERTY( shouldStart )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( TestAsyncRequest )

        BL_DM_DEFINE_PROPERTY( TestAsyncRequest, inputPath )
        BL_DM_DEFINE_PROPERTY( TestAsyncRequest, shouldStart )

        /*
         * TestAsyncResponse
         */

        BL_DM_DEFINE_CLASS_BEGIN( TestAsyncResponse )

            BL_DM_DECLARE_STRING_PROPERTY               ( finalPath )
            BL_DM_DECLARE_BOOL_PROPERTY                 ( hasStarted )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( finalPath )
                BL_DM_IMPL_PROPERTY( hasStarted )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( TestAsyncResponse )

        BL_DM_DEFINE_PROPERTY( TestAsyncResponse, finalPath )
        BL_DM_DEFINE_PROPERTY( TestAsyncResponse, hasStarted )

    } // dm

} // utest

namespace
{
    class TestConversationProcessing : public bl::messaging::ConversationProcessingBaseImpl<>
    {
        BL_DECLARE_OBJECT_IMPL( TestConversationProcessing )

    protected:

        typedef bl::messaging::ConversationProcessingBaseImpl<>                 base_type;

        typedef base_type::payload_t                                            payload_t;
        typedef base_type::object_dispatch_t                                    object_dispatch_t;

        typedef bl::messaging::BrokerProtocol                                   BrokerProtocol;
        typedef bl::messaging::Payload                                          Payload;

        bl::cpp::ScalarTypeIniter< bool >                                       m_isSender;
        bl::cpp::ScalarTypeIniter< bool >                                       m_useRequestResponseProcessingWrappers;

        bl::cpp::ScalarTypeIniter< int >                                        m_ticks;

        /*
         * The request failure injection hook - when it is set processRequestImpl() calls
         * it before it does anything else, so the exception it throws travels through
         * defaultProcessRequest()'s error handling, over the broker and back to the sender
         *
         * It defaults to empty, so every other construction site is unaffected
         */

        bl::cpp::function< void () >                                            m_requestFailure;

        TestConversationProcessing(
            SAA_in          const bool                                          isSender,
            SAA_in          const bl::uuid_t&                                   peerId,
            SAA_in          const bl::uuid_t&                                   targetPeerId,
            SAA_in          const bl::uuid_t&                                   conversationId,
            SAA_in          bl::om::ObjPtr< object_dispatch_t >&&               objectDispatcher,
            SAA_in_opt      std::string&&                                       authenticationCookies,
            SAA_in_opt      MessageInfo&&                                       seedMessage = MessageInfo(),
            SAA_in_opt      const bool                                          useRequestResponseProcessingWrappers = false,
            SAA_in_opt      bl::cpp::function< void () >&&                      requestFailure =
                bl::cpp::function< void () >()
            )
            :
            base_type(
                peerId,
                targetPeerId,
                conversationId,
                BL_PARAM_FWD( objectDispatcher ),
                BL_PARAM_FWD( authenticationCookies ),
                BL_PARAM_FWD( seedMessage )
                ),
            m_isSender( isSender ),
            m_useRequestResponseProcessingWrappers( useRequestResponseProcessingWrappers ),
            m_requestFailure( BL_PARAM_FWD( requestFailure ) )
        {
        }

        virtual auto processRequestImpl( SAA_in const bl::om::ObjPtr< base_type::request_t >& request )
            -> bl::om::ObjPtr< base_type::response_t > OVERRIDE
        {
            if( m_requestFailure )
            {
                m_requestFailure();
            }

            return base_type::processRequestImpl( request );
        }

        virtual void processCurrentMessage() OVERRIDE
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace bl::messaging;

            /*
             * The seed message will be sent automatically, so the sender is expected to
             * only receive a response message
             *
             * The receiver is expected to receive a request message then process it and
             * then send a response
             */

            if( m_isSender )
            {
                if( m_ticks )
                {
                    if( m_ticks == 3 )
                    {
                        BL_LOG(
                            bl::Logging::debug(),
                            "Sender finished processing response"
                            );

                        if( m_useRequestResponseProcessingWrappers )
                        {
                            base_type::defaultProcessResponse( "TestConversationProcessing" );
                        }
                        else
                        {
                            m_isFinished = true;
                        }
                    }
                    else
                    {
                        BL_LOG(
                            bl::Logging::debug(),
                            "Sender is processing response..."
                            );
                    }
                }
                else
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        "Sender received response for processing"
                        );
                }

                ++m_ticks.lvalue();
            }
            else
            {
                if( m_ticks )
                {
                    if( m_ticks == 3 )
                    {
                        BL_LOG(
                            bl::Logging::debug(),
                            "Receiver finished processing request"
                            );

                        if( m_useRequestResponseProcessingWrappers )
                        {
                            base_type::defaultProcessRequest( "TestConversationProcessing" );
                        }
                        else
                        {
                            const auto brokerProtocol =
                                MessagingUtils::createResponseProtocolMessage( m_conversationId );

                            const auto payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                                utest::TestUtils::resolveDataFilePath( "async_rpc_response.json" )
                                );

                            base_type::sendMessage( true /* isLastMessage */, brokerProtocol, payload );

                            m_currentMessage = MessageInfo();
                        }
                    }
                    else
                    {
                        BL_LOG(
                            bl::Logging::debug(),
                            BL_MSG()
                                << "Receiver is processing request..."
                            );
                    }
                }
                else
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "Receiver received request for processing"
                        );
                }

                ++m_ticks.lvalue();
            }
        }

    public:

        /*
         * The raw server error document exactly as it arrived on the wire, before
         * getAsyncRpcResponseOrThrowIfError() turns it back into an exception - without it
         * the test cannot tell 'the transport dropped the exception type' apart from 'the
         * deserializer dropped it'
         */

        auto getRawServerErrorJson() const -> bl::om::ObjPtr< bl::dm::ServerErrorJson >
        {
            bl::om::ObjPtr< bl::dm::ServerErrorJson > result;

            if( m_currentMessage.payload && m_currentMessage.payload -> asyncRpcResponse() )
            {
                const auto& serverErrorJson =
                    m_currentMessage.payload -> asyncRpcResponse() -> serverErrorJson();

                if( serverErrorJson )
                {
                    result = bl::om::copy( serverErrorJson );
                }
            }

            return result;
        }

        auto getResponse() const -> bl::om::ObjPtr< utest::dm::TestAsyncResponse >
        {
            auto response = bl::dm::DataModelUtils::castTo< utest::dm::TestAsyncResponse >(
                base_type::getAsyncRpcResponseOrThrowIfError()
                );

            BL_CHK(
                false,
                ! response -> finalPath().empty() && response -> hasStarted(),
                BL_MSG()
                    << "The message returned by the remote host does not contain valid response:\n"
                    << bl::dm::DataModelUtils::getDocAsPrettyJsonString( response )
                );

            return bl::om::copy( response );
        }
    };

    typedef bl::om::ObjectImpl< TestConversationProcessing > TestConversationProcessingImpl;

    /*
     * Tester class for the timeouts...
     */

    class TestConversationProcessingTimeouts : public TestConversationProcessing
    {
        BL_DECLARE_OBJECT_IMPL( TestConversationProcessingTimeouts )

    protected:

        typedef TestConversationProcessing                                      base_type;
        typedef base_type::object_dispatch_t                                    object_dispatch_t;

        typedef bl::messaging::BrokerProtocol                                   BrokerProtocol;
        typedef bl::messaging::Payload                                          Payload;

        bl::cpp::ScalarTypeIniter< bool >                                       m_emulateAckTimeout;
        bl::cpp::ScalarTypeIniter< bool >                                       m_emulateMsgTimeout;

        TestConversationProcessingTimeouts(
            SAA_in          const bool                                          isSender,
            SAA_in          const bl::uuid_t&                                   peerId,
            SAA_in          const bl::uuid_t&                                   targetPeerId,
            SAA_in          const bl::uuid_t&                                   conversationId,
            SAA_in          bl::om::ObjPtr< object_dispatch_t >&&               objectDispatcher,
            SAA_in_opt      std::string&&                                       authenticationCookies,
            SAA_in_opt      MessageInfo&&                                       seedMessage = MessageInfo()
            )
            :
            base_type(
                isSender,
                peerId,
                targetPeerId,
                conversationId,
                BL_PARAM_FWD( objectDispatcher ),
                BL_PARAM_FWD( authenticationCookies ),
                BL_PARAM_FWD( seedMessage )
                )
        {
        }

    public:

        void emulateAckTimeout( SAA_in const bool emulateAckTimeout ) NOEXCEPT
        {
            m_emulateAckTimeout = emulateAckTimeout;
        }

        void emulateMsgTimeout( SAA_in const bool emulateMsgTimeout ) NOEXCEPT
        {
            m_emulateMsgTimeout = emulateMsgTimeout;
        }

        virtual void processCurrentMessage() OVERRIDE
        {
            if( ! base_type::m_isSender && m_emulateMsgTimeout )
            {
                BL_ASSERT( ! m_emulateAckTimeout );

                /*
                 * Just swallow the messages and don't respond to anything
                 */

                m_currentMessage = MessageInfo();

                m_isFinished = true;

                return;
            }

            base_type::processCurrentMessage();
        }

        void onProcessing()
        {
            if( ! base_type::m_isSender && m_emulateAckTimeout )
            {
                BL_ASSERT( ! m_emulateMsgTimeout );

                m_isFinished = true;

                return;
            }

            base_type::onProcessing();
        }

        void pushMessage(
            SAA_in              const bl::uuid_t&                                   targetPeerId,
            SAA_in              const bl::om::ObjPtr< BrokerProtocol >&             brokerProtocol,
            SAA_in_opt          const bl::om::ObjPtr< Payload >&                    payload
            )
        {
            if( m_emulateAckTimeout )
            {
                /*
                 * Just swallow the messages and don't respond with acknowledgment messages
                 */

                return;
            }

            base_type::pushMessage( targetPeerId, brokerProtocol, payload );
        }
    };

    typedef bl::om::ObjectImpl< TestConversationProcessingTimeouts > TestConversationProcessingTimeoutsImpl;

    /*
     * A test object dispatcher which records the messages pushed for sending; each send
     * is completed asynchronously when the test calls completePendingSend() (completing
     * a send from within the push itself is not allowed by the external completion task
     * contract); the first 'm_failNextSends' pushes are failed with a retryable broker
     * error
     */

    class TestRecordingObjectDispatch : public bl::messaging::MessagingClientObjectDispatch
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE_DISPOSABLE(
            TestRecordingObjectDispatch,
            bl::messaging::MessagingClientObjectDispatch
            )

    public:

        typedef bl::messaging::BrokerProtocol                                   BrokerProtocol;
        typedef bl::messaging::Payload                                          Payload;

        std::vector< bl::om::ObjPtr< BrokerProtocol > >                         m_sent;
        bl::cpp::ScalarTypeIniter< std::size_t >                                m_pushCount;
        bl::cpp::ScalarTypeIniter< std::size_t >                                m_failNextSends;

    protected:

        bl::os::mutex                                                           m_dispatchLock;
        bl::tasks::CompletionCallback                                           m_pendingCompletion;
        std::exception_ptr                                                      m_pendingError;

        TestRecordingObjectDispatch()
        {
        }

    public:

        virtual void dispose() NOEXCEPT OVERRIDE
        {
        }

        virtual void pushMessage(
            SAA_in                  const bl::uuid_t&                           targetPeerId,
            SAA_in                  const bl::om::ObjPtr< BrokerProtocol >&     brokerProtocol,
            SAA_in_opt              const bl::om::ObjPtr< Payload >&            payload,
            SAA_in_opt              bl::tasks::CompletionCallback&&             completionCallback =
                bl::tasks::CompletionCallback()
            ) OVERRIDE
        {
            BL_UNUSED( targetPeerId );
            BL_UNUSED( payload );

            BL_MUTEX_GUARD( m_dispatchLock );

            UTF_REQUIRE( completionCallback );
            UTF_REQUIRE( ! m_pendingCompletion );

            ++m_pushCount.lvalue();

            if( m_failNextSends )
            {
                --m_failNextSends.lvalue();

                m_pendingError = BL_MAKE_EXCEPTION_PTR(
                    bl::ServerErrorException()
                        << bl::eh::errinfo_error_code(
                            bl::eh::errc::make_error_code( bl::messaging::BrokerErrorCodes::TargetPeerQueueFull )
                            ),
                    "Simulated retryable send failure"
                    );
            }
            else
            {
                m_pendingError = nullptr;

                m_sent.push_back( bl::om::copy( brokerProtocol ) );
            }

            m_pendingCompletion = BL_PARAM_FWD( completionCallback );
        }

        virtual bool isConnected() const NOEXCEPT OVERRIDE
        {
            return true;
        }

        bool completePendingSend()
        {
            bl::tasks::CompletionCallback completionCallback;
            std::exception_ptr eptr;

            {
                BL_MUTEX_GUARD( m_dispatchLock );

                if( ! m_pendingCompletion )
                {
                    return false;
                }

                completionCallback.swap( m_pendingCompletion );
                eptr = m_pendingError;
                m_pendingError = nullptr;
            }

            completionCallback( eptr );

            return true;
        }
    };

    typedef bl::om::ObjectImpl< TestRecordingObjectDispatch > TestRecordingObjectDispatchImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingOutboundQueueTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    /*
     * Deterministic test for the outbound message queue of the conversation processing
     * state machine (no broker involved): a message which was requested to be sent, but
     * was not yet picked up for sending, must not be lost when another message arrives
     * and gets acknowledged in the meantime; acknowledgments are sent ahead of the other
     * messages and a retried send doesn't disturb the order
     */

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            const auto peerId = uuids::create();
            const auto remotePeerId = uuids::create();
            const auto conversationId = uuids::create();

            const auto cookiesText = utest::TestMessagingUtils::getTokenData();

            const auto requestPayload = AsyncRpcPolicyDefault::castToBasePayload(
                bl::dm::DataModelUtils::loadFromFile< payload_t >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    )
                );

            const auto cbCreateRequest = [ & ]( SAA_in const uuid_t& messageId ) -> om::ObjPtr< BrokerProtocol >
            {
                auto brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    conversationId,
                    cookiesText,
                    messageId
                    );

                brokerProtocol -> sourcePeerId( uuids::uuid2string( remotePeerId ) );

                /*
                 * Request messages must be authenticated (normally the broker stamps the principal)
                 */

                brokerProtocol -> principalIdentityInfo() -> securityPrincipal(
                    dm::messaging::SecurityPrincipal::createInstance()
                    );

                return brokerProtocol;
            };

            const auto cbIsAck = []( SAA_in const om::ObjPtr< BrokerProtocol >& brokerProtocol ) -> bool
            {
                return MessageType::AsyncRpcAcknowledgment == MessageType::toEnum( brokerProtocol -> messageType() );
            };

            const auto cbCreateProcessor = [ & ]( SAA_in const om::ObjPtr< TestRecordingObjectDispatchImpl >& dispatcher )
                -> om::ObjPtr< TestConversationProcessingImpl >
            {
                return TestConversationProcessingImpl::createInstance(
                    false /* isSender */,
                    peerId,
                    remotePeerId,
                    conversationId,
                    om::qi< MessagingClientObjectDispatch >( dispatcher ),
                    std::string() /* authenticationCookies */
                    );
            };

            /*
             * Runs the sends which are pending (retrying the ones which fail with a
             * retryable error) and returns the number of sends which succeeded
             */

            const auto cbRunPendingSends = [ & ](
                SAA_in          const om::ObjPtr< TestConversationProcessingImpl >&     processor,
                SAA_in          const om::ObjPtr< TestRecordingObjectDispatchImpl >&    dispatcher
                )
                -> std::size_t
            {
                std::size_t count = 0U;

                for( ;; )
                {
                    const auto task = processor -> tryPopProcessingTask();

                    if( ! task )
                    {
                        break;
                    }

                    eq -> push_back( task );

                    /*
                     * The send is completed once the task has been scheduled and has pushed
                     * the message into the dispatcher
                     */

                    while( ! dispatcher -> completePendingSend() )
                    {
                        os::sleep( time::milliseconds( 10 ) );
                    }

                    eq -> wait( task );

                    if( task -> isFailed() )
                    {
                        UTF_REQUIRE( processor -> retryProcessingTask( task -> exception() ) );

                        continue;
                    }

                    ++count;
                }

                return count;
            };

            {
                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                const auto requestMessageId = uuids::create();

                /*
                 * The request arrives and gets acknowledged
                 */

                processor -> pushMessage( peerId, cbCreateRequest( requestMessageId ), requestPayload );

                UTF_REQUIRE( 1U == cbRunPendingSends( processor, dispatcher ) );
                UTF_REQUIRE( 1U == dispatcher -> m_sent.size() );
                UTF_REQUIRE( cbIsAck( dispatcher -> m_sent[ 0 ] ) );
                UTF_REQUIRE_EQUAL( uuids::uuid2string( requestMessageId ), dispatcher -> m_sent[ 0 ] -> messageId() );

                /*
                 * The request is processed (the test processor needs 4 ticks) and the response
                 * is requested to be sent, but it is not picked up for sending yet
                 */

                for( std::size_t i = 0U; i < 4U; ++i )
                {
                    processor -> onProcessing();
                }

                UTF_REQUIRE( 1U == dispatcher -> m_sent.size() );

                /*
                 * A duplicate delivery of the request arrives before the response is picked
                 * up for sending (this used to overwrite and lose the response); the first
                 * send is failed with a retryable error to verify that the retry doesn't
                 * disturb the order
                 */

                processor -> pushMessage( peerId, cbCreateRequest( requestMessageId ), requestPayload );

                dispatcher -> m_failNextSends = 1U;

                UTF_REQUIRE( 2U == cbRunPendingSends( processor, dispatcher ) );
                UTF_REQUIRE( 4U == dispatcher -> m_pushCount );
                UTF_REQUIRE( 3U == dispatcher -> m_sent.size() );

                /*
                 * The acknowledgment of the duplicate goes ahead of the response
                 */

                UTF_REQUIRE( cbIsAck( dispatcher -> m_sent[ 1 ] ) );
                UTF_REQUIRE_EQUAL( uuids::uuid2string( requestMessageId ), dispatcher -> m_sent[ 1 ] -> messageId() );

                UTF_REQUIRE( ! cbIsAck( dispatcher -> m_sent[ 2 ] ) );
                UTF_REQUIRE_EQUAL( uuids::uuid2string( conversationId ), dispatcher -> m_sent[ 2 ] -> conversationId() );

                /*
                 * The duplicate itself is still rejected by the state machine (the acknowledgment
                 * for the response is expected, but a different message was received)
                 */

                UTF_CHECK_THROW( processor -> onProcessing(), SystemException );
            }

            {
                /*
                 * The outbound queue is bounded: with the response queued and not yet picked up,
                 * acknowledging more inbound messages than the queue can hold must fail with
                 * TargetPeerQueueFull for the outbound queue
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload );

                UTF_REQUIRE( 1U == cbRunPendingSends( processor, dispatcher ) );

                for( std::size_t i = 0U; i < 4U; ++i )
                {
                    processor -> onProcessing();
                }

                try
                {
                    for( std::size_t i = 0U; i < 40U; ++i )
                    {
                        processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload );
                    }

                    UTF_FAIL( "pushMessage must throw when the outbound queue is full" );
                }
                catch( SystemException& e )
                {
                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );
                    UTF_REQUIRE( nullptr != ec );
                    UTF_CHECK( eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) == *ec );

                    const auto* message = eh::get_error_info< eh::errinfo_message >( e );
                    UTF_REQUIRE( nullptr != message );
                    UTF_CHECK( std::string::npos != message -> find( "outbound queue" ) );
                }
            }

            {
                /*
                 * The retry budget: MAX_MESSAGE_DELIVERY_ATTEMPTS is 5, so a permanently
                 * failing (but retryable) send must be accepted for retry exactly 4 times
                 * and then refused, i.e. 5 delivery attempts in total
                 *
                 * An infinite budget would turn a permanently unreachable peer into a task
                 * which never completes
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload );

                dispatcher -> m_failNextSends = 10U;

                std::size_t retriesAccepted = 0U;

                /*
                 * A manual drive loop is required here - cbRunPendingSends asserts that
                 * every retry is accepted, which is exactly what is under test
                 */

                for( ;; )
                {
                    const auto task = processor -> tryPopProcessingTask();

                    UTF_REQUIRE( task );

                    eq -> push_back( task );

                    while( ! dispatcher -> completePendingSend() )
                    {
                        os::sleep( time::milliseconds( 10 ) );
                    }

                    eq -> wait( task );

                    UTF_REQUIRE( task -> isFailed() );

                    if( ! processor -> retryProcessingTask( task -> exception() ) )
                    {
                        break;
                    }

                    ++retriesAccepted;
                }

                UTF_REQUIRE_EQUAL( 4U, retriesAccepted );
                UTF_REQUIRE_EQUAL( 5U, dispatcher -> m_pushCount.value() );
                UTF_REQUIRE( dispatcher -> m_sent.empty() );

                /*
                 * Once the budget is spent nothing new may be manufactured behind the
                 * caller's back
                 */

                UTF_REQUIRE( ! processor -> tryPopProcessingTask() );
            }

            {
                /*
                 * A non-retryable verdict must be refused regardless of the remaining
                 * budget, and it must not leave a retry task behind
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload );

                UTF_REQUIRE( 1U == cbRunPendingSends( processor, dispatcher ) );

                const auto permanentEptr = BL_MAKE_EXCEPTION_PTR(
                    ServerErrorException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed )
                            ),
                    "Simulated permanent failure"
                    );

                UTF_REQUIRE( ! processor -> retryProcessingTask( permanentEptr ) );
                UTF_REQUIRE( ! processor -> tryPopProcessingTask() );

                const auto unexpectedEptr = BL_MAKE_EXCEPTION_PTR( UnexpectedException(), "boom" );

                UTF_REQUIRE( ! processor -> retryProcessingTask( unexpectedEptr ) );
                UTF_REQUIRE( ! processor -> tryPopProcessingTask() );
            }

            {
                /*
                 * The no-retry-message guard: a processor which has never popped a message
                 * has no retry message, so even a retryable error must be refused - without
                 * the guard createProcessingTask() would run with a null broker protocol
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                const auto retryableEptr = BL_MAKE_EXCEPTION_PTR(
                    ServerErrorException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull )
                            ),
                    "Simulated retryable failure"
                    );

                UTF_REQUIRE( MessagingUtils::isRetryableMessagingBrokerError( retryableEptr ) );

                UTF_REQUIRE( ! processor -> retryProcessingTask( retryableEptr ) );
            }

            {
                /*
                 * The pushMessage() validation guards, and the ordering of the inbound
                 * queue-full check relative to the acknowledgment
                 *
                 * The inbound cap is the only backpressure a conversation has against a
                 * flooding peer, and 'reject before acknowledging' is what stops the peer
                 * from being told that a dropped message was accepted
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                /*
                 * The target peer id must be this conversation's own peer id
                 */

                UTF_REQUIRE_THROW_MESSAGE(
                    processor -> pushMessage(
                        uuids::create() /* wrong targetPeerId */,
                        cbCreateRequest( uuids::create() ),
                        requestPayload
                        ),
                    bl::UnexpectedException,
                    "does not match the expected peer id"
                    );

                UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );

                /*
                 * A non-acknowledgment message must carry a source peer id, and it must be
                 * the remote peer this conversation is bound to; neither rejection may be
                 * acknowledged
                 */

                {
                    const auto brokerProtocol = cbCreateRequest( uuids::create() );

                    brokerProtocol -> sourcePeerId( bl::str::empty() );

                    UTF_REQUIRE_THROW_MESSAGE(
                        processor -> pushMessage( peerId, brokerProtocol, requestPayload ),
                        bl::UnexpectedException,
                        "Invalid source peer id"
                        );

                    UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );
                }

                {
                    const auto brokerProtocol = cbCreateRequest( uuids::create() );

                    brokerProtocol -> sourcePeerId( uuids::uuid2string( uuids::create() ) );

                    UTF_REQUIRE_THROW_MESSAGE(
                        processor -> pushMessage( peerId, brokerProtocol, requestPayload ),
                        bl::UnexpectedException,
                        "does not match the expected peer id"
                        );

                    UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );
                }

                /*
                 * Acknowledgment messages skip the source peer checks entirely - they must,
                 * as createAcknowledgmentMessage() does not set a source peer id at all
                 */

                UTF_REQUIRE_NO_THROW(
                    processor -> pushMessage(
                        peerId,
                        MessagingUtils::createAcknowledgmentMessage( conversationId, uuids::create() ),
                        nullptr /* payload */
                        )
                    );

                /*
                 * An inbound acknowledgment produces no outbound message of its own
                 */

                UTF_REQUIRE( 0U == cbRunPendingSends( processor, dispatcher ) );
                UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );
            }

            {
                /*
                 * The inbound ring buffer holds BLOCK_QUEUE_SIZE (32) messages; the
                 * outbound deque is drained after every push so it cannot be the queue
                 * which fills first - the existing block above asserts that other one
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();
                const auto processor = cbCreateProcessor( dispatcher );

                for( std::size_t i = 0U; i < 32U; ++i )
                {
                    processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload );

                    UTF_REQUIRE( 1U == cbRunPendingSends( processor, dispatcher ) );
                }

                const auto sentBefore = dispatcher -> m_sent.size();
                const auto pushedBefore = dispatcher -> m_pushCount.value();

                UTF_REQUIRE_EQUAL( 32U, sentBefore );

                const auto cbIsInboundQueueFull = []( SAA_in const SystemException& e ) -> bool
                {
                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

                    if( ! ec || eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) != *ec )
                    {
                        return false;
                    }

                    const auto* message = eh::get_error_info< eh::errinfo_message >( e );

                    return message && std::string::npos != message -> find( "can't receive messages" );
                };

                UTF_REQUIRE_EXCEPTION(
                    processor -> pushMessage( peerId, cbCreateRequest( uuids::create() ), requestPayload ),
                    SystemException,
                    cbIsInboundQueueFull
                    );

                /*
                 * The load bearing assertion for the ordering - the rejected message must
                 * not have been acknowledged
                 */

                UTF_REQUIRE_EQUAL( sentBefore, dispatcher -> m_sent.size() );
                UTF_REQUIRE_EQUAL( pushedBefore, dispatcher -> m_pushCount.value() );
            }
        }
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingConversationTaskCancelTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    typedef TestConversationProcessingImpl::MessageInfo MessageInfo;

    typedef om::ObjectImpl
    <
        ConversationProcessingTaskT< TestConversationProcessingImpl >
    >
    processing_task_t;

    /*
     * A cancelled conversation task must end promptly with asio::error::operation_aborted
     * and without any retries - without the m_stopWasRequested arm it would keep
     * alternating the processing and the timer tasks until m_msgTimeout expires, which is
     * five minutes by default
     */

    const auto cbIsOperationAborted = []( SAA_in const SystemException& e ) -> bool
    {
        const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

        return nullptr != ec && asio::error::operation_aborted == *ec;
    };

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            const auto peerId = uuids::create();
            const auto remotePeerId = uuids::create();

            const auto cbCreateProcessor = [ & ](
                SAA_in          const om::ObjPtr< TestRecordingObjectDispatchImpl >&    dispatcher,
                SAA_in          MessageInfo&&                                           seedMessage
                )
                -> om::ObjPtr< TestConversationProcessingImpl >
            {
                return TestConversationProcessingImpl::createInstance(
                    false /* isSender */,
                    peerId,
                    remotePeerId,
                    uuids::create() /* conversationId */,
                    om::qi< MessagingClientObjectDispatch >( dispatcher ),
                    std::string() /* authenticationCookies */,
                    BL_PARAM_FWD( seedMessage )
                    );
            };

            {
                /*
                 * A receiver with nothing to do simply alternates the processing task and
                 * the timer task; the cancel must break out of that immediately
                 */

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();

                const auto task = processing_task_t::createInstance< Task >(
                    cbCreateProcessor( dispatcher, MessageInfo() )
                    );

                const auto t0 = time::microsec_clock::universal_time();

                eq -> push_back( task );

                /*
                 * Let the state machine alternate at least once - the timer task's initial
                 * delay is timeout(), i.e. 1000 ms by default
                 */

                os::sleep( time::milliseconds( 200 ) );

                task -> requestCancel();

                eq -> wait( task );

                const auto elapsed = time::microsec_clock::universal_time() - t0;

                UTF_REQUIRE( task -> isFailed() );

                /*
                 * The predicate holds for both arms of the m_stopWasRequested branch - the
                 * already failed one and the force throw one - so the assertion does not
                 * depend on which of the two wins the race
                 */

                UTF_REQUIRE_EXCEPTION(
                    cpp::safeRethrowException( task -> exception() ),
                    SystemException,
                    cbIsOperationAborted
                    );

                /*
                 * Comfortably below the 30 s default ackTimeout and far below the 5 min
                 * msgTimeout - this is the assertion which fails when the cancel is ignored
                 */

                UTF_REQUIRE( elapsed < time::seconds( 30 ) );

                UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );
            }

            {
                /*
                 * The same, but with a seed message the conversation would otherwise have
                 * delivered - a cancelled conversation must not send it
                 *
                 * Note the cancel is requested before the task is scheduled: the outbound
                 * send is an ExternalCompletionTask created without a cancel callback, so
                 * once it has been scheduled it can only be completed by the dispatcher and
                 * cancelling it at that point would not end the task at all. Requesting the
                 * cancel up front makes the m_stopWasRequested arm deterministic - the very
                 * first processing task is aborted, so the seed message is never even
                 * picked up for sending
                 */

                const auto cookiesText = utest::TestMessagingUtils::getTokenData();

                const auto dispatcher = TestRecordingObjectDispatchImpl::createInstance();

                MessageInfo seedMessage;

                seedMessage.brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    uuids::create()             /* conversationId */,
                    cookiesText
                    );

                seedMessage.payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                const auto task = processing_task_t::createInstance< Task >(
                    cbCreateProcessor( dispatcher, std::move( seedMessage ) )
                    );

                const auto t0 = time::microsec_clock::universal_time();

                task -> requestCancel();

                eq -> push_back( task );

                eq -> wait( task );

                const auto elapsed = time::microsec_clock::universal_time() - t0;

                UTF_REQUIRE( task -> isFailed() );

                UTF_REQUIRE_EXCEPTION(
                    cpp::safeRethrowException( task -> exception() ),
                    SystemException,
                    cbIsOperationAborted
                    );

                UTF_REQUIRE( elapsed < time::seconds( 30 ) );

                UTF_REQUIRE( dispatcher -> m_sent.empty() );
                UTF_REQUIRE_EQUAL( 0U, dispatcher -> m_pushCount.value() );
            }
        }
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    const auto callbackTests = []() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

                const auto targetPeerId1 = uuids::create();
                const auto targetPeerId2 = uuids::create();

                const auto cookiesText = utest::TestMessagingUtils::getTokenData();

                const om::ObjPtrCopyable< om::Proxy > client1Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

                const auto incomingObjectChannel1 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client1Sink,
                            targetPeerId1 /* targetPeerIdExpected */,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                const om::ObjPtrCopyable< om::Proxy > client2Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto incomingObjectChannel2 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client2Sink,
                            targetPeerId2 /* targetPeerIdExpected */,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                const auto client1 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel1 ),
                        dataBlocksPool,
                        targetPeerId1,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections1.first )                     /* inboundConnection */,
                        std::move( connections1.second )                    /* outboundConnection */
                        )
                    );

                const auto client2 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel2 ),
                        dataBlocksPool,
                        targetPeerId2,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections2.first )                     /* inboundConnection */,
                        std::move( connections2.second )                    /* outboundConnection */
                        )
                    );

                typedef TestConversationProcessingImpl::MessageInfo MessageInfo;

                /*
                 * Create and place an initial seed message to be passed by the sender
                 */

                const auto conversationId = uuids::create();

                MessageInfo seedMessage;

                seedMessage.brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    conversationId,
                    cookiesText
                    );

                seedMessage.payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                /*
                 * First test the baseline / success scenario... (i.e. a real request /
                 * response conversation)
                 *
                 * task1 will be the logical sender / initiator task and task2 will be the
                 * logical receiver / processing task
                 */

                typedef om::ObjectImpl
                <
                    ConversationProcessingTaskT< TestConversationProcessingImpl >
                >
                processing_task_t;

                auto processor1 = TestConversationProcessingImpl::createInstance(
                    true /* isSender */,
                    targetPeerId1                                           /* peerId (self) */,
                    targetPeerId2                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client1 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    cpp::copy( cookiesText )                                /* authenticationCookies */,
                    cpp::copy( seedMessage )
                    );

                auto processor2 = TestConversationProcessingImpl::createInstance(
                    false /* isSender */,
                    targetPeerId2                                           /* peerId (self) */,
                    targetPeerId1                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client2 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    ""                                                      /* authenticationCookies */
                    );

                const auto task1 = processing_task_t::createInstance< Task >( bl::om::copy( processor1 ) );
                const auto task2 = processing_task_t::createInstance< Task >( std::move( processor2 ) );

                BL_SCOPE_EXIT(
                    {
                        client1Sink -> disconnect();
                        client2Sink -> disconnect();
                    }
                    );

                client1Sink -> connect( task1.get() );
                client2Sink -> connect( task2.get() );

                eq -> push_back( task1 );
                eq -> push_back( task2 );

                /*
                 * Now post an initial message to task1 and wait for the conversation
                 * to finish with the exchange of back and forth messages
                 */

                eq -> wait( task2 );
                eq -> waitForSuccess( task1 );

                const auto response = processor1 -> getResponse();

                UTF_REQUIRE( response );
                UTF_CHECK_EQUAL( response -> hasStarted(), true );

                const auto payload = bl::dm::DataModelUtils::loadFromFile< AsyncRpcPayload >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_response.json" )
                    );

                const auto expectedResponse =
                    bl::dm::DataModelUtils::castTo< utest::dm::TestAsyncResponse >(
                        payload -> asyncRpcResponse()
                        );

                UTF_CHECK_EQUAL( response -> finalPath(), expectedResponse -> finalPath() );

                dispatchAssertions -> requireNone();
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingTestWrappers )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    const auto callbackTests = []() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

                const auto targetPeerId1 = uuids::create();
                const auto targetPeerId2 = uuids::create();

                const auto cookiesText = utest::TestMessagingUtils::getTokenData();

                const om::ObjPtrCopyable< om::Proxy > client1Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

                const auto incomingObjectChannel1 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client1Sink,
                            targetPeerId1,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                const om::ObjPtrCopyable< om::Proxy > client2Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto incomingObjectChannel2 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client2Sink,
                            targetPeerId2,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                const auto client1 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel1 ),
                        dataBlocksPool,
                        targetPeerId1,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections1.first )                     /* inboundConnection */,
                        std::move( connections1.second )                    /* outboundConnection */
                        )
                    );

                const auto client2 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel2 ),
                        dataBlocksPool,
                        targetPeerId2,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections2.first )                     /* inboundConnection */,
                        std::move( connections2.second )                    /* outboundConnection */
                        )
                    );

                typedef TestConversationProcessingImpl::MessageInfo MessageInfo;

                /*
                 * The async RPC error transport round trip: when a request processor throws,
                 * defaultProcessRequest() serializes the exception into
                 * AsyncRpcResponse::serverErrorJson, the document crosses a real socket via
                 * the broker, and getAsyncRpcResponseOrThrowIfError() reconstructs and
                 * rethrows it on the sender side
                 *
                 * This is the only production round trip of the createServerErrorObject /
                 * createExceptionFromObject pair outside HTTP and the only one which crosses
                 * a socket, so every sub-case asserts both the raw document which arrived on
                 * the wire and the exception which was reconstructed from it - without the
                 * former the test cannot tell 'the transport dropped the type' apart from
                 * 'the deserializer dropped the type'
                 *
                 * Note that defaultProcessRequest() logs the injected failure at warning
                 * level through utils::tryCatchLog and the UTF harness turns warnings into
                 * test errors, hence the line logger below
                 */

                bl::Logging::LineLoggerPusher pushLineLogger( &utest::warningToDebugLineLogger );

                typedef om::ObjectImpl
                <
                    ConversationProcessingTaskT< TestConversationProcessingImpl >
                >
                processing_task_t;

                /*
                 * Runs one complete request / response conversation over the broker with the
                 * given failure injected into the receiver's processRequestImpl() and returns
                 * the sender's processor, which then holds the response message exactly as it
                 * arrived
                 */

                const auto cbRunConversation = [ & ]( SAA_in bl::cpp::function< void () >&& requestFailure )
                    -> om::ObjPtr< TestConversationProcessingImpl >
                {
                    const auto conversationId = uuids::create();

                    MessageInfo seedMessage;

                    seedMessage.brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                        MessageType::AsyncRpcDispatch,
                        conversationId,
                        cookiesText
                        );

                    seedMessage.payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                        utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                        );

                    auto processor1 = TestConversationProcessingImpl::createInstance(
                        true /* isSender */,
                        targetPeerId1                                           /* peerId (self) */,
                        targetPeerId2                                           /* targetPeerId (the target) */,
                        conversationId,
                        om::copy( client1 -> outgoingObjectChannel() )          /* objectDispatcher */,
                        cpp::copy( cookiesText )                                /* authenticationCookies */,
                        std::move( seedMessage ),
                        true                                                    /* useProcessRequestWrapper */
                        );

                    auto processor2 = TestConversationProcessingImpl::createInstance(
                        false /* isSender */,
                        targetPeerId2                                           /* peerId (self) */,
                        targetPeerId1                                           /* targetPeerId (the target) */,
                        conversationId,
                        om::copy( client2 -> outgoingObjectChannel() )          /* objectDispatcher */,
                        ""                                                      /* authenticationCookies */,
                        MessageInfo()                                           /* seedMessage */,
                        true                                                    /* useProcessRequestWrapper */,
                        BL_PARAM_FWD( requestFailure )
                        );

                    const auto task1 = processing_task_t::createInstance< Task >( bl::om::copy( processor1 ) );
                    const auto task2 = processing_task_t::createInstance< Task >( std::move( processor2 ) );

                    client1Sink -> connect( task1.get() );
                    client2Sink -> connect( task2.get() );

                    BL_SCOPE_EXIT(
                        {
                            client1Sink -> disconnect();
                            client2Sink -> disconnect();
                        }
                        );

                    eq -> push_back( task1 );
                    eq -> push_back( task2 );

                    /*
                     * Now post an initial message to task1 and wait for the conversation
                     * to finish with the exchange of back and forth messages
                     */

                    eq -> wait( task2 );
                    eq -> waitForSuccess( task1 );

                    return processor1;
                };

                const auto cbRequireServerErrorJson = [](
                    SAA_in          const om::ObjPtr< TestConversationProcessingImpl >&     processor,
                    SAA_in          const std::string&                                     exceptionTypeExpected
                    )
                    -> void
                {
                    const auto serverErrorJson = processor -> getRawServerErrorJson();

                    UTF_REQUIRE( serverErrorJson );
                    UTF_REQUIRE( serverErrorJson -> result() );

                    UTF_REQUIRE_EQUAL(
                        exceptionTypeExpected,
                        serverErrorJson -> result() -> exceptionType()
                        );
                };

                {
                    /*
                     * (a) no failure is injected, so processRequestImpl() itself throws
                     * because it was not overridden
                     */

                    const auto processor1 = cbRunConversation( bl::cpp::function< void () >() );

                    cbRequireServerErrorJson(
                        processor1,
                        bl::UnexpectedException::fullTypeNameStatic()
                        );

                    const auto cbIsNotOverridden = []( SAA_in const bl::UnexpectedException& e ) -> bool
                    {
                        return bl::cpp::contains( std::string( e.what() ), "has to be overridden" );
                    };

                    UTF_REQUIRE_EXCEPTION(
                        processor1 -> getResponse(),
                        bl::UnexpectedException,
                        cbIsNotOverridden
                        );
                }

                {
                    /*
                     * (b) a discriminating exception type - this is the assertion which
                     * fails the moment the type discriminator stops surviving the wire
                     */

                    const auto processor1 = cbRunConversation(
                        []() -> void
                        {
                            BL_THROW( bl::TimeoutException(), "async-rpc marker: timeout" );
                        }
                        );

                    cbRequireServerErrorJson( processor1, bl::TimeoutException::fullTypeNameStatic() );

                    const auto cbIsTimeout = []( SAA_in const bl::TimeoutException& e ) -> bool
                    {
                        return
                            std::string( "bl::TimeoutException" ) == std::string( e.fullTypeName() ) &&
                            std::string( "async-rpc marker: timeout" ) == std::string( e.what() );
                    };

                    UTF_REQUIRE_EXCEPTION( processor1 -> getResponse(), bl::TimeoutException, cbIsTimeout );
                }

                {
                    /*
                     * (c) a coded system error, which takes the SystemException special case
                     * of the dispatch chain
                     *
                     * Note that the marker text deliberately contains no ': ' separator -
                     * when it rebuilds a SystemException createExceptionFromObject() splits
                     * the serialized message at the first ': ' to recover the original
                     * what() prefix (ServerErrorHelpers.h:378-405), so a marker carrying one
                     * would be truncated on the way back
                     */

                    const auto processor1 = cbRunConversation(
                        []() -> void
                        {
                            BL_THROW_EC(
                                bl::eh::errc::make_error_code( bl::eh::errc::no_such_file_or_directory ),
                                "async-rpc marker enoent"
                                );
                        }
                        );

                    cbRequireServerErrorJson( processor1, bl::SystemException::fullTypeNameStatic() );

                    const auto cbIsEnoent = []( SAA_in const bl::SystemException& e ) -> bool
                    {
                        const auto ecExpected =
                            bl::eh::errc::make_error_code( bl::eh::errc::no_such_file_or_directory );

                        const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

                        if( nullptr == ec || ecExpected != *ec || bl::eh::generic_category() != ec -> category() )
                        {
                            return false;
                        }

                        const auto* errNo = e.errNo();

                        if( nullptr == errNo || ecExpected.value() != *errNo )
                        {
                            return false;
                        }

                        /*
                         * A substring match, because system_error::what() gains a Boost
                         * version dependent error code suffix
                         */

                        return bl::cpp::contains( std::string( e.what() ), "async-rpc marker enoent" );
                    };

                    UTF_REQUIRE_EXCEPTION( processor1 -> getResponse(), bl::SystemException, cbIsEnoent );
                }

                {
                    /*
                     * (d) an exception type the dispatch chain does not know about - the
                     * fallback preserves the message but loses the type, which is the
                     * observable symptom of the unmapped type fallback crossing a real wire
                     */

                    const auto processor1 = cbRunConversation(
                        []() -> void
                        {
                            BL_THROW( bl::NotFoundException(), "async-rpc marker: notfound" );
                        }
                        );

                    cbRequireServerErrorJson( processor1, bl::NotFoundException::fullTypeNameStatic() );

                    const auto cbIsNotFoundMessage = []( SAA_in const bl::UnexpectedException& e ) -> bool
                    {
                        return std::string( "async-rpc marker: notfound" ) == std::string( e.what() );
                    };

                    UTF_REQUIRE_EXCEPTION(
                        processor1 -> getResponse(),
                        bl::UnexpectedException,
                        cbIsNotFoundMessage
                        );
                }

                dispatchAssertions -> requireNone();
                }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingTestAckTimeout )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    const auto callbackTests = []() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

                const auto targetPeerId1 = uuids::create();
                const auto targetPeerId2 = uuids::create();

                const auto cookiesText = utest::TestMessagingUtils::getTokenData();

                const om::ObjPtrCopyable< om::Proxy > client1Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

                const auto incomingObjectChannel1 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client1Sink,
                            targetPeerId1,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                const om::ObjPtrCopyable< om::Proxy > client2Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto incomingObjectChannel2 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client2Sink,
                            targetPeerId2,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                const auto client1 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel1 ),
                        dataBlocksPool,
                        targetPeerId1,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections1.first )                     /* inboundConnection */,
                        std::move( connections1.second )                    /* outboundConnection */
                        )
                    );

                const auto client2 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel2 ),
                        dataBlocksPool,
                        targetPeerId2,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections2.first )                     /* inboundConnection */,
                        std::move( connections2.second )                    /* outboundConnection */
                        )
                    );

                typedef TestConversationProcessingImpl::MessageInfo MessageInfo;

                /*
                 * Create and place an initial seed message to be passed by the sender
                 */

                const auto conversationId = uuids::create();

                MessageInfo seedMessage;

                seedMessage.brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    conversationId,
                    cookiesText
                    );

                seedMessage.payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                /*
                 * Now let's test the timeout scenarios...
                 *
                 * timeoutTask1 will be the logical sender / initiator task and timeoutTask2 will
                 * be the logical receiver / processing task
                 */

                typedef om::ObjectImpl
                <
                    ConversationProcessingTaskT< TestConversationProcessingTimeoutsImpl >
                >
                processing_task_t;

                /*
                 * Set the execution queue to not keep the tasks, so we can flush it
                 * safely even when some of the tasks fail
                 */

                eq -> setOptions( ExecutionQueue::OptionKeepNone );

                auto processor1 = TestConversationProcessingTimeoutsImpl::createInstance(
                    true /* isSender */,
                    targetPeerId1                                           /* peerId (self) */,
                    targetPeerId2                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client1 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    cpp::copy( cookiesText )                                /* authenticationCookies */,
                    cpp::copy( seedMessage )
                    );

                auto processor2 = TestConversationProcessingTimeoutsImpl::createInstance(
                    false /* isSender */,
                    targetPeerId2                                           /* peerId (self) */,
                    targetPeerId1                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client2 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    ""                                                      /* authenticationCookies */
                    );

                /*
                 * For the timeout we are going to emulate we set it to some small value
                 * (smallTimeoutInSeconds) and then we set all other timeouts for 5x this
                 * value to ensure they are normally never hit (unless there is a bug)
                 *
                 * We don't want to leave the defaults for ackTimeout() and msgTimeout()
                 * because they are too big, but 5 x smallTimeoutInSeconds is better
                 * (otherwise in the case of an error it would take too long for the test
                 * to complete)
                 */

                const auto smallTimeoutInSeconds = 3L;

                processor1 -> ackTimeout( time::seconds( smallTimeoutInSeconds ) );
                processor1 -> msgTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );

                processor2 -> ackTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );
                processor2 -> msgTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );

                processor2 -> emulateAckTimeout( true );

                const auto task1 = processing_task_t::createInstance< Task >( std::move( processor1 ) );
                const auto task2 = processing_task_t::createInstance< Task >( std::move( processor2 ) );

                BL_SCOPE_EXIT(
                    {
                        client1Sink -> disconnect();
                        client2Sink -> disconnect();
                    }
                    );

                client1Sink -> connect( task1.get() );
                client2Sink -> connect( task2.get() );

                eq -> push_back( task1 );
                eq -> push_back( task2 );

                eq -> wait( task1 );
                eq -> wait( task2 );

                UTF_REQUIRE( task1 -> isFailed() );
                UTF_REQUIRE( ! task2 -> isFailed() );

                UTF_REQUIRE_THROW_MESSAGE(
                    cpp::safeRethrowException( task1 -> exception() ),
                    TimeoutException,
                    "Messaging client did not receive acknowledgment within the specified interval"
                    );

                dispatchAssertions -> requireNone();
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingTestMsgTimeout )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

    const auto callbackTests = []() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

                const auto targetPeerId1 = uuids::create();
                const auto targetPeerId2 = uuids::create();

                const auto cookiesText = utest::TestMessagingUtils::getTokenData();

                const om::ObjPtrCopyable< om::Proxy > client1Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto dispatchAssertions = std::make_shared< utest::DeferredAssertions >();

                const auto incomingObjectChannel1 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client1Sink,
                            targetPeerId1,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                const om::ObjPtrCopyable< om::Proxy > client2Sink =
                    om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

                const auto incomingObjectChannel2 = bl::om::lockDisposable(
                    MessagingClientObjectDispatchFromCallback::createInstance(
                        cpp::bind(
                            &utest::TestMessagingUtils::dispatchCallback,
                            client2Sink,
                            targetPeerId2,
                            dispatchAssertions,
                            _1,
                            _2,
                            _3
                            )
                        )
                    );

                auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                    "localhost"                                             /* host */,
                    test::UtfArgsParser::port()                             /* inboundPort */,
                    test::UtfArgsParser::port() + 1                         /* outboundPort */
                    );

                const auto client1 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel1 ),
                        dataBlocksPool,
                        targetPeerId1,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections1.first )                     /* inboundConnection */,
                        std::move( connections1.second )                    /* outboundConnection */
                        )
                    );

                const auto client2 = bl::om::lockDisposable(
                    MessagingClientObjectFactory::createFromObjectDispatchTcp(
                        om::qi< MessagingClientObjectDispatch >( incomingObjectChannel2 ),
                        dataBlocksPool,
                        targetPeerId2,
                        "localhost"                                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections2.first )                     /* inboundConnection */,
                        std::move( connections2.second )                    /* outboundConnection */
                        )
                    );

                typedef TestConversationProcessingImpl::MessageInfo MessageInfo;

                /*
                 * Create and place an initial seed message to be passed by the sender
                 */

                const auto conversationId = uuids::create();

                MessageInfo seedMessage;

                seedMessage.brokerProtocol = utest::TestMessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    conversationId,
                    cookiesText
                    );

                seedMessage.payload = bl::dm::DataModelUtils::loadFromFile< payload_t >(
                    utest::TestUtils::resolveDataFilePath( "async_rpc_request.json" )
                    );

                /*
                 * Now let's test the timeout scenarios...
                 *
                 * timeoutTask1 will be the logical sender / initiator task and timeoutTask2 will
                 * be the logical receiver / processing task
                 */

                typedef om::ObjectImpl
                <
                    ConversationProcessingTaskT< TestConversationProcessingTimeoutsImpl >
                >
                processing_task_t;

                /*
                 * Set the execution queue to not keep the tasks, so we can flush it
                 * safely even when some of the tasks fail
                 */

                eq -> setOptions( ExecutionQueue::OptionKeepNone );

                auto processor1 = TestConversationProcessingTimeoutsImpl::createInstance(
                    true /* isSender */,
                    targetPeerId1                                           /* peerId (self) */,
                    targetPeerId2                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client1 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    cpp::copy( cookiesText )                                /* authenticationCookies */,
                    cpp::copy( seedMessage )
                    );

                auto processor2 = TestConversationProcessingTimeoutsImpl::createInstance(
                    false /* isSender */,
                    targetPeerId2                                           /* peerId (self) */,
                    targetPeerId1                                           /* targetPeerId (the target) */,
                    conversationId,
                    om::copy( client2 -> outgoingObjectChannel() )          /* objectDispatcher */,
                    ""                                                      /* authenticationCookies */
                    );

                /*
                 * For the timeout we are going to emulate we set it to some small value
                 * (smallTimeoutInSeconds) and then we set all other timeouts for 5x this
                 * value to ensure they are normally never hit (unless there is a bug)
                 *
                 * We don't want to leave the defaults for ackTimeout() and msgTimeout()
                 * because they are too big, but 5 x smallTimeoutInSeconds is better
                 * (otherwise in the case of an error it would take too long for the test
                 * to complete)
                 */

                const auto smallTimeoutInSeconds = 3L;

                processor1 -> ackTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );
                processor1 -> msgTimeout( time::seconds( smallTimeoutInSeconds ) );

                processor2 -> ackTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );
                processor2 -> msgTimeout( time::seconds( 5 * smallTimeoutInSeconds ) );

                processor2 -> emulateMsgTimeout( true );

                const auto task1 = processing_task_t::createInstance< Task >( std::move( processor1 ) );
                const auto task2 = processing_task_t::createInstance< Task >( std::move( processor2 ) );

                BL_SCOPE_EXIT(
                    {
                        client1Sink -> disconnect();
                        client2Sink -> disconnect();
                    }
                    );

                client1Sink -> connect( task1.get() );
                client2Sink -> connect( task2.get() );

                eq -> push_back( task1 );
                eq -> push_back( task2 );

                eq -> wait( task1 );
                eq -> wait( task2 );

                UTF_REQUIRE( task1 -> isFailed() );
                UTF_REQUIRE( ! task2 -> isFailed() );

                UTF_REQUIRE_THROW_MESSAGE(
                    cpp::safeRethrowException( task1 -> exception() ),
                    TimeoutException,
                    "Messaging client did not receive response within the specified interval"
                    );

                dispatchAssertions -> requireNone();
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingPerfTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    typedef utest::TestMessagingUtils utils_t;

    std::atomic< int > noOfMessagesInFlight( 0 );
    std::atomic< std::size_t > noOfMessagesDelivered( 0U );

    const auto completionCallback = []( SAA_in_opt const std::exception_ptr& eptr ) NOEXCEPT -> void
    {
        if( eptr )
        {
            /*
             * In our specific test case scenario the broker error codes are expected,
             * so we should filter these out first before we invoke the default handler
             */

            const auto ec = eh::errorCodeFromExceptionPtr( eptr );

            if( BrokerErrorCodes::isExpectedErrorCode( ec ) )
            {
                return;
            }
        }

        utils_t::client_t::completionCallbackDefault( eptr );
    };

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

                --noOfMessagesInFlight;
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

            const auto block =
                MessagingUtils::serializeObjectsToBlock( brokerProtocol, payload, dataBlocksPool );

            const auto messageSize = block -> size();

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Messages size is "
                    << messageSize
                );

            /*
             * Create UtfArgsParser::connections() messaging clients backed by the same
             * async wrapper and then perform perf tests by sending and processing
             * messages
             *
             * Note that these don't own the backend and the queue, so when they get
             * disposed they will not actually dispose the backend, but just tear down
             * the connections
             */

            auto connections = utils_t::createConnections();

            UTF_REQUIRE_EQUAL( connections.size(), test::UtfArgsParser::connections() );
            UTF_REQUIRE( test::UtfArgsParser::connections() > 1 );

            utils_t::clients_list_t clients;

            clients.reserve( test::UtfArgsParser::connections() );

            for( std::size_t i = 0; i < test::UtfArgsParser::connections(); ++i )
            {
                const auto peerId = uuids::create();

                auto blockDispatch = om::lockDisposable(
                    utils_t::client_factory_t::createWithSmartDefaults(
                        om::copy( eq ),
                        peerId,
                        om::copy( backend ),
                        om::copy( asyncWrapper ),
                        test::UtfArgsParser::host()                         /* host */,
                        test::UtfArgsParser::port()                         /* inboundPort */,
                        test::UtfArgsParser::port() + 1                     /* outboundPort */,
                        std::move( connections[ i ].first )                 /* inboundConnection */,
                        std::move( connections[ i ].second )                /* outboundConnection */,
                        om::copy( dataBlocksPool )
                        )
                    );

                {
                    const auto clientImpl = om::qi< utils_t::client_t >( blockDispatch );

                    clientImpl -> completionCallback( cpp::copy( completionCallback ) );
                }

                auto client = om::lockDisposable(
                    MessagingClientObjectImplDefault::createInstance< MessagingClientObject >(
                        om::qi< MessagingClientBlockDispatch >( blockDispatch ),
                        dataBlocksPool
                        )
                    );

                blockDispatch.detachAsObjPtr();

                clients.emplace_back( std::make_pair( peerId, std::move( client ) ) );
            }

            bool cancelRequested = false;

            const auto loadGeneratorTask = SimpleTaskImpl::createInstance< Task >(
                [ & ]() -> void
                {
                    const auto startTime = time::microsec_clock::universal_time();

                    const int maxMessagesInFlight = static_cast< int >(
                        clients.size() * ( utils_t::sender_connection_t::BLOCK_QUEUE_SIZE / 3U )
                        );

                    const double messagesPerSecondDelta = std::min< double >( 0.05 * maxMessagesInFlight, 200.0 );
                    const double elapsedInSecondsDelta = 2.0;

                    double messagesPerSecondLast = 0.0;
                    double elapsedInSecondsLast = 0.0;

                    for( ;; )
                    {
                        if( cancelRequested )
                        {
                            break;
                        }

                        if( noOfMessagesInFlight >= maxMessagesInFlight )
                        {
                            os::sleep( time::milliseconds( 100L )  );
                            continue;
                        }

                        /*
                         * Choose two clients at random and send message between them
                         */

                        const auto pos1 = random::getUniformRandomUnsignedValue< std::size_t >( clients.size() - 1U );
                        const auto pos2 = random::getUniformRandomUnsignedValue< std::size_t >( clients.size() - 1U );

                        if( pos1 == pos2 )
                        {
                            /*
                             * The sender and the receiver can't be the same; try again...
                             */

                            continue;
                        }

                        const auto& client = clients[ pos1 ].second;
                        const auto& targetPeerId = clients[ pos2 ].first;

                        try
                        {
                            client -> outgoingObjectChannel() -> pushMessage(
                                targetPeerId,
                                brokerProtocol,
                                payload
                                );
                        }
                        catch( bl::ServerErrorException& e )
                        {
                            const auto* ec = e.errorCode();

                            if( ec && eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) == *ec )
                            {
                                /*
                                 * The queue of this specific client is full
                                 *
                                 * Let's find another one...
                                 */

                                continue;
                            }

                            throw;
                        }

                        ++noOfMessagesInFlight;

                        const auto duration = time::microsec_clock::universal_time() - startTime;

                        const auto elapsedInSeconds = duration.total_milliseconds() / 1000.0;

                        if( std::abs( elapsedInSeconds - elapsedInSecondsLast ) < elapsedInSecondsDelta )
                        {
                            continue;
                        }

                        elapsedInSecondsLast = elapsedInSeconds;

                        const auto messagesPerSecond = noOfMessagesDelivered / elapsedInSeconds;

                        if( std::abs( messagesPerSecond - messagesPerSecondLast ) > messagesPerSecondDelta )
                        {
                            /*
                             * Only print if there is a meaningful delta relative to last time
                             */

                            const auto kbytesPerSecond =
                                ( noOfMessagesDelivered * messageSize ) / elapsedInSeconds / 1024.0;

                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Messages per second is "
                                    << messagesPerSecond
                                    << "; kilobytes per second is "
                                    << kbytesPerSecond
                                );

                            messagesPerSecondLast = messagesPerSecond;
                        }
                    }
                }
                );

            {
                BL_SCOPE_EXIT(
                    {
                        clientSink -> disconnect();
                    }
                    );

                clientSink -> connect( incomingSink.get() );

                eq -> push_back( loadGeneratorTask );

                {
                    BL_SCOPE_EXIT(
                        {
                            cancelRequested = true;
                            waitForSuccessOrCancel( eq, loadGeneratorTask );
                        }
                        );

                    utils_t::waitForKeyOrTimeout();
                }
            }
        }
        );

    dispatchAssertions -> requireNone();
}

UTF_AUTO_TEST_CASE( RotatingMessagingClientObjectDispatchTests )
{
    using namespace bl;
    using namespace bl::messaging;
    using namespace bl::dm::messaging;

    typedef RotatingMessagingClientObjectDispatch rotating_dispatcher_t;

    const std::size_t objectCount = 10;
    std::vector< std::size_t > usageCount( objectCount );

    const auto createDispatchers =
        [ & ]() -> rotating_dispatcher_t::DispatchList
        {
            rotating_dispatcher_t::DispatchList dispatchers;

            for( size_t n = 0; n < objectCount; ++n )
            {
                dispatchers.emplace_back(
                    MessagingClientObjectDispatchFromCallback::createInstance< MessagingClientObjectDispatch >(
                        [ &usageCount, n ](
                            SAA_in              const bl::uuid_t&                               targetPeerId,
                            SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                            SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                            ) -> void
                            {
                                BL_UNUSED( targetPeerId );
                                BL_UNUSED( brokerProtocol );
                                BL_UNUSED( payload );

                                ++usageCount[ n ];
                            }
                        )
                    );
            }

            return dispatchers;
        };

    /*
     * Check that all dispatchers are evenly used as each pushMesage should select the next one
     */

    auto rotatingDispatcher =
        rotating_dispatcher_t::createInstance< MessagingClientObjectDispatch >( createDispatchers() );

    const auto brokerProtocol = createProtocolMessage();

    for( std::size_t n = 0; n < objectCount * 2; ++n )
    {
        rotatingDispatcher -> pushMessage( uuids::nil() /* targetPeerId */, brokerProtocol, nullptr /* payload */ );
    }

    for( std::size_t n = 0; n < objectCount; ++n )
    {
        UTF_REQUIRE_EQUAL( 2U, usageCount[ n ] );
    }

    /*
     * Check that disposed dispatchers aren't used
     */

    usageCount = std::vector< size_t >( objectCount );

    auto dispatchers = createDispatchers();

    for( std::size_t n = 0; n < objectCount; n += 2 )
    {
        om::qi< MessagingClientObjectDispatchFromCallback >( dispatchers[ n ] ) -> dispose();
    }

    rotatingDispatcher =
        rotating_dispatcher_t::createInstance< MessagingClientObjectDispatch >( std::move( dispatchers ) );

    for( std::size_t n = 0; n < objectCount * 2; ++n )
    {
        rotatingDispatcher -> pushMessage( uuids::nil() /* targetPeerId */, brokerProtocol, nullptr /* payload */ );
    }

    for( std::size_t n = 0; n < objectCount; ++n )
    {
        UTF_REQUIRE_EQUAL( ( n % 2 == 0 ) ? 0U : 4U, usageCount[ n ] );
    }

    /*
     * Check that the initially selected dispatcher is random
     */

    const auto getUsedIndex =
        [ & ]() -> std::size_t
        {
            for( std::size_t n = 0; n < usageCount.size(); ++n )
            {
                if( usageCount[ n ] > 0 )
                {
                    return n;
                }
            }

            UTF_FAIL( "No dispatcher was found");

            return 0;
        };

    std::unordered_set< std::size_t > usedIndices;

    for( std::size_t n = 0; n < 100; ++n )
    {
        usageCount = std::vector< size_t >( objectCount );

        rotatingDispatcher =
            rotating_dispatcher_t::createInstance< MessagingClientObjectDispatch >( createDispatchers() );

        rotatingDispatcher -> pushMessage( uuids::nil() /* targetPeerId */, brokerProtocol, nullptr /* payload */ );

        usedIndices.emplace( getUsedIndex() );
    }

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << usedIndices.size()
            << " out of "
            << objectCount
            << " dispatchers selected randomly after 100 iterations"
        );

    UTF_REQUIRE( usedIndices.size() > 1 );

    /*
     * Check the exhausted arm - when every target reports itself as disconnected the
     * rotating dispatch must throw the very same decorated NotSupportedException which
     * MessagingClientImpl::pushBlock() throws, so the caller's failover logic treats it
     * as transient; losing the error uuid decoration would silently break failover
     */

    {
        usageCount = std::vector< size_t >( objectCount );

        auto disconnectedDispatchers = createDispatchers();

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            om::qi< MessagingClientObjectDispatchFromCallback >( disconnectedDispatchers[ n ] ) -> dispose();
        }

        const auto exhaustedDispatcher =
            rotating_dispatcher_t::createInstance( std::move( disconnectedDispatchers ) );

        UTF_REQUIRE( ! exhaustedDispatcher -> isConnected() );

        UTF_REQUIRE_THROW_MESSAGE(
            exhaustedDispatcher -> pushMessage(
                uuids::nil() /* targetPeerId */,
                brokerProtocol,
                nullptr /* payload */
                ),
            NotSupportedException,
            "Messaging client is not connected to messaging broker"
            );

        try
        {
            exhaustedDispatcher -> pushMessage(
                uuids::nil() /* targetPeerId */,
                brokerProtocol,
                nullptr /* payload */
                );

            UTF_FAIL( "The code above is expected to throw" );
        }
        catch( NotSupportedException& e )
        {
            const auto* uuid = eh::get_error_info< eh::errinfo_error_uuid >( e );

            UTF_REQUIRE( uuid && *uuid == uuiddefs::ErrorUuidNotConnectedToBroker() );

            UTF_REQUIRE( MessagingUtils::isRetryableMessagingBrokerError( std::current_exception() ) );
        }

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            UTF_REQUIRE_EQUAL( 0U, usageCount[ n ] );
        }
    }

    /*
     * Check getNextDispatch() - the only entry point production uses - and the locked
     * dispose(), which must leave the rotating dispatch empty rather than handing out
     * targets it has already disposed
     */

    {
        usageCount = std::vector< size_t >( objectCount );

        auto liveDispatchers = createDispatchers();

        rotating_dispatcher_t::DispatchList capturedTargets;

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            capturedTargets.emplace_back( om::copy( liveDispatchers[ n ] ) );
        }

        const auto liveDispatcher =
            rotating_dispatcher_t::createInstance( std::move( liveDispatchers ) );

        UTF_REQUIRE( liveDispatcher -> isConnected() );

        const auto next1 = liveDispatcher -> getNextDispatch();
        const auto next2 = liveDispatcher -> getNextDispatch();

        UTF_REQUIRE( next1 );
        UTF_REQUIRE( next2 );
        UTF_REQUIRE( next1.get() != next2.get() );

        /*
         * Obtaining the next dispatch must not invoke any of the targets
         */

        std::size_t totalUsage = 0U;

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            totalUsage += usageCount[ n ];
        }

        UTF_REQUIRE_EQUAL( 0U, totalUsage );

        liveDispatcher -> dispose();

        UTF_REQUIRE( ! liveDispatcher -> isConnected() );

        UTF_REQUIRE_THROW(
            liveDispatcher -> pushMessage(
                uuids::nil() /* targetPeerId */,
                brokerProtocol,
                nullptr /* payload */
                ),
            NotSupportedException
            );

        UTF_REQUIRE_THROW( liveDispatcher -> getNextDispatch(), NotSupportedException );

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            UTF_REQUIRE( ! capturedTargets[ n ] -> isConnected() );
        }

        /*
         * dispose() is idempotent
         */

        UTF_REQUIRE_NO_THROW( liveDispatcher -> dispose() );
    }
}

UTF_AUTO_TEST_CASE( RotatingMessagingClientBlockDispatchTests )
{
    using namespace bl;
    using namespace bl::messaging;
    using namespace bl::dm::messaging;

    typedef RotatingMessagingClientBlockDispatch rotating_dispatcher_t;

    const std::size_t objectCount = 10;
    std::vector< std::size_t > usageCount( objectCount );

    const auto createDispatchers =
        [ & ]() -> rotating_dispatcher_t::DispatchList
        {
            rotating_dispatcher_t::DispatchList dispatchers;

            for( size_t n = 0; n < objectCount; ++n )
            {
                dispatchers.emplace_back(
                    MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
                        [ &usageCount, n ](
                            SAA_in              const bl::uuid_t&                               targetPeerId,
                            SAA_in              const bl::om::ObjPtr< bl::data::DataBlock >&    dataBlock
                            ) -> void
                            {
                                BL_UNUSED( targetPeerId );
                                BL_UNUSED( dataBlock );

                                ++usageCount[ n ];
                            }
                        )
                    );
            }

            return dispatchers;
        };

    /*
     * Check that all dispatchers are evenly used as each pushMesage should select the next one
     */

    auto rotatingDispatcher =
        rotating_dispatcher_t::createInstance< MessagingClientBlockDispatch >( createDispatchers() );

    const auto brokerProtocol = createProtocolMessage();

    const auto dataBlock = MessagingUtils::serializeObjectsToBlock( brokerProtocol, nullptr /* payload */ );

    for( std::size_t n = 0; n < objectCount * 2; ++n )
    {
        rotatingDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock );
    }

    for( std::size_t n = 0; n < objectCount; ++n )
    {
        UTF_REQUIRE_EQUAL( 2U, usageCount[ n ] );
    }

    /*
     * Check that disposed dispatchers aren't used
     */

    usageCount = std::vector< size_t >( objectCount );

    auto dispatchers = createDispatchers();

    for( std::size_t n = 0; n < objectCount; n += 2 )
    {
        om::qi< MessagingClientBlockDispatchFromCallback >( dispatchers[ n ] ) -> dispose();
    }

    rotatingDispatcher =
        rotating_dispatcher_t::createInstance< MessagingClientBlockDispatch >( std::move( dispatchers ) );

    for( std::size_t n = 0; n < objectCount * 2; ++n )
    {
        rotatingDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock );
    }

    for( std::size_t n = 0; n < objectCount; ++n )
    {
        UTF_REQUIRE_EQUAL( ( n % 2 == 0 ) ? 0U : 4U, usageCount[ n ] );
    }

    /*
     * Check that the initially selected dispatcher is random
     */

    const auto getUsedIndex =
        [ & ]() -> std::size_t
        {
            for( std::size_t n = 0; n < usageCount.size(); ++n )
            {
                if( usageCount[ n ] > 0 )
                {
                    return n;
                }
            }

            UTF_FAIL( "No dispatcher was found");

            return 0;
        };

    std::unordered_set< std::size_t > usedIndices;

    for( std::size_t n = 0; n < 100; ++n )
    {
        usageCount = std::vector< size_t >( objectCount );

        rotatingDispatcher =
            rotating_dispatcher_t::createInstance< MessagingClientBlockDispatch >( createDispatchers() );

        rotatingDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock );

        usedIndices.emplace( getUsedIndex() );
    }

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << usedIndices.size()
            << " out of "
            << objectCount
            << " dispatchers selected randomly after 100 iterations"
        );

    UTF_REQUIRE( usedIndices.size() > 1 );

    /*
     * Check the exhausted arm - when every target reports itself as disconnected the
     * rotating dispatch must throw the very same decorated NotSupportedException which
     * MessagingClientImpl::pushBlock() throws, so the caller's failover logic treats it
     * as transient; losing the error uuid decoration would silently break failover
     *
     * Note the targets must be MessagingClientBlockDispatchFromCallback objects - the
     * local block dispatch implementation hard codes isConnected() to true even after it
     * was disposed, which would defeat this arm
     */

    {
        usageCount = std::vector< size_t >( objectCount );

        auto disconnectedDispatchers = createDispatchers();

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            om::qi< MessagingClientBlockDispatchFromCallback >( disconnectedDispatchers[ n ] ) -> dispose();
        }

        const auto exhaustedDispatcher =
            rotating_dispatcher_t::createInstance( std::move( disconnectedDispatchers ) );

        UTF_REQUIRE( ! exhaustedDispatcher -> isConnected() );

        UTF_REQUIRE_THROW_MESSAGE(
            exhaustedDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock ),
            NotSupportedException,
            "Messaging client is not connected to messaging broker"
            );

        try
        {
            exhaustedDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock );

            UTF_FAIL( "The code above is expected to throw" );
        }
        catch( NotSupportedException& e )
        {
            const auto* uuid = eh::get_error_info< eh::errinfo_error_uuid >( e );

            UTF_REQUIRE( uuid && *uuid == uuiddefs::ErrorUuidNotConnectedToBroker() );

            UTF_REQUIRE( MessagingUtils::isRetryableMessagingBrokerError( std::current_exception() ) );
        }

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            UTF_REQUIRE_EQUAL( 0U, usageCount[ n ] );
        }
    }

    /*
     * Check getNextDispatch() - the only entry point production uses - and the locked
     * dispose(), which must leave the rotating dispatch empty rather than handing out
     * targets it has already disposed
     */

    {
        usageCount = std::vector< size_t >( objectCount );

        auto liveDispatchers = createDispatchers();

        rotating_dispatcher_t::DispatchList capturedTargets;

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            capturedTargets.emplace_back( om::copy( liveDispatchers[ n ] ) );
        }

        const auto liveDispatcher =
            rotating_dispatcher_t::createInstance( std::move( liveDispatchers ) );

        UTF_REQUIRE( liveDispatcher -> isConnected() );

        const auto next1 = liveDispatcher -> getNextDispatch();
        const auto next2 = liveDispatcher -> getNextDispatch();

        UTF_REQUIRE( next1 );
        UTF_REQUIRE( next2 );
        UTF_REQUIRE( next1.get() != next2.get() );

        /*
         * Obtaining the next dispatch must not invoke any of the targets
         */

        std::size_t totalUsage = 0U;

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            totalUsage += usageCount[ n ];
        }

        UTF_REQUIRE_EQUAL( 0U, totalUsage );

        liveDispatcher -> dispose();

        UTF_REQUIRE( ! liveDispatcher -> isConnected() );

        UTF_REQUIRE_THROW(
            liveDispatcher -> pushBlock( uuids::nil() /* targetPeerId */, dataBlock ),
            NotSupportedException
            );

        UTF_REQUIRE_THROW( liveDispatcher -> getNextDispatch(), NotSupportedException );

        for( std::size_t n = 0; n < objectCount; ++n )
        {
            UTF_REQUIRE( ! capturedTargets[ n ] -> isConnected() );
        }

        /*
         * dispose() is idempotent
         */

        UTF_REQUIRE_NO_THROW( liveDispatcher -> dispose() );
    }
}

UTF_AUTO_TEST_CASE( IO_MessagingClientBackendProcessingTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef BackendProcessing::OperationId                                      OperationId;
    typedef BackendProcessing::CommandId                                        CommandId;

    /*
     * The messaging client backend only accepts ( Put, None ) - everything else must be
     * declined by returning nullptr, which makes the async executor fall back on the
     * async operation state and reject the request with NotSupportedException
     *
     * Widening the filter (e.g. by dropping the command id half of it) would deliver a
     * broker originated Remove / FlushPeerSessions block to the application's message
     * sink as if it were a message
     */

    std::atomic< std::size_t > calls( 0U );

    uuid_t seenPeer = uuids::nil();
    om::ObjPtr< data::DataBlock > seen;

    const auto sink = om::lockDisposable(
        MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
            [ & ](
                SAA_in              const uuid_t&                                   peerId,
                SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                ) -> void
            {
                ++calls;

                seenPeer = peerId;
                seen = om::copy( dataBlock );
            }
            )
        );

    const auto backend = om::lockDisposable(
        MessagingClientFactorySsl::createClientBackendProcessingFromBlockDispatch( om::copy( sink ) )
        );

    const auto sessionId = uuids::create();
    const auto chunkId = uuids::create();
    const auto sourcePeerId = uuids::create();
    const auto targetPeerId = uuids::create();

    const std::size_t dataSize = 512U;

    const auto data = data::DataBlock::createInstance( dataSize );

    for( std::size_t i = 0U; i < dataSize; ++i )
    {
        data -> begin()[ i ] = ( char )( i % 97U );
    }

    data -> setSize( dataSize );

    /*
     * All the operation / command combinations below must be declined, and none of them
     * may throw - returning nullptr is how the backend declines
     */

    const auto cbRequireDeclined = [ & ](
        SAA_in              const OperationId                                       operationId,
        SAA_in              const CommandId                                         commandId
        )
        -> void
    {
        om::ObjPtr< Task > task;

        UTF_REQUIRE_NO_THROW(
            task = backend -> createBackendProcessingTask(
                operationId,
                commandId,
                sessionId,
                chunkId,
                sourcePeerId,
                targetPeerId,
                data
                )
            );

        UTF_REQUIRE( ! task );
    };

    cbRequireDeclined( OperationId::Get,                 CommandId::None );
    cbRequireDeclined( OperationId::Command,             CommandId::Remove );
    cbRequireDeclined( OperationId::Command,             CommandId::FlushPeerSessions );
    cbRequireDeclined( OperationId::Put,                 CommandId::Remove );
    cbRequireDeclined( OperationId::Alloc,               CommandId::None );
    cbRequireDeclined( OperationId::AuthenticateClient,  CommandId::None );

    UTF_REQUIRE_EQUAL( 0U, calls.load() );

    /*
     * The one accepted combination
     */

    const auto task = backend -> createBackendProcessingTask(
        OperationId::Put,
        CommandId::None,
        sessionId,
        chunkId,
        sourcePeerId,
        targetPeerId,
        data
        );

    UTF_REQUIRE( task );

    tasks::scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> push_back( task );
        }
        );

    UTF_REQUIRE_EQUAL( calls.load(), 1U );
    UTF_REQUIRE_EQUAL( seenPeer, targetPeerId );

    /*
     * The block must be forwarded by reference and not copied - the completion of it is
     * handled by the caller downstream, and a defensive copy here would both break that
     * and double the allocation on the client's receive path
     */

    UTF_REQUIRE( seen.get() == data.get() );

    /*
     * The backend owns the target it was created from - this is the mechanism by which
     * createWithSmartDefaults( peerId, target, ... ) releases the caller supplied dispatch
     */

    UTF_REQUIRE( sink -> isConnected() );

    backend -> dispose();

    UTF_REQUIRE( ! sink -> isConnected() );

    UTF_REQUIRE_NO_THROW( backend -> dispose() );
}

UTF_AUTO_TEST_CASE( IO_MessagingDemultiplexingTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto callbackTests = []() -> void
    {
        typedef utest::TestMessagingUtils utils_t;

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
                const auto peerId1 = uuids::create();
                const auto peerId2 = uuids::create();

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
                 * Create UtfArgsParser::connections() messaging clients backed by the same
                 * async wrapper and then perform perf tests by sending and processing
                 * messages
                 *
                 * Note that these don't own the backend and the queue, so when they get
                 * disposed they will not actually dispose the backend, but just tear down
                 * the connections
                 */

                auto connections = utils_t::createConnections();

                UTF_REQUIRE_EQUAL( connections.size(), test::UtfArgsParser::connections() );
                UTF_REQUIRE( test::UtfArgsParser::connections() > 1 );

                utils_t::clients_list_t clients;

                clients.reserve( test::UtfArgsParser::connections() );

                for( std::size_t i = 0; i < test::UtfArgsParser::connections(); ++i )
                {
                    /*
                     * Choose the peer id based on if it is odd  vs even index
                     */

                    const auto& peerId = ( 0 == i % 2 ) ? peerId1 : peerId2;

                    auto blockDispatch = om::lockDisposable(
                        utils_t::client_factory_t::createWithSmartDefaults(
                            om::copy( eq ),
                            peerId,
                            om::copy( backend ),
                            om::copy( asyncWrapper ),
                            test::UtfArgsParser::host()                         /* host */,
                            test::UtfArgsParser::port()                         /* inboundPort */,
                            test::UtfArgsParser::port() + 1                     /* outboundPort */,
                            std::move( connections[ i ].first )                 /* inboundConnection */,
                            std::move( connections[ i ].second )                /* outboundConnection */,
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

                    clients.emplace_back( std::make_pair( peerId, std::move( client ) ) );
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

                            const std::size_t noOfBlocks = 20 * clients.size();

                            for( std::size_t i = 0U; i < noOfBlocks; ++i )
                            {
                                const auto pos1 = i % clients.size();
                                const auto pos2 = ( i + 1 ) % clients.size();

                                const auto& client = clients[ pos1 ].second;
                                const auto& targetPeerId = clients[ pos2 ].first;

                                eqLocal -> push_back(
                                    ExternalCompletionTaskImpl::createInstance< Task >(
                                        cpp::bind(
                                            &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                            om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                client -> outgoingObjectChannel().get()
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
                                    eqLocal -> flush();
                                }
                            }

                            eqLocal -> flush();

                            /*
                             * Verify that all messages were delivered successfully and the
                             * message distribution over the channels was uniform
                             */

                            UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );

                            utils_t::verifyUniformMessageDistribution( clients );
                        }
                        );
                }
            }
            );

        dispatchAssertions -> requireNone();
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

UTF_AUTO_TEST_CASE( IO_MessagingMultiplexingTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto callbackTests = []() -> void
    {
        typedef utest::TestMessagingUtils utils_t;

        const auto executeTests = [](
            SAA_in          const std::size_t                                           noOfConnections,
            SAA_in_opt      const bl::uuid_t                                            peerId
            )
            -> void
        {
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

                    auto connections = utils_t::createNoOfConnections( noOfConnections );
                    UTF_REQUIRE_EQUAL( connections.size(), noOfConnections );

                    utils_t::clients_list_t clients;

                    clients.reserve( noOfConnections );

                    for( std::size_t i = 0; i < noOfConnections; ++i )
                    {
                        const auto clientPeerId = uuids::nil() == peerId ? uuids::create() : peerId;

                        /*
                         * The peer id is either fixed or a unique one is generated for each connection
                         */

                        auto blockDispatch = om::lockDisposable(
                            utils_t::client_factory_t::createWithSmartDefaults(
                                om::copy( eq ),
                                clientPeerId,
                                om::copy( backend ),
                                om::copy( asyncWrapper ),
                                test::UtfArgsParser::host()                         /* host */,
                                test::UtfArgsParser::port()                         /* inboundPort */,
                                test::UtfArgsParser::port() + 1                     /* outboundPort */,
                                std::move( connections[ i ].first )                 /* inboundConnection */,
                                std::move( connections[ i ].second )                /* outboundConnection */,
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

                        clients.emplace_back( std::make_pair( clientPeerId, std::move( client ) ) );
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

                                /*
                                 * Create a number of logical peer ids an register them with the backend
                                 */

                                const std::size_t noOfLogicalPeerIds = 5 * clients.size();

                                std::vector< std::pair< bl::uuid_t, bl::uuid_t > > logicalPeerIds;
                                logicalPeerIds.reserve( noOfLogicalPeerIds );

                                for( std::size_t i = 0; i < noOfLogicalPeerIds; ++i )
                                {
                                    const auto logicalPeerId = uuids::create();

                                    const auto pos = i % clients.size();

                                    const auto& client = clients[ pos ].second;
                                    const auto& physicalTargetPeerId = clients[ pos ].first;

                                    const auto associateMessage = utest::TestMessagingUtils::createBrokerProtocolMessage(
                                        MessageType::BackendAssociateTargetPeerId,
                                        bl::uuids::create() /* conversationId */,
                                        "" /* cookiesText */
                                        );

                                    associateMessage -> sourcePeerId( bl::uuids::uuid2string( physicalTargetPeerId ) );
                                    associateMessage -> targetPeerId( bl::uuids::uuid2string( logicalPeerId ) );

                                    eqLocal -> push_back(
                                        ExternalCompletionTaskImpl::createInstance< Task >(
                                            cpp::bind(
                                                &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                    client -> outgoingObjectChannel().get()
                                                    ),
                                                uuids::create() /* targetPeerId */,
                                                om::ObjPtrCopyable< BrokerProtocol >( associateMessage ),
                                                om::ObjPtrCopyable< Payload >(),
                                                _1 /* onReady - the completion callback */
                                                )
                                            )
                                        );

                                    logicalPeerIds.push_back( std::make_pair( logicalPeerId, physicalTargetPeerId ) );
                                }

                                eqLocal -> flush();

                                /*
                                 * The number of delivered messages is expected to be zero as these
                                 * messages are handled by the broker and never dispatched to the client
                                 */

                                UTF_REQUIRE_EQUAL( noOfMessagesDelivered, 0U );

                                {
                                    /*
                                     * Send a bunch of messages to the logical peer ids and verify that they
                                     * arrived correctly
                                     */

                                    const std::size_t noOfBlocks = 10 * noOfLogicalPeerIds;

                                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                                    {
                                        const auto pos1 = i % clients.size();
                                        const auto pos2 = i % noOfLogicalPeerIds;

                                        const auto& client = clients[ pos1 ].second;
                                        const auto& targetPeerId = logicalPeerIds[ pos2 ].first;

                                        eqLocal -> push_back(
                                            ExternalCompletionTaskImpl::createInstance< Task >(
                                                cpp::bind(
                                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                        client -> outgoingObjectChannel().get()
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
                                            eqLocal -> flush();
                                        }
                                    }

                                    eqLocal -> flush();

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );

                                    utils_t::verifyUniformMessageDistribution( clients );
                                }

                                {
                                    /*
                                     * Send some messages to the physical client peer ids and verify
                                     * that these are also delivered
                                     */

                                    noOfMessagesDelivered = 0;

                                    const std::size_t noOfBlocks = 10 * clients.size();

                                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                                    {
                                        const auto pos1 = i % clients.size();
                                        const auto pos2 = ( i + 1 ) % clients.size();

                                        const auto& client = clients[ pos1 ].second;
                                        const auto& targetPeerId = clients[ pos2 ].first;

                                        eqLocal -> push_back(
                                            ExternalCompletionTaskImpl::createInstance< Task >(
                                                cpp::bind(
                                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                        client -> outgoingObjectChannel().get()
                                                        ),
                                                    targetPeerId,
                                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                                    om::ObjPtrCopyable< Payload >( payload ),
                                                    _1 /* onReady - the completion callback */
                                                    )
                                                )
                                            );
                                    }

                                    eqLocal -> flush();

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );
                                }

                                /*
                                 * Dissociate all logical peer ids
                                 */

                                noOfMessagesDelivered = 0;

                                for( std::size_t i = 0; i < noOfLogicalPeerIds; ++i )
                                {
                                    const auto& logicalPeerId = logicalPeerIds[ i ].first;
                                    const auto& physicalTargetPeerId = logicalPeerIds[ i ].second;

                                    const auto pos = i % clients.size();
                                    const auto& client = clients[ pos ].second;

                                    const auto associateMessage = utest::TestMessagingUtils::createBrokerProtocolMessage(
                                        MessageType::BackendDissociateTargetPeerId,
                                        bl::uuids::create() /* conversationId */,
                                        "" /* cookiesText */
                                        );

                                    associateMessage -> sourcePeerId( bl::uuids::uuid2string( physicalTargetPeerId ) );
                                    associateMessage -> targetPeerId( bl::uuids::uuid2string( logicalPeerId ) );

                                    eqLocal -> push_back(
                                        ExternalCompletionTaskImpl::createInstance< Task >(
                                            cpp::bind(
                                                &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                    client -> outgoingObjectChannel().get()
                                                    ),
                                                uuids::create() /* targetPeerId */,
                                                om::ObjPtrCopyable< BrokerProtocol >( associateMessage ),
                                                om::ObjPtrCopyable< Payload >(),
                                                _1 /* onReady - the completion callback */
                                                )
                                            )
                                        );
                                }

                                eqLocal -> flush();

                                UTF_REQUIRE_EQUAL( noOfMessagesDelivered, 0U );

                                {
                                    /*
                                     * Send a bunch of messages to the logical peer ids and verify that they
                                     * all fail now
                                     */

                                    const std::size_t noOfBlocks = 10 * noOfLogicalPeerIds;

                                    for( std::size_t i = 0U; i < noOfBlocks; ++i )
                                    {
                                        const auto pos1 = i % clients.size();
                                        const auto pos2 = i % noOfLogicalPeerIds;

                                        const auto& client = clients[ pos1 ].second;
                                        const auto& targetPeerId = logicalPeerIds[ pos2 ].first;

                                        eqLocal -> push_back(
                                            ExternalCompletionTaskImpl::createInstance< Task >(
                                                cpp::bind(
                                                    &MessagingClientObjectDispatch::pushMessageCopyCallback,
                                                    om::ObjPtrCopyable< MessagingClientObjectDispatch >::acquireRef(
                                                        client -> outgoingObjectChannel().get()
                                                        ),
                                                    targetPeerId,
                                                    om::ObjPtrCopyable< BrokerProtocol >( brokerProtocol ),
                                                    om::ObjPtrCopyable< Payload >( payload ),
                                                    _1 /* onReady - the completion callback */
                                                    )
                                                )
                                            );
                                    }

                                    eqLocal -> flushNoThrowIfFailed();

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, 0U );

                                    UTF_REQUIRE_EQUAL( eqLocal -> size(), noOfBlocks );

                                    while( ! eqLocal -> isEmpty() )
                                    {
                                        const auto task = eqLocal -> pop();

                                        if( task )
                                        {
                                            UTF_REQUIRE_EQUAL( task -> isFailed(), true );

                                            UTF_REQUIRE_THROW_ERROR_CODE(
                                                cpp::safeRethrowException( task -> exception() ),
                                                ServerErrorException,
                                                eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound )
                                                );
                                        }
                                    }
                                }
                            }
                            );
                    }
                }
                );

            dispatchAssertions -> requireNone();
        };

        executeTests( 1U /* noOfConnections */, uuids::create() /* peerId */ );
        executeTests( 2U /* noOfConnections */, uuids::create() /* peerId */ );
        executeTests( 3U /* noOfConnections */, uuids::create() /* peerId */ );

        /*
         * Execute the tests with peerId=nil() which will generate unique physical peerId
         * for each connection
         */

        executeTests( 3U /* noOfConnections */, uuids::nil() /* peerId */ );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = bl::om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

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

                                utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );

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

                                utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );

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
        };

        const auto executeTests = [ brokerInboundPort ](
            SAA_in          const std::size_t                                           noOfConnections,
            SAA_in_opt      const bl::uuid_t                                            peerId
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
                                            utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );
                                        }
                                    }

                                    utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );
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
                                            utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );
                                        }
                                    }

                                    utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );

                                    /*
                                     * Verify that all messages were delivered successfully and the
                                     * message distribution over the channels was uniform
                                     */

                                    UTF_REQUIRE_EQUAL( noOfMessagesDelivered, noOfBlocks );
                                }
                            }
                            );
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

            executeTests( 1U /* noOfConnections */, peerId );
        }

        {
            const auto peerId = uuids::create();

            executeTests( 21U /* noOfConnections */, peerId );
        }

        /*
         * Execute the tests with peerId=nil() which will generate unique physical peerId
         * for each connection
         */

        executeTests( 24 /* noOfConnections */, uuids::nil() /* peerId */ );

        /*
         * Now test if the client pruning logic works correctly
         */

        UTF_REQUIRE( proxyBackendRef );

        const auto proxyBackend =
            om::qi< ProxyBrokerBackendProcessingFactorySsl::proxy_backend_t >( proxyBackendRef );

        /*
         * The proxy backend does not override isConnected() and therefore inherits the always
         * connected default from BackendProcessingBase - it keeps reporting itself as connected
         * even once SharedStateT::isFullyDisconnected() is true and it has already called
         * m_controlToken -> requestCancel()
         *
         * This pins today's behaviour only; whether the proxy should delegate to its outgoing
         * channel the way the forwarding backend does is a product question for the owners and
         * is deliberately not being "fixed" here
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

UTF_AUTO_TEST_CASE( IO_ConnectionEstablisherHangTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Executing hang test on the following platform "
            << str::quoteString( bl::BuildInfo::platform )
            << " ..."
        );

    const auto callbackTests = [ & ]() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                typedef ProxyBrokerBackendProcessingFactorySsl::connection_establisher_t connection_establisher_t;

                const std::size_t noOfConnections = 256U;

                const std::size_t randomInMiddle =
                    ( 2U * noOfConnections ) / 3U +
                    random::getUniformRandomUnsignedValue< std::size_t >( noOfConnections / 3U - 1U );

                for( std::size_t i = 0U; i < noOfConnections; ++i )
                {
                    if( randomInMiddle == i )
                    {
                        controlToken -> requestCancel();
                    }

                    const auto inboundConnection =
                        connection_establisher_t::template createInstance< Task >(
                            cpp::copy( test::UtfArgsParser::host() ),
                            test::UtfArgsParser::port()                     /* inboundPort */,
                            false                                           /* logExceptions */
                            );

                    const auto outboundConnection =
                        connection_establisher_t::template createInstance< Task >(
                            cpp::copy( test::UtfArgsParser::host() ),
                            static_cast< unsigned short >( test::UtfArgsParser::port() + 1U )   /* outboundPort */,
                            false                                                               /* logExceptions */
                            );

                    eq -> push_back( inboundConnection );
                    eq -> push_back( outboundConnection );
                }

                const auto startTime = time::second_clock::universal_time();

                for( ;; )
                {
                    os::sleep( time::seconds( 2 ) );

                    if( eq -> isEmpty() )
                    {
                        break;
                    }

                    if( test::UtfArgsParser::isClient() )
                    {
                        continue;
                    }

                    const auto elapsed = time::second_clock::universal_time() - startTime;

                    /*
                     * Note: the checks below are to handle an apparently OS issue with connect
                     * operations hanging occasionally on rhel5 & rhel6 only
                     */

                    if( elapsed > time::minutes( 5 ) && bl::BuildInfo::platform == "linux-rhel5" )
                    {
                        eq -> cancelAll( false /* wait */ );
                    }

                    if( elapsed > time::minutes( 15 ) && bl::BuildInfo::platform == "linux-rhel5" )
                    {
                        BL_RIP_MSG( "Connection tasks are hung more than 15 minutes" );
                    }

                    if( elapsed > time::minutes( 5 ) && bl::BuildInfo::platform == "linux-rhel6" )
                    {
                        eq -> cancelAll( false /* wait */ );
                    }

                    if( elapsed > time::minutes( 15 ) && bl::BuildInfo::platform == "linux-rhel6" )
                    {
                        BL_RIP_MSG( "Connection tasks are hung more than 15 minutes" );
                    }
                }
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests,
        om::copy( controlToken )
        );
}

namespace utest
{
    template
    <
        typename STREAM,
        typename ASYNCWRAPPER,
        typename SERVERPOLICY = bl::tasks::TcpServerPolicyDefault
    >
    class TestTcpBlockServerT :
        public bl::tasks::TcpBlockServerT< STREAM, ASYNCWRAPPER, SERVERPOLICY >
    {
        BL_DECLARE_OBJECT_IMPL( TestTcpBlockServerT )

    public:

        typedef bl::tasks::TcpBlockServerT< STREAM, ASYNCWRAPPER, SERVERPOLICY >            base_type;

    protected:

        TestTcpBlockServerT(
            SAA_in              const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&      controlToken,
            SAA_in              const bl::om::ObjPtr< bl::data::datablocks_pool_type >&     dataBlocksPool,
            SAA_in              std::string&&                                               host,
            SAA_in              const unsigned short                                        port,
            SAA_in              const std::string&                                          privateKeyPem,
            SAA_in              const std::string&                                          certificatePem,
            SAA_in              const bl::om::ObjPtr< ASYNCWRAPPER >&                       asyncWrapper,
            SAA_in_opt          const bl::uuid_t&                                           peerId = bl::uuids::nil()
            )
            :
            base_type(
                controlToken,
                dataBlocksPool,
                BL_PARAM_FWD( host ),
                port,
                privateKeyPem,
                certificatePem,
                asyncWrapper,
                peerId
                )
        {
        }

        virtual auto createConnection( SAA_inout typename STREAM::stream_ref&& connectedStream )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            auto connection = base_type::createConnection( BL_PARAM_FWD( connectedStream ) );

            /*
             * For testing purpose force the connection to be cancelled before it is returned
             */

            connection -> requestCancel();

            return connection;
        }

        virtual auto createProtocolHandshakeTask( SAA_inout typename STREAM::stream_ref&& connectedStream )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            auto task = base_type::createProtocolHandshakeTask( BL_PARAM_FWD( connectedStream ) );

            /*
             * For testing purpose force the connection to be cancelled before it is returned
             */

            task -> requestCancel();

            return task;
        }
    };

} // utest

UTF_AUTO_TEST_CASE( IO_EarlyCancelIssueTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef om::ObjectImpl
    <
        utest::TestTcpBlockServerT
        <
            TcpSslSocketAsyncBase /* STREAM */,
            AsyncMessageDispatcherWrapper,
            TcpServerPolicySmooth
        >
    >
    acceptor_t;

    std::vector< om::ObjPtr< Task > > connections;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                /*
                 * Note that the connection establisher is chosen to be non-SSL on purpose to
                 * simulate the bug of a task which is canceled before even started is allowed
                 * to be scheduled
                 *
                 * The non-SSL connection establisher is connected to SSL backend which will
                 * cause the handshake task to actually hang while trying to read on the socket
                 * (which is the big because it should never be scheduled if it is cancelled)
                 */

                typedef TcpConnectionEstablisherConnectorImpl< TcpSocketAsyncBase /* STREAM */ >
                    connection_establisher_t;

                const std::size_t noOfConnections = 256U;

                for( std::size_t i = 0U; i < noOfConnections; ++i )
                {
                    const auto inboundConnection =
                        connection_establisher_t::template createInstance< Task >(
                            cpp::copy( test::UtfArgsParser::host() ),
                            test::UtfArgsParser::port()                     /* inboundPort */,
                            false                                           /* logExceptions */
                            );

                    connections.push_back( om::copy( inboundConnection ) );

                    eq -> push_back( inboundConnection );
                }

                eq -> flushNoThrowIfFailed();

                /*
                 * Wait for 2 seconds for the server tasks to start and then try
                 * to cancel the acceptor and unwind everything
                 */

                os::sleep( time::seconds( 2L ) );

                controlToken -> requestCancel();
            }
            );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    BrokerFacade::execute
    <
        SslBrokerDispatchingBackendProcessingImpl           /* BACKEND */,
        AsyncMessageDispatcherWrapper                       /* ASYNCWRAPPER */,
        acceptor_t                                          /* ACCEPTOR */
    >
    (
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests,
        om::copy( controlToken )
    );
}

UTF_AUTO_TEST_CASE( IO_ConnectionEstablisherBasicTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    typedef utest::TestMessagingUtils                                               utils_t;
    typedef ProxyBrokerBackendProcessingFactorySsl::connection_establisher_t        connection_establisher_t;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( tasks::ExecutionQueue::OptionKeepAll );

            const std::size_t noOfConnections = test::UtfArgsParser::connections();

            for( std::size_t i = 0U; i < noOfConnections; ++i )
            {
                const auto connection =
                    connection_establisher_t::template createInstance< Task >(
                        cpp::copy( test::UtfArgsParser::host() ),
                        test::UtfArgsParser::port()
                        );


                eq -> push_back( connection );
            }

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Establishing "
                    << noOfConnections
                    << " connections to endpoint: "
                    << net::formatEndpointId( test::UtfArgsParser::host(), test::UtfArgsParser::port() )
                );

            eq -> flush();

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "All "
                    << noOfConnections
                    << " connections to endpoint "
                    << net::formatEndpointId( test::UtfArgsParser::host(), test::UtfArgsParser::port() )
                    << " were established successfully"
                );

            utils_t::waitForKeyOrTimeout();
        }
        );
}

UTF_AUTO_TEST_CASE( IO_FlushQueueWithRetriesOnTargetPeerNotFoundTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    typedef utest::TestMessagingUtils utils_t;

    const auto heartbeatInterval =  time::seconds( 3L );

    const auto callbackTests = []() -> void
    {
        const auto singleMessageWithRetriesTests = [](
            SAA_in          const unsigned short                                        port1,
            SAA_in          const unsigned short                                        port2,
            SAA_in          const bl::uuid_t&                                           peerId1,
            SAA_in          const bl::uuid_t&                                           peerId2
            )
            -> void
        {
            const auto incomingSink = om::lockDisposable(
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
                     * First create a client for the proxy and then create such client for the
                     * real broker
                     *
                     * The peer id is either fixed or a unique one is generated for each connection
                     */

                    om::ObjPtrDisposable< MessagingClientObject > client1;
                    om::ObjPtrDisposable< MessagingClientObject > client2;

                    {
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
                    }

                    /*
                     * Create a simple timer which will connect client2 with some delay (e.g. 2 seconds)
                     */

                    const auto defaultDuration = time::seconds( 4L );

                    SimpleTimer timer(
                        [ & ]() -> time::time_duration
                        {
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

                            return defaultDuration;
                        },
                        cpp::copy( defaultDuration ),
                        cpp::copy( defaultDuration )       /* initDelay (same as defaultDuration) */
                        );

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

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message was being scheduled to be sent to incomingSink..."
                                    );

                                clientSink -> connect( incomingSink.get() );

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

                                utils_t::flushQueueWithRetriesOnTargetPeerNotFound( eqLocal );

                                BL_LOG(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "Message send to incomingSink successfully"
                                    );

                                clientSink -> disconnect();
                            }
                            );
                    }
                }
                );

            dispatchAssertions -> requireNone();
        };

        singleMessageWithRetriesTests(
            test::UtfArgsParser::port()             /* port1 */,
            test::UtfArgsParser::port()             /* port2 */,
            uuids::create()                         /* peerId1 */,
            uuids::create()                         /* peerId2 */
            );
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
        test::UtfArgsParser::port()                                 /* inboundPort */,
        test::UtfArgsParser::port() + 1U                            /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                          /* maxConcurrentTasks */,
        callbackTests,
        om::copy( controlToken ),
        dataBlocksPool,
        cpp::copy( heartbeatInterval )
        );
}

UTF_AUTO_TEST_CASE( ForwardingBackendBasicTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    const auto callbackTests = [ & ]() -> void
    {
        utest::TestMessagingUtils::forwardingBackendTests( om::copy( controlToken ) );
    };

    test::MachineGlobalTestLock lock;

    const auto processingBackend = om::lockDisposable(
        utest::TestMessagingUtils::createTestMessagingBackend()
        );

    BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests,
        om::copy( controlToken )
        );
}

