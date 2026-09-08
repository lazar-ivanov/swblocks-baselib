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

#include <baselib/tasks/Algorithms.h>

#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/************************************************************************
 * parallelMap error and boundary behavior
 */

UTF_AUTO_TEST_CASE( Tasks_ParallelMap_ErrorsAndBoundaries )
{
    /*
     * Tasks_ParallelMap_4 and Tasks_ParallelMap_1024 both use a non-throwing functor and the
     * default maxExecuting, so what an empty range does, what happens when the functor throws,
     * and whether maxExecuting is plumbed through at all are all unverified. parallelMap has no
     * production caller anywhere - only its definition in Algorithms.h - so this is public API
     * hygiene
     *
     * NOTE: parallelMap writes concurrently into distinct result[ index ] slots. That is well
     * defined for std::vector< T >, but NOT for std::vector< bool >, whose proxy references
     * share the underlying words - so the algorithm must never be instantiated with bool
     */

    std::vector< std::size_t > input;

    for( std::size_t i = 0; i < 5U; ++i )
    {
        input.push_back( i );
    }

    std::function< std::string ( SAA_in const std::size_t& ) > toString =
        []( SAA_in const std::size_t& i ) -> std::string
        {
            return std::to_string( i );
        };

    /*
     * An empty range schedules nothing and returns an empty vector
     */

    std::vector< std::string > emptyResult;

    UTF_REQUIRE_NO_THROW(
        emptyResult = bl::tasks::parallelMap( input.begin(), input.begin(), toString )
        );

    UTF_REQUIRE( emptyResult.empty() );

    /*
     * A single failing element fails the whole call - the per element exception is not
     * swallowed and no partially filled result is handed back
     */

    std::function< std::string ( SAA_in const std::size_t& ) > throwOnThree =
        []( SAA_in const std::size_t& i ) -> std::string
        {
            if( 3U == i )
            {
                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "element-3 failed"
                    );
            }

            return std::to_string( i );
        };

    UTF_REQUIRE_THROW_MESSAGE(
        bl::tasks::parallelMap( input.begin(), input.end(), throwOnThree ),
        bl::UnexpectedException,
        "element-3 failed"
        );

    /*
     * The maxExecuting argument is plumbed into eq -> setThrottleLimit(), and timing the same
     * four 200 ms sleeps twice is the only way to observe it: throttled to one they cannot
     * finish in under 800 ms, run four at a time they take about 200 ms. The thresholds are
     * deliberately loose - the module already relies on comparable wall clock assertions
     */

    std::vector< std::size_t > four;

    for( std::size_t i = 0; i < 4U; ++i )
    {
        four.push_back( i );
    }

    std::function< std::string ( SAA_in const std::size_t& ) > sleepAndToString =
        []( SAA_in const std::size_t& i ) -> std::string
        {
            bl::os::sleep( bl::time::milliseconds( 200 ) );

            return std::to_string( i );
        };

    const auto measure = [ & ]( SAA_in const std::size_t maxExecuting ) -> bl::time::time_duration
    {
        const auto start = bl::os::get_system_time();

        const std::vector< std::string > output = bl::tasks::parallelMap(
            four.begin(),
            four.end(),
            sleepAndToString,
            maxExecuting
            );

        const auto elapsed = bl::os::get_system_time() - start;

        UTF_REQUIRE_EQUAL( output.size(), four.size() );

        for( std::size_t i = 0; i < four.size(); ++i )
        {
            UTF_CHECK_EQUAL( std::to_string( i ), output[ i ] );
        }

        return elapsed;
    };

    const auto throttled = measure( 1U /* maxExecuting */ );
    const auto unthrottled = measure( 4U /* maxExecuting */ );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "parallelMap of 4 x 200ms sleeps took "
            << throttled.total_milliseconds()
            << "ms with maxExecuting = 1 and "
            << unthrottled.total_milliseconds()
            << "ms with maxExecuting = 4"
        );

    UTF_REQUIRE( throttled >= bl::time::milliseconds( 700 ) );
    UTF_REQUIRE( unthrottled < bl::time::milliseconds( 500 ) );
}
