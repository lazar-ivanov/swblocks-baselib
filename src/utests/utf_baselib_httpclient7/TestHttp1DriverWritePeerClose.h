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

#ifndef __UTEST_TESTHTTP1DRIVERWRITEPEERCLOSE_H_
#define __UTEST_TESTHTTP1DRIVERWRITEPEERCLOSE_H_

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * A2 - a peer which goes away while the REQUEST is still going out
 *
 * THE DEFECT. onWriteCompleted( ) had one classification arm and it was this task's own state: a
 * write failed by initiateClose( )'s own shutdown is not a failure of this task. Everything else
 * went to BL_TASKS_HANDLER_CHK_EC( ), including the one ending which is not a fault at all - the
 * peer hanging up. A peer close reaches BOTH halves of a duplex connection at once, so whether the
 * request failed depended on which handler got there first, which is the same complaint the HTTP/2
 * driver's own onWrite( ) comment records against its own past.
 *
 * SO THE ORDER HAS TO BE ARRANGED AND NOT HOPED FOR, AND A RUN COUNT IS NOT THE EVIDENCE. The
 * defect exists only when the WRITE handler reaches the ending first: if the read gets there
 * first it takes onPeerClosed( ) and closeConnection( ), and the write's error is then excused by
 * the TEARDOWN arm - so a run in the other order is green against the unfixed tree and proves
 * nothing. Two earlier shapes of this exchange measured 19 of 20 and 17 of 20 red, which is
 * exactly the "mostly" this case must not be built on.
 *
 * WHAT MAKES IT CERTAIN IS THAT THE READ OP DOES NOT EXIST WHEN THE RESET LANDS. A read handler
 * which is RUNNING is a read op that is not registered with the reactor, and the sink is called
 * from inside one, synchronously, on the driver's own strand. So the sink holds it - see
 * ResetWhileStrandIsHeldSink - while the peer resets. The reactor then finds the write op ALONE,
 * and:
 *
 *   - the write handler is enqueued on a strand which is locked, so it waits in the strand's
 *     waiting queue (strand_executor_service::enqueue)
 *   - the sink returns, scheduleRead( ) re-arms, and only THEN can a read completion be enqueued
 *     behind it
 *   - the strand promotes its waiting queue as one block and drains it in order
 *     (push_waiting_to_ready, run_ready_handlers), so the write handler runs first
 *
 * THE OTHER HALF OF THE ARRANGEMENT IS THAT THE WRITE MUST ALREADY BE PARKED. A composed
 * asio::async_write between two of its internal steps has NO op registered, and its intermediate
 * handlers run on the strand - which is the very thing the sink is holding. So the exchange lets
 * the driver reach a full peer window with the strand free first, and only then puts the second
 * body chunk and the reset behind it. An earlier shape which gated on the response HEAD gated too
 * early for exactly this reason, and that is what its three green runs in twenty were.
 *
 * THE CODES, MEASURED. The reset is consumed by whichever syscall reaches it first, because
 * sock_error( ) takes it with an exchange: send( ) first returns ECONNRESET and leaves the
 * following recv( ) with a plain end of stream; recv( ) first returns ECONNRESET and leaves the
 * following send( ) with EPIPE. broken_pipe and connection_reset on a write are therefore one
 * event seen from the two sides of one race, which is why net::isPeerClosedOnWriteErrorCode( )
 * admits both rather than choosing between them.
 *
 * WHAT THE PEER DOES IS NOT A CHOICE. It answers from the request head - a 413, a 401, or as here
 * a response whose body it never finishes - and then closes with our upload still unread, which
 * puts a RST on the wire on every platform (RFC 2525 section 2.17).
 *
 * AND THE SAME EXCHANGE UNDER THE OTHER FRAMING IS A SECOND DEFECT, WHICH A1-CLEARTEXT FIXES AND
 * WHICH THE TWO CASES AT THE END OF THIS FILE PIN. A2 declares Content-Length so that its red is
 * about the write arm alone; with the response framed by the CLOSE instead, the eof the write left
 * behind was handed to parseEof( ) and a body cut short by a RST was reported to the caller as a
 * complete 200. That is design section 12.5's face 3 - the read consults what the write recorded,
 * and defers to it while a write is still in flight - and its red is this exchange with the length
 * taken off.
 */

