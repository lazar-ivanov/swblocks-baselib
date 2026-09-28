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

#ifndef __UTEST_TESTSIMPLEHTTPHANDSHAKEDEADLINE_H_
#define __UTEST_TESTSIMPLEHTTPHANDSHAKEDEADLINE_H_

#include <baselib/http/SimpleHttpSslTask.h>
#include <baselib/http/SimpleHttpTask.h>

#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * SIMPLEHTTPTASK'S DEADLINE OVER ITS OWN TLS HANDSHAKE (D2, change-set CS-6)
 *
 * WHAT IS UNDER TEST. SimpleHttpTask arms its request timer in continueAfterConnected( ), which a TLS
 * connection reaches only once its handshake has completed, and the protocol timer is not armed for a
 * connector's handshake - so a TLS server which accepts TCP and never answers the ClientHello holds a
 * SimpleHttpSslTask until the server acts. The maintainer's decision
 * (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md, section 8): one deadline, armed when the
 * first attempt's TCP connection is established, before the handshake, and kept until the task stops -
 * through a handshake retry too; and onTimer( ) no longer ignores an expiry while there is no channel,
 * which it meets only during a retry's resolve.
 *
 * THE CONTROLS, green on both sides:
 *
 *   - SimpleHttp_ACleartextRequestStillTimesOutTests - over cleartext the timer was armed right after
 *     the connect and still is: a server which accepts and never answers ends the task at its timeout.
 *   - SimpleHttpTls_ARequestWithinItsTimeoutSucceedsTests - an HTTPS exchange under a generous timeout.
 *
 * AND THE REDS, committed before the change:
 *
 *   - SimpleHttpTls_AHandshakeWhichNeverCompletesTimesOutTests - a listener which never accepts, so
 *     nothing answers the ClientHello. Today the task outlives its 1 s timeout and ends only when the
 *     case closes the listener; after the timer move it ends at the timeout, timed out. Certain: when
 *     the timer fires the ClientHello has long been written and the read for the ServerHello waits.
 *   - SimpleHttpTls_OneDeadlineHoldsAcrossAHandshakeRetryTests - the peer reads the first attempt's
 *     ClientHello and closes, a truncation which the connector retries, and never answers the second.
 *     The probe reads the timer's expiry after each entry of the pre-handshake stage. Today there is no
 *     timer there; after the timer move there is one after the first entry, the second finds the same
 *     expiry, and the task ends at it, timed out.
 *   - SimpleHttpTls_TheDeadlineHoldsDuringARetrysResolveTests - the same retry, whose resolve the probe
 *     holds - it returns without connecting, so there is no channel - until a cancel releases it. Red
 *     until onTimer( ) stops ignoring an expiry with no channel: before, the deadline expires inside the
 *     hold and nothing happens, and the task ends only as a plain cancel, the case's; after, the expiry
 *     cancels it and it ends at its deadline, timed out.
 *
 * Each task's timeout is set on the task (setTimeout( )), so no case touches the global parameter.
 * Nothing is timed but the deadlines themselves; the bound is several timeouts.
 */

namespace utest
{
    namespace simplehttpdeadline
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::TlsEndingPeer;
        using tlsteardown::chkOrFail;
        using tlsteardown::describeCode;

        enum : std::size_t
        {
            /**
             * @brief The request timeout the deadline cases set, and the bound they give the task -
             * several timeouts
             */

            TIMEOUT_IN_MILLISECONDS             = 1000U,

            BOUND_IN_MILLISECONDS               = 5000U,
        };

        /**
         * @brief A loopback listener which never accepts: the kernel completes the connect out of the
         * backlog, and whatever the client writes is never read
         */

        class SilentListener
        {
            BL_NO_COPY_OR_MOVE( SilentListener )

        public:

            SilentListener()
                :
                m_acceptor( m_ioService )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();
            }

            auto port() const -> unsigned short
            {
                return m_acceptor.local_endpoint().port();
            }

            /**
             * @brief Closes the listener, which resets the connections it never accepted
             */

            void close()
            {
                bl::eh::error_code ec;

                m_acceptor.close( ec );
            }

