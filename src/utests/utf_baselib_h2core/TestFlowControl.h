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

#ifndef __UTEST_TESTFLOWCONTROL_H_
#define __UTEST_TESTFLOWCONTROL_H_

#include <baselib/http2/FlowControlWindow.h>
#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>

#include <utests/baselib/Utf.h>

/*
 * HTTP/2 flow control - RFC 9113 section 6.9, notes/plans/http2-design.md 4.4
 *
 * Three things are pinned here which an implementation gets wrong quietly rather than loudly:
 *
 *  - a window is SIGNED and a negative one is legal, so the RFC's own worked example from 6.9.2
 *    is transcribed here as a case of its own
 *  - the same fault is a stream error on a stream window and a connection error on the
 *    connection window, and the one exception to that rule is a SETTINGS change, where 6.9.2
 *    makes the overflow of a STREAM window a CONNECTION error
 *  - a WINDOW_UPDATE is due when the CONSUMER took the bytes, not when they arrived, which is
 *    what makes this backpressure rather than an unbounded buffer
 */

namespace utest
{
    namespace flowcontrol
    {
        /**
         * @brief Requires the exact RFC 9113 section 7 code and stream identifier on the throw
         *
         * The exception TYPE is the assertion about the level - Http2StreamException for a stream
         * error and Http2ProtocolException for a connection error - so it is named at every call
         * site rather than inferred, and an error raised at the wrong level escapes the catch and
         * fails the case rather than passing quietly
         */

        template
        <
            typename EXCEPTION,
            typename CALLABLE
        >
        void requireHttp2Error(
            SAA_in          const CALLABLE&                      callable,
            SAA_in          const std::uint32_t                  expectedErrorCode,
            SAA_in          const std::uint32_t                  expectedStreamId
            )
        {
            bool caught = false;

            try
            {
                callable();
            }
            catch( EXCEPTION& e )
            {
                caught = true;

                const auto* const errorCode =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_error_code >( e );

                const auto* const streamId =
                    bl::eh::get_error_info< bl::eh::errinfo_http2_stream_id >( e );

                UTF_REQUIRE( errorCode != nullptr );
                UTF_REQUIRE_EQUAL( *errorCode, expectedErrorCode );
                UTF_REQUIRE( streamId != nullptr );
                UTF_REQUIRE_EQUAL( *streamId, expectedStreamId );
            }

            UTF_REQUIRE( caught );
        }

    } // flowcontrol

} // utest

UTF_AUTO_TEST_CASE( FlowControl_WindowArithmeticTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * Both a new stream and the connection start at 65,535 octets - RFC 9113 6.9.2
     */

    {
        FlowControlWindow connection( Globals::STREAM_ID_CONNECTION );
        FlowControlWindow stream( 1U );

        UTF_REQUIRE_EQUAL( connection.size(), Globals::INITIAL_WINDOW_SIZE_DEFAULT );
        UTF_REQUIRE_EQUAL( stream.size(), Globals::INITIAL_WINDOW_SIZE_DEFAULT );
        UTF_REQUIRE_EQUAL( connection.size(), 65535 );

        UTF_REQUIRE( connection.isConnectionLevel() );
        UTF_REQUIRE( ! stream.isConnectionLevel() );
        UTF_REQUIRE_EQUAL( stream.streamId(), 1U );

        UTF_REQUIRE( ! connection.isExhausted() );
        UTF_REQUIRE_EQUAL( connection.available(), 65535 );
    }

    /*
     * consume( ) is what a DATA frame costs. The whole payload counts, padding included (6.1),
     * which is why the session passes the frame header's Length and not the size of the data
     */

    {
        FlowControlWindow window( 1U );

        window.consume( 1000 );

        UTF_REQUIRE_EQUAL( window.size(), 64535 );

        window.consume( 0 );

        UTF_REQUIRE_EQUAL( window.size(), 64535 );

        window.consume( 64535 );

        UTF_REQUIRE_EQUAL( window.size(), 0 );
        UTF_REQUIRE( window.isExhausted() );
        UTF_REQUIRE_EQUAL( window.available(), 0 );

        UTF_REQUIRE_THROW( window.consume( -1 ), UnexpectedException );
    }

    /*
     * take( ) is the write path's question: it never takes more than there is, so a DATA frame is
     * never written which the window cannot pay for
     */

    {
        FlowControlWindow window( 1U, 100 );

        UTF_REQUIRE_EQUAL( window.take( 30 ), 30 );
        UTF_REQUIRE_EQUAL( window.size(), 70 );

        UTF_REQUIRE_EQUAL( window.take( 1000 ), 70 );
        UTF_REQUIRE_EQUAL( window.size(), 0 );

        UTF_REQUIRE_EQUAL( window.take( 1000 ), 0 );
        UTF_REQUIRE_EQUAL( window.size(), 0 );

        UTF_REQUIRE_THROW( window.take( -1 ), UnexpectedException );
    }

    /*
     * A window cannot be constructed outside the range 6.9.2 allows, and a stream identifier
     * still has to fit 31 bits
     */

    {
        UTF_REQUIRE_THROW( FlowControlWindow( 1U, -1 ), UnexpectedException );
        UTF_REQUIRE_THROW(
            FlowControlWindow( Globals::MAX_STREAM_ID + 1U, 0 ),
            UnexpectedException
            );

        FlowControlWindow largest( 1U, Globals::MAX_FLOW_CONTROL_WINDOW_SIZE );

        UTF_REQUIRE_EQUAL( largest.size(), 2147483647 );

        FlowControlWindow empty( 1U, 0 );

        UTF_REQUIRE( empty.isExhausted() );
    }
}

