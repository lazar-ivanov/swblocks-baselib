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

#ifndef __UTEST_TESTSTREAMSTATES_H_
#define __UTEST_TESTSTREAMSTATES_H_

#include <baselib/http2/StreamStateMachine.h>
#include <baselib/http2/FlowControlWindow.h>
#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeUtils.h>

#include <cstdint>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2 stream state machine of RFC 9113 section 5.1 - notes/plans/http2-design.md 4.3
 *
 * THE TWO TABLES BELOW ARE TRANSCRIBED FROM THE RFC, NOT READ OFF THE IMPLEMENTATION. Each row
 * carries the sentence of section 5.1 it comes from, so a reviewer can check the table against
 * the RFC without reading a line of the code it tests. Writing the table the other way round
 * proves only that the code agrees with itself
 *
 * The case which everyone gets wrong has one of its own: a DATA frame which arrives on a stream
 * we have already closed still spends the CONNECTION flow-control window and still has to be
 * given back, whatever else is done with the frame
 */

namespace utest
{
    namespace streamstates
    {
        /**
         * @brief The nine distinct positions a stream can be in
         *
         * Seven states, of which "closed" is three: RFC 9113 5.1 gives the closed state three
         * different answers to the same arriving frame depending on how the stream got there, so
         * for the purposes of a transition table they are three rows and not one
         */

        enum class Fixture : std::uint8_t
        {
            Idle = 0,
            ReservedLocal,
            ReservedRemote,
            Open,
            HalfClosedLocal,
            HalfClosedRemote,
            ClosedByRstStreamSent,
            ClosedByRstStreamReceived,
            ClosedByEndStream,
        };

        enum : std::size_t
        {
            FIXTURE_COUNT = 9U,
            STREAM_FRAME_TYPE_COUNT = 6U,
        };

        inline const char* fixtureName( SAA_in const Fixture fixture ) NOEXCEPT
        {
            switch( fixture )
            {
                case Fixture::Idle:                         return "idle";
                case Fixture::ReservedLocal:                return "reserved (local)";
                case Fixture::ReservedRemote:               return "reserved (remote)";
                case Fixture::Open:                         return "open";
                case Fixture::HalfClosedLocal:              return "half-closed (local)";
                case Fixture::HalfClosedRemote:             return "half-closed (remote)";
                case Fixture::ClosedByRstStreamSent:        return "closed, we reset it";
                case Fixture::ClosedByRstStreamReceived:    return "closed, the peer reset it";
                case Fixture::ClosedByEndStream:            return "closed, ended normally";
            }

            return "unknown";
        }

        /**
         * @brief Drives a fresh machine into the position wanted, using only its public API
         */

        inline bl::http2::StreamStateMachine makeStream(
            SAA_in          const Fixture                        fixture,
            SAA_in          const bl::http2::StreamRole          role,
            SAA_in          const std::uint32_t                  streamId
            )
        {
            using namespace bl::http2;

            StreamStateMachine stream( streamId, role );

            const auto none = static_cast< std::uint8_t >( Globals::FRAME_FLAG_NONE );
            const auto endStream = static_cast< std::uint8_t >( Globals::FRAME_FLAG_END_STREAM );
            const auto headers = static_cast< std::uint8_t >( Globals::FRAME_TYPE_HEADERS );
            const auto rstStream = static_cast< std::uint8_t >( Globals::FRAME_TYPE_RST_STREAM );

            switch( fixture )
            {
                case Fixture::Idle:
                    break;

                case Fixture::ReservedLocal:
                    stream.reserveLocal();
                    break;

                case Fixture::ReservedRemote:
                    stream.reserveRemote();
                    break;

                case Fixture::Open:
                    stream.onFrameSent( headers, none );
                    break;

                case Fixture::HalfClosedLocal:
                    stream.onFrameSent( headers, endStream );
                    break;

                case Fixture::HalfClosedRemote:
                    stream.onFrameSent( headers, none );
                    stream.onFrameReceived( headers, endStream, 0 );
                    break;

                case Fixture::ClosedByRstStreamSent:
                    stream.onFrameSent( headers, none );
                    stream.onFrameSent( rstStream, none );
                    break;

                case Fixture::ClosedByRstStreamReceived:
                    stream.onFrameSent( headers, none );
                    stream.onFrameReceived( rstStream, none, 0 );
                    break;

                case Fixture::ClosedByEndStream:
                    stream.onFrameSent( headers, endStream );
                    stream.onFrameReceived( headers, endStream, 0 );
                    break;
            }

            return stream;
        }

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

    } // streamstates

} // utest