namespace utest
{
    namespace http1writeclose
    {
        enum : std::size_t
        {
            /**
             * @brief The request body which cannot reach the peer
             *
             * Above the sum of a default Linux send buffer and the minimum receive buffer asked
             * for below, so the write PARKS in the reactor rather than merely staying outstanding
             * - the same instrument and the same size the write-barrier cases use
             */

            BLOCKED_BODY_SIZE                   = 8U * 1024U * 1024U,

            /**
             * @brief What the peer asks for as its receive buffer
             */

            PEER_RECEIVE_BUFFER_SIZE            = 2048U,

            /**
             * @brief What the response DECLARES and never delivers
             *
             * Framed by Content-Length and not by the close, deliberately. A close-delimited body
             * would make the ending's own CODE decide whether the message may be declared
             * complete, which is onPeerClosed( )'s subject and not this case's; with an explicit
             * length a short body is incomplete however the stream ended, so the assertion on the
             * error code says the same thing on every platform.
             *
             * AND THE CLOSE-DELIMITED SHAPE OF THIS EXCHANGE IS A SECOND DEFECT, WHICH A2 NEITHER
             * MADE NOR MENDED - which is the second reason and the load-bearing one. MEASURED,
             * both sides, 8 runs each on a64 clang debug: with the same peer and no
             * Content-Length, a 16-octet body cut short by a RST is reported to the caller as a
             * COMPLETE 200 - closed:ok, no error - 8 times in 8 WITH this driver's write arm and
             * 7 times in 8 without it. The cause is upstream of both: the write's send( ) consumes
             * the reset, so the read is handed a plain end of stream, isCleanEndOfStream( ) says
             * yes and parseEof( ) completes the message
             *
             * A1-CLEARTEXT FIXES IT, AND ITS RED IS THIS EXCHANGE WITH THE LENGTH TAKEN OFF -
             * Http1Driver_PeerResetsMidCloseDelimitedBodyDuringWriteTests below, which is why
             * runResetDuringBlockedUpload( ) takes the framing as a parameter rather than being
             * copied. A2's own case keeps the declared length, so that a future red HERE is about
             * the write arm and about nothing else
             */

            DECLARED_BODY_LENGTH                = 64U,

            /**
             * @brief What the peer has recorded once it has reset
             *
             * rcvbuf, head, chunk-two, linger, reset - the last of them taken under the peer's own
             * lock AFTER close( ) returned, which is the rendezvous the sink waits on
             */

            PEER_RECORDS_AFTER_RESET            = 5U,

            /**
             * @brief How long the driver is left with a FREE strand before the peer is released
             *
             * WHAT IT BOUNDS IS THE COMPOSED WRITE REACHING A FULL PEER WINDOW, and it is the one
             * bound in this case with no rendezvous available anywhere: nothing outside the
             * reactor can be asked whether a write is parked, and the peer cannot see it either
             * because the only thing that would tell it is reading, which is what unparks us.
             *
             * IT IS NOT ONE HANDLER BUT MANY. The peer's window is closed, so the send buffer
             * drains only as fast as the stack lets it grow - each async_write_some takes what
             * fits and hands the strand back, so a couple of megabytes of body go out over a long
             * run of handlers. Fifty milliseconds was enough at idle and measurably NOT enough
             * beside a compile, which is what this number is set against.
             *
             * WHAT GOES WRONG IF IT IS TOO SHORT IS A GREEN RUN, NEVER A FALSE RED. An unparked
             * write has no op registered - its next step is a handler queued on the strand this
             * case is holding - so the reset finds nothing to fail, and the read reports the
             * ending first. That is the arrangement not being set up, and it can only make a tree
             * without the arm look correct
             */

            WRITE_PARKS_IN_MILLISECONDS         = 500U,

            /**
             * @brief How long the strand is held AFTER the peer says it has reset
             *
             * NOT A RENDEZVOUS IN PLACE OF ONE - the rendezvous is the peer's own record, taken
             * under its lock once close( ) has returned, so the RST is on the wire before this
             * begins. What is left to bound is one thread waking from epoll_wait and making one
             * send( ) call, and nothing outside the reactor can be waited on for that.
             *
             * IT CANNOT PRODUCE A FALSE RED. Too short, and the write op has not been enqueued on
             * the strand before scheduleRead( ) re-arms - which puts the case back on the dispatch
             * race this note rejects. The symptom of that is a run which goes GREEN against a tree
             * without the arm, never a run which fails against a tree with it
             */

