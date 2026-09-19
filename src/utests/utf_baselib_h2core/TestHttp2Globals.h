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

#ifndef __UTEST_TESTHTTP2GLOBALS_H_
#define __UTEST_TESTHTTP2GLOBALS_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <limits>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2 wire constants, against RFC 9113 rather than against the copy of them in the design
 *
 * Every frame type, flag, settings id and error code in http2/Globals.h is spelled out again here
 * with the value the RFC gives it, so that a transposed digit fails at the first build of this
 * module rather than in an interoperability test months later. The values were read out of the
 * RFC text, section by section, and the section is named on every group below
 *
 * The client-side limits are checked the same way against the design's 4.6 table, which is their
 * source - they are ours, not the RFC's
 *
 * Nothing here opens a socket, takes a lock or allocates a thread pool
 */

namespace utest
{
    namespace http2globals
    {
        /**
         * @brief The client connection preface as RFC 9113 section 3.4 gives it - the 24 octets
         * of 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a, transcribed one octet at a time
         *
         * Built from the hex sequence and not from the "PRI * HTTP/2.0..." spelling, because the
         * header stores the ASCII form and comparing that against itself would prove nothing. The
         * RFC gives both, and they have to agree
         */

        inline auto connectionPrefaceFromRfcOctets() -> std::string
        {
            static const std::uint8_t octets[] =
            {
                0x50U, 0x52U, 0x49U, 0x20U, 0x2aU, 0x20U, 0x48U, 0x54U,
                0x54U, 0x50U, 0x2fU, 0x32U, 0x2eU, 0x30U, 0x0dU, 0x0aU,
                0x0dU, 0x0aU, 0x53U, 0x4dU, 0x0dU, 0x0aU, 0x0dU, 0x0aU,
            };

            std::string result;

            for( const auto octet : octets )
            {
                result.push_back( static_cast< char >( octet ) );
            }

            return result;
        }

    } // http2globals

} // utest

UTF_AUTO_TEST_CASE( Http2Globals_FrameTypesAndFlagsTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * The ten frame types RFC 9113 section 6 defines, one row per subsection
     */

    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_DATA,            0x00U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_HEADERS,         0x01U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_PRIORITY,        0x02U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_RST_STREAM,      0x03U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_SETTINGS,        0x04U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_PUSH_PROMISE,    0x05U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_PING,            0x06U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_GOAWAY,          0x07U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_WINDOW_UPDATE,   0x08U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_TYPE_CONTINUATION,    0x09U );

    /*
     * The flag masks, from the frame diagrams and the prose of sections 6.1, 6.2, 6.5, 6.6, 6.7
     * and 6.10. END_STREAM and ACK are both 0x01 and that is not a mistake: the Flags octet is
     * interpreted per frame type (section 4.1), so the same bit means END_STREAM on DATA and
     * HEADERS and means ACK on SETTINGS and PING
     */

    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_NONE,            0x00U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_END_STREAM,      0x01U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_ACK,             0x01U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_END_HEADERS,     0x04U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_PADDED,          0x08U );
    UTF_REQUIRE_EQUAL( Globals::FRAME_FLAG_PRIORITY,        0x20U );

    /*
     * The four distinct masks occupy four distinct bits of the one octet, which is what makes
     * them combinable - a HEADERS frame really does carry PRIORITY, PADDED, END_HEADERS and
     * END_STREAM together
     */

    UTF_REQUIRE_EQUAL(
        (
            Globals::FRAME_FLAG_END_STREAM |
            Globals::FRAME_FLAG_END_HEADERS |
            Globals::FRAME_FLAG_PADDED |
            Globals::FRAME_FLAG_PRIORITY
        ),
        0x2dU
        );
}

