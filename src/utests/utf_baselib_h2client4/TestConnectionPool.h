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

#ifndef __UTEST_TESTCONNECTIONPOOL_H_
#define __UTEST_TESTCONNECTIONPOOL_H_

#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TaskBase.h>

#include <baselib/core/Uri.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * S5.2 - the connection pool (design 5.4, D6, D21)
 *
 * WHAT IS UNDER TEST IS THE BOOKKEEPING, and it is tested against stubs on purpose. The pool's
 * subject is who gets which connection and when - queueing behind a placeholder, the stream slot
 * accounting, the retry matrix, GOAWAY draining, the establishment bound and disposal - and none
 * of that is made more true by a socket. A real driver is exercised end to end by S6.1; what a
 * peer would add here is minutes of wall clock and the flakiness of a network in exchange for
 * nothing the pool decides.
 *
 * The stubs are deliberately shaped like the real thing where the shape is what the pool depends
 * on. StubConnectionTaskT is a TASK WHICH IS ALSO A ClientConnection, exactly as
 * Http2ConnectionTaskT is - it has om::Object as a base twice and the same two-entry QI table -
 * because the pool reaches its connection through om::tryQI< ClientConnection >( task ) and that
 * is a path which either works through a double base or does not. A second stub of the same type
 * plays the driver the ALPN fallback builds, which the pool has to prefer over the task it was
 * handed and has to schedule itself.
 *
 * WHAT MAKES A "NOT YET" ASSERTION HONEST HERE. One case asserts that a request is NOT dispatched
 * to a connection which is still establishing, which is an assertion about something not having
 * happened. It is paired, in the same case, with the same sequence over a replayable request,
 * which IS dispatched and is waited for positively. Without that control a bounded negative wait
 * proves only that the harness was slow
 */

namespace utest
{
    namespace connpool
    {
        /**
         * @brief What a stub connection task's completion is driven through
         *
         * The completion callback is kept OUTSIDE the task rather than bound to it, so nothing
         * has to reference a half-constructed object from an initializer list. A case flips the
         * task from "still connecting" to done through here
         */

        class StubControl FINAL
        {
        public:

            mutable bl::os::mutex                                               lock;
            mutable bl::os::condition_variable                                  cv;

            bl::tasks::CompletionCallback                                       onReady;

            bool                                                                isScheduled;
            bool                                                                isCancelRequested;
            bool                                                                isCompleted;

            /**
             * When set, the task completes with this error as soon as it is scheduled - a
             * connection attempt which fails before it could carry anything
             */

            std::string                                                         failWith;

            StubControl()
                :
                isScheduled( false ),
                isCancelRequested( false ),
                isCompleted( false )
            {
            }

            static void completeLater(
                SAA_in          const bl::tasks::CompletionCallback&            callback,
                SAA_in_opt      const std::exception_ptr&                       eptr
                )
            {
                /*
                 * Never from inside the cancel callback itself - ExternalCompletionTaskIfT calls
                 * it under the task lock and says, in as many words, that completing there
                 * deadlocks
                 */

                bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::GeneralPurpose )
                    -> aioService().post( bl::cpp::bind( callback, eptr ) );
            }

            void onScheduled( SAA_in const bl::tasks::CompletionCallback& callback )
            {
                std::string failure;

                {
                    BL_MUTEX_GUARD( lock );

                    onReady = callback;
                    isScheduled = true;

                    failure = failWith;

                    cv.notify_all();
                }

                if( ! failure.empty() )
                {
                    /*
                     * THE EXCEPTION IS BUILT INTO A NAMED LOCAL FIRST, which is not style. A
                     * BL_EXCEPTION temporary written inside the call would be destroyed at the end
                     * of the full expression - AFTER the post - and boost::exception's error-info
                     * container is reference counted with a plain int, so that destruction would
                     * race the handler's own on another thread. ThreadSanitizer reports it, and it
                     * is right to: the temporary has to die before the post, not after it
                     */

                    const auto eptr = std::make_exception_ptr(
                        BL_EXCEPTION( bl::UnexpectedException(), failure )
                        );

                    completeLater( callback, eptr );
                }
            }

            void onCancelRequested() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bl::tasks::CompletionCallback callback;

                {
                    BL_MUTEX_GUARD( lock );

                    isCancelRequested = true;

                    callback = onReady;

                    cv.notify_all();
                }

                if( callback )
                {
                    /*
                     * Named first, for the reason onScheduled( ) gives
                     */

                    const auto eptr = std::make_exception_ptr(
                        BL_EXCEPTION(
                            bl::UnexpectedException(),
                            std::string( "the stub connection task was cancelled" )
                            )
                        );

                    completeLater( callback, eptr );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Ends the attempt successfully - the connection was established
             */

            void complete()
            {
                bl::tasks::CompletionCallback callback;

                {
                    BL_MUTEX_GUARD( lock );

                    callback = onReady;
                    isCompleted = true;
                }

                if( callback )
                {
                    completeLater( callback, nullptr );
                }
            }

            /**
             * @brief Drops the completion callback, which is what breaks the stub's reference cycle
             *
             * The callback holds a reference to the TASK - it is the task's own markCompleted( ) -
             * and the task holds this control, so a task which is neither completed nor cancelled
             * keeps itself alive through it and is reported as a leaked object reference at exit.
             * The real driver has no such cycle: its completion goes through the task machinery
             * rather than through a callback it handed to something it owns
             */

            void releaseCompletion() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( lock );

                onReady = bl::tasks::CompletionCallback();

                BL_NOEXCEPT_END()
            }

            bool waitForScheduled( SAA_in const long timeoutInMilliseconds = 10000L ) const
            {
                return waitFor( &StubControl::isScheduled, timeoutInMilliseconds );
            }

            bool waitForCancel( SAA_in const long timeoutInMilliseconds = 10000L ) const
            {
                return waitFor( &StubControl::isCancelRequested, timeoutInMilliseconds );
            }

        private:

