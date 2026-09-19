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

#ifndef __UTEST_TESTHTTP1CONNECTIONTASK_H_
#define __UTEST_TESTHTTP1CONNECTIONTASK_H_

#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/httpserver/HttpServer.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/HttpServerHelpers.h>
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.3 - the HTTP/1.1 driver (design 5.5)
 *
 * The driver under test is tasks::Http1ConnectionTaskT< STREAM >, reached the way the pool will
 * reach it: an S4.1 ClientConnectionTaskBaseT establishes the connection and hands the connected
 * stream to the driver factory, which is the fallback path the plan's S4.1 entry corrected - the
 * h2 task does NOT go through the factory, this driver is what does.
 *
 * WHAT THESE CASES ARE ABOUT. Two things the L2 review singled out as easy to get wrong silently:
 *
 *   - REUSE IS DERIVED HERE. The codec publishes httpVersion(), needsEof() and the header list and
 *     no keep-alive verdict at all, so each input of that derivation gets a case of its own, and
 *     the positive verdict is proved by actually putting a second request down the same connection
 *     rather than by reading a flag.
 *   - INTERIM RESPONSES ARRIVE AFTER THE FACT. The parser files a 1xx and restarts with no
 *     per-interim callback, so the driver delivers them when the final header section completes.
 *     The case pins the ORDER the sink sees, which is what that arrangement has to preserve.
 *
 * THE PEERS, AND WHY THERE ARE TWO. Http1Driver_AgainstTheLibraryHttpServerTests runs against
 * bl::httpserver::HttpServer, a real peer with a real parser - but that server cannot keep a
 * connection alive: Response.h puts 'Connection: close' on every response it builds and says why,
 * and HttpServerConnection is finished after one. So it proves the request/response path and the
 * NEGATIVE half of the derivation against something that is not a fake, and the scripted peer
 * below proves the half it cannot - reuse, HTTP/1.0, read-until-close, interim responses and
 * chunked trailers, none of which HttpServer can produce.
 *
 * The helpers are self contained and include no sibling test header, so this file can move to a
 * numbered sibling module later (src/utests/AGENTS.md forbids a cross-module test include).
 */

namespace utest
{
    namespace http1driver
    {
        enum : std::size_t
        {
            WAIT_TIMEOUT_IN_MILLISECONDS = 30000U,
        };

        /**
         * @brief UTF has no REQUIRE with a message of its own, and a bare "waitForClosed() has
         * failed" says nothing about WHY a stream never ended
         */

        inline void chkOrFail(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( ! condition )
            {
                UTF_FAIL( message );
            }
        }

        /**
         * @brief One header block as the sink received it
         */

        struct HeaderBlock
        {
            unsigned                                                            status;
            bool                                                                isInterim;
            bl::http::HeaderList                                                headers;

            HeaderBlock()
                :
                status( 0U ),
                isInterim( false )
            {
            }
        };

        /**
         * @brief The request task's half of the S2.6 contract, recording what arrived and in which
         * order
         *
         * It is a MAILBOX and nothing more, which is what design 5.2 rule L3 asks of a sink: the
         * driver calls it from its own strand, it appends under its own leaf lock and returns, and
         * it never calls back into the connection. The condition variable is signalled inside that
         * same lock, so a case which waits on it has a happens-before with everything the driver
         * delivered - the rendezvous the L3 protocol requires instead of a poll before an assertion
         */

        class RecordingSink : public bl::httpclient::ClientStreamEventSink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( RecordingSink, bl::httpclient::ClientStreamEventSink )

        protected:

            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cvClosed;

            std::vector< std::string >                                          m_events;
            std::vector< HeaderBlock >                                          m_blocks;
            bl::http::HeaderList                                                m_trailers;
            std::string                                                         m_body;

            bool                                                                m_closed;
            bool                                                                m_retryable;
            bl::eh::error_code                                                  m_errorCode;

            RecordingSink()
                :
                m_closed( false ),
                m_retryable( false )
            {
            }

        public:

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                HeaderBlock block;

                block.status = status;
                block.isInterim = isInterim;
                block.headers = BL_PARAM_FWD( headers );

                m_blocks.push_back( std::move( block ) );

                m_events.push_back(
                    "headers:" +
                    bl::utils::lexical_cast< std::string >( status ) +
                    ( isInterim ? ":interim" : ":final" )
                    );
            }

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                const auto size = data -> size() - data -> offset1();

                m_body.append(
                    reinterpret_cast< const char* >( data -> pv() ) + data -> offset1(),
                    size
                    );

