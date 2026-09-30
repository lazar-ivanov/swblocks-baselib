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

#ifndef __UTEST_TESTCLIENTSESSIONOUTSTANDINGCAP_H_
#define __UTEST_TESTCLIENTSESSIONOUTSTANDINGCAP_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/Task.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The cap on what a request holds of a body and has not handed on, over a REAL HTTP/1.1 driver - D3
 * of astra's second review ( R02 ), implemented to
 * notes/plans/issues/astra2-cs2-d3-unread-bytes-cap-design.md, section 8, cases 2 and 3
 *
 * HTTP/1.1 is where the cap is the only bound: its driver re-arms the read after every chunk and
 * its consumed( ) is a no-op, so a sink which falls behind used to let the body pile up in the
 * request task for as long as the peer kept sending. Over HTTP/2 the stream window bounds the same
 * bytes first.
 *
 * The session helpers and the scripted peer are TestClientSessionDecoding.h's, which this module's
 * Main.cpp includes ahead of this file.
 */

namespace utest
{
    namespace plainsession
    {
        inline bool isBufferTooSmall( SAA_in const std::exception_ptr& eptr )
        {
            if( ! eptr )
            {
                return false;
            }

            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::BufferTooSmallException& )
            {
                return true;
            }
            catch( std::exception& )
            {
            }

            return false;
        }

        /**
         * @brief A caller's BodySink which either takes nothing, or takes everything and publishes
         * its tally for the peer to pace itself on
         *
         * The tally is updated and signalled inside the same lock, after the bytes are appended, so
         * "the tally reached n" and "the sink took n" are one step ( the review's P8 )
         */

        template
        <
            typename E = void
        >
        class TallySinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( TallySinkT, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;

            const bool                                                          m_takesNothing;

            std::string                                                         m_received;
            std::size_t                                                         m_offers;
            std::size_t                                                         m_completions;

            TallySinkT( SAA_in const bool takesNothing ) NOEXCEPT
                :
                m_takesNothing( takesNothing ),
                m_offers( 0U ),
                m_completions( 0U )
            {
            }

        public:

            virtual std::size_t onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_offers;

                if( m_takesNothing )
                {
                    return 0U;
                }

                const auto offered = data -> size() - data -> offset1();

                m_received.append(
                    reinterpret_cast< const char* >( data -> pv() ) + data -> offset1(),
                    offered
                    );

                m_cv.notify_all();

                return offered;
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_completions;
            }

            bool waitForTaken(
                SAA_in          const std::size_t                               expected,
                SAA_in          const std::size_t                               timeoutInMilliseconds
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this, expected ]() -> bool
                    {
                        return m_received.size() >= expected;
                    }
                    );
            }

            auto received() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_received;
            }

            std::size_t offers() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_offers;
            }

            std::size_t completions() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_completions;
            }
        };

        typedef bl::om::ObjectImpl< TallySinkT<> >                              TallySink;

    } // plainsession

} // utest

/**
 * @brief Case 2 - a sink which takes nothing, over a real HTTP/1.1 driver, is failed at the cap and
 * not handed the body
 *
 * The peer declares a body of 64 MiB and writes it from one reused 64 KiB buffer, so the case
 * allocates nothing large; the cap is 256 KiB; the sink takes nothing. The driver's reads are at
 * most its 64 KiB buffer, so a few blocks in, one crosses the cap.
 *
 * RED BEFORE D3: the whole body was held, and the request failed at the close with the drain's
 * "did not take 67108864 bytes" - the wrong exception, after the very allocation the cap exists to
 * refuse - and the peer wrote all of it. GREEN AFTER: BufferTooSmallException naming the cap, the
 * sink took nothing, and the peer could not write the whole body.
 *
 * THE PEER IS HELD AT TWICE THE CAP UNTIL THE CASE HAS SEEN THE REQUEST FAIL - measured on Windows,
 * 2026-09-29. The cap's cancel is as prompt as the drain ( the design's section 3 ), and until it
 * lands the driver goes on reading and dropping - so a peer writing flat out raced that hop against
 * loopback's throughput, and on a loaded two-core Windows host it lost: the client read all 64 MiB
 * before the cancel landed in three runs of forty, where the kernel there takes only 0.4 to 0.6 MiB
 * of a connection nobody reads, even after a 32 MiB flood. Held, the peer's first 512 KiB cross the
 * cap whatever the read sizes, and what it writes after the release meets a read loop which has
 * stopped, or which stops at its next read - the drain posts the cancel before it tells the caller.
 * So the short write rests on the socket buffers and the close alone, as the design says it does.
 * Before D3 nothing fails the request, the hold ends at its bound, and the red is the one above
 *
 * THE ORDER OF THE SCOPES IS LOAD-BEARING ( the review's F2 ). A writer blocked on a zero window is
 * freed by a RST, which the kernel sends when the client's socket is CLOSED with unread data - and
 * the driver's own teardown only shuts it down ( shutdownSocket( ), TcpBaseTasks.h ); the socket
 * closes when the retired driver task is destroyed. So the peer outlives the session, and its write
 * count is read after the session's scope has ended; the exception and the sink's nothing are
 * asserted inside it, first, so that a lingering socket can fail only the short-write assertion
 */

