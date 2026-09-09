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

#include <baselib/tasks/TasksUtils.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/ExecutionQueueNotify.h>
#include <baselib/tasks/ExecutionQueueNotifyBase.h>

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/Logging.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

/************************************************************************
 * Tasks library code tests (continued)
 *
 * Note that this header is included after TestTasks.h in the same translation unit,
 * so the file local helpers declared in the anonymous namespace of TestTasks.h - in
 * particular ExecutionQueueCompletionControl, createControlledCompletionTask,
 * ExecutionQueueNotificationRecorderImpl and ExecutionQueueThrottleObserverImpl -
 * are visible here and are deliberately reused rather than duplicated
 */

namespace
{
    /**
     * @brief Creates an exception_ptr which carries the very same system exception the
     * task layer uses to report cancellation
     */

    std::exception_ptr makeOperationAbortedExceptionPtr()
    {
        std::exception_ptr eptr;

        try
        {
            BL_THROW_EC( bl::asio::error::operation_aborted, BL_SYSTEM_ERROR_DEFAULT_MSG );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        return eptr;
    }

    /**
     * @brief Creates an ExternalCompletionTaskIf task which is wired with a cancel callback
     *
     * The 'scheduled' parameter is what the interface callback returns, i.e. 'true' models an
     * operation which was scheduled asynchronously and 'false' one which has already completed
     * synchronously, which is exactly the distinction the cancel callback is gated on
     *
     * Note that this is deliberately a separate helper - the signature of the existing
     * createControlledCompletionTask( ... ) must not change
     */

    bl::om::ObjPtr< bl::tasks::ExternalCompletionTaskIfImpl > createControlledCompletionTaskIf(
        SAA_inout               ExecutionQueueCompletionControl&                    control,
        SAA_in                  const bool                                          scheduled,
        SAA_inout               std::atomic< std::size_t >&                         cancelCounter,
        SAA_inout               std::atomic< std::size_t >&                         ifCallbackInvocations,
        SAA_inout_opt           bl::tasks::CompletionCallback*                      onReadyOut = nullptr
        )
    {
        return bl::tasks::ExternalCompletionTaskIfImpl::createInstance(
            [ &control, scheduled, &ifCallbackInvocations, onReadyOut ](
                SAA_in const bl::tasks::CompletionCallback& completionCallback
                )
                -> bool
            {
                ++ifCallbackInvocations;

                if( onReadyOut )
                {
                    /*
                     * This is written before control.scheduled( ... ) takes the control lock
                     * and the test thread only reads it after waitUntilScheduled( ... ) has
                     * acquired that same lock, so the hand-off is properly ordered
                     */

                    *onReadyOut = completionCallback;
                }

                control.scheduled( completionCallback );

                return scheduled;
            },
            [ &cancelCounter ]() -> void
            {
                /*
                 * The cancel callback is invoked while the task lock is held, so it must not
                 * throw and must not touch the task
                 */

                ++cancelCounter;
            }
            );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ExternalCompletionTaskCancelTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExternalCompletionTaskIfT::requestCancel() marks the cancel latch unconditionally, but
     * it only invokes the cancel callback when all three of the following hold: a cancel
     * callback was supplied at construction, the interface callback reported the operation as
     * scheduled ( m_scheduled ) and the task is still Running
     *
     * The existing Tasks_ExternalCompletionTaskTests constructs its tasks with a single
     * argument, so no cancel callback is ever supplied there and all three conditions are
     * dead in the suite, while HttpServerBackendMessagingBridge ships a real one in production
     *
     * The sequencing below is deterministic and needs no sleeps: onExecute() holds the task
     * lock across the whole interface callback and the m_scheduled assignment ( see
     * BL_TASKS_HANDLER_BEGIN ), so a requestCancel() issued once waitUntilScheduled( ... ) has
     * returned necessarily blocks on that lock and then observes the settled state
     */

    {
        /*
         * A - asynchronous: the operation was scheduled and the task is still Running, so the
         * cancel callback fires exactly once
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
            );

        ExecutionQueueCompletionControl control;

        std::atomic< std::size_t > cancelCount( 0U );
        std::atomic< std::size_t > ifCallbackInvocations( 0U );

        CompletionCallback onReady;

        const auto taskImpl = createControlledCompletionTaskIf(
            control,
            true                                            /* scheduled */,
            cancelCount,
            ifCallbackInvocations,
            &onReady
            );

        const auto task = om::qi< Task >( taskImpl );

        eq -> push_back( task );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );

        task -> requestCancel();

        UTF_REQUIRE_EQUAL( 1U, cancelCount.load() );

        /*
         * A real cancel callback aborts the external operation asynchronously and the
         * operation then completes the task with operation_aborted - that is what is
         * replayed here through the completion callback which was handed to us
         */

        UTF_REQUIRE( onReady );

        onReady( makeOperationAbortedExceptionPtr() );

        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_EQUAL( 1U, ifCallbackInvocations.load() );
        UTF_REQUIRE( task -> isFailed() );

        UTF_REQUIRE_THROW_ERROR_CODE(
            cpp::safeRethrowException( task -> exception() ),
            bl::SystemException,
            bl::asio::error::operation_aborted
            );

        UTF_REQUIRE( ! taskImpl -> completedSynchronously() );
    }

    {
        /*
         * B - synchronous: the interface callback reports the operation as completed
         * synchronously, so neither m_scheduled nor Running == m_state holds by the time
         * requestCancel() runs and the cancel callback must not fire at all
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
            );

        ExecutionQueueCompletionControl control;

        std::atomic< std::size_t > cancelCount( 0U );
        std::atomic< std::size_t > ifCallbackInvocations( 0U );

        const auto taskImpl = createControlledCompletionTaskIf(
            control,
            false                                           /* scheduled */,
            cancelCount,
            ifCallbackInvocations
            );

        const auto task = om::qi< Task >( taskImpl );

        eq -> push_back( task );
        eq -> flush();

        UTF_REQUIRE_EQUAL( 1U, ifCallbackInvocations.load() );
        UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
        UTF_REQUIRE( taskImpl -> completedSynchronously() );

        task -> requestCancel();

