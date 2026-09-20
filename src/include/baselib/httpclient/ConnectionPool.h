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

#ifndef __BL_HTTPCLIENT_CONNECTIONPOOL_H_
#define __BL_HTTPCLIENT_CONNECTIONPOOL_H_

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief The policy knobs of design 5.4, and the two numbers this slice had to choose
         *
         * It is a value the pool copies, and it travels to the connection factory with every key
         * the pool asks for - which is what lets a factory build a driver configured the way the
         * pool it serves is configured, rather than the two agreeing by coincidence. The idle
         * lifetime and the draining reserve are pool policy which only a DRIVER can enforce, and
         * that is exactly why they are here and not there ( see below )
         */

        class ConnectionPoolPolicy FINAL
        {
        public:

            enum : std::size_t
            {
                /**
                 * Design 5.4: one connection per key for HTTP/2, six for HTTP/1.1. Which of the
                 * two applies is not known until a connection has been established, so the pool
                 * uses the HTTP/2 figure until it is - see effectiveMaxConnectionsPerKey( )
                 */

                DEFAULT_MAX_CONNECTIONS_PER_KEY             = 1U,
                DEFAULT_MAX_CONNECTIONS_PER_KEY_HTTP11      = 6U,

                DEFAULT_MAX_TOTAL_CONNECTIONS               = 64U,

                /**
                 * @brief What the pool will dispatch onto one connection at once, whatever the
                 * peer advertises
                 *
                 * The peer's SETTINGS_MAX_CONCURRENT_STREAMS is a 32 bit value and RFC 9113
                 * 6.5.2 gives it no upper bound, so a peer which advertises 2^31-1 would
                 * otherwise let one connection absorb every request there is - and with it every
                 * request's failure, since they all die together when it does. This is the pool's
                 * own ceiling and the peer's value is honoured only when it is lower
                 */

                DEFAULT_MAX_STREAMS_PER_CONNECTION          = 256U,

                /**
                 * Design 4.6 - "retries of one request: 3, then fail with the last error"
                 */

                DEFAULT_MAX_RETRIES_PER_REQUEST             = 3U,

                /**
                 * Design 5.4 - what a connection is assumed to allow until the peer's SETTINGS
                 * have arrived. The same number the h2 driver assumes, and it has to be: the pool
                 * reads the driver's freeStreamSlots( ) to latch the real one
                 */

                ASSUMED_MAX_CONCURRENT_STREAMS              = 100U,
            };

            enum : std::uint32_t
            {
                /**
                 * @brief THE DRAINING RESERVE - the margin design 4.3 left to the pool, chosen
                 * here ( S5.2 )
                 *
                 * StreamRegistry::isDraining( ) fires when the identifiers a connection can still
                 * open fall to this many. The registry deliberately defaults it to NONE, which
                 * means "when the space is spent", because how much room is needed is a function
                 * of what the pool has already committed to that connection and only the pool
                 * knows that.
                 *
                 * WHAT IT HAS TO COVER. A request is committed to a connection at the moment the
                 * pool answers acquire( ) with it, and the identifier is not taken until the
                 * driver's mailbox drain reaches the strand. Everything in that window is past
                 * the pool's own check, so the reserve must be at least the number of requests
                 * which can be in it at once. That is bounded by what the pool will dispatch onto
                 * one connection - maxStreamsPerConnection, 256 by default - and the requests it
                 * will hand out as slots free while a replacement connection is still being
                 * established. 1024 is four times the dispatch ceiling, which covers the window
                 * plus three further generations of slot reuse.
                 *
                 * WHY BEING GENEROUS COSTS NOTHING. A client connection can open ( 2^31 - 1 ) / 2
                 * + 1 streams, about 1.07 billion. Reserving 1024 of them gives up under one
                 * millionth of the connection's life.
                 *
                 * WHY BEING STINGY COSTS SOMETHING. The margin is what the pool is told about:
                 * the driver publishes Draining as soon as the registry begins draining, and
                 * offers no slots from then on, so a connection at its margin is one the pool
                 * retires rather than one it dispatches to ( Http2ConnectionTaskT::
                 * chkPublishDraining( ), called at both answers of applySubmit( ) - it did not
                 * exist until the L5 review, and without it this reserve announced nothing ).
                 * What the SIZE of the margin buys is identifiers for what the pool has already
                 * committed: a request is committed at acquire( ) and does not take an identifier
                 * until the driver's strand reaches it, so a margin too small to cover that
                 * window sends every request in it back through applySubmit( )'s canOpenStream( )
                 * branch as a retryable bounce, spending a retry budget of three per request at
                 * the one moment a connection has none to give
                 */

                DEFAULT_DRAINING_RESERVE                    = 1024U,
            };

            enum : long
            {
                /**
                 * @brief THE ESTABLISHMENT BOUND - see the long note at ConnectionPoolImplT
                 *
                 * How long "acquire( ) through a connection which can carry a request" may take,
                 * armed when the Connecting placeholder is inserted
                 */

                DEFAULT_ESTABLISHMENT_TIMEOUT_IN_SECONDS    = 120L,

                /**
                 * Design 5.7's connection idle lifetime, whose owner that table names as the
                 * pool. It is enforced by the driver, which owns the timer, and configured from
                 * here, which is where the policy lives
                 */

                DEFAULT_IDLE_TIMEOUT_IN_SECONDS             = 300L,

                /**
                 * Design 5.7's request total timeout, which includes the pool wait. It is the
                 * session default a ClientRequest's own time::neg_infin resolves to
                 */

                DEFAULT_REQUEST_TIMEOUT_IN_SECONDS          = 30L * 60L,
            };

            enum : long
            {
                /**
                 * @brief The maintenance tick, and why there is one
                 *
                 * THE CONTRACT HAS NO CONNECTION -> POOL NOTIFICATION, although design 5.2 rule
                 * L3 names that direction. A driver publishes its state into an atomic and there
                 * is nothing to subscribe to, so the pool cannot be TOLD that a connection became
                 * Ready, that an establishment is taking too long or that a queued request's
                 * deadline has passed. Everything else the pool does is driven by a call it
                 * receives - acquire( ) or releaseStream( ) - and those three are not.
                 *
                 * So the pool looks, on a timer which runs only while there is something to look
                 * at: a waiter queued, or a connection which has not become usable yet. The
                 * interval starts at the minimum and doubles up to the maximum for every tick
                 * which changes nothing, so a connection which establishes in the usual few
                 * milliseconds is noticed at once while a black-holed one costs a few hundred
                 * wakeups over the whole establishment bound rather than tens of thousands.
                 *
                 * THIS IS THE SEAM. If a driver ever offers a "state changed" notification - the
                 * posted notification L3 anticipates - the pool consumes it and this tick becomes
                 * the backstop for the two deadlines, which are time based and need a timer in
                 * any case. Nothing above the pool would change
                 */

                MIN_MAINTENANCE_INTERVAL_IN_MILLISECONDS    = 10L,
                MAX_MAINTENANCE_INTERVAL_IN_MILLISECONDS    = 250L,
            };

            cpp::ScalarTypeIniter< std::size_t >                                maxConnectionsPerKey;
            cpp::ScalarTypeIniter< std::size_t >                                maxConnectionsPerKeyHttp11;
            cpp::ScalarTypeIniter< std::size_t >                                maxTotalConnections;
            cpp::ScalarTypeIniter< std::size_t >                                maxStreamsPerConnection;
            cpp::ScalarTypeIniter< std::size_t >                                maxRetriesPerRequest;

            cpp::ScalarTypeIniter< std::uint32_t >                              drainingReserve;

            time::time_duration                                                 establishmentTimeout;
            time::time_duration                                                 idleTimeout;
            time::time_duration                                                 requestTimeout;

            /**
             * @brief Retrying a request the server MAY have processed, after the connection was
             * lost - design 5.4's separate knob, default off
             *
             * It is off because "the connection died" does not prove the request was not
             * processed: the response may have been lost on the way back, and replaying a POST
             * then performs it twice. Turning it on limits the replay to the idempotent methods
             * of RFC 9110 9.2.2 even so
             */

            cpp::ScalarTypeIniter< bool >                                       retryIdempotentOnConnectionLoss;

            /**
             * @brief Connection coalescing ( D21 ) - designed, and not implemented in the first
             * version
             *
             * RFC 9113 9.1.1 lets a connection to one origin carry a request for another when the
             * server's certificate covers that name and the name resolves to the connection's
             * address. The design is written down so that the decision is not re-litigated:
             *
             *   - the certificate check is crypto::TlsPeerVerification::certificateMatchesPeerName
             *     against the peer certificate saved at handshake time, which is why the key
             *     carries the verification flags - two requests which disagree about verification
             *     may not share a connection however identical their origins are;
             *   - the address check needs the resolved addresses of both names, so the resolver
             *     result has to be kept with the connection rather than discarded after connect;
             *   - a 421 Misdirected Request is remembered per ( connection, host ) and forces a
             *     dedicated connection from then on;
             *   - an ORIGIN frame, which the engine parses and records, narrows the set a
             *     connection claims and may only ever narrow it.
             *
             * What makes it more than an optimization is that a mistake here sends a request for
             * one host to a different host's connection. So it is refused rather than silently
             * ignored while it is unimplemented: a caller who turns it on is told, instead of
             * believing they have it
             */

            cpp::ScalarTypeIniter< bool >                                       enableCoalescing;

            ConnectionPoolPolicy()
                :
                establishmentTimeout(
                    time::seconds( DEFAULT_ESTABLISHMENT_TIMEOUT_IN_SECONDS )
                    ),
                idleTimeout( time::seconds( DEFAULT_IDLE_TIMEOUT_IN_SECONDS ) ),
                requestTimeout( time::seconds( DEFAULT_REQUEST_TIMEOUT_IN_SECONDS ) )
            {
                maxConnectionsPerKey = DEFAULT_MAX_CONNECTIONS_PER_KEY;
                maxConnectionsPerKeyHttp11 = DEFAULT_MAX_CONNECTIONS_PER_KEY_HTTP11;
                maxTotalConnections = DEFAULT_MAX_TOTAL_CONNECTIONS;
                maxStreamsPerConnection = DEFAULT_MAX_STREAMS_PER_CONNECTION;
                maxRetriesPerRequest = DEFAULT_MAX_RETRIES_PER_REQUEST;

                drainingReserve = DEFAULT_DRAINING_RESERVE;
            }
        };

        /**
         * @brief What a connection factory hands the pool for one key
         *
         * TWO THINGS AND NOT ONE, because which object the requests go to is not known when the
         * attempt starts. The task is what the pool schedules and watches; the accessor is how
         * the pool finds the driver the ALPN fallback built, if it built one.
         *
         * THE RULE THE POOL APPLIES TO THEM, which is the L4 review's and cost a defect to learn:
         * prefer what the accessor returns, and fall back to the task itself while it returns
         * nothing. An h2 connection task IS a ClientConnection and builds no driver, so the
         * accessor never returns anything and the task is the connection. A task whose peer
         * selected http/1.1 hands the connected stream to the factory, and from that moment the
         * driver is the connection while the task's own state( ) reads Closed - so a pool which
         * asked the task it submitted to would see a dead connection and a live one at the same
         * time, and believe the dead one
         */

        struct ConnectionAttempt
        {
            /**
             * The connection task, created but NOT scheduled - scheduling it is the pool's, on
             * its own queue. The same goes for a driver the accessor later produces
             */

            om::ObjPtrCopyable< tasks::Task >                                   task;

            /**
             * Reads the task's connection( ) - the driver the ALPN fallback built, or nullptr.
             * May itself be empty for a factory which cannot fall back
             */

            cpp::function< om::ObjPtr< ClientConnection > () >                  driver;
        };

        /**
         * @brief What the pool asks of whoever knows how to open a connection ( the session, S6.1 )
         *
         * The policy travels with the key because a driver has to be configured from it - the idle
         * lifetime of design 5.7 and the draining reserve of design 4.3 are pool policy which only
         * the driver can enforce, and handing over the policy is what stops the two drifting apart
         *
         * It is called OUTSIDE the pool lock, always ( design 5.2 rule L4 )
         */

        typedef cpp::function
        <
            ConnectionAttempt (
                SAA_in          const ConnectionKey&                            key,
                SAA_in          const ConnectionPoolPolicy&                     policy
                )
        >
        connection_factory_t;

        /**
         * @brief What the connection said about the failure, for the replay rule below
         */

        struct RetryContext
        {
            /**
             * The "provably unprocessed" verdict of the connection - ClientStreamEventSink::
             * onClosed( )'s isRetryable. The stream id was above a GOAWAY's last-stream-id, or the
             * reset was REFUSED_STREAM, or nothing of the request had been written
             */

            cpp::ScalarTypeIniter< bool >                                       isRetryable;

            /**
             * The connection itself is gone - RequestOutcome::ConnectionUnusable. On its own this
             * proves nothing about whether the request was processed
             */

            cpp::ScalarTypeIniter< bool >                                       isConnectionLost;

            /**
             * How many attempts this request has already spent
             */

            cpp::ScalarTypeIniter< std::size_t >                                attempts;
        };

        /**
         * @brief The retry rule of design 5.4 and D6, as one predicate
         *
         * It is a free function rather than a member because BOTH halves of the retry need it and
         * they do not sit in the same place. The pool owns the half it can see end to end - a
         * request still queued in the pool when the connection it was queued behind failed never
         * left the pool, so the pool counts its attempts itself. A request which was already
         * dispatched comes back through releaseStream( ) and a fresh acquire( ), and the frozen
         * S2.6 contract gives the pool no identity to count those against: acquire( ) takes a
         * ClientRequest by reference and releaseStream( ) names a handle the pool never issued.
         * So the count for that half belongs to the request task, which has per-request state by
         * construction, and the RULE stays here so that there is one rule and not two
         *
         * Both limbs must hold ( design 5.4 ): the failure must prove the request was unprocessed,
         * and the request must be replayable. A request whose body source cannot rewind is not,
         * however unprocessed the failure was
         */

        inline bool isIdempotentMethod( SAA_in const std::string& method ) NOEXCEPT
        {
            /*
             * RFC 9110 9.2.2 - the methods defined as idempotent. POST and PATCH are not, and a
             * method we do not know is assumed not to be
             */

            return
                "GET" == method ||
                "HEAD" == method ||
                "PUT" == method ||
                "DELETE" == method ||
                "OPTIONS" == method ||
                "TRACE" == method;
        }

        inline bool chkRequestMayBeReplayed(
            SAA_in          const ClientRequest&                                request,
            SAA_in          const RetryContext&                                 context,
            SAA_in          const ConnectionPoolPolicy&                         policy
            ) NOEXCEPT
        {
            if( context.attempts > policy.maxRetriesPerRequest )
            {
                return false;
            }

            if( ! request.isReplayable() )
            {
                return false;
            }

            if( context.isRetryable )
            {
                return true;
            }

            return
                context.isConnectionLost &&
                policy.retryIdempotentOnConnectionLoss &&
                isIdempotentMethod( request.method() );
        }

        /******************************************************************************************
         * ================================= ConnectionPoolImplT ==================================
         */

        /**
         * @brief The connection pool of design 5.4
         *
         * ------------------------------------------------------------------------------------
         * THE LOCK DISCIPLINE - DESIGN 5.2 RULE L4, WHICH IS WHY THIS CLASS LOOKS THE WAY IT DOES
         * ------------------------------------------------------------------------------------
         *
         * The pool lock is a LEAF. Nothing is called while it is held: not a waiter's callback,
         * not the connection factory, not the execution queue, not a connection, not a timer.
         * Every entry point therefore has the same shape - take the lock, decide, fill an Actions
         * value, drop the lock, and only then act. The worked deadlock in design 5.2 is built
         * entirely out of individually reasonable calls made the other way round, and the only
         * thing which stops it is that this file never makes one.
         *
         * The one thing the pool does under the lock which LOOKS like an exception is inserting
         * the Connecting placeholder before the factory is called. That is the point: the entry
         * exists before the lock is released, so the second request for the same key finds it and
         * queues behind it instead of opening a second connection. The factory itself runs after
         * the lock is dropped, and the entry is filled in under a second acquisition.
         *
         * ------------------------------------------------------------------------------------
         * THE ESTABLISHMENT BOUND, WHICH THIS SLICE OWNS
         * ------------------------------------------------------------------------------------
         *
         * Design 5.7's connect deadline covers "TCP connected through preface" and says so: it is
         * armed inside the connect completion handler, so the resolve and the async_connect are
         * outside it. What bounds THOSE is the operating system, which is not one number - the
         * resolver query is all_matching, so async_connect walks every address returned and a
         * black-holed one costs a full SYN timeout each, measured at 134 s on this host at the
         * Linux default tcp_syn_retries of 6, and the establisher's handshake retry buys a second
         * pass over the lot. So an origin which drops SYNs holds a Connecting placeholder, and
         * every request queued behind it, for minutes - bounded only by each request's own 30
         * minute total timeout.
         *
         * Design 5.7 recorded arming at schedule time as deferred rather than rejected, and left
         * the part it does not cover to nobody. It is bounded HERE, for the reason that section
         * gives: an overall establishment bound belongs with the retry policy, design 5.4 gives
         * the retry policy to the pool, and the pool is where a caller's deadline for "get me a
         * connection" is actually known.
         *
         * The bound is armed when the placeholder is inserted and is satisfied the first time the
         * connection reads Ready - so it covers resolve, connect, tunnel, handshake, floor, ALPN
         * and the preface, across the establisher's handshake retry, which is exactly the set
         * nothing else covers. On expiry the task is cancelled, the entry is retired and the
         * waiters behind it are retried on a fresh connection or failed with a TimeoutException.
         *
         * 120 seconds, because the legitimate worst case is two attempts of the 60 s per-attempt
         * deadline plus two resolve-and-connect legs, and the per-attempt deadline fires first on
         * every path where it applies at all - so this bound truncates nothing which would have
         * succeeded, while cutting the black hole above from minutes to two. It is one twentieth
         * of the request total timeout, so a request which hits it still has time to be tried on
         * a fresh connection rather than simply dying with it.
         *
         * ------------------------------------------------------------------------------------
         * WHAT IT WATCHES, AND WHAT IT CANNOT
         * ------------------------------------------------------------------------------------
         *
         * A driver publishes its state into an atomic and offers nothing to subscribe to, so the
         * pool has to look - see the maintenance tick note in ConnectionPoolPolicy. Three things
         * it must not get wrong while looking, all of them learned in L4:
         *
         *  - a clean close is not a failure. A connection which ended because a GOAWAY was drained,
         *    because it idled out or because its last stream finished completes SUCCESSFULLY, and
         *    the pool must not count it against anything. cancelTask( ) still reports failed, and
         *    that one is a failure.
         *  - the state is settled before onClosed( ) is delivered, because the request task calls
         *    releaseStream( ) from its handling of onClosed( ). So asking state( ) inside
         *    releaseStream( ) is meaningful and is where a connection which has just gone Draining
         *    or Closed is retired.
         *  - cancel( ) ends an HTTP/1.1 connection, because HTTP/1.1 has no stream reset. Design
         *    5.7's "cancelling a request never closes the connection" cannot hold there, so a
         *    connection which reports itself unusable after a cancel is ordinary rather than
         *    surprising
         */

        template
        <
            typename E = void
        >
        class ConnectionPoolImplT :
            public ConnectionPool,
            public om::Disposable
        {
            BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ConnectionPoolImplT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( ConnectionPool )
                BL_QITBL_ENTRY( om::Disposable )
            BL_QITBL_END( ConnectionPool )

        public:

            typedef ConnectionPoolImplT< E >                                    this_type;

            /**
             * @brief The reference the maintenance timer's handler holds on this pool
             *
             * om::Object is a base of this type TWICE - once through ConnectionPool and once
             * through om::Disposable - so a plain om::ObjPtrCopyable< this_type > cannot be formed
             * at all: its acquireRef( ) converts to om::Object to call addRef( ) and that
             * conversion is ambiguous. Naming the interface half resolves it, which is the same
             * answer TcpServerBase and the HTTP/2 driver reach for the same reason
             */

            typedef om::ObjPtrCopyable< this_type, ConnectionPool >             self_ref_t;

            /**
             * @brief What the pool has done, for diagnostics and for the cases which pin it
             */

            struct Stats
            {
                cpp::ScalarTypeIniter< std::size_t >                            connectionsCreated;
                cpp::ScalarTypeIniter< std::size_t >                            connectionsRetired;
                cpp::ScalarTypeIniter< std::size_t >                            dispatched;
                cpp::ScalarTypeIniter< std::size_t >                            released;
                cpp::ScalarTypeIniter< std::size_t >                            establishmentTimeouts;
                cpp::ScalarTypeIniter< std::size_t >                            requestTimeouts;
                cpp::ScalarTypeIniter< std::size_t >                            establishmentRetries;
                cpp::ScalarTypeIniter< std::size_t >                            failures;
            };

        protected:

            /**
             * @brief One connection, or one attempt at one
             */

            struct Entry
            {
                ConnectionKey                                                   key;
                ConnectionAttempt                                               attempt;

                /**
                 * The task AS a ClientConnection, when it is one - an h2 connection task is. The
                 * driver the factory built on the fallback path, when there is one. current( )
                 * applies the rule of ConnectionAttempt to the two
                 */

                om::ObjPtrCopyable< ClientConnection >                          taskConnection;
                om::ObjPtrCopyable< ClientConnection >                          driverConnection;

                /**
                 * Slots the pool has handed out and not yet had back. It is the POOL's count and
                 * not the driver's: a request the pool has answered has not reached the driver's
                 * strand yet, so freeStreamSlots( ) is stale high for exactly as long as that
                 * takes and cannot be the thing which limits dispatch
                 */

                cpp::ScalarTypeIniter< std::size_t >                            slotsInUse;

                /**
                 * The peer's concurrency limit, latched from freeStreamSlots( ) at a moment when
                 * the pool held no slot on this connection - which is the only moment the two
                 * agree. Zero until then, and ASSUMED_MAX_CONCURRENT_STREAMS is used instead
                 */

                cpp::ScalarTypeIniter< std::size_t >                            peerLimit;

                cpp::ScalarTypeIniter< bool >                                   isReady;
                cpp::ScalarTypeIniter< bool >                                   isRetired;
                cpp::ScalarTypeIniter< bool >                                   isScheduled;

                time::ptime                                                     establishBy;

                auto current() const NOEXCEPT -> const om::ObjPtrCopyable< ClientConnection >&
                {
                    return driverConnection ? driverConnection : taskConnection;
                }
            };

            typedef std::shared_ptr< Entry >                                    entry_ptr_t;

            /**
             * @brief One request waiting for a connection
             *
             * The request is held BY VALUE. acquire( ) takes it by reference and answers later, so
             * a reference kept here would outlive the call; and the pool reads isReplayable( ) and
             * the method off it long after the caller has moved on
             */

            struct Waiter
            {
                ClientRequest                                                   request;
                on_ready_callback_t                                             onReady;

                time::ptime                                                     deadline;

                /**
                 * Establishment attempts the POOL has spent on this waiter - see
                 * chkRequestMayBeReplayed( ) for why only this half is counted here
                 */

                cpp::ScalarTypeIniter< std::size_t >                            attempts;

                std::exception_ptr                                              lastError;
            };

            typedef std::shared_ptr< Waiter >                                   waiter_ptr_t;

            struct KeyState
            {
                std::vector< entry_ptr_t >                                      connections;
                std::deque< waiter_ptr_t >                                      waiters;
            };

            /**
             * @brief What a decision taken under the lock turns into once the lock is dropped
             */

            struct Answer
            {
                on_ready_callback_t                                             onReady;
                om::ObjPtrCopyable< ClientConnection >                          connection;
                std::exception_ptr                                              exception;
            };

            struct Actions
            {
                std::vector< Answer >                                           answers;
                std::vector< om::ObjPtrCopyable< tasks::Task > >                cancels;
                std::vector< om::ObjPtrCopyable< tasks::Task > >                schedules;
                std::vector< entry_ptr_t >                                      starts;

                cpp::ScalarTypeIniter< bool >                                   armMaintenance;

                bool empty() const NOEXCEPT
                {
                    return
                        answers.empty() && cancels.empty() && schedules.empty() &&
                        starts.empty() && ! armMaintenance;
                }
            };

            const connection_factory_t                                          m_factory;
            const ConnectionPoolPolicy                                          m_policy;

            mutable os::mutex                                                   m_lock;

            std::map< ConnectionKey, KeyState >                                 m_keys;
            std::map< const ClientConnection*, entry_ptr_t >                    m_byConnection;

            om::ObjPtrDisposable< tasks::ExecutionQueue >                       m_eqConnections;

            cpp::SafeUniquePtr< asio::deadline_timer >                          m_maintenanceTimer;
            cpp::ScalarTypeIniter< bool >                                       m_isMaintenanceArmed;
            cpp::ScalarTypeIniter< long >                                       m_maintenanceIntervalMs;

            cpp::ScalarTypeIniter< std::size_t >                                m_totalConnections;
            cpp::ScalarTypeIniter< bool >                                       m_isDisposed;

            Stats                                                               m_stats;

            ConnectionPoolImplT(
                SAA_in              connection_factory_t                        factory,
                SAA_in_opt          ConnectionPoolPolicy                        policy = ConnectionPoolPolicy()
                )
                :
                m_factory( BL_PARAM_FWD( factory ) ),
                m_policy( BL_PARAM_FWD( policy ) )
            {
                BL_CHK_T(
                    false,
                    !! m_factory,
                    ArgumentException(),
                    BL_MSG()
                        << "A connection pool requires a connection factory"
                    );

                BL_CHK_T(
                    true,
                    m_policy.enableCoalescing.value(),
                    NotSupportedException(),
                    BL_MSG()
                        << "HTTP/2 connection coalescing is designed but not implemented in this "
                        << "version (D21) - see ConnectionPoolPolicy::enableCoalescing for what "
                        << "implementing it requires"
                    );

                m_maintenanceIntervalMs =
                    ConnectionPoolPolicy::MIN_MAINTENANCE_INTERVAL_IN_MILLISECONDS;

                m_eqConnections = tasks::ExecutionQueueImpl::createInstance< tasks::ExecutionQueue >(
                    tasks::ExecutionQueue::OptionKeepNone
                    );

                m_maintenanceTimer.reset(
                    new asio::deadline_timer(
                        ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose )
                            -> aioService()
                        )
                    );
            }

            ~ConnectionPoolImplT() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                disposeInternal( true /* isFromDestructor */ );

                BL_NOEXCEPT_END()
            }

            /*************************************************************************************
             * Small helpers
             */

            static auto now() NOEXCEPT -> time::ptime
            {
                return time::microsec_clock::universal_time();
            }

            static bool isEnabled( SAA_in const time::time_duration& duration ) NOEXCEPT
            {
                return ! duration.is_special() && duration.total_milliseconds() > 0;
            }

            template
            <
                typename EXCEPTION
            >
            static auto makeException( SAA_in std::string message ) NOEXCEPT -> std::exception_ptr
            {
                return std::make_exception_ptr(
                    BL_EXCEPTION( EXCEPTION(), BL_PARAM_FWD( message ) )
                    );
            }

            static auto makeTimeoutException( SAA_in const ConnectionKey& key ) NOEXCEPT
                -> std::exception_ptr
            {
                return makeException< TimeoutException >(
                    resolveMessage(
                        BL_MSG()
                            << "A request for a connection to '"
                            << key.host
                            << ":"
                            << key.port.value()
                            << "' timed out"
                        )
                    );
            }

            static auto makeAbortedException() NOEXCEPT -> std::exception_ptr
            {
                return std::make_exception_ptr(
                    BL_EXCEPTION(
                        UnexpectedException()
                            << eh::errinfo_error_code(
                                eh::error_code( asio::error::operation_aborted )
                                ),
                        resolveMessage(
                            BL_MSG()
                                << "The connection pool was disposed while the request was waiting "
                                << "for a connection"
                            )
                        )
                    );
            }

            /**
             * @brief The rule of ConnectionAttempt, in one place
             */

            static auto resolveDriver( SAA_in const ConnectionAttempt& attempt ) NOEXCEPT
                -> om::ObjPtr< ClientConnection >
            {
                if( attempt.driver )
                {
                    auto driver = attempt.driver();

                    if( driver )
                    {
                        return driver;
                    }
                }

                return nullptr;
            }

            /*************************************************************************************
             * Everything below runs UNDER the pool lock and calls nothing ( rule L4 )
             */

            /**
             * @brief Design 5.4's connections per key - one for HTTP/2, six for HTTP/1.1
             *
             * Which applies is a property of the protocol, which is not known until a connection
             * has been established. Until then the HTTP/2 figure is used, and that is the
             * conservative direction: opening a second connection to an origin which turns out to
             * speak h2 wastes a connection AND a handshake, while queueing behind the first one
             * costs only the wait the placeholder exists to impose
             */

            std::size_t effectiveMaxConnectionsPerKey( SAA_in const KeyState& ks ) const NOEXCEPT
            {
                for( const auto& entry : ks.connections )
                {
                    const auto& connection = entry -> current();

                    if(
                        connection &&
                        HttpProtocol::Http11 == connection -> negotiated().protocol() &&
                        entry -> isReady
                        )
                    {
                        return m_policy.maxConnectionsPerKeyHttp11;
                    }
                }

                return m_policy.maxConnectionsPerKey;
            }

            std::size_t capacityOf( SAA_in const Entry& entry ) const NOEXCEPT
            {
                const auto peer = entry.peerLimit ?
                    entry.peerLimit.value() :
                    static_cast< std::size_t >( ConnectionPoolPolicy::ASSUMED_MAX_CONCURRENT_STREAMS );

                return std::min< std::size_t >( m_policy.maxStreamsPerConnection, peer );
            }

            void forgetConnection( SAA_in const entry_ptr_t& entry ) NOEXCEPT
            {
                if( entry -> taskConnection )
                {
                    m_byConnection.erase( entry -> taskConnection.get() );
                }

                if( entry -> driverConnection )
                {
                    m_byConnection.erase( entry -> driverConnection.get() );
                }
            }

            /**
             * @brief Looks at one connection and answers whether its attempt has just failed
             *
             * Everything which can be learned by LOOKING at a connection is learned here, because
             * there is nothing to subscribe to: the driver the fallback built has appeared; the
             * connection has become usable, which satisfies the establishment bound; it has gone
             * Draining or Closed, which retires it; the bound has expired; or the task ended
             * without ever producing a usable connection, which is an attempt that failed
             */

            bool refreshEntry(
                SAA_in          const entry_ptr_t&                              entry,
                SAA_in          const time::ptime&                              timeNow,
                SAA_inout       Actions&                                        actions,
                SAA_inout       std::exception_ptr&                             failure
                )
            {
                bool hasFailed = false;

                if( ! entry -> driverConnection && entry -> attempt.task )
                {
                    auto driver = resolveDriver( entry -> attempt );

                    if( driver )
                    {
                        /*
                         * The ALPN fallback built a driver. It is created and NOT scheduled - the
                         * h1 tests say why in as many words: pushing it onto a queue from inside
                         * the factory would be a connection strand taking a queue lock, which is
                         * rule L2. So the pool schedules it, here, and hands it out only after
                         */

                        entry -> driverConnection = om::ObjPtrCopyable< ClientConnection >( driver );

                        m_byConnection[ entry -> driverConnection.get() ] = entry;

                        auto driverTask = om::tryQI< tasks::Task >( entry -> driverConnection );

                        if( driverTask && tasks::Task::Created == driverTask -> getState() )
                        {
                            actions.schedules.push_back(
                                om::ObjPtrCopyable< tasks::Task >( driverTask )
                                );
                        }
                    }
                }

                const auto& connection = entry -> current();

                if( connection )
                {
                    const auto state = connection -> state();

                    if( ConnectionState::Ready == state )
                    {
                        entry -> isReady = true;

                        if( 0U == entry -> slotsInUse )
                        {
                            /*
                             * The only moment at which the driver's count and the pool's agree,
                             * so the only moment at which freeStreamSlots( ) IS the peer's limit
                             */

                            const auto slots = connection -> freeStreamSlots();

                            if( slots )
                            {
                                entry -> peerLimit = slots;
                            }
                        }
                    }
                    else if( ConnectionState::Draining == state || ConnectionState::Closed == state )
                    {
                        if( ! entry -> isRetired )
                        {
                            entry -> isRetired = true;

                            ++m_stats.connectionsRetired.lvalue();
                        }
                    }
                }

                if( entry -> isReady )
                {
                    return false;
                }

                /*
                 * From here on the entry has never become usable, so the two ways an attempt ends
                 * without producing a connection apply
                 */

                if(
                    ! entry -> establishBy.is_special() &&
                    timeNow >= entry -> establishBy &&
                    ! entry -> isRetired
                    )
                {
                    entry -> isRetired = true;
                    hasFailed = true;

                    ++m_stats.establishmentTimeouts.lvalue();
                    ++m_stats.connectionsRetired.lvalue();

                    failure = makeTimeoutException( entry -> key );

                    if( entry -> attempt.task )
                    {
                        actions.cancels.push_back( entry -> attempt.task );
                    }

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "A connection to '"
                            << entry -> key.host
                            << ":"
                            << entry -> key.port.value()
                            << "' did not become usable within "
                            << m_policy.establishmentTimeout
                            << " and was abandoned by the pool"
                        );

                    return hasFailed;
                }

                if(
                    entry -> attempt.task &&
                    tasks::Task::Completed == entry -> attempt.task -> getState()
                    )
                {
                    /*
                     * The task ended and there is no connection to show for it. A task which fell
                     * back to HTTP/1.1 ends this way too and is NOT a failure - but it produced a
                     * driver, which the refresh above would have picked up, so reaching here means
                     * it did not
                     */

                    if( ! entry -> isRetired )
                    {
                        entry -> isRetired = true;

                        ++m_stats.connectionsRetired.lvalue();
                    }

                    hasFailed = true;

                    auto eptr = entry -> attempt.task -> exception();

                    failure = eptr ? eptr : makeException< UnexpectedException >(
                        resolveMessage(
                            BL_MSG()
                                << "A connection to '"
                                << entry -> key.host
                                << ":"
                                << entry -> key.port.value()
                                << "' ended before it could carry a request"
                            )
                        );
                }

                return hasFailed;
            }

            auto findDispatchable(
                SAA_in          const KeyState&                                 ks,
                SAA_in          const Waiter&                                   waiter
                ) const NOEXCEPT -> entry_ptr_t
            {
                entry_ptr_t connecting;

                for( const auto& entry : ks.connections )
                {
                    if( entry -> isRetired )
                    {
                        continue;
                    }

                    const auto& connection = entry -> current();

                    if( ! connection )
                    {
                        continue;
                    }

                    const auto state = connection -> state();

                    if( ConnectionState::Ready == state )
                    {
                        /*
                         * freeStreamSlots( ) is stale HIGH while the pool's dispatches are still
                         * on their way to the strand, never stale low, so a zero read is a true
                         * zero and is the one thing it can be trusted for. The limit which
                         * actually bounds dispatch is the pool's own count against the capacity
                         */

                        if(
                            entry -> slotsInUse < capacityOf( *entry ) &&
                            connection -> freeStreamSlots() > 0U
                            )
                        {
                            return entry;
                        }
                    }
                    else if( ConnectionState::Connecting == state && ! connecting && 0U == entry -> slotsInUse )
                    {
                        connecting = entry;
                    }
                }

                /*
                 * THE FIRST REQUEST RIDES THE PREFACE ( design 5.1 and 5.4 ): it is submitted to a
                 * connection which is still establishing, so that its HEADERS are gathered into
                 * the opening write with the client preface, the SETTINGS and the WINDOW_UPDATE,
                 * the way a browser does it. Exactly one, because until the peer's SETTINGS arrive
                 * nothing else is known.
                 *
                 * ONLY A REPLAYABLE REQUEST, and that is not a detail. A connection which is still
                 * establishing may yet turn out to speak http/1.1, and a request submitted to an
                 * h2 task before ALPN resolves is bounced retryable when it does. A request which
                 * cannot be replayed would simply fail, where waiting a few milliseconds for the
                 * connection to be Ready would have cost it nothing
                 */

                if( connecting && waiter.request.isReplayable() )
                {
                    return connecting;
                }

                return entry_ptr_t();
            }

            bool canStartConnection( SAA_in const KeyState& ks ) const NOEXCEPT
            {
                if( m_totalConnections >= m_policy.maxTotalConnections )
                {
                    return false;
                }

                std::size_t live = 0U;

                for( const auto& entry : ks.connections )
                {
                    if( ! entry -> isRetired )
                    {
                        ++live;
                    }
                }

                return live < effectiveMaxConnectionsPerKey( ks );
            }

            /**
             * @brief Inserts the Connecting placeholder - UNDER THE LOCK, before it is released
             *
             * This is the whole of design 5.4's queueing rule: the entry is in the map before the
             * lock is dropped, so the next request for the same key finds it and queues behind it.
             * The task is created afterwards, outside the lock, by startConnection( )
             */

            auto insertPlaceholder(
                SAA_in          const ConnectionKey&                            key,
                SAA_inout       KeyState&                                       ks
                ) -> entry_ptr_t
            {
                auto entry = std::make_shared< Entry >();

                entry -> key = key;

                if( isEnabled( m_policy.establishmentTimeout ) )
                {
                    entry -> establishBy = now() + m_policy.establishmentTimeout;
                }

                ks.connections.push_back( entry );

                ++m_totalConnections.lvalue();
                ++m_stats.connectionsCreated.lvalue();

                return entry;
            }

            void answerWaiter(
                SAA_inout       Actions&                                        actions,
                SAA_in          const waiter_ptr_t&                             waiter,
                SAA_in_opt      const om::ObjPtrCopyable< ClientConnection >&   connection,
                SAA_in_opt      const std::exception_ptr&                       exception
                )
            {
                Answer answer;

                answer.onReady = waiter -> onReady;
                answer.connection = connection;
                answer.exception = exception;

                actions.answers.push_back( std::move( answer ) );
            }

            /**
             * @brief The whole of the pool's decision making, for one key
             */

            void examineKey(
                SAA_in          const ConnectionKey&                            key,
                SAA_inout       KeyState&                                       ks,
                SAA_inout       Actions&                                        actions
                )
            {
                const auto timeNow = now();

                std::exception_ptr failure;
                bool hasFailedAttempt = false;

                for( auto it = ks.connections.begin(); it != ks.connections.end(); )
                {
                    const auto& entry = *it;

                    if( refreshEntry( entry, timeNow, actions, failure ) )
                    {
                        hasFailedAttempt = true;
                    }

                    if( entry -> isRetired && 0U == entry -> slotsInUse )
                    {
                        forgetConnection( entry );

                        if( m_totalConnections )
                        {
                            --m_totalConnections.lvalue();
                        }

                        it = ks.connections.erase( it );

                        continue;
                    }

                    ++it;
                }

                bool hasLiveConnection = false;

                for( const auto& entry : ks.connections )
                {
                    if( ! entry -> isRetired )
                    {
                        hasLiveConnection = true;

                        break;
                    }
                }

                if( hasFailedAttempt && ! hasLiveConnection )
                {
                    /*
                     * THE HALF OF THE RETRY THE POOL OWNS END TO END. These requests never left
                     * the pool - they were queued behind a connection which failed before it could
                     * carry anything, which is the third limb of design 5.4's "provably
                     * unprocessed": not a byte of them was written. So the pool counts their
                     * attempts itself and replays them on a fresh connection, up to the bound of
                     * design 4.6, after which they fail with the LAST error rather than with an
                     * invented one.
                     *
                     * ONLY WHEN NOTHING IS LEFT TO SERVE THEM. A key which may hold six HTTP/1.1
                     * connections loses them one at a time, and a waiter must not spend an attempt
                     * on a failure which did not actually leave it stuck - it is still queued, and
                     * what it is queued behind is the connection that is still there
                     */

                    for( auto it = ks.waiters.begin(); it != ks.waiters.end(); ++it )
                    {
                        ++( *it ) -> attempts.lvalue();

                        ( *it ) -> lastError = failure;
                    }

                    ++m_stats.establishmentRetries.lvalue();
                }

                while( ! ks.waiters.empty() )
                {
                    const auto waiter = ks.waiters.front();

                    if( ! waiter -> deadline.is_special() && timeNow >= waiter -> deadline )
                    {
                        answerWaiter( actions, waiter, nullptr, makeTimeoutException( key ) );

                        ks.waiters.pop_front();

                        ++m_stats.requestTimeouts.lvalue();
                        ++m_stats.failures.lvalue();

                        continue;
                    }

                    if( waiter -> attempts > m_policy.maxRetriesPerRequest )
                    {
                        answerWaiter(
                            actions,
                            waiter,
                            nullptr,
                            waiter -> lastError ?
                                waiter -> lastError :
                                makeException< UnexpectedException >(
                                    resolveMessage(
                                        BL_MSG()
                                            << "No connection to '"
                                            << key.host
                                            << ":"
                                            << key.port.value()
                                            << "' could be established"
                                        )
                                    )
                            );

                        ks.waiters.pop_front();

                        ++m_stats.failures.lvalue();

                        continue;
                    }

                    const auto entry = findDispatchable( ks, *waiter );

                    if( entry )
                    {
                        ++entry -> slotsInUse.lvalue();
                        ++m_stats.dispatched.lvalue();

                        answerWaiter( actions, waiter, entry -> current(), nullptr );

                        ks.waiters.pop_front();

                        continue;
                    }

                    if( canStartConnection( ks ) )
                    {
                        actions.starts.push_back( insertPlaceholder( key, ks ) );

                        /*
                         * The waiter STAYS queued - it is now queued behind the placeholder, which
                         * is the point of the placeholder
                         */
                    }

                    /*
                     * FIFO ( design 5.4 ): a waiter which cannot be served blocks the ones behind
                     * it. Serving a later one first would be a request overtaking an older one
                     * because its body happened to be replayable
                     */

                    break;
                }

                if( ! ks.waiters.empty() )
                {
                    actions.armMaintenance = true;
                }
                else
                {
                    for( const auto& entry : ks.connections )
                    {
                        if( ! entry -> isReady && ! entry -> isRetired )
                        {
                            actions.armMaintenance = true;

                            break;
                        }
                    }
                }
            }

            void examineAll( SAA_inout Actions& actions )
            {
                for( auto it = m_keys.begin(); it != m_keys.end(); )
                {
                    examineKey( it -> first, it -> second, actions );

                    if( it -> second.connections.empty() && it -> second.waiters.empty() )
                    {
                        it = m_keys.erase( it );

                        continue;
                    }

                    ++it;
                }

                if( actions.armMaintenance && ! m_isMaintenanceArmed )
                {
                    m_isMaintenanceArmed = true;
                }
                else
                {
                    /*
                     * Either there is nothing to watch, or a tick is already pending - and a
                     * pending tick keeps the flag true for its whole run, so this is the only
                     * thread which can be about to arm the timer
                     */

                    actions.armMaintenance = false;
                }
            }

            /*************************************************************************************
             * Everything below runs OUTSIDE the pool lock
             */

            static void deliver(
                SAA_in          const on_ready_callback_t&                      onReady,
                SAA_in_opt      const om::ObjPtrCopyable< ClientConnection >&   connection,
                SAA_in_opt      const std::exception_ptr&                       exception
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                onReady( connection, exception );

                BL_NOEXCEPT_END()
            }

            void post( SAA_in Answer&& answer ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * "acquire( ) never returns a connection: it posts one" - the contract says so and
                 * says why: a pool which answered inline would be calling into the request task
                 * from inside acquire( ), which is where the caller's own lock may still be held
                 */

                ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose )
                    -> aioService().post(
                        cpp::bind(
                            &this_type::deliver,
                            answer.onReady,
                            answer.connection,
                            answer.exception
                            )
                        );

                BL_NOEXCEPT_END()
            }

            void runActions( SAA_inout Actions& actions ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * The driver goes onto the queue BEFORE anyone is told about it, so a request task
                 * which submits the moment it is answered submits to a connection which is running
                 */

                for( const auto& task : actions.schedules )
                {
                    m_eqConnections -> push_back( om::ObjPtrCopyable< tasks::Task >( task ) );
                }

                for( auto& answer : actions.answers )
                {
                    post( std::move( answer ) );
                }

                for( const auto& task : actions.cancels )
                {
                    task -> requestCancel();
                }

                if( actions.armMaintenance )
                {
                    armMaintenance();
                }

                for( const auto& entry : actions.starts )
                {
                    startConnection( entry );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Builds the connection the placeholder stands for - outside the lock ( L4 )
             */

            void startConnection( SAA_in const entry_ptr_t& entry ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                ConnectionAttempt attempt;
                std::exception_ptr failure;

                try
                {
                    attempt = m_factory( entry -> key, m_policy );

                    BL_CHK_T(
                        false,
                        nullptr != attempt.task,
                        UnexpectedException(),
                        BL_MSG()
                            << "A client connection factory created no connection task"
                        );
                }
                catch( std::exception& )
                {
                    failure = std::current_exception();
                }

                Actions actions;

                {
                    BL_MUTEX_GUARD( m_lock );

                    if( failure )
                    {
                        /*
                         * The attempt never started. Retiring the placeholder is what turns it
                         * into a failed attempt on the next examine, which is what retries or
                         * fails the waiters queued behind it - one path for both, rather than a
                         * second copy of the retry rule here
                         */

                        entry -> isRetired = true;

                        if( entry -> attempt.task )
                        {
                            entry -> attempt.task.reset();
                        }

                        markAttemptFailed( entry, failure, actions );
                    }
                    else if( m_isDisposed )
                    {
                        actions.cancels.push_back( attempt.task );
                    }
                    else
                    {
                        entry -> attempt = attempt;

                        entry -> taskConnection = om::ObjPtrCopyable< ClientConnection >(
                            om::tryQI< ClientConnection >( attempt.task )
                            );

                        if( entry -> taskConnection )
                        {
                            m_byConnection[ entry -> taskConnection.get() ] = entry;
                        }

                        entry -> isScheduled = true;

                        actions.schedules.push_back( entry -> attempt.task );

                        examineAll( actions );
                    }
                }

                runActions( actions );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Fails the waiters of a key whose attempt could not even be started
             *
             * Called under the lock. It walks the same path examineKey( ) does for an attempt
             * which failed later, so the bound and the "fail with the last error" rule are stated
             * once
             */

            void markAttemptFailed(
                SAA_in          const entry_ptr_t&                              entry,
                SAA_in          const std::exception_ptr&                       failure,
                SAA_inout       Actions&                                        actions
                )
            {
                const auto it = m_keys.find( entry -> key );

                if( it == m_keys.end() )
                {
                    return;
                }

                for( const auto& waiter : it -> second.waiters )
                {
                    ++waiter -> attempts.lvalue();

                    waiter -> lastError = failure;
                }

                ++m_stats.establishmentRetries.lvalue();

                examineAll( actions );
            }

            void armMaintenance() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_maintenanceTimer -> expires_from_now(
                    time::milliseconds( m_maintenanceIntervalMs.value() )
                    );

                m_maintenanceTimer -> async_wait(
                    cpp::bind(
                        &this_type::onMaintenance,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief The tick - the only thing which LOOKS at what nothing reports
             */

            void onMaintenance( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( ec )
                {
                    /*
                     * Cancelled, which is what dispose( ) does to it. The flag stays true and
                     * nothing re-arms, which is what dispose( ) wants
                     */

                    return;
                }

                Actions actions;

                {
                    BL_MUTEX_GUARD( m_lock );

                    if( m_isDisposed )
                    {
                        m_isMaintenanceArmed = false;

                        return;
                    }

                    /*
                     * The flag is held true for the whole run, so no other thread can decide to
                     * arm the timer while this handler is deciding whether to re-arm it
                     */

                    examineAll( actions );

                    if( actions.empty() )
                    {
                        const auto doubled = m_maintenanceIntervalMs * 2L;

                        m_maintenanceIntervalMs = std::min< long >(
                            doubled,
                            ConnectionPoolPolicy::MAX_MAINTENANCE_INTERVAL_IN_MILLISECONDS
                            );
                    }
                    else
                    {
                        m_maintenanceIntervalMs =
                            ConnectionPoolPolicy::MIN_MAINTENANCE_INTERVAL_IN_MILLISECONDS;
                    }

                    if( hasWorkToWatch() )
                    {
                        actions.armMaintenance = true;
                    }
                    else
                    {
                        m_isMaintenanceArmed = false;

                        actions.armMaintenance = false;
                    }
                }

                runActions( actions );

                BL_NOEXCEPT_END()
            }

            bool hasWorkToWatch() const NOEXCEPT
            {
                for( const auto& pair : m_keys )
                {
                    if( ! pair.second.waiters.empty() )
                    {
                        return true;
                    }

                    for( const auto& entry : pair.second.connections )
                    {
                        if( ! entry -> isReady && ! entry -> isRetired )
                        {
                            return true;
                        }
                    }
                }

                return false;
            }

            /**
             * @brief The disposal of design 5.4
             *
             * FROM THE DESTRUCTOR IT ANSWERS NOBODY, and that is not a shortcut. A request which is
             * waiting holds the pool it is waiting on - that is what an om::ObjPtr is for - so a
             * pool which is being destroyed has no waiter anyone is still listening for, and the
             * queue it is dropping belongs to callers which are already gone. Posting there would
             * be worse than useless: the destructor of the last reference can run during process
             * teardown, when the thread pool it would post to has already been disposed, and
             * asking a disposed thread pool for its io_service is fatal rather than an error
             */

            void disposeInternal( SAA_in const bool isFromDestructor = false ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                std::vector< Answer > answers;
                std::vector< om::ObjPtrCopyable< tasks::Task > > tasks;

                {
                    BL_MUTEX_GUARD( m_lock );

                    if( m_isDisposed )
                    {
                        return;
                    }

                    m_isDisposed = true;

                    /*
                     * Design 5.4's disposal: the queued requests are FAILED with operation_aborted
                     * rather than left waiting for a connection which is never coming
                     */

                    for( auto& pair : m_keys )
                    {
                        if( isFromDestructor )
                        {
                            if( ! pair.second.waiters.empty() )
                            {
                                BL_LOG(
                                    Logging::warning(),
                                    BL_MSG()
                                        << "A connection pool was destroyed without being disposed, "
                                        << "dropping "
                                        << pair.second.waiters.size()
                                        << " request(s) queued for '"
                                        << pair.first.host
                                        << "'"
                                    );
                            }
                        }
                        else
                        {
                            for( const auto& waiter : pair.second.waiters )
                            {
                                Answer answer;

                                answer.onReady = waiter -> onReady;
                                answer.exception = makeAbortedException();

                                answers.push_back( std::move( answer ) );
                            }
                        }

                        pair.second.waiters.clear();

                        for( const auto& entry : pair.second.connections )
                        {
                            if( entry -> attempt.task )
                            {
                                tasks.push_back( entry -> attempt.task );
                            }
                        }

                        pair.second.connections.clear();
                    }

                    m_keys.clear();
                    m_byConnection.clear();

                    m_totalConnections = 0U;
                }

                for( auto& answer : answers )
                {
                    post( std::move( answer ) );
                }

                {
                    eh::error_code ec;

                    m_maintenanceTimer -> cancel( ec );
                }

                /*
                 * The connection queue is flushed the way TcpServerBase flushes its own: cancel
                 * everything, do not wait for it here, and let the queue's own disposal join. A
                 * connection which is cancelled sends no GOAWAY - a request cancel does not end an
                 * h2 connection but a TASK cancel does, and there is no public door on a driver
                 * for "say GOAWAY and close" ( closeGracefully( ) is the driver's own, taken from
                 * its idle timer, its drained GOAWAY and its last stream ). What the pool does
                 * have is the idle lifetime it configures, which is the graceful path for an
                 * ordinary connection; disposal is the abrupt one, and it says so
                 */

                for( const auto& task : tasks )
                {
                    task -> requestCancel();
                }

                if( m_eqConnections )
                {
                    m_eqConnections -> forceFlushNoThrow( false /* wait */ );

                    m_eqConnections.reset();
                }

                BL_NOEXCEPT_END()
            }

        public:

            /*************************************************************************************
             * httpclient::ConnectionPool
             */

            virtual void acquire(
                SAA_in          const ConnectionKey&                            key,
                SAA_in          const ClientRequest&                            request,
                SAA_in          on_ready_callback_t&&                           onReady
                ) OVERRIDE
            {
                BL_CHK_T(
                    false,
                    !! onReady,
                    ArgumentException(),
                    BL_MSG()
                        << "A connection cannot be acquired without a callback to answer"
                    );

                auto waiter = std::make_shared< Waiter >();

                waiter -> request = request;
                waiter -> onReady = BL_PARAM_FWD( onReady );

                const auto& timeout = isEnabled( request.totalTimeout() ) ?
                    request.totalTimeout() : m_policy.requestTimeout;

                if( isEnabled( timeout ) )
                {
                    waiter -> deadline = now() + timeout;
                }

                Actions actions;

                {
                    BL_MUTEX_GUARD( m_lock );

                    if( m_isDisposed )
                    {
                        Answer answer;

                        answer.onReady = waiter -> onReady;
                        answer.exception = makeAbortedException();

                        actions.answers.push_back( std::move( answer ) );
                    }
                    else
                    {
                        m_keys[ key ].waiters.push_back( waiter );

                        examineAll( actions );
                    }
                }

                runActions( actions );
            }

            virtual void releaseStream(
                SAA_in          const om::ObjPtr< ClientConnection >&           connection,
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const RequestOutcome                            outcome
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                Actions actions;

                {
                    BL_MUTEX_GUARD( m_lock );

                    const auto it = connection ?
                        m_byConnection.find( connection.get() ) : m_byConnection.end();

                    if( it != m_byConnection.end() )
                    {
                        const auto entry = it -> second;

                        if( entry -> slotsInUse )
                        {
                            --entry -> slotsInUse.lvalue();
                        }

                        ++m_stats.released.lvalue();

                        if( RequestOutcome::ConnectionUnusable == outcome )
                        {
                            if( ! entry -> isRetired )
                            {
                                entry -> isRetired = true;

                                ++m_stats.connectionsRetired.lvalue();
                            }
                        }

                        BL_LOG(
                            Logging::trace(),
                            BL_MSG()
                                << "The pool took back stream "
                                << handle
                                << " on a connection to '"
                                << entry -> key.host
                                << ":"
                                << entry -> key.port.value()
                                << "'"
                            );
                    }

                    examineAll( actions );
                }

                runActions( actions );

                BL_NOEXCEPT_END()
            }

            /*************************************************************************************
             * om::Disposable
             */

            virtual void dispose() OVERRIDE
            {
                disposeInternal( false /* isFromDestructor */ );
            }

            /*************************************************************************************
             * What a case can ask
             */

            auto stats() const NOEXCEPT -> Stats
            {
                BL_MUTEX_GUARD( m_lock );

                return m_stats;
            }

            std::size_t connectionCount() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                return m_totalConnections;
            }

            std::size_t waiterCount() const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                std::size_t count = 0U;

                for( const auto& pair : m_keys )
                {
                    count += pair.second.waiters.size();
                }

                return count;
            }

            std::size_t slotsInUse( SAA_in const om::ObjPtr< ClientConnection >& connection ) const NOEXCEPT
            {
                BL_MUTEX_GUARD( m_lock );

                const auto it = connection ?
                    m_byConnection.find( connection.get() ) : m_byConnection.end();

                return it == m_byConnection.end() ? 0U : it -> second -> slotsInUse.value();
            }

            auto policy() const NOEXCEPT -> const ConnectionPoolPolicy&
            {
                return m_policy;
            }
        };

        typedef om::ObjectImpl< ConnectionPoolImplT<> >                         ConnectionPoolImpl;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_CONNECTIONPOOL_H_ */
