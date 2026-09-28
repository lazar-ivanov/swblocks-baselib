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

#ifndef __UTEST_TESTHTTPSERVERLOSTCANCEL_H_
#define __UTEST_TESTHTTPSERVERLOSTCANCEL_H_

#include <baselib/httpserver/HttpServer.h>
#include <baselib/httpserver/Response.h>

#include <baselib/http/Globals.h>

#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <utility>

#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * A FORCED CANCEL LOST ON THE HTTP SERVER'S TASKS (D-L3-1, change-set CS-6)
 *
 * WHAT IS UNDER TEST. A forced cancel which lands between two steps of an operation reaps nothing,
 * and requestCancelInternal( ) is idempotent, so a deadline which fires afterwards cannot issue it
 * again (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md). The fix makes the deadlines
 * which survive a cancel issue it again, (a2); two of them are the HTTP server's - the receive task's
 * idle timer and the send task's response timer, each armed before the operation it bounds and
 * disarmed only when the task stops.
 *
 * THE REDS, committed before the fix. Each probe requests a cancel as its task starts - under the
 * task lock, with the task running - and swallows it: a lost cancel by construction, without the real
 * gap. Its timer is 1 s, and its peer never acts until the bound is over:
 *
 *   - HttpServerTimers_TheIdleTimerIssuesALostCancelAgainTests - the receive task reads a request
 *     which never comes. Today the idle timer fires, finds the task cancelled, and does nothing; the
 *     task ends only when the case closes the peer. After (a2) it ends at the timer, as a cancel.
 *   - HttpServerTimers_TheResponseTimerIssuesALostCancelAgainTests - the send task writes a
 *     response far larger than what the two small socket buffers hold, to a peer which never reads.
 *     The same red. A write ends at a forced cancel whether it is registered or between two steps,
 *     since its send side is shut down, so the green is certain too.
 */

namespace utest
{
    namespace httpserverlostcancel
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::chkOrFail;
        using tlsteardown::describeCode;

        typedef bl::tasks::TcpSocketAsyncBase                                   stream_t;

        enum : std::size_t
        {
            /**
             * @brief How long a case gives its task to end after its cancel, before the peer acts -
             * far above a prompt ending, and above the 1 s timers the cases set
             */

            LOST_CANCEL_BOUND_IN_MILLISECONDS   = 5000U,

            /**
             * @brief The socket buffers of the response case: the peer's receive buffer and the
             * server's send buffer, each far smaller than the response
             */

            SMALL_SOCKET_BUFFER_IN_BYTES        = 16U * 1024U,

            RESPONSE_CONTENT_IN_BYTES           = 1024U * 1024U,
        };

        /**
         * @brief A connected loopback pair: the server's end on the I/O thread pool's service, for
         * the task under test, and the peer's end on a service of its own, which the case drives
         * synchronously and which never sends or reads
         */

        class ConnectedPair
        {
            BL_NO_COPY_OR_MOVE( ConnectedPair )

        public:

            explicit ConnectedPair( SAA_in const bool isBufferSmall )
                :
                m_peer( m_ioService )
            {
                using bl::asio::ip::tcp;

                tcp::acceptor acceptor( m_ioService );

                const tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ );

                acceptor.open( endpoint.protocol() );
                acceptor.bind( endpoint );
                acceptor.listen();

                /*
                 * A receive buffer is set before the connect, so that the window it implies is the
                 * one the connection starts with
                 */

                m_peer.open( endpoint.protocol() );

                if( isBufferSmall )
                {
                    m_peer.set_option(
                        bl::asio::socket_base::receive_buffer_size( static_cast< int >( SMALL_SOCKET_BUFFER_IN_BYTES ) )
                        );
                }

                m_peer.connect( acceptor.local_endpoint() );

                m_server = stream_t::stream_ref::attach(
                    new tcp::socket(
                        bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::NonBlocking ) -> aioService()
                        )
                    );

                acceptor.accept( *m_server );

