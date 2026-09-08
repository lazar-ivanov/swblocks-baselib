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

#include <baselib/core/FileEncoding.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/Random.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/Utils.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

/************************************************************************
 * str::dataRateFormatter / str::dataRateParser tests
 */

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsDataRateTests )
{
    /*
     * The five formatter branches, pinned at their unit thresholds
     *
     * Note that the '< KiloByte' branch prints the raw integer while the other four
     * print std::fixed with the requested precision (3 by default)
     */

    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 0U ), "0 B" );
    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1023U ), "1023 B" );
    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1024U ), "1.000 KB" );
    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1024U * 1024U ), "1.000 MB" );
    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1024ULL * 1024U * 1024U ), "1.000 GB" );
    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1024ULL * 1024U * 1024U * 1024U ), "1.000 TB" );

    UTF_CHECK_EQUAL( bl::str::dataRateFormatter( 1536U, 1 ), "1.5 KB" );

    /*
     * The parser happy paths - all six unit mappings plus the no-unit case, and a lower
     * case unit which is only accepted because the parser calls str::to_upper( ... )
     */

    UTF_CHECK_EQUAL( bl::str::dataRateParser( "1024" ), 1024U );
    UTF_CHECK_EQUAL( bl::str::dataRateParser( "1.5 KB" ), 1536U );
    UTF_CHECK_EQUAL( bl::str::dataRateParser( "1.5k" ), 1536U );
    UTF_CHECK_EQUAL( bl::str::dataRateParser( "2 M" ), 2U * 1024U * 1024U );
    UTF_CHECK_EQUAL( bl::str::dataRateParser( "1 T" ), 1024ULL * 1024U * 1024U * 1024U );
    UTF_CHECK_EQUAL( bl::str::dataRateParser( "512 B" ), 512U );

    /*
     * A round trip through both functions
     */

    UTF_CHECK_EQUAL( bl::str::dataRateParser( bl::str::dataRateFormatter( 1536U, 1 ) ), 1536U );

    /*
     * The three failure paths - an unknown unit, an unparsable value and a value which
     * is outside the range representable as an std::uint64_t
     *
     * Note that the range is verified before the cast, so "-1" and "1e30" both throw
     * instead of performing an undefined double to unsigned conversion
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::str::dataRateParser( "2 X" ),
        bl::ArgumentException,
        "invalid size unit 'X'"
        );

    UTF_REQUIRE_THROW_MESSAGE( bl::str::dataRateParser( "" ), bl::ArgumentException, "invalid value" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::dataRateParser( "abc" ), bl::ArgumentException, "invalid value" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::dataRateParser( "-1" ), bl::ArgumentException, "invalid value" );
    UTF_REQUIRE_THROW_MESSAGE( bl::str::dataRateParser( "1e30" ), bl::ArgumentException, "invalid value" );
}

/************************************************************************
 * BL_SCOPE_EXIT_WARN_ON_FAILURE must swallow and log, not abort
 */

UTF_AUTO_TEST_CASE( BaseLib_TestScopeExitWarnOnFailure )
{
    bl::cpp::SafeOutputStringStream os;

    const bl::Logging::line_logger_t ll(
        bl::cpp::bind(
            &bl::Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /* addNewLine */, bl::cpp::ref( os )
            )
        );

    bl::Logging::LineLoggerPusher pushLogger( ll );

    bl::Logging::LevelPusher pushLevel( bl::Logging::LL_WARNING, true /* global */ );

    bool reachedAfterScope = false;

    {
        /*
         * Note that BL_SCOPE_EXIT with exactly the same body would reach BL_RIP_MSG and
         * abort the test binary - the throwing body must stay confined to this variant
         */

        BL_SCOPE_EXIT_WARN_ON_FAILURE(
            {
                BL_THROW( bl::UnexpectedException(), "cleanup-failure-marker" );
            },
            "BaseLib_TestScopeExitWarnOnFailure"
            );
    }

    /*
     * Reaching this statement is itself the observation: had the macro been defined in
     * terms of BL_NOEXCEPT_* the process would already have been aborted, and had the
     * exception been left to escape it would have propagated out of the test case
     */

    reachedAfterScope = true;

    UTF_REQUIRE( reachedAfterScope );

    const auto text = os.str();

    UTF_REQUIRE( bl::cpp::contains( text, std::string( "BaseLib_TestScopeExitWarnOnFailure" ) ) );
    UTF_REQUIRE( bl::cpp::contains( text, std::string( "NOEXCEPT block threw an exception" ) ) );
    UTF_REQUIRE( bl::cpp::contains( text, std::string( "cleanup-failure-marker" ) ) );
}

