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

#ifndef __UTEST_TESTHTTP2DRIVERWRITEPEERCLOSE_H_
#define __UTEST_TESTHTTP2DRIVERWRITEPEERCLOSE_H_

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The write path's peer-close arm - the two endings it got wrong
 *
 * BOTH CASES ARRANGE THE WINDOW IN WHICH NO READ IS ARMED, and the arranging is the whole
 * difficulty. This driver has exactly two such places, each inside one strand turn:
 * onProtocolNegotiated( )'s pumpWrites( ) / scheduleRead( ) pair, and onRead( )'s. One case per
 * window. With a read armed the ending reaches the read handler as well, and which of the two the
 * reactor and the strand reach first is a question these cases deliberately do NOT settle - what
 * they pin is the driver's answer where the write is unambiguously alone
 *
 * WHAT THE DRIVER GOT WRONG THERE, and they are two defects at the same six lines:
 *
 *   - it asked isPeerClosed( ), which is the READ side pair and admits no broken_pipe on any
 *     platform. A peer which half closes before it resets makes the send take EPIPE, so an
 *     ordinary ending reached BL_TASKS_HANDLER_CHK_EC( ) and FAILED the task - R1;
 *   - and it called onPeerClosed( ) from the write handler, which empties m_streams through
 *     closeAllStreamsUnwrittenRetryable( ) BEFORE the read handler - posted second, carrying the
 *     peer's actual response - is dispatched. sinkOf( ) then answers nullptr, feed( ) has nowhere
 *     to put the answer, and the caller is told connection_aborted and NOT retryable for a request
 *     the server had already answered - R2.
 *
 * R2 IS THE ONE THAT DISCRIMINATES. Both a predicate-only fix and the shape fix make R1 green;
 * only the write DECLINING makes R2 green, because a write which calls onPeerClosed( ) drops the
 * answer whichever predicate let it in
 *
 * AND THE RENDEZVOUS IS THE SEAM, NOT A SLEEP. onWriteScheduled( ) is called on the strand
 * immediately before async_write( ) is issued - which is to say INSIDE the no-read-armed window -
 * and TestHttp2DriverWriteBarrier.h already uses it that way. From in there the probe tells the
 * peer to end, then waits on a NON-CONSUMING poll( ) of the driver's own descriptor for POLLERR:
 * a FIN alone gives POLLIN, and POLLERR appears only once the RST has been applied and sk_err is
 * set. poll( ) takes neither the write's pending error nor the read's queued bytes, so the
 * ordering that follows is the strand's and the kernel's rather than the scheduler's. A run count
 * would prove nothing here; this makes the failure certain
 *
 * WHY THE PEER RESETS WITH SO_LINGER( true, 0 ) RATHER THAN BY LEAVING OUR BYTES UNREAD. Both put
 * a RST on the wire, but the unread-data route needs our bytes to have ARRIVED at the peer before
 * it closes, which is exactly the race the poll( ) above exists to remove - and in R1 nothing of
 * ours has been written yet when the peer is told to go. A zero linger close resets from FIN_WAIT
 * unconditionally, so the two packets are FIN then RST in that order on every run
 *
 * POSIX ONLY, AND DELIBERATELY. The ending these cases build - FIN, then RST, so the send takes
 * EPIPE from CLOSE_WAIT - is a POSIX kernel behaviour, and core/NetUtils.h says in
 * isPeerClosedOnWriteErrorCode( ) that what a send into a reset connection is spelled on Windows
 * is owed to the matrix and is not to be guessed. On Windows the codes a send there takes are
 * already ones the read-side predicate admits, so R1 would be green before and after and prove
 * nothing. The module's other case runs on every platform
 *
 * MEASURED ON THE WINDOWS MATRIX 2026-09-24, so the exclusion rests on a measurement and not on the
 * sentence above. A send parked across the peer's FIN and then its RST completes WSAECONNRESET -
 * raw sockets, 20 of 20, and after an ordinary close over our unread upload the same - and through
 * the HTTP/1.1 driver WSAECONNRESET or WSAECONNABORTED; never EPIPE. Both are codes
 * isPeerClosedErrorCode( ) admits there, which is exactly why R1 would be green before and after
 */

#if ! defined( _WIN32 )

#include <poll.h>