        UTF_REQUIRE_EQUAL( 0U, cancelCount.load() );
        UTF_REQUIRE( taskImpl -> completedSynchronously() );
    }

    {
        /*
         * C - pre-cancel: TaskBaseT::scheduleNothrow() short-circuits with operation_aborted
         * before it ever calls scheduleTask(), so onExecute() never runs, the interface
         * callback is never invoked and there is nothing for the cancel callback to cancel
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
            );

        ExecutionQueueCompletionControl control;

        std::atomic< std::size_t > cancelCount( 0U );
        std::atomic< std::size_t > ifCallbackInvocations( 0U );

        const auto taskImpl = createControlledCompletionTaskIf(
            control,
            true                                            /* scheduled */,
            cancelCount,
            ifCallbackInvocations
            );

        const auto task = om::qi< Task >( taskImpl );

        task -> requestCancel();

        eq -> push_back( task );
        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_EQUAL( 0U, ifCallbackInvocations.load() );
        UTF_REQUIRE_EQUAL( 0U, cancelCount.load() );
        UTF_REQUIRE( task -> isFailed() );

        UTF_REQUIRE_THROW_ERROR_CODE(
            cpp::safeRethrowException( task -> exception() ),
            bl::SystemException,
            bl::asio::error::operation_aborted
            );
    }

    {
        /*
         * D - idempotency: requestCancel() does no de-duplication of its own, so two calls
         * made inside the asynchronous window invoke the cancel callback TWICE
         *
         * This is pinned deliberately because the fan-out cancellation paths - TcpBaseTasks
         * and SimpleTaskControlToken - can call requestCancel() on the same task more than
         * once, so de-duplication is the caller's job and not the task's
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
            );

        ExecutionQueueCompletionControl control;

        std::atomic< std::size_t > cancelCount( 0U );
        std::atomic< std::size_t > ifCallbackInvocations( 0U );

        CompletionCallback onReady;

        const auto taskImpl = createControlledCompletionTaskIf(
            control,
            true                                            /* scheduled */,
            cancelCount,
            ifCallbackInvocations,
            &onReady
            );

        const auto task = om::qi< Task >( taskImpl );

        eq -> push_back( task );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );

        task -> requestCancel();
        task -> requestCancel();

        UTF_REQUIRE_EQUAL( 2U, cancelCount.load() );

        UTF_REQUIRE( onReady );

        onReady( makeOperationAbortedExceptionPtr() );

        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE( task -> isFailed() );

        /*
         * Once the task has completed the Running == m_state gate closes, so any further
         * cancellation request is a no-op
         */

        task -> requestCancel();

        UTF_REQUIRE_EQUAL( 2U, cancelCount.load() );
    }
}

UTF_AUTO_TEST_CASE( Tasks_WaitForSuccessFilteringTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExecutionQueue::waitForSuccess( task, cancel ) and TasksUtils::waitForSuccessOrCancel(
     * eq, task ) apply two *different* operation_aborted filters and the difference is what
     * this case pins down
     *
     * The queue level helper only swallows a SystemException carrying operation_aborted when
     * it was asked to cancel ( cancel == true ); waitForSuccessOrCancel() calls it with
     * cancel == false - so the queue level filter does not fire - and then applies its own,
     * unconditional, operation_aborted filter
     *
     * The very same aborted task is therefore swallowed by waitForSuccessOrCancel() and
     * rethrown by waitForSuccess( task, false ) - see sub-cases (b) and (c) below
     *
     * Every task is completed by flushNoThrowIfFailed() *before* the wait, so it is already
     * in the ready queue and waitInternal() takes the TaskInfo::Ready early exit path - the
     * case is fully deterministic and has no timing dependency
     */

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
        );

    {
        /*
         * (a) an aborted task and cancel == true - the queue level filter fires
         */

        const auto aborted = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

        aborted -> requestCancel();

        eq -> push_back( aborted );
        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE( aborted -> isFailed() );

        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( aborted, true /* cancel */ ) );

        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (b) the identical task with cancel == false - the filter is gated on 'cancel', so
         * the cancellation exception is rethrown to the caller
         */

        const auto aborted2 = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

        aborted2 -> requestCancel();

        eq -> push_back( aborted2 );
        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE( aborted2 -> isFailed() );

        UTF_REQUIRE_THROW_ERROR_CODE(
            eq -> waitForSuccess( aborted2, false /* cancel */ ),
            bl::SystemException,
            bl::asio::error::operation_aborted
            );

        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (c) the identical task again, but through TasksUtils::waitForSuccessOrCancel() -
         * same input, different helper, different outcome: nothing is thrown here even though
         * (b) above threw for exactly the same task shape
         *
         * Note that the exception is filtered for the *caller* only - it is not erased from
         * the task, which still reports itself as failed
         */

        const auto aborted3 = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

        aborted3 -> requestCancel();

        eq -> push_back( aborted3 );
        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_NO_THROW( waitForSuccessOrCancel( eq, aborted3 ) );

        UTF_REQUIRE( aborted3 -> isFailed() );
        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (d) a genuine failure which is not an eh::system_error at all is never swallowed,
         * not even when cancelling
         */

        const auto failed = eq -> push_back(
            []() -> void
            {
                BL_CHK(
                    false,
                    false,
                    BL_MSG()
                        << "genuine failure"
                    );
            }
            );

        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_THROW_MESSAGE(
            eq -> waitForSuccess( failed, true /* cancel */ ),
            bl::UnexpectedException,
            "genuine failure"
            );

        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (e) a system error which is *not* operation_aborted must survive both filters -
         * broadening either of them to any eh::system_error would make a real connection
         * failure during shutdown disappear
         */

        const auto refused = eq -> push_back(
            []() -> void
            {
                BL_THROW_EC( bl::asio::error::connection_refused, BL_SYSTEM_ERROR_DEFAULT_MSG );
            }
            );

        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_THROW_ERROR_CODE(
            waitForSuccessOrCancel( eq, refused ),
            bl::SystemException,
            bl::asio::error::connection_refused
            );

        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (f) a plain non-system_error failure is not caught by waitForSuccessOrCancel()'s
         * catch clause at all
         */

        const auto plain = eq -> push_back(
            []() -> void
            {
                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "plain"
                    );
            }
            );

        eq -> flushNoThrowIfFailed();

        UTF_REQUIRE_THROW_MESSAGE(
            waitForSuccessOrCancel( eq, plain ),
            bl::UnexpectedException,
            "plain"
            );

        UTF_REQUIRE( eq -> isEmpty() );
    }

    {
        /*
         * (g) the shutdown shape which the helper actually exists for - cancelling a running
         * timer task and waiting for it - must complete cleanly
         *
         * Note that SimpleTimerTask binds its timer to the default thread pool aio service,
         * so this sub-case can only live in a test module which has the global default pools
         */

        const auto timerTask = SimpleTimerTask::createInstance< Task >(
            []() -> bool
            {
                return true;
            },
            time::seconds( 10 )                             /* duration */,
            time::seconds( 10 )                             /* initDelay */
            );

        eq -> push_back( timerTask );

        UTF_REQUIRE_NO_THROW( cancelAndWaitForSuccess( eq, timerTask ) );

        UTF_REQUIRE_EQUAL( Task::Completed, timerTask -> getState() );
        UTF_REQUIRE( ! timerTask -> isFailed() );
        UTF_REQUIRE( eq -> isEmpty() );
    }
}