            STRAND_HELD_AFTER_RESET_IN_MILLISECONDS = 50U,
        };

        /**
         * @brief Reads the request HEAD and stops at the blank line, leaving the body unread
         *
         * ScriptedPeer::readRequest( ) reads the body its Content-Length declares, which is the
         * opposite of what this case needs: the upload must still be outstanding when the peer
         * goes away, or there is no write for the close to reach
         */

        inline auto readRequestHead( SAA_inout bl::asio::ip::tcp::socket& socket ) -> std::string
        {
            std::string data;

            char buffer[ 1024 ];

            while( std::string::npos == data.find( "\r\n\r\n" ) )
            {
                bl::eh::error_code ec;

                const auto transferred =
                    socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                if( ec || 0U == transferred )
                {
                    break;
                }

                data.append( buffer, transferred );
            }

            return data;
        }

        /**
         * @brief A sink which holds the driver's strand across the peer's reset, once, on demand
         *
         * THE ONE DOOR ONTO THE INSIDE OF A READ HANDLER. The driver calls a sink from its own
         * strand, synchronously, in the middle of onBytesRead( ) - so a sink which has not
         * returned is a read handler which has not returned, and that is a connection with no read
         * op registered. Everything else here is bookkeeping.
         *
         * IT DOES NOT CALL BACK INTO THE CONNECTION, which is what design 5.2 rule L3 forbids of a
         * sink. It waits on the peer's own lock and returns. It holds the strand exactly once,
         * because a second hold would be against the very handlers this case is waiting to see
         * run, and it holds it only after the case has ARMED it - the first body chunk must be
         * delivered with the strand free, or the composed write never reaches its parked state.
         *
         * IT DELEGATES RATHER THAN DERIVES so the case keeps the ordinary RecordingSink, with
         * every accessor and rendezvous on it unchanged
         */

        class ResetWhileStrandIsHeldSink : public bl::httpclient::ClientStreamEventSink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE(
                ResetWhileStrandIsHeldSink,
                bl::httpclient::ClientStreamEventSink
                )

        protected:

            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

            bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >             m_inner;
            bl::cpp::function< void () >                                        m_gate;

            mutable bl::os::mutex                                               m_lock;
            bool                                                                m_armed;
            bool                                                                m_gatePassed;

            ResetWhileStrandIsHeldSink()
                :
                m_armed( false ),
                m_gatePassed( false )
            {
            }

            bool chkTakeGate()
            {
                BL_MUTEX_GUARD( m_lock );

                if( ! m_armed || m_gatePassed )
                {
                    return false;
                }

                m_gatePassed = true;

                return true;
            }

        public:

            /**
             * @brief Handed over before the task is scheduled, so nothing here races the strand
             */

            void attachTo(
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& inner,
                SAA_in          bl::cpp::function< void () >&&                   gate
                )
            {
                m_inner = bl::om::copy( inner );
                m_gate = BL_PARAM_FWD( gate );
            }

            /**
             * @brief Arms the hold, from the test thread, before the peer is released
             *
             * Under the same lock the strand reads it, so the chunk which trips it is the one the
             * peer sends AFTER this - never one already in flight
             */

            void armGate()
            {
                BL_MUTEX_GUARD( m_lock );

                m_armed = true;
            }

            virtual void onBodyWanted(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                m_inner -> onBodyWanted( handle, bytes );
            }

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                m_inner -> onHeaders( handle, status, BL_PARAM_FWD( headers ), isInterim );
            }

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                /*
                 * BEFORE THE DELIVERY AND NOT AFTER IT, so that the case's own rendezvous on the
                 * body cannot return while this is still holding the strand
                 */

                if( chkTakeGate() )
                {
                    m_gate();
                }

