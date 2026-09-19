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
         * Note that initiateClose() and the terminal notifyReady() are invoked ONLY from
         * onOperationCompleted(), which BL_TASKS_HANDLER_END_MULTIOP() places outside the scope of
         * the task lock. beginOperation() and beginClose() only touch the accounting, so they are
         * safe to call from a handler body, where the task lock IS held. That asymmetry is
         * deliberate: notifyReady() must never be called while holding the task lock (see the
         * invariants at the top of TaskBase.h) and this is what guarantees it
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

            mutable os::mutex                                                       m_operationsLock;
            std::size_t                                                             m_pendingOperations = 0U;
            bool                                                                    m_closing = false;
            bool                                                                    m_closeInitiated = false;
            bool                                                                    m_terminalTaken = false;
            std::exception_ptr                                                      m_firstError = nullptr;
            bool                                                                    m_firstErrorIsExpected = false;

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
                     * A failure to cancel must not cost the task its terminal path - and it is
                     * already failing when this runs, so the original error is the interesting
                     * one. Errors are logged and discarded, exactly as TaskBase does for the
                     * cancelTask() call of requestCancelInternal()
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
             * @brief Enters the closing state without an error
             *
             * This is how a task ends deliberately - it has nothing left to do, or a deadline has
             * expired. The task then completes once the operations still in flight have completed
             * or been cancelled; the first error path enters the same state by itself
             */

            void beginClose() NOEXCEPT
            {
                BL_MUTEX_GUARD( m_operationsLock );

                m_closing = true;
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
                        m_firstError = eptr;
                        m_firstErrorIsExpected = isExpectedException;

                        m_closing = true;
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
