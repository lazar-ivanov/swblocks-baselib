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

#ifndef __UTEST_TESTTCPPREHANDSHAKESTAGETLS_H_
#define __UTEST_TESTTCPPREHANDSHAKESTAGETLS_H_

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TcpSslBaseTasks.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <string>
#include <vector>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/*
 * The order in which TcpConnectionEstablisherConnector drives a TLS connection
 *
 * The plain stream half of this contract is in utf_baselib_tasks2; what only TLS can show is that
 * the protocol handshake really does sit between the connect and continueAfterConnected, and that
 * a retryable handshake error restarts the whole resolve / connect / handshake transaction
 * (TcpBaseTasks.h:1389). Both are pinned here through the virtuals which exist today, so these
 * cases characterize current behavior and must go on passing unchanged once the pre-handshake
 * stage hook of design 3.6 is added
 *
 * Like every other case in this module these take no machine global test lock and do not bind the
 * fixed test port: the peer is a TLS server on an ephemeral loopback port, driven synchronously
 * from the test thread while the connector task runs on the I/O thread pool
 */

namespace utest
{
    namespace prehandshake
    {
        /**
         * @brief One byte of a protocol header, as a number
         */

        inline auto octet(
            SAA_in          const char*                                         buffer,
            SAA_in          const std::size_t                                   offset
            )
            -> std::size_t
        {
            return static_cast< std::size_t >( static_cast< unsigned char >( buffer[ offset ] ) );
        }

        /**
         * @brief A TLS server on an ephemeral loopback port, driven one connection at a time
         *
         * The accept and the read are deadline bounded so a connector which never arrives fails
         * the case on its own deadline instead of hanging it; the handshaken stream is kept alive
         * by the peer rather than by the accepting call, so the client is never raced by the
         * socket going away underneath it while it finishes its own side of the handshake
         */

        class TlsLoopbackPeer
        {
            BL_NO_COPY_OR_MOVE( TlsLoopbackPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            enum : long
            {
                DEFAULT_TIMEOUT_IN_SECONDS = 30L,
            };

            TlsLoopbackPeer()
                :
                m_acceptor( m_ioService ),
                m_timer( m_ioService ),
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();
            }

            auto port() const -> unsigned short
            {
                return m_acceptor.local_endpoint().port();
            }

            /**
             * @brief Accepts one connection and completes the server side of the TLS handshake
             */

            void acceptAndHandshake()
            {
                m_stream = bl::cpp::SafeUniquePtr< sslstream_t >::attach(
                    new sslstream_t( m_ioService, *m_serverContext )
                    );

                acceptOne( m_stream -> next_layer() );

                bl::eh::error_code ec;

                m_stream -> handshake( bl::asio::ssl::stream_base::server, ec );

                UTF_REQUIRE_EQUAL( bl::eh::error_code(), ec );
            }

            /**
             * @brief Accepts one connection, reads the whole client hello and then shuts it down
             *
             * Reading the client hello WHOLE is what keeps this an orderly end of the stream
             * rather than a reset - a reset would silently turn this into a different case. It
             * used to be one read_some( ) into 1024 bytes, which is not the same thing: a hello
             * OpenSSL 3.5 offers is routinely larger than that and a read_some( ) returns as soon
             * as any bytes are there, so bytes were left queued, and closing a socket with bytes
             * still in its receive queue is an ABORTIVE close on both platforms (RFC 2525 section
             * 2.17). The reset the 2026-09-21 Windows row measured was this peer's own
             *
             * The shutdown is of the send side only, for the reason
             * TcpSocketCommonBase::shutdownSocket( ) now is (bb53bdd): asking for SD_RECEIVE makes
             * the close abortive on Windows the moment anything arrives afterwards. That is the
             * lesser of the two here - this peer never reads again - but it costs nothing and it
             * leaves no way for the case to manufacture the code it is measuring
             */

