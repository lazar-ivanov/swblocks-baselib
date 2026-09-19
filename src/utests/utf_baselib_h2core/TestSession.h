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

#ifndef __UTEST_TESTSESSION_H_
#define __UTEST_TESTSESSION_H_

#include <baselib/http2/Session.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeUtils.h>

#include <cstdint>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * The HTTP/2 session engine - notes/plans/http2-design.md 4.5 and 4.6, RFC 9113
 *
 * Conformance is expressed as BYTE SCRIPTS: exact octets are fed in and the exact event sequence
 * and the exact octets produced are asserted. That is only possible because the engine calls
 * nothing back - feed( ) appends to a queue which the test drains - so every case here is a
 * statement about a pure function of its input
 *
 * The four contracts the slice was asked to decide each have a case of their own, named for it,
 * because each of them is a SILENT failure: the wrong answer still compiles, still passes every
 * other case here, and shows up as an interoperability defect or a crash against a slow peer
 *
 *  - Session_HeaderBlockGranularityTests      contract 1
 *  - Session_HpackDecoderCeilingTests         contract 2
 *  - Session_LocalSettingsTakeEffectOnAckTests contract 3
 *  - Session_StreamErrorOnClosedStreamTests   contract 4
 */

namespace utest
{
    namespace session
    {
        typedef bl::http2::Session::wire_buffer_t                   wire_buffer_t;

        inline std::string toText( SAA_in const wire_buffer_t& buffer )
        {
            return buffer.empty() ?
                std::string() :
                std::string(
                    reinterpret_cast< const char* >( &buffer[ 0 ] ),
                    buffer.size()
                    );
        }

        /**
         * @brief The nine octet frame header plus a payload, built by hand
         *
         * Deliberately not through FrameCodec: a byte script which builds its input with the same
         * serializer the engine parses with proves only that the two agree
         */

        inline std::string makeFrame(
            SAA_in          const std::uint8_t                   type,
            SAA_in          const std::uint8_t                   flags,
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   payload
            )
        {
            std::string out;

            out.push_back( static_cast< char >( ( payload.size() >> 16 ) & 0xFFU ) );
            out.push_back( static_cast< char >( ( payload.size() >> 8 ) & 0xFFU ) );
            out.push_back( static_cast< char >( payload.size() & 0xFFU ) );
            out.push_back( static_cast< char >( type ) );
            out.push_back( static_cast< char >( flags ) );
            out.push_back( static_cast< char >( ( streamId >> 24 ) & 0xFFU ) );
            out.push_back( static_cast< char >( ( streamId >> 16 ) & 0xFFU ) );
            out.push_back( static_cast< char >( ( streamId >> 8 ) & 0xFFU ) );
            out.push_back( static_cast< char >( streamId & 0xFFU ) );

            out.append( payload );

            return out;
        }

        inline std::string uint32Octets( SAA_in const std::uint32_t value )
        {
            std::string out;

            out.push_back( static_cast< char >( ( value >> 24 ) & 0xFFU ) );
            out.push_back( static_cast< char >( ( value >> 16 ) & 0xFFU ) );
            out.push_back( static_cast< char >( ( value >> 8 ) & 0xFFU ) );
            out.push_back( static_cast< char >( value & 0xFFU ) );

            return out;
        }

        inline std::string settingsPayload(
            SAA_in          const std::vector< bl::http2::Http2Setting >&    settings
            )
        {
            std::string out;

            for( std::size_t i = 0U; i < settings.size(); ++i )
            {
                out.push_back( static_cast< char >( ( settings[ i ].id >> 8 ) & 0xFFU ) );
                out.push_back( static_cast< char >( settings[ i ].id & 0xFFU ) );

                out.append( uint32Octets( settings[ i ].value ) );
            }

            return out;
        }

        inline bl::http2::Http2Setting setting(
            SAA_in          const std::uint16_t                  id,
            SAA_in          const std::uint32_t                  value
            )
        {
            bl::http2::Http2Setting result;

            result.id = id;
            result.value = value;

            return result;
        }

        inline std::string settingsFrame(
            SAA_in          const std::vector< bl::http2::Http2Setting >&    settings
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_SETTINGS,
                bl::http2::Globals::FRAME_FLAG_NONE,
                0U,
                settingsPayload( settings )
                );
        }

        inline std::string settingsAckFrame()
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_SETTINGS,
                bl::http2::Globals::FRAME_FLAG_ACK,
                0U,
                std::string()
                );
        }

        inline std::string rstStreamFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::uint32_t                  errorCode
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_RST_STREAM,
                bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                uint32Octets( errorCode )
                );
        }

        inline std::string windowUpdateFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::uint32_t                  increment
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_WINDOW_UPDATE,
                bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                uint32Octets( increment )
                );
        }

        inline std::string goAwayFrame(
            SAA_in          const std::uint32_t                  lastStreamId,
            SAA_in          const std::uint32_t                  errorCode,
            SAA_in_opt      const std::string&                   debugData = std::string()
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_GOAWAY,
                bl::http2::Globals::FRAME_FLAG_NONE,
                0U,
                uint32Octets( lastStreamId ) + uint32Octets( errorCode ) + debugData
                );
        }

        inline std::string pingFrame(
            SAA_in          const std::string&                   opaqueData,
            SAA_in          const bool                           isAck
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_PING,
                isAck ?
                    bl::http2::Globals::FRAME_FLAG_ACK : bl::http2::Globals::FRAME_FLAG_NONE,
                0U,
                opaqueData
                );
        }

        inline std::string dataFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   payload,
            SAA_in          const bool                           endStream
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_DATA,
                endStream ?
                    bl::http2::Globals::FRAME_FLAG_END_STREAM :
                    bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                payload
                );
        }

        inline std::string headersFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   block,
            SAA_in          const bool                           endStream,
            SAA_in          const bool                           endHeaders
            )
        {
            const auto flags = static_cast< std::uint8_t >(
                ( endStream ? bl::http2::Globals::FRAME_FLAG_END_STREAM : 0U ) |
                ( endHeaders ? bl::http2::Globals::FRAME_FLAG_END_HEADERS : 0U )
                );

            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_HEADERS,
                flags,
                streamId,
                block
                );
        }

        inline std::string continuationFrame(
            SAA_in          const std::uint32_t                  streamId,
            SAA_in          const std::string&                   block,
            SAA_in          const bool                           endHeaders
            )
        {
            return makeFrame(
                bl::http2::Globals::FRAME_TYPE_CONTINUATION,
                endHeaders ?
                    bl::http2::Globals::FRAME_FLAG_END_HEADERS :
                    bl::http2::Globals::FRAME_FLAG_NONE,
                streamId,
                block
                );
        }

        inline bl::http2::HpackField field(
            SAA_in          const std::string&                   name,
            SAA_in          const std::string&                   value
            )
        {
            return bl::http2::HpackField( bl::cpp::copy( name ), bl::cpp::copy( value ) );
        }

        /**
         * @brief One RFC 7541 6.2.2 literal without indexing, new name, neither string Huffman coded
         *
         * Built by hand rather than through HpackEncoder, because the encoder lowercases a field
         * name as RFC 9113 8.2.1 requires it to - so a block which is supposed to carry
         * "Uppercase-Name" cannot be produced by it at all. Only names and values shorter than 127
         * octets are needed here, so the length prefixes are one octet each
         */

        inline std::string rawLiteral(
            SAA_in          const std::string&                   name,
            SAA_in          const std::string&                   value
            )
        {
            UTF_REQUIRE( name.size() < 127U && value.size() < 127U );

            std::string out;

            out.push_back( static_cast< char >( 0x00 ) );
            out.push_back( static_cast< char >( name.size() ) );
            out.append( name );
            out.push_back( static_cast< char >( value.size() ) );
            out.append( value );

            return out;
        }

        /**
         * @brief The peer's HPACK encoder, so that a test writes a field list and gets a block
         *
         * One of these per script, because the encoding context is per connection and per
         * direction (RFC 7541 2.2) - a second one would drift from the session's decoder exactly
         * as two peers would
         */

        class PeerEncoder FINAL
        {
            BL_NO_COPY_OR_MOVE( PeerEncoder )

        private:

            bl::http2::HpackEncoder                     m_encoder;

        public:

            explicit PeerEncoder(
                SAA_in_opt      const std::size_t                capacity =
                    static_cast< std::size_t >(
                        bl::http2::Globals::HEADER_TABLE_SIZE_DEFAULT
                        )
                )
                :
                m_encoder( capacity )
            {
            }

            void setCapacity( SAA_in const std::size_t capacity ) NOEXCEPT
            {
                m_encoder.setDynamicTableCapacity( capacity );
            }

            std::string encode( SAA_in const bl::http2::HpackFieldList& fields )
            {
                std::string block;

                m_encoder.encode( fields, block );

                return block;
            }

            std::string response(
                SAA_in          const std::string&              status,
                SAA_in_opt      const bl::http2::HpackFieldList& extra =
                                    bl::http2::HpackFieldList()
                )
            {
                bl::http2::HpackFieldList fields;

                fields.push_back( field( ":status", status ) );

                for( std::size_t i = 0U; i < extra.size(); ++i )
                {
                    fields.push_back( extra[ i ] );
                }

                return encode( fields );
            }
        };

        inline bl::http2::SessionRequest makeRequest( SAA_in_opt const bool hasBody = false )
        {
            bl::http2::SessionRequest request;

            request.method = "GET";
            request.scheme = "https";
            request.authority = "example.com";
            request.path = "/";
            request.hasBody = hasBody;

            return request;
        }

        /**
         * @brief Drains the session and returns the events, for assertions on the whole sequence
         */

        inline std::vector< bl::http2::SessionEvent > drain(
            SAA_inout       bl::http2::Session&                  session
            )
        {
            std::vector< bl::http2::SessionEvent > events;

            while( session.hasEvents() )
            {
                events.push_back( session.frontEvent() );

                session.popEvent();
            }

            return events;
        }

        inline std::string produceText(
            SAA_inout       bl::http2::Session&                  session,
            SAA_in          const bl::time::ptime&               now
            )
        {
            wire_buffer_t out;

            session.produce( out, now );

            return toText( out );
        }

        inline void feedText(
            SAA_inout       bl::http2::Session&                  session,
            SAA_in          const std::string&                   bytes,
            SAA_in          const bl::time::ptime&               now
            )
        {
            session.feed( bytes, now );
        }

        inline bl::time::ptime baseTime()
        {
            return bl::time::ptime( bl::time::date( 2026, 9, 19 ) );
        }

        /**
         * @brief How many frames of a given type are in a buffer, walking the nine octet headers
         */

        inline std::size_t countFrames(
            SAA_in          const std::string&                   bytes,
            SAA_in          const std::uint8_t                   type
            )
        {
            std::size_t count = 0U;
            std::size_t offset = 0U;

            while( offset + 9U <= bytes.size() )
            {
                const auto length =
                    ( static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset ] ) ) << 16 ) |
                    ( static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset + 1U ] ) ) << 8 ) |
                      static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset + 2U ] ) );

                if( static_cast< std::uint8_t >( bytes[ offset + 3U ] ) == type )
                {
                    ++count;
                }

                offset += 9U + length;
            }

            return count;
        }

        /**
         * @brief The type of every frame in a buffer, in order
         */

        inline std::vector< std::uint8_t > frameTypes( SAA_in const std::string& bytes )
        {
            std::vector< std::uint8_t > types;

            std::size_t offset = 0U;

            while( offset + 9U <= bytes.size() )
            {
                const auto length =
                    ( static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset ] ) ) << 16 ) |
                    ( static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset + 1U ] ) ) << 8 ) |
                      static_cast< std::uint32_t >(
                        static_cast< unsigned char >( bytes[ offset + 2U ] ) );

                types.push_back( static_cast< std::uint8_t >( bytes[ offset + 3U ] ) );

                offset += 9U + length;
            }

            return types;
        }

        /**
         * @brief Establishes a connection - the opening write is produced and the peer's SETTINGS
         * and its acknowledgement are fed, so the session is in its steady state
         */

        inline void settle(
            SAA_inout       bl::http2::Session&                  session,
            SAA_in          const bl::time::ptime&               now,
            SAA_in_opt      const std::vector< bl::http2::Http2Setting >& peerSettings =
                                std::vector< bl::http2::Http2Setting >()
            )
        {
            ( void ) produceText( session, now );

            feedText( session, settingsFrame( peerSettings ), now );
            feedText( session, settingsAckFrame(), now );

            ( void ) drain( session );
            ( void ) produceText( session, now );
        }

    } // session

} // utest

