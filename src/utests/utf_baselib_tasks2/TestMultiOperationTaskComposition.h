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

#ifndef __UTEST_TESTMULTIOPERATIONTASKCOMPOSITION_H_
#define __UTEST_TESTMULTIOPERATIONTASKCOMPOSITION_H_

/*
 * LoopbackListener and runProbeToCompletion live in TestTcpPreHandshakeStage.h, in the named
 * namespace utest::prehandshake. They are included rather than copied: a second copy of a helper
 * in a named namespace of the same module is an ODR violation, not merely duplication
 * (src/utests/AGENTS.md, and invariant C6 of utf_inventory.py)
 */

#include "TestTcpPreHandshakeStage.h"

#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TcpBaseTasks.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * tasks::MultiOperationTaskT mixed over a connection establisher
 *
 * This is the shape design 5.1 needs and the only reason MultiOperationTaskT is parameterized on
 * its base at all: an HTTP/2 connection task is a TcpConnectionEstablisherConnector< STREAM > with
 * a read and a write and timers in flight together, so the mix-in has to sit on top of that chain
 * rather than bring a TaskBase of its own
 *
 * Nothing in the library instantiates that combination yet, and a mix-in which is only ever used
 * over a plain TaskBase would go on compiling and passing every test if it silently regressed to
 * "class MultiOperationTaskT : public TaskBase" - which is exactly how it was written the first
 * time, and it was not noticed until the composition was first attempted. These cases are the
 * standing proof, so that the regression fails here rather than in the middle of S4.1
 */

namespace utest
{
    namespace multiopcomposition
    {
        typedef bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSocketAsyncBase >
                                                                            establisher_t;

        /**
         * @brief A connection establisher which owns its post-connect work through the
         * multi-operation mix-in
         *
         * continueAfterConnected() returns true, so the connect handler does NOT complete the
         * task: the only remaining route to notifyReady() is the mix-in's single terminal path,
         * which is what makes this an end-to-end proof rather than a compile-only one
         *
         * The operation is a short timer rather than a socket read because it has to complete on
         * its own: the peer is a listening socket which never writes, so a read would only ever
         * be woken by the cancel and the case would be about cancellation instead
         */

