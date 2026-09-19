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

#ifndef __BL_HTTP2_GLOBALS_H_
#define __BL_HTTP2_GLOBALS_H_

#include <baselib/core/BaseIncludes.h>

#include <iomanip>
#include <string>

namespace bl
{
    namespace http2
    {
        /**
         * @brief class GlobalsT - the HTTP/2 wire constants and the client's default limits
         *
         * Every value here is either defined by RFC 9113 or is a default of the design's limits
         * table (notes/plans/http2-design.md 4.6). Each group names the RFC section it comes
         * from, and no other header in the library may redefine any of them: the frame codec,
         * HPACK, flow control, the stream state machine and the session engine are written in
         * parallel, and a second copy of a frame type or an error code which disagrees by one
         * is the kind of defect which survives review and fails in an interoperability test
         *
         * Nothing here has any dependency beyond the core headers - this is a leaf, included by
         * sans-I/O code which knows nothing of Asio, OpenSSL, tasks or locks (design 2.1)
         */

        template
        <
            typename E = void
        >
        class GlobalsT
        {
            BL_DECLARE_STATIC( GlobalsT )

        public:

            /*************************************************************************************
             * Frame types - RFC 9113 section 6
             *
             * The Type field of the frame header is one octet (section 4.1). The ten types below
             * are all the RFC defines; an unknown type is ignored on receipt (section 4.1), so
             * this is deliberately not a closed set and a parser must not reject what is missing
             * from it
             */

            enum FrameType : unsigned int
            {
                FRAME_TYPE_DATA                     = 0x00U,        /* RFC 9113 6.1 */
                FRAME_TYPE_HEADERS                  = 0x01U,        /* RFC 9113 6.2 */
                FRAME_TYPE_PRIORITY                 = 0x02U,        /* RFC 9113 6.3 */
                FRAME_TYPE_RST_STREAM               = 0x03U,        /* RFC 9113 6.4 */
                FRAME_TYPE_SETTINGS                 = 0x04U,        /* RFC 9113 6.5 */
                FRAME_TYPE_PUSH_PROMISE             = 0x05U,        /* RFC 9113 6.6 */
                FRAME_TYPE_PING                     = 0x06U,        /* RFC 9113 6.7 */
                FRAME_TYPE_GOAWAY                   = 0x07U,        /* RFC 9113 6.8 */
                FRAME_TYPE_WINDOW_UPDATE            = 0x08U,        /* RFC 9113 6.9 */
                FRAME_TYPE_CONTINUATION             = 0x09U,        /* RFC 9113 6.10 */
            };

            /*************************************************************************************
             * Frame flags - RFC 9113 sections 6.1, 6.2, 6.5, 6.6, 6.7 and 6.10
             *
             * The Flags field is one octet, and its bits are assigned semantics specific to the
             * frame type (section 4.1) - which is why END_STREAM and ACK share the value 0x01 and
             * why there is one set of masks here rather than one per frame type. Which flags a
             * type defines is the codec's business; what a given bit is called is not negotiable
             *
             * A flag with no defined semantics for the frame type carrying it MUST be ignored on
             * receipt and MUST be left unset when sending (section 4.1)
             */

            enum FrameFlags : unsigned int
            {
                FRAME_FLAG_NONE                     = 0x00U,

                /*
                 * END_STREAM on DATA (6.1) and HEADERS (6.2); ACK on SETTINGS (6.5) and PING (6.7)
                 */

                FRAME_FLAG_END_STREAM               = 0x01U,
                FRAME_FLAG_ACK                      = 0x01U,

                /*
                 * END_HEADERS on HEADERS (6.2), PUSH_PROMISE (6.6) and CONTINUATION (6.10)
                 */

                FRAME_FLAG_END_HEADERS              = 0x04U,

                /*
                 * PADDED on DATA (6.1), HEADERS (6.2) and PUSH_PROMISE (6.6)
                 */

                FRAME_FLAG_PADDED                   = 0x08U,

                /*
                 * PRIORITY on HEADERS only (6.2)
                 */

                FRAME_FLAG_PRIORITY                 = 0x20U,
            };

