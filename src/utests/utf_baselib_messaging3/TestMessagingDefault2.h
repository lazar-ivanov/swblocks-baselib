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
 * Continuation of TestMessagingDefault.h, split so that no single test translation unit exhausts a
 * 32-bit compiler host - see notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These are the conversation processing and message processing cases. The range was chosen because
 * it is a clean excision: everything it defines is used only within it, and nothing outside it
 * refers back in, so the two parts have no shared names at all.
 *
 * This file must be included after TestMessagingDefault.h - it is a continuation, not a standalone
 * header.
 */

#include <baselib/core/BuildInfo.h>

#include <utests/baselib/TestMessagingUtils.h>
#include <utests/baselib/UtfCrypto.h>
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

            const auto cbCreateRequest = [ & ]( SAA_in const bl::uuid_t& messageId ) -> om::ObjPtr< BrokerProtocol >
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

namespace
{
    /**
     * @brief The request / response conversation round trip between two messaging clients over
     * a real broker
     *
     * It is shared by IO_MessagingMessageProcessingTests and by
     * IO_MessagingSecretsNeverReachTheLogTests so that the log oracle of the latter can never
     * drift away from the round trip it is meant to observe
     *
     * When 'receivedPrincipal' is provided it receives the security principal the broker
     * stamped on the request, as it was seen by the receiving side
     */