UTF_AUTO_TEST_CASE( StreamStates_TransitionTableTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::streamstates;

    /*
     * The six frame types which are associated with a stream and mean something to its state.
     * SETTINGS, PING and GOAWAY are connection frames and never carry a stream identifier;
     * CONTINUATION belongs to a header block, which the frame reader owns
     */

    const std::uint8_t frameTypes[ STREAM_FRAME_TYPE_COUNT ] =
    {
        Globals::FRAME_TYPE_DATA,
        Globals::FRAME_TYPE_HEADERS,
        Globals::FRAME_TYPE_PRIORITY,
        Globals::FRAME_TYPE_RST_STREAM,
        Globals::FRAME_TYPE_PUSH_PROMISE,
        Globals::FRAME_TYPE_WINDOW_UPDATE,
    };

    /*
     * WHAT MAY BE SENT - RFC 9113 5.1, transcribed
     *
     *   idle                  "RST_STREAM frames MUST NOT be sent for a stream in the idle
     *                          state"; a stream is opened by sending HEADERS, and PRIORITY may
     *                          be sent on an idle stream (5.3)
     *   reserved (local)      "An endpoint MUST NOT send any type of frame other than HEADERS,
     *                          RST_STREAM, or PRIORITY in this state"
     *   reserved (remote)     "An endpoint MUST NOT send any type of frame other than
     *                          RST_STREAM, WINDOW_UPDATE, or PRIORITY in this state"
     *   open                  "either peer can send any type of frame"
     *   half-closed (local)   "cannot be used for sending frames other than WINDOW_UPDATE,
     *                          PRIORITY, and RST_STREAM"
     *   half-closed (remote)  the sending half is untouched, so any type
     *   closed                "An endpoint MUST NOT send frames other than PRIORITY on a closed
     *                          stream", whichever way it closed
     */

    const bool mayBeSent[ FIXTURE_COUNT ][ STREAM_FRAME_TYPE_COUNT ] =
    {
        /*                          DATA   HEADERS PRIORITY  RST  PUSH_PROM  WINDOW_UPD */
        /* idle                */ { false, true,   true,    false, false,    false },
        /* reserved (local)    */ { false, true,   true,    true,  false,    false },
        /* reserved (remote)   */ { false, false,  true,    true,  false,    true  },
        /* open                */ { true,  true,   true,    true,  true,     true  },
        /* half-closed (local) */ { false, false,  true,    true,  false,    true  },
        /* half-closed (remote)*/ { true,  true,   true,    true,  true,     true  },
        /* closed, we reset    */ { false, false,  true,    false, false,    false },
        /* closed, peer reset  */ { false, false,  true,    false, false,    false },
        /* closed, ended       */ { false, false,  true,    false, false,    false },
    };

    /*
     * WHAT MAY BE RECEIVED WITHOUT ERROR - RFC 9113 5.1, transcribed
     *
     *   idle                  "Receiving any frame other than HEADERS or PRIORITY on a stream
     *                          in this state MUST be treated as a connection error of type
     *                          PROTOCOL_ERROR"
     *   reserved (local)      "Receiving any type of frame other than RST_STREAM, PRIORITY, or
     *                          WINDOW_UPDATE on a stream in this state MUST be treated as a
     *                          connection error of type PROTOCOL_ERROR"
     *   reserved (remote)     "Receiving any type of frame other than HEADERS, RST_STREAM, or
     *                          PRIORITY on a stream in this state MUST be treated as a
     *                          connection error of type PROTOCOL_ERROR"
     *   open                  any type
     *   half-closed (local)   the receiving half is untouched, so any type
     *   half-closed (remote)  "If an endpoint receives additional frames, other than
     *                          WINDOW_UPDATE, PRIORITY, or RST_STREAM, for a stream that is in
     *                          this state, it MUST respond with a stream error of type
     *                          STREAM_CLOSED"
     *   closed, we reset it   "An endpoint MUST ignore frames that it receives on closed streams
     *                          after it has sent a RST_STREAM frame" - so everything
     *   closed, peer reset it "An endpoint that receives any frame other than PRIORITY after
     *                          receiving a RST_STREAM MUST treat that as a stream error of type
     *                          STREAM_CLOSED"
     *   closed, ended         "an endpoint that receives any frames after receiving a frame with
     *                          the END_STREAM flag set MUST treat that as a connection error of
     *                          type STREAM_CLOSED", except that "Endpoints MUST ignore
     *                          WINDOW_UPDATE or RST_STREAM frames received in this state"
     */

    const bool mayBeReceived[ FIXTURE_COUNT ][ STREAM_FRAME_TYPE_COUNT ] =
    {
        /*                          DATA   HEADERS PRIORITY  RST  PUSH_PROM  WINDOW_UPD */
        /* idle                */ { false, true,   true,    false, false,    false },
        /* reserved (local)    */ { false, false,  true,    true,  false,    true  },
        /* reserved (remote)   */ { false, true,   true,    true,  false,    false },
        /* open                */ { true,  true,   true,    true,  true,     true  },
        /* half-closed (local) */ { true,  true,   true,    true,  true,     true  },
        /* half-closed (remote)*/ { false, false,  true,    true,  false,    true  },
        /* closed, we reset    */ { true,  true,   true,    true,  true,     true  },
        /* closed, peer reset  */ { false, false,  true,    false, false,    false },
        /* closed, ended       */ { false, false,  true,    true,  false,    true  },
    };

    const StreamRole roles[ 2 ] = { StreamRole::Client, StreamRole::Server };

    /*
     * The tables are the same for both roles: RFC 9113 5.1 is written in terms of "local" and
     * "remote" and never in terms of client and server. What the role does decide is 5.1.1's
     * identifier parity, which has its own case
     */

    for( std::size_t roleIndex = 0U; roleIndex < 2U; ++roleIndex )
    {
        const auto role = roles[ roleIndex ];
        const std::uint32_t streamId = role == StreamRole::Client ? 1U : 2U;

        for( std::size_t row = 0U; row < FIXTURE_COUNT; ++row )
        {
            const auto fixture = static_cast< Fixture >( row );

            for( std::size_t column = 0U; column < STREAM_FRAME_TYPE_COUNT; ++column )
            {
                const auto frameType = frameTypes[ column ];

                /*
                 * The query agrees with the table
                 */

                {
                    const auto stream = makeStream( fixture, role, streamId );

                    UTF_REQUIRE_EQUAL( stream.canSend( frameType ), mayBeSent[ row ][ column ] );
                    UTF_REQUIRE_EQUAL(
                        stream.canReceive( frameType ),
                        mayBeReceived[ row ][ column ]
                        );
                }

                /*
                 * And so does what actually happens. Sending something the state forbids is a
                 * programming error, because what we send is ours to choose
                 */

                {
                    auto stream = makeStream( fixture, role, streamId );

                    if( mayBeSent[ row ][ column ] )
                    {
                        stream.onFrameSent( frameType, Globals::FRAME_FLAG_NONE );
                    }
                    else
                    {
                        UTF_REQUIRE_THROW(
                            stream.onFrameSent( frameType, Globals::FRAME_FLAG_NONE ),
                            UnexpectedException
                            );
                    }
                }

                {
                    auto stream = makeStream( fixture, role, streamId );

                    if( mayBeReceived[ row ][ column ] )
                    {
                        const auto result =
                            stream.onFrameReceived( frameType, Globals::FRAME_FLAG_NONE, 0 );

                        /*
                         * Everything a closed stream admits is tolerated rather than acted on
                         */

                        UTF_REQUIRE(
                            result.disposition.value() ==
                                ( fixture == Fixture::ClosedByRstStreamSent ||
                                  fixture == Fixture::ClosedByRstStreamReceived ||
                                  fixture == Fixture::ClosedByEndStream ?
                                      FrameDisposition::Ignored : FrameDisposition::Accepted )
                            );
                    }
                    else if(
                        fixture == Fixture::HalfClosedRemote ||
                        fixture == Fixture::ClosedByRstStreamReceived
                        )
                    {
                        /*
                         * A STREAM error - the connection lives and the caller sends RST_STREAM
                         */

                        const auto result =
                            stream.onFrameReceived( frameType, Globals::FRAME_FLAG_NONE, 0 );

                        UTF_REQUIRE( result.disposition.value() == FrameDisposition::StreamError );
                        UTF_REQUIRE_EQUAL(
                            result.streamErrorCode.value(),
                            Globals::ERROR_CODE_STREAM_CLOSED
                            );
                    }
                    else
                    {
                        /*
                         * A CONNECTION error - PROTOCOL_ERROR in the idle and reserved states,
                         * STREAM_CLOSED once the peer has ended the stream
                         */

                        const auto expectedCode =
                            fixture == Fixture::ClosedByEndStream ?
                                Globals::ERROR_CODE_STREAM_CLOSED :
                                Globals::ERROR_CODE_PROTOCOL_ERROR;

                        requireHttp2Error< Http2ProtocolException >(
                            [ &stream, frameType ]() -> void
                            {
                                stream.onFrameReceived( frameType, Globals::FRAME_FLAG_NONE, 0 );
                            },
                            expectedCode,
                            streamId
                            );
                    }
                }
            }
        }
    }

    /*
     * An unknown frame type is ignored in every state, because 4.1 makes that the receiver's
     * business rather than the state machine's - a state machine which rejected an extension
     * frame would make the connection brittle by design
     */

    for( std::size_t row = 0U; row < FIXTURE_COUNT; ++row )
    {
        auto stream = makeStream( static_cast< Fixture >( row ), StreamRole::Client, 1U );

        UTF_REQUIRE( stream.canReceive( 0x0bU ) );
        UTF_REQUIRE( stream.canSend( 0x0bU ) );

        const auto result = stream.onFrameReceived( 0x0bU, Globals::FRAME_FLAG_NONE, 0 );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
    }

    /*
     * And the frames which must never be routed to a stream at all
     */

    {
        auto stream = makeStream( Fixture::Open, StreamRole::Client, 1U );

        UTF_REQUIRE_THROW(
            stream.onFrameReceived( Globals::FRAME_TYPE_SETTINGS, 0U, 0 ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW(
            stream.onFrameReceived( Globals::FRAME_TYPE_PING, 0U, 0 ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW(
            stream.onFrameReceived( Globals::FRAME_TYPE_GOAWAY, 0U, 0 ),
            UnexpectedException
            );

        /*
         * CONTINUATION too: a field block is one message spread over several frames, and this
         * machine is told about the completed HEADERS rather than about its frames - which is
         * also what stops a HEADERS carrying END_STREAM but not END_HEADERS from closing the
         * stream out from under its own CONTINUATION frames
         */

        UTF_REQUIRE_THROW(
            stream.onFrameReceived( Globals::FRAME_TYPE_CONTINUATION, 0U, 0 ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW(
            stream.onFrameSent( Globals::FRAME_TYPE_CONTINUATION, 0U ),
            UnexpectedException
            );
    }
}

UTF_AUTO_TEST_CASE( StreamStates_LifecycleTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::streamstates;

    const auto none = static_cast< std::uint8_t >( Globals::FRAME_FLAG_NONE );
    const auto endStream = static_cast< std::uint8_t >( Globals::FRAME_FLAG_END_STREAM );

    /*
     * A GET: the request is one HEADERS frame carrying END_STREAM, and the response is HEADERS
     * then DATA then DATA with END_STREAM
     */

    {
        StreamStateMachine stream( 1U, StreamRole::Client );

        UTF_REQUIRE( stream.isIdle() );
        UTF_REQUIRE( stream.isLocallyInitiated() );
        UTF_REQUIRE( stream.state() == StreamState::Idle );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, endStream );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedLocal );

        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, none, 0 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedLocal );

        stream.onFrameReceived( Globals::FRAME_TYPE_DATA, none, 1024 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedLocal );

        const auto last = stream.onFrameReceived( Globals::FRAME_TYPE_DATA, endStream, 16 );

        UTF_REQUIRE( last.disposition.value() == FrameDisposition::Accepted );
        UTF_REQUIRE_EQUAL( last.connectionWindowBytes.value(), 16 );
        UTF_REQUIRE( stream.isClosed() );
        UTF_REQUIRE( stream.closeCause() == StreamCloseCause::EndStreamReceived );
    }

    /*
     * A POST with a body: HEADERS, then DATA frames, then a DATA frame carrying END_STREAM. The
     * response may begin before the upload finishes, which is why the stream passes through open
     * in both directions
     */

    {
        StreamStateMachine stream( 3U, StreamRole::Client );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );

        UTF_REQUIRE( stream.state() == StreamState::Open );

        stream.onFrameSent( Globals::FRAME_TYPE_DATA, none );
        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, none, 0 );

        UTF_REQUIRE( stream.state() == StreamState::Open );

        stream.onFrameSent( Globals::FRAME_TYPE_DATA, endStream );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedLocal );

        stream.onFrameReceived( Globals::FRAME_TYPE_DATA, endStream, 4 );

        UTF_REQUIRE( stream.isClosed() );
        UTF_REQUIRE( stream.closeCause() == StreamCloseCause::EndStreamReceived );
    }

    /*
     * The early response of design 4.5: the whole response arrives while we are still uploading,
     * and the peer resets the stream with NO_ERROR to say "stop sending". The stream closes
     * because the PEER reset it, so what arrives afterwards is a stream error and not a
     * connection error
     */

    {
        StreamStateMachine stream( 5U, StreamRole::Client );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, none, 0 );
        stream.onFrameReceived( Globals::FRAME_TYPE_DATA, endStream, 8 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedRemote );

        stream.onFrameReceived( Globals::FRAME_TYPE_RST_STREAM, none, 0 );

        UTF_REQUIRE( stream.isClosed() );
        UTF_REQUIRE( stream.closeCause() == StreamCloseCause::RstStreamReceived );
    }

    /*
     * And the other way: the peer finishes first and we end our own half afterwards, which is
     * still "the peer ended the stream" as far as a later frame is concerned
     */

    {
        StreamStateMachine stream( 7U, StreamRole::Client );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, endStream, 0 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedRemote );

        stream.onFrameSent( Globals::FRAME_TYPE_DATA, endStream );

        UTF_REQUIRE( stream.isClosed() );
        UTF_REQUIRE( stream.closeCause() == StreamCloseCause::EndStreamReceived );
    }

    /*
     * Cancelling: we send RST_STREAM, and from then on everything the peer had already sent is
     * ignored rather than treated as an error
     */

    {
        StreamStateMachine stream( 9U, StreamRole::Client );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        stream.onFrameSent( Globals::FRAME_TYPE_RST_STREAM, none );

        UTF_REQUIRE( stream.isClosed() );
        UTF_REQUIRE( stream.closeCause() == StreamCloseCause::RstStreamSent );

        const auto inFlight = stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, none, 0 );

        UTF_REQUIRE( inFlight.disposition.value() == FrameDisposition::Ignored );
    }

    /*
     * A stream driven from the receiving side, which is what a server does with a request. Role
     * neutrality (D8) is what lets the test peer of design 8.2 be built on this
     */

    {
        StreamStateMachine stream( 1U, StreamRole::Server );

        UTF_REQUIRE( ! stream.isLocallyInitiated() );

        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, none, 0 );

        UTF_REQUIRE( stream.state() == StreamState::Open );

        stream.onFrameReceived( Globals::FRAME_TYPE_DATA, endStream, 32 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedRemote );

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        stream.onFrameSent( Globals::FRAME_TYPE_DATA, endStream );

        UTF_REQUIRE( stream.isClosed() );
    }
}

