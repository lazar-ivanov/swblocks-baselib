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

#ifndef __UTEST_TESTHTTPCLIENTLOSTCANCEL_H_
#define __UTEST_TESTHTTPCLIENTLOSTCANCEL_H_

#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>

#include <utests/baselib/Http2DriverTlsProbe.h>
#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * A FORCED CANCEL LOST ON THE HTTP CLIENT (D-L3-1, change-set CS-6)
 *
 * WHAT IS UNDER TEST. A forced cancel which lands between two steps of a TLS operation reaps nothing,
 * and requestCancelInternal( ) is idempotent, so a deadline which fires afterwards cannot issue it
 * again (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md). Its fix adds a receive shutdown
 * to the forced cancel while the task's own handshake is incomplete, (c), and makes the deadlines
 * which survive a cancel issue it again, (a2) - the HTTP client's connect deadline among them.
 *
 * THE CHARACTERIZATION, committed before the fixes and green on both sides of them:
 *
 *   - Http2DriverTls_AnExternalCancelInTheApplicationPhaseEndsAsACancelTests - the HTTP/2 driver
 *     past its handshake and its opening write, with its read registered, cancelled externally: it
 *     ends at once, as a cancel, and its peer sees our FIN - a stream ended with no close_notify.
 *     The driver runs its own handshake, so it is the clear of (c)'s flag once that handshake
 *     completes which keeps the receive shutdown off it. This case pins the ending; it cannot see
 *     the flag, because the cancel reaps the registered read before any receive shutdown would -
 *     the flag's own life is pinned in utf_baselib_tasks3.
 *
 * AND THE REDS, committed before the fixes. The probe swallows its first cancelTask( ) - a lost
 * cancel by construction, without the real gap - with its connect deadline at 1 s and its handshake
 * waiting on a listener which never accepts:
 *
 *   - ConnectDeadline_ALostCancelIsIssuedAgainWithoutAReasonTests - an external cancel with no
 *     reason is lost. Today the deadline fires, finds the task cancelled, and does nothing: the task
 *     ends only when the case closes the listener. After (a2) the deadline issues the cancel again and
 *     the task ends at the deadline, as a cancel - and the deadline, which did not cancel, gives no
 *     reason: no "did not establish within" anywhere in the failure's chain (CS-4's R2-F4).
 *   - ConnectDeadline_ALostCancelKeepsThePoolsReasonTests - the pool's bound gives its reason, and
 *     its cancel is lost. The same red, and the reason the failure carries is the pool's.
 */

namespace utest
{
    namespace httpclientlostcancel
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::TlsEndingPeer;
        using tlsteardown::chkOrFail;
        using tlsteardown::describeCode;
        using tlsteardown::makeTlsKey;

        using h2driverprobe::tls_stream_t;
        using h2driverprobe::Http2DriverProbeImpl;

        enum : std::size_t
        {
            /**
             * @brief How long a case gives its task to end after its cancel, before it makes the
             * peer act - far above a prompt ending, and above the 1 s connect deadline the reds set
             */

            LOST_CANCEL_BOUND_IN_MILLISECONDS   = 5000U,
        };

        /**
         * @brief The HTTP client's connection task over the stranded TLS policy, with its first
         * cancelTask( ) swallowed, and its start - the deadline armed and the handshake begun - and
         * its stop signalled
         */

        class LostCancelConnectProbe :
            public bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( LostCancelConnectProbe )

        public:

            typedef bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >        base_type;

        protected:

            OneShotSignal                                                       m_started;
            OneShotSignal                                                       m_stop;

            /*
             * Under the task lock: every caller of cancelTask( ) holds it
             */

            bool                                                                m_isFirstCancelSwallowed;

            LostCancelConnectProbe(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          base_type::factory_ptr_t                        driverFactory,
                SAA_in          bl::tasks::ClientConnectionConfig               config
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    bl::tasks::ProxyConfig::none(),
                    BL_PARAM_FWD( config ),
                    false /* logExceptions */
                    ),
                m_isFirstCancelSwallowed( false )
            {
            }

            virtual bool beginPreHandshakeStage( SAA_in const bl::cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                /*
                 * The base arms the connect deadline and starts the handshake
                 */

                const bool result = base_type::beginPreHandshakeStage( continueCallback );

                m_started.signal();

                return result;
            }

