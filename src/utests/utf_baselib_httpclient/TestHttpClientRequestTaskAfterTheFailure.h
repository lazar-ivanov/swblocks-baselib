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

#ifndef __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_
#define __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * What reaches a request after it has FAILED - D3's guard, applied to its siblings ( owed-list
 * row I8 of notes/plans/issues/astra-remediation-owed-work.md )
 *
 * A timeout or a cancel fails the request in the drain's apply phase, and the connection learns of
 * it only afterwards, from the cancel( ) the deferred phase makes. Whatever the connection had
 * already sent up is still in the mailbox then, and whatever it sends before it sees the cancel
 * follows. D3 made a late Data block change nothing; this is the same question for the header
 * blocks and the trailers, which used to write the response the caller holds - after its task had
 * completed and while the caller could be reading it - and re-arm the stream idle timer.
 *
 * THE FAILURE IS PINNED BEFORE ANYTHING LATE IS DELIVERED, with the same rendezvous D3's case uses:
 * the probe's cancel record is made in the deferred phase of the batch which failed the request, and
 * runTask( ) returns only once the task has completed. The close delivered last is the second
 * rendezvous - its release is applied behind everything delivered ahead of it.
 *
 * AND WHAT THE SESSION DOES WITH A REQUEST WHICH FAILED ON A BODY CAP ( owed-list row I5 ): it does
 * not replay it, whatever connection loss lands in the same batch - the refusal D4 made for a sink
 * which threw. The batch is arranged the way D4's session case arranges it, with its gated probe.
 * The maintainer widened I5 into the general rule: a hop whose failure the request task decided
 * itself - a timeout, a body cap, a sink or source which threw - is never replayed, and only a
 * failure which is the connection's may be. So the same is asked of a TIMEOUT, whose Expired event
 * no probe can post: that case runs every drain and timer handler itself, on a ManualThreadPool.
 *
 * The probes are TestHttpClientRequestTask.h's, the gated connection
 * TestHttpClientRequestTaskSinkAccounting.h's and the counting sink
 * TestHttpClientRequestTaskOutstandingCap.h's - all of which this module's Main.cpp includes ahead
 * of this file.
 */

namespace utest
{
    namespace afterthefailure
    {
        /**
         * @brief How a request is made to fail before the late events reach it
         */

        enum class FailBy
        {
            Cancel,
            HeadersTimeout,
        };

        /**
         * @brief Fails one request, then delivers an interim block, a final block and trailers,
         * then the close - and checks that none of it reached the response the caller holds
         */

        inline void requireLateBlocksChangeNothing( SAA_in const FailBy failBy )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;

            const bool isTimeout = FailBy::HeadersTimeout == failBy;

            const auto connection = ProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                false /* isSubmitRefused */
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            /*
             * THE IDLE TIMEOUT IS ON, so that a late block which re-armed the idle timer would
             * really arm one; it is long enough that it never fires inside the case
             */

            HttpClientRequestConfig config;

            config.streamIdleTimeout = time::seconds( 60 );

            if( isTimeout )
            {
                config.responseHeadersTimeout = time::milliseconds( 250 );
            }

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

                    if( ! isTimeout )
                    {
                        task -> requestCancel();
                    }

