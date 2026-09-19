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

#ifndef __UTEST_TESTHTTP2CONNECTIONTASK_H_
#define __UTEST_TESTHTTP2CONNECTIONTASK_H_

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

#include <cstdint>
#include <string>
#include <vector>

#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/RawFrameScriptPeer.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * S4.2 - the HTTP/2 driver (design 5.1, 5.2 and 5.7)
 *
 * The task under test is tasks::Http2ConnectionTaskT< STREAM >, driven over both stranded stream
 * policies of design 3.1 against the in-process peer of design 8.2.
 *
 * WHAT THESE CASES ARE ABOUT. Not the protocol - the engine is S3.1's and is tested exhaustively
 * in utf_baselib_h2core against byte vectors. What this slice owns is the SHELL: that the opening
 * frames leave in one write, that a payload reaches a request as a pooled block, that the write
 * pump keeps exactly one write in flight while a read is outstanding, that the three connection
 * timers of design 5.7 do what the table says, and that the two ways out of the task stay
 * distinguishable - a deliberate close completes it SUCCESSFULLY and a cancel does not.
 *
 * NOTHING HERE POLLS BEFORE ASSERTING. requireRecorded( ) below waits on the record the case is
 * about rather than on a COUNT of records, and RecordingSink carries the same shape for what the
 * CLIENT saw - a case which read a sink's records straight after the task finished would be the
 * S3.5 flake again in a new place, because the task completing does not order the sink's last
 * append against this thread.
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

            unsigned                                                            m_status;
            bool                                                                m_isClosed;
            bool                                                                m_isRetryable;

            RecordingSinkT()
                :
                m_connection( nullptr ),
                m_status( 0U ),
                m_isClosed( false ),
                m_isRetryable( false )
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
        };

        typedef bl::om::ObjectImpl< RecordingSinkT<> > RecordingSink;

        /**
         * @brief The smallest thing which makes ClientRequest::hasBody( ) true without a buffered
         * body, so the HEADERS go out without END_STREAM and the upload arrives by provideBody( )
         *
         * It is never pulled: pulling a BodySource is the request task's job (design 5.3, S5.1),
         * and this driver is handed bytes rather than asking for them
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

        typedef bl::om::ObjectImpl
            <
                DriverProbeT< bl::tasks::TcpSocketAsyncStrandedBase >
            >
            PlainDriverImpl;

        typedef bl::om::ObjectImpl
            <
                DriverProbeT< bl::tasks::TcpSslSocketAsyncStrandedBase >
            >
            TlsDriverImpl;

        /**
         * @brief class TlsHttp2TestServerT - the peer of design 8.2 over TLS
         *
         * S4.4 could not build this: choosing "h2" is the SERVER half of ALPN and no entry point
         * for it existed in src/include until S4.2 added
         * crypto::CryptoBase::setAlpnServerPreference. Everything else is the cleartext peer -
         * which is the point, since TcpServerBase is what performs the server side handshake and
         * Http2TestConnectionT is already written against a stream policy
         *
         * It is also the first instantiation of Http2TestConnectionT over a TLS policy. A template
         * nothing instantiates is not compiled, and this project has now been bitten by that twice
         */

        template
        <
            typename E = void
        >
        class TlsHttp2TestServerT :
            public h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( TlsHttp2TestServerT )

        public:

            typedef h2peer::Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase > base_type;

        protected:

            TlsHttp2TestServerT(
                SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >& controlToken,
                SAA_in          const std::vector< std::string >&                preference
                )
                :
                base_type(
                    controlToken,
                    "localhost",
                    0U /* ephemeral */,
                    test::UtfCrypto::getDefaultServerKey(),
                    test::UtfCrypto::getDefaultServerCertificate()
                    )
            {
                /*
                 * The base constructor has built the server context by now - initServerContext( )
                 * runs from TcpServerBase's own constructor when the policy needs a handshake
                 */

                UTF_REQUIRE( nullptr != base_type::m_serverContext.get() );

                bl::crypto::CryptoBase::setAlpnServerPreference(
                    *base_type::m_serverContext,
                    preference
                    );
            }
        };

        typedef bl::om::ObjectImpl< TlsHttp2TestServerT<> > TlsHttp2TestServer;

        inline auto makePeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

        inline auto makeTlsPeer( SAA_in const std::vector< std::string >& preference )
            -> bl::om::ObjPtr< TlsHttp2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return TlsHttp2TestServer::createInstance<>( controlToken, preference );
        }

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

