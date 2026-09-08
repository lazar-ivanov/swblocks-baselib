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

#include <utests/baselib/Utf.h>

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/TimeZoneData.h>
#include <baselib/core/TimeUtils.h>

#include <cstdint>
#include <ctime>

static void logTestName( SAA_in const std::string& name )
{
    BL_LOG_MULTILINE(
        bl::Logging::info(),
        BL_MSG()
            << "\nTimezone data validation - "
            << name
            << " **********\n"
        );
}

UTF_AUTO_TEST_CASE( TestTimeZoneData )
{
    using namespace bl;
    using namespace bl::time;

    TimeZoneData::init();

    logTestName( "Empty Timezone check" );

    UTF_CHECK_EQUAL( TimeZoneData::validateTimeZone( "" ), false );

    logTestName( "Invalid Timezone check" );

    UTF_CHECK_EQUAL( TimeZoneData::validateTimeZone( "INVALID" ), false );

    logTestName( "Valid Timezone check" );

    UTF_CHECK_EQUAL( TimeZoneData::validateTimeZone( "IST" ), true );

    logTestName( "Invalid Timezone offset check" );

    UTF_CHECK_THROW( TimeZoneData::getTimeZoneOffset( "" ), UnexpectedException );

    logTestName( "Timezone offset check - Daylight saving timezone" );

    UTF_CHECK_EQUAL( TimeZoneData::getTimeZoneOffset( "GMT" ), "GMT+00:00:00BST+01:00:00,M3.5.0/+01:00:00,M10.5.0/+02:00:00" );

    UTF_CHECK_EQUAL( TimeZoneData::getTimeZoneOffset( "UTC" ), "UTC+00:00:00" );

    const auto fieldsGmt = TimeZoneData::getTimeZoneDataFields( "GMT" );
    UTF_CHECK( fieldsGmt.size() > 0 );

    const auto fieldsUtc = TimeZoneData::getTimeZoneDataFields( "UTC" );
    UTF_CHECK( fieldsUtc.size() > 0 );

    UTF_CHECK_EQUAL( fieldsGmt.size(), fieldsUtc.size() );

    logTestName( "Timezone offset check - Non-Daylight saving timezone" );

    UTF_CHECK_EQUAL( TimeZoneData::getTimeZoneOffset( "IST" ), "IST+05:30:00" );

    logTestName( "Timezone abbreviation priority resolution" );

    /*
     * The rows of g_timeZoneData are walked in order and the STD abbreviation of a row is
     * only registered when no row has claimed it yet - so without g_timeZonePriorityMap
     * and the 'continue' which enforces it, 'CST' would resolve to America/Belize and
     * 'EST' to America/Cayman ( both of which precede America/Chicago and
     * America/New_York in the table and carry NO daylight saving rules ), 'PST' to
     * America/Dawson and 'CET' to Africa/Algiers
     *
     * Comparing against the full POSIX spec is what makes this falsifiable: the
     * Central American rows produce "CST-06:00:00" / "EST-05:00:00" with no DST tail
     */

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "CST" ),
        "CST-06:00:00CDT+01:00:00,M3.2.0/+02:00:00,M11.1.0/+02:00:00"
        );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "EST" ),
        "EST-05:00:00EDT+01:00:00,M3.2.0/+02:00:00,M11.1.0/+02:00:00"
        );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "PST" ),
        TimeZoneData::getTimeZoneOffset( "America/Los_Angeles" )
        );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "CET" ),
        TimeZoneData::getTimeZoneOffset( "Europe/Paris" )
        );

    logTestName( "Timezone DST abbreviation keys" );

    /*
     * The DST abbreviation of a row gets its own key, but only for the row which also won
     * the STD abbreviation - so these keys disappear together with the priority handling
     */

    UTF_REQUIRE( TimeZoneData::validateTimeZone( "BST" ) );
    UTF_REQUIRE( TimeZoneData::validateTimeZone( "EDT" ) );
    UTF_REQUIRE( TimeZoneData::validateTimeZone( "CDT" ) );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "BST" ),
        TimeZoneData::getTimeZoneOffset( "GMT" )
        );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "EDT" ),
        TimeZoneData::getTimeZoneOffset( "EST" )
        );

    logTestName( "Timezone space substituted alias keys" );

    /*
     * Every Olson id which carries an underscore is also registered with the underscore
     * replaced by a space
     */

    UTF_REQUIRE( TimeZoneData::validateTimeZone( "America/New York" ) );

    UTF_REQUIRE_EQUAL(
        TimeZoneData::getTimeZoneOffset( "America/New York" ),
        TimeZoneData::getTimeZoneOffset( "America/New_York" )
        );

    UTF_REQUIRE( ! TimeZoneData::getTimeZoneDataMap().empty() );
}

