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

#ifndef __BL_TEST_MACHINEGLOBALTESTLOCK_H_
#define __BL_TEST_MACHINEGLOBALTESTLOCK_H_

#include <utests/baselib/UtfConcurrent.h>

#include <baselib/core/Checksum.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <exception>
#include <memory>
#include <string>
#include <type_traits>

namespace test
{
    /**
     * @brief class MachineGlobalTestLock - a machine global test lock
     */

    template
    <
        typename E = void
    >
    class MachineGlobalTestLockT
    {
        BL_NO_COPY_OR_MOVE( MachineGlobalTestLockT )

    private:

        typedef bl::os::RobustNamedMutex                mutex_t;

        enum : long
        {
            /**
             * @brief How often a progress line is emitted while the lock is held elsewhere
             */

            PROGRESS_INTERVAL_IN_SECONDS = 30L,

            /**
             * @brief The hard bound after which the acquisition is declared hung
             *
             * This matches the spirit of TestMessagingUtilsT::createNoOfConnections(), which
             * polls, escalates at 5 minutes and rips at 15
             */

            ACQUIRE_TIMEOUT_IN_SECONDS = 10L * 60L,

            /**
             * @brief The settle time before the lock is released
             *
             * It compensates for the SO_REUSEADDR / linger configuration which TcpBaseTasks.h
             * applies to the acceptor sockets - without it the next acquirer can hit
             * 'Cannot assign requested address'. A readiness probe on the *next* acquirer
             * would be a better guarantee than a sleep on the previous releaser, so this can
             * be reduced once such a probe exists
             */

            RELEASE_SETTLE_TIME_IN_SECONDS = 1L,
        };

        /**
         * @brief The state shared with the detached acquiring thread
         */

        struct AcquireState
        {
            utest::TestSignal                           signal;
            bl::cpp::SafeUniquePtr< mutex_t::Guard >    guard;
            std::exception_ptr                          exception;
        };

        static const std::string                        g_defaultLockName;

        const std::string                               m_name;
        mutex_t                                         m_lock;
        bl::cpp::SafeUniquePtr< mutex_t::Guard >        m_guard;

        /**
         * @brief The diagnostics an operator needs in order to clear a stale lock
         */

        std::string hungLockDiagnostics() const
        {
            bl::cs::crc_32_type crcc;

            crcc.process_bytes( m_name.c_str(), m_name.size() );

            const auto checksum = crcc.checksum();

            /*
             * The key is derived exactly the way OSImplUNIX.h's semOpenOrCreate() derives it,
             * so an operator can locate the semaphore with 'ipcs' and remove it with 'ipcrm'
             */

            const auto key =
                static_cast< std::remove_const< decltype( checksum ) >::type >( -1 ) == checksum
                ? 1234U /* the same default semOpenOrCreate() uses */
                : checksum;

            return bl::resolveMessage(
                BL_MSG()
                    << "The global machine test lock '"
                    << m_name
                    << "' could not be acquired within "
                    << static_cast< long >( ACQUIRE_TIMEOUT_IN_SECONDS )
                    << " seconds by process "
                    << bl::os::getPid()
                    << "; on UNIX the lock is a System V semaphore with the key "
                    << key
                    << " - use 'ipcs -s' to find it and 'ipcrm -S "
                    << key
                    << "' to remove it when it is stale"
                );
        }

        /**
         * @brief Acquires the lock with a bounded, diagnosable wait
         *
         * bl::os::RobustNamedMutex::lock() is an untimed ::semop, so a lock which is held by
         * a leftover process, by a stale semaphore or by a concurrent developer run would
         * otherwise block this process forever behind a single debug line - the build then
         * times out with no indication of which module, which case or which lock
         */

        void acquireWithWatchdog()
        {
            const auto state = std::make_shared< AcquireState >();

            mutex_t* const lock = &m_lock;

            /*
             * The acquiring thread must be detached rather than joined: after BL_RIP_MSG the
             * process is going down anyway, and joining a thread which is blocked in the
             * untimed ::semop would deadlock the teardown
             */

            bl::os::thread acquirer(
                [ state, lock ]() -> void
                {
                    try
                    {
                        state -> guard.reset( new ( mutex_t::Guard )( *lock ) );
                    }
                    catch( std::exception& )
                    {
                        state -> exception = std::current_exception();
                    }

                    state -> signal.signal();
                }
                );

            acquirer.detach();

            const auto started = bl::time::second_clock::universal_time();

            while(
                ! state -> signal.wait(
                    static_cast< std::size_t >( PROGRESS_INTERVAL_IN_SECONDS ) * 1000U /* timeoutInMilliseconds */
                    )
                )
            {
                const auto elapsed =
                    ( bl::time::second_clock::universal_time() - started ).total_seconds();

                BL_LOG(
                    bl::Logging::info(),
                    BL_MSG()
                        << "Still waiting for the global machine test lock '"
                        << m_name
                        << "' after "
                        << elapsed
                        << " seconds ..."
                    );

                if( elapsed >= static_cast< long >( ACQUIRE_TIMEOUT_IN_SECONDS ) )
                {
                    BL_RIP_MSG( hungLockDiagnostics() );
                }
            }

            if( state -> exception )
            {
                bl::cpp::safeRethrowException( state -> exception );
            }

            m_guard.swap( state -> guard );
        }

    public:

        MachineGlobalTestLockT( SAA_in_opt const std::string& name = g_defaultLockName )
            :
            m_name( name ),
            m_lock( name )
        {
            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "Trying to acquire the global machine test lock with name '"
                    << m_name
                    << "' ..."
                );

            acquireWithWatchdog();

            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "The global machine test lock with name '"
                    << m_name
                    << "' was acquired"
                );
        }

        ~MachineGlobalTestLockT() NOEXCEPT
        {
            if( m_guard )
            {
                /*
                 * Make sure we sleep one second before we release the lock to ensure that
                 * the acceptor socket(s) are indeed released and we don't hit issues of
                 * the type 'Cannot assign requested address'
                 */

                bl::os::sleep( bl::time::seconds( static_cast< long >( RELEASE_SETTLE_TIME_IN_SECONDS ) ) );

                m_guard.reset();

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "The global machine test lock with name '"
                        << m_name
                        << "' was released"
                    );
            }
        }
    };

    template
    <
        typename E
    >
    const std::string
    MachineGlobalTestLockT< E >::g_defaultLockName(
        "Test-Machine-Global-Lock-27f6db3c-4d61-4edb-8b66-c8ea32574c0c"
        );

    typedef MachineGlobalTestLockT<> MachineGlobalTestLock;

} // test

#endif /* __BL_TEST_MACHINEGLOBALTESTLOCK_H_ */