            /*************************************************************************************
             * Settings identifiers - RFC 9113 section 6.5.2
             *
             * The identifier of a setting is two octets and its value four (section 6.5.1). An
             * unknown or unsupported identifier MUST be ignored (section 6.5.2), so a profile is
             * free to send ids the library does not itself interpret (design 6.4)
             *
             * The six below are all the RFC defines, and this header deliberately carries no
             * others. That profile table already shows two such ids - 8
             * (SETTINGS_ENABLE_CONNECT_PROTOCOL, RFC 8441) and 9
             * (SETTINGS_NO_RFC7540_PRIORITIES, RFC 9218), in the Safari shape, which S2.4 and
             * S3.1 will meet from real peers - and they travel in the profile's settings list,
             * passed through unread, so no codec may add a constant for them of its own
             */

            enum SettingId : unsigned int
            {
                SETTINGS_HEADER_TABLE_SIZE          = 0x01U,
                SETTINGS_ENABLE_PUSH                = 0x02U,
                SETTINGS_MAX_CONCURRENT_STREAMS     = 0x03U,
                SETTINGS_INITIAL_WINDOW_SIZE        = 0x04U,
                SETTINGS_MAX_FRAME_SIZE             = 0x05U,
                SETTINGS_MAX_HEADER_LIST_SIZE       = 0x06U,
            };

            /*************************************************************************************
             * Error codes - RFC 9113 section 7
             *
             * Thirty-two bit values carried by RST_STREAM and GOAWAY. They share one code space:
             * some apply only to a stream or only to the connection and have no defined meaning
             * in the other context
             *
             * An unknown or unsupported code MUST NOT trigger any special behavior and MAY be
             * treated as INTERNAL_ERROR - so this too is an open set, which is why errorCodeToString
             * below accepts any 32-bit value rather than this enumeration
             */

            enum ErrorCode : std::uint32_t
            {
                ERROR_CODE_NO_ERROR                 = 0x00U,
                ERROR_CODE_PROTOCOL_ERROR           = 0x01U,
                ERROR_CODE_INTERNAL_ERROR           = 0x02U,
                ERROR_CODE_FLOW_CONTROL_ERROR       = 0x03U,
                ERROR_CODE_SETTINGS_TIMEOUT         = 0x04U,
                ERROR_CODE_STREAM_CLOSED            = 0x05U,
                ERROR_CODE_FRAME_SIZE_ERROR         = 0x06U,
                ERROR_CODE_REFUSED_STREAM           = 0x07U,
                ERROR_CODE_CANCEL                   = 0x08U,
                ERROR_CODE_COMPRESSION_ERROR        = 0x09U,
                ERROR_CODE_CONNECT_ERROR            = 0x0aU,
                ERROR_CODE_ENHANCE_YOUR_CALM        = 0x0bU,
                ERROR_CODE_INADEQUATE_SECURITY      = 0x0cU,
                ERROR_CODE_HTTP_1_1_REQUIRED        = 0x0dU,
            };

            /*************************************************************************************
             * Protocol constants - RFC 9113 sections 4.1, 4.2, 5.1.1, 6.5.2 and 6.9
             */

            enum : std::uint32_t
            {
                /*
                 * All frames begin with a fixed nine-octet header, which is not counted in the
                 * Length field nor in any flow-control calculation (4.1, 4.2, 6.9.1)
                 */

                FRAME_HEADER_SIZE                   = 9U,

                /*
                 * A stream identifier is an unsigned 31-bit integer; zero is reserved for frames
                 * associated with the connection as a whole and cannot open a stream (4.1, 5.1.1)
                 */

                STREAM_ID_CONNECTION                = 0U,
                MAX_STREAM_ID                       = 2147483647U,      /* 2^31 - 1 */

                /*
                 * SETTINGS_MAX_FRAME_SIZE may take any value from 2^14 to 2^24-1 inclusive (4.2),
                 * and 2^14 is also its initial value (6.5.2). Anything outside that range is a
                 * connection error of type PROTOCOL_ERROR
                 */

                MAX_FRAME_SIZE_DEFAULT              = 16384U,           /* 2^14 */
                MAX_FRAME_SIZE_UPPER_BOUND          = 16777215U,        /* 2^24 - 1 */

