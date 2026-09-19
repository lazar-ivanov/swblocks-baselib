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

#ifndef __UTEST_TESTTCPSTRANDEDSTREAMS_H_
#define __UTEST_TESTTCPSTRANDEDSTREAMS_H_

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/MultiOperationTask.h>
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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The stranded stream policies of design 3.1 (D13)
 *
 * A socket constructed on a strand makes that strand the default executor of every handler of
 * every operation on it, so a task with a read, a write and a timer outstanding together has all
 * of their handlers serialized without a single asio::bind_executor at any call site. These cases
 * are what says so of the delivered policies rather than of Asio in the abstract
 *
 * Two assertions carry the weight, and they are complementary:
 *
 *   - every handler asserts getStrand().running_in_this_thread(). A strand runs no two of its
 *     handlers at once, so a run in which every handler reports true is a run in which no two of
 *     them overlapped. This one is exact and it fails in an ORDINARY build, which is what makes
 *     the negative control cheap
 *
 *   - the counters the handlers keep are PLAIN members and no lock of any kind is taken around
 *     them - the handlers deliberately use BL_TASKS_HANDLER_BEGIN_NOLOCK(), so the strand is the
 *     only thing serializing them. That is what makes a ThreadSanitizer run on this module say
 *     something about the strand rather than about the task lock, and it is why the totals are
 *     cross-checked at the end: a lost update is what a race actually produces
 *
 * The probe and the case bodies are templates on the stream policy, because S3.3 is "S3.2 but
 * over TLS" and runs the very same cases over TcpSslSocketAsyncStrandedBase from its own header
 * in this directory
 *
 * The peer is an echo server on an ephemeral loopback port, driven from the test thread while the
 * task runs on the I/O thread pool (4 threads, ThreadPool.h:111), so the concurrency is real
 */

namespace utest
{
    namespace strandedstreams
    {
        /**
         * @brief The loopback machinery both peers share - the acceptor, the deadline and the
         * echo loop
         *
         * S3.3's TLS peer derives from this and differs only in what it accepts onto and in the
         * handshake it performs afterwards; the echo loop itself is the same for a plain socket
         * and for an SSL stream, both of which are AsyncReadStream and AsyncWriteStream
         *
         * Binding port zero is what keeps these cases free of the machine global test lock, and
         * every operation is deadline bounded so a client which never arrives fails the case on
         * its own deadline rather than hanging it
         */

        class LoopbackPeerBase
        {
            BL_NO_COPY_OR_MOVE( LoopbackPeerBase )

        public:

            enum : long
            {
                DEFAULT_TIMEOUT_IN_SECONDS = 30L,
            };

            enum : std::size_t
            {
                ECHO_CHUNK_SIZE = 16U * 1024U,
            };

            LoopbackPeerBase()
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