UTF_AUTO_TEST_CASE( StreamStates_ReservedStatesTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::streamstates;

    /*
     * The reserved states are present for completeness and are unreachable in this client (D11):
     * SETTINGS_ENABLE_PUSH is always zero and a PUSH_PROMISE which arrives anyway is a connection
     * error the session raises. They are defined and tested rather than left as a gap in a switch
     */

    {
        StreamStateMachine stream( 2U, StreamRole::Server );

        stream.reserveLocal();

        UTF_REQUIRE( stream.state() == StreamState::ReservedLocal );

        /*
         * "The endpoint can send a HEADERS frame. This causes the stream to open in a
         * half-closed (remote) state"
         */

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, Globals::FRAME_FLAG_NONE );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedRemote );
    }

    {
        StreamStateMachine stream( 2U, StreamRole::Client );

        stream.reserveRemote();

        UTF_REQUIRE( stream.state() == StreamState::ReservedRemote );

        /*
         * "Receiving a HEADERS frame causes the stream to transition to half-closed (local)"
         */

        stream.onFrameReceived( Globals::FRAME_TYPE_HEADERS, Globals::FRAME_FLAG_NONE, 0 );

        UTF_REQUIRE( stream.state() == StreamState::HalfClosedLocal );
    }

    /*
     * "Either endpoint can send a RST_STREAM frame to cause the stream to become closed. This
     * releases the stream reservation"
     */

    {
        StreamStateMachine reservedLocal( 2U, StreamRole::Server );
        reservedLocal.reserveLocal();
        reservedLocal.onFrameSent( Globals::FRAME_TYPE_RST_STREAM, Globals::FRAME_FLAG_NONE );

        UTF_REQUIRE( reservedLocal.isClosed() );
        UTF_REQUIRE( reservedLocal.closeCause() == StreamCloseCause::RstStreamSent );

        StreamStateMachine reservedRemote( 2U, StreamRole::Client );
        reservedRemote.reserveRemote();
        reservedRemote.onFrameReceived(
            Globals::FRAME_TYPE_RST_STREAM,
            Globals::FRAME_FLAG_NONE,
            0
            );

        UTF_REQUIRE( reservedRemote.isClosed() );
        UTF_REQUIRE( reservedRemote.closeCause() == StreamCloseCause::RstStreamReceived );
    }

    /*
     * Only an idle stream can be reserved - a PUSH_PROMISE names a stream which does not exist
     */

    {
        for( std::size_t row = 1U; row < FIXTURE_COUNT; ++row )
        {
            auto stream = makeStream( static_cast< Fixture >( row ), StreamRole::Client, 2U );

            UTF_REQUIRE_THROW( stream.reserveLocal(), UnexpectedException );
            UTF_REQUIRE_THROW( stream.reserveRemote(), UnexpectedException );
        }
    }
}