/**
 * @brief The ordinary path - one request, one response, over cleartext h2
 *
 * It also pins the two things the S2.6 contract publishes and which only a driver can fill: the
 * status arrives BESIDE the header list, because http::HeaderList cannot hold ":status", and the
 * pseudo-headers are not in the list at all
 */

UTF_AUTO_TEST_CASE( H2Driver_RequestAndResponseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            /*
             * A responder runs on an I/O thread, where the Boost.Test macros are not safe, so it
             * states what it expects by throwing - which the peer records
             */

            BL_CHK(
                false,
                "GET" == request.method && "/hello" == request.path,
                BL_MSG()
                    << "The peer was asked for an unexpected request: "
                    << request.method
                    << " "
                    << request.path
                );

            http2::HpackFieldList fields;

            fields.push_back(
                http2::HpackField( std::string( "content-type" ), std::string( "text/plain" ) )
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U, fields )
                .data( "hello world" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    const auto handle = connection -> submit(
                        makeRequest( "http://127.0.0.1/hello" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            /*
             * The driver never went through the factory for itself - it IS the connection
             */

            UTF_REQUIRE_EQUAL( record -> creations, 0U );

            UTF_REQUIRE( HttpProtocol::Http2 == connection -> negotiated().protocol() );
            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "hello world" ) );
            UTF_REQUIRE_EQUAL( sink -> headerValue( "content-type" ), std::string( "text/plain" ) );

            /*
             * ":status" is NOT a header - the list refuses a colon by design, which is why the
             * status travels as its own parameter
             */

            UTF_REQUIRE( sink -> headerValue( ":status" ).empty() );

            UTF_REQUIRE( ! sink -> errorCode() );

            const auto records = sink -> records();

            UTF_REQUIRE_EQUAL( records.size(), 3U );
            UTF_REQUIRE_EQUAL( records[ 0 ], std::string( "headers 200" ) );
            UTF_REQUIRE_EQUAL( records[ 1 ], std::string( "data 11" ) );
            UTF_REQUIRE_EQUAL( records[ 2 ], std::string( "closed cleanly" ) );

            requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

/**
 * @brief The opening write is ONE write, and it carries the first request with it
 *
 * This is a fingerprint statement and not an efficiency one: a browser coalesces the preface, its
 * SETTINGS, its connection WINDOW_UPDATE and its first HEADERS into one segment, and a client which
 * sends them in four writes is distinguishable from one which does not however identical the bytes
 * are. Nothing on the wire can tell the two apart, so the assertion is made where the writes are
 */

UTF_AUTO_TEST_CASE( H2Driver_OpeningWriteIsOneWriteTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            return h2peer::Http2ResponseScript()
                .headers( 204U, http2::HpackFieldList(), true /* endStream */ )
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            /*
             * The profile's connection WINDOW_UPDATE has to be non-zero for the frame to be in the
             * opening write at all - a zero increment would be an invalid frame, so the engine
             * emits none
             */

            Http2ConnectionConfig h2config;

            h2config.profile.connectionWindowUpdateIncrement = 15663105U;

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                h2config,
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            /*
             * SUBMITTED BEFORE THE TASK IS EVEN SCHEDULED, and that is what makes this case a
             * statement rather than a race. The mailbox takes a command with no strand to post it
             * to and onProtocolNegotiated( ) drains it as part of the opening write; submitting
             * from inside the run instead leaves it a race with the loopback connect, which loses
             * often enough to be seen - it lost under ThreadSanitizer the first time it was run
             * there. It is also what the pool does: the request which caused the connection to
             * exist is already waiting when the handshake completes
             */

            UTF_REQUIRE(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE !=
                    connection -> submit(
                        makeRequest( "http://127.0.0.1/first" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        )
                );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 204U );

            const auto writes = driver -> writes();

            UTF_REQUIRE( ! writes.empty() );

            const auto& opening = writes[ 0 ];

            /*
             * The preface first, verbatim
             */

            UTF_REQUIRE( opening.size() > http2::Globals::g_connectionPreface.size() );

            UTF_REQUIRE_EQUAL(
                opening.substr( 0U, http2::Globals::g_connectionPreface.size() ),
                http2::Globals::g_connectionPreface
                );

            const auto types = frameTypesOf( opening );

            UTF_REQUIRE( types.size() >= 3U );

            UTF_REQUIRE( http2::Globals::FRAME_TYPE_SETTINGS == types[ 0 ] );

            UTF_REQUIRE(
                containsFrameType( types, http2::Globals::FRAME_TYPE_WINDOW_UPDATE )
                );

            /*
             * And the first request's HEADERS, in the SAME write. This is the assertion the case
             * exists for - everything above would also hold if the HEADERS had gone out second
             */

            UTF_REQUIRE( containsFrameType( types, http2::Globals::FRAME_TYPE_HEADERS ) );
        }
        );
}