UTF_AUTO_TEST_CASE( Session_OpeningWriteTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * A client's first octets are the 24 octet preface of RFC 9113 3.4 followed by a SETTINGS
     * frame - and D11 has us always advertise SETTINGS_ENABLE_PUSH = 0, so a profile which names
     * no settings still sends that one
     */

    {
        Session session( StreamRole::Client, now );

        const auto opening = produceText( session, now );

        std::string expected = Globals::g_connectionPreface;

        std::vector< Http2Setting > ours;

        ours.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );

        expected += settingsFrame( ours );

        UTF_REQUIRE_EQUAL( opening, expected );

        UTF_REQUIRE( ! session.wantsWrite() );
        UTF_REQUIRE_EQUAL( session.unacknowledgedSettingsCount(), 1U );
    }

    /*
     * A profile's ordered list goes out verbatim, in its own order, because that order is part of
     * the fingerprint - and the connection WINDOW_UPDATE and the idle-stream PRIORITY frames
     * follow it, which is the opening write of design 5.1
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 0U ) );
        profile.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 6291456U ) );

        profile.connectionWindowUpdateIncrement = 15663105U;

        Http2PriorityFrame idle;

        idle.streamId = 3U;
        idle.streamDependency = 0U;
        idle.weight = 200U;
        idle.exclusive = false;

        profile.idleStreamPriorities.push_back( idle );

        Session session( StreamRole::Client, now, profile );

        const auto opening = produceText( session, now );

        std::string expected = Globals::g_connectionPreface;

        expected += settingsFrame( profile.settings );
        expected += windowUpdateFrame( Globals::STREAM_ID_CONNECTION, 15663105U );
        expected += makeFrame(
            Globals::FRAME_TYPE_PRIORITY,
            Globals::FRAME_FLAG_NONE,
            3U,
            uint32Octets( 0U ) + std::string( 1U, static_cast< char >( 200 ) )
            );

        UTF_REQUIRE_EQUAL( opening, expected );

        /*
         * The connection receive window holds exactly what that WINDOW_UPDATE advertised, which is
         * the one place the frame and the window have to agree
         */

        UTF_REQUIRE_EQUAL(
            session.connectionReceiveWindow(),
            static_cast< std::int32_t >( 65535 + 15663105 )
            );
    }

    /*
     * A client profile cannot advertise push - D11 is not negotiable, and getting it wrong is ours
     * rather than the peer's, so it is a programming error
     */

    {
        Http2Profile profile;

        profile.settings.push_back( setting( Globals::SETTINGS_ENABLE_PUSH, 1U ) );

        UTF_REQUIRE_THROW_MESSAGE(
            Session( StreamRole::Client, now, profile ),
            UnexpectedException,
            "An HTTP/2 client profile cannot advertise SETTINGS_ENABLE_PUSH with a value other "
                "than zero"
            );
    }

    /*
     * The server side of the preface (D8, the role neutrality the test peer needs): it is expected
     * on the way in, and anything else is a connection error of type PROTOCOL_ERROR
     */

    {
        Session server( StreamRole::Server, now );

        const auto opening = produceText( server, now );

        UTF_REQUIRE( opening.find( Globals::g_connectionPreface ) == std::string::npos );

        feedText( server, Globals::g_connectionPreface + settingsFrame(
            std::vector< Http2Setting >() ), now );

        UTF_REQUIRE( ! server.isClosed() );

        Session rude( StreamRole::Server, now );

        feedText( rude, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", now );

        UTF_REQUIRE( rude.isClosed() );
        UTF_REQUIRE_EQUAL( rude.connectionErrorCode(), Globals::ERROR_CODE_PROTOCOL_ERROR );
    }
}

