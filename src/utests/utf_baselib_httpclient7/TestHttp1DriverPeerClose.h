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

#ifndef __UTEST_TESTHTTP1DRIVERPEERCLOSE_H_
#define __UTEST_TESTHTTP1DRIVERPEERCLOSE_H_

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S6R.2 11a and N2 - how an HTTP/1.1 conversation is allowed to END
 *
 * THREE SHAPES OF AN END OF STREAM, AND THE LIBRARY USED TO HAVE ONE.
 *
 *   1. A close before a single response octet - a server dropping a request it will not serve,
 *      and the stale keep-alive race the retry rules exist for. Beast's put_eof( ) special cases
 *      only the start_line and fields states and the two framing flags, so a parser which has
 *      seen NOTHING is in neither and falls through to "complete, no error": the caller was told
 *      the request SUCCEEDED, with status 0 and no header block. In a debug build it never got
 *      that far - BOOST_ASSERT( got_some( ) ) took the process down instead. It is not one
 *      platform's behaviour and it is not close-delimited framing: it happens on eof, everywhere
 *   2. The same thing after an interim 1xx, because fileInterimAndRestart( ) installs a FRESH
 *      backend which has also seen nothing. A response with no final status line is not a
 *      response
 *   3. A close-delimited body cut short by a RESET rather than by an orderly close. Here the
 *      message HAS begun, and HTTP/1.1 gives a client nothing to compare the length against - so
 *      whether it may be declared complete is decided entirely by HOW the byte stream ended.
 *      That is N2's second part, and it is why N2 could not ship as a one-line predicate swap
 *
 * WHY THE RESET CASE IS RUNNABLE ON LINUX AT ALL. N2's finding is a Windows one - there a peer
 * close during a full-duplex transfer arrives as connection_aborted and a close with unread data
 * as connection_reset, neither of which the old comparison admitted. What a POSIX host CAN
 * produce is the reset spelling itself, with SO_LINGER( on, 0 ): net::isPeerClosedErrorCode( )
 * admits connection_reset on every platform, so the case below exercises exactly the routing N2
 * introduces and exactly the completion rule that routing makes necessary. What it cannot
 * exercise is the Windows premise - that an ORDERLY close of a close-delimited response arrives
 * as eof there - and no test on this machine can
 */

namespace utest
{
    namespace http1close
    {
        /**
         * @brief What a peer-close exchange left behind, read on the test thread
         */

        struct PeerCloseResult
        {
            bl::eh::error_code                                                  errorCode;
            std::size_t                                                         blocks;
            std::string                                                         body;
            bool                                                                retryable;
            bool                                                                closed;
            bool                                                                taskFailed;

            PeerCloseResult()
                :
                blocks( 0U ),
                retryable( false ),
                closed( false ),
                taskFailed( false )
            {
            }
        };

        /**
         * @brief One request against a peer which ends the conversation rather than answering it
         *
         * 'bytesBeforeClose' is what the peer writes before it goes away, and 'waitForBodyOctets'
         * is the RENDEZVOUS: when it is non-zero the peer holds its end until the case has seen
         * that many body octets reach the sink, so that what happens next happens to a driver
         * which has already taken the bytes. 'resetInstead' closes with SO_LINGER( on, 0 ) and no
         * shutdown at all, which is a RST on the wire - a shutdown first would put a FIN in front
         * of it and the driver would see the orderly close this case is not about
         */

        inline auto runPeerCloseExchange(
            SAA_in          const std::string&                                  bytesBeforeClose,
            SAA_in          const std::size_t                                   waitForBodyOctets = 0U,
            SAA_in          const bool                                          resetInstead = false
            )
            -> PeerCloseResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            PeerCloseResult result;

