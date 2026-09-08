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

#include <baselib/messaging/AsyncDataChunkStorage.h>

#include <utests/baselib/TestAsyncCommon.h>

/*****************************************************************************************************************
 * Async executor implementation tests (v2)
 */

namespace asyncv2
{
    typedef bl::AsyncDataChunkStorage                                                       AsyncDataChunkStorage;
    typedef bl::AsyncDataChunkStorage::AsyncOperationStateImpl                              AsyncOperationStateImpl;

    /**
     * @brief Test async task
     */

    template
    <
        typename BASE
    >
    class AsyncTestTaskBaseT :
        public utest::AsyncTestTaskSharedBase< BASE, AsyncDataChunkStorage >
    {
    protected:

        typedef AsyncTestTaskBaseT< BASE >                                                  this_type;
        typedef utest::AsyncTestTaskSharedBase< BASE, AsyncDataChunkStorage >               base_type;

        enum : std::size_t
        {
            BLOCK_CAPACITY = 512U,
        };

        using base_type::g_asyncCalls;
        using base_type::m_wrapperImpl;
        using base_type::m_asyncCalls;
        using base_type::m_operationState;
        using base_type::m_operation;
        using base_type::m_canceled;
        using base_type::m_started;
        using base_type::m_allowSleeps;

        using base_type::releaseOperation;
        using base_type::chk2SleepRandomTime;
        using base_type::wasCanceled;

        typedef AsyncDataChunkStorage::OperationId                                          OperationId;
        typedef AsyncDataChunkStorage::CommandId                                            CommandId;

        const bl::uuid_t                                                                    m_sessionId;
        const bl::uuid_t                                                                    m_chunkIdLoad;
        const bl::uuid_t                                                                    m_chunkIdSave;
        const bl::uuid_t                                                                    m_chunkIdRemove;
        const bl::om::ObjPtr< utest::BackendImplTestImpl >&                                 m_backendImpl;

        AsyncTestTaskBaseT(
            SAA_in              const bl::om::ObjPtr< utest::BackendImplTestImpl >&         backendImpl,
            SAA_in              const bl::om::ObjPtr< AsyncDataChunkStorage >&              asyncStorage
            )
            :
            base_type( asyncStorage ),
            m_sessionId( bl::uuids::create() ),
            m_chunkIdLoad( bl::uuids::create() ),
            m_chunkIdSave( bl::uuids::create() ),
            m_chunkIdRemove( bl::uuids::create() ),
            m_backendImpl( backendImpl )
        {
            BL_ASSERT( m_backendImpl );
        }

        void startTask()
        {
            createOperation( OperationId::Get, m_chunkIdLoad );

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onLoad,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            m_started = true;
        }

        auto dataBlock() const NOEXCEPT -> const bl::om::ObjPtr< bl::data::DataBlock >&
        {
            return m_backendImpl -> getData();
        }

        void createOperation(
            SAA_in          const OperationId           operationId,
            SAA_in          const bl::uuid_t&           chunkId
            )
        {
            releaseOperation();

            m_operationState = m_wrapperImpl -> template createOperationState< AsyncOperationStateImpl >(
                operationId,
                m_sessionId,
                chunkId,
                bl::uuids::nil(),       /* sourcePeerId */
                bl::uuids::nil()        /* targetPeerId */
                );

            m_operation = m_wrapperImpl -> asyncExecutor() -> createOperation(
                bl::om::qi< bl::AsyncOperationState >( m_operationState )
                );

            BL_CHK(
                false,
                ! m_operationState -> data(),
                BL_MSG()
                    << "Operation data should be nullptr"
                );
        }

        void validate(
            SAA_in          const bool                  expectData,
            SAA_in          const bool                  expectValidData,
            SAA_in          const bl::uuid_t&           chunkIdExpected
            )
        {
            BL_CHK(
                false,
                ( expectData || expectValidData ) ?
                    nullptr != m_operationState -> data() : nullptr == m_operationState -> data(),
                BL_MSG()
                    << "Operation data cannot be nullptr"
                );

            if( expectValidData )
            {
                /*
                 * This is an allocated and data block; the size should be valid and it
                 * should match dataBlock() -> size()
                 */

                BL_CHK(
                    false,
                    dataBlock() -> size() == m_operationState -> data() -> size(),
                    BL_MSG()
                        << "Operation data size is not valid"
                    );
            }
            else if( expectData )
            {
                /*
                 * This is an allocated data block; the size should be set to zero
                 */

                BL_CHK(
                    false,
                    0U == m_operationState -> data() -> size(),
                    BL_MSG()
                        << "Operation data size is not valid"
                    );
            }

            BL_CHK(
                false,
                chunkIdExpected == m_operationState -> chunkId(),
                BL_MSG()
                    << "Operation data chunk id is invalid"
                );

            BL_CHK(
                false,
                m_sessionId == m_operationState -> sessionId(),
                BL_MSG()
                    << "Operation data session id is invalid"
                );
        }

        void onLoad( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            validate( true /* expectData */, true /* expectValidData */, m_chunkIdLoad /* chunkIdExpected */ );

            /*
             * Corrupt the block before we return it to ensure it won't be reused
             * directly and come as correct
             */

            BL_CHK(
                false,
                m_operationState -> data() -> size() > sizeof( m_sessionId ),
                BL_MSG()
                    << "Operation data cannot be nullptr"
                );

            /*
             * Just corrupt the data block with some random data to ensure that the onAlloc
             * and onSave will copy the correct data - otherwise onSave will fire an assert
             */

            ::memcpy( m_operationState -> data() -> pv(), &m_sessionId, sizeof( m_sessionId ) );

            createOperation( OperationId::Alloc, m_chunkIdSave );

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onAlloc,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END_NOTREADY()
        }

        void onAlloc( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            validate( true /* expectData */, false /* expectValidData */, m_chunkIdSave /* chunkIdExpected */ );

            const auto& referenceData = dataBlock();

            BL_CHK(
                false,
                m_operationState -> data() -> capacity() >= referenceData -> size(),
                BL_MSG()
                    << "Allocated block does not have enough capacity"
                );

            ::memcpy( m_operationState -> data() -> pv(), referenceData -> pv(), referenceData -> size() );
            m_operationState -> data() -> setSize( referenceData -> size() );

            m_operationState -> operationId( OperationId::Put );

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onSave,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END_NOTREADY()
        }

        void onSave( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            validate( true /* expectData */, true /* expectValidData */, m_chunkIdSave /* chunkIdExpected */ );

            createOperation( OperationId::Command, m_chunkIdRemove );
            m_operationState -> commandId( CommandId::Remove );

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onRemove,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END_NOTREADY()
        }

        void onRemove( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            validate( false /* expectData */, false /* expectValidData */, m_chunkIdRemove /* chunkIdExpected */ );

            createOperation( OperationId::Command, bl::uuids::nil() /* chunkId */ );
            m_operationState -> commandId( CommandId::FlushPeerSessions );

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onFlushPeerSessions,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END_NOTREADY()
        }

        void onFlushPeerSessions( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            validate( false /* expectData */, false /* expectValidData */, bl::uuids::nil() /* chunkIdExpected */ );

            releaseOperation();

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END()
        }

    public:

        template
        <
            typename Functor
        >
        static void executeTests(
            SAA_in                      const bool                                                  noisyMode,
            SAA_in                      Functor&&                                                   cb
            )
        {
            {
                const auto backendImpl = bl::om::lockDisposable(
                    utest::BackendImplTestImpl::createInstance( BLOCK_CAPACITY )
                    );

                backendImpl -> setNoisyMode( noisyMode );

                {
                    const auto storage = bl::om::qi< bl::data::DataChunkStorage >( backendImpl );

                    const auto asyncStorage =
                        AsyncDataChunkStorage::createInstance(
                            storage /* writeBackend */,
                            storage /* readBackend */,
                            test::UtfArgsParser::threadsCount()
                            );

                    asyncStorage -> impl() -> blockCapacity( BLOCK_CAPACITY );

                    {
                        bl::tasks::scheduleAndExecuteInParallel(
                            [ & ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
                            {
                                cb( backendImpl, asyncStorage, eq );
                            }
                            );
                    }
                }
            }
        }

        template
        <
            typename IMPL
        >
        static void executePerfTests( SAA_in const bool testCancel )
        {
            this_type::executeTests(
                false /* noisyMode */,
                [ & ]
                (
                    SAA_in              const bl::om::ObjPtr< utest::BackendImplTestImpl >&         backendImpl,
                    SAA_in              const bl::om::ObjPtr< AsyncDataChunkStorage >&              asyncStorage,
                    SAA_in              const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&          eq
                ) -> void
                {
                    asyncStorage -> impl() -> blockCapacity( BLOCK_CAPACITY );

                    try
                    {
                        eq -> setOptions( bl::tasks::ExecutionQueue::OptionKeepAll );

                        const std::size_t maxIterations = ( testCancel ? 10U : 40U ) * 1024U;

                        const auto t1 = bl::time::microsec_clock::universal_time();

                        BL_LOG(
                            bl::Logging::debug(),
                            BL_MSG()
                                << "Executing "
                                << ( 5U * maxIterations )
                                << " async calls on task pool with size of "
                                << maxIterations
                            );

                        UTF_REQUIRE_EQUAL( 0U, g_asyncCalls );

                        try
                        {
                            for( std::size_t i = 0U; i < maxIterations; ++i )
                            {
                                const auto asyncTaskImpl =
                                    IMPL::template createInstance< IMPL >( backendImpl, asyncStorage );

                                asyncTaskImpl -> allowSleeps( testCancel );

                                eq -> push_back( bl::om::qi< bl::tasks::Task >( asyncTaskImpl ) );
                            }

                            {
                                const auto duration = bl::time::microsec_clock::universal_time() - t1;
                                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                                BL_LOG(
                                    bl::Logging::debug(),
                                    BL_MSG()
                                        << "Scheduling "
                                        << ( 5U * maxIterations )
                                        << " async calls on task pool with size of "
                                        << maxIterations
                                        << " took "
                                        << durationInSeconds
                                        << " seconds"
                                    );
                            }

                            if( testCancel )
                            {
                                bl::os::sleep( bl::time::seconds( 2 ) );

                                std::size_t canceledTests = 0U;

                                eq -> cancelAll( false /* wait */ );

                                for( ;; )
                                {
                                    const auto asyncTask = eq -> pop( true /* wait */ );

                                    if( ! asyncTask )
                                    {
                                        break;
                                    }

                                    if( asyncTask -> isFailed() )
                                    {
                                        const auto taskImpl = bl::om::qi< IMPL >( asyncTask );

                                        if( taskImpl -> started() )
                                        {
                                            if( ! taskImpl -> canceled() )
                                            {
                                                /*
                                                 * This is a strange case as the task is expected to either
                                                 * complete successfully or fail due to cancellation or due
                                                 * to bl::asio::error::operation_aborted
                                                 *
                                                 * Let's dump the exception info and call BL_RIP_MSG to catch
                                                 * and debug this case when it happens
                                                 */

                                                const auto printExceptionAndRip = []( SAA_inout std::exception& e )
                                                {
                                                    const auto msg = bl::resolveMessage(
                                                        BL_MSG()
                                                            << "Task failed but was not canceled; exception details:\n"
                                                            << bl::eh::diagnostic_information( e )
                                                        );

                                                    BL_LOG(
                                                        bl::Logging::debug(),
                                                        BL_MSG()
                                                            << msg
                                                        );

                                                    BL_RIP_MSG( msg.c_str() );
                                                };

                                                try
                                                {
                                                    bl::cpp::safeRethrowException( taskImpl -> exception() );
                                                }
                                                catch( bl::eh::system_error& e )
                                                {
                                                    if( bl::asio::error::operation_aborted != e.code() )
                                                    {
                                                        printExceptionAndRip( e );
                                                    }
                                                }
                                                catch( std::exception& e )
                                                {
                                                    printExceptionAndRip( e );
                                                }
                                            }
                                        }

                                        try
                                        {
                                            bl::cpp::safeRethrowException( asyncTask -> exception() );
                                        }
                                        catch( bl::eh::system_error& e )
                                        {
                                            UTF_REQUIRE( bl::asio::error::operation_aborted == e.code() );
                                        }

                                        ++canceledTests;
                                    }
                                }

                                BL_LOG(
                                    bl::Logging::debug(),
                                    BL_MSG()
                                        << "Canceled "
                                        << canceledTests
                                        << " async tasks"
                                    );

                                /*
                                 * The 2 s sleep above leaves plenty of the 10K x 4-5 randomised
                                 * async calls still in flight, so cancelAll() must have failed at
                                 * least one of them - otherwise a cancellation path which silently
                                 * stopped cancelling would leave this branch green, because the
                                 * client tasks are cancelled by their own execution queue no
                                 * matter what the executor does
                                 *
                                 * The BL_ASSERT( eq -> isEmpty() ) below is deliberately left
                                 * alone - it sits outside this branch and also guards the
                                 * flushAndDiscardReady() path shared with the non-cancel cases
                                 */

                                UTF_REQUIRE( canceledTests > 0U );

                                UTF_REQUIRE( eq -> isEmpty() );
                            }
                            else
                            {
                                eq -> flushAndDiscardReady();
                            }

                            BL_ASSERT( eq -> isEmpty() );
                        }
                        catch( std::exception& )
                        {
                            g_asyncCalls = 0U;
                            throw;
                        }

                        const auto duration = bl::time::microsec_clock::universal_time() - t1;
                        const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                        BL_LOG(
                            bl::Logging::debug(),
                            BL_MSG()
                                << "Executing async operations took "
                                << durationInSeconds
                                << " seconds; "
                                << "speed is "
                                << ( g_asyncCalls / durationInSeconds )
                                << " async calls/s"
                            );

                        if( ! testCancel )
                        {
                            UTF_REQUIRE_EQUAL( 5U * maxIterations, g_asyncCalls );
                            g_asyncCalls = 0U;
                        }
                    }
                    catch( std::exception& )
                    {
                        eq -> forceFlushNoThrow();
                        throw;
                    }
                }
                );
        }

        template
        <
            typename IMPL
        >
        static void executeBasicTests()
        {
            this_type::executeTests(
                true /* noisyMode */,
                [ & ]
                (
                    SAA_in              const bl::om::ObjPtr< utest::BackendImplTestImpl >&         backendImpl,
                    SAA_in              const bl::om::ObjPtr< AsyncDataChunkStorage >&              asyncStorage,
                    SAA_in              const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&          eq
                ) -> void
                {
                    asyncStorage -> impl() -> blockCapacity( BLOCK_CAPACITY );

                    const auto asyncTaskImpl =
                        IMPL::template createInstance< IMPL >( backendImpl, asyncStorage );

                    const auto asyncTask = bl::om::qi< bl::tasks::Task >( asyncTaskImpl );

                    UTF_REQUIRE_EQUAL( 0U, asyncTaskImpl -> asyncCalls() );
                    UTF_REQUIRE_EQUAL( 0U, g_asyncCalls );

                    eq -> push_back( asyncTask );

                    eq -> waitForSuccess( asyncTask );

                    UTF_REQUIRE_EQUAL( 5U, asyncTaskImpl -> asyncCalls() );
                    UTF_REQUIRE_EQUAL( 5U, g_asyncCalls );

                    g_asyncCalls = 0U;
                }
                );
        }
    };

    /**
     * @brief The async only flavor
     */

    template
    <
        typename E = void
    >
    class AsyncTestTaskAsyncOnlyT :
        public AsyncTestTaskBaseT< bl::tasks::TaskBase >
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( AsyncTestTaskAsyncOnlyT )

    protected:

        typedef AsyncTestTaskBaseT< bl::tasks::TaskBase >                                   base_type;

        AsyncTestTaskAsyncOnlyT(
            SAA_in              const bl::om::ObjPtr< utest::BackendImplTestImpl >&         backendImpl,
            SAA_in              const bl::om::ObjPtr< AsyncDataChunkStorage >&              asyncStorage
            )
            :
            base_type( backendImpl, asyncStorage )
        {
        }

        virtual void scheduleTask( SAA_in const std::shared_ptr< bl::tasks::ExecutionQueue >& eq ) OVERRIDE
        {
            BL_UNUSED( eq );

            base_type::startTask();
        }
    };