UTF_AUTO_TEST_CASE( Session_RequestAndResponseTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    Session session( StreamRole::Client, now );
    PeerEncoder peer;

    settle( session, now );

    /*
     * The first client stream is 1 and the identifiers are odd and monotonic (5.1.1)
     */

    const auto streamId = session.submitRequest( makeRequest() );

    UTF_REQUIRE_EQUAL( streamId, 1U );
    UTF_REQUIRE( session.wantsWrite() );

    const auto request = produceText( session, now );

    UTF_REQUIRE_EQUAL( frameTypes( request ).size(), 1U );
    UTF_REQUIRE_EQUAL( frameTypes( request )[ 0 ], Globals::FRAME_TYPE_HEADERS );

    /*
     * No body, so the HEADERS carries END_STREAM and END_HEADERS
     */

    UTF_REQUIRE_EQUAL(
        static_cast< std::uint8_t >( request[ 4 ] ),
        static_cast< std::uint8_t >(
            Globals::FRAME_FLAG_END_STREAM | Globals::FRAME_FLAG_END_HEADERS
            )
        );

    HpackFieldList extra;

    extra.push_back( field( "content-type", "text/plain" ) );
    extra.push_back( field( "content-length", "5" ) );

    feedText( session, headersFrame( 1U, peer.response( "200", extra ), false, true ), now );
    feedText( session, dataFrame( 1U, "hello", true ), now );

    const auto events = drain( session );

    UTF_REQUIRE_EQUAL( events.size(), 3U );

    UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
    UTF_REQUIRE_EQUAL( events[ 0 ].streamId.value(), 1U );
    UTF_REQUIRE_EQUAL( events[ 0 ].status.value(), 200U );
    UTF_REQUIRE( ! events[ 0 ].endStream );
    UTF_REQUIRE( ! events[ 0 ].isInformational );
    UTF_REQUIRE( ! events[ 0 ].isTrailers );
    UTF_REQUIRE_EQUAL( events[ 0 ].fields.size(), 3U );
    UTF_REQUIRE_EQUAL( events[ 0 ].fields[ 0 ].name(), std::string( ":status" ) );
    UTF_REQUIRE_EQUAL( events[ 0 ].fields[ 0 ].value(), std::string( "200" ) );

    UTF_REQUIRE( events[ 1 ].type == SessionEventType::Data );
    UTF_REQUIRE_EQUAL( events[ 1 ].data, std::string( "hello" ) );
    UTF_REQUIRE( events[ 1 ].endStream );

    UTF_REQUIRE( events[ 2 ].type == SessionEventType::StreamClosed );
    UTF_REQUIRE_EQUAL( events[ 2 ].errorCode.value(), Globals::ERROR_CODE_NO_ERROR );
    UTF_REQUIRE( events[ 2 ].isMessageComplete );
    UTF_REQUIRE( ! events[ 2 ].isRetryable );

    UTF_REQUIRE_EQUAL( session.activeStreamCount(), 0U );

    /*
     * An interim response does not end the stream and is reported as one (8.1). The final response
     * follows it on the same stream
     */

    const auto second = session.submitRequest( makeRequest() );

    ( void ) produceText( session, now );

    feedText( session, headersFrame( second, peer.response( "103" ), false, true ), now );
    feedText( session, headersFrame( second, peer.response( "200" ), true, true ), now );

    const auto interim = drain( session );

    UTF_REQUIRE_EQUAL( interim.size(), 3U );
    UTF_REQUIRE( interim[ 0 ].isInformational );
    UTF_REQUIRE_EQUAL( interim[ 0 ].status.value(), 103U );
    UTF_REQUIRE( ! interim[ 1 ].isInformational );
    UTF_REQUIRE_EQUAL( interim[ 1 ].status.value(), 200U );
    UTF_REQUIRE( interim[ 2 ].type == SessionEventType::StreamClosed );

    /*
     * A trailer section is a second header block after the final one, and it MUST end the stream
     */

    const auto third = session.submitRequest( makeRequest() );

    ( void ) produceText( session, now );

    HpackFieldList trailers;

    trailers.push_back( field( "x-checksum", "abc" ) );

    feedText( session, headersFrame( third, peer.response( "200" ), false, true ), now );
    feedText( session, dataFrame( third, "body", false ), now );
    feedText( session, headersFrame( third, peer.encode( trailers ), true, true ), now );

    const auto withTrailers = drain( session );

    UTF_REQUIRE_EQUAL( withTrailers.size(), 4U );
    UTF_REQUIRE( withTrailers[ 2 ].type == SessionEventType::Headers );
    UTF_REQUIRE( withTrailers[ 2 ].isTrailers );
    UTF_REQUIRE( withTrailers[ 2 ].endStream );
    UTF_REQUIRE_EQUAL( withTrailers[ 2 ].fields.size(), 1U );
    UTF_REQUIRE( withTrailers[ 3 ].type == SessionEventType::StreamClosed );
    UTF_REQUIRE( withTrailers[ 3 ].isMessageComplete );
}

UTF_AUTO_TEST_CASE( Session_HeaderBlockGranularityTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * CONTRACT 1. A HEADERS frame carrying END_STREAM but NOT END_HEADERS must not close the
     * stream: the field block is one message spread over several frames, and the transition
     * belongs to the message. Told about the frame instead, the stream would close out from under
     * its own CONTINUATION frames and they would arrive on a stream which no longer exists
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "x-one", "first" ) );
        extra.push_back( field( "x-two", "second" ) );

        const auto block = peer.response( "200", extra );

        const auto split = block.size() / 2U;

        feedText(
            session,
            headersFrame(
                streamId,
                block.substr( 0U, split ),
                true /* endStream */,
                false /* endHeaders */
                ),
            now
            );

        /*
         * Nothing yet - not an event, and above all not a closed stream
         */

        UTF_REQUIRE( ! session.hasEvents() );
        UTF_REQUIRE_EQUAL( session.activeStreamCount(), 1U );
        UTF_REQUIRE( ! session.isClosed() );

        feedText(
            session,
            continuationFrame( streamId, block.substr( split ), true /* endHeaders */ ),
            now
            );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE( events[ 0 ].endStream );
        UTF_REQUIRE_EQUAL( events[ 0 ].fields.size(), 3U );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );

        UTF_REQUIRE( ! session.isClosed() );
        UTF_REQUIRE_EQUAL( session.activeStreamCount(), 0U );
    }

    /*
     * The same script, fed one octet at a time. feed( ) is a pure function of the byte sequence
     * and not of how the network chopped it up
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        const auto block = peer.response( "200" );

        const auto script =
            headersFrame( streamId, block.substr( 0U, 1U ), true, false ) +
            continuationFrame( streamId, block.substr( 1U ), true );

        for( std::size_t i = 0U; i < script.size(); ++i )
        {
            feedText( session, script.substr( i, 1U ), now );
        }

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
    }

    /*
     * A block which is refused is still DECODED, or the dynamic table stops tracking the peer's
     * encoder and the next block - on another stream, perfectly good - fails to decode. Here the
     * first response is malformed and its stream is reset; the second one indexes an entry the
     * first one added, and it must still resolve
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto first = session.submitRequest( makeRequest() );
        const auto second = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList malformed;

        malformed.push_back( field( ":status", "200" ) );
        malformed.push_back( field( "x-remembered", "value-which-enters-the-table" ) );
        malformed.push_back( field( "te", "gzip" ) );

        feedText( session, headersFrame( first, peer.encode( malformed ), true, true ), now );

        const auto rejected = drain( session );

        UTF_REQUIRE_EQUAL( rejected.size(), 1U );
        UTF_REQUIRE( rejected[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            rejected[ 0 ].errorCode.value(),
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );

        UTF_REQUIRE( ! session.isClosed() );

        HpackFieldList good;

        good.push_back( field( ":status", "200" ) );
        good.push_back( field( "x-remembered", "value-which-enters-the-table" ) );

        feedText( session, headersFrame( second, peer.encode( good ), true, true ), now );

        const auto delivered = drain( session );

        UTF_REQUIRE_EQUAL( delivered.size(), 2U );
        UTF_REQUIRE( delivered[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE_EQUAL( delivered[ 0 ].fields.size(), 2U );
        UTF_REQUIRE_EQUAL(
            delivered[ 0 ].fields[ 1 ].value(),
            std::string( "value-which-enters-the-table" )
            );

        UTF_REQUIRE( ! session.isClosed() );
    }
}

UTF_AUTO_TEST_CASE( Session_HpackDecoderCeilingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * CONTRACT 2. A profile advertising LESS than the protocol's initial 4096 leaves a window
     * between our SETTINGS going out and the peer's acknowledgement in which the peer is still
     * entitled to 4096. The decoder is therefore constructed at the larger of the two and the
     * ceiling drops only on the ack
     */

    Http2Profile profile;

    profile.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 1024U ) );

    {
        Session session( StreamRole::Client, now );

        UTF_REQUIRE_EQUAL(
            session.hpackDecoderCeiling(),
            static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
            );
    }

    {
        Session session( StreamRole::Client, now, profile );

        UTF_REQUIRE_EQUAL(
            session.hpackDecoderCeiling(),
            static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT )
            );

        /*
         * A size update to 4096 arriving BEFORE the ack is legal and must decode. A decoder
         * constructed at 1024 would have refused it with COMPRESSION_ERROR and taken the whole
         * connection down over a frame the peer was entitled to send
         */

        ( void ) produceText( session, now );

        feedText( session, settingsFrame( std::vector< Http2Setting >() ), now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        PeerEncoder peer( 1024U );

        peer.setCapacity( static_cast< std::size_t >( Globals::HEADER_TABLE_SIZE_DEFAULT ) );

        ( void ) drain( session );

        feedText( session, headersFrame( streamId, peer.response( "200" ), true, true ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE_EQUAL( events[ 0 ].status.value(), 200U );

        /*
         * The ack is what lowers it
         */

        feedText( session, settingsAckFrame(), now );

        UTF_REQUIRE_EQUAL( session.hpackDecoderCeiling(), static_cast< std::size_t >( 1024 ) );

        const auto acked = drain( session );

        UTF_REQUIRE_EQUAL( acked.size(), 1U );
        UTF_REQUIRE( acked[ 0 ].type == SessionEventType::SettingsAcknowledged );
    }

    /*
     * And after the ack a size update above what we advertised is a COMPRESSION_ERROR connection
     * error, because the peer has now been told
     */

    {
        Session session( StreamRole::Client, now, profile );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        PeerEncoder peer( 1024U );

        peer.setCapacity( 2048U );

        feedText( session, headersFrame( streamId, peer.response( "200" ), true, true ), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_COMPRESSION_ERROR
            );
    }

    /*
     * Advertising MORE than 4096 is safe at any time, so the ceiling is the advertised value from
     * the start and the ack changes nothing about it
     */

    {
        Http2Profile larger;

        larger.settings.push_back( setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 65536U ) );

        Session session( StreamRole::Client, now, larger );

        UTF_REQUIRE_EQUAL( session.hpackDecoderCeiling(), static_cast< std::size_t >( 65536 ) );

        settle( session, now );

        UTF_REQUIRE_EQUAL( session.hpackDecoderCeiling(), static_cast< std::size_t >( 65536 ) );
    }
}

