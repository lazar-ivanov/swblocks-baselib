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

#include <baselib/reactive/FanoutTasksObservable.h>
#include <baselib/reactive/InputConnector.h>
#include <baselib/reactive/ObservableBase.h>
#include <baselib/reactive/Observer.h>
#include <baselib/reactive/ObserverBase.h>
#include <baselib/reactive/ProcessingUnit.h>

#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/ExecutionQueueNotify.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/ErrorDispatcher.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <vector>

#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

/************************************************************************
 * Observer and observable contract fixtures
 *
 * Note that these headers are all pulled into the same translation unit, so the fixtures
 * of TestTasks.h - MonotonicCounterObservableImpl in particular - are visible here
 */

namespace
{
    /**
     * @brief An observer which accepts every value and counts what it was told
     *
     * The counters are written on the events queue of the subscription and read from the
     * main test thread, so all of the state is guarded; waitForNextCount() is a bounded
     * rendezvous which replaces a sleep
     */

    template
    <
        typename E = void
    >
    class CountingObserverT :
        public bl::reactive::ObserverBase
    {
        BL_CTR_DEFAULT( CountingObserverT, protected )
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( CountingObserverT )

    protected:

        typedef bl::reactive::ObserverBase                                  base_type;

        mutable bl::os::mutex                                               m_lock;
        bl::os::condition_variable                                          m_cv;
        std::vector< std::size_t >                                          m_values;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_nextCount;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_completedCount;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_errorCount;

    public:

        std::size_t nextCount() const NOEXCEPT
        {
            BL_MUTEX_GUARD( m_lock );

            return m_nextCount;
        }

        std::size_t completedCount() const NOEXCEPT
        {
            BL_MUTEX_GUARD( m_lock );

            return m_completedCount;
        }

        std::size_t errorCount() const NOEXCEPT
        {
            BL_MUTEX_GUARD( m_lock );

            return m_errorCount;
        }

        /**
         * @brief The values which were delivered, in delivery order
         *
         * Only values which are boxed as std::size_t are recorded - the observables which
         * push other value types are only counted
         */

        std::vector< std::size_t > values() const
        {
            BL_MUTEX_GUARD( m_lock );

            return m_values;
        }

        bool waitForNextCount( SAA_in const std::size_t count )
        {
            bl::os::mutex_unique_lock guard( m_lock );

            return m_cv.wait_for(
                guard,
                bl::os::chrono::seconds( 10 ),
                [ this, count ]() -> bool
                {
                    return m_nextCount >= count;
                }
                );
        }

        virtual void onCompleted() OVERRIDE
        {
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_completedCount;
            }

            m_cv.notify_all();

            base_type::onCompleted();
        }

        virtual void onError( SAA_in const std::exception_ptr& eptr ) OVERRIDE
        {
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_errorCount;
            }

            m_cv.notify_all();

