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

#ifndef __BL_HTTP2_FLOWCONTROLWINDOW_H_
#define __BL_HTTP2_FLOWCONTROLWINDOW_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <cstdint>
#include <string>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class FlowControlWindowT - one RFC 9113 section 6.9 flow-control window
         *
         * A connection has four of these: the window the peer granted us and the window we
         * granted the peer, and the same pair again for every open stream. The arithmetic is the
         * same in all four, which is why there is one class and not two, and the direction shows
         * only in which operations a caller uses
         *
         * THE WINDOW IS SIGNED, AND THAT IS NOT AN OVERSIGHT. Section 6.9.2 lets a change to
         * SETTINGS_INITIAL_WINDOW_SIZE reduce a window below what is already in flight, and the
         * RFC's own worked example has a client recompute its window as -44 KB and go on holding
         * it there until WINDOW_UPDATE frames bring it back above zero. http2::Globals defines
         * INITIAL_WINDOW_SIZE_DEFAULT and MAX_FLOW_CONTROL_WINDOW_SIZE as std::int32_t for this
         * reason. An unsigned window would wrap into an enormous positive one at exactly the
         * moment the sender is required to stop sending
         *
         * THE LEVEL IS THE STREAM IDENTIFIER, as it is in a frame header: zero is the connection
         * (4.1, 6.9.1). It decides which error a violation is, because 6.9 assigns the same fault
         * a stream error on a stream window and a connection error on the connection window, and
         * the window is the only thing which knows which it is. The one exception is stated on
         * applyInitialWindowSizeChange below
         *
         * WHAT IS FLOW CONTROLLED. Only DATA frames (6.9), and the whole of a DATA frame payload
         * counts - the Pad Length octet and the padding with it (6.1). So the length a caller
         * passes to consume( ) is the frame header's Length field, not the size of the data the
         * frame codec hands back
         *
         * Sans-I/O and stateless beyond this one number: nothing here knows of Asio, sockets,
         * tasks or locks (design 2.1). A window is not thread safe and does not need to be - the
         * session which owns it is single threaded by contract (4.5)
         */

        template
        <
            typename E = void
        >
        class FlowControlWindowT
        {
        private:

            cpp::ScalarTypeIniter< std::int32_t >                               m_size;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_streamId;

            std::string where() const
            {
                return m_streamId == Globals::STREAM_ID_CONNECTION ?
                    std::string( "the connection flow-control window" ) :
                    std::string( "the flow-control window of stream " ) +
                        std::to_string( m_streamId.value() );
            }

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
                    );
            }

            SAA_noreturn
            void throwStreamError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                ) const
            {
                BL_THROW(
                    Http2StreamException()
                        << eh::errinfo_http2_error_code( errorCode )
                        << eh::errinfo_http2_stream_id( m_streamId.value() ),
                    BL_MSG()
                        << "HTTP/2 stream error "
                        << Globals::errorCodeToString( errorCode )
                        << " - "
                        << reason
                    );
            }

            /**
             * @brief Raises the fault at the level this window belongs to - 6.9's standing rule
             */

            SAA_noreturn
            void throwAtThisLevel(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                ) const
            {
                if( m_streamId == Globals::STREAM_ID_CONNECTION )
                {
                    throwConnectionError( errorCode, reason );
                }

                throwStreamError( errorCode, reason );
            }

            void setSizeChecked(
                SAA_in          const std::int64_t                   value,
                SAA_in          const bool                           asConnectionError,
                SAA_in          const std::string&                   reason
                )
            {
                if( value > static_cast< std::int64_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ) )
                {
                    if( asConnectionError )
                    {
                        throwConnectionError( Globals::ERROR_CODE_FLOW_CONTROL_ERROR, reason );
                    }

                    throwAtThisLevel( Globals::ERROR_CODE_FLOW_CONTROL_ERROR, reason );
                }

                /*
                 * The other end of the range cannot be reached while both peers respect the
                 * maximum: the deltas of successive SETTINGS_INITIAL_WINDOW_SIZE changes
                 * telescope, so a window is never driven below -( 2^31 - 1 ). It is checked
                 * rather than assumed, because the alternative to a diagnosis here is signed
                 * overflow, which is undefined behaviour rather than a wrong number
                 */

                if( value < -static_cast< std::int64_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ) )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
                        reason
                        );
                }

                m_size = static_cast< std::int32_t >( value );
            }

        public:

            /**
             * @brief A window for 'streamId', which is zero for the connection as a whole
             *
             * Both a new stream and the connection start at INITIAL_WINDOW_SIZE_DEFAULT, which is
             * also the initial value of SETTINGS_INITIAL_WINDOW_SIZE (6.5.2, 6.9.2)
             */

            explicit FlowControlWindowT(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in_opt      const std::int32_t                   initialSize =
                                    Globals::INITIAL_WINDOW_SIZE_DEFAULT
                )
                :
                m_size( initialSize ),
                m_streamId( streamId )
            {
                BL_CHK(
                    false,
                    initialSize >= 0 &&
                        initialSize <= Globals::MAX_FLOW_CONTROL_WINDOW_SIZE,
                    BL_MSG()
                        << "An HTTP/2 flow-control window cannot start outside the range RFC 9113 "
                        << "6.9.2 allows"
                    );

                BL_CHK(
                    false,
                    streamId <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "An HTTP/2 stream identifier does not fit 31 bits"
                    );
            }

            std::uint32_t streamId() const NOEXCEPT
            {
                return m_streamId;
            }

            bool isConnectionLevel() const NOEXCEPT
            {
                return m_streamId == Globals::STREAM_ID_CONNECTION;
            }

            /**
             * @brief What is left of the window, which may legally be negative
             */

            std::int32_t size() const NOEXCEPT
            {
                return m_size;
            }

            /**
             * @brief What may be sent right now - the window, or nothing while it is negative
             */

            std::int32_t available() const NOEXCEPT
            {
                return m_size > 0 ? m_size.value() : 0;
            }

            bool isExhausted() const NOEXCEPT
            {
                return m_size <= 0;
            }

            /**
             * @brief Accounts for a DATA frame of 'bytes' octets against this window
             *
             * Sending: the session has already asked what was available, so more than that is a
             * programming error - which is what take( ) below exists to make impossible
             *
             * Receiving: the peer sending more than we granted it is not, and 6.9.1 lets a
             * receiver answer that with a stream or a connection error of type
             * FLOW_CONTROL_ERROR, at the level of the window it overran
             */

            void consume( SAA_in const std::int32_t bytes )
            {
                BL_CHK(
                    false,
                    bytes >= 0,
                    BL_MSG()
                        << "A negative number of octets cannot be taken from a flow-control window"
                    );

                if( bytes > m_size )
                {
                    throwAtThisLevel(
                        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
                        "a DATA frame of " + std::to_string( bytes ) +
                            " octets does not fit " + where()
                        );
                }

                m_size = m_size - bytes;
            }

            /**
             * @brief Takes up to 'wanted' octets and returns how many were actually taken
             *
             * The write path of the session asks both its windows - the stream's and the
             * connection's - and sends the smaller answer, so a DATA frame is never written which
             * either window cannot pay for
             */

            std::int32_t take( SAA_in const std::int32_t wanted )
            {
                BL_CHK(
                    false,
                    wanted >= 0,
                    BL_MSG()
                        << "A negative number of octets cannot be taken from a flow-control window"
                    );

                const auto taken = wanted < available() ? wanted : available();

                m_size = m_size - taken;

                return taken;
            }

            /**
             * @brief A WINDOW_UPDATE which arrived from the peer - RFC 9113 6.9
             *
             * An increment of zero is a PROTOCOL_ERROR, and a window driven past 2^31-1 is a
             * FLOW_CONTROL_ERROR; both are stream errors on a stream window and connection errors
             * on the connection window, which is the rule this class holds the stream identifier
             * for
             *
             * The argument is unsigned because that is how it arrives: the frame codec masks the
             * reserved bit off and hands over the 31 bit field
             */

            void applyWindowUpdate( SAA_in const std::uint32_t increment )
            {
                if( increment == 0U )
                {
                    throwAtThisLevel(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "a WINDOW_UPDATE for " + where() + " carries an increment of zero"
                        );
                }

                if( increment > static_cast< std::uint32_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ) )
                {
                    throwAtThisLevel(
                        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
                        "a WINDOW_UPDATE increment is larger than a flow-control window may ever be"
                        );
                }

                setSizeChecked(
                    static_cast< std::int64_t >( m_size.value() ) +
                        static_cast< std::int64_t >( increment ),
                    false /* asConnectionError */,
                    "a WINDOW_UPDATE would drive " + where() + " past 2^31-1"
                    );
            }

            /**
             * @brief Grants octets to a window of ours, because we are about to advertise them
             *
             * The receive side's own increase, which is not a WINDOW_UPDATE arriving but one
             * being sent. There is no zero rule here - crediting nothing is a no-op rather than a
             * protocol error - and an overflow is our own arithmetic being wrong
             */

            void grant( SAA_in const std::int32_t increment )
            {
                BL_CHK(
                    false,
                    increment >= 0,
                    BL_MSG()
                        << "A flow-control window cannot be granted a negative number of octets"
                    );

                BL_CHK(
                    false,
                    static_cast< std::int64_t >( m_size.value() ) +
                        static_cast< std::int64_t >( increment ) <=
                            static_cast< std::int64_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ),
                    BL_MSG()
                        << "An HTTP/2 flow-control window would be granted past 2^31-1"
                    );

                m_size = m_size + increment;
            }

            /**
             * @brief The SETTINGS_INITIAL_WINDOW_SIZE re-adjustment of RFC 9113 6.9.2
             *
             * A change to that setting adjusts every open STREAM window by the difference,
             * retroactively, and may well drive one negative - which is legal, and is the whole
             * reason these windows are signed. It does NOT touch the connection window: "a
             * SETTINGS frame cannot alter the connection flow-control window", so calling this on
             * a connection-level window is a programming error
             *
             * The overflow here is the one exception to the standing rule above: 6.9.2 makes a
             * change which drives any window past the maximum a CONNECTION error even though the
             * window it overflowed is a stream's, because the fault is in the SETTINGS frame
             *
             * Both values arrive as the unsigned 32 bit setting values they are on the wire.
             * 6.5.2 makes anything above 2^31-1 a connection error of type FLOW_CONTROL_ERROR,
             * which is checked here rather than left for a caller to remember
             */

            void applyInitialWindowSizeChange(
                SAA_in          const std::uint32_t                  previousValue,
                SAA_in          const std::uint32_t                  newValue
                )
            {
                BL_CHK(
                    false,
                    ! isConnectionLevel(),
                    BL_MSG()
                        << "A SETTINGS frame cannot alter the connection flow-control window"
                    );

                validateInitialWindowSize( previousValue );
                validateInitialWindowSize( newValue );

                const auto delta =
                    static_cast< std::int64_t >( newValue ) -
                    static_cast< std::int64_t >( previousValue );

                setSizeChecked(
                    static_cast< std::int64_t >( m_size.value() ) + delta,
                    true /* asConnectionError */,
                    "a change to SETTINGS_INITIAL_WINDOW_SIZE would drive " + where() +
                        " past 2^31-1"
                    );
            }

            /**
             * @brief RFC 9113 6.5.2 - a SETTINGS_INITIAL_WINDOW_SIZE above 2^31-1 is a connection
             * error of type FLOW_CONTROL_ERROR
             */

            void validateInitialWindowSize( SAA_in const std::uint32_t value ) const
            {
                if( value > static_cast< std::uint32_t >( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE ) )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_FLOW_CONTROL_ERROR,
                        "a SETTINGS_INITIAL_WINDOW_SIZE above 2^31-1 was received"
                        );
                }
            }
        };

        typedef FlowControlWindowT<> FlowControlWindow;

        /**
         * @brief class ReceiveFlowControlWindowT - the window we granted, and when to replenish it
         *
         * The send side of flow control is arithmetic. The receive side is a POLICY, and it is
         * the backpressure mechanism of the whole client (design 4.4, 5.3): a WINDOW_UPDATE is
         * sent when the CONSUMER has taken the bytes, never when they merely arrived. A client
         * which replenishes on arrival has no backpressure at all - it has an unbounded buffer
         * with extra steps, and a slow reader turns into memory growth instead of a stalled peer
         *
         * So three numbers, not one:
         *
         *  - the window itself, which shrinks as DATA arrives
         *  - what has arrived and not yet been given back, 'outstanding'
         *  - what the consumer has taken and we have not yet advertised, 'pending credit'
         *
         * A WINDOW_UPDATE is due once the pending credit reaches the threshold, which defaults to
         * half the window as the design says and which a profile may set to its own value
         * (Http2Profile::windowUpdateThreshold, 6.4)
         *
         * THE CLOSED STREAM CASE, which the stream state machine (4.3) depends on: a DATA frame
         * which arrives for a stream we have already closed still spends the CONNECTION window,
         * and nobody will ever consume it - so its bytes are received and consumed in the same
         * breath, onDataReceived( ) then onConsumed( ), on the connection-level window. Skipping
         * either half leaks the connection window until the connection stalls for good
         */

        template
        <
            typename E = void
        >
        class ReceiveFlowControlWindowT
        {
        private:

            FlowControlWindow                                                   m_window;

            cpp::ScalarTypeIniter< std::int64_t >                               m_outstanding;
            cpp::ScalarTypeIniter< std::int64_t >                               m_pendingCredit;
            cpp::ScalarTypeIniter< std::int32_t >                               m_updateThreshold;

            /*
             * WHOSE NUMBER THE THRESHOLD IS - H15. The constructor derives HALF THE WINDOW, which
             * is a policy and has to follow the window when the window moves; a caller which set
             * the threshold itself stated a NUMBER, and a number stated is not ours to recompute.
             * setUpdateThreshold( ) has exactly two callers and both are already gated on
             * profile.windowUpdateThreshold != 0U, so "explicit" and "a profile set it" are the
             * same predicate and this flag needs no plumbing
             */

            cpp::ScalarTypeIniter< bool >                                       m_isThresholdExplicit;

        public:

            explicit ReceiveFlowControlWindowT(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in_opt      const std::int32_t                   initialSize =
                                    Globals::INITIAL_WINDOW_SIZE_DEFAULT
                )
                :
                m_window( streamId, initialSize ),
                m_updateThreshold( initialSize / 2 )
            {
            }

            const FlowControlWindow& window() const NOEXCEPT
            {
                return m_window;
            }

            std::uint32_t streamId() const NOEXCEPT
            {
                return m_window.streamId();
            }

            std::int32_t size() const NOEXCEPT
            {
                return m_window.size();
            }

            std::int32_t updateThreshold() const NOEXCEPT
            {
                return m_updateThreshold;
            }

            /**
             * @brief The profile's own threshold (6.4), in place of half the window
             *
             * Zero means every acknowledged octet is advertised immediately, which is legal and
             * is what a latency-sensitive caller would choose at the cost of more frames
             */

            void setUpdateThreshold( SAA_in const std::int32_t threshold )
            {
                BL_CHK(
                    false,
                    threshold >= 0,
                    BL_MSG()
                        << "A WINDOW_UPDATE threshold cannot be negative"
                    );

                m_updateThreshold = threshold;

                m_isThresholdExplicit = true;
            }

            /**
             * @brief The RFC 9113 6.9.2 re-adjustment, applied to the window we granted
             *
             * The send-side counterpart is applied when the PEER's SETTINGS arrives. This one is
             * applied when the peer ACKNOWLEDGES ours (6.5.3), and the difference is not a detail:
             * until the acknowledgement the peer is entitled to the old value, and every octet it
             * sent under it precedes the acknowledgement on the wire. Applied at send time
             * instead, a window lowered by our own SETTINGS makes onDataReceived( ) raise
             * FLOW_CONTROL_ERROR on data the peer sent perfectly legally
             *
             * Neither 'outstanding' nor 'pendingCredit' moves: what arrived still arrived and what
             * the consumer took is still owed to the peer. Only the granted window shifts by the
             * difference, which is exactly what the peer did to its own send window
             */

            void applyInitialWindowSizeChange(
                SAA_in          const std::uint32_t                  previousValue,
                SAA_in          const std::uint32_t                  newValue
                )
            {
                m_window.applyInitialWindowSizeChange( previousValue, newValue );

                /*
                 * H15 - AND THE THRESHOLD MOVES WITH IT, unless a profile stated one. The
                 * constructor derived half of whatever window the stream was OPENED with, and a
                 * stream opened before our SETTINGS was acknowledged was opened at 65535 - so a
                 * threshold of 32767 was left behind on a window the acknowledgement has just
                 * lowered to, say, 1024. Pending credit could then never reach it, no
                 * WINDOW_UPDATE was ever due, and the stream stopped for good
                 *
                 * It is confined to streams that were ALREADY OPEN: createStreamContext( ) builds
                 * each new stream's window from m_localInitialWindowSize, which
                 * applyLocalInitialWindowSize( ) has updated by then, so a stream opened after the
                 * acknowledgement gets the right size and the right half of it for free
                 */

                if( ! m_isThresholdExplicit )
                {
                    m_updateThreshold = static_cast< std::int32_t >( newValue / 2U );
                }
            }

            std::int64_t outstanding() const NOEXCEPT
            {
                return m_outstanding;
            }

            std::int64_t pendingCredit() const NOEXCEPT
            {
                return m_pendingCredit;
            }

            /**
             * @brief A DATA frame arrived - the whole of its payload spends this window
             */

            void onDataReceived( SAA_in const std::int32_t bytes )
            {
                m_window.consume( bytes );

                m_outstanding = m_outstanding + bytes;
            }

            /**
             * @brief The consumer took 'bytes' of what arrived, which may now be advertised again
             *
             * Crediting more than arrived would advertise a window we never spent, and the peer
             * would take us at our word - so it is refused rather than clamped. It is also the
             * shape of the two defects this accounting exists to prevent: the same DATA frame
             * credited twice, and a closed stream's DATA credited on the stream window that is
             * already gone instead of on the connection's
             */

            void onConsumed( SAA_in const std::int32_t bytes )
            {
                BL_CHK(
                    false,
                    bytes >= 0,
                    BL_MSG()
                        << "A negative number of octets cannot be consumed"
                    );

                BL_CHK(
                    false,
                    static_cast< std::int64_t >( bytes ) <= m_outstanding,
                    BL_MSG()
                        << "More octets were acknowledged on an HTTP/2 flow-control window than "
                        << "ever arrived on it"
                    );

                m_outstanding = m_outstanding - bytes;
                m_pendingCredit = m_pendingCredit + bytes;
            }

            /**
             * @brief Whether a WINDOW_UPDATE is due - the half-window threshold of design 4.4,
             * with a LIVENESS FLOOR under it
             *
             * H15(b), and it is not the same fix as the threshold recomputation above. That one
             * keeps the DEFAULT policy honest when the window moves; this one makes the wedge
             * unreachable by construction, including for a profile which asks for a threshold
             * LARGER than the window it also asks for - a caller mistake this library should not
             * turn into a hang.
             *
             * AN EXHAUSTED WINDOW WITH CREDIT OWED IS A STALL WHATEVER THE THRESHOLD IS, and the
             * threshold's only purpose - batching frames - is worthless once there is nothing
             * left to batch FOR. What it trades is frames for liveness: a small window with a
             * consumer taking a few octets at a time emits an update per consumption once the
             * window is empty. That is the only behaviour which makes progress at all, and it
             * fires only at zero
             */

            bool shouldSendWindowUpdate() const NOEXCEPT
            {
                return m_pendingCredit > 0 &&
                    ( m_pendingCredit >= m_updateThreshold || m_window.size() <= 0 );
            }

            /**
             * @brief The increment to put in a WINDOW_UPDATE, having credited it to this window
             *
             * Returns zero when there is nothing to advertise, because a WINDOW_UPDATE of zero is
             * a PROTOCOL_ERROR for the peer which receives it (6.9) - so a caller sends a frame
             * only for a non-zero answer. It ignores the threshold, which is shouldSendWindowUpdate( )'s
             * business, so that a stream being closed can flush what it owes
             */

            std::int32_t takeWindowUpdate()
            {
                if( m_pendingCredit == 0 )
                {
                    return 0;
                }

                const auto increment = static_cast< std::int32_t >( m_pendingCredit.value() );

                m_window.grant( increment );

                m_pendingCredit = 0;

                return increment;
            }
        };

        typedef ReceiveFlowControlWindowT<> ReceiveFlowControlWindow;

    } // http2

} // bl

#endif /* __BL_HTTP2_FLOWCONTROLWINDOW_H_ */