UTF_AUTO_TEST_CASE( StreamStates_StreamIdentifierTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * RFC 9113 5.1.1 - "Streams initiated by a client MUST use odd-numbered stream identifiers;
     * those initiated by the server MUST use even-numbered stream identifiers"
     */

    UTF_REQUIRE( StreamStateMachine::isLocallyInitiatedId( 1U, StreamRole::Client ) );
    UTF_REQUIRE( StreamStateMachine::isLocallyInitiatedId( 4097U, StreamRole::Client ) );
    UTF_REQUIRE( ! StreamStateMachine::isLocallyInitiatedId( 2U, StreamRole::Client ) );
    UTF_REQUIRE( StreamStateMachine::isLocallyInitiatedId( 2U, StreamRole::Server ) );
    UTF_REQUIRE( ! StreamStateMachine::isLocallyInitiatedId( 1U, StreamRole::Server ) );

    /*
     * Stream zero is the connection and cannot be a stream (4.1), and an identifier still has to
     * fit 31 bits
     */

    UTF_REQUIRE_THROW( StreamStateMachine( 0U, StreamRole::Client ), UnexpectedException );
    UTF_REQUIRE_THROW(
        StreamStateMachine( Globals::MAX_STREAM_ID + 1U, StreamRole::Client ),
        UnexpectedException
        );

    /*
     * "The identifier of a newly established stream MUST be numerically greater than all streams
     * that the initiating endpoint has opened or reserved" - so a client allocates 1, 3, 5, ...
     */

    {
        StreamRegistry registry( StreamRole::Client );

        UTF_REQUIRE_EQUAL( registry.nextLocalStreamId(), 1U );

        for( std::uint32_t expected = 1U; expected < 20U; expected += 2U )
        {
            UTF_REQUIRE_EQUAL( registry.nextLocalStreamId(), expected );

            auto& stream = registry.openLocalStream();

            UTF_REQUIRE_EQUAL( stream.streamId(), expected );
            UTF_REQUIRE( stream.isIdle() );
        }

        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 10U );

        StreamRegistry server( StreamRole::Server );

        UTF_REQUIRE_EQUAL( server.nextLocalStreamId(), 2U );
        UTF_REQUIRE_EQUAL( server.openLocalStream().streamId(), 2U );
        UTF_REQUIRE_EQUAL( server.openLocalStream().streamId(), 4U );
    }

    /*
     * "Stream identifiers cannot be reused ... A client that is unable to establish a new stream
     * identifier can establish a new connection for new streams" - so exhaustion is not an error,
     * it is a connection which has to drain (design 4.3). The margin at which the pool is told is
     * the pool's to set; with none, draining begins exactly when nothing is left
     */

    {
        StreamRegistry registry( StreamRole::Client );

        UTF_REQUIRE_EQUAL( registry.remainingLocalStreams(), 1073741824U );
        UTF_REQUIRE( ! registry.isDraining() );
        UTF_REQUIRE( registry.canOpenLocalStream() );

        /*
         * A reserve of everything that is left is the same position as having nothing left, and
         * it is the only way to reach that position in a test: walking to 2^31-1 one stream at a
         * time is a billion map insertions
         */

        registry.setDrainingReserve( registry.remainingLocalStreams() );

        UTF_REQUIRE( registry.isDraining() );
        UTF_REQUIRE( ! registry.canOpenLocalStream() );
        UTF_REQUIRE_THROW( registry.openLocalStream(), UnexpectedException );

        registry.setDrainingReserve( registry.remainingLocalStreams() - 1U );

        UTF_REQUIRE( ! registry.isDraining() );
        UTF_REQUIRE( registry.canOpenLocalStream() );

        registry.setDrainingReserve( 0U );

        UTF_REQUIRE( ! registry.isDraining() );

        /*
         * A GOAWAY, or the pool's own decision, marks it draining whatever is left
         */

        registry.markDraining();

        UTF_REQUIRE( registry.isDraining() );
        UTF_REQUIRE( ! registry.canOpenLocalStream() );
    }

    /*
     * The very end of the identifier space. 2^31-1 is odd, so it is the last stream a client can
     * ever open on one connection
     */

    {
        StreamRegistry registry( StreamRole::Client );

        /*
         * 2^31-1 is odd, so it is the last stream a client can ever open on one connection, and
         * the count falls by exactly one per stream opened
         */

        for( std::uint32_t opened = 0U; opened < 4U; ++opened )
        {
            UTF_REQUIRE_EQUAL(
                registry.remainingLocalStreams(),
                ( Globals::MAX_STREAM_ID - registry.nextLocalStreamId() ) / 2U + 1U
                );

            UTF_REQUIRE_EQUAL( registry.remainingLocalStreams(), 1073741824U - opened );

            registry.openLocalStream();
        }

        StreamRegistry server( StreamRole::Server );

        /*
         * A server has one fewer, because its identifiers start at 2 and 2^31-1 is not one of them
         */

        UTF_REQUIRE_EQUAL( server.nextLocalStreamId(), 2U );
        UTF_REQUIRE_EQUAL( server.remainingLocalStreams(), 1073741823U );
    }
}