        class ComposedConnectionProbe :
            public bl::tasks::MultiOperationTaskT< establisher_t >
        {
            BL_DECLARE_OBJECT_IMPL( ComposedConnectionProbe )

        public:

            typedef ComposedConnectionProbe                                  this_type;
            typedef bl::tasks::MultiOperationTaskT< establisher_t >           base_type;

            enum : long
            {
                OPERATION_IN_MILLISECONDS = 20L,
            };

        protected:

            mutable bl::os::mutex                                            m_eventsLock;
            std::vector< std::string >                                       m_events;

            bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >               m_operationTimer;

            std::atomic< std::size_t >                                       m_pendingAtStop;
            std::atomic< bool >                                              m_closingAtConnected;
            std::atomic< bool >                                              m_closingAtStop;

            /*
             * Three arguments, not two: the mix-in forwards whatever it is given to the base
             * through BL_VARIADIC_CTOR, so passing the establisher's optional logExceptions is
             * also what proves the forwarding is variadic rather than a fixed pair
             */

            ComposedConnectionProbe(
                SAA_in                  std::string&&                        host,
                SAA_in                  const unsigned short                 port
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_pendingAtStop( 0U ),
                m_closingAtConnected( false ),
                m_closingAtStop( false )
            {
                base_type::isCloseStreamOnTaskFinish( true );
            }

            void record( SAA_in const char* event )
            {
                BL_MUTEX_GUARD( m_eventsLock );

                m_events.push_back( std::string( event ) );
            }

            void onOperationTimer( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                record( "operationBody" );

                /*
                 * The deliberate end of the run. beginClose() is called from a handler body, so
                 * the task lock IS held here - which is the case it is designed for
                 */

                base_type::beginClose();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                using namespace bl;

                record( "continueAfterConnected" );

                m_closingAtConnected = base_type::isClosing();

                /*
                 * The timer is built on the socket's own executor, not on the default thread pool's
                 * io_service. Design 3.1 (D13) is that every operation of a connection task - the
                 * reads, the writes and the timers alike - runs on the one executor the stream was
                 * constructed on, so that a stranded stream policy serializes all of them without a
                 * single handler being wrapped by hand
                 *
                 * The two are the same io_context today whatever the case does - the establisher
                 * creates its socket on ThreadPoolDefault::getDefault( getThreadPoolId() )
                 * (TcpBaseTasks.h:1414) and, unlike TimerTaskBaseT::resetTimer, does not honour a
                 * queue-local pool - so this is not a fix. It is the shape: under the stranded
                 * policies of 3.1 getSocket().get_executor() IS the strand, and a timer built on
                 * the pool's io_service instead would run its handler off that strand, which is
                 * exactly the race D13 exists to make unrepresentable
                 *
                 * It also settles the timer's lifetime by construction: the timer can no longer be
                 * built against any service other than the one the stream itself lives on, which is
                 * the pattern behind
                 * notes/plans/issues/multioperation-probe-timer-teardown-record.md - the sibling
                 * probe builds its timers on a queue-local pool which dies before they do
                 */

                m_operationTimer.reset(
                    new asio::deadline_timer(
                        #if ( ( BOOST_VERSION / 100 ) >= 1072 )
                        base_type::getSocket().get_executor()
                        #else
                        base_type::getSocket().get_io_service()
                        #endif
                        )
                    );

                m_operationTimer -> expires_from_now(
                    time::milliseconds( OPERATION_IN_MILLISECONDS )
                    );

                base_type::beginOperation();

                m_operationTimer -> async_wait(
                    cpp::bind(
                        &this_type::onOperationTimer,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );

                /*
                 * The task has started async operations of its own and is not ready to finish
                 */

                return true;
            }

            virtual void initiateClose() OVERRIDE
            {
                using namespace bl;

                record( "initiateClose" );

                if( m_operationTimer )
                {
                    eh::error_code ec;

                    m_operationTimer -> cancel( ec );
                }
            }

            virtual void cancelTask() OVERRIDE
            {
                using namespace bl;

                /*
                 * The base only knows about the socket, so the task has to cancel the async
                 * objects it owns itself or a cancel would never wake this one
                 */

                if( m_operationTimer )
                {
                    eh::error_code ec;

                    m_operationTimer -> cancel( ec );
                }

                base_type::cancelTask();
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&            eptrIn = nullptr,
                SAA_inout_opt           bool*                                isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                record( "taskStopped" );

                m_pendingAtStop = base_type::pendingOperations();
                m_closingAtStop = base_type::isClosing();

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }

        public:

            /**
             * @brief The single TaskBase subobject of the composed chain
             *
             * A mix-in which brought a TaskBase of its own beside BASE would make this conversion
             * ambiguous and this header would stop compiling, which is the point of it
             */

            auto asTaskBase() NOEXCEPT -> bl::tasks::TaskBase*
            {
                return static_cast< bl::tasks::TaskBase* >( this );
            }

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_eventsLock );

                return m_events;
            }

            std::size_t pendingAtStop() const NOEXCEPT
            {
                return m_pendingAtStop;
            }

            bool closingAtConnected() const NOEXCEPT
            {
                return m_closingAtConnected;
            }

            bool closingAtStop() const NOEXCEPT
            {
                return m_closingAtStop;
            }
        };

        typedef bl::om::ObjectImpl< ComposedConnectionProbe > ComposedConnectionProbeImpl;

    } // multiopcomposition

} // utest

UTF_AUTO_TEST_CASE( Tasks_MultiOperationTaskOverConnectionEstablisherTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::multiopcomposition;

    /*
     * The composition itself is pinned by this header compiling at all - the declaration above is
     * ill-formed unless MultiOperationTaskT takes its base as a template parameter. What the case
     * adds is that the composed task is a working task: it is created through the object model,
     * it resolves the Task interface through the mix-in, and it completes through the mix-in's
     * terminal path with the establisher chain underneath it
     */

    utest::prehandshake::LoopbackListener listener;

    const auto probe = ComposedConnectionProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port()
        );

    UTF_REQUIRE( nullptr != probe -> asTaskBase() );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( nullptr != task.get() );
    UTF_REQUIRE( ! probe -> isClosing() );

    utest::prehandshake::runProbeToCompletion( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );
    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

    /*
     * The order is the assertion which says where the completion came from: the connect
     * continuation kept the task alive, one operation body ran and asked to close, and only then
     * did the mix-in call initiateClose() and take its terminal path into base_type::notifyReady()
     * - which is TaskBase::notifyReady reached through TcpConnectionEstablisherConnector, not
     * through a TaskBase the mix-in brought with it
     */

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 4U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "continueAfterConnected" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "operationBody" ) );
    UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "initiateClose" ) );
    UTF_REQUIRE_EQUAL( events[ 3 ], std::string( "taskStopped" ) );

    UTF_REQUIRE( ! probe -> closingAtConnected() );
    UTF_REQUIRE( probe -> closingAtStop() );
    UTF_REQUIRE_EQUAL( probe -> pendingAtStop(), 0U );
}

#endif /* __UTEST_TESTMULTIOPERATIONTASKCOMPOSITION_H_ */
