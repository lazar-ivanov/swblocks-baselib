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

#ifndef __UTEST_TESTTCPCONNECTLOOP_H_
#define __UTEST_TESTTCPCONNECTLOOP_H_

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/NetUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <utests/baselib/HeldIoThreads.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * THE CONNECTION ESTABLISHER'S CONNECT OVER SEVERAL ENDPOINTS (D3, change-set CS-6)
 *
 * WHAT IS UNDER TEST. TcpConnectionEstablisherConnector connects to the resolved endpoints with
 * asio::async_connect( ) over the range - asio's iterator_connect_op, which between two attempts
 * closes the socket and opens it again for the next endpoint on an I/O thread, in its own
 * intermediate handler, under no lock of ours. A cancel reads the same socket under the task lock,
 * and ThreadSanitizer reported the race on both plain policies; and a cancel which reaps one attempt
 * does not stop the loop, which starts the next
 * (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md, section 9). The decided fix is a loop
 * of the library's own, whose attempts complete in task handlers.
 *
 * Every case substitutes its own endpoints for the resolver's one, in the probe's
 * continueAfterResolved( ), so that the connect runs over two endpoints however this host resolves a
 * name. 127.0.0.2 is loopback, and nothing listens there: a connect to it is refused at once.
 *
 * THE CHARACTERIZATION, committed before the fix and green on both sides of it - what the loop keeps
 * of asio's, over the cleartext policies, with and without a strand:
 *
 *   - TcpConnectLoop_TheFirstEndpointWhichAcceptsIsTheOneConnectedTests - two listeners: the task
 *     connects to the first; with the first refused, it connects to the second.
 *   - TcpConnectLoop_NoEndpointAcceptingFailsTheTaskTests - both refused: the task fails with the
 *     refusal.
 *
 * AND D3'S RED, committed before the fix, and certain - nothing is timed, because no attempt can
 * complete:
 *
 *   - TcpConnectLoop_ACancelDuringTheConnectEndsItPromptlyTests - two listeners whose accept queues
 *     are full, so every SYN is dropped, over the four policies. The cancel is requested in the
 *     resolve handler, right after the first attempt is issued, with the I/O pool's other threads
 *     held. Today it reaps that attempt, and asio, finding the socket still open, starts the
 *     second: the task is still running at the bound, and ends only once the case closes the
 *     listeners. After the fix the cancel is checked between two attempts, and the task ends at
 *     once, as a cancel. Without the held threads a plain policy's red would be probable only: its
 *     cancel runs on the requesting thread, and another I/O thread could complete the first
 *     attempt and open the second between the cancel's shutdown and its cancel - D3's race itself -
 *     so that the cancel reaped the second attempt by chance. Measured once without them: the
 *     cleartext plain policy's task ended at once, which that race is what explains (INFERRED).
 *
 * AND THE CASE THREADSANITIZER REPORTS TODAY - green in any other build, on both sides of the fix:
 *
 *   - TcpConnectLoop_ACancelAfterTheSwitchIsOrderedWithItTests - the first endpoint refused, the
 *     second a full accept queue. The case waits until /proc/net/tcp shows the second attempt's
 *     socket in SYN_SENT - every write of the switch comes before the connect( ) which puts it
 *     there - and only then cancels, over the four policies. Today ThreadSanitizer reports the
 *     cancel's reads of the socket racing the switch's writes; after the fix the switch runs in a
 *     task handler, under the lock the cancel takes, and it reports nothing. The rendezvous adds no
 *     happens-before edge which covers the switch: the switch's thread goes on to connect( ), which
 *     only an accept( ) acquires, and nothing accepts. Linux only.
 */

namespace utest
{
    namespace connectloop
    {
        using heldiothreads::HeldIoThreads;

        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,

            /**
             * @brief How long a case gives its task to end after its cancel, before it closes the
             * listeners - far above a prompt ending
             */

