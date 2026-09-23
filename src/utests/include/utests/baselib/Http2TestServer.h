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

#ifndef __UTEST_HTTP2TESTSERVER_H_
#define __UTEST_HTTP2TESTSERVER_H_

#include <baselib/http2/Session.h>
#include <baselib/http2/Globals.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/http2/StreamStateMachine.h>

#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TasksUtils.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

/*
 * The HTTP/2 test peer of notes/plans/http2-design.md 8.2 (D8), slice S4.4
 *
 * This is a TEST PEER AND NOT A SERVER. It has no DoS hardening, no connection caps worth the
 * name, no request routing and no back end - production HTTP/2 in HttpServer is deferred, see
 * notes/plans/issues/http2-server-side-deferral.md. What it is, is the first consumer of the
 * SERVER role of http2::Session, which S3.1 wrote role-neutral precisely so that this could exist;
 * a client driven against it is therefore driven against an independent USE of the protocol core
 * and not against an independent implementation of it. Design 8.4 says how that gap is closed
 *
 * THIS HEADER LIVES UNDER src/utests/include AND MUST NEVER BE INCLUDED FROM src/include. Only
 * test modules see it
 *
 * ----------------------------------------------------------------------------------------------
 * WHAT A CASE WRITES
 * ----------------------------------------------------------------------------------------------
 *
 * A case installs a RESPONDER, which is handed each request as it arrives and returns the ordered
 * list of things the peer should then do on that stream:
 *
 *     peer -> setResponder(
 *         []( SAA_in const Http2TestRequest& request ) -> Http2ResponseScript
 *         {
 *             return Http2ResponseScript()
 *                 .interim( 103 )
 *                 .headers( 200 )
 *                 .delay( 50 )
 *                 .data( "hello" )
 *                 .trailers( trailerFields );
 *         }
 *         );
 *
 * A step list rather than a bag of knobs because the case should read as the sequence of things
 * the peer does; a callback rather than a fixed list because "refuse the first two streams and
 * then serve" is then written with no second mechanism
 *
 * ----------------------------------------------------------------------------------------------
 * THE RENDEZVOUS, AND WHY IT IS NOT OPTIONAL
 * ----------------------------------------------------------------------------------------------
 *
 * The peer records what it did on its own I/O threads and the case asserts on those records. That
 * is exactly the shape which cost this project a 1-in-16 flake in S3.5, where a case read
 * FakeProxy::records() straight after waiting for the CLIENT task while the proxy's own worker had
 * yet to make its last record. Nothing was torn - records() is mutex guarded - what was missing
 * was the happens-before
 *
 * So the rendezvous is built in here from the start and copied from that file: record( ) notifies
 * a condition variable under the same lock which appends, waitForRecords( ) is bounded so a peer
 * which never gets there fails the case instead of hanging the suite, and waitForRecordsOf( )
 * reports both counts and the responder's own failure text on expiry. IF YOU FIND YOURSELF ADDING
 * A POLL LOOP OR A SLEEP BEFORE AN ASSERTION, the harness is missing a rendezvous and that is what
 * to fix
 *
 * ----------------------------------------------------------------------------------------------
 * THREADING
 * ----------------------------------------------------------------------------------------------
 *
 * http2::Session is single-threaded by contract. Here the TASK LOCK is what provides that: every
 * touch of the session happens inside a handler body or inside scheduleTask( ), and TaskBase holds
 * m_lock across both. The responder runs there too, so a responder must not use the Boost.Test
 * assertion macros - it records, or it throws, and the case asserts afterwards
 */

namespace utest
{
    namespace h2peer
    {
        /**
         * @brief What one step of a response script does
         */

        enum class Http2StepKind : std::uint8_t
        {
            Interim,            /* a 1xx header block, never ending the stream */
            Headers,            /* the final header section */
            Data,               /* a body chunk, placed as the windows allow */
            Trailers,           /* a trailer section, which ends the stream */
            EndStream,          /* an empty DATA frame carrying END_STREAM */
            Delay,              /* wait, without stopping the connection reading */
            Refuse,             /* RST_STREAM this stream - nothing further is sent ON IT */
            AwaitWindowStall,   /* wait until the receive window for this stream is exhausted */
            AwaitStreamClosed,  /* wait until BOTH halves of this stream have closed */
            AwaitRequests,      /* wait until the connection has seen this many requests */
            CreditWindow,       /* credit everything received so far on this stream */
            GoAway,             /* end the connection gracefully - RFC 9113 6.8 */
            Close,              /* close the connection once everything queued is written */
        };

        /**
         * @brief One step, flat rather than a hierarchy - the peer switches on the kind
         */

        struct Http2Step
        {
            bl::cpp::ScalarTypeIniter< Http2StepKind >                          kind;
            bl::cpp::ScalarTypeIniter< unsigned >                               status;
            bl::cpp::ScalarTypeIniter< std::uint32_t >                          errorCode;
            bl::cpp::ScalarTypeIniter< long >                                   delayInMilliseconds;
            bl::cpp::ScalarTypeIniter< std::uint32_t >                          requestCount;
            bl::cpp::ScalarTypeIniter< bool >                                   endStream;

            bl::http2::HpackFieldList                                           fields;
            std::string                                                         data;
        };

        /**
         * @brief class Http2ResponseScriptT - the ordered list of things the peer does on a stream
         *
         * A value type with a fluent builder, so a case reads as the sequence itself. Every method
         * returns *this, and the list is executed in order by the connection task
         *
         * TWO ORDERING RULES THE PEER ENFORCES FOR YOU, stated here because they are what makes
         * the naive spelling of a script correct:
         *
         *  - a body chunk is placed only as far as the flow-control windows allow, and the script
         *    resumes by itself when a WINDOW_UPDATE arrives - a case never has to ask
         *  - trailers are held back until the body queued before them has actually been written.
         *    Session::produce( ) writes header blocks BEFORE data, so trailers queued while a body
         *    is still pending would go out in front of it
         *
         * AND ONE THE PEER CANNOT ENFORCE FOR YOU, because only the case knows whether it wants
         * it: closeConnection( ) tears the connection down as soon as what the PEER queued has
         * been written, and says nothing about what the CLIENT is still sending. On a script whose
         * request half may still be open - any case which uploads a body - that close can land
         * while the client is mid-upload, and then the rest of the request, its END_STREAM and the
         * stream closure never arrive at all. This is the trailers rule one level up: the script
         * advancing past something the other side has not finished.
         *
         * awaitStreamClosed( ) before closeConnection( ) is the rendezvous for it, and it is an
         * EVENT and not a duration for the same reason awaitWindowStall( ) is. A case whose client
         * sends its whole request in the HEADERS - every GET - does not need it: that stream's
         * request half was closed before the responder ever ran.
         */