                    connection -> waitFor( "cancel:42" );
                }
                );

            requireTrue( task -> isFailed(), "the request should have failed before the late blocks" );

            const std::string expected = isTimeout ? "has timed out" : "was cancelled";

            requireTrue(
                std::string::npos != messageOf( task ).find( expected ),
                "the request should have failed with '" + expected + "', and it reports: " +
                    messageOf( task )
                );

            http::HeaderList hints;

            hints.append( "link", "</style.css>; rel=preload" );

            connection -> deliverHeaders( 103U, std::move( hints ), true /* isInterim */ );

            http::HeaderList headers;

            headers.append( "x-late", "headers" );

            connection -> deliverHeaders( 200U, std::move( headers ), false /* isInterim */ );

            http::HeaderList trailers;

            trailers.append( "x-late-trailer", "trailers" );

            connection -> deliverTrailers( std::move( trailers ) );

            connection -> deliverClosed(
                eh::errc::make_error_code( eh::errc::operation_canceled ),
                false /* isRetryable */
                );

            requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

            const auto& response = taskImpl -> response();

            UTF_CHECK_EQUAL( response.status(), 0U );
            UTF_CHECK( ! response.headers().has( "x-late" ) );
            UTF_CHECK( ! response.trailers().has( "x-late-trailer" ) );
            UTF_CHECK_EQUAL( taskImpl -> interimResponses().size(), 0U );

            UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
            UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:failed" ) );
        }

        /**
         * @brief Which of the two body caps a request is made to overflow
         */

        enum class Cap
        {
            Buffered,
            Outstanding,
        };

        /**
         * @brief Runs one GET through a session with retryIdempotentOnConnectionLoss on, and has its
         * first stream overflow a body cap and lose its connection in ONE batch
         *
         * The batch is [ headers, a ten byte block, a connection loss published as Draining ], all
         * delivered while the gated probe holds the drain inside its first submit( ): the block
         * overflows the cap, and the close behind it reads ConnectionUnusable - which is what feeds
         * the knob. Nothing else about the request forbids a replay: a GET, no body, and a sink
         * which is offered nothing, since the block which crosses a cap is never offered.
         *
         * A REPLAY WOULD BE ANSWERED ON ITS OWN - the gated probe answers a second submit( ) from a
         * pool thread with the same ten bytes, which overflow the cap again on a clean close - so
         * the chain completes either way, and the number of submits is the answer
         */

        inline void requireAnOverflowIsNotReplayed( SAA_in const Cap cap )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;
            using namespace utest::sinkaccounting;

            const auto connection = GatedProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                std::string( "0123456789" )
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            SessionRequestPlan plan;

            plan.pool = om::ObjPtrCopyable< ConnectionPool >( om::qi< ConnectionPool >( pool ) );
            plan.state = om::ObjPtrCopyable< SessionState >(
                SessionStateImpl::createInstance< SessionState >()
                );
            plan.transportScheme = "https";

            plan.policy.retryIdempotentOnConnectionLoss = true;

            om::ObjPtrCopyable< BodySink > sink;

            if( Cap::Buffered == cap )
            {
                plan.requestConfig.maxResponseBodySize = 4U;
            }
            else
            {
                /*
                 * One byte: a block of any size, with its allowance, is over it at post( )
                 */

                plan.requestConfig.maxOutstandingResponseBodySize = 1U;

                sink = om::ObjPtrCopyable< BodySink >(
                    om::qi< BodySink >(
                        utest::outstandingcap::ParkingSink::createInstance( false /* isParking */ )
                        )
                    );
            }

            const auto chain = om::qi< tasks::Task >(
                SessionRequestTaskImpl::createInstance( std::move( plan ), makeRequest(), sink )
                );

            runTask(
                chain,
                [ & ]() -> void
                {
                    connection -> waitFor( "submit" );

                    connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

                    connection -> deliverData( "0123456789" );

                    connection -> publishState( ConnectionState::Draining );

                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_reset ),
                        false /* isRetryable */
                        );

                    connection -> openTheGate();
                }
                );

            /*
             * ONE NETWORK ATTEMPT, and one slot given back - checked and not required. The submit
             * count is the discriminator: a replay's submit( ) precedes its hop's end, which the
             * chain waits for. The slot count is exact only without a replay, since a replay's
             * close may be applied after its hop has already failed on the cap
             */

            UTF_CHECK_EQUAL( connection -> submits(), 1U );
            UTF_CHECK_EQUAL( pool -> releases().size(), 1U );

            requireTrue( chain -> isFailed(), "a request which overflowed a body cap should have failed" );

            requireTrue(
                std::string::npos != messageOf( chain ).find( "exceeded the maximum of" ),
                "the chain should have failed on the body cap, and it reports: " + messageOf( chain )
                );
        }

        /**
         * @brief The overflow case's batch with no cap crossed - so the failure which wins is the
         * CONNECTION's - with retryIdempotentOnConnectionLoss on or off
         *
         * [ headers, the ten bytes, a connection loss published as Draining ] in one batch, held by
         * the gated probe as before, and the default caps take the ten bytes. The close fails the
         * hop with the connection's own error and reads ConnectionUnusable, which is what the knob
         * feeds on. With the knob on, the session replays the GET, and the gated probe answers the
         * replay on its own - the same ten bytes and a clean close - so the chain succeeds on its
         * second attempt. With the knob off, one attempt and the connection's failure
         */

        inline void requireAConnectionLossIsReplayedOnlyUnderTheKnob( SAA_in const bool isKnobOn )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;
            using namespace utest::sinkaccounting;

            const auto connection = GatedProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                std::string( "0123456789" )
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            SessionRequestPlan plan;

            plan.pool = om::ObjPtrCopyable< ConnectionPool >( om::qi< ConnectionPool >( pool ) );
            plan.state = om::ObjPtrCopyable< SessionState >(
                SessionStateImpl::createInstance< SessionState >()
                );
            plan.transportScheme = "https";

            plan.policy.retryIdempotentOnConnectionLoss = isKnobOn;

            const auto chain = om::qi< tasks::Task >(
                SessionRequestTaskImpl::createInstance(
                    std::move( plan ),
                    makeRequest(),
                    om::ObjPtrCopyable< BodySink >()
                    )
                );

            runTask(
                chain,
                [ & ]() -> void
                {
                    connection -> waitFor( "submit" );

                    connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

                    connection -> deliverData( "0123456789" );

                    connection -> publishState( ConnectionState::Draining );

                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_reset ),
                        false /* isRetryable */
                        );

                    connection -> openTheGate();
                }
                );

            /*
             * THE NUMBER OF ATTEMPTS IS THE ANSWER, checked and not required so that a red run shows
             * it. A replay's submit( ) precedes its hop's end, which the chain waits for, and each
             * hop hands its slot back in the deferred phase of its closing batch, before it completes
             */

            UTF_CHECK_EQUAL( connection -> submits(), isKnobOn ? 2U : 1U );
            UTF_CHECK_EQUAL( pool -> releases().size(), isKnobOn ? 2U : 1U );

            if( ! isKnobOn )
            {
                requireTrue( chain -> isFailed(), "a connection loss with the knob off should have failed" );

                requireTrue(
                    std::string::npos != messageOf( chain ).find( "The HTTP request failed" ),
                    "the chain should have failed with the connection loss, and it reports: " +
                        messageOf( chain )
                    );

                return;
            }

            requireTrue(
                ! chain -> isFailed(),
                "the knob should have replayed a GET whose connection was lost, and the chain reports: " +
                    messageOf( chain )
                );

            const auto requestTask = om::qi< ClientRequestTask >( chain );

            const auto& response = requestTask -> response();

            UTF_REQUIRE_EQUAL( response.status(), 200U );
            UTF_REQUIRE( response.body() );

            UTF_REQUIRE_EQUAL(
                std::string(
                    response.body() -> begin() + response.body() -> offset1(),
                    response.body() -> begin() + response.body() -> size()
                    ),
                std::string( "0123456789" )
                );
        }

        /**
         * @brief A thread pool which no thread runs - the case runs its handlers, one at a time
         *
         * WHAT IT IS FOR. A request task's drain and its timers run on its execution queue's local
         * pool ( TaskBase::getThreadPool( eq ) ), and a timer's Expired event is posted from the
         * timer's own handler, which no probe can reach or see. With nothing running this pool, a
         * handler runs only inside the case's run_one( ), so the case knows exactly when the timer
         * posted and can put the connection's close BEHIND it in the same batch - a rendezvous
         * rather than a sleep. Everything else still arrives from where it always does: the probe
         * pool answers from ThreadPoolId::GeneralPurpose, and its post lands here
         */

        template
        <
            typename E = void
        >
        class ManualThreadPoolT : public bl::ThreadPool
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE_DISPOSABLE( ManualThreadPoolT, bl::ThreadPool )

        protected:

            bl::asio::io_service                                                m_io;

            ManualThreadPoolT()
            {
            }

        public:

            virtual std::size_t size() const NOEXCEPT OVERRIDE
            {
                return 0U;
            }

            virtual std::size_t resize( SAA_in const std::size_t threadCount ) OVERRIDE
            {
                BL_UNUSED( threadCount );

                BL_THROW(
                    bl::NotSupportedException(),
                    BL_MSG()
                        << "A manual thread pool has no threads to resize"
                    );
            }

            virtual bl::asio::io_service& aioService() OVERRIDE
            {
                return m_io;
            }

            virtual std::exception_ptr lastException() const OVERRIDE
            {
                return nullptr;
            }

            virtual void dispose() OVERRIDE
            {
            }
        };

        typedef bl::om::ObjectImpl< ManualThreadPoolT<> > ManualThreadPool;

        /**
         * @brief Runs the manual pool's handlers, one at a time, until the predicate holds - false
         * when it does not within the case's bound
         */

        template
        <
            typename PREDICATE
        >
        inline bool runHandlersUntil(
            SAA_inout       bl::asio::io_service&                               io,
            SAA_in          const PREDICATE&                                    predicate
            )
        {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    static_cast< std::size_t >( utest::requesttask::DEFAULT_WAIT_IN_MILLISECONDS )
                    );

            while( ! predicate() )
            {
                if( std::chrono::steady_clock::now() >= deadline )
                {
                    return false;
                }

                ( void ) io.run_one_for( std::chrono::milliseconds( 100 ) );
            }

            return true;
        }

        /**
         * @brief How many times the probe connection was asked to open a stream
         */

        inline std::size_t submitsOf(
            SAA_in          const bl::om::ObjPtr< utest::requesttask::ProbeConnection >& connection
            )
        {
            const auto trace = connection -> trace();

            std::size_t count = 0U;

            for( std::size_t i = 0U; i < trace.size(); ++i )
            {
                if( "submit" == trace[ i ] )
                {
                    ++count;
                }
            }

            return count;
        }

        /**
         * @brief Runs one GET through a session whose request task times out waiting for response
         * headers, with the connection's close drained in the SAME batch as the timer's Expired
         * and behind it
         *
         * THE CLOSE IS ONE A REPLAY WOULD BE MADE ON: either retryable, which replays a replayable
         * request whatever the knob says, or a connection loss published as Draining with
         * retryIdempotentOnConnectionLoss on. The timeout is the request's verdict - it is applied
         * first - and the close behind it only reports what the connection did.
         *
         * THE STEPS, each a rendezvous on this thread: run until the probe has seen the submit,
         * which is the batch that arms the one millisecond headers timer; run ONE handler, which
         * can only be that timer's, since nothing else is pending on the pool - it posts the
         * Expired and schedules the drain; deliver the close, which queues behind it; run one
         * handler more - the drain, applying [ Expired, Closed ], whose completion runs the
         * session's continuation on this thread. A chain which completes there made no replay
         */

        inline void requireATimeoutIsNotReplayed( SAA_in const bool isRetryableClose )
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

            SessionRequestPlan plan;

            plan.pool = om::ObjPtrCopyable< ConnectionPool >( om::qi< ConnectionPool >( pool ) );
            plan.state = om::ObjPtrCopyable< SessionState >(
                SessionStateImpl::createInstance< SessionState >()
                );
            plan.transportScheme = "https";

            plan.policy.retryIdempotentOnConnectionLoss = ! isRetryableClose;

            plan.requestConfig.responseHeadersTimeout = time::milliseconds( 1 );

            const auto chain = om::qi< tasks::Task >(
                SessionRequestTaskImpl::createInstance(
                    std::move( plan ),
                    makeRequest(),
                    om::ObjPtrCopyable< BodySink >()
                    )
                );

            const auto threadPool = ManualThreadPool::createInstance();

            auto& io = threadPool -> aioService();

            std::unique_ptr< asio::io_service::work > work( new asio::io_service::work( io ) );

            {
                const auto eq = om::lockDisposable(
                    tasks::ExecutionQueueImpl::createInstance< tasks::ExecutionQueue >(
                        tasks::ExecutionQueue::OptionKeepAll
                        )
                    );

                eq -> setLocalThreadPool( threadPool.get() );

                eq -> push_back( chain );

                requireTrue(
                    runHandlersUntil( io, [ & ]() -> bool { return connection -> has( "submit" ); } ),
                    "the request never reached the connection"
                    );

                /*
                 * THE HEADERS TIMER, AND ONLY IT - nothing else is pending on this pool
                 */

                ( void ) io.run_one();

                requireTrue(
                    ! connection -> has( "cancel:42" ) && tasks::Task::Completed != chain -> getState(),
                    "the timer's Expired should have been posted and not yet applied"
                    );

                if( isRetryableClose )
                {
                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_aborted ),
                        true /* isRetryable */
                        );
                }
                else
                {
                    connection -> publishState( ConnectionState::Draining );

                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_reset ),
                        false /* isRetryable */
                        );
                }

                /*
                 * THE DRAIN - [ Expired, Closed ] in one batch, and the session's continuation
                 * behind its completion, all on this thread
                 */

                ( void ) io.run_one();

                requireTrue( connection -> has( "cancel:42" ), "the timeout was never applied" );

                /*
                 * THE ANSWER: a chain which made no replay has completed here. Checked and not
                 * required, so that a red run also shows the replay's own submit below
                 */

                UTF_CHECK( tasks::Task::Completed == chain -> getState() );

                /*
                 * A replay, where there is one, times out on its own headers timer, with no close
                 * in its batch - so the chain always completes, and the probe then holds the
                 * replay's stream until it is closed
                 */

                requireTrue(
                    runHandlersUntil(
                        io,
                        [ & ]() -> bool { return tasks::Task::Completed == chain -> getState(); }
                        ),
                    "the chain never completed"
                    );

                const auto submits = submitsOf( connection );

                UTF_CHECK_EQUAL( submits, 1U );

                if( submits > 1U )
                {
                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::operation_canceled ),
                        false /* isRetryable */
                        );
                }

                requireTrue( chain -> isFailed(), "a request which timed out should have failed" );

                requireTrue(
                    std::string::npos != messageOf( chain ).find( "has timed out" ),
                    "the chain should have failed with the timeout, and it reports: " +
                        messageOf( chain )
                    );

                requireTrue(
                    runHandlersUntil(
                        io,
                        [ & ]() -> bool { return pool -> releases().size() == submits; }
                        ),
                    "a stream slot never came back"
                    );
            }

            /*
             * THE QUEUE FIRST, THEN THE POOL IT POINTS AT - and every handler still pending is
             * run, so that nothing holds a task when the case ends
             */

            work.reset();

            io.restart();

            ( void ) io.run();
        }

        /**
         * @brief The close a connection sends up after its request has already failed
         */

        enum class LateClose
        {
            ConnectionLoss,
            Retryable,
        };

        /**
         * @brief What the caller reads of a request's status, as one string a failed check prints
         */

        inline auto statusOf(
            SAA_in          const bl::om::ObjPtr< bl::httpclient::HttpClientRequestTaskImpl >& taskImpl
            )
            -> std::string
        {
            using bl::httpclient::RequestOutcome;

            const bool isRetryable = taskImpl -> isRetryable();
            const auto outcome = taskImpl -> outcome();

            return
                std::string( "retryable=" ) +
                ( isRetryable ? "true" : "false" ) +
                " outcome=" +
                (
                    RequestOutcome::Completed == outcome ? "completed" :
                        ( RequestOutcome::Failed == outcome ? "failed" : "unusable" )
                );
        }

        /**
         * @brief Fails one request, then delivers its stream's close LATE - in a batch of its own,
         * after the caller's task has completed - and checks that isRetryable( ) and outcome( ) read
         * as the failure left them, while the pool still gets the close's own verdict
         *
         * D1 (b') OF ASTRA'S THIRD REVIEW, T01: the pair is frozen at completion. A close which the
         * request's own failure has beaten - a cancel or a timeout, applied in an earlier batch -
         * still hands its slot back and lets go of the connection, and the pool is still told what
         * the connection did; what it must not do is write the two fields a caller which has waited
         * for the task may be reading
         *
         * THREE READINGS, AND THE MIDDLE ONE IS WHERE THE RACE WAS. The first is made before the
         * close is posted, and the mailbox lock orders it before anything the close writes. The
         * second is made as soon as deliverClosed( ) returns, which does not wait for the drain, so
         * nothing orders it against the drain applying the close: a write of the pair raced it,
         * and an ordinary build could see either value there. The third is made after the release
         * - a deferred action of the batch which applies the close - so it is ordered after that
         * batch's writes, and a close which wrote the pair is certain to show
         *
         * FOUR REQUESTS, AND ThreadSanitizer IS ONE REASON. The pair shares one 8-byte word of memory
         * with six flags the drain reads and writes as it goes; ThreadSanitizer keeps four shadow
         * values for a word and evicts one when they are full, and one report retires the word's
         * reads - so a racing reading is not reported on every request, and the pair's two races
         * report as one. Before the fix about one request in seven went unreported, and every run
         * of the four reported: 50 of 50, never fewer than two
         *
         * TWO CLOSES, ONE FOR EACH FIELD. A connection loss published as Draining reads
         * ConnectionUnusable, which is what outcome( ) would take; a close marked retryable is what
         * isRetryable( ) would take. Each is also the verdict the pool is handed
         */

        inline void requireALateCloseChangesNoStatus(
            SAA_in          const FailBy                                        failBy,
            SAA_in          const LateClose                                     lateClose
            )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;

            const bool isTimeout = FailBy::HeadersTimeout == failBy;
            const bool isConnectionLoss = LateClose::ConnectionLoss == lateClose;

            const auto connection = ProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                false /* isSubmitRefused */
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            HttpClientRequestConfig config;

            if( isTimeout )
            {
                config.responseHeadersTimeout = time::milliseconds( 250 );
            }

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

                    if( ! isTimeout )
                    {
                        task -> requestCancel();
                    }

                    connection -> waitFor( "cancel:42" );
                }
                );

            requireTrue( task -> isFailed(), "the request should have failed before the late close" );

            const std::string expected = isTimeout ? "has timed out" : "was cancelled";

            requireTrue(
                std::string::npos != messageOf( task ).find( expected ),
                "the request should have failed with '" + expected + "', and it reports: " +
                    messageOf( task )
                );

            /*
             * AT COMPLETION: a timeout or a cancel sets neither field, so they read as the task was
             * made
             */

            const auto atCompletion = statusOf( taskImpl );

            UTF_REQUIRE_EQUAL( atCompletion, std::string( "retryable=false outcome=failed" ) );

            if( isConnectionLoss )
            {
                connection -> publishState( ConnectionState::Draining );

                connection -> deliverClosed(
                    eh::errc::make_error_code( eh::errc::connection_reset ),
                    false /* isRetryable */
                    );
            }
            else
            {
                connection -> deliverClosed(
                    eh::errc::make_error_code( eh::errc::connection_aborted ),
                    true /* isRetryable */
                    );
            }

            /*
             * WHILE THE CLOSE IS APPLIED - see the comment above
             */

            const auto whileClosing = statusOf( taskImpl );

            requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

            /*
             * AFTER THE RELEASE
             */

            const auto afterRelease = statusOf( taskImpl );

            UTF_CHECK_EQUAL( whileClosing, atCompletion );
            UTF_CHECK_EQUAL( afterRelease, atCompletion );

            /*
             * THE POOL STILL GETS THE CLOSE'S VERDICT, and one slot back: a second Closed would be
             * dropped by applyClosed( )'s first line, and the probe drops its sink on the first
             */

            UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );

            UTF_REQUIRE_EQUAL(
                pool -> releases()[ 0 ],
                std::string( isConnectionLoss ? "42:unusable" : "42:failed" )
                );
        }

        /**
         * @brief Times one request out on its response-headers timer with its stream's close in the
         * SAME batch, behind the Expired, and checks that the close still sets isRetryable( ) and
         * outcome( ) - the other half of D1 (b')
         *
         * THE FREEZE IS AT COMPLETION AND NOT AT THE FAILURE, and that is decided: a close drained in
         * the batch which fails the request writes the pair, because the task completes only in that
         * batch's last phase. applyClosed( ) guards on m_isCompleted alone for exactly that, where
         * every guard beside it also asks m_isCompletionPending - so a guard "harmonized" to its
         * siblings would pass every other case and still stop this close writing the pair. This
         * case is what goes red then. isOwnFailure( ) says the failure was the timeout's
         *
         * THE STEPS, each a rendezvous on this thread, as requireATimeoutIsNotReplayed( ) takes them
         * on a session: run until the probe has seen the submit, which is the batch that arms the
         * one millisecond headers timer; run ONE handler, which can only be that timer's, since
         * nothing else is pending on the pool - it posts the Expired and schedules the drain; deliver
         * the close, which queues behind it; run one handler more - the drain, applying
         * [ Expired, Closed ], whose completion is on this thread too
         */

        inline void requireACloseInTheFailuresBatchSetsTheStatus( SAA_in const LateClose lateClose )
        {
            using namespace bl;
            using namespace bl::httpclient;
            using namespace utest::requesttask;

            const bool isConnectionLoss = LateClose::ConnectionLoss == lateClose;

            const auto connection = ProbeConnection::createInstance(
                NegotiatedProtocol::fromAlpn( "h2" ),
                false /* isSubmitRefused */
                );

            const auto pool = ProbePool::createInstance(
                om::qi< ClientConnection >( connection ),
                true /* isAnswered */
                );

            HttpClientRequestConfig config;

            config.responseHeadersTimeout = time::milliseconds( 1 );

            const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
                makeRequest(),
                makeKey(),
                om::qi< ConnectionPool >( pool ),
                config
                );

            const auto task = om::qi< tasks::Task >( taskImpl );

            const auto threadPool = ManualThreadPool::createInstance();

            auto& io = threadPool -> aioService();

            std::unique_ptr< asio::io_service::work > work( new asio::io_service::work( io ) );

            {
                const auto eq = om::lockDisposable(
                    tasks::ExecutionQueueImpl::createInstance< tasks::ExecutionQueue >(
                        tasks::ExecutionQueue::OptionKeepAll
                        )
                    );

                eq -> setLocalThreadPool( threadPool.get() );

                eq -> push_back( task );

                requireTrue(
                    runHandlersUntil( io, [ & ]() -> bool { return connection -> has( "submit" ); } ),
                    "the request never reached the connection"
                    );

                /*
                 * THE HEADERS TIMER, AND ONLY IT - nothing else is pending on this pool
                 */

                ( void ) io.run_one();

                requireTrue(
                    ! connection -> has( "cancel:42" ) && tasks::Task::Completed != task -> getState(),
                    "the timer's Expired should have been posted and not yet applied"
                    );

                if( isConnectionLoss )
                {
                    connection -> publishState( ConnectionState::Draining );

                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_reset ),
                        false /* isRetryable */
                        );
                }
                else
                {
                    connection -> deliverClosed(
                        eh::errc::make_error_code( eh::errc::connection_aborted ),
                        true /* isRetryable */
                        );
                }

                /*
                 * THE DRAIN - [ Expired, Closed ] in one batch, and the task's completion behind it,
                 * on this thread
                 */

                ( void ) io.run_one();

                requireTrue( connection -> has( "cancel:42" ), "the timeout was never applied" );

                requireTrue(
                    tasks::Task::Completed == task -> getState(),
                    "the batch which applied the timeout should have completed the request"
                    );

                requireTrue( task -> isFailed(), "a request which timed out should have failed" );

                requireTrue(
                    std::string::npos != messageOf( task ).find( "has timed out" ),
                    "the request should have failed with the timeout, and it reports: " +
                        messageOf( task )
                    );

                UTF_CHECK( taskImpl -> isOwnFailure() );

                /*
                 * THE ANSWER: the close behind the timeout wrote what the connection did
                 */

                UTF_CHECK_EQUAL(
                    statusOf( taskImpl ),
                    std::string(
                        isConnectionLoss ?
                            "retryable=false outcome=unusable" :
                            "retryable=true outcome=failed"
                        )
                    );

                /*
                 * AND THE POOL WAS TOLD THE SAME, once - its release is a deferred action of the
                 * batch which has just been run
                 */

                UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );

                UTF_REQUIRE_EQUAL(
                    pool -> releases()[ 0 ],
                    std::string( isConnectionLoss ? "42:unusable" : "42:failed" )
                    );
            }

            /*
             * THE QUEUE FIRST, THEN THE POOL IT POINTS AT - and every handler still pending is
             * run, so that nothing holds a task when the case ends
             */

            work.reset();

            io.restart();

            ( void ) io.run();
        }

    } // afterthefailure

} // utest

