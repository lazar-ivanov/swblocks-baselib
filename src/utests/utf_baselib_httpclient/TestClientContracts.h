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

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * Slice S2.6 - the client contracts, and the stub implementations S5.1 develops against
 *
 * The acceptance of this slice is "interfaces compile against a stub implementation; value objects
 * round-trip; a stub ClientConnection + sink pair exists in httpclient tests for S5.1 to develop
 * against". The stubs below ARE that deliverable: they are not scaffolding for the cases, they are
 * the thing the slice ships, and the cases are what proves they satisfy the interfaces
 *
 * The stubs live in a NAMED namespace rather than an anonymous one because a later slice in this
 * module - S5.1's request task cases - has to reach them from its own header, and an anonymous
 * namespace has internal linkage per translation unit but is still one namespace per header in a
 * single-TU test module; naming it is what makes the sharing explicit and greppable
 * ( src/utests/AGENTS.md )
 */

namespace utest
{
    namespace clientcontracts
    {
        /**
         * @brief A body source over a std::string, optionally refusing to rewind
         *
         * The refusing form is what makes the replayability rule observable: a request carrying it
         * must report isReplayable() false, and design 5.4 then forbids replaying it however
         * provably unprocessed the failure was
         */

        template
        <
            typename E = void
        >
        class StubBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubBodySourceT, bl::httpclient::BodySource )

        protected:

            std::string                                                         m_content;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_pos;
            bl::cpp::ScalarTypeIniter< bool >                                   m_canRewind;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_chunkSize;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_rewindCount;

            StubBodySourceT(
                std::string                                                     content,
                const bool                                                      canRewind,
                const std::size_t                                               chunkSize
                )
                :
                m_content( BL_PARAM_FWD( content ) )
            {
                m_canRewind = canRewind;
                m_chunkSize = chunkSize;
            }

        public:

            std::size_t rewindCount() const NOEXCEPT
            {
                return m_rewindCount;
            }

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                bl::httpclient::BodyReadResult result;

                const auto remaining = m_content.size() - m_pos;

                auto size = std::min< std::size_t >( m_chunkSize, remaining );

                size = std::min< std::size_t >( size, target.capacity() - target.size() );

                if( size )
                {
                    std::memcpy( target.begin() + target.size(), m_content.c_str() + m_pos, size );

                    target.setSize( target.size() + size );

                    m_pos = m_pos + size;
                }

                result.size = size;
                result.isEndOfStream = ( m_pos == m_content.size() );

