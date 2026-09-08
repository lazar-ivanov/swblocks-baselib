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

#include <baselib/reactive/ObservableBase.h>
#include <baselib/reactive/Observer.h>
#include <baselib/reactive/ObserverBase.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/ExecutionQueueNotify.h>
#include <baselib/tasks/SimpleTaskControlToken.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TaskControlToken.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <memory>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

/************************************************************************
 * Task control token, task restart / failure state and execution queue
 * ownership fixtures
 *
 * Note that these headers are all pulled into the same translation unit, so the fixtures of
 * TestTasks.h (ExecutionQueueCompletionControl, ExecutionQueueNotificationTestContext,
 * ExecutionQueueThrottleObserverImpl, createControlledCompletionTask,
 * MonotonicCounterObservableImpl), of TestTasks5.h (CountingObserverImpl) and of TestTasks6.h
 * (settledOutstandingObjectRefs, waitForOutstandingObjectRefs) are all visible here
 */

namespace
{
    /**
     * @brief A do nothing task which flips a caller owned flag when it is destroyed and which
     * exposes the execution queue back reference the task layer keeps in SimpleTaskBase::m_eq
     *
     * The destructor flag is what makes 'who was holding the last reference' directly
     * observable; the back reference accessor is what lets a test recover an execution queue
     * whose only remaining owner is the task itself
     */

    template
    <
        typename E = void
    >
    class TaskLifetimeProbeT : public bl::tasks::SimpleTaskBase
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( TaskLifetimeProbeT )

    protected:

        typedef bl::tasks::SimpleTaskBase                                   base_type;

        bool*                                                               m_destroyed;

        TaskLifetimeProbeT( SAA_inout bool* const destroyed )
            :
            m_destroyed( destroyed )
        {
            base_type::m_name = "TaskLifetimeProbe";
        }

        ~TaskLifetimeProbeT() NOEXCEPT
        {
            *m_destroyed = true;
        }

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            /*
             * Nothing to do - the task only exists so that its lifetime can be observed
             */