namespace utest
{
    namespace h2driver
    {
        namespace writepeerclose
        {
            enum : std::size_t
            {
                /**
                 * @brief How long any one rendezvous in these cases is given
                 *
                 * Every wait here is for something the other side has already been TOLD to do, so
                 * the bound is only there to turn a broken fixture into a diagnosis instead of a
                 * hung suite. Nothing is timed against it
                 */

                RENDEZVOUS_IN_MILLISECONDS          = 10000U,

                /**
                 * @brief The HTTP/2 frame header, and the client connection preface
                 */

                FRAME_HEADER_SIZE                   = 9U,
                CLIENT_PREFACE_SIZE                 = 24U,
            };

            /**
             * @brief HTTP/2 frame types, only the three these cases put on the wire or look for
             */

            enum : unsigned char
            {
                FRAME_TYPE_DATA                     = 0x00U,
                FRAME_TYPE_HEADERS                  = 0x01U,
                FRAME_TYPE_SETTINGS                 = 0x04U,

                FLAG_END_STREAM                     = 0x01U,
                FLAG_ACK                            = 0x01U,
                FLAG_END_HEADERS                    = 0x04U,
            };

            /**
             * @brief One HTTP/2 frame, built by hand
             *
             * THE PEER BELOW IS NOT Http2TestServerT AND MUST NOT BECOME ONE, for the reason the
             * barrier case gives of its own peer: the session in there reads its socket
             * continuously and answers on its own schedule, and both of those are wakers for the
             * very write these cases hold. What is needed here is four canned frames and a socket
             * which stops reading when it is told to, which is below the protocol
             *
             * AND IT IS BUILT HERE RATHER THAN TAKEN FROM utests/baselib/RawFrameScriptPeer.h,
             * whose frameOctets( ) is the same nine octets. That header includes Http2TestServer.h
             * for its recorder, which is the very thing this module exists NOT to instantiate, and
             * its peer is a fixed script with no lever the driver's own strand can pull
             */

            inline auto frameOf(
                SAA_in          const unsigned char                             type,
                SAA_in          const unsigned char                             flags,
                SAA_in          const std::uint32_t                             streamId,
                SAA_in_opt      const std::string&                              payload = std::string()
                )
                -> std::string
            {
                std::string frame;

                frame += static_cast< char >( ( payload.size() >> 16 ) & 0xFFU );
                frame += static_cast< char >( ( payload.size() >> 8 ) & 0xFFU );
                frame += static_cast< char >( payload.size() & 0xFFU );
                frame += static_cast< char >( type );
                frame += static_cast< char >( flags );
                frame += static_cast< char >( ( streamId >> 24 ) & 0xFFU );
                frame += static_cast< char >( ( streamId >> 16 ) & 0xFFU );
                frame += static_cast< char >( ( streamId >> 8 ) & 0xFFU );
                frame += static_cast< char >( streamId & 0xFFU );
                frame += payload;

                return frame;
            }

            /**
             * @brief The peer's SETTINGS - empty, and only there to make the driver answer
             *
             * It is what gets the driver into the onRead( ) window at all: a received SETTINGS
             * owes an acknowledgement, so feed( ) leaves the session wanting a write and onRead( )
             * pumps it before it arms the next read
             */

            inline auto settingsFrame() -> std::string
            {
                return frameOf( FRAME_TYPE_SETTINGS, 0U /* flags */, 0U /* streamId */ );
            }

            /**
             * @brief A complete response on stream 1 - ":status: 200" and a body
             *
             * The header block is ONE byte: HPACK's indexed representation of static table entry
             * 8, which is ":status: 200" (RFC 7541 appendix A). Nothing here needs a real encoder,
             * and a case which shipped one would be testing it rather than the driver
             */

            inline auto responseBody() -> std::string
            {
                return std::string( "hello" );
            }

            inline auto responseFrames() -> std::string
            {
                return
                    frameOf(
                        FRAME_TYPE_HEADERS,
                        FLAG_END_HEADERS,
                        1U /* streamId */,
                        std::string( 1U, static_cast< char >( 0x88U ) )
                        ) +
                    frameOf(
                        FRAME_TYPE_DATA,
                        FLAG_END_STREAM,
                        1U /* streamId */,
                        responseBody()
                        );
            }

