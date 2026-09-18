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

#include <baselib/core/OS.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/TimeUtils.h>

#include <utests/baselib/Utf.h>

#include <atomic>
#include <cstddef>

/************************************************************************
 * Thread pool size() and resize() tests
 *
 * These pin the observable contract of the two members which read the
 * pool's thread vector, so that putting those reads under the pool lock
 * can be shown not to change any of the answers.
 */

UTF_AUTO_TEST_CASE( Tasks_ThreadPoolSizeContractTests )
{
    using namespace bl;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_ThreadPoolSizeContractTests ********************************\n" );

    const auto tp = ThreadPoolImpl::createInstance< ThreadPool >(
        os::AbstractPriority::Normal,
        2U /* threadsCount */
        );

    {
        const auto disposeLock = om::lockDisposable( tp );

        UTF_REQUIRE_EQUAL( 2U, tp -> size() );

        /*
         * The pool can only grow, so a request to shrink is ignored and
         * reports the size unchanged
         */

        UTF_REQUIRE_EQUAL( 2U, tp -> resize( 1U ) );
        UTF_REQUIRE_EQUAL( 2U, tp -> size() );

        /*
         * A request for the size the pool already has is a nop
         */

        UTF_REQUIRE_EQUAL( 2U, tp -> resize( 2U ) );
        UTF_REQUIRE_EQUAL( 2U, tp -> size() );

        /*
         * A request to grow returns the new size and size() agrees with it
         */

        UTF_REQUIRE_EQUAL( 5U, tp -> resize( 5U ) );
        UTF_REQUIRE_EQUAL( 5U, tp -> size() );
    }

    /*
     * The dispose lock above has disposed the pool, which swaps the threads out
     * of the pool and joins them, so the pool now reports no threads and refuses
     * to be resized
     */

    UTF_REQUIRE_EQUAL( 0U, tp -> size() );

    UTF_REQUIRE_THROW( tp -> resize( 8U ), UnexpectedException );
}

UTF_AUTO_TEST_CASE( Tasks_ThreadPoolConcurrentSizeReadTests )
{
    using namespace bl;

    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******************************** Starting test: Tasks_ThreadPoolConcurrentSizeReadTests ********************************\n" );

    /*
     * A caller of size() concurrent with a caller growing the pool is the access
     * pair the pool lock has to cover, and no other case exercises it - the size()
     * call in Tasks_ThreadPoolResizeAndDisposeTests is made after both of its resize
     * threads have been joined, so it is ordered against them
     *
     * The reader asserts only what the growth contract guarantees: every value it
     * observes is a size the pool really had, so it lies between the size the pool
     * was created with and the size it is being grown to, and it never goes backwards
     */

    const std::size_t initialSize = 2U;
    const std::size_t finalSize = 12U;

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        const auto tp = ThreadPoolImpl::createInstance< ThreadPool >(
            os::AbstractPriority::Normal,
            initialSize
            );

        {
            const auto disposeLock = om::lockDisposable( tp );

            std::atomic< bool > stop( false );
            std::atomic< std::size_t > reads( 0U );
            std::atomic< std::size_t > outOfRange( 0U );
            std::atomic< std::size_t > wentBackwards( 0U );

            os::thread reader(
                [ &tp, &stop, &reads, &outOfRange, &wentBackwards ]() -> void
                {
                    std::size_t previous = initialSize;

                    while( ! stop )
                    {
                        const auto observed = tp -> size();

                        if( observed < initialSize || observed > finalSize )
                        {
                            ++outOfRange;
                        }

                        if( observed < previous )
                        {
                            ++wentBackwards;
                        }

                        previous = observed;

                        ++reads;
                    }
                }
                );

            /*
             * Wait for the reader to have observed the initial size before the pool starts
             * growing, so that its reads overlap the growth by construction rather than by the
             * scheduler's favor: on a busy host the three resize calls below can complete before
             * a freshly created thread has run at all, and the requirement that it read
             * something would then fail on timing alone
             *
             * The wait is bounded so that a reader which never runs fails the assertion below
             * with its diagnostic rather than hanging the module until the harness timeout kills
             * it; five seconds is far beyond the tick this is waiting for
             */

            for( std::size_t waited = 0U; 0U == reads.load() && waited < 5000U; ++waited )
            {
                os::sleep( time::milliseconds( 1 ) );
            }

            tp -> resize( 4U );
            tp -> resize( 8U );
            tp -> resize( finalSize );

            stop = true;
            reader.join();

            UTF_REQUIRE_EQUAL( finalSize, tp -> size() );

            UTF_REQUIRE( reads.load() > 0U );
            UTF_REQUIRE_EQUAL( 0U, outOfRange.load() );
            UTF_REQUIRE_EQUAL( 0U, wentBackwards.load() );
        }

        tp -> dispose();
    }
}
