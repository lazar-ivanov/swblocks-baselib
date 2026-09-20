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

#ifndef __UTEST_TESTHTTPCLIENTREQUESTTASK_H_
#define __UTEST_TESTHTTPCLIENTREQUESTTASK_H_

#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ConnectionPool.h>

#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * S5.1 - the request task (design 5.3 and 5.7)
 *
 * The task under test is protocol-agnostic, so everything here is driven through the S2.6
 * contracts and NOTHING here speaks HTTP/2. That is the property being exercised as much as any
 * single assertion: if a case needed a frame, a stream id or a session, the task would not be what
 * design 5.3 says it is
 *
 * WHY THE PROBES HERE AND NOT utest::clientcontracts' STUBS. The S2.6 stubs are single-threaded
 * value recorders - they exist to prove the interfaces compile and round-trip, and their comment
 * says so. A request task is genuinely concurrent: its mailbox drains on ThreadPoolId::
 * GeneralPurpose while a case delivers events from its own thread, so every record here is mutex
 * guarded and every wait is a CONDITION VARIABLE signalled inside that same lock. There is no poll
 * and no sleep anywhere below, which is the L3 protocol's rule and also the only way these cases
 * are not flakes. The value helpers which ARE reusable - StubBodySource, StubBodySink - are reused
 * rather than copied, which is what the named namespace next door exists for
 *
 * AND WHY TWO CASES USE THE REAL ConnectionPool INSTEAD (the L5 fix round). The probe pool can only
 * record what it was asked; it cannot say whether the pool's own books balance. The seam defect the
 * L5 review ranked High - a stream slot leaked on every refused submit - was invisible to every
 * module in the tree precisely because the pool, the request task and a connection had never been
 * composed in one: S5.1 ran against this probe pool, S5.2 against stub connections. So the cases
 * which pin the pairing rule and the fate of a refusing connection instantiate ConnectionPoolImpl
 * and assert on ITS numbers, and they are still protocol-agnostic - the pool is given a connection,
 * not a driver, and speaks no HTTP/2
 */

namespace utest
{
    namespace requesttask
    {
        using bl::httpclient::ClientConnection;
        using bl::httpclient::ClientRequest;
        using bl::httpclient::ClientStreamEventSink;
        using bl::httpclient::ConnectionKey;
        using bl::httpclient::ConnectionPool;
        using bl::httpclient::ConnectionState;
        using bl::httpclient::NegotiatedProtocol;
        using bl::httpclient::RequestOutcome;
        using bl::httpclient::stream_handle_t;

        enum : std::size_t
        {
            DEFAULT_WAIT_IN_MILLISECONDS        = 30U * 1000U,
        };

        /**
         * @brief The connection a case plays, and the record of what the task asked of it
         *
         * Two directions cross here. Downwards, the task calls submit( ), consumed( ),
         * provideBody( ) and cancel( ), and each is appended to a trace. Upwards, the case calls
         * the deliver…( ) helpers, which is the connection strand's role in design 5.2 - the sink
         * only ever posts, so calling it from the case's thread is exactly as legal as calling it
         * from a strand, and that is itself worth having exercised
         */

        template
        <
            typename E = void
        >
        class ProbeConnectionT : public ClientConnection
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( ProbeConnectionT, ClientConnection )

        protected:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cvTrace;

            std::vector< std::string >                                          m_trace;
            std::string                                                         m_uploaded;

            bl::om::ObjPtr< ClientStreamEventSink >                             m_sink;

            const NegotiatedProtocol                                            m_negotiated;

            bl::cpp::ScalarTypeIniter< stream_handle_t >                        m_handle;
            bl::cpp::ScalarTypeIniter< bool >                                   m_isSubmitRefused;
            bl::cpp::ScalarTypeIniter< bool >                                   m_isStreamingRefused;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_consumedTotal;

            /**
             * @brief The two refusals a driver has, and they are not the same refusal
             *
             * isSubmitRefused is the connection's: it refuses everything, which is what a driver on
             * its way out does. isStreamingRefused is the HTTP/1.1 driver's rule and only that -
             * every request whose body is a BodySource is refused, unconditionally and ahead of
             * every other test its submit( ) makes, because HTTP/1.1 would need request-side
             * chunked framing for a body of unknown length. A connection which refuses THAT way is
             * perfectly well, and a case which cannot express the difference cannot ask what the
             * pool should do about it
             */

            ProbeConnectionT(
                NegotiatedProtocol                                              negotiated,
                const bool                                                      isSubmitRefused,
                const bool                                                      isStreamingRefused = false
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) )
            {
                m_handle = ClientConnection::INVALID_STREAM_HANDLE;
                m_isSubmitRefused = isSubmitRefused;
                m_isStreamingRefused = isStreamingRefused;
            }

            void record( SAA_in std::string what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_trace.push_back( BL_PARAM_FWD( what ) );

                m_cvTrace.notify_all();
            }

            auto sink() const -> bl::om::ObjPtr< ClientStreamEventSink >
            {
                BL_MUTEX_GUARD( m_lock );

                return bl::om::copy( m_sink );
            }

        public:

