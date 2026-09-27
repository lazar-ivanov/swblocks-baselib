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

#ifndef __UTEST_HTTP1DRIVERTLSTESTUTILS_H_
#define __UTEST_HTTP1DRIVERTLSTESTUTILS_H_

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The TLS side of the HTTP/1.1 DRIVER suites - lifted here by CS-1 of astra's second review
 *
 * It lives here and not in a module directory because two CS-1 modules need it:
 * utf_baselib_httpclient8 carries D1's cases, and D2's go to utf_baselib_httpclient11 - and to
 * utf_baselib_httpclient12 as well if the first measures over its budget - while a test header may
 * never be included across module directories (src/utests/AGENTS.md). The peer below moved here from
 * utf_baselib_httpclient8/TestHttp1DriverTlsTruncation.h, where it was written.
 *
 * It is NOT utf_baselib_httpclient5's peer, which stays where it is: one TLS HTTP/1.1 peer for the
 * whole tree is owed, and is a tidy of its own
 */

namespace utest
{
    namespace http1drivertls
    {
        enum : std::size_t
        {
            /**
             * @brief How long the peer waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,
        };

        /**
         * @brief class TlsPeer - a loopback HTTP/1.1 peer over TLS which runs a canned script on
         * one connection and records HOW its stream ended
         *
         * SELF CONTAINED, for the reason utf_baselib_httpclient5's TestClientSessionTlsHttp1.h
         * gives for its own: a test header which includes a sibling module's header silently
         * duplicates its cases into two binaries (src/utests/AGENTS.md). Its shape is that peer's -
         * ephemeral IPv4 loopback port, the script on a worker thread, the client connecting to
         * "localhost" so that the test server certificate's name verifies, and "http/1.1" alone as
         * the ALPN preference. What it adds is the two ways a SERVER ends a TLS stream, which is the
         * whole axis of these cases
         */

        class TlsPeer
        {
            BL_NO_COPY_OR_MOVE( TlsPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            typedef bl::cpp::function
            <
                void (
                    SAA_inout       TlsPeer&                                    peer,
                    SAA_inout       sslstream_t&                                stream
                    )
            >
            script_t;

            TlsPeer( SAA_in script_t&& script )
                :
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    ),
                m_acceptor( m_ioService ),
                m_port( 0U ),
                m_script( BL_PARAM_FWD( script ) ),
                m_released( false ),
                m_hasScriptEnded( false )
            {
                std::vector< std::string > preference;

                preference.push_back( "http/1.1" );

                bl::crypto::CryptoBase::setAlpnServerPreference( *m_serverContext, preference );

                const bl::asio::ip::tcp::endpoint endpoint(
                    bl::asio::ip::address_v4::loopback(),
                    0 /* ephemeral */
                    );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                m_port = m_acceptor.local_endpoint().port();

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &TlsPeer::run, this ) ) );
            }

            ~TlsPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                release();

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept( ), so one throwaway connection does it - harmless when the script
                     * has already run
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint(
                            bl::asio::ip::address_v4::loopback(),
                            m_port
                            ),
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

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );

                m_cv.notify_all();
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            auto failure() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_failure;
            }

            void release()
            {
                BL_MUTEX_GUARD( m_lock );

                m_released = true;

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
                        return m_released;
                    }
                    );
            }

            /**
             * @brief Blocks until the script has returned, or the bound expires
             *
             * THE RENDEZVOUS THE CASES TEAR DOWN ON. Every script here ends by waiting for the
             * client's answer to the ending it chose, so a script which has returned is a peer
             * which has seen the driver observe that ending and close - signalled inside the
             * record lock, so the case has a happens-before with everything the script recorded
             */

            bool waitForScriptEnd() const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_hasScriptEnded;
                    }
                    );
            }

            /*
             * The script vocabulary - all synchronous, all on the worker thread
             */

            /**
             * @brief Reads to the end of the request head and stops there
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

            static void send(
                SAA_inout       sslstream_t&                                    stream,
                SAA_in          const std::string&                              data
                )
            {
                bl::eh::error_code ec;

                ( void ) bl::asio::write( stream, bl::asio::buffer( data ), ec );
            }

            /**
             * @brief ENDS THE STREAM WITHOUT A close_notify - the transport's FIN and nothing else
             *
             * The send side of the TRANSPORT is shut down under the TLS session, so the client's
             * next read finds the byte stream over with no closure alert before it, which is the
             * truncation of RFC 9112 section 9.8. The receive side stays open, and that is what
             * lets observeStreamEnd( ) take the client's own answer afterwards instead of closing
             * on it - a close with the client's close_notify unread would put a reset on the wire
             */

            static void endWithoutCloseNotify( SAA_inout sslstream_t& stream )
            {
                bl::eh::error_code ec;

                stream.next_layer().shutdown( bl::asio::ip::tcp::socket::shutdown_send, ec );
            }

            /**
             * @brief ENDS THE STREAM WITH A close_notify, and waits for the client's in answer
             *
             * asio's synchronous shutdown( ) sends the alert and then reads until the client's own
             * arrives, which it does when the client closes the connection on the ending - so what
             * it returns is how the client answered: nothing, when its close_notify came back, and
             * the truncation, when it did not
             */

            static auto endWithCloseNotify( SAA_inout sslstream_t& stream ) -> bl::eh::error_code
            {
                bl::eh::error_code ec;

                stream.shutdown( ec );

                return ec;
            }

            /**
             * @brief Reads until the stream ends and returns the code it ended with
             *
             * asio::error::eof means the client sent a close_notify and this peer processed it;
             * asio.ssl.stream:1 means the client's transport ended without one - the same mapping
             * the endings above rely on, from this side
             */

            static auto observeStreamEnd( SAA_inout sslstream_t& stream ) -> bl::eh::error_code
            {
                char buffer[ 16U * 1024U ];

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
             * @brief Whether a code is the truncated TLS stream - spelled the way TcpSslBaseTasks.h's
             * isExpectedSslErrorCode( ) spells it, so that an assertion reads the same as the
             * library's own predicate for the same ending
             */

            static bool isTruncated( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
            {
                return std::string( "asio.ssl.stream" ) == ec.category().name() && 1 == ec.value();
            }

            static auto describe( SAA_in const bl::eh::error_code& ec ) -> std::string
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

            static auto requestLineOf( SAA_in const std::string& request ) -> std::string
            {
                const auto pos = request.find( "\r\n" );

                return std::string::npos == pos ? request : request.substr( 0U, pos );
            }

        private:

            void run()
            {
                sslstream_t stream( m_ioService, *m_serverContext );

                try
                {
                    bl::eh::error_code ec;

                    m_acceptor.accept( stream.next_layer(), ec );

                    if( ec )
                    {
                        record( "accept-failed" );

                        return;
                    }

                    stream.handshake( bl::asio::ssl::stream_base::server, ec );

                    if( ec )
                    {
                        record( "handshake-failed:" + describe( ec ) );

                        return;
                    }

                    m_script( *this, stream );
                }
                catch( std::exception& e )
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_failure = e.what();

                    m_cv.notify_all();
                }

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_hasScriptEnded = true;

                    m_cv.notify_all();
                }

                bl::eh::error_code ec;

                stream.next_layer().close( ec );
            }

            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::os::port_t                                                      m_port;
            const script_t                                                      m_script;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::vector< std::string >                                          m_records;
            std::string                                                         m_failure;
            bool                                                                m_released;
            bool                                                                m_hasScriptEnded;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

    } // http1drivertls

} // utest

#endif /* __UTEST_HTTP1DRIVERTLSTESTUTILS_H_ */