            base_type::onError( eptr );
        }

        virtual bool onNext( SAA_in const bl::cpp::any& value ) OVERRIDE
        {
            {
                BL_MUTEX_GUARD( m_lock );

                const auto* const index = bl::cpp::any_cast< std::size_t >( &value );

                if( index )
                {
                    m_values.push_back( *index );
                }

                ++m_nextCount;
            }

            m_cv.notify_all();

            return true;
        }
    };

    typedef bl::om::ObjectImpl< CountingObserverT<> > CountingObserverImpl;

    /**
     * @brief A counting observer whose gateCallIndex-th onNext parks until the test thread
     * opens the gate
     *
     * Parking the observer is what makes the notifyOnNext( ... ) throttle limit observable
     * without any timing window - the events queue of the subscription is stranded, so
     * while the observer is inside onNext everything else queues up behind it
     */

    template
    <
        typename E = void
    >
    class BlockingCountingObserverT :
        public CountingObserverT< E >
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( BlockingCountingObserverT )

    protected:

        typedef CountingObserverT< E >                                      base_type;

        utest::TestSignal&                                                  m_gate;
        const std::size_t                                                   m_gateCallIndex;
        utest::DeferredAssertions                                           m_assertions;

        BlockingCountingObserverT(
            SAA_inout           utest::TestSignal&                          gate,
            SAA_in              const std::size_t                           gateCallIndex
            )
            :
            m_gate( gate ),
            m_gateCallIndex( gateCallIndex )
        {
        }

    public:

        const utest::DeferredAssertions& assertions() const NOEXCEPT
        {
            return m_assertions;
        }

        virtual bool onNext( SAA_in const bl::cpp::any& value ) OVERRIDE
        {
            const auto accepted = base_type::onNext( value );

            if( base_type::nextCount() == m_gateCallIndex )
            {
                UTF_RECORD( m_assertions, m_gate.wait() );
            }

            return accepted;
        }
    };

    typedef bl::om::ObjectImpl< BlockingCountingObserverT<> > BlockingCountingObserverImpl;

    /**
     * @brief A counting observer which throws a caller supplied exception from its
     * throwOnCall-th onNext and accepts every other value
     */

    template
    <
        typename E = void
    >
    class ThrowingObserverT :
        public CountingObserverT< E >
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ThrowingObserverT )

    protected:

        typedef CountingObserverT< E >                                      base_type;

        const std::size_t                                                   m_throwOnCall;
        const bl::cpp::void_callback_t                                      m_thrower;

        ThrowingObserverT(
            SAA_in              const std::size_t                           throwOnCall,
            SAA_in              bl::cpp::void_callback_t&&                  thrower
            )
            :
            m_throwOnCall( throwOnCall ),
            m_thrower( BL_PARAM_FWD( thrower ) )
        {
        }

    public:

        virtual bool onNext( SAA_in const bl::cpp::any& value ) OVERRIDE
        {
            const auto accepted = base_type::onNext( value );

            if( base_type::nextCount() == m_throwOnCall )
            {
                m_thrower();
            }

            return accepted;
        }
    };

    typedef bl::om::ObjectImpl< ThrowingObserverT<> > ThrowingObserverImpl;

    /**
     * @brief A counting observer whose first onCompleted() throws
     *
     * The counter is bumped before the throw, so 'exactly once' remains assertable
     */

    template
    <
        typename E = void
    >
    class ThrowingOnCompletedObserverT :
        public CountingObserverT< E >
    {
        BL_CTR_DEFAULT( ThrowingOnCompletedObserverT, protected )
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ThrowingOnCompletedObserverT )

    protected:

        typedef CountingObserverT< E >                                      base_type;

    public:

        virtual void onCompleted() OVERRIDE
        {
            base_type::onCompleted();

            if( 1U == base_type::completedCount() )
            {
                BL_CHK( false, false, BL_MSG() << "probe completed failure" );
            }
        }
    };

    typedef bl::om::ObjectImpl< ThrowingOnCompletedObserverT<> > ThrowingOnCompletedObserverImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveSubscriberFailureRoutingTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ObservableBaseT::checkSubscribersQueues() is the whole failure containment policy of
     * the reactive layer: an ObjectDisconnectedException raised by an observer unsubscribes
     * that observer quietly, while any other exception unsubscribes it *and* fails the
     * observable with the observer's own exception
     *
     * The throttle limit of one is what makes the routing observable without any timing
     * window - notifyOnNext() cannot admit the next value until every subscription queue has
     * drained the previous one, so by the time checkSubscribersQueues() pops the failed
     * invoker task it is the only entry in that queue and unsubscribeInternal( id, false )
     * can dispose the queue and erase the subscription in the very same pass
     *
     * Note: m_ignoreSubscribersFailures (ObservableBase.h:363) is a protected member with no
     * setter anywhere in src/, so the 'silently disconnect the failed subscriber' arm is
     * dead code - only the reachable arm is covered here (planning concern PC-11)
     */

    {
        /*
         * The disconnect arm - a disconnected subscriber must not fail the observable
         */

        scheduleAndExecuteInParallel(
            []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableImpl = om::getSharedPtr(
                    MonotonicCounterObservableImpl::createInstance(
                        false               /* throwFromInnerLoop */,
                        8U                  /* ticksCount */,
                        0U                  /* intervalInMilliseconds */
                        )
                    );

                const auto observable = om::qi< reactive::Observable >( observableImpl );

                observableImpl -> setThrottleLimit( 1U );

                const auto throwingObserver = ThrowingObserverImpl::createInstance(
                    2U /* throwOnCall */,
                    []() -> void
                    {
                        BL_THROW( ObjectDisconnectedException(), BL_MSG() << "probe disconnect" );
                    }
                    );

                const auto healthyObserver = CountingObserverImpl::createInstance();

                /*
                 * The subscription handles have to be held - ~ObserverDisposerT() disposes,
                 * so discarding one unsubscribes the observer immediately
                 */

                const auto throwingSubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( throwingObserver ) );

                const auto healthySubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( healthyObserver ) );

                BL_UNUSED( throwingSubscription );
                BL_UNUSED( healthySubscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                /*
                 * Two reads of the same counter after the observable has completed - the
                 * disconnected subscriber must have stopped receiving, i.e. it really was
                 * unsubscribed
                 *
                 * The bound is deliberate and must not be tightened to an equality: the
                 * per-subscription queue keeps draining the invoker tasks which were already
                 * queued behind the failed one, and checkSubscribersQueues() only runs at the
                 * top of the next observable iteration
                 */

                const auto throwingNextCount = throwingObserver -> nextCount();
                const auto throwingNextCountAgain = throwingObserver -> nextCount();
                const auto throwingCompletedCount = throwingObserver -> completedCount();
                const auto healthyNextCount = healthyObserver -> nextCount();
                const auto healthyCompletedCount = healthyObserver -> completedCount();

                UTF_REQUIRE( throwingNextCount >= 2U );
                UTF_REQUIRE_EQUAL( throwingNextCountAgain, throwingNextCount );
                UTF_REQUIRE_EQUAL( throwingCompletedCount, 0U );
                UTF_REQUIRE_EQUAL( healthyNextCount, 8U );
                UTF_REQUIRE_EQUAL( healthyCompletedCount, 1U );
            }
            );
    }

    {
        /*
         * The genuine failure arm - the observer's exception becomes the observable's own
         */

        scheduleAndExecuteInParallel(
            []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observableImpl = om::getSharedPtr(
                    MonotonicCounterObservableImpl::createInstance(
                        false               /* throwFromInnerLoop */,
                        8U                  /* ticksCount */,
                        0U                  /* intervalInMilliseconds */
                        )
                    );

                const auto observable = om::qi< reactive::Observable >( observableImpl );

                observableImpl -> setThrottleLimit( 1U );

                const auto throwingObserver = ThrowingObserverImpl::createInstance(
                    2U /* throwOnCall */,
                    []() -> void
                    {
                        BL_CHK( false, false, BL_MSG() << "probe subscriber failure" );
                    }
                    );

                const auto healthyObserver = CountingObserverImpl::createInstance();

                /*
                 * The subscription handles have to be held - ~ObserverDisposerT() disposes,
                 * so discarding one unsubscribes the observer immediately
                 */

                const auto throwingSubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( throwingObserver ) );

                const auto healthySubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( healthyObserver ) );

                BL_UNUSED( throwingSubscription );
                BL_UNUSED( healthySubscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "probe subscriber failure"
                    );

                /*
                 * The failed subscription is erased by the loop at ObservableBase.h:522-526,
                 * which runs before the rethrow at 541-543, so it gets neither onError nor
                 * onCompleted while the healthy one gets exactly one of each
                 */

                const auto throwingErrorCount = throwingObserver -> errorCount();
                const auto throwingCompletedCount = throwingObserver -> completedCount();
                const auto healthyErrorCount = healthyObserver -> errorCount();
                const auto healthyCompletedCount = healthyObserver -> completedCount();

                UTF_REQUIRE_EQUAL( throwingErrorCount, 0U );
                UTF_REQUIRE_EQUAL( throwingCompletedCount, 0U );
                UTF_REQUIRE_EQUAL( healthyErrorCount, 1U );
                UTF_REQUIRE_EQUAL( healthyCompletedCount, 1U );
            }
            );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ReactiveOnCompletedExactlyOnceAndLifecycleTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * An observable which completed normally has already scheduled and delivered one
     * onCompleted per subscription; a later dispose() re-pushes a completion for every
     * subscription which is not disposing yet (ObservableBase.h:649-657) because the flush
     * it does first would otherwise discard a completion which had merely been queued
     *
     * The only thing which keeps that from being delivered twice is the flag inside
     * SingletonOnCompleteExecute::callOnCompleted(), which is set *before* the observer is
     * called - moving it after the call would produce a duplicate completion, which is a
     * contract break for every observer that releases resources there
     *
     * The same sequence also pins the dispose lifecycle - the post-dispose subscribe
     * message, dispose idempotence, unsubscribe() of an unknown id and the
     * allowNoSubscribers accessor pair
     */

    const auto cbLifecycle = [](
        SAA_in              const om::ObjPtr< ExecutionQueue >&                 eq,
        SAA_in              const om::ObjPtr< reactive::Observer >&             observer,
        SAA_in              const cpp::function< std::size_t () >&              cbCompletedCount
        ) -> void
    {
        const auto observableImpl = om::getSharedPtr(
            MonotonicCounterObservableImpl::createInstance(
                false                   /* throwFromInnerLoop */,
                3U                      /* ticksCount */,
                0U                      /* intervalInMilliseconds */
                )
            );

        const auto observable = om::qi< reactive::Observable >( observableImpl );

        /*
         * An id which was never issued must be reported as not found - before anything has
         * been subscribed, after the run and after the observable has been disposed
         */

        UTF_REQUIRE( ! observableImpl -> unsubscribe( uuids::create() ) );

        /*
         * The subscription handle has to be held - ~ObserverDisposerT() disposes, so
         * discarding one unsubscribes the observer immediately
         */

        const auto subscription = observable -> subscribe( observer );

        BL_UNUSED( subscription );

        const auto task = om::qi< Task >( observable.get() );

        eq -> push_back( task );

        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

        const auto completedAfterRun = cbCompletedCount();

        UTF_REQUIRE( ! observableImpl -> unsubscribe( uuids::create() ) );

        /*
         * dispose() is NOEXCEPT wrapped and retries every 50 ms until the events queues are
         * flushed, so this also pins that it terminates
         */

        UTF_REQUIRE_NO_THROW( om::qi< om::Disposable >( observable ) -> dispose() );

        const auto completedAfterDispose = cbCompletedCount();

        UTF_REQUIRE_THROW_MESSAGE(
            observableImpl -> subscribe( observer ),
            bl::UnexpectedException,
            "Attempt to subscribe to observable which has been disposed"
            );

        /*
         * The second dispose must be a no-op - it returns through the early out in
         * tryDisposeInternal(), which also asserts the subscription map is empty
         */

        UTF_REQUIRE_NO_THROW( om::qi< om::Disposable >( observable ) -> dispose() );

        UTF_REQUIRE( ! observableImpl -> unsubscribe( uuids::create() ) );

        observableImpl -> allowNoSubscribers( true );

        UTF_REQUIRE( observableImpl -> allowNoSubscribers() );

        const auto completedAfterSecondDispose = cbCompletedCount();

        UTF_REQUIRE_EQUAL( completedAfterRun, 1U );
        UTF_REQUIRE_EQUAL( completedAfterDispose, 1U );
        UTF_REQUIRE_EQUAL( completedAfterSecondDispose, 1U );
    };

    {
        scheduleAndExecuteInParallel(
            [ &cbLifecycle ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observerImpl = CountingObserverImpl::createInstance();

                cbLifecycle(
                    eq,
                    om::qi< reactive::Observer >( observerImpl ),
                    [ &observerImpl ]() -> std::size_t
                    {
                        return observerImpl -> completedCount();
                    }
                    );

                UTF_REQUIRE_EQUAL( observerImpl -> nextCount(), 3U );
                UTF_REQUIRE_EQUAL( observerImpl -> errorCount(), 0U );
            }
            );
    }

    {
        /*
         * The same sequence with an observer whose first onCompleted() throws - the throw is
         * swallowed and logged by notifyObserverComplete()'s NOEXCEPT wrapper, so neither the
         * observable task nor dispose() must propagate it, and the observer must still be
         * told exactly once
         *
         * Note that the harness turns every warning it sees into a test failure, so the
         * line logger is redirected for the duration of this sub-block - the diagnostic is
         * still written to the module log, just not as an error
         */

        const Logging::LineLoggerPusher pushLogger( &utest::warningToDebugLineLogger );

        scheduleAndExecuteInParallel(
            [ &cbLifecycle ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto observerImpl = ThrowingOnCompletedObserverImpl::createInstance();

                cbLifecycle(
                    eq,
                    om::qi< reactive::Observer >( observerImpl ),
                    [ &observerImpl ]() -> std::size_t
                    {
                        return observerImpl -> completedCount();
                    }
                    );
            }
            );
    }
}

