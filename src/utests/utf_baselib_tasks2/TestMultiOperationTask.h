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

#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The handler macros of TaskBase.h, and the multi-operation task mix-in
 *
 * The first half of this header characterizes BL_TASKS_HANDLER_END_IMPL as it stands, because
 * the generalization of it is a change to a macro which every task in the library expands. A
 * test written after such a refactor can only ever agree with the refactor, so these cases are
 * committed, and shown passing, before it
 */

namespace
{
    /**
     * @brief What BL_TASKS_HANDLER_END_IMPL did with one handler body
     *
     * Which catch clause of the macro handled a given exception is not directly observable, but
     * the clauses differ in two ways which are: only some of them enhance the exception, and
     * only some of them consult the isExpectedException() hook - and of those, only some pass it
     * an error code. Together with the expectedness the task ends up with, that identifies the
     * clause uniquely
     */

    struct HandlerMacroOutcome
    {
        bool                                                                failed;
        bool                                                                stoppedCalled;
        bool                                                                stoppedWithException;
        bool                                                                stoppedIsExpected;
        std::size_t                                                         enhanceCalls;
        std::size_t                                                         hookCalls;
        bool                                                                hookSawErrorCode;

        HandlerMacroOutcome()
            :
            failed( false ),
            stoppedCalled( false ),
            stoppedWithException( false ),
            stoppedIsExpected( false ),
            enhanceCalls( 0U ),
            hookCalls( 0U ),
            hookSawErrorCode( false )
        {
        }
    };

    /**
     * @brief A task whose only purpose is to run one handler body between
     * BL_TASKS_HANDLER_BEGIN() and BL_TASKS_HANDLER_END() and record what the macro did with it
     *
     * Note that the task name is deliberately left empty. notifyReadyImpl() only enters its
     * logging block for a named task, and that block calls both isExpectedException() (through
     * chk2DumpException) and enhanceException() (for the printable wrapper it builds) - which
     * would add counts that have nothing to do with the macro
     */

    template
    <
        typename E = void
    >
    class HandlerMacroProbeT : public bl::tasks::SimpleTaskBase
    {
        BL_DECLARE_OBJECT_IMPL( HandlerMacroProbeT )

    public:

        typedef bl::tasks::SimpleTaskBase                                   base_type;

    protected:

        const bl::cpp::void_callback_t                                      m_body;
        const bool                                                          m_hookAnswer;

        /*
         * The handler runs on a thread pool thread while the test thread reads these after the
         * queue has been flushed, so they are atomic for the same reason the probes in
         * TestTasks7.h are - a plain member here is a data race the ThreadSanitizer run reports
         */

        mutable std::atomic< std::size_t >                                  m_enhanceCalls;
        std::atomic< std::size_t >                                          m_hookCalls;
        std::atomic< bool >                                                 m_hookSawErrorCode;
        std::atomic< bool >                                                 m_stoppedCalled;
        std::atomic< bool >                                                 m_stoppedWithException;
        std::atomic< bool >                                                 m_stoppedIsExpected;

        HandlerMacroProbeT(
            SAA_in                  bl::cpp::void_callback_t&&                  body,
            SAA_in_opt              const bool                                  hookAnswer = false
            )
            :
            m_body( BL_PARAM_FWD( body ) ),
            m_hookAnswer( hookAnswer ),
            m_enhanceCalls( 0U ),
            m_hookCalls( 0U ),
            m_hookSawErrorCode( false ),
            m_stoppedCalled( false ),
            m_stoppedWithException( false ),
            m_stoppedIsExpected( false )
        {
        }

        virtual void enhanceException( SAA_in bl::eh::exception& exception ) const OVERRIDE
        {
            ++m_enhanceCalls;

            base_type::enhanceException( exception );
        }

        virtual bool isExpectedException(
            SAA_in                  const std::exception_ptr&                   eptr,
            SAA_in                  const std::exception&                       exception,
            SAA_in_opt              const bl::eh::error_code*                   ec
            ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( eptr );
            BL_UNUSED( exception );

            ++m_hookCalls;

            if( ec )
            {
                m_hookSawErrorCode = true;
            }

            return m_hookAnswer;
        }

        virtual auto onTaskStoppedNothrow(
            SAA_in_opt              const std::exception_ptr&                   eptrIn = nullptr,
            SAA_inout_opt           bool*                                       isExpectedException = nullptr
            ) NOEXCEPT
            -> std::exception_ptr OVERRIDE
        {
            m_stoppedCalled = true;
            m_stoppedWithException = ( nullptr != eptrIn );
            m_stoppedIsExpected = ( isExpectedException && *isExpectedException );

            return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
        }

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            m_body();

            BL_TASKS_HANDLER_END()
        }

    public:

        HandlerMacroOutcome outcome() const NOEXCEPT
        {
            HandlerMacroOutcome result;

            result.failed = base_type::isFailedOrFailing();
            result.stoppedCalled = m_stoppedCalled;
            result.stoppedWithException = m_stoppedWithException;
            result.stoppedIsExpected = m_stoppedIsExpected;
            result.enhanceCalls = m_enhanceCalls;
            result.hookCalls = m_hookCalls;
            result.hookSawErrorCode = m_hookSawErrorCode;

            return result;
        }
    };

    typedef bl::om::ObjectImpl< HandlerMacroProbeT<> > HandlerMacroProbeImpl;

    /**
     * @brief Runs one handler body through the macro and returns what it did
     */

    HandlerMacroOutcome runHandlerMacroProbe(
        SAA_in                  bl::cpp::void_callback_t&&                  body,
        SAA_in_opt              const bool                                  hookAnswer = false
        )
    {
        using namespace bl;
        using namespace bl::tasks;

        const auto taskImpl = HandlerMacroProbeImpl::createInstance( BL_PARAM_FWD( body ), hookAnswer );

        {
            const auto eq = om::lockDisposable(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
                );

            eq -> push_back( om::qi< Task >( taskImpl ) );

            eq -> flushNoThrowIfFailed();
        }

        return taskImpl -> outcome();
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_HandlerMacroCatchClauseTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * BL_TASKS_HANDLER_END_IMPL has five catch clauses and their order, together with what each
     * one does, is load-bearing: it decides whether an exception is reported as expected, which
     * in turn decides whether the task layer logs it as a failure. Nothing enumerates that
     * today - one test depends on a single clause of it in passing
     * (utf_baselib_http/TestClientHttpTasks.h:1189) and that is all
     *
     * Each case below picks an exception which only one clause can catch, and asserts the
     * fingerprint of that clause
     */

    {
        /*
         * No exception - the success expression runs, nothing is enhanced, the hook is not
         * consulted, and the task stops without an exception
         */

        const auto outcome = runHandlerMacroProbe( []() -> void {} );

        UTF_REQUIRE( ! outcome.failed );
        UTF_REQUIRE( outcome.stoppedCalled );
        UTF_REQUIRE( ! outcome.stoppedWithException );
        UTF_REQUIRE( ! outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 0U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 0U );
    }

    {
        /*
         * catch( bl::SystemException& ) - the first clause. It enhances, and it short circuits
         * on operation_aborted, so the hook is never reached
         *
         * That this is caught here and not by the later catch( bl::eh::system_error& ) - whose
         * base it derives from - is exactly what the enhance count proves
         */

        const auto outcome = runHandlerMacroProbe(
            []() -> void
            {
                BL_THROW_EC( asio::error::operation_aborted, BL_SYSTEM_ERROR_DEFAULT_MSG );
            }
            );

        UTF_REQUIRE( outcome.failed );
        UTF_REQUIRE( outcome.stoppedWithException );
        UTF_REQUIRE( outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 1U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 0U );
    }

    {
        /*
         * The same clause with an error code which is not operation_aborted - now the hook is
         * consulted, and it is given the error code
         */

        const auto throwOther = []() -> void
        {
            BL_THROW_EC( asio::error::connection_refused, BL_SYSTEM_ERROR_DEFAULT_MSG );
        };

        const auto rejected = runHandlerMacroProbe( cpp::void_callback_t( throwOther ), false /* hookAnswer */ );

        UTF_REQUIRE( rejected.failed );
        UTF_REQUIRE( rejected.stoppedWithException );
        UTF_REQUIRE( ! rejected.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( rejected.enhanceCalls, 1U );
        UTF_REQUIRE_EQUAL( rejected.hookCalls, 1U );
        UTF_REQUIRE( rejected.hookSawErrorCode );

        const auto accepted = runHandlerMacroProbe( cpp::void_callback_t( throwOther ), true /* hookAnswer */ );

        UTF_REQUIRE( accepted.failed );
        UTF_REQUIRE( accepted.stoppedWithException );
        UTF_REQUIRE( accepted.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( accepted.hookCalls, 1U );
    }

    {
        /*
         * catch( bl::BaseExceptionDefault& ) - the second clause. It enhances, and it consults
         * the hook with a NULL error code when the exception carries none
         *
         * The hook being consulted at all is what tells this clause apart from
         * catch( bl::eh::exception& ) below, which would also match this exception
         */

        const auto outcome = runHandlerMacroProbe(
            []() -> void
            {
                BL_THROW( UnexpectedException(), BL_MSG() << "characterization" );
            }
            );

        UTF_REQUIRE( outcome.failed );
        UTF_REQUIRE( outcome.stoppedWithException );
        UTF_REQUIRE( ! outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 1U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 1U );
        UTF_REQUIRE( ! outcome.hookSawErrorCode );
    }

    {
        /*
         * The same clause, but the exception carries errinfo_error_code( operation_aborted ) -
         * which short circuits the hook exactly as the first clause does
         */

        const auto outcome = runHandlerMacroProbe(
            []() -> void
            {
                auto exception = UnexpectedException();

                exception << eh::errinfo_error_code(
                    static_cast< eh::error_code >( asio::error::operation_aborted )
                    );

                BL_THROW( exception, BL_MSG() << "characterization" );
            }
            );

        UTF_REQUIRE( outcome.failed );
        UTF_REQUIRE( outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 1U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 0U );
    }

    {
        /*
         * catch( bl::eh::system_error& ) - the third clause, reached by a system_error which is
         * not a bl::SystemException. It is the one clause which does NOT enhance, and it does
         * pass the error code to the hook
         */

        const auto outcome = runHandlerMacroProbe(
            []() -> void
            {
                throw eh::system_error(
                    static_cast< eh::error_code >( asio::error::connection_refused ),
                    "characterization"
                    );
            }
            );

        UTF_REQUIRE( outcome.failed );
        UTF_REQUIRE( outcome.stoppedWithException );
        UTF_REQUIRE( ! outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 0U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 1U );
        UTF_REQUIRE( outcome.hookSawErrorCode );
    }

    {
        /*
         * catch( bl::eh::exception& ) - the fourth clause, reached by a boost exception which is
         * not one of ours. This is the clause TestClientHttpTasks.h:1189 depends on, where a
         * boost::throw_exception of a std exception arrives wrapped
         *
         * It enhances, and it is the only clause which never consults the hook - so the hook is
         * told to say 'expected' here and the task must still come out unexpected
         */

        const auto outcome = runHandlerMacroProbe(
            []() -> void
            {
                throw eh::enable_error_info( std::runtime_error( "characterization" ) );
            },
            true /* hookAnswer */
            );

        UTF_REQUIRE( outcome.failed );
        UTF_REQUIRE( outcome.stoppedWithException );
        UTF_REQUIRE( ! outcome.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( outcome.enhanceCalls, 1U );
        UTF_REQUIRE_EQUAL( outcome.hookCalls, 0U );
    }

    {
        /*
         * catch( std::exception& ) - the last clause. It does not enhance, and it consults the
         * hook with a NULL error code, which is what decides the outcome entirely
         */

        const auto throwPlain = []() -> void
        {
            throw std::runtime_error( "characterization" );
        };

        const auto rejected = runHandlerMacroProbe( cpp::void_callback_t( throwPlain ), false /* hookAnswer */ );

        UTF_REQUIRE( rejected.failed );
        UTF_REQUIRE( rejected.stoppedWithException );
        UTF_REQUIRE( ! rejected.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( rejected.enhanceCalls, 0U );
        UTF_REQUIRE_EQUAL( rejected.hookCalls, 1U );
        UTF_REQUIRE( ! rejected.hookSawErrorCode );

        const auto accepted = runHandlerMacroProbe( cpp::void_callback_t( throwPlain ), true /* hookAnswer */ );

        UTF_REQUIRE( accepted.failed );
        UTF_REQUIRE( accepted.stoppedIsExpected );
        UTF_REQUIRE_EQUAL( accepted.enhanceCalls, 0U );
        UTF_REQUIRE_EQUAL( accepted.hookCalls, 1U );
    }
}

/************************************************************************
 * tasks::MultiOperationTaskT
 */

namespace
{
    /**
     * @brief How a probe task behaves on one run
     *
     * Timer i expires after firstDelayMs + i * failStepMs if it is one of the failing ones, and
     * after restDelayMs otherwise, which is what lets a case decide whether a failure lands while
     * the others are still pending or after they have already fired
     */

    struct MultiOperationProbeOptions
    {
        std::size_t                                                         operations;
        std::size_t                                                         failures;
        std::size_t                                                         firstDelayMs;
        std::size_t                                                         failStepMs;
        std::size_t                                                         restDelayMs;

        /*
         * closeWhenAllSucceed asks for the deliberate close once every operation has succeeded,
         * so nothing is left in flight for initiateClose() to cancel. closeAfterSuccesses, when
         * non zero, asks for it after that many successes instead - which leaves the rest
         * outstanding, and that is the quadrant the two differ on
         */

        std::size_t                                                         closeAfterSuccesses;
        bool                                                                closeWhenAllSucceed;
        bool                                                                initiateCloseCancels;
        bool                                                                initiateCloseThrows;

        MultiOperationProbeOptions(
            SAA_in                  const std::size_t                           operationsIn,
            SAA_in                  const std::size_t                           failuresIn
            )
            :
            operations( operationsIn ),
            failures( failuresIn ),
            firstDelayMs( 50U ),
            failStepMs( 0U ),
            restDelayMs( 50U ),
            closeAfterSuccesses( 0U ),
            closeWhenAllSucceed( false ),
            initiateCloseCancels( true ),
            initiateCloseThrows( false )
        {
        }
    };

    /**
     * @brief What the mix-in did over one run
     */

    struct MultiOperationOutcome
    {
        std::size_t                                                         stopCalls;
        std::size_t                                                         initiateCloseCalls;
        std::size_t                                                         finishContinuationCalls;
        std::size_t                                                         pendingAtFinishContinuation;
        std::size_t                                                         pendingAtStop;
        std::size_t                                                         bodiesAtStop;
        std::size_t                                                         bodiesEntered;
        std::size_t                                                         bodiesSeeingClosing;
        bool                                                                stoppedWithException;
        bool                                                                stoppedIsExpected;
        bool                                                                closingAtScheduleTask;
        bool                                                                closingAfterBeginClose;
        bool                                                                closingAtInitiateClose;

        MultiOperationOutcome()
            :
            stopCalls( 0U ),
            initiateCloseCalls( 0U ),
            finishContinuationCalls( 0U ),
            pendingAtFinishContinuation( 0U ),
            pendingAtStop( 0U ),
            bodiesAtStop( 0U ),
            bodiesEntered( 0U ),
            bodiesSeeingClosing( 0U ),
            stoppedWithException( false ),
            stoppedIsExpected( false ),
            closingAtScheduleTask( false ),
            closingAfterBeginClose( false ),
            closingAtInitiateClose( false )
        {
        }
    };

    /**
     * @brief A task with several timers in flight at once, which is the smallest thing that
     * exercises the accounting of tasks::MultiOperationTaskT
     *
     * Timers rather than sockets because they are the library's own asynchronous operation with
     * no external dependency, they can be made to complete in a chosen order, and cancelling one
     * delivers operation_aborted through exactly the path a socket read would
     */

    template
    <
        typename E = void
    >
    class MultiOperationProbeT : public bl::tasks::MultiOperationTask
    {
        BL_DECLARE_OBJECT_IMPL( MultiOperationProbeT )

    public:

        typedef MultiOperationProbeT< E >                                   this_type;
        typedef bl::tasks::MultiOperationTask                               base_type;

    protected:

        const MultiOperationProbeOptions                                    m_options;

        /*
         * Written by scheduleTask() under the task lock and read by cancelTask(), which the task
         * layer also calls under it, and by initiateClose(), which runs after a handler has
         * acquired and released it - so the write is ordered before every read
         */

        std::vector< bl::cpp::SafeUniquePtr< bl::asio::deadline_timer > >   m_timers;

        /*
         * Atomic throughout: the handlers run on pool threads and the test thread reads these
         * once the queue has been flushed
         */

        std::atomic< std::size_t >                                          m_bodiesEntered;
        std::atomic< std::size_t >                                          m_bodiesSucceeded;
        std::atomic< std::size_t >                                          m_bodiesSeeingClosing;
        std::atomic< std::size_t >                                          m_stopCalls;
        std::atomic< std::size_t >                                          m_initiateCloseCalls;
        std::atomic< std::size_t >                                          m_finishContinuationCalls;
        std::atomic< std::size_t >                                          m_pendingAtFinishContinuation;
        std::atomic< std::size_t >                                          m_pendingAtStop;
        std::atomic< std::size_t >                                          m_bodiesAtStop;
        std::atomic< bool >                                                 m_stoppedWithException;
        std::atomic< bool >                                                 m_stoppedIsExpected;
        std::atomic< bool >                                                 m_closingAtScheduleTask;
        std::atomic< bool >                                                 m_closingAfterBeginClose;
        std::atomic< bool >                                                 m_closingAtInitiateClose;

        MultiOperationProbeT( SAA_in const MultiOperationProbeOptions& options )
            :
            m_options( options ),
            m_bodiesEntered( 0U ),
            m_bodiesSucceeded( 0U ),
            m_bodiesSeeingClosing( 0U ),
            m_stopCalls( 0U ),
            m_initiateCloseCalls( 0U ),
            m_finishContinuationCalls( 0U ),
            m_pendingAtFinishContinuation( 0U ),
            m_pendingAtStop( 0U ),
            m_bodiesAtStop( 0U ),
            m_stoppedWithException( false ),
            m_stoppedIsExpected( false ),
            m_closingAtScheduleTask( false ),
            m_closingAfterBeginClose( false ),
            m_closingAtInitiateClose( false )
        {
        }

        std::size_t delayMs( SAA_in const std::size_t index ) const NOEXCEPT
        {
            /*
             * The early operations are the failing ones, and then the successes a
             * closeAfterSuccesses case needs to land before the rest. They expire on the
             * firstDelayMs ladder; everything else waits restDelayMs, which is what leaves it
             * outstanding. With closeAfterSuccesses at its default of zero this is the ladder
             * the failing operations alone were already on
             */

            if( index < m_options.failures + m_options.closeAfterSuccesses )
            {
                return m_options.firstDelayMs + index * m_options.failStepMs;
            }

            return m_options.restDelayMs;
        }

        void onTimer(
            SAA_in                  const std::size_t                           index,
            SAA_in                  const bl::eh::error_code&                   ec
            ) NOEXCEPT
        {
            BL_TASKS_HANDLER_BEGIN()

            ++m_bodiesEntered;

            /*
             * Whether the accessor already reports the closing state when this body is entered.
             * It is read before the error check so that a body which was cancelled by
             * initiateClose() is counted too - those are the ones which must see it
             */

            if( base_type::isClosing() )
            {
                ++m_bodiesSeeingClosing;
            }

            BL_TASKS_HANDLER_CHK_EC( ec );

            if( index < m_options.failures )
            {
                BL_THROW( bl::UnexpectedException(), BL_MSG() << "multiop-failure-" << index );
            }

            ++m_bodiesSucceeded;

            /*
             * After how many successes the run closes deliberately - every one of them, or the
             * chosen number which leaves the rest of the operations in flight
             */

            const std::size_t closeAfter = m_options.closeWhenAllSucceed
                ? m_options.operations
                : m_options.closeAfterSuccesses;

            if( closeAfter && m_bodiesSucceeded == closeAfter )
            {
                /*
                 * The deliberate end of a run which had no error. Note this is called from a
                 * handler body, so the task lock IS held - which is the case beginClose() is
                 * designed for
                 */

                base_type::beginClose();

                m_closingAfterBeginClose = base_type::isClosing();
            }

            BL_TASKS_HANDLER_END_MULTIOP()
        }

        virtual void initiateClose() OVERRIDE
        {
            using namespace bl;

            ++m_initiateCloseCalls;

            m_closingAtInitiateClose = base_type::isClosing();

            if( m_options.initiateCloseThrows )
            {
                BL_THROW( bl::UnexpectedException(), BL_MSG() << "multiop-initiate-close" );
            }

            if( ! m_options.initiateCloseCancels )
            {
                /*
                 * The operations still in flight are left to finish on their own, which is how
                 * a case gets several genuine failures instead of one failure and a fan of
                 * operation_aborted
                 */

                return;
            }

            for( auto& timer : m_timers )
            {
                if( timer )
                {
                    eh::error_code ec;

                    timer -> cancel( ec );
                }
            }
        }

        virtual void cancelTask() OVERRIDE
        {
            using namespace bl;

            for( auto& timer : m_timers )
            {
                if( timer )
                {
                    eh::error_code ec;

                    timer -> cancel( ec );
                }
            }
        }

        virtual void scheduleTask( SAA_in const std::shared_ptr< bl::tasks::ExecutionQueue >& eq ) OVERRIDE
        {
            using namespace bl;

            m_timers.clear();
            m_bodiesEntered = 0U;
            m_bodiesSucceeded = 0U;
            m_bodiesSeeingClosing = 0U;
            m_closingAfterBeginClose = false;
            m_closingAtInitiateClose = false;

            const auto threadPool = base_type::getThreadPool( eq );

            auto& aioService = threadPool -> aioService();

            m_timers.resize( m_options.operations );

            for( std::size_t i = 0U; i < m_options.operations; ++i )
            {
                m_timers[ i ].reset( new asio::deadline_timer( aioService ) );
            }

            for( std::size_t i = 0U; i < m_options.operations; ++i )
            {
                m_timers[ i ] -> expires_from_now(
                    time::milliseconds( static_cast< long >( delayMs( i ) ) )
                    );

                base_type::beginOperation();

                m_timers[ i ] -> async_wait(
                    cpp::bind(
                        &this_type::onTimer,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        i,
                        asio::placeholders::error
                        )
                    );
            }

            /*
             * Read with every operation already in flight and the task lock still held, so no
             * handler body can have run yet: this is what the accessor must report on a task
             * which is running and has not closed
             */

            m_closingAtScheduleTask = base_type::isClosing();
        }

        virtual bool scheduleTaskFinishContinuation(
            SAA_in_opt              const std::exception_ptr&                   eptrIn = nullptr
            ) OVERRIDE
        {
            BL_UNUSED( eptrIn );

            ++m_finishContinuationCalls;

            m_pendingAtFinishContinuation = base_type::pendingOperations();

            return false;
        }

        virtual auto onTaskStoppedNothrow(
            SAA_in_opt              const std::exception_ptr&                   eptrIn = nullptr,
            SAA_inout_opt           bool*                                       isExpectedException = nullptr
            ) NOEXCEPT
            -> std::exception_ptr OVERRIDE
        {
            ++m_stopCalls;

            m_pendingAtStop = base_type::pendingOperations();
            m_bodiesAtStop = m_bodiesEntered.load();
            m_stoppedWithException = ( nullptr != eptrIn );
            m_stoppedIsExpected = ( isExpectedException && *isExpectedException );

            return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
        }

    public:

        MultiOperationOutcome outcome() const NOEXCEPT
        {
            MultiOperationOutcome result;

            result.stopCalls = m_stopCalls;
            result.initiateCloseCalls = m_initiateCloseCalls;
            result.finishContinuationCalls = m_finishContinuationCalls;
            result.pendingAtFinishContinuation = m_pendingAtFinishContinuation;
            result.pendingAtStop = m_pendingAtStop;
            result.bodiesAtStop = m_bodiesAtStop;
            result.bodiesEntered = m_bodiesEntered;
            result.bodiesSeeingClosing = m_bodiesSeeingClosing;
            result.stoppedWithException = m_stoppedWithException;
            result.stoppedIsExpected = m_stoppedIsExpected;
            result.closingAtScheduleTask = m_closingAtScheduleTask;
            result.closingAfterBeginClose = m_closingAfterBeginClose;
            result.closingAtInitiateClose = m_closingAtInitiateClose;

            return result;
        }
    };

    typedef bl::om::ObjectImpl< MultiOperationProbeT<> > MultiOperationProbeImpl;

    /**
     * @brief A probe together with the pool it ran on
     *
     * The probe holds its timers by value and they were constructed against the pool's
     * io_service, so destroying the probe destroys them and each destructor reaches into the
     * deadline_timer_service to cancel. The pool therefore has to outlive the probe, and the
     * two cannot be handed back separately without the caller having to know that.
     *
     * Members are destroyed in reverse order of declaration, so declaring the pool first is
     * what makes the probe - and with it the timers - go first. Do not reorder them.
     */

    struct MultiOperationProbeRun
    {
        bl::om::ObjPtrDisposable< bl::ThreadPool >                          threadPool;
        bl::om::ObjPtr< MultiOperationProbeImpl >                           taskImpl;
    };

    /**
     * @brief Runs one probe on a pool of the given size and hands the task back for inspection,
     * with the pool it ran on so that it outlives the task
     *
     * cancelAfterMs, when non zero, requests a cancel that many milliseconds after the push -
     * long enough for the task to be executing and short enough for its timers to be pending
     */

    auto runMultiOperationProbe(
        SAA_in                  const MultiOperationProbeOptions&           options,
        SAA_in                  const std::size_t                           threadsCount,
        SAA_in_opt              const std::size_t                           cancelAfterMs = 0U
        )
        -> MultiOperationProbeRun
    {
        using namespace bl;
        using namespace bl::tasks;

        auto taskImpl = MultiOperationProbeImpl::createInstance( options );

        auto tpLocal = om::lockDisposable(
            ThreadPoolImpl::createInstance< ThreadPool >( os::AbstractPriority::Normal, threadsCount )
            );

        {
            /*
             * The queue lives in the inner scope so it is disposed before the pool it points at
             */

            const auto eq = om::lockDisposable(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepNone )
                );

            eq -> setLocalThreadPool( tpLocal.get() );

            eq -> push_back( om::qi< Task >( taskImpl ) );

            if( cancelAfterMs )
            {
                os::sleep( time::milliseconds( static_cast< long >( cancelAfterMs ) ) );

                taskImpl -> requestCancel();
            }

            eq -> flushNoThrowIfFailed();
        }

        return MultiOperationProbeRun { std::move( tpLocal ), std::move( taskImpl ) };
    }

    /**
     * @brief The whole multi-operation suite, on a pool of the given size
     *
     * Every case is run with one thread and with four. Nothing here may depend on a second
     * thread: the model TaskBase.h asks for is one where a task never blocks on another, so a
     * single threaded pool has to be enough
     */

    void runMultiOperationSuite( SAA_in const std::size_t threadsCount )
    {
        using namespace bl;
        using namespace bl::tasks;

        {
            /*
             * Every operation succeeds. The last handler asks for the close, and the task
             * completes once - with no error - when the count reaches zero
             */

            MultiOperationProbeOptions options( 3U, 0U /* failures */ );

            options.closeWhenAllSucceed = true;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( ! taskImpl -> isFailedOrFailing() );
            UTF_REQUIRE( ! taskImpl -> exception() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE( ! outcome.stoppedWithException );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
        }

        {
            /*
             * The deliberate close with operations still OUTSTANDING - the quadrant the case
             * above does not reach, because there everything had already succeeded and
             * initiateClose() had nothing to cancel
             *
             * The first operation succeeds and asks for the close while the other two are still
             * pending, initiateClose() cancels them, and both arrive with operation_aborted.
             * Those aborts are self inflicted: the task caused them by closing, nothing failed,
             * and the task must complete SUCCESSFULLY. Without the fix in MultiOperationTask.h
             * the first of them becomes the task's error and the task reports isFailed() on its
             * own clean shutdown, which is what this case exists to hold down
             *
             * This is the shape design 5.1's connection task closes in as a matter of course -
             * on GOAWAY, on the idle deadline, when the last stream finishes - with a read and
             * its timers outstanding
             */

            MultiOperationProbeOptions options( 3U, 0U /* failures */ );

            options.firstDelayMs = 50U;
            options.restDelayMs = 5000U;
            options.closeAfterSuccesses = 1U;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( ! taskImpl -> isFailedOrFailing() );
            UTF_REQUIRE( ! taskImpl -> exception() );
            UTF_REQUIRE( ! outcome.stoppedWithException );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.finishContinuationCalls, 1U );

            /*
             * All three handlers ran - one succeeded and two were cancelled - and the task took
             * its terminal path only once nothing was left outstanding. Without these the case
             * could pass on a run where the two timers were never armed at all
             */

            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.bodiesAtStop, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );
            UTF_REQUIRE( outcome.closingAfterBeginClose );
            UTF_REQUIRE( outcome.closingAtInitiateClose );

            /*
             * The two cancelled bodies are entered while the task is already closing, which is
             * what says they were in flight when beginClose() was called
             */

            UTF_REQUIRE( outcome.bodiesSeeingClosing >= 2U );
        }

        {
            /*
             * The same shape, plus an external cancel - which must still report failed
             *
             * initiateClose() is told not to cancel, so the two operations the deliberate close
             * left outstanding are still pending when requestCancel() arrives and cancelTask()
             * cancels them. The operation_aborted they then report is NOT self inflicted: the
             * caller asked for it, and a caller which cancels a task must not be told the task
             * finished normally just because the task had privately decided to close. This pins
             * the one thing the close fix must not silently change
             */

            MultiOperationProbeOptions options( 3U, 0U /* failures */ );

            options.firstDelayMs = 50U;
            options.restDelayMs = 5000U;
            options.closeAfterSuccesses = 1U;
            options.initiateCloseCancels = false;

            const auto probeRun = runMultiOperationProbe( options, threadsCount, 300U /* cancelAfterMs */ );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE( outcome.stoppedWithException );
            UTF_REQUIRE( outcome.stoppedIsExpected );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            try
            {
                cpp::safeRethrowException( taskImpl -> exception() );

                UTF_FAIL( "A cancelled multi-operation task must complete with an exception" );
            }
            catch( bl::SystemException& e )
            {
                UTF_REQUIRE( asio::error::operation_aborted == e.code() );
            }
        }

        {
            /*
             * One operation fails while the other two are still pending. initiateClose() runs
             * once and cancels them, the task completes only after all three handlers have run,
             * and the error it reports is the first one - not the operation_aborted of the two
             * which were cancelled because of it
             */

            MultiOperationProbeOptions options( 3U, 1U /* failures */ );

            options.firstDelayMs = 50U;
            options.restDelayMs = 5000U;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.bodiesAtStop, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( taskImpl -> exception() ),
                bl::UnexpectedException,
                "multiop-failure-0"
                );
        }

        {
            /*
             * Three operations fail, one after another, with nothing cancelled - so all three
             * errors are genuine and arrive while the task is already closing. The first one
             * wins and there is still exactly one completion
             */

            MultiOperationProbeOptions options( 3U, 3U /* failures */ );

            options.firstDelayMs = 50U;
            options.failStepMs = 60U;
            options.initiateCloseCancels = false;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( taskImpl -> exception() ),
                bl::UnexpectedException,
                "multiop-failure-0"
                );
        }

        {
            /*
             * The same configuration is the direct regression test for the defect this mix-in
             * exists for: notifyReadyImpl() runs scheduleTaskFinishContinuation() and
             * onTaskStoppedNothrow() BEFORE it consults m_notifyCalled, so under the plain
             * handler epilog each of the three failing handlers would re-enter the finish path
             * of a task which is already finishing
             *
             * The finish continuation must be entered exactly once, and only once nothing is
             * outstanding - which is what keeps an asynchronous shutdown continuation from
             * starting while a read or a write is still pending
             */

            MultiOperationProbeOptions options( 3U, 3U /* failures */ );

            options.firstDelayMs = 50U;
            options.failStepMs = 60U;
            options.initiateCloseCancels = false;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE_EQUAL( outcome.finishContinuationCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtFinishContinuation, 0U );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
        }

        {
            /*
             * Cancel with three operations pending. Each handler is entered with
             * operation_aborted, which the macro classifies as expected, and the task still
             * completes exactly once
             */

            MultiOperationProbeOptions options( 3U, 0U /* failures */ );

            options.restDelayMs = 5000U;

            const auto probeRun = runMultiOperationProbe( options, threadsCount, 150U /* cancelAfterMs */ );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );
            UTF_REQUIRE( outcome.stoppedWithException );
            UTF_REQUIRE( outcome.stoppedIsExpected );

            try
            {
                cpp::safeRethrowException( taskImpl -> exception() );

                UTF_FAIL( "A cancelled multi-operation task must complete with an exception" );
            }
            catch( bl::SystemException& e )
            {
                UTF_REQUIRE( asio::error::operation_aborted == e.code() );
            }
        }

        {
            /*
             * initiateClose() itself throws. Nothing is cancelled, so the other two operations
             * run to completion on their own, and the task still completes exactly once - with
             * the original error rather than the one the close attempt raised
             */

            MultiOperationProbeOptions options( 3U, 1U /* failures */ );

            options.firstDelayMs = 50U;
            options.restDelayMs = 400U;
            options.initiateCloseThrows = true;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( taskImpl -> exception() ),
                bl::UnexpectedException,
                "multiop-failure-0"
                );
        }

        {
            /*
             * Stress: many operations, half of them failing at the same instant, so several
             * threads run onOperationCompleted() concurrently and race for the first error and
             * for the single terminal path
             */

            MultiOperationProbeOptions options( 32U, 16U /* failures */ );

            options.firstDelayMs = 100U;
            options.restDelayMs = 150U;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE_EQUAL( outcome.stopCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.finishContinuationCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 32U );
            UTF_REQUIRE_EQUAL( outcome.bodiesAtStop, 32U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );
        }

        {
            /*
             * A completed task is pushed again. scheduleNothrow() resets the accounting the same
             * way it resets the exception, m_notifyCalled and the cancel latch - and a stale
             * terminal flag would be invisible except as a second run which never completes
             */

            MultiOperationProbeOptions options( 2U, 0U /* failures */ );

            options.closeWhenAllSucceed = true;

            /*
             * The pool is declared before the probe so that it is destroyed after it, for the
             * reason MultiOperationProbeRun states: the probe's timers are built against this
             * pool's io_service and reach into it as they are destroyed. Do not reorder these two
             */

            const auto tpLocal = om::lockDisposable(
                ThreadPoolImpl::createInstance< ThreadPool >( os::AbstractPriority::Normal, threadsCount )
                );

            const auto taskImpl = MultiOperationProbeImpl::createInstance( options );

            {
                const auto eq = om::lockDisposable(
                    ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                    );

                eq -> setLocalThreadPool( tpLocal.get() );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> flush() );

                UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                UTF_REQUIRE_EQUAL( taskImpl -> outcome().stopCalls, 1U );

                while( eq -> pop( false /* wait */ ) )
                {
                    /*
                     * Drain the ready queue so the task is pushed again as a fresh entry
                     */
                }

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> flush() );

                UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

                const auto outcome = taskImpl -> outcome();

                UTF_REQUIRE( ! task -> isFailed() );
                UTF_REQUIRE_EQUAL( outcome.stopCalls, 2U );
                UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 2U );
                UTF_REQUIRE_EQUAL( outcome.bodiesAtStop, 2U );
                UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );
                UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 2U );

                eq -> flushAndDiscardReady();
            }
        }
    }

    /**
     * @brief What isClosing() reports, on a pool of the given size
     *
     * The accessor is the one piece of the accounting the mix-in publishes, because a task whose
     * loops re-arm operations has to ask whether it is still worth starting another one. It is
     * therefore the member most likely to be quietly reduced to a field which nothing maintains,
     * and these assertions are about the state it tracks rather than about the value it returns
     */

    void runIsClosingSuite( SAA_in const std::size_t threadsCount )
    {
        using namespace bl;
        using namespace bl::tasks;

        {
            /*
             * A task which has never run is not closing
             */

            const auto taskImpl = MultiOperationProbeImpl::createInstance(
                MultiOperationProbeOptions( 1U /* operations */, 0U /* failures */ )
                );

            UTF_REQUIRE( ! taskImpl -> isClosing() );
        }

        {
            /*
             * The deliberate path. Nothing is closing while the three operations are in flight;
             * beginClose() is what enters the state, and the accessor reports it immediately -
             * from inside the very handler body which called it, with the task lock held; the
             * accounting then agrees, because it decides to call initiateClose() only when the
             * same flag is set
             */

            MultiOperationProbeOptions options( 3U /* operations */, 0U /* failures */ );

            options.closeWhenAllSucceed = true;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( ! taskImpl -> isFailedOrFailing() );
            UTF_REQUIRE( ! outcome.closingAtScheduleTask );
            UTF_REQUIRE( outcome.closingAfterBeginClose );
            UTF_REQUIRE( outcome.closingAtInitiateClose );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            /*
             * The accounting is per run, not per completion: the state the run ended in is still
             * readable once the queue has been flushed
             */

            UTF_REQUIRE( taskImpl -> isClosing() );
        }

        {
            /*
             * The error path. beginClose() is never called here - the first error is what enters
             * the closing state, inside onOperationCompleted() and under the accounting lock -
             * and the accessor has to report that too
             *
             * The other two operations are five seconds out, so the only reason their handlers
             * ever run is that initiateClose() cancelled them because of the first error. Each of
             * them therefore enters after the state was entered and must see it, while the one
             * which failed entered before anything had closed and must not: two bodies out of
             * three, on any pool size
             */

            MultiOperationProbeOptions options( 3U /* operations */, 1U /* failures */ );

            options.firstDelayMs = 50U;
            options.restDelayMs = 5000U;

            const auto probeRun = runMultiOperationProbe( options, threadsCount );
            const auto& taskImpl = probeRun.taskImpl;
            const auto outcome = taskImpl -> outcome();

            UTF_REQUIRE( taskImpl -> isFailed() );
            UTF_REQUIRE( ! outcome.closingAtScheduleTask );
            UTF_REQUIRE( ! outcome.closingAfterBeginClose );
            UTF_REQUIRE( outcome.closingAtInitiateClose );
            UTF_REQUIRE_EQUAL( outcome.initiateCloseCalls, 1U );
            UTF_REQUIRE_EQUAL( outcome.bodiesEntered, 3U );
            UTF_REQUIRE_EQUAL( outcome.bodiesSeeingClosing, 2U );
            UTF_REQUIRE_EQUAL( outcome.pendingAtStop, 0U );

            UTF_REQUIRE( taskImpl -> isClosing() );
        }

        {
            /*
             * A completed task is pushed again. scheduleNothrow() clears the closing state along
             * with the rest of the accounting, and the accessor has to follow it - a task which
             * came back still reporting that it is closing would refuse to start any operation on
             * its second run
             */

            MultiOperationProbeOptions options( 2U /* operations */, 0U /* failures */ );

            options.closeWhenAllSucceed = true;

            /*
             * The pool is declared before the probe so that it is destroyed after it, for the
             * reason MultiOperationProbeRun states: the probe's timers are built against this
             * pool's io_service and reach into it as they are destroyed. Do not reorder these two
             */

            const auto tpLocal = om::lockDisposable(
                ThreadPoolImpl::createInstance< ThreadPool >( os::AbstractPriority::Normal, threadsCount )
                );

            const auto taskImpl = MultiOperationProbeImpl::createInstance( options );

            {
                const auto eq = om::lockDisposable(
                    ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                    );

                eq -> setLocalThreadPool( tpLocal.get() );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> flush() );

                UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                UTF_REQUIRE( taskImpl -> isClosing() );

                while( eq -> pop( false /* wait */ ) )
                {
                    /*
                     * Drain the ready queue so the task is pushed again as a fresh entry
                     */
                }

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> flush() );

                UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

                const auto outcome = taskImpl -> outcome();

                UTF_REQUIRE( ! task -> isFailed() );
                UTF_REQUIRE( ! outcome.closingAtScheduleTask );
                UTF_REQUIRE( outcome.closingAfterBeginClose );
                UTF_REQUIRE_EQUAL( outcome.stopCalls, 2U );

                eq -> flushAndDiscardReady();
            }
        }
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_MultiOperationTaskSingleThreadedTests )
{
    runMultiOperationSuite( 1U /* threadsCount */ );
}

UTF_AUTO_TEST_CASE( Tasks_MultiOperationTaskMultiThreadedTests )
{
    runMultiOperationSuite( 4U /* threadsCount */ );
}

UTF_AUTO_TEST_CASE( Tasks_MultiOperationTaskIsClosingTests )
{
    runIsClosingSuite( 1U /* threadsCount */ );
    runIsClosingSuite( 4U /* threadsCount */ );
}
