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

#ifndef __UTEST_TESTCONNECTIONPOOLRETIREDENTRY_H_
#define __UTEST_TESTCONNECTIONPOOLRETIREDENTRY_H_

#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientConnection.h>

#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <chrono>
#include <cstddef>

#include <utests/baselib/Utf.h>

/************************************************************************
 * A retired entry whose task has ENDED, examined while a rider still holds its slot - found by E2 of
 * astra's second review's change-set CS-2, and fixed there
 *
 * THE DEFECT. examineKey( ) keeps examining a retired entry until it holds no slot, and
 * refreshEntry( )'s Completed arm reported a failed attempt on EVERY such examine once the entry's
 * task had completed - only the retire and its counter were guarded. Every report charges each
 * queued waiter an attempt and bumps establishmentRetries, whenever no live connection is left. So:
 *
 *   - an entry retired by the establishment bound was charged once by the bound and then again,
 *     and again, by the Completed arm - one establishment, several charges, and a queued request
 *     failed before its retry, with the cancelled task's exception in place of the bound's;
 *   - an entry which SERVED a request, and which the Closed arm therefore retired without a charge
 *     ( the second witness, isPeerLimitKnown ), was charged by the Completed arm anyway - the origin
 *     which answers one request per connection and closes, charged for behaving normally.
 *
 * Both need no live connection to be left, which a pool at maxTotalConnections produces: a retired
 * entry still holding its rider's slot counts against the total until it is forgotten, so no
 * replacement can start.
 *
 * THE FIX: the Completed arm reports only when it is the arm which retires the entry, as the other
 * two arms of refreshEntry( ) always have.
 *
 * WHAT MAKES THESE DETERMINISTIC. Every assertion about a charge reads the pool's own state -
 * waiterCount( ) and the statistics, both under the pool lock - straight after an examine the case
 * runs itself, synchronously, once the task it is about has ended. A maintenance tick landing
 * earlier does exactly what the case's own examine would, so it cannot change the answer.
 *
 * The harness is TestConnectionPool.h's, which this module's Main.cpp includes ahead of this file.
 */

namespace utest
{
    namespace connpoolretired
    {
        /**
         * @brief Waits for a stub connection task to reach Completed - the ARRANGEMENT, bounded
         *
         * It polls, and it is not the flake src/utests/AGENTS.md names: it waits for the condition
         * the case is about to exercise, and no assertion reads anything it waited on. What the
         * case asserts afterwards is the pool's answer to an examine the case runs itself once
         * this returns. The stub's completion is posted to a thread pool, and nothing in the
         * harness signals the moment the task's state becomes Completed
         */

        inline bool waitForTaskCompleted(
            SAA_in          const bl::om::ObjPtr< bl::tasks::Task >&            task,
            SAA_in_opt      const long                                          timeoutInMilliseconds = 10000L
            )
        {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds( timeoutInMilliseconds );

            for( ;; )
            {
                if( bl::tasks::Task::Completed == task -> getState() )
                {
                    return true;
                }

                if( std::chrono::steady_clock::now() >= deadline )
                {
                    return false;
                }

                bl::os::sleep( bl::time::milliseconds( 5 ) );
            }
        }

        /**
         * @brief Runs one examine of every key, now, on this thread
         *
         * A release which names no connection is exactly that: releaseStream( ) finds nothing to
         * give back and examines every key under the pool lock before it returns, so what the pool
         * decides is settled - and readable through waiterCount( ) and stats( ) - when this returns
         */

        inline void examineNow( SAA_in const bl::om::ObjPtr< utest::connpool::pool_impl_t >& pool )
        {
            pool -> releaseStream(
                bl::om::ObjPtr< bl::httpclient::ClientConnection >(),
                bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE,
                bl::httpclient::RequestOutcome::Failed
                );
        }

    } // connpoolretired

} // utest

/**
 * @brief One establishment is ONE charge, however long its rider holds the retired entry
 *
 * One connection in the whole pool, so nothing can replace it while it is held. A rides its preface
 * and holds its slot; B cannot ride - it is unreplayable - so it is queued. The bound expires: the
 * entry is retired, its task cancelled, and B charged ONCE, which its budget of one retry allows.
 * The cancelled task then ends, and the case examines.
 *
 * RED BEFORE THE FIX: that examine charged B again, so B was failed while A still held its slot -
 * after one establishment, without its retry, and with the cancelled task's exception rather than
 * the bound's; and establishmentRetries read two for one establishment. GREEN AFTER: B is still
 * queued, establishmentRetries reads one; and once A lets go, B gets its retry on a second
 * connection and fails at that one's bound, with the bound's own TimeoutException
 */