namespace
{
    /**
     * @brief An observable which pushes a fixed number of std::size_t values and records
     * how many of them notifyOnNext( ... ) rejected
     *
     * It also verifies, on the spot, that a rejected value was not consumed - the value is
     * only moved into the shared holder after every subscription has passed the throttle
     * check, so the caller can retry with the very same value
     */

    template
    <
        typename E = void
    >
    class ThrottleProbeObservableT :
        public bl::reactive::ObservableBase
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ThrottleProbeObservableT )

    protected:

        typedef bl::reactive::ObservableBase                                base_type;

        const std::size_t                                                   m_valuesCount;
        std::atomic< std::size_t >                                          m_pushed;
        std::atomic< std::size_t >                                          m_rejected;
        utest::TestSignal                                                   m_firstRejection;
        utest::DeferredAssertions                                           m_assertions;

        ThrottleProbeObservableT( SAA_in const std::size_t valuesCount )
            :
            m_valuesCount( valuesCount ),
            m_pushed( 0U ),
            m_rejected( 0U )
        {
        }

        virtual void tryStopObservable() OVERRIDE
        {
            BL_ASSERT( base_type::m_stopRequested );
        }

        virtual bl::time::time_duration chk2LoopUntilFinished() OVERRIDE
        {
            using namespace bl;

            const std::size_t pushed = m_pushed;

            if( base_type::m_stopRequested || pushed == m_valuesCount )
            {
                return time::neg_infin;
            }

            cpp::any value( pushed );

            if( base_type::notifyOnNext( std::move( value ) ) )
            {
                m_pushed = pushed + 1U;
            }
            else
            {
                UTF_RECORD( m_assertions, ! value.empty() );

                if( 0U == m_rejected )
                {
                    m_firstRejection.signal();
                }

                m_rejected = m_rejected + 1U;
            }

            return time::milliseconds( 0 );
        }

    public:

        std::size_t pushedCount() const NOEXCEPT
        {
            return m_pushed;
        }

        std::size_t rejectedCount() const NOEXCEPT
        {
            return m_rejected;
        }

        bool waitForFirstRejection()
        {
            return m_firstRejection.wait();
        }

        const utest::DeferredAssertions& assertions() const NOEXCEPT
        {
            return m_assertions;
        }
    };

    typedef bl::om::ObjectImpl< ThrottleProbeObservableT<>, true /* enableSharedPtr */ >
        ThrottleProbeObservableImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveNotifyOnNextThrottleTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * notifyOnNext() walks *all* the subscriptions before it touches the value: if any one
     * of them is at the throttle limit it returns false without consuming the value and
     * without pushing to any subscription at all
     *
     * Both halves matter - a value which was consumed on rejection is silently lost data
     * for every caller which retries on false (FilesPkgUnpkgBase and
     * RecursiveDirectoryScanner both do), and a fan-out which pushed to the subscriptions
     * it had already visited would deliver the same value twice on the retry
     */

    {
        /*
         * One subscriber, parked inside the very first onNext, so values 1..3 fill its
         * stranded events queue and value 4 has to be rejected
         */

        utest::TestSignal gate;

        scheduleAndExecuteInParallel(
            [ &gate ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto probeImpl = om::getSharedPtr(
                    ThrottleProbeObservableImpl::createInstance( 10U /* valuesCount */ )
                    );

                const auto observable = om::qi< reactive::Observable >( probeImpl );

                probeImpl -> setThrottleLimit( 3U );

                const auto observer = BlockingCountingObserverImpl::createInstance(
                    gate,
                    1U /* gateCallIndex */
                    );

                /*
                 * The subscription handle has to be held - ~ObserverDisposerT() disposes, so
                 * discarding one unsubscribes the observer immediately
                 */

                const auto subscription =
                    observable -> subscribe( om::qi< reactive::Observer >( observer ) );

                BL_UNUSED( subscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                const auto firstRejectionSeen = probeImpl -> waitForFirstRejection();

                gate.signal();

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                const auto rejectedCount = probeImpl -> rejectedCount();
                const auto pushedCount = probeImpl -> pushedCount();
                const auto values = observer -> values();

                UTF_REQUIRE( firstRejectionSeen );
                UTF_REQUIRE( rejectedCount > 0U );
                UTF_REQUIRE_EQUAL( pushedCount, 10U );
                UTF_REQUIRE_EQUAL( values.size(), 10U );

                for( std::size_t i = 0U; i < values.size(); ++i )
                {
                    UTF_REQUIRE_EQUAL( values[ i ], i );
                }

                probeImpl -> assertions().requireNone();
                observer -> assertions().requireNone();
            }
            );
    }

    {
        /*
         * Two subscribers, one parked and one free running - the free running one must not
         * be able to run ahead of the parked one, which is what makes the fan-out
         * all-or-nothing
         */

        utest::TestSignal gate;

        scheduleAndExecuteInParallel(
            [ &gate ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto probeImpl = om::getSharedPtr(
                    ThrottleProbeObservableImpl::createInstance( 10U /* valuesCount */ )
                    );

                const auto observable = om::qi< reactive::Observable >( probeImpl );

                probeImpl -> setThrottleLimit( 3U );

                const auto blockedObserver = BlockingCountingObserverImpl::createInstance(
                    gate,
                    1U /* gateCallIndex */
                    );

                const auto freeObserver = CountingObserverImpl::createInstance();

                /*
                 * The subscription handles have to be held - ~ObserverDisposerT() disposes,
                 * so discarding one unsubscribes the observer immediately
                 */

                const auto blockedSubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( blockedObserver ) );

                const auto freeSubscription =
                    observable -> subscribe( om::qi< reactive::Observer >( freeObserver ) );

                BL_UNUSED( blockedSubscription );
                BL_UNUSED( freeSubscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                const auto firstRejectionSeen = probeImpl -> waitForFirstRejection();

                /*
                 * A bounded rendezvous rather than a sleep - the free running observer can
                 * only ever see the three values which were admitted before the throttle
                 * engaged, so once it has drained them its counter cannot move again while
                 * the other observer is parked
                 */

                const auto freeObserverDrained = freeObserver -> waitForNextCount( 3U );

                const auto pushedAtRejection = probeImpl -> pushedCount();
                const auto freeNextCountAtRejection = freeObserver -> nextCount();
                const auto blockedNextCountAtRejection = blockedObserver -> nextCount();

                gate.signal();

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                const auto pushedCount = probeImpl -> pushedCount();
                const auto blockedValues = blockedObserver -> values();
                const auto freeValues = freeObserver -> values();

                UTF_REQUIRE( firstRejectionSeen );
                UTF_REQUIRE( freeObserverDrained );
                UTF_REQUIRE_EQUAL( pushedAtRejection, 3U );
                UTF_REQUIRE_EQUAL( freeNextCountAtRejection, 3U );
                UTF_REQUIRE_EQUAL( blockedNextCountAtRejection, 1U );
                UTF_REQUIRE_EQUAL( pushedCount, 10U );
                UTF_REQUIRE_EQUAL( blockedValues.size(), 10U );
                UTF_REQUIRE_EQUAL( freeValues.size(), 10U );

                for( std::size_t i = 0U; i < blockedValues.size(); ++i )
                {
                    UTF_REQUIRE_EQUAL( blockedValues[ i ], i );
                    UTF_REQUIRE_EQUAL( freeValues[ i ], i );
                }

                probeImpl -> assertions().requireNone();
                blockedObserver -> assertions().requireNone();
            }
            );
    }
}