            BL_TASKS_HANDLER_END()
        }

    public:

        /**
         * @brief The execution queue which last scheduled this task
         *
         * Note that SimpleTaskBaseT::m_eq is never reset, so this stays valid (and keeps the
         * queue alive) for the rest of the task's life - which is exactly the ownership edge
         * Tasks_ExecutionQueueOwnershipCycleTests is about
         */

        std::shared_ptr< bl::tasks::ExecutionQueue > executionQueueBackReference() const NOEXCEPT
        {
            return base_type::m_eq;
        }
    };

    typedef bl::om::ObjectImpl< TaskLifetimeProbeT<> > TaskLifetimeProbeImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_SimpleTaskControlTokenTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * SimpleTaskControlTokenT is created at roughly 20 test sites purely as a constructor
     * argument and registerCancelableTask() / unregisterCancelableTask() have no direct
     * coverage at all, even though a broken fan-out (or a broken clear() of the set) would
     * leak every acceptor task registered for the life of the token
     *
     * Note that the race between a token cancel and an acceptor registering itself from
     * scheduleTask() cannot be reproduced deterministically - the single threaded coverage
     * below is the best available and the late registration hazard is pinned explicitly
     */

    const auto token = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    UTF_REQUIRE( ! token -> isCanceled() );

    const auto taskImpl1 = SimpleTaskImpl::createInstance( []() -> void {} );
    const auto taskImpl2 = SimpleTaskImpl::createInstance( []() -> void {} );
    const auto taskImpl3 = SimpleTaskImpl::createInstance( []() -> void {} );

    token -> registerCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( taskImpl1 ) ) );
    token -> registerCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( taskImpl2 ) ) );
    token -> registerCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( taskImpl3 ) ) );

    token -> unregisterCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( taskImpl3 ) ) );

    UTF_REQUIRE( ! taskImpl1 -> isCanceled() );
    UTF_REQUIRE( ! taskImpl2 -> isCanceled() );
    UTF_REQUIRE( ! taskImpl3 -> isCanceled() );

    token -> requestCancel();

    /*
     * The fan-out reaches every task which is still registered and only those - note that
     * these tasks were never scheduled, so SimpleTaskBaseT::requestCancel() only marks the
     * flag and takes no lock
     */

    UTF_REQUIRE( token -> isCanceled() );
    UTF_REQUIRE( taskImpl1 -> isCanceled() );
    UTF_REQUIRE( taskImpl2 -> isCanceled() );
    UTF_REQUIRE( ! taskImpl3 -> isCanceled() );

    /*
     * Cancelling twice must stay a safe no-op
     */

    UTF_REQUIRE_NO_THROW( token -> requestCancel() );

    /*
     * ... and so must unregistering something which was never registered
     */

    const auto neverRegistered = SimpleTaskImpl::createInstance( []() -> void {} );

    UTF_REQUIRE_NO_THROW(
        token -> unregisterCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( neverRegistered ) ) )
        );

    {
        /*
         * requestCancel() clears the set, which is what releases the strong ObjPtrCopyable
         * references the token holds - losing that clear() would pin every registered task
         * for the life of the token
         */

        bool destroyed = false;

        const auto freshToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        {
            const auto probeImpl = TaskLifetimeProbeImpl::createInstance( &destroyed );

            freshToken -> registerCancelableTask(
                om::ObjPtrCopyable< Task >( om::qi< Task >( probeImpl ) )
                );
        }

        /*
         * The test's own reference is gone, so the token's set is the only owner left
         */

        UTF_REQUIRE( ! destroyed );

        freshToken -> requestCancel();

        UTF_REQUIRE( destroyed );
    }

    {
        /*
         * HAZARD, pinned deliberately: registerCancelableTask() does not consult m_canceled,
         * so a task registered *after* requestCancel() is never cancelled and stays pinned in
         * the set until the token itself dies. This asserts the CURRENT behaviour so that
         * changing it is a deliberate decision rather than a silent one
         */

        const auto lateImpl = SimpleTaskImpl::createInstance( []() -> void {} );

        token -> registerCancelableTask( om::ObjPtrCopyable< Task >( om::qi< Task >( lateImpl ) ) );

        UTF_REQUIRE( token -> isCanceled() );
        UTF_REQUIRE( ! lateImpl -> isCanceled() );

        /*
         * ... and it really did stay in the set - a further requestCancel() reaches it
         */

        UTF_REQUIRE_NO_THROW( token -> requestCancel() );
        UTF_REQUIRE( lateImpl -> isCanceled() );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ExecuteQueueAndCancelOnFailureTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * The failure branch of executeQueueAndCancelOnFailure() is reached today only from the
     * network heavy blob transfer tests. Nothing asserts that the *first* failure is the one
     * which propagates, that the remainder are cancelled rather than executed, or that the
     * queue is empty afterwards
     */

    const std::size_t sleepingTasksCount = 3U;

    /*
     * The sleeps only have to guarantee that the sleeping tasks are still pending when
     * cancelAll() lands; they must not be shortened below ~500 ms
     */

    const auto cbSleep = []() -> void
    {
        os::sleep( time::milliseconds( 1000 ) );
    };

    {
        std::atomic< std::size_t > executed( 0U );

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
            );

        eq -> setThrottleLimit( 1U );

        eq -> push_back(
            cpp::void_callback_t(
                []() -> void
                {
                    BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom-0" );
                }
                )
            );

        eq -> push_back(
            cpp::void_callback_t(
                []() -> void
                {
                    BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom-1" );
                }
                )
            );

        for( std::size_t i = 0U; i < sleepingTasksCount; ++i )
        {
            eq -> push_back(
                cpp::void_callback_t(
                    [ &executed, &cbSleep ]() -> void
                    {
                        ++executed;
                        cbSleep();
                    }
                    )
                );
        }

        /*
         * Only the first failure propagates - a later one must never overwrite it
         */

        UTF_REQUIRE_THROW_MESSAGE(
            executeQueueAndCancelOnFailure( eq ),
            bl::UnexpectedException,
            "boom-0"
            );

        UTF_REQUIRE( eq -> isEmpty() );

        /*
         * cancelAll( false ) discards the pending tasks instead of executing them. This stays
         * a bound rather than an equality: the completion of the first failing task can pad
         * the executing queue before the test thread's pop() returns
         */

        UTF_REQUIRE( executed.load() <= 1U );
    }

    {
        /*
         * TRAP, pinned deliberately: on an OptionKeepNone queue pop() returns nullptr for
         * every completed task, so this helper drains the queue and returns *without
         * throwing* even though tasks failed - callers must not use it that way
         */

        std::atomic< std::size_t > executed( 0U );

        const auto eqKeepNone = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
            );

        eqKeepNone -> setThrottleLimit( 1U );

        eqKeepNone -> push_back(
            cpp::void_callback_t(
                []() -> void
                {
                    BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom-0" );
                }
                )
            );

        eqKeepNone -> push_back(
            cpp::void_callback_t(
                []() -> void
                {
                    BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom-1" );
                }
                )
            );

        for( std::size_t i = 0U; i < sleepingTasksCount; ++i )
        {
            eqKeepNone -> push_back(
                cpp::void_callback_t(
                    [ &executed, &cbSleep ]() -> void
                    {
                        ++executed;
                        cbSleep();
                    }
                    )
                );
        }

        UTF_REQUIRE_NO_THROW( executeQueueAndCancelOnFailure( eqKeepNone ) );

        UTF_REQUIRE( eqKeepNone -> isEmpty() );

        /*
         * ... and because nothing was ever cancelled every task ran to completion
         */

        UTF_REQUIRE_EQUAL( sleepingTasksCount, executed.load() );
    }
}

UTF_AUTO_TEST_CASE( Tasks_TaskRestartTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * Re-pushing a completed task hits the restart branch of scheduleNothrow(), which clears
     * the exception, m_notifyCalled and m_cancelRequested. Only the m_cancelRequested half is
     * asserted anywhere today, and SimpleTaskT::setCB - used by AsyncExecutorImpl to recycle
     * pooled tasks, which is exactly this scenario - has no coverage at all
     */

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    const auto taskImpl = SimpleTaskImpl::createInstance(
        []() -> void
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "first run" );
        }
        );

    const auto task = om::qi< Task >( taskImpl );

    eq -> push_back( task );
    eq -> flushNoThrowIfFailed();

    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE( task -> isFailedOrFailing() );
    UTF_REQUIRE_THROW_MESSAGE(
        cpp::safeRethrowException( task -> exception() ),
        bl::UnexpectedException,
        "first run"
        );
    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

    std::size_t invoked = 0U;

    /*
     * setCB() asserts that the task is not Running, so it can only be called once the flush
     * above has returned
     */

    taskImpl -> setCB(
        [ &invoked ]() -> void
        {
            ++invoked;
        }
        );

    while( eq -> pop( false /* wait */ ) )
    {
        /*
         * Drain the ready queue so the task is pushed again as a fresh entry
         */
    }

    eq -> push_back( task );

    UTF_REQUIRE_NO_THROW( eq -> flush() );

    /*
     * The stale exception did not survive the restart, the new callback ran and the task
     * reported completion again (a stale m_notifyCalled would have suppressed that)
     */

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> isFailedOrFailing() );
    UTF_REQUIRE( ! task -> exception() );
    UTF_REQUIRE_EQUAL( 1U, invoked );
    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

    /*
     * The third leg - a cancel latch set on the completed task is cleared by the restart, it
     * is not inherited by the next run
     */

    task -> requestCancel();

    UTF_REQUIRE( taskImpl -> isCanceled() );

    while( eq -> pop( false /* wait */ ) )
    {
    }

    eq -> push_back( task );

    UTF_REQUIRE_NO_THROW( eq -> flush() );

    UTF_REQUIRE( ! taskImpl -> isCanceled() );
    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE_EQUAL( 2U, invoked );

    eq -> flushAndDiscardReady();

    UTF_REQUIRE( eq -> isEmpty() );
}