            virtual void cancelTask() OVERRIDE
            {
                if( ! m_isFirstCancelSwallowed )
                {
                    /*
                     * THE LOST CANCEL: requested, and nothing done - what a forced cancel which
                     * lands between two steps of the handshake amounts to
                     */

                    m_isFirstCancelSwallowed = true;

                    return;
                }

                base_type::cancelTask();
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

            bool waitForStart() const
            {
                return m_started.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            bool wasFirstCancelSwallowed() const
            {
                BL_MUTEX_GUARD( base_type::m_lock );

                return m_isFirstCancelSwallowed;
            }
        };

        typedef bl::om::ObjectImpl< LostCancelConnectProbe >                    LostCancelConnectProbeImpl;

        /**
         * @brief What one run came to, read on the test thread
         */

        struct LostCancelResult
        {
            bool                                                                isStarted;
            bool                                                                wasFirstCancelSwallowed;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
            bool                                                                hasStopped;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            std::exception_ptr                                                  exception;

            LostCancelResult()
                :
                isStarted( false ),
                wasFirstCancelSwallowed( false ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
                hasStopped( false ),
                isFailed( false )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( "started " ) +
                    ( isStarted ? "yes" : "no" ) +
                    ", first cancel swallowed " +
                    ( wasFirstCancelSwallowed ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", stopped once the peer acted " +
                    ( hasStopped ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) );
            }
        };

        /**
         * @brief Whether any exception down a chain of nested causes has a message which contains
         * 'text' - walked link by link, and bounded, so that a cycle cannot hang it
         */

        inline bool chainHasMessage(
            SAA_in          const std::exception_ptr&                           eptr,
            SAA_in          const std::string&                                  text
            )
        {
            auto current = eptr;

            for( std::size_t depth = 0U; current && depth < 16U; ++depth )
            {
                std::exception_ptr nested;

                try
                {
                    std::rethrow_exception( current );
                }
                catch( std::exception& e )
                {
                    if( std::string::npos != std::string( e.what() ).find( text ) )
                    {
                        return true;
                    }

                    const auto* const boostException = dynamic_cast< const bl::eh::exception* >( &e );

                    if( boostException )
                    {
                        const auto* const cause =
                            bl::eh::get_error_info< bl::eh::errinfo_nested_exception_ptr >( *boostException );

                        nested = cause ? *cause : std::exception_ptr();
                    }
                }
                catch( ... )
                {
                }

                current = nested;
            }

            return false;
        }

        /**
         * @brief Runs the probe against a listener which never accepts: the kernel completes the
         * connect from the backlog and nothing ever answers the ClientHello. Once the probe has
         * started, the case gives the pool's reason when there is one and requests the cancel the
         * probe swallows; once the bound is over it closes the listener, which resets the connection
         */

        inline auto runLostConnectCancel( SAA_in_opt const std::exception_ptr& poolReason = nullptr ) -> LostCancelResult
        {
            using namespace bl;
            using namespace bl::tasks;

            LostCancelResult result;

            asio::io_service ioService;

            auto listener = cpp::SafeUniquePtr< asio::ip::tcp::acceptor >::attach(
                new asio::ip::tcp::acceptor(
                    ioService,
                    asio::ip::tcp::endpoint( asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    )
                );

            httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = "127.0.0.1";
            key.port = listener -> local_endpoint().port();

            ClientConnectionConfig config;

            config.connectTimeout = time::seconds( 1 );

            const auto probe = LostCancelConnectProbeImpl::createInstance(
                BL_PARAM_FWD( key ),
                std::make_shared< httpclient::ClientDriverFactoryT< tls_stream_t > >(),
                BL_PARAM_FWD( config )
                );

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    result.isStarted = probe -> waitForStart();

                    if( poolReason )
                    {
                        probe -> cancelReason( poolReason );
                    }

                    task -> requestCancel();

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( LOST_CANCEL_BOUND_IN_MILLISECONDS ) );

                    result.isCanceled = probe -> isCanceled();

                    listener.reset();

                    result.hasStopped = probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( task );
                }
                );

            result.wasFirstCancelSwallowed = probe -> wasFirstCancelSwallowed();
            result.isFailed = task -> isFailed();
            result.exception = task -> exception();

            if( result.exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( result.exception );
            }

            return result;
        }

        /**
         * @brief What both reds assert: the lost cancel was issued again by the deadline, so the
         * task ended inside the bound, as a cancel - with no reason of the deadline's in its chain
         */

        inline void chkIssuedAgainByTheDeadline(
            SAA_in          const LostCancelResult&                             result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.isStarted && result.wasFirstCancelSwallowed,
                which + ": the cancel was never requested and swallowed; " + result.describe()
                );

            chkOrFail(
                result.hasStoppedWithinBound && result.isCanceled,
                which + ": the connect deadline did not issue the lost cancel again; " + result.describe()
                );

            chkOrFail(
                result.isFailed && bl::asio::error::operation_aborted == result.taskCode,
                which + ": the task did not end as a cancel; " + result.describe()
                );

            chkOrFail(
                ! chainHasMessage( result.exception, "did not establish within" ),
                which + ": the deadline gave its reason to a cancel it did not make; " + result.describe()
                );
        }

    } // httpclientlostcancel

} // utest