UTF_AUTO_TEST_CASE( ClientSession_AStalledSinkIsFailedAtTheOutstandingCapTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    enum : std::size_t
    {
        CHUNK_SIZE          = 64U * 1024U,
        CHUNKS              = 1024U,
        CAP                 = 256U * 1024U,
        HELD_AT             = 2U * CAP / CHUNK_SIZE,
    };

    const std::string chunk( static_cast< std::size_t >( CHUNK_SIZE ), 'x' );

    ScriptedPeer peer(
        [ chunk ]( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( "request:" + ScriptedPeer::requestLineOf( request ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: " +
                    utils::lexical_cast< std::string >( CHUNK_SIZE * CHUNKS ) +
                    "\r\n"
                "\r\n"
                );

            /*
             * ITS OWN LOOP, recording how many writes succeeded, because ScriptedPeer::send( )
             * discards the result
             */

            std::size_t writes = 0U;

            for( ; writes < static_cast< std::size_t >( CHUNKS ); ++writes )
            {
                if( static_cast< std::size_t >( HELD_AT ) == writes )
                {
                    /*
                     * Twice the cap is out: held until the case has seen the request fail - see
                     * the case's comment. Bounded, so a request which never fails cannot hang this
                     * thread and the destructor's join
                     */

                    self.waitForRelease();
                }

                eh::error_code ec;

                ( void ) asio::write( socket, asio::buffer( chunk ), ec );

                if( ec )
                {
                    break;
                }
            }

            self.record( "writes:" + utils::lexical_cast< std::string >( writes ) );
        }
        );

    {
        httpclient::ClientSessionConfig config;

        config.requestConfig.maxOutstandingResponseBodySize = static_cast< std::size_t >( CAP );

        const auto session = makeSession( std::move( config ) );

        BL_SCOPE_EXIT_WARN_ON_FAILURE(
            {
                session -> dispose();
            },
            "utest::plainsession::ClientSession_AStalledSinkIsFailedAtTheOutstandingCapTests"
            );

        const auto sink = TallySink::createInstance( true /* takesNothing */ );

        const auto requestTask = session -> createRequestTask(
            makeRequest( peer.port(), "/body" ),
            om::ObjPtrCopyable< httpclient::BodySink >( om::qi< httpclient::BodySink >( sink ) )
            );

        const auto task = om::qi< tasks::Task >( requestTask );

        runSessionTask( task );

        /*
         * THE PEER GOES ON NOW, WHILE THE CLIENT'S SOCKET IS STILL OPEN, so that what it writes meets
         * the stopped read loop and the kernel's buffers before the close frees it - the short write
         * then says both things the design wants of it. The request is over, and its cancel is on
         * the driver's strand already: the drain posts it before it tells the caller
         */

        peer.release();

        requireTrue( task -> isFailed(), "a request whose sink takes nothing should have failed" );

        const auto failure = failureOf( task );

        requireTrue(
            isBufferTooSmall( task -> exception() ),
            "the request should have failed with BufferTooSmallException, and it reports: " + failure
            );

        requireTrue(
            std::string::npos != failure.find( utils::lexical_cast< std::string >( CAP ) ),
            "the failure should name the cap, and it reads: " + failure
            );

        UTF_REQUIRE_EQUAL( sink -> received().size(), 0U );
        UTF_REQUIRE( sink -> offers() >= 1U );
    }

    /*
     * THE SESSION IS GONE, and with it the driver and its socket: the peer's blocked write has been
     * reset, and its loop has ended short of the body
     */

    requireTrue(
        peer.waitForRecords( 2U ),
        "the peer's writes never ended, so the client's socket was never closed"
        );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "request:GET /body HTTP/1.1" ) );

    const std::string prefix( "writes:" );

    UTF_REQUIRE_EQUAL( records[ 1 ].compare( 0U, prefix.size(), prefix ), 0 );

    const auto writes = utils::lexical_cast< std::size_t >( records[ 1 ].substr( prefix.size() ) );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "The peer wrote "
            << writes
            << " of its "
            << static_cast< std::size_t >( CHUNKS )
            << " chunks before the client's socket was closed"
        );

    UTF_CHECK( writes < static_cast< std::size_t >( CHUNKS ) );
}

