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

#ifndef __UTEST_TESTHTTP1DRIVERWRITEBARRIER_H_
#define __UTEST_TESTHTTP1DRIVERWRITEBARRIER_H_

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S6R.2 H01 - the write completion barrier of the HTTP/1.1 driver
 *
 * THE DEFECT IS NOT A CROSS-THREAD RACE, AND THAT IS WHAT MAKES IT TESTABLE. Every handler and
 * every post of this driver runs on the socket's executor, which under the stranded policies is
 * the strand - the class comment narrows the driver's correctness to exactly those. A composed
 * asio::async_write returns to that executor between its internal async_write_some calls, so the
 * READ handler can run in the middle of one write, on one thread, deterministically. That is the
 * interleaving these two cases produce on purpose, with no fault injection and no fake stream.
 *
 * HOW THE WRITE IS HELD OPEN, AND WHY NOT BY CHOOSING A LARGE NUMBER AND HOPING. The peer shrinks
 * its own receive buffer to the minimum the stack will take and then STOPS READING, having read
 * only the request head; the request body is then larger than the peer's window plus whatever our
 * send buffer will absorb, so the write cannot complete until the peer reads - which it never
 * does. The peer answers from the head alone, which is what a real 413 or a real 401 does.
 *
 * NEITHER CASE ASSERTS ON A CRASH, which is what makes them worth their green. The faces of H01
 * are a second overlapping write on one stream, a dropped reference to the caller's DataBlock and
 * a mutated head - all of them undefined behaviour, none of them reliably observable. What IS
 * observable is the verdict the driver publishes while a write is still in flight, and both cases
 * assert on that
 */

namespace utest
{
    namespace http1barrier
    {
        enum : std::size_t
        {
            /**
             * @brief The request body which cannot reach the peer
             *
             * Comfortably above the sum of a default Linux send buffer and the minimum receive
             * buffer set below, so the write is still in flight when the response arrives. It
             * costs one allocation of this size per case and nothing else
             */

            BLOCKED_BODY_SIZE                   = 8U * 1024U * 1024U,

            /**
             * @brief What the peer asks for as its receive buffer
             *
             * The stack rounds this up to its own minimum, which is the point - the request is
             * for "as small as you will give me" and the assertions do not depend on the number
             */

            PEER_RECEIVE_BUFFER_SIZE            = 2048U,
        };

        /**
         * @brief Reads the request HEAD and stops at the blank line, leaving the body unread
         *
         * ScriptedPeer::readRequest( ) reads the body its Content-Length declares, which is the
         * opposite of what these cases need: the whole point is a peer which answers without ever
         * taking the upload
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
         * @brief What the blocked-upload exchange settled on, read on the test thread
         */

        struct BlockedUploadResult
        {
            bl::httpclient::ConnectionState                                     state;
            std::size_t                                                         freeSlots;
            unsigned                                                            status;
            bool                                                                closed;
            bool                                                                taskFailed;
            bl::httpclient::stream_handle_t                                     secondHandle;

            BlockedUploadResult()
                :
                state( bl::httpclient::ConnectionState::Closed ),
                freeSlots( 0U ),
                status( 0U ),
                closed( false ),
                taskFailed( false ),
                secondHandle( bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE )
            {
            }
        };

        /**
         * @brief One POST whose body the peer never reads, answered from the head alone
         *
         * 'submitAgain' is the overlap face of H01: a second submit( ) offered to the driver the
         * moment the first stream ended. It is a question the case asks of the driver and not
         * something the harness needs, so it is off by default
         *
         * THE TASK IS WAITED FOR RATHER THAN ABANDONED. With the barrier in place the driver
         * refuses to reuse the connection, closeConnection( ) begins the close, and the read
         * handler's own epilog runs initiateClose( ) - which cancels the socket and so completes
         * the very write the barrier refused to wait for. That chain is what makes the barrier a
         * barrier rather than a deadlock, and waiting for the task here is what would catch it
         * if it ever stopped being true
         */

        inline auto runBlockedUpload( SAA_in const bool submitAgain = false ) -> BlockedUploadResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            BlockedUploadResult result;

