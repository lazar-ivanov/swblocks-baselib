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

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/Utils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <string>

namespace
{
    /**
     * @brief An error category which is neither system_category() nor generic_category()
     *
     * SystemException::create must take its third branch for it and attach neither
     * errinfo_system_code nor errinfo_errno
     *
     * It has to live at namespace scope - eh::error_code only stores a pointer to the
     * category and the exceptions created from it outlive the test case
     */

    class UtestErrorCategory : public bl::eh::error_category
    {
    public:

        virtual const char* name() const NOEXCEPT OVERRIDE
        {
            return "utest-category";
        }

        virtual std::string message( int ) const OVERRIDE
        {
            return std::string();
        }
    };

    static const UtestErrorCategory g_utestErrorCategory;

} // __unnamed

/************************************************************************
 * SystemException::create category branch tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestSystemExceptionCreateTests )
{
    /*
     * ServerErrorHelpers maps errinfo_system_code onto the wire field 'systemCode' and
     * errinfo_errno onto 'errNo', so which of the three branches below is taken decides
     * what a remote peer receives; Windows OS errors always arrive as system_category
     */

    {
        const bl::eh::error_code code( 5, bl::eh::system_category() );

        const auto e = bl::SystemException::create( code, "sys-msg" );

        const int* systemCode = bl::eh::get_error_info< bl::eh::errinfo_system_code >( e );

        UTF_REQUIRE( nullptr != systemCode );
        UTF_REQUIRE_EQUAL( 5, *systemCode );

        UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_errno >( e ) );
        UTF_REQUIRE( nullptr == e.errNo() );

        const std::string* categoryName =
            bl::eh::get_error_info< bl::eh::errinfo_category_name >( e );

        UTF_REQUIRE( nullptr != categoryName );
        UTF_REQUIRE_EQUAL( std::string( "system" ), *categoryName );

        UTF_REQUIRE( nullptr != e.errorCode() );
        UTF_REQUIRE( code == *e.errorCode() );

        UTF_REQUIRE_EQUAL( std::string( "bl::SystemException" ), std::string( e.fullTypeName() ) );

        const std::string* codeMessage =
            bl::eh::get_error_info< bl::eh::errinfo_error_code_message >( e );

        UTF_REQUIRE( nullptr != codeMessage );
        UTF_REQUIRE( ! codeMessage -> empty() );

        /*
         * Substring, because the ' [category:value]' suffix is Boost version dependent
         */

        UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "sys-msg" ) ) );
    }

    {
        const bl::eh::error_code code( EACCES, bl::eh::generic_category() );

        const auto e = bl::SystemException::create( code, "gen-msg" );

        UTF_REQUIRE( nullptr != e.errNo() );
        UTF_REQUIRE_EQUAL( EACCES, *e.errNo() );

        UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_system_code >( e ) );

        const std::string* categoryName =
            bl::eh::get_error_info< bl::eh::errinfo_category_name >( e );

        UTF_REQUIRE( nullptr != categoryName );
        UTF_REQUIRE_EQUAL( std::string( "generic" ), *categoryName );

        const std::string* codeMessage =
            bl::eh::get_error_info< bl::eh::errinfo_error_code_message >( e );

        UTF_REQUIRE( nullptr != codeMessage );
        UTF_REQUIRE_EQUAL( std::string( "Permission denied" ), *codeMessage );

        UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "gen-msg" ) ) );
    }

    {
        const bl::eh::error_code code( 42, g_utestErrorCategory );

        const auto e = bl::SystemException::create( code, "custom-msg" );

        /*
         * Neither of the two code properties may be attached for an unknown category
         */

        UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_system_code >( e ) );
        UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_errno >( e ) );
        UTF_REQUIRE( nullptr == e.errNo() );

        const std::string* categoryName =
            bl::eh::get_error_info< bl::eh::errinfo_category_name >( e );

        UTF_REQUIRE( nullptr != categoryName );
        UTF_REQUIRE_EQUAL( std::string( "utest-category" ), *categoryName );

        /*
         * The category message is empty, so it must not be attached at all - an empty
         * errorCodeMessage would otherwise travel onto the wire
         */

        UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_error_code_message >( e ) );

        UTF_REQUIRE( nullptr != e.errorCode() );
        UTF_REQUIRE_EQUAL( 42, e.errorCode() -> value() );

        UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "custom-msg" ) ) );
    }
}