                return result;
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return m_canRewind;
            }

            virtual void rewind() OVERRIDE
            {
                BL_CHK_T(
                    false,
                    m_canRewind.value(),
                    bl::NotSupportedException(),
                    BL_MSG()
                        << "This body source cannot be rewound"
                    );

                m_pos = 0U;
                m_rewindCount = m_rewindCount + 1U;
            }
        };

        typedef bl::om::ObjectImpl< StubBodySourceT<> > StubBodySource;

        /**
         * @brief A body sink which accumulates, and which can be told to consume only part of what
         * it is offered - which is how backpressure is expressed (design 5.3)
         */

        template
        <
            typename E = void
        >
        class StubBodySinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubBodySinkT, bl::httpclient::BodySink )

        protected:

            std::string                                                         m_received;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_maxPerCall;
            bl::cpp::ScalarTypeIniter< bool >                                   m_isComplete;

            StubBodySinkT( const std::size_t maxPerCall )
            {
                m_maxPerCall = maxPerCall;
            }

        public:

            const std::string& received() const NOEXCEPT
            {
                return m_received;
            }

            bool isComplete() const NOEXCEPT
            {
                return m_isComplete;
            }

            virtual auto onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data )
                -> std::size_t OVERRIDE
            {
                const auto offered = data -> size() - data -> offset1();

                const auto consumed = std::min< std::size_t >( m_maxPerCall, offered );

                m_received.append( data -> begin() + data -> offset1(), consumed );

                return consumed;
            }

            virtual void onComplete() OVERRIDE
            {
                m_isComplete = true;
            }
        };

        typedef bl::om::ObjectImpl< StubBodySinkT<> > StubBodySink;

        /**
         * @brief The stub stream event sink S5.1 develops against
         *
         * It records the events as a flat, readable trace, because the property the contract
         * states about them is an ORDER - interim headers, then final headers, then data, then
         * trailers, then exactly one onClosed - and an order is asserted by comparing a rendered
         * trace, not by counting calls
         */

        template
        <
            typename E = void
        >
        class StubStreamEventSinkT : public bl::httpclient::ClientStreamEventSink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubStreamEventSinkT, bl::httpclient::ClientStreamEventSink )

        protected:

            std::vector< std::string >                                          m_trace;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_closedCount;
            bl::cpp::ScalarTypeIniter< bool >                                   m_lastIsRetryable;

            /*
             * The status side, kept apart the way a request task keeps it: the final block's
             * status is the response's and an interim one is that interim response's own
             */

            bl::cpp::ScalarTypeIniter< unsigned >                               m_finalStatus;
            std::vector< unsigned >                                             m_interimStatuses;

            StubStreamEventSinkT() NOEXCEPT
            {
            }

        public:

            const std::vector< std::string >& trace() const NOEXCEPT
            {
                return m_trace;
            }

            unsigned finalStatus() const NOEXCEPT
            {
                return m_finalStatus;
            }

            const std::vector< unsigned >& interimStatuses() const NOEXCEPT
            {
                return m_interimStatuses;
            }

            std::size_t closedCount() const NOEXCEPT
            {
                return m_closedCount;
            }

            bool lastIsRetryable() const NOEXCEPT
            {
                return m_lastIsRetryable;
            }

            std::string traceText() const
            {
                std::string result;

                for( const auto& entry : m_trace )
                {
                    result += entry;
                    result += "|";
                }

                return result;
            }

            virtual void onHeaders(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                const bl::http::HeaderList taken( BL_PARAM_FWD( headers ) );

                if( isInterim )
                {
                    m_interimStatuses.push_back( status );
                }
                else
                {
                    m_finalStatus = status;
                }

                m_trace.push_back(
                    ( isInterim ? "interim:" : "headers:" ) +
                    std::to_string( handle ) +
                    ":" +
                    std::to_string( status ) +
                    ":" +
                    std::to_string( taken.size() )
                    );
            }

            virtual void onData(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                m_trace.push_back(
                    "data:" +
                    std::to_string( handle ) +
                    ":" +
                    std::string( data -> begin() + data -> offset1(), data -> size() - data -> offset1() )
                    );
            }

            virtual void onTrailers(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          bl::http::HeaderList&&                          trailers
                ) OVERRIDE
            {
                const bl::http::HeaderList taken( BL_PARAM_FWD( trailers ) );

                m_trace.push_back(
                    "trailers:" + std::to_string( handle ) + ":" + std::to_string( taken.size() )
                    );
            }

            virtual void onClosed(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::eh::error_code&                       errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                m_closedCount = m_closedCount + 1U;
                m_lastIsRetryable = isRetryable;

                m_trace.push_back(
                    "closed:" +
                    std::to_string( handle ) +
                    ":" +
                    ( errorCode ? std::string( "error" ) : std::string( "ok" ) ) +
                    ":" +
                    ( isRetryable ? std::string( "retryable" ) : std::string( "final" ) )
                    );
            }
        };

        typedef bl::om::ObjectImpl< StubStreamEventSinkT<> > StubStreamEventSink;

        /**
         * @brief The stub connection S5.1 develops against
         *
         * It honors the asynchrony rule the way a test can: submit( ... ) returns a handle and
         * delivers NOTHING, and the events are pushed afterwards by the case calling deliver*( ).
         * A stub which called the sink from inside submit( ... ) would let a request task be
         * written against an ordering the real drivers never provide
         */

        template
        <
            typename E = void
        >
        class StubClientConnectionT : public bl::httpclient::ClientConnection
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubClientConnectionT, bl::httpclient::ClientConnection )

        protected:

            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

            std::vector< bl::httpclient::ClientRequest >                        m_submitted;
            std::vector< bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink > >
                                                                                m_sinks;

            std::vector< std::string >                                          m_calls;

            bl::cpp::ScalarTypeIniter< stream_handle_t >                        m_nextHandle;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_freeSlots;
            bl::cpp::ScalarTypeIniter< bl::httpclient::ConnectionState >        m_state;
            bl::cpp::ScalarTypeIniter< bl::httpclient::HttpProtocol >           m_protocol;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_consumedTotal;

            StubClientConnectionT(
                const bl::httpclient::HttpProtocol                              protocol,
                const std::size_t                                               freeSlots
                ) NOEXCEPT
            {
                m_nextHandle = 1U;
                m_freeSlots = freeSlots;
                m_state = bl::httpclient::ConnectionState::Ready;
                m_protocol = protocol;
            }

        public:

            const std::vector< bl::httpclient::ClientRequest >& submitted() const NOEXCEPT
            {
                return m_submitted;
            }

            const std::vector< std::string >& calls() const NOEXCEPT
            {
                return m_calls;
            }

            std::size_t consumedTotal() const NOEXCEPT
            {
                return m_consumedTotal;
            }

            void setState( const bl::httpclient::ConnectionState state ) NOEXCEPT
            {
                m_state = state;

                if( bl::httpclient::ConnectionState::Ready != state )
                {
                    m_freeSlots = 0U;
                }
            }

            /*
             * The event side - a case drives these to replay a stream onto the sink
             */

            void deliverHeaders(
                const stream_handle_t                                           handle,
                const unsigned                                                  status,
                bl::http::HeaderList                                            headers,
                const bool                                                      isInterim
                )
            {
                /*
                 * The stub ENFORCES the contract's own invariant rather than trusting the case:
                 * isInterim is true exactly when the status is 1xx and is not 101. A stub which
                 * let the two contradict each other would let a request task be developed against
                 * a stream no driver can produce - and 101 is the one that catches people out,
                 * being 1xx and yet a final response
                 */

                BL_CHK_T(
                    false,
                    isInterim == ( status >= 100U && status <= 199U && 101U != status ),
                    bl::ArgumentException(),
                    BL_MSG()
                        << "The interim flag contradicts the response status code"
                    );

                sinkFor( handle ) -> onHeaders( handle, status, BL_PARAM_FWD( headers ), isInterim );
            }

            void deliverData(
                const stream_handle_t                                           handle,
                const std::string&                                              text
                )
            {
                const auto block = bl::data::DataBlock::get( nullptr /* dataBlocksPool */, text.size() + 1U );

                std::memcpy( block -> begin(), text.c_str(), text.size() );
                block -> setSize( text.size() );

                sinkFor( handle ) -> onData( handle, block );
            }

            void deliverTrailers(
                const stream_handle_t                                           handle,
                bl::http::HeaderList                                            trailers
                )
            {
                sinkFor( handle ) -> onTrailers( handle, BL_PARAM_FWD( trailers ) );
            }

            void deliverClosed(
                const stream_handle_t                                           handle,
                const bl::eh::error_code&                                       errorCode,
                const bool                                                      isRetryable
                ) NOEXCEPT
            {
                sinkFor( handle ) -> onClosed( handle, errorCode, isRetryable );
            }

            virtual auto submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                )
                -> stream_handle_t OVERRIDE
            {
                if( 0U == m_freeSlots )
                {
                    return bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                }

                m_freeSlots = m_freeSlots - 1U;

                m_submitted.push_back( request );
                m_sinks.push_back( bl::om::copy( eventSink ) );

                const auto handle = static_cast< stream_handle_t >( m_nextHandle );

                m_nextHandle = handle + 1U;

                m_calls.push_back( "submit:" + std::to_string( handle ) );

                return handle;
            }

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       /* errorCode */
                ) NOEXCEPT OVERRIDE
            {
                m_calls.push_back( "cancel:" + std::to_string( handle ) );
            }

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                m_consumedTotal = m_consumedTotal + bytes;

                m_calls.push_back( "consumed:" + std::to_string( handle ) + ":" + std::to_string( bytes ) );
            }

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const bl::om::ObjPtr< bl::data::DataBlock >&    data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                m_calls.push_back(
                    "body:" +
                    std::to_string( handle ) +
                    ":" +
                    std::to_string( data ? data -> size() - data -> offset1() : 0U ) +
                    ( endStream ? ":end" : ":more" )
                    );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                return m_freeSlots;
            }

            virtual auto state() const NOEXCEPT -> bl::httpclient::ConnectionState OVERRIDE
            {
                return m_state;
            }

            virtual auto protocol() const NOEXCEPT -> bl::httpclient::HttpProtocol OVERRIDE
            {
                return m_protocol;
            }

        protected:

            const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& sinkFor(
                const stream_handle_t                                           handle
                ) const
            {
                BL_CHK_T(
                    true,
                    0U == handle || handle > m_sinks.size(),
                    bl::ArgumentException(),
                    BL_MSG()
                        << "No such stream handle on this stub connection"
                    );

                return m_sinks[ static_cast< std::size_t >( handle ) - 1U ];
            }
        };

        typedef bl::om::ObjectImpl< StubClientConnectionT<> > StubClientConnection;

        /**
         * @brief A stub pool which queues the acquire( ... ) answers instead of calling them
         *
         * Queueing is the whole point: the contract says acquire( ... ) POSTS its answer, and a
         * stub which answered inline would let a caller be written against a synchronous pool.
         * flush() is where the posted handlers would run
         */

        template
        <
            typename E = void
        >
        class StubConnectionPoolT : public bl::httpclient::ConnectionPool
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StubConnectionPoolT, bl::httpclient::ConnectionPool )

        protected:

            std::deque< on_ready_callback_t >                                   m_pending;
            std::vector< bl::httpclient::ConnectionKey >                        m_keys;
            std::vector< std::string >                                          m_releases;

            bl::om::ObjPtr< bl::httpclient::ClientConnection >                  m_connection;
            bl::cpp::ScalarTypeIniter< bool >                                   m_shouldFail;

            StubConnectionPoolT( bl::om::ObjPtr< bl::httpclient::ClientConnection > connection ) NOEXCEPT
                :
                m_connection( BL_PARAM_FWD( connection ) )
            {
            }

        public:

            const std::vector< bl::httpclient::ConnectionKey >& keys() const NOEXCEPT
            {
                return m_keys;
            }

            const std::vector< std::string >& releases() const NOEXCEPT
            {
                return m_releases;
            }

            std::size_t pendingCount() const NOEXCEPT
            {
                return m_pending.size();
            }

            void setShouldFail( const bool shouldFail ) NOEXCEPT
            {
                m_shouldFail = shouldFail;
            }

            /**
             * @brief Runs the answers which acquire( ... ) posted, in order
             */

            void flush()
            {
                while( ! m_pending.empty() )
                {
                    const auto onReady = m_pending.front();

                    m_pending.pop_front();

                    if( m_shouldFail )
                    {
                        onReady(
                            nullptr,
                            std::make_exception_ptr( bl::TimeoutException() )
                            );
                    }
                    else
                    {
                        onReady( m_connection, nullptr );
                    }
                }
            }

            virtual void acquire(
                SAA_in          const bl::httpclient::ConnectionKey&            key,
                SAA_in          const bl::httpclient::ClientRequest&            /* request */,
                SAA_in          on_ready_callback_t&&                           onReady
                ) OVERRIDE
            {
                m_keys.push_back( key );

                m_pending.push_back( BL_PARAM_FWD( onReady ) );
            }

            virtual void releaseStream(
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientConnection >& /* connection */,
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::httpclient::RequestOutcome            outcome
                ) NOEXCEPT OVERRIDE
            {
                m_releases.push_back(
                    std::to_string( handle ) +
                    ":" +
                    std::to_string( static_cast< unsigned >( outcome ) )
                    );
            }
        };

        typedef bl::om::ObjectImpl< StubConnectionPoolT<> > StubConnectionPool;

        /**
         * @brief A stream policy which is nothing but the typedef the driver factory needs
         *
         * The factory is parameterized by the stream policy and touches exactly one thing about
         * it - STREAM::stream_ref - so the smallest honest stand-in is a policy which has that
         * typedef and nothing else. That is also the demonstration that matters: the factory
         * contract pulls in no I/O type of its own
         */

        struct FakeStreamPolicy
        {
            typedef bl::cpp::SafeUniquePtr< std::string >                       stream_ref;
        };

        typedef bl::httpclient::ClientDriverFactoryT< FakeStreamPolicy >        fake_driver_factory_t;

    } // clientcontracts

} // utest