UTF_AUTO_TEST_CASE( TestWindowsToOlsonMappingConsistency )
{
    using namespace bl;
    using namespace bl::time;

    TimeZoneData::init();

    /*
     * getOlsonFromWindows( ... ) is NOT gated on _WIN32, so this consistency check runs
     * everywhere. getDefaultTimeZone() on Windows returns its result WITHOUT validating
     * it, so a Windows -> Olson row whose target is missing from the Olson table produces
     * a non-empty time zone name for which validateTimeZone() is false and
     * getTimeZoneOffset() throws
     *
     * TimeZoneDataT exposes no accessor for g_windows2OlsonData, so the names below are
     * spelled out here rather than enumerated - this test cannot be exhaustive over the
     * table, but it does fail loudly the moment a fifth broken mapping is added
     */

    const char* const healthy[] =
    {
        "Eastern Standard Time",
        "GMT Standard Time",
        "India Standard Time",
        "AUS Eastern Standard Time",
        "Newfoundland Standard Time",
        "UTC"
    };

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( healthy ); ++i )
    {
        const auto olson = TimeZoneData::getOlsonFromWindows( healthy[ i ] );

        UTF_REQUIRE( ! olson.empty() );
        UTF_REQUIRE( TimeZoneData::validateTimeZone( olson ) );
    }

    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "Eastern Standard Time" ), "America/New_York" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "GMT Standard Time" ), "Europe/London" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "India Standard Time" ), "Asia/Calcutta" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "AUS Eastern Standard Time" ), "Australia/Sydney" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "Newfoundland Standard Time" ), "America/St_Johns" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getOlsonFromWindows( "UTC" ), "Etc/GMT" );

    /*
     * PRODUCTION DATA DEFECT - quarantined mappings
     *
     * These four rows of g_windows2OlsonData in baselib/core/TimeZoneData.h:
     *
     *     :948  "Dateline Standard Time,Etc/GMT+12"
     *     :1012 "UTC-02,Etc/GMT+2"
     *     :1013 "UTC-11,Etc/GMT+11"
     *     :1014 "UTC+12,Etc/GMT-12"
     *
     * name Olson zones which do NOT exist in g_timeZoneData - its only Etc/ row is
     * "Etc/GMT" ( :810 ). On a machine configured with one of these Windows zones
     * getDefaultTimeZone() therefore returns a name which validateTimeZone() rejects and
     * getTimeZoneOffset() throws on
     *
     * The current ( broken ) behaviour is pinned here rather than fixed, because the fix
     * is a production data change - either add the four Etc/GMT+/-N rows to
     * g_timeZoneData or drop the four mappings. When that lands, move these four names
     * into the healthy list above
     */

    const char* const quarantined[] =
    {
        "Dateline Standard Time",
        "UTC-02",
        "UTC-11",
        "UTC+12"
    };

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( quarantined ); ++i )
    {
        const auto olson = TimeZoneData::getOlsonFromWindows( quarantined[ i ] );

        UTF_REQUIRE( ! olson.empty() );
        UTF_REQUIRE( ! TimeZoneData::validateTimeZone( olson ) );
    }

    /*
     * The miss case must return the empty string and not a garbage non-empty one
     */

    UTF_REQUIRE( TimeZoneData::getOlsonFromWindows( "No Such Standard Time" ).empty() );
}