namespace
{
    /**
     * @brief A fanout observable whose only child task fails
     *
     * It counts the pushReadyTask() and flushAllPendingTasks() calls it receives and can be
     * configured to take either side of the allowPushingOfFailedChildTasks() policy hook
     */

    template
    <
        typename E = void
    >
    class FanoutChildFailureProbeT :
        public bl::reactive::FanoutTasksObservable
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( FanoutChildFailureProbeT )

    protected:

        typedef bl::reactive::FanoutTasksObservable                         base_type;

        const bool                                                          m_allowPushingOfFailedChildTasks;
        std::atomic< std::size_t >                                          m_pushReadyTaskCount;
        std::atomic< std::size_t >                                          m_flushAllPendingTasksCount;

        FanoutChildFailureProbeT( SAA_in const bool allowPushingOfFailedChildTasks )
            :
            m_allowPushingOfFailedChildTasks( allowPushingOfFailedChildTasks ),
            m_pushReadyTaskCount( 0U ),
            m_flushAllPendingTasksCount( 0U )
        {
        }

        virtual bl::om::ObjPtr< bl::tasks::Task > createSeedingTask() OVERRIDE
        {
            return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                []() -> void
                {
                    BL_CHK( false, false, BL_MSG() << "probe child failure" );
                }
                );
        }

        virtual bool canAcceptReadyTask() OVERRIDE
        {
            return true;
        }

        virtual bool pushReadyTask( SAA_in const bl::om::ObjPtrCopyable< bl::tasks::Task >& task ) OVERRIDE
        {
            BL_UNUSED( task );

            m_pushReadyTaskCount = m_pushReadyTaskCount + 1U;

            return true;
        }

        virtual bool flushAllPendingTasks() OVERRIDE
        {
            m_flushAllPendingTasksCount = m_flushAllPendingTasksCount + 1U;

            return true;
        }

        virtual bool isWaitingExternalInput() NOEXCEPT OVERRIDE
        {
            return false;
        }

        virtual bool allowPushingOfFailedChildTasks() const NOEXCEPT OVERRIDE
        {
            return m_allowPushingOfFailedChildTasks;
        }

    public:

        std::size_t pushReadyTaskCount() const NOEXCEPT
        {
            return m_pushReadyTaskCount;
        }

        std::size_t flushAllPendingTasksCount() const NOEXCEPT
        {
            return m_flushAllPendingTasksCount;
        }
    };

    typedef bl::om::ObjectImpl< FanoutChildFailureProbeT<>, true /* enableSharedPtr */ >
        FanoutChildFailureProbeImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveFanoutChildTaskFailureTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * This is the primary error path of the class which RecursiveDirectoryScanner,
     * FilesPackagerUnit and FilesUnpackagerUnit all inherit unchanged
     *
     * Under the default policy a failed child task is never handed to pushReadyTask() - the
     * observable saves the child's exception, breaks out of the pump, drains the child queue
     * through cancelAll( false ) and then rethrows the child's own exception from
     * chk2LoopUntilFinished(), which is fanned out as onError followed by onCompleted
     *
     * Note that this uses its own probe rather than the ReactiveFanoutProbeT of TestTasks2.h,
     * which has no failing seeding task and no counters
     */

    {
        /*
         * The default policy - allowPushingOfFailedChildTasks() returns false
         */

        scheduleAndExecuteInParallel(
            []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto probeImpl = om::getSharedPtr(
                    FanoutChildFailureProbeImpl::createInstance( false /* allowPushingOfFailedChildTasks */ )
                    );

                const auto observable = om::qi< reactive::Observable >( probeImpl );

                const auto observer = CountingObserverImpl::createInstance();

                /*
                 * The subscription handle has to be held - ~ObserverDisposerT() disposes, so
                 * discarding one unsubscribes the observer immediately
                 */

                const auto subscription =
                    observable -> subscribe( om::qi< reactive::Observer >( observer ) );

                BL_UNUSED( subscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "probe child failure"
                    );

                const auto pushReadyTaskCount = probeImpl -> pushReadyTaskCount();
                const auto flushAllPendingTasksCount = probeImpl -> flushAllPendingTasksCount();
                const auto nextCount = observer -> nextCount();
                const auto errorCount = observer -> errorCount();
                const auto completedCount = observer -> completedCount();

                UTF_REQUIRE_EQUAL( pushReadyTaskCount, 0U );
                UTF_REQUIRE( flushAllPendingTasksCount > 0U );
                UTF_REQUIRE_EQUAL( nextCount, 0U );
                UTF_REQUIRE_EQUAL( errorCount, 1U );
                UTF_REQUIRE_EQUAL( completedCount, 1U );
            }
            );
    }

    {
        /*
         * The policy hook ChunksSendRecvBase depends on - a failed child task is handed to
         * pushReadyTask() and the observable is not failed by it
         */

        scheduleAndExecuteInParallel(
            []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                const auto probeImpl = om::getSharedPtr(
                    FanoutChildFailureProbeImpl::createInstance( true /* allowPushingOfFailedChildTasks */ )
                    );

                const auto observable = om::qi< reactive::Observable >( probeImpl );

                const auto observer = CountingObserverImpl::createInstance();

                /*
                 * The subscription handle has to be held - ~ObserverDisposerT() disposes, so
                 * discarding one unsubscribes the observer immediately
                 */

                const auto subscription =
                    observable -> subscribe( om::qi< reactive::Observer >( observer ) );

                BL_UNUSED( subscription );

                const auto task = om::qi< Task >( observable.get() );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                const auto pushReadyTaskCount = probeImpl -> pushReadyTaskCount();
                const auto flushAllPendingTasksCount = probeImpl -> flushAllPendingTasksCount();
                const auto errorCount = observer -> errorCount();
                const auto completedCount = observer -> completedCount();

                UTF_REQUIRE_EQUAL( pushReadyTaskCount, 1U );
                UTF_REQUIRE( flushAllPendingTasksCount > 0U );
                UTF_REQUIRE_EQUAL( errorCount, 0U );
                UTF_REQUIRE_EQUAL( completedCount, 1U );
            }
            );
    }
}