            bool waitFor(
                SAA_in          bool StubControl::*                             member,
                SAA_in          const long                                      timeoutInMilliseconds
                ) const
            {
                bl::os::mutex_unique_lock guard( lock );

                const auto deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds( timeoutInMilliseconds );

                while( ! ( this ->* member ) )
                {
                    if( bl::os::cv_status::timeout == cv.wait_until( guard, deadline ) )
                    {
                        return this ->* member;
                    }
                }

                return true;
            }
        };

        typedef std::shared_ptr< StubControl >                                  stub_control_ptr_t;

        /**
         * @brief A task which is also a ClientConnection - the shape of the real HTTP/2 driver
         *
         * It is a task which runs until something completes it AND a ClientConnection at the same
         * time, so it has om::Object as a base TWICE and om::tryQI< ClientConnection >( task ) -
         * which is how the pool finds the connection inside a connection task - is a real question
         * rather than a formality.
         *
         * IT IS BUILT ON SimpleTaskBase AND NOT ON tasks::ExternalCompletionTaskT< ClientConnection >,
         * although that template takes a second base exactly for this. Its own handlers form an
         * om::ObjPtrCopyable< this_type > of a this_type which INCLUDES the extra base, and with
         * om::Object reachable twice that conversion is ambiguous - the template does not compile
         * over an om interface at all. SimpleTaskBase's handlers name only themselves, so the
         * ambiguity stops at the seam and this class resolves its own with the second template
         * argument, exactly as Http2ConnectionTaskT does
         */

        template
        <
            typename E = void
        >
        class StubConnectionTaskT :
            public bl::tasks::SimpleTaskBase,
            public bl::httpclient::ClientConnection
        {
        public:

            typedef bl::tasks::SimpleTaskBase                                   base_type;
            typedef StubConnectionTaskT< E >                                    this_type;

            typedef bl::om::ObjPtrCopyable< this_type, bl::tasks::Task >        self_ref_t;

            typedef bl::httpclient::ConnectionState                             ConnectionState;
            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

        private:

            BL_DECLARE_OBJECT_IMPL( StubConnectionTaskT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( bl::httpclient::ClientConnection )
                BL_QITBL_ENTRY_CHAIN_BASE( base_type )
            BL_QITBL_END( bl::tasks::Task )

        protected:

            const stub_control_ptr_t                                            m_control;

            std::atomic< ConnectionState >                                      m_state;
            std::atomic< std::size_t >                                          m_freeSlots;
            std::atomic< std::uint64_t >                                        m_nextHandle;
            std::atomic< std::size_t >                                          m_submitCount;

            const bl::httpclient::NegotiatedProtocol                            m_negotiated;

            StubConnectionTaskT(
                SAA_in          const stub_control_ptr_t&                       control,
                SAA_in          const ConnectionState                           state,
                SAA_in          const std::size_t                               freeSlots,
                SAA_in          const bl::httpclient::HttpProtocol              protocol
                )
                :
                m_control( control ),
                m_state( state ),
                m_freeSlots( freeSlots ),
                m_nextHandle( 0U ),
                m_submitCount( 0U ),
                m_negotiated( bl::httpclient::NegotiatedProtocol::withoutAlpn( protocol ) )
            {
            }

            void markCompleted( SAA_in_opt const std::exception_ptr& eptr ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                notifyReady( eptr );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Starts and stays Running - the attempt ends when the case says it does
             */

            virtual void onExecute() NOEXCEPT OVERRIDE
            {
                BL_TASKS_HANDLER_BEGIN()

                if( isCanceled() )
                {
                    BL_TASKS_HANDLER_CHK_EC( bl::asio::error::operation_aborted );
                }

                m_control -> onScheduled(
                    bl::cpp::bind( &this_type::markCompleted, self_ref_t::acquireRef( this ), _1 )
                    );

                return;

                BL_TASKS_HANDLER_END()
            }

        public:

            virtual void requestCancel() NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                base_type::requestCancel();

                m_control -> onCancelRequested();

                BL_NOEXCEPT_END()
            }

            void setState( SAA_in const ConnectionState state ) NOEXCEPT
            {
                m_state.store( state );

                if( ConnectionState::Ready != state )
                {
                    m_freeSlots.store( 0U );
                }
            }

            void setReady( SAA_in const std::size_t freeSlots ) NOEXCEPT
            {
                m_freeSlots.store( freeSlots );
                m_state.store( ConnectionState::Ready );
            }

            std::size_t submitCount() const NOEXCEPT
            {
                return m_submitCount.load();
            }

            /*************************************************************************************
             * httpclient::ClientConnection
             */

            virtual stream_handle_t submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                ) OVERRIDE
            {
                BL_UNUSED( request );
                BL_UNUSED( eventSink );

                ++m_submitCount;

                return ++m_nextHandle;
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
                return m_freeSlots.load();
            }

            virtual ConnectionState state() const NOEXCEPT OVERRIDE
            {
                return m_state.load();
            }

            virtual auto negotiated() const NOEXCEPT
                -> const bl::httpclient::NegotiatedProtocol& OVERRIDE
            {
                return m_negotiated;
            }
        };

        typedef bl::om::ObjectImpl< StubConnectionTaskT<> >                     StubConnectionTask;

        /**
         * @brief A body source which cannot rewind - what makes a request unreplayable
         */

        template
        <
            typename E = void
        >
        class NoRewindBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( NoRewindBodySourceT, bl::httpclient::BodySource )

        protected:

            NoRewindBodySourceT() NOEXCEPT
            {
            }

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                BL_UNUSED( target );

                bl::httpclient::BodyReadResult result;

                result.size = 0U;
                result.isEndOfStream = true;

                return result;
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

        typedef bl::om::ObjectImpl< NoRewindBodySourceT<> >                     NoRewindBodySource;

        /**
         * @brief What the pool answered, and the rendezvous a case waits on
         *
         * The wait is a condition variable signalled inside the already-locked record( ), not a
         * poll before an assertion - an answer arrives on a thread pool thread and a case which
         * read the count directly would be reading it while it was still being written
         */

        class Answers FINAL
        {
        public:

            struct Record
            {
                std::size_t                                                     index;
                bl::om::ObjPtrCopyable< bl::httpclient::ClientConnection >      connection;
                std::exception_ptr                                              exception;
            };

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;

            std::vector< Record >                                               m_records;

        public:

            void record(
                SAA_in          const std::size_t                               index,
                SAA_in_opt      const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection,
                SAA_in_opt      const std::exception_ptr&                       exception
                )
            {
                BL_MUTEX_GUARD( m_lock );

                Record record;

                record.index = index;
                record.connection = bl::om::ObjPtrCopyable< bl::httpclient::ClientConnection >(
                    connection
                    );
                record.exception = exception;

                m_records.push_back( std::move( record ) );

                m_cv.notify_all();
            }

            std::size_t count() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records.size();
            }

            auto records() const -> std::vector< Record >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            /**
             * @brief Waits for 'expected' answers, and says what it actually got when it gives up
             */

            bool waitFor(
                SAA_in          const std::size_t                               expected,
                SAA_in          const long                                      timeoutInMilliseconds = 10000L
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                const auto deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds( timeoutInMilliseconds );

                while( m_records.size() < expected )
                {
                    if( bl::os::cv_status::timeout == m_cv.wait_until( guard, deadline ) )
                    {
                        break;
                    }
                }

                if( m_records.size() < expected )
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "The pool recorded "
                            << m_records.size()
                            << " answers where the case expects "
                            << expected
                        );

                    return false;
                }

                return true;
            }
        };

        /**
         * @brief The connection factory a case drives, and the record of what the pool asked it
         */

        class StubFactory FINAL
        {
        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;

            std::vector< bl::om::ObjPtrCopyable< StubConnectionTask > >         m_tasks;
            std::vector< stub_control_ptr_t >                                   m_controls;
            std::vector< bl::om::ObjPtrCopyable< StubConnectionTask > >         m_drivers;
            std::vector< stub_control_ptr_t >                                   m_driverControls;

            std::size_t                                                         m_calls;

        public:

            /*
             * What the next connection the factory builds looks like
             */

            bl::httpclient::ConnectionState                                     initialState;
            std::size_t                                                         initialFreeSlots;
            bl::httpclient::HttpProtocol                                        protocol;
            std::string                                                         failWith;

            /*
             * When set, the task the factory returns reports itself Closed and the connection is
             * a SECOND object handed over through the accessor - the ALPN fallback of design 5.5
             */

            bool                                                                isFallback;

            ~StubFactory() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                for( const auto& control : m_controls )
                {
                    control -> releaseCompletion();
                }

                for( const auto& control : m_driverControls )
                {
                    control -> releaseCompletion();
                }

                BL_NOEXCEPT_END()
            }

            StubFactory()
                :
                m_calls( 0U ),
                initialState( bl::httpclient::ConnectionState::Connecting ),
                initialFreeSlots( 0U ),
                protocol( bl::httpclient::HttpProtocol::Http2 ),
                isFallback( false )
            {
            }

            std::size_t calls() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_calls;
            }

            bool waitForCalls(
                SAA_in          const std::size_t                               expected,
                SAA_in          const long                                      timeoutInMilliseconds = 10000L
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                const auto deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds( timeoutInMilliseconds );

                while( m_calls < expected )
                {
                    if( bl::os::cv_status::timeout == m_cv.wait_until( guard, deadline ) )
                    {
                        break;
                    }
                }

                return m_calls >= expected;
            }

            auto taskAt( SAA_in const std::size_t index ) const
                -> bl::om::ObjPtr< StubConnectionTask >
            {
                BL_MUTEX_GUARD( m_lock );

                UTF_REQUIRE( index < m_tasks.size() );

                return bl::om::copy( m_tasks[ index ] );
            }

            auto controlAt( SAA_in const std::size_t index ) const -> stub_control_ptr_t
            {
                BL_MUTEX_GUARD( m_lock );

                UTF_REQUIRE( index < m_controls.size() );

                return m_controls[ index ];
            }

            auto driverAt( SAA_in const std::size_t index ) const
                -> bl::om::ObjPtr< StubConnectionTask >
            {
                BL_MUTEX_GUARD( m_lock );

                UTF_REQUIRE( index < m_drivers.size() );

                return bl::om::copy( m_drivers[ index ] );
            }

            auto driverControlAt( SAA_in const std::size_t index ) const -> stub_control_ptr_t
            {
                BL_MUTEX_GUARD( m_lock );

                UTF_REQUIRE( index < m_driverControls.size() );

                return m_driverControls[ index ];
            }

            /**
             * @brief The connection_factory_t itself
             */

            auto operator()(
                SAA_in          const bl::httpclient::ConnectionKey&            key,
                SAA_in          const bl::httpclient::ConnectionPoolPolicy&     policy
                )
                -> bl::httpclient::ConnectionAttempt
            {
                BL_UNUSED( key );
                BL_UNUSED( policy );

                const auto control = std::make_shared< StubControl >();

                bl::om::ObjPtr< StubConnectionTask > driver;
                stub_control_ptr_t driverControl;

                control -> failWith = failWith;

                auto task = StubConnectionTask::createInstance(
                    control,
                    isFallback ? bl::httpclient::ConnectionState::Closed : initialState,
                    isFallback ? 0U : initialFreeSlots,
                    protocol
                    );

                if( isFallback )
                {
                    driverControl = std::make_shared< StubControl >();

                    driver = StubConnectionTask::createInstance(
                        driverControl,
                        initialState,
                        initialFreeSlots,
                        bl::httpclient::HttpProtocol::Http11
                        );
                }

                bl::httpclient::ConnectionAttempt attempt;

                attempt.task = bl::om::ObjPtrCopyable< bl::tasks::Task >(
                    bl::om::qi< bl::tasks::Task >( task )
                    );

                if( driver )
                {
                    const auto driverCopy =
                        bl::om::ObjPtrCopyable< StubConnectionTask >( driver );

                    attempt.driver = bl::cpp::bind( &StubFactory::driverOf, driverCopy );
                }

                {
                    BL_MUTEX_GUARD( m_lock );

                    m_tasks.push_back( bl::om::ObjPtrCopyable< StubConnectionTask >( task ) );
                    m_controls.push_back( control );

                    if( driver )
                    {
                        m_drivers.push_back(
                            bl::om::ObjPtrCopyable< StubConnectionTask >( driver )
                            );

                        m_driverControls.push_back( driverControl );
                    }

                    ++m_calls;

                    m_cv.notify_all();
                }

                return attempt;
            }

            static auto driverOf( SAA_in const bl::om::ObjPtrCopyable< StubConnectionTask >& driver )
                -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
            {
                return bl::om::qi< bl::httpclient::ClientConnection >( driver );
            }
        };

        typedef bl::om::ObjectImpl< bl::httpclient::ConnectionPoolImplT<> >     pool_impl_t;

        /**
         * @brief Disposes the pool on the way out of a case, however the case leaves
         *
         * A UTF_REQUIRE which fails leaves by throwing, so a dispose( ) written at the end of the
         * case is not reached - and a pool with a request still queued keeps itself alive through
         * its own maintenance timer, which means the failing case's pool is still there when the
         * process tears its thread pools down. One failure would then be followed by an abort with
         * an unrelated message, which is exactly the kind of second failure that hides the first
         */

        class PoolGuard FINAL
        {
        private:

            const bl::om::ObjPtr< pool_impl_t >                                 m_pool;

        public:

            explicit PoolGuard( SAA_in const bl::om::ObjPtr< pool_impl_t >& pool )
                :
                m_pool( bl::om::copy( pool ) )
            {
            }

            ~PoolGuard() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_pool -> dispose();

                BL_NOEXCEPT_END()
            }
        };

        inline auto makeKey( SAA_in const std::string& host = "example.com" )
            -> bl::httpclient::ConnectionKey
        {
            bl::httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = host;
            key.port = 443U;

            return key;
        }

        inline auto makeRequest( SAA_in const bool isReplayable = true )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.url( bl::net::Uri::parse( "https://example.com/items" ) );

            if( ! isReplayable )
            {
                request.bodySource(
                    bl::om::ObjPtrCopyable< bl::httpclient::BodySource >(
                        NoRewindBodySource::createInstance< bl::httpclient::BodySource >()
                        )
                    );
            }

            return request;
        }

        inline void acquireInto(
            SAA_in          const bl::om::ObjPtr< pool_impl_t >&                pool,
            SAA_in          const bl::httpclient::ConnectionKey&                key,
            SAA_in          const bl::httpclient::ClientRequest&                request,
            SAA_in          Answers*                                            answers,
            SAA_in          const std::size_t                                   index
            )
        {
            pool -> acquire(
                key,
                request,
                [ answers, index ](
                    SAA_in_opt      const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection,
                    SAA_in_opt      const std::exception_ptr&                   exception
                    ) -> void
                {
                    answers -> record( index, connection, exception );
                }
                );
        }

        /**
         * @brief The error code an exception carries, or a default constructed one
         */

        inline auto errorCodeOf( SAA_in const std::exception_ptr& eptr ) -> bl::eh::error_code
        {
            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::eh::exception& e )
            {
                const auto* const code = bl::eh::get_error_info< bl::eh::errinfo_error_code >( e );

                if( code )
                {
                    return *code;
                }
            }
            catch( std::exception& )
            {
            }

            return bl::eh::error_code();
        }

        inline bool isTimeoutException( SAA_in const std::exception_ptr& eptr )
        {
            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::TimeoutException& )
            {
                return true;
            }
            catch( std::exception& )
            {
            }

            return false;
        }

    } // connpool

} // utest