UTF_AUTO_TEST_CASE( FlowControl_WindowUpdateTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::flowcontrol;

    /*
     * The ordinary case - a WINDOW_UPDATE gives the window back
     */

    {
        FlowControlWindow window( 1U );

        window.consume( 60000 );

        UTF_REQUIRE_EQUAL( window.size(), 5535 );

        window.applyWindowUpdate( 60000U );

        UTF_REQUIRE_EQUAL( window.size(), 65535 );
    }

    /*
     * An increment of zero - RFC 9113 6.9: "a stream error of type PROTOCOL_ERROR; errors on the
     * connection flow-control window MUST be treated as a connection error". Two levels, two
     * exception types, one rule
     */

    {
        FlowControlWindow stream( 7U );

        requireHttp2Error< Http2StreamException >(
            [ &stream ]() -> void
            {
                stream.applyWindowUpdate( 0U );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR,
            7U
            );

        FlowControlWindow connection( Globals::STREAM_ID_CONNECTION );

        requireHttp2Error< Http2ProtocolException >(
            [ &connection ]() -> void
            {
                connection.applyWindowUpdate( 0U );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR,
            Globals::STREAM_ID_CONNECTION
            );
    }

    /*
     * Overflow past 2^31-1 is a FLOW_CONTROL_ERROR, at the same two levels (6.9.1). The boundary
     * either side of it is pinned, because an off-by-one here is a connection which dies on a
     * legal frame
     */

    {
        FlowControlWindow window( 3U, Globals::MAX_FLOW_CONTROL_WINDOW_SIZE - 1 );

        window.applyWindowUpdate( 1U );

        UTF_REQUIRE_EQUAL( window.size(), Globals::MAX_FLOW_CONTROL_WINDOW_SIZE );

        requireHttp2Error< Http2StreamException >(
            [ &window ]() -> void
            {
                window.applyWindowUpdate( 1U );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            3U
            );

        FlowControlWindow connection(
            Globals::STREAM_ID_CONNECTION,
            Globals::MAX_FLOW_CONTROL_WINDOW_SIZE
            );

        requireHttp2Error< Http2ProtocolException >(
            [ &connection ]() -> void
            {
                connection.applyWindowUpdate( 1U );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            Globals::STREAM_ID_CONNECTION
            );
    }

    /*
     * An increment which is itself larger than a window may ever be. The frame codec masks the
     * reserved bit off, so this cannot arrive from the wire - which is exactly why it is checked
     * here rather than assumed away
     */

    {
        FlowControlWindow window( 1U, 0 );

        requireHttp2Error< Http2StreamException >(
            [ &window ]() -> void
            {
                window.applyWindowUpdate( 2147483648U );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            1U
            );

        UTF_REQUIRE_EQUAL( window.size(), 0 );
    }

    /*
     * The largest legal single increment, onto an empty window
     */

    {
        FlowControlWindow window( 1U, 0 );

        window.applyWindowUpdate( static_cast< std::uint32_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ) );

        UTF_REQUIRE_EQUAL( window.size(), Globals::MAX_FLOW_CONTROL_WINDOW_SIZE );
    }
}

UTF_AUTO_TEST_CASE( FlowControl_InitialWindowSizeAdjustmentTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::flowcontrol;

    /*
     * RFC 9113 6.9.2, transcribed from the RFC's own worked example: "if the client sends 60 KB
     * immediately on connection establishment and the server sets the initial window size to be
     * 16 KB, the client will recalculate the available flow-control window to be -44 KB on
     * receipt of the SETTINGS frame"
     */

    {
        FlowControlWindow window( 1U );

        window.consume( 60 * 1024 );

        UTF_REQUIRE_EQUAL( window.size(), 65535 - 61440 );

        window.applyInitialWindowSizeChange( 65535U, 16U * 1024U );

        UTF_REQUIRE_EQUAL( window.size(), -44 * 1024 );
        UTF_REQUIRE( window.isExhausted() );
        UTF_REQUIRE_EQUAL( window.available(), 0 );
    }

    /*
     * The adjustment is the difference, applied retroactively, and it composes: successive
     * changes telescope, so what a window ends at depends only on the last value of the setting
     */

    {
        FlowControlWindow window( 1U, 1000 );

        window.applyInitialWindowSizeChange( 1000U, 3000U );
        UTF_REQUIRE_EQUAL( window.size(), 3000 );

        window.applyInitialWindowSizeChange( 3000U, 500U );
        UTF_REQUIRE_EQUAL( window.size(), 500 );

        window.applyInitialWindowSizeChange( 500U, 500U );
        UTF_REQUIRE_EQUAL( window.size(), 500 );

        window.applyInitialWindowSizeChange( 500U, 1000U );
        UTF_REQUIRE_EQUAL( window.size(), 1000 );
    }

    /*
     * "A SETTINGS frame cannot alter the connection flow-control window" - so asking it to is a
     * programming error rather than anything the wire can ask for
     */

    {
        FlowControlWindow connection( Globals::STREAM_ID_CONNECTION );

        UTF_REQUIRE_THROW(
            connection.applyInitialWindowSizeChange( 65535U, 1024U ),
            UnexpectedException
            );

        UTF_REQUIRE_EQUAL( connection.size(), 65535 );
    }

    /*
     * THE ONE EXCEPTION to "the level decides the error". 6.9.2 makes a SETTINGS change which
     * drives any window past the maximum a CONNECTION error, although the window which overflowed
     * belongs to a stream - the fault is in the SETTINGS frame, not in the stream. So this must
     * be an Http2ProtocolException even though the window is stream 5's
     */

    {
        FlowControlWindow window( 5U, Globals::MAX_FLOW_CONTROL_WINDOW_SIZE );

        requireHttp2Error< Http2ProtocolException >(
            [ &window ]() -> void
            {
                window.applyInitialWindowSizeChange(
                    0U,
                    static_cast< std::uint32_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE )
                    );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            5U
            );
    }

    /*
     * And 6.5.2 - a SETTINGS_INITIAL_WINDOW_SIZE above 2^31-1 is a connection error of type
     * FLOW_CONTROL_ERROR, whichever side of the change it is on
     */

    {
        FlowControlWindow window( 1U );

        requireHttp2Error< Http2ProtocolException >(
            [ &window ]() -> void
            {
                window.applyInitialWindowSizeChange( 65535U, 2147483648U );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            1U
            );

        requireHttp2Error< Http2ProtocolException >(
            [ &window ]() -> void
            {
                window.validateInitialWindowSize( 4294967295U );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            1U
            );

        window.validateInitialWindowSize(
            static_cast< std::uint32_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE )
            );

        UTF_REQUIRE_EQUAL( window.size(), 65535 );
    }
}

UTF_AUTO_TEST_CASE( FlowControl_NegativeWindowTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::flowcontrol;

    /*
     * A negative window is a state the sender sits in, not an error it raises: "A sender MUST
     * track the negative flow-control window and MUST NOT send new flow-controlled frames until
     * it receives WINDOW_UPDATE frames that cause the flow-control window to become positive"
     */

    FlowControlWindow window( 9U, 1000 );

    window.consume( 900 );
    window.applyInitialWindowSizeChange( 1000U, 100U );

    UTF_REQUIRE_EQUAL( window.size(), -800 );

    /*
     * While it is negative: nothing may be sent, and nothing is - take( ) answers zero rather
     * than digging the window deeper, and the number itself is kept rather than clamped
     */

    UTF_REQUIRE( window.isExhausted() );
    UTF_REQUIRE_EQUAL( window.available(), 0 );
    UTF_REQUIRE_EQUAL( window.take( 500 ), 0 );
    UTF_REQUIRE_EQUAL( window.size(), -800 );

    /*
     * And consuming against it is refused rather than allowed to pile up - on the receive side
     * that is a peer which overran what we granted, which 6.9.1 answers with FLOW_CONTROL_ERROR
     * at the level of the window it overran
     */

    requireHttp2Error< Http2StreamException >(
        [ &window ]() -> void
        {
            window.consume( 1 );
        },
        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
        9U
        );

    /*
     * A WINDOW_UPDATE which does not cover the whole debt leaves it negative, which is the case
     * an implementation that clamps at zero gets wrong: it would resume sending 300 octets early
     */

    window.applyWindowUpdate( 500U );

    UTF_REQUIRE_EQUAL( window.size(), -300 );
    UTF_REQUIRE( window.isExhausted() );
    UTF_REQUIRE_EQUAL( window.take( 1 ), 0 );

    /*
     * Exactly covering it leaves zero, which is still nothing to send
     */

    window.applyWindowUpdate( 300U );

    UTF_REQUIRE_EQUAL( window.size(), 0 );
    UTF_REQUIRE( window.isExhausted() );

    /*
     * And one octet more is one octet to send
     */

    window.applyWindowUpdate( 1U );

    UTF_REQUIRE_EQUAL( window.size(), 1 );
    UTF_REQUIRE( ! window.isExhausted() );
    UTF_REQUIRE_EQUAL( window.take( 10 ), 1 );

    /*
     * A connection window which overruns is a connection error, for the same overrun
     */

    FlowControlWindow connection( Globals::STREAM_ID_CONNECTION, 10 );

    requireHttp2Error< Http2ProtocolException >(
        [ &connection ]() -> void
        {
            connection.consume( 11 );
        },
        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
        Globals::STREAM_ID_CONNECTION
        );

    UTF_REQUIRE_EQUAL( connection.size(), 10 );
}

UTF_AUTO_TEST_CASE( FlowControl_ReceiveThresholdTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * The threshold defaults to half the window - design 4.4
     */

    {
        ReceiveFlowControlWindow window( 1U );

        UTF_REQUIRE_EQUAL( window.updateThreshold(), 65535 / 2 );
        UTF_REQUIRE_EQUAL( window.size(), 65535 );
        UTF_REQUIRE_EQUAL( window.outstanding(), 0 );
        UTF_REQUIRE_EQUAL( window.pendingCredit(), 0 );
    }

    /*
     * A WINDOW_UPDATE is due when the CONSUMER has taken the bytes, never when they arrived.
     * Everything below the threshold is silence, whatever has arrived
     */

    {
        ReceiveFlowControlWindow window( 1U, 1000 );

        UTF_REQUIRE_EQUAL( window.updateThreshold(), 500 );

        window.onDataReceived( 900 );

        UTF_REQUIRE_EQUAL( window.size(), 100 );
        UTF_REQUIRE_EQUAL( window.outstanding(), 900 );
        UTF_REQUIRE_EQUAL( window.pendingCredit(), 0 );

        /*
         * 900 octets have arrived and the window is nearly spent - and still nothing is due,
         * because the consumer has taken none of them. That is the backpressure
         */

        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );

        window.onConsumed( 499 );

        UTF_REQUIRE_EQUAL( window.outstanding(), 401 );
        UTF_REQUIRE_EQUAL( window.pendingCredit(), 499 );
        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );

        window.onConsumed( 1 );

        UTF_REQUIRE_EQUAL( window.pendingCredit(), 500 );
        UTF_REQUIRE( window.shouldSendWindowUpdate() );

        /*
         * Taking the update advertises exactly what was consumed, credits it back to the window
         * and starts the count again
         */

        UTF_REQUIRE_EQUAL( window.takeWindowUpdate(), 500 );
        UTF_REQUIRE_EQUAL( window.size(), 600 );
        UTF_REQUIRE_EQUAL( window.pendingCredit(), 0 );
        UTF_REQUIRE_EQUAL( window.outstanding(), 400 );
        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );

        /*
         * With nothing pending there is nothing to send, and the answer is zero rather than a
         * WINDOW_UPDATE of zero - which would be a PROTOCOL_ERROR for the peer receiving it
         */

        UTF_REQUIRE_EQUAL( window.takeWindowUpdate(), 0 );
        UTF_REQUIRE_EQUAL( window.size(), 600 );

        /*
         * And what is left may still be flushed below the threshold, which is what a stream being
         * closed does with the credit it owes the connection
         */

        window.onConsumed( 400 );

        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );
        UTF_REQUIRE_EQUAL( window.takeWindowUpdate(), 400 );
        UTF_REQUIRE_EQUAL( window.size(), 1000 );
        UTF_REQUIRE_EQUAL( window.outstanding(), 0 );
    }

    /*
     * A profile may set its own threshold (Http2Profile::windowUpdateThreshold, design 6.4),
     * including zero, which advertises every acknowledged octet at once
     */

    {
        ReceiveFlowControlWindow window( 1U, 1000 );

        window.setUpdateThreshold( 0 );

        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );

        window.onDataReceived( 1 );
        window.onConsumed( 1 );

        UTF_REQUIRE( window.shouldSendWindowUpdate() );
        UTF_REQUIRE_EQUAL( window.takeWindowUpdate(), 1 );
        UTF_REQUIRE_EQUAL( window.size(), 1000 );

        window.setUpdateThreshold( 900 );

        window.onDataReceived( 899 );
        window.onConsumed( 899 );

        UTF_REQUIRE( ! window.shouldSendWindowUpdate() );

        UTF_REQUIRE_THROW( window.setUpdateThreshold( -1 ), UnexpectedException );
    }
}