namespace
{
    /**
     * @brief The behaviour which the onTaskStoppedNothrow() override under test exhibits
     */

    enum RemapTaskMode
    {
        RemapTaskModeDefault,
        RemapTaskModeReplace,
        RemapTaskModeSuppress,
        RemapTaskModeExpected,
    };

    /**
     * @brief A named task which always fails and whose onTaskStoppedNothrow() override
     * exercises each of the three things the base class contract allows the hook to do
     *
     * The task must be named - notifyReadyImpl() gates the whole logging block on
     * ( ! m_name.empty() ), so an unnamed task logs nothing at all
     */

    template
    <
        typename E = void
    >
    class RemapTaskT : public bl::tasks::SimpleTaskBase
    {
        BL_DECLARE_OBJECT_IMPL( RemapTaskT )

    protected:

        typedef bl::tasks::SimpleTaskBase                                   base_type;

        const RemapTaskMode                                                 m_mode;

        RemapTaskT( SAA_in const RemapTaskMode mode )
            :
            m_mode( mode )
        {
            base_type::m_name = "MyRemapTask";
        }

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "original"
                );

            BL_TASKS_HANDLER_END()
        }

        virtual auto onTaskStoppedNothrow(
            SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
            SAA_inout_opt           bool*                                   isExpectedException = nullptr
            ) NOEXCEPT
            -> std::exception_ptr OVERRIDE
        {
            /*
             * Note that this hook is invoked while the task lock is held, so it must never
             * block and must never call back into the task or into its execution queue
             */

            std::exception_ptr eptr = eptrIn;

            BL_NOEXCEPT_BEGIN()

            switch( m_mode )
            {
                default:
                    break;

                case RemapTaskModeReplace:
                    eptr = BL_MAKE_EXCEPTION_PTR(
                        bl::TimeoutException(),
                        BL_MSG()
                            << "remapped"
                        );
                    break;

                case RemapTaskModeSuppress:
                    eptr = nullptr;
                    break;

                case RemapTaskModeExpected:
                    if( isExpectedException )
                    {
                        *isExpectedException = true;
                    }
                    break;
            }

            BL_NOEXCEPT_END()

            return base_type::onTaskStoppedNothrow( eptr, isExpectedException );
        }
    };

    typedef bl::om::ObjectImpl< RemapTaskT<> > RemapTaskImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_OnTaskStoppedRemapTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * TaskBaseT::notifyReadyImpl() does eptr = onTaskStoppedNothrow( eptr, &isExpected ) and
     * then uses the returned pointer both for logging and - because m_exception is still null
     * for a task which threw out of onExecute() - as the task's final exception
     *
     * The hook can therefore replace the exception ( SimpleHttpTask and TcpBaseTasks both rely
     * on this ), suppress it entirely, or flip *isExpectedException to stop the debug dump.
     * None of the three is covered anywhere today
     */

    const auto dumpLine = "Task 'MyRemapTask' failed with the following exception:";

    {
        /*
         * 1 - replace: the hook's return value becomes the task's exception
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto task = om::qi< Task >( RemapTaskImpl::createInstance( RemapTaskModeReplace ) );

        cpp::SafeOutputStringStream os;

        {
            const Logging::line_logger_t ll(
                cpp::bind(
                    &Logging::defaultLineLoggerWithLock,
                    _1,
                    _2,
                    _3,
                    _4,
                    true /* addNewLine */,
                    cpp::ref( os )
                    )
                );

            Logging::LineLoggerPusher pushLogger( ll );

            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

            eq -> push_back( task );
            eq -> flushNoThrowIfFailed();
        }

        UTF_REQUIRE( task -> isFailed() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( task -> exception() ),
            bl::TimeoutException,
            "remapped"
            );
    }

    {
        /*
         * 2 - suppress: returning nullptr from the hook makes a task which threw out of
         * onExecute() complete as a success, and a non-throwing flush() then succeeds
         *
         * This is the strongest and the most surprising part of the contract
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto task = om::qi< Task >( RemapTaskImpl::createInstance( RemapTaskModeSuppress ) );

        cpp::SafeOutputStringStream os;

        {
            const Logging::line_logger_t ll(
                cpp::bind(
                    &Logging::defaultLineLoggerWithLock,
                    _1,
                    _2,
                    _3,
                    _4,
                    true /* addNewLine */,
                    cpp::ref( os )
                    )
                );

            Logging::LineLoggerPusher pushLogger( ll );

            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

            eq -> push_back( task );

            UTF_REQUIRE_NO_THROW( eq -> flush() );
        }

        UTF_REQUIRE( ! task -> isFailed() );
        UTF_REQUIRE( ! task -> exception() );
    }

    {
        /*
         * 3 - expected: the hook returns the original exception but marks it as expected,
         * which must suppress the debug dump
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto task = om::qi< Task >( RemapTaskImpl::createInstance( RemapTaskModeExpected ) );

        cpp::SafeOutputStringStream os;

        {
            const Logging::line_logger_t ll(
                cpp::bind(
                    &Logging::defaultLineLoggerWithLock,
                    _1,
                    _2,
                    _3,
                    _4,
                    true /* addNewLine */,
                    cpp::ref( os )
                    )
                );

            Logging::LineLoggerPusher pushLogger( ll );

            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

            eq -> push_back( task );
            eq -> flushNoThrowIfFailed();
        }

        UTF_REQUIRE( task -> isFailed() );
        UTF_REQUIRE( ! str::contains( os.str(), dumpLine ) );
    }

    {
        /*
         * 4 - the control: the identical task with *isExpectedException left untouched *does*
         * produce the dump line, so what block 3 measures is the plumb-through and nothing else
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto task = om::qi< Task >( RemapTaskImpl::createInstance( RemapTaskModeDefault ) );

        cpp::SafeOutputStringStream os;

        {
            const Logging::line_logger_t ll(
                cpp::bind(
                    &Logging::defaultLineLoggerWithLock,
                    _1,
                    _2,
                    _3,
                    _4,
                    true /* addNewLine */,
                    cpp::ref( os )
                    )
                );

            Logging::LineLoggerPusher pushLogger( ll );

            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

            eq -> push_back( task );
            eq -> flushNoThrowIfFailed();
        }

        UTF_REQUIRE( task -> isFailed() );
        UTF_REQUIRE( str::contains( os.str(), dumpLine ) );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( task -> exception() ),
            bl::UnexpectedException,
            "original"
            );
    }

    {
        /*
         * 5 - the separate 'success:' name branch of notifyReadyImpl(), which eight production
         * task classes depend on for their completion diagnostics
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto task = SimpleTaskImpl::createInstance< Task >(
            []() -> void
            {
            },
            "success:MyTask"
            );

        cpp::SafeOutputStringStream os;

        {
            const Logging::line_logger_t ll(
                cpp::bind(
                    &Logging::defaultLineLoggerWithLock,
                    _1,
                    _2,
                    _3,
                    _4,
                    true /* addNewLine */,
                    cpp::ref( os )
                    )
                );

            Logging::LineLoggerPusher pushLogger( ll );

            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

            eq -> push_back( task );
            eq -> flush();
        }

        UTF_REQUIRE( ! task -> isFailed() );
        UTF_REQUIRE( str::contains( os.str(), "Task 'success:MyTask' completed successfully" ) );
    }
}