UTF_AUTO_TEST_CASE( TestTimeZoneOffsetIsValidPosixSpec )
{
    using namespace bl;
    using namespace bl::time;

    /*
     * Every TimeZoneData read API answers from empty maps until init() has run, and
     * Boost.Test can execute a single case in isolation ( --run_test=... ) or reorder
     * the cases, so init() must be the first statement of the case
     */

    TimeZoneData::init();

    /*
     * getTimeZoneOffset()'s sole purpose is to feed bl::time::posix_time_zone, which is
     * used nowhere else in the repository - i.e. the emitted text is a library-external
     * contract. Comparing it against a literal only pins the bytes; parsing it back is
     * what proves the eight field indices, the sign and the ",M<month>.<week>.<day>"
     * rule ordering are all still what Boost expects
     */

    const auto parseSpec = []( SAA_in const std::string& spec ) -> time_zone_ptr
    {
        return time_zone_ptr( new posix_time_zone( spec ) );
    };

    logTestName( "POSIX TZ spec - negative offset, northern hemisphere DST" );

    const std::string newYorkSpec( "EST-05:00:00EDT+01:00:00,M3.2.0/+02:00:00,M11.1.0/+02:00:00" );

    /*
     * 'EST' resolves to America/New_York through g_timeZonePriorityMap, so both keys
     * must emit the same spec
     */

    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "EST" ), newYorkSpec );
    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "America/New_York" ), newYorkSpec );

    const auto newYork = parseSpec( newYorkSpec );

    UTF_REQUIRE_EQUAL( newYork -> base_utc_offset(), hours( -5 ) );
    UTF_REQUIRE_EQUAL( newYork -> dst_offset(), hours( 1 ) );
    UTF_REQUIRE( newYork -> has_dst() );
    UTF_REQUIRE_EQUAL( newYork -> dst_local_start_time( 2015 ), ptime( date( 2015, 3, 8 ), hours( 2 ) ) );
    UTF_REQUIRE_EQUAL( newYork -> dst_local_end_time( 2015 ), ptime( date( 2015, 11, 1 ), hours( 2 ) ) );

    logTestName( "POSIX TZ spec - negative half hour offset, last week of month rule" );

    const std::string stJohnsSpec( "NST-03:30:00NDT+01:00:00,M4.1.0/+00:01:00,M10.5.0/+00:01:00" );

    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "America/St_Johns" ), stJohnsSpec );

    const auto stJohns = parseSpec( stJohnsSpec );

    UTF_REQUIRE_EQUAL( stJohns -> base_utc_offset(), -( hours( 3 ) + minutes( 30 ) ) );
    UTF_REQUIRE_EQUAL( stJohns -> dst_offset(), hours( 1 ) );
    UTF_REQUIRE( stJohns -> has_dst() );
    UTF_REQUIRE_EQUAL( stJohns -> dst_local_start_time( 2015 ), ptime( date( 2015, 4, 5 ), minutes( 1 ) ) );
    UTF_REQUIRE_EQUAL( stJohns -> dst_local_end_time( 2015 ), ptime( date( 2015, 10, 25 ), minutes( 1 ) ) );

    logTestName( "POSIX TZ spec - southern hemisphere, DST start month after end month" );

    const std::string sydneySpec( "AEST+10:00:00AEDT+01:00:00,M10.1.0/+02:00:00,M4.1.0/+03:00:00" );

    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "Australia/Sydney" ), sydneySpec );

    const auto sydney = parseSpec( sydneySpec );

    UTF_REQUIRE_EQUAL( sydney -> base_utc_offset(), hours( 10 ) );
    UTF_REQUIRE_EQUAL( sydney -> dst_offset(), hours( 1 ) );
    UTF_REQUIRE( sydney -> has_dst() );
    UTF_REQUIRE_EQUAL( sydney -> dst_local_start_time( 2015 ), ptime( date( 2015, 10, 4 ), hours( 2 ) ) );
    UTF_REQUIRE_EQUAL( sydney -> dst_local_end_time( 2015 ), ptime( date( 2015, 4, 5 ), hours( 3 ) ) );

    logTestName( "POSIX TZ spec - no DST zones" );

    const std::string istSpec( "IST+05:30:00" );

    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "IST" ), istSpec );

    const auto ist = parseSpec( istSpec );

    UTF_REQUIRE_EQUAL( ist -> base_utc_offset(), hours( 5 ) + minutes( 30 ) );
    UTF_REQUIRE( ! ist -> has_dst() );

    const std::string utcSpec( "UTC+00:00:00" );

    UTF_REQUIRE_EQUAL( TimeZoneData::getTimeZoneOffset( "UTC" ), utcSpec );

    const auto utc = parseSpec( utcSpec );

    UTF_REQUIRE_EQUAL( utc -> base_utc_offset(), hours( 0 ) );
    UTF_REQUIRE( ! utc -> has_dst() );

    logTestName( "POSIX TZ spec - end to end wall clock conversion" );

    /*
     * local_date_time( utcPtime, tz ) takes a time_zone_ptr, not a posix_time_zone
     * object, and interprets the ptime as UTC
     */

    const ptime winterUtc( date( 2015, 1, 15 ), hours( 12 ) );
    const ptime summerUtc( date( 2015, 7, 15 ), hours( 12 ) );

    const local_date_time newYorkWinter( winterUtc, newYork );
    const local_date_time newYorkSummer( summerUtc, newYork );

    UTF_REQUIRE_EQUAL( newYorkWinter.local_time(), ptime( date( 2015, 1, 15 ), hours( 7 ) ) );
    UTF_REQUIRE( ! newYorkWinter.is_dst() );

    UTF_REQUIRE_EQUAL( newYorkSummer.local_time(), ptime( date( 2015, 7, 15 ), hours( 8 ) ) );
    UTF_REQUIRE( newYorkSummer.is_dst() );

    const auto london = parseSpec( TimeZoneData::getTimeZoneOffset( "Europe/London" ) );

    const local_date_time londonWinter( winterUtc, london );
    const local_date_time londonSummer( summerUtc, london );

    UTF_REQUIRE_EQUAL( londonWinter.local_time(), ptime( date( 2015, 1, 15 ), hours( 12 ) ) );
    UTF_REQUIRE( ! londonWinter.is_dst() );

    UTF_REQUIRE_EQUAL( londonSummer.local_time(), ptime( date( 2015, 7, 15 ), hours( 13 ) ) );
    UTF_REQUIRE( londonSummer.is_dst() );

    logTestName( "DST start / end rule reordering" );

    /*
     * getDSTStartEndRule() takes "week;dayOfWeek;month" and must emit
     * "month.week.dayOfWeek", mapping the -1 week marker to the last week of month
     */

    UTF_REQUIRE_THROW( TimeZoneData::getDSTStartEndRule( "1;0" ), UnexpectedException );

    UTF_REQUIRE_EQUAL( TimeZoneData::getDSTStartEndRule( "-1;0;3" ), "3.5.0" );
    UTF_REQUIRE_EQUAL( TimeZoneData::getDSTStartEndRule( "2;0;3" ), "3.2.0" );
}