            CANCEL_BOUND_IN_MILLISECONDS        = 5000U,
        };

        inline auto describeCode( SAA_in const bl::eh::error_code& ec ) -> std::string
        {
            if( ! ec )
            {
                return "success";
            }

            return std::string( ec.category().name() ) +
                ":" +
                bl::utils::lexical_cast< std::string >( ec.value() ) +
                " (" +
                ec.message() +
                ")";
        }

        /**
         * @brief Fails with a diagnosis, and otherwise counts the assertion
         */

        inline void chkOrFail(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( ! condition )
            {
                UTF_FAIL( message );
            }

            UTF_REQUIRE( condition );
        }

        /**
         * @brief A one-shot signal a probe raises on one of its threads and a case waits for
         */

        class Latch
        {
            BL_NO_COPY_OR_MOVE( Latch )

        public:

            Latch()
                :
                m_isSet( false )
            {
            }

            void set()
            {
                BL_MUTEX_GUARD( m_lock );

                m_isSet = true;

                m_cv.notify_all();
            }

            bool waitFor( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isSet;
                    }
                    );
            }

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            bool                                                                m_isSet;
        };

        /**
         * @brief A loopback listener which never accepts - optionally with its accept queue full,
         * so that the kernel drops every further SYN and a connect to it stays in SYN_SENT
         *
         * The queue is filled the way Linux counts it: listen( 0 ) and one connection, which the
         * kernel completes into the queue and nothing accepts
         */

        class Listener
        {
            BL_NO_COPY_OR_MOVE( Listener )

        public:

            explicit Listener( SAA_in const bool isQueueFull )
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

                if( isQueueFull )
                {
                    m_acceptor.listen( 0 );
                }
                else
                {
                    m_acceptor.listen();
                }

                m_port = m_acceptor.local_endpoint().port();

                if( isQueueFull )
                {
                    m_filler.connect( bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ) );
                }
            }

            bl::os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            auto endpoint() const -> bl::asio::ip::tcp::endpoint
            {
                return bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port );
            }

            /**
             * @brief The same port on 127.0.0.2, where nothing listens - a connect to it is refused
             */

            auto refusingEndpoint() const -> bl::asio::ip::tcp::endpoint
            {
                return bl::asio::ip::tcp::endpoint( bl::asio::ip::make_address( "127.0.0.2" ), m_port );
            }

            void close()
            {
                bl::eh::error_code ec;

                m_filler.close( ec );
                m_acceptor.close( ec );
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            bl::asio::ip::tcp::socket                                           m_filler;
            bl::os::port_t                                                      m_port;
        };

        /**
         * @brief Where a case requests its cancel
         */

        enum class CancelPoint
        {
            /**
             * @brief Nowhere - the connect runs to its end
             */

            None,

            /**
             * @brief In the resolve handler, under its task lock, right after the first attempt is
             * issued - with the I/O pool's other threads held, so that the cancel's shutdown and
             * cancel both run before anything completes the attempt
             */

            InTheResolveHandler,

            /**
             * @brief From the case, once /proc/net/tcp shows the second attempt in SYN_SENT
             */

            AfterTheSwitch,
        };

        /**
         * @brief A connection establisher over STREAM which connects to the case's endpoints in
         * place of the resolver's, signals once the first attempt is in flight, and records the
         * endpoint it connected to
         */

        template
        <
            typename STREAM
        >
        class ConnectLoopProbeT :
            public bl::tasks::TcpConnectionEstablisherConnector< STREAM >
        {
            BL_DECLARE_OBJECT_IMPL( ConnectLoopProbeT )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< STREAM >      base_type;

        protected:

            Latch                                                               m_connectStarted;
            Latch                                                               m_stopped;

            const std::vector< bl::asio::ip::tcp::endpoint >                   m_endpoints;
            const CancelPoint                                                   m_cancelPoint;

            mutable bl::os::mutex                                               m_recordLock;
            std::string                                                         m_connectedTo;

            ConnectLoopProbeT(
                SAA_in                  std::vector< bl::asio::ip::tcp::endpoint >&& endpoints,
                SAA_in                  const CancelPoint                       cancelPoint
                )
                :
                base_type( std::string( "127.0.0.1" ), 1U /* port, never used */, false /* logExceptions */ ),
                m_endpoints( BL_PARAM_FWD( endpoints ) ),
                m_cancelPoint( cancelPoint )
            {
                /*
                 * A TLS policy never reaches its handshake here; no retry could either
                 */

                base_type::m_maxRetryCount = 0U;
            }

            virtual bool continueAfterResolved( SAA_in typename base_type::tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                BL_UNUSED( endpoints );

                typedef typename base_type::tcp_resolver_type::results_type     results_t;

                const auto results = results_t::create(
                    m_endpoints.begin(),
                    m_endpoints.end(),
                    std::string( "localhost" ),
                    std::string( "0" )
                    );

                /*
                 * Under the resolve handler's task lock: the first attempt is issued inside
                 */

                const auto result = base_type::continueAfterResolved( results.begin() );

                if( CancelPoint::InTheResolveHandler == m_cancelPoint )
                {
                    base_type::requestCancelInternal();
                }

                m_connectStarted.set();

                return result;
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                bl::eh::error_code ec;

                const auto remote = base_type::getSocket().remote_endpoint( ec );

                {
                    BL_MUTEX_GUARD( m_recordLock );

                    m_connectedTo = ec ? "unknown: " + describeCode( ec ) : bl::net::formatEndpointId( remote );
                }

                return false;
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stopped.set();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForConnectStarted() const
            {
                return m_connectStarted.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stopped.waitFor( timeoutInMilliseconds );
            }

            auto connectedTo() const -> std::string
            {
                BL_MUTEX_GUARD( m_recordLock );

                return m_connectedTo;
            }
        };

        template
        <
            typename STREAM
        >
        using ConnectLoopProbeImpl = bl::om::ObjectImpl< ConnectLoopProbeT< STREAM > >;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct ConnectResult
        {
            bool                                                                isConnectStarted;
            bool                                                                isRendezvousMet;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
            bool                                                                hasStopped;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            std::string                                                         connectedTo;

            /*
             * The task, kept for as long as its result is: a case which runs several policies keeps
             * every probe alive until it ends, so that no two share an address - ThreadSanitizer
             * reports one race per address, and one policy's report could otherwise hide the next's
             */

            bl::om::ObjPtrCopyable< bl::tasks::Task >                           keepAlive;

            ConnectResult()
                :
                isConnectStarted( false ),
                isRendezvousMet( true ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
                hasStopped( false ),
                isFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "connect started " ) +
                    ( isConnectStarted ? "yes" : "no" ) +
                    ", rendezvous met " +
                    ( isRendezvousMet ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", stopped " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
                    ", connected to " +
                    ( connectedTo.empty() ? std::string( "nothing" ) : connectedTo );
            }
        };

        /**
         * @brief Whether /proc/net/tcp shows a socket in SYN_SENT ( state 02 ) towards 'endpoint'
         *
         * The file gives each address as the in-memory hex of its four octets - so 127.0.0.1 reads
         * 0100007F on a little endian host - and each port as four hex digits
         */

        inline bool isSynSentTowards( SAA_in const bl::asio::ip::tcp::endpoint& endpoint )
        {
            std::ifstream file( "/proc/net/tcp" );

            const auto octets = endpoint.address().to_v4().to_bytes();

            char remote[ 16 ];

            ( void ) std::snprintf(
                remote,
                sizeof( remote ),
                "%02X%02X%02X%02X:%04X",
                static_cast< unsigned >( octets[ 3 ] ),
                static_cast< unsigned >( octets[ 2 ] ),
                static_cast< unsigned >( octets[ 1 ] ),
                static_cast< unsigned >( octets[ 0 ] ),
                static_cast< unsigned >( endpoint.port() )
                );

            std::string line;

            while( std::getline( file, line ) )
            {
                std::istringstream fields( line );

                std::string slot;
                std::string local;
                std::string rem;
                std::string state;

                if( ( fields >> slot >> local >> rem >> state ) && rem == remote && state == "02" )
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * @brief Runs a probe over the endpoints given, with its cancel where 'cancelPoint' says -
         * after the switch, once /proc/net/tcp shows an attempt towards 'awaitSynSentTowards' in
         * SYN_SENT - and closes the listeners only once the bound is over
         */

        template
        <
            typename STREAM
        >
        inline auto runConnect(
            SAA_in          std::vector< bl::asio::ip::tcp::endpoint >&&        endpoints,
            SAA_in          const CancelPoint                                   cancelPoint,
            SAA_in          const bl::cpp::void_callback_t&                     closeListeners,
            SAA_in_opt      const bl::asio::ip::tcp::endpoint*                  awaitSynSentTowards = nullptr
            )
            -> ConnectResult
        {
            using namespace bl;
            using namespace bl::tasks;

            ConnectResult result;

            const auto probe = ConnectLoopProbeImpl< STREAM >::createInstance( BL_PARAM_FWD( endpoints ), cancelPoint );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.isConnectStarted = probe -> waitForConnectStarted();

                    if( CancelPoint::AfterTheSwitch == cancelPoint && awaitSynSentTowards )
                    {
                        /*
                         * A state, polled - not a time: the switch's writes all come before the
                         * connect( ) which puts this socket in SYN_SENT
                         */

                        result.isRendezvousMet = false;

                        for( std::size_t i = 0U; i < static_cast< std::size_t >( WAIT_IN_MILLISECONDS ); ++i )
                        {
                            if( isSynSentTowards( *awaitSynSentTowards ) )
                            {
                                result.isRendezvousMet = true;

                                break;
                            }

                            os::sleep( time::milliseconds( 1 ) );
                        }

                        task -> requestCancel();
                    }

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( CANCEL_BOUND_IN_MILLISECONDS ) );

                    result.isCanceled = probe -> isCanceled();

                    closeListeners();

                    result.hasStopped = probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( task );
                }
                );

            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            result.connectedTo = probe -> connectedTo();

            result.keepAlive = om::ObjPtrCopyable< Task >( task );

            return result;
        }

        /**
         * @brief A cancelled run: the task stopped inside the bound, as a cancel
         */

        inline void chkCancelEndedItPromptly(
            SAA_in          const ConnectResult&                                result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.isConnectStarted && result.isRendezvousMet,
                which + ": the connect never reached the point the cancel is aimed at; " + result.describe()
                );

            chkOrFail(
                result.hasStoppedWithinBound && result.isCanceled,
                which + ": the task did not end within the bound after its cancel; " + result.describe()
                );

            chkOrFail(
                result.isFailed && bl::asio::error::operation_aborted == result.taskCode,
                which + ": the task did not end as a cancel; " + result.describe()
                );
        }

        /**
         * @brief Two listeners with full accept queues, and a cancel requested in the resolve
         * handler, right after the first attempt is issued
         *
         * The I/O pool's other threads are held, so that nothing completes the first attempt
         * between the cancel's shutdown and its cancel: a plain policy's cancel runs on the thread
         * which requests it, and an attempt which the shutdown ends could otherwise be completed,
         * and the next one opened, on another thread before the cancel reads the socket - which is
         * D3's race itself, and would reap the second attempt by chance
         */

        template
        <
            typename STREAM
        >
        inline auto runACancelDuringTheConnect() -> ConnectResult
        {
            Listener first( true /* isQueueFull */ );
            Listener second( true /* isQueueFull */ );

            std::vector< bl::asio::ip::tcp::endpoint > endpoints;

            endpoints.push_back( first.endpoint() );
            endpoints.push_back( second.endpoint() );

            HeldIoThreads held;

            chkOrFail( held.isHeld(), "the I/O pool's other threads could not be held" );

            return runConnect< STREAM >(
                std::move( endpoints ),
                CancelPoint::InTheResolveHandler,
                [ &first, &second ]() -> void
                {
                    first.close();
                    second.close();
                }
                );
        }

        /**
         * @brief The first endpoint refused at once, the second a full accept queue, and a cancel
         * requested once the second attempt's socket is in SYN_SENT
         */

        template
        <
            typename STREAM
        >
        inline auto runACancelAfterTheSwitch() -> ConnectResult
        {
            Listener listener( true /* isQueueFull */ );

            std::vector< bl::asio::ip::tcp::endpoint > endpoints;

            endpoints.push_back( listener.refusingEndpoint() );
            endpoints.push_back( listener.endpoint() );

            const auto awaited = listener.endpoint();

            return runConnect< STREAM >(
                std::move( endpoints ),
                CancelPoint::AfterTheSwitch,
                [ &listener ]() -> void
                {
                    listener.close();
                },
                &awaited
                );
        }

        /**
         * @brief Connects over the endpoints given, with no cancel, and returns what came of it
         */

        template
        <
            typename STREAM
        >
        inline auto runPlainConnect( SAA_in std::vector< bl::asio::ip::tcp::endpoint >&& endpoints ) -> ConnectResult
        {
            return runConnect< STREAM >(
                BL_PARAM_FWD( endpoints ),
                CancelPoint::None,
                []() -> void
                {
                }
                );
        }

        template
        <
            typename STREAM
        >
        inline void chkTheFirstWhichAcceptsIsConnected( SAA_in const std::string& which )
        {
            Listener first( false /* isQueueFull */ );
            Listener second( false /* isQueueFull */ );

            {
                std::vector< bl::asio::ip::tcp::endpoint > endpoints;

                endpoints.push_back( first.endpoint() );
                endpoints.push_back( second.endpoint() );

                const auto result = runPlainConnect< STREAM >( std::move( endpoints ) );

                chkOrFail(
                    result.hasStoppedWithinBound &&
                        ! result.isFailed &&
                        bl::net::formatEndpointId( first.endpoint() ) == result.connectedTo,
                    which + ", both accepting: the task did not connect to the first; " + result.describe()
                    );
            }

            {
                std::vector< bl::asio::ip::tcp::endpoint > endpoints;

                endpoints.push_back( first.refusingEndpoint() );
                endpoints.push_back( second.endpoint() );

                const auto result = runPlainConnect< STREAM >( std::move( endpoints ) );

                chkOrFail(
                    result.hasStoppedWithinBound &&
                        ! result.isFailed &&
                        bl::net::formatEndpointId( second.endpoint() ) == result.connectedTo,
                    which + ", the first refused: the task did not connect to the second; " + result.describe()
                    );
            }
        }

        template
        <
            typename STREAM
        >
        inline void chkNoneAcceptingFailsTheTask( SAA_in const std::string& which )
        {
            Listener first( false /* isQueueFull */ );
            Listener second( false /* isQueueFull */ );

            std::vector< bl::asio::ip::tcp::endpoint > endpoints;

            endpoints.push_back( first.refusingEndpoint() );
            endpoints.push_back( second.refusingEndpoint() );

            const auto result = runPlainConnect< STREAM >( std::move( endpoints ) );

            chkOrFail(
                result.hasStoppedWithinBound &&
                    result.isFailed &&
                    bl::asio::error::connection_refused == result.taskCode &&
                    result.connectedTo.empty(),
                which + ": the task did not fail with the refusal; " + result.describe()
                );
        }

    } // connectloop

} // utest

/**
 * @brief CHARACTERIZATION - the endpoints are tried in order, and the first which accepts is the one
 * connected
 */

UTF_AUTO_TEST_CASE( TcpConnectLoop_TheFirstEndpointWhichAcceptsIsTheOneConnectedTests )
{
    using namespace bl::tasks;
    using namespace utest::connectloop;

    chkTheFirstWhichAcceptsIsConnected< TcpSocketAsyncBase >( "cleartext" );
    chkTheFirstWhichAcceptsIsConnected< TcpSocketAsyncStrandedBase >( "cleartext, stranded" );
}

/**
 * @brief CHARACTERIZATION - with no endpoint accepting, the task fails with the refusal - the last
 * attempt's error, and both attempts' here
 */

UTF_AUTO_TEST_CASE( TcpConnectLoop_NoEndpointAcceptingFailsTheTaskTests )
{
    using namespace bl::tasks;
    using namespace utest::connectloop;

    chkNoneAcceptingFailsTheTask< TcpSocketAsyncBase >( "cleartext" );
    chkNoneAcceptingFailsTheTask< TcpSocketAsyncStrandedBase >( "cleartext, stranded" );
}

/**
 * @brief D3'S RED - a cancel during a connect over several endpoints ends it at once, over the four
 * stream policies
 */

UTF_AUTO_TEST_CASE( TcpConnectLoop_ACancelDuringTheConnectEndsItPromptlyTests )
{
    using namespace bl::tasks;
    using namespace utest::connectloop;

    /*
     * All four are run before any is asserted, so that a failure's message carries every reading
     */

    const ConnectResult results[] =
    {
        runACancelDuringTheConnect< TcpSocketAsyncBase >(),
        runACancelDuringTheConnect< TcpSocketAsyncStrandedBase >(),
        runACancelDuringTheConnect< TcpSslSocketAsyncBase >(),
        runACancelDuringTheConnect< TcpSslSocketAsyncStrandedBase >(),
    };

    const char* const names[] =
    {
        "cleartext",
        "cleartext, stranded",
        "TLS",
        "TLS, stranded",
    };

    std::string readings;

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        readings += std::string( names[ i ] ) + ": " + results[ i ].describe() + "; ";
    }

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        chkCancelEndedItPromptly( results[ i ], std::string( names[ i ] ) + " (of " + readings + ")" );
    }
}

/**
 * @brief THREADSANITIZER'S CASE - a cancel which lands after the switch to the next endpoint is
 * ordered with it, over the four stream policies; green in any other build
 */

UTF_AUTO_TEST_CASE( TcpConnectLoop_ACancelAfterTheSwitchIsOrderedWithItTests )
{
    using namespace bl::tasks;
    using namespace utest::connectloop;

    if( ! std::ifstream( "/proc/net/tcp" ) )
    {
        UTF_MESSAGE( "skipped: this platform has no /proc/net/tcp, which the case's rendezvous reads" );

        return;
    }

    /*
     * All four are run before any is asserted, and each keeps its task alive until then
     */

    const ConnectResult results[] =
    {
        runACancelAfterTheSwitch< TcpSocketAsyncBase >(),
        runACancelAfterTheSwitch< TcpSocketAsyncStrandedBase >(),
        runACancelAfterTheSwitch< TcpSslSocketAsyncBase >(),
        runACancelAfterTheSwitch< TcpSslSocketAsyncStrandedBase >(),
    };

    const char* const names[] =
    {
        "cleartext",
        "cleartext, stranded",
        "TLS",
        "TLS, stranded",
    };

    std::string readings;

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        readings += std::string( names[ i ] ) + ": " + results[ i ].describe() + "; ";
    }

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( results ); ++i )
    {
        chkCancelEndedItPromptly( results[ i ], std::string( names[ i ] ) + " (of " + readings + ")" );
    }
}

#endif /* __UTEST_TESTTCPCONNECTLOOP_H_ */
