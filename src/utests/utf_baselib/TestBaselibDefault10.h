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
#include <utests/baselib/UtfBaseLibCommon.h>

#include <baselib/core/EnumUtils.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/OS.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <ctime>
#include <set>
#include <string>
#include <unordered_set>

/************************************************************************
 * eh::isErrorCondition tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestIsErrorConditionTests )
{
    using namespace bl;

    /*
     * isErrorCondition( ... ) matches the generic category on every platform and, on
     * POSIX only, the system category as well - the latter is what keeps an ECONNRESET
     * which asio reported under the system category classified as an expected disconnect
     * by tasks/TcpBaseTasks.h instead of an unexpected server error
     *
     * The platform is selected with the runtime predicate rather than a preprocessor
     * guard so both legs stay compiled everywhere
     */

    const auto checkCondition = [](
        SAA_in          const eh::errc::errc_t                      condition,
        SAA_in          const eh::errc::errc_t                      otherCondition
        )
        -> void
    {
        const auto generic = eh::errc::make_error_code( condition );

        UTF_REQUIRE( eh::isErrorCondition( condition, generic ) );
        UTF_REQUIRE( ! eh::isErrorCondition( otherCondition, generic ) );

        if( ! os::onWindows() )
        {
            const auto systemVariant =
                eh::error_code( static_cast< int >( condition ), eh::system_category() );

            /*
             * The two codes carry the same numeric value but different categories, so
             * this leg would also fail if the comparison ever dropped the category
             */

            UTF_REQUIRE( generic != systemVariant );

            UTF_REQUIRE( eh::isErrorCondition( condition, systemVariant ) );
            UTF_REQUIRE( ! eh::isErrorCondition( otherCondition, systemVariant ) );
        }

        /*
         * A default constructed (success) code must not match any condition
         */

        UTF_REQUIRE( ! eh::isErrorCondition( condition, eh::error_code() ) );
    };

    /*
     * The three conditions the TCP layer keys on, each tested against a condition it
     * must not match
     */

    checkCondition( eh::errc::connection_reset, eh::errc::timed_out );
    checkCondition( eh::errc::broken_pipe, eh::errc::timed_out );
    checkCondition( eh::errc::bad_file_descriptor, eh::errc::timed_out );
}

/************************************************************************
 * EnumUtils.h tests
 *
 * The test enums are prefixed with 'Utf' so they cannot collide with anything else
 * compiled into this binary
 */

namespace
{
    BL_DEFINE_ENUM_WITH_STRING_CONVERSIONS( UtfTestColor, "UtfTestColor",
        ( Red )
        ( Green )
        ( Blue )
        )

    BL_DEFINE_ENUM2_WITH_STRING_CONVERSIONS( UtfTestShape, "UtfTestShape",
        ( ( RoundRect, "round-rect" ) )
        ( ( Circle, "circle" ) )
        ( ( Square, "square" ) )
        )

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_EnumUtilsTests )
{
    /*
     * The plain macro derives the string form from the identifier; the ENUM2 macro takes
     * it from the second element of each tuple, so the two can - and for the JOSE / JWT
     * algorithm names do - diverge
     */

    UTF_CHECK_EQUAL( UtfTestColor::toString( UtfTestColor::Red ), "Red" );
    UTF_CHECK_EQUAL( UtfTestColor::toString( UtfTestColor::Green ), "Green" );
    UTF_CHECK_EQUAL( UtfTestColor::toString( UtfTestColor::Blue ), "Blue" );

    UTF_CHECK_EQUAL( UtfTestColor::toEnum( "Green" ), UtfTestColor::Green );
    UTF_CHECK_EQUAL( UtfTestColor::toEnum( "Red" ), UtfTestColor::Red );
    UTF_CHECK_EQUAL( UtfTestColor::toEnum( "Blue" ), UtfTestColor::Blue );

    UTF_CHECK_EQUAL( UtfTestShape::toString( UtfTestShape::RoundRect ), "round-rect" );
    UTF_CHECK_EQUAL( UtfTestShape::toString( UtfTestShape::Circle ), "circle" );

    UTF_CHECK_EQUAL( UtfTestShape::toEnum( "round-rect" ), UtfTestShape::RoundRect );
    UTF_CHECK_EQUAL( UtfTestShape::toEnum( "circle" ), UtfTestShape::Circle );

    /*
     * The identifier itself must not be accepted - only the string form is
     */

    UTF_REQUIRE_THROW( UtfTestShape::toEnum( "RoundRect" ), bl::UserMessageException );

    /*
     * tryToEnum( ... ) must leave the out parameter untouched when nothing matches
     */

    UtfTestColor::Enum value = UtfTestColor::Blue;

    UTF_CHECK( ! UtfTestColor::tryToEnum( "Purple", value ) );
    UTF_CHECK_EQUAL( value, UtfTestColor::Blue );

    UTF_CHECK( ! UtfTestColor::tryToEnum( "", value ) );
    UTF_CHECK( ! UtfTestColor::tryToEnum( "Gree", value ) );
    UTF_CHECK( ! UtfTestColor::tryToEnum( "green", value ) );

    UTF_CHECK_EQUAL( value, UtfTestColor::Blue );

    /*
     * A successful lookup does write it
     */

    UTF_CHECK( UtfTestColor::tryToEnum( "Red", value ) );
    UTF_CHECK_EQUAL( value, UtfTestColor::Red );

    /*
     * The generated failure messages are the diagnostics the JOSE / JWT / authorization
     * parsers surface when they are handed an untrusted document
     */

    UTF_REQUIRE_THROW_MESSAGE(
        UtfTestColor::toEnum( "Purple" ),
        bl::UserMessageException,
        "UtfTestColor has invalid string value 'Purple'"
        );

    /*
     * With three enumerators the representable range is 0..3, so casting 3 is well
     * defined - a larger value would not be
     */

    UTF_REQUIRE_THROW_MESSAGE(
        UtfTestColor::toString( static_cast< UtfTestColor::Enum >( 3 ) ),
        bl::UserMessageException,
        "UtfTestColor has invalid integral value 3"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        UtfTestShape::toEnum( "hexagon" ),
        bl::UserMessageException,
        "UtfTestShape has invalid string value 'hexagon'"
        );
}