/************************************************************************
 * BL_CHK_ERRNO errno reset contract
 */

UTF_AUTO_TEST_CASE( BaseLib_TestChkErrnoMacro )
{
    bl::fs::TmpDir tmpDir;

    /*
     * BL_CHK_ERRNO resets errno to zero before it evaluates the expression, so a stale
     * errno left behind by an unrelated earlier call can never be reported as the cause
     * of this failure - the code below must be the ENOENT of the failed fopen and not
     * the EACCES which was planted before it
     */

    {
        std::FILE* file = nullptr;

        errno = EACCES;

        UTF_REQUIRE_EXCEPTION(
            BL_CHK_ERRNO(
                false,
                (
                    nullptr != ( file = std::fopen(
                        ( tmpDir.path() / "no-such-dir" / "no-such-file" ).string().c_str(),
                        "r"
                        ) )
                ),
                "cannot open the test file"
                ),
            bl::SystemException,
            []( SAA_in const bl::SystemException& e ) -> bool
            {
                UTF_CHECK( nullptr != e.errNo() );

                if( nullptr != e.errNo() )
                {
                    UTF_CHECK_EQUAL( ENOENT, *e.errNo() );
                }

                UTF_CHECK( nullptr != e.errorCode() );

                if( nullptr != e.errorCode() )
                {
                    UTF_CHECK( e.errorCode() -> category() == bl::eh::generic_category() );
                }

                UTF_CHECK( bl::cpp::contains( std::string( e.what() ), std::string( "cannot open the test file" ) ) );
                UTF_CHECK_EQUAL( std::string( "bl::SystemException" ), std::string( e.fullTypeName() ) );

                return true;
            }
            );

        UTF_REQUIRE( nullptr == file );
    }

    /*
     * A check which does not fail must not throw and must leave errno at zero
     */

    {
        errno = EACCES;

        BL_CHK_ERRNO( false, ( true ), "must not throw" );

        UTF_REQUIRE_EQUAL( 0, errno );
    }
}

/************************************************************************
 * The 'user friendly' throw / check macro family
 */

