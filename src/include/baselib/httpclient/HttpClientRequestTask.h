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

#ifndef __BL_HTTPCLIENT_HTTPCLIENTREQUESTTASK_H_
#define __BL_HTTPCLIENT_HTTPCLIENTREQUESTTASK_H_

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief The per-request knobs a session applies to every request task it makes
         *
         * The two timeouts which ClientRequest carries of its own - totalTimeout and
         * responseHeadersTimeout - are DEFAULTS here and overrides there, which is what their
         * "unset means apply the session default" sentinel is for. The stream-idle timeout is
         * session level only: design 5.7 has it off by default and ClientRequest is frozen, so
         * putting it on the request type would be a negotiated change to every consumer bought for
         * a knob nothing turns on
         */

        struct HttpClientRequestConfig
        {
            /**
             * The deadline for the whole request, pool wait included - design 5.7, 30 minutes,
             * matching http/Globals.h
             */

            time::time_duration                                                 totalTimeout;

            /**
             * The deadline for the final response header block alone. Off by default
             */

            time::time_duration                                                 responseHeadersTimeout;

            /**
             * The longest gap between two stream events once the response has started. Off by
             * default
             */

            time::time_duration                                                 streamIdleTimeout;

            /**
             * The cap on a buffered response body - design 4.6, 64 MB, matching
             * SimpleHttpTask.h:83. Exceeding it resets the stream with CANCEL and fails the
             * request; it does not truncate, because a truncated body which looked successful
             * would be the worse failure
             */

            cpp::ScalarTypeIniter< std::size_t >                                maxResponseBodySize;

            /**
             * The cap on what one request holds of a response body and has not handed on - D3 of
             * astra's second review, 64 MB by default. Past it the stream is reset with CANCEL and
             * the request fails with BufferTooSmallException; nothing is truncated, and no read is
             * paused
             *
             * WHAT IS CHARGED is each held block's payload not yet taken PLUS a fixed allowance for
             * the memory the block costs beyond its payload -
             * HttpClientRequestTaskT::OUTSTANDING_BLOCK_ALLOWANCE - so that a peer which delivers one
             * byte per read cannot hold hundreds of bytes of memory per byte of payload. The unit
             * stays bytes
             *
             * IT BOUNDS WHAT IS OUTSTANDING AND NOT THE BODY: bytes a sink has taken, or the
             * buffered path has appended, are released, so a body of any size passes a sink which
             * keeps up. There is no "off" value, as maxResponseBodySize has none - a caller who
             * wants none sets SIZE_MAX - and 0 fails the first block of any body. On the buffered
             * path the body is appended as soon as it is applied, so this bounds only the mailbox
             * there: it is the backstop behind maxResponseBodySize for a drain which has fallen
             * behind, and its message differs, so a caller can meet it on a body under the total
             * cap
             */

            cpp::ScalarTypeIniter< std::size_t >                                maxOutstandingResponseBodySize;

            enum : std::size_t
            {
                DEFAULT_MAX_RESPONSE_BODY_SIZE      = 64U * 1024U * 1024U,
                DEFAULT_MAX_OUTSTANDING_RESPONSE_BODY_SIZE
                                                    = 64U * 1024U * 1024U,
                DEFAULT_TOTAL_TIMEOUT_IN_MINUTES    = 30U,
            };

            HttpClientRequestConfig()
                :
                totalTimeout( time::minutes( DEFAULT_TOTAL_TIMEOUT_IN_MINUTES ) ),
                responseHeadersTimeout( time::neg_infin ),
                streamIdleTimeout( time::neg_infin )
            {
                maxResponseBodySize = DEFAULT_MAX_RESPONSE_BODY_SIZE;
                maxOutstandingResponseBodySize = DEFAULT_MAX_OUTSTANDING_RESPONSE_BODY_SIZE;
            }

            /**
             * @brief Whether a duration is a real one, or the "unset" sentinel
             *
             * A duration is off when it is the negative-infinity sentinel this library already
             * uses for an optional duration, when it is not-a-date-time, or when it is not
             * positive. The last of those matters: a zero duration would otherwise arm a timer
             * which expires immediately and fail every request
             */

            static bool isArmed( SAA_in const time::time_duration& duration ) NOEXCEPT
            {
                return ! duration.is_special() && duration.total_milliseconds() > 0;
            }
        };

        /**
         * @brief One HTTP request, over whatever the connection it is given speaks
         *
         * PROTOCOL AGNOSTIC BY CONSTRUCTION - design 5.3. Nothing below names HTTP/2, an
         * http2::Session, a stream id or a frame; the only thing this task knows about the wire is
         * the ClientConnection interface, which the HTTP/2 driver and the HTTP/1.1 driver both
         * implement. That is the whole reason the S2.6 contract is expressed in plain values
         *
         * THE MAILBOX IS THE CONCURRENCY MODEL AND NOT AN OPTIMIZATION (design 5.2 rule L3).
         * Posting two handlers to a multi-threaded io_service does not order them, and this task
         * is fed from at least three places at once - a connection strand delivering stream
         * events, the pool answering an acquire( ), and its own timers. So every one of them
         * APPENDS to one deque and, if no drain is already scheduled, posts one. The drain runs on
         * ThreadPoolId::GeneralPurpose (design 5.2: the I/O threads are left to I/O) and consumes
         * the queue in order
         *
         * WHY THE DRAIN FLAG IS CLEARED AT THE END AND NOT AT THE START, which is the one place
         * this differs from the driver's otherwise identical command mailbox. The driver drains on
         * a STRAND, so two of its drains cannot overlap however the flag is managed. This one
         * drains on a thread POOL, where clearing the flag first would let an event posted during
         * a drain schedule a second drain which runs CONCURRENTLY with it, on another thread, and
         * every ordering guarantee above would be gone. The flag therefore stays set for the whole
         * drain and is cleared only at the moment the mailbox is observed empty under the mailbox
         * lock - so exactly one drain is ever scheduled or running, and no event is stranded
         *
         * ONE CALL LEAVES UNDER THE TASK LOCK AND EVERY OTHER ONE DOES NOT. A drain has three
         * phases: apply the events under the task lock, run what they decided to call out with the
         * lock released, then decide completion under the lock again and notify with it released.
         * consumed( ), provideBody( ), cancel( ), releaseStream( ) and the caller's BodySink and
         * BodySource are in the middle phase, and notifyReady( ) comes after the third - behind a
         * second cancel( ) when a deferred action threw. That is rule L4 in the direction this task
         * is responsible for, it keeps the caller's own callbacks off our lock, and it is what makes
         * notifyReady( ) legal at all, since TaskBase requires it not be called under the lock
         *
         * THE ONE EXCEPTION IS submit( ), AND IT IS AN EXCEPTION BY CHOICE, FOR SAME-BATCH
         * ORDERING. This comment used to say "by necessity", on the argument that a deferred
         * submit( ) would put the handle behind an event of its own which a sink event could
         * overtake. That argument does not hold: sink events cannot overtake this drain at all.
         * They APPEND to the mailbox and are applied by a LATER batch, the drain flag rather than
         * the task lock is what serializes the phases, and the middle phase already reads m_handle
         * every time offerToSink( ) hands a response body on
         *
         * THE HAZARD DEFERRING IT WOULD REALLY OPEN is one batch carrying [ Acquired, Expired ]:
         * phase one applies the expiry while there is still no handle, so it defers no reset, and
         * phase two then opens a stream for a request which has already failed - a stream reset
         * only when its response completes. One check of m_isCompletionPending inside a deferred
         * submit( ) would cover that, so the placement is "simpler here", not "impossible there".
         * It buys the same-batch ordering for nothing, and costs the lock order below and
         * toSessionRequest( ) under this task's lock
         *
         * WHAT IS NOT A CHOICE IS THE GUARD. Called in the apply phase it sits inside onDrain( )'s
         * NOEXCEPT region with nothing in between, so a throw has to be caught HERE: it fails this
         * request rather than reaching BL_RIP_MSG, which would end the process over one malformed
         * request. Deferred, runDeferred( ) would catch it instead and the request would fail in
         * phase three - the same outcome by another route, which is the other half of why the
         * placement is a choice
         *
         * THE LOCK ORDER THAT FOLLOWS FROM THAT, written down because nothing else records it:
         * this task's lock is taken before whatever submit( ) takes - the h2 driver's mailbox lock,
         * the h1 driver's state lock. Neither driver reaches for a request task's lock, so there is
         * no cycle; that sentence is what has to stay true
         *
         * The middle phase still belongs to the drain, so the state it touches is not shared with
         * anything: only one drain is ever scheduled or running, and the lock is what protects
         * that state from the SINK methods above, which run on the connection's strand
         */

        template
        <
            typename E = void
        >
        class HttpClientRequestTaskT :
            public tasks::TaskBase,
            public ClientStreamEventSink
        {
        public:

            typedef HttpClientRequestTaskT< E >                                 this_type;
            typedef tasks::TaskBase                                             base_type;

        private:

            BL_DECLARE_OBJECT_IMPL( HttpClientRequestTaskT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( ClientStreamEventSink )
                BL_QITBL_ENTRY_CHAIN_BASE( base_type )
            BL_QITBL_END( tasks::Task )

        public:

            /**
             * @brief The reference this task's own handlers hold on it
             *
             * om::Object is a base of this type TWICE - once through the task chain and once
             * through ClientStreamEventSink - so a plain om::ObjPtrCopyable< this_type > cannot be
             * formed: its acquireRef( ) converts to om::Object to call addRef( ) and that
             * conversion is ambiguous. Naming the task half resolves it to the subobject the
             * object model's implementation lives in, exactly as Http2ConnectionTaskT does
             */

            typedef om::ObjPtrCopyable< this_type, tasks::Task >                self_ref_t;

            /**
             * @brief Why this request stopped waiting, for the one message shape a timeout has
             */

            enum class TimeoutKind : std::uint8_t
            {
                Total,
                ResponseHeaders,
                StreamIdle,
            };

        protected:

            enum class EventKind : std::uint8_t
            {
                Start,
                Acquired,
                Headers,
                Data,
                Trailers,
                BodyWanted,
                Closed,
                Expired,
                Cancelled,

                /*
                 * A Data event whose block would have crossed maxOutstandingResponseBodySize,
                 * stripped of its block by post( ) - D3. Applied, it resets the stream and fails
                 * the request; see applyOverflow( )
                 */

                Overflow,
            };

            /**
             * @brief One entry of the mailbox
             *
             * One struct for every kind rather than a variant, which is what the driver's Command
             * does and for the same reason: a deque of one type needs no allocation discipline of
             * its own, and the unused members of an entry cost a few words on a queue whose body
             * blocks are bounded - by the stream window over HTTP/2, and over both protocols by
             * maxOutstandingResponseBodySize, which charges each block its Event as part of
             * OUTSTANDING_BLOCK_ALLOWANCE ( D3 )
             */

            struct Event
            {
                cpp::ScalarTypeIniter< EventKind >                              kind;
                cpp::ScalarTypeIniter< unsigned >                               status;
                cpp::ScalarTypeIniter< bool >                                   isInterim;
                cpp::ScalarTypeIniter< bool >                                   isRetryable;
                cpp::ScalarTypeIniter< std::size_t >                            bytes;
                cpp::ScalarTypeIniter< TimeoutKind >                            timeoutKind;

                http::HeaderList                                                headers;
                eh::error_code                                                  errorCode;

                om::ObjPtrCopyable< data::DataBlock >                           data;
                om::ObjPtrCopyable< ClientConnection >                          connection;

                std::exception_ptr                                              exception;
            };

        public:

            /**
             * @brief What each held body block is charged against
             * HttpClientRequestConfig::maxOutstandingResponseBodySize on top of its payload - D3
             *
             * DERIVED FROM THE TYPES, so that it stays honest as they change: the mailbox Event a
             * block waits in, the DataBlock object which carries it, and ALLOCATOR_OVERHEAD_PER_BLOCK
             * for the two heap allocations a block makes - the object and its buffer - each paying
             * the allocator's header and its rounding ( glibc's smallest chunk is 32 bytes ). At a64
             * clang debug that is 104 + 64 + 64 = 232. A block which has left the mailbox no longer
             * has its Event and holds less than it is charged, and that is accepted: one charge per
             * block, the larger one, is simpler to keep exact than one which changes as it moves
             *
             * Public because a case which states a cap in blocks has to compute it from this
             */

            enum : std::size_t
            {
                ALLOCATOR_OVERHEAD_PER_BLOCK        = 64U,

                OUTSTANDING_BLOCK_ALLOWANCE         =
                    sizeof( Event ) + sizeof( data::DataBlock ) + ALLOCATOR_OVERHEAD_PER_BLOCK,
            };

        protected:

            /**
             * @brief An interim (1xx) response, kept because ClientResponse has nowhere for one
             *
             * A 103 Early Hints block is the reason the sink carries interim header blocks at all,
             * and dropping it here would make the contract's care over it pointless. It is NOT put
             * on ClientResponse: that type is frozen, holds exactly one status and one header
             * list, and an interim response is a different response rather than a part of this one
             */

            struct InterimResponse
            {
                cpp::ScalarTypeIniter< unsigned >                               status;
                http::HeaderList                                                headers;
            };

            const ClientRequest                                                 m_request;
            const ConnectionKey                                                 m_key;
            const HttpClientRequestConfig                                       m_config;

            const om::ObjPtr< ConnectionPool >                                  m_pool;
            const om::ObjPtrCopyable< BodySink >                                m_bodySink;

            /*
             * The mailbox and its lock - a LEAF lock (design 5.2 rule L4). Nothing is called while
             * it is held, not even the task lock
             */

            mutable os::mutex                                                   m_mailboxLock;
            std::deque< Event >                                                 m_mailbox;
            cpp::ScalarTypeIniter< bool >                                       m_isDrainScheduled;

            /*
             * WHAT THIS TASK HOLDS OF THE BODY AND HAS NOT HANDED ON, as charged against
             * maxOutstandingResponseBodySize - D3. Mailbox state, under m_mailboxLock and never
             * touched without it: it rises in post( ), in the critical section which queues the
             * block, and falls as the sink takes bytes, as the buffered path appends a block or
             * refuses it, and as a block reaches a request which has already failed - see
             * chargeOf( ) and releaseOutstanding( )
             *
             * THE INVARIANT IS count <= cap, by construction: post( ) refuses a charge which would
             * cross the cap, and every release matches a charge. The latch is set by the first
             * refusal, after which no further body block is queued at all
             */

            cpp::ScalarTypeIniter< std::size_t >                                m_outstandingCharge;
            cpp::ScalarTypeIniter< bool >                                       m_isOutstandingCapExceeded;

            /*
             * Everything below is the task's own state, and after construction only the drain
             * writes it - under the task lock, or in the deferred phase where a member's comment
             * says so - except m_threadPool, which scheduleTask( ) sets under the mailbox lock. The
             * getters under "What the caller reads afterwards" read it with no lock: every field
             * they return is frozen at completion, and sinkDelivered( ) says where such a read is
             * safe
             */

            om::ObjPtr< ThreadPool >                                            m_threadPool;

            om::ObjPtr< ClientConnection >                                      m_connection;
            cpp::ScalarTypeIniter< stream_handle_t >                            m_handle;

            ClientResponse                                                      m_response;
            std::vector< InterimResponse >                                      m_interimResponses;

            std::string                                                         m_responseBody;
            std::deque< om::ObjPtrCopyable< data::DataBlock > >                 m_pendingDownload;

            /*
             * HOW MANY RESPONSE BYTES THE CALLER'S SINK HAS ACTUALLY TAKEN, which is the one thing
             * that decides whether another hop may reuse it - see sinkDelivered( ). Written in the
             * deferred phase by offerToSink( ), exactly like m_pendingDownload and for the reason
             * that function's comment gives
             */

            cpp::ScalarTypeIniter< std::size_t >                                m_sinkDelivered;

            /*
             * WHETHER THE CALLER'S SINK HAS THROWN - D4 of astra's second review. Once it has, it is
             * offered nothing more by this hop, and the session makes no replay onto it; see
             * hasSinkThrown( ). Written in the deferred phase by offerToSink( ), like the count above
             * and for the same reason, and never cleared
             */

            cpp::ScalarTypeIniter< bool >                                       m_hasSinkThrown;

            cpp::ScalarTypeIniter< bool >                                       m_isFinalHeadersSeen;
            cpp::ScalarTypeIniter< bool >                                       m_isStreamClosed;
            cpp::ScalarTypeIniter< bool >                                       m_isRetryable;
            cpp::ScalarTypeIniter< RequestOutcome >                             m_outcome;

            cpp::ScalarTypeIniter< bool >                                       m_isCompleted;
            cpp::ScalarTypeIniter< bool >                                       m_isCompletionPending;
            cpp::ScalarTypeIniter< bool >                                       m_isCompletionExpected;
            std::exception_ptr                                                  m_completionException;

            /*
             * WHETHER THE FAILURE m_completionException HOLDS WAS DECIDED BY THIS TASK - a timeout, a
             * cancel, a body cap, a sink or source which threw - rather than by the connection; see
             * isOwnFailure( ). Written by failWith( ) together with the exception, so it always
             * describes the failure which won
             */

            cpp::ScalarTypeIniter< bool >                                       m_isOwnFailure;

            cpp::SafeUniquePtr< asio::deadline_timer >                          m_totalTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_headersTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_idleTimer;

            HttpClientRequestTaskT(
                SAA_in              ClientRequest                               request,
                SAA_in              ConnectionKey                               key,
                SAA_in              om::ObjPtr< ConnectionPool >                pool,
                SAA_in_opt          HttpClientRequestConfig                     config =
                                        HttpClientRequestConfig(),
                SAA_in_opt          om::ObjPtrCopyable< BodySink >              bodySink =
                                        om::ObjPtrCopyable< BodySink >()
                )
                :
                m_request( BL_PARAM_FWD( request ) ),
                m_key( BL_PARAM_FWD( key ) ),
                m_config( BL_PARAM_FWD( config ) ),
                m_pool( BL_PARAM_FWD( pool ) ),
                m_bodySink( BL_PARAM_FWD( bodySink ) )
            {
                BL_CHK_T(
                    false,
                    nullptr != m_pool,
                    ArgumentException(),
                    BL_MSG()
                        << "An HTTP client request task requires a connection pool"
                    );

                m_handle = ClientConnection::INVALID_STREAM_HANDLE;
                m_outcome = RequestOutcome::Failed;
            }

            /*************************************************************************************
             * The mailbox
             */

            void post( SAA_inout Event&& event ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * DECLARED FIRST SO THAT IT DIES LAST, after the mailbox lock is released. An
                 * overflow marker's block is moved in here, and dropping a block runs its
                 * destructor - the kind of work this leaf lock keeps out
                 */

                om::ObjPtrCopyable< data::DataBlock > dropped;

                om::ObjPtr< ThreadPool > threadPool;

                {
                    BL_MUTEX_GUARD( m_mailboxLock );

                    if( EventKind::Data == event.kind && event.data )
                    {
                        /*
                         * D3 - THE ONE PLACE THE CHARGE RISES, and the check, the latch and the
                         * push are one step under this lock, so "counted" and "queued" can never
                         * disagree. This decides and cannot act: it is NOEXCEPT, runs on the
                         * driver's strand under a leaf lock, and the completion is the drain's. So
                         * a block which would cross the cap becomes a marker which the drain
                         * applies in order - behind every block queued before it, and ahead of the
                         * Closed, which the contract makes the last event
                         */

                        if( m_isOutstandingCapExceeded )
                        {
                            /*
                             * After the latch: not queued, not counted, no drain scheduled. The
                             * event dies in the caller's frame, off the lock
                             */

                            return;
                        }

                        const auto charge = chargeOf( *event.data );
                        const auto cap = m_config.maxOutstandingResponseBodySize.value();

                        if( charge > cap - m_outstandingCharge.value() )
                        {
                            m_isOutstandingCapExceeded = true;

                            event.kind = EventKind::Overflow;

                            dropped = event.data;
                            event.data.reset();
                        }
                        else
                        {
                            m_outstandingCharge = m_outstandingCharge.value() + charge;
                        }
                    }

                    m_mailbox.push_back( BL_PARAM_FWD( event ) );

                    /*
                     * A TASK WHICH HAS NOT BEEN SCHEDULED YET HAS NOWHERE TO POST, and that is
                     * reachable: requestCancel( ) may be called on a task the caller never pushed.
                     * The event is queued rather than dropped, and the post which schedules the
                     * task picks it up in order - which is also why the pool is read under this
                     * lock rather than off a bare member, since requestCancel( ) races with
                     * scheduleTask( ) on any thread the caller likes
                     */

                    if( m_isDrainScheduled || ! m_threadPool )
                    {
                        return;
                    }

                    m_isDrainScheduled = true;

                    threadPool = om::copy( m_threadPool );
                }

                threadPool -> aioService().post(
                    cpp::bind( &this_type::onDrain, self_ref_t::acquireRef( this ) )
                    );

                BL_NOEXCEPT_END()
            }

            void postKind( SAA_in const EventKind kind ) NOEXCEPT
            {
                Event event;

                event.kind = kind;

                post( std::move( event ) );
            }

            /**
             * @brief What one block is charged against maxOutstandingResponseBodySize - its payload
             * not yet taken, plus OUTSTANDING_BLOCK_ALLOWANCE for the memory it costs beyond it
             *
             * A block is not touched between post( ) and its apply, so what this computes when the
             * block is applied is exactly what post( ) charged
             */

            static std::size_t chargeOf( SAA_in const data::DataBlock& block ) NOEXCEPT
            {
                return
                    ( block.size() - block.offset1() ) +
                    static_cast< std::size_t >( OUTSTANDING_BLOCK_ALLOWANCE );
            }

            /**
             * @brief Gives back what a block was charged, or the part of it the sink has taken
             *
             * UNDER THE MAILBOX LOCK, which is a leaf: this calls nothing while holding it. It runs
             * from the drain's apply phase with the task lock held - an edge the task already has,
             * since TaskBase::scheduleNothrow( ) holds the task lock across scheduleTask( ), which
             * takes this lock - and from the deferred phase with no lock held
             *
             * A RELEASE NEVER EXCEEDS WHAT IS HELD, by construction: every release matches a
             * charge, and the crossing block and everything after the latch were never charged. It
             * is asserted, and clamped rather than allowed to wrap, so that a defect here would
             * loosen the cap rather than refuse every block of every later response
             */

            void releaseOutstanding( SAA_in const std::size_t charge ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( 0U == charge )
                {
                    return;
                }

                BL_MUTEX_GUARD( m_mailboxLock );

                BL_ASSERT( charge <= m_outstandingCharge.value() );

                m_outstandingCharge =
                    m_outstandingCharge.value() -
                    std::min< std::size_t >( charge, m_outstandingCharge.value() );

                BL_NOEXCEPT_END()
            }

            void onDrain() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                for( ;; )
                {
                    std::deque< Event > events;

                    {
                        BL_MUTEX_GUARD( m_mailboxLock );

                        if( m_mailbox.empty() )
                        {
                            /*
                             * Cleared HERE and nowhere else - see the class comment. The emptiness
                             * and the clearing are one atomic step under the mailbox lock, so a
                             * post( ) racing with this either sees the flag still set and appends
                             * to a queue this loop will take, or sees it clear and schedules the
                             * next drain itself
                             */

                            m_isDrainScheduled = false;

                            return;
                        }

                        events.swap( m_mailbox );
                    }

                    applyEvents( events );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Applies one batch in order, then runs what it decided to call out
             */

            void applyEvents( SAA_inout std::deque< Event >& events )
            {
                std::vector< cpp::void_callback_t > deferred;

                {
                    BL_MUTEX_GUARD( base_type::m_lock );

                    for( auto it = events.begin(); it != events.end(); ++it )
                    {
                        applyEvent( *it, deferred );
                    }
                }

                /*
                 * THE DEFERRED ACTIONS INCLUDE THE CALLER'S OWN BodySink AND BodySource, so one of
                 * them throwing is something that happens rather than something that cannot. It
                 * must not escape, because onDrain( ) is NOEXCEPT and an exception reaching it
                 * takes the process down over a user callback; and it must not stop the actions
                 * behind it either, or a sink which threw would also cost the stream slot that
                 * releaseStream( ) was queued to give back. So each one is guarded on its own and
                 * the FIRST failure is what the request is failed with - and what the stream is
                 * reset for, below
                 */

                std::exception_ptr deferredException;

                runDeferred( deferred, deferredException );

                bool complete = false;
                bool isExpected = false;

                std::exception_ptr eptr;

                std::vector< cpp::void_callback_t > reset;

                {
                    BL_MUTEX_GUARD( base_type::m_lock );

                    if( deferredException )
                    {
                        /*
                         * THE STREAM IS RESET BEFORE THE REQUEST IS FAILED, which is what every
                         * other giving-up path does ( applyStopped( ) ) and what this one used to
                         * omit. A sink or a provideBody( ) which threw leaves a stream the driver
                         * still holds open: no RST_STREAM, no timer left running to send one, and
                         * a peer waiting on an upload or a window which is never credited again.
                         * Failing the caller without it bounds the stream by the PEER's patience
                         */

                        cancelStream( reset );

                        failWith( deferredException, false /* isExpected */, true /* isOwnFailure */ );
                    }

                    if( m_isCompletionPending && ! m_isCompleted )
                    {
                        m_isCompleted = true;

                        complete = true;
                        eptr = m_completionException;
                        isExpected = m_isCompletionExpected;
                    }
                }

                /*
                 * BEFORE THE CALLER IS TOLD, and off the lock like every other call out. The first
                 * failure is already what the request failed with, so a second one has nowhere to
                 * go - and cancel( ) is NOEXCEPT on the contract, so there is not expected to be
                 * one; the guard is what the std::function machinery around it still owes
                 */

                runDeferred( reset, deferredException );

                if( complete )
                {
                    base_type::notifyReady( eptr, isExpected );
                }
            }

            /**
             * @brief Runs one deferred list with each action guarded, keeping the first failure
             */

            static void runDeferred(
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred,
                SAA_inout       std::exception_ptr&                             firstException
                )
            {
                for( std::size_t i = 0U; i < deferred.size(); ++i )
                {
                    try
                    {
                        deferred[ i ]();
                    }
                    catch( std::exception& )
                    {
                        if( ! firstException )
                        {
                            firstException = std::current_exception();
                        }
                    }
                }
            }

            void applyEvent(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                switch( event.kind.value() )
                {
                    default:
                    case EventKind::Start:
                        applyStart( deferred );
                        break;

                    case EventKind::Acquired:
                        applyAcquired( event, deferred );
                        break;

                    case EventKind::Headers:
                        applyHeaders( event, deferred );
                        break;

                    case EventKind::Data:
                        applyData( event, deferred );
                        break;

                    case EventKind::Trailers:
                        applyTrailers( event );
                        break;

                    case EventKind::BodyWanted:
                        applyBodyWanted( event, deferred );
                        break;

                    case EventKind::Closed:
                        applyClosed( event, deferred );
                        break;

                    case EventKind::Expired:
                    case EventKind::Cancelled:
                        applyStopped( event, deferred );
                        break;

                    /*
                     * AN ARM OF ITS OWN AND NOT THE default:, which is Start's - a marker which fell
                     * into it would arm the total timer again and acquire a second connection
                     */

                    case EventKind::Overflow:
                        applyOverflow( deferred );
                        break;
                }
            }

            /*************************************************************************************
             * The states
             */

            void applyStart( SAA_inout std::vector< cpp::void_callback_t >& deferred )
            {
                armTimer(
                    m_totalTimer,
                    effectiveTimeout( m_request.totalTimeout(), m_config.totalTimeout ),
                    TimeoutKind::Total
                    );

                const auto self = self_ref_t::acquireRef( this );
                const om::ObjPtrCopyable< ConnectionPool > pool( m_pool );

                const auto key = m_key;
                const auto request = m_request;

                deferred.push_back(
                    [ self, pool, key, request ]() -> void
                    {
                        pool -> acquire(
                            key,
                            request,
                            [ self ](
                                SAA_in_opt      const om::ObjPtr< ClientConnection >&    connection,
                                SAA_in_opt      const std::exception_ptr&                exception
                                ) -> void
                            {
                                Event answer;

                                answer.kind = EventKind::Acquired;
                                answer.connection = connection;
                                answer.exception = exception;

                                const_cast< this_type* >( self.get() ) -> post( std::move( answer ) );
                            }
                            );
                    }
                    );
            }

            /**
             * @brief Whether a refused submit( ) is about THIS REQUEST and not about the connection
             *
             * A driver refuses for one of two reasons and the pool has to be told which one. A
             * driver on its way out - the h2 driver after closeSubmissions( ), one which is
             * draining or closed - refuses everything, and the connection is what is wrong. An
             * HTTP/1.1 driver refuses EVERY request whose body is a BodySource, unconditionally
             * and before it so much as looks at its own state ( Http1ConnectionTask.h's submit( )
             * tests it first ): HTTP/1.1 would need request-side chunked framing for a body of
             * unknown length and the S2.5 serializer has none. That refusal is a statement about
             * the request, and the connection it was made on is healthy
             *
             * BOTH HALVES ARE READ OFF THE FROZEN CONTRACT and nothing was added for this:
             * ClientRequest::bodySource( ) is the request this task was handed, and
             * ClientConnection::negotiated( ) is what completeResponse( ) already reads off the
             * same connection. Both are NOEXCEPT, which is what makes reading them in the apply
             * phase legal at all
             *
             * IT DOES NOT ASK WHETHER THE CONNECTION IS OTHERWISE WELL, because it does not have
             * to: the refusal is certain from these two values alone, and a connection which is
             * additionally draining or closed is retired by the pool's own maintenance tick, which
             * reads state( ) on every pass
             */

            bool isRequestUnsuitableForConnection() const NOEXCEPT
            {
                return
                    nullptr != m_request.bodySource() &&
                    HttpProtocol::Http11 == m_connection -> negotiated().protocol();
            }

            void applyAcquired(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * The deadline expired while this request sat in the pool's queue, which is
                     * the ordinary shape of a timeout and not an error. The slot is handed
                     * straight back rather than leaked - no stream was ever opened on this path,
                     * and by the pairing rule at releaseConnectionSlot( ) none is needed
                     */

                    releaseConnectionSlot( event.connection, m_outcome.value(), deferred );

                    return;
                }

                if( event.exception || ! event.connection )
                {
                    failWith(
                        event.exception ?
                            event.exception :
                            std::make_exception_ptr(
                                BL_EXCEPTION(
                                    UnexpectedException(),
                                    "The connection pool answered with neither a connection nor an error"
                                    )
                                ),
                        false /* isExpected */
                        );

                    return;
                }

                m_connection = om::copy( event.connection );

                /*
                 * Qualified through the TASK half before the query, for the same reason
                 * self_ref_t is: om::Object is a base of this type twice, so an unqualified
                 * 'this' cannot be converted to it at all. The query itself then returns a
                 * counted reference the connection holds for the life of the stream, which is
                 * what keeps this task alive to see its own onClosed( ) even when the caller has
                 * long since been answered
                 */

                stream_handle_t handle = ClientConnection::INVALID_STREAM_HANDLE;

                try
                {
                    handle = m_connection -> submit(
                        m_request,
                        om::qi< ClientStreamEventSink >( static_cast< tasks::Task* >( this ) )
                        );
                }
                catch( std::exception& )
                {
                    /*
                     * A THROW IS ALWAYS THE REQUEST'S FAULT, where a refusal is the connection's
                     * unless the branch below finds otherwise. A driver which cannot turn THIS
                     * request into a submission - the h2 driver's ArgumentException over a header
                     * it will not put on the wire - has nothing wrong with it, so the outcome is
                     * Failed and the connection stays poolable; a retry would only reproduce the
                     * same exception on the same request, on any connection, so it is not marked
                     * retryable either
                     *
                     * It is caught at all because this call is in phase one: see the class comment.
                     * An escape here reaches onDrain( )'s BL_NOEXCEPT_END, which is BL_RIP_MSG
                     */

                    m_outcome = RequestOutcome::Failed;

                    failWith( std::current_exception(), false /* isExpected */ );

                    releaseConnectionSlot( event.connection, m_outcome.value(), deferred );

                    releaseConnection( event.connection, deferred );

                    return;
                }

                if( ClientConnection::INVALID_STREAM_HANDLE == handle )
                {
                    /*
                     * A refusal is RETRYABLE by nature - the request was provably not written -
                     * which is the half of the retry rule of design 5.4 this layer can state.
                     * isRetryable( ) answers "the failure proves this request was never
                     * processed", so it stands on both branches below; what a retry is WORTH is a
                     * different question and it is the retrying layer's, decided from the outcome
                     *
                     * THE OUTCOME IS WHERE THE TWO KINDS OF REFUSAL PART. A driver which refuses
                     * because it is on its way out is not one to hand out again, and that is
                     * ConnectionUnusable. A driver which refuses because THIS REQUEST is not one
                     * its protocol can carry - isRequestUnsuitableForConnection( ) above - is
                     * healthy, and reporting it unusable retires it; since the pool cancels what
                     * it forgets, that DESTROYS a working HTTP/1.1 connection once per streaming
                     * upload a caller sends its way. So it is Failed, exactly as the throw above
                     * is, and the connection stays poolable. The retry is still worth making, on a
                     * connection which negotiated h2 - which is the one thing that separates this
                     * from the throw, where the request is malformed for every connection alike
                     *
                     * THE SLOT GOES BACK either way, and the earlier reading that it could not -
                     * "there is no stream, so there is nothing releaseStream( ) can name" - was the
                     * L5 review's finding 1. The pool's count is the POOL's: it is incremented when
                     * the pool ANSWERS an acquire( ), and releaseStream( ) decrements it for the
                     * connection it is given and ignores the handle altogether. So the pairing rule
                     * is every answered acquire( ) against exactly one releaseStream( ), STREAM OR
                     * NO STREAM, and a path which skipped it leaked a slot for the life of the
                     * entry, which for h2 - one connection per key - is a permanent unit of capacity
                     */

                    m_isRetryable = true;

                    if( isRequestUnsuitableForConnection() )
                    {
                        m_outcome = RequestOutcome::Failed;

                        failWith(
                            std::make_exception_ptr(
                                BL_EXCEPTION(
                                    NotSupportedException(),
                                    "An HTTP/1.1 connection cannot carry a request whose body is streamed"
                                    )
                                ),
                            false /* isExpected */
                            );
                    }
                    else
                    {
                        m_outcome = RequestOutcome::ConnectionUnusable;

                        failWith(
                            std::make_exception_ptr(
                                BL_EXCEPTION(
                                    UnexpectedException(),
                                    "The connection refused the request it was acquired for"
                                    )
                                ),
                            false /* isExpected */
                            );
                    }

                    releaseConnectionSlot( event.connection, m_outcome.value(), deferred );

                    /*
                     * No stream was opened, so no onClosed( ) will ever arrive to let go of the
                     * connection - which makes this the one other place it has to be done
                     */

                    releaseConnection( event.connection, deferred );

                    return;
                }

                m_handle = handle;

                armTimer(
                    m_headersTimer,
                    effectiveTimeout(
                        m_request.responseHeadersTimeout(),
                        m_config.responseHeadersTimeout
                        ),
                    TimeoutKind::ResponseHeaders
                    );
            }

            void applyHeaders(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                BL_UNUSED( deferred );

                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * A HEADER BLOCK FOR A REQUEST WHICH HAS ALREADY FAILED IS DROPPED, as a late
                     * Data block is ( applyData( ), which says why a decided completion can only be
                     * a failure while one can still arrive ): the response the caller holds is not
                     * written - its task has completed, and the caller may be reading it - and no
                     * timer is armed. D3's guard at its siblings, owed-list row I8
                     */

                    return;
                }

                if( event.isInterim )
                {
                    InterimResponse interim;

                    interim.status = event.status;
                    interim.headers = std::move( event.headers );

                    m_interimResponses.push_back( std::move( interim ) );

                    armIdleTimer();

                    return;
                }

                /*
                 * THE FINAL BLOCK'S STATUS IS THE RESPONSE'S AND AN INTERIM'S NEVER IS. Every
                 * header block carries its own, and a 103 Early Hints which overwrote the 200 that
                 * followed it would be the defect the contract's status parameter exists to make
                 * impossible to reach by accident
                 */

                m_response.status( event.status );
                m_response.headers( std::move( event.headers ) );

                m_isFinalHeadersSeen = true;

                cancelTimer( m_headersTimer );

                armIdleTimer();
            }

            void applyTrailers( SAA_inout Event& event )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * Dropped for applyHeaders( )'s reason: not written into the response the caller
                     * holds, and no timer armed
                     */

                    return;
                }

                m_response.trailers( std::move( event.headers ) );

                armIdleTimer();
            }

            void applyData(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * A BLOCK FOR A REQUEST WHICH HAS ALREADY FAILED IS DROPPED - offered to no
                     * sink, appended to no body, credited to no window - and its charge is given
                     * back. The maintainer's third decision of this run, folded into D3
                     *
                     * WHILE Data CAN STILL ARRIVE, A DECIDED COMPLETION CAN ONLY BE A FAILURE. The
                     * one pending SUCCESS is answerOnClosed( )'s, set on a clean Closed, and no Data
                     * is applied after a Closed: the contract makes it the last event, and both
                     * drivers make it so for Data structurally - the HTTP/1.1 driver retires the
                     * sink and the handle before it calls onClosed( ), and the HTTP/2 driver erases
                     * the stream before it does. So this is a timeout or a cancel, a sink which
                     * threw, a failed upload read or the buffered cap - never a body the caller is
                     * owed, and every one of them has already reset the stream, so what is not
                     * credited here is squared up when the stream is reaped
                     *
                     * BEFORE armIdleTimer( ), deliberately: failWith( ) has cancelled every timer,
                     * and re-arming one here would hold this task for a stream idle timeout nobody
                     * is waiting on
                     */

                    if( event.data )
                    {
                        releaseOutstanding( chargeOf( *event.data ) );
                    }

                    return;
                }

                armIdleTimer();

                if( ! event.data )
                {
                    return;
                }

                if( m_bodySink )
                {
                    m_pendingDownload.push_back( event.data );

                    const auto self = self_ref_t::acquireRef( this );

                    deferred.push_back(
                        [ self ]() -> void
                        {
                            const_cast< this_type* >( self.get() ) -> offerToSink();
                        }
                        );

                    return;
                }

                const auto size = event.data -> size() - event.data -> offset1();

                if( m_responseBody.size() + size > m_config.maxResponseBodySize )
                {
                    /*
                     * The body is over the cap. The stream is reset and the request fails; the
                     * bytes are NOT credited, because crediting them would ask the server for more
                     * of a body we have already decided not to take
                     *
                     * THE BLOCK'S CHARGE GOES BACK FIRST, whatever follows, since it is dropped here
                     * - D3. Every block after this one reaches the guard above instead
                     */

                    releaseOutstanding( chargeOf( *event.data ) );

                    cancelStream( deferred );

                    failWith(
                        std::make_exception_ptr(
                            BL_EXCEPTION(
                                BufferTooSmallException(),
                                resolveMessage(
                                    BL_MSG()
                                        << "The HTTP response body exceeded the maximum of "
                                        << m_config.maxResponseBodySize.value()
                                        << " bytes"
                                    )
                                )
                            ),
                        false /* isExpected */,
                        true /* isOwnFailure */
                        );

                    return;
                }

                m_responseBody.append(
                    event.data -> begin() + event.data -> offset1(),
                    event.data -> begin() + event.data -> size()
                    );

                /*
                 * Appended, so no longer outstanding - D3. The buffered path lets go of a block as
                 * soon as it applies it, which is why the cap bounds only the mailbox here
                 */

                releaseOutstanding( chargeOf( *event.data ) );

                reportConsumed( size, deferred );
            }

            /**
             * @brief Offers what the sink has not taken yet, front first, and credits what it took
             *
             * A sink which consumes LESS than it was offered is applying backpressure, and the
             * remainder stays at the front of the queue to be offered again before anything newer
             * - which is what keeps a streamed body in order. Only what was actually consumed is
             * credited, and the same number is RETURNED, so that drainToSink( ) can tell a pass
             * which moved something from one which did not without looking at the queue twice
             *
             * THE BACKPRESSURE IT PRODUCES IS HTTP/2's AND IS NOT HTTP/1.1's, which this comment
             * used to state as though it held on both. Over h2 a sink which takes nothing closes
             * the stream window and the server stops sending - the design 5.3 chain working, not a
             * stall to be worked around. Over h1 there is no such chain: consumed( ) is a
             * documented no-op ( Http1ConnectionTask.h - "HTTP/1.1 has no flow control window" )
             * and the read is re-armed unconditionally, so the blocks accumulate in
             * m_pendingDownload and the remainder is re-offered on every new chunk instead - until
             * what is held reaches maxOutstandingResponseBodySize, where D3 fails the request
             * rather than hold more
             */

            std::size_t offerToSink()
            {
                std::size_t consumed = 0U;

                if( m_hasSinkThrown )
                {
                    /*
                     * A SINK WHICH THREW IS OFFERED NOTHING MORE - D4. The block it threw on is still
                     * at the front of the queue, and this is how the same batch used to hand it that
                     * block again: the next data event's own offer
                     */

                    return consumed;
                }

                while( ! m_pendingDownload.empty() )
                {
                    const auto block = m_pendingDownload.front();

                    const auto offered = block -> size() - block -> offset1();

                    std::size_t taken = 0U;

                    try
                    {
                        taken = std::min< std::size_t >(
                            offered,
                            m_bodySink -> onData( block )
                            );
                    }
                    catch( std::exception& )
                    {
                        /*
                         * Latched before the throw leaves, and the throw is not swallowed:
                         * runDeferred( ) is what turns it into this request's failure. The credit
                         * for what earlier blocks of this offer took is not sent - the stream is
                         * reset for the throw ( applyEvents( ) ), and a reset stream's unconsumed
                         * bytes are credited to the connection when the session reaps it
                         */

                        m_hasSinkThrown = true;

                        throw;
                    }

                    /*
                     * RECORDED BEFORE THE NEXT CALLBACK, which is the other half of D4. A later
                     * block of this same offer may throw, and a count kept only in the local went
                     * with it: the session then read zero from sinkDelivered( ) and could replay the
                     * request onto a sink which already held a prefix
                     */

                    m_sinkDelivered = m_sinkDelivered.value() + taken;

                    consumed += taken;

                    /*
                     * AND GIVEN BACK IN THE SAME PLACE - D3, after the call and never across it:
                     * what the sink took is no longer outstanding, and the block's allowance goes
                     * when the block leaves the queue. A partly taken block keeps its allowance
                     * until it does
                     */

                    if( taken < offered )
                    {
                        block -> setOffset1( block -> offset1() + taken );

                        releaseOutstanding( taken );

                        break;
                    }

                    m_pendingDownload.pop_front();

                    releaseOutstanding( taken + static_cast< std::size_t >( OUTSTANDING_BLOCK_ALLOWANCE ) );
                }

                /*
                 * THIS RUNS IN THE DEFERRED PHASE, off the task lock - which is the only way the
                 * caller's sink can be called without our lock held, since its RETURN VALUE is
                 * what decides the credit and there is nothing to defer it behind. The state it
                 * touches is still drain-owned: the deferred phase is part of the same drain, and
                 * the drain is serialized by the mailbox flag, so no second drain can be in here
                 */

                if( 0U != consumed && m_connection && ! m_isStreamClosed )
                {
                    m_connection -> consumed( m_handle, consumed );
                }

                return consumed;
            }

            /**
             * @brief The LAST offer, and the verdict on whether the body arrived whole
             *
             * "THE REMAINDER IS OFFERED AGAIN" HAS A LAST AGAIN, AND THE STREAM CLOSING IS IT.
             * offerToSink( ) breaks out of its own loop on a partial take, so one call does not
             * drain a sink which takes less than a block; this calls it until a whole pass moves
             * nothing, which is the only stopping rule that neither truncates a sink still taking
             * bytes nor spins on one that has stopped
             *
             * THE VERDICT IS A THROW AND NOT A FLAG. What is left in the queue when progress stops
             * is what the caller will never see, and a request which lost bytes is not a success -
             * so the truncation goes out through the ONE channel the deferred phase already has:
             * runDeferred( ) keeps it and applyEvents( ) hands it to failWith( ), whose second
             * guard admits it precisely because a pending success carries no exception of its own
             *
             * AND onComplete( ) MEANS THE BODY ARRIVED. It is called here and nowhere else, only
             * on the queue being empty, which is what makes ClientTypes.h's "the body is complete"
             * true of every call rather than of some of them
             *
             * NOTHING HAS A DEADLINE OVER THIS PHASE, AND WHAT IT HAS TO DO IS BOUNDED BY D3 RATHER
             * THAN BY TIME. applyClosed( ) calls cancelAllTimers( ) before the deferred phase, so
             * the drain runs with the idle timer and the total timer already dead. The BODY is
             * still capped by nothing this library sets - N1 took the h1 cap off
             * Http1ResponseLimits precisely because a codec cannot see whether a sink was
             * installed, and h2 never had one - and a cap on the body is still deliberately not
             * taken, because any number would silently truncate a body which was about to be
             * accepted, which is the defect this exists to close. What is bounded is what this
             * drain can find waiting: maxOutstandingResponseBodySize caps what is held and not
             * taken while the body arrives, and past it the request fails rather than truncates -
             * so a sink taking one byte a call makes at most that many onData( ) calls here, in
             * one uncancellable, undeadlined phase. It is NOT new work - it is the work the
             * contract already implied, compressed into one phase with no deadline over it
             */

            void drainToSink()
            {
                if( m_hasSinkThrown )
                {
                    /*
                     * A sink which threw is told nothing more - neither the bytes it threw on nor
                     * that the body is complete ( D4 ). Its exception is already the one the
                     * request fails with, because runDeferred( ) keeps the first failure of the
                     * phase and the throw ran ahead of this drain, so there is no verdict to add
                     */

                    return;
                }

                while( ! m_pendingDownload.empty() )
                {
                    if( 0U == offerToSink() )
                    {
                        break;
                    }
                }

                if( ! m_pendingDownload.empty() )
                {
                    std::size_t outstanding = 0U;

                    for( std::size_t i = 0U; i < m_pendingDownload.size(); ++i )
                    {
                        const auto& block = m_pendingDownload[ i ];

                        outstanding += block -> size() - block -> offset1();
                    }

                    BL_THROW(
                        createException< UnexpectedException >( false /* isExpected */ ),
                        BL_MSG()
                            << "The HTTP response body sink did not take "
                            << outstanding
                            << " bytes which arrived before the stream closed"
                        );
                }

                m_bodySink -> onComplete();
            }

            void applyBodyWanted(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                const auto& source = m_request.bodySource();

                if( ! source || ! m_connection || m_isStreamClosed )
                {
                    return;
                }

                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * A PULL FOR A REQUEST WHICH HAS ALREADY FAILED IS NOT ANSWERED, and the
                     * caller's source is not read again - applyHeaders( )'s guard, for the other
                     * direction. No answer is owed: a completion decided while the stream is still
                     * open can only be a failure ( applyData( ) says why ), and every such failure
                     * has reset the stream
                     */

                    return;
                }

                /*
                 * EXACTLY ONE ANSWER PER PULL, which is the half of the contract this side owes -
                 * the driver will not ask again until this returns, so returning nothing at all
                 * would stop the upload for good
                 */

                const auto wanted = std::max< std::size_t >( event.bytes, 1U );

                const om::ObjPtrCopyable< ClientConnection > connection( m_connection );
                const auto handle = m_handle.value();

                /*
                 * THE READ IS IN THE MIDDLE PHASE WITH EVERY OTHER CALL OUT, which the class
                 * comment always claimed and this function used not to do. read( ) is the
                 * CALLER's code: a file-backed source whose read fails throws, and a throw from
                 * phase one reaches onDrain( )'s BL_NOEXCEPT_END - BL_RIP_MSG, then fastAbort( ) -
                 * so an I/O error on an upload used to take the process down. Here it is one more
                 * guarded deferred action, and the request fails with the source's own exception
                 *
                 * The allocation goes with it: a block the size of the pull is as much the
                 * caller's memory as the bytes are, and there is no reason to take it under a lock
                 *
                 * ONE READ PER PULL still holds, and more visibly than before - the read and the
                 * provideBody( ) which answers it are now the same action
                 */

                deferred.push_back(
                    [ source, connection, handle, wanted ]() -> void
                    {
                        const auto block = data::DataBlock::get( nullptr /* dataBlocksPool */, wanted );

                        const auto result = source -> read( *block );

                        /*
                         * THE BLOCK'S OWN SIZE IS THE AUTHORITY over the reported one. The contract
                         * has the source APPEND at size( ) and grow the block, so the bytes which
                         * exist are the ones it grew to; a source whose report ran ahead of what it
                         * wrote would otherwise put uninitialised memory on the wire
                         */

                        const auto produced = std::min< std::size_t >( result.size, block -> size() );

                        if( produced != block -> size() )
                        {
                            block -> setSize( produced );
                        }

                        om::ObjPtrCopyable< data::DataBlock > payload;

                        if( 0U != block -> size() )
                        {
                            payload = block;
                        }

                        connection -> provideBody( handle, payload, result.isEndOfStream );
                    }
                    );
            }

            /**
             * @brief Whether a stream which ended in an error took its CONNECTION with it
             *
             * THE TWO VERDICTS A CLOSED STREAM CARRIES ARE NOT THE SAME VERDICT. isRetryable is
             * about THIS REQUEST - "the failure proves it was never processed" - and the outcome
             * is about the CONNECTION: ConnectionUnusable is "the connection cannot be used again"
             * ( ClientConnection.h ). Deriving the outcome from the error code alone, as this used
             * to, made ConnectionUnusable unreachable from a closed stream, and with it the only
             * feed of ConnectionPoolPolicy::retryIdempotentOnConnectionLoss:
             * chkRequestMayBeReplayed( ) reads isConnectionLost from exactly this value, so a
             * caller who turned that knob on got nothing whatsoever. A knob which reaches nothing
             * is the same defect one layer up, which is the standard this layer set for itself
             *
             * THE CONNECTION'S OWN STATE IS THE EVIDENCE, read at the close and not deduced from
             * the error code - the event carries no more than a code, and the same code reaches
             * here from a connection which died and from a stream which was reset on a healthy
             * one. publishState( ) is monotone and every connection-level route to a closed stream
             * publishes Draining or Closed with it - a GOAWAY, a peer which closed, a keepalive or
             * drain deadline, the task stopping - while a stream RESET leaves the connection
             * reading Ready. Anything but Ready is therefore a connection which cannot take this
             * request again, which is what the outcome says
             *
             * IT IS ASKED ONLY OF A CLOSE WHICH DOES NOT ALREADY PROVE THE REQUEST UNPROCESSED,
             * and that limit is not tidiness. ConnectionUnusable is what makes the pool RETIRE the
             * entry ( releaseStream( ) ), and the pool indexes one entry by EVERY connection it has
             * held: on design 5.5's ALPN fallback the bounced rider's connection is the h2
             * placeholder the pool has already replaced with the adopted HTTP/1.1 driver, and both
             * point at the same entry - so retiring "that connection" would destroy a healthy
             * driver and take the fallback path with it. Such a bounce is retryable, so the limit
             * costs nothing: chkRequestMayBeReplayed( ) returns from its isRetryable limb without
             * ever consulting isConnectionLost, and a connection which really is dead is retired by
             * the pool's own maintenance tick, which reads state( ) on every pass
             */

            auto outcomeOnClosed( SAA_in const Event& event ) const NOEXCEPT -> RequestOutcome
            {
                if( ! event.errorCode )
                {
                    return RequestOutcome::Completed;
                }

                if( event.isRetryable || ! m_connection )
                {
                    return RequestOutcome::Failed;
                }

                return ConnectionState::Ready == m_connection -> state() ?
                    RequestOutcome::Failed : RequestOutcome::ConnectionUnusable;
            }

            void applyClosed(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( m_isStreamClosed )
                {
                    return;
                }

                m_isStreamClosed = true;

                cancelAllTimers();

                const om::ObjPtrCopyable< ClientConnection > connection( m_connection );

                const auto outcome = outcomeOnClosed( event );

                /*
                 * WHAT THE CALLER READS IS FROZEN AT COMPLETION - D1 (b') of astra's third review,
                 * T01, which is I8's principle applied to the last two fields a caller reads.
                 * isRetryable( ) and outcome( ) are read with no lock by a caller which has seen the
                 * task complete, and nothing orders such a read against a close applied after it -
                 * so a close drained in a LATER batch than the failure which completed the task
                 * writes neither. It is still applied: its slot goes back, with the connection's
                 * verdict passed to the pool as it is, and the connection is let go
                 *
                 * THE GUARD IS m_isCompleted ALONE, and m_isCompletionPending is left out on purpose.
                 * A close drained in the SAME batch behind the task's own failure still writes both -
                 * isOwnFailure( ) says what that means - and a pending completion never spans two
                 * batches: the last phase of the batch which sets it completes it ( applyEvents( ) )
                 */

                if( ! m_isCompleted )
                {
                    m_isRetryable = event.isRetryable;
                    m_outcome = outcome;
                }

                /*
                 * THE SINK IS TOLD SOMETHING ONLY WHEN THIS CLOSE IS THE ANSWER, and both halves
                 * of that are load-bearing. The OUTCOME is why the ALPN bounce stops lying: it
                 * closes the stream with connection_aborted and isRetryable, so the outcome is not
                 * Completed, no terminal callback precedes the retry, and the sink which used to be
                 * told onComplete( ) with zero bytes and then handed the whole body is not
                 *
                 * The COMPLETION FLAGS are answerOnClosed( )'s own first line, and the batch
                 * [ Expired, Closed ] is why they are here too: a timeout applied by applyStopped( )
                 * fails the request and decides nothing about the close, so a clean close behind it
                 * in the same batch still reads Completed - and the caller holding a TimeoutException
                 * would have its sink told the body was complete
                 *
                 * SO A SINK GETS NO TERMINAL CALLBACK WHEN THE REQUEST FAILS, deliberately: the
                 * caller learns of that from the TASK, and a callback which fired whatever happened
                 * is onComplete( outcome ) - a change to a frozen contract, deferred in
                 * notes/plans/issues/body-sink-terminal-callback-and-reset-deferral.md
                 */

                if(
                    m_bodySink &&
                    RequestOutcome::Completed == outcome &&
                    ! m_isCompletionPending &&
                    ! m_isCompleted
                    )
                {
                    const auto self = self_ref_t::acquireRef( this );

                    deferred.push_back(
                        [ self ]() -> void
                        {
                            const_cast< this_type* >( self.get() ) -> drainToSink();
                        }
                        );
                }

                releaseConnectionSlot( connection, outcome, deferred );

                answerOnClosed( event );

                /*
                 * LAST, because everything above it still needs the connection - the slot goes
                 * back through it and completeResponse( ) reads negotiated( ) off it
                 */

                releaseConnection( connection, deferred );
            }

            /**
             * @brief Why the CONNECTION failed, when it failed, so the answer can name it
             *
             * EVERY ESTABLISHMENT FAILURE LOOKS THE SAME FROM HERE AND THAT IS THE PROBLEM. A
             * request dispatched onto a connection which is still Connecting is answered by the
             * driver's closeSubmissions( ), which bounces it with connection_aborted whatever
             * ended the establishment - so ECONNREFUSED, a resolver failure, an expired
             * establishment bound and a TLS CERTIFICATE VERIFICATION FAILURE all reach the caller
             * as "The HTTP request failed", after a full retry budget of establishments, with the
             * cause sitting unread on the connection task. The pool keeps that cause as lastError
             * for the requests it had QUEUED; the dispatched ones, which is every first request to
             * an origin, had nowhere to get it from. This is where they get it
             *
             * THE CONNECTION IS QUERIED FOR IT rather than told it, because ClientStreamEventSink::
             * onClosed( ) carries an error code and nothing else, and that contract is frozen.
             * Every driver is a tasks::Task, which is how the pool already reads one
             * ( om::tryQI< tasks::Task > in refreshEntry( ) ), and a task's exception( ) is its
             * failure verbatim
             *
             * AND IT IS DETERMINISTIC RATHER THAN A RACE, which is the one thing that makes it
             * worth pinning. notifyReadyImpl( ) holds the connection task's own lock across
             * onTaskStoppedNothrow( ) - which is where closeSubmissions( ) answers this sink - and
             * releases it only after recording the exception, while exception( ) takes that same
             * lock. So a reader which arrives during the close BLOCKS and then sees the cause; it
             * cannot see the half-way state. The wait is bounded because that critical section
             * never blocks on anything ( TaskBase says so in as many words )
             *
             * THE LOCK ORDER IS THIS TASK'S THEN THE CONNECTION'S, and it closes no cycle: the
             * only thing a driver ever takes on a request task is its MAILBOX lock, through
             * post( ), and the mailbox lock reaches nothing further. It is also the house idiom -
             * ForwarderTaskBaseT::exception( ) takes the wrapper's lock and then the wrapped
             * task's, which is the same edge in the same direction
             */

            auto connectionFailureCause() const NOEXCEPT -> std::exception_ptr
            {
                std::exception_ptr cause;

                BL_NOEXCEPT_BEGIN()

                if( m_connection )
                {
                    const auto task = om::tryQI< tasks::Task >( m_connection );

                    if( task )
                    {
                        cause = task -> exception();
                    }
                }

                BL_NOEXCEPT_END()

                return cause;
            }

            /**
             * @brief What the caller is told about a stream which has just ended
             */

            void answerOnClosed( SAA_in const Event& event )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    /*
                     * A timeout or a cancel already answered the caller; this is the closure which
                     * follows the RST_STREAM it sent. The slot has just been handed back, which is
                     * the whole reason this task stays alive to see it
                     */

                    return;
                }

                if( event.errorCode )
                {
                    auto failure = createException< HttpException >( false /* isExpected */ );

                    failure << eh::errinfo_error_code( event.errorCode );

                    const auto cause = connectionFailureCause();

                    if( cause )
                    {
                        /*
                         * CHAINED AND NOT SUBSTITUTED. The error code above is what this request
                         * saw and stays the answer; the connection's failure is what EXPLAINS it,
                         * and eh::diagnostic_information( ) prints a chain
                         */

                        failure << eh::errinfo_nested_exception_ptr( cause );
                    }

                    failWith(
                        std::make_exception_ptr(
                            BL_EXCEPTION( failure, "The HTTP request failed" )
                            ),
                        false /* isExpected */
                        );

                    return;
                }

                if( ! m_isFinalHeadersSeen )
                {
                    failWith(
                        std::make_exception_ptr(
                            BL_EXCEPTION(
                                UnexpectedException(),
                                "The HTTP stream ended before any response header block"
                                )
                            ),
                        false /* isExpected */
                        );

                    return;
                }

                completeResponse();

                cancelAllTimers();

                m_isCompletionPending = true;
            }

            /**
             * @brief Lets go of the connection, off the task lock
             *
             * A CONNECTION HOLDS THE SINK FOR THE LIFE OF THE STREAM, and the sink is this task,
             * so while a stream is open the two reference each other. The driver drops its half
             * after onClosed( ) - its contract says so - and this is the other half; a task which
             * kept the connection would hold a pooled connection alive for as long as its caller
             * kept the task
             *
             * The release is DEFERRED rather than done under the lock, because it may be the last
             * reference: destroying a connection task inside the request's own lock is the shape
             * of hazard rule L2 exists to keep out, and it costs one empty lambda to avoid
             */

            void releaseConnection(
                SAA_in          const om::ObjPtrCopyable< ClientConnection >&   connection,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                m_connection.reset();

                deferred.push_back( [ connection ]() -> void {} );
            }

            /**
             * @brief A timeout or a cancel - design 5.7: the stream is reset, the connection is not
             */

            void applyStopped(
                SAA_inout       Event&                                          event,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    return;
                }

                cancelAllTimers();

                cancelStream( deferred );

                if( EventKind::Cancelled == event.kind )
                {
                    failWith(
                        std::make_exception_ptr(
                            BL_EXCEPTION(
                                createException< UnexpectedException >( true /* isExpected */ )
                                    << eh::errinfo_error_code(
                                        asio::error::make_error_code( asio::error::operation_aborted )
                                        ),
                                "The HTTP request was cancelled"
                                )
                            ),
                        true /* isExpected */,
                        true /* isOwnFailure */
                        );

                    return;
                }

                failWith(
                    std::make_exception_ptr(
                        BL_EXCEPTION(
                            createException< TimeoutException >( true /* isExpected */ ),
                            createTimeoutMessage( event.timeoutKind )
                            )
                        ),
                    true /* isExpected */,
                    true /* isOwnFailure */
                    );
            }

            /**
             * @brief The overflow marker post( ) queued - D3: the stream is reset and the request
             * fails with BufferTooSmallException, as the buffered cap already does
             *
             * THE EARLIER VERDICT STANDS when there is one, and returning is right: every path which
             * decides a completion before a Closed has already reset the stream or never opened one
             * - applyStopped( ), the buffered cap, a failed deferred action - and a pending success
             * is set only on a Closed, which no marker can follow. The marker carries no charged
             * block, so nothing is given back here, and no timer is armed
             *
             * NOT A SILENT TRUNCATION: the pending failure is what stops applyClosed( ) queueing the
             * drain which tells the sink the body is complete, and what makes answerOnClosed( ) - the
             * success path - return at once. The sink keeps the prefix which arrived before the cap
             * was crossed, as after any other failure of a streamed request, and the caller holds
             * the failure
             */

            void applyOverflow( SAA_inout std::vector< cpp::void_callback_t >& deferred )
            {
                if( m_isCompleted || m_isCompletionPending )
                {
                    return;
                }

                cancelStream( deferred );

                failWith(
                    std::make_exception_ptr(
                        BL_EXCEPTION(
                            BufferTooSmallException(),
                            resolveMessage(
                                BL_MSG()
                                    << "The HTTP response body received and not yet taken exceeded "
                                    << "the maximum of "
                                    << m_config.maxOutstandingResponseBodySize.value()
                                    << " bytes, counting each held block's payload and an allowance of "
                                    << static_cast< std::size_t >( OUTSTANDING_BLOCK_ALLOWANCE )
                                    << " bytes for its memory"
                                )
                            )
                        ),
                    false /* isExpected */,
                    true /* isOwnFailure */
                    );
            }

            /*************************************************************************************
             * Completion
             */

            void completeResponse()
            {
                /*
                 * FILLED FROM THE ONE negotiated( ) VALUE AND NEVER FROM TWO. ClientResponse has a
                 * single NegotiatedProtocol and no setter for either half, precisely so a protocol
                 * and an ALPN identifier which disagree cannot be assembled here; protocol( ) and
                 * negotiatedAlpn( ) are read-only forwarders over it
                 *
                 * It is read at the END and not at submit time: a request submitted to an h2 task
                 * before ALPN resolves can be bounced onto a fallback driver, and the value which
                 * describes the connection the response actually came over is the one that
                 * connection has published by then. The END IS NOT ALWAYS AFTER THE NEGOTIATION,
                 * though - a request which fails before its connection's handshake has completed
                 * reads Unknown with no identifier. The read is safe either way: the connection
                 * publishes the value itself, and negotiated( ) may be called from any thread at
                 * any time (ClientConnection.h)
                 */

                if( m_connection )
                {
                    m_response.negotiated( m_connection -> negotiated() );
                }

                if( ! m_bodySink )
                {
                    /*
                     * The capacity is passed EXPLICITLY. DataBlock::copy( ) defaults it to the
                     * one-megabyte block default, which would refuse any response body larger than
                     * that - and a 64 MB cap exists precisely because they are expected
                     */

                    m_response.body(
                        om::ObjPtrCopyable< data::DataBlock >(
                            data::DataBlock::copy(
                                m_responseBody.c_str(),
                                m_responseBody.size(),
                                nullptr /* dataBlocksPool */,
                                std::max< std::size_t >( m_responseBody.size(), 1U )
                                )
                            )
                        );
                }
            }

            /**
             * @brief Records a failure, when it is the first - isOwnFailure says this task decided it
             *
             * isOwnFailure is TRUE WHERE THE TASK DECIDES THE FAILURE ITSELF - a timeout or a cancel
             * ( applyStopped( ) ), a body cap ( applyData( ), applyOverflow( ) ), a deferred action
             * which threw ( applyEvents( ) ) - the caller's sink or source, or anything else the
             * deferred phase calls - and false elsewhere: the connection's failures, the pool's
             * answer, and a submit( ) which threw, which the replay rule already refuses as not
             * retryable. It is recorded with the exception, so it describes the failure which won;
             * see isOwnFailure( )
             */

            void failWith(
                SAA_in          const std::exception_ptr&                       eptr,
                SAA_in          const bool                                      isExpected,
                SAA_in_opt      const bool                                      isOwnFailure = false
                )
            {
                /*
                 * TWO GUARDS IN PLACE OF ONE, BECAUSE A PENDING SUCCESS IS NOT A FAILURE. The
                 * comments above already promise that "the FIRST failure is what the request is
                 * failed with"; what one guard could not say is that a pending SUCCESS must not
                 * outrank one. It did: applyClosed( ) queues the caller's onComplete( ) and then
                 * answers the caller, so a sink which threw in the deferred phase arrived with
                 * m_isCompletionPending already set - and was discarded, with the request
                 * reported as a success
                 *
                 * PUBLISHED wins over everything, an EARLIER FAILURE wins over a later one, and a
                 * pending success loses to any failure
                 */

                if( m_isCompleted )
                {
                    return;
                }

                if( m_isCompletionPending && m_completionException )
                {
                    return;
                }

                /*
                 * EVERY TIMER DIES WITH THE REQUEST, and this is the funnel every failure passes
                 * through. An armed timer holds a reference to this task, so a total deadline left
                 * running would keep a finished request alive for its whole thirty minutes - not a
                 * leak in the end, but thirty minutes of one
                 *
                 * It stays unconditional because it is idempotent; completeResponse( ) does NOT,
                 * and the guard on it is not cosmetic. A second call would rebuild the response
                 * body from m_responseBody all over again, and by this point releaseConnection( )
                 * has already dropped m_connection - so the negotiated protocol it filled in
                 * would not be filled in twice, but the body copy would
                 */

                cancelAllTimers();

                if( ! m_isCompletionPending )
                {
                    completeResponse();
                }

                m_completionException = eptr;
                m_isCompletionExpected = isExpected;
                m_isCompletionPending = true;

                m_isOwnFailure = isOwnFailure;
            }

            /**
             * @brief An exception of this task's own, marked expected or not and enhanced
             *
             * The same helper SimpleHttpTask carries, and deliberately the same shape: marking an
             * expected exception with errinfo_is_expected is what stops TaskBase logging a
             * timeout as a failure, and enhanceException( ) is what puts the task's own properties
             * on it. It is a local helper there too rather than a TaskBase member, so this is the
             * house idiom and not a duplicate of something inherited
             */

            template
            <
                typename EXCEPTION
            >
            EXCEPTION createException( SAA_in const bool isExpected ) const
            {
                auto exception = EXCEPTION();

                if( isExpected )
                {
                    exception << eh::errinfo_is_expected( true );
                }

                base_type::enhanceException( exception );

                return exception;
            }

            std::string createTimeoutMessage( SAA_in const TimeoutKind kind ) const
            {
                /*
                 * THE EXISTING MESSAGE SHAPE, which is SimpleHttpTask::createTimeoutMessage( )'s -
                 * "HTTP <METHOD> request to '<url>' has timed out (<duration>)". Callers and logs
                 * already recognise it, and a second shape for the same event would be a gratuitous
                 * difference between the old task and the new one
                 *
                 * THE URL IS REDACTED AND THE SHAPE IS NOT. redactedUrl( ) renders the scheme, the
                 * authority and the path; this used to call net::Uri::toString( ), which also
                 * recomposes the userinfo, the query and the fragment (astra H20). See that
                 * function for what this does and does not claim to close
                 */

                return resolveMessage(
                    BL_MSG()
                        << "HTTP "
                        << m_request.method()
                        << " request to '"
                        << redactedUrl( m_request.url() )
                        << "' has timed out ("
                        << timeoutOf( kind )
                        << ")"
                    );
            }

            auto timeoutOf( SAA_in const TimeoutKind kind ) const -> time::time_duration
            {
                switch( kind )
                {
                    default:
                    case TimeoutKind::Total:
                        return effectiveTimeout( m_request.totalTimeout(), m_config.totalTimeout );

                    case TimeoutKind::ResponseHeaders:
                        return effectiveTimeout(
                            m_request.responseHeadersTimeout(),
                            m_config.responseHeadersTimeout
                            );

                    case TimeoutKind::StreamIdle:
                        return m_config.streamIdleTimeout;
                }
            }

            /**
             * @brief The request's own duration when it set one, and the session's otherwise
             */

            static auto effectiveTimeout(
                SAA_in          const time::time_duration&                      requested,
                SAA_in          const time::time_duration&                      fallback
                )
                -> time::time_duration
            {
                return requested.is_special() ? fallback : requested;
            }

            /*************************************************************************************
             * Calling out
             */

            void reportConsumed(
                SAA_in          const std::size_t                               bytes,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( 0U == bytes || ! m_connection || m_isStreamClosed )
                {
                    return;
                }

                /*
                 * OVER-ACKNOWLEDGING FAILS THE WHOLE CONNECTION AND NOT JUST THIS STREAM, so what
                 * is reported here is what was really taken and never what was offered. That is
                 * also why the streaming path credits the sink's return value rather than the
                 * block size
                 */

                const om::ObjPtrCopyable< ClientConnection > connection( m_connection );
                const auto handle = m_handle.value();

                deferred.push_back(
                    [ connection, handle, bytes ]() -> void
                    {
                        connection -> consumed( handle, bytes );
                    }
                    );
            }

            void cancelStream( SAA_inout std::vector< cpp::void_callback_t >& deferred )
            {
                if( ! m_connection || ClientConnection::INVALID_STREAM_HANDLE == m_handle )
                {
                    return;
                }

                const om::ObjPtrCopyable< ClientConnection > connection( m_connection );
                const auto handle = m_handle.value();

                deferred.push_back(
                    [ connection, handle ]() -> void
                    {
                        /*
                         * The driver puts CANCEL on the wire whatever is passed - design 5.7 - and
                         * the code here is only the reason this task is giving up
                         */

                        connection -> cancel(
                            handle,
                            asio::error::make_error_code( asio::error::operation_aborted )
                            );
                    }
                    );
            }

            /**
             * @brief Gives the pool back what it handed out - a CONNECTION SLOT, stream or no stream
             *
             * THE PAIRING RULE, which is the S5.1/S5.2 seam and the L5 review's finding 1: every
             * acquire( ) the pool ANSWERS is paired with exactly one releaseStream( ). The count
             * being given back is the pool's own - Entry::slotsInUse - and it is incremented at the
             * moment the pool answers, long before any stream exists; releaseStream( ) decrements
             * it for the connection it is passed and does not look at the handle at all. So the
             * guard here is on the CONNECTION and never on the handle: the two paths which hand a
             * connection back without ever opening a stream - a refused or throwing submit( ), and
             * an answer which arrives at a request that has already timed out - are exactly the
             * ones a handle guard used to drop, and each one dropped leaked a slot for the life of
             * the entry, kept the entry from ever being forgotten, and held its connection task
             * alive with it
             *
             * INVALID_STREAM_HANDLE is what the pool is then told, and that is honest: it names no
             * stream because there was none, and the pool logs it rather than accounting with it
             *
             * THE OUTCOME IS PASSED IN and not read off m_outcome, because the two part company
             * once the task has completed: a close applied after that leaves m_outcome as the
             * task completed with it ( applyClosed( ) ), and the pool is still owed what the
             * connection did - ConnectionUnusable is what retires a connection which died under the
             * stream. The acquire paths pass m_outcome, which is what this read when it read it
             */

            void releaseConnectionSlot(
                SAA_in          const om::ObjPtrCopyable< ClientConnection >&   connection,
                SAA_in          const RequestOutcome                            outcome,
                SAA_inout       std::vector< cpp::void_callback_t >&            deferred
                )
            {
                if( ! connection )
                {
                    return;
                }

                const om::ObjPtrCopyable< ConnectionPool > pool( m_pool );
                const auto handle = m_handle.value();

                const auto held = connection;

                deferred.push_back(
                    [ pool, held, handle, outcome ]() -> void
                    {
                        pool -> releaseStream( held, handle, outcome );
                    }
                    );
            }

            /*************************************************************************************
             * Timers - all of them armed and cancelled from the drain, so one timer object is
             * never touched by two threads at once
             */

            void armTimer(
                SAA_inout       cpp::SafeUniquePtr< asio::deadline_timer >&     timer,
                SAA_in          const time::time_duration&                      duration,
                SAA_in          const TimeoutKind                               kind
                )
            {
                if( ! HttpClientRequestConfig::isArmed( duration ) )
                {
                    return;
                }

                if( ! timer )
                {
                    timer.reset(
                        new asio::deadline_timer(
                            m_threadPool -> aioService(),
                            time::milliseconds( 0 )
                            )
                        );
                }

                timer -> expires_from_now( duration );

                timer -> async_wait(
                    cpp::bind(
                        &this_type::onTimerExpired,
                        self_ref_t::acquireRef( this ),
                        kind,
                        asio::placeholders::error
                        )
                    );
            }

            void armIdleTimer()
            {
                armTimer( m_idleTimer, m_config.streamIdleTimeout, TimeoutKind::StreamIdle );
            }

            static void cancelTimer( SAA_inout cpp::SafeUniquePtr< asio::deadline_timer >& timer ) NOEXCEPT
            {
                if( timer )
                {
                    eh::error_code ec;

                    timer -> cancel( ec );
                }
            }

            void cancelAllTimers() NOEXCEPT
            {
                cancelTimer( m_totalTimer );
                cancelTimer( m_headersTimer );
                cancelTimer( m_idleTimer );
            }

            void onTimerExpired(
                SAA_in          const TimeoutKind                               kind,
                SAA_in          const eh::error_code&                           ec
                ) NOEXCEPT
            {
                if( asio::error::operation_aborted == ec )
                {
                    return;
                }

                Event event;

                event.kind = EventKind::Expired;
                event.timeoutKind = kind;

                post( std::move( event ) );
            }

            /*************************************************************************************
             * tasks::Task
             */

            virtual void scheduleTask( SAA_in const std::shared_ptr< tasks::ExecutionQueue >& eq ) OVERRIDE
            {
                /*
                 * NOTHING BUT A POST, which TaskBase.h:857 requires: this runs with the task lock
                 * held AND under the caller's execution queue lock, so anything done here which
                 * reached the pool would be taking the pool lock under the user's queue lock -
                 * the first edge of the deadlock design 5.2 walks through
                 */

                {
                    /*
                     * Under the MAILBOX lock and not the task lock, because post( ) is what reads
                     * it and post( ) may run on any thread. Every other read of it is from a drain
                     * handler, which the post below happens-before
                     */

                    BL_MUTEX_GUARD( m_mailboxLock );

                    m_threadPool = base_type::getThreadPool( eq );
                }

                postKind( EventKind::Start );
            }

            virtual void requestCancel() NOEXCEPT OVERRIDE
            {
                base_type::requestCancelInternalMarkOnlyNoLock();

                postKind( EventKind::Cancelled );
            }

        public:

            /*************************************************************************************
             * ClientStreamEventSink - every one of these is a post and nothing else, because they
             * are called ON THE CONNECTION'S STRAND (design 5.2 rule L2)
             */

            virtual void onBodyWanted(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                Event event;

                event.kind = EventKind::BodyWanted;
                event.bytes = bytes;

                post( std::move( event ) );
            }

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          http::HeaderList&&                              headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                Event event;

                event.kind = EventKind::Headers;
                event.status = status;
                event.isInterim = isInterim;
                event.headers = BL_PARAM_FWD( headers );

                post( std::move( event ) );
            }

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const om::ObjPtr< data::DataBlock >&            data
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                Event event;

                event.kind = EventKind::Data;
                event.data = data;

                post( std::move( event ) );
            }

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          http::HeaderList&&                              trailers
                ) OVERRIDE
            {
                BL_UNUSED( handle );

                Event event;

                event.kind = EventKind::Trailers;
                event.headers = BL_PARAM_FWD( trailers );

                post( std::move( event ) );
            }

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( handle );

                Event event;

                event.kind = EventKind::Closed;
                event.errorCode = errorCode;
                event.isRetryable = isRetryable;

                post( std::move( event ) );
            }

            /*************************************************************************************
             * What the caller reads afterwards
             */

            const ClientRequest& request() const NOEXCEPT
            {
                return m_request;
            }

            const ClientResponse& response() const NOEXCEPT
            {
                return m_response;
            }

            /**
             * @brief The interim (1xx) responses which preceded the final one, in order
             *
             * On the HTTP/1.1 path these are delivered AFTER THE FACT - S4.3 files each interim as
             * the parser sees it and hands the whole list over immediately before the final block,
             * so a 103 reaches this task later than it reached the wire. That is accepted rather
             * than worked around: nothing in this library acts on an early hint yet, and a
             * per-interim callback out of a parser which has not finished the message is the kind
             * of re-entrancy the pull-style parser exists to avoid
             */

            const std::vector< InterimResponse >& interimResponses() const NOEXCEPT
            {
                return m_interimResponses;
            }

            /**
             * @brief Whether the failure proves this request was never processed
             *
             * The connection's half of the retry rule of design 5.4; the other half is
             * ClientRequest::isReplayable( ), and a replay needs both. False for a request which
             * succeeded, which is not a statement about it
             *
             * Read it with isOwnFailure( ): a close applied in the same batch behind a failure of
             * this task's own still sets it, and says what the connection did afterwards
             *
             * FROZEN AT COMPLETION, with outcome( ) - D1 (b') of astra's third review. A close
             * applied in a later batch, after the task has completed, changes neither, so both are
             * safe to read on any thread once the task is seen to have completed; sinkDelivered( )
             * says where that is
             */

            bool isRetryable() const NOEXCEPT
            {
                return m_isRetryable;
            }

            /**
             * @brief How this request ended for its connection - Completed, Failed or
             * ConnectionUnusable, as ClientConnection.h defines them
             *
             * Frozen at completion with isRetryable( ), and safe to read wherever that is. After a
             * failure of the task's own - isOwnFailure( ) - it is what a close drained in the same
             * batch said, or the Failed it was made with, which is no statement about the connection.
             * A close applied after completion still hands the pool its own verdict
             * ( releaseConnectionSlot( ) ), so the two can then differ: this one is the request as it
             * completed
             */

            RequestOutcome outcome() const NOEXCEPT
            {
                return m_outcome;
            }

            /**
             * @brief How many response bytes this hop's sink actually took
             *
             * WHAT IT IS FOR is the other half of the retry rule when a sink is installed: a
             * BodySink cannot be rewound - ClientTypes.h has no such method and freezing it is a
             * decision, not an oversight - so a hop which delivered bytes has spent the caller's
             * sink, and a replay would APPEND a second copy of the body to the prefix it already
             * holds. The session refuses the retry on exactly this value ( chkPrepareRetry( ) ),
             * and what it would take to permit one instead is
             * notes/plans/issues/body-sink-terminal-callback-and-reset-deferral.md
             *
             * WHERE IT IS SAFE TO READ, which is where isRetryable( ), outcome( ), hasSinkThrown( )
             * and isOwnFailure( ) are too: on any thread, with no task lock, once the hop is seen to
             * have completed - in its continuationTask( ), after waiting for it on its queue, or
             * from its state. The last write is offerToSink( )'s in the deferred phase, and
             * applyEvents( ) notifies ready only after that phase and after the locked section
             * behind it, so the completion edge orders the write before any such read - and no
             * write follows it: a completed hop's sink is offered nothing more, and the other
             * four are frozen at completion too
             */

            std::size_t sinkDelivered() const NOEXCEPT
            {
                return m_sinkDelivered;
            }

            /**
             * @brief Whether this hop's sink threw - after which it was offered nothing more
             *
             * THE OTHER READING THE SESSION TAKES BEFORE IT REPLAYS - D4 of astra's second review.
             * A sink which threw is spent whatever sinkDelivered( ) says, since it may have thrown on
             * the very first byte, and replaying onto it would hand the body again to a sink which
             * has already refused it. So chkPrepareRetry( ) refuses on this too
             *
             * Safe to read where sinkDelivered( ) is, and for its reason: the write is in the
             * deferred phase, before applyEvents( ) notifies ready
             */

            bool hasSinkThrown() const NOEXCEPT
            {
                return m_hasSinkThrown;
            }

            /**
             * @brief Whether this hop's failure was decided by the task itself - a timeout, a
             * cancel, a body cap, a sink or source which threw - rather than by the connection
             *
             * THE THIRD READING THE SESSION TAKES BEFORE IT REPLAYS - the general rule the maintainer
             * decided with owed-list row I5: such a hop is never replayed, and only a failure which
             * is the connection's - a close with an error code, a refused submit - may be. A close
             * drained in the same batch behind a failure of the task's own still writes what
             * isRetryable( ) and outcome( ) report, but it says what the connection did afterwards
             * and not why the request failed; one drained in a later batch, after the task has
             * completed, writes neither - they are frozen at completion ( applyClosed( ) ). False
             * for a request which succeeded
             *
             * NOT THE SAME READING AS hasSinkThrown( ), which refuses a replay even when the failure
             * which won was the connection's: a sink which threw is spent either way
             *
             * AND A SOURCE WHICH THREW BEHIND THE CONNECTION'S FAILURE IS NOT A REFUSAL. When a close
             * decides a batch and a pull applied ahead of it then has the caller's source throw in
             * the deferred phase, the failure which won is the connection's, and the session may
             * replay: unlike a sink, a source is rewound for a replay ( BodySource::rewind( ) ), and
             * one which cannot rewind is already refused by chkRequestMayBeReplayed( ). Any deferred
             * action which threw is otherwise this task's own failure, whatever threw - the caller's
             * sink or source, drainToSink( )'s verdict on bytes the sink never took, or a call out
             * to the connection or the pool
             *
             * Safe to read where sinkDelivered( ) is: failWith( ) writes it in the apply phase, or in
             * the locked section after the deferred one, and either is before applyEvents( ) notifies
             * ready
             */

            bool isOwnFailure() const NOEXCEPT
            {
                return m_isOwnFailure;
            }
        };

        typedef om::ObjectImpl< HttpClientRequestTaskT<> >                      HttpClientRequestTaskImpl;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_HTTPCLIENTREQUESTTASK_H_ */