UTF_AUTO_TEST_CASE( H2Pool_ARetiredEntryIsChargedOnceWhileItsRiderIsOutTests )
{
    using namespace bl;
    using namespace utest::connpool;
    using namespace utest::connpoolretired;

    const auto factory = std::make_shared< StubFactory >();
    const auto answers = std::make_shared< Answers >();

    httpclient::ConnectionPoolPolicy policy;

    policy.establishmentTimeout = time::milliseconds( 200 );
    policy.maxRetriesPerRequest = 1U;
    policy.maxTotalConnections = 1U;

    const auto pool = pool_impl_t::createInstance( factoryOf( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    acquireInto( pool, key, makeRequest( true /* isReplayable */ ), answers, 0U );

    UTF_REQUIRE( answers -> waitFor( 1U ) );

    const auto firstConnection = om::qi< httpclient::ClientConnection >( factory -> taskAt( 0U ) );

    UTF_REQUIRE( answers -> records()[ 0 ].connection.get() == firstConnection.get() );

    acquireInto( pool, key, makeRequest( false /* isReplayable */ ), answers, 1U );

    /*
     * THE BOUND EXPIRED: the cancel is requested after the examine which retired the entry and
     * charged B has let go of the lock, so once it has arrived that charge has happened
     */

    UTF_REQUIRE( factory -> controlAt( 0U ) -> waitForCancel() );

    UTF_REQUIRE( waitForTaskCompleted( om::qi< tasks::Task >( factory -> taskAt( 0U ) ) ) );

    examineNow( pool );

    /*
     * B IS STILL QUEUED AND WAS CHARGED ONCE. Checked and not required, so that a red run shows
     * both
     */

    UTF_CHECK_EQUAL( pool -> waiterCount(), 1U );
    UTF_CHECK_EQUAL( pool -> stats().establishmentRetries.value(), 1U );
    UTF_CHECK_EQUAL( factory -> calls(), 1U );

    /*
     * A lets go, the entry is forgotten, and B gets its retry: a second connection, whose bound
     * expires too, and B fails there - with the bound's own exception
     */

    pool -> releaseStream( firstConnection, 1U, httpclient::RequestOutcome::Failed );

    UTF_REQUIRE( answers -> waitFor( 2U ) );

    const auto records = answers -> records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 1 ].index, 1U );
    UTF_REQUIRE( nullptr == records[ 1 ].connection );
    UTF_CHECK( isTimeoutException( records[ 1 ].exception ) );

    UTF_CHECK_EQUAL( factory -> calls(), 2U );

    const auto stats = pool -> stats();

    UTF_CHECK_EQUAL( stats.establishmentTimeouts.value(), 2U );
    UTF_CHECK_EQUAL( stats.establishmentRetries.value(), 2U );

    pool -> dispose();
}

/**
 * @brief The control: with room for a replacement, B gets its retry while A still holds its slot
 *
 * The same arrangement with the default maxTotalConnections. The examine which charges B for the
 * first bound can start a replacement, which is live, and a retired entry is charged only when no
 * live connection is left - so this case is green before the fix and after it, and shows that the
 * case above is about the pool being full and nothing else
 */

