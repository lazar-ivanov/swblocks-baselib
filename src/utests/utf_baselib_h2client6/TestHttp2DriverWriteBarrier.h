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

#ifndef __UTEST_TESTHTTP2DRIVERWRITEBARRIER_H_
#define __UTEST_TESTHTTP2DRIVERWRITEBARRIER_H_

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * initiateClose( ) must reach a write a cancel cannot - the HTTP/2 half
 *
 * THE DEFECT IS THE SAME ONE THE HTTP/1.1 BARRIER CASE MEASURED, in a driver that reaches it
 * through a different door. asio::async_write( ) is a resumable loop over async_write_some( ), and
 * between two of its steps it has NOTHING registered with the reactor; socket.cancel( ) reaps what
 * is registered, so it finds nothing of that write, and the loop arms its next step afterwards.
 * initiateClose( ) runs exactly once per run, so no second cancel is coming and the task can never
 * take its terminal path - a HANG, because MultiOperationTaskT requires zero pending operations
 *
 * AND THE DOOR HAS TO BE THE PEER'S CLOSE, WHICH IS WHY THIS CASE LOOKS NOTHING LIKE A GRACEFUL
 * ONE. The h2 driver's graceful close CANNOT reach initiateClose( ) with a write outstanding:
 * chkFinishClose( ) is called only from pumpWrites( ), and pumpWrites( ) returns at its first line
 * while m_isWriteInFlight is true. That door is protected by construction, and a case built on it
 * would be green against the unfixed driver and prove nothing
 *
 * What is NOT protected is onRead( ) taking a peer close with a write still going out:
 * onPeerClosed( ) -> cancelTimers( ) -> beginClose( ), and the epilog of that same read handler
 * runs initiateClose( ). Every timer has just been cancelled and this door arms no deadline, so
 * nothing whatever is left to bound the wait. That is the door this case uses
 *
 * HOW THE WRITE IS HELD OPEN, AND WHY IT IS NOT A LARGE NUMBER AND A HOPE. Three levers, none of
 * them timing:
 *
 *   - the peer shrinks its own receive buffer to the minimum the stack will give it, and then
 *     never reads at all;
 *   - the driver's own send buffer is shrunk, from the one seam that runs on the strand just
 *     before each async_write - so the pipe between the two is a few kilobytes end to end;
 *   - the request carries a body far larger than the initial HTTP/2 flow-control window, so the
 *     driver's second write is the whole window's worth of DATA and cannot be absorbed.
 *
 * The opening write - preface, SETTINGS, WINDOW_UPDATE and HEADERS - is a few hundred bytes and
 * goes out through that narrow pipe unaided, which is what lets the driver reach the DATA write
 *
 * AND THE RENDEZVOUS IS THE WRITE ITSELF, NOT A SLEEP. onWriteScheduled( ) is called on the strand
 * immediately before async_write( ) is issued, so the probe signals from there and the case half
 * closes the peer only once the driver has handed the big buffer over. The read completion that
 * then carries the peer's FIN cannot be dispatched until the strand is free, and the strand is
 * inside the handler that is issuing the write - so by the time onRead( ) runs, the composed write
 * is in flight. The ordering is the strand's and not the scheduler's
 */

namespace utest
{
    namespace h2driver
    {
        enum : std::size_t
        {
            /**
             * @brief The request body, far above the initial flow-control window
             *
             * A peer that sends no SETTINGS leaves the window at its RFC 9113 default of 65535,
             * so one produce( ) can emit at most that much DATA however large the body is. The
             * body is bigger than the window on purpose: what it has to guarantee is that the
             * window is FULL, not that the body fits
             */

            BLOCKED_BODY_SIZE                   = 1U * 1024U * 1024U,

            /**
             * @brief What the peer asks for as its receive buffer, and the driver as its send one
             *
             * The stack rounds both up to its own minimum, which is the point - the request is
             * for "as small as you will give me" and no assertion depends on the number
             */

            TINY_SOCKET_BUFFER_SIZE             = 2048U,