            ScriptedPeer peer(
                []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
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
                     * A complete final response which says nothing about the connection, so that
                     * deriveIsReusable( ) has every reason to call this connection reusable and
                     * the ONLY thing standing in its way is the write still in flight. Answering
                     * before the body arrived is exactly what a 413 is for
                     */

                    ScriptedPeer::send(
                        socket,
                        "HTTP/1.1 413 Payload Too Large\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n"
                        );

                    /*
                     * AND NOW IT STOPS READING. The upload is still coming, and the case reads
                     * the driver's verdict while it is
                     */

                    self.waitForRelease();
                }
                );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &result, submitAgain ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    auto request = makeRequest( peer.port(), "/blocked", "POST" );

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
                        "the stream never ended; events so far: " + joinEvents( sink -> events() )
                        );

                    /*
                     * READ BEFORE THE PEER IS RELEASED. A connection this driver may reuse is one
                     * it leaves open, so the verdict has to be read while the peer is still
                     * holding its end - the same rendezvous every reuse case in
                     * utf_baselib_httpclient3 uses, and for the same reason
                     */

                    result.state = driver -> state();
                    result.freeSlots = driver -> freeStreamSlots();
                    result.status = sink -> finalStatus();

                    if( submitAgain )
                    {
                        result.secondHandle = driver -> submit(
                            makeRequest( peer.port(), "/second", "GET" ),
                            om::qi< httpclient::ClientStreamEventSink >( sink )
                            );
                    }

                    peer.release();

                    eq -> wait( driverTask );

                    result.taskFailed = driverTask -> isFailed();

                    /*
                     * DISCARDED HERE rather than left to the outer flush, which turns a failed
                     * task into an exception out of the harness. Whether the driver task ended
                     * clean is an ASSERTION of the case below - the barrier must not leave a
                     * write nothing will wake - so it has to be read and reported rather than
                     * thrown, and a red run has to reach its own assertion to be evidence
                     */

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            return result;
        }

    } // http1barrier

} // utest

UTF_AUTO_TEST_CASE( Http1Driver_WriteInFlightRefusesReuseTests )
{
    using namespace bl;
    using namespace utest::http1barrier;

    /*
     * H01 - A CONNECTION WITH A WRITE STILL IN FLIGHT IS NOT REUSABLE, and this is the case which
     * says so. The 413 above is complete, carries no close token, needs no EOF, is not a 101 and
     * leaves nothing unconsumed, so deriveIsReusable( ) answers true on every input it has: the
     * only thing that can refuse this connection is the write still going out on it
     *
     * IT IS THE EXACT MIRROR OF A RULE THIS DRIVER ALREADY APPLIES. A response which left bytes
     * unconsumed makes the connection unusable, because those bytes are either unsolicited or a
     * second message smuggled behind the first. Request bytes still in OUR send buffer are the
     * same sentence with the arrow reversed: the server has not consumed them, and a second
     * request put behind them is read as this request's body
     */

    const auto result = runBlockedUpload();

    UTF_REQUIRE( result.closed );

    UTF_REQUIRE_EQUAL( result.status, 413U );

    /*
     * Draining or Closed - both are "not Ready", and which one it is depends only on whether the
     * task had already taken its terminal path when the state was read. Ready is the red
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );

    UTF_REQUIRE_EQUAL( result.freeSlots, 0U );

    /*
     * AND THE BARRIER IS NOT A DEADLOCK. Refusing reuse takes closeConnection( ), and the read
     * handler's own epilog then reaches initiateClose( ), which cancels the socket and completes
     * the pending write as operation_aborted - which the accounting excuses while the task is
     * closing deliberately. So the same handler that trips the barrier is the one that frees the
     * write, and the task ends clean. This assertion is what would catch that chain breaking
     */

    UTF_REQUIRE( ! result.taskFailed );
}

UTF_AUTO_TEST_CASE( Http1Driver_WriteInFlightRefusesASecondRequestTests )
{
    using namespace bl;
    using namespace utest::http1barrier;

    /*
     * H01's FIRST AND MOST CERTAIN FACE - the overlapping write. finishStream( ) clears the handle
     * and publishes Ready, and submit( ) gates on exactly those two, so without the barrier the
     * pool may hand this connection a second request while the first write is still pending:
     * onStartRequest( ) then assigns m_requestHead - which may reallocate the string under a
     * const_buffer the pending write still points at - and issues a SECOND composed write over a
     * stream that already has one. Asio forbids that outright, and under a TLS policy it corrupts
     * the record stream
     *
     * The handle is captured and asserted after the exchange rather than inside it, so that a red
     * run reports the defect instead of abandoning a driver with two writes on it
     */

    const auto result = runBlockedUpload( true /* submitAgain */ );

    UTF_REQUIRE( result.closed );

    UTF_REQUIRE(
        httpclient::ClientConnection::INVALID_STREAM_HANDLE == result.secondHandle
        );
}

#endif /* __UTEST_TESTHTTP1DRIVERWRITEBARRIER_H_ */