UTF_AUTO_TEST_CASE( Tasks_TaskFailureStateTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * Three related contracts which no test separates today:
     *
     * -- isFailed() requires m_state >= PendingCompletion *and* m_hasException, while
     *    isFailedOrFailing() requires only m_hasException; isFailedOrFailing() drives the
     *    unwind decisions in FanoutTasksObservable, ChunksTransmitter and FilesUnpackagerUnit
     *    and is never called from any test
     * -- an exception injected through task -> exception( eptr ) wins over the one produced
     *    by the run, because notifyReadyImpl() only sets it if( ! m_exception )
     * -- SimpleCompletedTask, which AsyncExecutorImpl uses to represent an operation which
     *    failed before a task could be built, has no coverage at all
     */

    const auto eptr = BL_MAKE_EXCEPTION_PTR( bl::UnexpectedException(), BL_MSG() << "injected" );

    {
        /*
         * Note that a SimpleCompletedTask must never be pushed onto a queue - scheduling one
         * hits BL_RIP_MSG and aborts the process
         */

        const auto completedFailed = SimpleCompletedTask::createInstance< Task >( eptr );

        UTF_REQUIRE_EQUAL( Task::Completed, completedFailed -> getState() );
        UTF_REQUIRE( completedFailed -> isFailed() );
        UTF_REQUIRE( completedFailed -> isFailedOrFailing() );
        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( completedFailed -> exception() ),
            bl::UnexpectedException,
            "injected"
            );
        UTF_REQUIRE_NO_THROW( completedFailed -> requestCancel() );
    }

    {
        const auto completedClean = SimpleCompletedTask::createInstance< Task >();

        UTF_REQUIRE_EQUAL( Task::Completed, completedClean -> getState() );
        UTF_REQUIRE( ! completedClean -> isFailed() );
        UTF_REQUIRE( ! completedClean -> isFailedOrFailing() );
    }

    std::atomic< bool > callbackRan( false );

    const auto taskImpl = SimpleTaskImpl::createInstance(
        [ &callbackRan ]() -> void
        {
            callbackRan = true;
        }
        );

    const auto task = om::qi< Task >( taskImpl );

    task -> exception( eptr );

    /*
     * This is the only shape which separates the two predicates - the task carries an
     * exception but has not reached PendingCompletion yet
     */

    UTF_REQUIRE( task -> isFailedOrFailing() );
    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

    const auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    eq -> push_back( task );

    /*
     * The task was in Created state when it was pushed, so scheduleNothrow() does not take
     * the restart branch and the injected exception is not cleared; the callback runs, and
     * the task still completes as failed with the injected exception
     */

    UTF_REQUIRE_THROW_MESSAGE( eq -> flush(), bl::UnexpectedException, "injected" );

    UTF_REQUIRE( callbackRan.load() );
    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE_THROW_MESSAGE(
        cpp::safeRethrowException( task -> exception() ),
        bl::UnexpectedException,
        "injected"
        );

    UTF_REQUIRE( eq -> isEmpty() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueCancelWaitVersusNoWaitKeepCanceledTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * cancel( task, true ) delegates to wait( task, true ), whose pending branch does an
     * unconditional unlinkAndDestroy() and returns - OptionKeepCanceled is never consulted
     * there. cancel( task, false ) on the very same entry honours it and moves the entry into
     * the ready queue instead. The two spellings of 'cancel' are therefore not
     * interchangeable, and nothing asserted that until now
     */

    ExecutionQueueNotificationTestContext context( ExecutionQueue::OptionKeepCanceled );

    context.eq -> setThrottleLimit( 1U );

    ExecutionQueueCompletionControl controlA;

    context.eq -> push_back( om::qi< Task >( createControlledCompletionTask( controlA ) ) );

    const bool scheduledA = controlA.waitUntilScheduled( 1U );

    std::atomic< bool > executedB( false );
    std::atomic< bool > executedC( false );

    const auto taskB = context.eq -> push_back(
        cpp::void_callback_t(
            [ &executedB ]() -> void
            {
                executedB = true;
            }
            )
        );

    const auto taskC = context.eq -> push_back(
        cpp::void_callback_t(
            [ &executedC ]() -> void
            {
                executedC = true;
            }
            )
        );

    /*
     * The baseline is taken before the first cancel and the comparison below before task A is
     * completed, so no other queue activity can pollute the counts
     */

    const auto readyEventsBaseline =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskReady );

    const auto discardedEventsBaseline =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded );

    const bool cancelWaitResult = context.eq -> cancel( taskB, true /* wait */ );

    const auto readyAfterCancelWait = context.eq -> getQueueSize( ExecutionQueue::Ready );
    const auto sizeAfterCancelWait = context.eq -> size();

    const bool cancelNoWaitResult = context.eq -> cancel( taskC, false /* wait */ );

    const auto readyAfterCancelNoWait = context.eq -> getQueueSize( ExecutionQueue::Ready );

    const auto readyEventsAfterCancels =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskReady );

    const auto discardedEventsAfterCancels =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskDiscarded );

    const bool completedA = controlA.completeNext();

    context.eq -> flushAndDiscardReady();

    /*
     * Snapshots are taken first and asserted only here - an assertion which fired while task A
     * was still executing would leave the queue holding a task which can never finish and the
     * cleanup would hang instead of failing
     */

    UTF_REQUIRE( scheduledA );
    UTF_REQUIRE( completedA );

    /*
     * The documented contract: if wait is true the return value is always false
     */

    UTF_REQUIRE( ! cancelWaitResult );

    /*
     * B was destroyed despite OptionKeepCanceled - only A (executing) and C (pending) are left
     */

    UTF_REQUIRE_EQUAL( 0U, readyAfterCancelWait );
    UTF_REQUIRE_EQUAL( 2U, sizeAfterCancelWait );

    /*
     * ... whereas C, cancelled the other way, was retained
     */

    UTF_REQUIRE( cancelNoWaitResult );
    UTF_REQUIRE_EQUAL( 1U, readyAfterCancelNoWait );

    UTF_REQUIRE( ! executedB.load() );
    UTF_REQUIRE( ! executedC.load() );

    /*
     * The notification half. moveTaskToReadyQueue() and unlinkAndDestroy() are pure list
     * operations under m_lock with no getEventNotifyCB() / invokeNotifyCB() call, so C entered
     * the ready queue without a TaskReady and B was destroyed without a TaskDiscarded. That
     * silently under-counts outstanding work for every observer which follows the counting
     * contract documented at ExecutionQueueNotify.h:199 - this is the single place the
     * expectation flips from 'no event' to 'an event' once that hole is closed
     */

    UTF_REQUIRE_EQUAL( readyEventsBaseline, readyEventsAfterCancels );
    UTF_REQUIRE_EQUAL( discardedEventsBaseline, discardedEventsAfterCancels );

    UTF_REQUIRE( ! context.recorder -> hookFailed() );

    UTF_REQUIRE( context.eq -> isEmpty() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueObserverThrottleSurvivesDisconnectTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * m_maxReadyOrExecuting is sampled once in setNotifyCallback() outside m_lock and never
     * re-read, and a failed tryAcquireRef() suppresses only the notification, not the limit.
     * ExecutionQueueNotify.h documents that explicitly: the limit stays in effect even if the
     * observer proxy is subsequently disconnected, which is the half of the documented
     * behaviour change that differs from the previous release
     */

    const std::size_t limit = 2U;
    const std::size_t tasksCount = 5U;

    const auto observer = ExecutionQueueThrottleObserverImpl::createInstance();
    observer -> setLimit( limit );

    const auto notifyProxy = om::ProxyImpl::createInstance< om::Proxy >();
    notifyProxy -> connect( static_cast< ExecutionQueueNotify* >( observer.get() ) );

    const om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
        );

    /*
     * om::ProxyImplT::disconnect() nulls m_ref under its own lock and is idempotent, so this
     * stays correct even though the body below disconnects the proxy explicitly
     */

    BL_SCOPE_EXIT( { notifyProxy -> disconnect(); } );

    eq -> setNotifyCallback(
        om::copy( notifyProxy ),
        ExecutionQueueNotify::DeliveryConcurrent,
        ExecutionQueueNotify::AllEvents
        );

    /*
     * A single shared control is used for all the tasks, so the waitUntilScheduled() and
     * waitUntilReturned() counts below are cumulative and must be kept absolute
     */

    ExecutionQueueCompletionControl control;

    for( std::size_t i = 0U; i < tasksCount; ++i )
    {
        eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );
    }

    const bool initiallyScheduled = control.waitUntilScheduled( limit );

    const auto executingAfterPush = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAfterPush = eq -> getQueueSize( ExecutionQueue::Pending );

    notifyProxy -> disconnect();

    const bool firstCompleted = control.completeNext();
    const bool refilled = control.waitUntilScheduled( limit + 1U );

    const auto executingAfterDisconnect = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAfterDisconnect = eq -> getQueueSize( ExecutionQueue::Pending );

    /*
     * Drain whatever remains. This must stay correct even if the throttle is not enforced at
     * all, which is exactly what a regression would produce
     */

    for( std::size_t completed = 1U; completed < tasksCount; ++completed )
    {
        if( ! control.waitUntilScheduled( completed + 1U ) || ! control.completeNext() )
        {
            break;
        }
    }

    /*
     * Best effort: complete anything which was scheduled but not completed above, so that a
     * failure of the drain cannot leave the queue holding a task which can never finish
     */

    while( control.completeNext() )
    {
    }

    const bool allReturned = control.waitUntilReturned( tasksCount );

    if( allReturned )
    {
        eq -> flush();
    }
    else
    {
        eq -> cancelAll( true /* wait */ );
    }

    const auto queryCountAtEnd = observer -> queryCount();
    const bool emptyAtEnd = eq -> isEmpty();

    UTF_REQUIRE( initiallyScheduled );
    UTF_REQUIRE( firstCompleted );
    UTF_REQUIRE( refilled );
    UTF_REQUIRE( allReturned );

    UTF_REQUIRE_EQUAL( limit, executingAfterPush );
    UTF_REQUIRE_EQUAL( tasksCount - limit, pendingAfterPush );

    /*
     * The load bearing assertion - exactly one task was admitted after the observer was gone,
     * so the limit of two is still in force
     */

    UTF_REQUIRE_EQUAL( limit, executingAfterDisconnect );
    UTF_REQUIRE_EQUAL( tasksCount - limit - 1U, pendingAfterDisconnect );

    /*
     * ... and the observer was never queried again, not even to discover it was disconnected
     */

    UTF_REQUIRE_EQUAL( 1U, queryCountAtEnd );

    UTF_REQUIRE( emptyAtEnd );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueThrottleLimitLoweredAndResetTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * No test anywhere constructs an ExecutionQueueImpl with the two argument form, although
     * AsyncExecutorImpl builds both of its queues that way - a constructor which dropped its
     * maxExecuting argument would leave them unthrottled with nothing failing
     *
     * Lowering the limit does not preempt anything either: the padding stops at the limit and
     * it is enforced on the completion path, so admission resumes only once m_executingCount
     * falls below the new limit. And 0 means 'no limit', not 'admit nothing'
     */

    const std::size_t tasksCount = 6U;
    const std::size_t constructorLimit = 4U;

    const om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >(
            ExecutionQueue::OptionKeepNone,
            constructorLimit                    /* maxExecuting */
            )
        );

    /*
     * Note that setThrottleLimit() is deliberately *not* called here - the snapshot below is
     * what gates the constructor argument. As above, one shared control means the counts are
     * cumulative
     */

    ExecutionQueueCompletionControl control;

    for( std::size_t i = 0U; i < tasksCount; ++i )
    {
        eq -> push_back( om::qi< Task >( createControlledCompletionTask( control ) ) );
    }

    const bool scheduledConstructorLimit = control.waitUntilScheduled( constructorLimit );

    const auto executingFromConstructor = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingFromConstructor = eq -> getQueueSize( ExecutionQueue::Pending );

    eq -> setThrottleLimit( 1U );

    const auto executingAfterLowering = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAfterLowering = eq -> getQueueSize( ExecutionQueue::Pending );

    bool completedWhileAboveLimit = true;

    for( std::size_t completed = 0U; completed < constructorLimit - 1U; ++completed )
    {
        /*
         * Note that the outcome is accumulated separately - short circuiting the drain on a
         * failure would leave tasks which can never finish behind and hang the cleanup
         */

        const bool completedOne = control.completeNext();
        const bool returned = control.waitUntilReturned( completed + 1U );

        completedWhileAboveLimit = completedWhileAboveLimit && completedOne && returned;
    }

    const auto executingAtLimit = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAtLimit = eq -> getQueueSize( ExecutionQueue::Pending );

    const bool completedToZero = control.completeNext();
    const bool admittedOne = control.waitUntilScheduled( constructorLimit + 1U );

    const auto executingAfterAdmission = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAfterAdmission = eq -> getQueueSize( ExecutionQueue::Pending );

    eq -> setThrottleLimit( 0U );

    const bool admittedRest = control.waitUntilScheduled( tasksCount );

    const auto executingAfterReset = eq -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingAfterReset = eq -> getQueueSize( ExecutionQueue::Pending );

    /*
     * Drain whatever remains. This must stay correct even if none of the limits were enforced
     */

    while( control.completeNext() )
    {
    }

    const bool allReturned = control.waitUntilReturned( tasksCount );

    if( allReturned )
    {
        eq -> flush();
    }
    else
    {
        eq -> cancelAll( true /* wait */ );
    }

    const bool emptyAtEnd = eq -> isEmpty();

    UTF_REQUIRE( scheduledConstructorLimit );
    UTF_REQUIRE( completedWhileAboveLimit );
    UTF_REQUIRE( completedToZero );
    UTF_REQUIRE( admittedOne );
    UTF_REQUIRE( admittedRest );
    UTF_REQUIRE( allReturned );

    /*
     * The constructor supplied limit is in force with no setThrottleLimit() call at all
     */

    UTF_REQUIRE_EQUAL( constructorLimit, executingFromConstructor );
    UTF_REQUIRE_EQUAL( tasksCount - constructorLimit, pendingFromConstructor );

    /*
     * Lowering the limit does not preempt anything
     */

    UTF_REQUIRE_EQUAL( constructorLimit, executingAfterLowering );
    UTF_REQUIRE_EQUAL( tasksCount - constructorLimit, pendingAfterLowering );

    /*
     * ... and nothing is admitted while m_executingCount is still at the new limit
     */

    UTF_REQUIRE_EQUAL( 1U, executingAtLimit );
    UTF_REQUIRE_EQUAL( tasksCount - constructorLimit, pendingAtLimit );

    /*
     * Exactly one is admitted once the executing count reaches zero
     */

    UTF_REQUIRE_EQUAL( 1U, executingAfterAdmission );
    UTF_REQUIRE_EQUAL( tasksCount - constructorLimit - 1U, pendingAfterAdmission );

    /*
     * ... and 0 means unlimited, padding immediately
     */

    UTF_REQUIRE_EQUAL( 2U, executingAfterReset );
    UTF_REQUIRE_EQUAL( 0U, pendingAfterReset );

    UTF_REQUIRE( emptyAtEnd );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueDisposalOnReadySaturatedQueueTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExecutionQueue::disposeQueue() appears in no test, despite five production shutdown call
     * sites. flushInternal() runs clearQueue( m_pending ) *before* the wait on m_cvReady and
     * that ordering is load bearing: on a queue whose observer limit is saturated by ready
     * tasks padExecutingQueueNothrow() can no longer admit anything, so hasPendingOrExecuting()
     * would never become false if the pending list were still populated when the wait started
     *
     * The disposal therefore runs on its own thread behind a bounded signal, so a regression
     * fails this case rather than hanging the whole module
     */

    const std::size_t limit = 2U;

    const auto observer = ExecutionQueueThrottleObserverImpl::createInstance();
    observer -> setLimit( limit );

    const auto notifyProxy = om::ProxyImpl::createInstance< om::Proxy >();
    notifyProxy -> connect( static_cast< ExecutionQueueNotify* >( observer.get() ) );

    /*
     * disposeQueue() takes om::ObjPtrDisposable< ExecutionQueue >& and calls queue.reset(), so
     * every piece of state the disposal thread touches has to be shared - a stack local
     * captured by reference plus a detach on timeout would be a use after free
     */

    const auto eqHolder = std::make_shared< om::ObjPtrDisposable< ExecutionQueue > >(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    BL_SCOPE_EXIT( { notifyProxy -> disconnect(); } );

    ( *eqHolder ) -> setNotifyCallback(
        om::copy( notifyProxy ),
        ExecutionQueueNotify::DeliveryConcurrent,
        ExecutionQueueNotify::AllEvents
        );

    const auto controlA = std::make_shared< ExecutionQueueCompletionControl >();
    const auto controlB = std::make_shared< ExecutionQueueCompletionControl >();
    const auto controlC = std::make_shared< ExecutionQueueCompletionControl >();
    const auto controlD = std::make_shared< ExecutionQueueCompletionControl >();

    ( *eqHolder ) -> push_back( om::qi< Task >( createControlledCompletionTask( *controlA ) ) );
    ( *eqHolder ) -> push_back( om::qi< Task >( createControlledCompletionTask( *controlB ) ) );
    ( *eqHolder ) -> push_back( om::qi< Task >( createControlledCompletionTask( *controlC ) ) );
    ( *eqHolder ) -> push_back( om::qi< Task >( createControlledCompletionTask( *controlD ) ) );

    const bool scheduledA = controlA -> waitUntilScheduled( 1U );
    const bool scheduledB = controlB -> waitUntilScheduled( 1U );

    const bool completedA = controlA -> completeNext();
    const bool completedB = controlB -> completeNext();

    /*
     * A and B are now ready and the observer limit of two is saturated by ready entries alone,
     * so C and D are pending and permanently unadmittable
     */

    const auto readyBeforeDisposal = ( *eqHolder ) -> getQueueSize( ExecutionQueue::Ready );
    const auto executingBeforeDisposal = ( *eqHolder ) -> getQueueSize( ExecutionQueue::Executing );
    const auto pendingBeforeDisposal = ( *eqHolder ) -> getQueueSize( ExecutionQueue::Pending );

    const auto disposalSignal = std::make_shared< utest::TestSignal >();

    /*
     * Everything the thread touches is captured by value; the controls are captured too so
     * that they outlive the thread even on the detach path below
     */

    os::thread disposalThread(
        [ eqHolder, disposalSignal, controlA, controlB, controlC, controlD ]() -> void
        {
            ExecutionQueue::disposeQueue( *eqHolder );

            disposalSignal -> signal();
        }
        );

    const bool disposed = disposalSignal -> wait();

    if( disposed )
    {
        disposalThread.join();
    }
    else
    {
        /*
         * The disposal is stuck - the thread keeps its own references to everything it uses,
         * so nothing below may touch the queue holder or the controls
         */

        disposalThread.detach();

        UTF_FAIL( "ExecutionQueue::disposeQueue() did not return within the bound" );
    }

    UTF_REQUIRE( scheduledA );
    UTF_REQUIRE( scheduledB );
    UTF_REQUIRE( completedA );
    UTF_REQUIRE( completedB );

    UTF_REQUIRE_EQUAL( 2U, readyBeforeDisposal );
    UTF_REQUIRE_EQUAL( 0U, executingBeforeDisposal );
    UTF_REQUIRE_EQUAL( 2U, pendingBeforeDisposal );

    UTF_REQUIRE( disposed );

    /*
     * disposeQueue() is forceFlushNoThrow() + dispose() + reset() - forgetting the reset would
     * leave a disposed queue behind every shutdown site which uses it
     */

    UTF_REQUIRE( ! *eqHolder );

    /*
     * C and D were discarded while still pending, so they never ran and never registered a
     * completion callback with their controls
     */

    UTF_REQUIRE( ! controlC -> completeNext() );
    UTF_REQUIRE( ! controlD -> completeNext() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueWaitOnAbsentTaskTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * waitInternal() resolves the task with allowMissing = true and then requires it to be
     * Completed, so a task which is absent and still in Created state throws while a task
     * which is absent and already Completed returns silently. prioritize() resolves with
     * allowMissing = false and therefore throws for *any* absent task - the two are not
     * interchangeable, and neither branch is asserted anywhere today
     */

    const om::ObjPtrDisposable< ExecutionQueue > eq(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
        );

    /*
     * Absent and Created - never pushed into any queue
     */

    const auto orphan = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

    UTF_REQUIRE_EQUAL( Task::Created, orphan -> getState() );

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> wait( orphan ),
        bl::UnexpectedException,
        "not in the execution queue"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> waitNoPrioritize( orphan ),
        bl::UnexpectedException,
        "not in the execution queue"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> waitForSuccess( orphan ),
        bl::UnexpectedException,
        "not in the execution queue"
        );

    /*
     * Absent and Completed - OptionKeepNone is what removes the entry from m_allTasks while
     * the Task object itself is still held by the test
     */

    const auto done = eq -> push_back( cpp::void_callback_t( []() -> void {} ) );

    eq -> flush();

    UTF_REQUIRE_EQUAL( Task::Completed, done -> getState() );

    UTF_REQUIRE_NO_THROW( eq -> wait( done ) );
    UTF_REQUIRE_NO_THROW( eq -> waitNoPrioritize( done ) );
    UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( done ) );

    /*
     * ... whereas prioritize() has no Completed escape hatch. That asymmetry is deliberate
     * today - it is what makes wait() and prioritize() non-interchangeable, and it is what
     * keeps prioritize() from masking use-after-completion bugs
     */

    UTF_REQUIRE_THROW_MESSAGE(
        eq -> prioritize( done, true /* wait */ ),
        bl::UnexpectedException,
        "not in the execution queue"
        );

    UTF_REQUIRE( eq -> isEmpty() );
}