                m_events.push_back( "data:" + bl::utils::lexical_cast< std::string >( size ) );
            }

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          bl::http::HeaderList&&                          trailers
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                m_trailers = BL_PARAM_FWD( trailers );

                m_events.push_back(
                    "trailers:" + bl::utils::lexical_cast< std::string >( m_trailers.size() )
                    );
            }

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                m_errorCode = errorCode;
                m_retryable = isRetryable;
                m_closed = true;

                m_events.push_back( errorCode ? "closed:error" : "closed:ok" );

                m_cvClosed.notify_all();

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Blocks until onClosed( ... ) arrived, or the bound expires
             *
             * Bounded so that a stream which never ends fails the case with a diagnosis rather
             * than hanging the suite
             */

            bool waitForClosed(
                SAA_in          const std::size_t                               timeoutInMilliseconds =
                                    static_cast< std::size_t >( WAIT_TIMEOUT_IN_MILLISECONDS )
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cvClosed.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_closed;
                    }
                    );
            }

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_events;
            }

            auto blocks() const -> std::vector< HeaderBlock >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_blocks;
            }

            auto body() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_body;
            }

            auto trailers() const -> bl::http::HeaderList
            {
                BL_MUTEX_GUARD( m_lock );

                return m_trailers;
            }

            auto errorCode() const -> bl::eh::error_code
            {
                BL_MUTEX_GUARD( m_lock );

                return m_errorCode;
            }

            bool isRetryable() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_retryable;
            }

            /**
             * @brief The final response's status, or zero when no final block arrived
             */

            unsigned finalStatus() const
            {
                BL_MUTEX_GUARD( m_lock );

                for( std::size_t i = 0U; i < m_blocks.size(); ++i )
                {
                    if( ! m_blocks[ i ].isInterim )
                    {
                        return m_blocks[ i ].status;
                    }
                }

                return 0U;
            }
        };

        typedef bl::om::ObjectImpl< RecordingSink > RecordingSinkImpl;

        /**
         * @brief A body source which is never read, because this driver refuses the request
         * carrying it
         *
         * It declares that it cannot rewind, which makes such a request unreplayable too - but
         * that is not what gets it refused here; a rewindable one is refused just the same,
         * because what HTTP/1.1 lacks is the request-side framing and not the ability to replay
         */

        class RefusingBodySource : public bl::httpclient::BodySource
        {
            BL_CTR_DEFAULT( RefusingBodySource, protected )
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( RefusingBodySource, bl::httpclient::BodySource )

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                BL_UNUSED( target );

                UTF_FAIL( "the HTTP/1.1 driver must not read a body source it refused" );

                return bl::httpclient::BodyReadResult();
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return false;
            }

            virtual void rewind() OVERRIDE
            {
                UTF_FAIL( "the HTTP/1.1 driver must not rewind a body source it refused" );
            }
        };

        typedef bl::om::ObjectImpl< RefusingBodySource > RefusingBodySourceImpl;

        /**
         * @brief A loopback HTTP/1.1 peer which runs a canned script on one connection
         *
         * Port zero, so the cases need no machine global test lock, and the script runs on a worker
         * thread so the test thread is free to wait on the sink's rendezvous. Its shape - a handler
         * plus recorded strings plus a bounded wait signalled inside the record lock - is the one
         * S3.5's suite arrived at after its poll-before-assert flake
         */

        class ScriptedPeer
        {
            BL_NO_COPY_OR_MOVE( ScriptedPeer )

        public:

            typedef bl::cpp::function
            <
                void (
                    SAA_inout       ScriptedPeer&                               peer,
                    SAA_inout       bl::asio::ip::tcp::socket&                  socket
                    )
            >
            script_t;

            ScriptedPeer( SAA_in script_t&& script )
                :
                m_acceptor(
                    m_ioService,
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    ),
                m_port( m_acceptor.local_endpoint().port() ),
                m_script( BL_PARAM_FWD( script ) ),
                m_released( false )
            {
                m_thread.reset( new bl::os::thread( bl::cpp::bind( &ScriptedPeer::run, this ) ) );
            }

            ~ScriptedPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                release();

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept(), so one throwaway connection does it - the same reason S3.5's fake
                     * proxy does it, and harmless when the script already ran
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

            bool waitForRecords(
                SAA_in          const std::size_t                               expected,
                SAA_in          const std::size_t                               timeoutInMilliseconds =
                                    static_cast< std::size_t >( WAIT_TIMEOUT_IN_MILLISECONDS )
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this, expected ]() -> bool
                    {
                        return m_records.size() >= expected;
                    }
                    );
            }

            /**
             * @brief Lets a script which is holding the connection open go on and close it
             *
             * THE RENDEZVOUS IN THE OTHER DIRECTION, and what makes the reuse verdict observable
             * at all. A connection this driver may reuse is one it leaves open, so the case has to
             * read state() while it is still open - and if the peer closed as soon as it had
             * answered, the driver's idle read would see the end of stream first and the case
             * would be reading the state of a connection the peer had already ended
             */

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
                    bl::os::chrono::milliseconds(
                        static_cast< std::size_t >( WAIT_TIMEOUT_IN_MILLISECONDS )
                        ),
                    [ this ]() -> bool
                    {
                        return m_released;
                    }
                    );
            }

            /*
             * The script vocabulary - all synchronous, all on the worker thread
             */

            /**
             * @brief Reads one complete request - the head, and the body its Content-Length declares
             *
             * Returns what it got when the peer closes instead of finishing, which is how a script
             * observes a request this driver refused to send at all
             */

            static auto readRequest( SAA_inout bl::asio::ip::tcp::socket& socket ) -> std::string
            {
                std::string data;

                char buffer[ 4096 ];

                for( ;; )
                {
                    const auto headEnd = data.find( "\r\n\r\n" );

                    if( std::string::npos != headEnd )
                    {
                        const auto bodyStart = headEnd + 4U;

                        if( data.size() - bodyStart >= contentLengthOf( data.substr( 0U, bodyStart ) ) )
                        {
                            return data;
                        }
                    }

                    bl::eh::error_code ec;

                    const auto transferred =
                        socket.read_some( bl::asio::buffer( buffer, sizeof( buffer ) ), ec );

                    if( ec || 0U == transferred )
                    {
                        return data;
                    }

                    data.append( buffer, transferred );
                }
            }

            static void send(
                SAA_inout       bl::asio::ip::tcp::socket&                      socket,
                SAA_in          const std::string&                              data
                )
            {
                bl::eh::error_code ec;

                ( void ) bl::asio::write( socket, bl::asio::buffer( data ), ec );
            }

            static void closeSocket( SAA_inout bl::asio::ip::tcp::socket& socket )
            {
                bl::eh::error_code ec;

                socket.shutdown( bl::asio::ip::tcp::socket::shutdown_both, ec );
                socket.close( ec );
            }

            /**
             * @brief The request line of a recorded request, for a readable assertion
             */

            static auto requestLineOf( SAA_in const std::string& request ) -> std::string
            {
                const auto pos = request.find( "\r\n" );

                return std::string::npos == pos ? request : request.substr( 0U, pos );
            }

            /**
             * @brief The body of a recorded request
             */

            static auto requestBodyOf( SAA_in const std::string& request ) -> std::string
            {
                const auto pos = request.find( "\r\n\r\n" );

                return std::string::npos == pos ? std::string() : request.substr( pos + 4U );
            }

            /**
             * @brief Whether a recorded request carries a field with this name, ASCII folded
             */

            static bool requestHasField(
                SAA_in          const std::string&                              request,
                SAA_in          const std::string&                              name
                )
            {
                return std::string::npos != toLowerAscii( request ).find( "\r\n" + toLowerAscii( name ) + ":" );
            }

            static auto toLowerAscii( SAA_in const std::string& value ) -> std::string
            {
                std::string result( value );

                for( std::size_t i = 0U; i < result.size(); ++i )
                {
                    if( result[ i ] >= 'A' && result[ i ] <= 'Z' )
                    {
                        result[ i ] = static_cast< char >( result[ i ] - 'A' + 'a' );
                    }
                }

                return result;
            }

        private:

            static std::size_t contentLengthOf( SAA_in const std::string& head )
            {
                const auto lowered = toLowerAscii( head );

                const auto pos = lowered.find( "\r\ncontent-length:" );

                if( std::string::npos == pos )
                {
                    return 0U;
                }

                const auto valueStart = pos + std::strlen( "\r\ncontent-length:" );
                const auto valueEnd = lowered.find( "\r\n", valueStart );

                if( std::string::npos == valueEnd )
                {
                    return 0U;
                }

                std::size_t result = 0U;

                for( auto i = valueStart; i < valueEnd; ++i )
                {
                    const auto ch = lowered[ i ];

                    if( ch >= '0' && ch <= '9' )
                    {
                        result = ( result * 10U ) + static_cast< std::size_t >( ch - '0' );
                    }
                }

                return result;
            }

            void run()
            {
                bl::asio::ip::tcp::socket socket( m_ioService );

                try
                {
                    bl::eh::error_code ec;

                    m_acceptor.accept( socket, ec );

                    if( ec )
                    {
                        record( "accept-failed" );

                        return;
                    }

                    m_script( *this, socket );
                }
                catch( std::exception& e )
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_failure = e.what();

                    m_cv.notify_all();
                }

                closeSocket( socket );
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            const bl::os::port_t                                                m_port;
            const script_t                                                      m_script;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::vector< std::string >                                          m_records;
            std::string                                                         m_failure;
            bool                                                                m_released;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /*************************************************************************
         * Establishing a connection the way the pool will
         */

        typedef bl::tasks::TcpSocketAsyncStrandedBase                           plain_stream_t;

        typedef bl::om::ObjectImpl
        <
            bl::tasks::ClientConnectionTaskBaseT< plain_stream_t >
        >
        PlainEstablisherImpl;

        typedef bl::tasks::Http1ConnectionTaskImpl< plain_stream_t >            PlainDriverImpl;

        /**
         * @brief A factory which builds THE driver under test for HTTP/1.1 and hands it back
         *
         * This is the path the plan's S4.1 entry corrected in the tree: the h2 task never goes
         * through the factory, because a driver which attachStream()s a stream created elsewhere
         * loses that policy's m_strand - so what the factory is actually for is exactly this
         */

        inline auto makeHttp1Factory(
            SAA_in          const std::shared_ptr< bl::om::ObjPtr< bl::httpclient::ClientConnection > >& slot
            )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< plain_stream_t > >
        {
            typedef bl::httpclient::ClientDriverFactoryT< plain_stream_t >      factory_t;

            auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                bl::httpclient::HttpProtocol::Http11,
                [ slot ](
                    SAA_in      const bl::httpclient::NegotiatedProtocol&       negotiated,
                    SAA_inout   plain_stream_t::stream_ref&&                    connectedStream,
                    SAA_in      const bl::httpclient::ConnectionKey&            key
                    )
                    -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    auto driver = PlainDriverImpl::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        bl::cpp::copy( key )
                        );

                    auto result = bl::om::qi< bl::httpclient::ClientConnection >( driver );

                    *slot = bl::om::copy( result );

                    return result;
                }
                );

            return factory;
        }

        inline auto makeKey(
            SAA_in          std::string                                         host,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::httpclient::ConnectionKey
        {
            bl::httpclient::ConnectionKey key;

            key.scheme = "http";
            key.host = BL_PARAM_FWD( host );
            key.port = port;

            return key;
        }

        inline auto makeRequest(
            SAA_in          const bl::os::port_t                                port,
            SAA_in          const std::string&                                  target = "/",
            SAA_in          const std::string&                                  method = "GET"
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( bl::cpp::copy( method ) );

            request.url(
                bl::net::Uri::parse(
                    "http://127.0.0.1:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        inline void chkTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;

            if( ! task -> isFailed() )
            {
                return;
            }

            std::string message( "<no exception>" );

            if( task -> exception() )
            {
                try
                {
                    cpp::safeRethrowException( task -> exception() );
                }
                catch( std::exception& e )
                {
                    message = e.what();
                }
            }

            UTF_FAIL( "the HTTP/1.1 driver task failed: " + message );
        }

        inline auto joinEvents( SAA_in const std::vector< std::string >& events ) -> std::string
        {
            std::string result;

            for( std::size_t i = 0U; i < events.size(); ++i )
            {
                if( ! result.empty() )
                {
                    result += "|";
                }

                result += events[ i ];
            }

            return result;
        }

        /**
         * @brief Establishes a connection to 'port' and gives the driver the factory built
         *
         * The establisher is run to completion first and only then is the driver scheduled, which
         * is deliberate: pushing the driver onto an execution queue from inside the factory would
         * be a connection strand taking a queue lock, which is what design 5.2 rule L2 forbids
         */

        inline auto establishDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port,
            SAA_in          const std::string&                                  host = "127.0.0.1"
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto slot =
                std::make_shared< om::ObjPtr< httpclient::ClientConnection > >();

            const auto establisher = PlainEstablisherImpl::createInstance(
                makeKey( cpp::copy( host ), port ),
                makeHttp1Factory( slot ),
                ProxyConfig::none(),
                ClientConnectionConfig(),
                false /* logExceptions */
                );

            const auto establisherTask = om::qi< Task >( establisher );

            eq -> push_back( establisherTask );
            eq -> wait( establisherTask );

            chkTaskSucceeded( establisherTask );

            UTF_REQUIRE( nullptr != slot -> get() );

            return om::copy( *slot );
        }

        /**
         * @brief What one request / response exchange produced - including the request the peer
         * read off the wire, for the cases whose subject is what this driver sent
         */

        struct ExchangeResult
        {
            std::string                                                         request;
            std::vector< std::string >                                          events;
            std::vector< HeaderBlock >                                          blocks;
            std::string                                                         body;
            bl::http::HeaderList                                                trailers;
            bl::httpclient::ConnectionState                                     stateAfterResponse;
            bool                                                                closed;

            ExchangeResult()
                :
                stateAfterResponse( bl::httpclient::ConnectionState::Closed ),
                closed( false )
            {
            }
        };

        /**
         * @brief One request, one canned response, and the connection state the driver settled on
         *
         * 'closeImmediately' is what a read-until-close response needs - the message has no end
         * other than the close - and everything else holds the connection open until the case has
         * read the verdict off it
         *
         * 'requestHeaders' are appended to the request exactly as given, which is how a case makes
         * the REQUEST an input of the exchange rather than only the response
         */

        inline auto runExchange(
            SAA_in          const std::string&                                  response,
            SAA_in          const bool                                          closeImmediately = false,
            SAA_in          const std::string&                                  method = "GET",
            SAA_in          const std::string&                                  target = "/resource",
            SAA_in          const bl::http::HeaderList&                         requestHeaders =
                                bl::http::HeaderList()
            )
            -> ExchangeResult
        {
            using namespace bl;
            using namespace bl::tasks;

            ExchangeResult result;

            ScriptedPeer peer(
                [ &response, closeImmediately ](
                    SAA_inout   ScriptedPeer&                                   self,
                    SAA_inout   asio::ip::tcp::socket&                          socket
                    ) -> void
                {
                    const auto request = ScriptedPeer::readRequest( socket );

                    ScriptedPeer::send( socket, response );

                    if( closeImmediately )
                    {
                        ScriptedPeer::closeSocket( socket );
                    }

                    /*
                     * The WHOLE request and not only its line: a case whose subject is a request
                     * header needs to see that the header reached the wire, and record( ) is what
                     * publishes it under the peer's own lock
                     */

                    self.record( bl::cpp::copy( request ) );

                    if( ! closeImmediately )
                    {
                        self.waitForRelease();
                    }
                }
                );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &result, &method, &target, &requestHeaders ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    auto request = makeRequest( peer.port(), target, method );

                    for( auto it = requestHeaders.begin(); it != requestHeaders.end(); ++it )
                    {
                        request.headers().append(
                            cpp::copy( it -> name() ),
                            cpp::copy( it -> value() )
                            );
                    }

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    result.closed = sink -> waitForClosed();

                    chkOrFail(
                        result.closed,
                        "the stream never ended; events so far: " + joinEvents( sink -> events() )
                        );

                    result.stateAfterResponse = driver -> state();

                    peer.release();

                    eq -> wait( driverTask );

                    chkTaskSucceeded( driverTask );
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            chkOrFail(
                peer.waitForRecords( 1U ),
                "the peer never recorded the request it read"
                );

            result.request = peer.records().front();

            result.events = sink -> events();
            result.blocks = sink -> blocks();
            result.body = sink -> body();
            result.trailers = sink -> trailers();

            return result;
        }

    } // http1driver

} // utest

