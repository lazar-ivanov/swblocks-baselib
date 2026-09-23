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

#ifndef __UTEST_TESTHTTP2DRIVERACCOUNTING_H_
#define __UTEST_TESTHTTP2DRIVERACCOUNTING_H_

#include <baselib/tasks/TcpStrandedStreams.h>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Utf.h>

#include <atomic>

/************************************************************************
 * A3 - an operation of the HTTP/2 driver which was BEGUN and never STARTED
 *
 * WHAT THIS IS ABOUT. MultiOperationTaskT's contract (MultiOperationTask.h) is that a task calls
 * beginOperation( ) immediately before each async_* call and that every begun operation is
 * completed - "a task which lets the count fall to zero while it is not closing has simply stopped
 * doing anything", and, the other way round, a task whose count can never REACH zero can never take
 * its terminal path. The second is the worse symptom, because it is a HANG rather than a failure:
 * nothing fails, nothing is logged, the task simply never ends
 *
 * An initiator which throws is what produces it. The driver has begun the operation and the
 * async_* call which was to complete it never started, so the count carries a phantom for the life
 * of the task. h1's driver guards all three of its own initiators against exactly this and says so
 * above scheduleRead( ); h2's eight sites carried no guard at all
 *
 * WHY THE CASE NEEDS A STREAM POLICY OF ITS OWN. Asio reports I/O failure through the handler and
 * not by throwing, so what can throw out of an initiator is the allocation the initiating call
 * makes - which no test can arrange from the outside. The one initiator with a seam is the
 * command mailbox's postToStrand( ), which the stream policy supplies and which a policy of our
 * own can therefore be asked to fail. Hiding it is the mechanism the policies themselves use and
 * document ("the stream policy is a static interface resolved by template composition ... so the
 * hiding definition is the one they find", TcpStrandedStreams.h)
 *
 * WHAT THE CASE ASSERTS, AND WHY NOT THE COUNT. pendingOperations( ) would read a number which the
 * strand is free to change under the reading thread, so the assertion is made on the CONSEQUENCE
 * the contract names: after the initiator has thrown, the connection is asked to end and has to
 * END. Against the unfixed driver it cannot, and the case fails on its bound rather than hanging
 * the module - the release below is what lets the queue drain afterwards, and it runs BEFORE the
 * assertion for that reason
 */

namespace utest
{
    namespace h2accounting
    {
        using namespace utest::h2driver;

        /**
         * @brief The one-shot seam - the NEXT post to the strand fails, and only that one
         *
         * One shot because the driver posts to the strand from cancelTask( ) as well, and a seam
         * which stayed armed would fail the case's own teardown instead of the initiator it is
         * about
         */

        inline auto postFailureArmed() NOEXCEPT -> std::atomic< bool >&
        {
            static std::atomic< bool > armed( false );

            return armed;
        }

        /**
         * @brief class ThrowingPostPolicyT - the cleartext stranded policy with that one seam
         */

        template
        <
            typename E = void
        >
        class ThrowingPostPolicyT : public bl::tasks::TcpSocketAsyncStrandedBase
        {
            BL_CTR_DEFAULT( ThrowingPostPolicyT, protected )
            BL_DECLARE_OBJECT_IMPL( ThrowingPostPolicyT )

        public:

            typedef ThrowingPostPolicyT< E >                                    this_type;
            typedef bl::tasks::TcpSocketAsyncStrandedBase                       base_type;

        protected:

            template
            <
                typename HANDLER
            >
            void postToStrand( SAA_in HANDLER&& handler )
            {
                if( postFailureArmed().exchange( false ) )
                {
                    BL_THROW(
                        bl::UnexpectedException(),
                        BL_MSG()
                            << "The post to the strand was made to fail"
                        );
                }

                base_type::postToStrand( BL_PARAM_FWD( handler ) );
            }
        };

        typedef ThrowingPostPolicyT<> ThrowingPostPolicy;