/************************************************************************
 * StringUtils.h: characterisation of the four public helpers which have
 * no in-repository callers
 *
 * The keep-or-delete decision for str::toBool, str::unquoteString, str::setToString and
 * str::formatTime was taken as KEEP - they are public library API - and is recorded in
 * notes/plans/issues/string-utils-dead-public-helpers-keep-decision.md. This case is
 * therefore a characterisation test: it pins the current behaviour of all four, which is
 * otherwise entirely unspecified
 */

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsDeadHelpersTests )
{
    /*
     * str::toBool lower-cases the input first, so the accepted set is case insensitive,
     * and it accepts nothing but the two std::boolalpha spellings
     */

    UTF_REQUIRE( bl::str::toBool( "true" ) );
    UTF_REQUIRE( ! bl::str::toBool( "false" ) );
    UTF_REQUIRE( bl::str::toBool( "TRUE" ) );
    UTF_REQUIRE( ! bl::str::toBool( "False" ) );

    UTF_REQUIRE_THROW_MESSAGE( bl::str::toBool( "yes" ), bl::ArgumentException, "cannot be converted to boolean" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::toBool( "" ), bl::ArgumentException, "cannot be converted to boolean" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::toBool( "1" ), bl::ArgumentException, "cannot be converted to boolean" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::toBool( "tru" ), bl::ArgumentException, "cannot be converted to boolean" );

    /*
     * This is the header's only BL_THROW_USER_FRIENDLY site, and it rests on the
     * SafeInputStringStream exception mask not covering failbit - if it did, the
     * extraction would throw std::ios_base::failure before is.fail() is ever read
     */

    try
    {
        ( void ) bl::str::toBool( "maybe" );

        UTF_FAIL( BL_MSG() << "bl::str::toBool( ... ) must throw" );
    }
    catch( bl::ArgumentException& e )
    {
        UTF_REQUIRE( bl::eh::isUserFriendly( e ) );
    }

    /*
     * str::unquoteString strips exactly one leading and one trailing quote, and only
     * when both are present and the string is longer than one character
     */

    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "\"abc\"" ) ), "abc" );
    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "abc" ) ), "abc" );
    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "\"abc" ) ), "\"abc" );
    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "abc\"" ) ), "abc\"" );
    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "\"\"" ) ), "" );
    UTF_REQUIRE_EQUAL( bl::str::unquoteString( std::string( "\"" ) ), "\"" );

    /*
     * str::setToString delegates to joinFormattedImpl with the default separator, header
     * and footer for both set types
     */

    {
        std::set< int > values;
        values.insert( 3 );
        values.insert( 1 );
        values.insert( 2 );

        UTF_REQUIRE_EQUAL( bl::str::setToString< int >( values ), "{1, 2, 3}" );
    }

    {
        std::set< std::string > values;
        values.insert( "beta" );
        values.insert( "alpha" );

        UTF_REQUIRE_EQUAL( bl::str::setToString< std::string >( values ), "{alpha, beta}" );
    }

    {
        /*
         * The rendering order of an unordered_set is unspecified, so only the length and
         * the membership may be asserted - never the order
         */

        std::unordered_set< int > values;
        values.insert( 11 );
        values.insert( 22 );
        values.insert( 33 );

        const auto text = bl::str::setToString< int >( values );

        UTF_REQUIRE_EQUAL( text.size(), std::string( "{11, 22, 33}" ).size() );

        UTF_REQUIRE( bl::cpp::contains( text, std::string( "11" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "22" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "33" ) ) );

        UTF_REQUIRE_EQUAL( 0U, text.find( "{" ) );
        UTF_REQUIRE_EQUAL( text.size() - 1U, text.find( "}" ) );
    }

    /*
     * str::formatTime fills the tm with gmtime_r when useUtcTime is requested, so the
     * epoch renders identically on every machine regardless of the local time zone
     */

    UTF_REQUIRE_EQUAL( bl::str::formatTime( 0, "%Y%m%d", true /* useUtcTime */ ), "19700101" );

    /*
     * The internal buffer is 64 bytes and strftime reports the overflow by returning
     * zero, which the BL_CHK turns into an UnexpectedException
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::str::formatTime(
            0,
            "%Y-%m-%d %H:%M:%S %Y-%m-%d %H:%M:%S %Y-%m-%d %H:%M:%S %Y-%m-%d %H:%M:%S",
            true /* useUtcTime */
            ),
        bl::UnexpectedException,
        "insufficient buffer size for format string"
        );
}