UTF_AUTO_TEST_CASE( ClientContracts_RequestAndResponseRoundTripTests )
{
    using namespace bl;
    using namespace bl::httpclient;

    /*
     * The value objects carry everything a driver and a request task need, and a copy of one is a
     * full copy - the retry rule of design 5.4 replays a request on a second connection and a
     * redirect derives a second request from the first, so both duplicate rather than mutate
     */

    ClientRequest request;

    UTF_CHECK_EQUAL( request.method(), std::string( "GET" ) );
    UTF_CHECK( HttpRequestKind::Fetch == request.kind() );
    UTF_CHECK( ! request.hasBody() );
    UTF_CHECK( request.totalTimeout().is_special() );
    UTF_CHECK( request.responseHeadersTimeout().is_special() );
    UTF_CHECK_EQUAL(
        static_cast< unsigned >( request.priority().urgency.value() ),
        static_cast< unsigned >( RequestPriority::DEFAULT_URGENCY )
        );
    UTF_CHECK( ! request.priority().isIncremental.value() );

    request.method( "POST" );
    request.url( net::Uri::parse( "https://example.com/api/items?page=2" ) );
    request.headers().append( "content-type", "application/json" );
    request.kind( HttpRequestKind::Navigation );
    request.totalTimeout( time::seconds( 45 ) );

    RequestPriority priority;
    priority.urgency = 1U;
    priority.isIncremental = true;
    request.priority( priority );

    const auto body = data::DataBlock::get( nullptr /* dataBlocksPool */, 64U );
    std::memcpy( body -> begin(), "{}", 2U );
    body -> setSize( 2U );

    request.body( body );

    const ClientRequest copied( request );

    UTF_CHECK_EQUAL( copied.method(), std::string( "POST" ) );
    UTF_CHECK_EQUAL( copied.url().host(), std::string( "example.com" ) );
    UTF_CHECK_EQUAL( copied.url().pathAndQuery(), std::string( "/api/items?page=2" ) );
    UTF_CHECK_EQUAL( copied.headers().get( "Content-Type" ), std::string( "application/json" ) );
    UTF_CHECK( HttpRequestKind::Navigation == copied.kind() );
    UTF_CHECK_EQUAL( copied.totalTimeout().total_seconds(), 45L );
    UTF_CHECK_EQUAL( static_cast< unsigned >( copied.priority().urgency.value() ), 1U );
    UTF_CHECK( copied.priority().isIncremental.value() );
    UTF_CHECK( copied.hasBody() );
    UTF_CHECK_EQUAL( copied.body() -> size(), 2U );

    /*
     * The body is either buffered or streamed and never both, and the type enforces that rather
     * than trusting the caller - a driver which found both would have no defined answer to which
     * one goes on the wire
     */

    request.bodySource(
        om::qi< BodySource >(
            utest::clientcontracts::StubBodySource::createInstance(
                std::string( "payload" ),
                true    /* canRewind */,
                4U      /* chunkSize */
                )
            )
        );

    UTF_CHECK( nullptr == request.body() );
    UTF_CHECK( nullptr != request.bodySource() );

    request.body( body );

    UTF_CHECK( nullptr != request.body() );
    UTF_CHECK( nullptr == request.bodySource() );

    /*
     * The response side
     */

    ClientResponse response;

    UTF_CHECK_EQUAL( response.status(), 0U );
    UTF_CHECK( HttpProtocol::Unknown == response.protocol() );
    UTF_CHECK( response.negotiatedAlpn().empty() );
    UTF_CHECK( nullptr == response.impersonationReport() );

    response.status( 200U );
    response.protocol( HttpProtocol::Http2 );
    response.negotiatedAlpn( "h2" );
    response.headers().append( "server", "nginx" );
    response.trailers().append( "x-checksum", "abc" );
    response.body( body );

    const ClientResponse copiedResponse( response );

    UTF_CHECK_EQUAL( copiedResponse.status(), 200U );
    UTF_CHECK( HttpProtocol::Http2 == copiedResponse.protocol() );
    UTF_CHECK_EQUAL( copiedResponse.negotiatedAlpn(), std::string( "h2" ) );
    UTF_CHECK_EQUAL( copiedResponse.headers().get( "Server" ), std::string( "nginx" ) );
    UTF_CHECK_EQUAL( copiedResponse.trailers().get( "X-Checksum" ), std::string( "abc" ) );
    UTF_CHECK_EQUAL( copiedResponse.body() -> size(), 2U );
}

