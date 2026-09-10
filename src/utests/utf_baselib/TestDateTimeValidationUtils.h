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

#include <baselib/core/DateTimeValidationUtils.h>

#include <utests/baselib/Utf.h>

/*
 * bl::time::DateTimeValidationUtils has no in-repo callers and is not reached through
 * PreCompiled.h, so the production header has to be included explicitly above
 *
 * The three validators all report through BL_THROW_USER / BL_CHK_USER, i.e. they throw
 * bl::UserMessageException and nothing else
 */

UTF_AUTO_TEST_CASE( DateTimeValidation_ValidateTime )
{
    using namespace bl;
    using namespace bl::time;

    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateTime( "00:00:00" ) );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateTime( "23:59:59" ) );

    /*
     * The shape gates - an empty value and a value which is neither three ':' separated
     * fields nor exactly eight characters long
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "" ), UserMessageException );
    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "1:2:3" ), UserMessageException );

    /*
     * The per field upper limits - { 23, 59, 59 }
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "24:00:00" ), UserMessageException );
    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "00:60:00" ), UserMessageException );
    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "00:00:60" ), UserMessageException );

    /*
     * A non-numeric field reaches the catch( utils::bad_lexical_cast& ) fallback
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "aa:00:00" ), UserMessageException );

    /*
     * "1::23456" is eight characters long and does split into exactly three fields, so it
     * is rejected only by the third check - the size() != 2U clause on the first field
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "1::23456" ), UserMessageException );

    /*
     * A signed value is rejected by bad_lexical_cast because the cast target is unsigned,
     * not by the upper limit comparison
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateTime( "-1:00:00" ), UserMessageException );

    /*
     * The message names the offending value and the expected format
     */

    UTF_REQUIRE_THROW_MESSAGE(
        DateTimeValidationUtils::validateTime( "24:00:00" ),
        UserMessageException,
        "Value '24:00:00' is out of range or not in valid time format. Should be in 'HH:MM:SS' format."
        );
}

UTF_AUTO_TEST_CASE( DateTimeValidation_ValidateStartEndRange )
{
    using namespace bl;
    using namespace bl::time;

    /*
     * A bare date really is accepted - Boost's time_input_facet::get loop terminates at
     * end of input and still yields a valid ptime, so the not_a_date_time gate does not
     * fire and the optional time part is simply absent
     */

    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateStartEndRange( "20150101" ) );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateStartEndRange( "20150101 12:30:45" ) );

    /*
     * The empty input indexes dateTime[ 0 ] straight after the split, so this also pins
     * that bl::str::split yields one empty token rather than an empty vector - anything
     * else would be an out of range access rather than a thrown UserMessageException
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateStartEndRange( "" ), UserMessageException );

    /*
     * The date part must be exactly eight characters
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateStartEndRange( "2015010" ), UserMessageException );

    /*
     * Month 13 leaves the ptime as not_a_date_time
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateStartEndRange( "20151301" ), UserMessageException );

    /*
     * The date part is valid here and ptime happily normalises the time, so this input is
     * rejected only by the separate validateTime() pass - the branch the code comment in
     * validateStartEndRange() exists for. Dropping that pass would silently accept it
     */

    UTF_REQUIRE_THROW(
        DateTimeValidationUtils::validateStartEndRange( "20150101 25:00:00" ),
        UserMessageException
        );

    UTF_REQUIRE_THROW_MESSAGE(
        DateTimeValidationUtils::validateStartEndRange( "2015010" ),
        UserMessageException,
        "Value '2015010' is out of range or not in valid datetime format."
        );
}

UTF_AUTO_TEST_CASE( DateTimeValidation_ValidateDayOfWeek )
{
    using namespace bl;
    using namespace bl::time;

    /*
     * The lookup is case insensitive and the input may be a ',' separated list
     */

    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateDayOfWeek( "Mon" ) );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateDayOfWeek( "mon" ) );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateDayOfWeek( "MON" ) );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::validateDayOfWeek( "Sun,Mon,Sat" ) );

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateDayOfWeek( "" ), UserMessageException );
    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateDayOfWeek( "Funday" ), UserMessageException );

    /*
     * Every element of the list is validated, not just the first
     */

    UTF_REQUIRE_THROW( DateTimeValidationUtils::validateDayOfWeek( "Mon,Funday" ), UserMessageException );

    /*
     * The message quotes the offending value lower cased and spells out the full allowed
     * list in g_dayOfWeekList order
     */

    UTF_REQUIRE_THROW_MESSAGE(
        DateTimeValidationUtils::validateDayOfWeek( "Funday" ),
        UserMessageException,
        "Invalid day of week found 'funday'. Allowed values: 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri' and 'Sat'"
        );
}

UTF_AUTO_TEST_CASE( DateTimeValidation_GetDateAndGetDateTime )
{
    using namespace bl;
    using namespace bl::time;

    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDate( "20150101" ), date( 2015, 1, 1 ) );

    /*
     * bl::time::date is boost::gregorian::date, which has is_not_a_date() rather than
     * is_not_a_date_time()
     *
     * Both parsers report a failed extraction by returning the not-a-date-time value and
     * never by throwing - that rests on the SafeInputStringStream exception mask being
     * badbit only, which BaseLib_SafeStringStreamTests pins directly; asserted here is
     * the consequence DateTimeValidationUtils actually depends on
     */

    UTF_REQUIRE( DateTimeValidationUtils::getDate( "bogus" ).is_not_a_date() );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::getDate( "bogus" ) );

    UTF_REQUIRE_EQUAL(
        DateTimeValidationUtils::getDateTime( "20150101 12:30:45" ),
        ptime( date( 2015, 1, 1 ), hours( 12 ) + minutes( 30 ) + seconds( 45 ) )
        );

    UTF_REQUIRE( DateTimeValidationUtils::getDateTime( "" ).is_not_a_date_time() );
    UTF_REQUIRE_NO_THROW( DateTimeValidationUtils::getDateTime( "" ) );
}

UTF_AUTO_TEST_CASE( DateTimeValidation_GetDayOfWeek )
{
    using namespace bl;
    using namespace bl::time;

    /*
     * g_dayOfWeekList is indexed with date::day_of_week(), where 0 is Sunday - these two
     * assertions are what pin the list ordering to Boost's numbering
     */

    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 4 ) ), "Sun" );
    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 10 ) ), "Sat" );

    /*
     * ... and the days in between, so a rotation of the list cannot pass on the endpoints
     * alone
     */

    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 5 ) ), "Mon" );
    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 6 ) ), "Tue" );
    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 7 ) ), "Wed" );
    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 8 ) ), "Thu" );
    UTF_REQUIRE_EQUAL( DateTimeValidationUtils::getDayOfWeek( date( 2015, 1, 9 ) ), "Fri" );
}
