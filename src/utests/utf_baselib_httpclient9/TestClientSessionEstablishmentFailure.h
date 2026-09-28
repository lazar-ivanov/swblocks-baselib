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

#ifndef __UTEST_TESTCLIENTSESSIONESTABLISHMENTFAILURE_H_
#define __UTEST_TESTCLIENTSESSIONESTABLISHMENTFAILURE_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The establishment failure through a REAL pool and a REAL session - E2 of astra's second review's
 * change-set CS-2 ( the L6 review's second pass, and the owed list's E2 )
 *
 * WHAT WAS NEVER RUN. The establishment failure was run at the driver ( the dead-port case ) and in
 * the pool against stub connections, never with a real connection task through both. So two things
 * were pinned by reading only:
 *
 *   - L5 section 9's obligation: when the pool's establishment bound expires it cancels a REAL
 *     connection task in the middle of establishing, and that task has to end - it is what answers
 *     the request which rode its preface, and until it has, the pool cannot let go of the entry;
 *   - and L6 finding 4's second part: the rider is bounced with connection_aborted whatever ended
 *     the establishment, so the request task chains the connection task's own exception onto its
 *     failure ( connectionFailureCause( ) ) - the caller must see WHY.
 *
 * THE ORIGIN NEVER ACCEPTS, AND ITS QUEUE IS FULL. A listening socket completes TCP handshakes into
 * its accept queue whether or not anything accepts, so a plain one would establish every
 * connection. This one listens with a backlog of zero and the case fills that queue with one
 * connection of its own first; the kernel then DROPS every further SYN, and the session's connect
 * neither completes nor fails - it hangs until the pool's bound expires. Deterministic, unlike a port
 * just released, which another process can take between two calls. MEASURED ON LINUX before the
 * case was written: the second connect to such a listener timed out on the client's own clock.
 * Winsock answers a full queue with a reset instead, so there the attempt is refused rather than
 * abandoned - which is why the one assertion that names the bound is Linux's alone.
 *
 * WHY HTTP/2 BY PRIOR KNOWLEDGE. The chained cause belongs to the DISPATCHED path: only a request
 * which rode the preface is bounced by the connection task and so can be told the task's failure. A
 * cleartext session rides the preface only where it can produce HTTP/2, so this session is
 * configured for it - the same ClientSessionT over the same stream policy as the rest of this
 * module, which instantiates the HTTP/2 connection task already.
 */

namespace utest
{
    namespace plainsession
    {
        /**
         * @brief A loopback origin whose listening socket never accepts, with its queue already full
         *
         * Synchronous and on no thread at all: the listener and the one connection filling its queue
         * are made in the constructor and closed in the destructor, and nothing ever reads either
         */

        class FullQueueOrigin
        {
            BL_NO_COPY_OR_MOVE( FullQueueOrigin )

        public:

            FullQueueOrigin()
                :
                m_acceptor( m_ioService ),
                m_filler( m_ioService ),
                m_port( 0U )
            {
                const bl::asio::ip::tcp::endpoint endpoint(
                    bl::asio::ip::address_v4::loopback(),
                    0 /* ephemeral */
                    );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );

                /*
                 * A BACKLOG OF ZERO, which the kernel holds at one completed connection - the one
                 * made below
                 */

                m_acceptor.listen( 0 );

                m_port = m_acceptor.local_endpoint().port();

                m_filler.connect(
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port )
                    );
            }

            ~FullQueueOrigin() NOEXCEPT
            {
                bl::eh::error_code ec;

                m_filler.close( ec );
                m_acceptor.close( ec );
            }

            bl::os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::ip::tcp::socket                                           m_filler;
            bl::os::port_t                                                      m_port;
        };

        /**
         * @brief The nested cause a failure carries, or null when it carries none
         */

        inline auto nestedCauseOf( SAA_in const std::exception_ptr& eptr ) -> std::exception_ptr
        {
            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::eh::exception& e )
            {
                const auto* const nested = bl::eh::get_error_info< bl::eh::errinfo_nested_exception_ptr >( e );

                if( nested )
                {
                    return *nested;
                }
            }
            catch( std::exception& )
            {
            }

            return std::exception_ptr();
        }

        /**
         * @brief Whether any exception down a chain of nested causes names this endpoint port
         *
         * Walked rather than searched for in the diagnostic text, so that a port number which
         * happens to appear in a timestamp cannot satisfy it. Bounded, so a cycle cannot hang it
         */

        inline bool chainNamesThePort(
            SAA_in          const std::exception_ptr&                           eptr,
            SAA_in          const unsigned short                                port
            )
        {
            auto current = eptr;

            for( std::size_t depth = 0U; current && depth < 16U; ++depth )
            {
                try
                {
                    std::rethrow_exception( current );
                }
                catch( bl::eh::exception& e )
                {
                    const auto* const named = bl::eh::get_error_info< bl::eh::errinfo_endpoint_port >( e );

                    if( named && port == *named )
                    {
                        return true;
                    }
                }
                catch( std::exception& )
                {
                }

                current = nestedCauseOf( current );
            }

            return false;
        }

        /**
         * @brief Whether any exception down a chain of nested causes is a TimeoutException which
         * says the establishment bound expired - the pool's reason for its cancel ( owed-list row I6 )
         *
         * Walked, and bounded, as chainNamesThePort( ) is
         */

        inline bool chainNamesTheEstablishmentTimeout( SAA_in const std::exception_ptr& eptr )
        {
            auto current = eptr;

            for( std::size_t depth = 0U; current && depth < 16U; ++depth )
            {
                try
                {
                    std::rethrow_exception( current );
                }
                catch( bl::TimeoutException& e )
                {
                    if( std::string::npos != std::string( e.what() ).find( "did not become usable within" ) )
                    {
                        return true;
                    }
                }
                catch( std::exception& )
                {
                }

                current = nestedCauseOf( current );
            }

            return false;
        }

    } // plainsession

} // utest