UTF_AUTO_TEST_CASE( ClientContracts_ReplayabilityIsAQueryOnTheRequestTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * Design 5.4 makes a replay need BOTH halves - the failure must prove the request was
     * unprocessed, and the request must be replayable. This is the second half, and the work order
     * says in as many words that it is a query on ClientRequest and NOT on the pool: only the
     * request knows what its body is
     */

    ClientRequest bodiless;
    UTF_CHECK( bodiless.isReplayable() );

    const auto block = data::DataBlock::get( nullptr /* dataBlocksPool */, 16U );
    block -> setSize( 4U );

    ClientRequest buffered;
    buffered.body( block );
    UTF_CHECK( buffered.isReplayable() );

    const auto rewindable = StubBodySource::createInstance(
        std::string( "hello" ),
        true    /* canRewind */,
        2U      /* chunkSize */
        );

    ClientRequest streamedRewindable;
    streamedRewindable.bodySource( om::qi< BodySource >( rewindable ) );
    UTF_CHECK( streamedRewindable.isReplayable() );

    const auto oneShot = StubBodySource::createInstance(
        std::string( "hello" ),
        false   /* canRewind */,
        2U      /* chunkSize */
        );

    ClientRequest streamedOneShot;
    streamedOneShot.bodySource( om::qi< BodySource >( oneShot ) );

    UTF_CHECK( ! streamedOneShot.isReplayable() );

    /*
     * ... and a source which says it cannot rewind refuses to, rather than silently restarting -
     * a source which quietly reset itself would send a truncated or duplicated body
     */

    UTF_CHECK_THROW( oneShot -> rewind(), NotSupportedException );
    UTF_CHECK_EQUAL( oneShot -> rewindCount(), 0U );

    /*
     * Replacing a one-shot source with a buffered body makes the request replayable again, which
     * is exactly what a caller does when it wants retries
     */

    streamedOneShot.body( block );
    UTF_CHECK( streamedOneShot.isReplayable() );
}

