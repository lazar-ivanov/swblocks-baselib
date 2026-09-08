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
#include <baselib/reactive/FixedWorkerPoolUnitBase.h>
#include <baselib/reactive/ObservableBase.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ErrorDispatcher.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * Reactive processing units shutdown tests
 */

namespace
{
    /**
     * @brief A minimal fanout observable which never does any work
     *
     * It exists to drive the shutdown path of an observable which was failed through
     * dispatchException() before its first iteration had a chance to create the child
     * tasks execution queue
     */

    template
    <
        typename E = void
    >
    class ReactiveFanoutProbeT :
        public bl::reactive::FanoutTasksObservable
    {
        BL_CTR_DEFAULT( ReactiveFanoutProbeT, protected )
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ReactiveFanoutProbeT )

    protected:

        typedef bl::reactive::FanoutTasksObservable                     base_type;

        virtual bl::om::ObjPtr< bl::tasks::Task > createSeedingTask() OVERRIDE
        {
            return nullptr;
        }

        virtual bool canAcceptReadyTask() OVERRIDE
        {
            return true;
        }

        virtual bool pushReadyTask( SAA_in const bl::om::ObjPtrCopyable< bl::tasks::Task >& task ) OVERRIDE
        {
            BL_UNUSED( task );

            return true;
        }

        virtual bool flushAllPendingTasks() OVERRIDE
        {
            return true;
        }

        virtual bool isWaitingExternalInput() NOEXCEPT OVERRIDE
        {
            return false;
        }
    };

    typedef bl::om::ObjectImpl< ReactiveFanoutProbeT<>, true /* enableSharedPtr */ > ReactiveFanoutProbeImpl;

    /**
     * @brief A minimal fixed worker pool unit which never does any work
     *
     * The STREAM template parameter of FixedWorkerPoolUnitBase is not used by the class
     * body, so void is sufficient here
     */

    template
    <
        typename E = void
    >
    class ReactiveWorkerPoolProbeT :
        public bl::reactive::FixedWorkerPoolUnitBase< void >
    {
        BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ReactiveWorkerPoolProbeT )

    protected:

        typedef bl::reactive::FixedWorkerPoolUnitBase< void >           base_type;

        ReactiveWorkerPoolProbeT()
            :
            base_type( std::string( "success:Reactive_WorkerPool_Probe" ) )
        {
        }

        virtual bool canAcceptReadyTask() OVERRIDE
        {
            return true;
        }

        virtual bool pushReadyTask( SAA_in const bl::om::ObjPtrCopyable< bl::tasks::Task >& task ) OVERRIDE
        {
            BL_UNUSED( task );

            return true;
        }

        virtual bool flushAllPendingTasks() OVERRIDE
        {
            return true;
        }
    };

    typedef bl::om::ObjectImpl< ReactiveWorkerPoolProbeT<>, true /* enableSharedPtr */ > ReactiveWorkerPoolProbeImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ReactiveFanoutFailBeforeFirstIterationTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ObservableBaseT::run() rethrows m_dispatchedException before it calls
     * chk2LoopUntilFinished(), so an observable which is failed through dispatchException()
     * before its task is pushed never runs FanoutTasksObservableT::chk2InitAndBeginProcessing()
     * and neither m_eqChildTasks nor m_notifyCB is ever created
     *
     * The shutdown path which follows must not assume otherwise - it has to surface the
     * dispatched exception rather than touch the child tasks queue which does not exist
     *
     * Note that FanoutTasksObservableT::chk2LoopUntilDoneInternal() still evaluates
     * m_eqChildTasks -> isEmpty() inside a BL_ASSERT, which is compiled out under NDEBUG, so
     * this case passes in release and dereferences the null queue in debug; the sibling guards
     * at FanoutTasksObservable.h:81 and FixedWorkerPoolUnitBase.h:149 are what that line needs
     */

    const auto createInjectedException = []() -> std::exception_ptr
    {
        std::exception_ptr eptr;

        try
        {
            BL_CHK(
                false,
                false,
                BL_MSG()
                    << "injected pre-start failure"
                );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        return eptr;
    };

    scheduleAndExecuteInParallel(
        [ &createInjectedException ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            {
                /*
                 * A bare fanout observable
                 */

                const auto probe = ReactiveFanoutProbeImpl::createInstance();

                probe -> allowNoSubscribers( true );

                const auto eptr = createInjectedException();

                UTF_REQUIRE( nullptr != eptr );

                om::qi< ErrorDispatcher >( probe ) -> dispatchException( eptr );

                const auto task = om::qi< Task >( probe );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "injected pre-start failure"
                    );

                UTF_REQUIRE( task -> isFailed() );
            }

            {
                /*
                 * The same for a fixed worker pool unit - this also pins the m_eqWorkerTasks
                 * guard in FixedWorkerPoolUnitBase::chk2LoopUntilShutdownFinished()
                 */

                const auto probe = ReactiveWorkerPoolProbeImpl::createInstance();

                probe -> allowNoSubscribers( true );

                const auto eptr = createInjectedException();

                UTF_REQUIRE( nullptr != eptr );

                om::qi< ErrorDispatcher >( probe ) -> dispatchException( eptr );

                const auto task = om::qi< Task >( probe );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "injected pre-start failure"
                    );

                UTF_REQUIRE( task -> isFailed() );
            }
        }
        );
}