UTF_AUTO_TEST_CASE( Tasks_RetryableWrapperTaskCancelTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * Cancellation stops the retry loop: RetryableWrapperTaskT keeps a cancel latch of its own
     * because ForwarderTaskBase::requestCancel() only reaches the task which happens to be
     * wrapped at the time of the call, while the operation this task represents is the whole
     * retry sequence
     *
     * Once the latch is set continuationTask() creates no further work task no matter which
     * task is wrapped - the work task which has just failed, or the retry sleep timer, whose
     * SimpleTimerTaskT::run() returns time::neg_infin *without* an exception when cancelled
     *
     * Tasks_RetryableWrapperTaskCancelStressTests covers the lock ordering race and asserts
     * only isFailed(), which holds whether or not cancellation stops the retry loop; this
     * case is the only one which can tell the two apart
     */

    const std::size_t maxRetryCount = 5U;
    const auto retryTimeout = time::seconds( 2 );

    std::atomic< std::size_t > factoryCalls( 0U );
    utest::TestSignal firstExecution;

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
        );

    const auto taskImpl = RetryableWrapperTask::createInstance(
        [ &factoryCalls, &firstExecution ]() -> om::ObjPtr< Task >
        {
            ++factoryCalls;

            return om::qi< Task >(
                SimpleTaskImpl::createInstance(
                    [ &firstExecution ]() -> void
                    {
                        firstExecution.signal();

                        BL_THROW(
                            bl::UnexpectedException(),
                            BL_MSG()
                                << "consistent error"
                            );
                    }
                    )
                );
        },
        maxRetryCount,
        cpp::copy( retryTimeout )                           /* retryTimeout */
        );

    const auto task = om::qi< Task >( taskImpl );

    eq -> push_back( task );

    /*
     * The first work task has provably started, so the wrapper is either still in it or has
     * just entered the first retry sleep
     */

    UTF_REQUIRE( firstExecution.wait() );

    task -> requestCancel();

    const auto started = time::microsec_clock::universal_time();

    eq -> flushNoThrowIfFailed();

    const auto elapsed = time::microsec_clock::universal_time() - started;

    /*
     * The constructor's own taskFactory() call is the first one; the cancellation request can
     * race with a retry which was already decided, so at most one more work task is created
     * and never the full maxRetryCount the uncancelled loop would run
     */

    UTF_REQUIRE( factoryCalls.load() <= 2U );

    UTF_REQUIRE( task -> isFailed() );

    /*
     * The final exception is the work task's own error when the cancellation landed on the
     * work task, or operation_aborted when it landed on the retry sleep timer, which
     * completes without an exception of its own
     */

    bool exceptionAsExpected = false;

    try
    {
        cpp::safeRethrowException( task -> exception() );
    }
    catch( bl::UnexpectedException& e )
    {
        exceptionAsExpected = str::contains( eh::diagnostic_information( e ), "consistent error" );
    }
    catch( bl::SystemException& e )
    {
        exceptionAsExpected = ( asio::error::operation_aborted == e.code() );
    }

    UTF_REQUIRE( exceptionAsExpected );

    /*
     * Only the retry sleep which was already in flight when the cancellation landed can still
     * elapse, so the whole run finishes in well under the ( maxRetryCount - 1 ) sleeps of
     * retryTimeout each which the uncancelled loop would take
     */

    UTF_REQUIRE( elapsed < retryTimeout * static_cast< int >( maxRetryCount - 1U ) );
}

UTF_AUTO_TEST_CASE( Tasks_RetryableWrapperTaskCancelDuringRetrySleepTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * The companion of Tasks_RetryableWrapperTaskCancelTests above which pins the case the
     * forwarding requestCancel() alone cannot handle: the cancellation lands while the retry
     * sleep timer is the wrapped task
     *
     * The timer is cancelled and completes without an exception, so nothing but the wrapper's
     * own cancel latch can stop the loop from creating the next work task
     *
     * The hand-off is made deterministic rather than timed. The verification callback runs
     * inside continuationTask() under the wrapper's own lock, at the moment the work task has
     * completed and the retry decision is being taken, so signalling from it and cancelling
     * from the test thread orders the two: requestCancel() blocks on that same lock until the
     * sleep timer has been swapped in, and therefore always lands on the timer. The retry
     * timeout is a long one so that the sleep cannot elapse on a loaded machine, and the case
     * still finishes in well under a second because cancelling aborts the timer
     */

    const std::size_t maxRetryCount = 5U;
    const auto retryTimeout = time::seconds( 30 );

    std::atomic< std::size_t > factoryCalls( 0U );
    utest::TestSignal retryDecisionReached;

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
        );

    const auto taskImpl = RetryableWrapperTask::createInstance(
        [ &factoryCalls ]() -> om::ObjPtr< Task >
        {
            ++factoryCalls;

            return om::qi< Task >(
                SimpleTaskImpl::createInstance(
                    []() -> void
                    {
                        BL_THROW(
                            bl::UnexpectedException(),
                            BL_MSG()
                                << "consistent error"
                            );
                    }
                    )
                );
        },
        maxRetryCount,
        cpp::copy( retryTimeout )                           /* retryTimeout */,
        [ &retryDecisionReached ]( SAA_in const om::ObjPtr< Task >& wrappedTask ) -> bool
        {
            retryDecisionReached.signal();

            /*
             * The default verification, so the wrapper retries exactly as it would without
             * a callback at all
             */

            return ! wrappedTask -> isFailed();
        }
        );

    const auto task = om::qi< Task >( taskImpl );

    const auto started = time::microsec_clock::universal_time();

    eq -> push_back( task );

    UTF_REQUIRE( retryDecisionReached.wait() );

    UTF_REQUIRE_EQUAL( 1U, factoryCalls.load() );

    task -> requestCancel();

    eq -> flushNoThrowIfFailed();

    const auto elapsed = time::microsec_clock::universal_time() - started;

    /*
     * No work task is created after the sleep the cancellation interrupted
     */

    UTF_REQUIRE_EQUAL( 1U, factoryCalls.load() );

    /*
     * The sleep was aborted rather than run out - without that, this case alone would take
     * the full retryTimeout
     */

    UTF_REQUIRE( elapsed < retryTimeout );

    UTF_REQUIRE( task -> isFailed() );

    /*
     * The interrupted sleep timer has no exception of its own, so the wrapper completes the
     * task as cancelled
     */

    try
    {
        cpp::safeRethrowException( task -> exception() );

        UTF_FAIL( "The cancelled retryable task must complete with an exception" );
    }
    catch( bl::SystemException& e )
    {
        UTF_REQUIRE( asio::error::operation_aborted == e.code() );
    }
}