namespace
{
    /**
     * @brief An error dispatcher which records what was dispatched into it
     */

    template
    <
        typename E = void
    >
    class RecordingErrorDispatcherT :
        public bl::ErrorDispatcher
    {
        BL_CTR_DEFAULT( RecordingErrorDispatcherT, protected )
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( RecordingErrorDispatcherT, bl::ErrorDispatcher )

    protected:

        mutable bl::os::mutex                                               m_lock;
        bl::cpp::ScalarTypeIniter< std::size_t >                            m_dispatchCount;
        std::exception_ptr                                                  m_lastError;

    public:

        std::size_t dispatchCount() const NOEXCEPT
        {
            BL_MUTEX_GUARD( m_lock );

            return m_dispatchCount;
        }

        std::exception_ptr lastError() const
        {
            BL_MUTEX_GUARD( m_lock );

            return m_lastError;
        }

        virtual void dispatchException( SAA_in const std::exception_ptr eptr ) NOEXCEPT OVERRIDE
        {
            BL_MUTEX_GUARD( m_lock );

            ++m_dispatchCount;
            m_lastError = eptr;
        }
    };

    typedef bl::om::ObjectImpl< RecordingErrorDispatcherT<> > RecordingErrorDispatcherImpl;

    /**
     * @brief The same recorder, but also a task
     *
     * InputConnectorT::chkTargetActive() only throws when the error dispatcher it was given
     * QIs to a tasks::Task whose state is not Running, so the target of that arm has to be
     * task shaped - and having the recorder inside it is what makes it directly observable
     * that the disconnect was *not* dispatched into the target
     */

    template
    <
        typename E = void
    >
    class RecordingErrorDispatcherTaskT :
        public bl::ErrorDispatcher,
        public bl::tasks::SimpleTaskBase
    {
        BL_CTR_DEFAULT( RecordingErrorDispatcherTaskT, protected )
        BL_DECLARE_OBJECT_IMPL( RecordingErrorDispatcherTaskT )

        BL_QITBL_BEGIN()
            BL_QITBL_ENTRY( bl::ErrorDispatcher )
            BL_QITBL_ENTRY( bl::tasks::Task )
        BL_QITBL_END( bl::tasks::Task )

    protected:

        std::atomic< std::size_t >                                          m_dispatchCount;

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            /*
             * This task is never scheduled - it only exists so that it can be observed in a
             * state which is not Running
             */

            BL_TASKS_HANDLER_END()
        }

    public:

        std::size_t dispatchCount() const NOEXCEPT
        {
            return m_dispatchCount;
        }

