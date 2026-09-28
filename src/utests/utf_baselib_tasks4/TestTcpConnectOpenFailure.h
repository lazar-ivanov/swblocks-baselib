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

#ifndef __UTEST_TESTTCPCONNECTOPENFAILURE_H_
#define __UTEST_TESTTCPCONNECTOPENFAILURE_H_

#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/NetUtils.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cerrno>
#include <cstddef>
#include <string>
#include <vector>

#if ! defined( _WIN32 )
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <utests/baselib/HeldIoThreads.h>
#include <utests/baselib/Utf.h>

#include "TestTcpConnectLoop.h"

/************************************************************************
 * AN ENDPOINT WHOSE SOCKET CANNOT BE OPENED (D-A, change-set CS-6)
 *
 * WHAT IS UNDER TEST. asio::async_connect( ) opens a closed socket inline, and when that open fails it
 * posts the open's error with the socket still closed (basic_socket.hpp, initiate_async_connect). asio's
 * ranged connect then found the socket closed and ended the whole connect with operation_aborted: a
 * cancel reported by a task nobody had cancelled, with no later endpoint tried. The connection
 * establisher's own loop (D3) treats that attempt like any other which fails - the next endpoint is
 * tried, and after the last the open's own error is reported - and the maintainer decided to keep that,
 * record it and test it (D-A; notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md, sections 9
 * and 10).
 *
 * THE OPEN IS MADE TO FAIL WITH NO SEAM: the process runs out of descriptors, for the first attempt only.
 * Before the task starts the case lowers the soft RLIMIT_NOFILE a little above the lowest free
 * descriptor, so that filling the table takes a few dups. The probe fills it inside its
 * continueAfterResolved( ) - under the resolve handler's task lock, on the one free I/O thread, the
 * pool's others held - lets the base create the socket and start the first attempt, whose inline open
 * fails with EMFILE, and closes the fillers before it returns. That attempt's completion is posted, so
 * it runs only after them: the one free I/O thread is the one returning, and the loop's handler takes
 * the task lock besides. Nothing is timed, and the cleartext policy keeps OpenSSL, which may open files
 * when first used, out of the window.
 *
 * Both limits are put back on every path - the fillers closed by the probe's RAII, the soft limit by the
 * case's - and this header is the module's last, so its cases run after every other.
 *
 *   - TcpConnectOpenFailure_TheNextEndpointIsTriedTests - two listeners: the first attempt cannot open
 *     its socket, and the task connects to the second. With asio's loop the task ended with
 *     operation_aborted, cancelled by nobody, and the second listener was never tried.
 *   - TcpConnectOpenFailure_TheOpensOwnErrorIsReportedTests - one listener: the task fails with the
 *     descriptor exhaustion itself, EMFILE, not with operation_aborted, as it did with asio's loop.
 *
 * EMFILE is a resource error the process raises against itself, not a code a peer can produce, so it is
 * compared as itself: AGENTS.md's networking rule is about the transport codes of a peer going away.
 *
 * POSIX only. Windows has no small per-process descriptor limit to exhaust, and the cases say so and
 * skip there; the equivalent on IOCP is untested.
 */

#if ! defined( _WIN32 )

namespace utest
{
    namespace connectopenfailure
    {
        using connectloop::WAIT_IN_MILLISECONDS;
        using connectloop::Latch;
        using connectloop::Listener;
        using connectloop::chkOrFail;
        using connectloop::describeCode;
        using heldiothreads::HeldIoThreads;

        enum : std::size_t
        {
            /**
             * @brief How many descriptors above the lowest free one the lowered limit leaves - room for
             * what the process opens between the lowering and the fill, and the size of the fill
             */

            DESCRIPTOR_HEADROOM                 = 64U,

            /**
             * @brief A bound on the fill, which fails the case instead of hanging it if the limit could
             * not be lowered
             */

            MAX_FILLERS                         = 4096U,
        };

        /**
         * @brief Lowers the soft RLIMIT_NOFILE to DESCRIPTOR_HEADROOM above the lowest free descriptor,
         * and puts back the limit it found when it goes
         */