            void acceptAndShutdown()
            {
                bl::asio::ip::tcp::socket socket( m_ioService );

                acceptOne( socket );

                /*
                 * A TLS record body cannot exceed SSL3_RT_MAX_PLAIN_LENGTH, so a buffer of one
                 * whole record can never be too small for a well formed hello
                 */

                char buffer[ SSL3_RT_HEADER_LENGTH + SSL3_RT_MAX_PLAIN_LENGTH ];

                bl::eh::error_code ec;

                const auto helloSize =
                    readClientHelloWithDeadline( socket, buffer, sizeof( buffer ), ec );

                UTF_REQUIRE_EQUAL( bl::eh::error_code(), ec );

                /*
                 * Reported and not asserted: how big a hello is depends on the OpenSSL version and
                 * on what the client offers, and no case should turn on it. What it records is
                 * whether one 1024 byte read could ever have taken the whole of one
                 */

                UTF_MESSAGE(
                    BL_MSG()
                        << "the pre-handshake peer read a client hello of "
                        << helloSize
                        << " bytes before shutting the connection down"
                    );

                socket.shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );
                socket.close( ec );
            }

        private:

            /**
             * @brief Arms the deadline which cancels the pending operation if it does not complete
             *
             * The operation's own completion handler cancels this timer, so the service runs out
             * of work as soon as the operation is done rather than waiting the deadline out
             */