/**
 * @brief Full duplex - a body going out while a body is coming in, both larger than one window
 *
 * The upload is handed over by provideBody( ) in chunks, as a streaming request task would do it,
 * and the peer withholds its WINDOW_UPDATEs until the client has actually run out of window. The
 * stall is an EVENT and not a duration: awaitWindowStall( ) fires at the exact octet the client
 * cannot go past, so the case is deterministic rather than a race with a timer
 */

UTF_AUTO_TEST_CASE( H2Driver_FullDuplexUploadAndDownloadTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const std::string chunk( 16U * 1024U, 'u' );
    const std::size_t chunks = 6U;

    const std::string download( 200U * 1024U, 'd' );

    const auto peer = makePeer();

    peer -> setWithholdWindowUpdates( true );

    peer -> setResponder(
        [ &download ]( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                request.hasBody,
                BL_MSG()
                    << "The peer expected a request with a body"
                );

            return h2peer::Http2ResponseScript()
                .awaitWindowStall()
                .creditWindow()
                .headers( 200U )
                .data( download )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    auto request = makeRequest( "http://127.0.0.1/upload", "POST" );

                    request.bodySource(
                        om::ObjPtrCopyable< httpclient::BodySource >(
                            om::qi< httpclient::BodySource >( StubBodySource::createInstance() )
                            )
                        );

                    UTF_REQUIRE( request.hasBody() );

                    const auto handle = connection -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    for( std::size_t i = 0U; i < chunks; ++i )
                    {
                        connection -> provideBody(
                            handle,
                            blockOf( chunk ),
                            i + 1U == chunks /* endStream */
                            );
                    }

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body().size(), download.size() );
            UTF_REQUIRE_EQUAL( sink -> body(), download );

            /*
             * The whole upload arrived, which it could not have without the WINDOW_UPDATE the
             * stall released and without the driver holding back what the windows would not take
             */

            requireStreamClosedAtPeer( peer -> recorder(), 1U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );

            UTF_REQUIRE_EQUAL(
                peer -> recorder().bodyOf( 1U ).size(),
                chunk.size() * chunks
                );

            /*
             * More than one write was needed for a body this size, and exactly one was ever in
             * flight - which is what the pump is
             */

            UTF_REQUIRE( driver -> writes().size() > 1U );
        }
        );
}

/**
 * @brief The keepalive PING of design 5.7, and the connection idle close
 *
 * Both are observed rather than inferred: the PING is counted in the writes the driver made, and
 * the idle close is the GOAWAY the peer records plus a task which completed SUCCESSFULLY - the
 * deliberate door of design 3.2, which is what stops the pool counting a clean shutdown as a
 * failed connection
 */

UTF_AUTO_TEST_CASE( H2Driver_KeepAliveAndIdleCloseTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            Http2ConnectionConfig h2config;

            h2config.keepAliveInterval = time::milliseconds( 50 );
            h2config.keepAlivePingReplyTimeout = time::seconds( 10 );
            h2config.idleTimeout = time::milliseconds( 400 );

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                h2config,
                cleartextHttp2Config()
                );

            runDriver( driver, []() -> void {} );

            /*
             * Nothing was ever submitted, so the idle timer is what ended this connection - and it
             * ended it the deliberate way
             */

            chkTaskSucceeded( om::qi< Task >( driver ) );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
            UTF_REQUIRE_EQUAL( connection -> freeStreamSlots(), 0U );

            std::size_t pings = 0U;
            bool sawGoAway = false;

            const auto writes = driver -> writes();

            for( std::size_t i = 0U; i < writes.size(); ++i )
            {
                const auto types = frameTypesOf( writes[ i ] );

                for( std::size_t j = 0U; j < types.size(); ++j )
                {
                    if( http2::Globals::FRAME_TYPE_PING == types[ j ] )
                    {
                        ++pings;
                    }

                    if( http2::Globals::FRAME_TYPE_GOAWAY == types[ j ] )
                    {
                        sawGoAway = true;
                    }
                }
            }

            /*
             * 400ms of idling at a 50ms interval - several PINGs, and every one of them was
             * answered or the reply deadline would have cancelled the task instead
             */

            UTF_REQUIRE( pings >= 2U );

            UTF_REQUIRE( sawGoAway );

            requireRecorded( peer -> recorder(), "the client sent GOAWAY with error 0" );
        }
        );
}

