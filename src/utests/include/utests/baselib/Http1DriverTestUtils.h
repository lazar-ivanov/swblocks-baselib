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

#ifndef __UTEST_HTTP1DRIVERTESTUTILS_H_
#define __UTEST_HTTP1DRIVERTESTUTILS_H_

#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

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
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The helpers the HTTP/1.1 DRIVER suites share - S4.3, moved here by S6R.2
 *
 * It lives here and not in a module directory because two modules need it now:
 * utf_baselib_httpclient3 carries the S4.3 suites and utf_baselib_httpclient7 carries S6R.2's,
 * and a test header may never be included across module directories - that silently duplicates
 * its cases into two binaries (src/utests/AGENTS.md). Copying the block into both would be the
 * other way out and is worse, which is the same argument utests/baselib/Http2DriverTestUtils.h
 * makes for the HTTP/2 driver.
 *
 * THE BLOCK BELOW IS VERBATIM what utf_baselib_httpclient3/TestHttp1ConnectionTask.h carried
 * before S6R.2 - moved, not edited, so the preprocessed translation unit of that module is
 * unchanged and its object size must not move. What did NOT move is everything that was outside
 * the namespace: the HttpServerHelpers.h include and the real-server case behind it, and the
 * explicit instantiation over the TLS stranded policy. Both stay where their cases are, which is
 * what keeps a module that does not want the library's own HTTP server from paying for it
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

            /**
             * @brief Recorded so that a case can assert it never happens on this driver
             *
             * The HTTP/1.1 driver refuses a BodySource at submit( ), so it can never pull for
             * upload - see Http1ConnectionTask::provideBody( ). Recording the call rather than
             * ignoring it is what turns that from a claim into something a case can check
             */

            virtual void onBodyWanted(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                m_events.push_back(
                    "wanted:" + bl::utils::lexical_cast< std::string >( bytes )
                    );
            }

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

        /**
         * @brief The driver under test with a door onto its OWN strand, for the one question
         * which can only be asked between the write going out and its completion
         *
         * WHY BOTH POSTS COME FROM A STRAND HANDLER, AND WHY NOTHING WEAKER IS DETERMINISTIC.
         * The case asks what the driver would tell a sink if the peer closed while the request
         * write was in flight - so it needs the peer-close to be handled AFTER onStartRequest( )
         * has issued the write and BEFORE onWriteCompleted( ) runs. Issuing submit( ) and the
         * probe post from a handler already running on the strand settles that before either can
         * run: the strand's FIFO takes [ onStartRequest, probe ] while this handler still holds
         * it, and asio never invokes a completion handler from inside the initiating call, so the
         * write completion can only be queued behind both.
         *
         * Posted from the TEST thread instead, the same two calls race the strand: onStartRequest
         * may finish and release it before the probe post lands, and a loopback write completes
         * in microseconds - so the "red" run would be green about half the time. Ordering by
         * thread scheduling is not ordering.
         *
         * THE PARTIAL STATUS LINE IS NOT DECORATION. Beast's put_eof( ) opens with
         * BOOST_ASSERT( got_some( ) ), and NDEBUG is defined only by the release toolchain files,
         * so calling onPeerClosed( ) on a parser which has seen no byte aborts a debug build
         * rather than failing the stream. Feeding one partial status line first is also the
         * realistic shape of this defect: the server began answering and the connection died
         */

        class Http1DriverProbe : public bl::tasks::Http1ConnectionTaskT< plain_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( Http1DriverProbe )

        public:

            typedef bl::tasks::Http1ConnectionTaskT< plain_stream_t >           base_type;

        protected:

            bl::httpclient::ClientRequest                                       m_probeRequest;
            bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >             m_probeSink;

            /*
             * Written on the strand before the sink is told, and read by the case after the
             * sink's rendezvous - so the sink's own lock orders the two. A UTF assertion here
             * would be one made from a pool thread, which Boost.Test does not support
             */

            bool                                                                m_probeParsed;

            Http1DriverProbe(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_inout       plain_stream_t::stream_ref&&                    connectedStream,
                SAA_in          bl::httpclient::ConnectionKey                   key
                )
                :
                base_type(
                    BL_PARAM_FWD( negotiated ),
                    BL_PARAM_FWD( connectedStream ),
                    BL_PARAM_FWD( key )
                    ),
                m_probeParsed( false )
            {
            }

            virtual void scheduleTask(
                SAA_in          const std::shared_ptr< bl::tasks::ExecutionQueue >&     eq
                ) OVERRIDE
            {
                /*
                 * The base arms the read and publishes m_started - both synchronously - so by the
                 * time the handler below runs, submit( ) will post rather than defer
                 */

                base_type::scheduleTask( eq );

                const auto ref = base_type::selfRef();

                base_type::postToStreamExecutor(
                    [ this, ref ]() -> void
                    {
                        submitAndProbe();
                    }
                    );
            }

            void submitAndProbe()
            {
                ( void ) base_type::submit( m_probeRequest, m_probeSink );

                const auto ref = base_type::selfRef();

                base_type::postToStreamExecutor(
                    [ this, ref ]() -> void
                    {
                        peerClosedProbe();
                    }
                    );
            }

            void peerClosedProbe()
            {
                /*
                 * The parser exists because onStartRequest( ) ran first, which is the ordering
                 * this probe is built on - checked rather than assumed away, so that a future
                 * change which breaks that ordering fails the case with a diagnosis instead of
                 * dereferencing a null pointer. onPeerClosed( ) itself already guards it
                 */

                if( ! base_type::m_parser )
                {
                    return;
                }

                const std::string partial( "HTTP/1.1 200 OK\r\n" );

                bl::eh::error_code ec;

                const auto consumed =
                    base_type::m_parser -> parse( partial.c_str(), partial.size(), ec );

                m_probeParsed = ! ec && consumed == partial.size();

                base_type::onPeerClosed();
            }

        public:

            /**
             * @brief Handed over before the task is scheduled, so nothing here races the strand
             */

            void probeWith(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& sink
                )
            {
                m_probeRequest = request;
                m_probeSink = bl::om::copy( sink );
            }

            bool probeParsed() const NOEXCEPT
            {
                return m_probeParsed;
            }
        };

        typedef bl::om::ObjectImpl< Http1DriverProbe >                          Http1DriverProbeImpl;

        /**
         * @brief makeHttp1Factory( )'s sibling, building the probe above instead of the driver
         */

        inline auto makeProbeFactory(
            SAA_in          const std::shared_ptr< bl::om::ObjPtr< Http1DriverProbeImpl > >& slot
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
                    auto driver = Http1DriverProbeImpl::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        bl::cpp::copy( key )
                        );

                    auto result = bl::om::qi< bl::httpclient::ClientConnection >( driver );

                    *slot = bl::om::copy( driver );

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
         * @brief establishDriver( )'s sibling, which hands back the probe rather than the
         * ClientConnection - the case needs the derived type to arm it before it is scheduled
         */

        inline auto establishProbeDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::om::ObjPtr< Http1DriverProbeImpl >
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto slot = std::make_shared< om::ObjPtr< Http1DriverProbeImpl > >();

            const auto establisher = PlainEstablisherImpl::createInstance(
                makeKey( std::string( "127.0.0.1" ), port ),
                makeProbeFactory( slot ),
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

#endif /* __UTEST_HTTP1DRIVERTESTUTILS_H_ */