/************************************************************************
 * Queueing behind the Connecting placeholder (design 5.4)
 */

UTF_AUTO_TEST_CASE( H2Pool_QueueBehindPlaceholderTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    httpclient::ConnectionPoolPolicy policy;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        acquireInto( pool, key, makeRequest(), &answers, i );
    }

    UTF_REQUIRE( factory.waitForCalls( 1U ) );

    /*
     * ONE connection for five concurrent requests - the placeholder was inserted under the pool
     * lock before it was released, so the other four found it instead of each opening one
     */

    UTF_REQUIRE_EQUAL( factory.calls(), 1U );

    /*
     * And exactly one request went out while it was still connecting: design 5.1's first request
     * rides the preface, and nothing else can, because until the peer's SETTINGS arrive nothing
     * about its capacity is known
     */

    UTF_REQUIRE( answers.waitFor( 1U ) );
    UTF_REQUIRE_EQUAL( answers.count(), 1U );

    const auto task = factory.taskAt( 0U );

    task -> setReady( 10U );

    UTF_REQUIRE( answers.waitFor( 5U ) );

    UTF_REQUIRE_EQUAL( factory.calls(), 1U );
    UTF_REQUIRE_EQUAL( answers.count(), 5U );

    const auto records = answers.records();

    for( const auto& record : records )
    {
        UTF_REQUIRE( nullptr == record.exception );
        UTF_REQUIRE( nullptr != record.connection );
    }

    const auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
    UTF_REQUIRE_EQUAL( stats.dispatched.value(), 5U );

    pool -> dispose();
}

/************************************************************************
 * The slot limit is the POOL's count and not the driver's (design 5.4)
 */