            std::vector< std::string > trace() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_trace;
            }

            std::string uploaded() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_uploaded;
            }

            std::size_t consumedTotal() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_consumedTotal;
            }

            stream_handle_t handle() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_handle;
            }

            /**
             * @brief Waits until the trace contains a line, and prints the whole trace when it
             * never does
             *
             * ON CONTENT AND NEVER ON A COUNT. A rendezvous on a number makes the case guess how
             * many records precede the one it is about, and the L3 protocol records a suite which
             * guessed wrong and passed three times before failing. Each iteration blocks for one
             * MORE record than has been seen, so the loop advances only when the task really
             * appends and at most one wait can expire
             */

            void waitFor(
                SAA_in          const std::string&                              expected,
                SAA_in_opt      const std::size_t                               timeoutInMilliseconds =
                                    DEFAULT_WAIT_IN_MILLISECONDS
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                for( ;; )
                {
                    if( std::find( m_trace.begin(), m_trace.end(), expected ) != m_trace.end() )
                    {
                        return;
                    }

                    const auto seen = m_trace.size();

                    const auto advanced = m_cvTrace.wait_for(
                        guard,
                        bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                        [ this, seen ]() -> bool
                        {
                            return m_trace.size() > seen;
                        }
                        );

                    if( ! advanced )
                    {
                        break;
                    }
                }

                std::string what;

                for( std::size_t i = 0U; i < m_trace.size(); ++i )
                {
                    what += "\n    ";
                    what += m_trace[ i ];
                }

                UTF_FAIL(
                    BL_MSG()
                        << "The request task never did '"
                        << expected
                        << "'; it did:"
                        << what
                    );
            }

            /**
             * @brief Waits for the CUMULATIVE credited total to reach a number
             *
             * The trace records each consumed( ) call by its own byte count, so two rounds of
             * three bytes are two identical lines and waiting on the line would return on the
             * first. The quantity a backpressure case asserts is the TOTAL, so that is what it
             * waits on - the same predicate as the assertion, which is what the L3 protocol
             * requires of a rendezvous
             */

            void waitForConsumedTotal(
                SAA_in          const std::size_t                               expected,
                SAA_in_opt      const std::size_t                               timeoutInMilliseconds =
                                    DEFAULT_WAIT_IN_MILLISECONDS
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                const auto reached = m_cvTrace.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this, expected ]() -> bool
                    {
                        return m_consumedTotal >= expected;
                    }
                    );

                if( ! reached )
                {
                    UTF_FAIL(
                        BL_MSG()
                            << "The request task credited "
                            << m_consumedTotal.value()
                            << " bytes where the case expects "
                            << expected
                        );
                }
            }

            bool has( SAA_in const std::string& expected ) const
            {
                BL_MUTEX_GUARD( m_lock );

                return std::find( m_trace.begin(), m_trace.end(), expected ) != m_trace.end();
            }

            /*************************************************************************************
             * What the case delivers, playing the connection strand
             */

            void deliverHeaders(
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList                            headers,
                SAA_in          const bool                                      isInterim
                )
            {
                sink() -> onHeaders( handle(), status, BL_PARAM_FWD( headers ), isInterim );
            }

            void deliverData( SAA_in const std::string& text )
            {
                const auto block = bl::data::DataBlock::copy(
                    text.c_str(),
                    text.size(),
                    nullptr /* dataBlocksPool */,
                    std::max< std::size_t >( text.size(), 1U )
                    );

                sink() -> onData( handle(), block );
            }

            void deliverTrailers( SAA_in bl::http::HeaderList trailers )
            {
                sink() -> onTrailers( handle(), BL_PARAM_FWD( trailers ) );
            }

            void deliverBodyWanted( SAA_in const std::size_t bytes )
            {
                sink() -> onBodyWanted( handle(), bytes );
            }

            /**
             * @brief The last event, and the one which RELEASES the sink
             *
             * "The sink is held for the life of the stream and released after onClosed( ... )" is
             * part of the ClientConnection contract and not housekeeping: the connection holds the
             * request task and the request task holds the connection, so a driver which kept the
             * sink would make a cycle that outlives both. A probe which kept it would report that
             * cycle as a leak and blame the task
             */

            void deliverClosed(
                SAA_in_opt      const bl::eh::error_code&                       errorCode =
                                    bl::eh::error_code(),
                SAA_in_opt      const bool                                      isRetryable = false
                )
            {
                const auto held = sink();

                held -> onClosed( handle(), errorCode, isRetryable );

                BL_MUTEX_GUARD( m_lock );

                m_sink.reset();
            }

            /*************************************************************************************
             * ClientConnection
             */

            virtual auto submit(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const bl::om::ObjPtr< ClientStreamEventSink >&  eventSink
                )
                -> stream_handle_t OVERRIDE
            {
                if( m_isSubmitRefused || ( m_isStreamingRefused && nullptr != request.bodySource() ) )
                {
                    record( "submit:refused" );

                    return ClientConnection::INVALID_STREAM_HANDLE;
                }

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_sink = bl::om::copy( eventSink );
                    m_handle = 42U;
                }

                record( "submit" );

                return 42U;
            }

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( errorCode );

                record( "cancel:" + bl::utils::lexical_cast< std::string >( handle ) );
            }

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_consumedTotal = m_consumedTotal + bytes;
                }

                record( "consumed:" + bl::utils::lexical_cast< std::string >( bytes ) );
            }

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const bl::om::ObjPtr< bl::data::DataBlock >&    data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                std::size_t size = 0U;

                {
                    BL_MUTEX_GUARD( m_lock );

                    if( data )
                    {
                        size = data -> size() - data -> offset1();

                        m_uploaded.append(
                            data -> begin() + data -> offset1(),
                            data -> begin() + data -> size()
                            );
                    }

                }

                record(
                    "body:" +
                    bl::utils::lexical_cast< std::string >( size ) +
                    ( endStream ? ":end" : ":more" )
                    );
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

        typedef bl::om::ObjectImpl< ProbeConnectionT<> > ProbeConnection;

        /**
         * @brief The pool a case plays - it POSTS its answer, or withholds it entirely
         *
         * Posting is not a detail of the stub, it is the contract: acquire( ) "never returns a
         * connection: it posts one", because a pool which answered inline would be calling into
         * the request task under the pool lock. The thread the answer is made on is recorded, so a
         * case can assert that the task's start really did leave the caller's execution queue
         */

        template
        <
            typename E = void
        >
        class ProbePoolT : public ConnectionPool
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( ProbePoolT, ConnectionPool )

        protected:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cvReleased;

            const bl::om::ObjPtr< ClientConnection >                            m_connection;

            std::vector< std::string >                                          m_releases;

            std::thread::id                                                     m_acquiredOn;

            std::shared_ptr< on_ready_callback_t >                              m_held;

            bl::cpp::ScalarTypeIniter< bool >                                   m_isAcquired;
            bl::cpp::ScalarTypeIniter< bool >                                   m_isAnswered;
            bl::cpp::ScalarTypeIniter< bool >                                   m_isHeld;

            ProbePoolT(
                bl::om::ObjPtr< ClientConnection >                              connection,
                const bool                                                      isAnswered
                )
                :
                m_connection( BL_PARAM_FWD( connection ) )
            {
                m_isAnswered = isAnswered;
            }

            /**
             * @brief The one place an answer is delivered, so held and prompt ones are one path
             */

            void postAnswer( SAA_in const std::shared_ptr< on_ready_callback_t >& callback ) const
            {
                const auto connection = bl::om::ObjPtrCopyable< ClientConnection >( m_connection );

                bl::ThreadPoolDefault::getDefault(
                    bl::ThreadPoolId::GeneralPurpose
                    ) -> aioService().post(
                        [ connection, callback ]() -> void
                        {
                            ( *callback )( connection, std::exception_ptr() );
                        }
                        );
            }

        public:

            std::vector< std::string > releases() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_releases;
            }

            bool isAcquired() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_isAcquired;
            }

            auto acquiredOn() const -> std::thread::id
            {
                BL_MUTEX_GUARD( m_lock );

                return m_acquiredOn;
            }

            /**
             * @brief Keeps the next answer back, so a case can deliver it LATE
             *
             * The request whose deadline expires while it is queued is not answered with nothing -
             * the pool has no way to know it has given up, so a connection it frees a moment later
             * is dispatched to a request which has already failed. That answer is the one the pool
             * must still get its slot back for, and holding the callback is how a case produces it
             * without a race
             */

            void holdTheAnswer() NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                m_isHeld = true;
            }

            /**
             * @brief Delivers the answer that was held, on the pool's own thread as always
             */

            void answerNow()
            {
                std::shared_ptr< on_ready_callback_t > callback;

                {
                    BL_MUTEX_GUARD( m_lock );

                    callback.swap( m_held );
                }

                UTF_REQUIRE( nullptr != callback );

                postAnswer( callback );
            }

            /**
             * @brief Waits for the stream slot to come back - the rendezvous for everything a case
             * wants to assert about releaseStream( )
             */

            bool waitForRelease(
                SAA_in_opt      const std::size_t                               timeoutInMilliseconds =
                                    DEFAULT_WAIT_IN_MILLISECONDS
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cvReleased.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return ! m_releases.empty();
                    }
                    );
            }

            virtual void acquire(
                SAA_in          const ConnectionKey&                            key,
                SAA_in          const ClientRequest&                            request,
                SAA_in          on_ready_callback_t&&                           onReady
                ) OVERRIDE
            {
                BL_UNUSED( key );
                BL_UNUSED( request );

                bool answer = false;
                bool hold = false;

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_isAcquired = true;
                    m_acquiredOn = std::this_thread::get_id();

                    answer = m_isAnswered;
                    hold = m_isHeld;
                }

                if( ! answer )
                {
                    /*
                     * The connection which never comes - which is what a request timing out in the
                     * pool's FIFO queue looks like from here. The callback is simply dropped
                     */

                    return;
                }

                const auto callback = std::make_shared< on_ready_callback_t >( BL_PARAM_FWD( onReady ) );

                if( hold )
                {
                    BL_MUTEX_GUARD( m_lock );

                    m_held = callback;

                    return;
                }

                postAnswer( callback );
            }

            virtual void releaseStream(
                SAA_in          const bl::om::ObjPtr< ClientConnection >&       connection,
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const RequestOutcome                            outcome
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( connection );

                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( m_lock );

                m_releases.push_back(
                    bl::utils::lexical_cast< std::string >( handle ) +
                    ":" +
                    (
                        RequestOutcome::Completed == outcome ? "completed" :
                            ( RequestOutcome::Failed == outcome ? "failed" : "unusable" )
                    )
                    );

                m_cvReleased.notify_all();

                BL_NOEXCEPT_END()
            }
        };

        typedef bl::om::ObjectImpl< ProbePoolT<> > ProbePool;

        /**
         * @brief A body source whose read( ) fails, which is the everyday file-backed one
         *
         * The deferral record says every source that exists today is "always ready", and a source
         * over a file is the reason that is not the same as "always succeeds": the file can be
         * truncated, unlinked or served off a mount which went away between one pull and the next.
         * read( ) is the CALLER's code and the caller is entitled to throw out of it
         */

        template
        <
            typename E = void
        >
        class ThrowingBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( ThrowingBodySourceT, bl::httpclient::BodySource )

        protected:

            ThrowingBodySourceT() NOEXCEPT
            {
            }

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                BL_UNUSED( target );

                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "The request body could not be read"
                    );
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return false;
            }

            virtual void rewind() OVERRIDE
            {
                BL_THROW(
                    bl::NotSupportedException(),
                    BL_MSG()
                        << "This body source cannot rewind"
                    );
            }
        };

        typedef bl::om::ObjectImpl< ThrowingBodySourceT<> > ThrowingBodySource;

        /**
         * @brief What the real ConnectionPool asks for, answered with a connection the case holds
         *
         * The shape is the ALPN fallback's and not the h2 task's: the attempt carries a task the
         * pool schedules and an accessor which names the connection, and the pool's own rule -
         * "prefer what the accessor returns" - makes the probe connection the connection for this
         * key. That is what lets a case compose the real pool with a ClientConnection which is not
         * also a task, and it is exercised by the pool's own suite from the other side
         */

        inline auto connectionFactoryFor( SAA_in const bl::om::ObjPtr< ClientConnection >& connection )
            -> bl::httpclient::connection_factory_t
        {
            const bl::om::ObjPtrCopyable< ClientConnection > held( connection );

            return [ held ](
                SAA_in          const ConnectionKey&                            key,
                SAA_in          const bl::httpclient::ConnectionPoolPolicy&     policy
                )
                -> bl::httpclient::ConnectionAttempt
            {
                BL_UNUSED( key );
                BL_UNUSED( policy );

                bl::httpclient::ConnectionAttempt attempt;

                attempt.task = bl::om::ObjPtrCopyable< bl::tasks::Task >(
                    bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >()
                    );

                attempt.driver = [ held ]() -> bl::om::ObjPtr< ClientConnection >
                {
                    return bl::om::copy( held );
                };

                return attempt;
            };
        }

        inline auto makeRequest( SAA_in_opt const std::string& method = "GET" ) -> ClientRequest
        {
            ClientRequest request;

            request.method( bl::cpp::copy( method ) );
            request.url( bl::net::Uri::parse( "https://example.com/resource" ) );

            return request;
        }

        inline auto makeKey() -> ConnectionKey
        {
            return ConnectionKey::fromUri( bl::net::Uri::parse( "https://example.com/resource" ) );
        }

        /**
         * @brief Runs one request task, with the case driving the connection while it runs
         */

        template
        <
            typename CALLBACK
        >
        inline void runTask(
            SAA_in          const bl::om::ObjPtr< bl::tasks::Task >&            task,
            SAA_in          const CALLBACK&                                     callback
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    callback();

                    eq -> wait( task );
                }
                );
        }

        inline auto messageOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> std::string
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
         * @brief Fails with the reason the task failed, rather than with a bare predicate
         *
         * IT IS A FUNCTION AND NOT A UTF MACRO ARGUMENT, and that is not a style choice.
         * UTF_FAIL( msg ) takes UtfGlobals::g_lock and THEN evaluates msg, and bl::os::mutex is not
         * recursive - so a UTF macro inside the message of another one deadlocks the binary on its
         * own thread, looking exactly like a hung test. Utf.h warns about it where the exception
         * macros are defined
         */

        inline void requireSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            if( ! task -> isFailed() )
            {
                return;
            }

            UTF_FAIL( "the request task failed: " + messageOf( task ) );
        }

        inline void requireTrue(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( condition )
            {
                return;
            }

            UTF_FAIL( message );
        }

    } // requesttask

} // utest