    typedef bl::om::ObjectImpl< AsyncTestTaskAsyncOnlyT<> > AsyncTestTaskAsyncOnlyImpl;

    /**
     * @brief The fast start flavor
     */

    template
    <
        typename E = void
    >
    class AsyncTestTaskAsyncFastStartT :
        public AsyncTestTaskBaseT< bl::tasks::SimpleTaskBase >
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( AsyncTestTaskAsyncFastStartT )

    protected:

        typedef AsyncTestTaskBaseT< bl::tasks::SimpleTaskBase >                             base_type;

        AsyncTestTaskAsyncFastStartT(
            SAA_in              const bl::om::ObjPtr< utest::BackendImplTestImpl >&         backendImpl,
            SAA_in              const bl::om::ObjPtr< AsyncDataChunkStorage >&              asyncStorage
            )
            :
            base_type( backendImpl, asyncStorage )
        {
        }

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            base_type::startTask();

            BL_TASKS_HANDLER_END_NOTREADY()
        }
    };

    typedef bl::om::ObjectImpl< AsyncTestTaskAsyncFastStartT<> > AsyncTestTaskAsyncFastStartImpl;

} // asyncv2

UTF_AUTO_TEST_CASE( AsyncV2_BasicTests )
{
    using namespace asyncv2;
    AsyncTestTaskAsyncOnlyImpl::executeBasicTests< AsyncTestTaskAsyncOnlyImpl >();
}