UTF_AUTO_TEST_CASE( ClientContracts_BodySourceAndSinkStreamingTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * The pull side: size and end-of-stream are independent, so the last non-empty chunk can carry
     * the end flag with it rather than costing one extra pull
     */

    const auto source = StubBodySource::createInstance(
        std::string( "abcdefg" ),
        true    /* canRewind */,
        3U      /* chunkSize */
        );

    std::string pulled;
    std::size_t pulls = 0U;
    bool sawEnd = false;

    while( ! sawEnd && pulls < 10U )
    {
        const auto block = data::DataBlock::get( nullptr /* dataBlocksPool */, 3U );

        const auto result = source -> read( *block );

        pulled.append( block -> begin(), block -> size() );

        sawEnd = result.isEndOfStream;

        ++pulls;
    }

    UTF_CHECK_EQUAL( pulled, std::string( "abcdefg" ) );
    UTF_CHECK( sawEnd );
    UTF_CHECK_EQUAL( pulls, 3U );

    source -> rewind();
    UTF_CHECK_EQUAL( source -> rewindCount(), 1U );

    const auto again = data::DataBlock::get( nullptr /* dataBlocksPool */, 3U );
    const auto firstAgain = source -> read( *again );
    UTF_CHECK_EQUAL( firstAgain.size.value(), 3U );
    UTF_CHECK( ! firstAgain.isEndOfStream.value() );
    UTF_CHECK_EQUAL( std::string( again -> begin(), again -> size() ), std::string( "abc" ) );

    /*
     * The push side: a sink which consumes less than it was offered is NOT an error - that is how
     * a slow consumer applies backpressure - and the remainder is offered again
     */

    const auto sink = StubBodySink::createInstance( 2U /* maxPerCall */ );

    const auto block = data::DataBlock::get( nullptr /* dataBlocksPool */, 16U );
    std::memcpy( block -> begin(), "12345", 5U );
    block -> setSize( 5U );

    std::size_t offeredRounds = 0U;

    while( block -> offset1() < block -> size() && offeredRounds < 10U )
    {
        const auto consumed = sink -> onData( block );

        UTF_CHECK( consumed <= block -> size() - block -> offset1() );

        block -> setOffset1( block -> offset1() + consumed );

        ++offeredRounds;
    }

    sink -> onComplete();

    UTF_CHECK_EQUAL( sink -> received(), std::string( "12345" ) );
    UTF_CHECK_EQUAL( offeredRounds, 3U );
    UTF_CHECK( sink -> isComplete() );
}