                /*
                 * The two remaining settings whose initial value 6.5.2 states as a number. These
                 * are the RFC's initial values, which is what a peer's settings are worth until
                 * it sends a SETTINGS frame saying otherwise - they are not what this client
                 * sends: D11 has us always send SETTINGS_ENABLE_PUSH = 0, and what else we send
                 * comes from the active profile's ordered list (design 6.4)
                 *
                 * SETTINGS_MAX_CONCURRENT_STREAMS and SETTINGS_MAX_HEADER_LIST_SIZE have no
                 * numeric initial value in the RFC - "initially, there is no limit to this value"
                 * and "the initial value of this setting is unlimited" - so none is invented here
                 */

                HEADER_TABLE_SIZE_DEFAULT           = 4096U,
                ENABLE_PUSH_DEFAULT                 = 1U,
            };

            enum : std::int32_t
            {
                /*
                 * Flow-control windows are signed: a change to SETTINGS_INITIAL_WINDOW_SIZE
                 * adjusts every open stream and may legally drive a window negative (6.9.2),
                 * which is why these two are not in the unsigned group above
                 *
                 * A new stream and the connection both start at 65,535 octets (6.9.2), which is
                 * also the initial value of SETTINGS_INITIAL_WINDOW_SIZE (6.5.2). A sender MUST
                 * NOT allow a window to exceed 2^31-1; a WINDOW_UPDATE which would is a
                 * FLOW_CONTROL_ERROR (6.9.1)
                 */

                INITIAL_WINDOW_SIZE_DEFAULT         = 65535,            /* 2^16 - 1 */
                MAX_FLOW_CONTROL_WINDOW_SIZE        = 2147483647,       /* 2^31 - 1 */
            };

            /*************************************************************************************
             * Client-side limits - the defaults of the design's 4.6 table
             *
             * Unlike everything above, these are ours and not the RFC's, and every one of them is
             * meant to be configurable; what the table fixes is the value taken when nothing says
             * otherwise. A client needs fewer defenses than a server, not none
             *
             * The table's first row, the decoded header list size, deliberately has no constant
             * here. Its value is "our SETTINGS_MAX_HEADER_LIST_SIZE", which is whatever the
             * active profile advertises (design 6.4) rather than a fixed number - and RFC 9113
             * 6.5.2 gives that setting no numeric initial value either. The decoder takes it as
             * a parameter; nothing in this header may invent one
             */

            enum : std::uint32_t
            {
                /*
                 * Compressed header-block bytes per block, and CONTINUATION frames per block.
                 * Exceeding either is a connection error, the first with ENHANCE_YOUR_CALM
                 */

                MAX_COMPRESSED_HEADER_BLOCK_SIZE_DEFAULT        = 256U * 1024U,
                MAX_CONTINUATION_FRAMES_PER_BLOCK_DEFAULT       = 64U,

                /*
                 * Control-frame bytes queued for a peer which is not reading - the acknowledgements
                 * we owe it. Exceeding it is a connection error
                 */

                MAX_QUEUED_CONTROL_FRAME_BYTES_DEFAULT          = 64U * 1024U,

                /*
                 * Inbound PING and SETTINGS frames per second; exceeding it is a connection error
                 */

                MAX_INBOUND_PING_AND_SETTINGS_PER_SECOND_DEFAULT = 100U,

                /*
                 * Recently closed streams are remembered so that frames arriving on a stream we
                 * have reset can be tolerated, bounded by both a count and an age - the oldest is
                 * forgotten first
                 */

                MAX_REMEMBERED_CLOSED_STREAMS_DEFAULT           = 1000U,
                REMEMBERED_CLOSED_STREAM_TIMEOUT_IN_SECONDS_DEFAULT = 30U,

                /*
                 * The response body accepted in buffered mode, beyond which the stream is
                 * cancelled and an exception thrown. It matches g_maxResponseSizeDefault of
                 * http/SimpleHttpTask.h deliberately, so the two clients agree; the value is
                 * repeated rather than referenced because http2/ does not depend on http/
                 */

                MAX_RESPONSE_SIZE_BUFFERED_DEFAULT              = 1U << 26,     /* 64 MB */

