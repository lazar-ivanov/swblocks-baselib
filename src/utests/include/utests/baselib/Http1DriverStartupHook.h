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

#ifndef __UTEST_HTTP1DRIVERSTARTUPHOOK_H_
#define __UTEST_HTTP1DRIVERSTARTUPHOOK_H_

#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Http1DriverTlsTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * D2 - THE HOOK IN THE FIRST READ'S START, and the two exchanges the cases run over it
 *
 * IT LIVES HERE AND NOT IN A MODULE DIRECTORY because two CS-1 modules need it: D2's four cases
 * measured 39.9MB a64 clang debug in utf_baselib_httpclient11 alone, over the 36MB the orchestrator
 * set for it, so the cleartext pair stays there and the TLS pair is in utf_baselib_httpclient12 - and
 * a test header may never be included across module directories (src/utests/AGENTS.md)
 *
 * WHAT D2 IS. The HTTP/1.1 driver used to publish m_started and then arm its first read INLINE in
 * scheduleTask( ), on the scheduling thread - and over TLS asio::ssl::stream::async_read_some( ) runs
 * its first engine step on the thread which calls it (boost/asio/ssl/detail/io.hpp, async_io( )). A
 * submit( ) in that window posted onStartRequest( ) to a free strand, which started the request's
 * write on the same engine: two threads in one OpenSSL connection. And a cancel which landed before
 * the start was erased by the run's reset. D2 starts the connection in one accounted strand handler
 * instead; the design is notes/plans/issues/astra2-cs1-d2-startup-handler-design.md, and its section 9
 * is what this file builds.
 *
 * THE SEAM IS A STREAM POLICY, for the reason the H01 and A4 seams are: the driver's handlers are
 * non-virtual and bound by cpp::bind, so the stream is the only lever. ReadStartHookPolicyT hides
 * getStream( ) exactly as utf_baselib_httpclient7's HeldWritePolicyT does, over either stranded
 * policy, and hands the driver a stream which forwards everything and adds two things: the FIRST read
 * start after the hook is armed is HELD, and every write start is RECORDED. The TLS finish
 * continuation's shutdown calls the base policy's own getStream( ), which a hiding definition cannot
 * reach, so it never goes through the hook.
 *
 * THE HOLD COMES AFTER THE REAL INITIATOR, deliberately: on the unfixed tree the read's own engine
 * step has finished before the hold, so the red run observes the ordering violation without itself
 * driving two TLS engine steps at once.
 *
 * AND IT SAYS WHICH THREAD IT IS ON, which is what lets a case release it deterministically - see
 * runBarrierExchange( ). The socket's executor is the strand the establisher built it on
 * (make_strand( io_context& ) yields strand< io_context::executor_type >, which is exactly strand_t),
 * so target< strand_t >( ) cannot miss; if it ever returned null the hook records 'no-strand' and
 * does not hold, so the case fails with a name instead of taking the wrong branch.
 *
 * The control block is process global, like the H01 seam's and for its reason: the driver is built by
 * a factory, so no per-connection control can reach it, and this module runs one connection at a time.
 */

namespace utest
{
    namespace http1startup
    {
        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,

            /**
             * @brief The bound on the hold - it turns a broken arrangement into a reported failure
             * and decides nothing: a correct case releases the hold long before it
             */

            HOLD_BOUND_IN_MILLISECONDS          = 30000U,

            /**
             * @brief How long the cancel-before-start case gives the task to end on its own
             *
             * A4's TASK_END_TIMEOUT_IN_MILLISECONDS (utf_baselib_httpclient7,
             * TestHttp1DriverScheduleThrow.h), repeated because a test header is never included
             * across modules. It bounds an event which on the unfixed tree cannot happen at all and
             * on the fixed one is due at once
             */

            TASK_END_TIMEOUT_IN_MILLISECONDS    = 30000U,

            /**
             * @brief The idle lifetime the cancel-before-start case gives its driver - past the bound,
             * so that on the unfixed tree the idle close is not what ends the task within it
             */

            IDLE_LIFETIME_IN_SECONDS            = 120U,
        };

        typedef bl::asio::ip::tcp::socket::executor_type                        executor_t;

        /**
         * @brief A one-shot rendezvous, owned by a shared pointer when a handler may outlive a wait
         */

        class Latch
        {
            BL_NO_COPY_OR_MOVE( Latch )

        public:

            Latch()
                :
                m_set( false )
            {
            }

            void set()
            {
                BL_MUTEX_GUARD( m_lock );

                m_set = true;

                m_cv.notify_all();
            }

            bool wait() const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_set;
                    }
                    );
            }

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            bool                                                                m_set;
        };

        /**
         * @brief The hook's control block and its record
         */

        struct HookControl
        {
            bl::os::mutex                                                       lock;
            bl::os::condition_variable                                          cv;

            bool                                                                armed;
            bool                                                                entered;
            bool                                                                hasStrand;
            bool                                                                onStrand;
            bool                                                                released;

            executor_t                                                          executor;

            /*
             * In the order it happened: read-start:begin, read-start:end, write-start, hold-timeout
             * and no-strand
             */

            std::vector< std::string >                                          record;

            HookControl()
                :
                armed( false ),
                entered( false ),
                hasStrand( false ),
                onStrand( false ),
                released( false )
            {
            }
        };

        inline auto hook() NOEXCEPT -> HookControl&
        {
            static HookControl control;

            return control;
        }

        /**
         * @brief Resets the control block; 'isArmed' decides whether the next read start holds
         */

        inline void resetHook( SAA_in const bool isArmed )
        {
            BL_MUTEX_GUARD( hook().lock );

            hook().armed = isArmed;
            hook().entered = false;
            hook().hasStrand = false;
            hook().onStrand = false;
            hook().released = false;
            hook().executor = executor_t();
            hook().record.clear();
        }

        inline void recordHookEvent( SAA_in std::string&& event )
        {
            BL_MUTEX_GUARD( hook().lock );

            hook().record.push_back( BL_PARAM_FWD( event ) );
        }

        inline bool waitForHookEntered()
        {
            bl::os::mutex_unique_lock guard( hook().lock );

            return hook().cv.wait_for(
                guard,
                bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                []() -> bool
                {
                    return hook().entered;
                }
                );
        }

        inline void releaseHook()
        {
            BL_MUTEX_GUARD( hook().lock );

            hook().released = true;

            hook().cv.notify_all();
        }

        /**
         * @brief What the hook knew at one moment, copied out from under its lock
         */

        struct HookSnapshot
        {
            bool                                                                entered;
            bool                                                                hasStrand;
            bool                                                                onStrand;
            executor_t                                                          executor;
            std::vector< std::string >                                          record;

            HookSnapshot()
                :
                entered( false ),
                hasStrand( false ),
                onStrand( false )
            {
            }
        };

        inline auto snapshotHook() -> HookSnapshot
        {
            BL_MUTEX_GUARD( hook().lock );

            HookSnapshot snapshot;

            snapshot.entered = hook().entered;
            snapshot.hasStrand = hook().hasStrand;
            snapshot.onStrand = hook().onStrand;
            snapshot.executor = hook().executor;
            snapshot.record = hook().record;

            return snapshot;
        }

        /**
         * @brief The stream the driver reads and writes through - the policy's own, with a hook
         *
         * It models only what the driver asks of its stream: executor_type, get_executor( ),
         * async_read_some( ) and async_write_some( ) - the last two taking the handler by forwarding
         * reference and passing it on untouched, since a by-value handler is a defect of its own here
         * (AsioSslStreamWrapper.h says why). INNER is tcp::socket for cleartext and
         * AsioSslStreamWrapper for TLS
         */

        template
        <
            typename INNER
        >
        class ReadStartHookStream
        {
            BL_NO_COPY_OR_MOVE( ReadStartHookStream )

        public:

            typedef executor_t                                                  executor_type;

            ReadStartHookStream(
                SAA_inout       INNER&                                          inner,
                SAA_in          const executor_type&                            executor
                )
                :
                m_inner( inner ),
                m_executor( executor )
            {
            }

            auto get_executor() NOEXCEPT -> executor_type
            {
                return m_executor;
            }

            template
            <
                typename MutableBufferSequence,
                typename Handler
            >
            void async_read_some(
                SAA_in          const MutableBufferSequence&                    buffers,
                SAA_in          Handler&&                                       handler
                )
            {
                bool isHeld = false;

                {
                    BL_MUTEX_GUARD( hook().lock );

                    if( hook().armed )
                    {
                        hook().armed = false;

                        isHeld = true;

                        hook().record.push_back( "read-start:begin" );
                    }
                }

                m_inner.async_read_some( buffers, std::forward< Handler >( handler ) );

                if( isHeld )
                {
                    holdReadStart();
                }
            }

            template
            <
                typename ConstBufferSequence,
                typename Handler
            >
            void async_write_some(
                SAA_in          const ConstBufferSequence&                      buffers,
                SAA_in          Handler&&                                       handler
                )
            {
                recordHookEvent( "write-start" );

                m_inner.async_write_some( buffers, std::forward< Handler >( handler ) );
            }

        private:

            /**
             * @brief Holds the read start until the case releases it, or the bound expires
             *
             * THE DRIVER'S CALL HAS NOT RETURNED WHILE THIS WAITS, which is the whole of what the hook
             * is: whatever runs meanwhile runs before the read's start has returned
             */

            void holdReadStart()
            {
                const auto* const strand = m_executor.target< bl::asio::strand_t >();

                bl::os::mutex_unique_lock guard( hook().lock );

                hook().executor = m_executor;
                hook().hasStrand = nullptr != strand;

                if( strand )
                {
                    hook().onStrand = strand -> running_in_this_thread();
                }
                else
                {
                    hook().record.push_back( "no-strand" );
                }

                hook().entered = true;

                hook().cv.notify_all();

                if( strand )
                {
                    const bool isReleased = hook().cv.wait_for(
                        guard,
                        bl::os::chrono::milliseconds(
                            static_cast< std::size_t >( HOLD_BOUND_IN_MILLISECONDS )
                            ),
                        []() -> bool
                        {
                            return hook().released;
                        }
                        );

                    if( ! isReleased )
                    {
                        hook().record.push_back( "hold-timeout" );
                    }
                }

                hook().record.push_back( "read-start:end" );
            }

            INNER&                                                              m_inner;
            const executor_type                                                 m_executor;
        };

        /**
         * @brief Either stranded policy, with getStream( ) answering the hook stream
         *
         * Hiding rather than overriding, which is the mechanism the stranded policies themselves use
         * for createSocket( ): the stream policy is a static interface resolved by template
         * composition, so the hiding definition is the one the driver finds. stream_t and stream_ref
         * are inherited, so the establisher hands over the real stream unchanged
         */

        template
        <
            typename BASE
        >
        class ReadStartHookPolicyT : public BASE
        {
            BL_CTR_DEFAULT( ReadStartHookPolicyT, protected )
            BL_DECLARE_OBJECT_IMPL( ReadStartHookPolicyT )

        public:

            typedef ReadStartHookPolicyT< BASE >                                this_type;
            typedef BASE                                                        base_type;
            typedef ReadStartHookStream< typename BASE::stream_t >              hook_stream_t;

        protected:

            bl::cpp::SafeUniquePtr< hook_stream_t >                             m_hookStream;

            auto getStream() NOEXCEPT -> hook_stream_t&
            {
                if( ! m_hookStream )
                {
                    m_hookStream.reset(
                        new hook_stream_t(
                            base_type::getStream(),
                            base_type::getSocket().get_executor()
                            )
                        );
                }

                return *m_hookStream;
            }
        };

        typedef ReadStartHookPolicyT< bl::tasks::TcpSocketAsyncStrandedBase >   PlainHookPolicy;
        typedef ReadStartHookPolicyT< bl::tasks::TcpSslSocketAsyncStrandedBase > TlsHookPolicy;

        typedef bl::tasks::Http1ConnectionTaskImpl< PlainHookPolicy >           PlainHookDriverImpl;
        typedef bl::tasks::Http1ConnectionTaskImpl< TlsHookPolicy >             TlsHookDriverImpl;

        /**
         * @brief Establishes a cleartext connection and gives it to the hook driver
         *
         * The establisher is utests/baselib/Http1DriverTestUtils.h's; only the factory differs, and
         * it takes the driver's idle lifetime because the cancel-before-start case needs one
         */

        inline auto establishPlainHookDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port,
            SAA_in_opt      const bl::time::time_duration&                      idleTimeout =
                                bl::time::neg_infin
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            typedef httpclient::ClientDriverFactoryT< plain_stream_t >          factory_t;

            const auto slot =
                std::make_shared< om::ObjPtr< httpclient::ClientConnection > >();

            const auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                httpclient::HttpProtocol::Http11,
                [ slot, idleTimeout ](
                    SAA_in      const httpclient::NegotiatedProtocol&           negotiated,
                    SAA_inout   plain_stream_t::stream_ref&&                    connectedStream,
                    SAA_in      const httpclient::ConnectionKey&                key
                    )
                    -> om::ObjPtr< httpclient::ClientConnection >
                {
                    auto driver = PlainHookDriverImpl::createInstance(
                        cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        cpp::copy( key ),
                        httpclient::Http1ResponseLimits(),
                        cpp::copy( idleTimeout )
                        );

                    auto result = om::qi< httpclient::ClientConnection >( driver );

                    *slot = om::copy( result );

                    return result;
                }
                );

            const auto establisher = PlainEstablisherImpl::createInstance(
                makeKey( std::string( "127.0.0.1" ), port ),
                factory,
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

        typedef bl::cpp::function
        <
            bl::om::ObjPtr< bl::httpclient::ClientConnection > (
                SAA_in      const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq
                )
        >
        establish_t;

        /**
         * @brief What the barrier exchange came to, read on the test thread
         */

        struct BarrierResult
        {
            bool                                                                entered;
            bool                                                                hasStrand;
            bool                                                                onStrand;
            bool                                                                probeRan;
            bool                                                                submitted;
            bool                                                                closed;
            unsigned                                                            status;
            bl::eh::error_code                                                  errorCode;
            std::string                                                         events;
            std::vector< std::string >                                          record;
            bool                                                                taskEnded;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            BarrierResult()
                :
                entered( false ),
                hasStrand( false ),
                onStrand( false ),
                probeRan( false ),
                submitted( false ),
                closed( false ),
                status( 0U ),
                taskEnded( false ),
                taskFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    "hook " +
                    std::string( entered ? "entered" : "NOT entered" ) +
                    ( onStrand ? " on the strand" : " off the strand" ) +
                    ", record " +
                    utest::http1driver::joinEvents( record ) +
                    ", probe " +
                    std::string( probeRan ? "ran" : "did not run" ) +
                    ", sink " +
                    events +
                    ", task " +
                    ( taskFailed ? taskFailure : std::string( taskEnded ? "clean" : "did not end" ) );
            }
        };

        /**
         * @brief D2-a - a request submitted while the first read's start is held
         *
         * THE PUSH RUNS ON A THREAD OF ITS OWN, because on the unfixed tree it is the call which enters
         * the hook: scheduleTask( ) arms the read inline, under the execution queue's lock and the task
         * lock, and that thread stays in the hook until the case releases it.
         *
         * THE BRANCH IS TAKEN BEFORE submit( ), ON WHERE THE HOOK SAYS IT IS:
         *
         *   - on the strand (the fixed shape): submit( ) from this thread, and release at once.
         *     Nothing can run on the strand while the hook holds it, and submit( ) only posts, so no
         *     write can start during the hold;
         *   - off the strand (the unfixed shape, or any fix that keeps the start off-strand): ONE
         *     handler on the strand calls submit( ) and then posts the probe, and the case waits for
         *     the probe before it releases. While that handler holds the strand its two posts queue in
         *     order behind it - [ onStartRequest, probe ] - and the write's completion cannot be
         *     enqueued before onStartRequest( ) has run, so it lands behind the probe. That is
         *     utests/baselib/Http1DriverTestUtils.h's submitAndProbe( ) shape, and the note there says
         *     why nothing weaker is deterministic: submit( ) from this thread with a probe posted after
         *     it lets the write's completion reach the strand first, its prolog takes the task lock
         *     the held push holds, the strand stalls and the probe never runs - A4's unreportable hang,
         *     as a race.
         *
         * THE RELEASE AND THE JOIN COME BEFORE ANY ASSERTION, so that no failure can throw past a thread
         * still holding the queue's lock. The peer is told to end the connection only once the case has
         * read everything it asserts on
         */

        inline auto runBarrierExchange(
            SAA_in          const establish_t&                                  establish,
            SAA_in          const bl::httpclient::ClientRequest&                request,
            SAA_in          const bl::cpp::void_callback_t&                     endPeer
            )
            -> BarrierResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            BarrierResult result;

            resetHook( true /* isArmed */ );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establish( eq );
                    const auto driverTask = om::qi< Task >( driver );

                    const auto eventSink = om::ObjPtrCopyable< httpclient::ClientStreamEventSink >(
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    const auto handle = std::make_shared< std::atomic< httpclient::stream_handle_t > >(
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE
                        );

                    os::thread pusher(
                        [ &eq, &driverTask ]() -> void
                        {
                            eq -> push_back( driverTask );
                        }
                        );

                    result.entered = waitForHookEntered();

                    const auto entered = snapshotHook();

                    if( result.entered && entered.hasStrand && ! entered.onStrand )
                    {
                        const auto probe = std::make_shared< Latch >();
                        const om::ObjPtrCopyable< httpclient::ClientConnection > connection( driver );
                        const auto executor = entered.executor;
                        const auto copy = request;

                        asio::post(
                            executor,
                            [ connection, eventSink, copy, handle, probe, executor ]() -> void
                            {
                                handle -> store( connection -> submit( copy, eventSink ) );

                                asio::post(
                                    executor,
                                    [ probe ]() -> void
                                    {
                                        probe -> set();
                                    }
                                    );
                            }
                            );

                        result.probeRan = probe -> wait();
                    }
                    else if( result.entered )
                    {
                        handle -> store( driver -> submit( request, eventSink ) );
                    }

                    releaseHook();

                    os::safeThreadJoin( pusher );

                    result.submitted = httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle -> load();

                    if( result.submitted )
                    {
                        result.closed = sink -> waitForClosed();
                    }

                    const auto released = snapshotHook();

                    result.hasStrand = released.hasStrand;
                    result.onStrand = released.onStrand;
                    result.record = released.record;

                    result.status = sink -> finalStatus();
                    result.errorCode = sink -> errorCode();
                    result.events = joinEvents( sink -> events() );

                    endPeer();

                    result.taskEnded = waitForTaskEnd(
                        driverTask,
                        static_cast< std::size_t >( WAIT_IN_MILLISECONDS )
                        );

                    if( ! result.taskEnded )
                    {
                        driverTask -> requestCancel();

                        ( void ) waitForTaskEnd(
                            driverTask,
                            static_cast< std::size_t >( WAIT_IN_MILLISECONDS )
                            );
                    }

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = taskFailureText( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            return result;
        }

        inline auto indexOf(
            SAA_in          const std::vector< std::string >&                  record,
            SAA_in          const std::string&                                  event
            )
            -> std::size_t
        {
            for( std::size_t i = 0U; i < record.size(); ++i )
            {
                if( event == record[ i ] )
                {
                    return i;
                }
            }

            return record.size();
        }

        /**
         * @brief What every barrier case asserts - each line refuses a different outcome
         *
         *   - the hook was entered, knew its thread, and was released rather than timing out - so the
         *     arrangement happened, and on the off-strand branch the probe proved every post of
         *     submit( )'s had run;
         *   - AND THE ONE THIS EXISTS FOR: no write started between the read start's beginning and its
         *     return, and one did start after it;
         *   - the request itself was answered - a 200, closed successfully - and the driver ended clean
         *     once the peer ended the connection
         */

        inline void chkNoWriteBeforeTheReadStartReturns(
            SAA_in          const BarrierResult&                                result,
            SAA_in          const std::string&                                  which
            )
        {
            using utest::http1driver::chkOrFail;

            const auto what = ". " + result.describe();

            chkOrFail( result.entered, which + ": the first read's start was never held" + what );

            chkOrFail(
                result.hasStrand,
                which + ": the socket's executor is not the strand, so the hook could not tell which "
                    "thread it held" + what
                );

            chkOrFail(
                result.record.size() == indexOf( result.record, "hold-timeout" ),
                which + ": the hold ran into its bound" + what
                );

            chkOrFail(
                result.onStrand || result.probeRan,
                which + ": the probe behind submit( )'s posts never ran" + what
                );

            const auto begin = indexOf( result.record, "read-start:begin" );
            const auto end = indexOf( result.record, "read-start:end" );
            const auto write = indexOf( result.record, "write-start" );

            chkOrFail(
                begin < end && end < result.record.size(),
                which + ": the read start's record is incomplete" + what
                );

            chkOrFail(
                write < result.record.size(),
                which + ": the request's write never started" + what
                );

            chkOrFail(
                end < write,
                which + ": a WRITE STARTED BEFORE THE FIRST READ'S START HAD RETURNED" + what
                );

            chkOrFail(
                result.submitted && result.closed && 200U == result.status && ! result.errorCode,
                which + ": the request was not answered with a successful 200" + what
                );

            chkOrFail(
                result.taskEnded && ! result.taskFailed,
                which + ": the driver did not end clean once the peer ended the connection" + what
                );
        }

        /**
         * @brief What the cancel-before-start exchange came to, read on the test thread
         */

        struct CancelBeforeStartResult
        {
            bool                                                                submitted;
            bool                                                                cancelDelivered;
            bl::eh::error_code                                                  errorCode;
            std::size_t                                                         closedEvents;
            std::string                                                         events;
            bool                                                                endedWithinBound;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            CancelBeforeStartResult()
                :
                submitted( false ),
                cancelDelivered( false ),
                closedEvents( 0U ),
                endedWithinBound( false ),
                taskFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    "sink " +
                    events +
                    ", task " +
                    std::string( endedWithinBound ? "ended within the bound" : "did NOT end within the bound" ) +
                    ( taskFailed ? ", failed: " + taskFailure : std::string( ", clean" ) );
            }
        };

        /**
         * @brief D2-b - submit, cancel, and only then schedule the driver
         *
         * THE CANCEL IS DELIVERED BEFORE THE PUSH, which is a rendezvous and not a hope: the sink's
         * onClosed( ) is what the cancel's onCancelStream( ) ends with, so once it has arrived the
         * cancel has run, and the push that follows starts a run whose reset erases the m_closing that
         * cancel set - the ordering the decision describes.
         *
         * THE BOUND IS FOR AN EVENT WHICH CANNOT HAPPEN ON THE UNFIXED TREE: the read is armed, the peer
         * never ends the connection on its own and the idle lifetime is past the bound, so nothing ends
         * the task. On the fixed tree the start handler takes the terminal on its first strand turn. So
         * a stall longer than the bound can only make a passing case fail, never make the unfixed tree
         * pass. When the bound does expire the case cancels the task, to tear down
         */

        inline auto runCancelBeforeStart(
            SAA_in          const establish_t&                                  establish,
            SAA_in          const bl::httpclient::ClientRequest&                request
            )
            -> CancelBeforeStartResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            CancelBeforeStartResult result;

            resetHook( false /* isArmed */ );

            const auto sink = RecordingSinkImpl::createInstance();

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establish( eq );
                    const auto driverTask = om::qi< Task >( driver );

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    result.submitted = httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle;

                    if( result.submitted )
                    {
                        driver -> cancel( handle, eh::error_code() );

                        result.cancelDelivered = sink -> waitForClosed();
                    }

                    eq -> push_back( driverTask );

                    result.endedWithinBound = waitForTaskEnd(
                        driverTask,
                        static_cast< std::size_t >( TASK_END_TIMEOUT_IN_MILLISECONDS )
                        );

                    if( ! result.endedWithinBound )
                    {
                        driverTask -> requestCancel();

                        ( void ) waitForTaskEnd(
                            driverTask,
                            static_cast< std::size_t >( WAIT_IN_MILLISECONDS )
                            );
                    }

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = taskFailureText( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            const auto events = sink -> events();

            for( const auto& event : events )
            {
                if( 0U == event.compare( 0U, 7U, "closed:" ) )
                {
                    ++result.closedEvents;
                }
            }

            result.events = joinEvents( events );
            result.errorCode = sink -> errorCode();

            return result;
        }

        /**
         * @brief What every cancel-before-start case asserts of the driver and the sink - the peer's
         * side is the case's own, because what the peer saw is spelled per transport
         */

        inline void chkCancelBeforeStartEndedTheConnection(
            SAA_in          const CancelBeforeStartResult&                      result,
            SAA_in          const std::string&                                  which
            )
        {
            using utest::http1driver::chkOrFail;

            const auto what = ". " + result.describe();

            chkOrFail(
                result.submitted && result.cancelDelivered,
                which + ": the request was not submitted and cancelled before the start" + what
                );

            chkOrFail(
                1U == result.closedEvents && bl::asio::error::operation_aborted == result.errorCode,
                which + ": the sink was not told operation_aborted exactly once" + what
                );

            chkOrFail(
                result.endedWithinBound,
                which + ": THE TASK DID NOT END - a cancel before the start was erased by the run's "
                    "reset, and the connection waits for the idle lifetime or the peer" + what
                );

            chkOrFail(
                ! result.taskFailed,
                which + ": the task ended failed rather than as a deliberate close" + what
                );
        }

    } // http1startup

} // utest

#endif /* __UTEST_HTTP1DRIVERSTARTUPHOOK_H_ */