/**
 * @brief The whole buffered path, and the two things about it which were settled before it existed
 *
 * The response is filled from the FINAL header block and from the ONE negotiated( ) value; the
 * stream slot goes back to the pool marked Completed; and the pool's acquire( ) is shown to have
 * happened on a thread other than the one which pushed the task, which is what "scheduleTask only
 * posts a start handler" means in practice - the contract at TaskBase.h:857 exists so that nothing
 * pool-related runs under the caller's execution queue lock
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_BufferedCompletionTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    const auto pushedOn = std::this_thread::get_id();

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            http::HeaderList headers;

            headers.append( "content-type", "text/plain" );

            connection -> deliverHeaders( 200U, std::move( headers ), false /* isInterim */ );
            connection -> deliverData( "hello " );
            connection -> deliverData( "world" );

            http::HeaderList trailers;

            trailers.append( "x-checksum", "abc" );

            connection -> deliverTrailers( std::move( trailers ) );

            connection -> deliverClosed();
        }
        );

    requireSucceeded( task );

    const auto& response = taskImpl -> response();

    UTF_REQUIRE_EQUAL( response.status(), 200U );
    UTF_REQUIRE( response.headers().has( "content-type" ) );
    UTF_REQUIRE( response.trailers().has( "x-checksum" ) );

    UTF_REQUIRE( response.body() );

    UTF_REQUIRE_EQUAL(
        std::string(
            response.body() -> begin() + response.body() -> offset1(),
            response.body() -> begin() + response.body() -> size()
            ),
        std::string( "hello world" )
        );

    /*
     * ONE VALUE FILLS BOTH, which is the whole reason ClientResponse has no setter for either half
     */

    UTF_REQUIRE( HttpProtocol::Http2 == response.protocol() );
    UTF_REQUIRE_EQUAL( response.negotiatedAlpn(), std::string( "h2" ) );

    /*
     * The bytes were credited as they were taken - the backpressure chain of design 5.3
     */

    UTF_REQUIRE_EQUAL( connection -> consumedTotal(), 11U );

    UTF_REQUIRE( pool -> waitForRelease() );
    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:completed" ) );

    UTF_REQUIRE( pool -> isAcquired() );
    UTF_REQUIRE( pool -> acquiredOn() != pushedOn );
}