        template
        <
            typename E = void
        >
        class Http2ResponseScriptT
        {
        public:

            typedef Http2ResponseScriptT< E >                                   this_type;

            /**
             * @brief An interim (1xx) response - RFC 9113 8.1, design 8.2
             */

            this_type& interim(
                SAA_in              const unsigned                              status,
                SAA_in_opt          const bl::http2::HpackFieldList&            fields =
                                        bl::http2::HpackFieldList()
                )
            {
                Http2Step step;

                step.kind = Http2StepKind::Interim;
                step.status = status;
                step.fields = fields;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief The final header section; 'endStream' makes it the whole response
             */

            this_type& headers(
                SAA_in_opt          const unsigned                              status = 200U,
                SAA_in_opt          const bl::http2::HpackFieldList&            fields =
                                        bl::http2::HpackFieldList(),
                SAA_in_opt          const bool                                  endStream = false
                )
            {
                Http2Step step;

                step.kind = Http2StepKind::Headers;
                step.status = status;
                step.fields = fields;
                step.endStream = endStream;

                m_steps.push_back( step );

                return *this;
            }

            this_type& data( SAA_in const std::string& data )
            {
                Http2Step step;

                step.kind = Http2StepKind::Data;
                step.data = data;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief A trailer section - it carries END_STREAM, so nothing may follow it
             */

            this_type& trailers( SAA_in const bl::http2::HpackFieldList& fields )
            {
                Http2Step step;

                step.kind = Http2StepKind::Trailers;
                step.fields = fields;

                m_steps.push_back( step );

                return *this;
            }

            this_type& endStream()
            {
                Http2Step step;

                step.kind = Http2StepKind::EndStream;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Waits before the next step, while the connection goes on reading
             */

            this_type& delay( SAA_in const long delayInMilliseconds )
            {
                Http2Step step;

                step.kind = Http2StepKind::Delay;
                step.delayInMilliseconds = delayInMilliseconds;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief RST_STREAM this stream - REFUSED_STREAM by default, which is what the retry
             * budget of D6 is exercised with
             *
             * It ends the STREAM and not the script: a connection-level step which follows it,
             * goAway( ) or closeConnection( ), still runs. Nothing addressed to the stream may
             * follow, and the session refuses it loudly if one does
             */

            this_type& refuse(
                SAA_in_opt          const std::uint32_t                         errorCode =
                                        bl::http2::Globals::ERROR_CODE_REFUSED_STREAM
                )
            {
                Http2Step step;

                step.kind = Http2StepKind::Refuse;
                step.errorCode = errorCode;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Waits until the client has actually run out of window on this stream
             *
             * THE STALL IS AN EVENT AND NOT A DURATION, and this is what makes a window-stall case
             * deterministic rather than a race with a timer: the peer's receive window falls to
             * zero at the exact octet the client can no longer go past, so a script which waits
             * for that and only then credits has stalled the client for certain, and has credited
             * a number the case can name. A delay long enough to "probably" stall it instead
             * credits however much happened to have arrived
             *
             * Nothing polls: every DATA frame advances the script again, so the step is re-read
             * exactly when the answer can have changed
             *
             * Only meaningful together with setWithholdWindowUpdates( true ), which is what stops
             * the window from being credited as the body arrives
             */

            this_type& awaitWindowStall()
            {
                Http2Step step;

                step.kind = Http2StepKind::AwaitWindowStall;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Waits until both halves of this stream have actually closed
             *
             * THE RENDEZVOUS FOR A SCRIPT WHICH CLOSES THE CONNECTION UNDER AN UPLOAD. The peer's
             * own endStream( ) closes only the RESPONSE half; the request half closes when the
             * client's END_STREAM arrives, on the client's schedule and not on the script's. A
             * closeConnection( ) placed straight after endStream( ) therefore races the client's
             * upload, and under load it wins - the connection goes down mid-upload and the request
             * body, its END_STREAM and the stream closure are never seen. Put this between them
             * and the close waits for the thing a case like that goes on to assert.
             *
             * An EVENT and not a duration, exactly like awaitWindowStall( ): the step re-reads on
             * every read AND every write, which is when the answer can have changed, and nothing
             * polls. The condition is the session's own StreamClosed for this stream - the same
             * record requireStreamClosedAtPeer( ) waits for, so the script waits on precisely what
             * the case asserts
             *
             * Harmless where it is not needed: a GET's request half is closed before the responder
             * runs, so the step passes as soon as the peer's own half does
             */

            this_type& awaitStreamClosed()
            {
                Http2Step step;

                step.kind = Http2StepKind::AwaitStreamClosed;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Waits until this connection has seen at least 'count' requests
             *
             * THE RENDEZVOUS FOR A CASE ABOUT CONCURRENCY, and it exists because the naive
             * spelling of such a case cannot fail. A peer which answers the first request before
             * the second one arrives lets two requests which were meant to overlap run one after
             * the other, and nothing a case asserts AFTERWARDS can tell the two apart - both
             * finish, both are correct, and the client never had two streams open. Put this in
             * front of the first response and the overlap is forced rather than hoped for: the
             * stream this script is on cannot close until the request it waits for has arrived,
             * so the case either sees two streams open at the peer at once or it times out here
             * and prints what the peer did see.
             *
             * An EVENT and not a duration, like awaitWindowStall( ) and awaitStreamClosed( ):
             * onRead( ) advances every script after every feed( ), and a request arriving IS a
             * read, so the step is re-read exactly when its answer can have changed. Nothing
             * polls and nothing sleeps.
             *
             * The count is requests ON THIS CONNECTION, the same number Http2TestRequest::
             * streamIndex carries, so a case which expects the second request names two
             */

            this_type& awaitRequests( SAA_in const std::uint32_t count )
            {
                Http2Step step;

                step.kind = Http2StepKind::AwaitRequests;
                step.requestCount = count;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Credits the flow-control window for everything received so far on the stream
             *
             * Only meaningful together with setWithholdWindowUpdates( true ), which is what makes
             * the peer stall an upload in the first place: this is how the stall is released
             */

            this_type& creditWindow()
            {
                Http2Step step;

                step.kind = Http2StepKind::CreditWindow;

                m_steps.push_back( step );

                return *this;
            }

            this_type& goAway(
                SAA_in_opt          const std::uint32_t                         errorCode =
                                        bl::http2::Globals::ERROR_CODE_NO_ERROR,
                SAA_in_opt          const std::string&                          debugData =
                                        std::string()
                )
            {
                Http2Step step;

                step.kind = Http2StepKind::GoAway;
                step.errorCode = errorCode;
                step.data = debugData;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Closes the connection once everything already queued has been written
             */

            this_type& closeConnection()
            {
                Http2Step step;

                step.kind = Http2StepKind::Close;

                m_steps.push_back( step );

                return *this;
            }

            const std::vector< Http2Step >& steps() const NOEXCEPT
            {
                return m_steps;
            }

        private:

            std::vector< Http2Step >                                            m_steps;
        };

        typedef Http2ResponseScriptT<> Http2ResponseScript;

        /**
         * @brief What the responder is given - one request, as the peer's session decoded it
         *
         * 'streamIndex' counts requests on this connection from one, which is what a responder
         * that refuses the first N streams and then serves is written against; the stream
         * identifier is the wire one and is not a count
         */

        struct Http2TestRequest
        {
            bl::cpp::ScalarTypeIniter< std::uint32_t >                          streamId;
            bl::cpp::ScalarTypeIniter< std::uint32_t >                          streamIndex;
            bl::cpp::ScalarTypeIniter< bool >                                   hasBody;

            std::string                                                         method;
            std::string                                                         scheme;
            std::string                                                         authority;
            std::string                                                         path;

            bl::http2::HpackFieldList                                           fields;

            /**
             * @brief The first value of a field, or an empty string - the responder's convenience
             */

            std::string valueOf( SAA_in const std::string& name ) const
            {
                for( std::size_t i = 0U; i < fields.size(); ++i )
                {
                    if( fields[ i ].name() == name )
                    {
                        return fields[ i ].value();
                    }
                }

                return std::string();
            }
        };

        typedef bl::cpp::function
            <
                Http2ResponseScript ( SAA_in const Http2TestRequest& request )
            >
            responder_t;

        /**
         * @brief class Http2TestRecorderT - what the peer did, and the rendezvous a count
         * assertion needs
         *
         * Shared by the acceptor and by every connection it makes, through a shared_ptr rather
         * than through the object model: a connection task holding a reference to its server would
         * be a cycle, and a cycle which survives a leaked connection is reported as a leak
         *
         * THE CONDITION VARIABLE IS THE POINT. It is notified inside record( ), under the same
         * lock which appends, so a case which waits on it has a happens-before with the I/O thread
         * that made the record - see the header comment for what its absence cost
         */

        template
        <
            typename E = void
        >
        class Http2TestRecorderT
        {
            BL_NO_COPY_OR_MOVE( Http2TestRecorderT )

        public:

            Http2TestRecorderT() = default;

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );

                m_cvRecorded.notify_all();
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            /**
             * @brief Blocks until the peer has made 'expected' records, or the bound expires
             *
             * Bounded on purpose: a peer which genuinely never gets there has to fail the case
             * with a diagnosis rather than hang the suite
             *
             * @return true when 'expected' records had arrived before the bound expired
             */

            bool waitForRecords(
                SAA_in              const std::size_t                           expected,
                SAA_in              const std::size_t                           timeoutInMilliseconds
                ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cvRecorded.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this, expected ]() -> bool
                    {
                        return m_records.size() >= expected;
                    }
                    );
            }

            /**
             * @brief What a responder threw, if anything; empty when every one of them returned
             *
             * READ IT AFTER waitForRecordsOf, for the same reason records() is read there: it is
             * written on an I/O thread and nothing orders a client's task finishing against the
             * peer leaving a connection
             */

            auto failure() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_failure;
            }

            void recordFailure( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                if( m_failure.empty() )
                {
                    m_failure = BL_PARAM_FWD( what );
                }

                /*
                 * A responder which threw will normally never make the record the case is waiting
                 * for, so the wait has to be woken here too - otherwise the diagnosis arrives only
                 * after the full timeout has been paid
                 */

                m_cvRecorded.notify_all();
            }

            /**
             * @brief Everything the peer received as a request body on this stream
             *
             * Keyed by the wire stream identifier and NOT by connection, because a test peer
             * belongs to one case; two connections of one peer which both use stream 1 would
             * append into one string, and a case which needs to tell them apart should use one
             * peer per connection
             */

            auto bodyOf( SAA_in const std::uint32_t streamId ) const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                const auto it = m_bodies.find( streamId );

                return it == m_bodies.end() ? std::string() : it -> second;
            }

            void appendBody(
                SAA_in              const std::uint32_t                         streamId,
                SAA_in              const std::string&                          data
                )
            {
                BL_MUTEX_GUARD( m_lock );

                m_bodies[ streamId ].append( data );
            }

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cvRecorded;

            std::vector< std::string >                                          m_records;
            std::map< std::uint32_t, std::string >                              m_bodies;
            std::string                                                         m_failure;
        };

        typedef Http2TestRecorderT<> Http2TestRecorder;

        /**
         * @brief Waits for the peer to have recorded 'expected' entries, and says what it did
         * record when it has not
         *
         * EVERY ASSERTION ON A RECORD COUNT IS PRECEDED BY THIS. The failure names both counts
         * because "EQUAL( records.size(), 2U ) has failed" is what this race looked like for as
         * long as it went undiagnosed in S3.5, and it names what a responder threw because a
         * responder which throws never makes the record the case was waiting for - so a thrown
         * responder arrives HERE, and this failure is fatal, which means the case's own failure()
         * check is never reached to report it
         */

        inline void waitForRecordsOf(
            SAA_in              const Http2TestRecorder&                        recorder,
            SAA_in              const std::size_t                               expected
            )
        {
            /*
             * Generous, because it is only ever reached when something is already wrong: every
             * wait here is satisfied in milliseconds when the peer is behaving
             */

            if( recorder.waitForRecords( expected, 15U * 1000U /* timeoutInMilliseconds */ ) )
            {
                return;
            }

            const auto failure = recorder.failure();
            const auto records = recorder.records();

            std::string what;

            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                what += "\n    ";
                what += records[ i ];
            }

            UTF_FAIL(
                BL_MSG()
                    << "The HTTP/2 test peer recorded "
                    << records.size()
                    << " entries where the case expects "
                    << expected
                    << "; it did not reach the end of its script in time"
                    << ( failure.empty() ? std::string() : " - a responder failed with: " + failure )
                    << what
                );
        }

        /**
         * @brief Everything a connection of this peer is configured with, copied into it when it
         * is created
         *
         * A value type, so a connection never reads a member of the acceptor while the case is
         * writing one. Set on the acceptor before it is scheduled
         */

        struct Http2TestPeerConfig
        {
            responder_t                                                         responder;
            bl::http2::Http2Profile                                             profile;

            /*
             * After this many requests the peer sends GOAWAY once it has answered them; zero
             * never does
             */

            bl::cpp::ScalarTypeIniter< std::uint32_t >                          goAwayAfterStreams;
            bl::cpp::ScalarTypeIniter< std::uint32_t >                          goAwayErrorCode;

            /*
             * When set the peer never reports a received body as consumed, so no WINDOW_UPDATE is
             * ever sent for it and an upload stalls once the windows close - which is what a
             * window stall IS, from the client's point of view. A script releases it with
             * creditWindow( )
             */

            bl::cpp::ScalarTypeIniter< bool >                                   withholdWindowUpdates;

            /*
             * How long the peer waits before writing its own opening SETTINGS - a peer which
             * accepts the connection and then says nothing
             */

            bl::cpp::ScalarTypeIniter< long >                                   openingDelayInMilliseconds;
        };

        /**
         * @brief class Http2TestConnectionT - one connection of the peer, over a server-role
         * http2::Session
         *
         * MultiOperationTaskT and not a plain task, because a read, a write and a step timer are
         * outstanding together and the handler macros of TaskBase.h assume one operation in
         * flight; design 3.2 wrote that accounting for exactly this shape. Every operation is
         * begun with beginOperation( ) and ends with BL_TASKS_HANDLER_END_MULTIOP( ), the task has
         * one terminal path, and the deliberate end of a connection is beginClose( )
         */

        template
        <
            typename STREAM
        >
        class Http2TestConnectionT :
            public bl::tasks::MultiOperationTaskT< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( Http2TestConnectionT )

        public:

            typedef Http2TestConnectionT< STREAM >                              this_type;
            typedef bl::tasks::MultiOperationTaskT< STREAM >                    base_type;

            enum : std::size_t
            {
                READ_BUFFER_SIZE = 16U * 1024U,
            };

        protected:

            /**
             * @brief Where one stream's script has got to
             */

            struct StreamScript
            {
                Http2ResponseScript                                             script;
                bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >              timer;

                std::size_t                                                     nextStep = 0U;
                std::size_t                                                     dataOffset = 0U;
                std::size_t                                                     received = 0U;

                bool                                                            isDelaying = false;
                bool                                                            isDone = false;

                /*
                 * Set when the session reports this stream closed - what awaitStreamClosed( )
                 * waits for. It is remembered rather than re-derived because the session drops a
                 * closed stream, so there is nothing left to ask afterwards
                 */

                bool                                                            isClosedAtPeer = false;
            };

            const std::shared_ptr< Http2TestRecorder >                          m_recorder;
            const Http2TestPeerConfig                                           m_config;

            bl::http2::Session                                                  m_session;

            std::vector< std::uint8_t >                                         m_readBuffer;
            bl::http2::Session::wire_buffer_t                                   m_writeBuffer;

            std::map< std::uint32_t, StreamScript >                             m_scripts;

            bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >                  m_openingTimer;

            bool                                                                m_isWriteInFlight = false;
            bool                                                                m_isCloseWhenDrained = false;

            /*
             * Nothing is written until the opening delay has passed. Without this the delay would
             * be undone by the first thing the client said: the read is armed straight away, and
             * every read path ends in pumpWrites( ), so the SETTINGS this peer was told to sit on
             * would go out the moment the client's preface arrived
             */

            bool                                                                m_isWriteAllowed = false;

            std::uint32_t                                                       m_streamsSeen = 0U;

            Http2TestConnectionT(
                SAA_in              typename STREAM::stream_ref&&               connectedStream,
                SAA_in              const std::shared_ptr< Http2TestRecorder >& recorder,
                SAA_in              const Http2TestPeerConfig&                  config
                )
                :
                m_recorder( recorder ),
                m_config( config ),
                m_session(
                    bl::http2::StreamRole::Server,
                    bl::time::microsec_clock::universal_time(),
                    config.profile
                    ),
                m_readBuffer( READ_BUFFER_SIZE )
            {
                base_type::attachStream( BL_PARAM_FWD( connectedStream ) );

                base_type::isCloseStreamOnTaskFinish( true );
            }

            static auto now() -> bl::time::ptime
            {
                return bl::time::microsec_clock::universal_time();
            }

            void record( SAA_in std::string&& what )
            {
                m_recorder -> record( BL_PARAM_FWD( what ) );
            }

            /**
             * @brief A timer on the stream's own executor
             *
             * Design 3.1 (D13): every operation of a connection - the reads, the writes and the
             * timers alike - is built on the one executor the stream was constructed on, so that a
             * stranded stream policy serializes all of them without a handler being wrapped by
             * hand. It also settles the timer's lifetime, which is what
             * notes/plans/issues/multioperation-probe-timer-teardown-record.md is about
             */

            auto makeTimer() -> bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >
            {
                return bl::cpp::SafeUniquePtr< bl::asio::deadline_timer >::attach(
                    new bl::asio::deadline_timer(
                        #if ( ( BOOST_VERSION / 100 ) >= 1072 )
                        base_type::getSocket().get_executor()
                        #else
                        base_type::getSocket().get_io_service()
                        #endif
                        )
                    );
            }

            /*************************************************************************************
             * The write pump - one write in flight, and everything the session has to say goes
             * into it
             */

            void pumpWrites()
            {
                using namespace bl;

                if( m_isWriteInFlight || ! m_isWriteAllowed || base_type::isClosing() )
                {
                    return;
                }

                if( ! m_session.wantsWrite() )
                {
                    if( m_isCloseWhenDrained )
                    {
                        record( "closed the connection" );

                        base_type::beginClose();
                    }

                    return;
                }

                m_writeBuffer.clear();

                m_session.produce( m_writeBuffer, now() );

                if( m_writeBuffer.empty() )
                {
                    return;
                }

                m_isWriteInFlight = true;

                base_type::beginOperation();

                asio::async_write(
                    base_type::getStream(),
                    asio::buffer( &m_writeBuffer[ 0 ], m_writeBuffer.size() ),
                    cpp::bind(
                        &this_type::onWrite,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred
                        )
                    );
            }

            void onWrite(
                SAA_in              const bl::eh::error_code&                   ec,
                SAA_in              const std::size_t                           bytesTransferred
                ) NOEXCEPT
            {
                BL_UNUSED( bytesTransferred );

                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                m_isWriteInFlight = false;

                /*
                 * A stream which the peer itself ended is reported closed by the produce( ) that
                 * wrote the last frame of it, so the queue is drained HERE as well as on the read
                 * path - otherwise that closure would wait for whatever the client says next, and
                 * a client which says nothing more would never see it recorded
                 */

                drainEvents();

                /*
                 * The scripts are advanced again HERE and not only on the read path, because this
                 * is where "the body queued before the trailers has actually gone out" becomes
                 * true - see the ordering rules on Http2ResponseScriptT
                 */

                advanceScripts();

                pumpWrites();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************************
             * The read pump
             */

            void scheduleRead()
            {
                using namespace bl;

                if( base_type::isClosing() )
                {
                    return;
                }

                base_type::beginOperation();

                base_type::getStream().async_read_some(
                    asio::buffer( &m_readBuffer[ 0 ], m_readBuffer.size() ),
                    cpp::bind(
                        &this_type::onRead,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred
                        )
                    );
            }

            /**
             * @brief Whether an error code means the client ended the conversation
             *
             * operation_aborted is deliberately NOT here: it is what our own initiateClose( ) and
             * an external cancelTask( ) produce, and the accounting of design 3.2 tells those two
             * apart. Calling either of them "the client closed" would lose that distinction
             */

            bool isPeerClosed( SAA_in const bl::eh::error_code& ec )
            {
                using namespace bl;

                /*
                 * The same question the driver asks, and asked the same way - see core/NetUtils.h.
                 * This peer has reads outstanding exactly as the driver does, so a client which
                 * closes while one is pending renames the close here too, and a hand-written list
                 * of codes here would be a harness that fails intermittently on Windows while the
                 * product code it is testing does not
                 */

                return net::isPeerClosedErrorCode( ec ) || base_type::isStreamTruncationError( ec );
            }

            void onRead(
                SAA_in              const bl::eh::error_code&                   ec,
                SAA_in              const std::size_t                           bytesTransferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ec )
                {
                    if( isPeerClosed( ec ) )
                    {
                        record( "the client closed the connection" );

                        base_type::beginClose();
                    }
                    else
                    {
                        BL_TASKS_HANDLER_CHK_EC( ec );
                    }
                }
                else
                {
                    m_session.feed( &m_readBuffer[ 0 ], bytesTransferred, now() );

                    drainEvents();

                    advanceScripts();

                    pumpWrites();

                    scheduleRead();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************************
             * The event queue of the session, drained after every feed( )
             */

            void drainEvents()
            {
                using namespace bl::http2;

                while( m_session.hasEvents() )
                {
                    const auto& event = m_session.frontEvent();

                    switch( event.type.value() )
                    {
                        case SessionEventType::Headers:

                            if( event.isTrailers )
                            {
                                record(
                                    "request trailers on stream " +
                                    bl::utils::lexical_cast< std::string >( event.streamId.value() )
                                    );
                            }
                            else
                            {
                                onRequest( event );
                            }

                            if( event.endStream )
                            {
                                onRequestBodyEnd( event.streamId );
                            }

                            break;

                        case SessionEventType::Data:

                            onRequestData( event.streamId, event.data );

                            if( event.endStream )
                            {
                                onRequestBodyEnd( event.streamId );
                            }

                            break;

                        case SessionEventType::StreamClosed:

                            record(
                                "stream " +
                                bl::utils::lexical_cast< std::string >( event.streamId.value() ) +
                                " closed with error " +
                                bl::utils::lexical_cast< std::string >( event.errorCode.value() )
                                );

                            /*
                             * Whatever the error code: awaitStreamClosed( ) is a rendezvous on the
                             * stream being over, and a stream the client reset is over too - a
                             * script which waited only for a clean closure would hang on one
                             */

                            {
                                const auto it = m_scripts.find( event.streamId );

                                if( it != m_scripts.end() )
                                {
                                    it -> second.isClosedAtPeer = true;
                                }
                            }

                            break;

                        case SessionEventType::GoAwayReceived:

                            record(
                                "the client sent GOAWAY with error " +
                                bl::utils::lexical_cast< std::string >( event.errorCode.value() )
                                );

                            break;

                        case SessionEventType::ConnectionError:

                            record( "connection error: " + event.reason );

                            m_isCloseWhenDrained = true;

                            break;

                        default:

                            /*
                             * SettingsReceived, SettingsAcknowledged and PingAcknowledged are
                             * deliberately not recorded: they happen on every connection whatever
                             * the case is about, and a record every case has to count but none
                             * asserts on is a record which only makes counts brittle
                             */

                            break;
                    }

                    m_session.popEvent();
                }
            }

            void onRequest( SAA_in const bl::http2::SessionEvent& event )
            {
                Http2TestRequest request;

                ++m_streamsSeen;

                request.streamId = event.streamId;
                request.streamIndex = m_streamsSeen;
                request.hasBody = ! event.endStream;
                request.fields = event.fields;

                for( std::size_t i = 0U; i < event.fields.size(); ++i )
                {
                    const auto& name = event.fields[ i ].name();

                    if( name == ":method" )
                    {
                        request.method = event.fields[ i ].value();
                    }
                    else if( name == ":scheme" )
                    {
                        request.scheme = event.fields[ i ].value();
                    }
                    else if( name == ":authority" )
                    {
                        request.authority = event.fields[ i ].value();
                    }
                    else if( name == ":path" )
                    {
                        request.path = event.fields[ i ].value();
                    }
                }

                record(
                    "request " + request.method + " " + request.path + " on stream " +
                    bl::utils::lexical_cast< std::string >( request.streamId.value() )
                    );

                auto& state = m_scripts[ request.streamId ];

                state.script = responseFor( request );

                if(
                    m_config.goAwayAfterStreams != 0U &&
                    m_streamsSeen == m_config.goAwayAfterStreams
                    )
                {
                    /*
                     * The GOAWAY is appended to this stream's own script rather than sent here, so
                     * that it goes out AFTER the response to the request which reached the count -
                     * which is what 6.8's graceful shutdown means and what a retrying client is
                     * entitled to
                     */

                    state.script.goAway( m_config.goAwayErrorCode );
                }
            }

            /**
             * @brief Runs the responder, turning a throw into a recorded failure
             *
             * A responder runs on an I/O thread, so it must not assert; one which throws would
             * otherwise take the connection task down with an exception the case cannot read. The
             * failure is recorded instead and the stream is answered with an internal error, which
             * is what a case will see if it ignores failure( )
             */

            auto responseFor( SAA_in const Http2TestRequest& request ) -> Http2ResponseScript
            {
                if( ! m_config.responder )
                {
                    return Http2ResponseScript().headers( 200U, bl::http2::HpackFieldList(), true );
                }

                try
                {
                    return m_config.responder( request );
                }
                catch( std::exception& e )
                {
                    m_recorder -> recordFailure( std::string( e.what() ) );

                    return Http2ResponseScript().headers( 500U, bl::http2::HpackFieldList(), true );
                }
            }

            void onRequestData(
                SAA_in              const std::uint32_t                         streamId,
                SAA_in              const std::string&                          data
                )
            {
                m_recorder -> appendBody( streamId, data );

                auto& state = m_scripts[ streamId ];

                state.received += data.size();

                if( ! m_config.withholdWindowUpdates && ! data.empty() )
                {
                    m_session.consumed( streamId, data.size() );

                    state.received = 0U;
                }
            }

            void onRequestBodyEnd( SAA_in const std::uint32_t streamId )
            {
                record(
                    "end of request body on stream " +
                    bl::utils::lexical_cast< std::string >( streamId ) +
                    ", " +
                    bl::utils::lexical_cast< std::string >(
                        m_recorder -> bodyOf( streamId ).size()
                        ) +
                    " bytes"
                    );
            }

            /*************************************************************************************
             * The scripts
             */

            void advanceScripts()
            {
                for( auto it = m_scripts.begin(); it != m_scripts.end(); ++it )
                {
                    advanceOne( it -> first, it -> second );
                }
            }

            /**
             * @brief Whether everything queued for this stream has actually been written
             *
             * Session::produce( ) writes header blocks before DATA - so a trailer section queued
             * while a body is still pending would be written in FRONT of that body. The test for
             * "the body is out" is that nothing is in flight, the session has nothing more to say,
             * and both windows are open: with a shut window the session also has nothing to say,
             * and the body would still be sitting there
             */

            bool isStreamDrained( SAA_in const std::uint32_t streamId ) const
            {
                return
                    ! m_isWriteInFlight &&
                    ! m_session.wantsWrite() &&
                    m_session.streamSendWindow( streamId ) > 0 &&
                    m_session.connectionSendWindow() > 0;
            }

            void advanceOne(
                SAA_in              const std::uint32_t                         streamId,
                SAA_inout           StreamScript&                               state
                )
            {
                using namespace bl::http2;

                if( state.isDone || state.isDelaying )
                {
                    return;
                }

                while( state.nextStep < state.script.steps().size() )
                {
                    const auto& step = state.script.steps()[ state.nextStep ];

                    switch( step.kind.value() )
                    {
                        case Http2StepKind::Interim:

                            m_session.submitHeaders(
                                streamId,
                                headerSection( step ),
                                false /* endStream */
                                );

                            record(
                                "interim " +
                                bl::utils::lexical_cast< std::string >( step.status.value() ) +
                                " on stream " +
                                bl::utils::lexical_cast< std::string >( streamId )
                                );

                            break;

                        case Http2StepKind::Headers:

                            m_session.submitHeaders(
                                streamId,
                                headerSection( step ),
                                step.endStream
                                );

                            record(
                                "responded " +
                                bl::utils::lexical_cast< std::string >( step.status.value() ) +
                                " on stream " +
                                bl::utils::lexical_cast< std::string >( streamId )
                                );

                            /*
                             * A step which ends the STREAM does not end the SCRIPT: goAway( ) and
                             * closeConnection( ) are connection-level and are written after the
                             * response they follow, which is what 6.8's graceful shutdown is
                             */

                            break;

                        case Http2StepKind::Data:

                            if( ! placeBody( streamId, step, state ) )
                            {
                                /*
                                 * The windows are shut - the script resumes by itself when the
                                 * WINDOW_UPDATE arrives, because every read advances it again
                                 */

                                return;
                            }

                            break;

                        case Http2StepKind::Trailers:

                            if( ! isStreamDrained( streamId ) )
                            {
                                return;
                            }

                            m_session.submitHeaders(
                                streamId,
                                step.fields,
                                true /* endStream */
                                );

                            record(
                                "trailers on stream " +
                                bl::utils::lexical_cast< std::string >( streamId )
                                );

                            break;

                        case Http2StepKind::EndStream:

                            m_session.provideBody(
                                streamId,
                                nullptr,
                                0U,
                                true /* endStream */
                                );

                            record(
                                "ended stream " +
                                bl::utils::lexical_cast< std::string >( streamId )
                                );

                            break;

                        case Http2StepKind::Delay:

                            armDelay( streamId, state, step.delayInMilliseconds );

                            return;

                        case Http2StepKind::Refuse:

                            m_session.resetStream( streamId, step.errorCode );

                            record(
                                "refused stream " +
                                bl::utils::lexical_cast< std::string >( streamId ) +
                                " with error " +
                                bl::utils::lexical_cast< std::string >( step.errorCode.value() )
                                );

                            break;

                        case Http2StepKind::AwaitWindowStall:

                            if(
                                m_session.streamReceiveWindow( streamId ) > 0 &&
                                m_session.connectionReceiveWindow() > 0
                                )
                            {
                                /*
                                 * Not stalled yet - the next DATA frame brings the script back
                                 * here, and there is no other way for the answer to change
                                 */

                                return;
                            }

                            break;

                        case Http2StepKind::AwaitStreamClosed:

                            if( ! state.isClosedAtPeer )
                            {
                                /*
                                 * Not over yet. Every read and every write advances the script
                                 * again, and the client's END_STREAM arrives on a read while the
                                 * peer's own last frame is reported on the write that carried it,
                                 * so both halves bring the script back here - and pumpWrites( )
                                 * still runs after this return, which is what lets the peer finish
                                 * writing the response this step is waiting behind
                                 */

                                return;
                            }

                            break;

                        case Http2StepKind::AwaitRequests:

                            if( m_streamsSeen < step.requestCount )
                            {
                                /*
                                 * Not yet. A request arrives on a read and every read advances
                                 * the scripts again, so there is no other way for the answer to
                                 * change - and pumpWrites( ) still runs after this return, so
                                 * everything queued BEFORE this step goes out while it waits
                                 */

                                return;
                            }

                            break;

                        case Http2StepKind::CreditWindow:

                            if( state.received != 0U )
                            {
                                m_session.consumed( streamId, state.received );

                                record(
                                    "credited " +
                                    bl::utils::lexical_cast< std::string >( state.received ) +
                                    " bytes on stream " +
                                    bl::utils::lexical_cast< std::string >( streamId )
                                    );

                                state.received = 0U;
                            }

                            break;

                        case Http2StepKind::GoAway:

                            m_session.goAway( step.errorCode, step.data );

                            record(
                                "sent GOAWAY with error " +
                                bl::utils::lexical_cast< std::string >( step.errorCode.value() )
                                );

                            break;

                        case Http2StepKind::Close:

                            m_isCloseWhenDrained = true;

                            break;

                        default:

                            BL_RIP_MSG( "An HTTP/2 test peer script carries an unknown step" );

                            break;
                    }

                    ++state.nextStep;
                }

                state.isDone = true;
            }

            /**
             * @brief The status pseudo-header plus the step's own fields
             *
             * :status comes first because RFC 9113 8.3 requires pseudo-headers to precede the
             * regular ones; the peer builds the section rather than making every case remember it
             */

            static auto headerSection( SAA_in const Http2Step& step ) -> bl::http2::HpackFieldList
            {
                bl::http2::HpackFieldList fields;

                fields.push_back(
                    bl::http2::HpackField(
                        std::string( ":status" ),
                        bl::utils::lexical_cast< std::string >( step.status.value() )
                        )
                    );

                for( std::size_t i = 0U; i < step.fields.size(); ++i )
                {
                    fields.push_back( step.fields[ i ] );
                }

                return fields;
            }

            /**
             * @brief Places as much of this step's body as the windows allow
             *
             * @return true when the whole chunk has been handed over and the script may go on
             */

            bool placeBody(
                SAA_in              const std::uint32_t                         streamId,
                SAA_in              const Http2Step&                            step,
                SAA_inout           StreamScript&                               state
                )
            {
                while( state.dataOffset < step.data.size() )
                {
                    const auto wanted = m_session.bodyBytesWanted( streamId );

                    if( 0U == wanted )
                    {
                        return false;
                    }

                    const auto chunk =
                        std::min< std::size_t >( wanted, step.data.size() - state.dataOffset );

                    m_session.provideBody(
                        streamId,
                        reinterpret_cast< const std::uint8_t* >(
                            step.data.data() + state.dataOffset
                            ),
                        chunk,
                        false /* endStream */
                        );

                    state.dataOffset += chunk;
                }

                record(
                    "sent " +
                    bl::utils::lexical_cast< std::string >( step.data.size() ) +
                    " body bytes on stream " +
                    bl::utils::lexical_cast< std::string >( streamId )
                    );

                state.dataOffset = 0U;

                return true;
            }

            /**
             * @brief Arms this stream's delay
             *
             * One timer per stream and never re-armed while armed, which is what keeps the
             * accounting honest: re-arming an outstanding deadline_timer cancels its wait, and
             * that cancellation would arrive as an operation_aborted this task has no way to tell
             * from a real one
             */

            void armDelay(
                SAA_in              const std::uint32_t                         streamId,
                SAA_inout           StreamScript&                               state,
                SAA_in              const long                                  delayInMilliseconds
                )
            {
                using namespace bl;

                BL_ASSERT( ! state.isDelaying );

                if( ! state.timer )
                {
                    state.timer = makeTimer();
                }

                state.isDelaying = true;

                state.timer -> expires_from_now( time::milliseconds( delayInMilliseconds ) );

                base_type::beginOperation();

                state.timer -> async_wait(
                    cpp::bind(
                        &this_type::onDelayExpired,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error,
                        streamId
                        )
                    );
            }

            void onDelayExpired(
                SAA_in              const bl::eh::error_code&                   ec,
                SAA_in              const std::uint32_t                         streamId
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                const auto it = m_scripts.find( streamId );

                if( it != m_scripts.end() )
                {
                    it -> second.isDelaying = false;

                    /*
                     * The delay step itself is consumed here, so the script does not arm it again
                     */

                    ++it -> second.nextStep;

                    advanceOne( streamId, it -> second );

                    pumpWrites();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************************
             * The opening write, and the task's own life cycle
             */

            /**
             * @brief Lets the peer speak
             *
             * The session queued its SETTINGS at construction - a server sends no preface of its
             * own (RFC 9113 3.4), it consumes the client's - so there is nothing to build here,
             * only a gate to open
             */

            void releaseOpeningWrite()
            {
                m_isWriteAllowed = true;

                pumpWrites();
            }

            void onOpeningDelayExpired( SAA_in const bl::eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                releaseOpeningWrite();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            virtual void scheduleTask(
                SAA_in              const std::shared_ptr< bl::tasks::ExecutionQueue >&     eq
                ) OVERRIDE
            {
                using namespace bl;

                BL_UNUSED( eq );

                base_type::ensureChannelIsOpen();

                record( "connected" );

                if( m_config.openingDelayInMilliseconds <= 0L )
                {
                    releaseOpeningWrite();
                }
                else
                {
                    /*
                     * A peer which accepts the connection and then says nothing for a while -
                     * what design 5.7's connect-through-preface deadline exists to catch. The
                     * read below is armed either way, so the client's own preface and its first
                     * request are consumed meanwhile and are answered when the gate opens
                     */

                    m_openingTimer = makeTimer();

                    m_openingTimer -> expires_from_now(
                        time::milliseconds( m_config.openingDelayInMilliseconds.value() )
                        );

                    base_type::beginOperation();

                    m_openingTimer -> async_wait(
                        cpp::bind(
                            &this_type::onOpeningDelayExpired,
                            om::ObjPtrCopyable< this_type >::acquireRef( this ),
                            asio::placeholders::error
                            )
                        );
                }

                scheduleRead();
            }

            void cancelTimers() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bl::eh::error_code ec;

                if( m_openingTimer )
                {
                    m_openingTimer -> cancel( ec );
                }

                for( auto it = m_scripts.begin(); it != m_scripts.end(); ++it )
                {
                    if( it -> second.timer )
                    {
                        it -> second.timer -> cancel( ec );
                    }
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Cancels what is in flight so the one terminal path can be taken
             *
             * It must NOT go through cancelTask( ): that sets the cancel flag, and the accounting
             * of design 3.2 excuses the operation_aborted of a deliberate close only while the
             * task has not been cancelled. A peer which closed itself because its script said so
             * would otherwise complete isFailed( )
             */

            virtual void initiateClose() OVERRIDE
            {
                cancelTimers();

                if( base_type::isSocketCreated() )
                {
                    bl::eh::error_code ec;

                    base_type::getSocket().cancel( ec );
                }
            }

            virtual void cancelTask() OVERRIDE
            {
                cancelTimers();

                base_type::cancelTask();
            }
        };

        /**
         * @brief class Http2TestServerT - the acceptor, one server-role session per connection
         *
         * TcpServerBase and not a worker thread with blocking calls, and the reason is not
         * fidelity to design 8.2's wording: a blocking peer cannot be full duplex, and a window
         * stall is precisely "the client has stopped sending, and a timer must now write while a
         * read is outstanding"
         *
         * THE PORT IS EPHEMERAL, WHICH IS WHAT KEEPS THESE CASES OFF THE MACHINE GLOBAL TEST LOCK.
         * TcpServerBase records the endpoint it was ASKED for, so binding port zero would leave it
         * reporting zero; continueAfterResolved( ) is virtual and the acceptor is protected, so
         * the bound port is read back from the acceptor itself once the base has bound it. The
         * same override is the readiness signal, which means no probe connection - one would
         * arrive at this peer as a connection like any other - and no sleep
         */

        template
        <
            typename STREAM,
            typename SERVERPOLICY = bl::tasks::TcpServerPolicyQuiet
        >
        class Http2TestServerT :
            public bl::tasks::TcpServerBase< STREAM, SERVERPOLICY >
        {
            BL_DECLARE_OBJECT_IMPL( Http2TestServerT )

        public:

            typedef Http2TestServerT< STREAM, SERVERPOLICY >                    this_type;
            typedef bl::tasks::TcpServerBase< STREAM, SERVERPOLICY >            base_type;
            typedef typename STREAM::stream_ref                                 stream_ref;

            typedef bl::om::ObjectImpl< Http2TestConnectionT< STREAM > >        connection_t;

        protected:

            const std::shared_ptr< Http2TestRecorder >                          m_recorder;
            Http2TestPeerConfig                                                 m_config;

            mutable bl::os::mutex                                               m_portLock;
            mutable bl::os::condition_variable                                  m_cvListening;
            unsigned short                                                      m_port = 0U;

            Http2TestServerT(
                SAA_in              const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&  controlToken,
                SAA_in_opt          std::string&&                               host = "127.0.0.1",
                SAA_in_opt          const unsigned short                        port = 0U,
                SAA_in_opt          const std::string&                          privateKeyPem = bl::str::empty(),
                SAA_in_opt          const std::string&                          certificatePem = bl::str::empty()
                )
                :
                base_type( controlToken, BL_PARAM_FWD( host ), port, privateKeyPem, certificatePem ),
                m_recorder( std::make_shared< Http2TestRecorder >() )
            {
                m_config.goAwayErrorCode = bl::http2::Globals::ERROR_CODE_NO_ERROR;
            }

            virtual bool continueAfterResolved(
                SAA_in              typename base_type::tcp_resolver_type::iterator          endpoints
                ) OVERRIDE
            {
                const auto result = base_type::continueAfterResolved( endpoints );

                {
                    BL_MUTEX_GUARD( m_portLock );

                    /*
                     * The base has opened, bound and listened by now, so this is the port the
                     * kernel actually gave us - which is the whole point of binding zero
                     */

                    m_port = base_type::m_acceptor -> local_endpoint().port();

                    m_cvListening.notify_all();
                }

                return result;
            }

            virtual bl::om::ObjPtr< bl::tasks::Task > createConnection(
                SAA_inout           stream_ref&&                                connectedStream
                ) OVERRIDE
            {
                const auto connection = connection_t::createInstance(
                    BL_PARAM_FWD( connectedStream ),
                    m_recorder,
                    m_config
                    );

                return bl::om::qi< bl::tasks::Task >( connection );
            }

            virtual std::uint64_t connectionMemoryFootprint() const NOEXCEPT OVERRIDE
            {
                /*
                 * A connection holds a read buffer, whatever one write is worth and the bodies of
                 * the streams in flight; a test peer is never asked to hold more
                 */

                return 1024U * 1024U;
            }

        public:

            /**
             * @brief The port the peer is listening on, once it is listening
             *
             * THE READINESS RENDEZVOUS, and the reason no case here sleeps or probes: the acceptor
             * task is scheduled asynchronously, so the port does not exist yet when the case
             * returns from pushing it onto a queue. Bounded, so a peer which never binds fails the
             * case with a diagnosis
             */

            unsigned short waitForPort(
                SAA_in_opt          const std::size_t                           timeoutInMilliseconds = 15U * 1000U
                ) const
            {
                bl::os::mutex_unique_lock guard( m_portLock );

                const auto isListening = m_cvListening.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return 0U != m_port;
                    }
                    );

                if( ! isListening )
                {
                    UTF_FAIL( "The HTTP/2 test peer did not begin listening in time" );
                }

                return m_port;
            }

            auto recorder() const NOEXCEPT -> const Http2TestRecorder&
            {
                return *m_recorder;
            }

            /*
             * Everything below is read by a connection when it is created, so it is set before the
             * acceptor is scheduled
             */

            void setResponder( SAA_in responder_t&& responder )
            {
                m_config.responder = BL_PARAM_FWD( responder );
            }

            void setProfile( SAA_in const bl::http2::Http2Profile& profile )
            {
                m_config.profile = profile;
            }

            void setGoAwayAfterStreams(
                SAA_in              const std::uint32_t                         streams,
                SAA_in_opt          const std::uint32_t                         errorCode =
                                        bl::http2::Globals::ERROR_CODE_NO_ERROR
                )
            {
                m_config.goAwayAfterStreams = streams;
                m_config.goAwayErrorCode = errorCode;
            }

            void setWithholdWindowUpdates( SAA_in const bool withhold ) NOEXCEPT
            {
                m_config.withholdWindowUpdates = withhold;
            }

            void setOpeningDelayInMilliseconds( SAA_in const long delayInMilliseconds ) NOEXCEPT
            {
                m_config.openingDelayInMilliseconds = delayInMilliseconds;
            }
        };

        typedef bl::om::ObjectImpl
            <
                Http2TestServerT< bl::tasks::TcpSocketAsyncBase >
            >
            Http2TestServer;

    } // h2peer

} // utest

#endif /* __UTEST_HTTP2TESTSERVER_H_ */