UTF_AUTO_TEST_CASE( H2Pool_SlotLimitingTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots = 2U;

    httpclient::ConnectionPoolPolicy policy;

    policy.maxStreamsPerConnection = 8U;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        acquireInto( pool, key, makeRequest(), &answers, i );
    }

    UTF_REQUIRE( answers.waitFor( 2U ) );

    /*
     * The stub's freeStreamSlots( ) never changes - it says two for ever. What holds the third
     * request back is the pool's own count of what it has handed out, which is the only count
     * that is right while a dispatch is still on its way to a driver's strand
     */

    UTF_REQUIRE_EQUAL( answers.count(), 2U );
    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 3U );

    const auto connection = om::qi< httpclient::ClientConnection >( factory.taskAt( 0U ) );

    UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 2U );

    pool -> releaseStream( connection, 1U, httpclient::RequestOutcome::Completed );

    UTF_REQUIRE( answers.waitFor( 3U ) );

    pool -> releaseStream( connection, 2U, httpclient::RequestOutcome::Completed );

    UTF_REQUIRE( answers.waitFor( 4U ) );

    /*
     * FIFO - design 5.4 - asserted on WHICH requests were served and when, not on the order the
     * answers arrived in. An answer is POSTED (the contract says it must be), so two answers
     * posted together are delivered by whichever thread pool thread takes them first and their
     * order says nothing. What FIFO means here is that the first two requests were the two which
     * got the two slots, and that each slot given back went to the oldest request still waiting
     */

    const auto records = answers.records();

    UTF_REQUIRE_EQUAL( records.size(), 4U );

    UTF_REQUIRE(
        ( 0U == records[ 0 ].index && 1U == records[ 1 ].index ) ||
        ( 1U == records[ 0 ].index && 0U == records[ 1 ].index )
        );

    UTF_REQUIRE_EQUAL( records[ 2 ].index, 2U );
    UTF_REQUIRE_EQUAL( records[ 3 ].index, 3U );

    pool -> dispose();
}

/************************************************************************
 * A connection which is still establishing takes a replayable request and nothing else
 */

UTF_AUTO_TEST_CASE( H2Pool_ConnectingTakesOnlyReplayableTests )
{
    using namespace bl;
    using namespace utest::connpool;

    httpclient::ConnectionPoolPolicy policy;

    /*
     * THE CONTROL FIRST: the same sequence with a replayable request, which IS given the
     * connection while it is still establishing. Without it the negative wait below would prove
     * only that the harness is slow
     */

    {
        StubFactory factory;
        Answers answers;

        const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

        const PoolGuard guard( pool );

        acquireInto( pool, makeKey(), makeRequest( true /* isReplayable */ ), &answers, 0U );

        UTF_REQUIRE( answers.waitFor( 1U ) );

        UTF_REQUIRE( httpclient::ConnectionState::Connecting == factory.taskAt( 0U ) -> state() );

        pool -> dispose();
    }

    {
        StubFactory factory;
        Answers answers;

        const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

        const PoolGuard guard( pool );

        acquireInto( pool, makeKey(), makeRequest( false /* isReplayable */ ), &answers, 0U );

        UTF_REQUIRE( factory.waitForCalls( 1U ) );

        /*
         * It waits instead: a request which cannot be replayed must not be put on a connection
         * which may yet turn out to speak http/1.1 and bounce it
         */

        UTF_REQUIRE( ! answers.waitFor( 1U, 500L /* timeoutInMilliseconds */ ) );

        factory.taskAt( 0U ) -> setReady( 4U );

        UTF_REQUIRE( answers.waitFor( 1U ) );

        pool -> dispose();
    }
}

/************************************************************************
 * The retry matrix (design 5.4, D6, 4.6)
 */

UTF_AUTO_TEST_CASE( H2Pool_RetryMatrixTests )
{
    using namespace bl;
    using namespace utest::connpool;

    httpclient::ConnectionPoolPolicy policy;

    const auto replayable = makeRequest( true /* isReplayable */ );
    const auto unreplayable = makeRequest( false /* isReplayable */ );

    httpclient::RetryContext context;

    /*
     * Provably unprocessed AND replayable - the only combination design 5.4 replays
     */

    context.isRetryable = true;

    UTF_REQUIRE( httpclient::chkRequestMayBeReplayed( replayable, context, policy ) );

    UTF_REQUIRE( ! httpclient::chkRequestMayBeReplayed( unreplayable, context, policy ) );

    /*
     * A failure which proves nothing is not replayed, however replayable the request is
     */

    context.isRetryable = false;
    context.isConnectionLost = true;

    UTF_REQUIRE( ! httpclient::chkRequestMayBeReplayed( replayable, context, policy ) );

    /*
     * ... unless the separate knob of design 5.4 is on, and then only for an idempotent method
     */

    auto lenient = policy;

    lenient.retryIdempotentOnConnectionLoss = true;

    UTF_REQUIRE( httpclient::chkRequestMayBeReplayed( replayable, context, lenient ) );

    auto posted = makeRequest( true /* isReplayable */ );

    posted.method( "POST" );

    UTF_REQUIRE( ! httpclient::chkRequestMayBeReplayed( posted, context, lenient ) );

    auto deleted = makeRequest( true /* isReplayable */ );

    deleted.method( "DELETE" );

    UTF_REQUIRE( httpclient::chkRequestMayBeReplayed( deleted, context, lenient ) );

    /*
     * The bound of design 4.6 - three retries, then fail with the last error
     */

    context.isRetryable = true;
    context.isConnectionLost = false;

    context.attempts = policy.maxRetriesPerRequest;

    UTF_REQUIRE( httpclient::chkRequestMayBeReplayed( replayable, context, policy ) );

    context.attempts = policy.maxRetriesPerRequest + 1U;

    UTF_REQUIRE( ! httpclient::chkRequestMayBeReplayed( replayable, context, policy ) );
}

/************************************************************************
 * A connection attempt which fails takes the requests queued behind it with it - and they are
 * retried, bounded, and fail with the LAST error
 */

UTF_AUTO_TEST_CASE( H2Pool_FailedEstablishmentRetriesQueuedRequestsTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    factory.failWith = "the stub connection attempt failed";

    httpclient::ConnectionPoolPolicy policy;

    policy.maxRetriesPerRequest = 2U;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    /*
     * Unreplayable, so that neither of them rides the preface and both stay the pool's to answer
     */

    acquireInto( pool, key, makeRequest( false /* isReplayable */ ), &answers, 0U );
    acquireInto( pool, key, makeRequest( false /* isReplayable */ ), &answers, 1U );

    UTF_REQUIRE( answers.waitFor( 2U ) );

    /*
     * One attempt, then two retries - and then the requests fail rather than the pool trying for
     * ever
     */

    UTF_REQUIRE_EQUAL( factory.calls(), 3U );

    const auto records = answers.records();

    for( const auto& record : records )
    {
        UTF_REQUIRE( nullptr != record.exception );
        UTF_REQUIRE( nullptr == record.connection );
    }

    const auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 3U );
    UTF_REQUIRE_EQUAL( stats.failures.value(), 2U );
    UTF_REQUIRE_EQUAL( stats.dispatched.value(), 0U );

    pool -> dispose();
}

