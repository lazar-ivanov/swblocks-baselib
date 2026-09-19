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

#ifndef __BL_HTTP2_FRAMECODEC_H_
#define __BL_HTTP2_FRAMECODEC_H_

#include <baselib/http2/Globals.h>
#include <baselib/http2/Http2Profile.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bl
{
    namespace http2
    {
        /**
         * @brief The nine octet frame header of RFC 9113 section 4.1
         *
         * The reserved bit which precedes the stream identifier is not represented: it is ignored
         * on receipt and left unset when sending, exactly as 4.1 requires, so keeping it would
         * only offer somewhere for it to be got wrong
         *
         * 'type' and 'flags' are octets rather than the enumerations of Globals, because both are
         * open sets on the wire - an unknown type is ignored (4.1) and a flag with no defined
         * semantics for its type is ignored - and an enumeration would suggest they are closed
         */

        struct FrameHeader
        {
            cpp::ScalarTypeIniter< std::uint32_t >                              length;
            cpp::ScalarTypeIniter< std::uint8_t >                               type;
            cpp::ScalarTypeIniter< std::uint8_t >                               flags;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamId;
        };

        /**
         * @brief One complete frame, as a header plus a borrowed view of its payload
         *
         * LIFETIME. 'payload' points into the buffer of the FrameReader which produced the frame
         * and is valid only until the next call to consumeFrame() or feed(). Nothing here owns
         * anything; a consumer which needs the bytes to outlive the frame copies them
         *
         * 'streamErrorCode' is NO_ERROR for a well formed frame. When it is not, the frame was
         * structurally invalid in a way RFC 9113 makes a STREAM error rather than a connection
         * error - see FrameCodecT::validateFrameHeader - and the whole frame has still been
         * consumed so that the connection stays in sync. Such a frame is answered with
         * RST_STREAM( streamErrorCode ) and must not be acted on otherwise; its payload accessors
         * will throw. A connection error is never reported this way: it is thrown, because there
         * is no state in which parsing could usefully continue
         */

        struct FrameView
        {
            FrameHeader                                                         header;
            cpp::ScalarTypeIniter< const std::uint8_t* >                        payload;
            cpp::ScalarTypeIniter< std::size_t >                                payloadSize;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamErrorCode;
        };

        /**
         * @brief The optional padding of a DATA, HEADERS or PUSH_PROMISE frame being serialized
         *
         * 'isSet' is what distinguishes "no padding" from "padding of length zero", which are
         * different frames: the second still carries the PADDED flag and the Pad Length octet, so
         * it is one octet longer. Same reason Http2HeadersPriority carries an isSet of its own
         */

        struct FramePadding
        {
            cpp::ScalarTypeIniter< bool >                                       isSet;
            cpp::ScalarTypeIniter< std::uint8_t >                               padLength;
        };

        /**
         * @brief The payload of a DATA frame - RFC 9113 section 6.1
         */

        struct DataPayload
        {
            cpp::ScalarTypeIniter< const std::uint8_t* >                        data;
            cpp::ScalarTypeIniter< std::size_t >                                size;
            cpp::ScalarTypeIniter< std::uint8_t >                               padLength;
            cpp::ScalarTypeIniter< bool >                                       isPadded;
            cpp::ScalarTypeIniter< bool >                                       endStream;
        };

        /**
         * @brief The payload of a HEADERS frame - RFC 9113 section 6.2
         *
         * The priority fields are the profile's own Http2HeadersPriority, so what a profile says
         * to emit and what was parsed from the wire are the same type
         */

        struct HeadersPayload
        {
            cpp::ScalarTypeIniter< const std::uint8_t* >                        fieldBlock;
            cpp::ScalarTypeIniter< std::size_t >                                fieldBlockSize;
            Http2HeadersPriority                                                priority;
            cpp::ScalarTypeIniter< std::uint8_t >                               padLength;
            cpp::ScalarTypeIniter< bool >                                       isPadded;
            cpp::ScalarTypeIniter< bool >                                       endStream;
            cpp::ScalarTypeIniter< bool >                                       endHeaders;
        };

        /**
         * @brief The payload of a PUSH_PROMISE frame - RFC 9113 section 6.6
         *
         * Parsed although D11 disables push and makes a received PUSH_PROMISE a connection error:
         * the error is the session's to raise (8.4), and it can only raise it for a frame it was
         * able to read. The protocol core is role neutral (D8), so the test peer serializes one
         */

        struct PushPromisePayload
        {
            cpp::ScalarTypeIniter< std::uint32_t >                              promisedStreamId;
            cpp::ScalarTypeIniter< const std::uint8_t* >                        fieldBlock;
            cpp::ScalarTypeIniter< std::size_t >                                fieldBlockSize;
            cpp::ScalarTypeIniter< std::uint8_t >                               padLength;
            cpp::ScalarTypeIniter< bool >                                       isPadded;
            cpp::ScalarTypeIniter< bool >                                       endHeaders;
        };

        /**
         * @brief The payload of a GOAWAY frame - RFC 9113 section 6.8
         *
         * The debug data is a borrowed view under the lifetime rule of FrameView, and it is
         * arbitrary peer controlled bytes: it is never logged or put in an exception message
         * unescaped
         */

        struct GoAwayPayload
        {
            cpp::ScalarTypeIniter< std::uint32_t >                              lastStreamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              errorCode;
            cpp::ScalarTypeIniter< const std::uint8_t* >                        debugData;
            cpp::ScalarTypeIniter< std::size_t >                                debugDataSize;
        };

        /**
         * @brief The payload of a CONTINUATION frame - RFC 9113 section 6.10
         */

        struct ContinuationPayload
        {
            cpp::ScalarTypeIniter< const std::uint8_t* >                        fieldBlock;
            cpp::ScalarTypeIniter< std::size_t >                                fieldBlockSize;
            cpp::ScalarTypeIniter< bool >                                       endHeaders;
        };

        /**
         * @brief class FrameCodecT - the RFC 9113 section 4 and 6 frame format, both directions
         *
         * Stateless and sans-I/O: every member is static and nothing here knows of Asio, OpenSSL,
         * tasks or locks (design 2.1). The cross frame state a real connection needs - whether a
         * header block is open, how much of a frame has arrived - lives in FrameReaderT below
         *
         * ERRORS. Validation failures are thrown, not returned, which is the library's mechanism
         * and is what S1.3 added Http2ProtocolException and Http2StreamException for. Both carry
         * eh::errinfo_http2_error_code, so the session engine reads the RFC 9113 section 7 code
         * out of the exception and puts it straight into a GOAWAY or a RST_STREAM. The one case
         * which is NOT thrown from the reader is a stream error, because a stream error must not
         * stop the connection - see FrameView::streamErrorCode
         *
         * The one exception to "throw" is a programming error - a parse accessor called for the
         * wrong frame type - which is an UnexpectedException like everywhere else in the library
         *
         * WHAT IS NOT VALIDATED HERE, deliberately. Anything which needs connection state: a
         * WINDOW_UPDATE increment of zero and window overflow belong to FlowControlWindow (4.4),
         * a frame arriving in the wrong stream state belongs to StreamStateMachine (4.3), and
         * settings values out of range, header block semantics and the rate limits of 4.6 belong
         * to the session engine (4.5). This class decides only what the nine octet header and the
         * payload layout can decide on their own
         */

        template
        <
            typename E = void
        >
        class FrameCodecT
        {
            BL_DECLARE_STATIC( FrameCodecT )

        private:

            /*************************************************************************************
             * Fixed payload sizes and minimums, RFC 9113 section 6
             */

            enum : std::uint32_t
            {
                PRIORITY_FIELDS_SIZE                = 5U,       /* E + Stream Dependency + Weight */
                RST_STREAM_PAYLOAD_SIZE             = 4U,
                SETTINGS_ENTRY_SIZE                 = 6U,       /* identifier + value */
                PING_PAYLOAD_SIZE                   = 8U,
                GOAWAY_MIN_PAYLOAD_SIZE             = 8U,       /* last stream id + error code */
                WINDOW_UPDATE_PAYLOAD_SIZE          = 4U,
                PUSH_PROMISE_MIN_PAYLOAD_SIZE       = 4U,       /* the promised stream id */
                PAD_LENGTH_FIELD_SIZE               = 1U,
            };

            /*************************************************************************************
             * Network byte order, on octets rather than on a machine word - a frame header is
             * never guaranteed to be aligned in a read buffer
             */

            static std::uint32_t readUint24( SAA_in const std::uint8_t* const buffer ) NOEXCEPT
            {
                return
                    ( static_cast< std::uint32_t >( buffer[ 0 ] ) << 16 ) |
                    ( static_cast< std::uint32_t >( buffer[ 1 ] ) << 8 )  |
                      static_cast< std::uint32_t >( buffer[ 2 ] );
            }

            static std::uint32_t readUint32( SAA_in const std::uint8_t* const buffer ) NOEXCEPT
            {
                return
                    ( static_cast< std::uint32_t >( buffer[ 0 ] ) << 24 ) |
                    ( static_cast< std::uint32_t >( buffer[ 1 ] ) << 16 ) |
                    ( static_cast< std::uint32_t >( buffer[ 2 ] ) << 8 )  |
                      static_cast< std::uint32_t >( buffer[ 3 ] );
            }

            static std::uint16_t readUint16( SAA_in const std::uint8_t* const buffer ) NOEXCEPT
            {
                return static_cast< std::uint16_t >(
                    ( static_cast< std::uint32_t >( buffer[ 0 ] ) << 8 ) |
                      static_cast< std::uint32_t >( buffer[ 1 ] )
                    );
            }

            static void appendUint8(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const std::uint32_t                  value
                )
            {
                out.push_back( static_cast< std::uint8_t >( value & 0xFFU ) );
            }

            static void appendUint16(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const std::uint32_t                  value
                )
            {
                appendUint8( out, value >> 8 );
                appendUint8( out, value );
            }

            static void appendUint24(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const std::uint32_t                  value
                )
            {
                appendUint8( out, value >> 16 );
                appendUint8( out, value >> 8 );
                appendUint8( out, value );
            }

            static void appendUint32(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const std::uint32_t                  value
                )
            {
                appendUint8( out, value >> 24 );
                appendUint8( out, value >> 16 );
                appendUint8( out, value >> 8 );
                appendUint8( out, value );
            }

            /**
             * @brief The RFC 9113 4.2 rule for a frame which is too small or the wrong size
             *
             * "A frame size error in a frame that could alter the state of the entire connection
             * MUST be treated as a connection error; this includes any frame carrying a field
             * block, a SETTINGS frame, and any frame with a stream identifier of 0"
             */

            static void checkExactLength(
                SAA_in          const FrameHeader&                   header,
                SAA_in          const std::uint32_t                  expected,
                SAA_in          const std::string&                   frameName
                )
            {
                if( header.length != expected )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_FRAME_SIZE_ERROR,
                        "a " + frameName + " frame must carry exactly " +
                            std::to_string( expected ) + " octets"
                        );
                }
            }

            static void checkMinimumLength(
                SAA_in          const FrameHeader&                   header,
                SAA_in          const std::uint32_t                  minimum,
                SAA_in          const std::string&                   frameName
                )
            {
                if( header.length < minimum )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_FRAME_SIZE_ERROR,
                        "a " + frameName + " frame is too small to carry the fields its flags "
                            "say are present"
                        );
                }
            }

            static void checkStreamIdIsNotZero(
                SAA_in          const FrameHeader&                   header,
                SAA_in          const std::string&                   frameName
                )
            {
                if( header.streamId == Globals::STREAM_ID_CONNECTION )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "a " + frameName + " frame must be associated with a stream, and its "
                            "stream identifier is zero"
                        );
                }
            }

            static void checkStreamIdIsZero(
                SAA_in          const FrameHeader&                   header,
                SAA_in          const std::string&                   frameName
                )
            {
                if( header.streamId != Globals::STREAM_ID_CONNECTION )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "a " + frameName + " frame applies to the connection as a whole, and it "
                            "carries a non-zero stream identifier"
                        );
                }
            }

            /**
             * @brief The padding rule shared by DATA, HEADERS and PUSH_PROMISE
             *
             * 'fixedFields' is what the frame carries ahead of the variable part once the Pad
             * Length octet has been accounted for - the priority fields of a HEADERS frame, the
             * promised stream id of a PUSH_PROMISE, nothing for DATA
             *
             * RFC 9113 6.1: "If the length of the padding is the length of the frame payload or
             * greater, the recipient MUST treat this as a connection error of type
             * PROTOCOL_ERROR", and 6.2 and 6.6 say the same of padding which exceeds the size
             * remaining for the field block fragment
             */

            static std::size_t checkPaddingAndGetVariableSize(
                SAA_in          const FrameView&                     frame,
                SAA_in          const std::uint32_t                  fixedFields,
                SAA_in          const std::string&                   frameName
                )
            {
                const std::uint32_t length = frame.header.length;
                const bool isPadded = 0U != ( frame.header.flags & Globals::FRAME_FLAG_PADDED );

                const std::uint32_t prefix =
                    fixedFields + ( isPadded ? PAD_LENGTH_FIELD_SIZE : 0U );

                /*
                 * Checked again here rather than trusted from validateFrameHeader, so that a
                 * FrameView assembled by hand - which is what a test peer does - can never make
                 * this read past the end of the payload
                 */

                checkMinimumLength( frame.header, prefix, frameName );

                if( ! isPadded )
                {
                    return length - fixedFields;
                }

                const std::uint32_t padLength = frame.payload.value()[ 0 ];

                if( padLength > length - prefix )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "the padding of a " + frameName + " frame is longer than the payload "
                            "which is left to hold it"
                        );
                }

                return length - prefix - padLength;
            }

            /**
             * @brief A view whose payload does not match its header cannot be read safely
             *
             * Always a programming error rather than a protocol error - the reader below never
             * produces one - but it is checked on every entry point, because the alternative on
             * a hand built view is reading past the end of a buffer
             */

            static void checkViewIsConsistent( SAA_in const FrameView& frame )
            {
                BL_CHK(
                    false,
                    frame.payloadSize == frame.header.length,
                    BL_MSG()
                        << "An HTTP/2 frame view's payload size disagrees with the length in "
                        << "its header"
                    );

                BL_CHK(
                    false,
                    frame.header.length == 0U || frame.payload.value() != nullptr,
                    BL_MSG()
                        << "An HTTP/2 frame view of non-zero length carries no payload"
                    );
            }

            static std::uint32_t checkedLength( SAA_in const std::size_t length )
            {
                BL_CHK(
                    false,
                    length <= Globals::MAX_FRAME_SIZE_UPPER_BOUND,
                    BL_MSG()
                        << "An HTTP/2 frame payload of "
                        << length
                        << " octets does not fit the 24 bit Length field"
                    );

                return static_cast< std::uint32_t >( length );
            }

            static void checkFrameType(
                SAA_in          const FrameView&                     frame,
                SAA_in          const std::uint32_t                  expected
                )
            {
                BL_CHK(
                    false,
                    frame.header.type == expected,
                    BL_MSG()
                        << "An HTTP/2 frame payload accessor was called for the wrong frame type"
                    );

                if( frame.streamErrorCode != Globals::ERROR_CODE_NO_ERROR )
                {
                    throwStreamError(
                        frame.streamErrorCode,
                        frame.header.streamId,
                        "the payload of a frame which did not pass validation was asked for"
                        );
                }
            }

            /**
             * @brief Everything a parse accessor must re-establish before it reads a field
             *
             * The bound used here is the absolute wire maximum of 4.2 and not what we advertise:
             * the advertised limit is the reader's to enforce as bytes arrive (see FrameReaderT),
             * and re-imposing it on a frame which was already accepted would reject frames on the
             * way back out of the reader when the limit had since been lowered
             */

            static void revalidate(
                SAA_in          const FrameView&                     frame,
                SAA_in          const std::uint32_t                  expectedType
                )
            {
                checkFrameType( frame, expectedType );

                const auto streamErrorCode =
                    validateFrameHeader( frame.header, Globals::MAX_FRAME_SIZE_UPPER_BOUND );

                if( streamErrorCode != Globals::ERROR_CODE_NO_ERROR )
                {
                    throwStreamError(
                        streamErrorCode,
                        frame.header.streamId,
                        "the frame is not of the size its type requires"
                        );
                }

                checkViewIsConsistent( frame );
            }

        public:

            /*************************************************************************************
             * Raising an RFC 9113 section 7 error
             *
             * Public because the incremental reader below raises errors of its own and because
             * every layer of the core should raise them identically - a second helper which
             * formats the same error differently is the drift Globals.h exists to prevent. The
             * error code travels on the exception as eh::errinfo_http2_error_code, which is what
             * the session engine puts into its GOAWAY or RST_STREAM
             *
             * Neither ever echoes a peer-controlled byte: the reason is a fixed string the
             * library wrote, because an exception message reaches logs
             */

            SAA_noreturn
            static void throwConnectionError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                )
            {
                BL_THROW(
                    Http2ProtocolException()
                        << eh::errinfo_http2_error_code( errorCode ),
                    BL_MSG()
                        << "HTTP/2 connection error "
                        << Globals::errorCodeToString( errorCode )
                        << " - "
                        << reason
                    );
            }

            SAA_noreturn
            static void throwStreamError(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::string&                   reason
                )
            {
                BL_THROW(
                    Http2StreamException()
                        << eh::errinfo_http2_error_code( errorCode )
                        << eh::errinfo_http2_stream_id( streamId ),
                    BL_MSG()
                        << "HTTP/2 stream error "
                        << Globals::errorCodeToString( errorCode )
                        << " - "
                        << reason
                    );
            }

            /*************************************************************************************
             * The nine octet header - RFC 9113 section 4.1
             */

            /**
             * @brief Parses the nine octet frame header; 'buffer' must hold FRAME_HEADER_SIZE bytes
             *
             * It cannot fail: every one of the 2^72 possible headers parses. What a header means,
             * and whether it is allowed, is validateFrameHeader below
             */

            static FrameHeader parseFrameHeader( SAA_in const std::uint8_t* const buffer ) NOEXCEPT
            {
                FrameHeader header;

                header.length = readUint24( buffer );
                header.type = buffer[ 3 ];
                header.flags = buffer[ 4 ];

                /*
                 * The high bit is the reserved bit, which is ignored on receipt (4.1)
                 */

                header.streamId = readUint32( buffer + 4 + 1 ) & Globals::MAX_STREAM_ID;

                return header;
            }

            static void serializeFrameHeader(
                SAA_in          const FrameHeader&                   header,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                BL_CHK(
                    false,
                    header.length <= Globals::MAX_FRAME_SIZE_UPPER_BOUND,
                    BL_MSG()
                        << "An HTTP/2 frame payload of "
                        << header.length.value()
                        << " octets does not fit the 24 bit Length field"
                    );

                BL_CHK(
                    false,
                    header.streamId <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "An HTTP/2 stream identifier does not fit 31 bits"
                    );

                appendUint24( out, header.length );
                appendUint8( out, header.type );
                appendUint8( out, header.flags );

                /*
                 * The reserved bit is left unset when sending (4.1)
                 */

                appendUint32( out, header.streamId );
            }

            /**
             * @brief Whether the type is one of the ten RFC 9113 section 6 defines
             *
             * An unknown type is ignored on receipt (4.1), so this is the question a consumer
             * asks before it decides to ignore a frame - it is not a validity check and a parser
             * must never reject a type merely because it is missing from the enumeration
             */

            static bool isKnownFrameType( SAA_in const std::uint8_t type ) NOEXCEPT
            {
                return type <= Globals::FRAME_TYPE_CONTINUATION;
            }

            /*************************************************************************************
             * Validation
             */

            /**
             * @brief Everything the nine octet header can be judged on by itself
             *
             * Throws Http2ProtocolException for a connection error. Returns a non-zero RFC 9113
             * section 7 code for a STREAM error, which the caller answers with RST_STREAM after
             * it has consumed the payload, and NO_ERROR when the header is good
             *
             * 'maxFrameSize' is our own advertised SETTINGS_MAX_FRAME_SIZE. A frame larger than
             * it is a connection error raised here, before the payload is buffered anywhere
             * (design 4.1) - a peer which ignores the limit does not get to make us allocate
             */

            static std::uint32_t validateFrameHeader(
                SAA_in          const FrameHeader&                   header,
                SAA_in          const std::uint32_t                  maxFrameSize
                )
            {
                if( header.length > maxFrameSize )
                {
                    throwConnectionError(
                        Globals::ERROR_CODE_FRAME_SIZE_ERROR,
                        "the peer has sent a frame larger than the maximum frame size we "
                            "advertised"
                        );
                }

                switch( header.type.value() )
                {
                    case Globals::FRAME_TYPE_DATA:
                        checkStreamIdIsNotZero( header, "DATA" );

                        if( 0U != ( header.flags & Globals::FRAME_FLAG_PADDED ) )
                        {
                            /*
                             * Too small to hold the Pad Length octet its own flags promise. DATA
                             * carries no field block and its stream identifier is not zero, so
                             * 4.2 makes this a stream error rather than a connection error
                             */

                            if( header.length < PAD_LENGTH_FIELD_SIZE )
                            {
                                return Globals::ERROR_CODE_FRAME_SIZE_ERROR;
                            }
                        }
                        break;

                    case Globals::FRAME_TYPE_HEADERS:
                        checkStreamIdIsNotZero( header, "HEADERS" );

                        checkMinimumLength(
                            header,
                            ( 0U != ( header.flags & Globals::FRAME_FLAG_PADDED ) ?
                                PAD_LENGTH_FIELD_SIZE : 0U ) +
                            ( 0U != ( header.flags & Globals::FRAME_FLAG_PRIORITY ) ?
                                PRIORITY_FIELDS_SIZE : 0U ),
                            "HEADERS"
                            );
                        break;

                    case Globals::FRAME_TYPE_PRIORITY:
                        checkStreamIdIsNotZero( header, "PRIORITY" );

                        /*
                         * RFC 9113 6.3 - "A PRIORITY frame with a length other than 5 octets MUST
                         * be treated as a stream error of type FRAME_SIZE_ERROR". A stream error,
                         * uniquely among the length rules of section 6, which is the whole reason
                         * this function returns a code instead of only throwing
                         */

                        if( header.length != PRIORITY_FIELDS_SIZE )
                        {
                            return Globals::ERROR_CODE_FRAME_SIZE_ERROR;
                        }
                        break;

                    case Globals::FRAME_TYPE_RST_STREAM:
                        checkStreamIdIsNotZero( header, "RST_STREAM" );
                        checkExactLength( header, RST_STREAM_PAYLOAD_SIZE, "RST_STREAM" );
                        break;

                    case Globals::FRAME_TYPE_SETTINGS:
                        checkStreamIdIsZero( header, "SETTINGS" );

                        if( 0U != ( header.flags & Globals::FRAME_FLAG_ACK ) )
                        {
                            checkExactLength( header, 0U, "SETTINGS with the ACK flag set" );
                        }

                        if( 0U != ( header.length % SETTINGS_ENTRY_SIZE ) )
                        {
                            throwConnectionError(
                                Globals::ERROR_CODE_FRAME_SIZE_ERROR,
                                "the payload of a SETTINGS frame must be a multiple of six octets"
                                );
                        }
                        break;

                    case Globals::FRAME_TYPE_PUSH_PROMISE:
                        checkStreamIdIsNotZero( header, "PUSH_PROMISE" );

                        checkMinimumLength(
                            header,
                            PUSH_PROMISE_MIN_PAYLOAD_SIZE +
                            ( 0U != ( header.flags & Globals::FRAME_FLAG_PADDED ) ?
                                PAD_LENGTH_FIELD_SIZE : 0U ),
                            "PUSH_PROMISE"
                            );
                        break;

                    case Globals::FRAME_TYPE_PING:
                        checkStreamIdIsZero( header, "PING" );
                        checkExactLength( header, PING_PAYLOAD_SIZE, "PING" );
                        break;

                    case Globals::FRAME_TYPE_GOAWAY:
                        checkStreamIdIsZero( header, "GOAWAY" );
                        checkMinimumLength( header, GOAWAY_MIN_PAYLOAD_SIZE, "GOAWAY" );
                        break;

                    case Globals::FRAME_TYPE_WINDOW_UPDATE:

                        /*
                         * The only type which is legal both on a stream and on the connection, so
                         * there is no stream identifier rule to apply. An increment of zero is a
                         * flow control matter and belongs to FlowControlWindow (4.4), not here
                         */

                        checkExactLength( header, WINDOW_UPDATE_PAYLOAD_SIZE, "WINDOW_UPDATE" );
                        break;

                    case Globals::FRAME_TYPE_CONTINUATION:
                        checkStreamIdIsNotZero( header, "CONTINUATION" );
                        break;

                    default:

                        /*
                         * An unknown type. 4.1 requires that it be ignored, so there is nothing
                         * to check beyond the size limit above - in particular its stream
                         * identifier means nothing to us and must not be judged
                         */

                        break;
                }

                return Globals::ERROR_CODE_NO_ERROR;
            }

            /**
             * @brief Everything which needs the payload octets - which is the padding, and only
             * the padding
             *
             * Throws Http2ProtocolException; a padding error is always a connection error. It is
             * called once the whole payload has arrived, and it is safe on any view whose size
             * matches its header, including one a test built by hand
             */

            static void validateFramePayload( SAA_in const FrameView& frame )
            {
                checkViewIsConsistent( frame );

                switch( frame.header.type.value() )
                {
                    case Globals::FRAME_TYPE_DATA:
                        checkPaddingAndGetVariableSize( frame, 0U, "DATA" );
                        break;

                    case Globals::FRAME_TYPE_HEADERS:
                        checkPaddingAndGetVariableSize(
                            frame,
                            ( 0U != ( frame.header.flags & Globals::FRAME_FLAG_PRIORITY ) ?
                                PRIORITY_FIELDS_SIZE : 0U ),
                            "HEADERS"
                            );
                        break;

                    case Globals::FRAME_TYPE_PUSH_PROMISE:
                        checkPaddingAndGetVariableSize(
                            frame,
                            PUSH_PROMISE_MIN_PAYLOAD_SIZE,
                            "PUSH_PROMISE"
                            );
                        break;

                    default:
                        break;
                }
            }

            /*************************************************************************************
             * Typed payload accessors
             *
             * Each one re-validates before it reads a field, so that it is safe on a view which
             * did not come from FrameReaderT, and each one throws if it is called for the wrong
             * type. None of them copies: what they return points into the view they were given
             * and lives exactly as long as it does
             */

            static DataPayload parseData( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_DATA );

                const auto size = checkPaddingAndGetVariableSize( frame, 0U, "DATA" );

                const std::uint8_t* const payload = frame.payload;
                const bool isPadded = 0U != ( frame.header.flags & Globals::FRAME_FLAG_PADDED );

                DataPayload result;

                result.isPadded = isPadded;
                result.padLength = isPadded ? payload[ 0 ] : 0U;
                result.endStream = 0U != ( frame.header.flags & Globals::FRAME_FLAG_END_STREAM );
                result.size = size;
                result.data =
                    size != 0U ? payload + ( isPadded ? PAD_LENGTH_FIELD_SIZE : 0U ) : nullptr;

                return result;
            }

            static HeadersPayload parseHeaders( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_HEADERS );

                const bool hasPriority =
                    0U != ( frame.header.flags & Globals::FRAME_FLAG_PRIORITY );

                const auto size = checkPaddingAndGetVariableSize(
                    frame,
                    hasPriority ? PRIORITY_FIELDS_SIZE : 0U,
                    "HEADERS"
                    );

                const std::uint8_t* const payload = frame.payload;
                const bool isPadded = 0U != ( frame.header.flags & Globals::FRAME_FLAG_PADDED );

                std::size_t offset = isPadded ? PAD_LENGTH_FIELD_SIZE : 0U;

                HeadersPayload result;

                result.isPadded = isPadded;
                result.padLength = isPadded ? payload[ 0 ] : 0U;
                result.endStream = 0U != ( frame.header.flags & Globals::FRAME_FLAG_END_STREAM );
                result.endHeaders = 0U != ( frame.header.flags & Globals::FRAME_FLAG_END_HEADERS );

                if( hasPriority )
                {
                    const auto word = readUint32( payload + offset );

                    result.priority.isSet = true;
                    result.priority.exclusive = 0U != ( word & 0x80000000U );
                    result.priority.streamDependency = word & Globals::MAX_STREAM_ID;
                    result.priority.weight = payload[ offset + 4U ];

                    offset += PRIORITY_FIELDS_SIZE;
                }

                result.fieldBlockSize = size;
                result.fieldBlock = size != 0U ? payload + offset : nullptr;

                return result;
            }

            /**
             * @brief Parses a PRIORITY frame into the profile's own Http2PriorityFrame
             *
             * 'weight' is the octet as it appears on the wire; RFC 9113 6.3 has the actual weight
             * one higher, and this does not add the one - the profile records what a browser puts
             * on the wire and this must round-trip it exactly
             */

            static Http2PriorityFrame parsePriority( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_PRIORITY );

                const std::uint8_t* const payload = frame.payload;
                const auto word = readUint32( payload );

                Http2PriorityFrame result;

                result.streamId = frame.header.streamId;
                result.exclusive = 0U != ( word & 0x80000000U );
                result.streamDependency = word & Globals::MAX_STREAM_ID;
                result.weight = payload[ 4 ];

                return result;
            }

            static std::uint32_t parseRstStream( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_RST_STREAM );

                return readUint32( frame.payload );
            }

            /**
             * @brief The settings a SETTINGS frame carries, in the order they were sent
             *
             * A vector rather than a map, and Http2Setting rather than a type of its own, for the
             * reason Http2Profile.h gives: the order is part of a fingerprint, and an identifier
             * the library does not interpret must survive being read (6.5.2)
             *
             * Duplicates are kept. RFC 9113 6.5.3 requires that the values be processed in the
             * order they appear, so removing a duplicate here would change what the peer asked
             * for. An ACK carries no settings and yields an empty vector
             */

            static std::vector< Http2Setting > parseSettings( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_SETTINGS );

                const std::uint8_t* const payload = frame.payload;
                const std::size_t count = frame.header.length / SETTINGS_ENTRY_SIZE;

                std::vector< Http2Setting > result;
                result.reserve( count );

                for( std::size_t i = 0U; i < count; ++i )
                {
                    const std::uint8_t* const entry = payload + i * SETTINGS_ENTRY_SIZE;

                    Http2Setting setting;

                    setting.id = readUint16( entry );
                    setting.value = readUint32( entry + 2 );

                    result.push_back( setting );
                }

                return result;
            }

            static PushPromisePayload parsePushPromise( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_PUSH_PROMISE );

                const auto size = checkPaddingAndGetVariableSize(
                    frame,
                    PUSH_PROMISE_MIN_PAYLOAD_SIZE,
                    "PUSH_PROMISE"
                    );

                const std::uint8_t* const payload = frame.payload;
                const bool isPadded = 0U != ( frame.header.flags & Globals::FRAME_FLAG_PADDED );

                const std::size_t offset = isPadded ? PAD_LENGTH_FIELD_SIZE : 0U;

                PushPromisePayload result;

                result.isPadded = isPadded;
                result.padLength = isPadded ? payload[ 0 ] : 0U;
                result.endHeaders = 0U != ( frame.header.flags & Globals::FRAME_FLAG_END_HEADERS );

                /*
                 * The high bit of the promised stream id is reserved and ignored, as it is in the
                 * frame header (6.6)
                 */

                result.promisedStreamId = readUint32( payload + offset ) & Globals::MAX_STREAM_ID;

                result.fieldBlockSize = size;
                result.fieldBlock =
                    size != 0U ?
                        payload + offset + PUSH_PROMISE_MIN_PAYLOAD_SIZE : nullptr;

                return result;
            }

            /**
             * @brief The eight opaque octets of a PING frame, borrowed from the view
             *
             * Opaque is the point: they are echoed back unexamined in the ACK (6.7), so they are
             * never interpreted and never logged
             */

            static const std::uint8_t* parsePing( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_PING );

                return frame.payload;
            }

            static GoAwayPayload parseGoAway( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_GOAWAY );

                const std::uint8_t* const payload = frame.payload;
                const std::size_t debugDataSize = frame.header.length - GOAWAY_MIN_PAYLOAD_SIZE;

                GoAwayPayload result;

                result.lastStreamId = readUint32( payload ) & Globals::MAX_STREAM_ID;
                result.errorCode = readUint32( payload + 4 );
                result.debugDataSize = debugDataSize;
                result.debugData =
                    debugDataSize != 0U ? payload + GOAWAY_MIN_PAYLOAD_SIZE : nullptr;

                return result;
            }

            /**
             * @brief The window size increment of a WINDOW_UPDATE frame
             *
             * An increment of zero parses: it is a PROTOCOL_ERROR, on the stream or on the
             * connection according to which the frame arrived on (6.9), and that judgement is
             * FlowControlWindow's (4.4) because it is the half of the rule which needs the window
             */

            static std::uint32_t parseWindowUpdate( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_WINDOW_UPDATE );

                return readUint32( frame.payload ) & Globals::MAX_STREAM_ID;
            }

            static ContinuationPayload parseContinuation( SAA_in const FrameView& frame )
            {
                revalidate( frame, Globals::FRAME_TYPE_CONTINUATION );

                const std::size_t size = frame.header.length;

                ContinuationPayload result;

                result.endHeaders = 0U != ( frame.header.flags & Globals::FRAME_FLAG_END_HEADERS );
                result.fieldBlockSize = size;
                result.fieldBlock = size != 0U ? frame.payload.value() : nullptr;

                return result;
            }

            /*************************************************************************************
             * Serialization
             *
             * Every one appends a complete frame - header and payload - to 'out', which is never
             * cleared, so a caller composes several frames into one buffer by calling them in
             * turn. A request which cannot be expressed on the wire at all is a programming
             * error and an UnexpectedException, not a protocol error
             */

            static void serializeData(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size,
                SAA_in          const bool                           endStream,
                SAA_in          const FramePadding&                  padding,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_DATA;
                header.streamId = streamId;
                header.flags = static_cast< std::uint8_t >(
                    ( endStream ? Globals::FRAME_FLAG_END_STREAM : 0U ) |
                    ( padding.isSet ? Globals::FRAME_FLAG_PADDED : 0U )
                    );

                header.length = checkedLength(
                    size +
                    ( padding.isSet ? PAD_LENGTH_FIELD_SIZE + padding.padLength : 0U )
                    );

                serializeFrameHeader( header, out );

                if( padding.isSet )
                {
                    appendUint8( out, padding.padLength );
                }

                if( size != 0U )
                {
                    out.insert( out.end(), data, data + size );
                }

                appendPadding( out, padding );
            }

            static void serializeHeaders(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in_opt      const std::uint8_t*                  fieldBlock,
                SAA_in          const std::size_t                    size,
                SAA_in          const bool                           endStream,
                SAA_in          const bool                           endHeaders,
                SAA_in          const Http2HeadersPriority&          priority,
                SAA_in          const FramePadding&                  padding,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_HEADERS;
                header.streamId = streamId;
                header.flags = static_cast< std::uint8_t >(
                    ( endStream ? Globals::FRAME_FLAG_END_STREAM : 0U ) |
                    ( endHeaders ? Globals::FRAME_FLAG_END_HEADERS : 0U ) |
                    ( padding.isSet ? Globals::FRAME_FLAG_PADDED : 0U ) |
                    ( priority.isSet ? Globals::FRAME_FLAG_PRIORITY : 0U )
                    );

                header.length = checkedLength(
                    size +
                    ( padding.isSet ? PAD_LENGTH_FIELD_SIZE + padding.padLength : 0U ) +
                    ( priority.isSet ? PRIORITY_FIELDS_SIZE : 0U )
                    );

                serializeFrameHeader( header, out );

                if( padding.isSet )
                {
                    appendUint8( out, padding.padLength );
                }

                if( priority.isSet )
                {
                    appendPriorityFields(
                        out,
                        priority.streamDependency,
                        priority.weight,
                        priority.exclusive
                        );
                }

                if( size != 0U )
                {
                    out.insert( out.end(), fieldBlock, fieldBlock + size );
                }

                appendPadding( out, padding );
            }

            static void serializePriority(
                SAA_in          const Http2PriorityFrame&            priority,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_PRIORITY;
                header.streamId = priority.streamId;
                header.length = PRIORITY_FIELDS_SIZE;

                serializeFrameHeader( header, out );

                appendPriorityFields(
                    out,
                    priority.streamDependency,
                    priority.weight,
                    priority.exclusive
                    );
            }

            static void serializeRstStream(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  errorCode,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_RST_STREAM;
                header.streamId = streamId;
                header.length = RST_STREAM_PAYLOAD_SIZE;

                serializeFrameHeader( header, out );

                appendUint32( out, errorCode );
            }

            static void serializeSettings(
                SAA_in          const std::vector< Http2Setting >&    settings,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_SETTINGS;
                header.length = checkedLength( settings.size() * SETTINGS_ENTRY_SIZE );

                serializeFrameHeader( header, out );

                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    appendUint16( out, settings[ i ].id );
                    appendUint32( out, settings[ i ].value );
                }
            }

            static void serializeSettingsAck( SAA_inout std::vector< std::uint8_t >& out )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_SETTINGS;
                header.flags = Globals::FRAME_FLAG_ACK;

                serializeFrameHeader( header, out );
            }

            /**
             * @brief 'opaqueData' must point at PING_PAYLOAD_SIZE octets
             */

            static void serializePing(
                SAA_in          const std::uint8_t*                  opaqueData,
                SAA_in          const bool                           isAck,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_PING;
                header.flags = isAck ? Globals::FRAME_FLAG_ACK : Globals::FRAME_FLAG_NONE;
                header.length = PING_PAYLOAD_SIZE;

                serializeFrameHeader( header, out );

                out.insert( out.end(), opaqueData, opaqueData + PING_PAYLOAD_SIZE );
            }

            static void serializeGoAway(
                SAA_in          const std::uint32_t                  lastStreamId,
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in_opt      const std::uint8_t*                  debugData,
                SAA_in          const std::size_t                    debugDataSize,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                BL_CHK(
                    false,
                    lastStreamId <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "A GOAWAY last stream identifier does not fit 31 bits"
                    );

                FrameHeader header;

                header.type = Globals::FRAME_TYPE_GOAWAY;
                header.length = checkedLength( GOAWAY_MIN_PAYLOAD_SIZE + debugDataSize );

                serializeFrameHeader( header, out );

                appendUint32( out, lastStreamId );
                appendUint32( out, errorCode );

                if( debugDataSize != 0U )
                {
                    out.insert( out.end(), debugData, debugData + debugDataSize );
                }
            }

            static void serializeWindowUpdate(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  increment,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                BL_CHK(
                    false,
                    increment <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "A WINDOW_UPDATE increment does not fit 31 bits"
                    );

                FrameHeader header;

                header.type = Globals::FRAME_TYPE_WINDOW_UPDATE;
                header.streamId = streamId;
                header.length = WINDOW_UPDATE_PAYLOAD_SIZE;

                serializeFrameHeader( header, out );

                appendUint32( out, increment );
            }

            static void serializeContinuation(
                SAA_in_opt      const std::uint8_t*                  fieldBlock,
                SAA_in          const std::size_t                    size,
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const bool                           endHeaders,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                FrameHeader header;

                header.type = Globals::FRAME_TYPE_CONTINUATION;
                header.streamId = streamId;
                header.flags = endHeaders ?
                    Globals::FRAME_FLAG_END_HEADERS : Globals::FRAME_FLAG_NONE;
                header.length = checkedLength( size );

                serializeFrameHeader( header, out );

                if( size != 0U )
                {
                    out.insert( out.end(), fieldBlock, fieldBlock + size );
                }
            }

            /**
             * @brief Serialized for the role neutrality of D8 and never sent by the client
             *
             * The client always advertises SETTINGS_ENABLE_PUSH = 0 (D11), so it has nothing to
             * promise. The test peer of design 8.2 does, and a codec which could not write the
             * frame would make "a received PUSH_PROMISE is a connection error" untestable
             */

            static void serializePushPromise(
                SAA_in          const std::uint32_t                  streamId,
                SAA_in          const std::uint32_t                  promisedStreamId,
                SAA_in_opt      const std::uint8_t*                  fieldBlock,
                SAA_in          const std::size_t                    size,
                SAA_in          const bool                           endHeaders,
                SAA_in          const FramePadding&                  padding,
                SAA_inout       std::vector< std::uint8_t >&         out
                )
            {
                BL_CHK(
                    false,
                    promisedStreamId <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "A promised stream identifier does not fit 31 bits"
                    );

                FrameHeader header;

                header.type = Globals::FRAME_TYPE_PUSH_PROMISE;
                header.streamId = streamId;
                header.flags = static_cast< std::uint8_t >(
                    ( endHeaders ? Globals::FRAME_FLAG_END_HEADERS : 0U ) |
                    ( padding.isSet ? Globals::FRAME_FLAG_PADDED : 0U )
                    );

                header.length = checkedLength(
                    PUSH_PROMISE_MIN_PAYLOAD_SIZE + size +
                    ( padding.isSet ? PAD_LENGTH_FIELD_SIZE + padding.padLength : 0U )
                    );

                serializeFrameHeader( header, out );

                if( padding.isSet )
                {
                    appendUint8( out, padding.padLength );
                }

                appendUint32( out, promisedStreamId );

                if( size != 0U )
                {
                    out.insert( out.end(), fieldBlock, fieldBlock + size );
                }

                appendPadding( out, padding );
            }

        private:

            static void appendPadding(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const FramePadding&                  padding
                )
            {
                if( padding.isSet && padding.padLength != 0U )
                {
                    /*
                     * "Padding octets MUST be set to zero when sending" (6.1)
                     */

                    out.insert(
                        out.end(),
                        padding.padLength.value(),
                        static_cast< std::uint8_t >( 0 )
                        );
                }
            }

            static void appendPriorityFields(
                SAA_inout       std::vector< std::uint8_t >&         out,
                SAA_in          const std::uint32_t                  streamDependency,
                SAA_in          const std::uint8_t                   weight,
                SAA_in          const bool                           exclusive
                )
            {
                BL_CHK(
                    false,
                    streamDependency <= Globals::MAX_STREAM_ID,
                    BL_MSG()
                        << "A stream dependency does not fit 31 bits"
                    );

                appendUint32(
                    out,
                    streamDependency | ( exclusive ? 0x80000000U : 0U )
                    );

                appendUint8( out, weight );
            }
        };

        typedef FrameCodecT<> FrameCodec;

        /**
         * @brief class FrameReaderT - one frame at a time, out of bytes which arrive any way at all
         *
         * A TCP read returns whatever the network gave it. The nine octet header can arrive as
         * nine separate reads, a payload can be split anywhere, and one read can carry the tail
         * of one frame, several whole frames and the first octet of another. This is the only
         * class in the codec which remembers anything, and remembering it is the whole job
         *
         * The contract is a pull, exactly as design 4.5 asks of the session engine: feed( ) never
         * calls back out, so nothing can re-enter the reader mid-parse. A caller drains it
         *
         *     while( offset < size )
         *     {
         *         offset += reader.feed( data + offset, size - offset );
         *
         *         if( reader.hasFrame() )
         *         {
         *             handle( reader.frame() );
         *             reader.consumeFrame();
         *         }
         *     }
         *
         * feed( ) stops at the end of a frame and returns how much of the block it took, so the
         * loop is the same whether the block held a tenth of a frame or ten of them
         *
         * LIFETIME. The payload of the view frame( ) returns points into this object's buffer.
         * It is valid until consumeFrame( ) or the next feed( ), and the frame is copied out of
         * it or consumed before then. The view itself is a value and may be copied freely
         *
         * ERRORS. A connection error is thrown - Http2ProtocolException carrying the RFC 9113
         * section 7 code - and leaves the reader unusable, because after one there is no state in
         * which parsing could continue: the caller sends a GOAWAY and closes. A stream error is
         * NOT thrown. The frame is consumed in full so that the connection stays in sync, and it
         * is surfaced with FrameView::streamErrorCode set, for the caller to answer with a
         * RST_STREAM and carry on
         *
         * WHAT IT IS NOT. It does not interpret a single payload, does not know which streams
         * exist and does not know what a setting means. The one piece of protocol state it does
         * keep is whether a header block is open, because RFC 9113 6.10 makes that a rule about
         * the frame SEQUENCE rather than about any one frame, and because the design puts it here
         * (4.1): an unknown frame type is ignored, except inside a header block, where it is a
         * connection error
         */

        template
        <
            typename E = void
        >
        class FrameReaderT
        {
        private:

            std::uint8_t                                    m_headerBytes[ Globals::FRAME_HEADER_SIZE ];
            std::size_t                                     m_headerFilled;

            std::vector< std::uint8_t >                     m_payload;
            std::size_t                                     m_payloadFilled;

            FrameHeader                                     m_header;

            cpp::ScalarTypeIniter< bool >                   m_hasFrame;
            cpp::ScalarTypeIniter< bool >                   m_failed;
            cpp::ScalarTypeIniter< std::uint32_t >          m_streamErrorCode;
            cpp::ScalarTypeIniter< std::uint32_t >          m_maxFrameSize;

            /*
             * The stream whose header block is open, or STREAM_ID_CONNECTION - which cannot be a
             * real stream (4.1) - when none is
             */

            cpp::ScalarTypeIniter< std::uint32_t >          m_headerBlockStreamId;

            /**
             * @brief The header has arrived in full - decide everything it can decide by itself
             *
             * ORDER MATTERS AND IS OBSERVABLE, because the two checks below report different
             * error codes for a frame which breaks both. Sequence first: a frame which arrives
             * where a CONTINUATION was required is wrong whatever its size, and "a CONTINUATION
             * was expected" is the useful diagnosis. Nothing has been buffered either way - the
             * resize below is what allocates, and both of these throw before it, which is the
             * promise design 4.1 makes about an oversize frame
             */

            void beginFrame()
            {
                m_header = FrameCodec::parseFrameHeader( m_headerBytes );

                checkHeaderBlockContinuity();

                m_streamErrorCode = FrameCodec::validateFrameHeader( m_header, m_maxFrameSize );

                updateHeaderBlockState();

                m_payload.resize( m_header.length );
                m_payloadFilled = 0U;
            }

            void checkHeaderBlockContinuity()
            {
                if( m_headerBlockStreamId != Globals::STREAM_ID_CONNECTION )
                {
                    if(
                        m_header.type != Globals::FRAME_TYPE_CONTINUATION ||
                        m_header.streamId != m_headerBlockStreamId
                        )
                    {
                        FrameCodec::throwConnectionError(
                            Globals::ERROR_CODE_PROTOCOL_ERROR,
                            "a header block is open and what followed it is not a CONTINUATION "
                                "frame on the same stream"
                            );
                    }
                }
                else if( m_header.type == Globals::FRAME_TYPE_CONTINUATION )
                {
                    FrameCodec::throwConnectionError(
                        Globals::ERROR_CODE_PROTOCOL_ERROR,
                        "a CONTINUATION frame arrived while no header block was open"
                        );
                }
            }

            void updateHeaderBlockState() NOEXCEPT
            {
                const bool endHeaders =
                    0U != ( m_header.flags & Globals::FRAME_FLAG_END_HEADERS );

                switch( m_header.type.value() )
                {
                    case Globals::FRAME_TYPE_HEADERS:
                    case Globals::FRAME_TYPE_PUSH_PROMISE:
                        m_headerBlockStreamId = endHeaders ?
                            static_cast< std::uint32_t >( Globals::STREAM_ID_CONNECTION ) :
                            m_header.streamId.value();
                        break;

                    case Globals::FRAME_TYPE_CONTINUATION:
                        if( endHeaders )
                        {
                            m_headerBlockStreamId = Globals::STREAM_ID_CONNECTION;
                        }
                        break;

                    default:
                        break;
                }
            }

            void completeFrame()
            {
                if( m_streamErrorCode == Globals::ERROR_CODE_NO_ERROR )
                {
                    FrameCodec::validateFramePayload( currentView() );
                }

                m_hasFrame = true;
            }

            FrameView currentView() const NOEXCEPT
            {
                FrameView view;

                view.header = m_header;
                view.payloadSize = m_header.length.value();
                view.payload = m_header.length != 0U ? m_payload.data() : nullptr;
                view.streamErrorCode = m_streamErrorCode;

                return view;
            }

            std::size_t feedImpl(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                std::size_t consumed = 0U;

                if( m_headerFilled < Globals::FRAME_HEADER_SIZE )
                {
                    const std::size_t take = std::min< std::size_t >(
                        Globals::FRAME_HEADER_SIZE - m_headerFilled,
                        size
                        );

                    if( take != 0U )
                    {
                        std::memcpy( m_headerBytes + m_headerFilled, data, take );

                        m_headerFilled += take;
                        consumed += take;
                    }

                    if( m_headerFilled < Globals::FRAME_HEADER_SIZE )
                    {
                        return consumed;
                    }

                    beginFrame();

                    if( m_header.length == 0U )
                    {
                        completeFrame();

                        return consumed;
                    }
                }

                const std::size_t take = std::min< std::size_t >(
                    m_header.length - m_payloadFilled,
                    size - consumed
                    );

                if( take != 0U )
                {
                    std::memcpy( &m_payload[ m_payloadFilled ], data + consumed, take );

                    m_payloadFilled += take;
                    consumed += take;
                }

                if( m_payloadFilled == m_header.length )
                {
                    completeFrame();
                }

                return consumed;
            }

        public:

            FrameReaderT()
                :
                m_headerFilled( 0U ),
                m_payloadFilled( 0U ),
                m_maxFrameSize( Globals::MAX_FRAME_SIZE_DEFAULT )
            {
                std::memset( m_headerBytes, 0, sizeof( m_headerBytes ) );
            }

            /**
             * @brief Our own advertised SETTINGS_MAX_FRAME_SIZE, which bounds what will be
             * buffered for a single frame
             */

            std::uint32_t maxFrameSize() const NOEXCEPT
            {
                return m_maxFrameSize;
            }

            void setMaxFrameSize( SAA_in const std::uint32_t maxFrameSize )
            {
                /*
                 * The range of 4.2 - our own value, so anything outside it is a programming
                 * error here rather than the PROTOCOL_ERROR it would be arriving from a peer
                 */

                BL_CHK(
                    false,
                    maxFrameSize >= Globals::MAX_FRAME_SIZE_DEFAULT &&
                        maxFrameSize <= Globals::MAX_FRAME_SIZE_UPPER_BOUND,
                    BL_MSG()
                        << "An HTTP/2 maximum frame size of "
                        << maxFrameSize
                        << " octets is outside the range RFC 9113 4.2 allows"
                    );

                m_maxFrameSize = maxFrameSize;
            }

            /**
             * @brief Hands the reader bytes; returns how many of them it took
             *
             * It takes at most up to the end of one frame, so a return which is less than 'size'
             * means either that a frame is now available or - only when the block ran out - that
             * more bytes are needed. Calling it again with a frame still available is a
             * programming error, since it would have to drop that frame to make progress
             */

            std::size_t feed(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                BL_CHK(
                    false,
                    ! m_failed,
                    BL_MSG()
                        << "An HTTP/2 frame reader was fed again after a connection error"
                    );

                BL_CHK(
                    false,
                    ! m_hasFrame,
                    BL_MSG()
                        << "An HTTP/2 frame reader was fed again before the frame it had "
                        << "already parsed was consumed"
                    );

                BL_CHK(
                    false,
                    size == 0U || data != nullptr,
                    BL_MSG()
                        << "An HTTP/2 frame reader was fed a non-empty block of no bytes"
                    );

                /*
                 * Anything which throws below is a connection error, and there is no recovering
                 * the frame boundaries after one - so the reader stays failed unless it is reset
                 */

                m_failed = true;

                const std::size_t consumed = feedImpl( data, size );

                m_failed = false;

                return consumed;
            }

            bool hasFrame() const NOEXCEPT
            {
                return m_hasFrame;
            }

            /**
             * @brief The frame which has arrived, as a view whose payload this object owns
             */

            FrameView frame() const
            {
                BL_CHK(
                    false,
                    m_hasFrame,
                    BL_MSG()
                        << "An HTTP/2 frame was asked for before one had been parsed"
                    );

                return currentView();
            }

            /**
             * @brief Discards the frame and makes the reader ready for the next one
             *
             * The payload buffer keeps its capacity: a connection reads frames of much the same
             * size for its whole life, so it settles after the first few
             */

            void consumeFrame()
            {
                BL_CHK(
                    false,
                    m_hasFrame,
                    BL_MSG()
                        << "An HTTP/2 frame was consumed before one had been parsed"
                    );

                m_hasFrame = false;
                m_headerFilled = 0U;
                m_payloadFilled = 0U;
                m_streamErrorCode = Globals::ERROR_CODE_NO_ERROR;
            }

            /**
             * @brief Whether a header block is open, and on which stream
             *
             * RFC 9113 6.10: while one is open the only frame which may arrive is a CONTINUATION
             * on the same stream, and the reader enforces that itself. These are here because the
             * session engine needs the same fact for the limits of 4.6 - the CONTINUATION count
             * and the compressed block size are both per block
             */

            bool isInHeaderBlock() const NOEXCEPT
            {
                return m_headerBlockStreamId != Globals::STREAM_ID_CONNECTION;
            }

            std::uint32_t headerBlockStreamId() const NOEXCEPT
            {
                return m_headerBlockStreamId;
            }

            /**
             * @brief Back to the state of a freshly constructed reader, keeping the frame size
             */

            void reset() NOEXCEPT
            {
                m_headerFilled = 0U;
                m_payloadFilled = 0U;
                m_payload.clear();
                m_header = FrameHeader();
                m_hasFrame = false;
                m_failed = false;
                m_streamErrorCode = Globals::ERROR_CODE_NO_ERROR;
                m_headerBlockStreamId = Globals::STREAM_ID_CONNECTION;
            }
        };

        typedef FrameReaderT<> FrameReader;

    } // http2

} // bl

#endif /* __BL_HTTP2_FRAMECODEC_H_ */