/*
 * S4.3 DELIVERS A TEMPLATE OVER A STREAM POLICY, AND A TEMPLATE NOTHING INSTANTIATES IS NOT
 * COMPILED. That has bitten this project twice - BeastBoostImports.h, which no module includes, and
 * TcpTunnelStageT over a TLS policy, which S3.5's release validation passed straight over because
 * nothing had ever named the combination. The cases above exercise the driver over the cleartext
 * stranded policy; this explicit instantiation is what turns "it also works over the TLS one" from
 * a claim into a fact, by compiling every member of it.
 */

template class bl::om::ObjectImpl
<
    bl::tasks::Http1ConnectionTaskT< bl::tasks::TcpSslSocketAsyncStrandedBase >
>;

UTF_AUTO_TEST_CASE( Http1Driver_RequestResponseAndKeepAliveReuseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * THE POSITIVE HALF OF THE REUSE DERIVATION, PROVED BY REUSING. A first response which says
     * nothing about the connection leaves an HTTP/1.1 connection persistent, so the driver must put
     * it back in the Ready state with its slot free - and the thing which proves it is not the
     * state but the second request going down the same connection and being answered on it
     *
     * The second request is a POST with a buffered body, which also pins the framing this driver
     * owns: it SETS Content-Length from the body it will actually write, and the peer reads exactly
     * that many bytes rather than blocking for a body which never comes
     */

    const std::string body( "posted-body" );

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto first = ScriptedPeer::readRequest( socket );

            /*
             * RECORDED RATHER THAN ASSERTED, because this runs on the peer's worker thread and a
             * UTF_REQUIRE which fails there throws boost::execution_aborted, which is not an
             * std::exception and would leave the process rather than the case
             */

            self.record(
                ScriptedPeer::requestHasField( first, "Host" ) ? "host:yes" : "host:no"
                );

            self.record( "first:" + ScriptedPeer::requestLineOf( first ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "hello"
                );

            const auto second = ScriptedPeer::readRequest( socket );

            self.record( "second:" + ScriptedPeer::requestLineOf( second ) );
            self.record( "body:" + ScriptedPeer::requestBodyOf( second ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 201 Created\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "ok"
                );

            self.waitForRelease();
        }
        );

    const auto firstSink = RecordingSinkImpl::createInstance();
    const auto secondSink = RecordingSinkImpl::createInstance();

    auto stateAfterFirst = httpclient::ConnectionState::Closed;
    auto stateAfterSecond = httpclient::ConnectionState::Ready;

    std::size_t slotsAfterFirst = 0U;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            const auto driver = establishDriver( eq, peer.port() );
            const auto driverTask = om::qi< Task >( driver );

            eq -> push_back( driverTask );

            UTF_REQUIRE_EQUAL( driver -> negotiated().protocol(), httpclient::HttpProtocol::Http11 );

            /*
             * A cleartext connection was not decided by ALPN, so it reports no identifier - the
             * value S4.1 constructs through withoutAlpn, travelling unchanged through the factory
             */

            UTF_REQUIRE( ! driver -> negotiated().hasAlpn() );

            const auto firstHandle = driver -> submit(
                makeRequest( peer.port(), "/first" ),
                om::qi< httpclient::ClientStreamEventSink >( firstSink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != firstHandle );

            chkOrFail(
                firstSink -> waitForClosed(),
                "the first stream never ended; events so far: " + joinEvents( firstSink -> events() )
                );

            stateAfterFirst = driver -> state();
            slotsAfterFirst = driver -> freeStreamSlots();

            auto request = makeRequest( peer.port(), "/second", "POST" );

            const auto block = data::DataBlock::createInstance( body.size() );

            std::memcpy( block -> pv(), body.c_str(), body.size() );

            block -> setSize( body.size() );

            request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );

            const auto secondHandle = driver -> submit(
                request,
                om::qi< httpclient::ClientStreamEventSink >( secondSink )
                );

            chkOrFail(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE != secondHandle,
                "the connection refused a second request, so it was not reused"
                );

            UTF_REQUIRE( secondHandle != firstHandle );

            chkOrFail(
                secondSink -> waitForClosed(),
                "the second stream never ended; events so far: " + joinEvents( secondSink -> events() )
                );

            stateAfterSecond = driver -> state();

            peer.release();

            eq -> wait( driverTask );

            chkTaskSucceeded( driverTask );
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    /*
     * The first exchange - persistent, so the connection went back to Ready with its slot free
     */

    UTF_REQUIRE_EQUAL(
        joinEvents( firstSink -> events() ),
        std::string( "headers:200:final|data:5|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( firstSink -> body(), std::string( "hello" ) );
    UTF_REQUIRE_EQUAL( firstSink -> finalStatus(), 200U );
    UTF_REQUIRE_EQUAL( stateAfterFirst, httpclient::ConnectionState::Ready );
    UTF_REQUIRE_EQUAL( slotsAfterFirst, 1U );

    const auto blocks = firstSink -> blocks();

    UTF_REQUIRE_EQUAL( blocks.size(), 1U );
    UTF_REQUIRE( ! blocks[ 0 ].isInterim );
    UTF_REQUIRE( nullptr != blocks[ 0 ].headers.tryGet( "content-type" ) );

    /*
     * The second exchange - the server said close, so the connection did not go back to the pool
     */

    UTF_REQUIRE_EQUAL(
        joinEvents( secondSink -> events() ),
        std::string( "headers:201:final|data:2|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( secondSink -> body(), std::string( "ok" ) );
    UTF_REQUIRE( httpclient::ConnectionState::Ready != stateAfterSecond );

    UTF_REQUIRE( peer.waitForRecords( 4U ) );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 4U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "host:yes" ) );
    UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "first:GET /first HTTP/1.1" ) );
    UTF_REQUIRE_EQUAL( records[ 2 ], std::string( "second:POST /second HTTP/1.1" ) );
    UTF_REQUIRE_EQUAL( records[ 3 ], std::string( "body:" ) + body );
}