/**
 * @brief A peer which stops answering PINGs loses the connection, and every stream on it
 *
 * The raw peer reads the opening write, answers the SETTINGS so the connection is established, and
 * then says nothing at all. The reply deadline is the only thing which ends this - and it ends it
 * the OTHER way, failed, because a connection which stopped answering is not one that was done
 */

UTF_AUTO_TEST_CASE( H2Driver_KeepAlivePingDeadlineTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto settingsAck = h2peer::frameOctets(
        static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
        static_cast< std::uint8_t >( http2::Globals::FRAME_FLAG_ACK ),
        0U
        );

    h2peer::RawFrameScriptPeer rawPeer(
        h2peer::RawFrameScript()
            .expectPreface()
            .send(
                h2peer::frameOctets(
                    static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
                    0U,
                    0U
                    )
                )
            .send( settingsAck )
            .delay( 3000 )
        );

    const auto record = std::make_shared< FallbackRecord >();

    Http2ConnectionConfig h2config;

    h2config.keepAliveInterval = time::milliseconds( 50 );
    h2config.keepAlivePingReplyTimeout = time::milliseconds( 300 );

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", rawPeer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        h2config,
        cleartextHttp2Config()
        );

    runDriver( driver, []() -> void {} );

    const auto task = om::qi< Task >( driver );

    ( void ) chkTaskFailed( task );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
}

/**
 * @brief A peer which never acknowledges our SETTINGS is dropped on the SETTINGS timeout
 *
 * THE VALUE IS SessionLimits::settingsTimeoutInSeconds AND THERE IS ONLY ONE OF IT. Design 5.7 used
 * to quote 30 seconds beside the engine's 10; S4.2 reconciled that in favour of 10 and amended the
 * design, and this case is what pins the driver to the engine's number rather than to a second one
 * of its own - it sets the limit and the connection dies on it
 */

UTF_AUTO_TEST_CASE( H2Driver_SettingsAcknowledgementTimeoutTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    /*
     * The peer sends its own SETTINGS - so the connection is up and the client acknowledges it -
     * and then withholds the acknowledgement of ours forever
     */

    h2peer::RawFrameScriptPeer rawPeer(
        h2peer::RawFrameScript()
            .expectPreface()
            .send(
                h2peer::frameOctets(
                    static_cast< std::uint8_t >( http2::Globals::FRAME_TYPE_SETTINGS ),
                    0U,
                    0U
                    )
                )
            .delay( 5000 )
        );

    const auto record = std::make_shared< FallbackRecord >();

    Http2ConnectionConfig h2config;

    h2config.limits.settingsTimeoutInSeconds = 1U;

    const auto driver = PlainDriverImpl::createInstance(
        makeKey( "http", "127.0.0.1", rawPeer.port() ),
        makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
        h2config,
        cleartextHttp2Config()
        );

    runDriver( driver, []() -> void {} );

    const auto task = om::qi< Task >( driver );

    /*
     * A connection error is neither a clean end nor an external cancel, and the pool must not be
     * told it was either - so the task fails, with the reason
     */

    const auto message = chkTaskFailed( task );

    UTF_REQUIRE( message.find( "SETTINGS" ) != std::string::npos );

    const auto connection = om::qi< httpclient::ClientConnection >( driver );

    UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

    /*
     * The GOAWAY went out BEFORE the task failed - RFC 9113 5.4.1 asks for it, and throwing at the
     * point the error was detected would have taken the task down with it still queued
     */

    bool sawGoAway = false;

    const auto writes = driver -> writes();

    for( std::size_t i = 0U; i < writes.size(); ++i )
    {
        const auto types = frameTypesOf( writes[ i ] );

        for( std::size_t j = 0U; j < types.size(); ++j )
        {
            if( http2::Globals::FRAME_TYPE_GOAWAY == types[ j ] )
            {
                sawGoAway = true;
            }
        }
    }

    UTF_REQUIRE( sawGoAway );
}