/**
 * @brief An interim response is its own response, and never the one being made
 *
 * A 103 Early Hints block arrives with its own status and is followed by the real one. The thing
 * this pins is that the 103 does NOT become ClientResponse::status( ) - which it would under any
 * implementation that took the status of the last block it saw, and which is the defect the S2.6
 * status parameter was added to make expressible in the first place
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_InterimResponsesDoNotOverwriteTheStatusTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            http::HeaderList hints;

            hints.append( "link", "</style.css>; rel=preload" );

            connection -> deliverHeaders( 103U, std::move( hints ), true /* isInterim */ );

            http::HeaderList headers;

            headers.append( "content-type", "text/html" );

            connection -> deliverHeaders( 204U, std::move( headers ), false /* isInterim */ );

            connection -> deliverClosed();
        }
        );

    requireSucceeded( task );

    UTF_REQUIRE_EQUAL( taskImpl -> response().status(), 204U );

    UTF_REQUIRE_EQUAL( taskImpl -> interimResponses().size(), 1U );
    UTF_REQUIRE_EQUAL( taskImpl -> interimResponses()[ 0 ].status.value(), 103U );
    UTF_REQUIRE( taskImpl -> interimResponses()[ 0 ].headers.has( "link" ) );
}

/**
 * @brief A sink which takes three bytes at a time, and the credit that follows it exactly
 *
 * THE NUMBER IS THE ASSERTION. The sink is offered eight bytes in one block and consumes at most
 * three per call, so a task which credited what it was OFFERED would report eight and a task which
 * credits what was TAKEN reports three per round - and since over-acknowledging fails the whole
 * connection rather than the stream, the difference is not cosmetic. The remainder is re-offered
 * before anything newer, which is what keeps the streamed body in order, and the case checks the
 * bytes came out in order rather than merely adding up
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_StreamingSinkCreditsOnlyWhatItTookTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::withoutAlpn( HttpProtocol::Http11 ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = utest::clientcontracts::StubBodySink::createInstance( 3U /* maxPerCall */ );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        HttpClientRequestConfig(),
        om::ObjPtrCopyable< BodySink >( om::qi< BodySink >( sink ) )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverData( "abcdefgh" );

            /*
             * Three bytes were taken and credited; the rest is held for the next offer, which is
             * what the second block triggers
             */

            connection -> waitForConsumedTotal( 3U );

            connection -> deliverData( "ij" );

            connection -> waitForConsumedTotal( 6U );

            connection -> deliverClosed();
        }
        );

    requireSucceeded( task );

    /*
     * IN ORDER, and never a byte of the second block before the first block is done
     */

    UTF_REQUIRE_EQUAL( sink -> received(), std::string( "abcdef" ) );
    UTF_REQUIRE( sink -> isComplete() );

    UTF_REQUIRE_EQUAL( connection -> consumedTotal(), 6U );

    /*
     * The body did NOT also accumulate in the response - a streamed body goes to the sink and
     * body() stays empty, which is design 5.3's "both forms are representable"
     */

    UTF_REQUIRE( ! taskImpl -> response().body() );
}

