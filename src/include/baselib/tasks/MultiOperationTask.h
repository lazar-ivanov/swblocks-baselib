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

#ifndef __BL_TASKS_MULTIOPERATIONTASK_H_
#define __BL_TASKS_MULTIOPERATIONTASK_H_

#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/core/Utils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>

namespace bl
{
    namespace tasks
    {
        /**
         * @brief class MultiOperationTaskT - the accounting a task needs when it can have more
         * than one asynchronous operation in flight at a time
         *
         * The handler macros of TaskBase.h assume one operation in flight: BL_TASKS_HANDLER_END()
         * completes the task from whichever handler fails first. A task with a read, a write and
         * timers outstanding together therefore re-enters its own completion path from the second
         * failing handler, because notifyReadyImpl() runs scheduleTaskFinishContinuation() and
         * onTaskStoppedNothrow() before it consults m_notifyCalled. For such a task that is the
         * normal failure mode rather than an edge case
         *
         * This class holds the accounting which fixes it, and BL_TASKS_HANDLER_END_MULTIOP() is
         * the handler epilog which feeds it. The rule it enforces is that such a task has ONE
         * terminal path and takes it only after every outstanding operation has completed or been
         * cancelled - which is also what keeps an asynchronous shutdown continuation, such as the
         * TLS shutdown of TcpSslSocketAsyncBase, from starting while a read or a write is still
         * pending
         *
         * A task using it:
         *
         *   - calls beginOperation() immediately before each async_* call
         *   - ends the handler of each such operation with BL_TASKS_HANDLER_END_MULTIOP()
         *   - overrides initiateClose() to cancel its sockets and its timers
         *   - calls beginClose() when it decides it is done for a reason other than an error
         *
         * It follows that the task must keep at least one operation in flight for as long as it
         * is running. A task which lets the count fall to zero while it is not closing has simply
         * stopped doing anything, and nothing is left to complete it
         *
         * Note that initiateClose() is invoked ONLY from onOperationCompleted(), and the terminal
         * notifyReady() from it and from abandonOperation() - both outside the scope of the task
         * lock, the first because BL_TASKS_HANDLER_END_MULTIOP() places it there and the second
         * because a terminal cannot be DUE while that lock is held. beginOperation() and
         * beginClose() only touch the accounting, so they are safe from a handler body, where the
         * lock IS held: notifyReady() must never be called under it (see TaskBase.h's invariants)
         *
         * The class is parameterized on its base rather than deriving from TaskBase directly, so
         * that it can be mixed into a task which already has TaskBase in its chain - a connection
         * task deriving from TcpConnectionEstablisherConnector< STREAM >, for one. Hard-wiring
         * TaskBase here would give such a task two TaskBase subobjects, which is to say the mix-in
         * could not be mixed into it at all. MultiOperationTask, the mix-in over a plain TaskBase,
         * is what a task with no other base uses
         */

        template
        <
            typename BASE = TaskBase
        >
        class MultiOperationTaskT : public BASE
        {
            BL_DECLARE_OBJECT_IMPL( MultiOperationTaskT )

        public:

            typedef MultiOperationTaskT< BASE >                                     this_type;
            typedef BASE                                                            base_type;

        private:

            /*
             * The accounting has a lock of its own rather than sharing the task lock, and it is a
             * leaf lock - nothing is called while it is held
             *
             * The task lock cannot be used for this. beginOperation() is called from a handler
             * body, which is inside the scope of the task lock, while onOperationCompleted() is
             * called from the handler epilog, which is outside it. One member guarded by the task
             * lock would therefore deadlock on the first re-entry, since it is not recursive
             */

            /*
             * m_closingDeliberate is deliberately NOT the same flag as m_closing. Both doors set
             * m_closing - a first error, and beginClose() - so m_closing alone cannot tell a task
             * which is closing because it failed from one which is closing because it is done.
             * Only the second may excuse the operation_aborted its own initiateClose() produces
             *
             * m_closingDeliberate implies m_closing: beginClose() sets both, nothing else sets
             * m_closingDeliberate, and scheduleNothrow() clears both
             */