/************************************************************************
 * utils::ExecutionTimer move / cancel / threshold logging
 */

namespace
{
    std::size_t countOccurrences(
        SAA_in      const std::string&                      text,
        SAA_in      const std::string&                      value
        )
    {
        std::size_t count = 0U;

        for(
            auto pos = text.find( value );
            pos != std::string::npos;
            pos = text.find( value, pos + value.size() )
            )
        {
            ++count;
        }

        return count;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_ExecutionTimerTests )
{
    bl::cpp::SafeOutputStringStream os;

    const bl::Logging::line_logger_t ll(
        bl::cpp::bind(
            &bl::Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /* addNewLine */, bl::cpp::ref( os )
            )
        );

    bl::Logging::LineLoggerPusher pushLogger( ll );

    /*
     * The default channel is Logging::notify(), which is at LL_NOTIFY (level 1) and is
     * therefore always emitted - no LevelPusher is needed here
     *
     * Every scope is inspected on its own suffix of the capture and every scope also
     * asserts the total number of completion lines it emitted; counting only the timer's
     * own name is not enough, because a moved from timer has an *empty* name and would
     * emit a spurious "Command completed in ..." line which a name based count cannot see
     */

    std::size_t consumed = 0U;

    const auto cbNewText = [ & ]() -> std::string
    {
        const auto all = os.str();

        const auto text = all.substr( consumed );

        consumed = all.size();

        return text;
    };

    {
        bl::utils::ExecutionTimer t( std::string( "utf-timer-basic" ) );
    }

    {
        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 1U );
        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-basic" ), 1U );
        UTF_CHECK( bl::cpp::contains( text, std::string( "utf-timer-basic completed in" ) ) );
    }

    {
        bl::utils::ExecutionTimer t1( std::string( "utf-timer-mc" ) );
        bl::utils::ExecutionTimer t2( std::move( t1 ) );
    }

    {
        /*
         * The move constructor dismisses the moved from timer, so the completion is
         * logged exactly once
         */

        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-mc" ), 1U );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 1U );
    }

    {
        bl::utils::ExecutionTimer a( std::string( "utf-timer-A" ) );
        bl::utils::ExecutionTimer b( std::string( "utf-timer-B" ) );

        b = std::move( a );
    }

    {
        /*
         * Move assignment moves the name too - 'b' is destroyed first and logs
         * "utf-timer-A", while the dismissed 'a' logs nothing at all
         */

        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-A" ), 1U );
        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-B" ), 0U );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 1U );
    }

    {
        bl::utils::ExecutionTimer a( std::string( "utf-timer-C" ) );

        a.cancel();

        bl::utils::ExecutionTimer b( std::string( "utf-timer-D" ) );

        b = std::move( a );
    }

    {
        /*
         * A canceled source propagates its canceled state into the target
         */

        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-C" ), 0U );
        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-D" ), 0U );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 0U );
    }

    {
        bl::utils::ExecutionTimer t( std::string( "utf-timer-cancel" ) );

        t.cancel();
    }

    {
        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-cancel" ), 0U );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 0U );
    }

    {
        bl::utils::ExecutionTimer t(
            std::string( "utf-timer-threshold" ),
            bl::Logging::notify(),
            3600L /* durationThresholdInSeconds */
            );
    }

    {
        /*
         * The elapsed time cannot exceed an hour, so the threshold suppresses the line
         */

        const auto text = cbNewText();

        UTF_CHECK_EQUAL( countOccurrences( text, "utf-timer-threshold" ), 0U );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 0U );
    }

    {
        bl::utils::ExecutionTimer t;
    }

    {
        /*
         * The empty name fallback
         */

        const auto text = cbNewText();

        UTF_CHECK( bl::cpp::contains( text, std::string( "Command completed in" ) ) );
        UTF_CHECK_EQUAL( countOccurrences( text, "completed in" ), 1U );
    }
}

