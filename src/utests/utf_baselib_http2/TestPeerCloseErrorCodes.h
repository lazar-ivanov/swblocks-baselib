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

#ifndef __UTEST_TESTPEERCLOSEERRORCODES_H_
#define __UTEST_TESTPEERCLOSEERRORCODES_H_

#include <baselib/tasks/TcpBaseTasks.h>

#include <baselib/core/NetUtils.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * What this platform's TCP stack reports to a reader when a peer ends the conversation the way
 * every task in this library ends it - TcpSocketCommonBase::shutdownSocket( ), which shuts down
 * both directions and cancels, and never closes
 *
 * These cases exist because the two predicates of core/NetUtils.h were written from two
 * measurements taken inside product code on Windows, one 10054 and one 10053, and the mechanism
 * behind either was never established - see
 * notes/plans/issues/windows-peer-close-error-codes-record.md, which records what was measured,
 * what was ruled out and what is only hypothesis. A loopback pair with the library's own teardown
 * on the peer side reproduces the two shapes those measurements came from, deterministically and
 * without a driver in between, so that the same run on each platform says what the platform does
 *
 * What is ASSERTED on every platform is the only thing product code relies on: the code the read
 * ends with is one net::isPeerClosedErrorCode( ) accepts. What differs by platform is asserted in
 * two arms, keyed on the os:: facts the predicates are built from, so that neither arm can go
 * vacuous. How many of the peer's bytes reach the reader before the end is REPORTED and not
 * asserted: whether a reset discards unread data is the open half of the hypothesis, and either
 * answer is one the library has to live with rather than one it may require
 *
 * The teardown is the real one on purpose. Replicating its three calls here would test a copy;
 * what matters is what a task's peer sees, and that is what shutdownSocket( ) does
 */

namespace utest
{
    namespace peerclose
    {
        /**
         * @brief A connected loopback pair on one io_service - an accepted 'server' end which
         * plays the peer, and a 'client' end which plays the reader
         *
         * Everything is driven synchronously from the case, one operation at a time, with a
         * deadline on each so that a stack which never ends the conversation fails the case with
         * an error code rather than hanging the module
         */

        class LoopbackPair
        {
            BL_NO_COPY_OR_MOVE( LoopbackPair )

        public:

            enum : long
            {
                DEFAULT_TIMEOUT_IN_SECONDS = 30L,

                /*
                 * NOT A RENDEZVOUS BEFORE AN ASSERTION, which src/utests/AGENTS.md forbids. It
                 * puts the two ends into the STATE THE CASE IS NAMED FOR - bytes sitting unread in
                 * the peer's buffer when it shuts down, or a send issued after the shutdown has
                 * landed - and no assertion below depends on it having been long enough
                 *
                 * On a stack which answers the shutdown with FIN both cases are deterministic
                 * without it, by TCP ordering alone: the payload was written before the shutdown,
                 * so it precedes the FIN in the stream, and readerReadsUntilTheEnd( ) loops until
                 * the stack reports an end rather than sampling once. Where it matters is on a
                 * stack which renames the close, and there the assertion is a disjunction over
                 * both spellings for exactly this reason
                 */

                SETTLE_IN_MILLISECONDS = 100L,
            };

            enum : std::size_t
            {
                PAYLOAD_SIZE = 16U * 1024U,
            };

            struct ReadOutcome
            {
                std::size_t                                                 delivered = 0U;
                bl::eh::error_code                                          ec;
            };

            LoopbackPair()
                :
                m_acceptor( m_ioService ),
                m_server( m_ioService ),
                m_client( m_ioService ),
                m_timer( m_ioService )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                bl::eh::error_code acceptEc;
                bl::eh::error_code connectEc;
                bool accepted = false;
                bool connected = false;

                m_acceptor.async_accept(
                    m_server,
                    [ this, &acceptEc, &accepted, &connected ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        acceptEc = ec;
                        accepted = true;

                        if( connected )
                        {
                            m_timer.cancel();
                        }
                    }
                    );

                m_client.async_connect(
                    m_acceptor.local_endpoint(),
                    [ this, &connectEc, &accepted, &connected ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        connectEc = ec;
                        connected = true;

                        if( accepted )
                        {
                            m_timer.cancel();
                        }
                    }
                    );

                armDeadline();

                runService();