        /**
         * @brief class AccountingProbeT - the driver over that policy, with a stop rendezvous
         *
         * onTaskStoppedNothrow( ) is the task's own terminal path and is therefore the rendezvous
         * the assertion wants - there is nothing to poll and nothing to sleep on. It is called
         * under the task lock (TaskBase.h says so where it is declared), so it may notify and must
         * not do anything else
         */

        template
        <
            typename E = void
        >
        class AccountingProbeT : public bl::tasks::Http2ConnectionTaskT< ThrowingPostPolicy >
        {
            BL_DECLARE_OBJECT_IMPL( AccountingProbeT )

        public:

            typedef AccountingProbeT< E >                                       this_type;
            typedef bl::tasks::Http2ConnectionTaskT< ThrowingPostPolicy >       base_type;

        protected:

            mutable bl::os::mutex                                               m_stopLock;
            mutable bl::os::condition_variable                                  m_cvStopped;

            bool                                                                m_isStopped;

            AccountingProbeT(
                SAA_in          ConnectionKey                                   key,
                SAA_in          typename base_type::factory_ptr_t               driverFactory,
                SAA_in          Http2ConnectionConfig                           h2config,
                SAA_in          ClientConnectionConfig                          config
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( h2config ),
                    bl::tasks::ProxyConfig::none(),
                    BL_PARAM_FWD( config ),
                    false /* logExceptions */
                    ),
                m_isStopped( false )
            {
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt      const std::exception_ptr&                       eptrIn = nullptr,
                SAA_inout_opt   bool*                                           isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                const auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                {
                    BL_MUTEX_GUARD( m_stopLock );

                    m_isStopped = true;
                }

                m_cvStopped.notify_all();

                return result;
            }

        public:

            /**
             * @brief Blocks until the task has taken its terminal path, or the bound expires
             */

            bool waitForStopped( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                bl::os::mutex_unique_lock guard( m_stopLock );

                return m_cvStopped.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_isStopped;
                    }
                    );
            }

            /**
             * @brief TEARDOWN ONLY - gives back whatever the accounting is still holding
             *
             * It exists so that a case which has just shown the defect can still drain its
             * execution queue: a task whose count cannot reach zero would otherwise hang the
             * module at the scope exit of scheduleAndExecuteInParallel( ), and a hang is not a
             * test result. Never called on the green path, and bounded either way
             */

            bool chkReleaseAbandonedOperations( SAA_in const std::size_t maxReleases ) NOEXCEPT
            {
                for( std::size_t i = 0U; i < maxReleases; ++i )
                {
                    if( 0U == base_type::pendingOperations() )
                    {
                        break;
                    }

                    base_type::onOperationCompleted( nullptr, false );

                    if( waitForStopped( 1000U ) )
                    {
                        return true;
                    }
                }

                return waitForStopped( 1000U );
            }
        };

        typedef bl::om::ObjectImpl< AccountingProbeT<> > AccountingProbe;

        inline auto makeAccountingPeer() -> bl::om::ObjPtr< h2peer::Http2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return h2peer::Http2TestServer::createInstance<>( controlToken );
        }

    } // h2accounting

} // utest

/**
 * @brief An initiator which throws leaves no phantom operation behind
 *
 * THE NEGATIVE CONTROL. Against the driver as it was, the assertion at the end of this case fails
 * on its bound: postToStrand( ) throws with the mailbox's operation already begun, the pending
 * count carries it for good, and the connection can no longer take the terminal path however it is
 * asked to. With the guard in place the same throw leaves the count exactly where it found it, the
 * cancel is answered, and the task ends
 *
 * The throw itself is NOT what changes - submit( ) reports the failure to its caller before and
 * after, which is the other half of the shape: the guard gives the operation back and lets the
 * exception take the route it already took
 */