UTF_AUTO_TEST_CASE( AsyncV2_SmallPerfTests )
{
    using namespace asyncv2;
    AsyncTestTaskAsyncOnlyImpl::executePerfTests< AsyncTestTaskAsyncOnlyImpl >( false /* testCancel */ );
}

UTF_AUTO_TEST_CASE( AsyncV2_CancelTests )
{
    using namespace asyncv2;
    AsyncTestTaskAsyncFastStartImpl::executePerfTests< AsyncTestTaskAsyncFastStartImpl >( true /* testCancel */ );
}


UTF_AUTO_TEST_CASE( AsyncV2_BlocksOutstandingCapTests )
{
    using namespace bl;
    using namespace asyncv2;

    typedef AsyncDataChunkStorage::OperationId                                              OperationId;

    /*
     * The shared state's admission control refuses to allocate a new data block once the
     * configured maximum number of outstanding blocks has been reached, and a block is only
     * returned to the pool - and the counter decremented - by releaseResources()
     *
     * The decrement is made through a compare exchange loop, so that a state which never
     * obtained a block from allocateBlock() can never underflow the counter
     *
     * The operation states are driven directly here, so no server and no sockets are needed
     */

    const auto backendImpl = om::lockDisposable( utest::BackendImplTestImpl::createInstance() );

    {
        const auto storage = om::qi< data::DataChunkStorage >( backendImpl );

        const auto asyncStorage = om::lockDisposable(
            AsyncDataChunkStorage::createInstance(
                storage                                     /* writeBackend */,
                storage                                     /* readBackend */,
                test::UtfArgsParser::threadsCount()
                )
            );

        const auto newAllocState = [ &asyncStorage ]() -> om::ObjPtr< AsyncOperationStateImpl >
        {
            return asyncStorage -> createOperationState< AsyncOperationStateImpl >(
                OperationId::Alloc,
                uuids::create()                             /* sessionId */,
                uuids::create()                             /* chunkId */,
                uuids::nil()                                /* sourcePeerId */,
                uuids::nil()                                /* targetPeerId */
                );
        };

        asyncStorage -> impl() -> maxOutstandingBlocks( 2U );

        UTF_REQUIRE_EQUAL( 2U, asyncStorage -> impl() -> maxOutstandingBlocks() );
        UTF_REQUIRE_EQUAL( 0U, asyncStorage -> impl() -> outstandingBlocks() );

        const auto state1 = newAllocState();
        state1 -> execute();

        const auto state2 = newAllocState();
        state2 -> execute();

        UTF_REQUIRE( state1 -> data() );
        UTF_REQUIRE( state2 -> data() );
        UTF_REQUIRE_EQUAL( 2U, asyncStorage -> impl() -> outstandingBlocks() );

        /*
         * The cap has been reached, so the next allocation must be refused
         */

        const auto state3 = newAllocState();

        UTF_REQUIRE_THROW_ERROR_CODE_AND_MESSAGE(
            state3 -> execute(),
            SystemException,
            eh::errc::make_error_code( eh::errc::no_buffer_space ),
            "maximum number of outstanding data blocks"
            );

        UTF_REQUIRE( ! state3 -> data() );
        UTF_REQUIRE_EQUAL( 2U, asyncStorage -> impl() -> outstandingBlocks() );

        /*
         * Releasing an operation returns its block and frees the slot again
         */

        state1 -> releaseResources();

        UTF_REQUIRE_EQUAL( 1U, asyncStorage -> impl() -> outstandingBlocks() );
        UTF_REQUIRE( ! state1 -> data() );

        const auto state4 = newAllocState();
        state4 -> execute();

        UTF_REQUIRE( state4 -> data() );
        UTF_REQUIRE_EQUAL( 2U, asyncStorage -> impl() -> outstandingBlocks() );

        /*
         * Releasing a state which never obtained a block must leave the counter alone
         */

        const auto state5 = newAllocState();

        state5 -> releaseResources();

        UTF_REQUIRE_EQUAL( 2U, asyncStorage -> impl() -> outstandingBlocks() );

        state2 -> releaseResources();
        state3 -> releaseResources();
        state4 -> releaseResources();

        UTF_REQUIRE_EQUAL( 0U, asyncStorage -> impl() -> outstandingBlocks() );

        /*
         * Zero means unbounded, which is the default
         */

        asyncStorage -> impl() -> maxOutstandingBlocks( 0U );

        UTF_REQUIRE_EQUAL( 0U, asyncStorage -> impl() -> maxOutstandingBlocks() );

        const auto state6 = newAllocState();
        state6 -> execute();

        const auto state7 = newAllocState();
        state7 -> execute();

        const auto state8 = newAllocState();
        state8 -> execute();

        UTF_REQUIRE_EQUAL( 3U, asyncStorage -> impl() -> outstandingBlocks() );

        state6 -> releaseResources();
        state7 -> releaseResources();
        state8 -> releaseResources();

        UTF_REQUIRE_EQUAL( 0U, asyncStorage -> impl() -> outstandingBlocks() );
    }
}