        protected:

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
            void armDeadline(
                SAA_inout       CANCELABLE&                                     cancelable,
                SAA_in          const bl::time::time_duration&                  timeout =
                                    bl::time::seconds( DEFAULT_TIMEOUT_IN_SECONDS )
                )
            {
                m_timer.expires_from_now( timeout );

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

            void acceptOne( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
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
            }

            /**
             * @brief Whether the client has shut its end of the connection down, as seen from
             * here, within the given deadline
             *
             * This is the observable the cancel case turns on, and it is read off the LOWEST
             * layer on purpose: what the stranded cancelTask() does is shut the socket down, and
             * an orderly socket shutdown is an end of stream on the raw connection whether or not
             * there is a TLS session above it. Nothing is ever sent on these connections, so the
             * read can only end in eof or in the deadline cancelling it
             */

            template
            <
                typename SOCKET
            >
            bool readsEofWithin(
                SAA_inout       SOCKET&                                         socket,
                SAA_in          const bl::time::time_duration&                  timeout
                )
            {
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

                armDeadline( socket, timeout );

                runService();

                UTF_REQUIRE( readCompleted );

                return bl::asio::error::eof == readEc;
            }

            /**
             * @brief Reads and echoes back exactly totalBytes, a chunk at a time
             *
             * The peer reads and then writes, one operation at a time, while the client has both
             * outstanding together - which is the shape that keeps the two from deadlocking on a
             * full socket buffer: the client drains what the peer writes while the peer is still
             * reading what the client writes
             */

            template
            <
                typename STREAM
            >
            void echoExactly(
                SAA_inout       STREAM&                                         stream,
                SAA_in          const std::size_t                               totalBytes
                )
            {
                std::vector< char > chunk( ECHO_CHUNK_SIZE );

                std::size_t echoed = 0U;

                while( echoed < totalBytes )
                {
                    const auto toRead = std::min< std::size_t >( chunk.size(), totalBytes - echoed );

                    bl::eh::error_code readEc;
                    std::size_t bytesRead = 0U;
                    bool readCompleted = false;

                    stream.async_read_some(
                        bl::asio::buffer( chunk.data(), toRead ),
                        [ this, &readEc, &bytesRead, &readCompleted ](
                            SAA_in      const bl::eh::error_code&               ec,
                            SAA_in      const std::size_t                       transferred
                            ) -> void
                        {
                            readEc = ec;
                            bytesRead = transferred;
                            readCompleted = true;

                            m_timer.cancel();
                        }
                        );

                    armDeadline( stream.lowest_layer() );

                    runService();

                    UTF_REQUIRE( readCompleted );
                    UTF_REQUIRE_EQUAL( bl::eh::error_code(), readEc );
                    UTF_REQUIRE( 0U != bytesRead );

                    bl::eh::error_code writeEc;
                    bool writeCompleted = false;

                    bl::asio::async_write(
                        stream,
                        bl::asio::buffer( chunk.data(), bytesRead ),
                        [ this, &writeEc, &writeCompleted ](
                            SAA_in      const bl::eh::error_code&               ec,
                            SAA_in      const std::size_t                       transferred
                            ) -> void
                        {
                            BL_UNUSED( transferred );

                            writeEc = ec;
                            writeCompleted = true;

                            m_timer.cancel();
                        }
                        );

                    armDeadline( stream.lowest_layer() );

                    runService();

                    UTF_REQUIRE( writeCompleted );
                    UTF_REQUIRE_EQUAL( bl::eh::error_code(), writeEc );

                    echoed += bytesRead;
                }
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::deadline_timer                                            m_timer;
        };

        /**
         * @brief The cleartext echo peer
         */

        class EchoLoopbackPeer :
            public LoopbackPeerBase
        {
            BL_NO_COPY_OR_MOVE( EchoLoopbackPeer )

        public:

            EchoLoopbackPeer()
                :
                m_socket( m_ioService )
            {
            }

            /**
             * @brief Accepts one connection and holds it open without reading or writing anything
             */

            void acceptAndIdle()
            {
                acceptOne( m_socket );
            }

            /**
             * @brief Whether the client has shut the connection down, within the given deadline
             */

            bool sawClientShutdownWithin( SAA_in const bl::time::time_duration& timeout )
            {
                return readsEofWithin( m_socket, timeout );
            }

            /**
             * @brief Accepts one connection and echoes exactly totalBytes back
             */

            void runEchoSession( SAA_in const std::size_t totalBytes )
            {
                acceptOne( m_socket );

                echoExactly( m_socket, totalBytes );
            }

        private:

            bl::asio::ip::tcp::socket                                           m_socket;
        };

        /**
         * @brief A connection task which runs a read, a write and a timer at the same time over a
         * stranded stream policy
         *
         * It is the multi-operation mix-in of design 3.2 over the connection establisher, which is
         * the composition S4.1 delivers for real; here it is the smallest thing which can have
         * three operations outstanding at once and still have one terminal path
         *
         * The handlers take NO task lock - BL_TASKS_HANDLER_BEGIN_NOLOCK() rather than
         * BL_TASKS_HANDLER_BEGIN(). That is the point of the probe rather than a shortcut: with
         * the task lock held the handlers would be serialized whether the socket is on a strand or
         * not, and neither the on-strand assertion nor a ThreadSanitizer run would be saying
         * anything about D13
         */

        template
        <
            typename STREAM
        >
        class StrandedFullDuplexProbeT :
            public bl::tasks::MultiOperationTaskT< bl::tasks::TcpConnectionEstablisherConnector< STREAM > >
        {
            BL_DECLARE_OBJECT_IMPL( StrandedFullDuplexProbeT )

        public:

            typedef StrandedFullDuplexProbeT< STREAM >                          this_type;

            typedef bl::tasks::MultiOperationTaskT
                <
                    bl::tasks::TcpConnectionEstablisherConnector< STREAM >
                >
                base_type;

            enum Mode : std::size_t
            {
                /**
                 * @brief Write the payload, read it back echoed, and tick a timer throughout
                 */

                ModeFullDuplex = 0U,

                /**
                 * @brief One read which the peer never completes - the cancel case
                 */

                ModeReadAndPark = 1U,
            };

            enum : std::size_t
            {
                TRANSFER_SIZE = 128U * 1024U,
                PARK_BUFFER_SIZE = 64U,

                /*
                 * The run does not end until the timer has ticked this many times, so the timer is
                 * concurrent with the transfer rather than a race against how fast loopback is
                 */

                MIN_TIMER_TICKS = 3U,

                /*
                 * 30 seconds at BLOCKER_POLL_IN_MILLISECONDS - the blocker's own deadline, so a
                 * failing assertion between blockStrand() and releaseStrand() cannot hang the run
                 */

                BLOCKER_MAX_POLLS = 6000U,
            };

            enum : long
            {
                TIMER_INTERVAL_IN_MILLISECONDS = 2L,
                BLOCKER_POLL_IN_MILLISECONDS = 5L,
            };

        protected:

            const Mode                                                          m_mode;

            bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >                  m_timer;

            std::vector< char >                                                 m_writeBuffer;
            std::vector< char >                                                 m_readBuffer;

            /*
             * The ThreadSanitizer probe - plain members, touched from the read, the write and the
             * timer handler with no lock of any kind held. Nothing but the strand serializes them
             */

            std::size_t                                                         m_unguardedHandlerCalls;
            std::size_t                                                         m_unguardedWriteCalls;
            std::size_t                                                         m_unguardedReadCalls;
            std::size_t                                                         m_unguardedTimerTicks;
            std::size_t                                                         m_unguardedBytesWritten;
            std::size_t                                                         m_unguardedBytesRead;

            std::size_t                                                         m_writeOffset;
            std::size_t                                                         m_readOffset;
            bool                                                                m_readDone;

            /*
             * What the TEST thread reads while the task is still running is atomic on purpose -
             * those reads are genuinely concurrent with the handlers and are not what is being
             * measured here
             */

            std::atomic< bool >                                                 m_offStrandSeen;
            std::atomic< bool >                                                 m_connected;
            std::atomic< std::size_t >                                          m_readHandlerCalls;
            std::atomic< bool >                                                 m_blockerRunning;
            std::atomic< bool >                                                 m_blockerReleased;

            StrandedFullDuplexProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const Mode                              mode
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ ),
                m_mode( mode ),
                m_unguardedHandlerCalls( 0U ),
                m_unguardedWriteCalls( 0U ),
                m_unguardedReadCalls( 0U ),
                m_unguardedTimerTicks( 0U ),
                m_unguardedBytesWritten( 0U ),
                m_unguardedBytesRead( 0U ),
                m_writeOffset( 0U ),
                m_readOffset( 0U ),
                m_readDone( false ),
                m_offStrandSeen( false ),
                m_connected( false ),
                m_readHandlerCalls( 0U ),
                m_blockerRunning( false ),
                m_blockerReleased( false )
            {
                /*
                 * isCloseStreamOnTaskFinish is deliberately left at its default of false: for the
                 * TLS policy it would schedule the TLS shutdown continuation after the task is
                 * done, and by then nothing is driving the peer's io_service any more, so the
                 * close_notify exchange would only end on the 60 second protocol deadline
                 */

                if( ModeFullDuplex == m_mode )
                {
                    m_writeBuffer.resize( TRANSFER_SIZE );
                    m_readBuffer.resize( TRANSFER_SIZE );

                    for( std::size_t i = 0U; i < m_writeBuffer.size(); ++i )
                    {
                        m_writeBuffer[ i ] = static_cast< char >( ( i * 7U + 11U ) % 251U );
                    }
                }
                else
                {
                    m_readBuffer.resize( PARK_BUFFER_SIZE );
                }
            }

            /**
             * @brief Records a handler which did NOT run on the socket's strand
             *
             * A strand executes no two of its handlers at the same time, so "every handler ran on
             * the strand" is also "no two handlers overlapped"
             */

            void chkOnStrand() NOEXCEPT
            {
                if( ! base_type::getStrand().running_in_this_thread() )
                {
                    m_offStrandSeen = true;
                }
            }

            void armTimer()
            {
                m_timer -> expires_from_now(
                    bl::time::milliseconds( TIMER_INTERVAL_IN_MILLISECONDS )
                    );

                base_type::beginOperation();

                m_timer -> async_wait(
                    bl::cpp::bind(
                        &this_type::onTimer,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error
                        )
                    );
            }

            void beginWrite()
            {
                base_type::beginOperation();

                base_type::getStream().async_write_some(
                    bl::asio::buffer(
                        m_writeBuffer.data() + m_writeOffset,
                        m_writeBuffer.size() - m_writeOffset
                        ),
                    bl::cpp::bind(
                        &this_type::onWritten,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error,
                        bl::asio::placeholders::bytes_transferred
                        )
                    );
            }

            void beginRead()
            {
                base_type::beginOperation();

                base_type::getStream().async_read_some(
                    bl::asio::buffer(
                        m_readBuffer.data() + m_readOffset,
                        m_readBuffer.size() - m_readOffset
                        ),
                    bl::cpp::bind(
                        &this_type::onRead,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        bl::asio::placeholders::error,
                        bl::asio::placeholders::bytes_transferred
                        )
                    );
            }

            /**
             * @brief The deliberate end of the run - everything echoed back and the timer has
             * ticked enough times to have been genuinely concurrent with it
             *
             * It is called ONLY from the timer handler. That WAS load bearing and is now merely
             * sufficient, and the history is worth keeping because it is how the defect was found.
             *
             * When this probe was written, MultiOperationTaskT recorded the FIRST error, and the
             * operation_aborted of an operation which initiateClose() cancelled was an error like
             * any other unless some genuine error preceded it. A deliberate beginClose() from the
             * read handler, with the timer still armed, therefore cancelled that timer and
             * completed the task with operation_aborted - it failed on its own clean close. Taken
             * from the timer handler instead, the timer is the operation completing, nothing else
             * is in flight, and initiateClose() has nothing to cancel. That is why it is here.
             *
             * It was reported as a finding against S0.1 rather than worked around silently, and
             * the mix-in was then fixed as its own gated change-set: beginClose() marks the run
             * deliberately closing and onOperationCompleted() no longer records an
             * operation_aborted arriving under that mark
             * (notes/plans/issues/multioperation-deliberate-close-fails-task-record.md). So a
             * deliberate close from the read handler would no longer fail the task, and this
             * restriction is no longer required for correctness.
             *
             * It is kept because the timer handler is still the point at which this probe knows
             * both halves are done, not because the alternative is broken. Do not cite the old
             * reason for it.
             */

            void chkToClose() NOEXCEPT
            {
                if( m_readDone && m_unguardedTimerTicks >= MIN_TIMER_TICKS )
                {
                    base_type::beginClose();
                }
            }

            void onTimer( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_NOLOCK()

                chkOnStrand();

                BL_TASKS_HANDLER_CHK_EC( ec );
                BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                ++m_unguardedHandlerCalls;
                ++m_unguardedTimerTicks;

                chkToClose();

                if( ! base_type::isClosing() )
                {
                    armTimer();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            void onWritten(
                SAA_in                  const bl::eh::error_code&               ec,
                SAA_in                  const std::size_t                       transferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_NOLOCK()

                chkOnStrand();

                BL_TASKS_HANDLER_CHK_EC( ec );
                BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                ++m_unguardedHandlerCalls;
                ++m_unguardedWriteCalls;

                m_unguardedBytesWritten += transferred;
                m_writeOffset += transferred;

                if( m_writeOffset < m_writeBuffer.size() && ! base_type::isClosing() )
                {
                    beginWrite();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            void onRead(
                SAA_in                  const bl::eh::error_code&               ec,
                SAA_in                  const std::size_t                       transferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_NOLOCK()

                /*
                 * Counted and checked BEFORE the error checks below, because the cancel case needs
                 * to know that the aborted read handler ran at all and on which executor it ran
                 */

                m_readHandlerCalls.fetch_add( 1U );

                chkOnStrand();

                BL_TASKS_HANDLER_CHK_EC( ec );
                BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                ++m_unguardedHandlerCalls;
                ++m_unguardedReadCalls;

                m_unguardedBytesRead += transferred;
                m_readOffset += transferred;

                /*
                 * The isClosing() guard cannot go stale here, and that is a property of the
                 * strand rather than luck: the epilog which sets the closing state runs on the
                 * strand too, and the cancel initiateClose() posts runs on it as well, so a read
                 * armed here is always either cancelled by that post or never armed at all
                 */

                if( m_readOffset < m_readBuffer.size() && ! base_type::isClosing() )
                {
                    beginRead();
                }
                else if( m_readOffset == m_readBuffer.size() )
                {
                    /*
                     * The close is NOT taken here - see chkToClose() for why the timer handler is
                     * the only place it may be taken from
                     */

                    m_readDone = true;
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /**
             * @brief The blocker of the cancel case - it occupies the strand until the test
             * releases it
             *
             * Blocking an I/O thread is exactly what a task must never do, which is why this
             * exists only in a probe: while it sits here the strand runs nothing else, so anything
             * cancelTask() POSTS to the strand cannot have happened yet, and anything it did
             * synchronously would already be visible
             *
             * It also lets go on its OWN deadline and not only when the test releases it. A
             * UTF_REQUIRE between blockStrand() and releaseStrand() is fatal to the case and
             * skips the release, and a blocker which only ever waited for that release would
             * then hold the strand - and the task, and the queue's flush - for good. A failing
             * assertion has to fail, not hang
             */

            void strandBlocker() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_blockerRunning = true;

                for(
                    std::size_t i = 0U;
                    i < BLOCKER_MAX_POLLS && ! m_blockerReleased;
                    ++i
                    )
                {
                    bl::os::sleep( bl::time::milliseconds( BLOCKER_POLL_IN_MILLISECONDS ) );
                }

                BL_NOEXCEPT_END()
            }

            void cancelOperationsOnStrand() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_timer )
                {
                    bl::eh::error_code ec;

                    m_timer -> cancel( ec );
                }

                if( base_type::isChannelOpen() )
                {
                    bl::eh::error_code ec;

                    base_type::getSocket().cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                /*
                 * For the plain policy this runs inside the connect handler and for the TLS one
                 * inside the handshake handler; both are operations on a stream built on the
                 * strand, so both must already be running on it
                 */

                chkOnStrand();

                if( ModeReadAndPark == m_mode )
                {
                    beginRead();
                }
                else
                {
                    m_timer = base_type::createTimer();

                    armTimer();
                    beginWrite();
                    beginRead();
                }

                m_connected = true;

                /*
                 * The task has started asynchronous operations of its own and is not ready to
                 * finish - it completes through the mix-in's single terminal path
                 */

                return true;
            }

            /**
             * @brief Cancels what is still in flight, ON the strand
             *
             * It is posted rather than done inline: the mix-in calls this from the epilog of
             * whichever handler completed last, and a cancel which reached the socket from any
             * other thread would be the very race D13 exists to remove
             */

            virtual void initiateClose() OVERRIDE
            {
                base_type::postToStrand(
                    bl::cpp::bind(
                        &this_type::cancelOperationsOnStrand,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this )
                        )
                    );
            }

        public:

            bool isConnected() const NOEXCEPT
            {
                return m_connected;
            }

            bool offStrandSeen() const NOEXCEPT
            {
                return m_offStrandSeen;
            }

            std::size_t readHandlerCalls() const NOEXCEPT
            {
                return m_readHandlerCalls;
            }

            /*
             * The accessors below read the unguarded members and are only valid once the task has
             * completed, which is what makes them safe to call from the test thread
             */

            std::size_t unguardedHandlerCalls() const NOEXCEPT
            {
                return m_unguardedHandlerCalls;
            }

            std::size_t unguardedWriteCalls() const NOEXCEPT
            {
                return m_unguardedWriteCalls;
            }

            std::size_t unguardedReadCalls() const NOEXCEPT
            {
                return m_unguardedReadCalls;
            }

            std::size_t unguardedTimerTicks() const NOEXCEPT
            {
                return m_unguardedTimerTicks;
            }

            std::size_t unguardedBytesWritten() const NOEXCEPT
            {
                return m_unguardedBytesWritten;
            }

            std::size_t unguardedBytesRead() const NOEXCEPT
            {
                return m_unguardedBytesRead;
            }

            bool echoMatches() const
            {
                return m_readBuffer == m_writeBuffer;
            }

            void blockStrand()
            {
                base_type::postToStrand(
                    bl::cpp::bind(
                        &this_type::strandBlocker,
                        bl::om::ObjPtrCopyable< this_type >::acquireRef( this )
                        )
                    );
            }

            bool isBlockerRunning() const NOEXCEPT
            {
                return m_blockerRunning;
            }

            void releaseStrand() NOEXCEPT
            {
                m_blockerReleased = true;
            }
        };

        /**
         * @brief Fails the case with the message the task failed with, rather than with a bare
         * "isFailed() has failed" which says nothing about why
         */

        inline void chkTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;

            if( ! task -> isFailed() )
            {
                UTF_REQUIRE( ! task -> exception() );

                return;
            }

            std::string message( "<no exception>" );

            if( task -> exception() )
            {
                try
                {
                    cpp::safeRethrowException( task -> exception() );
                }
                catch( std::exception& e )
                {
                    message = e.what();
                }
            }

            UTF_FAIL( "the probe task failed: " + message );
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
                UTF_FAIL( "The task exception is not an eh::system_error" );
            }

            return eh::error_code();
        }

        /**
         * @brief Waits, bounded, for a predicate the task sets from an I/O thread
         */

        template
        <
            typename PREDICATE
        >
        inline bool waitFor( SAA_in const PREDICATE& predicate )
        {
            for( std::size_t i = 0U; i < 1000U && ! predicate(); ++i )
            {
                bl::os::sleep( bl::time::milliseconds( 10 ) );
            }

            return predicate();
        }

        /**
         * @brief The full-duplex case body, shared by the plain and the TLS policy
         */

        template
        <
            typename PROBE,
            typename PEER
        >
        inline void runFullDuplexCase(
            SAA_inout       PEER&                                               peer,
            SAA_in          const std::string&                                  host
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            typedef om::ObjectImpl< PROBE > probe_impl_t;

            const auto probe = probe_impl_t::createInstance(
                std::string( host ),
                peer.port(),
                PROBE::ModeFullDuplex
                );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ &peer, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    peer.runEchoSession( PROBE::TRANSFER_SIZE );

                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );
                }
                );