/**
 * @brief The upload pull of S5.1, from this side of it
 *
 * EXACTLY ONE provideBody( ) PER onBodyWanted( ), which is the half of the contract the request
 * task owes: the driver does not ask again until it has been answered, so an answer which never
 * came would stop the upload for good. Two pulls are made here and the source is set to produce
 * four bytes at a time, so the trace has to show two answers carrying four bytes each and the
 * second of them ending the stream - the source runs out exactly on it
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_UploadIsAnsweredOncePerPullTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto source = utest::clientcontracts::StubBodySource::createInstance(
        std::string( "12345678" ),
        true /* canRewind */,
        4U /* chunkSize */
        );

    auto request = makeRequest( "POST" );

    request.bodySource(
        om::ObjPtrCopyable< BodySource >( om::qi< BodySource >( source ) )
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        std::move( request ),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverBodyWanted( 4096U );

            connection -> waitFor( "body:4:more" );

            connection -> deliverBodyWanted( 4096U );

            connection -> waitFor( "body:4:end" );

            connection -> deliverHeaders( 201U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverClosed();
        }
        );

    requireSucceeded( task );

    UTF_REQUIRE_EQUAL( connection -> uploaded(), std::string( "12345678" ) );

    UTF_REQUIRE_EQUAL( taskImpl -> response().status(), 201U );
}