            /**
             * @brief The write size that tells the probe a DATA frame is the one going out
             *
             * produce( ) places ONE DATA frame per write - SETTINGS_MAX_FRAME_SIZE is 16384 and
             * bodyBytesWanted( ) never offers more than one frame's worth - so the writes are
             * about 16.4KB each and not one window-sized buffer. Measured: 16467, 16393, 16393.
             * This sits above the control frames and below one DATA frame, so the first write
             * that carries a body reaches it - and with the pipe below shrunk at BOTH ends, that
             * write cannot complete
             */

            BIG_WRITE_THRESHOLD                 = 8U * 1024U,

            /**
             * @brief How long the driver is given to end the exchange WITHOUT outside help
             *
             * The whole exchange takes a few milliseconds when the driver frees its own write.
             * It is deliberately shorter than the session's own 10s SETTINGS acknowledgement
             * deadline, so that a task which ends within it ended because the teardown freed the
             * write and not because some other timer fired
             */

            UNAIDED_END_IN_MILLISECONDS         = 5000U,
        };

        /**
         * @brief Waits, BOUNDED, for a task to reach its terminal path on its own
         *
         * ExecutionQueue::wait( ) is the rendezvous everywhere a case knows the task will end, and
         * it is unbounded. What this is for is the opposite question - whether the task ends at
         * all when nothing outside it helps - and the ABSENCE of an event cannot be waited for,
         * only bounded. It is not a substitute for a rendezvous before an assertion about what was
         * delivered, and it is not used as one here
         */

        inline bool waitForTaskEnd(
            SAA_in          const bl::om::ObjPtr< bl::tasks::Task >&            task,
            SAA_in          const std::size_t                                  timeoutInMilliseconds
            )
        {
            enum : std::size_t
            {
                POLL_INTERVAL_IN_MILLISECONDS = 20U,
            };

            /*
             * ELAPSED TIME, AGAINST A STEADY CLOCK, AND AN ANSWER COUNTS ONLY IF IT WAS SEEN INSIDE
             * THE WINDOW - the same rule, and the same reason, as the HTTP/1.1 copies of this helper
             * in utests/baselib/Http1DriverTestUtils.h and utf_baselib_httpclient5. Counting the
             * interval the loop asked to sleep let the window grow under load, which a caller asking
             * whether a task SURVIVED a bound pays for with a false red; the state is read before
             * the clock, so a completion counts only when the clock read after it still says inside
             */

            const auto deadline =
                bl::os::chrono::steady_clock::now() +
                bl::os::chrono::milliseconds( timeoutInMilliseconds );

            for( ;; )
            {
                const bool isCompleted = bl::tasks::Task::Completed == task -> getState();

                if( bl::os::chrono::steady_clock::now() >= deadline )
                {
                    return false;
                }

                if( isCompleted )
                {
                    return true;
                }

                bl::os::sleep(
                    bl::time::milliseconds(
                        static_cast< long >( POLL_INTERVAL_IN_MILLISECONDS )
                        )
                    );
            }
        }

        /**
         * @brief The sizes of the writes the driver handed over, so a failure says what it saw
         */

        inline auto describeWrites( SAA_in const std::vector< std::string >& writes ) -> std::string
        {
            std::string result;

            for( std::size_t i = 0U; i < writes.size(); ++i )
            {
                if( ! result.empty() )
                {
                    result += ", ";
                }

                result += bl::utils::lexical_cast< std::string >( writes[ i ].size() );
            }

            return result.empty() ? std::string( "none" ) : result;
        }

        /**
         * @brief A loopback peer which accepts, never reads, and half closes when it is told to
         *
         * IT IS NOT AN HTTP/2 PEER AND MUST NOT BECOME ONE. Everything this case needs is below
         * the protocol: a socket which will not take what the driver is sending, and a FIN that
         * arrives while it will not. Http2TestServerT reads its socket continuously - which is
         * correct of a peer and fatal to this case, because a peer which reads is a second waker
         * for the very write under test
         *
         * HALF CLOSE AND NOT CLOSE, and that is the whole instrument. close( ) with an unread
         * receive queue puts a RST on the wire (RFC 2525 section 2.17, and Linux does this too),
         * and a RST completes the blocked write with broken_pipe - which would free it without the
         * driver's teardown having anything to do with it, and the case would assert on whichever
         * waker got there first. shutdown( shutdown_send ) puts FIN out and leaves the receive
         * queue exactly as it is, so the driver's own initiateClose( ) stays the only thing that
         * can free the write
         */

