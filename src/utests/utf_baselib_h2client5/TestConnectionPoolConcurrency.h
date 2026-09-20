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

#ifndef __UTEST_TESTCONNECTIONPOOLCONCURRENCY_H_
#define __UTEST_TESTCONNECTIONPOOLCONCURRENCY_H_

#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http2/Http2ConnectionTask.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/http2/Globals.h>

#include <baselib/tasks/TcpStrandedStreams.h>

#include <baselib/tasks/TaskBase.h>

#include <baselib/core/Uri.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * THE POOL'S CAPACITY LOGIC, COMPOSED WITH A REAL DRIVER AND A REAL PEER (L6, evidence finding 7)
 *
 * WHAT WAS MISSING. The pool decides how many streams it may put on one connection from a single
 * number the driver publishes, freeStreamSlots( ), and from the rule the L5 round built around it:
 * ONE stream until the peer's limit is known, because until the peer's SETTINGS arrive that number
 * is the driver's assumption of 100 and a burst dispatched against an assumption cannot be
 * unwound. utf_baselib_h2client4 pins every limb of that rule against a stub which publishes
 * whatever the case tells it to. What no case anywhere ran was the rule against a driver which
 * publishes the number for itself, with two requests actually in flight: every composed case in
 * the feature runs its requests one after another, and in that regime the pool learns the peer's
 * limit from releaseStream( )'s markPeerLimitKnown( ) and NONE of the concurrency logic - the
 * band inference, the settle window, the burst bound - has to decide anything.
 *
 * WHY THIS CASE CANNOT PASS VACUOUSLY, which is the whole difficulty. Two requests which happen
 * to run one after another prove nothing and look exactly like two which overlapped: both
 * complete, both are correct, and no assertion made afterwards can tell them apart. So the
 * overlap is FORCED rather than hoped for, by the peer and not by a sleep: the response to the
 * first request is held behind awaitRequests( 2 ), so stream 1 cannot close until the request on
 * stream 3 has arrived. If the pool ever serialises the two, that request never arrives, the hold
 * never releases and the case fails on the sink's bounded wait with the peer's own records
 * printed. The same construction is what makes the third request's assertion honest: it is held
 * by the pool's count against the PEER's limit of two, at a moment the test thread controls,
 * rather than by a race with a timer.
 *
 * WHAT DISCRIMINATES THE PEER'S NUMBER FROM THE ASSUMPTION. The peer advertises
 * SETTINGS_MAX_CONCURRENT_STREAMS = 2, which is below the driver's assumed 100 - so the pool
 * cannot arrive at 2 by assuming, and the only route to it is learnPeerLimit( )'s band inference
 * from a reading the assumption could not have produced. The settle window is set far out of
 * reach, so the pool cannot fall back on assuming after a second either; and the capacity of 2 is
 * asserted while the first request is STILL IN FLIGHT, which is before any response has completed
 * and therefore before markPeerLimitKnown( ) has been called from releaseStream( ) - the route
 * the sequential cases take and the one this case exists to avoid.
 *
 * THE ONE DURATION, and what it is for. The peer is told to say nothing at all for two seconds
 * (setOpeningDelayInMilliseconds, which gates the peer's WRITES and not its reads). That is not a
 * rendezvous - nothing waits for it - it is what makes the window in which the pool does not know
 * the peer's limit long enough to assert something about, since on loopback a peer's SETTINGS
 * otherwise arrive a fraction of a millisecond after the connection. Every actual rendezvous in
 * the case is an event: a pool answer, a peer record, or a stream closing at the sink.
 *
 * WHAT IS REAL HERE AND WHAT IS STOOD IN FOR. The pool, the HTTP/2 driver, the sockets and the
 * peer are the production objects. What the case stands in for is the request task of S5.1: it
 * submits to the connection the pool answered with and calls releaseStream( ) when the stream is
 * closed out, which is what HttpClientRequestTaskImpl does from its handling of onClosed( ). That
 * is deliberate - putting the request task in as well would make this a session case, and the two
 * session modules are both over the size target (L6 finding 8's withdrawal says why there is no
 * room in either).
 */

namespace utest
{
    namespace poolconc
    {
        typedef bl::om::ObjectImpl< bl::httpclient::ConnectionPoolImplT<> >     pool_impl_t;

        typedef bl::om::ObjectImpl
            <
                bl::tasks::Http2ConnectionTaskT< bl::tasks::TcpSocketAsyncStrandedBase >
            >
            h2_task_impl_t;

        typedef bl::httpclient::ClientDriverFactoryT< bl::tasks::TcpSocketAsyncStrandedBase >
            driver_factory_t;

        enum : std::uint32_t
        {
            /**
             * @brief What the peer advertises, and why this number
             *
             * BELOW the driver's ASSUMED_MAX_CONCURRENT_STREAMS of 100, so a pool which took the
             * assumption would dispatch all three requests at once and this case would see it;
             * ABOVE one, so that reaching it means something. Two is the smallest number with
             * both properties and is the one the L6 record's recipe names
             */

            PEER_MAX_CONCURRENT_STREAMS                 = 2U,
        };

        enum : long
        {
            /**
             * @brief How long the peer says nothing - see the note at the top of this file
             */

            OPENING_DELAY_IN_MILLISECONDS               = 2000L,

            /**
             * @brief The bound on "and it did NOT happen", a quarter of the window it sits in
             */

            NEGATIVE_WAIT_IN_MILLISECONDS               = 500L,

            /**
             * @brief The settle window, put out of reach on purpose
             *
             * Expiry is the pool's answer for a peer whose limit IS the assumed number and which
             * can therefore never distinguish itself. This peer's limit is two, so the window has
             * nothing to contribute here - and leaving it at its one second default would give
             * the pool a second route to "known" which is not the peer's number, which is exactly
             * what this case has to rule out
             */

            SETTLE_TIMEOUT_IN_SECONDS                   = 120L,
        };

        /**
         * @brief What the pool answered, and the rendezvous for waiting on it
         *
         * Held by a shared_ptr and captured by value into every callback, for the reason the
         * pool's own cases give: the pool answers from a thread of its own, outside its lock, and
         * an answer can land after the case has left the frame it would otherwise live in
         */

        class PoolAnswers FINAL
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
                record.connection =
                    bl::om::ObjPtrCopyable< bl::httpclient::ClientConnection >( connection );
                record.exception = exception;

                m_records.push_back( std::move( record ) );

                m_cv.notify_all();
            }

            std::size_t count() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records.size();
            }

            /**
             * @brief The connection the pool answered the n-th acquire( ) with
             *
             * It also states, as an assertion, that the answer was a connection rather than an
             * exception - a case which read a null connection out of a failed answer and went on
             * would report the failure much later and as something else
             */

            auto connectionAt( SAA_in const std::size_t index ) const
                -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
            {
                BL_MUTEX_GUARD( m_lock );

                UTF_REQUIRE( index < m_records.size() );

                for( std::size_t i = 0U; i < m_records.size(); ++i )
                {
                    if( m_records[ i ].index != index )
                    {
                        continue;
                    }

                    UTF_REQUIRE( nullptr == m_records[ i ].exception );
                    UTF_REQUIRE( m_records[ i ].connection );

                    return bl::om::copy( m_records[ i ].connection.get() );
                }

                UTF_FAIL( "The pool never answered the acquire the case is asking about" );

                return bl::om::ObjPtr< bl::httpclient::ClientConnection >();
            }

            /**
             * @brief Waits for 'expected' answers, and says what it actually got when it gives up
             */

            bool waitFor(
                SAA_in          const std::size_t                               expected,
                SAA_in_opt      const long                                      timeoutInMilliseconds =
                                    static_cast< long >( h2driver::DEFAULT_WAIT_IN_MILLISECONDS )
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
                            << "The pool answered "
                            << m_records.size()
                            << " acquires where the case expects "
                            << expected
                        );

                    return false;
                }

                return true;
            }
        };

        typedef std::shared_ptr< PoolAnswers >                                  answers_ptr_t;

        /**
         * @brief Disposes the pool however the case leaves
         *
         * A UTF_REQUIRE which fails leaves by throwing, and a pool with a request still queued
         * keeps itself alive through its own maintenance timer - so the failing case's pool would
         * still be there when the process tears its thread pools down, and one failure would be
         * followed by an abort with an unrelated message
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

        /**
         * @brief The pool's connection factory, built exactly as ClientSessionT builds its own
         *
         * The two policy knobs which reach nothing but a driver are carried across the same way -
         * they are what makes the factory the place where the pool's policy becomes a driver's
         * configuration. The driver factory is registered with nothing, and that is not an
         * omission: a cleartext connection configured HTTP/2 speaks it by prior knowledge (RFC
         * 9113 3.3), nothing negotiates and the ALPN fallback of design 5.5 cannot be reached
         */

        inline auto connectionFactory() -> bl::httpclient::connection_factory_t
        {
            const auto driverFactory = std::make_shared< driver_factory_t >();

            return [ driverFactory ](
                SAA_in          const bl::httpclient::ConnectionKey&            key,
                SAA_in          const bl::httpclient::ConnectionPoolPolicy&     policy
                )
                -> bl::httpclient::ConnectionAttempt
            {
                bl::tasks::Http2ConnectionConfig h2config;

                h2config.limits.drainingReserve = policy.drainingReserve;
                h2config.idleTimeout = policy.idleTimeout;

                auto task = h2_task_impl_t::createInstance(
                    bl::cpp::copy( key ),
                    driverFactory,
                    std::move( h2config ),
                    bl::tasks::ProxyConfig::none(),
                    h2driver::cleartextHttp2Config()
                    );

                const bl::om::ObjPtrCopyable< h2_task_impl_t > held( task );

                bl::httpclient::ConnectionAttempt attempt;

                attempt.task = bl::om::ObjPtrCopyable< bl::tasks::Task >(
                    bl::om::qi< bl::tasks::Task >( task )
                    );

                attempt.driver = [ held ]() -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    return bl::om::copy( held -> connection() );
                };

                return attempt;
            };
        }

        inline void acquireInto(
            SAA_in          const bl::om::ObjPtr< pool_impl_t >&                pool,
            SAA_in          const bl::httpclient::ConnectionKey&                key,
            SAA_in          const bl::httpclient::ClientRequest&                request,
            SAA_in          const answers_ptr_t&                                answers,
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

        inline auto settingOf(
            SAA_in          const std::uint32_t                                 id,
            SAA_in          const std::uint32_t                                 value
            )
            -> bl::http2::Http2Setting
        {
            bl::http2::Http2Setting setting;

            setting.id = static_cast< std::uint16_t >( id );
            setting.value = value;

            return setting;
        }

        /**
         * @brief Where a record the peer has already made sits in its list - NOT a wait
         *
         * Every record this case orders against has been waited for by content first, so the list
         * is settled by the time this reads it; what is left is the ordering, which is what the
         * case is about. Absent is a failure rather than a silent large number, because a
         * comparison between two positions one of which does not exist is not an ordering
         */

        inline std::size_t positionOf(
            SAA_in          const std::vector< std::string >&                   records,
            SAA_in          const std::string&                                  expected
            )
        {
            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                if( records[ i ] == expected )
                {
                    return i;
                }
            }

            UTF_FAIL( "The HTTP/2 test peer never recorded '" + expected + "'" );

            return records.size();
        }

        inline auto makePeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

    } // poolconc

} // utest