            ScriptedPeer peer(
                [ &bytesBeforeClose, waitForBodyOctets, resetInstead ](
                    SAA_inout   ScriptedPeer&                                   self,
                    SAA_inout   asio::ip::tcp::socket&                          socket
                    ) -> void
                {
                    const auto request = ScriptedPeer::readRequest( socket );

                    self.record( "read:" + ScriptedPeer::requestLineOf( request ) );

                    if( ! bytesBeforeClose.empty() )
                    {
                        ScriptedPeer::send( socket, bytesBeforeClose );
                    }

                    if( 0U != waitForBodyOctets )
                    {
                        self.waitForRelease();
                    }

                    if( resetInstead )
                    {
                        eh::error_code ec;

                        socket.set_option( asio::socket_base::linger( true, 0 ), ec );

                        self.record( ec ? "linger:failed" : "linger:set" );

                        socket.close( ec );

                        return;
                    }

                    ScriptedPeer::closeSocket( socket );
                }
                );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &result, waitForBodyOctets ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    const auto handle = driver -> submit(
                        makeRequest( peer.port(), "/closed" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    if( 0U != waitForBodyOctets )
                    {
                        chkOrFail(
                            sink -> waitForBodyAtLeast( waitForBodyOctets ),
                            "the body never reached the sink; events so far: " +
                                joinEvents( sink -> events() )
                            );

                        peer.release();
                    }

                    result.closed = sink -> waitForClosed();

                    chkOrFail(
                        result.closed,
                        "the stream never ended; events so far: " + joinEvents( sink -> events() )
                        );

                    peer.release();

                    eq -> wait( driverTask );

                    result.taskFailed = driverTask -> isFailed();

                    /*
                     * Discarded here rather than left to the outer flush, which turns a failed
                     * task into an exception out of the harness - and whether the task failed is
                     * an assertion of the reset case below
                     */

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            result.errorCode = sink -> errorCode();
            result.retryable = sink -> isRetryable();
            result.blocks = sink -> blocks().size();
            result.body = sink -> body();

            return result;
        }

    } // http1close

} // utest

UTF_AUTO_TEST_CASE( Http1Driver_PeerClosesWithoutAnsweringTests )
{
    using namespace bl;
    using namespace utest::http1close;

    /*
     * 11a, SHAPE 1 - the peer reads the request and goes away without writing one octet.
     *
     * BEFORE THE CONVERSION THIS CASE COULD NOT EXIST: in a debug build it took the whole module
     * down on Beast's BOOST_ASSERT( got_some( ) ), and in a release build it asserted nothing
     * because the driver reported SUCCESS - status 0, no header block, no body. The red here is
     * the process dying, which is why this case and its conversion are one commit
     *
     * isRetryable is FALSE and that is deliberate rather than incidental. The whole request
     * reached the wire - the peer read it, which the recorded request line proves - so it is not
     * provably unsent, and replaying it would be a duplicate rather than a retry. Whether it is
     * retried anyway is then the POOL's decision under its own policy, which is what
     * m_requestMayHaveBeenSent's name and placement are load-bearing for
     */

    const auto result = runPeerCloseExchange( std::string() );

    UTF_REQUIRE( result.closed );

    UTF_REQUIRE( result.errorCode );

    UTF_REQUIRE_EQUAL( result.blocks, 0U );

    UTF_REQUIRE( ! result.retryable );
}

UTF_AUTO_TEST_CASE( Http1Driver_PeerClosesAfterEarlyHintsTests )
{
    using namespace bl;
    using namespace utest::http1close;

    /*
     * 11a, SHAPE 2 - a 103 Early Hints and then nothing. The parser files the interim and calls
     * makeBackend( ), which hands the final response a FRESH backend that has also seen no octet,
     * so the parser as a whole has seen a whole message and the CURRENT backend has not. That is
     * why the conversion asks the current backend rather than remembering whether the parser ever
     * saw anything
     *
     * No block reaches the sink at all, interim or final: this driver delivers the filed interims
     * together with the final header section, because the codec has no per-interim callback - so
     * a 103 with no final response behind it is exactly as much of a response as none at all
     */

    const auto result = runPeerCloseExchange(
        "HTTP/1.1 103 Early Hints\r\n"
        "Link: </style.css>; rel=preload\r\n"
        "\r\n"
        );

    UTF_REQUIRE( result.closed );

    UTF_REQUIRE( result.errorCode );

    UTF_REQUIRE_EQUAL( result.blocks, 0U );

    UTF_REQUIRE( ! result.retryable );
}

UTF_AUTO_TEST_CASE( Http1Driver_PeerResetsMidCloseDelimitedBodyTests )
{
    using namespace bl;
    using namespace utest::http1close;

    /*
     * N2, BOTH PARTS, AND THE ONLY CASE WHICH CAN PIN EITHER ON THIS PLATFORM.
     *
     * The response declares no length and no transfer coding, so its only framing is the
     * connection closing - needsEof( ). The peer then RESETS in the middle of it.
     *
     * THREE OUTCOMES, AND EACH ASSERTION BELOW REFUSES A DIFFERENT ONE:
     *
     *   - with neither part: connection_reset is not eof, so the old hand comparison sent it to
     *     BL_TASKS_HANDLER_CHK_EC( ) and the connection TASK failed. The caller saw a failure,
     *     which is the right answer reached the wrong way - and on Windows that is what happened
     *     to ORDINARY peer closes, about one time in eight. taskFailed is the assertion which
     *     refuses it
     *   - with part (a) alone: the reset now reaches onPeerClosed( ), parseEof( ) hands it to
     *     put_eof( ), and a close-delimited message with octets in it is COMPLETED - a truncated
     *     response reported to the caller as a SUCCESS, with no way to tell. A non-empty error
     *     code is the assertion which refuses that, and it is why (a) may not ship alone
     *   - with both: the transport's own code, the body that did arrive, and a task which ends
     *     clean because the conversation is over however it ended
     *
     * The body assertion is not decoration either: it is what says the driver had ALREADY taken
     * the octets when the reset landed, so the case is about the completion rule and not about
     * whose bytes the reset discarded
     */

    const std::string partial( "partial-body" );

    const auto result = runPeerCloseExchange(
        "HTTP/1.1 200 OK\r\n"
        "\r\n" + partial,
        partial.size(),
        true /* resetInstead */
        );

    UTF_REQUIRE( result.closed );

    UTF_REQUIRE_EQUAL( result.body, partial );

    UTF_REQUIRE( result.errorCode );

    UTF_REQUIRE( ! result.taskFailed );
}

#endif /* __UTEST_TESTHTTP1DRIVERPEERCLOSE_H_ */