UTF_AUTO_TEST_CASE( Tasks_ReactiveObservableRequestCancelTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * An observable cannot be cancelled as a task - ObservableBaseT makes isCanceled(),
     * cancelTask() and requestCancelInternal() private and remaps requestCancel() onto
     * requestStop(), so the observable unwinds instead of being torn down mid-stream: the next
     * loop iteration returns neg_infin, notifyOnComplete() fans out onCompleted to every
     * subscriber and the iteration after that fails the task with operation_aborted
     *
     * requestCancel() is the path an ExecutionQueue takes during shutdown (cancelAll), so it
     * runs in production far more often than dispose(), which is the only one covered today
     */

    const auto observerImpl = CountingObserverImpl::createInstance();

    scheduleAndExecuteInParallel(
        [ &observerImpl ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            const auto observableShared = om::getSharedPtr(
                MonotonicCounterObservableImpl::createInstance(
                    false                       /* throwFromInnerLoop */,
                    200U                        /* ticksCount */,
                    10U                         /* intervalInMilliseconds */
                    )
                );

            /*
             * The observable is held through a std::shared_ptr, so the subscription handle
             * really does unsubscribe when it is discarded - it must stay alive here
             */

            const auto subscription = observableShared -> subscribe(
                om::qi< reactive::Observer >( observerImpl )
                );

            const auto task = om::qi< Task >( observableShared.get() );

            eq -> push_back( task );

            /*
             * Long enough that the cancel lands in the middle of the stream
             */

            os::sleep( time::milliseconds( 500 ) );

            om::qi< Task >( observableShared.get() ) -> requestCancel();

            try
            {
                eq -> waitForSuccess( task );

                UTF_FAIL( "A cancelled observable task must fail with operation_aborted" );
            }
            catch( eh::system_error& e )
            {
                UTF_REQUIRE( asio::error::operation_aborted == e.code() );
            }

            BL_UNUSED( subscription );
        }
        );

    /*
     * The subscriber was completed exactly once - the observable unwound gracefully rather
     * than being hard-cancelled, which would have skipped notifyOnComplete() and left the
     * subscriber hanging
     */

    UTF_REQUIRE_EQUAL( 1U, observerImpl -> completedCount() );

    /*
     * ... and it really was stopped mid-stream
     */

    UTF_REQUIRE( observerImpl -> nextCount() > 0U );
    UTF_REQUIRE( observerImpl -> nextCount() < 200U );

    /*
     * A stop is not an error
     */

    UTF_REQUIRE_EQUAL( 0U, observerImpl -> errorCount() );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueOwnershipCycleTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * Every task an execution queue schedules receives om::getSharedPtr< ExecutionQueue >( this )
     * and stores it in m_eq for the rest of its life, and the queue in turn owns an
     * om::ObjPtr< Task > for every task it holds. For any option which retains completed tasks
     * that is a genuine strong reference cycle: queue -> TaskInfo -> Task -> shared_ptr -> queue.
     * It is broken only by flush( ..., discardReady ), forceFlushNoThrow(), cancelAll(),
     * dispose() or disposeQueue()
     *
     * The suite had no lifetime oracle for the library's most important owning relationship at
     * all. Note that two global thread pools stay alive for the whole module, so the counts are
     * always compared against a captured baseline and never against zero, and the baseline is
     * captured inside the case because case ordering is not fixed
     */

    const auto baseline = settledOutstandingObjectRefs();

    {
        /*
         * (1) The cycle exists - dropping the caller's own reference releases nothing
         *
         * The probe task is created before the baseline for this arm so that the comparison
         * isolates the *queue* side of the cycle, and it is kept alive across the measurement
         * so that the queue can be recovered and torn down afterwards. Note that the arm
         * deliberately does not use waitForSuccess(): wait() unlinks a ready entry, which would
         * break the queue -> Task edge and hide the very cycle being asserted
         */

        bool destroyed = false;

        const auto probeImpl = TaskLifetimeProbeImpl::createInstance( &destroyed );
        const auto probeTask = om::qi< Task >( probeImpl );

        const auto baselineWithTask = settledOutstandingObjectRefs();

        {
            om::ObjPtr< ExecutionQueue > eq(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                );

            eq -> push_back( probeTask );
            eq -> flush();

            /*
             * The caller simply drops its om::ObjPtr - no discard of the ready queue, no
             * dispose. This is the shape which leaks the queue, every task it retains and
             * everything those tasks capture, with no assert, no log and no abort
             */
        }

        const auto afterDrop = settledOutstandingObjectRefs();

        /*
         * The queue is still alive and still reachable, but only through the task's own back
         * reference
         */

        const auto leakedQueue = probeImpl -> executionQueueBackReference();

        UTF_MESSAGE(
            "The queue <-> task ownership cycle keeps the execution queue alive after its last"
            " external reference is dropped - this arm pins that leaking-by-design behaviour"
            );

        UTF_REQUIRE( afterDrop > baselineWithTask );
        UTF_REQUIRE( nullptr != leakedQueue );
        UTF_REQUIRE( ! destroyed );

        /*
         * Recover and tear the queue down - the module turns any reference which is still
         * outstanding at exit into a failure of the whole run, so this arm must not leave a
         * permanent leak behind
         */

        if( leakedQueue )
        {
            auto recovered = om::lockDisposable( leakedQueue.get() );

            ExecutionQueue::disposeQueue( recovered );
        }
    }

    UTF_REQUIRE_EQUAL( baseline, waitForOutstandingObjectRefs( baseline ) );

    {
        /*
         * (2) disposeQueue() breaks it - the universal teardown idiom of the five production
         * shutdown sites
         */

        const auto baselineArm = settledOutstandingObjectRefs();

        bool emptyBeforeRelease = false;

        {
            om::ObjPtrDisposable< ExecutionQueue > eq(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                );

            const auto task = om::qi< Task >( SimpleTaskImpl::createInstance( []() -> void {} ) );

            eq -> push_back( task );
            eq -> flush();

            /*
             * disposeQueue() resets the handle it is given, so emptiness is observed through a
             * separate reference which is released again right away
             */

            const auto probe = om::copy( eq.get() );

            ExecutionQueue::disposeQueue( eq );

            emptyBeforeRelease = ( ! eq ) && probe -> isEmpty();
        }

        UTF_REQUIRE( emptyBeforeRelease );
        UTF_REQUIRE_EQUAL( baselineArm, waitForOutstandingObjectRefs( baselineArm ) );
    }

    {
        /*
         * (3) Discarding the ready queue breaks the queue -> Task edge on its own, with no
         * dispose involved at all
         *
         * The queue's ready entry is deliberately the *only* owner of the task here, so the
         * destructor flag reports exactly when that edge was released. Note that the handle
         * stays an om::ObjPtrDisposable: an execution queue which is destroyed without ever
         * being disposed reaches its destructor with m_observerThis still connected
         */

        const auto baselineArm = settledOutstandingObjectRefs();

        bool destroyed = false;
        bool emptyAfterFlush = false;

        {
            om::ObjPtrDisposable< ExecutionQueue > eq(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                );

            {
                const auto probeImpl = TaskLifetimeProbeImpl::createInstance( &destroyed );

                eq -> push_back( om::qi< Task >( probeImpl ) );
            }

            eq -> flush();

            /*
             * Retained in the ready queue, which is now the last reference to the task
             */

            UTF_REQUIRE( ! destroyed );

            eq -> flushAndDiscardReady();

            emptyAfterFlush = eq -> isEmpty();

            UTF_REQUIRE( destroyed );
        }

        UTF_REQUIRE( emptyAfterFlush );
        UTF_REQUIRE_EQUAL( baselineArm, waitForOutstandingObjectRefs( baselineArm ) );
    }

    {
        /*
         * (4) The task side of the edge - once the queue is disposed and released the task,
         * not the queue, is the last owner
         */

        const auto baselineArm = settledOutstandingObjectRefs();

        bool destroyed = false;

        auto probeImpl = TaskLifetimeProbeImpl::createInstance( &destroyed );

        {
            om::ObjPtrDisposable< ExecutionQueue > eq(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                );

            eq -> push_back( om::qi< Task >( probeImpl ) );
            eq -> flush();

            const auto probe = om::copy( eq.get() );

            ExecutionQueue::disposeQueue( eq );

            UTF_REQUIRE( probe -> isEmpty() );
        }

        UTF_REQUIRE( ! destroyed );

        probeImpl.reset();

        UTF_REQUIRE( destroyed );
        UTF_REQUIRE_EQUAL( baselineArm, waitForOutstandingObjectRefs( baselineArm ) );
    }
}