        class HalfClosingPeer
        {
            BL_NO_COPY_OR_MOVE( HalfClosingPeer )

        public:

            HalfClosingPeer()
                :
                m_acceptor(
                    m_ioService,
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    ),
                m_port( m_acceptor.local_endpoint().port() ),
                m_halfClose( false ),
                m_isHalfClosed( false ),
                m_released( false )
            {
                /*
                 * SET ON THE ACCEPTOR, AND NOT ONLY ON THE ACCEPTED SOCKET. An accepted socket
                 * inherits the listening socket's buffer sizes, and the receive window is
                 * advertised during the handshake - so a shrink applied after accept( ) clamps the
                 * buffer but arrives after the peer has already been told it may send more.
                 * Measured: with the post-accept set alone the driver got three 16.4KB frames away
                 * before it blocked; with this one it blocks on the first
                 */

                {
                    bl::eh::error_code ec;

                    m_acceptor.set_option(
                        bl::asio::socket_base::receive_buffer_size(
                            static_cast< int >( TINY_SOCKET_BUFFER_SIZE )
                            ),
                        ec
                        );
                }

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &HalfClosingPeer::run, this ) ) );
            }

            ~HalfClosingPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                requestHalfClose();

                release();

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept( ), so one throwaway connection does it - harmless when the peer has
                     * already accepted the driver's
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket probe( ioService );

                    probe.connect(
                        bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                        ec
                        );

                    probe.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            auto port() const NOEXCEPT -> bl::os::port_t
            {
                return m_port;
            }

            auto failure() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_failure;
            }

            /**
             * @brief Puts FIN on the wire without taking anything the driver has queued
             */

            void requestHalfClose()
            {
                BL_MUTEX_GUARD( m_lock );

                m_halfClose = true;

                m_cv.notify_all();
            }

            bool waitForHalfClosed(
                SAA_in          const std::size_t                               timeoutInMilliseconds
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isHalfClosed;
                    }
                    );
            }

            /**
             * @brief Lets the peer let go of its end, once the case has taken its answer
             */

            void release()
            {
                BL_MUTEX_GUARD( m_lock );

                m_released = true;

                m_cv.notify_all();
            }

        private:

            void run()
            {
                bl::asio::ip::tcp::socket socket( m_ioService );

                try
                {
                    bl::eh::error_code ec;

                    m_acceptor.accept( socket, ec );

                    if( ec )
                    {
                        BL_MUTEX_GUARD( m_lock );

                        m_failure = "the peer could not accept: " + ec.message();

                        m_cv.notify_all();

                        return;
                    }

                    /*
                     * AGAIN, ON THE ACCEPTED SOCKET - the acceptor's value is inherited on the
                     * platforms that inherit it, and this covers the ones that do not. Nothing is
                     * ever read from this socket either way
                     */

                    socket.set_option(
                        bl::asio::socket_base::receive_buffer_size(
                            static_cast< int >( TINY_SOCKET_BUFFER_SIZE )
                            ),
                        ec
                        );

                    waitFor(
                        [ this ]() -> bool
                        {
                            return m_halfClose;
                        }
                        );

                    socket.shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );

                    {
                        BL_MUTEX_GUARD( m_lock );

                        if( ec )
                        {
                            m_failure = "the peer could not half close: " + ec.message();
                        }

                        m_isHalfClosed = true;

                        m_cv.notify_all();
                    }

                    waitFor(
                        [ this ]() -> bool
                        {
                            return m_released;
                        }
                        );
                }
                catch( std::exception& e )
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_failure = e.what();

                    m_cv.notify_all();
                }

                {
                    bl::eh::error_code ec;

                    socket.close( ec );
                }
            }

            template
            <
                typename PREDICATE
            >
            void waitFor( SAA_in const PREDICATE& predicate )
            {
                enum : std::size_t
                {
                    PEER_WAIT_TIMEOUT_IN_MILLISECONDS = 30000U,
                };

                bl::os::mutex_unique_lock guard( m_lock );

                ( void ) m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds(
                        static_cast< std::size_t >( PEER_WAIT_TIMEOUT_IN_MILLISECONDS )
                        ),
                    predicate
                    );
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            const bl::os::port_t                                                m_port;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::string                                                         m_failure;
            bool                                                                m_halfClose;
            bool                                                                m_isHalfClosed;
            bool                                                                m_released;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /**
         * @brief The driver, with its send buffer shrunk and its big write announced
         *
         * BOTH THINGS HAPPEN IN onWriteScheduled( ), which the driver calls on the strand
         * immediately before each async_write( ) - so the socket option is set before any write
         * has been issued, and the announcement is made while the strand still holds the handler
         * that is about to issue the one the case is waiting for
         */

        template
        <
            typename E = void
        >
        class BlockedWriteProbeT :
            public DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
        {
            BL_DECLARE_OBJECT_IMPL( BlockedWriteProbeT )

        public:

            typedef BlockedWriteProbeT< E >                                     this_type;
            typedef DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >       base_type;

        protected:

            mutable bl::os::mutex                                               m_probeLock;
            mutable bl::os::condition_variable                                  m_probeCv;

            bool                                                                m_isBufferShrunk;
            bool                                                                m_isBigWriteScheduled;
            std::size_t                                                         m_biggestWrite;

            BlockedWriteProbeT(
                SAA_in          ConnectionKey                                   key,
                SAA_in          typename base_type::factory_ptr_t               driverFactory,
                SAA_in          Http2ConnectionConfig                           h2config,
                SAA_in          ClientConnectionConfig                          config
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( h2config ),
                    BL_PARAM_FWD( config )
                    ),
                m_isBufferShrunk( false ),
                m_isBigWriteScheduled( false ),
                m_biggestWrite( 0U )
            {
            }

            virtual void onWriteScheduled(
                SAA_in          const bl::http2::Session::wire_buffer_t&        buffer
                ) OVERRIDE
            {
                base_type::onWriteScheduled( buffer );

                if( ! m_isBufferShrunk )
                {
                    m_isBufferShrunk = true;

                    bl::eh::error_code ec;

                    base_type::getSocket().set_option(
                        bl::asio::socket_base::send_buffer_size(
                            static_cast< int >( TINY_SOCKET_BUFFER_SIZE )
                            ),
                        ec
                        );
                }

                {
                    BL_MUTEX_GUARD( m_probeLock );

                    if( buffer.size() > m_biggestWrite )
                    {
                        m_biggestWrite = buffer.size();
                    }

                    if( buffer.size() >= static_cast< std::size_t >( BIG_WRITE_THRESHOLD ) )
                    {
                        m_isBigWriteScheduled = true;

                        m_probeCv.notify_all();
                    }
                }
            }

        public:

            bool waitForBigWrite( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                bl::os::mutex_unique_lock guard( m_probeLock );

                return m_probeCv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isBigWriteScheduled;
                    }
                    );
            }

            auto biggestWrite() const NOEXCEPT -> std::size_t
            {
                BL_MUTEX_GUARD( m_probeLock );

                return m_biggestWrite;
            }
        };

        typedef bl::om::ObjectImpl< BlockedWriteProbeT<> > BlockedWriteProbe;

    } // h2driver

} // utest