UTF_AUTO_TEST_CASE( Http2Globals_SettingsAndErrorCodesTests )
{
    using namespace bl;
    using namespace bl::http2;

    /*
     * The six settings RFC 9113 section 6.5.2 defines
     */

    UTF_REQUIRE_EQUAL( Globals::SETTINGS_HEADER_TABLE_SIZE,         0x01U );
    UTF_REQUIRE_EQUAL( Globals::SETTINGS_ENABLE_PUSH,               0x02U );
    UTF_REQUIRE_EQUAL( Globals::SETTINGS_MAX_CONCURRENT_STREAMS,    0x03U );
    UTF_REQUIRE_EQUAL( Globals::SETTINGS_INITIAL_WINDOW_SIZE,       0x04U );
    UTF_REQUIRE_EQUAL( Globals::SETTINGS_MAX_FRAME_SIZE,            0x05U );
    UTF_REQUIRE_EQUAL( Globals::SETTINGS_MAX_HEADER_LIST_SIZE,      0x06U );

    /*
     * The fourteen error codes of RFC 9113 section 7, 0x00 through 0x0d with no gaps
     */

    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_NO_ERROR,                0x00U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_PROTOCOL_ERROR,          0x01U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_INTERNAL_ERROR,          0x02U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_FLOW_CONTROL_ERROR,      0x03U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_SETTINGS_TIMEOUT,        0x04U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_STREAM_CLOSED,           0x05U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_FRAME_SIZE_ERROR,        0x06U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_REFUSED_STREAM,          0x07U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_CANCEL,                  0x08U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_COMPRESSION_ERROR,       0x09U );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_CONNECT_ERROR,           0x0aU );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_ENHANCE_YOUR_CALM,       0x0bU );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_INADEQUATE_SECURITY,     0x0cU );
    UTF_REQUIRE_EQUAL( Globals::ERROR_CODE_HTTP_1_1_REQUIRED,       0x0dU );

    /*
     * Every one of them renders under the RFC's own name, which is what makes a GOAWAY in a log
     * or an exception readable
     */

    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x00U ), "NO_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x01U ), "PROTOCOL_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x02U ), "INTERNAL_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x03U ), "FLOW_CONTROL_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x04U ), "SETTINGS_TIMEOUT" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x05U ), "STREAM_CLOSED" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x06U ), "FRAME_SIZE_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x07U ), "REFUSED_STREAM" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x08U ), "CANCEL" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x09U ), "COMPRESSION_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x0aU ), "CONNECT_ERROR" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x0bU ), "ENHANCE_YOUR_CALM" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x0cU ), "INADEQUATE_SECURITY" );
    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x0dU ), "HTTP_1_1_REQUIRED" );

    /*
     * A code the RFC does not define must not throw and must not be silently rewritten into one
     * that is: section 7 says an unknown code triggers no special behavior, and a peer can put
     * any 32-bit value in a RST_STREAM or a GOAWAY. The value itself survives into the text -
     * 0x0e is the first unassigned code, and the second row is the largest value there is
     */

    UTF_REQUIRE_EQUAL( Globals::errorCodeToString( 0x0eU ), "UNKNOWN_ERROR_CODE(0x0000000e)" );

    UTF_REQUIRE_EQUAL(
        Globals::errorCodeToString( std::numeric_limits< std::uint32_t >::max() ),
        "UNKNOWN_ERROR_CODE(0xffffffff)"
        );
}