    void messageProcessingRoundTrip(
        SAA_inout_opt   bl::om::ObjPtr< bl::messaging::SecurityPrincipal >*     receivedPrincipal = nullptr
        )
    {
        using namespace bl;
        using namespace bl::tasks;
        using namespace bl::messaging;

        typedef bl::messaging::ConversationProcessingBaseImpl<>::payload_t payload_t;

        const auto callbackTests = [ receivedPrincipal ]() -> void
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

                    /*
                     * The security principal exactly as it arrived on the receiving side - the
                     * broker stamps it on the request once it has authorized the authentication
                     * token, so observing it here is what tells a caller that the principal was
                     * still on the message after it had been logged
                     *
                     * It is recorded on a dispatch thread and handed to the caller further below,
                     * on the main test thread
                     */

                    const auto principalLock = std::make_shared< bl::os::mutex >();

                    const auto principalSlot =
                        std::make_shared< bl::om::ObjPtr< SecurityPrincipal > >();

                    const auto incomingObjectChannel2 = bl::om::lockDisposable(
                        MessagingClientObjectDispatchFromCallback::createInstance(
                            [ = ](
                                SAA_in              const bl::uuid_t&                               targetPeerId,
                                SAA_in              const bl::om::ObjPtr< BrokerProtocol >&         brokerProtocol,
                                SAA_in_opt          const bl::om::ObjPtr< Payload >&                payload
                                )
                                -> void
                            {
                                const auto& identityInfo = brokerProtocol -> principalIdentityInfo();

                                if( identityInfo && identityInfo -> securityPrincipal() )
                                {
                                    BL_MUTEX_GUARD( *principalLock );

                                    *principalSlot = om::copy( identityInfo -> securityPrincipal() );
                                }

                                utest::TestMessagingUtils::dispatchCallback(
                                    client2Sink,
                                    targetPeerId2   /* targetPeerIdExpected */,
                                    dispatchAssertions,
                                    targetPeerId,
                                    brokerProtocol,
                                    payload
                                    );
                            }
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

                    if( receivedPrincipal )
                    {
                        BL_MUTEX_GUARD( *principalLock );

                        *receivedPrincipal = om::copy( *principalSlot );
                    }
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

} // __unnamed

UTF_AUTO_TEST_CASE( IO_MessagingMessageProcessingTests )
{
    messageProcessingRoundTrip();
}

UTF_AUTO_TEST_CASE( IO_MessagingSecretsNeverReachTheLogTests )
{
    using namespace bl;
    using namespace bl::messaging;

    /*
     * Redaction of credentials in this library is spread over several independent, function
     * level mechanisms - the BrokerProtocol ostream operator swaps the principal identity info
     * out before it pretty prints, the payload operators print only null vs. non null and the
     * HTTP client substitutes "[REDACTED]" in secure mode. Whether a credential stays out of
     * the log is however a property of their composition, which no per function assertion can
     * observe
     *
     * This case is that stream level oracle - it captures everything the library logs while a
     * real conversation which carries a credential travels client -> broker -> server and back,
     * and then searches the whole of the captured text for the credential
     */

    const auto& secret = utest::TestMessagingUtils::getTokenData();

    UTF_REQUIRE( ! secret.empty() );

    const auto levelBefore = Logging::getLevel();

    om::ObjPtr< SecurityPrincipal > receivedPrincipal;

    cpp::SafeOutputStringStream roundTripCapture;

    {
        /*
         * The level has to be pushed globally - the one and only site in the repository which
         * uses the redacting operator is on the trace tier and it runs on worker threads, which
         * have no TLS override of their own
         */

        Logging::LineLoggerPusher pushLogger( Logging::getDefaultLineLogger( roundTripCapture ) );
        Logging::LevelPusher pushLevel( Logging::LL_TRACE, true /* global */ );

        messageProcessingRoundTrip( &receivedPrincipal );
    }

    /*
     * The negative control - the very same secret, logged through the very same mechanism into
     * a separate capture, has to be found by the very same search
     *
     * Without it every absence assertion below would pass just as happily against an empty
     * string, which is exactly the way a redaction oracle silently stops being one
     */

    cpp::SafeOutputStringStream canaryCapture;

    {
        Logging::LineLoggerPusher pushLogger( Logging::getDefaultLineLogger( canaryCapture ) );
        Logging::LevelPusher pushLevel( Logging::LL_TRACE, true /* global */ );

        BL_LOG(
            Logging::trace(),
            BL_MSG()
                << "canary-"
                << secret
            );
    }

    /*
     * Neither pusher may leak its level into the rest of the module
     */

    UTF_REQUIRE_EQUAL( ( int ) levelBefore, ( int ) Logging::getLevel() );

    const auto canaryText = canaryCapture.str();

    UTF_REQUIRE( cpp::contains( canaryText, "canary-" + secret ) );
    UTF_REQUIRE( std::string::npos != canaryText.find( secret ) );

    const auto text = roundTripCapture.str();

    UTF_REQUIRE( ! text.empty() );

    /*
     * The oracle is not vacuous - the round trip did log the very documents which carry the
     * credential, so a search over the captured text is a search over the right text
     */

    UTF_REQUIRE( cpp::contains( text, "Sending" ) );
    UTF_REQUIRE( cpp::contains( text, "conversationId" ) );
    UTF_REQUIRE( cpp::contains( text, "AsyncRpcDispatch" ) );

    /*
     * The payload was never dumped - only the placeholder operator reached the log
     */

    UTF_REQUIRE( cpp::contains( text, "<non null generic async RPC payload>" ) );

    /*
     * ... and neither the credential nor the container which carries it ever reached it
     */

    UTF_REQUIRE( std::string::npos == text.find( secret ) );
    UTF_REQUIRE( std::string::npos == text.find( "principalIdentityInfo" ) );
    UTF_REQUIRE( std::string::npos == text.find( "authenticationToken" ) );

    /*
     * The message still carried its principal after having been logged - the swap out in the
     * BrokerProtocol operator is undone by its BL_SCOPE_EXIT, so the broker was still able to
     * authorize the request and stamp its own principal on it
     */

    UTF_REQUIRE( receivedPrincipal );

    UTF_REQUIRE_EQUAL(
        bl::str::to_lower_copy( receivedPrincipal -> sid() ),
        utest::DummyAuthorizationCache::dummySid()
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
                     * (d) bl::NotFoundException used to have no arm in the dispatch chain and
                     * came back as a bl::UnexpectedException carrying the right message and
                     * the wrong type; it has one now, so the type survives a real wire the
                     * same way (b)'s does
                     *
                     * The fallback arm itself is no longer reachable with any declared bl::
                     * type - it exists for a type name a newer peer sends - so it is covered
                     * at unit level by ErrorToJsonExceptionTypeMappingTests in
                     * utf_baselib_data rather than here
                     */

                    const auto processor1 = cbRunConversation(
                        []() -> void
                        {
                            BL_THROW( bl::NotFoundException(), "async-rpc marker: notfound" );
                        }
                        );

                    cbRequireServerErrorJson( processor1, bl::NotFoundException::fullTypeNameStatic() );

                    const auto cbIsNotFound = []( SAA_in const bl::NotFoundException& e ) -> bool
                    {
                        return
                            std::string( "bl::NotFoundException" ) == std::string( e.fullTypeName() ) &&
                            std::string( "async-rpc marker: notfound" ) == std::string( e.what() );
                    };

                    UTF_REQUIRE_EXCEPTION(
                        processor1 -> getResponse(),
                        bl::NotFoundException,
                        cbIsNotFound
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
