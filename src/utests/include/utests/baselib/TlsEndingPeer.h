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

#ifndef __UTEST_TLSENDINGPEER_H_
#define __UTEST_TLSENDINGPEER_H_

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/UtfCrypto.h>

/*
 * A TLS peer which ENDS a stream in a chosen way - the role the teardown cases of CS-6 need (owed-list
 * rows I2 and D-L3-1). It is not an HTTP peer and not an HTTP/2 ALPN peer, the two roles CS-5 unified
 * elsewhere in this directory; it is shared by utf_baselib_tasks3 and utf_baselib_httpclient13
 */

namespace utest
{
    namespace tlsendingpeer
    {
        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,
        };

        inline auto describeCode( SAA_in const bl::eh::error_code& ec ) -> std::string
        {
            if( ! ec )
            {
                return "success";
            }

            return std::string( ec.category().name() ) +
                ":" +
                bl::utils::lexical_cast< std::string >( ec.value() ) +
                " (" +
                ec.message() +
                ")";
        }

        /**
         * @brief class TlsEndingPeer - a loopback TLS server which ends one connection's stream in a
         * chosen way and records what the client answered
         *
         * Everything runs synchronously on a worker thread with an io_service of its own. The
         * socket stays open until release( ) - or the destructor - so a case decides when the
         * peer's side of the connection goes away, and nothing the peer does after its ending can
         * wake the client
         */