UTF_AUTO_TEST_CASE( Tasks_TimerTaskCancelBeforeStartTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * The deterministic form of the race the case above can only hit intermittently: the timer
     * task is cancelled while it is still in the Created state, before the queue starts it
     *
     * requestCancel() marks the task and then returns without calling cancelTask(), because that
     * is only done for running tasks, and the generic "cancelled before it started" abort in
     * scheduleNothrow() is bypassed for timer tasks by scheduleEvenIfAlreadyCanceled(). The
     * pending cancellation must therefore be honoured when the timer is armed, or the task sleeps
     * its full init delay
     */

    const auto initDelay = time::seconds( 30 );

    /*
     * The aborted wait completes in well under a second, so this bound leaves a large margin
     * for a heavily loaded machine while staying far below initDelay
     *
     * Asserting only 'elapsed < initDelay' would also accept a cancellation which is honoured
     * just before the sleep runs out, so a partial regression would pass unnoticed
     */

    const auto maxCancelDelay = time::seconds( 5 );

    std::atomic< bool > callbackRan( false );

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    const auto task = om::qi< Task >(
        SimpleTimerTask::createInstance(
            [ &callbackRan ]() -> bool
            {
                callbackRan = true;

                return false;
            },
            cpp::copy( initDelay )                          /* duration */,
            cpp::copy( initDelay )                          /* initDelay */
            )
        );

    const auto started = time::microsec_clock::universal_time();

    task -> requestCancel();

    eq -> push_back( task );

    eq -> flushNoThrowIfFailed();

    const auto elapsed = time::microsec_clock::universal_time() - started;

    /*
     * The cancellation aborted the armed wait instead of letting it run out
     */

    UTF_REQUIRE( elapsed < maxCancelDelay );

    /*
     * The timer callback belongs to the elapsed sleep, which never happened
     */

    UTF_REQUIRE( ! callbackRan.load() );

    /*
     * An aborted timer wait completes the task without an exception of its own
     */

    UTF_REQUIRE( ! task -> exception() );
}

namespace
{
    /**
     * @brief The same shape as ExecutionQueueNotificationTestContext, but with the events
     * mask under the test's control instead of hardcoded to AllEvents
     *
     * The mask parameter type is unsigned to match ExecutionQueue::setNotifyCallback()
     * exactly, so a literal 0 can be passed without a cast or a narrowing warning
     */

    class ExecutionQueueNotificationMaskTestContext
    {
        BL_NO_COPY_OR_MOVE( ExecutionQueueNotificationMaskTestContext )

    public:

        const bl::om::ObjPtr< ExecutionQueueNotificationRecorderImpl >   recorder;
        const bl::om::ObjPtr< bl::om::Proxy >                           notifyProxy;
        bl::om::ObjPtrDisposable< bl::tasks::ExecutionQueue >            eq;

        ExecutionQueueNotificationMaskTestContext(
            SAA_in                  const unsigned                                          options,
            SAA_in                  const bl::tasks::ExecutionQueueNotify::NotifyDelivery   delivery,
            SAA_in                  const unsigned                                          eventsMask
            )
            :
            recorder( ExecutionQueueNotificationRecorderImpl::createInstance() ),
            notifyProxy( bl::om::ProxyImpl::createInstance< bl::om::Proxy >() ),
            eq(
                bl::tasks::ExecutionQueueImpl::createInstance< bl::tasks::ExecutionQueue >(
                    options
                    )
                )
        {
            notifyProxy -> connect(
                static_cast< bl::tasks::ExecutionQueueNotify* >( recorder.get() )
                );

            eq -> setNotifyCallback(
                bl::om::copy( notifyProxy ),
                delivery,
                eventsMask
                );
        }

        ~ExecutionQueueNotificationMaskTestContext() NOEXCEPT
        {
            notifyProxy -> disconnect();
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueNotificationEventsMaskTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExecutionQueueImplT::getEventNotifyCB() returns an empty callback when the requested
     * eventId is not in m_eventsMask, so invokeNotifyCB() is skipped entirely
     *
     * Every registration in the suite passes AllEvents, so nothing anywhere asserts that a
     * masked out event is *not* delivered and an implementation which dropped or inverted the
     * mask test would pass the whole suite. FanoutTasksObservable registers with
     * eventsMask = AllTasksCompleted and its onEvent() opens with a BL_ASSERT on exactly that
     * event id, so a mask regression turns into a debug build abort far away from the queue
     *
     * Delivery is inline on the thread which calls completeNext(), so every count below can be
     * evaluated as soon as it returns
     */

    {
        /*
         * (1) only TaskReady is unmasked, and the task is retained so TaskReady is the event
         * the completion produces
         */

        ExecutionQueueNotificationMaskTestContext context(
            ExecutionQueue::OptionKeepAll,
            ExecutionQueueNotify::DeliveryConcurrent,
            ExecutionQueueNotify::TaskReady
            );

        ExecutionQueueCompletionControl control;

        context.eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );
        UTF_REQUIRE( control.completeNext() );

        UTF_REQUIRE_EQUAL( 1U, context.recorder -> eventCount( ExecutionQueueNotify::TaskReady ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::AllTasksCompleted ) );
        UTF_REQUIRE( ! context.recorder -> hookFailed() );

        context.eq -> flushAndDiscardReady();
    }

    {
        /*
         * (2) only TaskDiscarded is unmasked, and the task is discarded rather than retained
         */

        ExecutionQueueNotificationMaskTestContext context(
            ExecutionQueue::OptionKeepNone,
            ExecutionQueueNotify::DeliveryConcurrent,
            ExecutionQueueNotify::TaskDiscarded
            );

        ExecutionQueueCompletionControl control;

        context.eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );
        UTF_REQUIRE( control.completeNext() );

