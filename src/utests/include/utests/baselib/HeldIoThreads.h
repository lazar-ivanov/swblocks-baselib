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


#ifndef __UTEST_HELDIOTHREADS_H_
#define __UTEST_HELDIOTHREADS_H_

#include <baselib/core/ThreadPool.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>

/*
 * Holding all but one thread of the I/O thread pool, so that the handlers of a case run in the order
 * they were queued - how the CS-6 cases make the order of a cancel certain without a delay. Shared by
 * utf_baselib_tasks3, where it was written, and utf_baselib_tasks4
 */

namespace utest
{
    namespace heldiothreads
    {
        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,
        };

        /**
         * @brief Holds all but one thread of the I/O thread pool, so that the one left runs every
         * handler of the case in the order they were queued
         *
         * Each held thread runs a handler which blocks until the destructor releases it. The
         * destructor also waits for each to return, because each refers to this object
         */

        class HeldIoThreads
        {
            BL_NO_COPY_OR_MOVE( HeldIoThreads )

        public:

            HeldIoThreads()
                :
                m_toHold( 0U ),
                m_holding( 0U ),
                m_returned( 0U ),
                m_isReleased( false ),
                m_isHeld( false )
            {
                const auto pool = bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::NonBlocking );

                BL_CHK(
                    false,
                    nullptr != pool && pool -> size() >= 1U,
                    BL_MSG()
                        << "The I/O thread pool is not available"
                    );

                m_toHold = pool -> size() - 1U;

                for( std::size_t i = 0U; i < m_toHold; ++i )
                {
                    pool -> aioService().post(
                        [ this ]() -> void
                        {
                            hold();
                        }
                        );
                }

                bl::os::mutex_unique_lock guard( m_lock );

                m_isHeld = m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_holding == m_toHold;
                    }
                    );
            }

            ~HeldIoThreads() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                bl::os::mutex_unique_lock guard( m_lock );

                m_isReleased = true;

                m_cv.notify_all();

                ( void ) m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return m_returned == m_holding;
                    }
                    );

                BL_NOEXCEPT_END()
            }

            bool isHeld() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_isHeld;
            }

        private:

            void hold()
            {
                bl::os::mutex_unique_lock guard( m_lock );

                ++m_holding;

                m_cv.notify_all();

                m_cv.wait(
                    guard,
                    [ this ]() -> bool
                    {
                        return m_isReleased;
                    }
                    );

                ++m_returned;

                m_cv.notify_all();
            }

            mutable bl::os::mutex                                               m_lock;
            bl::os::condition_variable                                          m_cv;
            std::size_t                                                         m_toHold;
            std::size_t                                                         m_holding;
            std::size_t                                                         m_returned;
            bool                                                                m_isReleased;
            bool                                                                m_isHeld;
        };

    } // heldiothreads

} // utest

#endif /* __UTEST_HELDIOTHREADS_H_ */