/**
 * @brief Header blocks and trailers which arrive after a cancel or a timeout change nothing
 *
 * The same guard D3 put at the top of applyData( ), at the top of applyHeaders( ) and of the
 * Trailers arm: a request whose completion is decided takes nothing more into its response and arms
 * no timer. Run twice, once for each way a request fails with its stream still open - a cancel and
 * a response-headers timeout.
 *
 * RED BEFORE: the late 200 became the failed request's status, its field and the trailer's field
 * were written into the response, and the 103 was filed as an interim - all after the caller's task
 * had completed. GREEN AFTER: the response is as the failure left it
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_HeadersAndTrailersAfterTheFailureChangeNothingTests )
{
    using namespace utest::afterthefailure;

    requireLateBlocksChangeNothing( FailBy::Cancel );
    requireLateBlocksChangeNothing( FailBy::HeadersTimeout );
}

/**
 * @brief An upload pull which arrives after a cancel is not answered - the caller's BodySource is
 * not read again
 *
 * The third sibling of applyData( )'s guard, found while folding in I8. applyBodyWanted( ) asked
 * only whether the stream had closed, so a pull the connection sent before it saw the cancel read
 * the caller's source and handed the bytes to a stream this task had already reset - after the
 * caller's task had completed. Every failure which leaves the stream open resets it first, so no
 * answer is owed to the connection once the completion is decided.
 *
 * StubBodySource is TestClientContracts.h's, which this module's Main.cpp includes ahead of this
 * file. RED BEFORE: the late pull read four bytes from the source and provided them. GREEN AFTER:
 * nothing read and nothing provided
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_AnUploadPullAfterTheFailureIsNotAnsweredTests )
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

            task -> requestCancel();

            connection -> waitFor( "cancel:42" );
        }
        );

    requireTrue( task -> isFailed(), "the request should have failed before the late pull" );

    connection -> deliverBodyWanted( 4096U );

    connection -> deliverClosed(
        eh::errc::make_error_code( eh::errc::operation_canceled ),
        false /* isRetryable */
        );

    /*
     * The close's release is applied behind the pull, and a pull's answer is a deferred action of
     * the batch which applied it - so once the release is in, an answer would have been made
     */

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    UTF_CHECK_EQUAL( connection -> uploaded(), std::string() );
    UTF_CHECK( ! connection -> has( "body:4:more" ) );
}

