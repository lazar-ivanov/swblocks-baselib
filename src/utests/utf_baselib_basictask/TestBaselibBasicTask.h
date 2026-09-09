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

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/ExecutionQueueImpl.h>

#include <baselib/core/OS.h>
#include <baselib/core/ThreadPoolImpl.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>

#include <atomic>
#include <thread>

/************************************************************************
 * Abstract process priority tests
 *
 * NOTE:
 * on Unix, it is possible to call nice/setpriority and lower a process'
 * priority, but one can't raise it back to what it was at process creation
 * unless its owner has superuser privileges.
 *
 * for this reason, the test below has a side effect on the process (i.e., it
 * lowers its priority) and, hence, we must make it a standalone test.
 *
 */

UTF_AUTO_TEST_CASE( Tasks_ThreadPoolResizeAndDisposeTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_ThreadPoolResizeAndDisposeTests ********************************\n" );

    /*
     * Two concurrent callers growing the pool must both return - the readiness
     * handshake must not be lost when the ready count passes the target of the
     * caller which is waiting for it
     *
     * Disposing the pool while it is being grown must not corrupt the threads
     * vector and the pool must stay usable (and idempotent to dispose) after that
     */

    for( std::size_t i = 0U; i < 10U; ++i )
    {
        const auto tp = ThreadPoolImpl::createInstance< ThreadPool >(
            os::AbstractPriority::Normal,
            2U /* threadsCount */
            );

        {
            const auto disposeLock = om::lockDisposable( tp );

            os::thread resizeThread1(
                [ &tp ]() -> void
                {
                    tp -> resize( 8U );
                }
                );

            os::thread resizeThread2(
                [ &tp ]() -> void
                {
                    tp -> resize( 12U );
                }
                );

            resizeThread1.join();
            resizeThread2.join();

            UTF_REQUIRE_EQUAL( 12U, tp -> size() );
        }

        /*
         * The pool was disposed by the lock above; disposing it again must be a nop
         */

        tp -> dispose();
    }
}

UTF_AUTO_TEST_CASE( Tasks_BasicTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_BasicTests ********************************\n" );

    const auto tp = ThreadPoolImpl::createInstance< ThreadPool >( os::AbstractPriority::Background );
    {
        const auto disposeLock = om::lockDisposable( tp );
        {
            bool called = false;

            const auto cb = [ &called ]( SAA_in const std::size_t id, SAA_in const std::size_t timeoutMilliseconds ) -> void
            {
                /*
                 * Sleep between 200 - 400 milliseconds and print message
                 */
                os::sleep( time::milliseconds( timeoutMilliseconds ) );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Task executed: "
                        << id
                        << "; task took "
                        << timeoutMilliseconds
                        << " milliseconds"
                    );

                called = true;
            };

            const auto task = SimpleTaskImpl::createInstance< Task >( cpp::bind< void >( cb, 1, 200 + ( std::rand() % 200 ) ) );

            const auto eq = om::lockDisposable(
                ExecutionQueueImpl::createInstance< ExecutionQueue >(
                    ExecutionQueue::OptionKeepAll
                    )
                );

            eq -> setLocalThreadPool( tp.get() );

            const auto lock = om::lockDisposable( eq );
            {
                UTF_CHECK_EQUAL( Task::Created, task -> getState() );
                eq -> push_back( task, true /* dontSchedule */ );
                os::sleep( time::milliseconds( 50 ) );
                UTF_CHECK_EQUAL( Task::Created, task -> getState() );

                eq -> push_back( task );
                const auto executedTask = eq -> pop( true /* wait */ );

                UTF_CHECK( executedTask );
                UTF_CHECK( om::areEqual( task, executedTask ) );
                UTF_CHECK_EQUAL( Task::Completed, task -> getState() );
                UTF_CHECK( eq -> isEmpty() );

                const auto task1 = eq -> push_back( cpp::bind< void >( cb, 2, 200 + ( std::rand() % 200 ) ) );
                const auto task2 = eq -> push_back( cpp::bind< void >( cb, 3, 200 + ( std::rand() % 200 ) ) );
                const auto task3 = eq -> push_front( cpp::bind< void >( cb, 2000, 200 + ( std::rand() % 200 ) ) );
                const auto task4 = eq -> push_front( cpp::bind< void >( cb, 3000, 200 + ( std::rand() % 200 ) ) );

                eq -> waitForSuccess( task1 );

                const auto top1 = eq -> top( true /* wait */ );
                const auto top2 = eq -> top( false /* wait */ );
                UTF_CHECK_EQUAL( top1, top2 );

                /*
                 * Test re-schedule
                 */

                eq -> push_back( top1 );

                eq -> flush( false /* discardPending */, false /* nothrowIfFailed */, true /* discardReady */ );
                UTF_CHECK( eq -> isEmpty() );
            }
        }
    }
}