                /*
                 * Retries of one request (D6) before it fails with the last error. This is the
                 * request-level budget for streams the peer provably did not process; it is not
                 * the connection establisher's MAX_RETRY_COUNT, which is a different mechanism
                 * living in tasks/TcpBaseTasks.h
                 */

                MAX_REQUEST_RETRIES_DEFAULT                     = 3U,
            };

            /*************************************************************************************
             * The connection preface and the protocol identifier
             */

            /*
             * The client connection preface, RFC 9113 section 3.4 - the 24 octets
             * 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a, which a client sends as the
             * first application data octets of a connection and follows with a SETTINGS frame
             *
             * An invalid preface is a connection error of type PROTOCOL_ERROR, and a GOAWAY may
             * be omitted in that case since the peer is evidently not speaking HTTP/2
             */

            static const std::string                            g_connectionPreface;

            /*
             * The ALPN protocol identifier for HTTP/2 over TLS, RFC 9113 section 3.1 - the string
             * "h2", serialized into an ALPN protocol identifier as the two octets 0x68, 0x32
             *
             * "h2c", the HTTP Upgrade token, is deprecated by RFC 9113 and is not supported here
             * (D11); cleartext HTTP/2 is reached by prior knowledge instead
             */

            static const std::string                            g_alpnProtocolIdHttp2;

            /**
             * @brief The RFC 9113 section 7 name of an error code, for diagnostics
             *
             * It takes any 32-bit value and never throws on one it does not know, because a peer
             * can put any value in a RST_STREAM or GOAWAY and section 7 requires that an unknown
             * code trigger no special behavior. An unknown code is rendered as its hexadecimal
             * value so that the diagnostic still carries what arrived on the wire
             *
             * Not named toString because a frame type, a settings id and an error code are all
             * small integers: an overload set on them would resolve silently and wrongly
             */

            static std::string errorCodeToString( SAA_in const std::uint32_t errorCode )
            {
                switch( errorCode )
                {
                    case ERROR_CODE_NO_ERROR:
                        return "NO_ERROR";

                    case ERROR_CODE_PROTOCOL_ERROR:
                        return "PROTOCOL_ERROR";

                    case ERROR_CODE_INTERNAL_ERROR:
                        return "INTERNAL_ERROR";

                    case ERROR_CODE_FLOW_CONTROL_ERROR:
                        return "FLOW_CONTROL_ERROR";

                    case ERROR_CODE_SETTINGS_TIMEOUT:
                        return "SETTINGS_TIMEOUT";

                    case ERROR_CODE_STREAM_CLOSED:
                        return "STREAM_CLOSED";

                    case ERROR_CODE_FRAME_SIZE_ERROR:
                        return "FRAME_SIZE_ERROR";

                    case ERROR_CODE_REFUSED_STREAM:
                        return "REFUSED_STREAM";

                    case ERROR_CODE_CANCEL:
                        return "CANCEL";

                    case ERROR_CODE_COMPRESSION_ERROR:
                        return "COMPRESSION_ERROR";

                    case ERROR_CODE_CONNECT_ERROR:
                        return "CONNECT_ERROR";

                    case ERROR_CODE_ENHANCE_YOUR_CALM:
                        return "ENHANCE_YOUR_CALM";

                    case ERROR_CODE_INADEQUATE_SECURITY:
                        return "INADEQUATE_SECURITY";

                    case ERROR_CODE_HTTP_1_1_REQUIRED:
                        return "HTTP_1_1_REQUIRED";

                    default:
                        break;
                }

                cpp::SafeOutputStringStream oss;

                oss
                    << "UNKNOWN_ERROR_CODE(0x"
                    << std::hex
                    << std::setfill( '0' )
                    << std::setw( 8 )
                    << errorCode
                    << ")";

                return oss.str();
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( GlobalsT, g_connectionPreface )      = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        BL_DEFINE_STATIC_CONST_STRING( GlobalsT, g_alpnProtocolIdHttp2 )    = "h2";

        typedef GlobalsT<> Globals;

    } // http2

} // bl

#endif /* __BL_HTTP2_GLOBALS_H_ */
