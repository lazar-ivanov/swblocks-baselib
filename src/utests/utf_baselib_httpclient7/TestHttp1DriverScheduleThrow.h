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

#ifndef __UTEST_TESTHTTP1DRIVERSCHEDULETHROW_H_
#define __UTEST_TESTHTTP1DRIVERSCHEDULETHROW_H_

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/*
 * THE SEAM THIS CASE ARMS LIVES IN THE H01 HEADER, and is included rather than relied on to have
 * been included first - the module's append convention puts this line at the end of Main.cpp, but
 * a header which needs another one should say so
 */

#include "TestHttp1DriverStrandSeam.h"

/************************************************************************
 * A4 - THE READ INITIATOR THROWS WHILE THE TASK LOCK IS HELD
 *
 * WHAT THE DEFECT IS. TaskBase::scheduleNothrow( ) takes the task lock and calls scheduleTask( )
 * under it. This driver's scheduleTask( ) arms its read DIRECTLY - "the read is armed FIRST and
 * unconditionally" - with nothing else outstanding. An initiator which throws there used to be
 * caught inside scheduleRead( ) and handed to MultiOperationTaskT::onOperationCompleted( )
 * INLINE: the count went back to zero, the single terminal path was taken, and notifyReady( )
 * was reached while scheduleNothrow( ) still held the task lock that notifyReadyImpl( )
 * re-acquires. os::mutex is std::mutex and is not recursive, so that is a SELF-DEADLOCK
 * on the scheduling thread - and, since ExecutionQueueImpl calls scheduleNothrow( ) under its own
 * lock, on the execution queue with it.
 *
 * WHAT THE FIX IS. armRead( ) lets the throw out to scheduleNothrow( )'s own catch, which
 * completes the task from the thread pool with no lock held. scheduleRead( ) - armRead( ) with
 * the accounting's guard - stays exactly as it was for the re-arm from onReadCompleted( ), where
 * the completing read is still outstanding, the count cannot reach zero and no terminal is due.
 *
 * WHY A STREAM POLICY. Asio reports I/O failure through the handler and never by throwing, so
 * what can throw out of a real initiator is the allocation the initiating call makes, which no
 * test can arrange from the outside. The driver is a template over its stream policy and its own
 * handlers are non-virtual and cpp::bind-bound, so subclassing cannot intercept them: the stream
 * is the only lever. This case borrows H01's, which already hides getStream( ) and hands the
 * driver a wrapper - one more one-shot arm on it costs nothing, where a stream of its own would
 * cost this module a second Http1ConnectionTaskImpl instantiation in one translation unit.
 *
 * WHAT ITS RED LOOKS LIKE, AND WHY IT IS NOT AN ASSERTION. Against the unfixed driver this case
 * does not fail - IT HANGS, inside eq -> push_back( ), on the test thread, holding the execution
 * queue's lock. Nothing in the process can report that: the thread which would have failed the
 * case is the deadlocked one, and every other thread that touches the queue joins it. A bounded
 * wait moved onto a helper thread would buy a printed verdict and still hang in teardown, because
 * the queue's lock is held for good. So the red is the module not finishing, and it was taken
 * with a backtrace showing notifyReadyImpl( ) blocked on the mutex scheduleNothrow( ) holds six
 * frames below it on the SAME thread - see the lane record.
 *
 * A REGRESSION HERE THEREFORE HANGS THIS MODULE RATHER THAN FAILING IT. That is a property of the
 * defect and not a choice of this case, and it is written down so a future timeout on
 * utf_baselib_httpclient7 is read as what it is.
 *
 * See notes/plans/issues/driver-read-write-arms-design.md sections 4, 4.1 and 10.2, and
 * notes/plans/issues/taskbase-schedule-lock-scope-deferral.md for the core question this does
 * NOT answer.
 */