UTF_AUTO_TEST_CASE( StreamStates_ClosedStreamDataAccountingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::streamstates;

    const auto none = static_cast< std::uint8_t >( Globals::FRAME_FLAG_NONE );
    const auto endStream = static_cast< std::uint8_t >( Globals::FRAME_FLAG_END_STREAM );

    /*
     * THE CASE EVERYONE GETS WRONG. RFC 9113 5.1: "Flow-controlled frames (i.e., DATA) received
     * after sending RST_STREAM are counted toward the connection flow-control window. Even though
     * these frames might be ignored, because they are sent before the sender receives the
     * RST_STREAM, the sender will consider the frames counted against the flow-control window"
     *
     * So the frame is IGNORED and the octets are REAL. The verdict and the octets arrive together
     * for exactly this reason
     */

    {
        auto stream = makeStream( Fixture::ClosedByRstStreamSent, StreamRole::Client, 1U );

        const auto result = stream.onFrameReceived( Globals::FRAME_TYPE_DATA, none, 4096 );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 4096 );
    }

    /*
     * The same for a stream the PEER reset, where the verdict is a stream error rather than
     * silence - the connection lives, so the octets still have to be given back
     */

    {
        auto stream = makeStream( Fixture::ClosedByRstStreamReceived, StreamRole::Client, 1U );

        const auto result = stream.onFrameReceived( Globals::FRAME_TYPE_DATA, none, 1234 );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::StreamError );
        UTF_REQUIRE_EQUAL( result.streamErrorCode.value(), Globals::ERROR_CODE_STREAM_CLOSED );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 1234 );
    }

    /*
     * And for a stream which is gone entirely - forgotten by the registry rather than remembered
     */

    {
        StreamRegistry registry( StreamRole::Client, 0U /* remember none */ );

        const auto now = time::microsec_clock::universal_time();

        auto& stream = registry.openLocalStream();

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        registry.onFrameSent( 1U, Globals::FRAME_TYPE_RST_STREAM, none, now );

        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 0U );
        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 0U );

        const auto result =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 999, now );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
        UTF_REQUIRE( ! result.isKnownStream.value() );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 999 );
    }

    /*
     * End to end against the connection window, which is what the accounting is FOR. Ten DATA
     * frames arrive for a stream we reset; every one is ignored, and the connection window comes
     * back to where it started because every one was credited
     */

    {
        StreamRegistry registry( StreamRole::Client );
        ReceiveFlowControlWindow connectionWindow( Globals::STREAM_ID_CONNECTION, 65535 );

        connectionWindow.setUpdateThreshold( 0 );

        const auto now = time::microsec_clock::universal_time();

        auto& stream = registry.openLocalStream();

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, endStream );
        registry.onFrameSent( 1U, Globals::FRAME_TYPE_RST_STREAM, none, now );

        for( int i = 0; i < 10; ++i )
        {
            const auto result =
                registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 1000, now );

            UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );

            /*
             * The connection window is spent whatever the verdict, and given straight back
             * because nobody will ever consume a frame for a stream which is gone
             */

            connectionWindow.onDataReceived( result.connectionWindowBytes.value() );
            connectionWindow.onConsumed( result.connectionWindowBytes.value() );

            UTF_REQUIRE_EQUAL( connectionWindow.takeWindowUpdate(), 1000 );
        }

        UTF_REQUIRE_EQUAL( connectionWindow.size(), 65535 );
        UTF_REQUIRE_EQUAL( connectionWindow.outstanding(), 0 );
    }

    /*
     * And the same ten frames with the accounting left out, which is the defect this exists to
     * prevent: the connection window is ten kilobytes short, nothing will give them back, and the
     * connection stalls once the shortfall reaches the whole window
     */

    {
        StreamRegistry registry( StreamRole::Client );
        ReceiveFlowControlWindow connectionWindow( Globals::STREAM_ID_CONNECTION, 65535 );

        const auto now = time::microsec_clock::universal_time();

        auto& stream = registry.openLocalStream();

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, endStream );
        registry.onFrameSent( 1U, Globals::FRAME_TYPE_RST_STREAM, none, now );

        for( int i = 0; i < 10; ++i )
        {
            const auto result =
                registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 1000, now );

            connectionWindow.onDataReceived( result.connectionWindowBytes.value() );

            /*
             * The frame was ignored, so a caller which credits only what it delivered credits
             * nothing here
             */
        }

        UTF_REQUIRE_EQUAL( connectionWindow.size(), 55535 );
        UTF_REQUIRE_EQUAL( connectionWindow.takeWindowUpdate(), 0 );
        UTF_REQUIRE_EQUAL( connectionWindow.size(), 55535 );
    }

    /*
     * Only DATA is flow controlled (6.9), so nothing else carries octets to credit
     */

    {
        auto stream = makeStream( Fixture::ClosedByRstStreamSent, StreamRole::Client, 1U );

        UTF_REQUIRE_EQUAL(
            stream.onFrameReceived(
                Globals::FRAME_TYPE_WINDOW_UPDATE, none, 4
                ).connectionWindowBytes.value(),
            0
            );

        UTF_REQUIRE_EQUAL(
            stream.onFrameReceived(
                Globals::FRAME_TYPE_HEADERS, none, 100
                ).connectionWindowBytes.value(),
            0
            );
    }

    /*
     * A DATA frame on a live stream carries them too - the field is not a special case for closed
     * streams, it is simply what the frame costs
     */

    {
        auto stream = makeStream( Fixture::HalfClosedLocal, StreamRole::Client, 1U );

        const auto result = stream.onFrameReceived( Globals::FRAME_TYPE_DATA, none, 16384 );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Accepted );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 16384 );
    }
}