            /**
             * @brief Whether a client's HEADERS frame has been seen in what the peer has read
             *
             * The peer waits for this before it says anything, so that its SETTINGS - and with it
             * the driver's acknowledging write, which is the write the probe arms on - can only
             * arrive AFTER the request is on the wire and stream 1 exists
             */

            inline bool sawClientHeaders( SAA_in const std::string& read )
            {
                if( read.size() < static_cast< std::size_t >( CLIENT_PREFACE_SIZE ) )
                {
                    return false;
                }

                const auto* const bytes = reinterpret_cast< const unsigned char* >( read.c_str() );

                std::size_t pos = static_cast< std::size_t >( CLIENT_PREFACE_SIZE );

                while( pos + static_cast< std::size_t >( FRAME_HEADER_SIZE ) <= read.size() )
                {
                    const std::size_t length =
                        ( static_cast< std::size_t >( bytes[ pos ] ) << 16 ) |
                        ( static_cast< std::size_t >( bytes[ pos + 1U ] ) << 8 ) |
                        static_cast< std::size_t >( bytes[ pos + 2U ] );

                    if( FRAME_TYPE_HEADERS == bytes[ pos + 3U ] )
                    {
                        return true;
                    }

                    pos += static_cast< std::size_t >( FRAME_HEADER_SIZE ) + length;
                }

                return false;
            }

            /**
             * @brief Waits for one of the poll( ) conditions on a descriptor WITHOUT reading it
             *
             * NON-CONSUMING IS THE WHOLE POINT. The driver's pending error and its queued bytes
             * are each taken by the FIRST syscall to ask for them - sock_error( ) is an exchange -
             * so a rendezvous which read, or which asked for the socket error, would take the code
             * the write under test is supposed to get. poll( ) touches neither
             *
             * THE EVENTS MASK AND THE CONDITION ARE SEPARATE ARGUMENTS, and the separation is
             * load bearing for the POLLERR wait. POLLERR, POLLHUP and POLLNVAL are set in revents
             * whether or not they were asked for, so a wait for POLLERR asks for NOTHING - with
             * POLLIN in the mask, a FIN which has already made the socket readable wakes poll( )
             * on every call and the wait becomes a hot spin
             *
             * AND THE BOUND IS A DEADLINE, NOT A SLICE COUNT. Counting a slice's worth of
             * milliseconds per iteration is only true while poll( ) actually BLOCKS for it - and
             * the whole point of the paragraph above is that it may not. Measured: the first shape
             * of this helper spent its entire ten second budget in four milliseconds and reported
             * a timeout, which read exactly like the reset never arriving
             */

            inline bool pollForRevents(
                SAA_in          const int                                       descriptor,
                SAA_in          const short                                     events,
                SAA_in          const short                                     wanted,
                SAA_in          const std::size_t                               timeoutInMilliseconds
                )
            {
                enum : int
                {
                    POLL_SLICE_IN_MILLISECONDS = 50,
                };

                const auto deadline =
                    bl::os::chrono::steady_clock::now() +
                    bl::os::chrono::milliseconds( timeoutInMilliseconds );

                for( ;; )
                {
                    struct pollfd descriptors = {};

                    descriptors.fd = descriptor;
                    descriptors.events = events;

                    const int count = ::poll(
                        &descriptors,
                        1U /* nfds */,
                        static_cast< int >( POLL_SLICE_IN_MILLISECONDS )
                        );

                    if( count > 0 && 0 != ( descriptors.revents & wanted ) )
                    {
                        return true;
                    }

                    if( bl::os::chrono::steady_clock::now() >= deadline )
                    {
                        return false;
                    }
                }
            }

            /**
             * @brief A loopback peer which ends the conversation FIN first and RST second
             *
             * Two levers, both pulled by the probe from inside the driver's write seam:
             *
             *   - release( ) - send whatever this peer was going to answer with, then FIN;
             *   - reset( ) - close with a zero linger, which puts the RST out from FIN_WAIT.
             *
             * They are separate so that the probe can satisfy itself, between the two, that the
             * answer has actually landed in the driver's receive queue. That is what makes R2 an
             * arrangement rather than a hope
             */

            class EndingPeer
            {
                BL_NO_COPY_OR_MOVE( EndingPeer )

            public:

                EndingPeer( SAA_in std::string answer, SAA_in const bool speaksHttp2 )
                    :
                    m_acceptor(
                        m_ioService,
                        bl::asio::ip::tcp::endpoint(
                            bl::asio::ip::address_v4::loopback(),
                            0 /* ephemeral */
                            )
                        ),
                    m_port( m_acceptor.local_endpoint().port() ),
                    m_answer( BL_PARAM_FWD( answer ) ),
                    m_speaksHttp2( speaksHttp2 ),
                    m_isReleased( false ),
                    m_isReset( false ),
                    m_isSpoken( false ),
                    m_isFinished( false )
                {
                    m_thread.reset( new bl::os::thread( bl::cpp::bind( &EndingPeer::run, this ) ) );
                }

                ~EndingPeer() NOEXCEPT
                {
                    BL_NOEXCEPT_BEGIN()

                    release();

                    reset();

                    {
                        /*
                         * Closing the acceptor does not reliably wake a worker already blocked in
                         * accept( ), so one throwaway connection does it - harmless once this peer
                         * has accepted the driver's
                         */

                        bl::eh::error_code ec;

                        bl::asio::io_service ioService;
                        bl::asio::ip::tcp::socket probe( ioService );

                        probe.connect(
                            bl::asio::ip::tcp::endpoint(
                                bl::asio::ip::address_v4::loopback(),
                                m_port
                                ),
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
                 * @brief Whether this peer got as far as saying its piece - R2's precondition
                 */

                bool hasSpoken() const NOEXCEPT
                {
                    BL_MUTEX_GUARD( m_lock );

                    return m_isSpoken;
                }

                void release()
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_isReleased = true;

                    m_cv.notify_all();
                }

                void reset()
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_isReset = true;

                    m_cv.notify_all();
                }

                bool waitForFinished( SAA_in const std::size_t timeoutInMilliseconds ) const
                {
                    bl::os::mutex_unique_lock guard( m_lock );

                    return m_cv.wait_for(
                        guard,
                        bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                        [ this ]() -> bool
                        {
                            return m_isFinished;
                        }
                        );
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
                            recordFailure( "the peer could not accept: " + ec.message() );

                            return;
                        }

                        if( m_speaksHttp2 && ! readUntilClientHeaders( socket ) )
                        {
                            return;
                        }

                        if( m_speaksHttp2 )
                        {
                            const auto settings = settingsFrame();

                            bl::asio::write( socket, bl::asio::buffer( settings ), ec );

                            if( ec )
                            {
                                recordFailure( "the peer could not send SETTINGS: " + ec.message() );

                                return;
                            }
                        }

                        waitFor(
                            [ this ]() -> bool
                            {
                                return m_isReleased;
                            }
                            );

                        if( ! m_answer.empty() )
                        {
                            bl::asio::write( socket, bl::asio::buffer( m_answer ), ec );

                            if( ec )
                            {
                                recordFailure( "the peer could not answer: " + ec.message() );

                                return;
                            }
                        }

                        /*
                         * FIN, AND IT HAS TO BE ORDERED BEFORE THE RESET. It is what puts the
                         * driver's socket into CLOSE_WAIT, which is the only state from which
                         * tcp_reset( ) writes EPIPE rather than ECONNRESET - and ECONNRESET is a
                         * code this driver already admitted, so a reset with no FIN in front of it
                         * comes out green and proves nothing
                         */

                        socket.shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );

                        if( ec )
                        {
                            recordFailure( "the peer could not half close: " + ec.message() );

                            return;
                        }

                        {
                            BL_MUTEX_GUARD( m_lock );

                            m_isSpoken = true;

                            m_cv.notify_all();
                        }

                        waitFor(
                            [ this ]() -> bool
                            {
                                return m_isReset;
                            }
                            );

                        /*
                         * AND THE RESET. A zero linger makes close( ) abort from FIN_WAIT rather
                         * than finish the handshake, so the RST is unconditional instead of
                         * depending on our bytes having reached this side unread
                         */

                        socket.set_option(
                            bl::asio::socket_base::linger( true /* enabled */, 0 /* timeout */ ),
                            ec
                            );

                        if( ec )
                        {
                            recordFailure( "the peer could not set a zero linger: " + ec.message() );
                        }
                    }
                    catch( std::exception& e )
                    {
                        recordFailure( e.what() );
                    }

