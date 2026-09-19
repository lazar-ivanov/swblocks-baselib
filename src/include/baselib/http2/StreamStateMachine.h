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

#ifndef __BL_HTTP2_STREAMSTATEMACHINE_H_
#define __BL_HTTP2_STREAMSTATEMACHINE_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeUtils.h>

#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace bl
{
    namespace http2
    {
        /**
         * @brief Which end of the connection we are - RFC 9113 section 5.1.1
         *
         * The machine below is role neutral, which is what D8 requires of the whole protocol core
         * so that the test peer can be built on it. The role decides only two things: which
         * stream identifiers we may open, and which of "local" and "remote" each half-closed
         * state means
         */

        enum class StreamRole : std::uint8_t
        {
            Client,
            Server,
        };

        /**
         * @brief The seven states of RFC 9113 section 5.1
         *
         * ReservedLocal and ReservedRemote are present for completeness and are unreachable in
         * this client: D11 disables server push, the client always advertises
         * SETTINGS_ENABLE_PUSH = 0, and a PUSH_PROMISE which arrives anyway is a connection error
         * the session raises (section 8.4). Nothing here drives into them except reserveLocal( )
         * and reserveRemote( ), which the client never calls - they exist so that the states are
         * defined and tested rather than approximated by a gap in a switch
         */

        enum class StreamState : std::uint8_t
        {
            Idle,
            ReservedLocal,
            ReservedRemote,
            Open,
            HalfClosedLocal,
            HalfClosedRemote,
            Closed,
        };

        /**
         * @brief How a stream reached the closed state, which decides how later frames are judged
         *
         * RFC 9113 5.1 gives the closed state three different answers to the same arriving frame,
         * and which one applies depends entirely on how the stream got there:
         *
         *  - closed because WE sent RST_STREAM: everything which arrives is IGNORED, because the
         *    peer cannot have seen the reset yet and may have frames already in flight
         *  - closed because the PEER sent RST_STREAM: anything but PRIORITY is a STREAM error of
         *    type STREAM_CLOSED
         *  - closed normally, having received END_STREAM: WINDOW_UPDATE and RST_STREAM must be
         *    ignored for a while, and anything else is a CONNECTION error of type STREAM_CLOSED
         *
         * So a closed stream which has forgotten why it closed cannot answer correctly, which is
         * why this is carried rather than reduced to a boolean
         */

        enum class StreamCloseCause : std::uint8_t
        {
            NotClosed,
            EndStreamReceived,
            RstStreamReceived,
            RstStreamSent,
        };

        /**
         * @brief What to do with a frame which has arrived on a stream
         */

        enum class FrameDisposition : std::uint8_t
        {
            Accepted,
            Ignored,
            StreamError,
        };

        /**
         * @brief The verdict on one inbound frame
         *
         * 'connectionWindowBytes' IS SET WHATEVER THE DISPOSITION, and that is the point of
         * returning a value here rather than only throwing. A DATA frame which arrives for a
         * stream we have already reset is ignored - and it has still spent the CONNECTION
         * flow-control window, because the peer sent it before it saw our RST_STREAM and has
         * already counted it against its own (RFC 9113 5.1, "flow-controlled frames received
         * after sending RST_STREAM are counted toward the connection flow-control window"). Those
         * octets have to be given back with a WINDOW_UPDATE or the connection window leaks a
         * little on every reset stream until the connection stalls for good
         *
         * A caller which reads this field only when the frame was accepted has written that leak
         */

        struct InboundFrameResult
        {
            cpp::ScalarTypeIniter< FrameDisposition >                           disposition;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamErrorCode;
            cpp::ScalarTypeIniter< std::int32_t >                               connectionWindowBytes;
            cpp::ScalarTypeIniter< bool >                                       isKnownStream;
        };

        /**
         * @brief class StreamStateMachineT - one stream, RFC 9113 section 5.1
         *
         * Sans-I/O and role parameterized. It holds a state, a close cause and an identifier, and
         * nothing else; it owns no buffers, no windows and no header state
         *
         * WHAT IT IS NOT TOLD ABOUT, and why. CONTINUATION frames never reach it: a field block
         * is one message spread over several frames, and the END_STREAM transition belongs to the
         * message and not to the first frame of it. The frame reader (4.1) already owns header
         * block continuity, so the session assembles the block and then tells this machine about
         * one completed HEADERS - which is also why a HEADERS frame carrying END_STREAM but not
         * END_HEADERS does not close the stream out from under its own CONTINUATION frames.
         * SETTINGS, PING and GOAWAY never reach it either; they are connection frames and always
         * carry stream identifier zero (4.1)
         *
         * ERRORS. A connection error is thrown - there is no state in which the connection could
         * usefully continue. A stream error is RETURNED, because the connection must survive it:
         * the caller answers with RST_STREAM and carries on. That is the same division the frame
         * codec makes, for the same reason
         *
         * An illegal frame on the SEND side is neither: it is a programming error, because we
         * choose what we send
         */

        template
        <
            typename E = void
        >
        class StreamStateMachineT
        {
        private:

            cpp::ScalarTypeIniter< std::uint32_t >                              m_streamId;
            cpp::ScalarTypeIniter< StreamRole >                                 m_role;
            cpp::ScalarTypeIniter< StreamState >                                m_state;
            cpp::ScalarTypeIniter< StreamCloseCause >                           m_closeCause;

            SAA_noreturn
            void throwConnectionError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                ) const
            {
                BL_THROW(
                    Http2ProtocolException()
                        << eh::errinfo_http2_error_code( errorCode )
                        << eh::errinfo_http2_stream_id( m_streamId.value() ),
                    BL_MSG()
                        << "HTTP/2 connection error "
                        << Globals::errorCodeToString( errorCode )
                        << " - "
                        << reason
                        << " (stream is "
                        << stateToString( m_state.value() )
                        << ")"
                    );
            }

            void close( SAA_in const StreamCloseCause cause ) NOEXCEPT
            {
                m_state = StreamState::Closed;
                m_closeCause = cause;
            }

            static bool isEndStream(
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags
                ) NOEXCEPT
            {
                /*
                 * The END_STREAM bit has that meaning only on DATA and HEADERS (6.1, 6.2); the
                 * same bit is ACK on SETTINGS and PING, and a flag with no defined semantics for
                 * its type MUST be ignored (4.1)
                 */

                return
                    ( frameType == Globals::FRAME_TYPE_DATA ||
                      frameType == Globals::FRAME_TYPE_HEADERS ) &&
                    0U != ( flags & Globals::FRAME_FLAG_END_STREAM );
            }

            void checkFrameTypeIsRoutable( SAA_in const std::uint8_t frameType ) const
            {
                BL_CHK(
                    false,
                    frameType != Globals::FRAME_TYPE_SETTINGS &&
                        frameType != Globals::FRAME_TYPE_PING &&
                        frameType != Globals::FRAME_TYPE_GOAWAY,
                    BL_MSG()
                        << "A connection-level HTTP/2 frame was routed to a stream"
                    );

                BL_CHK(
                    false,
                    frameType != Globals::FRAME_TYPE_CONTINUATION,
                    BL_MSG()
                        << "A CONTINUATION frame was routed to a stream state machine, which is "
                        << "told about the completed header block rather than about its frames"
                    );
            }

        public:

            StreamStateMachineT(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamRole                     role
                )
                :
                m_streamId( streamId ),
                m_role( role ),
                m_state( StreamState::Idle ),
                m_closeCause( StreamCloseCause::NotClosed )
            {
                BL_CHK(
                    false,
                    streamId != Globals::STREAM_ID_CONNECTION &&
                        streamId <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "An HTTP/2 stream identifier must be non-zero and fit 31 bits"
                    );
            }

            std::uint32_t streamId() const NOEXCEPT
            {
                return m_streamId;
            }

            StreamRole role() const NOEXCEPT
            {
                return m_role;
            }

            StreamState state() const NOEXCEPT
            {
                return m_state;
            }

            StreamCloseCause closeCause() const NOEXCEPT
            {
                return m_closeCause;
            }

            bool isClosed() const NOEXCEPT
            {
                return m_state == StreamState::Closed;
            }

            bool isIdle() const NOEXCEPT
            {
                return m_state == StreamState::Idle;
            }

            /**
             * @brief RFC 9113 5.1.1 - a client opens odd-numbered streams and a server even ones
             */

            static bool isLocallyInitiatedId(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamRole                     role
                ) NOEXCEPT
            {
                const bool isOdd = 0U != ( streamId & 1U );

                return role == StreamRole::Client ? isOdd : ! isOdd;
            }

            bool isLocallyInitiated() const NOEXCEPT
            {
                return isLocallyInitiatedId( m_streamId, m_role );
            }

            static std::string stateToString( SAA_in const StreamState state )
            {
                switch( state )
                {
                    case StreamState::Idle:             return "idle";
                    case StreamState::ReservedLocal:    return "reserved (local)";
                    case StreamState::ReservedRemote:   return "reserved (remote)";
                    case StreamState::Open:             return "open";
                    case StreamState::HalfClosedLocal:  return "half-closed (local)";
                    case StreamState::HalfClosedRemote: return "half-closed (remote)";
                    case StreamState::Closed:           return "closed";
                }

                return "unknown";
            }

            /**
             * @brief Whether this frame type may be SENT in the current state - RFC 9113 5.1
             *
             * An unknown type is always allowed, because 4.1 makes an unknown type something the
             * receiver ignores rather than something the state machine judges
             */

            bool canSend( SAA_in const std::uint8_t frameType ) const NOEXCEPT
            {
                if( ! isStreamFrameType( frameType ) )
                {
                    return true;
                }

                switch( m_state.value() )
                {
                    case StreamState::Idle:

                        /*
                         * "RST_STREAM frames MUST NOT be sent for a stream in the idle state"
                         */

                        return
                            frameType == Globals::FRAME_TYPE_HEADERS ||
                            frameType == Globals::FRAME_TYPE_PRIORITY;

                    case StreamState::ReservedLocal:

                        return
                            frameType == Globals::FRAME_TYPE_HEADERS ||
                            frameType == Globals::FRAME_TYPE_RST_STREAM ||
                            frameType == Globals::FRAME_TYPE_PRIORITY;

                    case StreamState::ReservedRemote:

                        return
                            frameType == Globals::FRAME_TYPE_RST_STREAM ||
                            frameType == Globals::FRAME_TYPE_WINDOW_UPDATE ||
                            frameType == Globals::FRAME_TYPE_PRIORITY;

                    case StreamState::Open:
                    case StreamState::HalfClosedRemote:

                        return true;

                    case StreamState::HalfClosedLocal:

                        /*
                         * "cannot be used for sending frames other than WINDOW_UPDATE, PRIORITY,
                         * and RST_STREAM"
                         */

                        return
                            frameType == Globals::FRAME_TYPE_WINDOW_UPDATE ||
                            frameType == Globals::FRAME_TYPE_PRIORITY ||
                            frameType == Globals::FRAME_TYPE_RST_STREAM;

                    case StreamState::Closed:

                        /*
                         * "An endpoint MUST NOT send frames other than PRIORITY on a closed
                         * stream"
                         */

                        return frameType == Globals::FRAME_TYPE_PRIORITY;
                }

                return false;
            }

            /**
             * @brief Whether this frame type may be RECEIVED in the current state without error
             *
             * "Without error" and not "with effect": in the closed state most of what this admits
             * is ignored rather than acted on, which is what RFC 9113 5.1 requires of frames the
             * peer had already sent when it learned the stream was gone
             */

            bool canReceive( SAA_in const std::uint8_t frameType ) const NOEXCEPT
            {
                if( ! isStreamFrameType( frameType ) )
                {
                    return true;
                }

                switch( m_state.value() )
                {
                    case StreamState::Idle:

                        /*
                         * "Receiving any frame other than HEADERS or PRIORITY on a stream in this
                         * state MUST be treated as a connection error of type PROTOCOL_ERROR"
                         */

                        return
                            frameType == Globals::FRAME_TYPE_HEADERS ||
                            frameType == Globals::FRAME_TYPE_PRIORITY;

                    case StreamState::ReservedLocal:

                        return
                            frameType == Globals::FRAME_TYPE_RST_STREAM ||
                            frameType == Globals::FRAME_TYPE_PRIORITY ||
                            frameType == Globals::FRAME_TYPE_WINDOW_UPDATE;

                    case StreamState::ReservedRemote:

                        return
                            frameType == Globals::FRAME_TYPE_HEADERS ||
                            frameType == Globals::FRAME_TYPE_RST_STREAM ||
                            frameType == Globals::FRAME_TYPE_PRIORITY;

                    case StreamState::Open:
                    case StreamState::HalfClosedLocal:

                        return true;

                    case StreamState::HalfClosedRemote:

                        /*
                         * "If an endpoint receives additional frames, other than WINDOW_UPDATE,
                         * PRIORITY, or RST_STREAM, for a stream that is in this state, it MUST
                         * respond with a stream error of type STREAM_CLOSED"
                         */

                        return
                            frameType == Globals::FRAME_TYPE_WINDOW_UPDATE ||
                            frameType == Globals::FRAME_TYPE_PRIORITY ||
                            frameType == Globals::FRAME_TYPE_RST_STREAM;

                    case StreamState::Closed:

                        if( frameType == Globals::FRAME_TYPE_PRIORITY )
                        {
                            return true;
                        }

                        switch( m_closeCause.value() )
                        {
                            case StreamCloseCause::RstStreamSent:

                                /*
                                 * "An endpoint MUST ignore frames that it receives on closed
                                 * streams after it has sent a RST_STREAM frame"
                                 */

                                return true;

                            case StreamCloseCause::EndStreamReceived:

                                /*
                                 * "WINDOW_UPDATE or RST_STREAM frames can be received in this
                                 * state for a short period ... Endpoints MUST ignore
                                 * WINDOW_UPDATE or RST_STREAM frames received in this state"
                                 */

                                return
                                    frameType == Globals::FRAME_TYPE_WINDOW_UPDATE ||
                                    frameType == Globals::FRAME_TYPE_RST_STREAM;

                            case StreamCloseCause::RstStreamReceived:

                                /*
                                 * "An endpoint that receives any frame other than PRIORITY after
                                 * receiving a RST_STREAM MUST treat that as a stream error of
                                 * type STREAM_CLOSED"
                                 */

                                return false;

                            case StreamCloseCause::NotClosed:

                                break;
                        }

                        return false;
                }

                return false;
            }

            /**
             * @brief The five frame types which are associated with a stream and mean something
             * to its state
             *
             * CONTINUATION is deliberately not one of them - see the class note - and neither is
             * an unknown type, which 4.1 makes the receiver's business to ignore rather than the
             * state machine's to judge
             */

            static bool isStreamFrameType( SAA_in const std::uint8_t frameType ) NOEXCEPT
            {
                return
                    frameType == Globals::FRAME_TYPE_DATA ||
                    frameType == Globals::FRAME_TYPE_HEADERS ||
                    frameType == Globals::FRAME_TYPE_PRIORITY ||
                    frameType == Globals::FRAME_TYPE_RST_STREAM ||
                    frameType == Globals::FRAME_TYPE_PUSH_PROMISE ||
                    frameType == Globals::FRAME_TYPE_WINDOW_UPDATE;
            }

            /**
             * @brief We sent a frame on this stream - advance the state
             *
             * Sending something the state does not allow is a programming error rather than a
             * protocol error, because what we send is ours to decide. canSend( ) is the question
             * to ask first
             */

            void onFrameSent(
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags
                )
            {
                checkFrameTypeIsRoutable( frameType );

                BL_CHK(
                    false,
                    canSend( frameType ),
                    BL_MSG()
                        << "An HTTP/2 frame of type "
                        << static_cast< unsigned >( frameType )
                        << " cannot be sent on a stream which is "
                        << stateToString( m_state.value() )
                    );

                if( ! isStreamFrameType( frameType ) )
                {
                    return;
                }

                if( frameType == Globals::FRAME_TYPE_RST_STREAM )
                {
                    close( StreamCloseCause::RstStreamSent );

                    return;
                }

                switch( m_state.value() )
                {
                    case StreamState::Idle:

                        if( frameType == Globals::FRAME_TYPE_HEADERS )
                        {
                            m_state = isEndStream( frameType, flags ) ?
                                StreamState::HalfClosedLocal : StreamState::Open;
                        }
                        break;

                    case StreamState::ReservedLocal:

                        if( frameType == Globals::FRAME_TYPE_HEADERS )
                        {
                            m_state = StreamState::HalfClosedRemote;
                        }
                        break;

                    case StreamState::Open:

                        if( isEndStream( frameType, flags ) )
                        {
                            m_state = StreamState::HalfClosedLocal;
                        }
                        break;

                    case StreamState::HalfClosedRemote:

                        if( isEndStream( frameType, flags ) )
                        {
                            /*
                             * Both ends are done. The peer's END_STREAM arrived first, which is
                             * what a frame arriving after this one has to be judged against
                             */

                            close( StreamCloseCause::EndStreamReceived );
                        }
                        break;

                    case StreamState::ReservedRemote:
                    case StreamState::HalfClosedLocal:
                    case StreamState::Closed:

                        break;
                }
            }

            /**
             * @brief A frame arrived on this stream - judge it and advance the state
             *
             * 'payloadLength' is the Length field of the frame header, and it is what a DATA
             * frame costs the connection flow-control window: the whole payload counts, the Pad
             * Length octet and the padding with it (6.1). It is carried back in the result
             * whatever the verdict - see InboundFrameResult
             */

            InboundFrameResult onFrameReceived(
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags,
                SAA_in          const std::int32_t                   payloadLength
                )
            {
                checkFrameTypeIsRoutable( frameType );

                BL_CHK(
                    false,
                    payloadLength >= 0,
                    BL_MSG()
                        << "A frame cannot have a negative payload length"
                    );

                InboundFrameResult result;

                result.isKnownStream = true;
                result.connectionWindowBytes =
                    frameType == Globals::FRAME_TYPE_DATA ? payloadLength : 0;

                if( ! isStreamFrameType( frameType ) )
                {
                    result.disposition = FrameDisposition::Ignored;

                    return result;
                }

                if( ! canReceive( frameType ) )
                {
                    raiseInboundViolation( frameType, result );

                    return result;
                }

                if( m_state == StreamState::Closed )
                {
                    /*
                     * Everything canReceive( ) admits in the closed state is tolerated rather
                     * than acted on - and a DATA frame among it has still spent the connection
                     * window, which is why the octets are in the result either way
                     */

                    result.disposition = FrameDisposition::Ignored;

                    return result;
                }

                result.disposition = FrameDisposition::Accepted;

                if( frameType == Globals::FRAME_TYPE_RST_STREAM )
                {
                    close( StreamCloseCause::RstStreamReceived );

                    return result;
                }

                switch( m_state.value() )
                {
                    case StreamState::Idle:

                        if( frameType == Globals::FRAME_TYPE_HEADERS )
                        {
                            m_state = isEndStream( frameType, flags ) ?
                                StreamState::HalfClosedRemote : StreamState::Open;
                        }
                        break;

                    case StreamState::ReservedRemote:

                        if( frameType == Globals::FRAME_TYPE_HEADERS )
                        {
                            m_state = StreamState::HalfClosedLocal;
                        }
                        break;

                    case StreamState::Open:

                        if( isEndStream( frameType, flags ) )
                        {
                            m_state = StreamState::HalfClosedRemote;
                        }
                        break;

                    case StreamState::HalfClosedLocal:

                        if( isEndStream( frameType, flags ) )
                        {
                            close( StreamCloseCause::EndStreamReceived );
                        }
                        break;

                    case StreamState::ReservedLocal:
                    case StreamState::HalfClosedRemote:
                    case StreamState::Closed:

                        break;
                }

                return result;
            }

            /**
             * @brief The stream is promised by a PUSH_PROMISE we sent - RFC 9113 5.1
             *
             * Unreachable in this client (D11) and present so that the state is defined. A server
             * built on this core - the test peer of design 8.2 - is what reaches it
             */

            void reserveLocal()
            {
                checkIsIdleForReservation();

                m_state = StreamState::ReservedLocal;
            }

            /**
             * @brief The stream is promised by a PUSH_PROMISE the peer sent - RFC 9113 5.1
             *
             * Unreachable in this client for a stronger reason than reserveLocal( ): the client
             * advertises SETTINGS_ENABLE_PUSH = 0, so a PUSH_PROMISE which arrives at all is a
             * connection error the session raises before any stream could be reserved (8.4)
             */

            void reserveRemote()
            {
                checkIsIdleForReservation();

                m_state = StreamState::ReservedRemote;
            }

        private:

            void checkIsIdleForReservation() const
            {
                BL_CHK(
                    false,
                    m_state == StreamState::Idle,
                    BL_MSG()
                        << "Only an idle HTTP/2 stream can be reserved"
                    );
            }

            /**
             * @brief The frame is not allowed here - which error that is depends on where here is
             */

            void raiseInboundViolation(
                SAA_in          const std::uint8_t                   frameType,
                SAA_inout       InboundFrameResult&                  result
                ) const
            {
                if(
                    m_state == StreamState::Idle ||
                    m_state == StreamState::ReservedLocal ||
                    m_state == StreamState::ReservedRemote
                    )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "a frame of type " + std::to_string( static_cast< unsigned >( frameType ) ) +
                            " arrived on a stream which cannot carry one"
                        );
                }

                if(
                    m_state == StreamState::Closed &&
                    m_closeCause == StreamCloseCause::EndStreamReceived
                    )
                {
                    /*
                     * "an endpoint that receives any frames after receiving a frame with the
                     * END_STREAM flag set MUST treat that as a CONNECTION error of type
                     * STREAM_CLOSED" - the one place where STREAM_CLOSED is not a stream error
                     */

                    throwConnectionError(
                        Globals::ERROR_CODE_STREAM_CLOSED,
                        "a frame arrived after the peer had ended the stream"
                        );
                }

                /*
                 * What is left is half-closed (remote) and a stream closed by the peer's
                 * RST_STREAM - both stream errors of type STREAM_CLOSED, so the connection lives
                 * and the caller answers with a RST_STREAM of its own
                 */

                result.disposition = FrameDisposition::StreamError;
                result.streamErrorCode = Globals::ERROR_CODE_STREAM_CLOSED;
            }
        };

        typedef StreamStateMachineT<> StreamStateMachine;

        /**
         * @brief class StreamRegistryT - every stream of one connection, and the ones just gone
         *
         * The state machine above knows one stream. This knows the set of them, and the three
         * things which are properties of the set rather than of any member:
         *
         *  - WHICH IDENTIFIERS ARE OURS TO OPEN. RFC 9113 5.1.1 - a client opens odd streams, a
         *    server even ones, each new one numerically greater than the last. They are never
         *    reused, so a long-lived connection eventually runs out, and running out is not an
         *    error: the connection is DRAINING and the pool opens another (design 4.3)
         *
         *  - WHICH STREAMS JUST CLOSED. A peer which has not yet seen our RST_STREAM goes on
         *    sending on a stream we consider gone, and 5.1 requires that we tolerate that rather
         *    than kill the connection over it. Closed streams are therefore remembered, bounded
         *    by both a count and an age (the defaults of design 4.6, in Globals), the oldest
         *    forgotten first. What is remembered is the whole state machine and not just the
         *    identifier, because how a stream closed is what decides the answer to the next frame
         *    on it - so a remembered stream judges its own frames with the same code an open one
         *    does, and there is no second copy of the closed-state rules to drift
         *
         *  - WHAT A FRAME ON AN UNKNOWN STREAM MEANS. Three different things, told apart by the
         *    identifier alone: one we have not opened yet is "unexpected" and a connection error
         *    (5.1.1); one we opened and have since forgotten is tolerated; and one the PEER may
         *    open is a new stream if the peer is a client, and a connection error if we are one -
         *    because a server may only create a stream by reserving it with PUSH_PROMISE, which
         *    this client never permits (D11, section 8.4)
         *
         * Sans-I/O. The current time is a parameter rather than a clock read inside, so that the
         * age bound is testable and the core stays free of anything ambient
         */

        template
        <
            typename E = void
        >
        class StreamRegistryT
        {
        private:

            struct ClosedStreamEntry
            {
                StreamStateMachine                                  machine;
                time::ptime                                         closedAt;

                ClosedStreamEntry(
                    SAA_in          const StreamStateMachine&       machineIn,
                    SAA_in          const time::ptime&              closedAtIn
                    )
                    :
                    machine( machineIn ),
                    closedAt( closedAtIn )
                {
                }
            };

            typedef std::map< std::uint32_t, StreamStateMachine >    active_streams_t;
            typedef std::map< std::uint32_t, ClosedStreamEntry >     closed_streams_t;

            cpp::ScalarTypeIniter< StreamRole >                                 m_role;

            active_streams_t                                                    m_active;
            closed_streams_t                                                    m_closed;

            /*
             * Insertion order, which is close order, so both bounds are prefixes of it
             */

            std::deque< std::uint32_t >                                         m_closedOrder;

            cpp::ScalarTypeIniter< std::uint32_t >                              m_nextLocalStreamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_highestPeerStreamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_maxRememberedClosedStreams;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_closedStreamTimeoutInSeconds;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_drainingReserve;
            cpp::ScalarTypeIniter< bool >                                       m_isDrainingExplicitly;

            SAA_noreturn
            void throwUnexpectedStreamId(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::string&                   reason
                ) const
            {
                BL_THROW(
                    Http2ProtocolException()
                        << eh::errinfo_http2_error_code( Globals::ERROR_CODE_PROTOCOL_ERROR )
                        << eh::errinfo_http2_stream_id( streamId ),
                    BL_MSG()
                        << "HTTP/2 connection error "
                        << Globals::errorCodeToString( Globals::ERROR_CODE_PROTOCOL_ERROR )
                        << " - "
                        << reason
                    );
            }

            void retire(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamStateMachine&            machine,
                SAA_in          const time::ptime&                   now
                )
            {
                m_closed.insert(
                    typename closed_streams_t::value_type(
                        streamId,
                        ClosedStreamEntry( machine, now )
                        )
                    );

                m_closedOrder.push_back( streamId );
                m_active.erase( streamId );

                while( m_closedOrder.size() > m_maxRememberedClosedStreams )
                {
                    m_closed.erase( m_closedOrder.front() );
                    m_closedOrder.pop_front();
                }
            }

            InboundFrameResult onFrameForUnknownStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags,
                SAA_in          const std::int32_t                   payloadLength,
                SAA_in          const time::ptime&                   now
                )
            {
                InboundFrameResult result;

                result.isKnownStream = false;
                result.disposition = FrameDisposition::Ignored;
                result.connectionWindowBytes =
                    frameType == Globals::FRAME_TYPE_DATA ? payloadLength : 0;

                if( StreamStateMachine::isLocallyInitiatedId( streamId, m_role.value() ) )
                {
                    if( streamId >= m_nextLocalStreamId )
                    {
                        throwUnexpectedStreamId(
                            streamId,
                            "a frame arrived on a stream identifier we have not opened"
                            );
                    }

                    /*
                     * Ours, opened, closed and since forgotten. 5.1 lets an endpoint limit how
                     * long it ignores frames on a closed stream and treat later ones as an error;
                     * this client takes the tolerant half of that, because the alternative is
                     * killing a connection over a frame which was in flight
                     */

                    return result;
                }

                if( streamId <= m_highestPeerStreamId )
                {
                    /*
                     * The peer's, and already used - so it too was closed and forgotten
                     */

                    return result;
                }

                /*
                 * The peer's, and new. Only a client may create a stream by sending HEADERS on
                 * it; a server creates one by reserving it with PUSH_PROMISE, which this client
                 * never permits (D11). So when WE are the client, there is no frame at all which
                 * may open a peer-initiated stream
                 */

                if( frameType == Globals::FRAME_TYPE_PRIORITY )
                {
                    /*
                     * PRIORITY is legal on an idle stream (5.1) and this client ignores the
                     * peer's priority signals entirely (design 4.7), so nothing is created for it
                     */

                    return result;
                }

                if( m_role == StreamRole::Server && frameType == Globals::FRAME_TYPE_HEADERS )
                {
                    m_highestPeerStreamId = streamId;

                    auto inserted = m_active.insert(
                        typename active_streams_t::value_type(
                            streamId,
                            StreamStateMachine( streamId, m_role.value() )
                            )
                        );

                    result = inserted.first -> second.onFrameReceived(
                        frameType,
                        flags,
                        payloadLength
                        );

                    if( inserted.first -> second.isClosed() )
                    {
                        retire( streamId, inserted.first -> second, now );
                    }

                    return result;
                }

                throwUnexpectedStreamId(
                    streamId,
                    "a frame arrived on a stream the peer is not allowed to open this way"
                    );
            }

        public:

            explicit StreamRegistryT(
                SAA_in          const StreamRole                     role,
                SAA_in_opt      const std::uint32_t                  maxRememberedClosedStreams =
                                    Globals::MAX_REMEMBERED_CLOSED_STREAMS_DEFAULT,
                SAA_in_opt      const std::uint32_t                  closedStreamTimeoutInSeconds =
                                    Globals::REMEMBERED_CLOSED_STREAM_TIMEOUT_IN_SECONDS_DEFAULT
                )
                :
                m_role( role ),
                m_nextLocalStreamId( role == StreamRole::Client ? 1U : 2U ),
                m_maxRememberedClosedStreams( maxRememberedClosedStreams ),
                m_closedStreamTimeoutInSeconds( closedStreamTimeoutInSeconds )
            {
            }

            StreamRole role() const NOEXCEPT
            {
                return m_role;
            }

            std::size_t activeStreamCount() const NOEXCEPT
            {
                return m_active.size();
            }

            std::size_t rememberedClosedStreamCount() const NOEXCEPT
            {
                return m_closedOrder.size();
            }

            bool isRecentlyClosed( SAA_in const std::uint32_t streamId ) const
            {
                return m_closed.find( streamId ) != m_closed.end();
            }

            StreamStateMachine* findStream( SAA_in const std::uint32_t streamId )
            {
                const auto it = m_active.find( streamId );

                return it != m_active.end() ? &it -> second : nullptr;
            }

            const StreamStateMachine* findStream( SAA_in const std::uint32_t streamId ) const
            {
                const auto it = m_active.find( streamId );

                return it != m_active.end() ? &it -> second : nullptr;
            }

            /**
             * @brief The identifier openLocalStream( ) would allocate next
             */

            std::uint32_t nextLocalStreamId() const NOEXCEPT
            {
                return m_nextLocalStreamId;
            }

            /**
             * @brief How many more streams this connection can ever open
             *
             * RFC 9113 5.1.1 - identifiers are never reused, so this only falls
             */

            std::uint32_t remainingLocalStreams() const NOEXCEPT
            {
                if( m_nextLocalStreamId > Globals::MAX_STREAM_ID )
                {
                    return 0U;
                }

                return ( Globals::MAX_STREAM_ID - m_nextLocalStreamId ) / 2U + 1U;
            }

            /**
             * @brief Whether the pool should stop sending requests here and open another
             * connection
             *
             * True once the identifier space is spent, or once a GOAWAY or the pool itself has
             * said so. THE MARGIN IS NOT DECIDED HERE: design 4.3 says a connection "approaching
             * 2^31-1" is marked draining without saying how close, and how much room the pool
             * needs for what it already has queued is the pool's business (5.4), so the reserve
             * defaults to none and is set from outside
             */

            bool isDraining() const NOEXCEPT
            {
                return m_isDrainingExplicitly || remainingLocalStreams() <= m_drainingReserve;
            }

            void markDraining() NOEXCEPT
            {
                m_isDrainingExplicitly = true;
            }

            std::uint32_t drainingReserve() const NOEXCEPT
            {
                return m_drainingReserve;
            }

            void setDrainingReserve( SAA_in const std::uint32_t reserve ) NOEXCEPT
            {
                m_drainingReserve = reserve;
            }

            bool canOpenLocalStream() const NOEXCEPT
            {
                return ! isDraining() && remainingLocalStreams() != 0U;
            }

            /**
             * @brief Opens the next stream of ours and returns it, in the idle state
             *
             * Opening one when the connection is draining is a programming error rather than a
             * protocol error - canOpenLocalStream( ) is the question, and the pool is expected to
             * have asked it before it got this far
             */

            StreamStateMachine& openLocalStream()
            {
                BL_CHK(
                    false,
                    canOpenLocalStream(),
                    BL_MSG()
                        << "An HTTP/2 connection which is draining cannot open another stream"
                    );

                const auto streamId = m_nextLocalStreamId.value();

                m_nextLocalStreamId = streamId + 2U;

                const auto inserted = m_active.insert(
                    typename active_streams_t::value_type(
                        streamId,
                        StreamStateMachine( streamId, m_role.value() )
                        )
                    );

                BL_CHK(
                    false,
                    inserted.second,
                    BL_MSG()
                        << "An HTTP/2 stream identifier was allocated twice"
                    );

                return inserted.first -> second;
            }

            /**
             * @brief We sent a frame on a stream - advance it, and retire it if that closed it
             */

            void onFrameSent(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags,
                SAA_in          const time::ptime&                   now
                )
            {
                const auto it = m_active.find( streamId );

                if( it != m_active.end() )
                {
                    it -> second.onFrameSent( frameType, flags );

                    if( it -> second.isClosed() )
                    {
                        retire( streamId, it -> second, now );
                    }

                    return;
                }

                const auto closedIt = m_closed.find( streamId );

                BL_CHK(
                    false,
                    closedIt != m_closed.end(),
                    BL_MSG()
                        << "A frame was sent on an HTTP/2 stream which does not exist"
                    );

                closedIt -> second.machine.onFrameSent( frameType, flags );
            }

            /**
             * @brief A frame arrived on a stream - route it and say what to do with it
             *
             * The result carries the connection flow-control octets WHATEVER the verdict, which
             * is the whole reason this returns a value. See InboundFrameResult
             */

            InboundFrameResult onFrameReceived(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint8_t                   frameType,
                SAA_in          const std::uint8_t                   flags,
                SAA_in          const std::int32_t                   payloadLength,
                SAA_in          const time::ptime&                   now
                )
            {
                BL_CHK(
                    false,
                    streamId != Globals::STREAM_ID_CONNECTION,
                    BL_MSG()
                        << "A connection-level HTTP/2 frame was routed to the stream registry"
                    );

                const auto it = m_active.find( streamId );

                if( it != m_active.end() )
                {
                    const auto result =
                        it -> second.onFrameReceived( frameType, flags, payloadLength );

                    if( it -> second.isClosed() )
                    {
                        retire( streamId, it -> second, now );
                    }

                    return result;
                }

                const auto closedIt = m_closed.find( streamId );

                if( closedIt != m_closed.end() )
                {
                    /*
                     * A stream we remember closing judges the frame itself, with the same code an
                     * open one uses - which is what keeps "ignore everything after we reset it"
                     * and "a frame after the peer ended it is a connection error" from being
                     * written twice
                     */

                    return closedIt -> second.machine.onFrameReceived(
                        frameType,
                        flags,
                        payloadLength
                        );
                }

                return onFrameForUnknownStream( streamId, frameType, flags, payloadLength, now );
            }

            /**
             * @brief Forgets the closed streams which have been remembered long enough
             *
             * The count bound is applied as streams close; this is the age bound, and it is a
             * separate call because it needs a clock reading rather than an event. The session
             * has a timer already
             */

            void forgetClosedStreamsOlderThan( SAA_in const time::ptime& now )
            {
                const auto timeout = time::seconds( m_closedStreamTimeoutInSeconds.value() );

                while( ! m_closedOrder.empty() )
                {
                    const auto oldest = m_closedOrder.front();
                    const auto entry = m_closed.find( oldest );

                    if( entry == m_closed.end() )
                    {
                        m_closedOrder.pop_front();

                        continue;
                    }

                    if( now - entry -> second.closedAt < timeout )
                    {
                        break;
                    }

                    m_closed.erase( entry );
                    m_closedOrder.pop_front();
                }
            }
        };

        typedef StreamRegistryT<> StreamRegistry;

    } // http2

} // bl

#endif /* __BL_HTTP2_STREAMSTATEMACHINE_H_ */