            template
            <
                typename CANCELABLE
            >
            void armDeadline( SAA_inout CANCELABLE& cancelable )
            {
                m_timer.expires_from_now( bl::time::seconds( DEFAULT_TIMEOUT_IN_SECONDS ) );

                m_timer.async_wait(
                    [ &cancelable ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        if( bl::asio::error::operation_aborted != ec )
                        {
                            bl::eh::error_code cancelEc;

                            cancelable.cancel( cancelEc );
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

            void acceptOne( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
                bl::eh::error_code acceptEc;
                bool acceptCompleted = false;

                m_acceptor.async_accept(
                    socket,
                    [ this, &acceptEc, &acceptCompleted ]( SAA_in const bl::eh::error_code& ec ) -> void
                    {
                        acceptEc = ec;
                        acceptCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( m_acceptor );

                runService();

                UTF_REQUIRE( acceptCompleted );
                UTF_REQUIRE_EQUAL( bl::eh::error_code(), acceptEc );
            }

            auto readSomeWithDeadline(
                SAA_inout       bl::asio::ip::tcp::socket&                      socket,
                SAA_out         char*                                           buffer,
                SAA_in          const std::size_t                               size,
                SAA_out         bl::eh::error_code&                             ec
                )
                -> std::size_t
            {
                std::size_t bytesRead = 0U;
                bool readCompleted = false;

                socket.async_read_some(
                    bl::asio::buffer( buffer, size ),
                    [ this, &ec, &bytesRead, &readCompleted ](
                        SAA_in      const bl::eh::error_code&                   readEc,
                        SAA_in      const std::size_t                           transferred
                        ) -> void
                    {
                        ec = readEc;
                        bytesRead = transferred;
                        readCompleted = true;

                        m_timer.cancel();
                    }
                    );

                armDeadline( socket );

                runService();

                UTF_REQUIRE( readCompleted );

                return bytesRead;
            }

            /**
             * @brief Reads exactly the number of bytes asked for, or stops at the first error
             *
             * A read_some( ) completes as soon as ANY bytes are available, so one of them is not a
             * read of a known quantity; this is
             */

            auto readExactlyWithDeadline(
                SAA_inout       bl::asio::ip::tcp::socket&                      socket,
                SAA_out         char*                                           buffer,
                SAA_in          const std::size_t                               size,
                SAA_out         bl::eh::error_code&                             ec
                )
                -> std::size_t
            {
                std::size_t bytesRead = 0U;

                while( bytesRead < size )
                {
                    bytesRead += readSomeWithDeadline( socket, buffer + bytesRead, size - bytesRead, ec );

                    if( ec )
                    {
                        break;
                    }
                }

                return bytesRead;
            }

            /**
             * @brief Reads the client hello whole, so that none of it is left queued unread
             *
             * The record header carries the length of what follows it, so the hello can be taken
             * exactly rather than guessed at. What this peer needs is not the hello's content - it
             * answers nothing - but the certainty that NOTHING of it is still in the receive queue
             * when the socket is closed, and a length taken from the wire is the only way to have
             * that
             */

            auto readClientHelloWithDeadline(
                SAA_inout       bl::asio::ip::tcp::socket&                      socket,
                SAA_out         char*                                           buffer,
                SAA_in          const std::size_t                               capacity,
                SAA_out         bl::eh::error_code&                             ec
                )
                -> std::size_t
            {
                ( void ) readExactlyWithDeadline( socket, buffer, SSL3_RT_HEADER_LENGTH, ec );

                if( ec )
                {
                    return 0U;
                }

                /*
                 * A content type, two version bytes and then the body length
                 */

                UTF_REQUIRE_EQUAL( octet( buffer, 0U ), std::size_t( SSL3_RT_HANDSHAKE ) );

                const std::size_t bodySize = ( octet( buffer, 3U ) << 8 ) + octet( buffer, 4U );

                UTF_REQUIRE( SSL3_RT_HEADER_LENGTH + bodySize <= capacity );

                ( void ) readExactlyWithDeadline( socket, buffer + SSL3_RT_HEADER_LENGTH, bodySize, ec );

                if( ec )
                {
                    return SSL3_RT_HEADER_LENGTH;
                }

                /*
                 * That the record holds exactly one complete handshake message, and that it is the
                 * hello, is what says the client's first flight has been taken entirely. Asserted
                 * rather than assumed: a client which ever fragmented its hello across records
                 * would quietly put this case back to closing with bytes unread, which is the
                 * reset it exists not to manufacture
                 */

                const auto* const body = buffer + SSL3_RT_HEADER_LENGTH;

                UTF_REQUIRE( bodySize > SSL3_HM_HEADER_LENGTH );

                UTF_REQUIRE_EQUAL( octet( body, 0U ), std::size_t( SSL3_MT_CLIENT_HELLO ) );

                UTF_REQUIRE_EQUAL(
                    ( octet( body, 1U ) << 16 ) + ( octet( body, 2U ) << 8 ) + octet( body, 3U )
                        + SSL3_HM_HEADER_LENGTH,
                    bodySize
                    );

                return SSL3_RT_HEADER_LENGTH + bodySize;
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::deadline_timer                                            m_timer;
            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;
            bl::cpp::SafeUniquePtr< sslstream_t >                               m_stream;
        };

        /**
         * @brief A TLS connection establisher which records the order it was driven in
         *
         * Only the virtuals which exist today are used, so the same probe observes the same
         * sequence before and after the pre-handshake stage hook is introduced
         */

        template
        <
            typename STREAM
        >
        class TlsConnectOrderProbeT :
            public bl::tasks::TcpConnectionEstablisherConnector< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( TlsConnectOrderProbeT )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< STREAM >      base_type;

        protected:

            typedef typename base_type::tcp_resolver_type                       tcp_resolver_type;

            mutable bl::os::mutex                                               m_eventsLock;
            std::vector< std::string >                                          m_events;

            bl::cpp::ScalarTypeIniter< bool >                                   m_wasHandshakeCompletedAtContinuation;

            TlsConnectOrderProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const std::size_t                       maxRetryCount
                )
                :
                base_type( BL_PARAM_FWD( host ), port, false /* logExceptions */ )
            {
                /*
                 * The default is MAX_RETRY_COUNT (5), i.e. six attempts; the retry case only needs
                 * to show that the transaction restarts and that it is bounded, and every extra
                 * attempt costs the peer another accept
                 */

                base_type::m_maxRetryCount = maxRetryCount;
            }

            void record( SAA_in const char* event )
            {
                BL_MUTEX_GUARD( m_eventsLock );

                m_events.push_back( std::string( event ) );
            }

            virtual bool continueAfterResolved( SAA_in typename base_type::tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                record( "resolved" );

                return base_type::continueAfterResolved( endpoints );
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                record( "continueAfterConnected" );

                m_wasHandshakeCompletedAtContinuation = base_type::hasHandshakeCompletedSuccessfully();

                return false;
            }

        public:

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_eventsLock );

                return m_events;
            }

            auto countOf( SAA_in const char* event ) const -> std::size_t
            {
                BL_MUTEX_GUARD( m_eventsLock );

                std::size_t count = 0U;

                for( const auto& recorded : m_events )
                {
                    if( recorded == event )
                    {
                        ++count;
                    }
                }

                return count;
            }

            bool wasHandshakeCompletedAtContinuation() const NOEXCEPT
            {
                return m_wasHandshakeCompletedAtContinuation;
            }
        };

        typedef bl::om::ObjectImpl< TlsConnectOrderProbeT< bl::tasks::TcpSslSocketAsyncBase > >
            TlsConnectOrderProbeImpl;

        /**
         * @brief The same probe with a pre-handshake stage which invokes the continuation at once
         *
         * Unlike the plain stream, the continuation does not complete inside the stage here:
         * beginProtocolHandshake starts an async handshake and returns true, so what the stage can
         * observe is that no handshake had happened before it ran and that its own call is what
         * started one
         */

        template
        <
            typename STREAM
        >
        class TlsStageProbeT : public TlsConnectOrderProbeT< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( TlsStageProbeT )

        public:

            typedef TlsConnectOrderProbeT< STREAM >                             base_type;

        protected:

            bl::cpp::ScalarTypeIniter< bool >                                   m_wasHandshakeCompletedAtStage;
            bl::cpp::ScalarTypeIniter< bool >                                   m_didStageStartTheHandshake;

            TlsStageProbeT(
                SAA_in                  std::string&&                           host,
                SAA_in                  const unsigned short                    port,
                SAA_in                  const std::size_t                       maxRetryCount
                )
                :
                base_type( BL_PARAM_FWD( host ), port, maxRetryCount )
            {
            }

            virtual bool beginPreHandshakeStage(
                SAA_in                  const bl::cpp::bool_callback_t&         continueCallback
                )
                OVERRIDE
            {
                base_type::record( "stageEntered" );

                m_wasHandshakeCompletedAtStage = base_type::hasHandshakeCompletedSuccessfully();

                const bool result = continueCallback();

                m_didStageStartTheHandshake = result;

                base_type::record( "stageLeft" );

                return result;
            }

        public:

            bool wasHandshakeCompletedAtStage() const NOEXCEPT
            {
                return m_wasHandshakeCompletedAtStage;
            }

            bool didStageStartTheHandshake() const NOEXCEPT
            {
                return m_didStageStartTheHandshake;
            }
        };

        typedef bl::om::ObjectImpl< TlsStageProbeT< bl::tasks::TcpSslSocketAsyncBase > >
            TlsStageProbeImpl;

    } // prehandshake

} // utest

UTF_AUTO_TEST_CASE( TcpPreHandshakeStageTls_TodaysHandshakeOrderTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * The TLS half of today's order: the resolve continuation, then the connect, then the protocol
     * handshake, and only then the connect continuation - which is what
     * hasHandshakeCompletedSuccessfully() reading true inside continueAfterConnected says. A change
     * which started the handshake late, or ran the continuation without one, would flip it
     */

    TlsLoopbackPeer peer;

    const auto probe = TlsConnectOrderProbeImpl::createInstance(
        std::string( "localhost" ),
        peer.port(),
        0U                                                  /* maxRetryCount */
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &peer, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            peer.acceptAndHandshake();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 2U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "continueAfterConnected" ) );