                    {
                        bl::eh::error_code ec;

                        socket.close( ec );
                    }

                    {
                        BL_MUTEX_GUARD( m_lock );

                        m_isFinished = true;

                        m_cv.notify_all();
                    }
                }

                bool readUntilClientHeaders( SAA_in bl::asio::ip::tcp::socket& socket )
                {
                    std::string read;

                    for( ;; )
                    {
                        char buffer[ 4096 ];

                        bl::eh::error_code ec;

                        const auto count = socket.read_some( bl::asio::buffer( buffer ), ec );

                        if( ec )
                        {
                            recordFailure(
                                "the peer never saw the request head: " + ec.message()
                                );

                            return false;
                        }

                        read.append( buffer, count );

                        if( sawClientHeaders( read ) )
                        {
                            return true;
                        }
                    }
                }

                void recordFailure( SAA_in std::string what )
                {
                    BL_MUTEX_GUARD( m_lock );

                    if( m_failure.empty() )
                    {
                        m_failure = BL_PARAM_FWD( what );
                    }

                    m_cv.notify_all();
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

                bl::asio::io_service                                            m_ioService;
                bl::asio::ip::tcp::acceptor                                     m_acceptor;
                const bl::os::port_t                                            m_port;
                const std::string                                               m_answer;
                const bool                                                      m_speaksHttp2;

                mutable bl::os::mutex                                           m_lock;
                mutable bl::os::condition_variable                              m_cv;
                std::string                                                     m_failure;
                bool                                                            m_isReleased;
                bool                                                            m_isReset;
                bool                                                            m_isSpoken;
                bool                                                            m_isFinished;

                bl::cpp::SafeUniquePtr< bl::os::thread >                        m_thread;
            };

            /**
             * @brief The driver, ending the conversation from inside its own write seam
             *
             * WHAT THE SEAM DOES, IN ORDER, AND WHY EACH STEP IS THERE:
             *
             *   1. it waits for the write the case armed it on - the preface for R1, the SETTINGS
             *      acknowledgement for R2 - so that the ending lands in the intended window and
             *      not in some other pump;
             *   2. it releases the peer, which answers ( R2 ) and then puts FIN out;
             *   3. it waits, by poll( ), until the answer is sitting in this socket's receive
             *      queue. THIS is what makes R2's second handler carry the response rather than an
             *      empty read, and it is a poll( ) rather than a read because a read would take
             *      the bytes the driver is supposed to get;
             *   4. it resets the peer and waits for POLLERR, which is sk_err arriving and is the
             *      proof the RST has been APPLIED rather than merely sent;
             *   5. and returns, so that async_write( )'s speculative send( ) runs against a socket
             *      that is already broken, on this strand, with no read armed.
             *
             * Every step is a fact about the kernel's state, never an interval - a sleep here
             * would leave the case asserting on whichever waker got there first
             */