                UTF_REQUIRE( accepted );
                UTF_REQUIRE( connected );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), acceptEc );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), connectEc );
            }

            /**
             * @brief The peer sends its payload, all of it, before it does anything else
             */

            void peerSends( SAA_in const std::string& payload )
            {
                bl::eh::error_code ec;

                const auto written = bl::asio::write( m_server, bl::asio::buffer( payload ), ec );

                UTF_REQUIRE_EQUAL( bl::eh::error_code(), ec );
                UTF_REQUIRE_EQUAL( written, payload.size() );
            }

            /**
             * @brief The reader sends something the peer will never read
             *
             * The outcome is returned rather than asserted: after the peer has shut down, a stack
             * may refuse the send itself, and that is part of what a case is measuring
             */

            auto readerSends( SAA_in const std::string& payload ) -> bl::eh::error_code
            {
                bl::eh::error_code ec;

                ( void ) bl::asio::write( m_client, bl::asio::buffer( payload ), ec );

                return ec;
            }

            /**
             * @brief The peer ends the conversation exactly as a task does at the end of its life
             */

            void peerShutsDownLikeATask()
            {
                bl::tasks::TcpSocketCommonBase::shutdownSocket( m_server, true /* force */ );
            }

            void settle()
            {
                bl::os::sleep( bl::time::milliseconds( SETTLE_IN_MILLISECONDS ) );
            }

            /**
             * @brief Reads until the stack reports that the conversation is over, and says how
             * many bytes arrived before it did
             */

            auto readerReadsUntilTheEnd() -> ReadOutcome
            {
                ReadOutcome outcome;

                for( ;; )
                {
                    bl::eh::error_code readEc;
                    std::size_t transferred = 0U;
                    bool completed = false;

                    m_client.async_read_some(
                        bl::asio::buffer( m_buffer, sizeof( m_buffer ) ),
                        [ this, &readEc, &transferred, &completed ](
                            SAA_in      const bl::eh::error_code&               ec,
                            SAA_in      const std::size_t                       bytesTransferred
                            ) -> void
                        {
                            readEc = ec;
                            transferred = bytesTransferred;
                            completed = true;

                            m_timer.cancel();
                        }
                        );

                    armDeadline();

                    runService();

                    UTF_REQUIRE( completed );

                    outcome.delivered += transferred;

                    if( readEc )
                    {
                        outcome.ec = readEc;

                        break;
                    }
                }

                return outcome;
            }

        private:

            /**
             * @brief Arms the deadline which cancels whatever is pending if it does not complete
             *
             * The operation's own completion handler cancels this timer, so the service runs out
             * of work as soon as the operation is done rather than waiting the deadline out
             */

            void armDeadline()
            {
                m_timer.expires_from_now( bl::time::seconds( DEFAULT_TIMEOUT_IN_SECONDS ) );

                m_timer.async_wait(
                    [ this ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        if( bl::asio::error::operation_aborted != ec )
                        {
                            bl::eh::error_code cancelEc;

                            m_acceptor.cancel( cancelEc );
                            m_client.cancel( cancelEc );
                            m_server.cancel( cancelEc );
                        }
                    }
                    );
            }

            void runService()
            {
                #if ( ( BOOST_VERSION / 100 ) >= 1066 )
                m_ioService.restart();
                #else
                m_ioService.reset();
                #endif

                m_ioService.run();
            }

            bl::asio::io_service                                            m_ioService;
            bl::asio::ip::tcp::acceptor                                     m_acceptor;
            bl::asio::ip::tcp::socket                                       m_server;
            bl::asio::ip::tcp::socket                                       m_client;
            bl::asio::deadline_timer                                        m_timer;

            char                                                            m_buffer[ 4096 ];
        };

        /**
         * @brief What a case reports about how the conversation ended, before it asserts anything
         *
         * Reported through the log rather than asserted, so that a Windows run of this module
         * leaves behind the two numbers the record is missing - which code, and how much of the
         * payload survived it - whatever they turn out to be
         */

        inline void reportOutcome(
            SAA_in          const char*                                         scenario,
            SAA_in          const LoopbackPair::ReadOutcome&                    outcome,
            SAA_in          const std::size_t                                   payloadSize
            )
        {
            UTF_MESSAGE(
                BL_MSG()
                    << "peer close, "
                    << scenario
                    << ": the read ended with category='"
                    << outcome.ec.category().name()
                    << "' value="
                    << outcome.ec.value()
                    << " ('"
                    << outcome.ec.message()
                    << "'), and "
                    << outcome.delivered
                    << " of "
                    << payloadSize
                    << " payload bytes were delivered before it"
                );
        }

        /**
         * @brief The assertions every platform must satisfy, and the arm for the one it is
         */

        inline void requireGracefulPeerClose(
            SAA_in          const LoopbackPair::ReadOutcome&                    outcome,
            SAA_in          const std::size_t                                   payloadSize
            )
        {
            using namespace bl;

            /*
             * The teardown is graceful ON EVERY PLATFORM, and that is the point of these cases
             *
             * They were written to measure a platform difference and they found one: with the
             * shutdown_both this function's peer used to perform, Windows reported 10054 or 10053
             * and delivered 0 of 16384 bytes, where Linux reported eof and delivered all of them.
             * The difference turned out to be self-inflicted - shutting down the RECEIVE side
             * makes the close abortive, and the reset discards what the peer had not yet read.
             * TcpSocketCommonBase::shutdownSocket( ) now shuts down the send side only, and the
             * asymmetry is gone.
             *
             * So there is no platform arm here any more. Both sides must see an orderly end of
             * stream and every byte, which is what makes this a regression test for the teardown
             * rather than a description of Windows: if anyone restores shutdown_both, these two
             * cases go red on Windows and say why
             */

            UTF_REQUIRE_EQUAL( asio::error::make_error_code( asio::error::eof ), outcome.ec );
            UTF_REQUIRE( net::isOrderlyPeerCloseErrorCode( outcome.ec ) );
            UTF_REQUIRE( net::isPeerClosedErrorCode( outcome.ec ) );
            UTF_REQUIRE_EQUAL( outcome.delivered, payloadSize );
        }

    } // peerclose

} // utest