    UTF_REQUIRE( probe -> wasHandshakeCompletedAtContinuation() );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStageTls_RetryableHandshakeErrorTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * A peer which accepts and then goes away gives the handshake a truncated stream, which
     * TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation (TcpBaseTasks.h:1389)
     * answers by restarting the *whole* resolve / connect / handshake transaction. Pinning the
     * attempt count is what the pre-handshake stage of design 3.6 has to match: the stage runs
     * once per attempt and carries nothing from one attempt to the next
     *
     * The retry budget is one, so there are exactly two attempts and the peer serves two
     * connections; the task then fails for good with the handshake error
     */

    TlsLoopbackPeer peer;

    const auto probe = TlsConnectOrderProbeImpl::createInstance(
        std::string( "localhost" ),
        peer.port(),
        1U                                                  /* maxRetryCount */
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &peer, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            peer.acceptAndShutdown();
            peer.acceptAndShutdown();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    /*
     * Which code a peer that goes away mid-handshake produces, reported and not asserted: it is a
     * property of the platform, and asserting it here would be asserting the platform. It is the
     * code of the LAST attempt, the one which exhausted the retry budget, and it is read the same
     * way isProtocolHandshakeRetryableError() reads it - so it is the code the predicate was
     * handed on the attempt before, which is the measurement
     * notes/plans/issues/tls-handshake-retry-unreachable-record.md is owed on Windows
     */

    const auto handshakeEc = eh::errorCodeFromExceptionPtr( task -> exception() );

    UTF_MESSAGE(
        BL_MSG()
            << "the handshake against a peer which went away ended with category='"
            << handshakeEc.category().name()
            << "' value="
            << handshakeEc.value()
            << " ('"
            << handshakeEc.message()
            << "')"
        );

    /*
     * Two attempts, and continueAfterConnected was never reached on either of them because the
     * handshake failed before it
     */

    UTF_REQUIRE_EQUAL( probe -> countOf( "resolved" ), 2U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "continueAfterConnected" ), 0U );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStageTls_StageRunsBeforeHandshakeTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * The TLS half of the default hook contract: the stage runs after the connect and *before* any
     * handshake - nothing had completed one when it was entered - and it is the stage's own call to
     * the continuation, made synchronously inside the connect handler, which starts one. The
     * handshake then completes before continueAfterConnected, exactly as it does without a stage
     */

