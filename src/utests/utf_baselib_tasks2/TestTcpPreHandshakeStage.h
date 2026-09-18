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

#ifndef __UTEST_TESTTCPPREHANDSHAKESTAGE_H_
#define __UTEST_TESTTCPPREHANDSHAKESTAGE_H_

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TcpBaseTasks.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * The order in which TcpConnectionEstablisherConnector drives a connection
 *
 * TcpConnectionEstablisherConnector::onConnectionEstablished (TcpBaseTasks.h:1315) goes straight
 * from connect to beginProtocolHandshake, and every TCP client task in the library derives from it.
 * Nothing pins that order today, which is what makes any change to that handler unreviewable: a
 * call moved out of the handler, out of the lock, or out of the try block would still pass the
 * suite. These cases pin it through the two virtuals which already exist - continueAfterResolved
 * and continueAfterConnected - so they characterize today's behavior and must go on passing
 * unchanged once the pre-handshake stage hook of design 3.6 is added
 *
 * The peer is a listening socket on an ephemeral loopback port which never accepts: the kernel
 * completes the connect out of the listen backlog, so no accepting thread is needed and no fixed
 * port is bound
 */

namespace utest
{
    namespace prehandshake
    {
        /**
         * @brief A listening socket on an ephemeral loopback port which never accepts
         *
         * Binding port zero is what keeps these cases free of the machine global test lock: two
         * of them can run at the same time, and they do not collide with the fixed test port
         */

        class LoopbackListener
        {
            BL_NO_COPY_OR_MOVE( LoopbackListener )

        public:

            LoopbackListener()
                :
                m_acceptor( m_ioService )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();
            }

            auto port() const -> unsigned short
            {
                return m_acceptor.local_endpoint().port();
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
        };

        /**
         * @brief A plain stream connection establisher which records the order it was driven in
         *
         * Only the virtuals which exist today are used, so the same probe observes the same
         * sequence before and after the pre-handshake stage hook is introduced
         */

        class ConnectOrderProbe :
            public bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSocketAsyncBase >
        {
        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSocketAsyncBase >
                                                                                base_type;

        protected:

            typedef base_type::tcp_resolver_type                                tcp_resolver_type;

            mutable bl::os::mutex                                               m_eventsLock;
            std::vector< std::string >                                          m_events;

            bl::cpp::ScalarTypeIniter< bool >                                   m_cancelAfterResolved;
            bl::cpp::ScalarTypeIniter< bool >                                   m_throwFromContinuation;
            bl::cpp::ScalarTypeIniter< bool >                                   m_wasChannelOpenAtContinuation;
            bl::cpp::ScalarTypeIniter< bool >                                   m_wasExpectedExceptionAtStop;

            ConnectOrderProbe(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ )
            {
            }

            void record( SAA_in const char* event )
            {
                BL_MUTEX_GUARD( m_eventsLock );

                m_events.push_back( std::string( event ) );
            }

            virtual bool continueAfterResolved( SAA_in tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                record( "resolved" );

                const bool result = base_type::continueAfterResolved( endpoints );

                if( m_cancelAfterResolved )
                {
                    /*
                     * The connect has been started by the base implementation above and is now in
                     * flight; marking the task cancelled without touching the socket is what makes
                     * "the cancel landed before the connect handler completed" deterministic - the
                     * connect itself still succeeds and it is the isCanceled() guard at the top of
                     * onConnectionEstablished which has to stop the task
                     */

                    base_type::requestCancelInternalMarkOnlyNoLock();
                }

                return result;
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                record( "continueAfterConnected" );

                m_wasChannelOpenAtContinuation = base_type::isChannelOpen();

                if( m_throwFromContinuation )
                {
                    BL_THROW(
                        bl::UnexpectedException(),
                        BL_MSG()
                            << "Continuation of the connect handler has failed"
                        );
                }

                return false;
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                if( isExpectedException )
                {
                    m_wasExpectedExceptionAtStop = *isExpectedException;
                }

                return result;
            }

        public:

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_eventsLock );

                return m_events;
            }

            void cancelAfterResolved() NOEXCEPT
            {
                m_cancelAfterResolved = true;
            }