UTF_AUTO_TEST_CASE( AsyncV2_ReadWriteStorageRoutingTests )
{
    using namespace bl;
    using namespace asyncv2;

    typedef AsyncDataChunkStorage::OperationId                                              OperationId;
    typedef AsyncDataChunkStorage::CommandId                                                CommandId;

    /*
     * The async storage is constructed from a write storage and a read storage and the
     * routing between the two is the entire point of the type - Get is served by the read
     * storage while Put, Command::Remove and Command::FlushPeerSessions all go to the write
     * storage
     *
     * The rest of the suite always passes the same backend twice, so a swap of the two
     * constructor arguments is invisible to it
     */

    const auto readImpl = om::lockDisposable( utest::BackendImplTestImpl::createInstance() );
    const auto writeImpl = om::lockDisposable( utest::BackendImplTestImpl::createInstance() );

    /*
     * The block which is saved below is the one obtained from the Alloc operation and not
     * the write backend's own reference block, so its payload must not be verified
     */

    writeImpl -> setExpectRealData( true );

    {
        const auto asyncStorage = om::lockDisposable(
            AsyncDataChunkStorage::createInstance(
                om::qi< data::DataChunkStorage >( writeImpl )       /* writeBackend */,
                om::qi< data::DataChunkStorage >( readImpl )        /* readBackend */,
                test::UtfArgsParser::threadsCount()
                )
            );

        const auto sessionId = uuids::create();

        const auto newOpState = [ &asyncStorage, &sessionId ](
            SAA_in              const OperationId                   operationId,
            SAA_in              const uuid_t&                       chunkId
            )
            -> om::ObjPtr< AsyncOperationStateImpl >
        {
            return asyncStorage -> createOperationState< AsyncOperationStateImpl >(
                operationId,
                sessionId,
                chunkId,
                uuids::nil()                                /* sourcePeerId */,
                uuids::nil()                                /* targetPeerId */
                );
        };

        {
            const auto state = newOpState( OperationId::Get, uuids::create() /* chunkId */ );

            state -> execute();

            UTF_REQUIRE_EQUAL( 1U, readImpl -> loadCalls() );
            UTF_REQUIRE_EQUAL( 0U, writeImpl -> loadCalls() );

            UTF_REQUIRE_EQUAL(
                0U,
                readImpl -> saveCalls() + readImpl -> removeCalls() + readImpl -> flushCalls()
                );

            /*
             * The read must have populated the block which the Get path allocated
             */

            UTF_REQUIRE( state -> data() );
            UTF_REQUIRE_EQUAL( readImpl -> getData() -> size(), state -> data() -> size() );

            state -> releaseResources();
        }

        {
            /*
             * Put requires a data block and Alloc is how a test obtains one
             */

            const auto state = newOpState( OperationId::Alloc, uuids::create() /* chunkId */ );

            state -> execute();

            UTF_REQUIRE( state -> data() );

            state -> operationId( OperationId::Put );

            state -> execute();

            UTF_REQUIRE_EQUAL( 1U, writeImpl -> saveCalls() );
            UTF_REQUIRE_EQUAL( 0U, readImpl -> saveCalls() );

            state -> releaseResources();
        }

        {
            const auto state = newOpState( OperationId::Command, uuids::create() /* chunkId */ );

            state -> commandId( CommandId::Remove );

            state -> execute();

            UTF_REQUIRE_EQUAL( 1U, writeImpl -> removeCalls() );
            UTF_REQUIRE_EQUAL( 0U, readImpl -> removeCalls() );

            state -> releaseResources();
        }

        {
            const auto state = newOpState( OperationId::Command, uuids::nil() /* chunkId */ );

            state -> commandId( CommandId::FlushPeerSessions );

            state -> execute();

            UTF_REQUIRE_EQUAL( 1U, writeImpl -> flushCalls() );
            UTF_REQUIRE_EQUAL( 0U, readImpl -> flushCalls() );

            state -> releaseResources();
        }

        readImpl -> assertions().requireNone();
        writeImpl -> assertions().requireNone();
    }
}