        virtual void dispatchException( SAA_in const std::exception_ptr eptr ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( eptr );

            m_dispatchCount = m_dispatchCount + 1U;
        }
    };

    typedef bl::om::ObjectImpl< RecordingErrorDispatcherTaskT<> > RecordingErrorDispatcherTaskImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveInputConnectorTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * InputConnector is the single point where a downstream unit's failure is converted into
     * both 'fail the downstream task' and 'unsubscribe from the upstream observable'
     *
     * ObjectDisconnectedException propagates *without* being dispatched - dispatching it
     * would poison a unit which had merely finished - while every other exception is handed
     * to the error dispatcher and then rethrown, so the subscription is not left attached
     *
     * No task, queue or thread is needed for any of this
     */

    {
        /*
         * (1) An accepted value - nothing is dispatched
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE( connector -> onNext( cpp::any( 1 ) ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (2) A rejected value - chkTargetActive() is inert for a dispatcher which is not a
         * task, so the rejection is simply passed back to the observable
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return false;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE( ! connector -> onNext( cpp::any( 1 ) ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (3) A throwing input callback - dispatched into the target and then rethrown
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                BL_CHK( false, false, BL_MSG() << "probe input failure" );

                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_THROW_MESSAGE(
            connector -> onNext( cpp::any( 1 ) ),
            bl::UnexpectedException,
            "probe input failure"
            );

        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );
        UTF_REQUIRE( nullptr != recorder -> lastError() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( recorder -> lastError() ),
            bl::UnexpectedException,
            "probe input failure"
            );
    }

    {
        /*
         * (4) The target is a task which is not running - the rejected value turns into a
         * disconnect, and that disconnect must not be dispatched into the target
         */

        const auto recorder = RecordingErrorDispatcherTaskImpl::createInstance();

        UTF_REQUIRE( Task::Created == om::qi< Task >( recorder ) -> getState() );

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return false;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_THROW_MESSAGE(
            connector -> onNext( cpp::any( 1 ) ),
            bl::ObjectDisconnectedException,
            "is a task that is not running"
            );

        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (5) onCompleted prefers the bound completed callback and returns immediately
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        bool completedCalled = false;

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder ),
            [ &completedCalled ]() -> void
            {
                completedCalled = true;
            }
            );

        UTF_REQUIRE_NO_THROW( connector -> onCompleted() );
        UTF_REQUIRE( completedCalled );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (6) A throwing completed callback follows the same dispatch-then-rethrow rule
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder ),
            []() -> void
            {
                BL_CHK( false, false, BL_MSG() << "probe completed failure" );
            }
            );

        UTF_REQUIRE_THROW_MESSAGE(
            connector -> onCompleted(),
            bl::UnexpectedException,
            "probe completed failure"
            );

        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );
    }

    {
        /*
         * (7) With no completed callback bound it falls back to ObserverBase::onCompleted()
         */

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onCompleted() );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (8) ObserverBase::onError() logs and swallows - it must never rethrow into the
         * observable which is already failing
         */

        std::exception_ptr eptr;

        try
        {
            BL_CHK( false, false, BL_MSG() << "probe observer error" );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        UTF_REQUIRE( nullptr != eptr );

        const auto plainObserver = MonotonicCounterObserverImpl::createInstance();

        UTF_REQUIRE_NO_THROW( om::qi< reactive::Observer >( plainObserver ) -> onError( eptr ) );
    }

    {
        /*
         * (9) onError() hands the upstream error to the error dispatcher, so the target fails
         * with it instead of taking the onCompleted() which follows for a normal end of input
         *
         * It must not throw: the error is not the connector's own, and the notification task
         * which delivers it must not see an exception
         *
         * And it must hand over a deep copy, not the upstream exception object itself: the
         * upstream task and the target each enhance and dump the exception they fail with when
         * they complete, on different threads, so one object shared between them is written by
         * both at once
         */

        std::exception_ptr eptr;

        try
        {
            BL_THROW( UnexpectedException(), BL_MSG() << "probe upstream error" );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        UTF_REQUIRE( nullptr != eptr );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( eptr ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );

        const auto dispatched = recorder -> lastError();

        UTF_REQUIRE( nullptr != dispatched );
        UTF_REQUIRE( dispatched != eptr );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( dispatched ),
            UnexpectedException,
            "probe upstream error"
            );

        /*
         * Enhancing the copy, as a failing task does, must leave the upstream exception as it was
         */

        try
        {
            cpp::safeRethrowException( dispatched );
        }
        catch( eh::exception& e )
        {
            e << eh::errinfo_task_info( "probe task info" );
        }

        try
        {
            cpp::safeRethrowException( eptr );
        }
        catch( eh::exception& e )
        {
            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_task_info >( e ) );
        }
    }

    {
        /*
         * (10) A non-boost exception is never enhanced in place by a task - it is wrapped for
         * printing instead - so the target is given the very same exception
         */

        const auto eptr = std::make_exception_ptr( std::runtime_error( "probe non-boost error" ) );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( eptr ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );
        UTF_REQUIRE( recorder -> lastError() == eptr );
    }

    {
        /*
         * (11) A boost exception which cannot be cloned can be neither copied nor shared
         * safely, so it is only logged, as by a connector without an error dispatcher
         */

        class NonClonableBoostException :
            public std::exception,
            public boost::exception
        {
        };

        const auto eptr = std::make_exception_ptr( NonClonableBoostException() );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( eptr ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (12) Without an error dispatcher there is no target to fail, so onError() falls back
         * to ObserverBase::onError() and swallows
         */

        std::exception_ptr eptr;

        try
        {
            BL_CHK( false, false, BL_MSG() << "probe upstream error" );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        UTF_REQUIRE( nullptr != eptr );

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::ObjPtr< ErrorDispatcher >()
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( eptr ) );
    }

    {
        /*
         * (13) The nested exception chain is copied too. A link shared with the upstream
         * exception would be formatted by both tasks at once - eh::diagnostic_information()
         * walks the chain and Boost's formatter writes a cached string inside every link it
         * formats - which is the race of the top level, one level down
         */

        std::exception_ptr inner;

        try
        {
            BL_THROW( UnexpectedException(), BL_MSG() << "probe inner error" );
        }
        catch( std::exception& )
        {
            inner = std::current_exception();
        }

        std::exception_ptr outer;

        try
        {
            BL_THROW(
                UnexpectedException()
                    << eh::errinfo_nested_exception_ptr( inner ),
                BL_MSG()
                    << "probe outer error"
                );
        }
        catch( std::exception& )
        {
            outer = std::current_exception();
        }

        UTF_REQUIRE( nullptr != inner );
        UTF_REQUIRE( nullptr != outer );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( outer ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );

        const auto dispatched = recorder -> lastError();

        UTF_REQUIRE( nullptr != dispatched );
        UTF_REQUIRE( dispatched != outer );

        std::exception_ptr nestedCopy;

        try
        {
            cpp::safeRethrowException( dispatched );
        }
        catch( eh::exception& e )
        {
            const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( nullptr != nested );

            nestedCopy = *nested;
        }

        UTF_REQUIRE( nullptr != nestedCopy );
        UTF_REQUIRE( nestedCopy != inner );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( nestedCopy ),
            UnexpectedException,
            "probe inner error"
            );

        /*
         * Enhancing the copy's link, as the target's completion would, must leave the
         * upstream's inner exception as it was
         */

        try
        {
            cpp::safeRethrowException( nestedCopy );
        }
        catch( eh::exception& e )
        {
            e << eh::errinfo_task_info( "probe task info" );
        }

        try
        {
            cpp::safeRethrowException( inner );
        }
        catch( eh::exception& e )
        {
            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_task_info >( e ) );
        }

        /*
         * The chain was copied, not dropped: the formatter still reaches the inner message
         * through the copy
         */

        UTF_REQUIRE( cpp::contains( eh::diagnostic_information( dispatched ), "probe inner error" ) );
    }

    {
        /*
         * (14) A chain whose inner link is a non-boost exception is dispatched with that link
         * shared - it has no error info container, so there is nothing for two tasks to write
         */

        const auto inner = std::make_exception_ptr( std::runtime_error( "probe non-boost inner error" ) );

        std::exception_ptr outer;

        try
        {
            BL_THROW(
                UnexpectedException()
                    << eh::errinfo_nested_exception_ptr( inner ),
                BL_MSG()
                    << "probe outer error"
                );
        }
        catch( std::exception& )
        {
            outer = std::current_exception();
        }

        UTF_REQUIRE( nullptr != outer );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( outer ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );

        try
        {
            cpp::safeRethrowException( recorder -> lastError() );
        }
        catch( eh::exception& e )
        {
            const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( nullptr != nested );
            UTF_REQUIRE( *nested == inner );
        }
    }

    {
        /*
         * (15) A chain whose inner link is a boost exception which cannot be cloned can be
         * neither copied nor shared, so the whole chain is not dispatched - the rule of (11)
         * applied to a link
         */

        class NonClonableBoostException :
            public std::exception,
            public boost::exception
        {
        };

        const auto inner = std::make_exception_ptr( NonClonableBoostException() );

        std::exception_ptr outer;

        try
        {
            BL_THROW(
                UnexpectedException()
                    << eh::errinfo_nested_exception_ptr( inner ),
                BL_MSG()
                    << "probe outer error"
                );
        }
        catch( std::exception& )
        {
            outer = std::current_exception();
        }

        UTF_REQUIRE( nullptr != outer );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( outer ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 0U );
    }

    {
        /*
         * (16) A null nested link is left in the copy as it is: it shares no object, so there is
         * nothing to copy, and following it would rethrow a null exception_ptr, which aborts the
         * process (cpp::safeRethrowException())
         */

        std::exception_ptr outer;

        try
        {
            BL_THROW(
                UnexpectedException()
                    << eh::errinfo_nested_exception_ptr( std::exception_ptr() ),
                BL_MSG()
                    << "probe outer error with a null link"
                );
        }
        catch( std::exception& )
        {
            outer = std::current_exception();
        }

        UTF_REQUIRE( nullptr != outer );

        const auto recorder = RecordingErrorDispatcherImpl::createInstance();

        const auto connector = reactive::createInputConnector(
            []( SAA_in const cpp::any& ) -> bool
            {
                return true;
            },
            om::qi< ErrorDispatcher >( recorder )
            );

        UTF_REQUIRE_NO_THROW( connector -> onError( outer ) );
        UTF_REQUIRE_EQUAL( recorder -> dispatchCount(), 1U );

        const auto dispatched = recorder -> lastError();

        UTF_REQUIRE( nullptr != dispatched );
        UTF_REQUIRE( dispatched != outer );

        try
        {
            cpp::safeRethrowException( dispatched );
        }
        catch( eh::exception& e )
        {
            const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( nullptr != nested );
            UTF_REQUIRE( nullptr == *nested );
        }
    }
}