            template
            <
                typename E = void
            >
            class EndingProbeT :
                public DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
            {
                BL_DECLARE_OBJECT_IMPL( EndingProbeT )

            public:

                typedef EndingProbeT< E >                                       this_type;
                typedef DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >   base_type;

            protected:

                EndingPeer&                                                     m_peer;
                const bool                                                      m_armOnSettingsAck;
                const std::size_t                                               m_answerSize;

                mutable bl::os::mutex                                           m_probeLock;

                bool                                                            m_isArmed;
                bool                                                            m_sawAnswerQueued;
                bool                                                            m_sawPollError;
                std::string                                                     m_seamFailure;

                EndingProbeT(
                    SAA_in          ConnectionKey                               key,
                    SAA_in          typename base_type::factory_ptr_t           driverFactory,
                    SAA_in          Http2ConnectionConfig                       h2config,
                    SAA_in          ClientConnectionConfig                      config,
                    SAA_in          EndingPeer*                                 peer,
                    SAA_in          const bool                                  armOnSettingsAck,
                    SAA_in          const std::size_t                           answerSize
                    )
                    :
                    base_type(
                        BL_PARAM_FWD( key ),
                        BL_PARAM_FWD( driverFactory ),
                        BL_PARAM_FWD( h2config ),
                        BL_PARAM_FWD( config )
                        ),
                    m_peer( *peer ),
                    m_armOnSettingsAck( armOnSettingsAck ),
                    m_answerSize( answerSize ),
                    m_isArmed( false ),
                    m_sawAnswerQueued( false ),
                    m_sawPollError( false )
                {
                }

                /**
                 * @brief Whether this write is the one the case wants the ending to land on
                 */

                bool isTheArmingWrite(
                    SAA_in          const bl::http2::Session::wire_buffer_t&    buffer
                    ) const
                {
                    if( ! m_armOnSettingsAck )
                    {
                        /*
                         * R1 - the opening write, identified by the connection preface it starts
                         * with rather than by being the first one to arrive
                         */

                        return
                            buffer.size() >= static_cast< std::size_t >( CLIENT_PREFACE_SIZE ) &&
                            'P' == buffer[ 0 ] && 'R' == buffer[ 1 ] && 'I' == buffer[ 2 ];
                    }

                    /*
                     * R2 - the SETTINGS acknowledgement, which is the ONE write this driver pumps
                     * out of onRead( ) for a GET whose request head is already away
                     */

                    return
                        buffer.size() >= static_cast< std::size_t >( FRAME_HEADER_SIZE ) &&
                        FRAME_TYPE_SETTINGS == buffer[ 3 ] &&
                        FLAG_ACK == buffer[ 4 ];
                }

                virtual void onWriteScheduled(
                    SAA_in          const bl::http2::Session::wire_buffer_t&    buffer
                    ) OVERRIDE
                {
                    base_type::onWriteScheduled( buffer );

                    {
                        BL_MUTEX_GUARD( m_probeLock );

                        if( m_isArmed || ! isTheArmingWrite( buffer ) )
                        {
                            return;
                        }

                        m_isArmed = true;
                    }

                    endTheConversation();
                }

                /**
                 * @brief NOTHING IN HERE MAY THROW, and nothing in here asserts
                 *
                 * This runs inside pumpWrites( ), inside a task handler, so a throw would be
                 * swallowed into the task's own error and a UTF macro would report from the wrong
                 * thread. Every failure is recorded and read back by the case instead
                 */

                void endTheConversation() NOEXCEPT
                {
                    BL_NOEXCEPT_BEGIN()

                    const auto bound = static_cast< std::size_t >( RENDEZVOUS_IN_MILLISECONDS );

                    const int descriptor =
                        static_cast< int >( base_type::getSocket().native_handle() );

                    m_peer.release();

                    bool sawAnswerQueued = true;

                    if( 0U != m_answerSize )
                    {
                        sawAnswerQueued = waitForQueuedAnswer( descriptor, bound );
                    }

                    m_peer.reset();

                    const bool sawPollError =
                        pollForRevents( descriptor, 0 /* events */, POLLERR, bound );

                    {
                        BL_MUTEX_GUARD( m_probeLock );

                        m_sawAnswerQueued = sawAnswerQueued;
                        m_sawPollError = sawPollError;

                        if( ! sawAnswerQueued )
                        {
                            m_seamFailure =
                                "the peer's answer never reached the driver's receive queue";
                        }
                        else if( ! sawPollError )
                        {
                            m_seamFailure =
                                "the peer's reset never reached the driver's socket - poll( ) "
                                "reported no POLLERR";
                        }
                    }

                    BL_NOEXCEPT_END()
                }

                /**
                 * @brief Waits until the peer's answer is queued on this socket, reading nothing
                 *
                 * available( ) is FIONREAD and takes nothing out of the queue; poll( ) is what
                 * blocks in between, so this is a rendezvous on an arrival and not a spin
                 */

                bool waitForQueuedAnswer(
                    SAA_in          const int                                   descriptor,
                    SAA_in          const std::size_t                           timeoutInMilliseconds
                    )
                {
                    enum : std::size_t
                    {
                        SLICE_IN_MILLISECONDS = 50U,
                    };

                    const auto deadline =
                        bl::os::chrono::steady_clock::now() +
                        bl::os::chrono::milliseconds( timeoutInMilliseconds );

                    for( ;; )
                    {
                        bl::eh::error_code ec;

                        const auto available = base_type::getSocket().available( ec );

                        if( ! ec && available >= m_answerSize )
                        {
                            return true;
                        }

                        if( bl::os::chrono::steady_clock::now() >= deadline )
                        {
                            return false;
                        }

                        ( void ) pollForRevents(
                            descriptor,
                            POLLIN,
                            POLLIN,
                            static_cast< std::size_t >( SLICE_IN_MILLISECONDS )
                            );
                    }
                }

            public:

                auto seamFailure() const -> std::string
                {
                    BL_MUTEX_GUARD( m_probeLock );

                    return m_seamFailure;
                }

                bool wasArmed() const NOEXCEPT
                {
                    BL_MUTEX_GUARD( m_probeLock );

                    return m_isArmed;
                }

                bool sawPollError() const NOEXCEPT
                {
                    BL_MUTEX_GUARD( m_probeLock );

                    return m_sawPollError;
                }
            };