            mutable os::mutex                                                       m_operationsLock;
            std::size_t                                                             m_pendingOperations = 0U;
            bool                                                                    m_closing = false;
            bool                                                                    m_closingDeliberate = false;
            bool                                                                    m_closeInitiated = false;
            bool                                                                    m_terminalTaken = false;
            std::exception_ptr                                                      m_firstError = nullptr;
            bool                                                                    m_firstErrorIsExpected = false;

            /**
             * @brief Whether an operation completed with the asio::error::operation_aborted of a
             * cancelled operation
             *
             * The error arrives as an std::exception_ptr and not as a code, so the code has to be
             * recovered from it. eh::errorCodeFromExceptionPtr() is the library's own way to do
             * that and this follows it rather than inventing a second mechanism, the same way
             * TcpSslBaseTasks classifies through isExpectedSslErrorCode(): it reads
             * eh::system_error::code(), which is what BL_TASKS_HANDLER_CHK_EC() throws, and
             * otherwise the errinfo_error_code of a BaseException. An exception carrying neither
             * yields an empty code, which never compares equal to operation_aborted - so an
             * exception with no code at all is a genuine error, which is the answer wanted here
             *
             * It recovers the code by rethrowing, which is NOT free. Every caller must therefore
             * keep it behind the cheap tests - see onOperationCompleted()
             */

            static bool isOperationAborted( SAA_in const std::exception_ptr& eptr ) NOEXCEPT
            {
                return asio::error::operation_aborted == eh::errorCodeFromExceptionPtr( eptr );
            }

            /**
             * @brief Claims the single terminal path, if it is due; the accounting lock is held
             */

            bool takeTerminalNoLock() NOEXCEPT
            {
                if( m_terminalTaken || ! m_closing || 0U != m_pendingOperations )
                {
                    return false;
                }

                m_terminalTaken = true;

                return true;
            }

            /**
             * @brief Performs what was decided under the accounting lock, with the lock released
             */

            void applyDecision(
                SAA_in                  const bool                                  close,
                SAA_in                  const bool                                  terminal,
                SAA_in_opt              const std::exception_ptr&                   firstError,
                SAA_in                  const bool                                  firstErrorIsExpected
                ) NOEXCEPT
            {
                if( close )
                {
                    /*
                     * A failure to cancel must not cost the task its terminal path. When the task
                     * is closing because it failed, the original error is the interesting one;
                     * when it is closing deliberately, it has already decided it is done and a
                     * cancel which did not take is not a reason to fail it. Errors are logged and
                     * discarded either way, exactly as TaskBase does for the cancelTask() call of
                     * requestCancelInternal()
                     */

                    utils::tryCatchLog(
                        "MultiOperationTaskT::initiateClose threw an exception",
                        [ & ]() -> void
                        {
                            initiateClose();
                        },
                        cpp::void_callback_t(),
                        utils::LogFlags::DEBUG_ONLY
                        );
                }

                if( terminal )
                {
                    base_type::notifyReady( firstError, firstErrorIsExpected );
                }
            }

            /*
             * The constructor forwards whatever it is given to the base, and BL_VARIADIC_CTOR
             * owns its definition - so the accounting members above are initialized in class
             * rather than in an init list there is no way to write
             *
             * BL_VARIADIC_CTOR emits the access label it is given and leaves the class in a
             * private section, hence the label after it
             */

            BL_VARIADIC_CTOR( MultiOperationTaskT, base_type, protected )

        protected:

            /**
             * @brief Accounts for one asynchronous operation which is about to be started
             *
             * It must be called before the async_* call and exactly once for each operation whose
             * handler ends with BL_TASKS_HANDLER_END_MULTIOP()
             */

            void beginOperation() NOEXCEPT
            {
                BL_MUTEX_GUARD( m_operationsLock );

                ++m_pendingOperations;
            }