/************************************************************************
 * Generated UUIDs must remain RFC-4122 v4
 */

UTF_AUTO_TEST_CASE( BaseLib_TestUuidGeneratorContract )
{
    for( std::size_t i = 0U; i < 100U; ++i )
    {
        const auto u = bl::uuids::create();

        UTF_REQUIRE_EQUAL( u.version(), bl::uuid_t::version_random_number_based );
        UTF_REQUIRE_EQUAL( u.variant(), bl::uuid_t::variant_rfc_4122 );
        UTF_REQUIRE( ! u.is_nil() );

        /*
         * The textual form is lower case, so the version nibble is at offset 14 and the
         * variant nibble at offset 19 of 'xxxxxxxx-xxxx-Vxxx-Nxxx-xxxxxxxxxxxx'
         */

        const auto text = bl::uuids::uuid2string( u );

        UTF_REQUIRE_EQUAL( text.at( 14 ), '4' );
        UTF_REQUIRE( std::string( "89ab" ).find( text.at( 19 ) ) != std::string::npos );
    }

    UTF_REQUIRE( bl::uuids::nil().is_nil() );

    UTF_REQUIRE_EQUAL(
        bl::uuids::uuid2string( bl::uuids::nil() ),
        "00000000-0000-0000-0000-000000000000"
        );

    UTF_REQUIRE( bl::uuids::isUuid( bl::uuids::uuid2string( bl::uuids::nil() ) ) );
}

/************************************************************************
 * random::seed direct coverage
 */

UTF_AUTO_TEST_CASE( BaseLib_RandomSeedTests )
{
    bl::random::mt19937 defaultSeeded;
    bl::random::mt19937 a;
    bl::random::mt19937 b;

    bl::random::seed( a );
    bl::random::seed( b );

    /*
     * A seed( ... ) which became a no-op, or which seeded from a constant, would make
     * these equal - the header records that exact defect having shipped on one platform,
     * where it produced duplicated UUIDs
     *
     * Both hold with probability 1 - 2^-32
     */

    UTF_REQUIRE( a() != defaultSeeded() );
    UTF_REQUIRE( a() != b() );
}

/************************************************************************
 * fs::copyDirectoryWithContents tests
 */

namespace
{
    std::size_t countDirectoryEntries( SAA_in const bl::fs::path& path )
    {
        std::size_t count = 0U;

        for( bl::fs::directory_iterator i( path ), end; i != end; ++i )
        {
            ++count;
        }

        return count;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( FsUtils_CopyDirectoryWithContentsTests )
{
    using namespace bl::encoding;

    bl::fs::TmpDir tmpDir;

    const auto& tmpPath = tmpDir.path();

    const auto source = tmpPath / "source";

    bl::fs::safeMkdirs( source / "sub" / "deeper" );
    bl::fs::safeMkdirs( source / "empty" );

    /*
     * Utf8_NoPreamble makes the on-disk size equal the content length exactly
     */

    writeTextFile( source / "a.txt", "aaa", TextFileEncoding::Utf8_NoPreamble );
    writeTextFile( source / "sub" / "b.txt", "bbb", TextFileEncoding::Utf8_NoPreamble );
    writeTextFile( source / "sub" / "deeper" / "c.txt", "ccc", TextFileEncoding::Utf8_NoPreamble );

    const auto target = tmpPath / "target";

    bl::fs::copyDirectoryWithContents( source, target );

    UTF_REQUIRE( bl::fs::is_directory( target ) );
    UTF_REQUIRE( bl::fs::is_directory( target / "empty" ) );
    UTF_REQUIRE( bl::fs::is_directory( target / "sub" / "deeper" ) );

    UTF_REQUIRE_EQUAL( readTextFile( target / "a.txt" ), std::string( "aaa" ) );
    UTF_REQUIRE_EQUAL( readTextFile( target / "sub" / "b.txt" ), std::string( "bbb" ) );
    UTF_REQUIRE_EQUAL( readTextFile( target / "sub" / "deeper" / "c.txt" ), std::string( "ccc" ) );

    UTF_REQUIRE_EQUAL( bl::fs::file_size( target / "a.txt" ), 3U );

    /*
     * Entry count parity per directory, so a lost entry fails loudly
     */

    UTF_REQUIRE_EQUAL( countDirectoryEntries( source ), 3U );
    UTF_REQUIRE_EQUAL( countDirectoryEntries( target ), countDirectoryEntries( source ) );

    UTF_REQUIRE_EQUAL( countDirectoryEntries( source / "sub" ), 2U );
    UTF_REQUIRE_EQUAL( countDirectoryEntries( target / "sub" ), countDirectoryEntries( source / "sub" ) );

    {
        /*
         * An existing target is rejected by the ensurePathDoesNotExist( ... ) precondition,
         * which runs first and always wins; the 'created' rollback guard - whose message is
         * "Directory <p> already exists" rather than this one's "Path <p> already exists" -
         * is reachable only in a TOCTOU race and is not unit testable
         *
         * The rejected call must not delete a directory it did not create
         */

        const auto occupied = tmpPath / "occupied";

        bl::fs::safeMkdirs( occupied );

        writeTextFile( occupied / "sentinel.txt", "keep", TextFileEncoding::Utf8_NoPreamble );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::copyDirectoryWithContents( source, occupied ),
            bl::UnexpectedException,
            "already exists"
            );

        UTF_REQUIRE( bl::fs::path_exists( occupied / "sentinel.txt" ) );
    }

    {
        /*
         * A missing source is rejected before the target directory is created
         */

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::copyDirectoryWithContents( tmpPath / "no-such-source", tmpPath / "t2" ),
            bl::UnexpectedException,
            "does not exist"
            );

        UTF_REQUIRE( ! bl::fs::path_exists( tmpPath / "t2" ) );
    }