/**
 * @brief I5 - a request which failed on a body cap is not replayed, whatever connection loss lands
 * in the same batch
 *
 * Run once for each cap: the buffered maxResponseBodySize, and D3's maxOutstandingResponseBodySize,
 * whose overflow marker is made at post( ). Each time the overflow is the request's verdict - it is
 * applied first - and the close behind it only reports the connection gone.
 *
 * RED BEFORE: the session read ConnectionUnusable off the hop and, with the knob on and a GET, made
 * a second attempt, which overflowed again - two submits and two slots. GREEN AFTER: one of each, and
 * the chain failed on the cap
 */

UTF_AUTO_TEST_CASE( ClientSession_AnOverflowedRequestIsNotReplayedTests )
{
    using namespace utest::afterthefailure;

    requireAnOverflowIsNotReplayed( Cap::Buffered );
    requireAnOverflowIsNotReplayed( Cap::Outstanding );
}

/**
 * @brief The control for I5's rule: a failure which is the CONNECTION's is still replayed under
 * retryIdempotentOnConnectionLoss
 *
 * The overflow case's batch with nothing crossing a cap - headers, the ten bytes, and a connection
 * loss published as Draining - so the failure which wins is the close's, and the knob replays the
 * GET: two submits, and the replay, answered on a clean close, is the chain's answer. With the knob
 * off, one submit and the connection's failure. It is what goes red if isOwnFailure( ) were set on a
 * failure the connection decided, which no other session case would notice - the fallback cases
 * replay on isRetryable( ), not on the knob
 */