        UTF_REQUIRE_EQUAL( 1U, context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskReady ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::AllTasksCompleted ) );
        UTF_REQUIRE( ! context.recorder -> hookFailed() );
    }

    {
        /*
         * (3) only AllTasksCompleted is unmasked
         */

        ExecutionQueueNotificationMaskTestContext context(
            ExecutionQueue::OptionKeepNone,
            ExecutionQueueNotify::DeliveryConcurrent,
            ExecutionQueueNotify::AllTasksCompleted
            );

        ExecutionQueueCompletionControl control;

        context.eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );
        UTF_REQUIRE( control.completeNext() );

        UTF_REQUIRE_EQUAL( 1U, context.recorder -> eventCount( ExecutionQueueNotify::AllTasksCompleted ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskReady ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded ) );
        UTF_REQUIRE( ! context.recorder -> hookFailed() );
    }

    {
        /*
         * (4) nothing is unmasked at all, so a registered and perfectly live observer
         * receives nothing
         */

        ExecutionQueueNotificationMaskTestContext context(
            ExecutionQueue::OptionKeepNone,
            ExecutionQueueNotify::DeliveryConcurrent,
            0U                                              /* eventsMask */
            );

        ExecutionQueueCompletionControl control;

        context.eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );

        UTF_REQUIRE( control.waitUntilScheduled( 1U ) );
        UTF_REQUIRE( control.completeNext() );

        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskReady ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded ) );
        UTF_REQUIRE_EQUAL( 0U, context.recorder -> eventCount( ExecutionQueueNotify::AllTasksCompleted ) );
        UTF_REQUIRE( ! context.recorder -> hookFailed() );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueReadyTasksCountTowardsObserverThrottleTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * padExecutingQueueNothrow() breaks on ( m_readyCount + m_executingCount ) >=
     * m_maxReadyOrExecuting, and that ready half is precisely what distinguishes the observer
     * throttle from setThrottleLimit(), which looks only at m_executingCount
     *
     * Tasks_ExecutionQueueThrottleFromObserverTest creates its queue with OptionKeepNone, so
     * m_readyCount is permanently zero there and that case would be satisfied by an
     * implementation in which getReadyOrExecuting() returned m_executingCount alone. The mode
     * covered here - OptionKeepAll plus an observer limit - is the one both production users
     * of the observer throttle actually run in
     *
     * The snapshots are taken inline and every assertion is evaluated at the very end: these
     * tasks only complete when this test completes them, so an assertion firing mid-test would
     * leave the queue holding tasks that can never finish and the cleanup would hang instead
     * of failing
     */

    const auto observer = ExecutionQueueThrottleObserverImpl::createInstance();
    observer -> setLimit( 2U );

    const auto notifyProxy = om::ProxyImpl::createInstance< om::Proxy >();
    notifyProxy -> connect( static_cast< ExecutionQueueNotify* >( observer.get() ) );

    om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    BL_SCOPE_EXIT( { notifyProxy -> disconnect(); } );

    eq -> setNotifyCallback(
        om::copy( notifyProxy ),
        ExecutionQueueNotify::DeliveryConcurrent,
        ExecutionQueueNotify::AllEvents
        );

    ExecutionQueueCompletionControl controlA;
    ExecutionQueueCompletionControl controlB;
    ExecutionQueueCompletionControl controlC;
    ExecutionQueueCompletionControl controlD;

    const auto taskA = om::qi< Task >( createControlledCompletionTask( controlA ) );
    const auto taskB = om::qi< Task >( createControlledCompletionTask( controlB ) );
    const auto taskC = om::qi< Task >( createControlledCompletionTask( controlC ) );
    const auto taskD = om::qi< Task >( createControlledCompletionTask( controlD ) );

    eq -> push_back( taskA );
    eq -> push_back( taskB );
    eq -> push_back( taskC );
    eq -> push_back( taskD );

    const bool scheduledA = controlA.waitUntilScheduled( 1U );
    const bool scheduledB = controlB.waitUntilScheduled( 1U );

    const auto ready1 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing1 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending1 = eq -> getQueueSize( ExecutionQueue::Pending );
    const auto size1 = eq -> size();

    const bool completedA = controlA.completeNext();

    const auto ready2 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing2 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending2 = eq -> getQueueSize( ExecutionQueue::Pending );

    const bool completedB = controlB.completeNext();

    const auto ready3 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing3 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending3 = eq -> getQueueSize( ExecutionQueue::Pending );
    const bool hasExecuting3 = eq -> hasExecuting();
    const bool hasPending3 = eq -> hasPending();

    const auto t1 = eq -> pop( false /* wait */ );

    const auto ready4 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing4 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending4 = eq -> getQueueSize( ExecutionQueue::Pending );

    const auto t2 = eq -> pop( false /* wait */ );

    const bool scheduledC = controlC.waitUntilScheduled( 1U );

    const auto ready5 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing5 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending5 = eq -> getQueueSize( ExecutionQueue::Pending );

    /*
     * Drain whatever remains; this must stay correct even if the throttle is not enforced at
     * all, which is exactly what a regression would produce
     */

    const bool completedC = controlC.completeNext();
    const bool scheduledD = controlD.waitUntilScheduled( 1U );
    const bool completedD = controlD.completeNext();

    const auto t3 = eq -> pop( false /* wait */ );
    const auto t4 = eq -> pop( false /* wait */ );

    eq -> flush();

    const auto queryCount = observer -> queryCount();

    UTF_REQUIRE( scheduledA );
    UTF_REQUIRE( scheduledB );
    UTF_REQUIRE( completedA );
    UTF_REQUIRE( completedB );
    UTF_REQUIRE( scheduledC );
    UTF_REQUIRE( completedC );
    UTF_REQUIRE( scheduledD );
    UTF_REQUIRE( completedD );

    /*
     * Snapshot 1 - the limit admits exactly two and the rest stays pending
     */

    UTF_REQUIRE_EQUAL( 0U, ready1 );
    UTF_REQUIRE_EQUAL( 2U, executing1 );
    UTF_REQUIRE_EQUAL( 2U, pending1 );
    UTF_REQUIRE_EQUAL( 4U, size1 );

    /*
     * Snapshot 2 - the key one: A retired into the ready queue, so the ready-or-executing
     * total is still 2 and C must NOT have been admitted. An executing-only throttle would
     * have admitted it here
     */

    UTF_REQUIRE_EQUAL( 1U, ready2 );
    UTF_REQUIRE_EQUAL( 1U, executing2 );
    UTF_REQUIRE_EQUAL( 2U, pending2 );

    /*
     * Snapshot 3 - also key: both retained results now count against the limit, so the queue
     * is deliberately idle with work still outstanding
     */

    UTF_REQUIRE_EQUAL( 2U, ready3 );
    UTF_REQUIRE_EQUAL( 0U, executing3 );
    UTF_REQUIRE_EQUAL( 2U, pending3 );
    UTF_REQUIRE( ! hasExecuting3 );
    UTF_REQUIRE( hasPending3 );

    /*
     * Snapshot 4 - getFirstReady() pads *before* it removes the front entry, so the capacity
     * this pop frees can only be used by the next queue operation
     */

    UTF_REQUIRE_EQUAL( 1U, ready4 );
    UTF_REQUIRE_EQUAL( 0U, executing4 );
    UTF_REQUIRE_EQUAL( 2U, pending4 );

    /*
     * Snapshot 5 - the next operation admits exactly one
     */

    UTF_REQUIRE_EQUAL( 0U, ready5 );
    UTF_REQUIRE_EQUAL( 1U, executing5 );
    UTF_REQUIRE_EQUAL( 1U, pending5 );

    /*
     * The ready queue is FIFO by completion order
     */

    UTF_REQUIRE( om::areEqual( t1, taskA ) );
    UTF_REQUIRE( om::areEqual( t2, taskB ) );
    UTF_REQUIRE( t3 );
    UTF_REQUIRE( t4 );

    UTF_REQUIRE( eq -> isEmpty() );
    UTF_REQUIRE_EQUAL( 1U, queryCount );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueFlushThrowDiscardsReadyTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * flushInternal()'s 'false == nothrowIfFailed' block scans m_ready in order, keeps the
     * *first* failure it finds, then calls clearQueue( m_ready, &m_readyCount ) and rethrows -
     * discardReady never runs. A flush() which throws therefore also destroys the caller's
     * successful ready results, and that destructive side effect plus the 'first failure wins'
     * rule are both entirely unasserted today: Tasks_ExecutionQueueOptionsTests only ever uses
     * flushNoThrowIfFailed()
     *
     * The throttle limit of 1 makes the completion order - and therefore the ready order -
     * deterministic, and the successful task is pushed first so the assertion that successful
     * results are destroyed too is meaningful
     */

    om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    eq -> setThrottleLimit( 1U );

    const auto taskS = eq -> push_back(
        []() -> void
        {
        }
        );

    eq -> push_back(
        []() -> void
        {
            BL_CHK(
                false,
                false,
                BL_MSG()
                    << "first failure"
                );
        }
        );

    eq -> push_back(
        []() -> void
        {
            BL_CHK(
                false,
                false,
                BL_MSG()
                    << "second failure"
                );
        }
        );

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> flush(),
        bl::UnexpectedException,
        "first failure"
        );

    UTF_REQUIRE( eq -> isEmpty() );
    UTF_REQUIRE_EQUAL( 0U, eq -> size() );
    UTF_REQUIRE_EQUAL( 0U, eq -> getQueueSize( ExecutionQueue::Ready ) );

    /*
     * The successful result is gone from the queue, but the task object itself survives -
     * only the queue entry was dropped
     */

    UTF_REQUIRE( ! eq -> pop( false /* wait */ ) );
    UTF_REQUIRE( ! taskS -> isFailed() );

    /*
     * And the now empty queue takes the isEmptyInternal() early return
     */

    UTF_REQUIRE_NO_THROW( eq -> flush() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueuePrioritizeAndPushFrontOrderingTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * prioritize( task, wait ) moves a *pending* entry to the head of m_pending and returns
     * true only in that case; for an executing or an already ready task it returns false, and
     * for a task which is not in the queue at all it throws
     *
     * push_front reaches pushInternalNoLock< false >, whose InsertSelector< false > does
     * m_pending.push_front. algorithmsSimpleParallelTest calls prioritize() and discards the
     * result, and no test anywhere asserts either the return value or the resulting order, so
     * swapping the two InsertSelector specialisations or making prioritize() a no-op which
     * returns true is undetectable today
     *
     * A throttle limit of 1 turns the pending order into the observable execution order
     *
     * Note that the try_lock failure path of prioritize( task, false ) is deliberately left
     * uncovered: making it fire requires holding m_lock from another thread across the call,
     * which no public API allows without a blocking task callback
     */

    om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    eq -> setThrottleLimit( 1U );

    ExecutionQueueCompletionControl controlA;
    ExecutionQueueCompletionControl controlB;
    ExecutionQueueCompletionControl controlC;
    ExecutionQueueCompletionControl controlD;

    const auto taskA = om::qi< Task >( createControlledCompletionTask( controlA ) );
    const auto taskB = om::qi< Task >( createControlledCompletionTask( controlB ) );
    const auto taskC = om::qi< Task >( createControlledCompletionTask( controlC ) );
    const auto taskD = om::qi< Task >( createControlledCompletionTask( controlD ) );

    /*
     * A starts executing immediately, then pending is [ B, C ] and push_front makes it
     * [ D, B, C ]
     */

    eq -> push_back( taskA );
    eq -> push_back( taskB );
    eq -> push_back( taskC );
    eq -> push_front( taskD );

    /*
     * Prioritising C from the tail makes pending [ C, D, B ]
     */

    const bool prioritizedC = eq -> prioritize( taskC, true /* wait */ );

    const bool prioritizedExecutingA = eq -> prioritize( taskA, true /* wait */ );

    const bool scheduledA = controlA.waitUntilScheduled( 1U );
    const bool completedA = controlA.completeNext();

    const bool cScheduled = controlC.waitUntilScheduled( 1U );

    /*
     * A is now in the ready queue rather than the pending one
     */

    const bool prioritizedReadyA = eq -> prioritize( taskA, true /* wait */ );

    const bool completedC = controlC.completeNext();

    const bool dScheduled = controlD.waitUntilScheduled( 1U );

    const bool completedD = controlD.completeNext();

    const bool bScheduled = controlB.waitUntilScheduled( 1U );
    const bool completedB = controlB.completeNext();

    eq -> flushAndDiscardReady();

    const auto orphan = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

    UTF_REQUIRE( scheduledA );
    UTF_REQUIRE( completedA );
    UTF_REQUIRE( completedC );
    UTF_REQUIRE( completedD );
    UTF_REQUIRE( bScheduled );
    UTF_REQUIRE( completedB );

    /*
     * C was prioritised out of the tail of the pending queue and therefore ran before both D
     * and B
     */

    UTF_REQUIRE( prioritizedC );
    UTF_REQUIRE( cScheduled );

    /*
     * D was inserted with push_front and therefore ran before B
     */

    UTF_REQUIRE( dScheduled );

    /*
     * prioritize() returns false for anything which is not pending
     */

    UTF_REQUIRE( ! prioritizedExecutingA );
    UTF_REQUIRE( ! prioritizedReadyA );

    /*
     * An unknown task is not silently tolerated - that is what would otherwise mask
     * use-after-completion bugs at the production call sites
     */

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> prioritize( orphan, true /* wait */ ),
        bl::UnexpectedException,
        "not in the execution queue"
        );

    UTF_REQUIRE( eq -> isEmpty() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueuePartitionObserversTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * size() returns m_allTasks.size(), i.e. the total across all three lists, getQueue() is a
     * switch whose default arm asserts and falls back to m_ready, and scanQueue() walks one
     * list under m_lock in list order
     *
     * hasReady() appears in no test at all, scanQueue() is never called directly, and
     * getQueueSize() is only ever called with Executing and Pending - so the Ready arm of that
     * switch is never taken. Production depends on exactly that arm:
     * FilesUnpackagerUnit::isInputDisconnectedAndAllWorkersDone() is literally
     * size() == getQueueSize( Ready ), so a switch arm swap or a default fallthrough would
     * make its completion predicate permanently false - a hang - with nothing failing here
     *
     * As above, the snapshots are collected inline and asserted at the very end
     */

    om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    eq -> setThrottleLimit( 1U );

    ExecutionQueueCompletionControl controlA;
    ExecutionQueueCompletionControl controlB;
    ExecutionQueueCompletionControl controlC;
    ExecutionQueueCompletionControl controlD;

    const auto taskA = om::qi< Task >( createControlledCompletionTask( controlA ) );
    const auto taskB = om::qi< Task >( createControlledCompletionTask( controlB ) );
    const auto taskC = om::qi< Task >( createControlledCompletionTask( controlC ) );
    const auto taskD = om::qi< Task >( createControlledCompletionTask( controlD ) );

    eq -> push_back( taskA );
    eq -> push_back( taskB );
    eq -> push_back( taskC );
    eq -> push_back( taskD );

    const bool scheduledA = controlA.waitUntilScheduled( 1U );

    const auto size1 = eq -> size();
    const auto ready1 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing1 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending1 = eq -> getQueueSize( ExecutionQueue::Pending );
    const bool hasReady1 = eq -> hasReady();
    const bool hasExecuting1 = eq -> hasExecuting();
    const bool hasPending1 = eq -> hasPending();
    const bool isEmpty1 = eq -> isEmpty();

    const bool completedA = controlA.completeNext();
    const bool scheduledB = controlB.waitUntilScheduled( 1U );

    const auto size2 = eq -> size();
    const auto ready2 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing2 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending2 = eq -> getQueueSize( ExecutionQueue::Pending );
    const bool hasReady2 = eq -> hasReady();

    /*
     * The scanQueue() callback runs under m_lock, so it must only append and must never call
     * back into the queue
     */

    std::vector< Task* > readyTasks;
    std::vector< Task* > executingTasks;
    std::vector< Task* > pendingTasks;

    const auto collect = []( SAA_inout std::vector< Task* >& out ) -> ExecutionQueue::task_callback_type
    {
        return [ &out ]( SAA_in const om::ObjPtr< Task >& task ) -> void
        {
            out.push_back( task.get() );
        };
    };

    eq -> scanQueue( ExecutionQueue::Ready, collect( readyTasks ) );
    eq -> scanQueue( ExecutionQueue::Executing, collect( executingTasks ) );
    eq -> scanQueue( ExecutionQueue::Pending, collect( pendingTasks ) );

    const bool completedB = controlB.completeNext();
    const bool scheduledC = controlC.waitUntilScheduled( 1U );
    const bool completedC = controlC.completeNext();
    const bool scheduledD = controlD.waitUntilScheduled( 1U );
    const bool completedD = controlD.completeNext();

    const auto p1 = eq -> pop( false /* wait */ );
    const auto p2 = eq -> pop( false /* wait */ );
    const auto p3 = eq -> pop( false /* wait */ );
    const auto p4 = eq -> pop( false /* wait */ );

    const bool isEmpty3 = eq -> isEmpty();
    const auto size3 = eq -> size();
    const auto ready3 = eq -> getQueueSize( ExecutionQueue::Ready );
    const auto executing3 = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pending3 = eq -> getQueueSize( ExecutionQueue::Pending );
    const bool hasReady3 = eq -> hasReady();
    const bool hasExecuting3 = eq -> hasExecuting();
    const bool hasPending3 = eq -> hasPending();

    UTF_REQUIRE( scheduledA );
    UTF_REQUIRE( completedA );
    UTF_REQUIRE( scheduledB );
    UTF_REQUIRE( completedB );
    UTF_REQUIRE( scheduledC );
    UTF_REQUIRE( completedC );
    UTF_REQUIRE( scheduledD );
    UTF_REQUIRE( completedD );

    UTF_REQUIRE( p1 );
    UTF_REQUIRE( p2 );
    UTF_REQUIRE( p3 );
    UTF_REQUIRE( p4 );

    /*
     * Snapshot 1 - one executing, nothing ready yet
     */

    UTF_REQUIRE_EQUAL( 4U, size1 );
    UTF_REQUIRE_EQUAL( 0U, ready1 );
    UTF_REQUIRE_EQUAL( 1U, executing1 );
    UTF_REQUIRE_EQUAL( 3U, pending1 );
    UTF_REQUIRE( ! hasReady1 );
    UTF_REQUIRE( hasExecuting1 );
    UTF_REQUIRE( hasPending1 );
    UTF_REQUIRE( ! isEmpty1 );

    /*
     * Snapshot 2 - all three lists are populated at once and size() is their total
     */

    UTF_REQUIRE_EQUAL( 4U, size2 );
    UTF_REQUIRE_EQUAL( 1U, ready2 );
    UTF_REQUIRE_EQUAL( 1U, executing2 );
    UTF_REQUIRE_EQUAL( 2U, pending2 );
    UTF_REQUIRE( hasReady2 );
    UTF_REQUIRE_EQUAL( size2, ready2 + executing2 + pending2 );

    /*
     * scanQueue() walks the right list, and walks it in list order
     */

    UTF_REQUIRE_EQUAL( 1U, readyTasks.size() );
    UTF_REQUIRE( readyTasks[ 0 ] == taskA.get() );

    UTF_REQUIRE_EQUAL( 1U, executingTasks.size() );
    UTF_REQUIRE( executingTasks[ 0 ] == taskB.get() );

    UTF_REQUIRE_EQUAL( 2U, pendingTasks.size() );
    UTF_REQUIRE( pendingTasks[ 0 ] == taskC.get() );
    UTF_REQUIRE( pendingTasks[ 1 ] == taskD.get() );

    /*
     * Snapshot 3 - fully drained
     */

    UTF_REQUIRE( isEmpty3 );
    UTF_REQUIRE_EQUAL( 0U, size3 );
    UTF_REQUIRE_EQUAL( 0U, ready3 );
    UTF_REQUIRE_EQUAL( 0U, executing3 );
    UTF_REQUIRE_EQUAL( 0U, pending3 );
    UTF_REQUIRE( ! hasReady3 );
    UTF_REQUIRE( ! hasExecuting3 );
    UTF_REQUIRE( ! hasPending3 );
}