            chkTaskSucceeded( task );

            UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );

            /*
             * The one assertion which fails the moment the socket stops being built on the strand
             */

            UTF_REQUIRE( ! probe -> offStrandSeen() );

            UTF_REQUIRE_EQUAL( probe -> unguardedBytesWritten(), std::size_t( PROBE::TRANSFER_SIZE ) );
            UTF_REQUIRE_EQUAL( probe -> unguardedBytesRead(), std::size_t( PROBE::TRANSFER_SIZE ) );

            UTF_REQUIRE( probe -> echoMatches() );

            /*
             * All three operation families really did run, and the total is consistent with the
             * three counters which fed it - a lost update is what a race on them produces
             */

            UTF_REQUIRE( 0U != probe -> unguardedWriteCalls() );
            UTF_REQUIRE( 0U != probe -> unguardedReadCalls() );
            UTF_REQUIRE( probe -> unguardedTimerTicks() >= std::size_t( PROBE::MIN_TIMER_TICKS ) );

            UTF_REQUIRE_EQUAL(
                probe -> unguardedHandlerCalls(),
                probe -> unguardedWriteCalls() + probe -> unguardedReadCalls() + probe -> unguardedTimerTicks()
                );
        }

        /**
         * @brief The cancel case body, shared by the plain and the TLS policy
         *
         * What it pins is that the stranded cancelTask() POSTS the forced shutdown instead of
         * making the socket call from the cancelling thread. With the strand occupied by the
         * blocker, a synchronous shutdown would abort the pending read at once and its handler
         * would run; a posted one cannot run until the blocker lets go
         */

        template
        <
            typename PROBE,
            typename PEER
        >
        inline void runCancelPostsShutdownCase(
            SAA_inout       PEER&                                               peer,
            SAA_in          const std::string&                                  host
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            typedef om::ObjectImpl< PROBE > probe_impl_t;

            const auto probe = probe_impl_t::createInstance(
                std::string( host ),
                peer.port(),
                PROBE::ModeReadAndPark
                );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ &peer, &probe, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    peer.acceptAndIdle();

                    UTF_REQUIRE( waitFor( [ &probe ]() -> bool { return probe -> isConnected(); } ) );

                    probe -> blockStrand();

                    UTF_REQUIRE( waitFor( [ &probe ]() -> bool { return probe -> isBlockerRunning(); } ) );

                    /*
                     * The peer has sent nothing, so the read is outstanding and can only be
                     * completed by the cancel
                     */

                    UTF_REQUIRE_EQUAL( probe -> readHandlerCalls(), 0U );

                    task -> requestCancel();

                    /*
                     * The decisive assertion, and the reason the peer is here at all: a cancel
                     * which shut the socket down from the CANCELLING thread would already be an
                     * end of stream here. It is not, because the strand has not run the shutdown
                     * yet - the blocker is still sitting on it
                     */

                    UTF_REQUIRE( ! peer.sawClientShutdownWithin( time::milliseconds( 250 ) ) );

                    UTF_REQUIRE_EQUAL( probe -> readHandlerCalls(), 0U );

                    probe -> releaseStrand();

                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );

                    /*
                     * ... and once the strand is free the shutdown it was given does happen
                     */

                    UTF_REQUIRE(
                        peer.sawClientShutdownWithin(
                            time::seconds( PEER::DEFAULT_TIMEOUT_IN_SECONDS )
                            )
                        );
                }
                );

            UTF_REQUIRE( task -> isFailed() );
            UTF_REQUIRE_EQUAL( exceptionCodeOf( task ), asio::error::operation_aborted );

            UTF_REQUIRE_EQUAL( probe -> readHandlerCalls(), 1U );

            UTF_REQUIRE( ! probe -> offStrandSeen() );

            UTF_REQUIRE( probe -> wasSocketShutdownForcefully() );
        }

    } // strandedstreams

} // utest

UTF_AUTO_TEST_CASE( TcpStrandedStreams_PlainFullDuplexTests )
{
    using namespace utest::strandedstreams;

    /*
     * S3.2's acceptance: a task using the stranded plain policy does a concurrent read, write and
     * timer on the socket with every handler on the strand and no lock of its own
     */

    EchoLoopbackPeer peer;

    runFullDuplexCase
        <
            StrandedFullDuplexProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
        >
        ( peer, std::string( "127.0.0.1" ) );
}

UTF_AUTO_TEST_CASE( TcpStrandedStreams_PlainCancelPostsShutdownToStrandTests )
{
    using namespace utest::strandedstreams;

    /*
     * The other half of S3.2: cancelTask() posts the forced shutdown to the strand rather than
     * calling into the socket from whichever thread requested the cancel
     */

    EchoLoopbackPeer peer;

    runCancelPostsShutdownCase
        <
            StrandedFullDuplexProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
        >
        ( peer, std::string( "127.0.0.1" ) );
}

#endif /* __UTEST_TESTTCPSTRANDEDSTREAMS_H_ */