UTF_AUTO_TEST_CASE( Http2Globals_ProtocolConstantsAndLimitsTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::http2globals;

    /*
     * The connection preface, RFC 9113 section 3.4 - 24 octets, and the same 24 octets the RFC
     * gives in hex. The header spells them as ASCII, so transcribing the hex is what makes this
     * a check rather than a restatement
     */

    UTF_REQUIRE_EQUAL( Globals::g_connectionPreface.size(), 24U );
    UTF_REQUIRE_EQUAL( Globals::g_connectionPreface, connectionPrefaceFromRfcOctets() );

    /*
     * The ALPN protocol identifier, section 3.1 - the string "h2", serialized as the two octets
     * 0x68, 0x32
     */

    UTF_REQUIRE_EQUAL( Globals::g_alpnProtocolIdHttp2, "h2" );

    UTF_REQUIRE(
        Globals::g_alpnProtocolIdHttp2.size() == 2U                                     &&
        static_cast< std::uint8_t >( Globals::g_alpnProtocolIdHttp2[ 0 ] ) == 0x68U      &&
        static_cast< std::uint8_t >( Globals::g_alpnProtocolIdHttp2[ 1 ] ) == 0x32U
        );

    /*
     * The frame header and the stream identifier space, sections 4.1 and 5.1.1
     */

    UTF_REQUIRE_EQUAL( Globals::FRAME_HEADER_SIZE,      9U );
    UTF_REQUIRE_EQUAL( Globals::STREAM_ID_CONNECTION,   0U );
    UTF_REQUIRE_EQUAL( Globals::MAX_STREAM_ID,          ( 1U << 31 ) - 1U );

    /*
     * The frame size range of section 4.2, whose lower end is also the initial value of
     * SETTINGS_MAX_FRAME_SIZE in 6.5.2
     */

    UTF_REQUIRE_EQUAL( Globals::MAX_FRAME_SIZE_DEFAULT,         1U << 14 );
    UTF_REQUIRE_EQUAL( Globals::MAX_FRAME_SIZE_UPPER_BOUND,     ( 1U << 24 ) - 1U );

    /*
     * The two settings of 6.5.2 with a numeric initial value, beside the window size below
     */

    UTF_REQUIRE_EQUAL( Globals::HEADER_TABLE_SIZE_DEFAULT,      4096U );
    UTF_REQUIRE_EQUAL( Globals::ENABLE_PUSH_DEFAULT,            1U );

    /*
     * Flow control, sections 6.9.1 and 6.9.2. The maximum is exactly what a signed 32-bit window
     * can hold, which is why the window type is signed and the maximum is not 2^32-1
     */

    UTF_REQUIRE_EQUAL( Globals::INITIAL_WINDOW_SIZE_DEFAULT,    65535 );
    UTF_REQUIRE_EQUAL( Globals::MAX_FLOW_CONTROL_WINDOW_SIZE,   2147483647 );

    UTF_REQUIRE_EQUAL(
        Globals::MAX_FLOW_CONTROL_WINDOW_SIZE,
        std::numeric_limits< std::int32_t >::max()
        );

    /*
     * The client-side limits of the design's 4.6 table, in the order the table lists them. Its
     * first row, the decoded header list size, has no constant to check: it is whatever the
     * active profile advertises as our SETTINGS_MAX_HEADER_LIST_SIZE
     */

    UTF_REQUIRE_EQUAL( Globals::MAX_COMPRESSED_HEADER_BLOCK_SIZE_DEFAULT,           262144U );
    UTF_REQUIRE_EQUAL( Globals::MAX_CONTINUATION_FRAMES_PER_BLOCK_DEFAULT,          64U );
    UTF_REQUIRE_EQUAL( Globals::MAX_QUEUED_CONTROL_FRAME_BYTES_DEFAULT,             65536U );
    UTF_REQUIRE_EQUAL( Globals::MAX_INBOUND_PING_AND_SETTINGS_PER_SECOND_DEFAULT,   100U );
    UTF_REQUIRE_EQUAL( Globals::MAX_REMEMBERED_CLOSED_STREAMS_DEFAULT,              1000U );
    UTF_REQUIRE_EQUAL( Globals::REMEMBERED_CLOSED_STREAM_TIMEOUT_IN_SECONDS_DEFAULT, 30U );
    UTF_REQUIRE_EQUAL( Globals::MAX_RESPONSE_SIZE_BUFFERED_DEFAULT,                 67108864U );
    UTF_REQUIRE_EQUAL( Globals::MAX_REQUEST_RETRIES_DEFAULT,                        3U );
}

#endif /* __UTEST_TESTHTTP2GLOBALS_H_ */