UTF_AUTO_TEST_CASE( Http1Driver_ReuseVerdictInputsTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    /*
     * EACH INPUT OF THE DERIVATION, ON ITS OWN. The codec answers none of this - it publishes
     * httpVersion(), needsEof() and the header list and no verdict - so every rule the driver
     * applies is a rule only these cases hold it to
     */

    {
        /*
         * Connection: close on an HTTP/1.1 response
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: close\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * The same, spelled as one token of a list and in a case the wire is free to choose. The
         * fold is ASCII only and never through std::locale(), which is the rule the whole codec
         * follows
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: Keep-Alive, CLOSE\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * HTTP/1.0 without keep-alive - persistence is opt-in below HTTP/1.1
         */

        const auto result = runExchange(
            "HTTP/1.0 200 OK\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * HTTP/1.0 WITH keep-alive - persistent, and the one case where the version alone would
         * have given the wrong answer
         */

        const auto result = runExchange(
            "HTTP/1.0 200 OK\r\n"
            "Content-Length: 3\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "abc"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );
        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }

    {
        /*
         * A body framed by the close: it is delivered in full and the connection cannot be reused,
         * because there is no end to such a message other than the close which just happened
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "until-the-close",
            true /* closeImmediately */
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "until-the-close" ) );
        UTF_REQUIRE_EQUAL( result.blocks.size(), 1U );
        UTF_REQUIRE_EQUAL( result.blocks[ 0 ].status, 200U );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * THE ONE INPUT WHICH IS NOT THE RESPONSE'S, AND THE ONLY WAY TO TELL IT APART IS A SERVER
         * WHICH DOES NOT ECHO. The request said close; the response says nothing about the
         * connection, is HTTP/1.1, is framed by a Content-Length and leaves no bytes over - so
         * every response-side input says reusable and only the request says otherwise. RFC 9112
         * section 9.6 puts the rule on the sender: a client which sent close MUST NOT send another
         * request on that connection, and the server MUST close after its final response while
         * only SHOULD echoing the token. A driver which derived this verdict from the response
         * alone would report Ready here, the pool would hand the connection to the next request,
         * and that request would reach a socket the server was already closing - and fail
         * NON-retryably, because its bytes did go out
         */

        http::HeaderList requestHeaders;

        requestHeaders.append( "Connection", "close" );

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "abc",
            false /* closeImmediately */,
            "GET",
            "/resource",
            requestHeaders
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "abc" ) );

        /*
         * The token reached the wire untouched - serializeRequestHead( ) passes the caller's fields
         * through - which is what puts the server under the rule in the first place. Without this
         * the case could pass on a driver which refused the header instead of honouring it
         */

        UTF_REQUIRE( ScriptedPeer::requestHasField( result.request, "Connection" ) );

        UTF_REQUIRE(
            std::string::npos !=
                ScriptedPeer::toLowerAscii( result.request ).find( "\r\nconnection: close\r\n" )
            );

        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }
}

UTF_AUTO_TEST_CASE( Http1Driver_InterimResponsesPrecedeTheFinalBlockTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    /*
     * INTERIM RESPONSES ARRIVE AFTER THE FACT, AND THE ORDER THE SINK SEES IS WHAT MUST SURVIVE IT.
     * The parser files a 1xx and restarts on the same buffer with no per-interim callback, so the
     * driver cannot deliver a 103 when it reaches the wire - it delivers every filed interim, in
     * order, immediately before the final block. What this case pins is that ordering and the
     * sink's status/flag invariant, and that the hints themselves are not lost on the way
     */

    const auto result = runExchange(
        "HTTP/1.1 100 Continue\r\n"
        "\r\n"
        "HTTP/1.1 103 Early Hints\r\n"
        "Link: </style.css>; rel=preload; as=style\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "done"
        );

    UTF_REQUIRE_EQUAL(
        joinEvents( result.events ),
        std::string( "headers:100:interim|headers:103:interim|headers:200:final|data:4|closed:ok" )
        );

    UTF_REQUIRE_EQUAL( result.blocks.size(), 3U );

    UTF_REQUIRE( result.blocks[ 0 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 0 ].status, 100U );

    UTF_REQUIRE( result.blocks[ 1 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 1 ].status, 103U );

    const auto* const link = result.blocks[ 1 ].headers.tryGet( "link" );

    UTF_REQUIRE( nullptr != link );
    UTF_REQUIRE_EQUAL( *link, std::string( "</style.css>; rel=preload; as=style" ) );

    UTF_REQUIRE( ! result.blocks[ 2 ].isInterim );
    UTF_REQUIRE_EQUAL( result.blocks[ 2 ].status, 200U );

    UTF_REQUIRE_EQUAL( result.body, std::string( "done" ) );

    /*
     * An interim response says nothing about the connection, so the final one decides - and this
     * one leaves it persistent
     */

    UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
}