            /**
             * @brief Gives back an operation which was begun and whose async_* call then THREW
             *
             * The initiating call can throw - an allocation, since asio reports I/O failure through
             * the handler - and an operation which was begun and never started has to be given back
             * or the pending count never reaches zero again. A task whose count cannot reach zero
             * can never take its terminal path, which is a HANG rather than a failure
             *
             * IT RECORDS NO ERROR AND INITIATES NO CLOSE, and that is the difference from
             * onOperationCompleted(). It may therefore be called while the TASK LOCK is held,
             * which the catch of an initiator called from a handler body or from a task's
             * establishment chain is - onOperationCompleted() in that position would reach
             * notifyReady() whenever the count fell to zero, and notifyReady() re-acquires the
             * task lock, which is not recursive. NOT every caller holds it: an initiator called
             * from a task's own public API runs on the caller's thread and under no lock at all
             *
             * IT DOES TAKE A TERMINAL WHICH IS ALREADY DUE - closing, count zero, not yet taken -
             * and nothing else, because giving back the LAST operation of a task which is already
             * closing leaves nobody to complete it. The difference matters only where nothing
             * decides after the rethrow: from a handler body or the establishment chain there is
             * always an epilog behind the throw, and the terminal cannot be due in either place
             * anyway - a handler's own operation is outstanding until its epilog, and a task being
             * scheduled is not closing. So this clause can only fire off that path, and it cannot
             * fire under the task lock
             *
             * A due terminal also means the close was initiated - the FLAG is set, though the call
             * may still be running on another thread - and that nothing is left in flight; the
             * safety is structural. What it moves is the THREAD the terminal runs on, so a task
             * whose initiateClose() or onTaskStoppedNothrow() touches an object it shares with
             * them must say so, as the HTTP/2 driver's mailbox post does of its timers and socket
             *
             * Apart from that terminal it leaves the accounting exactly as it would have been had
             * beginOperation() never been called, so the exception must go on to leave the
             * function: the task's ordinary error path - the handler epilog, or the caller - is
             * what reports it
             */

            void abandonOperation() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bool terminal = false;
                std::exception_ptr firstError;
                bool firstErrorIsExpected = false;

                {
                    BL_MUTEX_GUARD( m_operationsLock );

                    BL_ASSERT( 0U != m_pendingOperations );

                    if( 0U != m_pendingOperations )
                    {
                        --m_pendingOperations;
                    }

                    terminal = takeTerminalNoLock();

                    firstError = m_firstError;
                    firstErrorIsExpected = m_firstErrorIsExpected;
                }

                /*
                 * close is false on purpose - a terminal can only be due once the close has been
                 * initiated, so there is never one owed here
                 */

                applyDecision( false /* close */, terminal, firstError, firstErrorIsExpected );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Enters the closing state without an error
             *
             * This is how a task ends deliberately - it has nothing left to do, or a deadline has
             * expired. The task then completes once the operations still in flight have completed
             * or been cancelled; the first error path enters the same state by itself
             *
             * A task which closes this way completes SUCCESSFULLY even when initiateClose() had
             * operations to cancel: the operation_aborted each of those reports is self inflicted
             * and is not recorded as the task's error. A genuine failure while closing still is,
             * and so is the operation_aborted of an external cancelTask() - see
             * onOperationCompleted()
             */

            void beginClose() NOEXCEPT
            {
                BL_MUTEX_GUARD( m_operationsLock );

                m_closing = true;
                m_closingDeliberate = true;
            }

        public:

            /**
             * @brief Whether the task has entered the closing state - because an operation failed
             * or because beginClose() was called
             *
             * It is public rather than protected because it is the one piece of the accounting a
             * task has to expose: the loops which re-arm operations consult it to decide whether
             * to start another one
             */

            bool isClosing() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_operationsLock );

                return m_closing;
            }

        protected:

            /**
             * @brief The number of operations which have been begun and have not completed yet
             */