UTF_AUTO_TEST_CASE( StreamStates_RecentlyClosedSetTests )
{
    using namespace bl;
    using namespace bl::http2;

    const auto none = static_cast< std::uint8_t >( Globals::FRAME_FLAG_NONE );
    const auto endStream = static_cast< std::uint8_t >( Globals::FRAME_FLAG_END_STREAM );

    const auto start = time::microsec_clock::universal_time();

    /*
     * A stream is retired into the remembered set as it closes, and what is remembered is the
     * whole machine - so it goes on answering frames the way its own close cause requires
     */

    {
        StreamRegistry registry( StreamRole::Client );

        auto& stream = registry.openLocalStream();

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, endStream );
        registry.onFrameReceived( 1U, Globals::FRAME_TYPE_HEADERS, endStream, 0, start );

        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 0U );
        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 1U );
        UTF_REQUIRE( registry.isRecentlyClosed( 1U ) );
        UTF_REQUIRE( registry.findStream( 1U ) == nullptr );

        /*
         * It ended normally, so a WINDOW_UPDATE arriving late is ignored and a DATA frame is a
         * CONNECTION error of type STREAM_CLOSED - both decided by the remembered machine
         */

        const auto ignored =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_WINDOW_UPDATE, none, 0, start );

        UTF_REQUIRE( ignored.disposition.value() == FrameDisposition::Ignored );

        UTF_REQUIRE_THROW(
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 8, start ),
            Http2ProtocolException
            );
    }

    /*
     * A stream we reset ourselves is remembered too, and it tolerates everything
     */

    {
        StreamRegistry registry( StreamRole::Client );

        auto& stream = registry.openLocalStream();

        stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
        registry.onFrameSent( 1U, Globals::FRAME_TYPE_RST_STREAM, none, start );

        UTF_REQUIRE( registry.isRecentlyClosed( 1U ) );

        const auto result =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 64, start );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
        UTF_REQUIRE( result.isKnownStream.value() );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 64 );
    }

    /*
     * The count bound of design 4.6, oldest forgotten first
     */

    {
        StreamRegistry registry( StreamRole::Client, 3U );

        for( std::uint32_t i = 0U; i < 5U; ++i )
        {
            auto& stream = registry.openLocalStream();

            stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
            registry.onFrameSent(
                stream.streamId(),
                Globals::FRAME_TYPE_RST_STREAM,
                none,
                start
                );
        }

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 3U );

        UTF_REQUIRE( ! registry.isRecentlyClosed( 1U ) );
        UTF_REQUIRE( ! registry.isRecentlyClosed( 3U ) );
        UTF_REQUIRE( registry.isRecentlyClosed( 5U ) );
        UTF_REQUIRE( registry.isRecentlyClosed( 7U ) );
        UTF_REQUIRE( registry.isRecentlyClosed( 9U ) );

        /*
         * The two which were forgotten are still ours and still below the next identifier, so
         * frames on them are tolerated rather than treated as errors
         */

        const auto result =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 16, start );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
        UTF_REQUIRE( ! result.isKnownStream.value() );
        UTF_REQUIRE_EQUAL( result.connectionWindowBytes.value(), 16 );
    }

    /*
     * And the age bound, which is why the current time is a parameter rather than a clock read
     * inside - thirty seconds of a test would be thirty seconds of everyone's build
     */

    {
        StreamRegistry registry( StreamRole::Client, 1000U, 30U );

        for( std::uint32_t i = 0U; i < 3U; ++i )
        {
            auto& stream = registry.openLocalStream();

            stream.onFrameSent( Globals::FRAME_TYPE_HEADERS, none );
            registry.onFrameSent(
                stream.streamId(),
                Globals::FRAME_TYPE_RST_STREAM,
                none,
                start + time::seconds( static_cast< long >( i ) * 10L )
                );
        }

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 3U );

        registry.forgetClosedStreamsOlderThan( start + time::seconds( 29L ) );

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 3U );

        /*
         * The first closed at start, so at start + 30 it is exactly old enough
         */

        registry.forgetClosedStreamsOlderThan( start + time::seconds( 30L ) );

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 2U );
        UTF_REQUIRE( ! registry.isRecentlyClosed( 1U ) );
        UTF_REQUIRE( registry.isRecentlyClosed( 3U ) );

        registry.forgetClosedStreamsOlderThan( start + time::seconds( 41L ) );

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 1U );
        UTF_REQUIRE( registry.isRecentlyClosed( 5U ) );

        registry.forgetClosedStreamsOlderThan( start + time::seconds( 1000L ) );

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 0U );

        /*
         * Forgetting an empty set is a no-op rather than a mistake
         */

        registry.forgetClosedStreamsOlderThan( start + time::seconds( 2000L ) );

        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 0U );
    }
}