UTF_AUTO_TEST_CASE( FlowControl_ReceiveAccountingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::flowcontrol;

    /*
     * Acknowledging more than ever arrived would advertise a window we never spent, and the peer
     * would take us at our word. It is refused rather than clamped, which is what catches the
     * same DATA frame being credited twice
     */

    {
        ReceiveFlowControlWindow window( 1U, 1000 );

        window.onDataReceived( 10 );

        UTF_REQUIRE_THROW( window.onConsumed( 11 ), UnexpectedException );

        window.onConsumed( 10 );

        UTF_REQUIRE_THROW( window.onConsumed( 1 ), UnexpectedException );
        UTF_REQUIRE_THROW( window.onConsumed( -1 ), UnexpectedException );

        UTF_REQUIRE_EQUAL( window.outstanding(), 0 );
        UTF_REQUIRE_EQUAL( window.pendingCredit(), 10 );
    }

    /*
     * A peer which sends more than we granted overruns the window we advertised, which 6.9.1
     * answers with FLOW_CONTROL_ERROR at the level of the window it overran
     */

    {
        ReceiveFlowControlWindow stream( 11U, 100 );

        requireHttp2Error< Http2StreamException >(
            [ &stream ]() -> void
            {
                stream.onDataReceived( 101 );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            11U
            );

        UTF_REQUIRE_EQUAL( stream.size(), 100 );
        UTF_REQUIRE_EQUAL( stream.outstanding(), 0 );

        ReceiveFlowControlWindow connection( Globals::STREAM_ID_CONNECTION, 100 );

        requireHttp2Error< Http2ProtocolException >(
            [ &connection ]() -> void
            {
                connection.onDataReceived( 101 );
            },
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
            Globals::STREAM_ID_CONNECTION
            );
    }

    /*
     * THE CLOSED STREAM CASE, which the stream state machine depends on (design 4.3). A DATA
     * frame which arrives for a stream we already closed still spends the CONNECTION window and
     * nobody will ever consume it, so it is received and acknowledged in the same breath. The
     * connection window comes back to where it started; forgetting either half is the leak which
     * stalls a connection for good
     */

    {
        ReceiveFlowControlWindow connection( Globals::STREAM_ID_CONNECTION, 1000 );

        connection.setUpdateThreshold( 0 );

        for( int i = 0; i < 5; ++i )
        {
            connection.onDataReceived( 100 );
            connection.onConsumed( 100 );

            UTF_REQUIRE_EQUAL( connection.takeWindowUpdate(), 100 );
        }

        UTF_REQUIRE_EQUAL( connection.size(), 1000 );
        UTF_REQUIRE_EQUAL( connection.outstanding(), 0 );
        UTF_REQUIRE_EQUAL( connection.pendingCredit(), 0 );

        /*
         * And the same five frames with the crediting left out, which is the defect: the window
         * is 500 octets short and nothing will ever give them back
         */

        ReceiveFlowControlWindow leaking( Globals::STREAM_ID_CONNECTION, 1000 );

        for( int i = 0; i < 5; ++i )
        {
            leaking.onDataReceived( 100 );
        }

        UTF_REQUIRE_EQUAL( leaking.size(), 500 );
        UTF_REQUIRE_EQUAL( leaking.takeWindowUpdate(), 0 );
        UTF_REQUIRE_EQUAL( leaking.size(), 500 );
    }

    /*
     * The underlying window is reachable for inspection, and it is the same object the arithmetic
     * above operates on
     */

    {
        ReceiveFlowControlWindow window( 3U, 200 );

        UTF_REQUIRE_EQUAL( window.streamId(), 3U );
        UTF_REQUIRE( ! window.window().isConnectionLevel() );

        window.onDataReceived( 50 );

        UTF_REQUIRE_EQUAL( window.window().size(), 150 );
        UTF_REQUIRE_EQUAL( window.window().available(), 150 );
    }
}

#endif /* __UTEST_TESTFLOWCONTROL_H_ */