                if( isBufferSmall )
                {
                    m_server -> set_option(
                        bl::asio::socket_base::send_buffer_size( static_cast< int >( SMALL_SOCKET_BUFFER_IN_BYTES ) )
                        );
                }
            }

            auto takeServerStream() -> stream_t::stream_ref
            {
                return std::move( m_server );
            }

            /**
             * @brief Closes the peer's end - an end of stream for a read, and a reset for a write
             * whose data the peer never read
             */

            void closePeer()
            {
                bl::eh::error_code ec;

                m_peer.close( ec );
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::socket                                           m_peer;
            stream_t::stream_ref                                                m_server;
        };

        /**
         * @brief One of the HTTP server's tasks, with a cancel requested as it starts and swallowed,
         * and its stop signalled
         */

        template
        <
            typename TASK
        >
        class SwallowedCancelProbeT : public TASK
        {
            BL_DECLARE_OBJECT_IMPL( SwallowedCancelProbeT )

        public:

            typedef TASK                                                        base_type;

        protected:

            OneShotSignal                                                       m_stop;

            /*
             * Under the task lock: every caller of cancelTask( ) holds it
             */

            bool                                                                m_isFirstCancelSwallowed;

            template
            <
                typename... ARGS
            >
            SwallowedCancelProbeT( SAA_in ARGS&&... args )
                :
                base_type( std::forward< ARGS >( args )... ),
                m_isFirstCancelSwallowed( false )
            {
            }

            virtual void scheduleTask( SAA_in const std::shared_ptr< bl::tasks::ExecutionQueue >& eq ) OVERRIDE
            {
                /*
                 * The base arms the timer and starts the read or the write; then THE LOST CANCEL is
                 * requested - under the task lock, which scheduleNothrow( ) holds, and with the task
                 * running - and cancelTask( ) below swallows it
                 */

                base_type::scheduleTask( eq );

                base_type::requestCancelInternal();
            }

            virtual void cancelTask() OVERRIDE
            {
                if( ! m_isFirstCancelSwallowed )
                {
                    m_isFirstCancelSwallowed = true;

                    return;
                }

                base_type::cancelTask();
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stop.signal();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            bool wasFirstCancelSwallowed() const
            {
                BL_MUTEX_GUARD( base_type::m_lock );

                return m_isFirstCancelSwallowed;
            }
        };

        typedef bl::om::ObjectImpl<
            SwallowedCancelProbeT< bl::httpserver::HttpServerReceiveRequestTask< stream_t > >
            >
            IdleProbeImpl;

        typedef bl::om::ObjectImpl<
            SwallowedCancelProbeT< bl::httpserver::HttpServerSendResponseTask< stream_t > >
            >
            ResponseProbeImpl;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct LostCancelResult
        {
            bool                                                                wasFirstCancelSwallowed;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
            bool                                                                hasStopped;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;

            LostCancelResult()
                :
                wasFirstCancelSwallowed( false ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
                hasStopped( false ),
                isFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "first cancel swallowed " ) +
                    ( wasFirstCancelSwallowed ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", stopped once the peer acted " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) );
            }
        };

        /**
         * @brief Runs a probe to its end: gives it the bound to stop, then closes the peer's end and
         * waits for the task
         */

        template
        <
            typename PROBE
        >
        inline auto runLostCancel(
            SAA_in          const bl::om::ObjPtr< PROBE >&                      probe,
            SAA_inout       ConnectedPair&                                      pair
            )
            -> LostCancelResult
        {
            using namespace bl;
            using namespace bl::tasks;

            LostCancelResult result;

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( LOST_CANCEL_BOUND_IN_MILLISECONDS ) );

                    result.isCanceled = probe -> isCanceled();

                    pair.closePeer();

                    result.hasStopped = probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( task );
                }
                );

            result.wasFirstCancelSwallowed = probe -> wasFirstCancelSwallowed();
            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            return result;
        }

        /**
         * @brief What both reds assert: the timer issued the lost cancel again, so the task ended
         * inside the bound, as a cancel
         */

        inline void chkIssuedAgainByTheTimer(
            SAA_in          const LostCancelResult&                             result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.wasFirstCancelSwallowed,
                which + ": the cancel was never requested and swallowed; " + result.describe()
                );

            chkOrFail(
                result.hasStoppedWithinBound && result.isCanceled,
                which + ": the timer did not issue the lost cancel again; " + result.describe()
                );

            chkOrFail(
                result.isFailed && bl::asio::error::operation_aborted == result.taskCode,
                which + ": the task did not end as a cancel; " + result.describe()
                );
        }

    } // httpserverlostcancel

} // utest

/**
 * @brief D-L3-1's RED - the receive task's idle timer issues a lost cancel again
 */

UTF_AUTO_TEST_CASE( HttpServerTimers_TheIdleTimerIssuesALostCancelAgainTests )
{
    using namespace utest::httpserverlostcancel;

    ConnectedPair pair( false /* isBufferSmall */ );

    const auto probe = IdleProbeImpl::createInstance( pair.takeServerStream(), bl::time::seconds( 1 ) );

    chkIssuedAgainByTheTimer( runLostCancel( probe, pair ), "the idle timer" );
}

/**
 * @brief D-L3-1's RED - the send task's response timer issues a lost cancel again
 */

UTF_AUTO_TEST_CASE( HttpServerTimers_TheResponseTimerIssuesALostCancelAgainTests )
{
    using namespace bl;
    using namespace utest::httpserverlostcancel;

    ConnectedPair pair( true /* isBufferSmall */ );

    auto response = httpserver::Response::createInstance(
        http::Parameters::HTTP_SUCCESS_OK,
        std::string( static_cast< std::size_t >( RESPONSE_CONTENT_IN_BYTES ), 'x' )
        );

    const auto probe = ResponseProbeImpl::createInstance(
        pair.takeServerStream(),
        std::move( response ),
        time::seconds( 1 )
        );

    chkIssuedAgainByTheTimer( runLostCancel( probe, pair ), "the response timer" );
}

#endif /* __UTEST_TESTHTTPSERVERLOSTCANCEL_H_ */