UTF_AUTO_TEST_CASE( H2Driver_AnInitiatorWhichThrowsLeavesNoPhantomOperationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::h2driver;
    using namespace utest::h2accounting;

    const auto peer = makeAccountingPeer();

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            BL_CHK(
                false,
                "GET" == request.method,
                BL_MSG()
                    << "The peer was asked for an unexpected request: "
                    << request.method
                    << " "
                    << request.path
                );

            /*
             * The connection is deliberately left OPEN - the case needs it in Ready when it arms
             * the seam, because a connection which has closed its submissions answers a submit( )
             * instead of posting it
             */

            return h2peer::Http2ResponseScript()
                .headers( 200U, bl::http2::HpackFieldList() )
                .data( "hello" )
                .endStream();
        }
        );

    withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto record = std::make_shared< FallbackRecord >();

            const auto driver = AccountingProbe::createInstance(
                makeKey( "http", "127.0.0.1", port ),
                makeFallbackFactory< ThrowingPostPolicy >( record ),
                Http2ConnectionConfig(),
                cleartextHttp2Config()
                );

            const auto connection = om::qi< httpclient::ClientConnection >( driver );
            const auto task = om::qi< Task >( driver );

            /*
             * THE SINK IS DELIBERATELY NOT GIVEN THE CONNECTION, which is not an omission. A sink
             * which credits flow control posts a Consumed command of its own, and a command posted
             * while the strand has not drained the mailbox yet sets no new drain - so the seam
             * below would be armed for a post which postCommand( ) never makes, and the case would
             * be a race rather than a control. The response here is five octets, far inside the
             * initial window, so nothing needs crediting
             */

            const auto sink = RecordingSink::createInstance();

            bool stopped = false;
            bool submitThrew = false;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    /*
                     * One ordinary exchange first, so that the connection is established, Ready,
                     * and holding exactly what a steady state holds - the read of design 5.1. The
                     * sink's own rendezvous is what orders this against the strand
                     */

                    const auto handle = connection -> submit(
                        makeRequest( "http://127.0.0.1/hello" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    sink -> waitForClosed();

                    /*
                     * The initiator of the mailbox post fails on the NEXT submit, with the
                     * operation already begun
                     */

                    postFailureArmed().store( true );

                    const auto second = RecordingSink::createInstance();

                    try
                    {
                        ( void ) connection -> submit(
                            makeRequest( "http://127.0.0.1/hello" ),
                            om::qi< httpclient::ClientStreamEventSink >( second )
                            );
                    }
                    catch( std::exception& )
                    {
                        submitThrew = true;
                    }

                    /*
                     * DISARMED WHATEVER HAPPENED. The driver posts to the strand from cancelTask( )
                     * too, and a seam left armed would fail the teardown instead of the initiator
                     * this case is about - which is a hung module rather than a test result
                     */

                    postFailureArmed().store( false );

                    /*
                     * Now ask the connection to end, and see whether it can
                     *
                     * The task's OWN requestCancel( ), and not the queue's cancel( task, false ):
                     * that one only removes a task which has not started yet and answers false for
                     * one which is executing - it never reaches cancelTask( ), so the connection
                     * would simply keep running and the case would time out for the wrong reason
                     */

                    task -> requestCancel();

                    stopped = driver -> waitForStopped( DEFAULT_WAIT_IN_MILLISECONDS );

                    if( ! stopped )
                    {
                        ( void ) driver -> chkReleaseAbandonedOperations( 8U );
                    }

                    eq -> wait( task );
                }
                );

            UTF_REQUIRE( submitThrew );

            UTF_REQUIRE_EQUAL( sink -> status(), 200U );
            UTF_REQUIRE_EQUAL( sink -> body(), std::string( "hello" ) );

            /*
             * THE ASSERTION THIS CASE EXISTS FOR
             */

            UTF_REQUIRE( stopped );

            UTF_REQUIRE( ConnectionState::Closed == connection -> state() );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

#endif /* __UTEST_TESTHTTP2DRIVERACCOUNTING_H_ */