        private:

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
        };

        /**
         * @brief A loopback peer for a handshake retry: it accepts the first connection, reads its
         * ClientHello whole and closes it - an orderly end, which the client's handshake reads as a
         * truncation and the connector retries - then accepts the second and never answers it, until
         * the case releases it
         */

        class RetryPeer
        {
            BL_NO_COPY_OR_MOVE( RetryPeer )

        public:

            RetryPeer()
                :
                m_acceptor( m_ioService ),
                m_port( 0U ),
                m_isReleased( false )
            {
                const bl::asio::ip::tcp::endpoint endpoint( bl::asio::ip::address_v4::loopback(), 0U );

                m_acceptor.open( endpoint.protocol() );
                m_acceptor.bind( endpoint );
                m_acceptor.listen();

                m_port = m_acceptor.local_endpoint().port();

                m_thread.reset( new bl::os::thread( bl::cpp::bind( &RetryPeer::run, this ) ) );
            }

            ~RetryPeer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                release();

                {
                    /*
                     * Throwaway connections wake a worker still blocked in accept( ) - once for each
                     * connection it may still be waiting for
                     */

                    for( std::size_t i = 0U; i < 2U; ++i )
                    {
                        bl::eh::error_code ec;

                        bl::asio::io_service ioService;
                        bl::asio::ip::tcp::socket socket( ioService );

                        socket.connect(
                            bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                            ec
                            );

                        socket.close( ec );
                    }
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            auto port() const -> unsigned short
            {
                return m_port;
            }

            auto records() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_records;
            }

            void release()
            {
                BL_MUTEX_GUARD( m_lock );

                m_isReleased = true;

                m_cv.notify_all();
            }

        private:

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_records.push_back( BL_PARAM_FWD( what ) );
            }

            bool isReleased() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_isReleased;
            }

            void waitForRelease()
            {
                bl::os::mutex_unique_lock guard( m_lock );

                ( void ) m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_isReleased;
                    }
                    );
            }

            static auto readRecord( SAA_inout bl::asio::ip::tcp::socket& socket ) -> bl::eh::error_code
            {
                unsigned char header[ 5 ];

                bl::eh::error_code ec;

                ( void ) bl::asio::read( socket, bl::asio::buffer( header, sizeof( header ) ), ec );

                if( ec )
                {
                    return ec;
                }

                const std::size_t length =
                    ( static_cast< std::size_t >( header[ 3 ] ) << 8 ) | static_cast< std::size_t >( header[ 4 ] );

                std::vector< unsigned char > body( length );

                ( void ) bl::asio::read( socket, bl::asio::buffer( body ), ec );

                return ec;
            }

            void run()
            {
                bl::eh::error_code ec;

                {
                    bl::asio::ip::tcp::socket first( m_ioService );

                    m_acceptor.accept( first, ec );

                    if( ec || isReleased() )
                    {
                        record( "first-accept:" + describeCode( ec ) );

                        return;
                    }

                    record( "first-client-hello:" + describeCode( readRecord( first ) ) );

                    first.close( ec );
                }

                bl::asio::ip::tcp::socket second( m_ioService );

                m_acceptor.accept( second, ec );

                if( ec || isReleased() )
                {
                    record( "second-accept:" + describeCode( ec ) + ( isReleased() ? " after the release" : "" ) );

                    return;
                }

                record( "second-connected" );

                /*
                 * SILENT UNTIL RELEASED, and the acceptor closed with the socket, so that nothing the
                 * client tries after the release can connect
                 */

                waitForRelease();

                second.close( ec );
                m_acceptor.close( ec );
            }

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            unsigned short                                                      m_port;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::vector< std::string >                                          m_records;
            bool                                                                m_isReleased;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /**
         * @brief What the probe does beyond the task it derives from
         */

        enum class ProbeMode
        {
            /**
             * @brief Nothing - the stop is signalled
             */

            Plain,

            /**
             * @brief The timer's expiry is read after each entry of the pre-handshake stage
             */

            RecordTheDeadline,

            /**
             * @brief The same, and the retry's resolve is held until a cancel releases it
             */

            HoldTheRetrysResolve,
        };

        /**
         * @brief A SimpleHttpTask - BASE is SimpleHttpTaskT<> or SimpleHttpSslTaskT<> - which GETs
         * '/', with its stop signalled and, per ProbeMode, its deadline read or its retry's resolve
         * held
         */

        template
        <
            typename BASE
        >
        class SimpleHttpProbeT : public BASE
        {
            BL_DECLARE_OBJECT_IMPL( SimpleHttpProbeT )

        public:

            typedef BASE                                                        base_type;
            typedef SimpleHttpProbeT< BASE >                                    this_type;
            typedef typename base_type::tcp_resolver_type                       tcp_resolver_type;

        protected:

            const ProbeMode                                                     m_mode;

            OneShotSignal                                                       m_stop;

            /*
             * Under the task lock: every entry of the pre-handshake stage, of the resolve's
             * continuation and of cancelTask( ) holds it
             */

            std::vector< std::string >                                          m_expiries;
            std::size_t                                                         m_resolves;
            bool                                                                m_isResolveHeld;
            typename tcp_resolver_type::iterator                                m_heldEndpoints;

            SimpleHttpProbeT(
                SAA_in          std::string&&                                   host,
                SAA_in          const unsigned short                            port,
                SAA_in          const ProbeMode                                 mode
                )
                :
                base_type(
                    BL_PARAM_FWD( host ),
                    port,
                    std::string( "/" ),
                    std::string( "GET" ),
                    std::string() /* content */,
                    bl::http::HeadersMap()
                    ),
                m_mode( mode ),
                m_resolves( 0U ),
                m_isResolveHeld( false )
            {
            }

            virtual bool beginPreHandshakeStage( SAA_in const bl::cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                const bool result = base_type::beginPreHandshakeStage( continueCallback );

                if( ProbeMode::Plain != m_mode )
                {
                    m_expiries.push_back(
                        base_type::m_timer ?
                            bl::time::to_simple_string( base_type::m_timer -> expires_at() ) :
                            std::string( "no timer" )
                        );
                }

                return result;
            }

            virtual bool continueAfterResolved( SAA_in typename tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                ++m_resolves;

                if( ProbeMode::HoldTheRetrysResolve == m_mode && m_resolves > 1U )
                {
                    /*
                     * THE HOLD: the retry reset the stream before its resolve, and nothing creates a
                     * new one, so there is no channel until a cancel releases the hold
                     */

                    m_isResolveHeld = true;
                    m_heldEndpoints = endpoints;

                    return true;
                }

                return base_type::continueAfterResolved( endpoints );
            }

            virtual void cancelTask() OVERRIDE
            {
                base_type::cancelTask();

                if( m_isResolveHeld )
                {
                    m_isResolveHeld = false;

                    bl::ThreadPoolDefault::getDefault( base_type::getThreadPoolId() ) -> aioService().post(
                        bl::cpp::bind(
                            &this_type::onHoldReleased,
                            bl::om::ObjPtrCopyable< this_type >::acquireRef( this )
                            )
                        );
                }
            }

            /**
             * @brief The held resolve's continuation, a task handler of the probe's own: the cancel
             * which released it ends the task here
             */

            void onHoldReleased() NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                if( base_type::continueAfterResolved( m_heldEndpoints ) )
                {
                    return;
                }

                BL_TASKS_HANDLER_END()
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stop.signal();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            auto expiries() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( bl::tasks::TaskBase::m_lock );

                return m_expiries;
            }

            std::size_t resolves() const
            {
                BL_MUTEX_GUARD( bl::tasks::TaskBase::m_lock );

                return m_resolves;
            }
        };

        typedef bl::om::ObjectImpl< SimpleHttpProbeT< bl::tasks::SimpleHttpTaskT<> > >     CleartextProbeImpl;
        typedef bl::om::ObjectImpl< SimpleHttpProbeT< bl::tasks::SimpleHttpSslTaskT<> > >  TlsProbeImpl;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct DeadlineResult
        {
            bool                                                                hasStoppedWithinBound;
            bool                                                                isTimedOut;
            bool                                                                isFailed;
            bool                                                                isTimeoutException;
            bl::eh::error_code                                                  taskCode;
            std::string                                                         failure;
            unsigned int                                                        httpStatus;
            std::string                                                         response;
            std::vector< std::string >                                          expiries;
            std::size_t                                                         resolves;

            DeadlineResult()
                :
                hasStoppedWithinBound( false ),
                isTimedOut( false ),
                isFailed( false ),
                isTimeoutException( false ),
                httpStatus( 0U ),
                resolves( 0U )
            {
            }

            auto describe() const -> std::string
            {
                std::string expiriesText;

                for( const auto& expiry : expiries )
                {
                    expiriesText += ( expiriesText.empty() ? "" : " | " ) + expiry;
                }

                return
                    std::string( "stopped within the bound " ) +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", timed out " +
                    ( isTimedOut ? "yes" : "no" ) +
                    ", task " +
                    (
                        isFailed ?
                            "failed " + describeCode( taskCode ) + ( isTimeoutException ? " (a TimeoutException)" : "" ) +
                                ": " + failure
                            :
                            std::string( "succeeded" )
                    ) +
                    ", HTTP status " +
                    bl::utils::lexical_cast< std::string >( httpStatus ) +
                    ", resolves " +
                    bl::utils::lexical_cast< std::string >( resolves ) +
                    ", the deadline at each pre-handshake stage [" +
                    expiriesText +
                    "]";
            }
        };

        /**
         * @brief Runs a probe to its end: gives it the bound to stop, then makes the case's peer act
         * - and, if it has still not stopped, cancels it - and waits for it
         */

        template
        <
            typename PROBE
        >
        inline auto runToTheEnd(
            SAA_in          const bl::om::ObjPtr< PROBE >&                      probe,
            SAA_in          const std::size_t                                   boundInMilliseconds,
            SAA_in          const bl::cpp::void_callback_t&                     makeThePeerAct
            )
            -> DeadlineResult
        {
            using namespace bl;
            using namespace bl::tasks;

            DeadlineResult result;

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.hasStoppedWithinBound = probe -> waitForStop( boundInMilliseconds );

                    if( ! result.hasStoppedWithinBound )
                    {
                        makeThePeerAct();

                        task -> requestCancel();
                    }

                    ( void ) probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( task );
                }
                );

            result.isTimedOut = probe -> isTimedOut();
            result.isFailed = task -> isFailed();
            result.httpStatus = probe -> getHttpStatus();
            result.response = probe -> getResponse();
            result.expiries = probe -> expiries();
            result.resolves = probe -> resolves();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );

                try
                {
                    cpp::safeRethrowException( exception );
                }
                catch( TimeoutException& e )
                {
                    result.isTimeoutException = true;
                    result.failure = e.what();
                }
                catch( std::exception& e )
                {
                    result.failure = e.what();
                }
            }

            return result;
        }

        /**
         * @brief A task which timed out: it stopped within the bound, and failed with the
         * TimeoutException its timer gives
         */

        inline void chkTimedOut(
            SAA_in          const DeadlineResult&                               result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.hasStoppedWithinBound,
                which + ": the task outlived its timeout; " + result.describe()
                );

            chkOrFail(
                result.isTimedOut && result.isFailed && result.isTimeoutException,
                which + ": the task did not end with the TimeoutException of its timer; " + result.describe()
                );
        }

    } // simplehttpdeadline

} // utest