/**
 * @brief CHARACTERIZATION - the HTTP/2 driver cancelled externally in its application phase ends at
 * once, as a cancel, and its peer sees our FIN with no close_notify
 *
 * The peer selects h2 and then reads until the client ends the stream; the probe signals once the
 * driver's opening write is over, by when its read is registered - see utf_baselib_h2client9's
 * TestHttp2TlsTruncationTeardown.h - and the factory is empty, because a peer which selected h2 never
 * reaches it
 */

UTF_AUTO_TEST_CASE( Http2DriverTls_AnExternalCancelInTheApplicationPhaseEndsAsACancelTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::httpclientlostcancel;

    TlsEndingPeer::Script script( TlsEndingPeer::Ending::AwaitTheClient );

    script.alpnPreference.push_back( "h2" );

    TlsEndingPeer peer( script );

    const auto driver = Http2DriverProbeImpl::createInstance(
        makeTlsKey( peer.port() ),
        std::make_shared< httpclient::ClientDriverFactoryT< tls_stream_t > >()
        );

    const auto task = om::qi< Task >( driver );

    bool isQuiet = false;
    bool hasStoppedWithinBound = false;
    bool isCanceled = false;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> setOptions( ExecutionQueue::OptionKeepAll );

            eq -> push_back( task );

            isQuiet = driver -> waitForQuiet( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

            task -> requestCancel();

            hasStoppedWithinBound =
                driver -> waitForStop( static_cast< std::size_t >( LOST_CANCEL_BOUND_IN_MILLISECONDS ) );

            isCanceled = driver -> isCanceled();

            peer.release();

            ( void ) driver -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

            eq -> wait( task );
        }
        );

    const auto exception = task -> exception();

    const auto taskCode = exception ? eh::errorCodeFromExceptionPtr( exception ) : eh::error_code();

    ( void ) peer.waitForRecord( "script-ended" );

    std::string records;

    for( const auto& record : peer.records() )
    {
        records += ( records.empty() ? "" : " | " ) + record;
    }

    const std::string readings =
        std::string( "quiet " ) +
        ( isQuiet ? "yes" : "no" ) +
        ", stopped within the bound " +
        ( hasStoppedWithinBound ? "yes" : "no" ) +
        ", cancelled " +
        ( isCanceled ? "yes" : "no" ) +
        ", task " +
        ( task -> isFailed() ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
        ", peer " +
        records;

    chkOrFail( isQuiet, "the driver's opening write never ended; " + readings );

    chkOrFail(
        hasStoppedWithinBound && isCanceled,
        "the driver did not end within the bound after its cancel; " + readings
        );

    chkOrFail(
        task -> isFailed() && asio::error::operation_aborted == taskCode,
        "the driver did not end as a cancel; " + readings
        );

    chkOrFail(
        std::string::npos != records.find( "client-ended:asio.ssl.stream:1" ),
        "the peer did not see the stream end with no close_notify; " + readings
        );
}

/**
 * @brief D-L3-1's RED, THE CONNECT DEADLINE - an external cancel with no reason is lost, and the
 * deadline issues it again without giving a reason of its own
 */

UTF_AUTO_TEST_CASE( ConnectDeadline_ALostCancelIsIssuedAgainWithoutAReasonTests )
{
    using namespace utest::httpclientlostcancel;

    chkIssuedAgainByTheDeadline( runLostConnectCancel(), "the lost external cancel" );
}

/**
 * @brief D-L3-1's RED, THE CONNECT DEADLINE - the pool's bound gives its reason and its cancel is
 * lost; the deadline issues it again, and the pool's reason is the one the failure carries
 */

UTF_AUTO_TEST_CASE( ConnectDeadline_ALostCancelKeepsThePoolsReasonTests )
{
    using namespace bl;
    using namespace utest::httpclientlostcancel;

    const std::string reason( "the test pool's establishment bound expired" );

    const auto result = runLostConnectCancel(
        std::make_exception_ptr( BL_EXCEPTION( TimeoutException(), reason ) )
        );

    chkIssuedAgainByTheDeadline( result, "the pool's lost cancel" );

    chkOrFail(
        chainHasMessage( result.exception, reason ),
        "the failure does not carry the pool's reason; " + result.describe()
        );
}

#endif /* __UTEST_TESTHTTPCLIENTLOSTCANCEL_H_ */
