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

#ifndef __UTEST_TLSTEARDOWNTESTUTILS_H_
#define __UTEST_TLSTEARDOWNTESTUTILS_H_

#include <baselib/httpclient/ClientConnection.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/Utf.h>

/*
 * Running a TLS consumer - an HTTP driver, SimpleHttpTask - against utests/baselib/TlsEndingPeer.h to
 * the end of its teardown, and asserting how that teardown ended. The I2 cases of CS-6 on the real
 * consumers share it (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md, 8): each
 * consumer's protocol timer is shortened, so that a teardown which waits for the peer's close_notify
 * is told apart by being cancelled rather than by a bound expiring
 */

namespace utest
{
    namespace tlsteardown
    {
        using tlsendingpeer::WAIT_IN_MILLISECONDS;
        using tlsendingpeer::describeCode;
        using tlsendingpeer::TlsEndingPeer;

        enum : std::size_t
        {
            /**
             * @brief The bound a teardown must end within - well above the shortened protocol timer,
             * so that today's code ends inside it too and is told apart by isCanceled( )
             */

            TEARDOWN_BOUND_IN_MILLISECONDS      = 10000U,
        };

        /**
         * @brief The protocol timer each consumer gets, shortened so that a wait for the peer's
         * close_notify ends quickly
         */

        inline auto shortProtocolTimeout() -> bl::time::time_duration
        {
            return bl::time::seconds( 3 );
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
         * @brief A one-shot signal a probe raises on one of its threads and a case waits for on the
         * test thread - a stop, raised from onTaskStoppedNothrow( ), which is reached only once the
         * task's finish continuation, the TLS shutdown, is over; or a moment the case must not run
         * ahead of
         */

        class OneShotSignal
        {
            BL_NO_COPY_OR_MOVE( OneShotSignal )

        public:

            OneShotSignal()
                :
                m_isRaised( false )
            {
            }

            void signal()
            {
                BL_MUTEX_GUARD( m_lock );

                m_isRaised = true;

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
                        return m_isRaised;
                    }
                    );
            }

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            bool                                                                m_isRaised;
        };

        /**
         * @brief How one consumer's teardown ended, read on the test thread
         */

        struct TeardownResult
        {
            bool                                                                hasSettled;
            bool                                                                hasStoppedWithinBound;
            bool                                                                isCanceled;
            bool                                                                isFailed;
            bl::eh::error_code                                                  taskCode;
            std::vector< std::string >                                          peerRecords;

            TeardownResult()
                :
                hasSettled( true ),
                hasStoppedWithinBound( false ),
                isCanceled( false ),
                isFailed( false )
            {
            }

            bool hasPeerRecord( SAA_in const std::string& record ) const
            {
                for( const auto& recorded : peerRecords )
                {
                    if( recorded == record )
                    {
                        return true;
                    }
                }

                return false;
            }

            auto describe() const -> std::string
            {
                std::string peer;

                for( const auto& record : peerRecords )
                {
                    peer += ( peer.empty() ? "" : " | " ) + record;
                }

                return
                    std::string( "settled before the ending " ) +
                    ( hasSettled ? "yes" : "no" ) +
                    ", stopped within the bound " +
                    ( hasStoppedWithinBound ? "yes" : "no" ) +
                    ", cancelled when it stopped " +
                    ( isCanceled ? "yes" : "no" ) +
                    ", task " +
                    ( isFailed ? "failed " + describeCode( taskCode ) : std::string( "succeeded" ) ) +
                    ", peer " +
                    peer;
            }
        };

        /**
         * @brief Runs a consumer to its end: pushes it, runs 'afterPush' when there is one - which
         * releases a held ending once the consumer has settled, and answers whether it did - waits
         * the teardown bound, and reads whether it was cancelled BEFORE the case's own safety
         * cancel, which ends a regression instead of letting it hang the module
         *
         * PROBE is the consumer's ObjectImpl, with a waitForStop( ) of its own
         */

        template
        <
            typename PROBE
        >
        inline auto runToTheEnd(
            SAA_in          const bl::om::ObjPtr< PROBE >&                      probe,
            SAA_inout       TlsEndingPeer&                                      peer,
            SAA_in_opt      const bl::cpp::function< bool () >&                 afterPush =
                                bl::cpp::function< bool () >()
            )
            -> TeardownResult
        {
            using namespace bl;
            using namespace bl::tasks;

            TeardownResult result;

            const auto task = om::qi< Task >( probe );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    if( afterPush )
                    {
                        result.hasSettled = afterPush();
                    }

                    result.hasStoppedWithinBound =
                        probe -> waitForStop( static_cast< std::size_t >( TEARDOWN_BOUND_IN_MILLISECONDS ) );

                    result.isCanceled = probe -> isCanceled();

                    if( ! result.hasStoppedWithinBound )
                    {
                        task -> requestCancel();
                    }

                    ( void ) probe -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    peer.release();

                    eq -> wait( task );
                }
                );

            result.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                result.taskCode = eh::errorCodeFromExceptionPtr( exception );
            }

            ( void ) peer.waitForRecord( "script-ended" );

            result.peerRecords = peer.records();

            return result;
        }

        inline auto makeTlsKey( SAA_in const bl::os::port_t port ) -> bl::httpclient::ConnectionKey
        {
            bl::httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = "localhost";
            key.port = port;

            return key;
        }

        /**
         * @brief What every consumer's truncated teardown asserts: the consumer had settled before
         * the peer ended the stream, its teardown ended within the bound, NOT because the protocol
         * timer cancelled it, and clean - and the peer read our close_notify
         */

        inline void chkEndedWithoutWaiting(
            SAA_in          const TeardownResult&                               result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.hasSettled,
                which + ": the client did not settle before the peer's ending; " + result.describe()
                );

            chkOrFail(
                result.hasStoppedWithinBound,
                which + ": the teardown did not end within the bound; " + result.describe()
                );

            chkOrFail(
                ! result.isCanceled,
                which + ": the teardown ended only because the protocol timer cancelled it; " + result.describe()
                );

            chkOrFail(
                ! result.isFailed,
                which + ": the task failed; " + result.describe()
                );

            chkOrFail(
                result.hasPeerRecord( "client-ended:" + describeCode( bl::asio::error::eof ) ),
                which + ": the peer did not read our close_notify; " + result.describe()
                );
        }

    } // tlsteardown

} // utest

#endif /* __UTEST_TLSTEARDOWNTESTUTILS_H_ */