/**
 * @brief A request which never gets a connection times out in the pool's queue
 *
 * The pool here simply drops the callback, which is what a request sitting behind a busy
 * connection until its deadline looks like from the request's side. Two things are pinned: the
 * timeout covers the POOL WAIT and not only the stream, which is what design 5.7's "including pool
 * wait" means, and the message is the shape SimpleHttpTask already produces, so a caller
 * recognising one recognises the other
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_TotalTimeoutCoversThePoolWaitTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto pool = ProbePool::createInstance(
        om::ObjPtr< ClientConnection >(),
        false /* isAnswered */
        );

    HttpClientRequestConfig config;

    config.totalTimeout = time::milliseconds( 250 );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask( task, []() -> void {} );

    UTF_REQUIRE( task -> isFailed() );

    const auto message = messageOf( task );

    requireTrue(
        message.find( "HTTP GET request to 'https://example.com/resource' has timed out" ) !=
            std::string::npos,
        "the timeout message did not have the existing shape: " + message
        );

    UTF_REQUIRE( pool -> isAcquired() );

    /*
     * Nothing was ever acquired, so there is nothing to give back - and nothing was given back
     */

    UTF_REQUIRE( pool -> releases().empty() );
}

/**
 * @brief A response-headers timeout resets the STREAM and leaves the connection alone
 *
 * Design 5.7 is explicit that cancelling a request never closes the connection, and the trace is
 * where that is visible: a cancel( ) on the stream handle and nothing else. The closure which the
 * driver then sends arrives at a task which has already answered its caller, and the case follows
 * it through - because that late closure is what hands the stream slot back, and a task which
 * stopped listening after its own completion would leak a slot per timeout
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_HeadersTimeoutResetsTheStreamOnlyTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    HttpClientRequestConfig config;

    config.responseHeadersTimeout = time::milliseconds( 250 );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "cancel:42" );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        messageOf( task ).find( "has timed out" ) != std::string::npos
        );

    UTF_REQUIRE( ConnectionState::Ready == connection -> state() );

    /*
     * The closure the driver sends after the reset - delivered AFTER the task has completed, which
     * is the ordinary order and not a corner case
     */

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    UTF_REQUIRE( pool -> waitForRelease() );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:failed" ) );
}

/**
 * @brief requestCancel( ) is the timeout path without the timer
 *
 * The same reset goes out, the task fails with an EXPECTED exception - so TaskBase does not log a
 * deliberate cancel as a failure - and the connection is untouched
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_CancelResetsTheStreamTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            task -> requestCancel();

            connection -> waitFor( "cancel:42" );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        messageOf( task ).find( "was cancelled" ) != std::string::npos
        );

    UTF_REQUIRE( ConnectionState::Ready == connection -> state() );

    /*
     * The closure a driver always sends after a reset. It is not tidying-up: onClosed( ) is the
     * last event of EVERY stream however it ended, and a case which stopped short of it would
     * leave the connection holding the sink - which is to say the task - for good, and the module
     * would report a leak that belongs to the case rather than to the code
     */

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    UTF_REQUIRE( pool -> waitForRelease() );
}

/**
 * @brief A buffered body over the cap resets the stream rather than truncating
 *
 * THE BYTES ARE NOT CREDITED, which is the part worth pinning: crediting them would open the
 * window and ask the server for more of a body already refused. So the trace shows a cancel and no
 * consumed( ) at all, and the request fails rather than completing with a body that merely looks
 * short
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_ResponseBodyCapResetsTheStreamTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    HttpClientRequestConfig config;

    config.maxResponseBodySize = 4U;

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverData( "far too much" );

            connection -> waitFor( "cancel:42" );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE(
        messageOf( task ).find( "exceeded the maximum" ) != std::string::npos
        );

    UTF_REQUIRE_EQUAL( connection -> consumedTotal(), 0U );

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    UTF_REQUIRE( pool -> waitForRelease() );
}

/**
 * @brief A connection which refuses the request it was acquired for
 *
 * INVALID_STREAM_HANDLE is the contract's refusal, and what it means here is that the request was
 * provably not written - so it is marked retryable, which is the half of design 5.4's retry rule
 * this layer can state - while the connection goes back marked unusable, since one which cannot
 * take the request it was handed out for is not one to hand out again
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_RefusedSubmitIsRetryableTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        true /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask( task, []() -> void {} );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE( taskImpl -> isRetryable() );

    UTF_REQUIRE( connection -> has( "submit:refused" ) );

    UTF_REQUIRE( RequestOutcome::ConnectionUnusable == taskImpl -> outcome() );

    /*
     * The slot the pool handed out comes back even though no stream was ever opened - the probe
     * records it by the handle it was named with, and INVALID_STREAM_HANDLE is zero. What that
     * release DOES to the pool's books is the next case, which uses the real one
     */

    UTF_REQUIRE( pool -> waitForRelease() );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "0:unusable" ) );
}