    TlsLoopbackPeer peer;

    const auto probe = TlsStageProbeImpl::createInstance(
        std::string( "localhost" ),
        peer.port(),
        0U                                                  /* maxRetryCount */
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &peer, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            peer.acceptAndHandshake();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( ! task -> isFailed() );
    UTF_REQUIRE( ! task -> exception() );

    const auto events = probe -> events();

    UTF_REQUIRE_EQUAL( events.size(), 4U );
    UTF_REQUIRE_EQUAL( events[ 0 ], std::string( "resolved" ) );
    UTF_REQUIRE_EQUAL( events[ 1 ], std::string( "stageEntered" ) );
    UTF_REQUIRE_EQUAL( events[ 2 ], std::string( "stageLeft" ) );
    UTF_REQUIRE_EQUAL( events[ 3 ], std::string( "continueAfterConnected" ) );

    UTF_REQUIRE( ! probe -> wasHandshakeCompletedAtStage() );
    UTF_REQUIRE( probe -> didStageStartTheHandshake() );
    UTF_REQUIRE( probe -> wasHandshakeCompletedAtContinuation() );
}

UTF_AUTO_TEST_CASE( TcpPreHandshakeStageTls_StageRunsOncePerAttemptTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::prehandshake;

    /*
     * The stage is part of the transaction the handshake retry restarts, so it runs once per
     * attempt - as many times as the connect does, and never twice for one connect. A stage which
     * ran once for the whole task would leave the second attempt tunnelling through nothing
     */

    TlsLoopbackPeer peer;

    const auto probe = TlsStageProbeImpl::createInstance(
        std::string( "localhost" ),
        peer.port(),
        1U                                                  /* maxRetryCount */
        );

    const auto task = om::qi< Task >( probe );

    scheduleAndExecuteInParallel(
        [ &peer, &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            peer.acceptAndShutdown();
            peer.acceptAndShutdown();

            eq -> wait( task );

            UTF_REQUIRE( eq -> isEmpty() );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE_EQUAL( probe -> countOf( "resolved" ), 2U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "stageEntered" ), 2U );
    UTF_REQUIRE_EQUAL( probe -> countOf( "continueAfterConnected" ), 0U );
}

#endif /* __UTEST_TESTTCPPREHANDSHAKESTAGETLS_H_ */