/**
 * @brief A request to an origin which never accepts: it fails with the connection's own failure
 * chained, after exactly the bounded number of establishments, and every one of them let go
 *
 * TWO ESTABLISHMENTS, BECAUSE maxRetriesPerRequest IS ONE. The request rides the preface of the
 * first connection; its bound expires; the pool retires the entry and cancels the task; the task
 * ends and bounces the rider with connection_aborted, retryable; the session replays it, it rides a
 * second connection, and the same happens once more, after which the retry is refused.
 *
 * WHAT THE COUNTS SAY about the real cancelled tasks, which is L5 section 9's obligation. The pool
 * forgets a retired entry only once it holds no slot, and the slot here is the rider's - given back
 * by the request task only when the connection task has ANSWERED the rider, which it does from
 * inside its own ending ( closeSubmissions( ), from onTaskStoppedNothrow( ) ). So a pool holding no
 * connection at the end, with both slots returned, is two real connection tasks which were
 * cancelled mid-connect and ended - and the chain could not have failed at all otherwise: a task
 * which did not end would have held its rider until the request's own thirty-minute bound
 */

UTF_AUTO_TEST_CASE( ClientSession_AnOriginWhichNeverAcceptsFailsWithTheConnectionsCauseTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    FullQueueOrigin origin;

    httpclient::ClientSessionConfig config;

    config.connectionConfig.cleartextProtocol = httpclient::HttpProtocol::Http2;

    config.poolPolicy.establishmentTimeout = time::milliseconds( 200 );
    config.poolPolicy.maxRetriesPerRequest = 1U;

    const auto session = makeSession( std::move( config ) );

    BL_SCOPE_EXIT_WARN_ON_FAILURE(
        {
            session -> dispose();
        },
        "utest::plainsession::ClientSession_AnOriginWhichNeverAcceptsFailsWithTheConnectionsCauseTests"
        );

    const auto requestTask = session -> createRequestTask( makeRequest( origin.port(), "/" ) );

    const auto task = om::qi< tasks::Task >( requestTask );

    runSessionTask( task );

    requireTrue( task -> isFailed(), "a request to an origin which never accepts should have failed" );

    /*
     * THE FAILURE THE CALLER SEES: the bounce - "The HTTP request failed", connection_aborted - with
     * the connection task's own failure chained to it, which is what says why
     */

    const auto failure = failureOf( task );

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "The request to an origin which never accepts failed with:\n"
            << failure
        );

    requireTrue(
        std::string::npos != failure.find( "The HTTP request failed" ),
        "the request should have failed as a bounced rider, and it reports: " + failure
        );

    requireTrue(
        nullptr != nestedCauseOf( task -> exception() ),
        "the failure carries no chained cause - the connection's failure was lost: " + failure
        );

    /*
     * AND THE CHAIN NAMES THE ORIGIN: the connector's own exception, at the bottom of it, carries
     * the endpoint it could not reach - what a caller needs to tell this failure from any other
     * connection_aborted
     */

    requireTrue(
        chainNamesThePort( task -> exception(), origin.port() ),
        "no cause in the chain names the origin's port: " + failure
        );

    /*
     * THE BOUNDED NUMBER OF ESTABLISHMENTS, each one dispatched to and each slot given back
     */

    const auto stats = statsOf( session );

    UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 2U );
    UTF_REQUIRE_EQUAL( stats.dispatched.value(), 2U );
    UTF_REQUIRE_EQUAL( stats.released.value(), 2U );
    UTF_REQUIRE_EQUAL( stats.connectionsRetired.value(), 2U );

#if defined( __linux__ )

    /*
     * AND EACH WAS ABANDONED BY THE BOUND, which is what makes the tasks the pool retired CANCELLED
     * ones. Linux only: see the header - a full queue drops the SYN here, where Winsock resets it
     */

    UTF_REQUIRE_EQUAL( stats.establishmentTimeouts.value(), 2U );

    /*
     * AND THE CHAIN SAYS SO - owed-list row I6. The connector's own failure is the operation_aborted
     * of the pool's cancel, which names the endpoint and not why it was cancelled; the pool gives the
     * connection its reason with the cancel, and the connection chains it onto that failure. RED
     * BEFORE I6: "Operation canceled" and nothing about the bound. Checked and not required, so that
     * the assertions below still run
     */

    UTF_CHECK( chainNamesTheEstablishmentTimeout( task -> exception() ) );

#endif

    /*
     * AND NOTHING IS LEFT: both entries were forgotten, which the pool does for an entry holding a
     * rider only after the cancelled task has ended and answered it
     */

    UTF_REQUIRE_EQUAL(
        om::qi< httpclient::ConnectionPoolImpl >( session -> pool() ) -> connectionCount(),
        0U
        );
}

#endif /* __UTEST_TESTCLIENTSESSIONESTABLISHMENTFAILURE_H_ */