            std::size_t pendingOperations() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_operationsLock );

                return m_pendingOperations;
            }

            /**
             * @brief Cancels the operations which are still in flight - the sockets and the timers
             *
             * It is called exactly once per run, on the first error or on the first completed
             * operation after beginClose(), and never while the task lock is held. It must not
             * acquire the task lock, and it must not begin new operations
             *
             * An exception thrown out of it is logged and discarded, because the task is already
             * on its way to completing with an error which matters more
             */

            virtual void initiateClose()
            {
                /*
                 * NOP by default
                 */
            }

            /**
             * @brief Accounts for one completed operation - the target of
             * BL_TASKS_HANDLER_END_MULTIOP()
             *
             * The FIRST error wins: it is the one the task completes with, and it is the one
             * whose expected / unexpected classification is reported. Errors arriving after it,
             * including the operation_aborted of everything initiateClose() cancelled, are
             * accounted for but not reported
             *
             * That accounts for a task which is closing BECAUSE something failed. A task which
             * called beginClose() because it was done has no first error, so without more the
             * operation_aborted of the first operation its own initiateClose() cancelled would
             * become one - and the task would complete isFailed() on its own clean shutdown.
             * Such an abort is self inflicted and says nothing about the run, so it is not
             * recorded. Only operation_aborted is excused this way: a genuine I/O failure which
             * arrives while the task is closing is still the task's error
             *
             * An external cancelTask() is deliberately NOT excused. A cancelled task completes
             * isFailed() with operation_aborted exactly as it did before this, including when the
             * cancel lands on a task which had already begun closing deliberately: that abort is
             * not self inflicted, the caller asked for it, and the caller cannot know the task
             * had decided to close, so it must not be told the task finished normally. It is the
             * benign failure it always was - TasksUtils::waitForSuccess() and ExecutionQueue
             * already recognize operation_aborted from a cancel and filter it
             */

            void onOperationCompleted(
                SAA_in_opt              const std::exception_ptr&                   eptr,
                SAA_in_opt              const bool                                  isExpectedException
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bool close = false;
                bool terminal = false;
                std::exception_ptr firstError;
                bool firstErrorIsExpected = false;

                {
                    BL_MUTEX_GUARD( m_operationsLock );

                    BL_ASSERT( 0U != m_pendingOperations );

                    if( 0U != m_pendingOperations )
                    {
                        --m_pendingOperations;
                    }

                    if( eptr && ! m_firstError )
                    {
                        /*
                         * The tests are ordered so that isOperationAborted(), which recovers the
                         * code by rethrowing, is reached only where it can change the answer:
                         * never on the success path, never once an error has been recorded, and
                         * never on a task which is not closing deliberately - which is to say
                         * never on the path of a task which is simply failing
                         *
                         * isCanceled() reads an atomic and takes no lock, so consulting it does
                         * not break the leaf lock rule this accounting is written to
                         */

                        const bool isSelfInflictedAbort =
                            m_closingDeliberate &&
                            ! base_type::isCanceled() &&
                            isOperationAborted( eptr );

                        if( ! isSelfInflictedAbort )
                        {
                            m_firstError = eptr;
                            m_firstErrorIsExpected = isExpectedException;

                            m_closing = true;
                        }
                    }

                    if( m_closing && ! m_closeInitiated )
                    {
                        m_closeInitiated = true;

                        close = true;
                    }

                    terminal = takeTerminalNoLock();

                    firstError = m_firstError;
                    firstErrorIsExpected = m_firstErrorIsExpected;
                }

                applyDecision( close, terminal, firstError, firstErrorIsExpected );

                BL_NOEXCEPT_END()
            }

            virtual void scheduleNothrow(
                SAA_in                  const std::shared_ptr< ExecutionQueue >&    eq,
                SAA_in                  cpp::void_callback_noexcept_t&&             callbackReady
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                {
                    /*
                     * The accounting is per run, and the base class resets its own per run state
                     * (the exception, m_notifyCalled, the cancel request) here too
                     *
                     * The lock is taken and released before the base call rather than held across
                     * it, because the base class acquires the task lock and goes on to call
                     * scheduleTask(), which is where the task begins its first operations
                     */

                    BL_MUTEX_GUARD( m_operationsLock );

                    m_pendingOperations = 0U;
                    m_closing = false;
                    m_closingDeliberate = false;
                    m_closeInitiated = false;
                    m_terminalTaken = false;
                    m_firstError = nullptr;
                    m_firstErrorIsExpected = false;
                }

                base_type::scheduleNothrow( eq, BL_PARAM_FWD( callbackReady ) );

                BL_NOEXCEPT_END()
            }
        };

        typedef MultiOperationTaskT<> MultiOperationTask;

    } // tasks

} // bl

#endif /* __BL_TASKS_MULTIOPERATIONTASK_H_ */