/**
 * @brief A refused stream is answered, and is reported RETRYABLE
 *
 * REFUSED_STREAM is the peer saying "I did not process this", which is the first limb of the
 * retry rule of design 5.4. The connection stays up and takes the next request, which is the other
 * half of what makes the answer useful
 */

UTF_AUTO_TEST_CASE( H2Driver_RefusedStreamIsRetryableTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( 1U == request.streamIndex )
            {
                return h2peer::Http2ResponseScript().refuse();
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "second" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto refused = RecordingSink::createInstance();
            const auto served = RecordingSink::createInstance();

            served -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/first" ),
                        om::qi< httpclient::ClientStreamEventSink >( refused )
                        );

                    refused -> waitForClosed();

                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/second" ),
                        om::qi< httpclient::ClientStreamEventSink >( served )
                        );

                    served -> waitForClosed();
                }
                );

            served -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( refused -> isRetryable() );
            UTF_REQUIRE( refused -> errorCode() );
            UTF_REQUIRE_EQUAL( refused -> status(), 0U );

            UTF_REQUIRE( ! served -> isRetryable() );
            UTF_REQUIRE_EQUAL( served -> status(), 200U );
            UTF_REQUIRE_EQUAL( served -> body(), std::string( "second" ) );
        }
        );
}

/**
 * @brief Cancelling a request resets ONE stream and leaves the connection up (design 5.7)
 *
 * The cancelled stream is answered with operation_aborted and the other one completes on the same
 * connection - which is the property the sentence in 5.7 is actually about
 */

UTF_AUTO_TEST_CASE( H2Driver_CancelResetsOneStreamOnlyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            if( "/slow" == request.path )
            {
                /*
                 * Long enough that the cancel is certain to reach the peer first, and bounded so a
                 * peer left behind by a failing case still finishes
                 */

                return h2peer::Http2ResponseScript()
                    .delay( 5000 )
                    .headers( 200U, http2::HpackFieldList(), true /* endStream */ );
            }

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "fast" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            const auto slow = RecordingSink::createInstance();
            const auto fast = RecordingSink::createInstance();

            fast -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    const auto slowHandle = connection -> submit(
                        makeRequest( "http://127.0.0.1/slow" ),
                        om::qi< httpclient::ClientStreamEventSink >( slow )
                        );

                    connection -> cancel( slowHandle, asio::error::operation_aborted );

                    slow -> waitForClosed();

                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/fast" ),
                        om::qi< httpclient::ClientStreamEventSink >( fast )
                        );

                    fast -> waitForClosed();
                }
                );

            fast -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE( slow -> errorCode() );
            UTF_REQUIRE_EQUAL( slow -> status(), 0U );

            /*
             * The connection was never closed by the cancel - the second request went out on it
             */

            UTF_REQUIRE_EQUAL( fast -> status(), 200U );
            UTF_REQUIRE_EQUAL( fast -> body(), std::string( "fast" ) );
        }
        );
}

/**
 * @brief A GOAWAY drains what is in flight and then closes, cleanly
 *
 * The peer answers the request and then says GOAWAY, which is the ordinary graceful shutdown of
 * RFC 9113 6.8. The connection reports Draining from the moment it arrives and finishes as a
 * DELIBERATE close - the task succeeds and the pool is not told a connection failed
 */

UTF_AUTO_TEST_CASE( H2Driver_GoAwayDrainsAndClosesCleanlyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    const auto peer = makePeer();

    peer -> setGoAwayAfterStreams( 1U );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_UNUSED( request );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "bye" )
                .endStream();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = PlainDriverImpl::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< TcpSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest( "http://127.0.0.1/last" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "bye" ) );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );
            UTF_REQUIRE_EQUAL( connection -> freeStreamSlots(), 0U );

            /*
             * Nothing further may be submitted, and a request which arrives too late is answered
             * rather than dropped - and answered as RETRYABLE, since not a byte of it was written
             */

            const auto late = RecordingSink::createInstance();

            UTF_REQUIRE(
                httpclient::ClientConnection::INVALID_STREAM_HANDLE ==
                    connection -> submit(
                        makeRequest( "http://127.0.0.1/toolate" ),
                        om::qi< httpclient::ClientStreamEventSink >( late )
                        )
                );
        }
        );
}