UTF_AUTO_TEST_CASE( H2Pool_WithRoomAWaiterGetsItsRetryWhileTheRiderIsOutTests )
{
    using namespace bl;
    using namespace utest::connpool;

    const auto factory = std::make_shared< StubFactory >();
    const auto answers = std::make_shared< Answers >();

    httpclient::ConnectionPoolPolicy policy;

    policy.establishmentTimeout = time::milliseconds( 200 );
    policy.maxRetriesPerRequest = 1U;

    UTF_REQUIRE( policy.maxTotalConnections.value() > 1U );

    const auto pool = pool_impl_t::createInstance( factoryOf( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    acquireInto( pool, key, makeRequest( true /* isReplayable */ ), answers, 0U );

    UTF_REQUIRE( answers -> waitFor( 1U ) );

    const auto firstConnection = om::qi< httpclient::ClientConnection >( factory -> taskAt( 0U ) );

    acquireInto( pool, key, makeRequest( false /* isReplayable */ ), answers, 1U );

    /*
     * B is answered while A still holds its slot - after two establishments, at the second one's
     * bound, with that bound's exception
     */

    UTF_REQUIRE( answers -> waitFor( 2U ) );

    const auto records = answers -> records();

    UTF_REQUIRE_EQUAL( records.size(), 2U );
    UTF_REQUIRE_EQUAL( records[ 1 ].index, 1U );
    UTF_REQUIRE( nullptr == records[ 1 ].connection );
    UTF_REQUIRE( isTimeoutException( records[ 1 ].exception ) );

    UTF_REQUIRE_EQUAL( factory -> calls(), 2U );
    UTF_REQUIRE_EQUAL( pool -> stats().establishmentTimeouts.value(), 2U );

    pool -> releaseStream( firstConnection, 1U, httpclient::RequestOutcome::Failed );

    pool -> dispose();
}

/**
 * @brief A connection which SERVED a request is not charged when its task ends - the second hole
 *
 * A rides the one connection and completes, which is the second witness of a usable connection
 * ( releaseStream( )'s Completed arm marks the peer's limit known ); C rides it next and holds its
 * slot; B is queued. The connection closes, and the Closed arm retires it WITHOUT a charge, which is
 * that arm's rule: a clean close of a connection which carried a response is not a failed attempt.
 * Then its task ends, and the case examines.
 *
 * RED BEFORE THE FIX: the Completed arm charged the retired entry regardless, and with a budget of
 * zero B was failed at once - "ended before it could carry a request", of a connection which had
 * carried one. GREEN AFTER: B stays queued and uncharged, and once C lets go B is served by the next
 * connection
 */

UTF_AUTO_TEST_CASE( H2Pool_AnEntryWhichServedARequestIsNotChargedWhenItsTaskEndsTests )
{
    using namespace bl;
    using namespace utest::connpool;
    using namespace utest::connpoolretired;

    const auto factory = std::make_shared< StubFactory >();
    const auto answers = std::make_shared< Answers >();

    httpclient::ConnectionPoolPolicy policy;

    policy.maxRetriesPerRequest = 0U;
    policy.maxTotalConnections = 1U;

    const auto pool = pool_impl_t::createInstance( factoryOf( factory ), policy );

    const PoolGuard guard( pool );

    const auto key = makeKey();

    /*
     * A rides the preface and comes back complete - the connection has served a request
     */

    acquireInto( pool, key, makeRequest( true /* isReplayable */ ), answers, 0U );

    UTF_REQUIRE( answers -> waitFor( 1U ) );

    const auto first = factory -> taskAt( 0U );
    const auto firstConnection = om::qi< httpclient::ClientConnection >( first );

    pool -> releaseStream( firstConnection, 1U, httpclient::RequestOutcome::Completed );

    /*
     * C rides the same connection, which is still Connecting and holds no slot, and keeps its slot
     */

    acquireInto( pool, key, makeRequest( true /* isReplayable */ ), answers, 1U );

    UTF_REQUIRE( answers -> waitFor( 2U ) );

    UTF_REQUIRE( answers -> records()[ 1 ].connection.get() == firstConnection.get() );

    /*
     * B cannot ride, and the pool is full, so it waits
     */

    acquireInto( pool, key, makeRequest( false /* isReplayable */ ), answers, 2U );

    /*
     * The origin closes the connection, and the Closed arm retires it with no charge - checked in
     * both trees, since that arm is unchanged
     */

    first -> setState( httpclient::ConnectionState::Draining );

    examineNow( pool );

    UTF_REQUIRE_EQUAL( pool -> stats().connectionsRetired.value(), 1U );
    UTF_REQUIRE_EQUAL( pool -> waiterCount(), 1U );
    UTF_REQUIRE_EQUAL( pool -> stats().establishmentRetries.value(), 0U );

    /*
     * THE TASK ENDS, and the retired entry - held by C - is examined again
     */

    factory -> controlAt( 0U ) -> complete();

    UTF_REQUIRE( waitForTaskCompleted( om::qi< tasks::Task >( first ) ) );

    examineNow( pool );

    UTF_CHECK_EQUAL( pool -> waiterCount(), 1U );
    UTF_CHECK_EQUAL( pool -> stats().establishmentRetries.value(), 0U );

    /*
     * C lets go, the entry is forgotten, and B is served by the next connection - made Ready at
     * birth, so that B, which cannot ride, is dispatched to it at once
     */

    factory -> initialState = httpclient::ConnectionState::Ready;
    factory -> initialFreeSlots = 1U;

    pool -> releaseStream( firstConnection, 2U, httpclient::RequestOutcome::Completed );

    UTF_REQUIRE( answers -> waitFor( 3U ) );

    const auto records = answers -> records();

    UTF_REQUIRE_EQUAL( records.size(), 3U );
    UTF_REQUIRE_EQUAL( records[ 2 ].index, 2U );
    UTF_CHECK( nullptr == records[ 2 ].exception );
    UTF_CHECK( nullptr != records[ 2 ].connection );
    UTF_CHECK( records[ 2 ].connection.get() != firstConnection.get() );

    const auto stats = pool -> stats();

    UTF_CHECK_EQUAL( stats.establishmentRetries.value(), 0U );
    UTF_CHECK_EQUAL( stats.failures.value(), 0U );
    UTF_CHECK_EQUAL( factory -> calls(), 2U );

    pool -> dispose();
}

#endif /* __UTEST_TESTCONNECTIONPOOLRETIREDENTRY_H_ */