UTF_AUTO_TEST_CASE( Session_LocalSettingsTakeEffectOnAckTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * CONTRACT 3. SETTINGS_MAX_FRAME_SIZE reaches the frame reader on the ack and not before: a
     * peer which has not acknowledged does not know the new value, and an oversize frame from it
     * is still a connection error
     */

    Http2Profile profile;

    profile.settings.push_back( setting( Globals::SETTINGS_MAX_FRAME_SIZE, 32768U ) );

    const std::string large( 20000U, 'x' );

    {
        Session session( StreamRole::Client, now, profile );

        ( void ) produceText( session, now );

        feedText( session, settingsFrame( std::vector< Http2Setting >() ), now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, dataFrame( streamId, large, false ), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_FRAME_SIZE_ERROR
            );
    }

    {
        Session session( StreamRole::Client, now, profile );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );
        feedText( session, dataFrame( streamId, large, false ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::Data );
        UTF_REQUIRE_EQUAL( events[ 1 ].data.size(), large.size() );
    }

    /*
     * THE INSTANCE THAT BITES. A profile advertising a SMALLER receive window than the protocol's
     * 65535 must not make the engine refuse data the peer sent legally under the old value.
     * Applied when the SETTINGS was sent, the window would already be 1000 and 2000 octets would
     * be a FLOW_CONTROL_ERROR on a frame which broke no rule
     */

    {
        Http2Profile small;

        small.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 1000U ) );

        Session session( StreamRole::Client, now, small );
        PeerEncoder peer;

        ( void ) produceText( session, now );

        feedText( session, settingsFrame( std::vector< Http2Setting >() ), now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );
        ( void ) drain( session );

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( Globals::INITIAL_WINDOW_SIZE_DEFAULT )
            );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );
        feedText( session, dataFrame( streamId, std::string( 2000U, 'y' ), false ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::Data );

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( 65535 - 2000 )
            );

        /*
         * The ack applies the difference retroactively, exactly as the peer has just applied it to
         * its own send window - and a window driven negative by it is legal (6.9.2)
         */

        feedText( session, settingsAckFrame(), now );

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( 65535 - 2000 ) - static_cast< std::int32_t >( 64535 )
            );
    }

    /*
     * SETTINGS_MAX_HEADER_LIST_SIZE is the decoded-size limit of the design's 4.6 table, and it
     * too arrives with the ack
     */

    {
        Http2Profile bounded;

        bounded.settings.push_back( setting( Globals::SETTINGS_MAX_HEADER_LIST_SIZE, 200U ) );

        Session session( StreamRole::Client, now, bounded );

        UTF_REQUIRE_EQUAL(
            session.maxDecodedHeaderListSize(),
            ( std::numeric_limits< std::size_t >::max )()
            );

        settle( session, now );

        UTF_REQUIRE_EQUAL( session.maxDecodedHeaderListSize(), static_cast< std::size_t >( 200 ) );
    }

    /*
     * An acknowledgement for a SETTINGS we never sent is a connection error - there is nothing it
     * could be acknowledging
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        feedText( session, settingsAckFrame(), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_PROTOCOL_ERROR );
    }

    /*
     * Our own value out of range is ours to have got wrong, so it is a programming error raised
     * where the mistake is rather than a protocol error much later
     */

    {
        Session session( StreamRole::Client, now );

        std::vector< Http2Setting > bad;

        bad.push_back( setting( Globals::SETTINGS_MAX_FRAME_SIZE, 1024U ) );

        UTF_REQUIRE_THROW_MESSAGE(
            session.applyLocalSettings( bad ),
            UnexpectedException,
            "An HTTP/2 SETTINGS_MAX_FRAME_SIZE of ours is outside the range RFC 9113 4.2 allows"
            );
    }
}

UTF_AUTO_TEST_CASE( Session_SettingsTimeoutTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * An unacknowledged SETTINGS of ours times out as SETTINGS_TIMEOUT (6.5.3, design 4.5)
     */

    {
        Session session( StreamRole::Client, now );

        ( void ) produceText( session, now );

        session.onTimer( now + time::seconds( 9 ) );

        UTF_REQUIRE( ! session.isClosed() );

        session.onTimer( now + time::seconds( 10 ) );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_SETTINGS_TIMEOUT );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::ConnectionError );
        UTF_REQUIRE_EQUAL( events[ 0 ].errorCode.value(), Globals::ERROR_CODE_SETTINGS_TIMEOUT );

        /*
         * And the GOAWAY is queued, because the peer is owed one
         */

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_GOAWAY ), 1U );
    }

    /*
     * The ack is what stops the timer
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        UTF_REQUIRE_EQUAL( session.unacknowledgedSettingsCount(), 0U );

        session.onTimer( now + time::seconds( 600 ) );

        UTF_REQUIRE( ! session.isClosed() );
    }

    /*
     * The bound is configurable, like every other row of design 4.6
     */

    {
        SessionLimits limits;

        limits.settingsTimeoutInSeconds = 2U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        session.onTimer( now + time::seconds( 2 ) );

        UTF_REQUIRE( session.isClosed() );
    }
}