UTF_AUTO_TEST_CASE( ClientSession_AConnectionLossIsReplayedUnderTheKnobTests )
{
    using namespace utest::afterthefailure;

    requireAConnectionLossIsReplayedOnlyUnderTheKnob( true /* isKnobOn */ );
    requireAConnectionLossIsReplayedOnlyUnderTheKnob( false /* isKnobOn */ );
}

/**
 * @brief I5's general rule - a request which TIMED OUT is not replayed, whatever close lands in the
 * same batch behind the timeout
 *
 * Run twice: a close marked retryable, which replays a replayable request whatever the knob says;
 * and a connection loss published as Draining, with retryIdempotentOnConnectionLoss on. The
 * response-headers timeout is the one used because it is per hop - the chain's total timeout is
 * budgeted by the session, which refuses a hop once its budget is spent.
 *
 * RED BEFORE: the session read the close off the hop and replayed the timed-out GET - the chain had
 * not completed after the batch, and a second submit followed, which timed out in turn. GREEN
 * AFTER: the chain completed in that batch, failed with the timeout, after one submit
 */

UTF_AUTO_TEST_CASE( ClientSession_ATimedOutRequestIsNotReplayedTests )
{
    using namespace utest::afterthefailure;

    requireATimeoutIsNotReplayed( true /* isRetryableClose */ );
    requireATimeoutIsNotReplayed( false /* isRetryableClose */ );
}