/**
 * @brief CONTROL - over cleartext a request to a server which accepts and never answers still ends at
 * its timeout
 */

UTF_AUTO_TEST_CASE( SimpleHttp_ACleartextRequestStillTimesOutTests )
{
    using namespace utest::simplehttpdeadline;

    SilentListener listener;

    const auto probe = CleartextProbeImpl::createInstance( std::string( "127.0.0.1" ), listener.port(), ProbeMode::Plain );

    probe -> setTimeout( bl::time::milliseconds( static_cast< long >( TIMEOUT_IN_MILLISECONDS ) ) );

    const auto result = runToTheEnd(
        probe,
        static_cast< std::size_t >( BOUND_IN_MILLISECONDS ),
        [ &listener ]() -> void
        {
            listener.close();
        }
        );

    chkTimedOut( result, "cleartext" );
}

/**
 * @brief CONTROL - an HTTPS exchange under a generous timeout succeeds
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_ARequestWithinItsTimeoutSucceedsTests )
{
    using namespace utest::simplehttpdeadline;

    TlsEndingPeer::Script script( TlsEndingPeer::Ending::CloseNotify );

    script.response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";

    TlsEndingPeer peer( script );

    const auto probe = TlsProbeImpl::createInstance( std::string( "localhost" ), peer.port(), ProbeMode::Plain );

    probe -> setTimeout( bl::time::seconds( 30 ) );

    const auto result = runToTheEnd(
        probe,
        static_cast< std::size_t >( WAIT_IN_MILLISECONDS ),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    peer.release();

    chkOrFail(
        result.hasStoppedWithinBound && ! result.isFailed && ! result.isTimedOut &&
            200U == result.httpStatus && std::string( "hello" ) == result.response,
        "the HTTPS exchange did not succeed; " + result.describe()
        );
}

/**
 * @brief D2's RED - a TLS handshake which never completes ends at the request timeout
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_AHandshakeWhichNeverCompletesTimesOutTests )
{
    using namespace utest::simplehttpdeadline;

    SilentListener listener;

    const auto probe = TlsProbeImpl::createInstance( std::string( "127.0.0.1" ), listener.port(), ProbeMode::Plain );

    probe -> setTimeout( bl::time::milliseconds( static_cast< long >( TIMEOUT_IN_MILLISECONDS ) ) );

    const auto result = runToTheEnd(
        probe,
        static_cast< std::size_t >( BOUND_IN_MILLISECONDS ),
        [ &listener ]() -> void
        {
            listener.close();
        }
        );

    chkTimedOut( result, "TLS, the handshake never answered" );
}

/**
 * @brief D2's RED, THE RETRY - one deadline, armed at the first attempt, holds across a handshake
 * retry: the retry does not restart it, and the task ends at it
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_OneDeadlineHoldsAcrossAHandshakeRetryTests )
{
    using namespace utest::simplehttpdeadline;

    RetryPeer peer;

    const auto probe = TlsProbeImpl::createInstance( std::string( "127.0.0.1" ), peer.port(), ProbeMode::RecordTheDeadline );

    probe -> setTimeout( bl::time::milliseconds( static_cast< long >( TIMEOUT_IN_MILLISECONDS ) ) );

    const auto result = runToTheEnd(
        probe,
        static_cast< std::size_t >( BOUND_IN_MILLISECONDS ),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    std::string records;

    for( const auto& record : peer.records() )
    {
        records += ( records.empty() ? "" : " | " ) + record;
    }

    const std::string readings = result.describe() + "; peer " + records;

    chkOrFail(
        2U == result.expiries.size(),
        "the pre-handshake stage was not entered twice, so no retry happened; " + readings
        );

    chkOrFail(
        std::string( "no timer" ) != result.expiries[ 0 ] && result.expiries[ 0 ] == result.expiries[ 1 ],
        "the deadline was not armed at the first attempt, or the retry restarted it; " + readings
        );

    chkTimedOut( result, "TLS, across a retry" );
}

/**
 * @brief D2's RED, THE CONDITION - the deadline holds while a retry's resolve leaves no channel
 *
 * The bound is the same several timeouts: with the expiry ignored the task is still running at it,
 * and the case's own cancel - the only other thing which releases the hold - ends it as a plain
 * cancel
 */

UTF_AUTO_TEST_CASE( SimpleHttpTls_TheDeadlineHoldsDuringARetrysResolveTests )
{
    using namespace utest::simplehttpdeadline;

    RetryPeer peer;

    const auto probe = TlsProbeImpl::createInstance( std::string( "127.0.0.1" ), peer.port(), ProbeMode::HoldTheRetrysResolve );

    probe -> setTimeout( bl::time::milliseconds( static_cast< long >( TIMEOUT_IN_MILLISECONDS ) ) );

    const auto result = runToTheEnd(
        probe,
        static_cast< std::size_t >( BOUND_IN_MILLISECONDS ),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    chkOrFail(
        2U == result.resolves,
        "the retry's resolve was never held; " + result.describe()
        );

    chkTimedOut( result, "TLS, the deadline during a retry's resolve" );
}

#endif /* __UTEST_TESTSIMPLEHTTPHANDSHAKEDEADLINE_H_ */
