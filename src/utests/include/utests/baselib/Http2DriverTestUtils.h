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

#ifndef __UTEST_HTTP2DRIVERTESTUTILS_H_
#define __UTEST_HTTP2DRIVERTESTUTILS_H_

#include <baselib/http2/Http2ConnectionTask.h>

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/SimpleTaskControlToken.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/RawFrameScriptPeer.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The helpers the HTTP/2 DRIVER suites share - S4.2
 *
 * It lives here and not in a module directory because two modules need it:
 * utf_baselib_h2client2 drives the driver over cleartext and utf_baselib_h2client3 drives it over
 * TLS, and a test header may never be included across module directories - that silently
 * duplicates its cases into two binaries (src/utests/AGENTS.md). Copying the block into both would
 * be the other way out and is worse.
 *
 * EVERYTHING WHICH COSTS A SESSION INSTANTIATION IS A TEMPLATE HERE, deliberately. DriverProbeT,
 * FallbackDriverT, makeFallbackFactory, withPeer and runDriver are parameterized, so a module pays
 * only for the stream policies it actually names; the two peers, which are concrete, live in the
 * module that uses them. That is what makes the split reduce anything at all rather than move it.
 *
 * NOTHING HERE POLLS BEFORE ASSERTING. requireRecorded( ) waits on the record the case is about
 * rather than on a COUNT of records, and RecordingSink carries the same shape for what the CLIENT
 * saw - a case which read a sink's records straight after the task finished would be the S3.5
 * flake again in a new place, because the task completing does not order the sink's last append
 * against this thread.
 */

namespace utest
{
    namespace h2driver
    {
        using bl::httpclient::ClientConnection;
        using bl::httpclient::ClientRequest;
        using bl::httpclient::ClientStreamEventSink;
        using bl::httpclient::ConnectionKey;
        using bl::httpclient::ConnectionState;
        using bl::httpclient::HttpProtocol;
        using bl::httpclient::NegotiatedProtocol;
        using bl::httpclient::stream_handle_t;

        typedef bl::tasks::Http2ConnectionConfig                                Http2ConnectionConfig;
        typedef bl::tasks::ClientConnectionConfig                               ClientConnectionConfig;

        enum : std::size_t
        {
            /*
             * Generous, because every wait here is satisfied in milliseconds when the code under
             * test is behaving and is only reached when something is already wrong
             */

            DEFAULT_WAIT_IN_MILLISECONDS        = 15U * 1000U,
        };

        /**
         * @brief class RecordingSinkT - what one request saw, and the rendezvous for asserting it
         *
         * The condition variable is notified under the same lock which appends, so a case which
         * waits on it has a happens-before with the I/O thread that made the record
         *
         * IT CREDITS FLOW CONTROL, which is not incidental: a response larger than the initial
         * window never finishes unless someone reports the bytes consumed, so a sink which only
         * recorded would make every large-body case hang rather than fail. The connection is held
         * as a RAW pointer on purpose - an ObjPtr here would be a reference cycle, since the
         * connection holds the sink for the life of the stream, and a cycle which survives is
         * reported as a leak. The case owns the task for longer than the sink lives
         */

        template
        <
            typename E = void
        >
        class RecordingSinkT : public ClientStreamEventSink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( RecordingSinkT, ClientStreamEventSink )

        protected:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cvClosed;

            std::vector< std::string >                                          m_records;
            std::string                                                         m_body;
            bl::http::HeaderList                                                m_headers;
            bl::eh::error_code                                                  m_errorCode;

            ClientConnection*                                                   m_connection;

            /*
             * What this sink answers an onBodyWanted( ) with - the S5.1 upload pull. Empty unless
             * a case installed an upload with setUpload( ), and a sink with none installed simply
             * records the pull and answers nothing, which is what a case not testing uploads wants
             */

            std::string                                                         m_upload;
            bool                                                                m_hasUpload;

            unsigned                                                            m_status;
            bool                                                                m_isClosed;
            bool                                                                m_isRetryable;