namespace
{
    /**
     * @brief An observable which fails on its first iteration
     */

    template
    <
        typename E = void
    >
    class FailingObservableT :
        public bl::reactive::ObservableBase
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( FailingObservableT )

    protected:

        typedef bl::reactive::ObservableBase                                base_type;

        FailingObservableT()
        {
        }

        virtual void tryStopObservable() OVERRIDE
        {
            BL_ASSERT( base_type::m_stopRequested );
        }

        virtual bl::time::time_duration chk2LoopUntilFinished() OVERRIDE
        {
            if( ! base_type::m_stopRequested )
            {
                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "upstream observable failure"
                    );
            }

            return bl::time::neg_infin;
        }
    };

    typedef bl::om::ObjectImpl< FailingObservableT<>, true /* enableSharedPtr */ >
        FailingObservableImpl;

    /**
     * @brief A unit which does nothing until its input completes
     */

    template
    <
        typename E = void
    >
    class IdleUntilInputCompletedUnitT :
        public bl::reactive::ObservableBase
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( IdleUntilInputCompletedUnitT )

    protected:

        typedef bl::reactive::ObservableBase                                base_type;

        std::atomic< bool >                                                 m_inputCompleted;

        IdleUntilInputCompletedUnitT()
            :
            m_inputCompleted( false )
        {
        }

        virtual void tryStopObservable() OVERRIDE
        {
            BL_ASSERT( base_type::m_stopRequested );
        }

        virtual bl::time::time_duration chk2LoopUntilFinished() OVERRIDE
        {
            if( base_type::m_stopRequested || m_inputCompleted )
            {
                return bl::time::neg_infin;
            }

            return bl::time::milliseconds( 10 );
        }

    public:

        bool onInput( SAA_in const bl::cpp::any& )
        {
            return true;
        }

        void onInputCompleted()
        {
            m_inputCompleted = true;
        }
    };

    typedef bl::om::ObjectImpl
        <
            bl::reactive::ProcessingUnit< IdleUntilInputCompletedUnitT<>, bl::reactive::Observable >,
            true /* enableSharedPtr */
        >
        IdleUntilInputCompletedUnitImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveInputConnectorPropagatesUpstreamErrorTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * A failing observable delivers onError() and then onCompleted() to its subscribers. A unit
     * bound through bindInputConnector() must fail with the upstream error: were the error
     * swallowed, the onCompleted() which follows would look like a normal end of input, and the
     * unit would either finish successfully or - if it checks its input for completeness - fail
     * with an error of its own which hides the real cause
     */

    scheduleAndExecuteInParallel(
        []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            typedef IdleUntilInputCompletedUnitImpl unit_t;

            const auto observableImpl = om::getSharedPtr( FailingObservableImpl::createInstance() );
            const auto unit = om::getSharedPtr( unit_t::createInstance() );

            const auto observable = om::qi< reactive::Observable >( observableImpl );

            /*
             * The subscription handle has to be held - ~ObserverDisposerT() disposes, so
             * discarding one unsubscribes the observer immediately
             */

            const auto subscription = observable -> subscribe(
                unit -> bindInputConnector< unit_t >( &unit_t::onInput, &unit_t::onInputCompleted )
                );

            BL_UNUSED( subscription );

            const auto observableTask = om::qi< Task >( observable.get() );
            const auto unitTask = om::qi< Task >( unit.get() );

            eq -> push_back( unitTask );
            eq -> push_back( observableTask );

            UTF_REQUIRE_THROW_MESSAGE(
                eq -> waitForSuccess( observableTask ),
                UnexpectedException,
                "upstream observable failure"
                );

            UTF_REQUIRE_THROW_MESSAGE(
                eq -> waitForSuccess( unitTask ),
                UnexpectedException,
                "upstream observable failure"
                );
        }
        );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueDisposeIdempotenceTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExecutionQueueImplT::dispose() flushes the queue, disconnects the observer proxy and
     * resets it, but it sets no disposed flag - m_shutdown is declared, initialised and then
     * never read or written again
     *
     * What is pinned here is the half of the post-dispose contract which holds today: a
     * disposed queue is empty and disposing it again is a no-op rather than an abort. The
     * other half - rejecting a push into a disposed queue, which today binds the ready
     * callback through a null observer proxy and leaves the task Running forever - needs a
     * production guard first and is deliberately not exercised here
     *
     * The queue is deliberately held through a plain om::ObjPtr rather than
     * om::lockDisposable, because the point is to call dispose() at a chosen moment and keep
     * using the reference afterwards
     */

    const auto eq = ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll );

    /*
     * ~ExecutionQueueImplT() aborts the process if the queue was never disposed, so the
     * disposal must survive a failing assertion as well
     */

    BL_SCOPE_EXIT(
        {
            eq -> dispose();
        }
        );

    const auto task = eq -> push_back(
        []() -> void
        {
        }
        );

    eq -> flush();

    UTF_REQUIRE( Task::Completed == task -> getState() );
    UTF_REQUIRE_EQUAL( 1U, eq -> getQueueSize( ExecutionQueue::Ready ) );

    eq -> flushAndDiscardReady();

    UTF_REQUIRE( eq -> isEmpty() );

    eq -> dispose();

    UTF_REQUIRE( eq -> isEmpty() );

    /*
     * Disposing again must be a no-op rather than an abort
     */

    UTF_REQUIRE_NO_THROW( eq -> dispose() );
    UTF_REQUIRE_NO_THROW( eq -> dispose() );

    UTF_REQUIRE( eq -> isEmpty() );
    UTF_REQUIRE_EQUAL( 0U, eq -> getQueueSize( ExecutionQueue::Ready ) );
}

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueueNotifyDisconnectIsNotABarrierTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * om::Proxy::disconnect() guarantees 'no callback bound after this point' and nothing
     * more - the observer is resolved from the proxy under the queue lock and bound into
     * onNotify, the lock is released, and only then is the callback invoked
     *
     * So a callback which was bound before the disconnect can still be running after it has
     * returned. TcpServerBase's comment at TcpBaseTasks.h:1836-1846 states the opposite
     * intent, and its two defensive late-notification branches exist precisely because the
     * stated intent is not achievable - removing them would make them reachable and wrong
     * again
     *
     * The case is fully deterministic: the hook only samples the flag after the test thread
     * has set it and signalled, so there is no timing window in either direction
     */

    ExecutionQueueNotificationTestContext context(
        ExecutionQueue::OptionKeepAll,
        ExecutionQueueNotify::DeliverySerialized
        );

    /*
     * One completion control per task - a single shared control would hand out its callbacks
     * in scheduling order, which does not say which task they belong to
     */

    ExecutionQueueCompletionControl controlA;
    ExecutionQueueCompletionControl controlB;

    const auto taskA = om::qi< Task >( createControlledCompletionTask( controlA ) );
    const auto taskB = om::qi< Task >( createControlledCompletionTask( controlB ) );

    context.eq -> push_back( taskA );
    context.eq -> push_back( taskB );

    UTF_REQUIRE( controlA.waitUntilScheduled( 1U ) );
    UTF_REQUIRE( controlB.waitUntilScheduled( 1U ) );

    utest::TestSignal insideCallback;
    utest::TestSignal releaseCallback;
    utest::DeferredAssertions assertions;

    std::atomic< bool > disconnectReturned( false );
    std::atomic< bool > sawDisconnectedDelivery( false );

    context.recorder -> setHook(
        [ &insideCallback, &releaseCallback, &assertions, &disconnectReturned, &sawDisconnectedDelivery, &taskA ](
            SAA_in              const ExecutionQueueNotify::EventId                      eventId,
            SAA_in_opt          const om::ObjPtrCopyable< Task >&                        task
            ) -> void
        {
            if( ExecutionQueueNotify::TaskReady != eventId || ! om::areEqual( task, taskA ) )
            {
                return;
            }

            insideCallback.signal();

            UTF_RECORD( assertions, releaseCallback.wait() );

            sawDisconnectedDelivery = disconnectReturned.load();
        }
        );

    /*
     * Task A is completed on its own thread, so its TaskReady callback is entered there and
     * parks inside the hook
     */

    os::thread completionThreadA(
        [ &controlA ]() -> void
        {
            controlA.completeNext();
        }
        );

    BL_SCOPE_EXIT(
        {
            releaseCallback.signal();

            if( completionThreadA.joinable() )
            {
                completionThreadA.join();
            }
        }
        );

    const auto insideCallbackEntered = insideCallback.wait();

    context.notifyProxy -> disconnect();

    disconnectReturned = true;

    releaseCallback.signal();

    completionThreadA.join();

    /*
     * Task B is completed only after the in-flight delivery has finished, so its callback is
     * bound - or rather is not bound - strictly after the disconnect
     */

    const auto completedB = controlB.completeNext();

    const auto taskReadyCountA =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskReady, taskA.get() );

    const auto taskReadyCountB =
        context.recorder -> eventCount( ExecutionQueueNotify::TaskReady, taskB.get() );

    const auto readyQueueSize = context.eq -> getQueueSize( ExecutionQueue::Ready );

    UTF_REQUIRE( insideCallbackEntered );
    UTF_REQUIRE( completedB );
    UTF_REQUIRE( sawDisconnectedDelivery );
    UTF_REQUIRE_EQUAL( 1U, taskReadyCountA );
    UTF_REQUIRE_EQUAL( 0U, taskReadyCountB );
    UTF_REQUIRE( ! context.recorder -> hookFailed() );
    UTF_REQUIRE( readyQueueSize >= 1U );

    assertions.requireNone();
}