                m_inner -> onData( handle, data );
            }

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          bl::http::HeaderList&&                          trailers
                ) OVERRIDE
            {
                m_inner -> onTrailers( handle, BL_PARAM_FWD( trailers ) );
            }

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                m_inner -> onClosed( handle, errorCode, isRetryable );
            }
        };

        typedef bl::om::ObjectImpl< ResetWhileStrandIsHeldSink >                ResetWhileStrandIsHeldSinkImpl;

        /**
         * @brief What the exchange settled on, read on the test thread
         */

        struct WritePeerCloseResult
        {
            bl::eh::error_code                                                  errorCode;
            bl::httpclient::ConnectionState                                     state;
            unsigned                                                            status;
            std::string                                                         body;
            std::string                                                         events;
            std::string                                                         peerRecords;
            bool                                                                closed;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            WritePeerCloseResult()
                :
                state( bl::httpclient::ConnectionState::Closed ),
                status( 0U ),
                closed( false ),
                taskFailed( false )
            {
            }
        };

        /**
         * @brief One POST whose body the peer never reads, half answered and then reset
         *
         * THE FOUR STEPS, AND EVERY ONE OF THEM IS BEHIND A RENDEZVOUS OR A STATED BOUND.
         *
         *   1. the peer answers from the request head with the first body chunk, and stops reading
         *   2. the case waits for that chunk to reach the sink - a happens-before with the driver -
         *      and then leaves the strand alone while the composed write fills the peer's window
         *      and parks in the reactor
         *   3. the peer is released; it sends the second chunk and resets. The sink takes the
         *      strand on that chunk and holds it until the peer records that close( ) returned
         *   4. so the reset finds a write op and no read op, which is the whole arrangement
         *
         * Every field the assertions read is taken after the sink says the stream ended, and THAT
         * is itself the first thing this case measures: under the shape this change takes, the
         * write handler says nothing at all - so if a peer close which failed the write were not
         * also reported to the pending read, this stream would never end, and waitForClosed( )
         * says so rather than the case passing for a reason nobody checked
         *
         * 'isCloseDelimited' IS THE ONE THING THE TWO CASES DIFFER BY, and it changes exactly one
         * string: the response head the peer sends, with or without its Content-Length. The
         * arrangement above is identical for both, which is the point of parameterizing it -
         * A1-cleartext's red needs precisely A2's ordering, and an arrangement copied into a
         * second function is an arrangement that drifts
         */

        inline auto runResetDuringBlockedUpload(
            SAA_in          const bool                                          isCloseDelimited = false
            )
            -> WritePeerCloseResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            WritePeerCloseResult result;

            const std::string chunkOne( "part-one" );
            const std::string chunkTwo( "part-two" );

            ScriptedPeer peer(
                [ &chunkOne, &chunkTwo, isCloseDelimited ](
                    SAA_inout   ScriptedPeer&                                   self,
                    SAA_inout   asio::ip::tcp::socket&                          socket
                    ) -> void
                {
                    eh::error_code ec;

                    socket.set_option(
                        asio::socket_base::receive_buffer_size(
                            static_cast< int >( PEER_RECEIVE_BUFFER_SIZE )
                            ),
                        ec
                        );

                    self.record( ec ? "rcvbuf:failed" : "rcvbuf:set" );

                    const auto head = readRequestHead( socket );

                    self.record( "head:" + ScriptedPeer::requestLineOf( head ) );

                    /*
                     * A response which has begun and cannot finish - the header section is
                     * complete and the body will stay short of what it declared. It says nothing
                     * about the connection, so nothing in the response itself asks this driver to
                     * close and the read stays armed for a body which never comes
                     *
                     * WITHOUT THE LENGTH THE CLOSE IS THE ONLY FRAMING THERE IS, which is the
                     * shape RFC 9112 section 6.3 leaves a client nothing to check against - so
                     * whether this message may be declared complete is decided entirely by HOW
                     * the byte stream ended, and that is A1-cleartext's subject
                     */

                    ScriptedPeer::send(
                        socket,
                        isCloseDelimited ?
                            "HTTP/1.1 200 OK\r\n"
                            "\r\n" +
                            chunkOne
                            :
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Length: " +
                            utils::lexical_cast< std::string >(
                                static_cast< std::size_t >( DECLARED_BODY_LENGTH )
                                ) +
                            "\r\n"
                            "\r\n" +
                            chunkOne
                        );

                    /*
                     * AND NOW IT STOPS READING, with most of an 8MB upload still in its receive
                     * queue, until the case has seen the first chunk and given the write time to
                     * park
                     */

                    self.waitForRelease();

                    ScriptedPeer::send( socket, chunkTwo );

                    self.record( "chunk-two:sent" );

                    /*
                     * SO_LINGER( on, 0 ) AND NO SHUTDOWN FIRST, exactly as the peer-close cases do
                     * it: a shutdown would put a FIN in front of the reset and the driver would
                     * see the orderly close this case is not about. With the upload unread the
                     * close would put a RST on the wire anyway, and asking for it outright is what
                     * makes that independent of how much of it the stack absorbed
                     */

                    socket.set_option( asio::socket_base::linger( true, 0 ), ec );

                    self.record( ec ? "linger:failed" : "linger:set" );

                    socket.close( ec );

                    /*
                     * THE RECORD THE SINK IS WAITING FOR, taken under the peer's lock after
                     * close( ) has returned - so a sink which returns from waitForRecords( ) has a
                     * happens-before with the RST reaching the stack
                     */

                    self.record( "reset" );
                }
                );

            const auto sink = RecordingSinkImpl::createInstance();
            const auto gated = ResetWhileStrandIsHeldSinkImpl::createInstance();

            gated -> attachTo(
                om::qi< httpclient::ClientStreamEventSink >( sink ),
                [ &peer ]() -> void
                {
                    ( void ) peer.waitForRecords(
                        static_cast< std::size_t >( PEER_RECORDS_AFTER_RESET )
                        );

                    os::sleep(
                        time::milliseconds(
                            static_cast< long >( STRAND_HELD_AFTER_RESET_IN_MILLISECONDS )
                            )
                        );
                }
                );

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &gated, &result, &chunkOne ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    auto request = makeRequest( peer.port(), "/half-answered", "POST" );

                    const auto block =
                        data::DataBlock::createInstance(
                            static_cast< std::size_t >( BLOCKED_BODY_SIZE )
                            );

                    std::memset( block -> pv(), 'x', static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

                    block -> setSize( static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

                    request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( gated )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    /*
                     * THE RENDEZVOUS, and it carries two facts: the header block and the first
                     * body chunk reached the sink, so the driver holds a live parser on a response
                     * it has begun - which is what the reset below has to find
                     */

                    chkOrFail(
                        sink -> waitForBodyAtLeast( chunkOne.size() ),
                        "the first body chunk never reached the sink; events so far: " +
                            joinEvents( sink -> events() )
                        );

                    os::sleep(
                        time::milliseconds(
                            static_cast< long >( WRITE_PARKS_IN_MILLISECONDS )
                            )
                        );

                    gated -> armGate();

                    peer.release();

                    result.closed = sink -> waitForClosed();

                    chkOrFail(
                        result.closed,
                        "the stream never ended - a peer close which failed the write was not "
                        "reported to the pending read; events so far: " +
                            joinEvents( sink -> events() )
                        );

                    /*
                     * READ HERE, which is where the pool reads it: releaseStream( ) runs inside
                     * the request task's handling of onClosed and asks this connection whether it
                     * may be reused
                     */

                    result.state = driver -> state();

                    eq -> wait( driverTask );

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = taskFailureText( driverTask );

                    /*
                     * Discarded here rather than left to the outer flush, which turns a failed
                     * task into an exception out of the harness - and whether the task failed is
                     * THE assertion of this case, so a red run has to reach it to be evidence
                     */

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            result.errorCode = sink -> errorCode();
            result.status = sink -> finalStatus();
            result.body = sink -> body();
            result.events = joinEvents( sink -> events() );
            result.peerRecords = joinEvents( peer.records() );

            return result;
        }

        /**
         * @brief One POST whose body the peer never reads, half answered and then HALF CLOSED
         *
         * THE CONTROL THE DEFERRAL OWES, AND NOT A SECOND RED. Design section 12.5's third part
         * has the read hand its ending to the write handler whenever it observes one with a write
         * still in flight - so the two things that shape has to be held to are that the ending
         * still ARRIVES, and that a message the peer framed with a FIN is still completed. Today's
         * tree passes this case as well, by delivering from the read handler; what it pins is that
         * the deferral did not turn either answer into a hang or a truncation.
         *
         * THE INTERLEAVING IS CERTAIN AND NEEDS NO STRAND HELD. The peer reads the request HEAD
         * and then stops, so an 8MB body cannot complete however long it is given - the write is
         * in flight when the FIN arrives, on every run and on every machine. That is the whole
         * arrangement, and it is why this case has no gate, no bound and no sleep.
         *
         * WHAT WAKES THE WRITE IS OUR OWN TEARDOWN, which is the no-hang argument made observable:
         * the read's closeConnection( ) reaches initiateClose( ) through the epilog, initiateClose( )
         * shuts the send side down for a write in flight, and the parked write completes EPIPE.
         * A deferral waiting on a handler nothing would ever run is exactly what waitForClosed( )'s
         * bound turns into a diagnosis here.
         *
         * shutdown_send AND NOT close( ), and the difference is the whole case: a close with our
         * upload unread puts a RST on the wire (RFC 2525 section 2.17) and that is the OTHER case
         * above. Here the peer half closes and keeps its receive side open, so what the driver
         * sees is a plain FIN and the message it framed is complete
         */

        inline auto runHalfCloseDuringBlockedUpload() -> WritePeerCloseResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            WritePeerCloseResult result;

            const std::string chunkOne( "part-one" );

            ScriptedPeer peer(
                [ &chunkOne ](
                    SAA_inout   ScriptedPeer&                                   self,
                    SAA_inout   asio::ip::tcp::socket&                          socket
                    ) -> void
                {
                    eh::error_code ec;

                    socket.set_option(
                        asio::socket_base::receive_buffer_size(
                            static_cast< int >( PEER_RECEIVE_BUFFER_SIZE )
                            ),
                        ec
                        );

                    self.record( ec ? "rcvbuf:failed" : "rcvbuf:set" );

                    const auto head = readRequestHead( socket );

                    self.record( "head:" + ScriptedPeer::requestLineOf( head ) );

                    /*
                     * Framed by the close and by nothing else, so that what the FIN means for this
                     * message is the whole of what the case asserts
                     */

                    ScriptedPeer::send(
                        socket,
                        "HTTP/1.1 200 OK\r\n"
                        "\r\n" +
                        chunkOne
                        );

                    /*
                     * THE FIN, WITH THE UPLOAD STILL UNREAD IN ITS QUEUE. shutdown_send leaves the
                     * receive side open, which is what keeps those octets from becoming a RST
                     */

                    socket.shutdown( asio::ip::tcp::socket::shutdown_send, ec );

                    self.record( ec ? "fin:failed" : "fin:sent" );

                    /*
                     * AND IT HOLDS THE CONNECTION until the case has its verdict. The close which
                     * ends this script has our unread upload behind it and would put a RST on the
                     * wire - after the stream has ended it can harm nothing, and before it would
                     * make this the other case
                     */

                    self.waitForRelease();
                }
                );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &result ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    auto request = makeRequest( peer.port(), "/half-closed", "POST" );

                    const auto block =
                        data::DataBlock::createInstance(
                            static_cast< std::size_t >( BLOCKED_BODY_SIZE )
                            );

                    std::memset( block -> pv(), 'x', static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

                    block -> setSize( static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

                    request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    result.closed = sink -> waitForClosed();

                    chkOrFail(
                        result.closed,
                        "the stream never ended - a read which deferred its ending to the write "
                        "handler was left waiting for a write nothing woke; events so far: " +
                            joinEvents( sink -> events() )
                        );

                    /*
                     * READ BEFORE THE PEER IS LET GO, for the reason the other case states: this
                     * is where the pool reads it
                     */

                    result.state = driver -> state();

                    peer.release();

                    eq -> wait( driverTask );

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = taskFailureText( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            result.errorCode = sink -> errorCode();
            result.status = sink -> finalStatus();
            result.body = sink -> body();
            result.events = joinEvents( sink -> events() );
            result.peerRecords = joinEvents( peer.records() );

            return result;
        }

    } // http1writeclose

} // utest

UTF_AUTO_TEST_CASE( Http1Driver_PeerResetsWhileRequestWriteIsBlockedTests )
{
    using namespace bl;
    using namespace utest::http1writeclose;

    const auto result = runResetDuringBlockedUpload();

    UTF_REQUIRE( result.closed );

    /*
     * THE RESPONSE THE PEER DID BEGIN IS STILL THE ONE THE CALLER IS TOLD ABOUT. The write handler
     * reaches the ending first here by construction, and under this change it says nothing - so
     * the read keeps its parser and everything the peer sent is delivered. A write handler which
     * ended the stream itself would have reset that parser instead
     */

    UTF_REQUIRE_EQUAL( result.status, 200U );

    UTF_REQUIRE_EQUAL( result.body, std::string( "part-onepart-two" ) );

    /*
     * AND IT IS STILL A FAILED STREAM. The peer declared a body it did not deliver, so what the
     * caller gets is the truncation and not a success - the write arm excuses the TASK and excuses
     * nothing about the message
     */

    UTF_REQUIRE( result.errorCode );

    /*
     * NOT REUSABLE. The peer is gone, and the pool asks this the moment onClosed lands
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );

    /*
     * THE ASSERTION THIS CASE EXISTS FOR, AND THE ONE IT WAS WRITTEN RED AGAINST. Before the write
     * arm landed, onWriteCompleted( ) handed the peer's reset to BL_TASKS_HANDLER_CHK_EC( ) and
     * this task failed with it. A peer hanging up is an ordinary end of a conversation and not a
     * fault of ours. The message carries both codes, because a future red here has to say which
     * side reached the ending first before anything else can be concluded from it
     */

    utest::http1driver::chkOrFail(
        ! result.taskFailed,
        "the peer hanging up during the request write failed the driver task: " +
            result.taskFailure + "; the stream ended with '" + result.errorCode.message() +
            "'; events: " + result.events + "; peer: " + result.peerRecords
        );
}

UTF_AUTO_TEST_CASE( Http1Driver_PeerResetsMidCloseDelimitedBodyDuringWriteTests )
{
    using namespace bl;
    using namespace utest::http1writeclose;

    /*
     * A1-CLEARTEXT, FACE 3 - THE RESET THE WRITE CONSUMED, AND THE WORST ANSWER THIS DRIVER COULD
     * GIVE: a truncated response handed to the caller as a complete, successful 200.
     *
     * THE SAME EXCHANGE AS THE CASE ABOVE, WITH THE LENGTH TAKEN OFF. One RST sets the socket's
     * pending error once and the first syscall to reach it takes it away; the arrangement above
     * makes that syscall the WRITE's, which leaves the pending read a plain end of stream.
     * isCleanEndOfStream( ) admits eof on purpose - it is how a close-delimited message is framed
     * at all - so parseEof( ) completed a 16-octet body the peer had not finished, and the caller
     * was told closed:ok with no way to tell. Measured 8 of 8 on a64 clang debug, on the tree with
     * the write arm and on the tree without it.
     *
     * WHAT THE FIX RESTS ON IS THAT THE WRITE'S CODE IS EVIDENCE THE READ'S IS NOT. tcp_reset( )
     * writes ECONNRESET only from a state which has received no FIN and EPIPE from CLOSE_WAIT, and
     * tcp_fin( ) sets SOCK_DONE - so a write which completed connection_reset PROVES the ending
     * was a reset with no FIN before it, and the read's eof is what that reset left behind rather
     * than an orderly close. onPeerClosed( ) therefore consults what the write recorded, and the
     * ending is unclean with the WRITE's code.
     *
     * THE RED IS THE ERROR CODE ASSERTION AND NOTHING ELSE. Everything else here passed before the
     * fix too - which is what makes this the dangerous class of defect rather than a visible one
     */

    const auto result = runResetDuringBlockedUpload( true /* isCloseDelimited */ );

    UTF_REQUIRE( result.closed );

    /*
     * EVERYTHING THE PEER DID SEND IS STILL DELIVERED. The fix decides how the message ENDS and
     * takes nothing away from it - a caller told the transfer was cut short still gets the octets
     * it did receive, exactly as the read-first case above already had it
     */

    UTF_REQUIRE_EQUAL( result.status, 200U );

    UTF_REQUIRE_EQUAL( result.body, std::string( "part-onepart-two" ) );

    /*
     * NOT REUSABLE, for the same reason and read at the same moment as in the case above
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );

    /*
     * AND THE TASK STILL DOES NOT FAIL. A2's arm is what makes this true, and it is asserted here
     * as well because the two answers are independent: the stream's verdict is what the caller
     * gets, the task's is what the pool gets, and a change which fixed one by breaking the other
     * would be no fix at all
     */

    utest::http1driver::chkOrFail(
        ! result.taskFailed,
        "the peer hanging up during the request write failed the driver task: " +
            result.taskFailure + "; events: " + result.events + "; peer: " + result.peerRecords
        );

    /*
     * THE ASSERTION THIS CASE EXISTS FOR, AND THE ONE IT WAS WRITTEN RED AGAINST. A close-delimited
     * body has no length to check against, so if the ending is declared clean the message is
     * declared COMPLETE - and the caller has nothing left to consult. The message carries what the
     * stream actually ended with, because a future red here must say whether the verdict went
     * missing or merely changed
     *
     * AND IT IS THE TRANSPORT'S OWN CODE, NOT MERELY A NON-EMPTY ONE, which is the contract
     * onPeerClosed( ) states: a regression to the write handler's broken_pipe, or to a parser
     * code, would satisfy 'non-empty' and break that contract silently. Asked of net:: rather than
     * compared here, so it holds on every platform
     */

    utest::http1driver::chkOrFail(
        bl::net::isPeerClosedErrorCode( result.errorCode ),
        "a close-delimited body cut short by a RST was reported to the caller as a complete "
        "response: status " + utils::lexical_cast< std::string >( result.status ) +
            ", body '" + result.body + "', events: " + result.events +
            "; peer: " + result.peerRecords
        );
}

UTF_AUTO_TEST_CASE( Http1Driver_PeerHalfClosesWithAWriteInFlightTests )
{
    using namespace bl;
    using namespace utest::http1writeclose;

    /*
     * A1-CLEARTEXT'S CONTROL - THE FIN CASE THE DEFERRAL MUST NOT BREAK.
     *
     * The read observes the ending with the write still in flight, which is the interleaving the
     * deferral exists for: the write handler has not run, it is guaranteed to, and a record not yet
     * written is a record the read cannot consult. So the read hands its ending over and the WRITE
     * handler delivers it - here with a code that is not a reset (our own shutdown wakes the parked
     * write with EPIPE), so the read's own eof decides and the message the FIN framed is complete.
     *
     * IT IS NOT A RED, AND SAYING SO IS PART OF THE EVIDENCE. Today's tree completes this message
     * too, from the read handler. What would fail here is a deferral that waits on a handler
     * nothing wakes - a HANG, reported by waitForClosed( )'s bound - or one that lets the write's
     * broken_pipe decide, which would turn a complete response into a failure. Both are what this
     * change-set could plausibly have got wrong, and neither can be seen from the other case
     */

    const auto result = runHalfCloseDuringBlockedUpload();

    utest::http1driver::chkOrFail(
        result.closed,
        "the stream never ended; events: " + result.events + "; peer: " + result.peerRecords
        );

    /*
     * THE FIN REACHED THE WIRE, which is what makes this the half-close case rather than an
     * accident of the peer's own teardown
     */

    utest::http1driver::chkOrFail(
        std::string::npos != result.peerRecords.find( "fin:sent" ),
        "the peer did not half close: " + result.peerRecords
        );

    UTF_REQUIRE_EQUAL( result.status, 200U );

    UTF_REQUIRE_EQUAL( result.body, std::string( "part-one" ) );

    /*
     * COMPLETE, AND THAT IS THE ASSERTION. A close-delimited message is framed by the close, and
     * this one ended with the peer's own orderly FIN - so declaring it complete is the RIGHT answer
     * and the write's broken_pipe must not be allowed to override it
     */

    utest::http1driver::chkOrFail(
        ! result.errorCode,
        "a close-delimited response framed by the peer's own FIN was reported as failed with '" +
            result.errorCode.message() + "'; events: " + result.events +
            "; peer: " + result.peerRecords
        );

    /*
     * NOT REUSABLE - the peer ended the conversation, whichever way it ended it
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );

    utest::http1driver::chkOrFail(
        ! result.taskFailed,
        "a peer half closing during the request write failed the driver task: " +
            result.taskFailure + "; events: " + result.events + "; peer: " + result.peerRecords
        );
}

#endif /* __UTEST_TESTHTTP1DRIVERWRITEPEERCLOSE_H_ */