/**
 * @brief Case 3, the control - a sink which KEEPS UP passes a body many times the cap
 *
 * A 1 MiB body and a cap of 64 KiB. The peer writes 16 KiB pieces and sends each only once the
 * sink's own tally shows the previous one taken, so at most one piece - two, on the worst
 * interleaving of a release with the next post - is ever held. Its wait is bounded and a stall is
 * recorded, so a failed run cannot hang the peer's thread and the destructor's join ( P8 ).
 *
 * GREEN BEFORE D3 AND AFTER. What it rules out is a cap on the TOTAL, which would fail it: it is what
 * "outstanding, not total" means
 */

UTF_AUTO_TEST_CASE( ClientSession_ASinkWhichKeepsUpIsNotCappedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    enum : std::size_t
    {
        PIECE_SIZE          = 16U * 1024U,
        PIECES              = 64U,
        CAP                 = 64U * 1024U,
    };

    const auto sink = TallySink::createInstance( false /* takesNothing */ );

    const om::ObjPtrCopyable< TallySink > pacing( sink );

    std::string expected;

    for( std::size_t k = 0U; k < static_cast< std::size_t >( PIECES ); ++k )
    {
        expected.append( static_cast< std::size_t >( PIECE_SIZE ), static_cast< char >( 'a' + k % 26U ) );
    }

    ScriptedPeer peer(
        [ pacing ]( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( "request:" + ScriptedPeer::requestLineOf( request ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: " +
                    utils::lexical_cast< std::string >( PIECE_SIZE * PIECES ) +
                    "\r\n"
                "\r\n"
                );

            for( std::size_t k = 0U; k < static_cast< std::size_t >( PIECES ); ++k )
            {
                ScriptedPeer::send(
                    socket,
                    std::string(
                        static_cast< std::size_t >( PIECE_SIZE ),
                        static_cast< char >( 'a' + k % 26U )
                        )
                    );

                if(
                    ! pacing -> waitForTaken(
                        ( k + 1U ) * static_cast< std::size_t >( PIECE_SIZE ),
                        static_cast< std::size_t >( utest::http1driver::WAIT_TIMEOUT_IN_MILLISECONDS )
                        )
                    )
                {
                    self.record( "stalled:" + utils::lexical_cast< std::string >( k ) );

                    return;
                }
            }

            self.record( "sent" );

            self.waitForRelease();
        }
        );

    httpclient::ClientSessionConfig config;

    config.requestConfig.maxOutstandingResponseBodySize = static_cast< std::size_t >( CAP );

    const auto session = makeSession( std::move( config ) );

    BL_SCOPE_EXIT_WARN_ON_FAILURE(
        {
            session -> dispose();
        },
        "utest::plainsession::ClientSession_ASinkWhichKeepsUpIsNotCappedTests"
        );

    const auto requestTask = session -> createRequestTask(
        makeRequest( peer.port(), "/body" ),
        om::ObjPtrCopyable< httpclient::BodySink >( om::qi< httpclient::BodySink >( sink ) )
        );

    const auto task = om::qi< tasks::Task >( requestTask );

    runSessionTask( task );

    requireTaskSucceeded( task );

    UTF_REQUIRE_EQUAL( requestTask -> response().status(), 200U );

    UTF_REQUIRE_EQUAL( sink -> received().size(), expected.size() );
    UTF_REQUIRE( sink -> received() == expected );
    UTF_REQUIRE_EQUAL( sink -> completions(), 1U );

    requireTrue( peer.waitForRecords( 2U ), "the peer never recorded the end of its body" );

    UTF_REQUIRE_EQUAL( peer.records()[ 1 ], std::string( "sent" ) );
}

#endif /* __UTEST_TESTCLIENTSESSIONOUTSTANDINGCAP_H_ */