UTF_AUTO_TEST_CASE( Session_StreamErrorOnClosedStreamTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * CONTRACT 4. The peer resets a stream and then, because its frames were already in flight,
     * sends DATA on it. RFC 9113 5.1 calls that a stream error of type STREAM_CLOSED and 5.4.2
     * says a stream error is answered with RST_STREAM - but 5.1 also forbids sending anything but
     * PRIORITY on a closed stream, so the two cannot both be obeyed. The engine IGNORES it: no
     * RST_STREAM goes out, the connection lives, and answering the peer's reset with our own
     * cannot turn into a loop
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( true /* hasBody */ ) );

        ( void ) produceText( session, now );

        feedText( session, rstStreamFrame( streamId, Globals::ERROR_CODE_CANCEL ), now );

        const auto reset = drain( session );

        UTF_REQUIRE_EQUAL( reset.size(), 1U );
        UTF_REQUIRE( reset[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL( reset[ 0 ].errorCode.value(), Globals::ERROR_CODE_CANCEL );

        const auto before = session.connectionReceiveWindow();

        /*
         * Enough to carry the credit past the half-window threshold, in frames no larger than the
         * 16384 we advertised, so that the WINDOW_UPDATE which proves the octets came back is
         * actually due
         */

        const std::string chunk( 16384U, 'z' );

        feedText( session, dataFrame( streamId, chunk, false ), now );
        feedText( session, dataFrame( streamId, chunk, false ), now );

        UTF_REQUIRE( ! session.isClosed() );
        UTF_REQUIRE( ! session.hasEvents() );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_RST_STREAM ), 0U );

        /*
         * The octets still spent the CONNECTION window and were credited straight back, or the
         * window leaks a little on every reset stream until the connection stalls for good
         */

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_WINDOW_UPDATE ), 1U );
        UTF_REQUIRE_EQUAL( session.connectionReceiveWindow(), before );
    }

    /*
     * Half-closed (remote) has no such problem - RST_STREAM may be sent there, so the same stream
     * error IS answered
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( true /* hasBody */ ) );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), true, true ), now );

        ( void ) drain( session );
        ( void ) produceText( session, now );

        feedText( session, dataFrame( streamId, "late", false ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_RST_STREAM ), 1U );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL( events[ 0 ].errorCode.value(), Globals::ERROR_CODE_STREAM_CLOSED );
    }

    /*
     * A frame arriving after the peer ended the stream is the one place where STREAM_CLOSED is a
     * CONNECTION error (5.1), and that one is not ignored
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), true, true ), now );

        ( void ) drain( session );

        feedText( session, dataFrame( streamId, "after", false ), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_STREAM_CLOSED );
    }
}

UTF_AUTO_TEST_CASE( Session_MessageValidationTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * Every rejection of RFC 9113 8.1 to 8.3 is a STREAM error of type PROTOCOL_ERROR and never a
     * connection error - one malformed response must not take every other request on the
     * connection down with it
     */

    struct Script
    {
        const char*     name;
        const char*     value;
    };

    static const Script scripts[] =
    {
        { "Uppercase-Name",     "x"             },
        { "x-bad",              "line\r\nbreak" },
        { "x-bad",              " padded"       },
        { "x-bad",              "padded\t"      },
        { "connection",         "keep-alive"    },
        { "keep-alive",         "timeout=5"     },
        { "proxy-connection",   "keep-alive"    },
        { "transfer-encoding",  "chunked"       },
        { "upgrade",            "websocket"     },
        { "te",                 "gzip"          },
        { "content-length",     "not-a-number"  },
    };

    for( std::size_t i = 0U; i < sizeof( scripts ) / sizeof( scripts[ 0 ] ); ++i )
    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        const auto block =
            rawLiteral( ":status", "200" ) +
            rawLiteral( scripts[ i ].name, scripts[ i ].value );

        feedText( session, headersFrame( streamId, block, true, true ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 0 ].errorCode.value(),
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );
        UTF_REQUIRE( ! events[ 0 ].reason.empty() );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_RST_STREAM ), 1U );
    }

    /*
     * The pseudo-header rules of 8.3, which need a whole list rather than one field
     */

    {
        std::vector< HpackFieldList > malformed;

        {
            HpackFieldList list;

            list.push_back( field( "x-regular", "v" ) );
            list.push_back( field( ":status", "200" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( ":status", "200" ) );
            list.push_back( field( ":status", "201" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( ":status", "20" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( ":status", "2xx" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( ":status", "101" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( ":method", "GET" ) );

            malformed.push_back( list );
        }

        {
            HpackFieldList list;

            list.push_back( field( "x-no-status", "v" ) );

            malformed.push_back( list );
        }

        for( std::size_t i = 0U; i < malformed.size(); ++i )
        {
            Session session( StreamRole::Client, now );
            PeerEncoder peer;

            settle( session, now );

            const auto streamId = session.submitRequest( makeRequest() );

            ( void ) produceText( session, now );

            feedText(
                session,
                headersFrame( streamId, peer.encode( malformed[ i ] ), true, true ),
                now
                );

            UTF_REQUIRE( ! session.isClosed() );

            const auto events = drain( session );

            UTF_REQUIRE_EQUAL( events.size(), 1U );
            UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
            UTF_REQUIRE_EQUAL(
                events[ 0 ].errorCode.value(),
                Globals::ERROR_CODE_PROTOCOL_ERROR
                );
        }
    }

    /*
     * A trailer section carries no pseudo-headers and MUST end the stream (8.1)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );

        ( void ) drain( session );

        HpackFieldList trailers;

        trailers.push_back( field( ":status", "200" ) );

        feedText( session, headersFrame( streamId, peer.encode( trailers ), true, true ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 0 ].errorCode.value(),
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );
    }

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );

        ( void ) drain( session );

        HpackFieldList trailers;

        trailers.push_back( field( "x-checksum", "abc" ) );

        feedText(
            session,
            headersFrame( streamId, peer.encode( trailers ), false /* endStream */, true ),
            now
            );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
    }

    /*
     * An informational response which ends the stream leaves no final response - malformed (8.1)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "100" ), true, true ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
    }

    /*
     * content-length and the DATA total must agree (8.1.1) - both too little and too much
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "content-length", "10" ) );

        feedText(
            session,
            headersFrame( streamId, peer.response( "200", extra ), false, true ),
            now
            );

        feedText( session, dataFrame( streamId, "short", true ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 3U );
        UTF_REQUIRE( events[ 2 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 2 ].errorCode.value(),
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );
        UTF_REQUIRE( ! events[ 2 ].isMessageComplete );
    }

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "content-length", "2" ) );

        feedText(
            session,
            headersFrame( streamId, peer.response( "200", extra ), false, true ),
            now
            );

        feedText( session, dataFrame( streamId, "far too much", false ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 1 ].errorCode.value(),
            Globals::ERROR_CODE_PROTOCOL_ERROR
            );
    }

    /*
     * A 204 and a 304 are defined to carry no content, so a content-length on one is not checked
     * against a DATA total of zero (RFC 9110 8.6) - the one exception, and the one a naive
     * implementation gets wrong against real servers
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "content-length", "1234" ) );

        feedText(
            session,
            headersFrame( streamId, peer.response( "304", extra ), true, true ),
            now
            );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL( events[ 1 ].errorCode.value(), Globals::ERROR_CODE_NO_ERROR );
        UTF_REQUIRE( events[ 1 ].isMessageComplete );
    }

    /*
     * te: trailers is the one connection-specific field HTTP/2 still permits (8.2.2)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "te", "trailers" ) );

        feedText(
            session,
            headersFrame( streamId, peer.response( "200", extra ), true, true ),
            now
            );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE( events[ 1 ].isMessageComplete );
    }
}

UTF_AUTO_TEST_CASE( Session_EarlyResponseTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * RFC 9113 8.1 - a complete response followed by RST_STREAM( NO_ERROR ) while we are still
     * uploading is a SUCCESS. We stop sending, and the closure says the message was complete,
     * which is what tells this apart from a peer resetting an unfinished exchange
     */

    Session session( StreamRole::Client, now );
    PeerEncoder peer;

    settle( session, now );

    const auto streamId = session.submitRequest( makeRequest( true /* hasBody */ ) );

    ( void ) produceText( session, now );

    session.provideBody( streamId, std::string( 100U, 'a' ), false /* endStream */ );

    HpackFieldList extra;

    extra.push_back( field( "content-length", "3" ) );

    feedText(
        session,
        headersFrame( streamId, peer.response( "413", extra ), false, true ),
        now
        );

    feedText( session, dataFrame( streamId, "no!", true ), now );

    ( void ) drain( session );

    feedText( session, rstStreamFrame( streamId, Globals::ERROR_CODE_NO_ERROR ), now );

    const auto events = drain( session );

    UTF_REQUIRE_EQUAL( events.size(), 1U );
    UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
    UTF_REQUIRE_EQUAL( events[ 0 ].errorCode.value(), Globals::ERROR_CODE_NO_ERROR );
    UTF_REQUIRE( events[ 0 ].isMessageComplete );
    UTF_REQUIRE( ! events[ 0 ].isRetryable );

    /*
     * And the body we were still holding is dropped rather than written to a stream which is gone
     */

    UTF_REQUIRE( ! session.wantsWrite() );

    const auto out = produceText( session, now );

    UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_DATA ), 0U );
}

UTF_AUTO_TEST_CASE( Session_FlowControlTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * A WINDOW_UPDATE is sent when the CONSUMER took the bytes, never when they arrived - which is
     * the backpressure mechanism of the whole client (design 4.4, 5.3)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );

        /*
         * 40000 octets, in frames no larger than the 16384 we advertised - one frame of 40000
         * would be a FRAME_SIZE_ERROR before any of this was reached
         */

        feedText( session, dataFrame( streamId, std::string( 16384U, 'd' ), false ), now );
        feedText( session, dataFrame( streamId, std::string( 16384U, 'd' ), false ), now );
        feedText( session, dataFrame( streamId, std::string( 7232U, 'd' ), false ), now );

        ( void ) drain( session );

        /*
         * Arrived, not consumed - so nothing is advertised back and the window has really shrunk
         */

        UTF_REQUIRE_EQUAL(
            countFrames( produceText( session, now ), Globals::FRAME_TYPE_WINDOW_UPDATE ),
            0U
            );

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( 65535 - 40000 )
            );

        /*
         * Below the half-window threshold nothing goes out yet
         */

        session.consumed( streamId, 1000U );

        UTF_REQUIRE_EQUAL(
            countFrames( produceText( session, now ), Globals::FRAME_TYPE_WINDOW_UPDATE ),
            0U
            );

        session.consumed( streamId, 39000U );

        const auto out = produceText( session, now );

        /*
         * One for the stream and one for the connection, both past their thresholds now
         */

        UTF_REQUIRE_EQUAL(
            countFrames( out, Globals::FRAME_TYPE_WINDOW_UPDATE ),
            2U
            );

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( Globals::INITIAL_WINDOW_SIZE_DEFAULT )
            );
    }

    /*
     * The padding of a DATA frame spends the window and is credited back at once, because nobody
     * will ever consume it - consumed( ) is about the octets the caller actually received
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );

        std::string padded;

        padded.push_back( static_cast< char >( 4 ) );
        padded.append( "body" );
        padded.append( 4U, '\0' );

        feedText(
            session,
            makeFrame( Globals::FRAME_TYPE_DATA, Globals::FRAME_FLAG_PADDED, streamId, padded ),
            now
            );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE_EQUAL( events[ 1 ].data, std::string( "body" ) );

        /*
         * Nine octets arrived, of which five were padding and its length - the window is down by
         * all nine, and five of them have already been given back
         */

        UTF_REQUIRE_EQUAL(
            session.streamReceiveWindow( streamId ),
            static_cast< std::int32_t >( 65535 - 9 )
            );

        session.consumed( streamId, 4U );

        UTF_REQUIRE( ! session.isClosed() );
    }

    /*
     * A peer which overruns the STREAM window gets a stream error and the connection survives it
     */

    {
        Http2Profile small;

        small.settings.push_back( setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 100U ) );

        Session session( StreamRole::Client, now, small );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        UTF_REQUIRE_EQUAL( session.streamReceiveWindow( streamId ), 100 );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );
        feedText( session, dataFrame( streamId, std::string( 200U, 'x' ), false ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 2U );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 1 ].errorCode.value(),
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR
            );
    }

    /*
     * A peer which overruns the CONNECTION window ends the connection (6.9.1)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto first = session.submitRequest( makeRequest() );
        const auto second = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( first, peer.response( "200" ), false, true ), now );
        feedText( session, headersFrame( second, peer.response( "200" ), false, true ), now );

        /*
         * Split across two streams, so that it is unambiguously the CONNECTION window which runs
         * out: neither stream has spent more than half of its own
         */

        const std::string chunk( 16384U, 'x' );

        feedText( session, dataFrame( first, chunk, false ), now );
        feedText( session, dataFrame( first, chunk, false ), now );
        feedText( session, dataFrame( second, chunk, false ), now );

        UTF_REQUIRE( ! session.isClosed() );

        feedText( session, dataFrame( second, chunk, false ), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_FLOW_CONTROL_ERROR
            );
    }
}