UTF_AUTO_TEST_CASE( BaseLib_TestUserFriendlyExceptionMacros )
{
    /*
     * The user friendly flag decides what a human is ever shown - CmdLineAppBase and
     * ServerErrorHelpers both degrade the text to BL_GENERIC_FRIENDLY_UNEXPECTED_MSG
     * when eh::isUserFriendly( e ) is false, so losing the flag is a silent regression
     * at every one of the call sites
     *
     * The flag is attached by two different macro shapes - BL_CHK_T_USER_FRIENDLY
     * decorates the exception before BL_EXCEPTION wraps it, while BL_THROW_USER_FRIENDLY
     * decorates the already wrapped and decorated result - so both are covered below
     */

    const auto cbRequireUserFriendly = []( SAA_in const std::exception& e ) -> void
    {
        UTF_REQUIRE( bl::eh::isUserFriendly( e ) );

        const bool* isUserFriendly =
            bl::eh::get_error_info< bl::eh::errinfo_is_user_friendly >( e );

        UTF_REQUIRE( nullptr != isUserFriendly );
        UTF_REQUIRE( *isUserFriendly );
    };

    std::exception_ptr friendlyThrowPtr;

    /*
     * (1) BL_THROW_USER_FRIENDLY - the flag is applied after the decoration
     */

    try
    {
        BL_THROW_USER_FRIENDLY( bl::UnexpectedException(), "friendly-throw" );

        UTF_FAIL( BL_MSG() << "BL_THROW_USER_FRIENDLY must throw" );
    }
    catch( bl::UnexpectedException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "friendly-throw" ), std::string( e.what() ) );

        friendlyThrowPtr = std::current_exception();
    }

    /*
     * (2) The discriminating negative - a plain BL_THROW must not carry the flag
     */

    try
    {
        BL_THROW( bl::UnexpectedException(), "plain-throw" );

        UTF_FAIL( BL_MSG() << "BL_THROW must throw" );
    }
    catch( bl::UnexpectedException& e )
    {
        UTF_REQUIRE( ! bl::eh::isUserFriendly( e ) );
    }

    /*
     * (3) BL_CHK_USER_FRIENDLY
     */

    try
    {
        BL_CHK_USER_FRIENDLY( false, getFalse(), "friendly-chk" );

        UTF_FAIL( BL_MSG() << "BL_CHK_USER_FRIENDLY must throw" );
    }
    catch( bl::UnexpectedException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "friendly-chk" ), std::string( e.what() ) );
    }

    /*
     * (4) BL_CHK_T_USER_FRIENDLY - the flag is applied before the decoration and the
     * requested exception type must survive it
     */

    try
    {
        BL_CHK_T_USER_FRIENDLY( false, getFalse(), bl::ArgumentException(), "friendly-chk-t" );

        UTF_FAIL( BL_MSG() << "BL_CHK_T_USER_FRIENDLY must throw" );
    }
    catch( bl::ArgumentException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "friendly-chk-t" ), std::string( e.what() ) );
        UTF_CHECK_EQUAL( std::string( "bl::ArgumentException" ), std::string( e.fullTypeName() ) );
    }

    /*
     * (5) BL_CHK_USER - UserMessageException carries the flag from its own constructor
     */

    try
    {
        BL_CHK_USER( false, getFalse(), "user-chk" );

        UTF_FAIL( BL_MSG() << "BL_CHK_USER must throw" );
    }
    catch( bl::UserMessageException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "user-chk" ), std::string( e.what() ) );
    }

    /*
     * (6) BL_THROW_EC_USER_FRIENDLY
     *
     * The message is matched as a substring and not by equality, because system_error
     * what() gains a ' [generic:13]' suffix on newer Boost
     */

    try
    {
        BL_THROW_EC_USER_FRIENDLY(
            bl::eh::error_code( EACCES, bl::eh::generic_category() ),
            "friendly-ec-throw"
            );

        UTF_FAIL( BL_MSG() << "BL_THROW_EC_USER_FRIENDLY must throw" );
    }
    catch( bl::SystemException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "bl::SystemException" ), std::string( e.fullTypeName() ) );

        UTF_REQUIRE( nullptr != e.errNo() && EACCES == *e.errNo() );
        UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "friendly-ec-throw" ) ) );
    }

    /*
     * (7) BL_CHK_EC_USER_FRIENDLY with a failing error code
     */

    try
    {
        BL_CHK_EC_USER_FRIENDLY(
            bl::eh::error_code( EACCES, bl::eh::generic_category() ),
            "friendly-ec-chk"
            );

        UTF_FAIL( BL_MSG() << "BL_CHK_EC_USER_FRIENDLY must throw" );
    }
    catch( bl::SystemException& e )
    {
        cbRequireUserFriendly( e );

        UTF_CHECK_EQUAL( std::string( "bl::SystemException" ), std::string( e.fullTypeName() ) );

        UTF_REQUIRE( nullptr != e.errNo() && EACCES == *e.errNo() );
        UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "friendly-ec-chk" ) ) );
    }

    /*
     * (8) BL_CHK_EC_USER_FRIENDLY with a success code must not throw
     */

    UTF_REQUIRE_NO_THROW( BL_CHK_EC_USER_FRIENDLY( bl::eh::error_code(), "must-not-throw" ) );

    /*
     * (9) BL_CHK_ERRNO_USER_FRIENDLY - the errno variant of the family
     */

    {
        bl::fs::TmpDir tmpDir;

        std::FILE* file = nullptr;

        try
        {
            BL_CHK_ERRNO_USER_FRIENDLY(
                false,
                (
                    nullptr != ( file = std::fopen(
                        ( tmpDir.path() / "no-such-dir" / "no-such-file" ).string().c_str(),
                        "r"
                        ) )
                ),
                "friendly-errno"
                );

            UTF_FAIL( BL_MSG() << "BL_CHK_ERRNO_USER_FRIENDLY must throw" );
        }
        catch( bl::SystemException& e )
        {
            cbRequireUserFriendly( e );

            UTF_CHECK_EQUAL( std::string( "bl::SystemException" ), std::string( e.fullTypeName() ) );

            UTF_REQUIRE( nullptr != e.errNo() && ENOENT == *e.errNo() );
            UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), std::string( "friendly-errno" ) ) );
        }

        UTF_REQUIRE( nullptr == file );
    }

    /*
     * (10) The flag must survive capture into an std::exception_ptr and a re-throw -
     * this is exactly the path ServerErrorHelpers::createServerErrorObject takes
     */

    {
        UTF_REQUIRE( friendlyThrowPtr );

        bool rethrown = false;

        try
        {
            bl::cpp::safeRethrowException( friendlyThrowPtr );
        }
        catch( std::exception& e2 )
        {
            rethrown = true;

            UTF_REQUIRE( bl::eh::isUserFriendly( e2 ) );
        }

        UTF_REQUIRE( rethrown );
    }
}