        class TlsEndingPeer
        {
            BL_NO_COPY_OR_MOVE( TlsEndingPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            enum class Ending
            {
                /**
                 * @brief Sends its close_notify after the handshake, then reads until the client's
                 * answer - asio's synchronous shutdown( ) is exactly that
                 */

                CloseNotify,

                /**
                 * @brief Reads until the client ends the stream, then answers with its own
                 * close_notify - at once, or only once released if the answer is held
                 */

                AwaitTheClient,

                /**
                 * @brief Ends the stream WITHOUT a close_notify - the transport's send side is shut
                 * down under the TLS session, which is the truncation - and then reads until the
                 * client ends the stream, with its socket kept open and silent until released
                 */

                Truncate,

                /**
                 * @brief Ends the stream without a close_notify and closes its socket at once, so
                 * that anything the client sends after it draws a reset
                 */

                TruncateAndClose,
            };

            /**
             * @brief What the peer does: an optional exchange, then its ending
             */

            struct Script
            {
                Ending                                                          ending;

                /**
                 * @brief AwaitTheClient only: answer only once released
                 */

                bool                                                            isAnswerHeld;

                /**
                 * @brief The protocols the peer selects by ALPN, most preferred first; empty
                 * selects none
                 */

                std::vector< std::string >                                      alpnPreference;

                /**
                 * @brief When not empty, the peer reads one request head, sends this, and only then
                 * ends
                 */

                std::string                                                     response;

                explicit Script( SAA_in const Ending endingIn )
                    :
                    ending( endingIn ),
                    isAnswerHeld( false )
                {
                }
            };

            static auto makeScript(
                SAA_in          const Ending                                    ending,
                SAA_in          const bool                                      isAnswerHeld
                )
                -> Script
            {
                Script script( ending );

                script.isAnswerHeld = isAnswerHeld;

                return script;
            }

            TlsEndingPeer(
                SAA_in          const Ending                                    ending,
                SAA_in_opt      const bool                                      isAnswerHeld = false
                )
                :
                TlsEndingPeer( makeScript( ending, isAnswerHeld ) )
            {
            }

            explicit TlsEndingPeer( SAA_in const Script& script )
                :
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    ),
                m_acceptor( m_ioService ),
                m_port( 0U ),
                m_script( script ),
                m_isReleased( false )
            {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
                /*
                 * NO SESSION TICKETS. A TLS 1.3 server sends them after the handshake, and a client
                 * read which meets one completes a step inside asio and starts the next: between the
                 * two nothing is registered with the reactor, so a forced cancel which lands there
                 * reaps nothing and the read that follows waits for the peer. With none sent, the
                 * stream is silent after the handshake, and a read the client arms stays registered
                 * until something the case controls ends it
                 */

                ( void ) ::SSL_CTX_set_num_tickets( m_serverContext -> native_handle(), 0U );
#endif

                if( ! m_script.alpnPreference.empty() )
                {
                    bl::crypto::CryptoBase::setAlpnServerPreference( *m_serverContext, m_script.alpnPreference );
                }

                const bl::asio::ip::tcp::endpoint endpoint(
                    bl::asio::ip::address_v4::loopback(),
                    0 /* ephemeral */
                    );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                m_port = m_acceptor.local_endpoint().port();

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &TlsEndingPeer::run, this ) ) );
            }

            ~TlsEndingPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                release();

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept( ), so one throwaway connection does it - harmless when the connection
                     * under test has already been accepted
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                        ec
                        );

                    socket.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            bl::os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            /**
             * @brief Lets the peer answer a held close_notify, and then close its socket
             */

            void release()
            {
                BL_MUTEX_GUARD( m_lock );

                m_isReleased = true;

                m_cv.notify_all();
            }

            /**
             * @brief Blocks until a record which starts with 'prefix' has been made, or the bound
             * expires - signalled inside the record lock, so a case has a happens-before with it
             */

            bool waitForRecord( SAA_in const std::string& prefix ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this, &prefix ]() -> bool
                    {
                        for( const auto& record : m_records )
                        {
                            if( 0U == record.compare( 0U, prefix.size(), prefix ) )
                            {
                                return true;
                            }
                        }

                        return false;
                    }
                    );
            }

        private:

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );

                m_cv.notify_all();
            }

            void waitForRelease()
            {
                bl::os::mutex_unique_lock guard( m_lock );

                ( void ) m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_isReleased;
                    }
                    );
            }

            /**
             * @brief Reads until the stream ends and returns the code it ended with: eof is the
             * client's close_notify, asio.ssl.stream:1 the client's transport ending without one
             */

            static auto readUntilTheEnd( SAA_inout sslstream_t& stream ) -> bl::eh::error_code
            {
                char buffer[ 4096 ];

                for( ;; )
                {
                    bl::eh::error_code ec;

                    ( void ) stream.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec )
                    {
                        return ec;
                    }
                }
            }

            /**
             * @brief Reads to the end of one request head and stops there
             */

            static auto readRequestHead( SAA_inout sslstream_t& stream ) -> std::string
            {
                std::string data;

                char buffer[ 1024 ];

                while( std::string::npos == data.find( "\r\n\r\n" ) )
                {
                    bl::eh::error_code ec;

                    const auto transferred =
                        stream.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec || 0U == transferred )
                    {
                        break;
                    }

                    data.append( buffer, transferred );
                }

                return data;
            }

            void runExchange( SAA_inout sslstream_t& stream )
            {
                const auto head = readRequestHead( stream );

                const auto pos = head.find( "\r\n" );

                record( "request:" + ( std::string::npos == pos ? head : head.substr( 0U, pos ) ) );

                bl::eh::error_code ec;

                ( void ) bl::asio::write( stream, bl::asio::buffer( m_script.response ), ec );

                record( "responded:" + describeCode( ec ) );
            }

            void runEnding( SAA_inout sslstream_t& stream )
            {
                bl::eh::error_code ec;

                switch( m_script.ending )
                {
                    case Ending::CloseNotify:
                        {
                            stream.shutdown( ec );

                            record( "ended-with-close-notify:" + describeCode( ec ) );
                        }
                        break;

                    case Ending::AwaitTheClient:
                        {
                            ec = readUntilTheEnd( stream );

                            record( "client-ended:" + describeCode( ec ) );

                            if( m_script.isAnswerHeld )
                            {
                                waitForRelease();
                            }

                            stream.shutdown( ec );

                            record( "answered:" + describeCode( ec ) );
                        }
                        break;

                    case Ending::Truncate:
                        {
                            stream.next_layer().shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );

                            record( "truncated:" + describeCode( ec ) );

                            ec = readUntilTheEnd( stream );

                            record( "client-ended:" + describeCode( ec ) );
                        }
                        break;

                    case Ending::TruncateAndClose:
                        {
                            stream.next_layer().shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );

                            bl::eh::error_code closeEc;

                            stream.next_layer().close( closeEc );

                            record( "truncated-and-closed:" + describeCode( ec ) );
                        }
                        break;
                }
            }

            void run()
            {
                sslstream_t stream( m_ioService, *m_serverContext );

                try
                {
                    bl::eh::error_code ec;

                    m_acceptor.accept( stream.next_layer(), ec );

                    if( ec )
                    {
                        record( "accept-failed:" + describeCode( ec ) );

                        return;
                    }

                    stream.handshake( bl::asio::ssl::stream_base::server, ec );

                    record( "handshake:" + describeCode( ec ) );

                    if( ! ec )
                    {
                        if( ! m_script.response.empty() )
                        {
                            runExchange( stream );
                        }

                        runEnding( stream );
                    }
                }
                catch( std::exception& e )
                {
                    record( std::string( "failure:" ) + e.what() );
                }

                record( "script-ended" );

                /*
                 * THE SOCKET IS KEPT OPEN UNTIL THE CASE RELEASES IT, so that the peer's side going
                 * away can never be what ends the client's task
                 */

                waitForRelease();

                bl::eh::error_code ec;

                stream.next_layer().close( ec );
            }

            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::os::port_t                                                      m_port;
            const Script                                                        m_script;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::vector< std::string >                                          m_records;
            bool                                                                m_isReleased;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

    } // tlsendingpeer

} // utest

#endif /* __UTEST_TLSENDINGPEER_H_ */