        class LoweredDescriptorLimit
        {
            BL_NO_COPY_OR_MOVE( LoweredDescriptorLimit )

        public:

            LoweredDescriptorLimit()
                :
                m_isSaved( false ),
                m_isLowered( false ),
                m_lowered( 0 )
            {
                if( 0 != ::getrlimit( RLIMIT_NOFILE, &m_saved ) )
                {
                    return;
                }

                m_isSaved = true;

                /*
                 * A descriptor is always the lowest free number, so this one says where the fill
                 * will start
                 */

                const int lowest = ::open( "/dev/null", O_RDONLY | O_CLOEXEC );

                if( lowest < 0 )
                {
                    return;
                }

                ( void ) ::close( lowest );

                struct ::rlimit lowered = m_saved;

                lowered.rlim_cur = static_cast< ::rlim_t >( lowest ) + static_cast< ::rlim_t >( DESCRIPTOR_HEADROOM );

                if( RLIM_INFINITY != m_saved.rlim_cur && lowered.rlim_cur > m_saved.rlim_cur )
                {
                    lowered.rlim_cur = m_saved.rlim_cur;
                }

                m_isLowered = ( 0 == ::setrlimit( RLIMIT_NOFILE, &lowered ) );
                m_lowered = lowered.rlim_cur;
            }

            ~LoweredDescriptorLimit() NOEXCEPT
            {
                if( m_isSaved )
                {
                    ( void ) ::setrlimit( RLIMIT_NOFILE, &m_saved );
                }
            }

            bool isLowered() const NOEXCEPT
            {
                return m_isLowered;
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "soft RLIMIT_NOFILE " ) +
                    ( m_isLowered ? "lowered to " + bl::utils::lexical_cast< std::string >( m_lowered ) : std::string( "not lowered" ) );
            }

        private:

            struct ::rlimit                                                     m_saved;
            bool                                                                m_isSaved;
            bool                                                                m_isLowered;
            ::rlim_t                                                            m_lowered;
        };

        /**
         * @brief Takes every free descriptor below the soft limit - dups of /dev/null, close-on-exec -
         * until the next one fails, and closes them all when it goes
         */

        class FilledDescriptorTable
        {
            BL_NO_COPY_OR_MOVE( FilledDescriptorTable )

        public:

            FilledDescriptorTable()
                :
                m_lastErrno( 0 ),
                m_isFull( false )
            {
                m_fds.reserve( static_cast< std::size_t >( MAX_FILLERS ) + 1U );

                const int base = ::open( "/dev/null", O_RDONLY | O_CLOEXEC );

                if( base < 0 )
                {
                    m_lastErrno = errno;

                    return;
                }

                m_fds.push_back( base );

                for( std::size_t i = 0U; i < static_cast< std::size_t >( MAX_FILLERS ); ++i )
                {
                    const int fd = ::fcntl( base, F_DUPFD_CLOEXEC, 0 );

                    if( fd < 0 )
                    {
                        m_lastErrno = errno;
                        m_isFull = ( EMFILE == m_lastErrno );

                        break;
                    }

                    m_fds.push_back( fd );
                }
            }

            ~FilledDescriptorTable() NOEXCEPT
            {
                for( const auto fd : m_fds )
                {
                    ( void ) ::close( fd );
                }
            }

            bool isFull() const NOEXCEPT
            {
                return m_isFull;
            }

            std::size_t count() const NOEXCEPT
            {
                return m_fds.size();
            }

            int lastErrno() const NOEXCEPT
            {
                return m_lastErrno;
            }

        private:

            std::vector< int >                                                  m_fds;
            int                                                                 m_lastErrno;
            bool                                                                m_isFull;
        };

        /**
         * @brief A connection establisher over the cleartext policy which connects to the case's
         * endpoints in place of the resolver's, with the descriptor table full while it starts the first
         * attempt, and records the endpoint it connected to
         */

        class OpenFailureProbe :
            public bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( OpenFailureProbe )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSocketAsyncBase >
                base_type;

        protected:

            Latch                                                               m_stopped;

            const std::vector< bl::asio::ip::tcp::endpoint >                   m_endpoints;

            mutable bl::os::mutex                                               m_recordLock;
            bool                                                                m_wasTableFull;
            int                                                                 m_fillErrno;
            std::size_t                                                         m_fillers;
            std::string                                                         m_connectedTo;

            OpenFailureProbe( SAA_in std::vector< bl::asio::ip::tcp::endpoint >&& endpoints )
                :
                base_type( std::string( "127.0.0.1" ), 1U /* port, never used */, false /* logExceptions */ ),
                m_endpoints( BL_PARAM_FWD( endpoints ) ),
                m_wasTableFull( false ),
                m_fillErrno( 0 ),
                m_fillers( 0U )
            {
            }

            virtual bool continueAfterResolved( SAA_in base_type::tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                BL_UNUSED( endpoints );

                typedef base_type::tcp_resolver_type::results_type                results_t;

                const auto results = results_t::create(
                    m_endpoints.begin(),
                    m_endpoints.end(),
                    std::string( "localhost" ),
                    std::string( "0" )
                    );

                bool result = false;

                {
                    /*
                     * THE WINDOW: from here to the end of this block the process has no free
                     * descriptor, and the one thing which asks for one is the first attempt's open
                     */

                    FilledDescriptorTable filled;

                    {
                        BL_MUTEX_GUARD( m_recordLock );

                        m_wasTableFull = filled.isFull();
                        m_fillErrno = filled.lastErrno();
                        m_fillers = filled.count();
                    }

                    result = base_type::continueAfterResolved( results.begin() );
                }

                return result;
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                bl::eh::error_code ec;

                const auto remote = base_type::getSocket().remote_endpoint( ec );

                BL_MUTEX_GUARD( m_recordLock );

                m_connectedTo = ec ? "unknown: " + describeCode( ec ) : bl::net::formatEndpointId( remote );

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

            bool waitForStop() const
            {
                return m_stopped.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            bool wasTableFull() const
            {
                BL_MUTEX_GUARD( m_recordLock );

                return m_wasTableFull;
            }

            auto describeFill() const -> std::string
            {
                BL_MUTEX_GUARD( m_recordLock );

                return
                    std::string( "table full " ) +
                    ( m_wasTableFull ? "yes" : "no" ) +
                    " after " +
                    bl::utils::lexical_cast< std::string >( m_fillers ) +
                    " descriptors, the last errno " +
                    bl::utils::lexical_cast< std::string >( m_fillErrno );
            }

            auto connectedTo() const -> std::string
            {
                BL_MUTEX_GUARD( m_recordLock );

                return m_connectedTo;
            }
        };

        typedef bl::om::ObjectImpl< OpenFailureProbe >                          OpenFailureProbeImpl;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct OpenFailureResult
        {
            bool                                                                isHeld;
            bool                                                                wasTableFull;
            bool                                                                hasStopped;
            bool                                                                isCanceled;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            std::string                                                         connectedTo;
            std::string                                                         fill;
            std::string                                                         limit;

            OpenFailureResult()
                :
                isHeld( false ),
                wasTableFull( false ),
                hasStopped( false ),
                isCanceled( false ),
                isFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    limit +
                    ", other I/O threads held " +
                    ( isHeld ? "yes" : "no" ) +
                    ", " +
                    fill +
                    ", stopped " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", cancelled " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
                    ", connected to " +
                    ( connectedTo.empty() ? std::string( "nothing" ) : connectedTo );
            }
        };

        /**
         * @brief Runs a probe over the endpoints given, with the soft limit lowered and the I/O pool's
         * other threads held for the whole run
         */

        inline auto runOpenFailure( SAA_in std::vector< bl::asio::ip::tcp::endpoint >&& endpoints ) -> OpenFailureResult
        {
            using namespace bl;
            using namespace bl::tasks;

            OpenFailureResult result;

            {
                /*
                 * The I/O pool's socket service and its reactor are created with the first socket built
                 * on the pool's io_service, and they open descriptors of their own - built here, before
                 * the limit is lowered, so that none of that can happen inside the window, whichever
                 * case of the module ran first
                 */

                const bl::asio::ip::tcp::socket warm(
                    bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::NonBlocking ) -> aioService()
                    );
            }

            LoweredDescriptorLimit limit;

            result.limit = limit.describe();

            if( ! limit.isLowered() )
            {
                return result;
            }

            const auto probe = OpenFailureProbeImpl::createInstance( BL_PARAM_FWD( endpoints ) );

            const auto task = om::qi< Task >( probe );

            {
                HeldIoThreads held;

                result.isHeld = held.isHeld();

                if( ! result.isHeld )
                {
                    return result;
                }

                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        eq -> setOptions( ExecutionQueue::OptionKeepAll );

                        eq -> push_back( task );

                        result.hasStopped = probe -> waitForStop();

                        result.isCanceled = probe -> isCanceled();

                        if( ! result.hasStopped )
                        {
                            /*
                             * A regression fails the case instead of hanging it
                             */

                            task -> requestCancel();

                            ( void ) probe -> waitForStop();
                        }

                        eq -> wait( task );
                    }
                    );
            }

            result.wasTableFull = probe -> wasTableFull();
            result.fill = probe -> describeFill();
            result.isFailed = task -> isFailed();
            result.connectedTo = probe -> connectedTo();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            return result;
        }

        /**
         * @brief What both cases check first: the run had the conditions it was built for
         */

        inline void chkTheOpenFailed(
            SAA_in          const OpenFailureResult&                            result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.isHeld && result.wasTableFull,
                which + ": the descriptor table could not be filled for the first attempt; " + result.describe()
                );

            chkOrFail(
                result.hasStopped && ! result.isCanceled,
                which + ": the task did not end by itself; " + result.describe()
                );
        }

    } // connectopenfailure

} // utest

#endif /* ! defined( _WIN32 ) */

/**
 * @brief D-A - an endpoint whose socket cannot be opened fails like any attempt, and the next endpoint
 * is tried
 */

UTF_AUTO_TEST_CASE( TcpConnectOpenFailure_TheNextEndpointIsTriedTests )
{
#if defined( _WIN32 )

    UTF_MESSAGE( "skipped: POSIX only - Windows has no small per-process descriptor limit to exhaust" );

#else

    using namespace utest::connectopenfailure;

    Listener first( false /* isQueueFull */ );
    Listener second( false /* isQueueFull */ );

    std::vector< bl::asio::ip::tcp::endpoint > endpoints;

    endpoints.push_back( first.endpoint() );
    endpoints.push_back( second.endpoint() );

    const auto result = runOpenFailure( std::move( endpoints ) );

    chkTheOpenFailed( result, "two endpoints" );

    chkOrFail(
        ! result.isFailed && bl::net::formatEndpointId( second.endpoint() ) == result.connectedTo,
        "two endpoints: the task did not go on to the second after the first could not open its socket; " +
            result.describe()
        );

#endif
}

/**
 * @brief D-A - after the last endpoint, the open's own error is reported, not operation_aborted
 */

UTF_AUTO_TEST_CASE( TcpConnectOpenFailure_TheOpensOwnErrorIsReportedTests )
{
#if defined( _WIN32 )

    UTF_MESSAGE( "skipped: POSIX only - Windows has no small per-process descriptor limit to exhaust" );

#else

    using namespace utest::connectopenfailure;

    Listener only( false /* isQueueFull */ );

    std::vector< bl::asio::ip::tcp::endpoint > endpoints;

    endpoints.push_back( only.endpoint() );

    const auto result = runOpenFailure( std::move( endpoints ) );

    chkTheOpenFailed( result, "one endpoint" );

    chkOrFail(
        result.isFailed &&
            bl::asio::error::operation_aborted != result.taskCode &&
            bl::asio::error::no_descriptors == result.taskCode,
        "one endpoint: the task did not fail with the descriptor exhaustion itself; " + result.describe()
        );

#endif
}

#endif /* __UTEST_TESTTCPCONNECTOPENFAILURE_H_ */