/**
 * @brief A peer that half closes while our upload is stuck must not leave the task hanging
 *
 * THE TWO ASSERTIONS ARE ONE PER HALF OF THE FIX, and neither is redundant:
 *
 *   - the task ends WITHOUT outside help. That is initiateClose( ) shutting the send side down
 *     where its cancel cannot reach the composed write. Red before it, because nothing else was
 *     ever coming - the timers were cancelled by that same call;
 *   - and it ends CLEAN. That is onWrite( ) not counting the error our own shutdown produced as
 *     this task's. Red with the shutdown alone, because a locally shut send side reports
 *     broken_pipe here and only operation_aborted is excused by the accounting - so the fix
 *     without it would turn a hang into a FAILED connection, which is not a fix.
 *
 * The order matters: the second is read after the peer has been released, and a task which never
 * ended would fail the first and then report the release's own RST in the second. Asserting the
 * deadlock first is what keeps the two defects apart
 */

UTF_AUTO_TEST_CASE( H2Driver_PeerHalfClosesWithAWriteInFlightTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    HalfClosingPeer peer;

    const auto record = std::make_shared< FallbackRecord >();

    const auto driver = BlockedWriteProbe::createInstance(
        makeKey( "http", "127.0.0.1", peer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        Http2ConnectionConfig(),
        cleartextHttp2Config()
        );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );
    const auto sink = RecordingSink::createInstance();

    const auto driverTask = om::qi< Task >( driver );

    bool taskEndedUnaided = false;
    bool taskFailed = false;
    std::string taskFailure;
    std::string writeSizes;
    std::size_t biggestWrite = 0U;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( driverTask );

            auto request = makeRequest( "http://127.0.0.1/blocked", "POST" );

            const auto block =
                data::DataBlock::createInstance(
                    static_cast< std::size_t >( BLOCKED_BODY_SIZE )
                    );

            block -> setSize( static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

            request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );

            const auto handle = connection -> submit(
                request,
                om::qi< httpclient::ClientStreamEventSink >( sink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

            /*
             * THE RENDEZVOUS, and the whole reason this case is not timing. The driver has handed
             * a window's worth of DATA to async_write( ) and the peer will not take it, so from
             * here the write can only be freed by the driver's own teardown
             */

            const auto unaidedBound = static_cast< std::size_t >( UNAIDED_END_IN_MILLISECONDS );

            const bool sawBigWrite = driver -> waitForBigWrite( unaidedBound );

            biggestWrite = driver -> biggestWrite();

            writeSizes = describeWrites( driver -> writes() );

            if( ! sawBigWrite )
            {
                UTF_FAIL(
                    "the driver never handed a window's worth of DATA to async_write( ) - "
                    "writes so far: " + writeSizes
                    );
            }

            peer.requestHalfClose();

            UTF_REQUIRE( peer.waitForHalfClosed( unaidedBound ) );

            /*
             * THE PEER IS STILL HOLDING ITS END HERE, AND MUST BE. FIN is out, so our read handler
             * takes onPeerClosed( ) -> beginClose( ) and the epilog runs initiateClose( ) with the
             * write still outstanding; the peer's receive queue is untouched, so nothing on its
             * side can free that write
             */

            taskEndedUnaided = waitForTaskEnd( driverTask, unaidedBound );

            /*
             * RELEASED ONLY NOW, AND ONLY SO THE HARNESS CAN ALWAYS FINISH. If the bound expired
             * there is a write nothing woke, and the peer's close is then what ends it - with the
             * broken_pipe of its RST rather than with the driver's own teardown. That is the same
             * deadlock reported a second way, which is why the assertions are ordered as they are
             */

            peer.release();

            eq -> wait( driverTask );

            taskFailed = driverTask -> isFailed();
            taskFailure = exceptionMessageOf( driverTask );

            /*
             * DISCARDED HERE rather than left to the outer flush, which turns a failed task into
             * an exception out of the harness. Whether the task ended clean is an ASSERTION below,
             * so it has to be read and reported rather than thrown
             */

            eq -> forceFlushNoThrow();
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The precondition the two assertions rest on, asserted rather than assumed: the write the
     * driver was blocked on really was a window's worth of DATA and not a handful of control
     * frames. If the flow-control defaults or the framing ever change under this case, THIS is
     * what fails, and it fails saying so
     */

    if( biggestWrite < static_cast< std::size_t >( BIG_WRITE_THRESHOLD ) )
    {
        UTF_FAIL( "the blocked write was not a window's worth of DATA - writes: " + writeSizes );
    }

    if( ! taskEndedUnaided )
    {
        UTF_FAIL(
            "the peer's half close left a write nothing woke - the task had not ended with the "
            "peer still holding its end"
            );
    }

    if( taskFailed )
    {
        UTF_FAIL( "the driver task did not end clean: " + taskFailure );
    }
}

#endif /* __UTEST_TESTHTTP2DRIVERWRITEBARRIER_H_ */