/************************************************************************
 * Two requests in flight on one connection, against a peer which allows exactly two
 */

UTF_AUTO_TEST_CASE( H2Pool_TwoRequestsInFlightOnOneConnectionTests )
{
    using namespace bl;
    using namespace utest;
    using namespace utest::poolconc;

    const auto peer = makePeer();

    {
        http2::Http2Profile profile;

        profile.settings.push_back(
            settingOf( http2::Globals::SETTINGS_MAX_CONCURRENT_STREAMS, PEER_MAX_CONCURRENT_STREAMS )
            );

        peer -> setProfile( profile );
    }

    peer -> setOpeningDelayInMilliseconds( OPENING_DELAY_IN_MILLISECONDS );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            h2peer::Http2ResponseScript script;

            if( 1U == request.streamIndex )
            {
                /*
                 * THE HOLD WHICH FORCES THE OVERLAP - see the note at the top of this file. The
                 * first response does not go out until the peer has been asked a second question,
                 * so a pool which serialised the two requests hangs here rather than passing
                 */

                script.awaitRequests( 2U );
            }

            /*
             * The path comes back as the body, so a sink which received another stream's response
             * is a failed assertion rather than a coincidence
             */

            return script
                .headers( 200U, http2::HpackFieldList() )
                .data( request.path )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            httpclient::ConnectionPoolPolicy policy;

            policy.settingsSettleTimeout = time::seconds( SETTLE_TIMEOUT_IN_SECONDS );

            const auto pool = pool_impl_t::createInstance( connectionFactory(), policy );

            const PoolGuard guard( pool );

            const auto key = h2driver::makeKey( "http", "127.0.0.1", port );

            const auto requestA = h2driver::makeRequest( "http://127.0.0.1/a" );
            const auto requestB = h2driver::makeRequest( "http://127.0.0.1/b" );
            const auto requestC = h2driver::makeRequest( "http://127.0.0.1/c" );

            const auto answers = std::make_shared< PoolAnswers >();

            acquireInto( pool, key, requestA, answers, 0U );
            acquireInto( pool, key, requestB, answers, 1U );
            acquireInto( pool, key, requestC, answers, 2U );

            /*
             * The first answer is the pool putting the first request onto a connection which is
             * still Connecting - the preface rider of design 5.1 and 5.4, and the only dispatch
             * the pool makes while it knows nothing about the peer
             */

            UTF_REQUIRE( answers -> waitFor( 1U ) );

            const auto connection = answers -> connectionAt( 0U );

            const auto sinkA = h2driver::RecordingSink::createInstance();
            const auto sinkB = h2driver::RecordingSink::createInstance();
            const auto sinkC = h2driver::RecordingSink::createInstance();

            sinkA -> setConnection( connection.get() );
            sinkB -> setConnection( connection.get() );
            sinkC -> setConnection( connection.get() );

            BL_SCOPE_EXIT(
                {
                    sinkA -> setConnection( nullptr );
                    sinkB -> setConnection( nullptr );
                    sinkC -> setConnection( nullptr );
                }
                );

            const auto handleA = connection -> submit(
                requestA,
                om::qi< httpclient::ClientStreamEventSink >( sinkA )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handleA );

            /*
             * THE FIRST REQUEST RIDES THE PREFACE and reaches a peer which has not yet said one
             * word - the opening delay is still running, so this record is made from a read and
             * owes nothing to anything the peer has written
             */

            h2driver::requireRecorded( peer -> recorder(), "request GET /a on stream 1" );

            /*
             * ONE UNTIL THE PEER'S LIMIT IS KNOWN. Three requests were handed to the pool at once
             * and exactly one has been dispatched; the other two are still queued behind a
             * connection which is perfectly usable and whose driver is reporting ninety-nine free
             * slots. The negative wait is what says "one" rather than "one so far", and it sits
             * inside a window four times its own length
             */

            UTF_REQUIRE( ! answers -> waitFor( 2U, NEGATIVE_WAIT_IN_MILLISECONDS ) );

            UTF_REQUIRE_EQUAL( answers -> count(), 1U );
            UTF_REQUIRE_EQUAL( pool -> waiterCount(), 2U );
            UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 1U );
            UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 1U );

            /*
             * The opening delay expires, the peer's SETTINGS arrive, the driver republishes its
             * free slots from the peer's number and the pool's next examine reads a number the
             * assumption could not have produced - one free with one out, where the assumption
             * cannot report below ninety-nine. So it dispatches a SECOND request onto the same
             * connection, and only a second
             */

            UTF_REQUIRE( answers -> waitFor( 2U ) );

            UTF_REQUIRE( answers -> connectionAt( 1U ).get() == connection.get() );

            /*
             * THE PEER'S NUMBER, AND NOT THE ASSUMPTION, AND NOT A COMPLETED RESPONSE. The first
             * request is still in flight - its response is held at the peer - so releaseStream( )
             * has not been called for anything and markPeerLimitKnown( ) has not run from there.
             * The only route to a capacity of two is learnPeerLimit( )'s band inference from what
             * the peer actually said, which is the limb of the L5 round that no composed case has
             * ever entered
             */

            UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 2U );
            UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 2U );

            /*
             * AND NOT A THIRD. The pool had all three requests in hand and the connection had two
             * slots; a pool which had taken the driver's assumption of a hundred would have
             * answered all three in the same examine. This wait is honest for the same reason the
             * one above is - nothing in the case releases a slot until the release below, so the
             * state it asserts is held by the test thread rather than by a timer
             */

            UTF_REQUIRE( ! answers -> waitFor( 3U, NEGATIVE_WAIT_IN_MILLISECONDS ) );

            UTF_REQUIRE_EQUAL( pool -> waiterCount(), 1U );

            const auto handleB = connection -> submit(
                requestB,
                om::qi< httpclient::ClientStreamEventSink >( sinkB )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handleB );

            /*
             * The second request arriving is what releases the first response, so both streams
             * finish from here - and neither could have if they had not been open together
             */

            sinkA -> waitForClosed();
            sinkB -> waitForClosed();

            UTF_REQUIRE_EQUAL( sinkA -> status(), 200U );
            UTF_REQUIRE_EQUAL( sinkA -> body(), std::string( "/a" ) );
            UTF_REQUIRE( ! sinkA -> errorCode() );

            UTF_REQUIRE_EQUAL( sinkB -> status(), 200U );
            UTF_REQUIRE_EQUAL( sinkB -> body(), std::string( "/b" ) );
            UTF_REQUIRE( ! sinkB -> errorCode() );

            /*
             * What the request task of S5.1 does from its handling of onClosed( ), which this
             * case stands in for: the slot goes back and the third request takes it
             */

            pool -> releaseStream( connection, handleA, httpclient::RequestOutcome::Completed );

            UTF_REQUIRE( answers -> waitFor( 3U ) );

            UTF_REQUIRE( answers -> connectionAt( 2U ).get() == connection.get() );

            const auto handleC = connection -> submit(
                requestC,
                om::qi< httpclient::ClientStreamEventSink >( sinkC )
                );

            UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handleC );

            sinkC -> waitForClosed();

            UTF_REQUIRE_EQUAL( sinkC -> status(), 200U );
            UTF_REQUIRE_EQUAL( sinkC -> body(), std::string( "/c" ) );
            UTF_REQUIRE( ! sinkC -> errorCode() );

            pool -> releaseStream( connection, handleB, httpclient::RequestOutcome::Completed );
            pool -> releaseStream( connection, handleC, httpclient::RequestOutcome::Completed );

            /*
             * ONE CONNECTION CARRIED ALL THREE, which is what makes the two above concurrent
             * rather than merely simultaneous: three streams, three slots taken and given back,
             * nothing retired and nothing failed
             */

            const auto stats = pool -> stats();

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.connectionsRetired.value(), 0U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 3U );
            UTF_REQUIRE_EQUAL( stats.released.value(), 3U );
            UTF_REQUIRE_EQUAL( stats.failures.value(), 0U );

            UTF_REQUIRE_EQUAL( pool -> slotsInUse( connection ), 0U );
            UTF_REQUIRE_EQUAL( pool -> dispatchCapacity( connection ), 2U );

            UTF_REQUIRE( httpclient::ConnectionState::Ready == connection -> state() );

            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 1U );
            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 3U );
            h2driver::requireStreamClosedAtPeer( peer -> recorder(), 5U );

            UTF_REQUIRE( peer -> recorder().failure().empty() );

            /*
             * THE ORDER AT THE PEER, which is the same two statements read from the other end of
             * the socket. The second request arrived BEFORE the first was answered, so streams 1
             * and 3 were open at the peer at the same moment; the third did not arrive until
             * after, because the pool was holding it against a limit of two
             */

            const auto records = peer -> recorder().records();

            UTF_REQUIRE(
                positionOf( records, "request GET /b on stream 3" ) <
                positionOf( records, "responded 200 on stream 1" )
                );

            UTF_REQUIRE(
                positionOf( records, "responded 200 on stream 1" ) <
                positionOf( records, "request GET /c on stream 5" )
                );
        }
        );
}

#endif /* __UTEST_TESTCONNECTIONPOOLCONCURRENCY_H_ */