UTF_AUTO_TEST_CASE( ClientContracts_StubConnectionAndEventSinkTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * The deliverable S5.1 develops against, exercised through the interfaces rather than through
     * the stub's own type - which is what proves a request task written against ClientConnection
     * and ClientStreamEventSink can be developed before either driver exists
     */

    const auto stub = StubClientConnection::createInstance( HttpProtocol::Http2, 2U /* freeSlots */ );

    const auto connection = om::qi< ClientConnection >( stub );

    UTF_CHECK( ConnectionState::Ready == connection -> state() );
    UTF_CHECK( HttpProtocol::Http2 == connection -> protocol() );
    UTF_CHECK_EQUAL( connection -> freeStreamSlots(), 2U );

    const auto sinkImpl = StubStreamEventSink::createInstance();
    const auto sink = om::qi< ClientStreamEventSink >( sinkImpl );

    ClientRequest request;
    request.url( net::Uri::parse( "https://example.com/" ) );

    const auto handle = connection -> submit( request, sink );

    UTF_CHECK( ClientConnection::INVALID_STREAM_HANDLE != handle );
    UTF_CHECK_EQUAL( connection -> freeStreamSlots(), 1U );

    /*
     * submit( ... ) delivers NOTHING synchronously - the contract says every call posts and
     * returns, and a stub which called the sink from inside submit would let a request task be
     * written against an ordering the real drivers never provide
     */

    UTF_CHECK( sinkImpl -> trace().empty() );

    bl::http::HeaderList interim;
    interim.append( "x-early", "1" );

    bl::http::HeaderList headers;
    headers.append( "content-type", "text/plain" );
    headers.append( "content-length", "5" );

    bl::http::HeaderList trailers;
    trailers.append( "x-checksum", "abc" );

    stub -> deliverHeaders( handle, 103U /* status */, std::move( interim ), true /* isInterim */ );
    stub -> deliverHeaders( handle, 200U /* status */, std::move( headers ), false /* isInterim */ );
    stub -> deliverData( handle, "hello" );
    stub -> deliverTrailers( handle, std::move( trailers ) );
    stub -> deliverClosed( handle, eh::error_code(), false /* isRetryable */ );

    UTF_CHECK_EQUAL(
        sinkImpl -> traceText(),
        std::string( "interim:1:103:1|headers:1:200:2|data:1:hello|trailers:1:1|closed:1:ok:final|" )
        );

    UTF_CHECK_EQUAL( sinkImpl -> closedCount(), 1U );

    /*
     * Flow control is credited by the consumer, not by the driver (design 5.3)
     */

    connection -> consumed( handle, 5U );
    UTF_CHECK_EQUAL( stub -> consumedTotal(), 5U );

    connection -> provideBody( handle, nullptr, true /* endStream */ );
    connection -> cancel( handle, eh::error_code() );

    UTF_CHECK_EQUAL( stub -> calls().size(), 4U );
    UTF_CHECK_EQUAL( stub -> calls()[ 3 ], std::string( "cancel:1" ) );

    /*
     * A connection with no free slot refuses the submission rather than queueing it - queueing is
     * the pool's job, and a driver which queued silently would hide a stream limit from it
     */

    const auto second = connection -> submit( request, sink );
    UTF_CHECK( ClientConnection::INVALID_STREAM_HANDLE != second );
    UTF_CHECK_EQUAL( connection -> freeStreamSlots(), 0U );

    UTF_CHECK(
        ClientConnection::INVALID_STREAM_HANDLE == connection -> submit( request, sink )
        );

    /*
     * A draining connection reports no slots at all, which is what stops the pool dispatching to it
     */

    stub -> setState( ConnectionState::Draining );
    UTF_CHECK( ConnectionState::Draining == connection -> state() );
    UTF_CHECK_EQUAL( connection -> freeStreamSlots(), 0U );
}