namespace
{
    /**
     * @brief Makes bl::detail::AsyncExecutorImplT<>::verifyQueues reachable
     *
     * verifyQueues is a protected static helper with zero call sites anywhere in the
     * repository, so - being a member of a class template - it has never been instantiated
     * and therefore never compiled, in either variant on any platform. Its body calls
     * ExecutionQueue::scanQueue and om::qi< ExecutorTaskImpl >, both of which could have
     * drifted under it.
     *
     * No object of this type is ever constructed: AsyncExecutorImplT carries
     * BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR and has no default constructor, and verifyQueues
     * is static, so the derived probe idiom needs nothing more than the using declaration
     */

    struct VerifierProbe : public bl::detail::AsyncExecutorImplT<>
    {
        using bl::detail::AsyncExecutorImplT<>::verifyQueues;
    };

} // __unnamed

UTF_AUTO_TEST_CASE( AsyncV2_ExecutorQueueVerifierTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * verifyQueues() is documented as a DEBUG only BL_ASSERT helper which always returns
     * true, and the primary value of this case is forcing it to be instantiated at all
     *
     * Note that its scan callback does om::qi< ExecutorTaskImpl >( task ) *outside* the
     * BL_ASSERT, so it throws in release too for any task which is not an executor task -
     * i.e. it can only ever be called on a queue whose Pending and Executing queues are
     * empty, which is exactly the invariant the executor itself would call it under. Every
     * call below is therefore made on a drained queue.
     *
     * The debug only half - BL_ASSERT( taskImpl -> stopped() ) - is deliberately not
     * depended upon here; it is a no-op under NDEBUG
     */

    const auto queue = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
        );

    /*
     * An empty queue
     */

    UTF_REQUIRE( queue -> isEmpty() );
    UTF_REQUIRE( VerifierProbe::verifyQueues( queue ) );

    /*
     * After one task has run to completion - OptionKeepNone discards it, so both scanned
     * queues are empty again
     */

    {
        cpp::ScalarTypeIniter< bool > called;

        const auto task = SimpleTaskImpl::createInstance< Task >(
            cpp::void_callback_t(
                [ &called ]() -> void
                {
                    called = true;
                }
                )
            );

        queue -> push_back( task );
        queue -> waitForSuccess( task );

        UTF_REQUIRE( called.value() );
        UTF_REQUIRE( queue -> isEmpty() );

        UTF_REQUIRE( VerifierProbe::verifyQueues( queue ) );
    }

    /*
     * And after a task which does not finish on its own is cancelled out of the queue
     */

    {
        utest::AsyncTestSignal started;
        utest::AsyncTestSignal release;

        const auto task = SimpleTaskImpl::createInstance< Task >(
            cpp::void_callback_t(
                [ &started, &release ]() -> void
                {
                    started.signal();

                    ( void ) release.wait();
                }
                )
            );

        queue -> push_back( task );

        UTF_REQUIRE( started.wait() );

        release.signal();

        queue -> cancelAll( true /* wait */ );

        UTF_REQUIRE( queue -> isEmpty() );

        UTF_REQUIRE( VerifierProbe::verifyQueues( queue ) );
    }
}
