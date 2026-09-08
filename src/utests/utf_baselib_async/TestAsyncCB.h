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

#include <baselib/async/AsyncExecutorWrapperCallback.h>

#include <utests/baselib/TestAsyncCommon.h>
#include <utests/baselib/UtfConcurrent.h>

/*****************************************************************************************************************
 * Async executor implementation tests (for AsyncExecutorWrapperCallbackImpl)
 */

namespace asynccb
{
    typedef bl::AsyncExecutorWrapperCallbackImpl                             AsyncExecutorWrapperCallbackImpl;
    typedef bl::AsyncExecutorWrapperCallbackImpl::AsyncOperationStateImpl    AsyncOperationStateImpl;

    /**
     * @brief Test async task
     */

    template
    <
        typename BASE
    >
    class AsyncTestTaskBaseT :
        public utest::AsyncTestTaskSharedBase< BASE, AsyncExecutorWrapperCallbackImpl >
    {
    protected:

        typedef AsyncExecutorWrapperCallbackImpl                            wrapper_t;

        typedef AsyncTestTaskBaseT< BASE >                                  this_type;
        typedef utest::AsyncTestTaskSharedBase< BASE, wrapper_t >           base_type;

        typedef bl::cpp::function
        <
            void (
                SAA_in      const bl::om::ObjPtrCopyable< this_type >&      thisObj,
                SAA_in      const bl::AsyncOperation::Result&               result
                ) NOEXCEPT
        >
        async_callback_t;

        enum : std::size_t
        {
            NO_OF_CALLS = 4U,
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
        using base_type::coinToss;

        bl::cpp::ScalarTypeIniter< bool >                                   m_tasksAtEvenCalls;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_callNo;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_createTaskCalls;

        AsyncTestTaskBaseT(
            SAA_in              const bl::om::ObjPtr< wrapper_t >&          wrapperImpl,
            SAA_in_opt          const bool                                  tasksAtEvenCalls = coinToss()
            )
            :
            base_type( wrapperImpl ),
            m_tasksAtEvenCalls( tasksAtEvenCalls )
        {
        }

        void incrementCallNoWithRandomDelays( SAA_in const bool expectOddNumber )
        {
            chk2SleepRandomTime();

            if( expectOddNumber )
            {
                BL_CHK(
                    false,
                    0 != ( m_callNo % 2 ),
                    BL_MSG()
                        << "The sync call # is expected to be an odd number while it is "
                        << m_callNo.value()
                    );
            }
            else
            {
                BL_CHK(
                    false,
                    0 == ( m_callNo % 2 ),
                    BL_MSG()
                        << "The sync call # is expected to be an even number while it is "
                        << m_callNo.value()
                    );
            }

            ++m_callNo.lvalue();

            chk2SleepRandomTime();
        }

        bl::om::ObjPtr< bl::tasks::Task > createTask()
        {
            ++m_createTaskCalls.lvalue();

            if(
                ( 0 == ( m_callNo % 2 ) && m_tasksAtEvenCalls ) ||
                ( 0 != ( m_callNo % 2 ) && ! m_tasksAtEvenCalls )
                )
            {
                return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                    bl::cpp::bind(
                        &this_type::incrementCallNoWithRandomDelays,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        0 != ( m_callNo % 2 ) /* expectOddNumber ) */
                        )
                    );
            }

            return nullptr;
        }

        void startTask()
        {
            m_callNo = 0U;

            createOperation();

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    &this_type::onCall1,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            m_started = true;
        }

        void createOperation()
        {
            releaseOperation();

            m_operationState = m_wrapperImpl -> template createOperationState< AsyncOperationStateImpl >(
                bl::cpp::bind(
                    &this_type::incrementCallNoWithRandomDelays,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    m_tasksAtEvenCalls ? true : false /* expectOddNumber ) */
                    ),
                bl::cpp::bind(
                    &this_type::createTask,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this )
                    )
                );

            m_operation = m_wrapperImpl -> asyncExecutor() -> createOperation(
                bl::om::qi< bl::AsyncOperationState >( m_operationState )
                );

            BL_CHK(
                false,
                !! m_operationState -> callback(),
                BL_MSG()
                    << "Operation callback should be not be empty"
                );
        }

        void onHandleCall(
            SAA_in              const bl::AsyncOperation::Result&           result,
            SAA_in              const std::size_t                           expectedCallNo,
            SAA_in              const async_callback_t&                     nextCall
            ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            /*
             * depending on the value of m_tasksAtEvenCalls and the m_callNo we need
             * to check if result.task is set as expected
             */

            if( m_tasksAtEvenCalls )
            {
                UTF_REQUIRE( 0 != ( m_callNo % 2 ) ? !! result.task : ! result.task );
            }
            else
            {
                UTF_REQUIRE( 0 == ( m_callNo % 2 ) ? !! result.task : ! result.task );
            }

            UTF_REQUIRE_EQUAL( m_callNo.value(), expectedCallNo );

            if( coinToss() )
            {
                /*
                 * At random we either create a new operation or execute new call
                 * on the same operation and operation state object
                 */

                createOperation();
            }

            m_wrapperImpl -> asyncExecutor() -> asyncBegin(
                m_operation,
                bl::cpp::bind(
                    nextCall,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                    _1
                    )
                );

            ++m_asyncCalls.lvalue();
            ++g_asyncCalls;

            BL_TASKS_HANDLER_END_NOTREADY()
        }

        void onCall1( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            onHandleCall( result, 1U /* expectedCallNo */, bl::cpp::mem_fn( &this_type::onCall2 ) /* nextCall */ );
        }