UTF_AUTO_TEST_CASE( ClientContracts_ResponseStatusCrossesTheSinkTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * THE DEFECT THIS CASE EXISTS FOR. As first published the sink's only header event was
     * onHeaders( handle, HeaderList&&, isInterim ), and http::HeaderList refuses ':status' by
     * design - a colon is not a token character - so no driver could put the status anywhere a
     * request task could read it and ClientResponse::status() could not be filled at all. The gap
     * survived because nothing asserted the status; this case asserts it, end to end
     */

    http::HeaderList refuses;

    UTF_CHECK( ! http::HeaderList::isValidHeaderName( ":status" ) );
    UTF_CHECK_THROW( refuses.append( ":status", "200" ), InvalidDataFormatException );
    UTF_CHECK_EQUAL( refuses.size(), 0U );

    const auto stub = StubClientConnection::createInstance( HttpProtocol::Http2, 4U /* freeSlots */ );
    const auto connection = om::qi< ClientConnection >( stub );

    const auto sinkImpl = StubStreamEventSink::createInstance();
    const auto sink = om::qi< ClientStreamEventSink >( sinkImpl );

    ClientRequest request;
    request.url( net::Uri::parse( "https://example.com/" ) );

    const auto handle = connection -> submit( request, sink );

    /*
     * EVERY header block carries its own status. 100 Continue and 103 Early Hints are interim and
     * each brings its own code; the response that follows brings the final one
     */

    http::HeaderList cont;

    http::HeaderList hints;
    hints.append( "link", "</style.css>; rel=preload; as=style" );

    http::HeaderList headers;
    headers.append( "content-type", "text/html" );

    stub -> deliverHeaders( handle, 100U /* status */, std::move( cont ), true /* isInterim */ );
    stub -> deliverHeaders( handle, 103U /* status */, std::move( hints ), true /* isInterim */ );
    stub -> deliverHeaders( handle, 404U /* status */, std::move( headers ), false /* isInterim */ );
    stub -> deliverClosed( handle, eh::error_code(), false /* isRetryable */ );

    UTF_REQUIRE_EQUAL( sinkImpl -> interimStatuses().size(), 2U );
    UTF_CHECK_EQUAL( sinkImpl -> interimStatuses()[ 0 ], 100U );
    UTF_CHECK_EQUAL( sinkImpl -> interimStatuses()[ 1 ], 103U );

    /*
     * ... and an interim status never becomes the response's: the final block's does
     */

    UTF_CHECK_EQUAL( sinkImpl -> finalStatus(), 404U );

    UTF_CHECK_EQUAL(
        sinkImpl -> traceText(),
        std::string( "interim:1:100:0|interim:1:103:1|headers:1:404:1|closed:1:ok:final|" )
        );

    /*
     * What S5.1 will do with it, done here so that the contract is proven to compose: the status
     * the sink received fills ClientResponse::status(), which is the field that had no source
     */

    ClientResponse response;

    UTF_CHECK_EQUAL( response.status(), 0U );

    response.status( sinkImpl -> finalStatus() );
    response.protocol( connection -> protocol() );

    UTF_CHECK_EQUAL( response.status(), 404U );
    UTF_CHECK( HttpProtocol::Http2 == response.protocol() );

    /*
     * The two parameters must agree, and the stub refuses a delivery in which they do not. 101 is
     * the case worth pinning: it is 1xx and yet a FINAL response (RFC 9110 15.2), so a driver
     * which treated "1xx" as "interim" would wait for a header block that never comes
     */

    const auto second = connection -> submit( request, sink );

    UTF_CHECK_THROW(
        stub -> deliverHeaders( second, 101U, http::HeaderList(), true /* isInterim */ ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        stub -> deliverHeaders( second, 200U, http::HeaderList(), true /* isInterim */ ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        stub -> deliverHeaders( second, 103U, http::HeaderList(), false /* isInterim */ ),
        ArgumentException
        );

    http::HeaderList switching;
    switching.append( "upgrade", "websocket" );

    stub -> deliverHeaders( second, 101U /* status */, std::move( switching ), false /* isInterim */ );

    UTF_CHECK_EQUAL( sinkImpl -> finalStatus(), 101U );
    UTF_CHECK_EQUAL( sinkImpl -> interimStatuses().size(), 2U );
}

UTF_AUTO_TEST_CASE( ClientContracts_ConnectionKeyAndPoolTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * The key decides which requests share a connection. Uri::origin() is called for its REFUSAL:
     * a relative reference and a URI with no host are the two cases which would otherwise produce
     * a key with an empty host, and a key with an empty host collides with every other such key -
     * which is one request silently reusing another's connection
     */

    UTF_CHECK_THROW(
        ConnectionKey::fromUri( net::Uri::parse( "/relative/path" ) ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        ConnectionKey::fromUri( net::Uri::parse( "//example.com/p" ) ),
        ArgumentException
        );

    /*
     * A URL which spells out the default port and one which leaves it out are the same key,
     * because the key takes the EFFECTIVE port
     */

    const auto bare = ConnectionKey::fromUri( net::Uri::parse( "https://example.com/a" ) );
    const auto spelled = ConnectionKey::fromUri( net::Uri::parse( "https://example.com:443/b" ) );

    UTF_CHECK( bare == spelled );
    UTF_CHECK( ! ( bare < spelled ) );
    UTF_CHECK( ! ( spelled < bare ) );
    UTF_CHECK_EQUAL( static_cast< unsigned >( bare.port.value() ), 443U );

    /*
     * ... and every field of the key separates, including the two profile ids and the verification
     * flags. Two requests which disagree about verification must not share a connection: the
     * second would inherit the first one's verification decision silently
     */

    auto other = bare;
    other.http2ProfileId = "chrome";
    UTF_CHECK( other != bare );

    other = bare;
    other.tlsProfileId = "chrome";
    UTF_CHECK( other != bare );

    other = bare;
    other.proxyId = "socks5://127.0.0.1:1080";
    UTF_CHECK( other != bare );

    other = bare;
    other.verificationFlags = 1U;
    UTF_CHECK( other != bare );

    UTF_CHECK( ConnectionKey::fromUri( net::Uri::parse( "http://example.com/a" ) ) != bare );
    UTF_CHECK( ConnectionKey::fromUri( net::Uri::parse( "https://other.com/a" ) ) != bare );
    UTF_CHECK( ConnectionKey::fromUri( net::Uri::parse( "https://example.com:8443/a" ) ) != bare );

    /*
     * The pool posts its answer; it never returns one. A pool which answered inline would be
     * calling into the request task under the pool lock, which is rule L4 of design 5.2
     */

    const auto stub = StubClientConnection::createInstance( HttpProtocol::Http2, 4U /* freeSlots */ );

    const auto poolImpl = StubConnectionPool::createInstance(
        om::qi< ClientConnection >( stub )
        );

    const auto pool = om::qi< ConnectionPool >( poolImpl );

    ClientRequest request;
    request.url( net::Uri::parse( "https://example.com/a" ) );

    std::size_t answered = 0U;
    std::size_t failed = 0U;

    const auto onReady = [ &answered, &failed ](
        SAA_in_opt      const om::ObjPtr< ClientConnection >&                   connection,
        SAA_in_opt      const std::exception_ptr&                               exception
        ) -> void
    {
        if( connection )
        {
            ++answered;
        }

        if( exception )
        {
            ++failed;
        }
    };

    pool -> acquire( bare, request, ConnectionPool::on_ready_callback_t( onReady ) );

    UTF_CHECK_EQUAL( answered, 0U );
    UTF_CHECK_EQUAL( poolImpl -> pendingCount(), 1U );

    poolImpl -> flush();

    UTF_CHECK_EQUAL( answered, 1U );
    UTF_CHECK_EQUAL( failed, 0U );
    UTF_CHECK_EQUAL( poolImpl -> keys().size(), 1U );
    UTF_CHECK( poolImpl -> keys()[ 0 ] == bare );

    /*
     * Exactly one of the two is set, so a pool which cannot produce a connection reports an
     * exception rather than a null connection with no reason
     */

    poolImpl -> setShouldFail( true );

    pool -> acquire( bare, request, ConnectionPool::on_ready_callback_t( onReady ) );
    poolImpl -> flush();

    UTF_CHECK_EQUAL( answered, 1U );
    UTF_CHECK_EQUAL( failed, 1U );

    pool -> releaseStream(
        om::qi< ClientConnection >( stub ),
        7U      /* handle */,
        RequestOutcome::ConnectionUnusable
        );

    UTF_CHECK_EQUAL( poolImpl -> releases().size(), 1U );
    UTF_CHECK_EQUAL( poolImpl -> releases()[ 0 ], std::string( "7:2" ) );
}

UTF_AUTO_TEST_CASE( ClientContracts_DriverFactoryDispatchTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::clientcontracts;

    /*
     * The factory is the seam S4.1 dispatches through once ALPN has chosen. It is published before
     * either driver exists and it may not name them, so the creators are registered - which is
     * also what makes the dispatch testable now, against a stub creator and a stand-in stream
     * policy whose whole content is the stream_ref typedef
     */

    fake_driver_factory_t factory;

    UTF_CHECK( ! factory.hasDriver( HttpProtocol::Http2 ) );
    UTF_CHECK( ! factory.hasDriver( HttpProtocol::Http11 ) );

    /*
     * Unknown is the value which means "ALPN has not resolved", so it can never name a driver
     */

    UTF_CHECK_THROW(
        factory.registerDriver(
            HttpProtocol::Unknown,
            fake_driver_factory_t::creator_t(
                []( SAA_inout fake_driver_factory_t::stream_ref&&, SAA_in const ConnectionKey& )
                    -> om::ObjPtr< ClientConnection >
                {
                    return nullptr;
                }
                )
            ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        factory.registerDriver( HttpProtocol::Http2, fake_driver_factory_t::creator_t() ),
        ArgumentException
        );

    std::string handedOverStream;
    std::string handedOverHost;

    factory.registerDriver(
        HttpProtocol::Http2,
        fake_driver_factory_t::creator_t(
            [ &handedOverStream, &handedOverHost ](
                SAA_inout       fake_driver_factory_t::stream_ref&&             connectedStream,
                SAA_in          const ConnectionKey&                            key
                )
                -> om::ObjPtr< ClientConnection >
            {
                /*
                 * Ownership of the stream moves to the driver - after the call the connection
                 * establisher does not have it any more
                 */

                const auto taken = BL_PARAM_FWD( connectedStream );

                handedOverStream = *taken;
                handedOverHost = key.host;

                return om::qi< ClientConnection >(
                    StubClientConnection::createInstance( HttpProtocol::Http2, 100U /* freeSlots */ )
                    );
            }
            )
        );

    UTF_CHECK( factory.hasDriver( HttpProtocol::Http2 ) );
    UTF_CHECK( ! factory.hasDriver( HttpProtocol::Http11 ) );

    const auto key = ConnectionKey::fromUri( net::Uri::parse( "https://example.com/" ) );

    auto stream = fake_driver_factory_t::stream_ref::attach( new std::string( "the-connected-stream" ) );

    const auto driver = factory.createDriver( HttpProtocol::Http2, std::move( stream ), key );

    UTF_CHECK( nullptr != driver );
    UTF_CHECK( HttpProtocol::Http2 == driver -> protocol() );
    UTF_CHECK_EQUAL( handedOverStream, std::string( "the-connected-stream" ) );
    UTF_CHECK_EQUAL( handedOverHost, std::string( "example.com" ) );
    UTF_CHECK( nullptr == stream );

    /*
     * A peer selecting a protocol this build has no driver for is a REFUSAL and not a fallback:
     * quietly speaking something other than what was negotiated is how a client ends up sending
     * HTTP/1.1 into an h2 connection
     */

    auto other = fake_driver_factory_t::stream_ref::attach( new std::string( "another-stream" ) );

    UTF_CHECK_THROW(
        factory.createDriver( HttpProtocol::Http11, std::move( other ), key ),
        NotSupportedException
        );
}