            typedef bl::om::ObjectImpl< EndingProbeT<> > EndingProbe;

            /**
             * @brief What the sink was told, as one line, so a failure says what it saw
             */

            inline auto describeSink(
                SAA_in          const bl::om::ObjPtr< RecordingSink >&          sink
                )
                -> std::string
            {
                std::string what;

                const auto records = sink -> records();

                for( std::size_t i = 0U; i < records.size(); ++i )
                {
                    if( ! what.empty() )
                    {
                        what += ", ";
                    }

                    what += records[ i ];
                }

                return what.empty() ? std::string( "nothing" ) : what;
            }

        } // writepeerclose

    } // h2driver

} // utest

/**
 * @brief R1 - a peer which half closes and then resets must not fail the task
 *
 * THE WINDOW IS onProtocolNegotiated( )'s, and no HTTP/2 is spoken at all: the driver connects,
 * produces the preface, and the seam ends the conversation before the send is issued. With no read
 * armed and isClosing( ) false, the send takes EPIPE - which the read-side predicate the write
 * path used to ask does not admit on any platform, so the ending went to
 * BL_TASKS_HANDLER_CHK_EC( ) and the task FAILED
 *
 * RED BEFORE THE FIX with exactly that failure, and green after it under either shape. The reading
 * which led here recorded the red as "a peer that RSTs" and that is NOT this case: a reset with no
 * FIN in front of it gives connection_reset, which the driver already admitted, and would come out
 * green. The FIN is load bearing and the peer puts it out first
 */