        void onCall2( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            onHandleCall( result, 2U /* expectedCallNo */, bl::cpp::mem_fn( &this_type::onCall3 ) /* nextCall */ );
        }

        void onCall3( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            onHandleCall( result, 3U /* expectedCallNo */, bl::cpp::mem_fn( &this_type::onCall4 ) /* nextCall */ );
        }

        void onCall4( SAA_in const bl::AsyncOperation::Result& result ) NOEXCEPT
        {
            TEST_ASYNC_TASK_HANDLER_BEGIN_CHK_ASYNC_RESULT()

            /*
             * result.task needs to be checked depending on m_tasksAtEvenCalls
             */

            UTF_REQUIRE( m_tasksAtEvenCalls ? ! result.task : !! result.task );

            UTF_REQUIRE_EQUAL( m_callNo.value(), NO_OF_CALLS );

            UTF_REQUIRE_EQUAL( m_createTaskCalls.value(), NO_OF_CALLS );

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
        static void executeTests( SAA_in Functor&& cb )
        {
            {
                const auto wrapperImpl =
                    AsyncExecutorWrapperCallbackImpl::createInstance();

                {
                    bl::tasks::scheduleAndExecuteInParallel(
                        [ & ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
                        {
                            cb( wrapperImpl, eq );
                        }
                        );
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
                [ & ]
                (
                    SAA_in              const bl::om::ObjPtr< AsyncExecutorWrapperCallbackImpl >&   wrapperImpl,
                    SAA_in              const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&          eq
                ) -> void
                {
                    try
                    {
                        eq -> setOptions( bl::tasks::ExecutionQueue::OptionKeepAll );

                        const std::size_t maxIterations = ( testCancel ? 10U : 40U ) * 1024U;

                        const auto t1 = bl::time::microsec_clock::universal_time();

                        BL_LOG(
                            bl::Logging::debug(),
                            BL_MSG()
                                << "Executing "
                                << ( NO_OF_CALLS * maxIterations )
                                << " async calls on task pool with size of "
                                << maxIterations
                            );

                        UTF_REQUIRE_EQUAL( 0U, g_asyncCalls );

                        try
                        {
                            for( std::size_t i = 0U; i < maxIterations; ++i )
                            {
                                const auto asyncTaskImpl = IMPL::template createInstance< IMPL >( wrapperImpl );

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
                                        << ( NO_OF_CALLS * maxIterations )
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
                                            BL_UNUSED( e );
                                            BL_ASSERT( bl::asio::error::operation_aborted == e.code() );
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
                            UTF_REQUIRE_EQUAL( NO_OF_CALLS * maxIterations, g_asyncCalls );
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
                [ & ]
                (
                    SAA_in              const bl::om::ObjPtr< AsyncExecutorWrapperCallbackImpl >&   wrapperImpl,
                    SAA_in              const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&          eq
                ) -> void
                {
                    const auto testOneTime = [ & ]( SAA_in_opt const bool tasksAtEvenCalls ) -> void
                    {
                        const auto asyncTaskImpl =
                            IMPL::template createInstance< IMPL >( wrapperImpl, tasksAtEvenCalls );

                        asyncTaskImpl -> allowSleeps( true );

                        const auto asyncTask = bl::om::qi< bl::tasks::Task >( asyncTaskImpl );

                        UTF_REQUIRE_EQUAL( 0U, asyncTaskImpl -> asyncCalls() );
                        UTF_REQUIRE_EQUAL( 0U, g_asyncCalls );

                        eq -> push_back( asyncTask );

                        eq -> waitForSuccess( asyncTask );

                        UTF_REQUIRE_EQUAL( NO_OF_CALLS, asyncTaskImpl -> asyncCalls() );
                        UTF_REQUIRE_EQUAL( NO_OF_CALLS, g_asyncCalls );

                        g_asyncCalls = 0U;
                    };

                    testOneTime( true /* tasksAtEvenCalls */ );
                    testOneTime( false /* tasksAtEvenCalls */ );
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

        typedef AsyncExecutorWrapperCallbackImpl                            wrapper_t;

        typedef AsyncTestTaskBaseT< bl::tasks::TaskBase >                   base_type;

        AsyncTestTaskAsyncOnlyT(
            SAA_in              const bl::om::ObjPtr< wrapper_t >&          wrapperImpl,
            SAA_in_opt          const bool                                  tasksAtEvenCalls = coinToss()
            )
            :
            base_type( wrapperImpl, tasksAtEvenCalls )
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

        typedef AsyncExecutorWrapperCallbackImpl                            wrapper_t;

        typedef AsyncTestTaskBaseT< bl::tasks::SimpleTaskBase >             base_type;

        AsyncTestTaskAsyncFastStartT(
            SAA_in              const bl::om::ObjPtr< wrapper_t >&          wrapperImpl,
            SAA_in_opt          const bool                                  tasksAtEvenCalls = coinToss()
            )
            :
            base_type( wrapperImpl, tasksAtEvenCalls )
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

} // asynccb

UTF_AUTO_TEST_CASE( AsyncCB_BasicTests )
{
    using namespace asynccb;
    AsyncTestTaskAsyncOnlyImpl::executeBasicTests< AsyncTestTaskAsyncOnlyImpl >();
}

UTF_AUTO_TEST_CASE( AsyncCB_SmallPerfTests )
{
    using namespace asynccb;
    AsyncTestTaskAsyncOnlyImpl::executePerfTests< AsyncTestTaskAsyncOnlyImpl >( false /* testCancel */ );
}

UTF_AUTO_TEST_CASE( AsyncCB_CancelTests )
{
    using namespace asynccb;
    AsyncTestTaskAsyncFastStartImpl::executePerfTests< AsyncTestTaskAsyncFastStartImpl >( true /* testCancel */ );
}


UTF_AUTO_TEST_CASE( AsyncCB_CancelWithOperationTaskInProgressTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * An async operation which is cancelled while its operation state task is still
     * in progress must release its executor concurrency slot once that task completes
     *
     * The operation drops its task pointer on the cancel path, so the second (terminating)
     * cancellation call the executor task is waiting for can never arrive; if the task is
     * left running in the workers queue then, after maxConcurrentTasks such cancellations,
     * no new async operation can ever start executing
     *
     * The executor below is created with maxConcurrentTasks = 1, so a single leaked slot
     * is enough to stall the second operation
     */

    utest::AsyncTestSignal operationTaskStarted;
    utest::AsyncTestSignal releaseOperationTask;
    utest::AsyncTestSignal firstCallbackCalled;
    utest::AsyncTestSignal secondCallbackCalled;

    cpp::ScalarTypeIniter< bool > secondOperationExecuted;

    cpp::ScalarTypeIniter< bool > firstResultIsFailed;
    cpp::ScalarTypeIniter< bool > firstResultIsCanceled;
    cpp::ScalarTypeIniter< bool > firstResultHasTask;
    cpp::ScalarTypeIniter< Task::State > firstResultTaskState( Task::Created );

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance(
        4U                                          /* threadsCount */,
        om::ObjPtr< TaskControlToken >()            /* controlToken */,
        1U                                          /* maxConcurrentTasks */
        );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation1 = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                []() -> void
                {
                    /*
                     * Never called - this operation state models its execution as a task
                     */

                    UTF_FAIL( "The operation state callback must not be called" );
                },
                [ &operationTaskStarted, &releaseOperationTask ]() -> om::ObjPtr< Task >
                {
                    return SimpleTaskImpl::createInstance< Task >(
                        [ &operationTaskStarted, &releaseOperationTask ]() -> void
                        {
                            operationTaskStarted.signal();

                            ( void ) releaseOperationTask.wait();
                        }
                        );
                }
                )
            );

        asyncExecutor -> asyncBegin(
            operation1,
            [
                &firstCallbackCalled,
                &firstResultIsFailed,
                &firstResultIsCanceled,
                &firstResultHasTask,
                &firstResultTaskState
            ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                /*
                 * The result is only copied out here and asserted on the test thread
                 * below, as a failing REQUIRE on a thread pool worker would terminate
                 * the process instead of failing this case
                 */

                firstResultIsFailed = result.isFailed();
                firstResultIsCanceled = result.isCanceled();
                firstResultHasTask = !! result.task;
                firstResultTaskState = result.task ? result.task -> getState() : Task::Created;

                firstCallbackCalled.signal();
            }
            );

        UTF_REQUIRE( operationTaskStarted.wait() );

        /*
         * Cancel while the operation state task is in progress
         */

        operation1 -> cancel();

        releaseOperationTask.signal();

        UTF_REQUIRE( firstCallbackCalled.wait() );

        /*
         * The executor samples m_stopped before it acts on the pending cancellation, so
         * the operation state task - which was already past its own isCanceled() check
         * when it was marked - must be delivered as a completed task on a success result
         */

        UTF_REQUIRE( firstResultHasTask.value() );
        UTF_REQUIRE_EQUAL( Task::Completed, firstResultTaskState.value() );
        UTF_REQUIRE( ! firstResultIsFailed.value() );
        UTF_REQUIRE( ! firstResultIsCanceled.value() );

        asyncExecutor -> releaseOperation( operation1 );

        /*
         * The concurrency slot must have been released, so the second operation
         * must be able to execute and call its callback
         */

        auto operation2 = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &secondOperationExecuted ]() -> void
                {
                    secondOperationExecuted = true;
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        asyncExecutor -> asyncBegin(
            operation2,
            [ &secondCallbackCalled ]( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                BL_UNUSED( result );

                secondCallbackCalled.signal();
            }
            );

        UTF_REQUIRE( secondCallbackCalled.wait() );
        UTF_REQUIRE( secondOperationExecuted.value() );

        asyncExecutor -> releaseOperation( operation2 );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_DeferredAssertionsRecorderTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * The deferred assertions recorder is what allows the storage and the dispatch
     * invariants to be checked on thread pool worker threads without terminating the
     * process, so the recorder itself must be both thread safe and non-throwing
     *
     * Eight tasks record 100 satisfied predicates each and exactly one of them also
     * records a single violated predicate; the recorder is then asserted on the main
     * test thread, after the tasks have been joined
     */

    utest::DeferredAssertions recorder;

    const std::size_t noOfTasks = 8U;
    const std::size_t noOfRecordsPerTask = 100U;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepNone );

            for( std::size_t i = 0U; i < noOfTasks; ++i )
            {
                const bool recordViolation = ( 0U == i );

                eq -> push_back(
                    SimpleTaskImpl::createInstance< Task >(
                        [ &recorder, recordViolation ]() -> void
                        {
                            for( std::size_t j = 0U; j < noOfRecordsPerTask; ++j )
                            {
                                UTF_RECORD( recorder, true );
                            }

                            if( recordViolation )
                            {
                                UTF_RECORD( recorder, false );
                            }
                        }
                        )
                    );
            }
        }
        );

    UTF_REQUIRE_EQUAL( recorder.failures(), 1U );
    UTF_REQUIRE( cpp::contains( recorder.firstFailure(), "false" ) );
}

UTF_AUTO_TEST_CASE( AsyncCB_ConcurrencySlotBoundTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * The executor bounds the number of operations which can execute concurrently to
     * maxConcurrentTasks and that concurrency slot is owned by the operation's executor
     * task, which stays in the workers queue from asyncBegin until the operation is
     * cancelled or released - i.e. not merely until its callback has returned
     *
     * Beyond the bound the new executor task is simply left in the pending list and it is
     * never scheduled, so the operation state execute() callback cannot have been entered
     *
     * Note that threadsCount must exceed maxConcurrentTasks here - the terminating and the
     * cancellation calls are posted to the completion tasks queue, whose own throttle limit
     * equals threadsCount, and the execute() callbacks below block
     */

    const std::size_t noOfOperations = 3U;

    utest::AsyncTestSignal started[ noOfOperations ];
    utest::AsyncTestSignal release[ noOfOperations ];
    utest::AsyncTestSignal callbackCalled[ noOfOperations ];

    std::atomic< std::size_t > startedCount( 0U );

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance(
        4U                                          /* threadsCount */,
        om::ObjPtr< TaskControlToken >()            /* controlToken */,
        2U                                          /* maxConcurrentTasks */
        );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        om::ObjPtr< AsyncOperation > operations[ noOfOperations ];

        for( std::size_t i = 0U; i < noOfOperations; ++i )
        {
            operations[ i ] = asyncExecutor -> createOperation(
                wrapperImpl -> createOperationState(
                    [ &startedCount, &started, &release, i ]() -> void
                    {
                        ++startedCount;

                        started[ i ].signal();

                        ( void ) release[ i ].wait();
                    },
                    AsyncOperationState::create_task_callback_t()
                    )
                );

            asyncExecutor -> asyncBegin(
                operations[ i ],
                [ &callbackCalled, i ]( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
                {
                    BL_UNUSED( result );

                    callbackCalled[ i ].signal();
                }
                );
        }

        /*
         * Only the first two operations can execute; the third one must be throttled
         */

        UTF_REQUIRE( started[ 0 ].wait() );
        UTF_REQUIRE( started[ 1 ].wait() );

        UTF_REQUIRE_EQUAL( 2U, startedCount.load() );

        UTF_REQUIRE( ! started[ 2 ].wait( 500U ) );

        /*
         * Completing the first operation's callback is not sufficient to free its slot -
         * only releasing (or cancelling) the operation is
         */

        release[ 0 ].signal();

        UTF_REQUIRE( callbackCalled[ 0 ].wait() );

        UTF_REQUIRE( ! started[ 2 ].isSignaled() );

        asyncExecutor -> releaseOperation( operations[ 0 ] );

        UTF_REQUIRE( started[ 2 ].wait() );
        UTF_REQUIRE_EQUAL( 3U, startedCount.load() );

        /*
         * Every operation must be released before the wrapper leaves scope, as disposing
         * the executor while it still has outstanding calls trips a runtime assert
         */

        release[ 1 ].signal();
        release[ 2 ].signal();

        UTF_REQUIRE( callbackCalled[ 1 ].wait() );
        UTF_REQUIRE( callbackCalled[ 2 ].wait() );

        asyncExecutor -> releaseOperation( operations[ 1 ] );
        asyncExecutor -> releaseOperation( operations[ 2 ] );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_ConcurrentAsyncBeginIsRejectedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * A second async call on an operation which is still in flight must be rejected with
     * an exception and it must not overwrite the pending callback
     *
     * The check is made in asyncBeginImpl, outside of the NOEXCEPT requestNewAsyncCall,
     * so that it can throw rather than terminate the process
     */

    utest::AsyncTestSignal executeStarted;
    utest::AsyncTestSignal releaseExecute;
    utest::AsyncTestSignal firstCallbackCalled;
    utest::AsyncTestSignal secondCallbackCalled;

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance( 2U /* threadsCount */ );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &executeStarted, &releaseExecute ]() -> void
                {
                    executeStarted.signal();

                    ( void ) releaseExecute.wait();
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        asyncExecutor -> asyncBegin(
            operation,
            [ &firstCallbackCalled ]( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                BL_UNUSED( result );

                firstCallbackCalled.signal();
            }
            );

        UTF_REQUIRE( executeStarted.wait() );

        UTF_REQUIRE_THROW_MESSAGE(
            asyncExecutor -> asyncBegin(
                operation,
                [ &secondCallbackCalled ]( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
                {
                    BL_UNUSED( result );

                    secondCallbackCalled.signal();
                }
                ),
            UnexpectedException,
            "Another async call is already in progress"
            );

        releaseExecute.signal();

        /*
         * The first callback must still be delivered and the rejected one must never be
         * invoked or stored
         */

        UTF_REQUIRE( firstCallbackCalled.wait() );

        UTF_REQUIRE( ! secondCallbackCalled.isSignaled() );

        /*
         * The operation can only be released once the pending call has completed, as
         * releaseOperation checks that from within a NOEXCEPT block
         */

        asyncExecutor -> releaseOperation( operation );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_CanceledControlTokenAbortsOperationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * When the executor is created with a control token which is already cancelled the
     * operation must be aborted without calling execute() at all
     *
     * Note that this short circuit lives only on the execute() path - an operation state
     * which models its execution as a task is deliberately not aborted by the token, so
     * only the sync path is asserted here
     */

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance();

    controlToken -> requestCancel();

    utest::AsyncTestSignal callbackCalled;

    cpp::ScalarTypeIniter< bool > executed;

    eh::error_code capturedCode;
    cpp::ScalarTypeIniter< bool > capturedIsCanceled;
    cpp::ScalarTypeIniter< bool > capturedIsFailed;
    cpp::ScalarTypeIniter< bool > capturedHasTask;
    cpp::ScalarTypeIniter< bool > capturedHasException;

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance(
        2U                                              /* threadsCount */,
        om::qi< TaskControlToken >( controlToken )      /* controlToken */,
        0U                                              /* maxConcurrentTasks */
        );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &executed ]() -> void
                {
                    executed = true;
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        asyncExecutor -> asyncBegin(
            operation,
            [
                &callbackCalled,
                &capturedCode,
                &capturedIsCanceled,
                &capturedIsFailed,
                &capturedHasTask,
                &capturedHasException
            ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                capturedCode = result.code;
                capturedIsCanceled = result.isCanceled();
                capturedIsFailed = result.isFailed();
                capturedHasTask = !! result.task;
                capturedHasException = !! result.exception;

                callbackCalled.signal();
            }
            );

        UTF_REQUIRE( callbackCalled.wait() );

        UTF_REQUIRE( ! executed.value() );

        UTF_REQUIRE( asio::error::operation_aborted == capturedCode );
        UTF_REQUIRE( capturedIsCanceled.value() );
        UTF_REQUIRE( capturedIsFailed.value() );
        UTF_REQUIRE( ! capturedHasTask.value() );
        UTF_REQUIRE( ! capturedHasException.value() );

        asyncExecutor -> releaseOperation( operation );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_ExecuteExceptionIsDeliveredTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * An exception thrown out of execute() is captured and delivered to the callback as
     * Result::exception, with an empty error code and no task
     *
     * The remaining calls counter is decremented on the failure path too, so the very same
     * operation can be used to start a new async call immediately afterwards
     */

    std::atomic< int > executeCalls( 0 );

    utest::AsyncTestSignal firstCallbackCalled;
    utest::AsyncTestSignal secondCallbackCalled;

    std::exception_ptr capturedEptr;
    cpp::ScalarTypeIniter< bool > capturedHasTask;
    cpp::ScalarTypeIniter< bool > capturedIsFailed;
    cpp::ScalarTypeIniter< bool > capturedIsCanceled;
    cpp::ScalarTypeIniter< bool > secondFailed;

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance( 2U /* threadsCount */ );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &executeCalls ]() -> void
                {
                    if( 1 == ++executeCalls )
                    {
                        BL_THROW(
                            UnexpectedException(),
                            BL_MSG()
                                << "execute failed"
                            );
                    }
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        /*
         * The client callback is invoked from within a NOEXCEPT block, so the result is
         * only copied out here and asserted on the test thread below
         */

        asyncExecutor -> asyncBegin(
            operation,
            [
                &firstCallbackCalled,
                &capturedEptr,
                &capturedHasTask,
                &capturedIsFailed,
                &capturedIsCanceled
            ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                capturedEptr = result.exception;
                capturedHasTask = !! result.task;
                capturedIsFailed = result.isFailed();
                capturedIsCanceled = result.isCanceled();

                firstCallbackCalled.signal();
            }
            );

        UTF_REQUIRE( firstCallbackCalled.wait() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( capturedEptr ),
            UnexpectedException,
            "execute failed"
            );

        UTF_REQUIRE( capturedIsFailed.value() );
        UTF_REQUIRE( ! capturedIsCanceled.value() );
        UTF_REQUIRE( ! capturedHasTask.value() );

        /*
         * The failed operation must be reusable as is
         */

        asyncExecutor -> asyncBegin(
            operation,
            [ &secondCallbackCalled, &secondFailed ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                secondFailed = result.isFailed();

                secondCallbackCalled.signal();
            }
            );

        UTF_REQUIRE( secondCallbackCalled.wait() );
        UTF_REQUIRE( ! secondFailed.value() );

        asyncExecutor -> releaseOperation( operation );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_OperationStateTaskContinuationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * When the operation state models its execution as a task the executor walks the whole
     * continuation chain itself before it invokes the async callback, hands the tail of the
     * chain back in Result::task and marks that tail as completed
     *
     * A failure anywhere in the chain is propagated to the callback through the exception
     * carried by the tail task
     */

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance( 4U /* threadsCount */ );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        {
            /*
             * The full chain must have executed by the time the callback is invoked
             */

            std::atomic< std::size_t > stepsExecuted( 0U );

            utest::AsyncTestSignal callbackCalled;

            cpp::ScalarTypeIniter< std::size_t > capturedSteps;
            cpp::ScalarTypeIniter< bool > capturedIsFailed;
            om::ObjPtrCopyable< Task > capturedResultTask;

            const auto continuationTask = SimpleTaskImpl::createInstance< Task >(
                [ &stepsExecuted ]() -> void
                {
                    ++stepsExecuted;
                }
                );

            auto operation = asyncExecutor -> createOperation(
                wrapperImpl -> createOperationState(
                    []() -> void
                    {
                        /*
                         * Never called - this operation state models its execution as a task
                         */

                        UTF_FAIL( "The operation state callback must not be called" );
                    },
                    [ &stepsExecuted, &continuationTask ]() -> om::ObjPtr< Task >
                    {
                        return SimpleTaskWithContinuation::createInstance< Task >(
                            [ &stepsExecuted, &continuationTask ]() -> om::ObjPtr< Task >
                            {
                                ++stepsExecuted;

                                return om::copy( continuationTask );
                            }
                            );
                    }
                    )
                );

            asyncExecutor -> asyncBegin(
                operation,
                [ &callbackCalled, &stepsExecuted, &capturedSteps, &capturedIsFailed, &capturedResultTask ]
                ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
                {
                    capturedSteps = stepsExecuted.load();
                    capturedIsFailed = result.isFailed();
                    capturedResultTask = result.task;

                    callbackCalled.signal();
                }
                );

            UTF_REQUIRE( callbackCalled.wait() );

            /*
             * Both the head of the chain and its continuation must have run before the
             * callback was invoked
             */

            UTF_REQUIRE_EQUAL( 2U, capturedSteps.value() );

            /*
             * The tail of the chain, not its head, is what is handed back
             */

            UTF_REQUIRE( om::areEqual( capturedResultTask, continuationTask ) );
            UTF_REQUIRE( Task::Completed == capturedResultTask -> getState() );
            UTF_REQUIRE( ! capturedIsFailed.value() );

            asyncExecutor -> releaseOperation( operation );
        }

        {
            /*
             * A continuation which fails propagates its exception through the tail task
             */

            utest::AsyncTestSignal callbackCalled;

            std::exception_ptr capturedEptr;
            cpp::ScalarTypeIniter< bool > capturedIsFailed;

            const auto failingContinuation = SimpleTaskImpl::createInstance< Task >(
                []() -> void
                {
                    BL_THROW(
                        UnexpectedException(),
                        BL_MSG()
                            << "The continuation task has failed"
                        );
                }
                );

            auto operation = asyncExecutor -> createOperation(
                wrapperImpl -> createOperationState(
                    []() -> void
                    {
                        /*
                         * Never called - this operation state models its execution as a task
                         */

                        UTF_FAIL( "The operation state callback must not be called" );
                    },
                    [ &failingContinuation ]() -> om::ObjPtr< Task >
                    {
                        return SimpleTaskWithContinuation::createInstance< Task >(
                            [ &failingContinuation ]() -> om::ObjPtr< Task >
                            {
                                return om::copy( failingContinuation );
                            }
                            );
                    }
                    )
                );

            asyncExecutor -> asyncBegin(
                operation,
                [ &callbackCalled, &capturedEptr, &capturedIsFailed ]
                ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
                {
                    capturedEptr = result.exception;
                    capturedIsFailed = result.isFailed();

                    callbackCalled.signal();
                }
                );

            UTF_REQUIRE( callbackCalled.wait() );

            UTF_REQUIRE( capturedIsFailed.value() );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( capturedEptr ),
                UnexpectedException,
                "The continuation task has failed"
                );

            asyncExecutor -> releaseOperation( operation );
        }
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_CancelBeforeExecuteDeliversAbortedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * The headline guarantee of the AsyncOperation interface is that the callback is always
     * called, and that an operation which was cancelled successfully is reported through
     * Result::code being equal to asio::error::operation_aborted
     *
     * The workers throttle is used to make this deterministic - the first operation occupies
     * the single concurrency slot, so the second operation's executor task is left pending
     * and its execute() cannot have run by the time it is cancelled
     */

    utest::AsyncTestSignal started0;
    utest::AsyncTestSignal release0;
    utest::AsyncTestSignal callback0Called;
    utest::AsyncTestSignal callback1Called;

    cpp::ScalarTypeIniter< bool > executed1;

    cpp::ScalarTypeIniter< bool > captured0IsFailed;

    eh::error_code captured1Code;
    cpp::ScalarTypeIniter< bool > captured1IsCanceled;
    cpp::ScalarTypeIniter< bool > captured1IsFailed;
    cpp::ScalarTypeIniter< bool > captured1HasException;
    cpp::ScalarTypeIniter< bool > captured1HasTask;

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance(
        4U                                          /* threadsCount */,
        om::ObjPtr< TaskControlToken >()            /* controlToken */,
        1U                                          /* maxConcurrentTasks */
        );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation0 = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &started0, &release0 ]() -> void
                {
                    started0.signal();

                    ( void ) release0.wait();
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        asyncExecutor -> asyncBegin(
            operation0,
            [ &callback0Called, &captured0IsFailed ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                captured0IsFailed = result.isFailed();

                callback0Called.signal();
            }
            );

        UTF_REQUIRE( started0.wait() );

        auto operation1 = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &executed1 ]() -> void
                {
                    executed1 = true;
                },
                AsyncOperationState::create_task_callback_t()
                )
            );

        asyncExecutor -> asyncBegin(
            operation1,
            [
                &callback1Called,
                &captured1Code,
                &captured1IsCanceled,
                &captured1IsFailed,
                &captured1HasException,
                &captured1HasTask
            ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                captured1Code = result.code;
                captured1IsCanceled = result.isCanceled();
                captured1IsFailed = result.isFailed();
                captured1HasException = !! result.exception;
                captured1HasTask = !! result.task;

                callback1Called.signal();
            }
            );

        /*
         * The second operation must really be parked behind the first one
         */

        UTF_REQUIRE( ! callback1Called.wait( 500U ) );

        operation1 -> cancel();

        UTF_REQUIRE( callback1Called.wait() );

        UTF_REQUIRE( ! executed1.value() );

        UTF_REQUIRE( asio::error::operation_aborted == captured1Code );
        UTF_REQUIRE( captured1IsCanceled.value() );
        UTF_REQUIRE( captured1IsFailed.value() );
        UTF_REQUIRE( ! captured1HasException.value() );
        UTF_REQUIRE( ! captured1HasTask.value() );

        /*
         * Cancelling one operation must not disturb the other one
         */

        release0.signal();

        UTF_REQUIRE( callback0Called.wait() );
        UTF_REQUIRE( ! captured0IsFailed.value() );

        /*
         * The cancelled operation has already dropped its task pointer, so releasing it
         * skips the pending calls check - this is the documented cancel-then-release order
         */

        asyncExecutor -> releaseOperation( operation0 );
        asyncExecutor -> releaseOperation( operation1 );
    }
}

UTF_AUTO_TEST_CASE( AsyncOperation_ResultSemanticsTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * A pure struct test - no executor, no queue, no threads
     *
     * Result::isCanceled() has four branches and exactly one caller in the repository,
     * utest::AsyncTestTaskSharedBase::wasCanceled(), which ORs it with TaskBase::isCanceled()
     * and therefore masks a broken implementation entirely; the handler prolog macros in
     * TaskBase.h read result.exception and result.code directly and never call it
     *
     * Note that isCanceled() rethrows and catches internally, so every call on an exception
     * carrying Result costs a throw - the table is deliberately kept small
     */

    std::exception_ptr abortedSystemError;
    std::exception_ptr abortedDecorated;
    std::exception_ptr plainFailure;

    try
    {
        BL_THROW_EC(
            eh::error_code( asio::error::operation_aborted ),
            BL_MSG()
                << "aborted"
            );
    }
    catch( std::exception& )
    {
        abortedSystemError = std::current_exception();
    }

    try
    {
        BL_THROW(
            UnexpectedException()
                << eh::errinfo_error_code( eh::error_code( asio::error::operation_aborted ) ),
            BL_MSG()
                << "decorated"
            );
    }
    catch( std::exception& )
    {
        abortedDecorated = std::current_exception();
    }

    try
    {
        BL_THROW(
            UnexpectedException(),
            BL_MSG()
                << "plain"
            );
    }
    catch( std::exception& )
    {
        plainFailure = std::current_exception();
    }

    UTF_REQUIRE( abortedSystemError );
    UTF_REQUIRE( abortedDecorated );
    UTF_REQUIRE( plainFailure );

    {
        /*
         * (1) Default constructed - neither failed nor canceled, and no task
         */

        const AsyncOperation::Result result;

        UTF_REQUIRE( ! result.isFailed() );
        UTF_REQUIRE( ! result.isCanceled() );
        UTF_REQUIRE( ! result.task );
    }

    {
        /*
         * (2) The code only branch
         */

        const AsyncOperation::Result result( nullptr, eh::error_code( asio::error::operation_aborted ) );

        UTF_REQUIRE( result.isFailed() );
        UTF_REQUIRE( result.isCanceled() );
    }

    {
        /*
         * (3) A code which is not operation_aborted is a failure but not a cancellation
         */

        const AsyncOperation::Result result( nullptr, eh::error_code( asio::error::connection_refused ) );

        UTF_REQUIRE( result.isFailed() );
        UTF_REQUIRE( ! result.isCanceled() );
    }

    {
        /*
         * (4) An eh::system_error whose code() is operation_aborted
         */

        const AsyncOperation::Result result( abortedSystemError );

        UTF_REQUIRE( result.isFailed() );
        UTF_REQUIRE( result.isCanceled() );
    }

    {
        /*
         * (5) A non system exception which only carries eh::errinfo_error_code - dropping
         * that lookup, or reordering the catch blocks, would silently lose this one
         */

        const AsyncOperation::Result result( abortedDecorated );

        UTF_REQUIRE( result.isFailed() );
        UTF_REQUIRE( result.isCanceled() );
    }

    {
        /*
         * (6) An undecorated exception is a failure and nothing more
         */

        const AsyncOperation::Result result( plainFailure );

        UTF_REQUIRE( result.isFailed() );
        UTF_REQUIRE( ! result.isCanceled() );
    }

    {
        /*
         * The task member is an om::ObjPtrCopyable populated from an rvalue om::ObjPtr
         */

        const AsyncOperation::Result result(
            plainFailure,
            eh::error_code(),
            om::qi< Task >( SimpleTaskImpl::createInstance() )
            );

        UTF_REQUIRE( !! result.task );
    }
}

UTF_AUTO_TEST_CASE( AsyncCB_CreateTaskExceptionIsDeliveredTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * An exception thrown by createTask() is caught by ExecutorTaskT::scheduleCall, which
     * synthesises a tasks::SimpleCompletedTask carrying it; the executor then treats that
     * task as a normally finished operation state task, so the client is handed
     * Result{ exception = <the throw>, code = {}, task = <the synthetic task> } and
     * execute() is never called
     *
     * Dropping the synthetic task branch would leave m_operationStateTaskInProgress null and
     * lose the exception entirely - i.e. the client would be told the operation succeeded
     */

    cpp::ScalarTypeIniter< bool > executed;

    utest::AsyncTestSignal callbackCalled;

    std::exception_ptr capturedEptr;
    cpp::ScalarTypeIniter< bool > capturedHasTask;
    cpp::ScalarTypeIniter< bool > capturedTaskIsFailed;
    cpp::ScalarTypeIniter< bool > capturedIsFailed;
    cpp::ScalarTypeIniter< bool > capturedIsCanceled;

    Task::State capturedTaskState = Task::Created;

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance( 2U /* threadsCount */ );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        auto operation = asyncExecutor -> createOperation(
            wrapperImpl -> createOperationState(
                [ &executed ]() -> void
                {
                    executed = true;
                },
                AsyncOperationState::create_task_callback_t(
                    []() -> om::ObjPtr< Task >
                    {
                        BL_THROW(
                            UnexpectedException(),
                            BL_MSG()
                                << "createTask failed"
                            );
                    }
                    )
                )
            );

        /*
         * The client callback is invoked from within a NOEXCEPT block, so the result is only
         * copied out here and asserted on the test thread below
         */

        asyncExecutor -> asyncBegin(
            operation,
            [
                &callbackCalled,
                &capturedEptr,
                &capturedHasTask,
                &capturedTaskIsFailed,
                &capturedTaskState,
                &capturedIsFailed,
                &capturedIsCanceled
            ]
            ( SAA_in const AsyncOperation::Result& result ) NOEXCEPT -> void
            {
                capturedEptr = result.exception;
                capturedHasTask = !! result.task;

                if( result.task )
                {
                    capturedTaskState = result.task -> getState();
                    capturedTaskIsFailed = result.task -> isFailed();
                }

                capturedIsFailed = result.isFailed();
                capturedIsCanceled = result.isCanceled();

                callbackCalled.signal();
            }
            );

        UTF_REQUIRE( callbackCalled.wait() );

        asyncExecutor -> releaseOperation( operation );
    }

    UTF_REQUIRE( ! executed.value() );

    /*
     * Checked separately because cpp::safeRethrowException( nullptr ) is a BL_RIP_MSG, so a
     * regression which lost the exception would abort the binary instead of failing the case
     */

    UTF_REQUIRE( capturedEptr );

    UTF_REQUIRE_THROW_MESSAGE(
        cpp::safeRethrowException( capturedEptr ),
        UnexpectedException,
        "createTask failed"
        );

    UTF_REQUIRE( capturedHasTask.value() );
    UTF_REQUIRE( Task::Completed == capturedTaskState );
    UTF_REQUIRE( capturedTaskIsFailed.value() );

    UTF_REQUIRE( capturedIsFailed.value() );
    UTF_REQUIRE( ! capturedIsCanceled.value() );
}

UTF_AUTO_TEST_CASE( AsyncCB_OperationAndStatePoolingTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace asynccb;

    /*
     * The pooling of operations and of operation states, and the two guards which make it
     * safe - none of which any existing case observes, even though the reuse happens on every
     * single run
     *
     * Losing the freed() guard turns a use after release into silent corruption of a
     * *different* client's operation; losing the m_impl reset in releaseResources() recreates
     * the reference cycle the header explicitly documents, which shows up only as the
     * "Objects leaked!" teardown message and does not fail a run
     */

    const auto wrapperImpl = AsyncExecutorWrapperCallbackImpl::createInstance( 2U /* threadsCount */ );

    {
        const auto& asyncExecutor = wrapperImpl -> asyncExecutor();

        /*
         * Block A - the operations pool and the released object guard
         */

        {
            /*
             * Held as an om::ObjPtr lvalue so the lvalue createOperation( const ObjPtr& )
             * forwarder in AsyncExecutor.h is the overload which is exercised
             */

            const om::ObjPtr< AsyncOperationState > stateLvalue = wrapperImpl -> createOperationState(
                []() -> void
                {
                },
                AsyncOperationState::create_task_callback_t()
                );

            auto operation = asyncExecutor -> createOperation( stateLvalue );

            const auto operationCopy = om::copy( operation );

            asyncExecutor -> releaseOperation( operation );

            /*
             * releaseOperation takes its argument by reference precisely so it can clear it
             */

            UTF_REQUIRE( ! operation );

            UTF_REQUIRE_THROW_MESSAGE(
                asyncExecutor -> asyncBegin(
                    operationCopy,
                    []( SAA_in const AsyncOperation::Result& ) NOEXCEPT -> void
                    {
                    }
                    ),
                UnexpectedException,
                "Attempting to execute async operation on released object"
                );

            /*
             * The very next operation comes straight back out of the pool
             */

            auto operation2 = asyncExecutor -> createOperation(
                wrapperImpl -> createOperationState(
                    []() -> void
                    {
                    },
                    AsyncOperationState::create_task_callback_t()
                    )
                );

            UTF_REQUIRE( om::areEqual( operation2, operationCopy ) );

            asyncExecutor -> releaseOperation( operation2 );

            UTF_REQUIRE( ! operation2 );
        }

        /*
         * Block B - the operation states pool and the cycle breaking reset in
         * releaseResources()
         */

        {
            utest::AsyncTestSignal callbackCalled;

            const om::ObjPtr< AsyncOperationStateImpl > state1 =
                wrapperImpl -> createOperationState< AsyncOperationStateImpl >(
                    []() -> void
                    {
                    },
                    AsyncOperationState::create_task_callback_t()
                    );

            UTF_REQUIRE( state1 -> impl() );

            auto operation = asyncExecutor -> createOperation( om::qi< AsyncOperationState >( state1 ) );

            asyncExecutor -> asyncBegin(
                operation,
                [ &callbackCalled ]( SAA_in const AsyncOperation::Result& ) NOEXCEPT -> void
                {
                    callbackCalled.signal();
                }
                );

            UTF_REQUIRE( callbackCalled.wait() );

            asyncExecutor -> releaseOperation( operation );

            /*
             * dispose() flushes the completion queue and joins every executor thread, so the
             * terminating call - and with it releaseResources() - has certainly run by the
             * time it returns; this is the barrier which makes the two assertions below
             * deterministic
             */

            wrapperImpl -> dispose();

            UTF_REQUIRE( ! state1 -> impl() );

            /*
             * createOperationState only touches the shared state pool, so it is safe after
             * dispose() - and it must hand back the very object which was recycled
             */

            UTF_REQUIRE(
                om::areEqual(
                    state1,
                    wrapperImpl -> createOperationState< AsyncOperationStateImpl >(
                        []() -> void
                        {
                        },
                        AsyncOperationState::create_task_callback_t()
                        )
                    )
                );
        }
    }
}