/**
 * @brief THE SEAM: a refused submit gives the REAL pool its slot back (L5 review, finding 1)
 *
 * This is the first of the two cases in which the pool under test is ConnectionPoolImpl and not a
 * probe, and the reason is that nothing else can see the defect. It is the h2 half - a connection
 * which really is not to be handed out again; the case below is the other, where the connection is
 * healthy and the REQUEST is what it could not take
 *
 * The pool keeps a per-connection count of its own -
 * Entry::slotsInUse - incremented the moment it ANSWERS an acquire( ) and decremented only by
 * releaseStream( ), which is given a connection and ignores the handle. S5.1 read "there is no
 * stream to release" as "there is nothing to give back", so every refused submit left that count
 * one higher for ever: the entry could never be forgotten (it is forgotten only when retired AND at
 * zero), the connection task stayed alive in the pool's map, and for h2 - one connection per key -
 * a unit of capacity was gone for good. Refusals are ordinary: the h2 driver refuses everything
 * after closeSubmissions( ), the h1 driver refuses every BodySource request
 *
 * So the assertions are the POOL's numbers and not a probe's record: one dispatched, one released,
 * no slot outstanding, and no connection left behind. Against the unfixed task every one of the
 * last three is wrong
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_RefusedSubmitReturnsTheSlotToTheRealPoolTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        true /* isSubmitRefused */
        );

    const auto asConnection = om::qi< ClientConnection >( connection );

    const auto pool = ConnectionPoolImpl::createInstance(
        connectionFactoryFor( asConnection ),
        ConnectionPoolPolicy()
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask( task, []() -> void {} );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE( connection -> has( "submit:refused" ) );

    /*
     * No rendezvous is needed for what follows and none is used: releaseStream( ) is one of the
     * deferred actions of the same drain that failed the request, and the drain runs every one of
     * them BEFORE it notifies completion - which is what runTask( ) waited for
     */

    const auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );
    UTF_REQUIRE_EQUAL( stats.released.value(), 1U );

    UTF_REQUIRE_EQUAL( pool -> slotsInUse( asConnection ), 0U );

    /*
     * ConnectionUnusable retires the entry, and a retired entry at zero slots is forgotten - which
     * is the consequence the leak used to prevent, and the reason the leak cost a connection and
     * not only a slot
     */

    UTF_REQUIRE_EQUAL( stats.connectionsRetired.value(), 1U );
    UTF_REQUIRE_EQUAL( pool -> connectionCount(), 0U );

    pool -> dispose();
}

/**
 * @brief THE OTHER HALF OF IT: a connection refusing a request IT cannot carry is not a bad
 * connection (the L5 second pass)
 *
 * The HTTP/1.1 driver refuses every request whose body is a BodySource, unconditionally and before
 * it looks at its own state: HTTP/1.1 would need request-side chunked framing for a body of unknown
 * length and the S2.5 serializer has none. That refusal says nothing whatever about the connection,
 * and the task used to report it as ConnectionUnusable anyway - which retires the entry, and since
 * the fix round made the pool CANCEL what it forgets, destroys a working connection once per
 * streaming upload a caller sends its way. The fix round made that path worse while mending two
 * others, which is why this case exists in the same round
 *
 * WHAT THE PROBE STANDS IN FOR, and why that is honest: it refuses exactly what the h1 driver
 * refuses - a request whose body is a BodySource, and nothing else - and it reports HTTP/1.1. That
 * is the pair the task classifies on, ClientRequest::bodySource( ) and
 * ClientConnection::negotiated( ), both already on the frozen contract. The real driver's refusal
 * is its first statement, ahead of every other test it makes, so nothing about a real one would
 * reach the task differently. Its own suite owns that half; no test header is shared across modules
 *
 * THE POOL IS THE REAL ONE for the reason the case above it is: only ConnectionPoolImpl can say
 * whether the connection survived. connectionsRetired and connectionCount are the direct
 * observables, and the factory counter is the sharp one - a pool which forgot the connection would
 * have to establish another for the second request, and this asserts it did not have to. The second
 * request is an ordinary GET and it succeeds over that same connection, which is what "still
 * poolable" means. Against the unfixed task the entry is RETIRED - which is where the case stops -
 * and the rest follows it: retired at zero slots is forgotten, so the count falls to zero and the
 * second request has to establish a connection of its own
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_UnsuitableRequestKeepsTheConnectionPoolableTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::withoutAlpn( HttpProtocol::Http11 ),
        false /* isSubmitRefused */,
        true /* isStreamingRefused */
        );

    const auto asConnection = om::qi< ClientConnection >( connection );

    /*
     * Counted rather than captured by reference: the pool calls the factory from its own thread,
     * outside its lock, and the case reads the number from another
     */

    const auto established = std::make_shared< std::atomic< std::size_t > >( 0U );

    const auto inner = connectionFactoryFor( asConnection );

    const connection_factory_t factory =
        [ inner, established ](
            SAA_in          const ConnectionKey&                                key,
            SAA_in          const ConnectionPoolPolicy&                         policy
            )
            -> ConnectionAttempt
        {
            ++( *established );

            return inner( key, policy );
        };

    const auto pool = ConnectionPoolImpl::createInstance( factory, ConnectionPoolPolicy() );

    const auto source = utest::clientcontracts::StubBodySource::createInstance(
        std::string( "12345678" ),
        true /* canRewind */,
        4U /* chunkSize */
        );

    auto upload = makeRequest( "POST" );

    upload.bodySource(
        om::ObjPtrCopyable< BodySource >( om::qi< BodySource >( source ) )
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        std::move( upload ),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask( task, []() -> void {} );

    UTF_REQUIRE( task -> isFailed() );

    UTF_REQUIRE( connection -> has( "submit:refused" ) );

    /*
     * Failed and not ConnectionUnusable - the request is what could not be carried - and still
     * retryable, because nothing was written and a connection which negotiated h2 would take it
     */

    UTF_REQUIRE( RequestOutcome::Failed == taskImpl -> outcome() );

    UTF_REQUIRE( taskImpl -> isRetryable() );

    requireTrue(
        messageOf( task ).find( "cannot carry a request whose body is streamed" ) != std::string::npos,
        "the request did not fail with the protocol's own refusal: " + messageOf( task )
        );

    const auto afterRefusal = pool -> stats();

    UTF_REQUIRE_EQUAL( afterRefusal.dispatched.value(), 1U );
    UTF_REQUIRE_EQUAL( afterRefusal.released.value(), 1U );

    UTF_REQUIRE_EQUAL( pool -> slotsInUse( asConnection ), 0U );

    /*
     * The three the fix is for: the connection was not retired, so it was not forgotten, so it was
     * not cancelled
     */

    UTF_REQUIRE_EQUAL( afterRefusal.connectionsRetired.value(), 0U );
    UTF_REQUIRE_EQUAL( pool -> connectionCount(), 1U );
    UTF_REQUIRE_EQUAL( established -> load(), 1U );

    /*
     * And it is not merely present but usable: an ordinary request the protocol CAN carry goes out
     * over the same connection and comes back
     */

    const auto secondImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto second = om::qi< tasks::Task >( secondImpl );

    runTask(
        second,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverClosed();
        }
        );

    requireSucceeded( second );

    UTF_REQUIRE_EQUAL( secondImpl -> response().status(), 200U );

    const auto afterSecond = pool -> stats();

    UTF_REQUIRE_EQUAL( afterSecond.dispatched.value(), 2U );
    UTF_REQUIRE_EQUAL( afterSecond.connectionsRetired.value(), 0U );

    /*
     * ONE connection was ever established, for two requests
     */

    UTF_REQUIRE_EQUAL( established -> load(), 1U );

    pool -> dispose();
}