    if( bl::os::onUNIX() )
    {
        /*
         * Anything which is neither a regular file nor a directory - here a dangling
         * symbolic link - is dropped silently and must not abort the whole copy
         */

        const auto source2 = tmpPath / "source2";

        bl::fs::safeMkdirs( source2 );

        writeTextFile( source2 / "regular.txt", "reg", TextFileEncoding::Utf8_NoPreamble );

        bl::fs::create_symlink( source2 / "no-such-target", source2 / "dangling" );

        const auto target2 = tmpPath / "target2";

        UTF_REQUIRE_NO_THROW( bl::fs::copyDirectoryWithContents( source2, target2 ) );

        UTF_REQUIRE_EQUAL( readTextFile( target2 / "regular.txt" ), std::string( "reg" ) );

        UTF_REQUIRE( ! bl::fs::path_exists( target2 / "dangling" ) );
    }
}

#if ! defined( _WIN32 )

/************************************************************************
 * os::writeToStdErrNothrow byte-exact output
 */

UTF_AUTO_TEST_CASE( BaseLib_WriteToStdErrNothrowTests )
{
    bl::fs::TmpDir tmpDir;

    const auto path = tmpDir.path() / "stderr-capture.txt";

    const auto longMessage = std::string( 8192, 'x' ) + '\n';

    const int savedStdErr = ::dup( 2 );
    UTF_REQUIRE( -1 != savedStdErr );

    {
        BL_SCOPE_EXIT(
            {
                ::dup2( savedStdErr, 2 );
                ::close( savedStdErr );
            }
            );

        const int fd = ::open( path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
        UTF_REQUIRE( -1 != fd );
        UTF_REQUIRE( -1 != ::dup2( fd, 2 ) );
        UTF_REQUIRE( 0 == ::close( fd ) );

        bl::os::writeToStdErrNothrow( nullptr );
        bl::os::writeToStdErrNothrow( "" );
        bl::os::writeToStdErrNothrow( "RIP: line one\n" );
        bl::os::writeToStdErrNothrow( longMessage.c_str() );
    }

    /*
     * The capture must be read in binary and without any line based normalization -
     * the point of the assertions below is that the bytes are exactly the messages,
     * with no terminating NUL and no off-by-one in the hand rolled length loop
     */

    std::ifstream is( path.string().c_str(), std::ios::binary );

    const std::string text( ( std::istreambuf_iterator< char >( is ) ), std::istreambuf_iterator< char >() );

    UTF_REQUIRE_EQUAL( text, std::string( "RIP: line one\n" ) + longMessage );

    UTF_REQUIRE_EQUAL( text.size(), std::string( "RIP: line one\n" ).size() + longMessage.size() );
}

#endif // ! defined( _WIN32 )
