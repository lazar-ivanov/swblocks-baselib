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

#ifndef __UTEST_UTFCONCURRENT_H_
#define __UTEST_UTFCONCURRENT_H_

#include <utests/baselib/Utf.h>

#include <baselib/core/OS.h>

#include <atomic>
#include <string>

/*
 * Note that this header is included by tests which link against the tasks and the core
 * libraries only, so it must not depend on anything beyond Utf.h and baselib/core/OS.h
 */

namespace utest
{
    /**
     * @brief A simple manually reset signal with a bounded wait, so a test can
     * synchronize with a callback or with a task which is executing
     */

    class TestSignal
    {
        BL_NO_COPY_OR_MOVE( TestSignal )

    private:

        mutable bl::os::mutex           m_lock;
        bl::os::condition_variable      m_cv;
        bool                            m_signaled;

    public:

        TestSignal()
            :
            m_signaled( false )
        {
        }

        void signal() NOEXCEPT
        {
            {
                BL_MUTEX_GUARD( m_lock );

                m_signaled = true;
            }

            m_cv.notify_all();
        }

        bool wait()
        {
            return wait( 10U * 1000U /* timeoutInMilliseconds */ );
        }

        /**
         * @brief A wait with an explicit bound, so a test can also assert that something
         * has *not* happened without blocking for the full default timeout
         */

        bool wait( SAA_in const std::size_t timeoutInMilliseconds )
        {
            bl::os::mutex_unique_lock guard( m_lock );

            return m_cv.wait_for(
                guard,
                bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                [ this ]() -> bool
                {
                    return m_signaled;
                }
                );
        }

        /**
         * @brief Samples the current state without waiting
         */

        bool isSignaled() const
        {
            BL_MUTEX_GUARD( m_lock );

            return m_signaled;
        }
    };

    /**
     * @brief A recorder for assertions which have to be made on a thread other than the
     * main test thread (e.g. a thread pool worker or a task handler)
     *
     * The Boost.Test framework state is not thread safe and a failing REQUIRE-level
     * assertion throws boost::execution_aborted, which does not derive from std::exception
     * and therefore escapes both the task layer and the thread pool - terminating the
     * process without naming the test case which failed
     *
     * Worker threads record their predicates here instead and the main test thread calls
     * requireNone() at the point where it has joined with the workers
     */

    class DeferredAssertions
    {
        BL_NO_COPY_OR_MOVE( DeferredAssertions )

    private:

        std::atomic< std::size_t >      m_failures;
        mutable bl::os::mutex           m_lock;
        std::string                     m_firstFailure;

    public:

        DeferredAssertions()
            :
            m_failures( 0U )
        {
        }

        /**
         * @brief Records the outcome of an assertion; safe to call from any thread and
         * never throws Boost.Test's execution_aborted
         */

        void record(
            SAA_in                  const bool                                      condition,
            SAA_in                  std::string                                     message
            )
        {
            if( condition )
            {
                return;
            }

            ++m_failures;

            BL_MUTEX_GUARD( m_lock );

            if( m_firstFailure.empty() )
            {
                m_firstFailure = std::move( message );
            }
        }

        std::size_t failures() const NOEXCEPT
        {
            return m_failures;
        }

        /**
         * @brief The predicate text of the first recorded failure
         *
         * Note that this must only be called once the recording threads have been joined
         */

        const std::string& firstFailure() const NOEXCEPT
        {
            return m_firstFailure;
        }

        /**
         * @brief Fails the current test case if anything was recorded
         *
         * Note that this must only be called from the main test thread
         */

        void requireNone() const
        {
            if( 0U != failures() )
            {
                UTF_MESSAGE( firstFailure() );
            }

            UTF_REQUIRE_EQUAL( failures(), 0U );
        }
    };

} // utest

#define UTF_RECORD( recorder, expr ) \
    ( recorder ).record( !!( expr ), #expr ) \

#endif /* __UTEST_UTFCONCURRENT_H_ */
