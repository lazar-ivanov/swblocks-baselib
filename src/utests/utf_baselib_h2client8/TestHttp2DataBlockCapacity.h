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

#ifndef __UTEST_TESTHTTP2DATABLOCKCAPACITY_H_
#define __UTEST_TESTHTTP2DATABLOCKCAPACITY_H_

#include <baselib/http2/Globals.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/RawFrameScriptPeer.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The block a DATA frame is delivered in - decided 2026-09-27, in the first decision round of
 * astra's second review's implementation run
 *
 * This module's one case with a driver in it; the module's main says why it lives here
 */

namespace utest
{
    namespace h2blocks
    {
        typedef bl::om::ObjectImpl
            <
                h2driver::DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
            >
            PlainDriverImpl;

        /**
         * @brief class BlockRecordingSinkT - a RecordingSink which also writes down, for every
         * block delivered, its payload beside its capacity
         *
         * What it adds is recorded under the base's own lock and before the base's onData( ) runs,
         * so the waitForClosed( ) rendezvous - which the base notifies when the stream is closed
         * out, after every onData( ) - orders these records too
         */

        template
        <
            typename E = void
        >
        class BlockRecordingSinkT : public h2driver::RecordingSinkT<>
        {
            BL_DECLARE_OBJECT_IMPL( BlockRecordingSinkT )

        protected:

            typedef h2driver::RecordingSinkT<>                                  base_type;

            std::string                                                         m_blocks;

            BlockRecordingSinkT()
            {
            }

        public:

            virtual void onData(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                const auto payload = data -> size() - data -> offset1();

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_blocks += bl::utils::lexical_cast< std::string >( payload );
                    m_blocks += '/';
                    m_blocks += bl::utils::lexical_cast< std::string >( data -> capacity() );
                    m_blocks += ' ';
                }

                base_type::onData( handle, data );
            }

            /**
             * @brief Every delivered block as 'payload/capacity ', in delivery order
             */

            auto blocks() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_blocks;
            }
        };

        typedef bl::om::ObjectImpl< BlockRecordingSinkT<> > BlockRecordingSink;

    } // h2blocks

} // utest

/**
 * @brief A DATA frame is delivered in a block sized to its payload
 *
 * THE DEFECT. blockOf( ) asked DataBlock::get( ) for max( payload, defaultCapacity( ) ), and
 * defaultCapacity( ) is 1 MiB. With no pool configured - and when this was fixed nothing in the
 * tree set Http2ConnectionConfig::dataBlocksPool, or returned a block to one - every non-empty
 * DATA frame, a one-byte frame included, was a fresh allocation of at least 1 MiB. A stream window
 * of 65,535 octets bounds the payload a stalled sink holds, not the allocations: 65,535 one-byte
 * frames were 64 GiB of capacity.
 *
 * THE RULE, decided 2026-09-27: ask for the payload's size. What the sink is handed does not change
 * - the same bytes, the same size( ) - only the capacity behind them.
 *
 * A REAL DRIVER AND A RAW PEER, because the block is made on the driver's strand and handed
 * straight to the sink, and nothing short of a DATA frame on the wire reaches it. The peer answers
 * with a one-byte frame, a five-byte frame, and an EMPTY frame carrying END_STREAM - which pins the
 * other half of the premise: an empty frame is never delivered, so no block is ever asked for with
 * a size of zero, which DataBlock's constructor would turn into its 1 MiB default. The idle timeout
 * is what ends the connection, which the driver treats as the deliberate ending it is; the peer's
 * socket stays open until then, so no write of the client's can race a close.
 */