UTF_AUTO_TEST_CASE( Session_WriteSchedulingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * CONTROL FRAMES PRECEDE EVERYTHING. A PING arriving while a request and a body are waiting to
     * go out is acknowledged first, because an acknowledgement a peer is waiting for must not
     * queue behind a megabyte of upload
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( true /* hasBody */ ) );

        session.provideBody( streamId, std::string( 100U, 'b' ), true /* endStream */ );

        feedText( session, pingFrame( "12345678", false ), now );

        const auto out = produceText( session, now );
        const auto types = frameTypes( out );

        UTF_REQUIRE_EQUAL( types.size(), 3U );
        UTF_REQUIRE_EQUAL( types[ 0 ], Globals::FRAME_TYPE_PING );
        UTF_REQUIRE_EQUAL( types[ 1 ], Globals::FRAME_TYPE_HEADERS );
        UTF_REQUIRE_EQUAL( types[ 2 ], Globals::FRAME_TYPE_DATA );
    }

    /*
     * A HEADER BLOCK AND ITS CONTINUATION FRAMES ARE ONE UNIT. A block larger than the peer's
     * SETTINGS_MAX_FRAME_SIZE is split, and nothing may appear between the pieces - which here is
     * structural rather than a rule to remember, since the whole block is one element of the queue
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        auto request = makeRequest();

        request.headers.append( "x-large", std::string( 100000U, 'h' ) );

        const auto streamId = session.submitRequest( request );

        UTF_REQUIRE_EQUAL( streamId, 1U );

        session.ping( reinterpret_cast< const std::uint8_t* >( "87654321" ) );

        const auto out = produceText( session, now );
        const auto types = frameTypes( out );

        UTF_REQUIRE( types.size() >= 4U );

        UTF_REQUIRE_EQUAL( types[ 0 ], Globals::FRAME_TYPE_PING );
        UTF_REQUIRE_EQUAL( types[ 1 ], Globals::FRAME_TYPE_HEADERS );

        for( std::size_t i = 2U; i < types.size(); ++i )
        {
            UTF_REQUIRE_EQUAL( types[ i ], Globals::FRAME_TYPE_CONTINUATION );
        }

        /*
         * END_HEADERS is clear on the HEADERS and on every CONTINUATION but the last
         */

        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( out[ 9U + 8U + 4U ] ) &
                Globals::FRAME_FLAG_END_HEADERS,
            0U
            );
    }

    /*
     * DATA IS SCHEDULED BY RFC 9218 URGENCY, lowest number first
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        auto background = makeRequest( true );

        background.priority.urgency = 6U;
        background.priority.incremental = true;

        auto urgent = makeRequest( true );

        urgent.priority.urgency = 1U;
        urgent.priority.incremental = true;

        const auto backgroundId = session.submitRequest( background );
        const auto urgentId = session.submitRequest( urgent );

        ( void ) produceText( session, now );

        session.provideBody( backgroundId, std::string( 10U, 'b' ), true );
        session.provideBody( urgentId, std::string( 10U, 'u' ), true );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_DATA ), 2U );

        /*
         * The urgent stream's frame is first although its identifier is higher
         */

        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( out[ 8 ] ),
            static_cast< std::uint8_t >( urgentId )
            );

        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( out[ 9 + 10 + 8 ] ),
            static_cast< std::uint8_t >( backgroundId )
            );
    }

    /*
     * Within one urgency band the incremental streams share it round-robin, so a long upload
     * cannot starve its peers
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        auto shared = makeRequest( true );

        shared.priority.incremental = true;

        const auto first = session.submitRequest( shared );
        const auto second = session.submitRequest( shared );

        ( void ) produceText( session, now );

        session.provideBody( first, std::string( 10U, 'a' ), false );
        session.provideBody( second, std::string( 10U, 'b' ), false );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_DATA ), 2U );
        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( out[ 8 ] ),
            static_cast< std::uint8_t >( first )
            );
        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( out[ 9 + 10 + 8 ] ),
            static_cast< std::uint8_t >( second )
            );

        session.provideBody( first, std::string( 10U, 'a' ), false );
        session.provideBody( second, std::string( 10U, 'b' ), false );

        const auto again = produceText( session, now );

        /*
         * The cursor left off at the second stream, so the wrap puts the first one next
         */

        UTF_REQUIRE_EQUAL(
            static_cast< std::uint8_t >( again[ 8 ] ),
            static_cast< std::uint8_t >( first )
            );
    }

    /*
     * BODIES ARE PULLED. bodyBytesWanted( ) is what a caller may hand over, and it is bounded by
     * both windows and by the peer's maximum frame size - so an upload never buffers ahead of the
     * peer, whatever the caller has ready
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( true ) );

        ( void ) produceText( session, now );

        UTF_REQUIRE_EQUAL(
            session.bodyBytesWanted( streamId ),
            static_cast< std::size_t >( Globals::MAX_FRAME_SIZE_DEFAULT )
            );

        session.provideBody(
            streamId,
            std::string( static_cast< std::size_t >( Globals::MAX_FRAME_SIZE_DEFAULT ), 'x' ),
            false
            );

        UTF_REQUIRE_EQUAL( session.bodyBytesWanted( streamId ), 0U );

        UTF_REQUIRE_THROW_MESSAGE(
            session.provideBody( streamId, "one too many", false ),
            UnexpectedException,
            "More HTTP/2 body was provided than the session asked for"
            );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_DATA ), 1U );

        /*
         * 16384 octets of the 65535 window are gone, and the peer's maximum frame size still caps
         * what may be handed over next
         */

        UTF_REQUIRE_EQUAL(
            session.streamSendWindow( streamId ),
            static_cast< std::int32_t >( 65535 - 16384 )
            );

        UTF_REQUIRE_EQUAL(
            session.bodyBytesWanted( streamId ),
            static_cast< std::size_t >( Globals::MAX_FRAME_SIZE_DEFAULT )
            );
    }

    /*
     * A shut window stops the pull dead, and the peer's WINDOW_UPDATE is what reopens it
     */

    {
        Http2Profile tiny;

        Session session( StreamRole::Client, now, tiny );

        settle(
            session,
            now,
            std::vector< Http2Setting >(
                1U,
                setting( Globals::SETTINGS_INITIAL_WINDOW_SIZE, 10U )
                )
            );

        const auto streamId = session.submitRequest( makeRequest( true ) );

        ( void ) produceText( session, now );

        UTF_REQUIRE_EQUAL( session.bodyBytesWanted( streamId ), 10U );

        session.provideBody( streamId, "0123456789", false );

        ( void ) produceText( session, now );

        UTF_REQUIRE_EQUAL( session.bodyBytesWanted( streamId ), 0U );
        UTF_REQUIRE( ! session.wantsWrite() );

        feedText( session, windowUpdateFrame( streamId, 5U ), now );

        UTF_REQUIRE_EQUAL( session.bodyBytesWanted( streamId ), 5U );

        /*
         * Even with the window shut the caller can always say that the body is over
         */

        session.provideBody( streamId, std::string(), true /* endStream */ );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_DATA ), 1U );
    }
}