/**
 * @brief A close which arrives after a cancel or a timeout changes neither isRetryable( ) nor
 * outcome( ), and the pool still gets its verdict
 *
 * T01 of astra's third review, decided as D1 (b'): the two fields are frozen when the task completes,
 * as I8 froze the response. Run four times - each way a request fails with its stream still open, a
 * cancel and a response-headers timeout, against each close which would write a field, a connection
 * loss published as Draining and a close marked retryable.
 *
 * RED BEFORE: after the release, outcome( ) read unusable behind the connection loss and
 * isRetryable( ) read true behind the retryable close - both written after the caller's task had
 * completed - and the reading made while the close was applied raced that write. GREEN AFTER: all
 * three readings are the failure's, and the pool's record still carries the close's verdict, once
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_ACloseAfterTheFailureChangesNoStatusTests )
{
    using namespace utest::afterthefailure;

    requireALateCloseChangesNoStatus( FailBy::Cancel, LateClose::ConnectionLoss );
    requireALateCloseChangesNoStatus( FailBy::Cancel, LateClose::Retryable );
    requireALateCloseChangesNoStatus( FailBy::HeadersTimeout, LateClose::ConnectionLoss );
    requireALateCloseChangesNoStatus( FailBy::HeadersTimeout, LateClose::Retryable );
}

/**
 * @brief A close drained in the SAME batch as a timeout still sets isRetryable( ) and outcome( ) -
 * the freeze is at completion, not at the failure
 *
 * The other half of D1 (b'), and the half no other case can tell apart: every case which reads the
 * pair reads it where the close or a refusal IS the answer, and the session reads it only after its
 * isOwnFailure( ) refusal. Run twice, against a connection loss published as Draining and a close
 * marked retryable, each drained in one batch behind the timer's Expired.
 *
 * GREEN BEFORE AND AFTER the freeze, which keeps this half. ITS CONTROL is the guard harmonized to
 * its siblings' - ! m_isCompleted && ! m_isCompletionPending - under which the close writes nothing:
 * the status then reads the failure's, retryable=false outcome=failed, where the connection said
 * unusable or retryable, while the pool's record is unchanged
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_ACloseInTheFailuresBatchStillSetsTheStatusTests )
{
    using namespace utest::afterthefailure;

    requireACloseInTheFailuresBatchSetsTheStatus( LateClose::ConnectionLoss );
    requireACloseInTheFailuresBatchSetsTheStatus( LateClose::Retryable );
}

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASKAFTERTHEFAILURE_H_ */
