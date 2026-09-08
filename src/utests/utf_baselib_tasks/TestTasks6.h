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

#include <baselib/tasks/utils/ShutdownTask.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <thread>

#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

/************************************************************************
 * Object lifetime and thread affinity fixtures
 *
 * Note that these headers are all pulled into the same translation unit, so the fixtures
 * of TestTasks.h (MonotonicCounterObservableImpl, MonotonicCounterObserverImpl) and of
 * TestTasks5.h (CountingObserverImpl, ThrowingOnCompletedObserverImpl) are visible here
 */

namespace
{
    /**
     * @brief The process wide outstanding object reference count, sampled once it has
     * stopped moving
     *
     * A thread pool thread can still be releasing the last reference to something which a
     * previous case left behind when the test thread takes a baseline, so the baseline is
     * taken through a bounded retry (max 30 x 100 ms) which returns as soon as two
     * consecutive samples agree
     */

    long settledOutstandingObjectRefs() NOEXCEPT
    {
        const std::size_t maxRetries = 30U;

        auto last = bl::om::outstandingObjectRefs();

        for( std::size_t retries = 0U; retries < maxRetries; ++retries )
        {
            bl::os::sleep( bl::time::milliseconds( 100 ) );

            const auto current = bl::om::outstandingObjectRefs();

            if( current == last )
            {
                break;
            }

            last = current;
        }

        return last;
    }

    /**
     * @brief The process wide outstanding object reference count, sampled until it matches
     * the expected value or the bound is reached
     *
     * The bound (max 30 x 1 s, modelled on TestObjModel.h) is only ever paid when the
     * comparison is about to fail, so the assertion this feeds is exactly
     * 'expected == om::outstandingObjectRefs()' with the release given a chance to complete
     */

    long waitForOutstandingObjectRefs( SAA_in const long expected ) NOEXCEPT
    {
        const std::size_t maxRetries = 30U;

        auto current = bl::om::outstandingObjectRefs();

        for( std::size_t retries = 0U; expected != current && retries < maxRetries; ++retries )
        {
            bl::os::sleep( bl::time::seconds( 1L ) );

            current = bl::om::outstandingObjectRefs();
        }

        return current;
    }

    /**
     * @brief A bounded one shot rendezvous which carries the id of the thread that set it
     *
     * wait() is bounded at 10 seconds and returns false on timeout, so a genuine hang fails
     * the case instead of stalling the module; the id is only read after wait() returned true
     *
     * Note that set() is called from inside a task body, so it must never assert - the
     * assertions are all made on the main test thread
     */

    class ThreadIdProbe
    {
        BL_NO_COPY_OR_MOVE( ThreadIdProbe )

    private:

        mutable bl::os::mutex               m_lock;
        bl::os::condition_variable          m_cv;
        std::thread::id                     m_threadId;
        bool                                m_signaled;

    public:

        ThreadIdProbe()
            :
            m_signaled( false )
        {
        }

        void set( SAA_in const std::thread::id& threadId )
        {
            {
                BL_MUTEX_GUARD( m_lock );

                m_threadId = threadId;
                m_signaled = true;
            }

            m_cv.notify_all();
        }

        bool wait()
        {
            bl::os::mutex_unique_lock guard( m_lock );

            return m_cv.wait_for(
                guard,
                bl::os::chrono::seconds( 10 ),
                [ this ]() -> bool
                {
                    return m_signaled;
                }
                );
        }

        std::thread::id value() const
        {
            BL_MUTEX_GUARD( m_lock );

            return m_threadId;
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveObservableDisposeLifetimeTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ObservableBaseT::dispose() is the only teardown entry point of the reactive layer and
     * it is an unbounded busy wait - it retries tryDisposeInternal( true ) every 50 ms until
     * the events queue of every subscription has drained
     *
     * The first pass can never succeed: tryFlushObserversEventQueues() force flushes each
     * queue and then pushes a notifyObserverComplete task into it which holds a *strong*
     * reference back to the observable, so termination is owned by the thread pool which
     * runs that task, and until it does the observable cannot be destroyed even in principle
     *
     * Nothing in the suite bounds that loop or proves the release, so a regression which
     * leaves a task sitting in a subscription events queue turns a hung CI job into a
     * silent hang rather than into a named failure
     */

    const auto disposeBound = time::seconds( 30 );

    {
        /*
         * The graceful arm - an observable disposed in the middle of the stream
         */

        const auto baseline = settledOutstandingObjectRefs();

        bool sawValues = false;
        bool onCompletedCalled = false;
        eh::error_code taskErrorCode;
        time::time_duration disposeElapsed;
        time::time_duration secondDisposeElapsed;

        scheduleAndExecuteInParallel(
            [
                &sawValues,
                &onCompletedCalled,
                &taskErrorCode,
                &disposeElapsed,
                &secondDisposeElapsed
            ]
            ( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableImpl = om::getSharedPtr(
                    MonotonicCounterObservableImpl::createInstance( false /* throwFromInnerLoop */ )
                    );

                const auto observable = om::qi< reactive::Observable >( observableImpl );

                const auto observerImpl = MonotonicCounterObserverImpl::createInstance();

                const auto subscription =
                    observable -> subscribe( om::qi< reactive::Observer >( observerImpl ) );

                BL_UNUSED( subscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                /*
                 * Wait for the stream to be well under way before disposing it - the sleep
                 * below is the polling interval of a bounded loop, not a timing assumption
                 */

                for( std::size_t i = 0U; i < 1000U; ++i )
                {
                    if( observerImpl -> lastValue() >= 3U )
                    {
                        sawValues = true;

                        break;
                    }

                    os::sleep( time::milliseconds( 10 ) );
                }

                const auto t0 = time::microsec_clock::universal_time();

                om::qi< om::Disposable >( observable ) -> dispose();

                disposeElapsed = time::microsec_clock::universal_time() - t0;

                /*
                 * The second dispose must return through the early out in
                 * tryDisposeInternal() without aborting - the subscription map is empty now
                 */

                const auto t1 = time::microsec_clock::universal_time();

                om::qi< om::Disposable >( observable ) -> dispose();

                secondDisposeElapsed = time::microsec_clock::universal_time() - t1;

                /*
                 * Disposal requests the observable to stop, so the task either completed
                 * before that or it failed with operation_aborted - anything else is an error
                 */

                try
                {
                    eq -> waitForSuccess( task );
                }
                catch( eh::system_error& e )
                {
                    taskErrorCode = e.code();
                }

                onCompletedCalled = observerImpl -> onCompletedCalled();
            }
            );

        UTF_REQUIRE( sawValues );

        /*
         * The bound below does not exist in the production code at all - it is what turns a
         * dispose() which spins forever into a named failure
         */

        UTF_REQUIRE( disposeElapsed < disposeBound );
        UTF_REQUIRE( secondDisposeElapsed < disposeBound );

        UTF_REQUIRE( ! taskErrorCode || asio::error::operation_aborted == taskErrorCode );

        /*
         * Disposal delivers the terminal event rather than discarding it
         */

        UTF_REQUIRE( onCompletedCalled );

        /*
         * The observable, its SubscriptionInfo, its per subscription events queue and the
         * completion task which held the strong self reference were all released
         */

        UTF_REQUIRE_EQUAL( baseline, waitForOutstandingObjectRefs( baseline ) );
    }

    {
        /*
         * The same sequence with an observer whose onCompleted() throws - the throw is
         * swallowed by notifyObserverComplete()'s BL_WARN_NOEXCEPT wrapper, so the completion
         * task must not be retained as a failed task by the OptionKeepFailed events queue of
         * the subscription, which is what would make dispose() spin forever
         *
         * Note that the harness turns every warning it sees into a test failure, so the line
         * logger is redirected for the duration of this sub-block - the diagnostic is still
         * written to the module log, just not as an error
         */

        const Logging::LineLoggerPusher pushLogger( &utest::warningToDebugLineLogger );

        const auto baseline = settledOutstandingObjectRefs();

        bool sawValues = false;
        std::size_t completedCount = 0U;
        eh::error_code taskErrorCode;
        time::time_duration disposeElapsed;
        time::time_duration secondDisposeElapsed;

        scheduleAndExecuteInParallel(
            [
                &sawValues,
                &completedCount,
                &taskErrorCode,
                &disposeElapsed,
                &secondDisposeElapsed
            ]
            ( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableImpl = om::getSharedPtr(
                    MonotonicCounterObservableImpl::createInstance(
                        false                   /* throwFromInnerLoop */,
                        30U                     /* ticksCount */,
                        10U                     /* intervalInMilliseconds */
                        )
                    );

                const auto observable = om::qi< reactive::Observable >( observableImpl );

                const auto observerImpl = ThrowingOnCompletedObserverImpl::createInstance();

                const auto subscription =
                    observable -> subscribe( om::qi< reactive::Observer >( observerImpl ) );

                BL_UNUSED( subscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                sawValues = observerImpl -> waitForNextCount( 3U );

                const auto t0 = time::microsec_clock::universal_time();

                om::qi< om::Disposable >( observable ) -> dispose();

                disposeElapsed = time::microsec_clock::universal_time() - t0;

                const auto t1 = time::microsec_clock::universal_time();

                om::qi< om::Disposable >( observable ) -> dispose();

                secondDisposeElapsed = time::microsec_clock::universal_time() - t1;

                try
                {
                    eq -> waitForSuccess( task );
                }
                catch( eh::system_error& e )
                {
                    taskErrorCode = e.code();
                }

                completedCount = observerImpl -> completedCount();
            }
            );

        UTF_REQUIRE( sawValues );

        UTF_REQUIRE( disposeElapsed < disposeBound );
        UTF_REQUIRE( secondDisposeElapsed < disposeBound );

        UTF_REQUIRE( ! taskErrorCode || asio::error::operation_aborted == taskErrorCode );

        /*
         * The observer was told exactly once even though it threw from onCompleted()
         */

        UTF_REQUIRE_EQUAL( completedCount, 1U );

        UTF_REQUIRE_EQUAL( baseline, waitForOutstandingObjectRefs( baseline ) );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ReactiveSubscriptionHandleLifetimeTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ObservableBaseT::subscribe() materialises a std::shared_ptr to the observable purely
     * as a local and the disposer it returns keeps only a std::weak_ptr to it, so whether
     * discarding the subscription handle unsubscribes the observer depends entirely on
     * whether the *caller* holds a shared_ptr to the observable
     *
     * Four unrelated pipeline cases in this module rely on the no-op half of that coupling -
     * they discard the handle while holding the observable through a plain om::ObjPtr - so a
     * change on either side of it would fail them with a message which points at the
     * observable rather than at the change
     *
     * Whether that is the *intended* ownership model is an open design question - this case
     * pins the CURRENT behaviour in both directions, so that changing it has to be a
     * deliberate and visible test change rather than a silent one
     */

    {
        /*
         * (1) Handle discarded, observable held only through om::ObjPtr - the weak reference
         * in the disposer is already expired when the handle dies, so the subscription
         * survives the destruction of its own handle
         */

        const auto observerImpl = CountingObserverImpl::createInstance();

        scheduleAndExecuteInParallel(
            [ &observerImpl ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableImpl = MonotonicCounterObservableImpl::createInstance(
                    false                       /* throwFromInnerLoop */,
                    4U                          /* ticksCount */,
                    0U                          /* intervalInMilliseconds */
                    );

                observableImpl -> subscribe( om::qi< reactive::Observer >( observerImpl ) );

                const auto task = om::qi< Task >( observableImpl.get() );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );
            }
            );

        UTF_REQUIRE_EQUAL( observerImpl -> nextCount(), 4U );
        UTF_REQUIRE_EQUAL( observerImpl -> completedCount(), 1U );
    }

    {
        /*
         * (2) The same, except that the observable is additionally held through a
         * std::shared_ptr - subscribe() then hands the disposer a weak reference to a live
         * control block, so discarding the handle really does unsubscribe
         *
         * The observable must stay alive across the subscribe() call and the wait, so
         * observableShared is a plain local - resetting it early would silently turn this
         * sub-block into (1)
         */

        const auto observerImpl = CountingObserverImpl::createInstance();

        scheduleAndExecuteInParallel(
            [ &observerImpl ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableShared = om::getSharedPtr(
                    MonotonicCounterObservableImpl::createInstance(
                        false                   /* throwFromInnerLoop */,
                        4U                      /* ticksCount */,
                        0U                      /* intervalInMilliseconds */
                        )
                    );

                /*
                 * The zero subscriber state has to be made legal explicitly, otherwise
                 * notifyOnNext() would fail the observable with 'Observable cannot have
                 * empty subscribers list'
                 */

                observableShared -> allowNoSubscribers( true );

                observableShared -> subscribe( om::qi< reactive::Observer >( observerImpl ) );

                const auto task = om::qi< Task >( observableShared.get() );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );
            }
            );

        UTF_REQUIRE_EQUAL( observerImpl -> nextCount(), 0U );
        UTF_REQUIRE_EQUAL( observerImpl -> completedCount(), 0U );
    }

    {
        /*
         * (3) subscribe() requires the observable to have been created with shared pointer
         * support - this is why every observable implementation in src/ carries
         * enableSharedPtr = true
         */

        typedef om::ObjectImpl< MonotonicCounterObservableT<> > no_shared_t;

        const auto observerImpl = CountingObserverImpl::createInstance();

        UTF_REQUIRE_THROW(
            no_shared_t::createInstance() -> subscribe( om::qi< reactive::Observer >( observerImpl ) ),
            bl::InterfaceNotSupportedException
            );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ShutdownTaskCancelTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * A ShutdownTask is constructed, scheduled and then cancelled on every CI run -
     * tasks::startAcceptor() ends with requestCancel() followed by a plain wait() - but the
     * contract which matters is asserted nowhere: cancellation must never be mistaken for a
     * shutdown request
     *
     * cancelTask() cancels the signal set, so onStop() runs with operation_aborted and
     * BL_TASKS_HANDLER_BEGIN_CHK_EC() short circuits before the callbacks are reached; an
     * onStop() which fired them anyway would make every server run its whole shutdown
     * sequence when it is merely being torn down
     *
     * There is no signal involved here at all, so this is entirely deterministic
     */

    scheduleAndExecuteInParallel(
        []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            std::atomic< int > counter1( 0 );
            std::atomic< int > counter2( 0 );

            const auto shutdownWatcher = ShutdownTaskImpl::createInstance();

            shutdownWatcher -> registerCallback(
                cpp::void_callback_t(
                    [ &counter1 ]() -> void
                    {
                        ++counter1;
                    }
                    )
                );

            shutdownWatcher -> registerCallback(
                cpp::void_callback_t(
                    [ &counter2 ]() -> void
                    {
                        ++counter2;
                    }
                    )
                );

            /*
             * The victim is deliberately never pushed anywhere - the point is that the
             * requestCancel() which registerTask() bound was not invoked; it is held as a
             * concrete pointer because isCanceled() is public on TaskBase
             */

            const auto victim = SimpleTaskImpl::createInstance( cpp::void_callback_t( []() -> void {} ) );

            shutdownWatcher -> registerTask( om::qi< Task >( victim ) );

            const auto taskShutdownWatcher = om::qi< Task >( shutdownWatcher );

            eq -> push_back( taskShutdownWatcher );

            /*
             * cancelTask() is only called for a task which is already running, so the task
             * has to be given the chance to arm its signal set first - the sleep below is
             * the polling interval of a bounded loop, not a timing assumption
             */

            bool running = false;

            for( std::size_t i = 0U; i < 1000U; ++i )
            {
                if( Task::Running == taskShutdownWatcher -> getState() )
                {
                    running = true;

                    break;
                }

                os::sleep( time::milliseconds( 10 ) );
            }

            UTF_REQUIRE( running );

            /*
             * cancelAndWaitForSuccess() filters operation_aborted, so this also pins that
             * onStop() classifies the aborted wait as an expected exception
             */

            UTF_REQUIRE_NO_THROW( cancelAndWaitForSuccess( eq, taskShutdownWatcher ) );

            UTF_REQUIRE_EQUAL( 0, counter1.load() );
            UTF_REQUIRE_EQUAL( 0, counter2.load() );

            UTF_REQUIRE( ! victim -> isCanceled() );

            UTF_REQUIRE_EQUAL( Task::Completed, taskShutdownWatcher -> getState() );
        }
        );
}

UTF_AUTO_TEST_CASE( Tasks_TimerTaskIgnoresLocalThreadPoolTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * TimerTaskBaseT::resetTimer() constructs its deadline timer on
     * ThreadPoolDefault::getDefault( getThreadPoolId() ) -> aioService() and never consults
     * the execution queue, while SimpleTaskBaseT::scheduleTask() goes through
     * getThreadPool( eq ) and does honour the queue's local thread pool
     *
     * Because every ObservableBase *is* a TimerTaskBase, that split decides where every
     * reactive pipeline's timer runs, and nothing in the suite states it - 'fixing'
     * TimerTaskBase to honour the local pool would move all of them and nothing would fail
     *
     * This case pins the CURRENT behaviour; changing it is a deliberate and visible decision
     */

    UTF_MESSAGE(
        "Tasks_TimerTaskIgnoresLocalThreadPoolTests pins current behaviour: a timer task does "
        "NOT honour the execution queue local thread pool while a simple task does"
        );

    /*
     * The precondition, and the reason this arm lives in utf_baselib_tasks rather than in
     * utf_baselib_basictask - a timer task reaching resetTimer() without the global default
     * pools would dereference a null pointer
     */

    UTF_REQUIRE( nullptr != ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose ).get() );

    const auto tpLocal = om::lockDisposable(
        ThreadPoolImpl::createInstance< ThreadPool >(
            os::AbstractPriority::Normal,
            1U                                          /* threadsCount */
            )
        );

    ThreadIdProbe poolProbe;
    ThreadIdProbe timerProbe;
    ThreadIdProbe simpleProbe;

    /*
     * Learn the id of the single worker of the local pool by posting a probe on its own
     * io service
     */

    tpLocal -> aioService().post(
        [ &poolProbe ]() -> void
        {
            poolProbe.set( std::this_thread::get_id() );
        }
        );

    UTF_REQUIRE( poolProbe.wait() );

    {
        /*
         * The queue is torn down before the local pool it points at
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        eq -> setLocalThreadPool( tpLocal.get() );

        /*
         * Asserted at the point of the push, so a failure below cannot be blamed on the
         * local pool not having been set
         */

        UTF_REQUIRE_EQUAL( tpLocal.get(), eq -> getLocalThreadPool() );

        const auto timerTask = SimpleTimerTask::createInstance< Task >(
            [ &timerProbe ]() -> bool
            {
                timerProbe.set( std::this_thread::get_id() );

                /*
                 * Returning false terminates the timer task
                 */

                return false;
            },
            time::seconds( 10 )                         /* duration */,
            time::time_duration()                       /* initDelay */
            );

        eq -> push_back( timerTask );

        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( timerTask ) );

        /*
         * The in-case control - a simple task on the very same queue
         */

        const auto simpleTask = SimpleTaskImpl::createInstance< Task >(
            cpp::void_callback_t(
                [ &simpleProbe ]() -> void
                {
                    simpleProbe.set( std::this_thread::get_id() );
                }
                )
            );

        eq -> push_back( simpleTask );

        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( simpleTask ) );
    }

    UTF_REQUIRE( timerProbe.wait() );
    UTF_REQUIRE( simpleProbe.wait() );

    const auto localPoolThreadId = poolProbe.value();
    const auto timerBodyThreadId = timerProbe.value();
    const auto simpleTaskThreadId = simpleProbe.value();

    /*
     * The load bearing assertion - the timer body did not run on the queue's local pool
     */

    UTF_REQUIRE( timerBodyThreadId != localPoolThreadId );

    /*
     * ... and the control, which is what makes the divergence legible
     */

    UTF_REQUIRE_EQUAL( localPoolThreadId, simpleTaskThreadId );
}