UTF_AUTO_TEST_CASE( TestISOTimeFormat )
{
    using namespace bl::time;

    const char timeDesignator = 'T';
    const char dateSeparator = '-';
    const char timeSeparator = ':';
    const char microSecondSeparator = '.';

    /*
     * YYYY-MM-DDThh:mm:ss.uuuuuu-hh:mm
     * OR
     * YYYY-MM-DDThh:mm:ss-hh:mm
     */

    std::string utcTimeStr1( "20020131T235959.000159" );

    ptime utcTime( from_iso_string( utcTimeStr1 ) );

    auto localTime = getLocalTimeISO( utcTime );

    bl::str::regex regex( regexLocalTimeISO() );
    bl::str::smatch results;
    UTF_CHECK( bl::str::regex_match( localTime, results, regex ) );

    UTF_REQUIRE( localTime.size() == 32U );

    UTF_CHECK( localTime.at( 4 ) == dateSeparator );
    UTF_CHECK( localTime.at( 7 ) == dateSeparator );

    UTF_CHECK( localTime.at( 10 ) == timeDesignator );

    UTF_CHECK( localTime.at( 13 ) == timeSeparator );
    UTF_CHECK( localTime.at( 16 ) == timeSeparator );

    UTF_CHECK( localTime.at( 19 ) == microSecondSeparator );

    UTF_CHECK( localTime.at( 29 ) == timeSeparator );

    ptime invalidUtcTime;

    UTF_CHECK( getLocalTimeISO( invalidUtcTime ).empty() );

    std::string utcTimeStr2( "20020131T235959" );
    std::string utcTimeStr3( "20020131T235959.145" );
    std::string utcTimeStr4( "20020131T235959.000000" );

    ptime utcTime2( from_iso_string( utcTimeStr2 ) );
    ptime utcTime3( from_iso_string( utcTimeStr3 ) );
    ptime utcTime4( from_iso_string( utcTimeStr4 ) );

    /*
     * Note that the fractional part is always emitted - including for a whole number of
     * seconds, where Boost omits it - so the result always matches regexLocalTimeISO()
     */

    localTime = getLocalTimeISO( utcTime2 );
    UTF_CHECK( localTime.size() == 32U );
    UTF_CHECK( bl::str::regex_match( localTime, results, regex ) );

    localTime = getLocalTimeISO( utcTime3 );
    UTF_CHECK( localTime.size() == 32U );

    localTime = getLocalTimeISO( utcTime4 );
    UTF_CHECK( localTime.size() == 32U );
    UTF_CHECK( bl::str::regex_match( localTime, results, regex ) );

    /*
     * Everything Unix timestamp shaped in the library derives from g_epoch, so its value
     * is pinned here and cross checked against the C library
     */

    UTF_REQUIRE_EQUAL( bl::time::epoch(), bl::time::ptime( bl::time::date( 1970, 1, 1 ) ) );

    const auto secondsSinceEpoch =
        static_cast< std::int64_t >( bl::time::durationSinceEpochUtcInSeconds() );

    const auto secondsFromCLibrary = static_cast< std::int64_t >( std::time( nullptr ) );

    UTF_REQUIRE( ( secondsSinceEpoch - secondsFromCLibrary ) <= 5 );
    UTF_REQUIRE( ( secondsFromCLibrary - secondsSinceEpoch ) <= 5 );

    /*
     * getLocalTimeISO( ... ) guards only is_special() before handing the value to
     * c_local_adjustor< ptime >::utc_to_local( ... ), which throws std::out_of_range for
     * ANY non-special ptime before the epoch - and not at the epoch itself. Note that
     * ptime( min_date_time ) is NOT special, it is 1400-Jan-01, so it takes the throwing
     * path rather than the empty string path
     *
     * The exact position of that cliff is worth pinning because getCurrentLocalTimeISO()
     * is called from inside BL_THROW_* ( core/ErrorHandling.h ) and from the logger
     * ( core/Logging.h ) - a shift would make exception construction and logging
     * themselves throw
     */

    UTF_REQUIRE_NO_THROW( bl::time::getLocalTimeISO( bl::time::epoch() ) );

    localTime = bl::time::getLocalTimeISO( bl::time::epoch() );

    UTF_CHECK( bl::str::regex_match( localTime, results, regex ) );
    UTF_CHECK( localTime.size() == 32U );

    UTF_REQUIRE_THROW(
        bl::time::getLocalTimeISO( bl::time::epoch() - bl::time::seconds( 1 ) ),
        std::out_of_range
        );

    UTF_REQUIRE_THROW(
        bl::time::getLocalTimeISO( bl::time::ptime( bl::time::min_date_time ) ),
        std::out_of_range
        );

    /*
     * The remaining two special values - only not_a_date_time is covered above
     */

    UTF_REQUIRE( bl::time::getLocalTimeISO( bl::time::ptime( bl::time::pos_infin ) ).empty() );
    UTF_REQUIRE( bl::time::getLocalTimeISO( bl::time::ptime( bl::time::neg_infin ) ).empty() );

    if( test::UtfArgsParser::isClient() )
    {
        utest::TestUtils::measureRuntime(
            "Generating 1 thousand local timestamps",
            [ & ]() -> void
            {
                for( std::size_t i = 0U; i < 1000U; ++i )
                {
                    getCurrentLocalTimeISO();
                }
            }
            );

        utest::TestUtils::measureRuntime(
            "Generating 10 thousand local timestamps",
            [ & ]() -> void
            {
                for( std::size_t i = 0U; i < 10000U; ++i )
                {
                    getCurrentLocalTimeISO();
                }
            }
            );

        utest::TestUtils::measureRuntime(
            "Generating 1 million local timestamps",
            [ & ]() -> void
            {
                for( std::size_t i = 0U; i < 1000000U; ++i )
                {
                    getCurrentLocalTimeISO();
                }
            }
            );
    }
}