UTF_AUTO_TEST_CASE( Tasks_LocalThreadPoolScopeTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_LocalThreadPoolScopeTests ********************************\n" );

    /*
     * setLocalThreadPool() is still a partial contract - the TCP / SSL and shutdown tasks
     * bind their asio objects to the process global pool regardless - but timer tasks now
     * honour it too, through the same TaskBaseT::getThreadPool( eq ) accessor
     * SimpleTaskBaseT::scheduleTask uses. This case pins the half which does honour it,
     * because AsyncExecutorImpl exists specifically to isolate its work and depends on it
     *
     * This module runs with no default thread pools at all - see the
     * UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS define in UtfBaselibBasicTaskMain.cpp -
     * which is what makes 'the local pool' and 'the default pool' trivially
     * distinguishable here and gives the case its discriminating power
     *
     * It is also why a timer task can now be hosted here at all: it used to reach for the
     * absent global default pool and dereference a null pointer
     */

    UTF_REQUIRE( nullptr == ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose ) );

    const auto mainThreadIdHash = std::hash< std::thread::id >()( std::this_thread::get_id() );

    /*
     * A single threaded pool, so its worker has exactly one thread id
     */

    const auto tpLocal = om::lockDisposable(
        ThreadPoolImpl::createInstance< ThreadPool >(
            os::AbstractPriority::Normal,
            1U /* threadsCount */
            )
        );

    UTF_REQUIRE_EQUAL( 1U, tpLocal -> size() );

    /*
     * Learn that thread id by posting a probe directly on the pool's own io_service, and
     * wait for it with a bounded wait_for rather than a sleep, so the case cannot hang
     */

    std::atomic< std::size_t > poolThreadIdHash( 0U );

    {
        os::mutex lock;
        os::condition_variable cv;

        bool probeDone = false;

        tpLocal -> aioService().post(
            [ &lock, &cv, &probeDone, &poolThreadIdHash ]() -> void
            {
                poolThreadIdHash = std::hash< std::thread::id >()( std::this_thread::get_id() );

                os::mutex_unique_lock guard( lock );

                probeDone = true;

                cv.notify_all();
            }
            );

        os::mutex_unique_lock guard( lock );

        UTF_REQUIRE(
            cv.wait_for(
                guard,
                os::chrono::seconds( 30 ),
                [ &probeDone ]() -> bool
                {
                    return probeDone;
                }
                )
            );
    }

    std::atomic< std::size_t > taskThreadIdHash( 0U );

    {
        /*
         * The queue is declared after the pool, so it is disposed first - the teardown
         * ordering T098 fences
         */

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        eq -> setLocalThreadPool( tpLocal.get() );

        UTF_REQUIRE_EQUAL( tpLocal.get(), eq -> getLocalThreadPool() );

        const auto task = SimpleTaskImpl::createInstance< Task >(
            cpp::void_callback_t(
                [ &taskThreadIdHash ]() -> void
                {
                    taskThreadIdHash = std::hash< std::thread::id >()( std::this_thread::get_id() );
                }
                )
            );

        eq -> push_back( task );
        eq -> waitForSuccess( task );

        UTF_REQUIRE( task -> isFailed() == false );
        UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

        /*
         * Both slots really were written, so the comparison below cannot pass vacuously
         */

        UTF_REQUIRE( 0U != poolThreadIdHash.load() );
        UTF_REQUIRE( 0U != taskThreadIdHash.load() );

        /*
         * The work ran on the local pool and not inline on the pushing thread
         */

        UTF_REQUIRE_EQUAL( poolThreadIdHash.load(), taskThreadIdHash.load() );
        UTF_REQUIRE( taskThreadIdHash.load() != mainThreadIdHash );

        /*
         * A timer task binds its deadline timer through the very same accessor, so it runs
         * on the local pool too - and this module has no global default pools at all, so
         * before that was true a timer task here dereferenced a null pointer rather than
         * merely running in the wrong place
         *
         * Every ObservableBase is a timer task, which is what makes this the assertion that
         * decides where a reactive pipeline's timers run under an AsyncExecutorImpl
         */

        std::atomic< std::size_t > timerThreadIdHash( 0U );

        const auto timerTask = SimpleTimerTask::createInstance< Task >(
            [ &timerThreadIdHash ]() -> bool
            {
                timerThreadIdHash = std::hash< std::thread::id >()( std::this_thread::get_id() );

                /*
                 * Returning false terminates the timer task
                 */

                return false;
            },
            time::seconds( 30 )                         /* duration */,
            time::time_duration()                       /* initDelay */
            );

        eq -> push_back( timerTask );
        eq -> waitForSuccess( timerTask );

        UTF_REQUIRE( 0U != timerThreadIdHash.load() );

        UTF_REQUIRE_EQUAL( poolThreadIdHash.load(), timerThreadIdHash.load() );
        UTF_REQUIRE( timerThreadIdHash.load() != mainThreadIdHash );

        /*
         * The accessor is a plain non-owning slot which can also be cleared
         */

        eq -> setLocalThreadPool( nullptr );

        UTF_REQUIRE( nullptr == eq -> getLocalThreadPool() );
    }

    /*
     * The single thread identity argument above is only sound if the pool never grew
     */

    UTF_REQUIRE_EQUAL( 1U, tpLocal -> size() );
}