/************************************************************************
 * THE ESTABLISHMENT BOUND - design 5.7 as this slice amends it
 */

UTF_AUTO_TEST_CASE( H2Pool_EstablishmentBoundTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    httpclient::ConnectionPoolPolicy policy;

    /*
     * The stub connection never becomes usable and its task never ends by itself - which is what
     * a black-holed origin looks like from here, without the 134 seconds per address it takes to
     * produce one
     */

    policy.establishmentTimeout = time::milliseconds( 300 );
    policy.maxRetriesPerRequest = 0U;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    acquireInto( pool, makeKey(), makeRequest( false /* isReplayable */ ), &answers, 0U );

    UTF_REQUIRE( answers.waitFor( 1U ) );

    const auto records = answers.records();

    UTF_REQUIRE_EQUAL( records.size(), 1U );
    UTF_REQUIRE( nullptr != records[ 0 ].exception );
    UTF_REQUIRE( isTimeoutException( records[ 0 ].exception ) );

    /*
     * And the connection it was waiting for was abandoned rather than left running - which is the
     * half of the bound that matters for the next request, since a placeholder nobody cancelled
     * would keep the key occupied
     */

    UTF_REQUIRE( factory.controlAt( 0U ) -> waitForCancel() );

    const auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.establishmentTimeouts.value(), 1U );

    pool -> dispose();
}

/************************************************************************
 * GOAWAY draining, including the double GOAWAY servers commonly send
 */

UTF_AUTO_TEST_CASE( H2Pool_GoAwayDrainingTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots = 4U;

    httpclient::ConnectionPoolPolicy policy;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    acquireInto( pool, key, makeRequest(), &answers, 0U );
    acquireInto( pool, key, makeRequest(), &answers, 1U );

    UTF_REQUIRE( answers.waitFor( 2U ) );

    const auto first = factory.taskAt( 0U );
    const auto firstConnection = om::qi< httpclient::ClientConnection >( first );

    /*
     * The double GOAWAY of design 5.4 - first with 2^31-1 and then with the real last-stream-id.
     * The driver publishes Draining monotonically, so the second one changes nothing, and the
     * pool must not count the connection as retired twice for it
     */

    first -> setState( httpclient::ConnectionState::Draining );
    first -> setState( httpclient::ConnectionState::Draining );

    acquireInto( pool, key, makeRequest(), &answers, 2U );

    UTF_REQUIRE( answers.waitFor( 3U ) );

    /*
     * Nothing new was dispatched to the draining connection - a second one was opened for it, and
     * the two streams still on the first one were left to finish
     */

    UTF_REQUIRE_EQUAL( factory.calls(), 2U );

    const auto records = answers.records();

    UTF_REQUIRE_EQUAL( records.size(), 3U );
    UTF_REQUIRE( records[ 2 ].connection.get() != firstConnection.get() );

    UTF_REQUIRE_EQUAL( pool -> slotsInUse( firstConnection ), 2U );

    auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.connectionsRetired.value(), 1U );

    /*
     * And once its last stream is given back, the pool forgets it
     */

    pool -> releaseStream( firstConnection, 1U, httpclient::RequestOutcome::Completed );
    pool -> releaseStream( firstConnection, 2U, httpclient::RequestOutcome::Completed );

    UTF_REQUIRE_EQUAL( pool -> connectionCount(), 1U );

    stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.connectionsRetired.value(), 1U );

    pool -> dispose();
}

/************************************************************************
 * The ALPN fallback - the driver is what the pool hands out, and the pool is what schedules it
 */

UTF_AUTO_TEST_CASE( H2Pool_FallbackDriverIsPreferredTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    /*
     * The task reports itself Closed - which is exactly what an h2 connection task does once it
     * has handed the connected stream to the HTTP/1.1 driver - and the driver beside it is the
     * live connection
     */

    factory.isFallback = true;
    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots = 1U;

    httpclient::ConnectionPoolPolicy policy;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    acquireInto( pool, makeKey(), makeRequest(), &answers, 0U );

    UTF_REQUIRE( answers.waitFor( 1U ) );

    const auto records = answers.records();

    const auto taskConnection =
        om::qi< httpclient::ClientConnection >( factory.taskAt( 0U ) );

    const auto driverConnection =
        om::qi< httpclient::ClientConnection >( factory.driverAt( 0U ) );

    UTF_REQUIRE( nullptr == records[ 0 ].exception );
    UTF_REQUIRE( records[ 0 ].connection.get() == driverConnection.get() );
    UTF_REQUIRE( records[ 0 ].connection.get() != taskConnection.get() );

    /*
     * And the pool scheduled it. The factory returns the driver CREATED and not scheduled,
     * because scheduling it from inside the factory would be a connection strand taking an
     * execution queue lock - design 5.2 rule L2
     */

    UTF_REQUIRE( factory.driverControlAt( 0U ) -> waitForScheduled() );

    pool -> dispose();
}

/************************************************************************
 * Disposal (design 5.4)
 */