UTF_AUTO_TEST_CASE( H2Driver_DataBlockIsSizedToItsPayloadTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;
    using namespace utest::h2blocks;

    const auto frame =
        [](
            SAA_in          const unsigned                                      type,
            SAA_in          const unsigned                                      flags,
            SAA_in          const std::string&                                  payload
            )
            -> std::string
        {
            return h2peer::frameOctets(
                static_cast< std::uint8_t >( type ),
                static_cast< std::uint8_t >( flags ),
                ( http2::Globals::FRAME_TYPE_SETTINGS == type ) ? 0U : 1U /* streamId */,
                payload
                );
        };

    /*
     * THE PEER READS EVERY FRAME THE CLIENT SENDS, AND CLOSES ONLY ON THE CLIENT'S EOF. The client
     * sends exactly four: its SETTINGS and the HEADERS - the request is submitted before the task
     * runs, and the default profile opens no connection window - then the acknowledgement of the
     * peer's SETTINGS, then the GOAWAY of its idle close, after which it closes. A peer which
     * closed with any of them unread would put a reset on the wire and race the client's own reads
     *
     * The response's header block is one octet: HPACK's indexed representation of static table
     * entry 8, which is ':status: 200' (RFC 7541 appendix A)
     */

    h2peer::RawFrameScriptPeer rawPeer(
        h2peer::RawFrameScript()
            .expectPreface()
            .readFrames( 2U )
            .send( frame( http2::Globals::FRAME_TYPE_SETTINGS, 0U, std::string() ) )
            .send(
                frame(
                    http2::Globals::FRAME_TYPE_SETTINGS,
                    http2::Globals::FRAME_FLAG_ACK,
                    std::string()
                    )
                )
            .send(
                frame(
                    http2::Globals::FRAME_TYPE_HEADERS,
                    http2::Globals::FRAME_FLAG_END_HEADERS,
                    std::string( 1U, static_cast< char >( 0x88U ) )
                    )
                )
            .send( frame( http2::Globals::FRAME_TYPE_DATA, 0U, "a" ) )
            .send( frame( http2::Globals::FRAME_TYPE_DATA, 0U, "hello" ) )
            .send(
                frame(
                    http2::Globals::FRAME_TYPE_DATA,
                    http2::Globals::FRAME_FLAG_END_STREAM,
                    std::string()
                    )
                )
            .readFrames( 2U )
            .waitForClose()
        );

    const auto record = std::make_shared< FallbackRecord >();

    Http2ConnectionConfig h2config;

    h2config.idleTimeout = time::milliseconds( 100 );

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", rawPeer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        h2config,
        cleartextHttp2Config()
        );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    const auto sink = BlockRecordingSink::createInstance();

    sink -> setConnection( connection.get() );

    /*
     * Submitted before the task is scheduled, so the HEADERS leave with the preface and stream 1 is
     * open before the peer - which reads the preface first - can answer on it
     */

    const auto handle = connection -> submit(
        h2driver::makeRequest( "http://127.0.0.1/blocks" ),
        om::qi< httpclient::ClientStreamEventSink >( sink )
        );

    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

    runDriver(
        driver,
        [ & ]() -> void
        {
            sink -> waitForClosed();
        }
        );

    sink -> setConnection( nullptr );

    chkTaskSucceeded( om::qi< Task >( driver ) );

    /*
     * The ending was the one arranged: the peer read the client's four frames and then its EOF
     */

    requireRecorded( rawPeer.recorder(), "the client closed the connection" );

    UTF_REQUIRE_EQUAL( sink -> status(), 200U );
    UTF_REQUIRE( ! sink -> errorCode() );

    /*
     * The bytes are exactly what was sent - the fix changes the capacity behind them and nothing
     * the sink reads
     */

    UTF_CHECK_EQUAL( sink -> body(), std::string( "ahello" ) );

    /*
     * Red before the fix: '1/1048576 5/1048576 '. And two blocks, not three: the empty frame which
     * ended the stream was never delivered
     */

    UTF_CHECK_EQUAL( sink -> blocks(), std::string( "1/1 5/5 " ) );
}

#endif /* __UTEST_TESTHTTP2DATABLOCKCAPACITY_H_ */
