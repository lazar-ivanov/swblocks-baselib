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

#ifndef __BL_HTTP2_SESSION_H_
#define __BL_HTTP2_SESSION_H_

#include <baselib/http2/FlowControlWindow.h>
#include <baselib/http2/FrameCodec.h>
#include <baselib/http2/Globals.h>
#include <baselib/http2/HpackDecoder.h>
#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/HpackEncoder.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/http2/StreamStateMachine.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeUtils.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace bl
{
    namespace http2
    {
        /**
         * @brief The RFC 9218 priority signal a stream is scheduled by
         *
         * Urgency 0 is the most urgent and 7 the least; 3 is the RFC's default. 'incremental' says
         * the response is useful in pieces, which is what decides whether a stream shares its
         * urgency band round-robin or holds it until its body is done
         *
         * This is NOT Http2HeadersPriority. That one is the deprecated RFC 7540 dependency tree,
         * which the profile emits on HEADERS because a browser does and which this engine never
         * schedules by (design 4.7). This one is the scheme RFC 9218 replaced it with, and it is
         * the only thing the write scheduler below looks at
         */

        struct StreamPriority
        {
            std::uint8_t                        urgency = 3U;
            bool                                incremental = false;
        };

        /**
         * @brief The client-side limits of design 4.6, every one of them configurable
         *
         * The defaults are Globals', which is where the design's table lives. Two rows of that
         * table are deliberately absent, because they are not the session's:
         *
         *  - the buffered-mode response body cap belongs to the request task (design 5.3), which
         *    is the only layer that has a buffered mode at all - this engine hands every DATA
         *    payload to its caller and buffers no body
         *  - the retry budget belongs to the connection pool (D6, design 5.4). What this engine
         *    owes that budget is the RETRYABLE FLAG on a stream-closed event, and it carries it
         *
         * 'maxDecodedHeaderListSize' is the row a profile can also speak to: it is the floor this
         * client enforces whatever the profile advertises as SETTINGS_MAX_HEADER_LIST_SIZE, and a
         * profile advertising MORE raises it, because that is what we told the peer we accept
         *
         * 'settingsTimeoutInSeconds' is not from that table and not from the RFC, which gives
         * SETTINGS_TIMEOUT no numeric value. It is this engine's own, and onTimer( ) is what
         * enforces it
         */

        struct SessionLimits
        {
            std::uint32_t   maxDecodedHeaderListSize =
                                Globals::MAX_DECODED_HEADER_LIST_SIZE_DEFAULT;

            std::uint32_t   maxCompressedHeaderBlockSize =
                                Globals::MAX_COMPRESSED_HEADER_BLOCK_SIZE_DEFAULT;

            std::uint32_t   maxContinuationFramesPerBlock =
                                Globals::MAX_CONTINUATION_FRAMES_PER_BLOCK_DEFAULT;

            std::uint32_t   maxQueuedControlFrameBytes =
                                Globals::MAX_QUEUED_CONTROL_FRAME_BYTES_DEFAULT;

            std::uint32_t   maxInboundPingAndSettingsPerSecond =
                                Globals::MAX_INBOUND_PING_AND_SETTINGS_PER_SECOND_DEFAULT;

            std::uint32_t   maxRememberedClosedStreams =
                                Globals::MAX_REMEMBERED_CLOSED_STREAMS_DEFAULT;

            std::uint32_t   rememberedClosedStreamTimeoutInSeconds =
                                Globals::REMEMBERED_CLOSED_STREAM_TIMEOUT_IN_SECONDS_DEFAULT;

            std::uint32_t   settingsTimeoutInSeconds = 10U;
        };

        /**
         * @brief What a drained event is
         *
         * One flat type rather than a variant: every consumer of this engine is a switch on the
         * type, C++11 has no variant, and a hierarchy would put an allocation and a dynamic cast
         * in the path of every DATA frame
         */

        enum class SessionEventType : std::uint8_t
        {
            Headers,
            Data,
            StreamClosed,
            SettingsReceived,
            SettingsAcknowledged,
            PingAcknowledged,
            GoAwayReceived,
            ConnectionError,
        };

        /**
         * @brief One event, drained by the caller after feed( ) returns
         *
         * Which fields carry anything depends on 'type':
         *
         *  - Headers               streamId, fields, status, endStream, isInformational, isTrailers
         *  - Data                  streamId, data, endStream
         *  - StreamClosed          streamId, errorCode, isRetryable, isMessageComplete
         *  - SettingsReceived      settings, in the order the peer sent them
         *  - SettingsAcknowledged  settings - ours, the ones that have just taken effect
         *  - PingAcknowledged      pingData, the eight opaque octets echoed back
         *  - GoAwayReceived        errorCode, lastStreamId, debugData
         *  - ConnectionError       errorCode, reason
         *
         * 'debugData' is arbitrary peer-controlled bytes and 'reason' is a string this library
         * wrote; the first is never logged unescaped and the second always may be
         */

        struct SessionEvent
        {
            cpp::ScalarTypeIniter< SessionEventType >                           type;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              errorCode;
            cpp::ScalarTypeIniter< std::uint32_t >                              lastStreamId;
            cpp::ScalarTypeIniter< unsigned >                                   status;
            cpp::ScalarTypeIniter< bool >                                       endStream;
            cpp::ScalarTypeIniter< bool >                                       isInformational;
            cpp::ScalarTypeIniter< bool >                                       isTrailers;
            cpp::ScalarTypeIniter< bool >                                       isRetryable;
            cpp::ScalarTypeIniter< bool >                                       isMessageComplete;

            HpackFieldList                                                      fields;
            std::vector< Http2Setting >                                         settings;
            std::string                                                         data;
            std::string                                                         debugData;
            std::string                                                         pingData;
            std::string                                                         reason;
        };

        /**
         * @brief What submitRequest( ) needs - everything but the pseudo-header ORDER
         *
         * The order is the profile's (design 6.4), because it is part of the fingerprint, so the
         * four pseudo-headers arrive here named rather than as a list the caller has already
         * arranged. 'headers' is the caller's ordered list of regular fields; the encoder
         * lowercases the names as it writes them (RFC 9113 8.2.1)
         */

        struct SessionRequest
        {
            std::string                                                         method;
            std::string                                                         scheme;
            std::string                                                         authority;
            std::string                                                         path;
            http::HeaderList                                                    headers;
            StreamPriority                                                      priority;
            cpp::ScalarTypeIniter< bool >                                       hasBody;
        };

        /**
         * @brief class SessionT - the HTTP/2 connection engine, design 4.5 and 4.6
         *
         * The counterpart of nghttp2's session, and the thing every layer above drives.
         * Role-neutral (D8), sans-I/O, and SINGLE-THREADED BY CONTRACT - the connection's strand
         * is what provides that, and nothing here takes a lock
         *
         * PULL STYLE, BOTH DIRECTIONS, and this is the property everything else rests on.
         * feed( ) parses and appends to an event queue; it calls nothing back, so no consumer can
         * re-enter the engine mid-parse, which is where callback-driven protocol engines go wrong.
         * The caller drains the queue afterwards. Outbound the caller asks wantsWrite( ) and
         * produce( ). The engine is therefore a pure function of its input and is tested as one:
         * feed exact bytes, assert the exact events and the exact bytes produced
         *
         * ERRORS DO NOT ESCAPE feed( ). A connection error becomes a ConnectionError event plus a
         * queued GOAWAY plus a StreamClosed for every live stream, and the session stops parsing;
         * a stream error becomes a RST_STREAM and a StreamClosed, and the connection carries on. A
         * programming error - ours - is still an UnexpectedException and still escapes, because it
         * is not a thing a peer can cause
         *
         * ------------------------------------------------------------------------------------
         * THE FOUR CONTRACTS THIS SLICE WAS ASKED TO DECIDE, AND WHAT WAS DECIDED
         * ------------------------------------------------------------------------------------
         *
         * 1. THE STATE MACHINE IS CALLED ONCE PER COMPLETED HEADER BLOCK, NOT ONCE PER FRAME.
         *    CONTINUATION is not routable to it at all - StreamStateMachine checks that and
         *    throws - because a field block is one message and the END_STREAM transition belongs
         *    to the message, not to the frame that happened to carry the first of it. A HEADERS
         *    frame carrying END_STREAM without END_HEADERS would otherwise close the stream out
         *    from under its own CONTINUATION frames. So the fragments are accumulated, and on
         *    END_HEADERS exactly one onFrameReceived( HEADERS, flags-of-the-opening-frame ) is
         *    made. FrameReaderT already owns block continuity and is trusted for it
         *
         *    THE ORDER INSIDE THAT COMPLETION IS ALSO A CONTRACT: decode, then judge, then
         *    transition. Decoding comes first because a block we are going to refuse must still
         *    be decoded, or the HPACK dynamic table desynchronises from the peer's encoder and
         *    the NEXT stream's block - a perfectly good one - fails with COMPRESSION_ERROR and
         *    takes the connection down. Judging comes before the transition because a malformed
         *    FINAL response carries END_STREAM: transition first and the stream is closed, and
         *    the RST_STREAM which 8.1 demands cannot then be sent at all (5.1). Both orderings
         *    are silent when they are wrong - the first fails one connection later, the second
         *    just stops answering - which is why they are stated here
         *
         *    THE ORDERING HALF OF THIS HOLDS ON THE DATA PATH TOO. A DATA frame carrying
         *    END_STREAM ends an ordinary client stream, which was HalfClosedLocal, so anything
         *    the frame is judged for AFTER the transition - a content-length total that does not
         *    add up, a stream window it does not fit - is diagnosed on a stream that can no longer
         *    carry the RST_STREAM. handleData( ) judges first, for exactly the reason above
         *
         * 2. THE HPACK DECODER'S CAPACITY WHEN WE ADVERTISE BELOW 4096. Decided: a setter was
         *    added to HpackDecoderT, the decoder is constructed at max( advertised, 4096 ), and
         *    the ceiling drops to what we advertised when the peer ACKNOWLEDGES our SETTINGS
         *
         *    The alternative the work order offered - construct the decoder after the ack - is
         *    not merely awkward, it is WRONG, and that is why it was not taken. At the moment the
         *    ack arrives the peer's dynamic table is NOT empty: it holds whatever fitted in the
         *    new capacity, and the peer goes on naming those entries by index. A decoder
         *    constructed fresh at that point has an empty table and cannot resolve them. The
         *    setter moves only the ceiling; the table's own capacity follows the peer's dynamic
         *    table size update, which RFC 7541 4.2 requires at the start of the first block after
         *    the change, and that update is what evicts on both sides at the same point
         *
         * 3. OUR OWN SETTINGS TAKE EFFECT WHEN THE PEER ACKNOWLEDGES THEM (RFC 9113 6.5.3), never
         *    when they are sent. Two sets are kept - in effect and in flight - with a queue of
         *    unacknowledged frames drained in order, because a peer acknowledges in order. The ack
         *    is what applies SETTINGS_MAX_FRAME_SIZE to the frame reader, what applies
         *    SETTINGS_INITIAL_WINDOW_SIZE to the RECEIVE windows, what lowers the HPACK ceiling of
         *    contract 2, and what stops the SETTINGS_TIMEOUT timer
         *
         *    SETTINGS_MAX_HEADER_LIST_SIZE IS THE ONE THAT DOES NOT BELONG TO THIS RULE, and the
         *    rule was over-applied to it once. The settings above all say what the peer may
         *    LEGALLY send, so the peer is entitled to the old value until it acknowledges; 6.5.2
         *    makes this one advisory - exceeding it is no violation and a block over it is ours to
         *    refuse whenever we like. Enforced only from the ack it is not enforced at all for a
         *    profile which advertises none, and HPACK expands far enough for that to matter, so
         *    the bound is a limits row applied FROM CONSTRUCTION and the ack merely applies what
         *    we advertised on top of it
         *
         *    The receive-side window is the instance of this rule that bites: applied at send
         *    time, a window we have just made smaller makes onDataReceived( ) raise
         *    FLOW_CONTROL_ERROR on data the peer sent legally under the old value, because every
         *    such octet precedes the ack on the wire. ReceiveFlowControlWindow grew an
         *    applyInitialWindowSizeChange( ) for this, called from the ack and from nowhere else
         *
         * 4. A STREAM ERROR REPORTED ON A CLOSED STREAM IS IGNORED - no RST_STREAM is sent and the
         *    registry is not told. RFC 9113 contradicts itself here: 5.1 makes a frame on a stream
         *    the peer reset a stream error of type STREAM_CLOSED, 5.4.2 says a stream error is
         *    answered with RST_STREAM, and 5.1 also says an endpoint MUST NOT send frames other
         *    than PRIORITY on a closed stream - which StreamStateMachine::canSend enforces, so the
         *    naive path is an UnexpectedException against a peer that is merely late
         *
         *    Ignoring is what nghttp2 does and it is the reading that survives contact with a real
         *    peer. The reason is the prohibition and the crash above, not a reset loop - a peer
         *    which has sent RST_STREAM must ignore ours (5.1), so there is no loop to invite and
         *    that argument should not be made. The connection flow-control window is still
         *    credited back, whatever the disposition - InboundFrameResult carries those octets
         *    for exactly this reason.
         *    HalfClosedRemote is NOT closed and has no such problem, so a stream error there is
         *    answered with RST_STREAM in the ordinary way
         *
         * ------------------------------------------------------------------------------------
         *
         * WRITE SCHEDULING. Control frames - SETTINGS and its ack, PING and its ack,
         * WINDOW_UPDATE, RST_STREAM, GOAWAY, the profile's PRIORITY frames - precede everything. A
         * header block and its CONTINUATION frames are serialized together and queued as one unit,
         * so nothing can ever be interleaved into one. DATA is scheduled by RFC 9218 urgency, each
         * frame bounded by the peer's SETTINGS_MAX_FRAME_SIZE and by both windows, and request
         * bodies are PULLED: bodyBytesWanted( ) says how much the engine can place right now, and
         * that is all a caller may hand it, so an upload never buffers ahead of the peer
         *
         * TIME IS A PARAMETER, not a clock read inside, exactly as StreamRegistry has it - the age
         * bound on closed streams, the inbound frame-rate limit and the SETTINGS timeout are all
         * testable as a result. feed( ), produce( ) and onTimer( ) take it; the command entry
         * points use the most recent of those, which on a strand is microseconds old
         */

        template
        <
            typename E = void
        >
        class SessionT FINAL
        {
            BL_NO_COPY_OR_MOVE( SessionT )

        public:

            typedef std::vector< std::uint8_t >                 wire_buffer_t;

        private:

            /**
             * @brief Everything the engine keeps about one live stream
             *
             * The STATE of the stream is not here - StreamRegistry owns that, and there is no
             * second copy of it to drift. What is here is what the registry has no business
             * knowing: the two windows, the priority, the body not yet placed, and the message
             * bookkeeping that RFC 9113 8.1 needs in order to call a message complete
             */

            struct StreamContext
            {
                FlowControlWindow                               sendWindow;
                ReceiveFlowControlWindow                        receiveWindow;
                StreamPriority                                  priority;

                std::string                                     pendingBody;
                std::string                                     closeReason;

                cpp::ScalarTypeIniter< bool >                   pendingBodyEndStream;
                cpp::ScalarTypeIniter< bool >                   localEndStreamQueued;
                cpp::ScalarTypeIniter< bool >                   headersReceived;
                cpp::ScalarTypeIniter< bool >                   trailersReceived;
                cpp::ScalarTypeIniter< bool >                   messageComplete;
                cpp::ScalarTypeIniter< bool >                   expectsNoContent;
                cpp::ScalarTypeIniter< bool >                   sawContentLength;
                cpp::ScalarTypeIniter< std::int64_t >           declaredContentLength;
                cpp::ScalarTypeIniter< std::int64_t >           receivedDataBytes;
                cpp::ScalarTypeIniter< std::uint32_t >          closeErrorCode;
                cpp::ScalarTypeIniter< unsigned >               status;

                StreamContext(
                    SAA_in          const std::uint32_t         streamId,
                    SAA_in          const std::int32_t          initialSendWindow,
                    SAA_in          const std::int32_t          initialReceiveWindow
                    )
                    :
                    sendWindow( streamId, initialSendWindow ),
                    receiveWindow( streamId, initialReceiveWindow )
                {
                }
            };

            /**
             * @brief What judging a completed header block establishes, for delivering it
             *
             * Separate from the reason string so that the judgement can be made BEFORE the state
             * machine is told about the block - see completeHeaderBlock( ) - and its findings
             * still reach the recording afterwards
             */

            struct BlockVerdict
            {
                unsigned            status              = 0U;
                bool                sawContentLength    = false;
                std::int64_t        contentLength       = 0;
                bool                isTrailerSection    = false;
                bool                isInformational     = false;
            };

            /**
             * @brief A SETTINGS frame of ours the peer has not acknowledged yet
             *
             * The send time travels WITH the frame because the SETTINGS_TIMEOUT is a property of
             * the frame and not of the session: kept as one timestamp overwritten by every send,
             * a peer which withholds the acknowledgement of an early frame has its deadline
             * pushed out by every later frame we send
             */

            struct UnacknowledgedSettings
            {
                std::vector< Http2Setting >                     settings;
                time::ptime                                     sentAt;
            };

            typedef std::map< std::uint32_t, StreamContext >     streams_t;

            cpp::ScalarTypeIniter< StreamRole >                                 m_role;

            Http2Profile                                                        m_profile;
            SessionLimits                                                       m_limits;

            FrameReader                                                         m_reader;
            StreamRegistry                                                      m_registry;
            HpackDecoder                                                        m_decoder;
            HpackEncoder                                                        m_encoder;

            FlowControlWindow                                                   m_sendConnectionWindow;
            ReceiveFlowControlWindow                                            m_receiveConnectionWindow;

            streams_t                                                           m_streams;

            wire_buffer_t                                                       m_controlQueue;
            std::deque< wire_buffer_t >                                         m_headerBlockQueue;
            std::deque< SessionEvent >                                          m_events;
            std::deque< UnacknowledgedSettings >                                m_unackedSettings;

            /*
             * The header block being assembled out of HEADERS plus its CONTINUATION frames
             */

            std::string                                                         m_blockBuffer;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_blockStreamId;
            cpp::ScalarTypeIniter< std::uint8_t >                               m_blockFlags;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_blockContinuations;
            cpp::ScalarTypeIniter< bool >                                       m_blockIsOpen;

            /*
             * What the peer advertised, which bounds what we may send
             */

            std::uint32_t   m_peerMaxFrameSize      = Globals::MAX_FRAME_SIZE_DEFAULT;
            std::uint32_t   m_peerInitialWindowSize =
                                static_cast< std::uint32_t >( Globals::INITIAL_WINDOW_SIZE_DEFAULT );
            std::uint32_t   m_peerHeaderTableSize   = Globals::HEADER_TABLE_SIZE_DEFAULT;

            cpp::ScalarTypeIniter< bool >                                       m_peerLimitsConcurrentStreams;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_peerMaxConcurrentStreams;

            /*
             * What WE advertised and the peer has acknowledged - contract 3. Everything in this
             * group changes only in applyAcknowledgedSettings( ), with the one exception named
             * there: the decoded-list bound is a limits row and is already in force at
             * construction, because 6.5.2 makes that setting advisory
             */

            std::uint32_t   m_localInitialWindowSize =
                                static_cast< std::uint32_t >( Globals::INITIAL_WINDOW_SIZE_DEFAULT );

            std::size_t                                                         m_localMaxHeaderListSize;

            cpp::ScalarTypeIniter< std::uint32_t >                              m_highestPeerStreamIdSeen;
            cpp::ScalarTypeIniter< bool >                                       m_goAwaySent;
            cpp::ScalarTypeIniter< bool >                                       m_goAwayReceived;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_goAwayLastStreamId;
            cpp::ScalarTypeIniter< bool >                                       m_isClosed;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_connectionErrorCode;

            cpp::ScalarTypeIniter< std::uint32_t >                              m_dataCursorStreamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_inboundRateCount;

            cpp::ScalarTypeIniter< std::size_t >                                m_prefaceRemaining;

            time::ptime                                                         m_now;
            time::ptime                                                         m_rateWindowStart;

        public:

            SessionT(
                SAA_in          const StreamRole                     role,
                SAA_in          const time::ptime&                   now,
                SAA_in_opt      const Http2Profile&                  profile = Http2Profile(),
                SAA_in_opt      const SessionLimits&                 limits = SessionLimits()
                )
                :
                m_role( role ),
                m_profile( profile ),
                m_limits( limits ),
                m_registry(
                    role,
                    limits.maxRememberedClosedStreams,
                    limits.rememberedClosedStreamTimeoutInSeconds
                    ),
                m_decoder( advertisedHeaderTableSize( profile ) ),
                m_encoder( encoderTableSize( profile ) ),
                m_sendConnectionWindow( Globals::STREAM_ID_CONNECTION ),
                m_receiveConnectionWindow(
                    Globals::STREAM_ID_CONNECTION,
                    connectionReceiveWindowSize( profile )
                    ),
                m_localMaxHeaderListSize( initialMaxDecodedHeaderListSize( profile, limits ) ),
                m_now( now ),
                m_rateWindowStart( now )
            {
                HpackEncoder::Policy policy;

                policy.indexingPolicy = profile.hpackIndexingPolicy;
                policy.crumbleCookies = profile.cookieCrumbling;

                m_encoder.setPolicy( policy );

                if( profile.windowUpdateThreshold != 0U )
                {
                    m_receiveConnectionWindow.setUpdateThreshold(
                        static_cast< std::int32_t >( profile.windowUpdateThreshold.value() )
                        );
                }

                if( role == StreamRole::Server )
                {
                    m_prefaceRemaining = Globals::g_connectionPreface.size();
                }

                queueOpeningFrames();
            }

            StreamRole role() const NOEXCEPT
            {
                return m_role;
            }

            const Http2Profile& profile() const NOEXCEPT
            {
                return m_profile;
            }

            const SessionLimits& limits() const NOEXCEPT
            {
                return m_limits;
            }

            /**
             * @brief True once a CONNECTION ERROR has ended the connection - nothing further is
             * parsed, and the GOAWAY it queued is still worth writing
             *
             * A graceful goAway( ) of ours does NOT set this: 6.8 has the streams below the last
             * identifier finish, so the session goes on parsing and writing for them
             */

            bool isClosed() const NOEXCEPT
            {
                return m_isClosed;
            }

            std::uint32_t connectionErrorCode() const NOEXCEPT
            {
                return m_connectionErrorCode;
            }

            bool goAwayReceived() const NOEXCEPT
            {
                return m_goAwayReceived;
            }

            std::size_t activeStreamCount() const NOEXCEPT
            {
                return m_streams.size();
            }

            bool isDraining() const NOEXCEPT
            {
                return m_registry.isDraining();
            }

            std::uint32_t nextLocalStreamId() const NOEXCEPT
            {
                return m_registry.nextLocalStreamId();
            }

            /*
             * What the peer told us, which is what bounds our own writes. Accessors because the
             * conformance scripts assert on them and because the driver sizes its reads by them
             */

            std::uint32_t peerMaxFrameSize() const NOEXCEPT
            {
                return m_peerMaxFrameSize;
            }

            std::uint32_t peerInitialWindowSize() const NOEXCEPT
            {
                return m_peerInitialWindowSize;
            }

            std::uint32_t peerHeaderTableSize() const NOEXCEPT
            {
                return m_peerHeaderTableSize;
            }

            std::size_t hpackEncoderTableCapacity() const NOEXCEPT
            {
                return m_encoder.dynamicTable().capacity();
            }

            bool peerLimitsConcurrentStreams() const NOEXCEPT
            {
                return m_peerLimitsConcurrentStreams;
            }

            /**
             * @brief The window this stream has left to send on, for the conformance scripts
             */

            std::int32_t streamSendWindow( SAA_in const std::uint32_t streamId ) const
            {
                const auto it = m_streams.find( streamId );

                return it != m_streams.end() ? it -> second.sendWindow.size() : 0;
            }

            std::int32_t streamReceiveWindow( SAA_in const std::uint32_t streamId ) const
            {
                const auto it = m_streams.find( streamId );

                return it != m_streams.end() ? it -> second.receiveWindow.size() : 0;
            }

            std::uint32_t peerMaxConcurrentStreams() const NOEXCEPT
            {
                return m_peerMaxConcurrentStreams;
            }

            std::size_t maxDecodedHeaderListSize() const NOEXCEPT
            {
                return m_localMaxHeaderListSize;
            }

            std::size_t hpackDecoderCeiling() const NOEXCEPT
            {
                return m_decoder.maxDynamicTableSize();
            }

            std::int32_t connectionSendWindow() const NOEXCEPT
            {
                return m_sendConnectionWindow.size();
            }

            std::int32_t connectionReceiveWindow() const NOEXCEPT
            {
                return m_receiveConnectionWindow.size();
            }

            std::size_t unacknowledgedSettingsCount() const NOEXCEPT
            {
                return m_unackedSettings.size();
            }

            /*************************************************************************************
             * The inbound half
             */

            /**
             * @brief Parses 'size' octets and appends whatever they came to, to the event queue
             *
             * Calls nothing back. Throws nothing a peer can cause: a connection error ends the
             * session and becomes an event, a stream error becomes a RST_STREAM and an event
             */

            void feed(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size,
                SAA_in          const time::ptime&                   now
                )
            {
                BL_CHK(
                    false,
                    size == 0U || data != nullptr,
                    BL_MSG()
                        << "An HTTP/2 session was fed a non-empty block of no bytes"
                    );

                m_now = now;

                if( m_isClosed )
                {
                    return;
                }

                try
                {
                    std::size_t offset = 0U;

                    if( m_prefaceRemaining != 0U )
                    {
                        offset += consumePreface( data, size );
                    }

                    while( offset < size && ! m_isClosed )
                    {
                        const auto consumed = m_reader.feed( data + offset, size - offset );

                        offset += consumed;

                        if( ! m_reader.hasFrame() )
                        {
                            if( consumed == 0U )
                            {
                                break;
                            }

                            continue;
                        }

                        try
                        {
                            handleFrame( m_reader.frame() );
                        }
                        catch( Http2StreamException& e )
                        {
                            answerStreamException( e );
                        }

                        m_reader.consumeFrame();
                    }
                }
                catch( Http2ProtocolException& e )
                {
                    raiseConnectionErrorFromException( e );
                }
            }

            void feed(
                SAA_in          const std::string&                   data,
                SAA_in          const time::ptime&                   now
                )
            {
                feed(
                    data.empty() ?
                        nullptr : reinterpret_cast< const std::uint8_t* >( data.data() ),
                    data.size(),
                    now
                    );
            }

            bool hasEvents() const NOEXCEPT
            {
                return ! m_events.empty();
            }

            std::size_t eventCount() const NOEXCEPT
            {
                return m_events.size();
            }

            const SessionEvent& frontEvent() const
            {
                BL_CHK(
                    false,
                    ! m_events.empty(),
                    BL_MSG()
                        << "An HTTP/2 session event was asked for before one had been produced"
                    );

                return m_events.front();
            }

            void popEvent()
            {
                BL_CHK(
                    false,
                    ! m_events.empty(),
                    BL_MSG()
                        << "An HTTP/2 session event was consumed before one had been produced"
                    );

                m_events.pop_front();
            }

            /**
             * @brief The bounded clock work - the closed-stream age bound and the SETTINGS timeout
             */

            void onTimer( SAA_in const time::ptime& now )
            {
                m_now = now;

                m_registry.forgetClosedStreamsOlderThan( now );

                if( m_isClosed || m_unackedSettings.empty() )
                {
                    return;
                }

                /*
                 * The OLDEST unacknowledged frame is the one whose deadline can have passed - a
                 * peer acknowledges in order, so nothing behind it can be acknowledged first
                 */

                if(
                    now - m_unackedSettings.front().sentAt >=
                        time::seconds( m_limits.settingsTimeoutInSeconds )
                    )
                {
                    raiseConnectionError(
                        Globals::ERROR_CODE_SETTINGS_TIMEOUT,
                        "the peer did not acknowledge our SETTINGS frame in time"
                        );
                }
            }

            /*************************************************************************************
             * The outbound half
             */

            bool wantsWrite() const NOEXCEPT
            {
                if( ! m_controlQueue.empty() || ! m_headerBlockQueue.empty() )
                {
                    return true;
                }

                return firstSendableStreamId() != Globals::STREAM_ID_CONNECTION;
            }

            /**
             * @brief Appends everything that may go on the wire right now
             *
             * Control frames first, then whole header blocks, then DATA within the windows - and
             * every one of those three is bounded, so this terminates and is what a write pump
             * hands to one async_write
             */

            void produce(
                SAA_inout       wire_buffer_t&                       out,
                SAA_in          const time::ptime&                   now
                )
            {
                m_now = now;

                if( ! m_controlQueue.empty() )
                {
                    out.insert( out.end(), m_controlQueue.begin(), m_controlQueue.end() );

                    m_controlQueue.clear();
                }

                while( ! m_headerBlockQueue.empty() )
                {
                    const auto& block = m_headerBlockQueue.front();

                    out.insert( out.end(), block.begin(), block.end() );

                    m_headerBlockQueue.pop_front();
                }

                for( ;; )
                {
                    const auto streamId = firstSendableStreamId();

                    if( streamId == Globals::STREAM_ID_CONNECTION )
                    {
                        break;
                    }

                    writeOneDataFrame( streamId, m_streams.find( streamId ) -> second, out );
                }

                reapClosedStreams();
            }

            /*************************************************************************************
             * Commands
             */

            /**
             * @brief Opens a stream and queues its header block - returns the stream identifier
             *
             * The pseudo-headers are emitted in the profile's order (design 6.4), defaulting to
             * the RFC 9113 8.3.1 order when a profile names none
             */

            std::uint32_t submitRequest( SAA_in const SessionRequest& request )
            {
                BL_CHK(
                    false,
                    m_role == StreamRole::Client,
                    BL_MSG()
                        << "Only an HTTP/2 client submits a request"
                    );

                BL_CHK(
                    false,
                    ! m_isClosed,
                    BL_MSG()
                        << "An HTTP/2 request cannot be submitted on a connection which has ended"
                    );

                BL_CHK(
                    false,
                    m_registry.canOpenLocalStream(),
                    BL_MSG()
                        << "An HTTP/2 connection which is draining cannot open another stream"
                    );

                BL_CHK(
                    false,
                    ! request.method.empty() && ! request.scheme.empty() &&
                        ! request.path.empty(),
                    BL_MSG()
                        << "An HTTP/2 request needs a method, a scheme and a path"
                    );

                HpackFieldList fields;

                appendPseudoHeaders( request, fields );

                HpackFields::appendAll( request.headers, fields );

                const auto& machine = m_registry.openLocalStream();
                const auto streamId = machine.streamId();

                auto& context = createStreamContext( streamId ) -> second;

                context.priority = request.priority;
                context.expectsNoContent = isHeadMethod( request.method );

                queueHeaderBlock(
                    streamId,
                    fields,
                    ! request.hasBody /* endStream */,
                    m_profile.headersPriority
                    );

                return streamId;
            }

            /**
             * @brief The role-neutral header block - a response, or a trailer section
             *
             * submitRequest( ) is the client's convenience over this; a server built on the same
             * engine (D8, the test peer of design 8.2) answers with this one
             */

            void submitHeaders(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const HpackFieldList&                fields,
                SAA_in          const bool                           endStream
                )
            {
                BL_CHK(
                    false,
                    ! m_isClosed,
                    BL_MSG()
                        << "HTTP/2 headers cannot be submitted on a connection which has ended"
                    );

                auto* const machine = m_registry.findStream( streamId );

                BL_CHK(
                    false,
                    machine != nullptr && machine -> canSend( Globals::FRAME_TYPE_HEADERS ),
                    BL_MSG()
                        << "An HTTP/2 stream cannot carry a header block in its current state"
                    );

                Http2HeadersPriority noPriority;

                queueHeaderBlock( streamId, fields, endStream, noPriority );

                /*
                 * A block with END_STREAM on a HalfClosedRemote stream - the ordinary server
                 * answer - closes it, and the closure is an event the caller is owed NOW rather
                 * than whenever the next produce( ) or feed( ) happens to reap
                 */

                reapClosedStreams();
            }

            /**
             * @brief How many octets of body the engine can place for this stream right now
             *
             * Zero means "do not hand it any": either the windows are shut, or what it already
             * holds is as much as it can write. THIS IS THE PULL of design 4.5 - a caller which
             * hands over only what this returns can never make an upload buffer ahead of the peer
             */

            std::size_t bodyBytesWanted( SAA_in const std::uint32_t streamId ) const
            {
                const auto it = m_streams.find( streamId );

                if( it == m_streams.end() || m_isClosed )
                {
                    return 0U;
                }

                const auto& context = it -> second;

                if( context.localEndStreamQueued || context.pendingBodyEndStream )
                {
                    return 0U;
                }

                const auto* const machine = m_registry.findStream( streamId );

                if( machine == nullptr || ! machine -> canSend( Globals::FRAME_TYPE_DATA ) )
                {
                    return 0U;
                }

                const auto byWindows = std::min< std::int32_t >(
                    context.sendWindow.available(),
                    m_sendConnectionWindow.available()
                    );

                const auto room = std::min< std::size_t >(
                    static_cast< std::size_t >( m_peerMaxFrameSize ),
                    static_cast< std::size_t >( byWindows )
                    );

                return context.pendingBody.size() >= room ?
                    0U : room - context.pendingBody.size();
            }

            /**
             * @brief Hands the engine up to bodyBytesWanted( ) octets of a request or response body
             */

            void provideBody(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size,
                SAA_in          const bool                           endStream
                )
            {
                const auto it = m_streams.find( streamId );

                BL_CHK(
                    false,
                    it != m_streams.end(),
                    BL_MSG()
                        << "A body was provided for an HTTP/2 stream which does not exist"
                    );

                BL_CHK(
                    false,
                    size == 0U || data != nullptr,
                    BL_MSG()
                        << "A non-empty HTTP/2 body chunk of no bytes was provided"
                    );

                auto& context = it -> second;

                BL_CHK(
                    false,
                    ! context.localEndStreamQueued && ! context.pendingBodyEndStream,
                    BL_MSG()
                        << "An HTTP/2 body was provided after the stream had already been ended"
                    );

                BL_CHK(
                    false,
                    size <= bodyBytesWanted( streamId ),
                    BL_MSG()
                        << "More HTTP/2 body was provided than the session asked for - "
                        << "bodyBytesWanted( ) is the contract"
                    );

                if( size != 0U )
                {
                    context.pendingBody.append(
                        reinterpret_cast< const char* >( data ),
                        size
                        );
                }

                context.pendingBodyEndStream = endStream;
            }

            void provideBody(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::string&                   data,
                SAA_in          const bool                           endStream
                )
            {
                provideBody(
                    streamId,
                    data.empty() ?
                        nullptr : reinterpret_cast< const std::uint8_t* >( data.data() ),
                    data.size(),
                    endStream
                    );
            }

            /**
             * @brief Resets a stream of ours and reports it closed
             */

            void resetStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  errorCode
                )
            {
                sendRstStream( streamId, errorCode );

                reapClosedStreams();
            }

            /**
             * @brief The consumer took 'bytes' of what arrived on this stream - the backpressure
             * signal, and the only thing that ever sends a WINDOW_UPDATE
             *
             * A stream whose context is gone is a no-op rather than an error: when a stream
             * closes, everything that had arrived on it and would never be consumed is credited
             * to the connection window there and then, so crediting it again here would advertise
             * a window we never spent
             */

            void consumed(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::size_t                    bytes
                )
            {
                const auto it = m_streams.find( streamId );

                if( it == m_streams.end() || bytes == 0U )
                {
                    return;
                }

                const auto count = static_cast< std::int32_t >( bytes );

                it -> second.receiveWindow.onConsumed( count );
                m_receiveConnectionWindow.onConsumed( count );

                flushStreamWindowUpdate( streamId, it -> second, false /* force */ );
                flushConnectionWindowUpdate( false /* force */ );
            }

            void ping( SAA_in const std::uint8_t* const opaqueData )
            {
                BL_CHK_ARG( opaqueData != nullptr, opaqueData );

                FrameCodec::serializePing( opaqueData, false /* isAck */, m_controlQueue );

                checkControlQueueBound();
            }

            /**
             * @brief Ends the connection gracefully - RFC 9113 6.8
             */

            void goAway(
                SAA_in_opt      const std::uint32_t                  errorCode =
                                    Globals::ERROR_CODE_NO_ERROR,
                SAA_in_opt      const std::string&                   debugData = std::string()
                )
            {
                queueGoAway( errorCode, debugData );

                m_registry.markDraining();
            }

            /**
             * @brief Sends a SETTINGS frame of ours - it takes effect on the peer's ACK, never here
             */

            void applyLocalSettings( SAA_in const std::vector< Http2Setting >& settings )
            {
                /*
                 * Ours, so anything out of range here is a programming error and not the
                 * PROTOCOL_ERROR the same value would be arriving from a peer. Checked NOW rather
                 * than when the ack applies them, so that the diagnostic names the call that was
                 * wrong rather than a frame that arrived much later
                 */

                checkLocalSettingsAreInRange( settings );

                FrameCodec::serializeSettings( settings, m_controlQueue );

                checkControlQueueBound();

                UnacknowledgedSettings unacked;

                unacked.settings = settings;
                unacked.sentAt = m_now;

                m_unackedSettings.push_back( unacked );
            }

        private:

            /*************************************************************************************
             * Construction helpers - static, because they run in the initializer list
             */

            static std::size_t advertisedHeaderTableSize( SAA_in const Http2Profile& profile )
            {
                /*
                 * Contract 2. The decoder starts at the LARGER of what we are about to advertise
                 * and the protocol's own initial 4096, because until the peer acknowledges our
                 * SETTINGS it is entitled to the 4096 and a size update asking for it must not be
                 * refused. The ceiling drops to the advertised value in applyAcknowledgedSettings( )
                 */

                const auto advertised = findSetting(
                    profile,
                    Globals::SETTINGS_HEADER_TABLE_SIZE,
                    Globals::HEADER_TABLE_SIZE_DEFAULT
                    );

                return static_cast< std::size_t >(
                    std::max< std::uint32_t >(
                        advertised,
                        static_cast< std::uint32_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
                        )
                    );
            }

            static std::size_t initialMaxDecodedHeaderListSize(
                SAA_in          const Http2Profile&                  profile,
                SAA_in          const SessionLimits&                 limits
                )
            {
                /*
                 * NOT contract 3, and the one place that rule does not reach - see contract 3
                 * above. The bound is in force from here: the limits row, or what the profile
                 * advertises when that is larger, because a peer which has read our SETTINGS may
                 * legitimately send up to the number it saw. applyAcknowledgedSettings( ) applies
                 * the advertised value exactly when the peer acknowledges it
                 */

                const auto advertised = findSetting(
                    profile,
                    Globals::SETTINGS_MAX_HEADER_LIST_SIZE,
                    0U /* fallback - the profile advertises none */
                    );

                return static_cast< std::size_t >(
                    std::max< std::uint32_t >( advertised, limits.maxDecodedHeaderListSize )
                    );
            }

            static std::size_t encoderTableSize( SAA_in const Http2Profile& profile )
            {
                /*
                 * Ours to choose and the peer's to bound. It starts at what the profile asks for
                 * and is lowered in applyPeerSettings( ) if the peer advertises less
                 */

                return profile.hpackEncoderTableSize != 0U ?
                    static_cast< std::size_t >( profile.hpackEncoderTableSize.value() ) :
                    static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT );
            }

            static std::int32_t connectionReceiveWindowSize( SAA_in const Http2Profile& profile )
            {
                /*
                 * A SETTINGS frame cannot alter the connection window (6.9.2) - the only way to
                 * open it is a WINDOW_UPDATE, which is what the profile's increment is and what
                 * queueOpeningFrames( ) writes. The window is therefore created already holding
                 * what that frame is about to advertise, which is the one place the two have to
                 * agree
                 */

                const std::int64_t total =
                    static_cast< std::int64_t >( Globals::INITIAL_WINDOW_SIZE_DEFAULT ) +
                    static_cast< std::int64_t >( profile.connectionWindowUpdateIncrement.value() );

                BL_CHK(
                    false,
                    total <= static_cast< std::int64_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ),
                    BL_MSG()
                        << "An HTTP/2 profile's connection WINDOW_UPDATE increment would drive the "
                        << "connection window past 2^31-1"
                    );

                return static_cast< std::int32_t >( total );
            }

            static std::uint32_t findSetting(
                SAA_in          const Http2Profile&                  profile,
                SAA_in          const std::uint32_t                  id,
                SAA_in          const std::uint32_t                  fallback
                ) NOEXCEPT
            {
                /*
                 * The LAST entry for an id, not the first. 6.5.3 - "the values in the SETTINGS
                 * frame MUST be processed in the order they appear" - so a repeated identifier
                 * means the later value is the one in force, which is what the peer will make of
                 * the frame we are about to send it and what applyPeerSettings( ) and
                 * applyAcknowledgedSettings( ) already do by processing the list in order.
                 *
                 * The profile is ours, so a repeat in it is a configuration mistake rather than
                 * hostile input - but it is not a protocol violation to refuse, because 6.5.3
                 * defines what it means. Agreeing with the peer is the whole point: reading the
                 * first entry would have the session use one number while it advertised another,
                 * and change its mind at the ack
                 */

                std::uint32_t result = fallback;

                for( std::size_t i = 0U; i < profile.settings.size(); ++i )
                {
                    if( profile.settings[ i ].id == id )
                    {
                        result = profile.settings[ i ].value;
                    }
                }

                return result;
            }

            /**
             * @brief The preface, our SETTINGS, the connection WINDOW_UPDATE and the profile's
             * PRIORITY frames - the opening write of design 5.1, in the order a browser sends it
             */

            void queueOpeningFrames()
            {
                if( m_role == StreamRole::Client )
                {
                    m_controlQueue.insert(
                        m_controlQueue.end(),
                        Globals::g_connectionPreface.begin(),
                        Globals::g_connectionPreface.end()
                        );
                }

                auto settings = m_profile.settings;

                /*
                 * D11 - we never accept a push, so we always say so, and design 8.4 conditions the
                 * "a PUSH_PROMISE is a connection error" answer on having said it. A profile which
                 * carries the setting keeps its own position in the order, because that order is
                 * part of the fingerprint; one which omits it gets ours appended after its own
                 */

                bool sawEnablePush = false;

                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    if( settings[ i ].id != Globals::SETTINGS_ENABLE_PUSH )
                    {
                        continue;
                    }

                    sawEnablePush = true;

                    BL_CHK(
                        false,
                        m_role == StreamRole::Server || settings[ i ].value == 0U,
                        BL_MSG()
                            << "An HTTP/2 client profile cannot advertise SETTINGS_ENABLE_PUSH "
                            << "with a value other than zero"
                        );
                }

                if( ! sawEnablePush && m_role == StreamRole::Client )
                {
                    Http2Setting enablePush;

                    enablePush.id =
                        static_cast< std::uint16_t >( Globals::SETTINGS_ENABLE_PUSH );
                    enablePush.value = 0U;

                    settings.push_back( enablePush );
                }

                applyLocalSettings( settings );

                if( m_profile.connectionWindowUpdateIncrement != 0U )
                {
                    FrameCodec::serializeWindowUpdate(
                        Globals::STREAM_ID_CONNECTION,
                        m_profile.connectionWindowUpdateIncrement,
                        m_controlQueue
                        );
                }

                /*
                 * The profile's idle-stream PRIORITY frames are fingerprint shaping and nothing
                 * else: this engine ignores the peer's priority signals entirely (design 4.7), and
                 * PRIORITY on an idle stream does not open it (5.1), so the registry is not told
                 * about them and no stream state exists for them
                 */

                for( std::size_t i = 0U; i < m_profile.idleStreamPriorities.size(); ++i )
                {
                    FrameCodec::serializePriority(
                        m_profile.idleStreamPriorities[ i ],
                        m_controlQueue
                        );
                }

                checkControlQueueBound();
            }

            /**
             * @brief The per-stream bookkeeping, for a stream of ours or one the peer opened
             *
             * The two initial window sizes are the values IN EFFECT - contract 3 - which before
             * the peer's acknowledgement are still the protocol's 65535 whatever our profile
             * advertises. Lowering them here instead is what makes a later acknowledgement adjust
             * them twice
             */

            typename streams_t::iterator createStreamContext(
                SAA_in          const std::uint32_t                  streamId
                )
            {
                const auto inserted = m_streams.insert(
                    typename streams_t::value_type(
                        streamId,
                        StreamContext(
                            streamId,
                            static_cast< std::int32_t >( m_peerInitialWindowSize ),
                            static_cast< std::int32_t >( m_localInitialWindowSize )
                            )
                        )
                    );

                BL_CHK(
                    false,
                    inserted.second,
                    BL_MSG()
                        << "An HTTP/2 stream context was created twice"
                    );

                if( m_profile.windowUpdateThreshold != 0U )
                {
                    inserted.first -> second.receiveWindow.setUpdateThreshold(
                        static_cast< std::int32_t >( m_profile.windowUpdateThreshold.value() )
                        );
                }

                return inserted.first;
            }

            /*************************************************************************************
             * Errors
             */

            SAA_noreturn
            static void throwConnectionError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                )
            {
                FrameCodec::throwConnectionError( errorCode, reason );
            }

            static std::uint32_t errorCodeOf(
                SAA_in          const eh::exception&                 e,
                SAA_in          const std::uint32_t                  fallback
                ) NOEXCEPT
            {
                const auto* const code = eh::get_error_info< eh::errinfo_http2_error_code >( e );

                return code != nullptr ? *code : fallback;
            }

            void raiseConnectionErrorFromException( SAA_in const Http2ProtocolException& e )
            {
                raiseConnectionError(
                    errorCodeOf( e, Globals::ERROR_CODE_PROTOCOL_ERROR ),
                    std::string( e.what() != nullptr ? e.what() : "" )
                    );
            }

            /**
             * @brief Ends the connection - a GOAWAY, a closure for every live stream, one event
             *
             * The per-stream closures come FIRST and the connection event last, so that a caller
             * draining in order learns what happened to each request before it learns that the
             * connection is gone
             */

            void raiseConnectionError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                )
            {
                if( m_isClosed )
                {
                    return;
                }

                /*
                 * Set FIRST, so that nothing below can raise a second connection error - the
                 * queue bound is checked on every frame appended, and a throw from in here would
                 * escape feed( ), which promises that it never throws what a peer can cause
                 */

                m_isClosed = true;
                m_connectionErrorCode = errorCode;

                queueGoAway( errorCode, std::string() );

                /*
                 * The header blocks queued before the error are for streams closeEveryStream( )
                 * is about to close, and produce( ) would otherwise write them AFTER the GOAWAY -
                 * new work on a connection we have just told the peer is over. The control queue
                 * is kept: the GOAWAY is in it, and it is the one thing still owed
                 */

                m_headerBlockQueue.clear();

                closeEveryStream( errorCode );

                SessionEvent event;

                event.type = SessionEventType::ConnectionError;
                event.errorCode = errorCode;
                event.reason = reason;

                m_events.push_back( event );

                m_isClosed = true;
            }

            /**
             * @brief Contract 4 - answer a stream error only where the RFC lets us
             */

            void answerStreamException( SAA_in const Http2StreamException& e )
            {
                const auto* const streamId =
                    eh::get_error_info< eh::errinfo_http2_stream_id >( e );

                if( streamId == nullptr || *streamId == Globals::STREAM_ID_CONNECTION )
                {
                    /*
                     * A stream error which cannot name its stream cannot be answered; the frame
                     * has been consumed, so the connection is still in sync and carries on
                     */

                    return;
                }

                answerStreamError(
                    *streamId,
                    errorCodeOf( e, Globals::ERROR_CODE_PROTOCOL_ERROR )
                    );
            }

            void answerStreamError(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  errorCode
                )
            {
                const auto* const machine = m_registry.findStream( streamId );

                if( machine == nullptr || ! machine -> canSend( Globals::FRAME_TYPE_RST_STREAM ) )
                {
                    /*
                     * CONTRACT 4. The stream is closed, or gone, or was never ours - RFC 9113 5.1
                     * forbids sending anything but PRIORITY on it, and telling the registry
                     * otherwise is an UnexpectedException. Those two are the whole reason the
                     * error is dropped; a peer which has sent RST_STREAM must ignore ours (5.1),
                     * so there is no reset loop to avoid here and that is not the argument. The
                     * connection window was credited back by the caller either way
                     */

                    return;
                }

                sendRstStream( streamId, errorCode );

                reapClosedStreams();
            }

            void sendRstStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  errorCode
                )
            {
                auto* const machine = m_registry.findStream( streamId );

                if( machine == nullptr || ! machine -> canSend( Globals::FRAME_TYPE_RST_STREAM ) )
                {
                    return;
                }

                const auto it = m_streams.find( streamId );

                if( it != m_streams.end() )
                {
                    it -> second.closeErrorCode = errorCode;
                    it -> second.pendingBody.clear();
                }

                FrameCodec::serializeRstStream( streamId, errorCode, m_controlQueue );

                checkControlQueueBound();

                m_registry.onFrameSent(
                    streamId,
                    Globals::FRAME_TYPE_RST_STREAM,
                    Globals::FRAME_FLAG_NONE,
                    m_now
                    );
            }

            void queueGoAway(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   debugData
                )
            {
                if( m_goAwaySent )
                {
                    return;
                }

                FrameCodec::serializeGoAway(
                    m_highestPeerStreamIdSeen,
                    errorCode,
                    debugData.empty() ?
                        nullptr : reinterpret_cast< const std::uint8_t* >( debugData.data() ),
                    debugData.size(),
                    m_controlQueue
                    );

                m_goAwaySent = true;
            }

            /**
             * @brief Design 4.6 - the acknowledgements owed to a peer which will not read
             */

            void checkControlQueueBound()
            {
                if( m_isClosed )
                {
                    /*
                     * The GOAWAY of a connection error is queued after the session is already
                     * marked closed, precisely so that it cannot trip this and raise a second
                     * error from inside the first one's handling
                     */

                    return;
                }

                if( m_controlQueue.size() > m_limits.maxQueuedControlFrameBytes )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_ENHANCE_YOUR_CALM,
                        "more control-frame octets are owed to the peer than it is willing to read"
                        );
                }
            }

            /*************************************************************************************
             * The connection preface - RFC 9113 3.4, the server side of it
             */

            std::size_t consumePreface(
                SAA_in          const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                const auto& preface = Globals::g_connectionPreface;
                const auto seen = preface.size() - m_prefaceRemaining;

                const auto take = std::min< std::size_t >( m_prefaceRemaining, size );

                for( std::size_t i = 0U; i < take; ++i )
                {
                    if( data[ i ] != static_cast< std::uint8_t >( preface[ seen + i ] ) )
                    {
                        throwConnectionError(
                            Globals::ERROR_CODE_PROTOCOL_ERROR,
                            "what arrived first on the connection is not the HTTP/2 client preface"
                            );
                    }
                }

                m_prefaceRemaining = m_prefaceRemaining - take;

                return take;
            }

            /*************************************************************************************
             * Inbound dispatch
             */

            void handleFrame( SAA_in const FrameView& frame )
            {
                if( frame.streamErrorCode != Globals::ERROR_CODE_NO_ERROR )
                {
                    /*
                     * The reader consumed the whole frame so that the connection stays in sync,
                     * and surfaced a stream error rather than throwing one
                     */

                    answerStreamError( frame.header.streamId, frame.streamErrorCode );

                    return;
                }

                switch( frame.header.type.value() )
                {
                    case Globals::FRAME_TYPE_DATA:
                        handleData( frame );
                        break;

                    case Globals::FRAME_TYPE_HEADERS:
                        beginHeaderBlock( frame );
                        break;

                    case Globals::FRAME_TYPE_CONTINUATION:
                        continueHeaderBlock( frame );
                        break;

                    case Globals::FRAME_TYPE_PRIORITY:

                        /*
                         * Parsed and dropped. RFC 9113 deprecates the dependency tree and design
                         * 4.7 has us ignore the peer's signals entirely - and NOT routing it to
                         * the registry is deliberate: PRIORITY is legal on an idle stream (5.1),
                         * which the registry would report as a frame on an identifier we have not
                         * opened
                         */

                        break;

                    case Globals::FRAME_TYPE_RST_STREAM:
                        handleRstStream( frame );
                        break;

                    case Globals::FRAME_TYPE_SETTINGS:
                        handleSettings( frame );
                        break;

                    case Globals::FRAME_TYPE_PUSH_PROMISE:

                        /*
                         * D11 - server push is disabled, we advertise SETTINGS_ENABLE_PUSH = 0 on
                         * every connection, and 8.4 makes a PUSH_PROMISE which arrives anyway a
                         * connection error. This is stricter than 8.4 in one respect: it does not
                         * wait for our SETTINGS to have been acknowledged, because there is no
                         * state in which this client could accept a promised stream
                         */

                        throwConnectionError(
                            Globals::ERROR_CODE_PROTOCOL_ERROR,
                            "a PUSH_PROMISE arrived although server push was never enabled"
                            );

                        break;

                    case Globals::FRAME_TYPE_PING:
                        handlePing( frame );
                        break;

                    case Globals::FRAME_TYPE_GOAWAY:
                        handleGoAway( frame );
                        break;

                    case Globals::FRAME_TYPE_WINDOW_UPDATE:
                        handleWindowUpdate( frame );
                        break;

                    default:

                        /*
                         * An unknown type is ignored (4.1). Inside a header block it is a
                         * connection error, which the reader has already raised
                         */

                        break;
                }
            }

            void handleData( SAA_in const FrameView& frame )
            {
                const auto payload = FrameCodec::parseData( frame );

                const auto streamId = frame.header.streamId.value();
                const auto length = static_cast< std::int32_t >( frame.header.length.value() );
                const auto dataSize = static_cast< std::int32_t >( payload.size.value() );

                /*
                 * The CONNECTION window is spent by the whole payload, padding and the Pad Length
                 * octet included (6.9.1), and it is spent whatever becomes of the frame. The
                 * padding is credited back at once - nobody will ever consume it - so that
                 * consumed( ) is about the octets the caller actually received and nothing else
                 */

                m_receiveConnectionWindow.onDataReceived( length );

                if( length != dataSize )
                {
                    m_receiveConnectionWindow.onConsumed( length - dataSize );
                }

                noteHighestPeerStreamId( streamId );

                const auto it = m_streams.find( streamId );

                const auto* const machine = m_registry.findStream( streamId );

                /*
                 * CONTRACT 1, THE ORDERING HALF, ON THIS PATH TOO - judged before the registry is
                 * told, because a DATA frame carrying END_STREAM closes an ordinary client stream
                 * and 5.1 then forbids the RST_STREAM 8.1.1 demands
                 *
                 * Only for a frame the stream may receive at all: when it may not, the registry
                 * owes the peer its own answer - STREAM_CLOSED for a stream the peer has already
                 * ended - and judging first would answer with ours instead
                 */

                if(
                    it != m_streams.end() && machine != nullptr &&
                    machine -> canReceive( Globals::FRAME_TYPE_DATA ) &&
                    ! judgeDataFrame( streamId, it -> second, length, dataSize, payload.endStream )
                    )
                {
                    return;
                }

                const auto result = m_registry.onFrameReceived(
                    streamId,
                    Globals::FRAME_TYPE_DATA,
                    frame.header.flags,
                    length,
                    m_now
                    );

                if( result.disposition != FrameDisposition::Accepted || it == m_streams.end() )
                {
                    /*
                     * Nobody will consume these octets, so the connection window is credited back
                     * here and now - this is the leak design 4.3 warns about, and the reason
                     * InboundFrameResult carries the count whatever the verdict
                     */

                    m_receiveConnectionWindow.onConsumed( dataSize );

                    flushConnectionWindowUpdate( false /* force */ );

                    if( result.disposition == FrameDisposition::StreamError )
                    {
                        answerStreamError( streamId, result.streamErrorCode );
                    }

                    return;
                }

                auto& context = it -> second;

                context.receiveWindow.onDataReceived( length );

                if( length != dataSize )
                {
                    context.receiveWindow.onConsumed( length - dataSize );
                }

                context.receivedDataBytes = context.receivedDataBytes + dataSize;

                SessionEvent event;

                event.type = SessionEventType::Data;
                event.streamId = streamId;
                event.endStream = payload.endStream;

                if( payload.size != 0U )
                {
                    event.data.assign(
                        reinterpret_cast< const char* >( payload.data.value() ),
                        payload.size
                        );
                }

                m_events.push_back( event );

                if( payload.endStream )
                {
                    onPeerEndStream( streamId, context );
                }

                reapClosedStreams();
            }

            void beginHeaderBlock( SAA_in const FrameView& frame )
            {
                const auto payload = FrameCodec::parseHeaders( frame );

                m_blockIsOpen = true;
                m_blockStreamId = frame.header.streamId;
                m_blockFlags = frame.header.flags;
                m_blockContinuations = 0U;

                m_blockBuffer.clear();

                appendBlockFragment( payload.fieldBlock, payload.fieldBlockSize );

                if( payload.endHeaders )
                {
                    completeHeaderBlock();
                }
            }

            void continueHeaderBlock( SAA_in const FrameView& frame )
            {
                const auto payload = FrameCodec::parseContinuation( frame );

                BL_CHK(
                    false,
                    m_blockIsOpen,
                    BL_MSG()
                        << "A CONTINUATION frame reached the session with no header block open, "
                        << "which the frame reader is supposed to make impossible"
                    );

                m_blockContinuations = m_blockContinuations + 1U;

                if( m_blockContinuations > m_limits.maxContinuationFramesPerBlock )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_ENHANCE_YOUR_CALM,
                        "a header block is spread over more CONTINUATION frames than this client "
                            "accepts"
                        );
                }

                appendBlockFragment( payload.fieldBlock, payload.fieldBlockSize );

                if( payload.endHeaders )
                {
                    completeHeaderBlock();
                }
            }

            void appendBlockFragment(
                SAA_in_opt      const std::uint8_t*                  fragment,
                SAA_in          const std::size_t                    size
                )
            {
                if(
                    m_blockBuffer.size() + size >
                        static_cast< std::size_t >( m_limits.maxCompressedHeaderBlockSize )
                    )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_ENHANCE_YOUR_CALM,
                        "a compressed header block is larger than this client accepts"
                        );
                }

                if( size != 0U )
                {
                    m_blockBuffer.append(
                        reinterpret_cast< const char* >( fragment ),
                        size
                        );
                }
            }

            /**
             * @brief CONTRACT 1 - the one place the state machine hears about a header block
             */

            void completeHeaderBlock()
            {
                const auto streamId = m_blockStreamId.value();
                const auto flags = m_blockFlags.value();
                const bool endStream = 0U != ( flags & Globals::FRAME_FLAG_END_STREAM );

                std::string block;

                block.swap( m_blockBuffer );

                m_blockIsOpen = false;
                m_blockStreamId = Globals::STREAM_ID_CONNECTION;
                m_blockFlags = Globals::FRAME_FLAG_NONE;
                m_blockContinuations = 0U;

                /*
                 * DECODE FIRST. A block we are about to refuse still has to be decoded or the
                 * dynamic table stops tracking the peer's encoder and the NEXT block - a good one,
                 * on another stream - fails with COMPRESSION_ERROR and takes the connection down
                 */

                HpackFieldList fields;

                const auto outcome = m_decoder.decode(
                    block.empty() ? nullptr : block.data(),
                    block.size(),
                    m_localMaxHeaderListSize,
                    fields
                    );

                noteHighestPeerStreamId( streamId );

                auto it = m_streams.find( streamId );

                const bool known =
                    it != m_streams.end() && m_registry.findStream( streamId ) != nullptr;

                BlockVerdict verdict;

                if( known )
                {
                    /*
                     * AND JUDGED BEFORE THE TRANSITION, which is the other half of contract 1's
                     * ordering and is just as easy to get wrong. A malformed FINAL response
                     * carries END_STREAM: told about the block first, the state machine would
                     * close the stream, and the RST_STREAM which RFC 9113 8.1 demands cannot be
                     * sent on a closed one (5.1). Judging first keeps both rules at once
                     */

                    if( ! judgeAndReject( streamId, it -> second, outcome, fields, endStream,
                            verdict ) )
                    {
                        return;
                    }
                }

                const auto result = m_registry.onFrameReceived(
                    streamId,
                    Globals::FRAME_TYPE_HEADERS,
                    flags,
                    0 /* payloadLength */,
                    m_now
                    );

                if( result.disposition != FrameDisposition::Accepted )
                {
                    if( result.disposition == FrameDisposition::StreamError )
                    {
                        answerStreamError( streamId, result.streamErrorCode );
                    }

                    return;
                }

                if( ! known )
                {
                    /*
                     * A stream the registry has just created for the peer, which only the server
                     * role reaches (5.1.1). The judgement runs here instead, and it can: HEADERS
                     * on an idle stream leaves it open or half-closed (remote), never closed, so a
                     * RST_STREAM is still sendable
                     */

                    if( m_registry.findStream( streamId ) == nullptr )
                    {
                        return;
                    }

                    it = createStreamContext( streamId );

                    if( ! judgeAndReject( streamId, it -> second, outcome, fields, endStream,
                            verdict ) )
                    {
                        return;
                    }
                }

                deliverHeaderBlock( streamId, it -> second, fields, endStream, verdict );

                reapClosedStreams();
            }

            /**
             * @brief Judges the block and, when it is bad, resets the stream - returns whether to
             * carry on
             */

            bool judgeAndReject(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamContext&                 context,
                SAA_in          const HpackDecoder::Outcome          outcome,
                SAA_in          const HpackFieldList&                fields,
                SAA_in          const bool                           endStream,
                SAA_out         BlockVerdict&                        verdict
                )
            {
                if( outcome != HpackDecoder::Outcome::Complete )
                {
                    /*
                     * Design 4.6, first row - the block was consumed to the last octet so the
                     * table is in step, and only the stream dies
                     */

                    rejectStream(
                        streamId,
                        Globals::ERROR_CODE_ENHANCE_YOUR_CALM,
                        "the decoded header list is larger than this client accepts"
                        );

                    return false;
                }

                const auto reason = judgeHeaderBlock( context, fields, endStream, verdict );

                if( ! reason.empty() )
                {
                    rejectMalformedMessage( streamId, reason );

                    return false;
                }

                return true;
            }

            /**
             * @brief Judges a DATA frame and, when it is bad, resets the stream - returns whether
             * to carry on
             *
             * The DATA-path half of contract 1: everything here is decided from what the frame
             * brings and what the stream has already received, so it can be decided BEFORE the
             * registry is told, which is the only point at which the RST_STREAM is still sendable
             */

            bool judgeDataFrame(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamContext&                 context,
                SAA_in          const std::int32_t                   length,
                SAA_in          const std::int32_t                   dataSize,
                SAA_in          const bool                           endStream
                )
            {
                std::uint32_t errorCode = Globals::ERROR_CODE_PROTOCOL_ERROR;

                const char* reason = nullptr;

                const std::int64_t total =
                    context.receivedDataBytes + static_cast< std::int64_t >( dataSize );

                const bool lengthIsChecked =
                    context.sawContentLength && ! context.expectsNoContent;

                if( length > context.receiveWindow.size() )
                {
                    /*
                     * The peer overran the STREAM window, which 6.9.1 lets us answer at the level
                     * of the window it overran. Asked rather than caught, because the octets are
                     * the connection's to reclaim either way and a throw from the window would
                     * leave them charged to the connection and credited to nobody
                     */

                    errorCode = Globals::ERROR_CODE_FLOW_CONTROL_ERROR;
                    reason =
                        "a DATA frame does not fit the flow-control window of its stream (6.9.1)";
                }
                else if( ! context.headersReceived )
                {
                    /*
                     * 8.1 - a message is an optional informational HEADERS, then ONE HEADERS, then
                     * the DATA. Body octets before the header section are an invalid sequence and
                     * 8.1.1 makes that malformed. nghttp2 refuses it too, and goes further than
                     * this: it terminates the connection ("DATA: stream not opened"), where 8.1.1
                     * asks only for a stream error. The stream is enough - the other streams on
                     * this connection have done nothing wrong
                     */

                    reason = "DATA arrived before the header section of its message (8.1)";
                }
                else if( lengthIsChecked && total > context.declaredContentLength )
                {
                    reason = "more DATA arrived than the content-length field declared";
                }
                else if( endStream && lengthIsChecked && total != context.declaredContentLength )
                {
                    reason = "the DATA total does not match the content-length field (8.1.1)";
                }

                if( reason == nullptr )
                {
                    return true;
                }

                /*
                 * Nobody will consume these octets - no Data event is emitted for a frame judged
                 * bad - so the connection window is credited back here, exactly as it is for a
                 * frame the registry refuses
                 */

                m_receiveConnectionWindow.onConsumed( dataSize );

                flushConnectionWindowUpdate( false /* force */ );

                rejectStream( streamId, errorCode, reason );

                return false;
            }

            void handleRstStream( SAA_in const FrameView& frame )
            {
                const auto errorCode = FrameCodec::parseRstStream( frame );
                const auto streamId = frame.header.streamId.value();

                noteHighestPeerStreamId( streamId );

                const auto result = m_registry.onFrameReceived(
                    streamId,
                    Globals::FRAME_TYPE_RST_STREAM,
                    frame.header.flags,
                    0 /* payloadLength */,
                    m_now
                    );

                if( result.disposition != FrameDisposition::Accepted )
                {
                    if( result.disposition == FrameDisposition::StreamError )
                    {
                        answerStreamError( streamId, result.streamErrorCode );
                    }

                    return;
                }

                const auto it = m_streams.find( streamId );

                if( it != m_streams.end() )
                {
                    /*
                     * THE EARLY RESPONSE OF RFC 9113 8.1. A complete response followed by
                     * RST_STREAM( NO_ERROR ) while we are still uploading is a SUCCESS: the server
                     * answered without needing the rest of the request, and we stop sending. The
                     * message-complete flag on the closure is what tells the caller that apart
                     * from a peer which reset an unfinished exchange with the same code
                     */

                    it -> second.closeErrorCode = errorCode;
                    it -> second.pendingBody.clear();
                }

                reapClosedStreams();
            }

            void handleSettings( SAA_in const FrameView& frame )
            {
                checkInboundFrameRate();

                if( 0U != ( frame.header.flags & Globals::FRAME_FLAG_ACK ) )
                {
                    if( m_unackedSettings.empty() )
                    {
                        throwConnectionError(
                            Globals::ERROR_CODE_PROTOCOL_ERROR,
                            "a SETTINGS acknowledgement arrived for a SETTINGS frame we never sent"
                            );
                    }

                    const auto settings = m_unackedSettings.front().settings;

                    m_unackedSettings.pop_front();

                    applyAcknowledgedSettings( settings );

                    SessionEvent event;

                    event.type = SessionEventType::SettingsAcknowledged;
                    event.settings = settings;

                    m_events.push_back( event );

                    return;
                }

                const auto settings = FrameCodec::parseSettings( frame );

                applyPeerSettings( settings );

                FrameCodec::serializeSettingsAck( m_controlQueue );

                checkControlQueueBound();

                SessionEvent event;

                event.type = SessionEventType::SettingsReceived;
                event.settings = settings;

                m_events.push_back( event );
            }

            void handlePing( SAA_in const FrameView& frame )
            {
                checkInboundFrameRate();

                const auto* const opaqueData = FrameCodec::parsePing( frame );

                if( 0U != ( frame.header.flags & Globals::FRAME_FLAG_ACK ) )
                {
                    SessionEvent event;

                    event.type = SessionEventType::PingAcknowledged;
                    event.pingData.assign(
                        reinterpret_cast< const char* >( opaqueData ),
                        static_cast< std::size_t >( 8 )
                        );

                    m_events.push_back( event );

                    return;
                }

                FrameCodec::serializePing( opaqueData, true /* isAck */, m_controlQueue );

                checkControlQueueBound();
            }

            void handleGoAway( SAA_in const FrameView& frame )
            {
                const auto payload = FrameCodec::parseGoAway( frame );

                /*
                 * A second GOAWAY is legal (6.8) and usually narrows the first, but "endpoints
                 * MUST NOT increase the value they send in the last stream identifier" - the
                 * streams above it may already have been retried elsewhere. A peer which raises
                 * it anyway is held to the value it first gave rather than believed: the RFC
                 * gives the receiver no error to raise here, and believing the larger number
                 * would say that streams we have already reported RETRYABLE were processed
                 */

                const auto lastStreamId = m_goAwayReceived ?
                    std::min< std::uint32_t >( m_goAwayLastStreamId, payload.lastStreamId ) :
                    payload.lastStreamId.value();

                m_goAwayReceived = true;
                m_goAwayLastStreamId = lastStreamId;

                m_registry.markDraining();

                SessionEvent event;

                event.type = SessionEventType::GoAwayReceived;
                event.errorCode = payload.errorCode;
                event.lastStreamId = lastStreamId;

                if( payload.debugDataSize != 0U )
                {
                    event.debugData.assign(
                        reinterpret_cast< const char* >( payload.debugData.value() ),
                        payload.debugDataSize
                        );
                }

                m_events.push_back( event );

                /*
                 * D6. Every stream of ours above the last identifier the peer says it acted on was
                 * provably NOT processed, so it may be retried on another connection - which is
                 * the whole point of the retryable flag. The streams at or below it are left
                 * alone; the peer is still answering them
                 */

                closeStreamsAbove( lastStreamId, payload.errorCode );
            }

            void handleWindowUpdate( SAA_in const FrameView& frame )
            {
                const auto increment = FrameCodec::parseWindowUpdate( frame );
                const auto streamId = frame.header.streamId.value();

                if( streamId == Globals::STREAM_ID_CONNECTION )
                {
                    m_sendConnectionWindow.applyWindowUpdate( increment );

                    return;
                }

                noteHighestPeerStreamId( streamId );

                const auto result = m_registry.onFrameReceived(
                    streamId,
                    Globals::FRAME_TYPE_WINDOW_UPDATE,
                    frame.header.flags,
                    0 /* payloadLength */,
                    m_now
                    );

                if( result.disposition != FrameDisposition::Accepted )
                {
                    if( result.disposition == FrameDisposition::StreamError )
                    {
                        answerStreamError( streamId, result.streamErrorCode );
                    }

                    return;
                }

                const auto it = m_streams.find( streamId );

                if( it != m_streams.end() )
                {
                    it -> second.sendWindow.applyWindowUpdate( increment );
                }
            }

            /**
             * @brief Design 4.6 - inbound PING and SETTINGS per second
             */

            void checkInboundFrameRate()
            {
                if( m_now - m_rateWindowStart >= time::seconds( 1 ) )
                {
                    m_rateWindowStart = m_now;
                    m_inboundRateCount = 0U;
                }

                m_inboundRateCount = m_inboundRateCount + 1U;

                if( m_inboundRateCount > m_limits.maxInboundPingAndSettingsPerSecond )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_ENHANCE_YOUR_CALM,
                        "the peer is sending PING and SETTINGS frames faster than this client "
                            "accepts them"
                        );
                }
            }

            void noteHighestPeerStreamId( SAA_in const std::uint32_t streamId ) NOEXCEPT
            {
                if(
                    ! StreamStateMachine::isLocallyInitiatedId( streamId, m_role.value() ) &&
                    streamId > m_highestPeerStreamIdSeen
                    )
                {
                    m_highestPeerStreamIdSeen = streamId;
                }
            }

            /*************************************************************************************
             * Settings
             */

            void applyPeerSettings( SAA_in const std::vector< Http2Setting >& settings )
            {
                /*
                 * RFC 9113 6.5.3 - the values are processed in the order they appear, and a
                 * duplicate identifier means the later value wins, which falls out of doing them
                 * in order
                 */

                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    const auto id = settings[ i ].id.value();
                    const auto value = settings[ i ].value.value();

                    switch( id )
                    {
                        case Globals::SETTINGS_HEADER_TABLE_SIZE:
                            m_peerHeaderTableSize = value;

                            m_encoder.setDynamicTableCapacity(
                                std::min< std::size_t >(
                                    encoderTableSize( m_profile ),
                                    static_cast< std::size_t >( value )
                                    )
                                );
                            break;

                        case Globals::SETTINGS_ENABLE_PUSH:

                            if( value > 1U )
                            {
                                throwConnectionError(
                                    Globals::ERROR_CODE_PROTOCOL_ERROR,
                                    "SETTINGS_ENABLE_PUSH carries a value other than zero or one"
                                    );
                            }

                            if( m_role == StreamRole::Client && value != 0U )
                            {
                                /*
                                 * 6.5.2 - "A client MUST treat receipt of a SETTINGS frame with
                                 * SETTINGS_ENABLE_PUSH set to 1 as a connection error"
                                 */

                                throwConnectionError(
                                    Globals::ERROR_CODE_PROTOCOL_ERROR,
                                    "a server enabled server push, which a client never offers"
                                    );
                            }
                            break;

                        case Globals::SETTINGS_MAX_CONCURRENT_STREAMS:
                            m_peerLimitsConcurrentStreams = true;
                            m_peerMaxConcurrentStreams = value;
                            break;

                        case Globals::SETTINGS_INITIAL_WINDOW_SIZE:
                            applyPeerInitialWindowSize( value );
                            break;

                        case Globals::SETTINGS_MAX_FRAME_SIZE:

                            if(
                                value < Globals::MAX_FRAME_SIZE_DEFAULT ||
                                value > Globals::MAX_FRAME_SIZE_UPPER_BOUND
                                )
                            {
                                throwConnectionError(
                                    Globals::ERROR_CODE_PROTOCOL_ERROR,
                                    "SETTINGS_MAX_FRAME_SIZE is outside the range RFC 9113 4.2 "
                                        "allows"
                                    );
                            }

                            m_peerMaxFrameSize = value;
                            break;

                        case Globals::SETTINGS_MAX_HEADER_LIST_SIZE:

                            /*
                             * Advisory (6.5.2) - it bounds nothing we are required to do, and a
                             * block which exceeds it is still the peer's to refuse
                             */

                            break;

                        default:

                            /*
                             * An unknown or unsupported identifier MUST be ignored (6.5.2). The
                             * profile table already carries two of them
                             */

                            break;
                    }
                }
            }

            void applyPeerInitialWindowSize( SAA_in const std::uint32_t value )
            {
                m_sendConnectionWindow.validateInitialWindowSize( value );

                const auto previous = m_peerInitialWindowSize;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    it -> second.sendWindow.applyInitialWindowSizeChange( previous, value );
                }

                m_peerInitialWindowSize = value;
            }

            /**
             * @brief CONTRACT 3 - our SETTINGS take effect here and nowhere else
             */

            void applyAcknowledgedSettings( SAA_in const std::vector< Http2Setting >& settings )
            {
                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    const auto id = settings[ i ].id.value();
                    const auto value = settings[ i ].value.value();

                    switch( id )
                    {
                        case Globals::SETTINGS_HEADER_TABLE_SIZE:

                            /*
                             * CONTRACT 2 - the ceiling drops only now. Until this ack the peer was
                             * entitled to the protocol's 4096 whatever we advertised
                             */

                            m_decoder.setMaxDynamicTableSize(
                                static_cast< std::size_t >( value )
                                );
                            break;

                        case Globals::SETTINGS_INITIAL_WINDOW_SIZE:
                            applyLocalInitialWindowSize( value );
                            break;

                        case Globals::SETTINGS_MAX_FRAME_SIZE:
                            m_reader.setMaxFrameSize( value );
                            break;

                        case Globals::SETTINGS_MAX_HEADER_LIST_SIZE:

                            /*
                             * The exception to this function's rule, and the only setting here
                             * which was already in force before the ack: 6.5.2 makes it advisory,
                             * so the limits row bounded the decoded list from construction and
                             * this is where the advertised number replaces it
                             */

                            m_localMaxHeaderListSize = static_cast< std::size_t >( value );
                            break;

                        default:
                            break;
                    }
                }
            }

            static void checkLocalSettingsAreInRange(
                SAA_in          const std::vector< Http2Setting >&    settings
                )
            {
                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    const auto id = settings[ i ].id.value();
                    const auto value = settings[ i ].value.value();

                    if( id == Globals::SETTINGS_MAX_FRAME_SIZE )
                    {
                        BL_CHK(
                            false,
                            value >= Globals::MAX_FRAME_SIZE_DEFAULT &&
                                value <= Globals::MAX_FRAME_SIZE_UPPER_BOUND,
                            BL_MSG()
                                << "An HTTP/2 SETTINGS_MAX_FRAME_SIZE of ours is outside the "
                                << "range RFC 9113 4.2 allows"
                            );
                    }

                    if( id == Globals::SETTINGS_INITIAL_WINDOW_SIZE )
                    {
                        BL_CHK(
                            false,
                            value <= static_cast< std::uint32_t >(
                                Globals::MAX_FLOW_CONTROL_WINDOW_SIZE
                                ),
                            BL_MSG()
                                << "An HTTP/2 SETTINGS_INITIAL_WINDOW_SIZE of ours is above 2^31-1"
                            );
                    }

                    if( id == Globals::SETTINGS_ENABLE_PUSH )
                    {
                        BL_CHK(
                            false,
                            value <= 1U,
                            BL_MSG()
                                << "An HTTP/2 SETTINGS_ENABLE_PUSH of ours carries a value other "
                                << "than zero or one"
                            );
                    }
                }
            }

            void applyLocalInitialWindowSize( SAA_in const std::uint32_t value )
            {
                const auto previous = m_localInitialWindowSize;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    it -> second.receiveWindow.applyInitialWindowSizeChange( previous, value );
                }

                m_localInitialWindowSize = value;
            }

            /*************************************************************************************
             * Message validation - RFC 9113 8.1 to 8.3, design 4.5
             */

            static bool isAsciiDigit( SAA_in const char ch ) NOEXCEPT
            {
                const auto octet = static_cast< unsigned char >( ch );

                return octet >= '0' && octet <= '9';
            }

            static bool hasUpperAscii( SAA_in const std::string& value ) NOEXCEPT
            {
                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    const auto octet = static_cast< unsigned char >( value[ i ] );

                    if( octet >= 'A' && octet <= 'Z' )
                    {
                        return true;
                    }
                }

                return false;
            }

            static bool isHeadMethod( SAA_in const std::string& method ) NOEXCEPT
            {
                return method.size() == 4U &&
                    ( method[ 0 ] == 'H' || method[ 0 ] == 'h' ) &&
                    ( method[ 1 ] == 'E' || method[ 1 ] == 'e' ) &&
                    ( method[ 2 ] == 'A' || method[ 2 ] == 'a' ) &&
                    ( method[ 3 ] == 'D' || method[ 3 ] == 'd' );
            }

            /**
             * @brief An ASCII case-insensitive comparison against a lowercase literal
             *
             * Hand-rolled and local, like isHeadMethod( ) above: the shared str::ascii fold this
             * wants lives in core and is a change-set of its own
             */

            static bool equalsAsciiToken(
                SAA_in          const std::string&                   value,
                SAA_in          const char* const                    lowercase
                ) NOEXCEPT
            {
                std::size_t i = 0U;

                for( ; i < value.size(); ++i )
                {
                    const auto ch = value[ i ];

                    const char folded = ( ch >= 'A' && ch <= 'Z' ) ?
                        static_cast< char >( ch - 'A' + 'a' ) : ch;

                    if( lowercase[ i ] == '\0' || folded != lowercase[ i ] )
                    {
                        return false;
                    }
                }

                return lowercase[ i ] == '\0';
            }

            /**
             * @brief RFC 9113 8.2.2 - the fields an HTTP/2 message may never carry
             */

            static bool isConnectionSpecificField( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    name == "connection" ||
                    name == "keep-alive" ||
                    name == "proxy-connection" ||
                    name == "transfer-encoding" ||
                    name == "upgrade";
            }

            /**
             * @brief RFC 9113 8.3.1 - and deliberately not ":protocol"
             *
             * The extended CONNECT of RFC 8441 is one of the things design 4.7 leaves out, so
             * ":protocol" is not a pseudo-header this engine knows and a request carrying one is
             * malformed rather than silently accepted
             */

            static bool isDefinedRequestPseudoHeader( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    name == ":method" ||
                    name == ":scheme" ||
                    name == ":authority" ||
                    name == ":path";
            }

            /**
             * @brief Everything 8.1 to 8.3 can decide about one decoded block
             *
             * Returns an empty string when the message is well formed, and otherwise the reason -
             * which is a string this library wrote and never a peer-controlled byte, because it
             * reaches an exception message and a log
             */

            std::string validateFields(
                SAA_in          const HpackFieldList&                fields,
                SAA_in          const bool                           isTrailerSection,
                SAA_out         unsigned&                            status,
                SAA_out         bool&                                sawContentLength,
                SAA_out         std::int64_t&                        contentLength
                ) const
            {
                status = 0U;
                sawContentLength = false;
                contentLength = 0;

                bool sawRegularField = false;
                bool sawStatus = false;
                bool sawTe = false;

                std::vector< std::string > pseudoHeadersSeen;

                for( std::size_t i = 0U; i < fields.size(); ++i )
                {
                    const auto& name = fields[ i ].name();
                    const auto& value = fields[ i ].value();

                    if( name.empty() )
                    {
                        return "a field name is empty";
                    }

                    if( hasUpperAscii( name ) )
                    {
                        return "a field name carries an uppercase character (8.2.1)";
                    }

                    if( name[ 0 ] == ':' )
                    {
                        if( sawRegularField )
                        {
                            return "a pseudo-header follows a regular field (8.3)";
                        }

                        if( isTrailerSection )
                        {
                            return "a trailer section carries a pseudo-header (8.1)";
                        }

                        if(
                            std::find(
                                pseudoHeadersSeen.begin(),
                                pseudoHeadersSeen.end(),
                                name
                                ) != pseudoHeadersSeen.end()
                            )
                        {
                            return "a pseudo-header appears more than once (8.3)";
                        }

                        pseudoHeadersSeen.push_back( name );

                        const auto reason =
                            validatePseudoHeader( name, value, status, sawStatus );

                        if( ! reason.empty() )
                        {
                            return reason;
                        }

                        continue;
                    }

                    sawRegularField = true;

                    if( ! http::HeaderList::isValidHeaderName( name ) )
                    {
                        return "a field name is not a token (8.2.1)";
                    }

                    if( ! http::HeaderList::isValidHeaderValue( value ) )
                    {
                        return "a field value carries CR, LF, NUL or another forbidden octet "
                            "(8.2.1)";
                    }

                    if( hasEdgeWhitespace( value ) )
                    {
                        return "a field value starts or ends with whitespace (8.2.1)";
                    }

                    if( isConnectionSpecificField( name ) )
                    {
                        return "a connection-specific field is present (8.2.2)";
                    }

                    if( name == "te" )
                    {
                        /*
                         * The value is a token, and RFC 9110 makes a token case-insensitive, so
                         * "te: Trailers" is the same permitted value as "te: trailers"
                         */

                        if( ! equalsAsciiToken( value, "trailers" ) )
                        {
                            return "the te field carries something other than trailers (8.2.2)";
                        }

                        if( sawTe )
                        {
                            return "the te field appears more than once (8.2.2)";
                        }

                        sawTe = true;

                        continue;
                    }

                    if( name == "content-length" )
                    {
                        std::int64_t parsed = 0;

                        if( ! parseContentLength( value, parsed ) )
                        {
                            return "the content-length field is not a non-negative integer (8.1.1)";
                        }

                        if( sawContentLength && parsed != contentLength )
                        {
                            return "two content-length fields disagree (8.1.1)";
                        }

                        sawContentLength = true;
                        contentLength = parsed;
                    }
                }

                if( isTrailerSection )
                {
                    return std::string();
                }

                if( m_role == StreamRole::Client )
                {
                    if( ! sawStatus )
                    {
                        return "a response carries no :status pseudo-header (8.3.2)";
                    }

                    return std::string();
                }

                /*
                 * 8.3.1 - "All HTTP/2 requests MUST include exactly one valid value for the
                 * :method, :scheme, and :path pseudo-header fields". :authority is the one which
                 * may be absent, because the host field may carry it instead
                 */

                static const char* const required[] = { ":method", ":scheme", ":path" };

                for( std::size_t i = 0U; i < 3U; ++i )
                {
                    if(
                        std::find(
                            pseudoHeadersSeen.begin(),
                            pseudoHeadersSeen.end(),
                            std::string( required[ i ] )
                            ) == pseudoHeadersSeen.end()
                        )
                    {
                        return "a request is missing a required pseudo-header (8.3.1)";
                    }
                }

                return std::string();
            }

            std::string validatePseudoHeader(
                SAA_in          const std::string&                   name,
                SAA_in          const std::string&                   value,
                SAA_inout       unsigned&                            status,
                SAA_inout       bool&                                sawStatus
                ) const
            {
                if( m_role == StreamRole::Client )
                {
                    /*
                     * A response carries exactly one pseudo-header and it is :status (8.3.2)
                     */

                    if( name != ":status" )
                    {
                        return "a response carries a pseudo-header other than :status (8.3.2)";
                    }

                    if( sawStatus )
                    {
                        return "a response carries more than one :status (8.3.2)";
                    }

                    if(
                        value.size() != 3U ||
                        ! isAsciiDigit( value[ 0 ] ) ||
                        ! isAsciiDigit( value[ 1 ] ) ||
                        ! isAsciiDigit( value[ 2 ] )
                        )
                    {
                        return ":status is not three decimal digits (8.3.2)";
                    }

                    sawStatus = true;

                    status =
                        static_cast< unsigned >( value[ 0 ] - '0' ) * 100U +
                        static_cast< unsigned >( value[ 1 ] - '0' ) * 10U +
                        static_cast< unsigned >( value[ 2 ] - '0' );

                    if( status == 101U )
                    {
                        /*
                         * 8.1 - "The 101 (Switching Protocols) informational status code ... is
                         * not supported by HTTP/2"
                         */

                        return "a 101 response is not valid in HTTP/2 (8.1)";
                    }

                    return std::string();
                }

                if( ! isDefinedRequestPseudoHeader( name ) )
                {
                    return "a request carries a pseudo-header that is not defined (8.3.1)";
                }

                if( value.empty() && name != ":authority" )
                {
                    return "a required request pseudo-header is empty (8.3.1)";
                }

                return std::string();
            }

            static bool isSpaceOrTab( SAA_in const char ch ) NOEXCEPT
            {
                return ch == ' ' || ch == '\t';
            }

            static bool hasEdgeWhitespace( SAA_in const std::string& value ) NOEXCEPT
            {
                if( value.empty() )
                {
                    return false;
                }

                return
                    isSpaceOrTab( value[ 0 ] ) ||
                    isSpaceOrTab( value[ value.size() - 1U ] );
            }

            static bool parseContentLength(
                SAA_in          const std::string&                   value,
                SAA_out         std::int64_t&                        result
                ) NOEXCEPT
            {
                result = 0;

                if( value.empty() || value.size() > 18U )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < value.size(); ++i )
                {
                    if( ! isAsciiDigit( value[ i ] ) )
                    {
                        return false;
                    }

                    result = result * 10 + static_cast< std::int64_t >( value[ i ] - '0' );
                }

                return true;
            }

            /*************************************************************************************
             * Delivery
             */

            /**
             * @brief Everything RFC 9113 8.1 to 8.3 can say about one completed header block
             *
             * Pure - it records nothing and sends nothing - which is what lets it run BEFORE the
             * state machine is told about the block. Returns an empty string when the message is
             * well formed, and otherwise the reason, which is a string this library wrote and
             * never a peer-controlled byte
             */

            std::string judgeHeaderBlock(
                SAA_in          const StreamContext&                 context,
                SAA_in          const HpackFieldList&                fields,
                SAA_in          const bool                           endStream,
                SAA_out         BlockVerdict&                        verdict
                ) const
            {
                verdict = BlockVerdict();

                if( context.trailersReceived )
                {
                    return "a header block followed the trailer section (8.1)";
                }

                verdict.isTrailerSection = context.headersReceived;

                const auto reason = validateFields(
                    fields,
                    verdict.isTrailerSection,
                    verdict.status,
                    verdict.sawContentLength,
                    verdict.contentLength
                    );

                if( ! reason.empty() )
                {
                    return reason;
                }

                verdict.isInformational =
                    m_role == StreamRole::Client &&
                    verdict.status >= 100U &&
                    verdict.status < 200U;

                if( verdict.isInformational && endStream )
                {
                    return "an informational response ended the stream, leaving no final "
                        "response (8.1)";
                }

                if( verdict.isTrailerSection && ! endStream )
                {
                    return "a trailer section did not carry END_STREAM (8.1)";
                }

                if( endStream && ! verdict.isInformational )
                {
                    /*
                     * The content-length is closed out HERE and not only in onPeerEndStream( ),
                     * because a response whose declared length does not match is malformed and
                     * 8.1 wants a RST_STREAM for it - which this block's own END_STREAM would
                     * otherwise have made unsendable
                     */

                    const bool noContent =
                        context.expectsNoContent ||
                        ( ! verdict.isTrailerSection &&
                            ( verdict.status == 204U || verdict.status == 304U ) );

                    const bool sawLength = verdict.isTrailerSection ?
                        context.sawContentLength.value() : verdict.sawContentLength;

                    const auto declared = verdict.isTrailerSection ?
                        context.declaredContentLength.value() : verdict.contentLength;

                    if( sawLength && ! noContent && context.receivedDataBytes != declared )
                    {
                        return "the DATA total does not match the content-length field (8.1.1)";
                    }
                }

                return std::string();
            }

            /**
             * @brief Records what the block established and turns it into an event
             *
             * It judges nothing: judgeHeaderBlock( ) has already run, before the state machine was
             * told, and its findings arrive in the verdict
             */

            void deliverHeaderBlock(
                SAA_in          const std::uint32_t                  streamId,
                SAA_inout       StreamContext&                       context,
                SAA_inout       HpackFieldList&                      fields,
                SAA_in          const bool                           endStream,
                SAA_in          const BlockVerdict&                  verdict
                )
            {
                if( verdict.isTrailerSection )
                {
                    context.trailersReceived = true;
                }
                else if( ! verdict.isInformational )
                {
                    context.headersReceived = true;
                    context.status = verdict.status;

                    if( verdict.status == 204U || verdict.status == 304U )
                    {
                        context.expectsNoContent = true;
                    }

                    if( verdict.sawContentLength )
                    {
                        context.sawContentLength = true;
                        context.declaredContentLength = verdict.contentLength;
                    }
                }

                SessionEvent event;

                event.type = SessionEventType::Headers;
                event.streamId = streamId;
                event.endStream = endStream;
                event.status = verdict.status;
                event.isInformational = verdict.isInformational;
                event.isTrailers = verdict.isTrailerSection;

                event.fields.swap( fields );

                m_events.push_back( event );

                if( endStream )
                {
                    onPeerEndStream( streamId, context );
                }
            }

            /**
             * @brief The peer has ended its half - is what arrived a complete message?
             *
             * Both paths have already closed the content-length out by the time they reach here,
             * and both for contract 1's reason: judgeHeaderBlock( ) for a message ended by a
             * HEADERS block, judgeDataFrame( ) for one ended by a DATA frame, each judging before
             * its own transition. The same verdict is kept here, from the same two numbers, as
             * the one place that cannot be reached with a half-checked message
             */

            void onPeerEndStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_inout       StreamContext&                       context
                )
            {
                if(
                    context.sawContentLength &&
                    ! context.expectsNoContent &&
                    context.receivedDataBytes != context.declaredContentLength
                    )
                {
                    rejectMalformedMessage(
                        streamId,
                        "the DATA total does not match the content-length field (8.1.1)"
                        );

                    return;
                }

                context.messageComplete = true;
            }

            void rejectMalformedMessage(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::string&                   reason
                )
            {
                rejectStream( streamId, Globals::ERROR_CODE_PROTOCOL_ERROR, reason );
            }

            /**
             * @brief Resets one stream and leaves the connection alone
             *
             * The reason travels on the stream-closed event rather than into a log, which keeps
             * this header free of a logging dependency - nothing else in the sans-I/O core has one
             * - and gives the caller the diagnosis instead of burying it
             */

            void rejectStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                )
            {
                const auto it = m_streams.find( streamId );

                if( it != m_streams.end() )
                {
                    /*
                     * RECORDED WHETHER OR NOT THE RST_STREAM CAN GO OUT. A message which breaks
                     * 8.1 is often the LAST thing on its stream - a malformed final response
                     * carries END_STREAM, so by the time it is judged the stream is already
                     * closed and RFC 9113 5.1 forbids a RST_STREAM on it. The peer then never
                     * hears about it, but the caller still must: the request failed, and it
                     * failed for this reason
                     */

                    it -> second.closeErrorCode = errorCode;
                    it -> second.closeReason = reason;
                }

                sendRstStream( streamId, errorCode );

                reapClosedStreams();
            }

            /*************************************************************************************
             * Stream closure
             */

            /**
             * @brief Every context whose stream the registry no longer holds becomes an event
             *
             * This is also where the connection window is squared up. Whatever arrived on the
             * stream and was never consumed would otherwise be lost to the connection window for
             * good, and a connection leaking a little on every reset stream eventually stalls
             */

            void reapClosedStreams()
            {
                for( auto it = m_streams.begin(); it != m_streams.end(); )
                {
                    if( m_registry.findStream( it -> first ) != nullptr )
                    {
                        ++it;

                        continue;
                    }

                    emitStreamClosed( it -> first, it -> second );

                    m_streams.erase( it++ );
                }

                flushConnectionWindowUpdate( false /* force */ );
            }

            void emitStreamClosed(
                SAA_in          const std::uint32_t                  streamId,
                SAA_inout       StreamContext&                       context
                )
            {
                const auto outstanding = context.receiveWindow.outstanding();

                if( outstanding > 0 )
                {
                    m_receiveConnectionWindow.onConsumed(
                        static_cast< std::int32_t >( outstanding )
                        );
                }

                SessionEvent event;

                event.type = SessionEventType::StreamClosed;
                event.streamId = streamId;
                event.errorCode = context.closeErrorCode;
                event.isMessageComplete = context.messageComplete;
                event.status = context.status;
                event.isRetryable = isRetryable( streamId, context );
                event.reason = context.closeReason;

                m_events.push_back( event );
            }

            /**
             * @brief D6 - was the stream provably not processed by the peer?
             *
             * Only then may a request be replayed transparently. Once any part of a response has
             * arrived the peer has evidently acted on it, and REFUSED_STREAM or a GOAWAY naming a
             * lower last-stream-id is the peer saying outright that it did not
             */

            bool isRetryable(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamContext&                 context
                ) const NOEXCEPT
            {
                if( context.headersReceived )
                {
                    return false;
                }

                if( context.closeErrorCode == Globals::ERROR_CODE_REFUSED_STREAM )
                {
                    return true;
                }

                return m_goAwayReceived && streamId > m_goAwayLastStreamId;
            }

            void closeStreamsAbove(
                SAA_in          const std::uint32_t                  lastStreamId,
                SAA_in          const std::uint32_t                  errorCode
                )
            {
                std::vector< std::uint32_t > doomed;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    if(
                        it -> first > lastStreamId &&
                        StreamStateMachine::isLocallyInitiatedId( it -> first, m_role.value() )
                        )
                    {
                        it -> second.closeErrorCode = errorCode;

                        doomed.push_back( it -> first );
                    }
                }

                for( std::size_t i = 0U; i < doomed.size(); ++i )
                {
                    forceCloseStream( doomed[ i ] );
                }

                reapClosedStreams();
            }

            void closeEveryStream( SAA_in const std::uint32_t errorCode )
            {
                std::vector< std::uint32_t > doomed;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    if( it -> second.closeErrorCode == Globals::ERROR_CODE_NO_ERROR )
                    {
                        it -> second.closeErrorCode = errorCode;
                    }

                    doomed.push_back( it -> first );
                }

                for( std::size_t i = 0U; i < doomed.size(); ++i )
                {
                    forceCloseStream( doomed[ i ] );
                }

                reapClosedStreams();
            }

            /**
             * @brief Takes a stream out of the registry without putting a frame on the wire
             *
             * A GOAWAY or a connection error ends every stream, and there is nothing to send about
             * it: a RST_STREAM after a GOAWAY is pointless and after a connection error is
             * forbidden. The registry only closes a stream in response to a frame, so a
             * RST_STREAM is reported to it and the bytes are not queued
             */

            void forceCloseStream( SAA_in const std::uint32_t streamId )
            {
                auto* const machine = m_registry.findStream( streamId );

                if( machine == nullptr )
                {
                    return;
                }

                if( machine -> canSend( Globals::FRAME_TYPE_RST_STREAM ) )
                {
                    m_registry.onFrameSent(
                        streamId,
                        Globals::FRAME_TYPE_RST_STREAM,
                        Globals::FRAME_FLAG_NONE,
                        m_now
                        );
                }
            }

            /*************************************************************************************
             * WINDOW_UPDATE
             */

            void flushConnectionWindowUpdate( SAA_in const bool force )
            {
                if( ! force && ! m_receiveConnectionWindow.shouldSendWindowUpdate() )
                {
                    return;
                }

                const auto increment = m_receiveConnectionWindow.takeWindowUpdate();

                if( increment == 0 )
                {
                    return;
                }

                FrameCodec::serializeWindowUpdate(
                    Globals::STREAM_ID_CONNECTION,
                    static_cast< std::uint32_t >( increment ),
                    m_controlQueue
                    );

                checkControlQueueBound();
            }

            void flushStreamWindowUpdate(
                SAA_in          const std::uint32_t                  streamId,
                SAA_inout       StreamContext&                       context,
                SAA_in          const bool                           force
                )
            {
                if( ! force && ! context.receiveWindow.shouldSendWindowUpdate() )
                {
                    return;
                }

                const auto* const machine = m_registry.findStream( streamId );

                if(
                    machine == nullptr ||
                    ! machine -> canSend( Globals::FRAME_TYPE_WINDOW_UPDATE )
                    )
                {
                    return;
                }

                const auto increment = context.receiveWindow.takeWindowUpdate();

                if( increment == 0 )
                {
                    return;
                }

                FrameCodec::serializeWindowUpdate(
                    streamId,
                    static_cast< std::uint32_t >( increment ),
                    m_controlQueue
                    );

                checkControlQueueBound();
            }

            /*************************************************************************************
             * The write path
             */

            void appendPseudoHeaders(
                SAA_in          const SessionRequest&                request,
                SAA_inout       HpackFieldList&                      fields
                ) const
            {
                std::vector< Http2PseudoHeader > order = m_profile.pseudoHeaderOrder;

                if( order.empty() )
                {
                    /*
                     * RFC 9113 8.3.1 lists them in this order and a profile which names none gets
                     * it; the browsers differ, which is exactly why the order is a profile field
                     */

                    order.push_back( Http2PseudoHeader::Method );
                    order.push_back( Http2PseudoHeader::Authority );
                    order.push_back( Http2PseudoHeader::Scheme );
                    order.push_back( Http2PseudoHeader::Path );
                }

                for( std::size_t i = 0U; i < order.size(); ++i )
                {
                    switch( order[ i ] )
                    {
                        case Http2PseudoHeader::Method:
                            fields.push_back(
                                HpackField( ":method", cpp::copy( request.method ) )
                                );
                            break;

                        case Http2PseudoHeader::Scheme:
                            fields.push_back(
                                HpackField( ":scheme", cpp::copy( request.scheme ) )
                                );
                            break;

                        case Http2PseudoHeader::Authority:

                            if( ! request.authority.empty() )
                            {
                                fields.push_back(
                                    HpackField( ":authority", cpp::copy( request.authority ) )
                                    );
                            }
                            break;

                        case Http2PseudoHeader::Path:
                            fields.push_back(
                                HpackField( ":path", cpp::copy( request.path ) )
                                );
                            break;
                    }
                }
            }

            /**
             * @brief Serializes a whole header block and queues it as ONE unit
             *
             * The atomicity design 4.5 asks for is structural rather than a rule to remember: the
             * HEADERS frame and every CONTINUATION after it are one element of the queue, so there
             * is no point at which produce( ) could put anything between them
             */

            void queueHeaderBlock(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const HpackFieldList&                fields,
                SAA_in          const bool                           endStream,
                SAA_in          const Http2HeadersPriority&          priority
                )
            {
                std::string block;

                m_encoder.encode( fields, block );

                const auto maxFragment = static_cast< std::size_t >( m_peerMaxFrameSize );

                const auto* const data =
                    block.empty() ? nullptr : reinterpret_cast< const std::uint8_t* >( block.data() );

                const auto first = std::min< std::size_t >( block.size(), maxFragment );
                const bool endHeaders = first == block.size();

                wire_buffer_t frames;

                FramePadding noPadding;

                FrameCodec::serializeHeaders(
                    streamId,
                    data,
                    first,
                    endStream,
                    endHeaders,
                    priority,
                    noPadding,
                    frames
                    );

                std::size_t offset = first;

                while( offset < block.size() )
                {
                    const auto chunk =
                        std::min< std::size_t >( block.size() - offset, maxFragment );

                    FrameCodec::serializeContinuation(
                        data + offset,
                        chunk,
                        streamId,
                        offset + chunk == block.size() /* endHeaders */,
                        frames
                        );

                    offset += chunk;
                }

                m_headerBlockQueue.push_back( wire_buffer_t() );
                m_headerBlockQueue.back().swap( frames );

                m_registry.onFrameSent(
                    streamId,
                    Globals::FRAME_TYPE_HEADERS,
                    static_cast< std::uint8_t >(
                        endStream ? Globals::FRAME_FLAG_END_STREAM : Globals::FRAME_FLAG_NONE
                        ),
                    m_now
                    );

                const auto it = m_streams.find( streamId );

                if( it != m_streams.end() && endStream )
                {
                    it -> second.localEndStreamQueued = true;
                }
            }

            /**
             * @brief How much of this stream's pending body could go out right now
             */

            std::int32_t sendableBytes( SAA_in const StreamContext& context ) const NOEXCEPT
            {
                if( context.pendingBody.empty() )
                {
                    return 0;
                }

                const auto byWindows = std::min< std::int32_t >(
                    context.sendWindow.available(),
                    m_sendConnectionWindow.available()
                    );

                const auto byFrame = std::min< std::int64_t >(
                    static_cast< std::int64_t >( byWindows ),
                    static_cast< std::int64_t >( m_peerMaxFrameSize )
                    );

                return static_cast< std::int32_t >(
                    std::min< std::int64_t >(
                        byFrame,
                        static_cast< std::int64_t >( context.pendingBody.size() )
                        )
                    );
            }

            /**
             * @brief The RFC 9218 scheduler - lowest urgency first, round-robin within a band
             *
             * A stream whose body is done and whose END_STREAM is still owed counts as sendable
             * with nothing to send, so the empty DATA frame that ends it is written.
             * Non-incremental streams sort ahead of incremental ones in their band and are served
             * in identifier order, which is RFC 9218 4.2's "send the response in full before
             * starting another"; the incremental ones share the band round-robin, which is what
             * the cursor is for
             */

            std::uint32_t firstSendableStreamId() const NOEXCEPT
            {
                bool found = false;

                std::uint8_t bestUrgency = 0U;
                bool bestIncremental = false;
                bool bestAfterCursor = false;
                std::uint32_t bestStreamId = Globals::STREAM_ID_CONNECTION;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    const auto& context = it -> second;

                    if( ! isSendable( it -> first, context ) )
                    {
                        continue;
                    }

                    const auto urgency = context.priority.urgency;
                    const bool incremental = context.priority.incremental;
                    const bool afterCursor = it -> first > m_dataCursorStreamId;

                    if(
                        ! found ||
                        isBetterCandidate(
                            urgency,
                            incremental,
                            afterCursor,
                            it -> first,
                            bestUrgency,
                            bestIncremental,
                            bestAfterCursor,
                            bestStreamId
                            )
                        )
                    {
                        found = true;

                        bestUrgency = urgency;
                        bestIncremental = incremental;
                        bestAfterCursor = afterCursor;
                        bestStreamId = it -> first;
                    }
                }

                return found ? bestStreamId : Globals::STREAM_ID_CONNECTION;
            }

            static bool isBetterCandidate(
                SAA_in          const std::uint8_t                   urgency,
                SAA_in          const bool                           incremental,
                SAA_in          const bool                           afterCursor,
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint8_t                   bestUrgency,
                SAA_in          const bool                           bestIncremental,
                SAA_in          const bool                           bestAfterCursor,
                SAA_in          const std::uint32_t                  bestStreamId
                ) NOEXCEPT
            {
                if( urgency != bestUrgency )
                {
                    return urgency < bestUrgency;
                }

                if( incremental != bestIncremental )
                {
                    return ! incremental;
                }

                if( ! incremental )
                {
                    return streamId < bestStreamId;
                }

                if( afterCursor != bestAfterCursor )
                {
                    return afterCursor;
                }

                return streamId < bestStreamId;
            }

            bool isSendable(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const StreamContext&                 context
                ) const NOEXCEPT
            {
                if( context.localEndStreamQueued )
                {
                    return false;
                }

                const auto* const machine = m_registry.findStream( streamId );

                if( machine == nullptr || ! machine -> canSend( Globals::FRAME_TYPE_DATA ) )
                {
                    return false;
                }

                if( context.pendingBody.empty() )
                {
                    /*
                     * Nothing left but the END_STREAM the caller has already declared - an empty
                     * DATA frame costs no window and is what ends the stream
                     */

                    return context.pendingBodyEndStream;
                }

                return sendableBytes( context ) > 0;
            }

            void writeOneDataFrame(
                SAA_in          const std::uint32_t                  streamId,
                SAA_inout       StreamContext&                       context,
                SAA_inout       wire_buffer_t&                       out
                )
            {
                const auto count = sendableBytes( context );

                const bool endStream =
                    context.pendingBodyEndStream &&
                    static_cast< std::size_t >( count ) == context.pendingBody.size();

                if( count != 0 )
                {
                    const auto takenFromStream = context.sendWindow.take( count );
                    const auto takenFromConnection = m_sendConnectionWindow.take( count );

                    BL_CHK(
                        false,
                        takenFromStream == count && takenFromConnection == count,
                        BL_MSG()
                            << "An HTTP/2 DATA frame was scheduled for more octets than its "
                            << "windows could pay for"
                        );
                }

                FramePadding noPadding;

                FrameCodec::serializeData(
                    streamId,
                    count != 0 ?
                        reinterpret_cast< const std::uint8_t* >( context.pendingBody.data() ) :
                        nullptr,
                    static_cast< std::size_t >( count ),
                    endStream,
                    noPadding,
                    out
                    );

                context.pendingBody.erase( 0U, static_cast< std::size_t >( count ) );

                m_dataCursorStreamId = streamId;

                m_registry.onFrameSent(
                    streamId,
                    Globals::FRAME_TYPE_DATA,
                    static_cast< std::uint8_t >(
                        endStream ? Globals::FRAME_FLAG_END_STREAM : Globals::FRAME_FLAG_NONE
                        ),
                    m_now
                    );

                if( endStream )
                {
                    context.localEndStreamQueued = true;
                }
            }
        };

        typedef SessionT<> Session;

    } // http2

} // bl

#endif /* __BL_HTTP2_SESSION_H_ */