UTF_AUTO_TEST_CASE( Session_LimitsTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * Design 4.6, row by row. Every one is configurable and every breach is stated here, because a
     * limit which is never exercised is a limit which is wrong
     */

    {
        UTF_REQUIRE_EQUAL(
            SessionLimits().maxCompressedHeaderBlockSize,
            static_cast< std::uint32_t >( 256U * 1024U )
            );

        UTF_REQUIRE_EQUAL(
            SessionLimits().maxContinuationFramesPerBlock,
            static_cast< std::uint32_t >( 64 )
            );

        UTF_REQUIRE_EQUAL(
            SessionLimits().maxQueuedControlFrameBytes,
            static_cast< std::uint32_t >( 64U * 1024U )
            );

        UTF_REQUIRE_EQUAL(
            SessionLimits().maxInboundPingAndSettingsPerSecond,
            static_cast< std::uint32_t >( 100 )
            );

        UTF_REQUIRE_EQUAL(
            SessionLimits().maxRememberedClosedStreams,
            static_cast< std::uint32_t >( 1000 )
            );

        UTF_REQUIRE_EQUAL(
            SessionLimits().rememberedClosedStreamTimeoutInSeconds,
            static_cast< std::uint32_t >( 30 )
            );
    }

    /*
     * Compressed header-block octets - a connection error with ENHANCE_YOUR_CALM
     */

    {
        SessionLimits limits;

        limits.maxCompressedHeaderBlockSize = 64U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList big;

        big.push_back( field( ":status", "200" ) );
        big.push_back( field( "x-big", std::string( 200U, 'v' ) ) );

        feedText( session, headersFrame( streamId, peer.encode( big ), true, true ), now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );
    }

    /*
     * CONTINUATION frames per block
     */

    {
        SessionLimits limits;

        limits.maxContinuationFramesPerBlock = 2U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        const auto block = peer.response( "200" );

        std::string script = headersFrame( streamId, std::string(), false, false );

        for( std::size_t i = 0U; i < 3U; ++i )
        {
            script += continuationFrame( streamId, std::string(), false );
        }

        script += continuationFrame( streamId, block, true );

        feedText( session, script, now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );
    }

    /*
     * Control-frame octets owed to a peer which will not read them
     */

    {
        SessionLimits limits;

        limits.maxQueuedControlFrameBytes = 100U;
        limits.maxInboundPingAndSettingsPerSecond = 1000U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        settle( session, now );

        std::string script;

        for( std::size_t i = 0U; i < 20U; ++i )
        {
            script += pingFrame( "12345678", false );
        }

        feedText( session, script, now );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );
    }

    /*
     * Inbound PING and SETTINGS per second, with the window sliding
     */

    {
        SessionLimits limits;

        limits.maxInboundPingAndSettingsPerSecond = 3U;
        limits.maxQueuedControlFrameBytes = 1024U * 1024U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        /*
         * SETTINGS counts toward the same budget as PING, so settling has already spent two of
         * the three - the window below starts a second later, where the count is its own
         */

        settle( session, now );

        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 1 ) );
        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 1 ) );
        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 1 ) );

        UTF_REQUIRE( ! session.isClosed() );

        /*
         * A second later the count starts again, so a steady trickle is never refused
         */

        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 2 ) );
        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 2 ) );
        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 2 ) );

        UTF_REQUIRE( ! session.isClosed() );

        feedText( session, pingFrame( "12345678", false ), now + time::seconds( 2 ) );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL(
            session.connectionErrorCode(),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );
    }

    /*
     * The decoded header list size - the first row of the table, and the only one which is a
     * STREAM reset rather than a connection error, because the block was still consumed to the
     * last octet and the dynamic table is in step
     */

    {
        Http2Profile bounded;

        bounded.settings.push_back( setting( Globals::SETTINGS_MAX_HEADER_LIST_SIZE, 80U ) );

        Session session( StreamRole::Client, now, bounded );
        PeerEncoder peer;

        settle( session, now );

        const auto first = session.submitRequest( makeRequest() );
        const auto second = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        HpackFieldList big;

        big.push_back( field( ":status", "200" ) );
        big.push_back( field( "x-long", std::string( 120U, 'v' ) ) );

        feedText( session, headersFrame( first, peer.encode( big ), true, true ), now );

        UTF_REQUIRE( ! session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL(
            events[ 0 ].errorCode.value(),
            Globals::ERROR_CODE_ENHANCE_YOUR_CALM
            );

        /*
         * And the connection is still good: the table tracked the block we discarded, so a block
         * which indexes what it added still decodes
         */

        feedText( session, headersFrame( second, peer.encode( big ), true, true ), now );

        UTF_REQUIRE( ! session.isClosed() );
    }

    /*
     * The two rows which are deliberately NOT the session's - the buffered response body cap
     * belongs to the request task (design 5.3) and the retry budget to the pool (D6). What the
     * engine owes the second is the retryable flag, which Session_GoAwayAndRetryTests pins
     */
}

UTF_AUTO_TEST_CASE( Session_PushPromiseAndPingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * D11 - server push is disabled, we say so on every connection, and a PUSH_PROMISE which
     * arrives anyway is a connection error (8.4)
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        const auto payload = uint32Octets( 2U ) + peer.response( "200" );

        feedText(
            session,
            makeFrame(
                Globals::FRAME_TYPE_PUSH_PROMISE,
                Globals::FRAME_FLAG_END_HEADERS,
                streamId,
                payload
                ),
            now
            );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_PROTOCOL_ERROR );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( countFrames( out, Globals::FRAME_TYPE_GOAWAY ), 1U );
    }

    /*
     * A server enabling push is a connection error too - 6.5.2 says a client must treat
     * SETTINGS_ENABLE_PUSH set to anything but zero that way
     */

    {
        Session session( StreamRole::Client, now );

        ( void ) produceText( session, now );

        feedText(
            session,
            settingsFrame(
                std::vector< Http2Setting >(
                    1U,
                    setting( Globals::SETTINGS_ENABLE_PUSH, 1U )
                    )
                ),
            now
            );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_PROTOCOL_ERROR );
    }

    /*
     * A PING is echoed back unexamined (6.7), and an acknowledgement of ours is an event
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        feedText( session, pingFrame( "opaque!!", false ), now );

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL( out, pingFrame( "opaque!!", true ) );

        session.ping( reinterpret_cast< const std::uint8_t* >( "mine!!!!" ) );

        UTF_REQUIRE_EQUAL( produceText( session, now ), pingFrame( "mine!!!!", false ) );

        feedText( session, pingFrame( "mine!!!!", true ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::PingAcknowledged );
        UTF_REQUIRE_EQUAL( events[ 0 ].pingData, std::string( "mine!!!!" ) );
    }

    /*
     * The peer's SETTINGS are applied on receipt and acknowledged (6.5.3), and an identifier we do
     * not interpret survives being read - the Safari shape of design 6.4 carries two of them
     */

    {
        Session session( StreamRole::Client, now );

        ( void ) produceText( session, now );

        std::vector< Http2Setting > peerSettings;

        peerSettings.push_back( setting( Globals::SETTINGS_MAX_CONCURRENT_STREAMS, 100U ) );
        peerSettings.push_back( setting( Globals::SETTINGS_MAX_FRAME_SIZE, 32768U ) );
        peerSettings.push_back( setting( 0x08U, 1U ) );
        peerSettings.push_back( setting( 0x09U, 1U ) );

        feedText( session, settingsFrame( peerSettings ), now );

        UTF_REQUIRE( session.peerLimitsConcurrentStreams() );
        UTF_REQUIRE_EQUAL( session.peerMaxConcurrentStreams(), 100U );
        UTF_REQUIRE_EQUAL( session.peerMaxFrameSize(), 32768U );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::SettingsReceived );
        UTF_REQUIRE_EQUAL( events[ 0 ].settings.size(), 4U );
        UTF_REQUIRE_EQUAL( events[ 0 ].settings[ 2 ].id.value(), static_cast< std::uint16_t >( 8 ) );

        UTF_REQUIRE_EQUAL( produceText( session, now ), settingsAckFrame() );
    }

    /*
     * A peer's SETTINGS_MAX_FRAME_SIZE outside the range of 4.2 is a connection error
     */

    {
        Session session( StreamRole::Client, now );

        ( void ) produceText( session, now );

        feedText(
            session,
            settingsFrame(
                std::vector< Http2Setting >(
                    1U,
                    setting( Globals::SETTINGS_MAX_FRAME_SIZE, 1024U )
                    )
                ),
            now
            );

        UTF_REQUIRE( session.isClosed() );
        UTF_REQUIRE_EQUAL( session.connectionErrorCode(), Globals::ERROR_CODE_PROTOCOL_ERROR );
    }

    /*
     * The peer's SETTINGS_HEADER_TABLE_SIZE bounds OUR encoder, which is the mirror of contract 2
     */

    {
        Http2Profile profile;

        profile.hpackEncoderTableSize = 65536U;

        Session session( StreamRole::Client, now, profile );

        UTF_REQUIRE_EQUAL(
            session.hpackEncoderTableCapacity(),
            static_cast< std::size_t >( 65536 )
            );

        ( void ) produceText( session, now );

        feedText(
            session,
            settingsFrame(
                std::vector< Http2Setting >(
                    1U,
                    setting( Globals::SETTINGS_HEADER_TABLE_SIZE, 4096U )
                    )
                ),
            now
            );

        UTF_REQUIRE_EQUAL( session.peerHeaderTableSize(), 4096U );

        /*
         * The new capacity is signalled at the start of the next block, which is RFC 7541 4.2
         */

        ( void ) session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        UTF_REQUIRE_EQUAL(
            session.hpackEncoderTableCapacity(),
            static_cast< std::size_t >( 4096 )
            );
    }
}