UTF_AUTO_TEST_CASE( Http1Driver_ChunkedTrailersAndBodilessTests )
{
    using namespace bl;
    using namespace utest::http1driver;

    {
        /*
         * A chunked body reaches the sink de-chunked, and the trailer section reaches it as
         * trailers rather than as headers - keeping them apart is what stops a trailer becoming a
         * header injection, which is the codec's rule and is preserved here
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6\r\n world\r\n"
            "0\r\n"
            "X-Checksum: 9f2b\r\n"
            "\r\n"
            );

        UTF_REQUIRE_EQUAL( result.body, std::string( "hello world" ) );
        UTF_REQUIRE_EQUAL( result.trailers.size(), 1U );

        const auto* const checksum = result.trailers.tryGet( "x-checksum" );

        UTF_REQUIRE( nullptr != checksum );
        UTF_REQUIRE_EQUAL( *checksum, std::string( "9f2b" ) );

        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );

        /*
         * The ORDER is the point of the event list: the trailers arrive after the last body chunk
         * and before onClosed, which is the sink's contract
         */

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:200:final|data:11|trailers:1|closed:ok" )
            );
    }

    {
        /*
         * A trailer section carrying a field RFC 9110 section 6.5.1 forbids there. The rule is the
         * codec's and is tested with it; what is this driver's, and what this pins, is that the
         * refusal reaches the request as a FAILED stream and takes the connection with it - there
         * are bytes on it which this client has decided not to interpret, so there is nothing safe
         * to do with it but end it
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "0\r\n"
            "Expires: Wed, 21 Oct 2026 07:28:00 GMT\r\n"
            "\r\n"
            );

        UTF_REQUIRE( result.trailers.empty() );
        UTF_REQUIRE( std::string::npos != joinEvents( result.events ).find( "closed:error" ) );
        UTF_REQUIRE( httpclient::ConnectionState::Ready != result.stateAfterResponse );
    }

    {
        /*
         * 204 carries no body whatever it says, so nothing follows the header block
         */

        const auto result = runExchange(
            "HTTP/1.1 204 No Content\r\n"
            "\r\n"
            );

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:204:final|closed:ok" )
            );

        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }

    {
        /*
         * A response to HEAD carries a Content-Length and no body, which only a parser TOLD the
         * request was a HEAD can frame. The driver is what tells it, from the method it is about to
         * write - so this case fails outright if that never reaches the parser
         */

        const auto result = runExchange(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 1024\r\n"
            "\r\n",
            false /* closeImmediately */,
            "HEAD"
            );

        UTF_REQUIRE_EQUAL(
            joinEvents( result.events ),
            std::string( "headers:200:final|closed:ok" )
            );

        UTF_REQUIRE( result.body.empty() );
        UTF_REQUIRE_EQUAL( result.stateAfterResponse, httpclient::ConnectionState::Ready );
    }
}

