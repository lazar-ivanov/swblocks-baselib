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
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <stdexcept>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

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