/**
 * @brief h2 over TLS, on the library's own client path, against a TLS peer which selects "h2"
 *
 * THE SERVER HALF OF ALPN IS WHAT MADE THIS POSSIBLE. Until S4.2 added
 * crypto::CryptoBase::setAlpnServerPreference the tree had the client half only, so no TLS peer
 * could choose h2 and S4.4's peer was cleartext by necessity. This is also the first instantiation
 * of the peer's connection task over a TLS stream policy
 *
 * The host is "localhost" because the client verifies the peer name - UtfMain registers the dev
 * root CA for every test binary and the test server certificate is issued for that name
 *
 * WHAT IS OWED: the same case on OpenSSL 1.1.1w. No dist on this machine carries that flavor and
 * BL_USE_OPENSSL_1X=1 does not build for pre-existing reasons - see
 * notes/plans/issues/openssl-1x-flavor-deferral.md
 */

UTF_AUTO_TEST_CASE( H2Driver_TlsAlpnSelectsHttp2Tests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    std::vector< std::string > preference;

    preference.push_back( "h2" );
    preference.push_back( "http/1.1" );

    const auto peer = makeTlsPeer( preference );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                "https" == request.scheme,
                BL_MSG()
                    << "The peer expected an https scheme, got: "
                    << request.scheme
                );

            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( "secure" )
                .endStream()
                .closeConnection();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = TlsDriverImpl::createInstance(
                makeKey( "https", "localhost", port ),
                makeFallbackFactory< TcpSslSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                ClientConnectionConfig()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto sink = RecordingSink::createInstance();

            sink -> setConnection( connection.get() );

            runDriver(
                driver,
                [ & ]() -> void
                {
                    ( void ) connection -> submit(
                        makeRequest(
                            "https://localhost:" +
                            utils::lexical_cast< std::string >( port ) +
                            "/secure"
                            ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    sink -> waitForClosed();
                }
                );

            sink -> setConnection( nullptr );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            /*
             * The identifier the peer selected, verbatim - not one derived from the protocol
             */

            UTF_REQUIRE( HttpProtocol::Http2 == connection -> negotiated().protocol() );
            UTF_REQUIRE_EQUAL( connection -> negotiated().alpn(), std::string( "h2" ) );

            UTF_REQUIRE_EQUAL( record -> creations, 0U );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "secure" ) );
        }
        );
}

/**
 * @brief The ALPN selection order is the SERVER's, not the client's
 *
 * The client offers "h2, http/1.1" and the server prefers "http/1.1, h2"; the server's order wins,
 * so the connection is HTTP/1.1 and this h2 task hands the connected stream to the factory - which
 * is design 5.5's fallback and the only thing the factory is for on this path
 */

UTF_AUTO_TEST_CASE( H2Driver_TlsAlpnServerPreferenceWinsTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;

    std::vector< std::string > preference;

    preference.push_back( "http/1.1" );
    preference.push_back( "h2" );

    const auto peer = makeTlsPeer( preference );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = TlsDriverImpl::createInstance(
                makeKey( "https", "localhost", port ),
                makeFallbackFactory< TcpSslSocketAsyncStrandedBase >( record ),
                Http2ConnectionConfig(),
                ClientConnectionConfig()
                );

            runDriver( driver, []() -> void {} );

            chkTaskSucceeded( om::qi< Task >( driver ) );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );

            UTF_REQUIRE( HttpProtocol::Http11 == connection -> negotiated().protocol() );
            UTF_REQUIRE_EQUAL( connection -> negotiated().alpn(), std::string( "http/1.1" ) );

            /*
             * The stream went to the HTTP/1.1 driver, once, carrying the identifier the peer chose
             */

            UTF_REQUIRE_EQUAL( record -> creations, 1U );
            UTF_REQUIRE_EQUAL( record -> alpn, std::string( "http/1.1" ) );
        }
        );
}

#endif /* __UTEST_TESTHTTP2CONNECTIONTASK_H_ */