UTF_AUTO_TEST_CASE( H2Pool_DisposalTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    httpclient::ConnectionPoolPolicy policy;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 3U; ++i )
    {
        acquireInto( pool, key, makeRequest( false /* isReplayable */ ), &answers, i );
    }

    UTF_REQUIRE( factory.waitForCalls( 1U ) );

    pool -> dispose();

    UTF_REQUIRE( answers.waitFor( 3U ) );

    const auto records = answers.records();

    for( const auto& record : records )
    {
        UTF_REQUIRE( nullptr != record.exception );
        UTF_REQUIRE( nullptr == record.connection );

        UTF_REQUIRE( eh::error_code( asio::error::operation_aborted ) == errorCodeOf( record.exception ) );
    }

    UTF_REQUIRE( factory.controlAt( 0U ) -> waitForCancel() );

    /*
     * And a request which arrives afterwards is answered rather than queued for ever
     */

    acquireInto( pool, key, makeRequest(), &answers, 99U );

    UTF_REQUIRE( answers.waitFor( 4U ) );

    UTF_REQUIRE_EQUAL( factory.calls(), 1U );

    /*
     * Disposing twice is not a second disposal
     */

    pool -> dispose();
}

/************************************************************************
 * The two numbers this slice chose, and the D21 refusal
 */

UTF_AUTO_TEST_CASE( H2Pool_PolicyDefaultsTests )
{
    using namespace bl;
    using namespace utest::connpool;

    const httpclient::ConnectionPoolPolicy policy;

    /*
     * THE DRAINING RESERVE. 1024, and the argument for it is that it covers what the pool can
     * have committed to one connection but not yet opened - which is bounded by the dispatch
     * ceiling. So the two are related and this is what says so: a reserve below the ceiling is
     * not a margin at all
     */

    UTF_REQUIRE_EQUAL( policy.drainingReserve.value(), 1024U );

    UTF_REQUIRE( policy.drainingReserve.value() >= policy.maxStreamsPerConnection.value() );

    UTF_REQUIRE_EQUAL( policy.maxStreamsPerConnection.value(), 256U );

    /*
     * THE ESTABLISHMENT BOUND - 120 s, and the number it is chosen against is the 134 s a single
     * black-holed address costs, not two of design 5.7's 60 s connect deadlines. It is BELOW that
     * one, deliberately: no overall number clears a dead address without giving up on bounding
     * anything, and a caller waits this bound once per retry. The pool's long note says the rest
     */

    UTF_REQUIRE_EQUAL( policy.establishmentTimeout.total_seconds(), 120L );

    UTF_REQUIRE( policy.establishmentTimeout.total_seconds() < 134L );

    /*
     * THE SETTLE WINDOW - one second, which is how long the pool waits for a peer to say what it
     * allows before taking the driver's assumption of it as the answer
     */

    UTF_REQUIRE_EQUAL( policy.settingsSettleTimeout.total_milliseconds(), 1000L );

    UTF_REQUIRE_EQUAL(
        static_cast< std::size_t >(
            httpclient::ConnectionPoolPolicy::UNCONFIRMED_MAX_CONCURRENT_STREAMS
            ),
        1U
        );
    UTF_REQUIRE_EQUAL( policy.requestTimeout.total_seconds(), 30L * 60L );
    UTF_REQUIRE_EQUAL( policy.idleTimeout.total_seconds(), 300L );

    /*
     * Design 5.4's connections per key, and 4.6's retry budget
     */

    UTF_REQUIRE_EQUAL( policy.maxConnectionsPerKey.value(), 1U );
    UTF_REQUIRE_EQUAL( policy.maxConnectionsPerKeyHttp11.value(), 6U );
    UTF_REQUIRE_EQUAL( policy.maxRetriesPerRequest.value(), 3U );

    UTF_REQUIRE( ! policy.retryIdempotentOnConnectionLoss.value() );

    /*
     * D21 - coalescing is designed and default off, and asking for it is REFUSED rather than
     * quietly ignored: a caller who believes they have it would be trusting a check which is not
     * being made
     */

    UTF_REQUIRE( ! policy.enableCoalescing.value() );

    StubFactory factory;

    auto coalescing = policy;

    coalescing.enableCoalescing = true;

    UTF_REQUIRE_THROW(
        pool_impl_t::createInstance( cpp::ref( factory ), coalescing ),
        NotSupportedException
        );
}

/************************************************************************
 * Design 8.3's concurrency - and the case TSan is pointed at
 */

UTF_AUTO_TEST_CASE( H2Pool_ConcurrentAcquireAndReleaseTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots = 8U;

    httpclient::ConnectionPoolPolicy policy;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    const std::size_t threadCount = 4U;
    const std::size_t perThread = 25U;

    std::vector< os::thread > threads;

    std::atomic< std::size_t > released( 0U );

    for( std::size_t t = 0U; t < threadCount; ++t )
    {
        threads.push_back(
            os::thread(
                [ &pool, &key, &answers, &released, t ]() -> void
                {
                    for( std::size_t i = 0U; i < perThread; ++i )
                    {
                        const auto index = t * perThread + i;

                        pool -> acquire(
                            key,
                            makeRequest(),
                            [ &pool, &answers, &released, index ](
                                SAA_in_opt      const om::ObjPtr< httpclient::ClientConnection >& connection,
                                SAA_in_opt      const std::exception_ptr& exception
                                ) -> void
                            {
                                if( connection )
                                {
                                    /*
                                     * Straight back, from the thread the answer arrived on - which
                                     * is what a request task does from its own drain
                                     */

                                    pool -> releaseStream(
                                        connection,
                                        index + 1U,
                                        httpclient::RequestOutcome::Completed
                                        );

                                    ++released;
                                }

                                /*
                                 * RECORDED LAST, on purpose: the case waits on the record, so
                                 * everything this callback does has to have happened before it -
                                 * otherwise the wait returns while the counter beside it is still
                                 * being written, which is a missing rendezvous rather than a flake
                                 */

                                answers.record( index, connection, exception );
                            }
                            );
                    }
                }
                )
            );
    }

    for( auto& thread : threads )
    {
        thread.join();
    }

    const auto expected = threadCount * perThread;

    UTF_REQUIRE( answers.waitFor( expected, 30000L /* timeoutInMilliseconds */ ) );

    const auto records = answers.records();

    UTF_REQUIRE_EQUAL( records.size(), expected );

    for( const auto& record : records )
    {
        UTF_REQUIRE( nullptr == record.exception );
        UTF_REQUIRE( nullptr != record.connection );
    }

    UTF_REQUIRE_EQUAL( released.load(), expected );

    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 0U );

    const auto stats = pool -> stats();

    UTF_REQUIRE_EQUAL( stats.dispatched.value(), expected );
    UTF_REQUIRE_EQUAL( stats.released.value(), expected );

    pool -> dispose();
}