UTF_AUTO_TEST_CASE( Http1Driver_RequestsThisDriverRefusesTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * TWO REFUSALS, AND NEITHER OF THEM PUTS A BYTE ON THE WIRE. A streaming body is refused at
     * submit( ... ), because HTTP/1.1 would need request-side chunked framing for a body of unknown
     * length and S2.5's serializer has none - a driver which took the request and wrote only its
     * head would leave the server waiting for a body forever. A caller supplied Transfer-Encoding
     * is refused for the same reason, and refusing it is a framing defense: writing the head as
     * though this driver honoured the coding is how a request gets smuggled
     *
     * In both cases the connection itself is untouched and stays usable, which the final exchange
     * on the same connection proves
     */

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( "served:" + ScriptedPeer::requestLineOf( request ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "ok"
                );

            self.waitForRelease();
        }
        );

    const auto refusedSink = RecordingSinkImpl::createInstance();
    const auto acceptedSink = RecordingSinkImpl::createInstance();

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            const auto driver = establishDriver( eq, peer.port() );
            const auto driverTask = om::qi< Task >( driver );

            eq -> push_back( driverTask );

            /*
             * A streaming body source, refused synchronously
             */

            {
                auto request = makeRequest( peer.port(), "/streamed", "POST" );

                request.bodySource(
                    om::ObjPtrCopyable< httpclient::BodySource >(
                        om::qi< httpclient::BodySource >( RefusingBodySourceImpl::createInstance() )
                        )
                    );

                UTF_REQUIRE_EQUAL(
                    driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( refusedSink )
                        ),
                    static_cast< httpclient::stream_handle_t >(
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE
                        )
                    );
            }

            /*
             * A caller supplied transfer coding - accepted as a handle and then failed as a
             * stream, because nothing of it was written
             */

            {
                auto request = makeRequest( peer.port(), "/chunked", "POST" );

                request.headers().append( "Transfer-Encoding", "chunked" );

                const auto handle = driver -> submit(
                    request,
                    om::qi< httpclient::ClientStreamEventSink >( refusedSink )
                    );

                UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                chkOrFail(
                    refusedSink -> waitForClosed(),
                    "the refused stream never ended"
                    );

                UTF_REQUIRE( refusedSink -> errorCode() );

                /*
                 * Nothing was written, so the request is provably unprocessed
                 */

                UTF_REQUIRE( refusedSink -> isRetryable() );
            }

            /*
             * ... and the connection is still good
             */

            UTF_REQUIRE_EQUAL( driver -> state(), httpclient::ConnectionState::Ready );

            const auto handle = driver -> submit(
                makeRequest( peer.port(), "/after" ),
                om::qi< httpclient::ClientStreamEventSink >( acceptedSink )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

            chkOrFail(
                acceptedSink -> waitForClosed(),
                "the stream after the refusals never ended"
                );

            peer.release();

            eq -> wait( driverTask );

            chkTaskSucceeded( driverTask );
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    UTF_REQUIRE_EQUAL( refusedSink -> events().size(), 1U );
    UTF_REQUIRE_EQUAL( refusedSink -> events()[ 0 ], std::string( "closed:error" ) );

    UTF_REQUIRE_EQUAL(
        joinEvents( acceptedSink -> events() ),
        std::string( "headers:200:final|data:2|closed:ok" )
        );

    UTF_REQUIRE( peer.waitForRecords( 1U ) );

    const auto records = peer.records();

    UTF_REQUIRE_EQUAL( records.size(), 1U );
    UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "served:GET /after HTTP/1.1" ) );
}