UTF_AUTO_TEST_CASE( StreamStates_UnknownStreamIdentifierTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::streamstates;

    static constexpr auto none = static_cast< std::uint8_t >( Globals::FRAME_FLAG_NONE );

    const auto now = time::microsec_clock::universal_time();

    /*
     * RFC 9113 5.1.1 - "An endpoint that receives an unexpected stream identifier MUST respond
     * with a connection error of type PROTOCOL_ERROR". A frame on an identifier of ours which we
     * have not opened yet is exactly that
     */

    {
        StreamRegistry registry( StreamRole::Client );

        requireHttp2Error< Http2ProtocolException >(
            [ &registry, &now ]() -> void
            {
                registry.onFrameReceived( 5U, Globals::FRAME_TYPE_DATA, none, 8, now );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR,
            5U
            );

        registry.openLocalStream();

        /*
         * Stream 1 now exists; 3 still does not
         */

        requireHttp2Error< Http2ProtocolException >(
            [ &registry, &now ]() -> void
            {
                registry.onFrameReceived( 3U, Globals::FRAME_TYPE_HEADERS, none, 0, now );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR,
            3U
            );
    }

    /*
     * A server may only create a stream by reserving it with PUSH_PROMISE, which this client
     * never permits (D11, section 8.4) - so there is no frame at all which opens an even stream
     * on a client's registry
     */

    {
        StreamRegistry registry( StreamRole::Client );

        requireHttp2Error< Http2ProtocolException >(
            [ &registry, &now ]() -> void
            {
                registry.onFrameReceived( 2U, Globals::FRAME_TYPE_HEADERS, none, 0, now );
            },
            Globals::ERROR_CODE_PROTOCOL_ERROR,
            2U
            );

        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 0U );
    }

    /*
     * A server's registry, on the other hand, opens a stream when the client sends HEADERS on a
     * fresh odd identifier - which is the whole of how a server learns about a request
     */

    {
        StreamRegistry registry( StreamRole::Server );

        const auto result =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_HEADERS, none, 0, now );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Accepted );
        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 1U );

        const auto* const stream = registry.findStream( 1U );

        UTF_REQUIRE( stream != nullptr );
        UTF_REQUIRE( stream -> state() == StreamState::Open );

        /*
         * "The first use of a new stream identifier implicitly closes all streams in the idle
         * state that might have been initiated by that peer with a lower-valued identifier" - so
         * a frame on the lower one is tolerated rather than a surprise
         */

        const auto late =
            registry.onFrameReceived( 1U, Globals::FRAME_TYPE_DATA, none, 4, now );

        UTF_REQUIRE( late.disposition.value() == FrameDisposition::Accepted );
    }

    /*
     * PRIORITY is legal on an idle stream, and this client ignores the peer's priority signals
     * entirely (design 4.7) - so it neither errors nor creates anything
     */

    {
        StreamRegistry registry( StreamRole::Client );

        const auto result =
            registry.onFrameReceived( 2U, Globals::FRAME_TYPE_PRIORITY, none, 0, now );

        UTF_REQUIRE( result.disposition.value() == FrameDisposition::Ignored );
        UTF_REQUIRE_EQUAL( registry.activeStreamCount(), 0U );
        UTF_REQUIRE_EQUAL( registry.rememberedClosedStreamCount(), 0U );
    }

    /*
     * Stream zero is the connection and must never reach the registry at all
     */

    {
        StreamRegistry registry( StreamRole::Client );

        UTF_REQUIRE_THROW(
            registry.onFrameReceived(
                Globals::STREAM_ID_CONNECTION,
                Globals::FRAME_TYPE_WINDOW_UPDATE,
                none,
                0,
                now
                ),
            UnexpectedException
            );

        UTF_REQUIRE_THROW(
            registry.onFrameSent( 1U, Globals::FRAME_TYPE_HEADERS, none, now ),
            UnexpectedException
            );
    }
}

#endif /* __UTEST_TESTSTREAMSTATES_H_ */