/************************************************************************
 * The peer's limit is not the driver's assumption of it (L5 finding 5)
 */

UTF_AUTO_TEST_CASE( H2Pool_AssumedLimitIsNotDispatchedAgainstTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    /*
     * A connection which is Ready and reports exactly the assumed number is what the h2 driver
     * looks like between its Ready - published BEFORE the preface is written, so that the first
     * request's HEADERS can join it - and the peer's SETTINGS one round trip later. The reading
     * is the driver's assumption and says nothing whatever about the peer
     */

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots =
        httpclient::ConnectionPoolPolicy::ASSUMED_MAX_CONCURRENT_STREAMS;

    httpclient::ConnectionPoolPolicy policy;

    /*
     * Long enough that the settle window plays no part in this case - what is being pinned here
     * is what the pool does while it does not know, and the window is the other case
     */

    policy.settingsSettleTimeout = time::seconds( 60 );

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        acquireInto( pool, key, makeRequest(), &answers, i );
    }

    UTF_REQUIRE( answers.waitFor( 1U ) );

    /*
     * ONE, and the negative wait is what says it is one rather than one-so-far. Dispatching the
     * other four would be a burst of five against a peer which has not said it can take two -
     * and a request refused for being over the peer's limit is a LOST request here, because the
     * dispatched half of the retry does not exist
     */

    UTF_REQUIRE( ! answers.waitFor( 2U, 500L /* timeoutInMilliseconds */ ) );

    UTF_REQUIRE_EQUAL( answers.count(), 1U );
    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 4U );

    const auto connection = om::qi< httpclient::ClientConnection >( factory.taskAt( 0U ) );

    UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 1U );
    UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 1U );

    /*
     * Now the peer's SETTINGS arrive and say three, of which the one stream already dispatched
     * holds one. That reading - two free with one out - could not have come from the assumption,
     * which cannot report below ninety-nine while one stream is out, so the pool knows it is the
     * peer's and dispatches two more and not four
     */

    factory.taskAt( 0U ) -> setReady( 2U );

    UTF_REQUIRE( answers.waitFor( 3U ) );

    UTF_REQUIRE( ! answers.waitFor( 4U, 500L /* timeoutInMilliseconds */ ) );

    UTF_REQUIRE_EQUAL( answers.count(), 3U );
    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 2U );

    UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 3U );
    UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 3U );

    pool -> dispose();
}

/************************************************************************
 * A response which came back is proof the peer has spoken (L5 finding 5)
 */

UTF_AUTO_TEST_CASE( H2Pool_PeerLimitLearnedFromACompletedResponseTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    /*
     * This stub reports the assumed number for ever, which is the one peer no reading can tell
     * apart from a driver which has not heard from it: a peer whose own limit IS the assumption
     */

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots =
        httpclient::ConnectionPoolPolicy::ASSUMED_MAX_CONCURRENT_STREAMS;

    httpclient::ConnectionPoolPolicy policy;

    policy.settingsSettleTimeout = time::seconds( 60 );
    policy.maxStreamsPerConnection = 4U;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        acquireInto( pool, key, makeRequest(), &answers, i );
    }

    UTF_REQUIRE( answers.waitFor( 1U ) );

    UTF_REQUIRE( ! answers.waitFor( 2U, 500L /* timeoutInMilliseconds */ ) );

    const auto connection = om::qi< httpclient::ClientConnection >( factory.taskAt( 0U ) );

    UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 1U );

    /*
     * A response came back in full. A peer's SETTINGS is the first frame it sends and a driver
     * applies frames in the order they arrive, so the peer has certainly spoken by now and every
     * reading from here on is the peer's - the pool's own ceiling is what limits it after that
     */

    pool -> releaseStream( connection, 1U, httpclient::RequestOutcome::Completed );

    UTF_REQUIRE( answers.waitFor( 5U ) );

    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 0U );

    UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 4U );
    UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 4U );

    pool -> dispose();
}

/************************************************************************
 * The settle window, for the peer which never distinguishes itself
 */

UTF_AUTO_TEST_CASE( H2Pool_AssumptionIsTakenAfterTheSettleWindowTests )
{
    using namespace bl;
    using namespace utest::connpool;

    StubFactory factory;
    Answers answers;

    factory.initialState = httpclient::ConnectionState::Ready;
    factory.initialFreeSlots =
        httpclient::ConnectionPoolPolicy::ASSUMED_MAX_CONCURRENT_STREAMS;

    httpclient::ConnectionPoolPolicy policy;

    /*
     * The same stub as the case above, which never says anything the assumption could not have
     * said, and no response to prove the peer spoke. Without the window such a connection would
     * carry one request at a time for as long as its first response takes - which for a download
     * or an event stream is not a round trip but minutes
     */

    policy.settingsSettleTimeout = time::milliseconds( 200 );
    policy.maxStreamsPerConnection = 3U;

    const auto pool = pool_impl_t::createInstance( cpp::ref( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        acquireInto( pool, key, makeRequest(), &answers, i );
    }

    /*
     * The window expires and the pool takes the reading as the peer's, which is what this code
     * did unconditionally before - so what the window costs is the round trip and nothing else
     */

    UTF_REQUIRE( answers.waitFor( 3U ) );

    UTF_REQUIRE( ! answers.waitFor( 4U, 500L /* timeoutInMilliseconds */ ) );

    UTF_REQUIRE_EQUAL( answers.count(), 3U );
    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 2U );

    const auto connection = om::qi< httpclient::ClientConnection >( factory.taskAt( 0U ) );

    UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 3U );

    pool -> dispose();
}

#endif /* __UTEST_TESTCONNECTIONPOOL_H_ */