UTF_AUTO_TEST_CASE( Http1Driver_AgainstTheLibraryHttpServerTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::http1driver;

    /*
     * THE REAL PEER. Everything above is scripted bytes; this is the library's own HttpServer, with
     * its own parser and its own response builder, and what it proves is that the request this
     * driver renders is one a real server accepts and that the response a real server writes is one
     * this driver reads
     *
     * It also proves the NEGATIVE half of the reuse derivation against something that is not a
     * fake, and it is the only peer here which can: HttpServer puts 'Connection: close' on every
     * response it builds - Response.h says so, and says it does not implement persistent
     * connections - so the driver must notice and not return the connection to the pool. That the
     * scripted peer could say the same word is not the same as a real server saying it
     */

    const auto sink = RecordingSinkImpl::createInstance();

    auto stateAfterResponse = httpclient::ConnectionState::Ready;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        [ & ]() -> void
        {
            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto port = static_cast< os::port_t >( test::UtfArgsParser::port() );

                    const auto driver = establishDriver( eq, port, test::UtfArgsParser::host() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    httpclient::ClientRequest request;

                    request.method( "GET" );

                    request.url(
                        net::Uri::parse(
                            "http://" +
                            test::UtfArgsParser::host() +
                            ":" +
                            utils::lexical_cast< std::string >( port ) +
                            utest::http::g_requestUri
                            )
                        );

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE(
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle
                        );

                    chkOrFail(
                        sink -> waitForClosed(),
                        "the stream against HttpServer never ended; events so far: " +
                            joinEvents( sink -> events() )
                        );

                    stateAfterResponse = driver -> state();

                    eq -> wait( driverTask );

                    chkTaskSucceeded( driverTask );
                }
                );
        }
        );

    UTF_REQUIRE_EQUAL( sink -> finalStatus(), 200U );
    UTF_REQUIRE_EQUAL( sink -> body(), utest::http::g_desiredResult );
    UTF_REQUIRE( ! sink -> errorCode() );

    chkOrFail(
        httpclient::ConnectionState::Ready != stateAfterResponse,
        "HttpServer said 'Connection: close' and the driver returned the connection to the pool"
        );
}

#endif /* __UTEST_TESTHTTP1CONNECTIONTASK_H_ */