/*
 * The shape of the 10054 measurement: the peer has bytes from the reader it never read when it
 * shuts down. This is what a TLS peer which accepts and then goes away leaves behind - the
 * ClientHello is in its receive buffer - and it is the row
 * TlsHandshakeRetryClassifier_RetryableErrorSetTests pins both ways for connection_reset
 */

UTF_AUTO_TEST_CASE( PeerCloseErrorCodes_PeerShutsDownWithUnreadDataTests )
{
    using namespace bl;
    using namespace utest::peerclose;

    const std::string payload( LoopbackPair::PAYLOAD_SIZE, 'p' );

    LoopbackPair pair;

    pair.peerSends( payload );

    /*
     * Sent while the peer's receive side is still open, so the send itself cannot fail; the peer
     * simply never reads it
     */

    UTF_REQUIRE_EQUAL( eh::error_code(), pair.readerSends( "unread" ) );

    pair.settle();

    pair.peerShutsDownLikeATask();

    /*
     * No settle before the read: the reader loops until the stack reports an end, so whether the
     * answer has arrived yet only changes how many times round the loop it goes
     */

    const auto outcome = pair.readerReadsUntilTheEnd();

    reportOutcome( "the peer shut down with unread data", outcome, payload.size() );

    requireGracefulPeerClose( outcome, payload.size() );
}

/*
 * The shape of the 10053 measurement: the peer has shut down, and only then does the reader send
 * something - the late WINDOW_UPDATE of the HTTP/2 driver, whose read was outstanding when the
 * answer came back. Here the reader's read is issued after its send, which is what makes the
 * outcome deterministic: nothing races the send for the stack's attention
 */

UTF_AUTO_TEST_CASE( PeerCloseErrorCodes_ReaderSendsAfterPeerShutdownTests )
{
    using namespace bl;
    using namespace utest::peerclose;

    const std::string payload( LoopbackPair::PAYLOAD_SIZE, 'p' );

    LoopbackPair pair;

    pair.peerSends( payload );

    pair.peerShutsDownLikeATask();

    pair.settle();

    /*
     * The late send. Its own outcome is reported, not asserted: a stack is entitled to refuse it
     * outright, and that refusal is one more spelling of the same event
     */

    const auto sendEc = pair.readerSends( "late" );

    UTF_MESSAGE(
        BL_MSG()
            << "peer close, the reader sent after the peer shut down: the send ended with category='"
            << sendEc.category().name()
            << "' value="
            << sendEc.value()
        );

    const auto outcome = pair.readerReadsUntilTheEnd();

    reportOutcome( "the reader sent after the peer shut down", outcome, payload.size() );

    requireGracefulPeerClose( outcome, payload.size() );
}

#endif /* __UTEST_TESTPEERCLOSEERRORCODES_H_ */