            /*
             * WHAT THE CONNECTION HAD PUBLISHED AT THE INSTANT THIS STREAM WAS ANSWERED, and it is
             * a CONTRACT rather than an observation: a request task derives its outcome by reading
             * state( ) when it applies onClosed( ), so a driver which answers a sink before it
             * publishes the state that close belongs to makes that outcome a coin toss ( L6 review,
             * finding 16 ). A probe sink cannot see the breach - it publishes before it answers by
             * construction - so only a case AT THE DRIVER pins it, which is why this is recorded
             *
             * A sink with no connection installed leaves this at Connecting, which fails such an
             * assertion rather than passing it - the safe direction
             */

            ConnectionState                                                     m_stateOnClosed;

            RecordingSinkT()
                :
                m_connection( nullptr ),
                m_hasUpload( false ),
                m_status( 0U ),
                m_isClosed( false ),
                m_isRetryable( false ),
                m_stateOnClosed( ConnectionState::Connecting )
            {
            }

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );
            }

        public:

            void setConnection( SAA_in_opt ClientConnection* const connection ) NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                m_connection = connection;
            }

            /**
             * @brief Installs the bytes this sink hands over when the driver pulls for body
             */

            void setUpload( SAA_in std::string upload )
            {
                BL_MUTEX_GUARD( m_lock );

                m_upload = BL_PARAM_FWD( upload );
                m_hasUpload = true;
            }

            /**
             * @brief Answers the driver's upload pull from the installed upload, at most what it
             * asked for
             *
             * The point of answering with AT MOST 'bytes' rather than with everything left is that
             * it is what a real request task does, and it is what makes the recorded sequence show
             * the pull working: one "wanted" record per chunk the windows allowed, rather than one
             * pull and one enormous hand-over which would prove nothing
             */

            virtual void onBodyWanted(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                ClientConnection* connection = nullptr;

                std::string chunk;
                bool isLast = false;

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_records.push_back(
                        "wanted " + bl::utils::lexical_cast< std::string >( bytes )
                        );

                    if( ! m_hasUpload )
                    {
                        return;
                    }

                    const auto take = std::min< std::size_t >( bytes, m_upload.size() );

                    chunk = m_upload.substr( 0U, take );
                    m_upload.erase( 0U, take );

                    isLast = m_upload.empty();
                    connection = m_connection;
                }

                if( nullptr == connection )
                {
                    return;
                }

                bl::om::ObjPtr< bl::data::DataBlock > block;

                if( ! chunk.empty() )
                {
                    block = bl::data::DataBlock::get( nullptr /* dataBlocksPool */, chunk.size() );

                    std::memcpy( block -> begin(), chunk.c_str(), chunk.size() );
                    block -> setSize( chunk.size() );
                }

                connection -> provideBody( handle, block, isLast );
            }

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                record(
                    ( isInterim ? "interim " : "headers " ) +
                    bl::utils::lexical_cast< std::string >( status )
                    );

                BL_MUTEX_GUARD( m_lock );

                if( ! isInterim )
                {
                    m_status = status;
                    m_headers = BL_PARAM_FWD( headers );
                }
            }

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                const auto size = data -> size() - data -> offset1();

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_body.append( data -> begin() + data -> offset1(), size );

                    m_records.push_back(
                        "data " + bl::utils::lexical_cast< std::string >( size )
                        );
                }

                /*
                 * The backpressure signal of design 5.3 - and the only thing which ever sends a
                 * WINDOW_UPDATE
                 */

                ClientConnection* connection = nullptr;

                {
                    BL_MUTEX_GUARD( m_lock );

                    connection = m_connection;
                }

                if( nullptr != connection )
                {
                    connection -> consumed( handle, size );
                }
            }

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          bl::http::HeaderList&&                          trailers
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( trailers );

                record( "trailers" );
            }

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( handle );

                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( m_lock );

                m_records.push_back(
                    std::string( "closed" ) + ( errorCode ? " with an error" : " cleanly" )
                    );

                m_errorCode = errorCode;
                m_isRetryable = isRetryable;
                m_isClosed = true;

                if( nullptr != m_connection )
                {
                    m_stateOnClosed = m_connection -> state();
                }

                m_cvClosed.notify_all();

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Blocks until the stream has been closed out, or the bound expires
             *
             * Bounded on purpose: a stream which is never answered has to fail the case with a
             * diagnosis rather than hang the suite
             */

            void waitForClosed(
                SAA_in_opt          const std::size_t                           timeoutInMilliseconds =
                                        DEFAULT_WAIT_IN_MILLISECONDS
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                const auto closed = m_cvClosed.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isClosed;
                    }
                    );

                if( ! closed )
                {
                    std::string what;

                    for( std::size_t i = 0U; i < m_records.size(); ++i )
                    {
                        what += "\n    ";
                        what += m_records[ i ];
                    }

                    UTF_FAIL(
                        BL_MSG()
                            << "The HTTP/2 stream was never closed out; the sink recorded:"
                            << what
                        );
                }
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            auto body() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_body;
            }

            auto headerValue( SAA_in const std::string& name ) const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                for( auto it = m_headers.begin(); it != m_headers.end(); ++it )
                {
                    if( it -> name() == name )
                    {
                        return it -> value();
                    }
                }

                return std::string();
            }

            unsigned status() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_status;
            }

            bool isRetryable() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_isRetryable;
            }

            auto errorCode() const -> bl::eh::error_code
            {
                BL_MUTEX_GUARD( m_lock );

                return m_errorCode;
            }

            auto stateOnClosed() const NOEXCEPT -> ConnectionState
            {
                BL_MUTEX_GUARD( m_lock );

                return m_stateOnClosed;
            }
        };

        typedef bl::om::ObjectImpl< RecordingSinkT<> > RecordingSink;

        /**
         * @brief The smallest thing which makes ClientRequest::hasBody( ) true without a buffered
         * body, so the HEADERS go out without END_STREAM and the upload arrives by provideBody( )
         *
         * IT IS STILL NEVER READ, although S5.1 gave the driver a pull. Reading a BodySource is
         * the request task's job (design 5.3) and the driver never touches one; what the driver
         * asks for, through onBodyWanted( ), is answered by the SINK here - RecordingSinkT::
         * setUpload( ) is where a case puts the bytes. This type exists only to make the request
         * look like a streaming one, which is what decides that the HEADERS go out without
         * END_STREAM and that the stream is pulled at all
         */

        template
        <
            typename E = void
        >
        class StubBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubBodySourceT, bl::httpclient::BodySource )

            BL_CTR_DEFAULT( StubBodySourceT, protected )

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                BL_UNUSED( target );

                return bl::httpclient::BodyReadResult();
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return true;
            }

            virtual void rewind() OVERRIDE
            {
            }
        };

        typedef bl::om::ObjectImpl< StubBodySourceT<> > StubBodySource;

        /**
         * @brief The fallback driver the factory builds when ALPN did not choose h2
         *
         * Deliberately self contained rather than reaching into utf_baselib_h2client's own stub: a
         * test header which includes a sibling module's header silently duplicates its cases into
         * two binaries (src/utests/AGENTS.md)
         */

        template
        <
            typename STREAM
        >
        class FallbackDriverT : public ClientConnection
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( FallbackDriverT, ClientConnection )

        protected:

            const NegotiatedProtocol                                            m_negotiated;
            const typename STREAM::stream_ref                                   m_connectedStream;

            FallbackDriverT(
                SAA_in          NegotiatedProtocol                              negotiated,
                SAA_inout       typename STREAM::stream_ref&&                   connectedStream
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) ),
                m_connectedStream( BL_PARAM_FWD( connectedStream ) )
            {
            }

        public:

            virtual auto submit(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const bl::om::ObjPtr< ClientStreamEventSink >&  eventSink
                )
                -> stream_handle_t OVERRIDE
            {
                BL_UNUSED( request );
                BL_UNUSED( eventSink );

                return ClientConnection::INVALID_STREAM_HANDLE;
            }

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( errorCode );
            }

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( bytes );
            }

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const bl::om::ObjPtr< bl::data::DataBlock >&    data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( data );
                BL_UNUSED( endStream );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                return 1U;
            }

            virtual ConnectionState state() const NOEXCEPT OVERRIDE
            {
                return ConnectionState::Ready;
            }

            virtual auto negotiated() const NOEXCEPT -> const NegotiatedProtocol& OVERRIDE
            {
                return m_negotiated;
            }
        };

        /**
         * @brief How many times the fallback factory was asked for a driver
         */

        struct FallbackRecord
        {
            std::size_t                                                         creations;
            std::string                                                         alpn;

            FallbackRecord()
                :
                creations( 0U )
            {
            }
        };

        template
        <
            typename STREAM
        >
        inline auto makeFallbackFactory( SAA_in const std::shared_ptr< FallbackRecord >& record )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< STREAM > >
        {
            typedef bl::httpclient::ClientDriverFactoryT< STREAM >              factory_t;
            typedef bl::om::ObjectImpl< FallbackDriverT< STREAM > >             driver_impl_t;

            auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                HttpProtocol::Http11,
                [ record ](
                    SAA_in      const NegotiatedProtocol&                       negotiated,
                    SAA_inout   typename STREAM::stream_ref&&                   connectedStream,
                    SAA_in      const ConnectionKey&                            key
                    )
                    -> bl::om::ObjPtr< ClientConnection >
                {
                    BL_UNUSED( key );

                    record -> creations += 1U;
                    record -> alpn = negotiated.alpn();

                    auto driver = driver_impl_t::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream )
                        );

                    return bl::om::qi< ClientConnection >( driver );
                }
                );

            return factory;
        }

        /**
         * @brief class DriverProbeT - the driver under test, with its writes recorded
         *
         * The only thing it adds is the record. "The preface, SETTINGS, the connection
         * WINDOW_UPDATE and the first HEADERS leave in ONE write" cannot be checked on the wire -
         * TCP does not preserve write boundaries - so the assertion has to be made where the
         * writes are, which is what onWriteScheduled( ) exists for
         */

        template
        <
            typename STREAM
        >
        class DriverProbeT : public bl::tasks::Http2ConnectionTaskT< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( DriverProbeT )

        public:

            typedef DriverProbeT< STREAM >                                      this_type;
            typedef bl::tasks::Http2ConnectionTaskT< STREAM >                   base_type;

        protected:

            mutable bl::os::mutex                                               m_writesLock;
            std::vector< std::string >                                          m_writes;

            DriverProbeT(
                SAA_in          ConnectionKey                                   key,
                SAA_in          typename base_type::factory_ptr_t               driverFactory,
                SAA_in          Http2ConnectionConfig                           h2config,
                SAA_in          ClientConnectionConfig                          config
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( h2config ),
                    bl::tasks::ProxyConfig::none(),
                    BL_PARAM_FWD( config ),
                    false /* logExceptions */
                    )
            {
            }

            virtual void onWriteScheduled(
                SAA_in          const bl::http2::Session::wire_buffer_t&        buffer
                ) OVERRIDE
            {
                BL_MUTEX_GUARD( m_writesLock );

                m_writes.push_back(
                    std::string(
                        reinterpret_cast< const char* >( buffer.empty() ? nullptr : &buffer[ 0 ] ),
                        buffer.size()
                        )
                    );
            }

        public:

            auto writes() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_writesLock );

                return m_writes;
            }
        };

        /**
         * @brief What a CLEARTEXT connection to the test peer speaks
         *
         * Nothing negotiates a cleartext connection, so the protocol is the configuration's and
         * the default is HTTP/1.1 - which would make this h2 task hand the stream straight to the
         * fallback driver. The peer speaks h2 by prior knowledge (RFC 9113 section 3.3), and
         * saying so is what makes these cases about the driver at all
         */

        inline auto cleartextHttp2Config() -> ClientConnectionConfig
        {
            ClientConnectionConfig config;

            config.cleartextProtocol = HttpProtocol::Http2;

            return config;
        }

        inline auto makeKey(
            SAA_in          std::string                                         scheme,
            SAA_in          std::string                                         host,
            SAA_in          const bl::os::port_t                                port
            )
            -> ConnectionKey
        {
            ConnectionKey key;

            key.scheme = BL_PARAM_FWD( scheme );
            key.host = BL_PARAM_FWD( host );
            key.port = port;

            return key;
        }

        inline auto makeRequest(
            SAA_in          const std::string&                                  url,
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> ClientRequest
        {
            ClientRequest request;

            request.method( bl::cpp::copy( method ) );
            request.url( bl::net::Uri::parse( url ) );

            return request;
        }

        inline auto blockOf( SAA_in const std::string& payload )
            -> bl::om::ObjPtr< bl::data::DataBlock >
        {
            return bl::data::DataBlock::copy( payload.c_str(), payload.size() );
        }

        /**
         * @brief Runs a peer acceptor, hands the case its port, and cancels it afterwards
         *
         * There is no probe connection and no sleep: Http2TestServerT publishes the bound port
         * from the continueAfterResolved( ) override the base has already listened in
         */

        template
        <
            typename PEER,
            typename CALLBACK
        >
        inline void withPeer(
            SAA_in          const bl::om::ObjPtr< PEER >&                       peer,
            SAA_in          const CALLBACK&                                     callback
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    const auto acceptor = om::qi< Task >( peer );

                    eq -> push_back( acceptor );

                    BL_SCOPE_EXIT_WARN_ON_FAILURE(
                        {
                            cancelAndWaitForSuccess( eq, acceptor );
                        },
                        "utest::h2driver::withPeer"
                        );

                    callback( peer -> waitForPort() );
                }
                );
        }

        /**
         * @brief Runs one connection task while the case drives it from this thread
         *
         * The queue keeps the task so the case can look at how it ended; the callback returns once
         * it has said everything it is going to say to the connection, and the wait is what orders
         * the assertions after it against the task's own last handler
         */

        template
        <
            typename DRIVER,
            typename CALLBACK
        >
        inline void runDriver(
            SAA_in          const bl::om::ObjPtr< DRIVER >&                     driver,
            SAA_in          const CALLBACK&                                     callback
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto task = om::qi< Task >( driver );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    callback();

                    eq -> wait( task );

                    UTF_REQUIRE( eq -> isEmpty() );
                }
                );
        }

        /**
         * @brief What a task failed with, or a placeholder - and NOT an assertion
         *
         * IT MUST NOT ASSERT, and that is not a style preference. UTF_FAIL( msg ) takes
         * UtfGlobals::g_lock and THEN evaluates msg, and bl::os::mutex is not recursive - so a UTF
         * macro inside the argument of another UTF macro deadlocks the binary on its own thread,
         * silently, looking exactly like a hung test. Utf.h says so where the exception macros are
         * defined; this helper is the same hazard reached from a different direction
         */

        inline auto exceptionMessageOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
            -> std::string
        {
            using namespace bl;

            if( ! task -> exception() )
            {
                return std::string( "<no exception>" );
            }

            try
            {
                cpp::safeRethrowException( task -> exception() );
            }
            catch( std::exception& e )
            {
                return std::string( e.what() );
            }

            return std::string( "<unknown exception>" );
        }

        /**
         * @brief Fails with the message the task failed with, rather than with a bare
         * "isFailed() has failed" which says nothing about why
         */

        inline void chkTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            if( ! task -> isFailed() )
            {
                UTF_REQUIRE( ! task -> exception() );

                return;
            }

            const auto message = exceptionMessageOf( task );

            UTF_FAIL( "the HTTP/2 connection task failed: " + message );
        }

        /**
         * @brief The message of a task which was SUPPOSED to fail
         */

        inline auto chkTaskFailed( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
            -> std::string
        {
            const auto message = exceptionMessageOf( task );

            if( ! task -> isFailed() )
            {
                UTF_FAIL( "the HTTP/2 connection task was expected to fail and did not" );
            }

            return message;
        }

        inline bool hasRecord(
            SAA_in          const std::vector< std::string >&                   records,
            SAA_in          const std::string&                                  expected
            )
        {
            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                if( records[ i ] == expected )
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief WAITS for the peer to record a line, and prints every line when it never does
         *
         * BY CONTENT AND NOT BY COUNT, and that is the whole point. waitForRecordsOf( recorder, N )
         * is a rendezvous on a NUMBER, so a case which wants a particular record has to guess how
         * many come before it - and this suite's first version guessed one, which the peer's own
         * "connected" already satisfies. The wait then returned before the record the case was
         * about had been made, and the assertion read whatever happened to be there. That is the
         * S3.5 flake exactly, and the fix is the same one: wait on the thing being asserted
         *
         * There is nothing to guess and nothing to poll here. Each iteration blocks on the
         * recorder's condition variable for one MORE record than it has seen, so the loop advances
         * only when the peer really appends, and at most one wait can expire - which bounds the
         * whole call by one timeout whatever the peer does
         */

        inline void requireRecorded(
            SAA_in          const h2peer::Http2TestRecorder&                    recorder,
            SAA_in          const std::string&                                  expected
            )
        {
            for( std::size_t count = 1U; ; ++count )
            {
                if( hasRecord( recorder.records(), expected ) )
                {
                    return;
                }

                if( ! recorder.waitForRecords( count, DEFAULT_WAIT_IN_MILLISECONDS ) )
                {
                    break;
                }
            }

            const auto records = recorder.records();
            const auto failure = recorder.failure();

            std::string what;

            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                what += "\n    ";
                what += records[ i ];
            }

            UTF_FAIL(
                "the HTTP/2 test peer never recorded '" + expected + "'" +
                ( failure.empty() ? std::string() : " - a responder failed with: " + failure ) +
                "; it recorded:" + what
                );
        }

        /**
         * @brief The peer has seen a stream all the way out - the rendezvous for anything the
         * peer RECEIVED, such as a request body
         */

        inline void requireStreamClosedAtPeer(
            SAA_in          const h2peer::Http2TestRecorder&                    recorder,
            SAA_in          const std::uint32_t                                 streamId
            )
        {
            requireRecorded(
                recorder,
                "stream " + bl::utils::lexical_cast< std::string >( streamId ) +
                    " closed with error 0"
                );
        }

        /**
         * @brief The RFC 9113 frame type of the frame which starts at 'offset'
         *
         * NOT AN ASSERTION, and neither is its sibling below: how many frames a write carries
         * depends on when the peer answers, so a passing UTF_REQUIRE in here would make a case's
         * assertion COUNT vary from run to run - and the per-case counts are exactly what
         * verification tier 3 compares (src/utests/AGENTS.md). The bound is the caller's, and
         * frameTypesOf( ) below is the only caller there is
         */

        inline std::uint8_t frameTypeAt(
            SAA_in          const std::string&                                  wire,
            SAA_in          const std::size_t                                   offset
            )
        {
            return static_cast< std::uint8_t >( wire[ offset + 3U ] );
        }

        inline std::size_t frameLengthAt(
            SAA_in          const std::string&                                  wire,
            SAA_in          const std::size_t                                   offset
            )
        {
            return
                ( static_cast< std::size_t >( static_cast< unsigned char >( wire[ offset ] ) ) << 16 ) |
                ( static_cast< std::size_t >( static_cast< unsigned char >( wire[ offset + 1U ] ) ) << 8 ) |
                static_cast< std::size_t >( static_cast< unsigned char >( wire[ offset + 2U ] ) );
        }

        /**
         * @brief The frame types one write carried, in order, skipping the connection preface
         */

        inline auto frameTypesOf( SAA_in const std::string& wire ) -> std::vector< std::uint8_t >
        {
            std::vector< std::uint8_t > types;

            std::size_t offset = 0U;

            if(
                wire.size() >= bl::http2::Globals::g_connectionPreface.size() &&
                0 == wire.compare(
                    0U,
                    bl::http2::Globals::g_connectionPreface.size(),
                    bl::http2::Globals::g_connectionPreface
                    )
                )
            {
                offset = bl::http2::Globals::g_connectionPreface.size();
            }

            while( offset + 9U <= wire.size() )
            {
                types.push_back( frameTypeAt( wire, offset ) );

                offset += 9U + frameLengthAt( wire, offset );
            }

            return types;
        }

        inline bool containsFrameType(
            SAA_in          const std::vector< std::uint8_t >&                  types,
            SAA_in          const std::uint8_t                                  type
            )
        {
            for( std::size_t i = 0U; i < types.size(); ++i )
            {
                if( types[ i ] == type )
                {
                    return true;
                }
            }

            return false;
        }

    } // h2driver

} // utest

#endif /* __UTEST_HTTP2DRIVERTESTUTILS_H_ */