UTF_AUTO_TEST_CASE( H2Driver_PeerEndsWhileTheOpeningWriteIsBeingIssuedTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;
    using namespace utest::h2driver::writepeerclose;

    EndingPeer peer( std::string() /* answer */, false /* speaksHttp2 */ );

    const auto record = std::make_shared< FallbackRecord >();

    const auto driver = EndingProbe::createInstance(
        makeKey( "http", "127.0.0.1", peer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        Http2ConnectionConfig(),
        cleartextHttp2Config(),
        &peer,
        false /* armOnSettingsAck */,
        0U /* answerSize */
        );

    const auto driverTask = om::qi< Task >( driver );

    bool taskFailed = false;
    std::string taskFailure;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( driverTask );

            eq -> wait( driverTask );

            taskFailed = driverTask -> isFailed();
            taskFailure = exceptionMessageOf( driverTask );

            /*
             * DISCARDED HERE rather than left to the outer flush, which turns a failed task into
             * an exception out of the harness before the assertion below can report it
             */

            eq -> forceFlushNoThrow();
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The preconditions the assertion rests on, asserted rather than assumed: the seam really did
     * fire on the opening write, and the reset really had been applied to this socket before the
     * send was issued. If either is false the case is not exercising the window at all and says so
     */

    if( ! driver -> wasArmed() )
    {
        UTF_FAIL( "the seam never saw the opening write - writes: " + describeWrites( driver -> writes() ) );
    }

    UTF_REQUIRE_EQUAL( driver -> seamFailure(), std::string() );

    UTF_REQUIRE( driver -> sawPollError() );

    if( taskFailed )
    {
        UTF_FAIL(
            "the peer's ordinary ending failed the connection task: " + taskFailure
            );
    }
}

/**
 * @brief R2 - the answer the peer already gave must reach the caller, not "do not retry"
 *
 * THE WINDOW IS onRead( )'s, and this is the case that discriminates the two shapes. The peer
 * waits for the request head, sends SETTINGS, and the driver acknowledges it from onRead( ) -
 * which is pumpWrites( ) BEFORE scheduleRead( ), so no read is armed. The seam then has the peer
 * send a COMPLETE response, half close, and reset, and satisfies itself by poll( ) that the
 * response is in this socket's receive queue and that the reset has been applied. The write's
 * send( ) takes EPIPE and its handler is posted FIRST; the read's speculative recv( ) takes the
 * response and its handler is posted SECOND
 *
 * WHAT EACH SHAPE THEN DOES, AND ONLY ONE OF THE THREE IS RIGHT:
 *
 *   - the unfixed driver fails the task on EPIPE, as R1 does;
 *   - a driver which only swaps the predicate calls onPeerClosed( ) from the write handler, which
 *     empties m_streams and tells this sink connection_aborted and NOT retryable - and the read
 *     handler then feeds the answer into a session with no streams, where sinkOf( ) drops it;
 *   - a driver whose write handler DECLINES leaves the classification to the read, which feeds the
 *     answer to a live stream.
 *
 * So the status assertion below is red against the predicate-only shape and green only against the
 * one this driver takes. It is the assertion this case exists for; the task assertion it shares
 * with R1
 */

UTF_AUTO_TEST_CASE( H2Driver_AnsweredRequestSurvivesAPeerEndingOnTheWriteTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;
    using namespace utest::h2driver::writepeerclose;

    const auto answer = responseFrames();

    EndingPeer peer( cpp::copy( answer ), true /* speaksHttp2 */ );

    const auto record = std::make_shared< FallbackRecord >();

    const auto driver = EndingProbe::createInstance(
        makeKey( "http", "127.0.0.1", peer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        Http2ConnectionConfig(),
        cleartextHttp2Config(),
        &peer,
        true /* armOnSettingsAck */,
        answer.size()
        );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );
    /*
     * NO setConnection( ) ON THIS SINK, DELIBERATELY. That is what makes RecordingSinkT answer
     * onData( ) with a consumed( ) call, and a consumed( ) landing while the connection is ending
     * posts one more command and one more pump - a second moving part this case has no use for,
     * since five bytes of body press on no window
     */

    const auto sink = RecordingSink::createInstance();

    const auto driverTask = om::qi< Task >( driver );

    bool taskFailed = false;
    std::string taskFailure;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( driverTask );

            const auto handle = connection -> submit(
                makeRequest( "http://127.0.0.1/answered" ),
                om::qi< httpclient::ClientStreamEventSink >( sink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

            eq -> wait( driverTask );

            taskFailed = driverTask -> isFailed();
            taskFailure = exceptionMessageOf( driverTask );

            eq -> forceFlushNoThrow();
        }
        );

    sink -> setConnection( nullptr );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The preconditions, again asserted rather than assumed: the peer got the request head and
     * said its piece, the seam fired on the acknowledging write - which is the write inside the
     * window - and both halves of the ending were observed on this socket
     */

    UTF_REQUIRE( peer.hasSpoken() );

    if( ! driver -> wasArmed() )
    {
        UTF_FAIL(
            "the seam never saw the SETTINGS acknowledgement - writes: " +
            describeWrites( driver -> writes() )
            );
    }

    UTF_REQUIRE_EQUAL( driver -> seamFailure(), std::string() );

    UTF_REQUIRE( driver -> sawPollError() );

    if( taskFailed )
    {
        UTF_FAIL( "the peer's ordinary ending failed the connection task: " + taskFailure );
    }

    /*
     * AND THE ANSWER ITSELF. The server replied 200 with a body and then ended the connection;
     * a caller told anything else about this stream has been told something false
     */

    if( 200U != sink -> status() )
    {
        UTF_FAIL(
            "the answered response did not reach the sink - status " +
            utils::lexical_cast< std::string >( sink -> status() ) +
            ", recorded: " + describeSink( sink )
            );
    }

    UTF_REQUIRE_EQUAL( sink -> body(), responseBody() );

    if( sink -> errorCode() )
    {
        UTF_FAIL(
            "the answered stream was reported as an error: " +
            sink -> errorCode().message() + ", recorded: " + describeSink( sink )
            );
    }
}

#endif // ! defined( _WIN32 )

#endif /* __UTEST_TESTHTTP2DRIVERWRITEPEERCLOSE_H_ */