UTF_AUTO_TEST_CASE( Tasks_SchedulingWithoutUsableThreadPoolTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_SchedulingWithoutUsableThreadPoolTests ********************************\n" );

    /*
     * The documented teardown order - queues before the pools they point at
     *
     * ExecutionQueue::setLocalThreadPool() stores a *non-owning* ThreadPool*, so nothing
     * stops one component disposing a pool another component's queue is still using. The
     * two arms which cover that - a queue with no usable pool at all, and a queue whose
     * pool has already been disposed - are deliberately NOT written here: today the first
     * dereferences a null om::ObjPtr< ThreadPool > inside a NOEXCEPT function and the
     * second escapes BL_NOEXCEPT_END into os::fastAbort(), and neither ending the process
     * can be asserted in-process. They need a production guard on
     * TaskBaseT::getThreadPool() plus a non-throwing fallback in scheduleNothrow's catch
     * handler - whose entire recovery strategy is currently to use the very resource whose
     * failure it is recovering from - before they can be added
     *
     * What this case fences is the ordering which avoids all of that
     */

    UTF_REQUIRE( nullptr == ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose ) );

    const auto tp = ThreadPoolImpl::createInstance< ThreadPool >(
        os::AbstractPriority::Normal,
        1U /* threadsCount */
        );

    auto eq = om::lockDisposable(
        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepFailed )
        );

    eq -> setLocalThreadPool( tp.get() );

    /*
     * The pool is known good at this point
     */

    bool called = false;

    const auto task = SimpleTaskImpl::createInstance< Task >(
        cpp::void_callback_t(
            [ &called ]() -> void
            {
                called = true;
            }
            )
        );

    eq -> push_back( task );
    eq -> waitForSuccess( task );

    UTF_REQUIRE( called );
    UTF_REQUIRE( task -> isFailed() == false );
    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

    /*
     * Asserted here rather than after disposeQueue(), which releases the reference
     */

    UTF_REQUIRE( eq -> isEmpty() );

    ExecutionQueue::disposeQueue( eq );

    UTF_REQUIRE( ! eq );

    /*
     * Only now may the pool go away - and nothing aborts, which is the whole point
     */

    tp -> dispose();
}

UTF_AUTO_TEST_CASE( Tasks_ScheduleAndExecuteInParallelNoThreadPoolTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_ScheduleAndExecuteInParallelNoThreadPoolTests ********************************\n" );

    /*
     * scheduleAndExecuteInParallel's two guards are unreachable in every module except this
     * one, which is the only place where ThreadPoolDefault::getDefault( ... ) is null for
     * both pool ids - UtfBaselibBasicTaskMain.cpp defines
     * UTF_TEST_APP_INIT_DEACTIVATE_THREAD_POOLS
     *
     * Without them the failure mode of a mis-initialised application is a null dereference
     * inside TaskBase::getThreadPool() rather than an actionable message
     */

    UTF_REQUIRE( ! ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose ) );

    bool schedulerInvoked = false;

    UTF_REQUIRE_THROW_MESSAGE(
        scheduleAndExecuteInParallel(
            [ &schedulerInvoked ]( SAA_in const om::ObjPtr< ExecutionQueue >& ) -> void
            {
                schedulerInvoked = true;
            }
            ),
        bl::UnexpectedException,
        "No global default thread pool has been configured yet"
        );

    /*
     * The guard fires before any execution queue is created and before any user code runs
     */

    UTF_REQUIRE_EQUAL( schedulerInvoked, false );
}