namespace
{
    /**
     * @brief A processing unit base which has a member function of every shape the
     * bindInputConnector() overloads discriminate on
     */

    class BindInputConnectorProbe :
        public bl::om::ObjectDefaultBase
    {
        BL_CTR_DEFAULT( BindInputConnectorProbe, protected )
        BL_DECLARE_OBJECT_IMPL( BindInputConnectorProbe )

    public:

        bool onA( SAA_in const bl::cpp::any& )
        {
            return true;
        }

        bool onB( SAA_in const bl::cpp::any& ) const
        {
            return true;
        }

        void onDoneA()
        {
        }

        void onDoneB() const
        {
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ProcessingUnitBindInputConnectorOverloadsTests )
{
    using namespace bl;
    using namespace bl::reactive;

    /*
     * ProcessingUnit exposes six bindInputConnector() overloads, and only two of them - a
     * non-const input callback paired with a const completed callback, and a bare const
     * input callback - are instantiated anywhere in src/ or in the tests
     *
     * The other four are never compiled at all, so a member pointer signature which stopped
     * matching, or an overload which quietly resolved to the wrong one, would only be found
     * by the first caller which needed it. Instantiating all four here is the coverage
     */

    typedef om::ObjectImpl< ProcessingUnit< BindInputConnectorProbe, om::Object > > unit_t;

    const auto unit = unit_t::createInstance< unit_t >();

    const auto connectorA = unit -> bindInputConnector( &BindInputConnectorProbe::onA );

    const auto connectorB = unit -> bindInputConnector(
        &BindInputConnectorProbe::onA,
        &BindInputConnectorProbe::onDoneA
        );

    const auto connectorC = unit -> bindInputConnector(
        &BindInputConnectorProbe::onB,
        &BindInputConnectorProbe::onDoneA
        );

    const auto connectorD = unit -> bindInputConnector(
        &BindInputConnectorProbe::onB,
        &BindInputConnectorProbe::onDoneB
        );

    UTF_REQUIRE( connectorA && connectorB && connectorC && connectorD );
}