            void throwFromContinuation() NOEXCEPT
            {
                m_throwFromContinuation = true;
            }

            bool wasChannelOpenAtContinuation() const NOEXCEPT
            {
                return m_wasChannelOpenAtContinuation;
            }

            bool wasExpectedExceptionAtStop() const NOEXCEPT
            {
                return m_wasExpectedExceptionAtStop;
            }
        };

        typedef bl::om::ObjectImpl< ConnectOrderProbe > ConnectOrderProbeImpl;

        /**
         * @brief Runs a probe connector against the listener and waits for it to finish
         *
         * The queue keeps the task so the caller can interrogate the probe afterwards; the wait is
         * the rendezvous which makes everything the probe recorded visible to the test thread
         */

        template
        <
            typename PROBE
        >
        inline void runProbeToCompletion( SAA_in const bl::om::ObjPtr< PROBE >& probe )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ &probe ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto task = om::qi< Task >( probe );

                    eq -> push_back( task );
                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );
                }
                );
        }

        /**
         * @brief The exception a failed probe task carries, as a message
         */

        inline auto exceptionMessageOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> std::string
        {
            using namespace bl;

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE( task -> exception() );

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( std::exception& e )
            {
                return std::string( e.what() );
            }

            UTF_FAIL( "cpp::safeRethrowException must throw" );

            return std::string();
        }

        /**
         * @brief The error code a failed probe task carries
         */

        inline auto exceptionCodeOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> bl::eh::error_code
        {
            using namespace bl;

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE( task -> exception() );

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( eh::system_error& e )
            {
                return e.code();
            }
            catch( std::exception& )
            {
                UTF_FAIL( "The task must have failed with a system error" );
            }

            return eh::error_code();
        }

    } // prehandshake

} // utest

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_TodaysConnectOrderTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * The order of a successful connection establishment today: the resolve continuation first,
     * then - after the connect handler has run - the connect continuation, each exactly once. The
     * channel is already open when continueAfterConnected is entered, which is what says the
     * continuation runs after the connect and not beside it
     *
     * continueAfterConnected returning false is the "task completes" path of design 3.8 commit 2:
     * the handler falls through to BL_TASKS_HANDLER_END() and the task completes without an error
     */

    LoopbackListener listener;

    const auto probe = ConnectOrderProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port()
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "continueAfterConnected" ) );

    UTF_REQUIRE( probe -> wasChannelOpenAtContinuation() );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_ContinuationRunsInConnectHandlerTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * That the continuation runs *inside* the connect handler - in its try block, and therefore
     * inside the same BL_MUTEX_GUARD( m_lock ) scope BL_TASKS_HANDLER_BEGIN_CHK_EC() opened - is
     * not observable by looking at the order alone. Throwing from it is what proves it: only a
     * call made inside the handler's try block can fail the task with its own exception
     *
     * This is the assertion which would catch a refactoring that moved the handshake call out of
     * the connect handler, or posted it, or took it out from under the task lock
     */

    LoopbackListener listener;

    const auto probe = ConnectOrderProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port()
        );

    probe -> throwFromContinuation();

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        cpp::contains(
            exceptionMessageOf( task ),
            std::string( "Continuation of the connect handler has failed" )
            )
        );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "continueAfterConnected" ) );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_CancelBeforeConnectCompletesTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A cancel which lands while the connect is in flight must stop the task at the top of
     * onConnectionEstablished, before anything on the connected socket is started: the resolve
     * continuation ran, the connect continuation did not, and the task fails with
     * operation_aborted classified as an expected exception rather than a real error
     *
     * The cancel is marked from continueAfterResolved, after the base implementation has started
     * the connect, so the timing is exact rather than raced from the test thread
     */

    LoopbackListener listener;

    const auto probe = ConnectOrderProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port()
        );

    probe -> cancelAfterResolved();

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE_EQUAL( exceptionCodeOf( task ), asio::error::operation_aborted );

    UTF_REQUIRE( probe -> wasExpectedExceptionAtStop() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 1U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
}

#endif /* __UTEST_TESTTCPPREHANDSHAKESTAGE_H_ */