UTF_AUTO_TEST_CASE( Tasks_TaskSingleQueueOwnershipTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * 'A task belongs to exactly one execution queue' is stated as a comment in three places
     * and enforced by nothing in a release build - pushing a task which queue A retains into
     * queue B overwrites m_cbReady and m_eq, so A's entry is never removed, A's flush() blocks
     * forever and A's destructor asserts
     *
     * This case is the fence for the two *legitimate* patterns the library itself uses. The
     * illegitimate one - pushing into a second queue while the first still owns the task -
     * requires a production guard (an owning queue back-pointer compared in pushInternal and
     * promoted from BL_ASSERT to BL_CHK) before it can be exercised at all, since attempting
     * it today would hang this module rather than fail it
     */

    const om::ObjPtrDisposable< ExecutionQueue > eqA(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    const om::ObjPtrDisposable< ExecutionQueue > eqB(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
        );

    std::atomic< std::size_t > callCount( 0U );

    const auto task = SimpleTaskImpl::createInstance< Task >(
        cpp::void_callback_t(
            [ &callCount ]() -> void
            {
                ++callCount;
            }
            )
        );

    eqA -> push_back( task );

    UTF_REQUIRE_NO_THROW( eqA -> waitForSuccess( task ) );

    /*
     * Note the CURRENT behaviour of wait(): a ready entry is unlinked and destroyed by the
     * wait itself, so waiting on a task also removes it from the queue which owned it even
     * under OptionKeepAll
     */

    UTF_REQUIRE( eqA -> isEmpty() );
    UTF_REQUIRE_EQUAL( 1U, callCount.load() );

    /*
     * The same-queue re-push - this is the pattern AsyncExecutorImpl::asyncBeginImpl(),
     * postCompletionCallback() and FanoutTasksObservable all rely on, and it must keep working
     */

    UTF_REQUIRE_NO_THROW( eqA -> push_back( task ) );
    UTF_REQUIRE_NO_THROW( eqA -> waitForSuccess( task ) );

    UTF_REQUIRE_EQUAL( 2U, callCount.load() );
    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

    eqA -> flushAndDiscardReady();

    UTF_REQUIRE( eqA -> isEmpty() );

    /*
     * The negative control - the invariant is about *ownership*, not about identity: a task
     * which has been popped out of one queue may legitimately be pushed into another
     */

    eqA -> push_back( task );
    eqA -> flush();

    UTF_REQUIRE_EQUAL( 1U, eqA -> getQueueSize( ExecutionQueue::Ready ) );

    const auto popped = eqA -> pop( false /* wait */ );

    UTF_REQUIRE( popped );
    UTF_REQUIRE( eqA -> isEmpty() );

    UTF_REQUIRE_NO_THROW( eqB -> push_back( popped ) );
    UTF_REQUIRE_NO_THROW( eqB -> waitForSuccess( popped ) );

    UTF_REQUIRE_EQUAL( 4U, callCount.load() );

    UTF_REQUIRE( eqA -> isEmpty() );
    UTF_REQUIRE( eqB -> isEmpty() );
}
