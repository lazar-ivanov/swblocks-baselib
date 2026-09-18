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
#include <baselib/core/ThreadPool.h>
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
         * @brief A listening socket on an ephemeral loopback port
         *
         * Binding port zero is what keeps these cases free of the machine global test lock: two
         * of them can run at the same time, and they do not collide with the fixed test port.
         * A connection does not have to be accepted for the connect to complete - the kernel
         * completes it out of the listen backlog - so accepting is only done by the one case
         * which has to look at what the connector did to the socket
         */

        class LoopbackListener
        {
            BL_NO_COPY_OR_MOVE( LoopbackListener )

        public:

            enum : long
            {
                DEFAULT_TIMEOUT_IN_SECONDS = 30L,
            };

            LoopbackListener()
                :
                m_acceptor( m_ioService ),
                m_timer( m_ioService )
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

            /**
             * @brief Accepts the pending connection and reports whether the peer has shut it down
             *
             * A socket which has been shut down is not a socket which has been closed:
             * TcpSocketCommonBase::shutdownSocket calls shutdown() and cancel(), never close(), so
             * is_open() still reads true afterwards and says nothing. What the peer sees is the
             * only real observable, and it is an orderly end of stream
             */

            bool acceptedPeerHasShutDown()
            {
                bl::asio::ip::tcp::socket socket( m_ioService );

                bl::eh::error_code acceptEc;
                bool acceptCompleted = false;

                m_acceptor.async_accept(
                    socket,
                    [ this, &acceptEc, &acceptCompleted ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        acceptEc = ec;
                        acceptCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( m_acceptor );

                runService();

                UTF_REQUIRE( acceptCompleted );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), acceptEc );

                char buffer[ 64 ];

                bl::eh::error_code readEc;
                bool readCompleted = false;

                socket.async_read_some(
                    bl::asio::buffer( buffer, sizeof( buffer ) ),
                    [ this, &readEc, &readCompleted ](
                        SAA_in      const bl::eh::error_code&                   ec,
                        SAA_in      const std::size_t                           transferred
                        ) -> void
                    {
                        BL_UNUSED( transferred );

                        readEc = ec;
                        readCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( socket );

                runService();

                UTF_REQUIRE( readCompleted );

                return bl::asio::error::eof == readEc;
            }

        private:

            /**
             * @brief Arms the deadline which cancels the pending operation if it does not complete
             *
             * The operation's own completion handler cancels this timer, so the service runs out
             * of work as soon as the operation is done rather than waiting the deadline out
             */

            template
            <
                typename CANCELABLE
            >
            void armDeadline( SAA_inout CANCELABLE& cancelable )
            {
                m_timer.expires_from_now( bl::time::seconds( DEFAULT_TIMEOUT_IN_SECONDS ) );

                m_timer.async_wait(
                    [ &cancelable ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        if( bl::asio::error::operation_aborted != ec )
                        {
                            bl::eh::error_code cancelEc;

                            cancelable.cancel( cancelEc );
                        }
                    }
                    );
            }

            void runService()
            {
                #if ( ( BOOST_VERSION / 100 ) >= 1066 )
                m_ioService.restart();
                #else
                m_ioService.reset();
                #endif

                m_ioService.run();
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::deadline_timer                                            m_timer;
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
            BL_DECLARE_OBJECT_IMPL( ConnectOrderProbe )

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

            auto countOf( SAA_in const char* event ) const -> std::size_t
            {
                BL_MUTEX_GUARD( m_eventsLock );

                std::size_t count = 0U;

                for( const auto& recorded : m_events )
                {
                    if( recorded == event )
                    {
                        ++count;
                    }
                }

                return count;
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
         * @brief A connection establisher with a pre-handshake stage, which records what it saw
         *
         * The asynchronous modes complete the stage from a timer handler wrapped in the task
         * handler macros, which is what design 3.6 requires of a real stage and what
         * TcpSslSocketAsyncBase::onHandshakeCompleted does
         */

        class StageProbe : public ConnectOrderProbe
        {
            BL_DECLARE_OBJECT_IMPL( StageProbe )

        public:

            typedef ConnectOrderProbe                                           base_type;

            enum class StageMode
            {
                Synchronous,                /* invokes the continuation and returns its result */
                Asynchronous,               /* completes from a short timer */
                Failing,                    /* throws out of the stage */
                Parked,                     /* never completes on its own; waits to be cancelled */
            };

            enum : long
            {
                ASYNCHRONOUS_STAGE_IN_MILLISECONDS = 20L,
                PARKED_STAGE_IN_MILLISECONDS = 30000L,
            };

        protected:

            typedef StageProbe                                                  this_type;

            const StageMode                                                     m_mode;

            bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >                  m_stageTimer;
            bl::cpp::bool_callback_t                                            m_continueCallback;

            StageProbe(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const StageMode                         mode
                )
                :
                base_type( BL_PARAM_FWD( host ), port ),
                m_mode( mode )
            {
                /*
                 * A bare connection establisher leaves the socket open for whoever detaches the
                 * stream from it, so whether a failed stage closes it is not observable unless the
                 * task owns the socket - which is what every real consumer does (SimpleHttpTask.h,
                 * TcpBlockTransferServer.h, HttpServer.h all set this)
                 */

                base_type::isCloseStreamOnTaskFinish( true );
            }

            virtual bool beginPreHandshakeStage(
                SAA_in                  const bl::cpp::bool_callback_t&         continueCallback
                )
                OVERRIDE
            {
                record( "stageEntered" );

                if( StageMode::Failing == m_mode )
                {
                    BL_THROW(
                        bl::UnexpectedException(),
                        BL_MSG()
                            << "Pre-handshake stage has failed"
                        );
                }

                if( StageMode::Synchronous == m_mode )
                {
                    const bool result = continueCallback();

                    record( "stageLeft" );

                    return result;
                }

                m_continueCallback = continueCallback;

                const auto threadPool = bl::ThreadPoolDefault::getDefault(
                    base_type::getThreadPoolId()
                    );

                m_stageTimer.reset( new bl::asio::deadline_timer( threadPool -> aioService() ) );

                m_stageTimer -> expires_from_now(
                    bl::time::milliseconds(
                        StageMode::Asynchronous == m_mode ?
                            ASYNCHRONOUS_STAGE_IN_MILLISECONDS : PARKED_STAGE_IN_MILLISECONDS
                        )
                    );

                m_stageTimer -> async_wait(
                    bl::cpp::bind(
                        &this_type::onStageCompleted,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error
                        )
                    );

                record( "stageLeft" );

                return true;
            }

            void onStageCompleted( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                record( "stageCompleted" );

                if( m_continueCallback() )
                {
                    /*
                     * The handshake has started async operations on the socket, so the task is
                     * not ready to finish yet
                     */

                    return;
                }

                BL_TASKS_HANDLER_END()
            }

            virtual void cancelTask() OVERRIDE
            {
                /*
                 * A stage owns its own async objects and has to cancel them itself - the base
                 * only knows about the socket, and a stage parked on a timer has no I/O in flight
                 * for the socket shutdown to abort
                 */

                if( m_stageTimer )
                {
                    bl::eh::error_code ec;

                    m_stageTimer -> cancel( ec );
                }

                base_type::cancelTask();
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * The continuation the stage is given holds a reference to the task, so a stage
                 * which parks it in a member and a task which owns the stage are a reference
                 * cycle and neither is ever released - the suite's exit time leak check is what
                 * catches it. Releasing it when the task stops is the same discipline
                 * TcpConnectionEstablisherAcceptor applies to its acceptor and back-off timer,
                 * and a real stage has to do it too
                 */

                m_continueCallback = bl::cpp::bool_callback_t();

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }
        };

        typedef bl::om::ObjectImpl< StageProbe > StageProbeImpl;

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

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_SynchronousStageTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A stage which invokes the continuation and returns its result is what the default hook does,
     * spelled out so it can be observed. The continuation is recorded *between* the stage being
     * entered and left, which is what says the handshake was started synchronously inside the hook,
     * inside the connect handler - the property the default must preserve
     *
     * It also covers the continuation returning false: the stage returns it, the connect handler
     * falls through and the task completes, exactly as it did before the hook existed
     */

    LoopbackListener listener;

    const auto probe = StageProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port(),
        StageProbe::StageMode::Synchronous
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 4U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "continueAfterConnected" ) );
    UTF_REQUIRE_EQUAL( events[ 3 ], std::string( "stageLeft" ) );

    UTF_REQUIRE( probe -> wasChannelOpenAtContinuation() );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_AsynchronousStageTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A stage which returns true has started async work, and nothing on the handshake path may
     * happen until it invokes the continuation: the connect handler returns without finishing the
     * task, and the continuation is recorded only after the stage's own handler has completed it
     */

    LoopbackListener listener;

    const auto probe = StageProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port(),
        StageProbe::StageMode::Asynchronous
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 5U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "stageLeft" ) );
    UTF_REQUIRE_EQUAL( events[ 3 ], std::string( "stageCompleted" ) );
    UTF_REQUIRE_EQUAL( events[ 4 ], std::string( "continueAfterConnected" ) );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_StageFailureTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A stage which throws fails the task with its own exception, and nothing on the handshake
     * path runs at all - continueAfterConnected is never reached. The socket is shut down as the
     * task unwinds, which the peer sees as an orderly end of stream
     */

    LoopbackListener listener;

    const auto probe = StageProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port(),
        StageProbe::StageMode::Failing
        );

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        cpp::contains(
            exceptionMessageOf( task ),
            std::string( "Pre-handshake stage has failed" )
            )
        );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "stageEntered" ) );

    UTF_REQUIRE( listener.acceptedPeerHasShutDown() );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_CancelDuringStageTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A cancel which lands while the stage is still running ends the task with operation_aborted
     * classified as an expected exception, and no handshake is attempted: the stage's own handler
     * is entered with the cancel already visible, so it never invokes the continuation
     *
     * The stage is parked on a long timer and the cancel is the only thing that can end it, so the
     * bounded poll below is a rendezvous rather than a sleep - if the stage were never entered the
     * case fails on the poll instead of hanging
     */

    LoopbackListener listener;

    const auto probe = StageProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port(),
        StageProbe::StageMode::Parked
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &probe, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            for( std::size_t i = 0U; i < 300U && 0U == probe -> countOf( "stageEntered" ); ++i )
            {
                os::sleep( time::milliseconds( 10 ) );
            }

            UTF_REQUIRE_EQUAL( probe -> countOf( "stageEntered" ), 1U );

            task -> requestCancel();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE_EQUAL( exceptionCodeOf( task ), asio::error::operation_aborted );

    UTF_REQUIRE( probe -> wasExpectedExceptionAtStop() );

    UTF_REQUIRE_EQUAL( probe -> countOf( "stageCompleted" ), 0U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "continueAfterConnected" ), 0U );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStage_CancelBeforeConnectSkipsStageTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * The complement of TcpPreHandshakeStage_CancelBeforeConnectCompletesTests: a cancel which
     * lands before the connect handler runs must not enter the stage at all, because the stage is
     * reached only past the isCanceled guard at the top of that handler
     */

    LoopbackListener listener;

    const auto probe = StageProbeImpl::createInstance(
        std::string( "127.0.0.1" ),
        listener.port(),
        StageProbe::StageMode::Synchronous
        );

    probe -> cancelAfterResolved();

    runProbeToCompletion( probe );

    const auto task = om::qi< Task >( probe );

    UTF_REQUIRE( task -> isFailed() );
    UTF_REQUIRE_EQUAL( exceptionCodeOf( task ), asio::error::operation_aborted );

    UTF_REQUIRE_EQUAL( probe -> countOf( "stageEntered" ), 0U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "continueAfterConnected" ), 0U );
}

#endif /* __UTEST_TESTTCPPREHANDSHAKESTAGE_H_ */