/**
 * @brief The other unpaired path: an answer which arrives after the request has given up
 *
 * The pool cannot know a queued request has timed out, so a connection it frees a moment later is
 * dispatched to one which has already failed - and that dispatch took a slot. The task's own
 * comment always said the slot was "handed straight back rather than leaked"; it was not, because
 * the release was guarded on a stream handle this path never has. Nothing is submitted, which is
 * the other half of it: the connection is untouched
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_LatePoolAnswerStillReturnsTheSlotTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    pool -> holdTheAnswer();

    HttpClientRequestConfig config;

    config.totalTimeout = time::milliseconds( 250 );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask( task, []() -> void {} );

    UTF_REQUIRE( task -> isFailed() );

    requireTrue(
        messageOf( task ).find( "has timed out" ) != std::string::npos,
        "the request did not fail with a timeout: " + messageOf( task )
        );

    UTF_REQUIRE( pool -> releases().empty() );

    /*
     * The connection the pool freed a moment too late
     */

    pool -> answerNow();

    UTF_REQUIRE( pool -> waitForRelease() );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "0:failed" ) );

    UTF_REQUIRE( ! connection -> has( "submit" ) );
}

/**
 * @brief A BodySource which throws fails the request instead of ending the process
 *
 * read( ) used to be called in the apply phase, under the task lock, inside onDrain( )'s NOEXCEPT
 * region - so a throw from it reached BL_RIP_MSG and fastAbort( ). Against the unfixed task this
 * case does not fail, it takes the whole binary down with it. What it pins now is the whole of the
 * recovery: the caller's own exception is what the request fails with, and the stream is RESET
 * before the caller is told, which is what the deferred-exception path used to omit - a stream left
 * open on the driver with no RST_STREAM and no timer is bounded only by the peer's patience
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_BodySourceWhichThrowsFailsTheRequestTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto source = ThrowingBodySource::createInstance();

    auto request = makeRequest( "POST" );

    request.bodySource(
        om::ObjPtrCopyable< BodySource >( om::qi< BodySource >( source ) )
        );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        std::move( request ),
        makeKey(),
        om::qi< ConnectionPool >( pool )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverBodyWanted( 4096U );

            connection -> waitFor( "cancel:42" );
        }
        );

    UTF_REQUIRE( task -> isFailed() );

    requireTrue(
        messageOf( task ).find( "The request body could not be read" ) != std::string::npos,
        "the request did not fail with the source's own exception: " + messageOf( task )
        );

    /*
     * Nothing was put on the wire for a read which never produced anything - not even the empty
     * answer a source is allowed to give
     */

    UTF_REQUIRE( ! connection -> has( "body:0:more" ) );
    UTF_REQUIRE( ! connection -> has( "body:0:end" ) );

    /*
     * The closure the driver sends after the reset, and the slot which comes back with it
     */

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    UTF_REQUIRE( pool -> waitForRelease() );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:failed" ) );
}

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASK_H_ */