/************************************************************************
 * eh::diagnostic_information nested exception chain walk
 */

UTF_AUTO_TEST_CASE( BaseLib_TestNestedExceptionDiagnostics )
{
    /*
     * A three level chain with distinct markers - the walk must visit every level exactly
     * once; truncating it at depth one would silently drop every nested cause from every
     * server error dump
     */

    std::exception_ptr innermost;

    try
    {
        BL_THROW( bl::UnexpectedException(), "marker-innermost" );
    }
    catch( ... )
    {
        innermost = std::current_exception();
    }

    UTF_REQUIRE( innermost );

    std::exception_ptr middle;

    try
    {
        BL_THROW(
            bl::ArgumentException()
                << bl::eh::errinfo_nested_exception_ptr( innermost ),
            "marker-middle"
            );
    }
    catch( ... )
    {
        middle = std::current_exception();
    }

    UTF_REQUIRE( middle );

    bool caught = false;

    try
    {
        BL_THROW(
            bl::HttpException()
                << bl::eh::errinfo_nested_exception_ptr( middle ),
            "marker-outer"
            );
    }
    catch( bl::HttpException& e )
    {
        caught = true;

        const auto text = bl::eh::diagnostic_information( e );
        const auto text2 = bl::eh::diagnostic_information( std::current_exception() );
        const auto details = e.details();

        UTF_REQUIRE( bl::cpp::contains( text, std::string( "marker-outer" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "marker-middle" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "marker-innermost" ) ) );

        /*
         * The discriminating assertion - there must be exactly one 'Nested exception:'
         * marker per level below the outer one; it fails if the walk stops after the
         * first level and it fails if a level is emitted twice
         */

        const std::string marker( "Nested exception:" );

        std::size_t nestedCount = 0U;
        std::size_t pos = text.find( marker );

        while( pos != std::string::npos )
        {
            ++nestedCount;

            pos = text.find( marker, pos + marker.size() );
        }

        UTF_REQUIRE_EQUAL( 2U, nestedCount );

        /*
         * The std::exception_ptr overload must walk the very same chain
         */

        UTF_REQUIRE( bl::cpp::contains( text2, std::string( "marker-outer" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text2, std::string( "marker-middle" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text2, std::string( "marker-innermost" ) ) );

        /*
         * BaseException::details() goes through the same chain walker
         */

        UTF_REQUIRE( bl::cpp::contains( details, std::string( "marker-innermost" ) ) );
    }

    UTF_REQUIRE( caught );
}

/************************************************************************
 * utils::tryCatchLog log flag / filter / re-throw matrix
 */

UTF_AUTO_TEST_CASE( BaseLib_TryCatchLogTests )
{
    bl::cpp::SafeOutputStringStream os;

    const bl::Logging::line_logger_t ll(
        bl::cpp::bind(
            &bl::Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /* addNewLine */, bl::cpp::ref( os )
            )
        );

    bl::Logging::LineLoggerPusher pushLogger( ll );

    bl::Logging::LevelPusher pushLevel( bl::Logging::LL_WARNING );

    const auto cbReturns42 = []() -> int
    {
        return 42;
    };

    const auto cbIntThrows = []() -> int
    {
        BL_THROW( bl::UnexpectedException(), "boom" );
    };

    const auto cbVoidThrows = []() -> void
    {
        BL_THROW( bl::UnexpectedException(), "boom" );
    };

    const auto cbOnErrorMinusOne = []() -> int
    {
        return -1;
    };

    /*
     * (1) The success path returns the block's value and logs nothing
     *
     * cbOnError is supplied but must never be invoked here - a non-void call is required
     * to pass one (the BL_ASSERT at the top of tryCatchLogImpl), and passing it also
     * strengthens the check: a regression which routed the success path through the error
     * callback would return -1 rather than 42
     */

    UTF_REQUIRE_EQUAL( 42, ( bl::utils::tryCatchLog< int >( "ctx", cbReturns42, cbOnErrorMinusOne ) ) );
    UTF_REQUIRE( os.str().empty() );

    /*
     * (2) The cbOnError path - the value comes from the error callback and the failure
     * is logged as a warning carrying both the context and the exception text
     */

    UTF_REQUIRE_EQUAL( -1, ( bl::utils::tryCatchLog< int >( "ctx", cbIntThrows, cbOnErrorMinusOne ) ) );

    {
        const auto text = os.str();

        UTF_REQUIRE( bl::cpp::contains( text, std::string( "WARNING: " ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "ctx" ) ) );
        UTF_REQUIRE( bl::cpp::contains( text, std::string( "boom" ) ) );
    }

    /*
     * (3) LogFlags::NONE must append nothing at all - the exception text can carry
     * secrets and it must not reach the log when the caller asked for silence
     */

    {
        const auto before = os.str();

        UTF_REQUIRE_EQUAL(
            -1,
            (
                bl::utils::tryCatchLog< int >(
                    "ctx",
                    cbIntThrows,
                    cbOnErrorMinusOne,
                    bl::utils::LogFlags::NONE
                    )
            )
            );

        UTF_REQUIRE_EQUAL( before, os.str() );
    }

    /*
     * (4) LogFlags::DEBUG_ONLY logs the context on a DEBUG line and never a WARNING one
     */

    {
        bl::Logging::LevelPusher pushDebugLevel( bl::Logging::LL_DEBUG );

        const auto before = os.str();

        UTF_REQUIRE_EQUAL(
            -1,
            (
                bl::utils::tryCatchLog< int >(
                    "ctx",
                    cbIntThrows,
                    cbOnErrorMinusOne,
                    bl::utils::LogFlags::DEBUG_ONLY
                    )
            )
            );

        const auto appended = os.str().substr( before.size() );

        /*
         * The context is the first line of the debug message; the line is matched
         * piecewise because the logger inserts a timestamp between the prefix and the
         * text whenever the level is verbose - which is exactly what this block pushes
         */

        const auto firstLineEnd = appended.find( '\n' );

        UTF_REQUIRE( std::string::npos != firstLineEnd );

        const auto firstLine = appended.substr( 0U, firstLineEnd );

        UTF_REQUIRE_EQUAL( 0U, firstLine.find( "DEBUG: " ) );
        UTF_REQUIRE( bl::cpp::contains( firstLine, std::string( "ctx" ) ) );

        UTF_REQUIRE( ! bl::cpp::contains( appended, std::string( "WARNING: " ) ) );
    }

    /*
     * (5) A void call with no cbOnError swallows the exception by design
     */

    UTF_REQUIRE_NO_THROW( bl::utils::tryCatchLog( "ctx", cbVoidThrows ) );

    /*
     * (6) An exception which does not match the EXCEPTION filter passes straight through
     * and is not logged
     */

    {
        const auto before = os.str();

        UTF_REQUIRE_THROW(
            ( bl::utils::tryCatchLog< void, bl::SystemException >( "ctx", cbVoidThrows ) ),
            bl::UnexpectedException
            );

        UTF_REQUIRE_EQUAL( before, os.str() );
    }

    /*
     * (7) A non-void call with no cbOnError must re-throw the original exception rather
     * than replace it with the unrelated 'Shouldn't be here, RETURN type is non-void'
     *
     * Release only - the BL_ASSERT at the top of tryCatchLogImpl is live in debug and
     * would abort the run on this path
     */

    #if defined( NDEBUG )

    UTF_REQUIRE_THROW_MESSAGE(
        ( bl::utils::tryCatchLog< int >( "ctx", cbIntThrows ) ),
        bl::UnexpectedException,
        "boom"
        );

    {
        bool caught = false;

        try
        {
            ( void ) bl::utils::tryCatchLog< int >( "ctx", cbIntThrows );
        }
        catch( bl::UnexpectedException& e )
        {
            caught = true;

            UTF_REQUIRE_EQUAL( std::string( "boom" ), std::string( e.what() ) );
            UTF_REQUIRE( ! bl::cpp::contains( std::string( e.what() ), std::string( "Shouldn't be here" ) ) );
        }

        UTF_REQUIRE( caught );
    }

    #endif // defined( NDEBUG )
}

/************************************************************************
 * tryStdioText / stdioText lock contract
 */

UTF_AUTO_TEST_CASE( BaseLib_StdioTextLockTests )
{
    /*
     * (1) Uncontended - the callback runs and the call reports success
     */

    {
        bool invoked = false;

        UTF_REQUIRE( bl::tryStdioText( [ & ]() -> void { invoked = true; } ) );
        UTF_REQUIRE( invoked );
    }

    /*
     * (2) Same thread re-entrancy - BL_RIP_MSG calls tryStdioText and it is reachable
     * from inside a BL_STDIO_TEXT block, so swapping the recursive mutex for a plain one
     * would turn the abort path into a deadlock
     */

    {
        bool nestedResult = false;
        bool nestedInvoked = false;

        bl::stdioText(
            [ & ]() -> void
            {
                nestedResult = bl::tryStdioText( [ & ]() -> void { nestedInvoked = true; } );
            }
            );

        UTF_REQUIRE( nestedResult );
        UTF_REQUIRE( nestedInvoked );
    }

    /*
     * (3) Cross thread contention - the call must report failure and must not run the
     * callback; running it on the failure path is what BL_RIP_MSG relies on not happening
     *
     * Nothing between the handshake and the join may assert or log: every log line goes
     * through the very lock the holder thread is holding, so a failing UTF_* macro there
     * would deadlock the test instead of failing it - the outcome is recorded and
     * asserted after the join instead
     */

    {
        std::atomic< bool > holding( false );
        std::atomic< bool > release( false );
        std::atomic< bool > otherInvoked( false );

        bl::os::thread holder(
            [ & ]() -> void
            {
                bl::stdioText(
                    [ & ]() -> void
                    {
                        holding = true;

                        while( ! release )
                        {
                            bl::os::sleep( bl::time::milliseconds( 5 ) );
                        }
                    }
                    );
            }
            );

        while( ! holding )
        {
            bl::os::sleep( bl::time::milliseconds( 5 ) );
        }

        const bool contendedResult =
            bl::tryStdioText( [ & ]() -> void { otherInvoked = true; } );

        release = true;

        holder.join();

        UTF_REQUIRE( ! contendedResult );
        UTF_REQUIRE( ! otherInvoked.load() );
    }

    /*
     * (4) The lock was released once the holder thread finished
     */

    UTF_REQUIRE( bl::tryStdioText( []() -> void {} ) );
}