namespace utest
{
    namespace http1schedulethrow
    {
        enum : std::size_t
        {
            /**
             * @brief How long the task is given to reach its terminal path
             *
             * The task is completed from the thread pool by scheduleNothrow( )'s catch, so this is
             * a bound on a post and not on any I/O. It is generous because what it protects
             * against is a regression OTHER than the deadlock - one which loses the completion
             * rather than blocking on it, and which would otherwise wait for ever below
             */

            TASK_END_TIMEOUT_IN_MILLISECONDS    = 30U * 1000U,
        };

        /**
         * @brief Arms the seam so the NEXT read the driver arms fails in its initiator
         *
         * Armed after the connection is established and before the driver task is pushed, which
         * is what makes it the SCHEDULING read: scheduleTask( ) arms the first one, and every
         * later one is a re-arm from a handler
         */

        inline void armReadInitiatorThrow() NOEXCEPT
        {
            utest::http1seam::seam().throwOnNextReadArm = true;
        }

        /**
         * @brief What the case reads on the test thread, once the task has ended
         */

        struct ScheduleThrowResult
        {
            bool                                                                ended;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            ScheduleThrowResult()
                :
                ended( false ),
                taskFailed( false )
            {
            }
        };

        inline auto runScheduleThrow() -> ScheduleThrowResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;
            using namespace utest::http1seam;

            ScheduleThrowResult result;

            /*
             * The peer accepts and then does nothing at all, deliberately: this driver never gets
             * as far as writing a request, so a script which read one would be waiting for bytes
             * that are never coming
             */

            ScriptedPeer peer(
                []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
                {
                    BL_UNUSED( socket );

                    self.waitForRelease();
                }
                );

            scheduleAndExecuteInParallel(
                [ &peer, &result ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishSeamDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    armReadInitiatorThrow();

                    /*
                     * AND THIS IS THE CALL THE CASE IS ABOUT. push_back( ) schedules on the
                     * calling thread, under the queue's lock, and scheduleNothrow( ) takes the
                     * task lock and calls scheduleTask( ) under both - so the driver arms its
                     * first read, the seam refuses it, and where that throw goes is the fix
                     */

                    eq -> push_back( driverTask );

                    result.ended = waitForTaskEnd(
                        driverTask,
                        static_cast< std::size_t >( TASK_END_TIMEOUT_IN_MILLISECONDS )
                        );

                    if( result.ended )
                    {
                        result.taskFailed = driverTask -> isFailed();
                        result.taskFailure = taskFailureText( driverTask );
                    }

                    peer.release();

                    eq -> wait( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            return result;
        }

    } // http1schedulethrow

} // utest

/**
 * @brief THE FIRST READ IS REFUSED BY ITS OWN INITIATOR, AND THE TASK STILL ENDS
 *
 * The three assertions are one statement each, and the first of them is the whole defect:
 *
 *   - the case REACHED them. Against the unfixed driver the thread which would evaluate them is
 *     deadlocked on the task lock inside eq -> push_back( ), so arriving here at all is the
 *     negative control - see this file's header for why it cannot be spelled as a failure
 *   - the task ended, and ended FAILED. The throw is a genuine error and not an expected one:
 *     scheduleNothrow( )'s catch completes the task with it, from the thread pool
 *   - and it ended with OUR throw. The message discriminates the injected initiator failure from
 *     any other way this task could have failed - a connect, a cancel, a peer doing something
 *
 * What it deliberately does not assert is pendingOperations( ), which the propagating route
 * leaves at one. Reading it would pin an implementation detail this design explicitly leaves
 * free, and MultiOperationTaskT::scheduleNothrow( ) zeroes the accounting before every run.
 */

UTF_AUTO_TEST_CASE( Http1Driver_ScheduleReadInitiatorThrowEndsTheTaskTests )
{
    using namespace bl;
    using namespace utest::http1schedulethrow;

    const auto result = runScheduleThrow();

    UTF_REQUIRE( result.ended );
    UTF_REQUIRE( result.taskFailed );

    UTF_REQUIRE(
        std::string::npos != result.taskFailure.find( "The read initiator was made to fail" )
        );
}

#endif /* __UTEST_TESTHTTP1DRIVERSCHEDULETHROW_H_ */