UTF_AUTO_TEST_CASE( Session_GoAwayAndRetryTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * D6 - a stream the peer provably did not process may be replayed, and the retryable flag on
     * the closure is what tells the pool so. A GOAWAY names the last identifier it acted on;
     * everything above it was not processed
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto first = session.submitRequest( makeRequest() );
        const auto second = session.submitRequest( makeRequest() );
        const auto third = session.submitRequest( makeRequest() );

        UTF_REQUIRE_EQUAL( first, 1U );
        UTF_REQUIRE_EQUAL( second, 3U );
        UTF_REQUIRE_EQUAL( third, 5U );

        ( void ) produceText( session, now );

        feedText( session, goAwayFrame( 1U, Globals::ERROR_CODE_NO_ERROR, "bye" ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 3U );

        UTF_REQUIRE( events[ 0 ].type == SessionEventType::GoAwayReceived );
        UTF_REQUIRE_EQUAL( events[ 0 ].lastStreamId.value(), 1U );
        UTF_REQUIRE_EQUAL( events[ 0 ].debugData, std::string( "bye" ) );

        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL( events[ 1 ].streamId.value(), 3U );
        UTF_REQUIRE( events[ 1 ].isRetryable );

        UTF_REQUIRE( events[ 2 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE_EQUAL( events[ 2 ].streamId.value(), 5U );
        UTF_REQUIRE( events[ 2 ].isRetryable );

        /*
         * Stream 1 is at or below the last identifier, so it is left alone and the peer is still
         * answering it
         */

        UTF_REQUIRE_EQUAL( session.activeStreamCount(), 1U );
        UTF_REQUIRE( session.isDraining() );
        UTF_REQUIRE( session.goAwayReceived() );

        feedText( session, headersFrame( first, peer.response( "200" ), true, true ), now );

        const auto finished = drain( session );

        UTF_REQUIRE_EQUAL( finished.size(), 2U );
        UTF_REQUIRE( finished[ 0 ].type == SessionEventType::Headers );
        UTF_REQUIRE( finished[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE( ! finished[ 1 ].isRetryable );
    }

    /*
     * REFUSED_STREAM says outright that the peer did not process it
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, rstStreamFrame( streamId, Globals::ERROR_CODE_REFUSED_STREAM ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( events[ 0 ].isRetryable );
    }

    /*
     * Once any part of a response has arrived the peer has evidently acted on the request, so a
     * reset after that is NOT retryable however it is spelt
     */

    {
        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( true ) );

        ( void ) produceText( session, now );

        feedText( session, headersFrame( streamId, peer.response( "200" ), false, true ), now );

        ( void ) drain( session );

        feedText( session, rstStreamFrame( streamId, Globals::ERROR_CODE_REFUSED_STREAM ), now );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 1U );
        UTF_REQUIRE( ! events[ 0 ].isRetryable );
        UTF_REQUIRE( ! events[ 0 ].isMessageComplete );
    }

    /*
     * A connection error closes every live stream before it reports itself, so a caller draining
     * in order learns what happened to each request first
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        ( void ) session.submitRequest( makeRequest() );
        ( void ) session.submitRequest( makeRequest() );

        ( void ) produceText( session, now );

        feedText( session, makeFrame( Globals::FRAME_TYPE_DATA, 0U, 0U, "x" ), now );

        UTF_REQUIRE( session.isClosed() );

        const auto events = drain( session );

        UTF_REQUIRE_EQUAL( events.size(), 3U );
        UTF_REQUIRE( events[ 0 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE( events[ 1 ].type == SessionEventType::StreamClosed );
        UTF_REQUIRE( events[ 2 ].type == SessionEventType::ConnectionError );
        UTF_REQUIRE_EQUAL( events[ 2 ].errorCode.value(), Globals::ERROR_CODE_PROTOCOL_ERROR );

        /*
         * Nothing more is parsed afterwards, and what is queued is still worth writing
         */

        feedText( session, pingFrame( "12345678", false ), now );

        UTF_REQUIRE( ! session.hasEvents() );

        UTF_REQUIRE_EQUAL(
            countFrames( produceText( session, now ), Globals::FRAME_TYPE_GOAWAY ),
            1U
            );
    }

    /*
     * Our own GOAWAY names the highest peer-initiated stream we might have acted on, which for a
     * client is none at all
     */

    {
        Session session( StreamRole::Client, now );

        settle( session, now );

        session.goAway();

        const auto out = produceText( session, now );

        UTF_REQUIRE_EQUAL(
            out,
            goAwayFrame( 0U, Globals::ERROR_CODE_NO_ERROR )
            );

        UTF_REQUIRE( session.isDraining() );
    }
}

UTF_AUTO_TEST_CASE( Session_ProfileShapingTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * The pseudo-header order is the profile's, because it is the 'm,a,s,p' component of the
     * conventional HTTP/2 fingerprint (design 6.4). Here it is read back by decoding what the
     * session actually put on the wire, which is the only statement worth making about it
     */

    Http2Profile profile;

    profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Method );
    profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Path );
    profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Authority );
    profile.pseudoHeaderOrder.push_back( Http2PseudoHeader::Scheme );

    Session session( StreamRole::Client, now, profile );

    settle( session, now );

    auto request = makeRequest();

    request.headers.append( "User-Agent", "probe/1.0" );

    ( void ) session.submitRequest( request );

    const auto out = produceText( session, now );

    UTF_REQUIRE_EQUAL( frameTypes( out ).size(), 1U );

    const auto block = out.substr( 9U );

    HpackDecoder decoder;
    HpackFieldList decoded;

    UTF_REQUIRE(
        decoder.decode(
            block.data(),
            block.size(),
            ( std::numeric_limits< std::size_t >::max )(),
            decoded
            ) == HpackDecoder::Outcome::Complete
        );

    UTF_REQUIRE_EQUAL( decoded.size(), 5U );
    UTF_REQUIRE_EQUAL( decoded[ 0 ].name(), std::string( ":method" ) );
    UTF_REQUIRE_EQUAL( decoded[ 1 ].name(), std::string( ":path" ) );
    UTF_REQUIRE_EQUAL( decoded[ 2 ].name(), std::string( ":authority" ) );
    UTF_REQUIRE_EQUAL( decoded[ 3 ].name(), std::string( ":scheme" ) );

    /*
     * And the caller's spelling is folded to lowercase by the encoder, as RFC 9113 8.2.1 requires
     */

    UTF_REQUIRE_EQUAL( decoded[ 4 ].name(), std::string( "user-agent" ) );
    UTF_REQUIRE_EQUAL( decoded[ 4 ].value(), std::string( "probe/1.0" ) );

    /*
     * A profile's WINDOW_UPDATE threshold replaces the half-window default of design 4.4
     */

    {
        Http2Profile eager;

        eager.windowUpdateThreshold = 1U;

        Session eagerSession( StreamRole::Client, now, eager );
        PeerEncoder peer;

        settle( eagerSession, now );

        const auto streamId = eagerSession.submitRequest( makeRequest() );

        ( void ) produceText( eagerSession, now );

        feedText(
            eagerSession,
            headersFrame( streamId, peer.response( "200" ), false, true ),
            now
            );

        feedText( eagerSession, dataFrame( streamId, "tiny", false ), now );

        ( void ) drain( eagerSession );

        eagerSession.consumed( streamId, 4U );

        UTF_REQUIRE_EQUAL(
            countFrames(
                produceText( eagerSession, now ),
                Globals::FRAME_TYPE_WINDOW_UPDATE
                ),
            2U
            );
    }
}

#endif /* __UTEST_TESTSESSION_H_ */
