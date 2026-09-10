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

#include <baselib/cmdline/CmdLineAppBase.h>
#include <baselib/cmdline/CmdLineBase.h>
#include <baselib/cmdline/ExitCodes.h>
#include <baselib/cmdline/Result.h>

#include <baselib/core/OS.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfDirectoryFixture.h>

namespace
{
    /**
     * @brief Lowers the global logging level below errors and warnings for the scope
     *
     * Every exit code ladder under test logs the failure it handles at error level by
     * design, and the test binaries route every error and warning line to BOOST_ERROR
     * (UtfMain.h:121), so a case which provokes one on purpose would otherwise fail on its
     * own fixture. LL_NOTIFY is the highest level which suppresses exactly those two
     * channels
     */

    class CmdLineAppExpectedErrorsScope
    {
        BL_NO_COPY_OR_MOVE( CmdLineAppExpectedErrorsScope )

    private:

        const int                                                       m_saved;

    public:

        CmdLineAppExpectedErrorsScope()
            :
            m_saved( bl::Logging::setLevel( bl::Logging::LL_NOTIFY, true /* global */ ) )
        {
        }

        ~CmdLineAppExpectedErrorsScope() NOEXCEPT
        {
            BL_NOEXCEPT_BEGIN()

            bl::Logging::setLevel( m_saved, true /* global */ );

            BL_NOEXCEPT_END()
        }
    };

    /**
     * @brief A CmdLineAppBase probe which skips all of the global initialisation
     *
     * The probe must set m_isNoDefaultInit (no AppInitDone, no thread pool) and must leave
     * m_isServer false - otherwise main() would call os::setAbstractPriorityDefault( Normal )
     * and setOptions() would raise the global logging level for every later case in this
     * process
     */

    template
    <
        typename E = void
    >
    class TestCmdLineAppT : public bl::cmdline::CmdLineAppBase< TestCmdLineAppT< E > >
    {
        BL_CTR_DEFAULT( TestCmdLineAppT, public )
        BL_NO_COPY_OR_MOVE( TestCmdLineAppT )

    public:

        typedef bl::cmdline::CmdLineAppBase< TestCmdLineAppT< E > >         base_type;

        /**
         * @brief The body which appMain() runs, i.e. what the application does
         */

        bl::cpp::void_callback_t                                            m_body;

        void parseArgs(
            SAA_in                      std::size_t                         argc,
            SAA_in_ecount( argc )       const char* const*                  argv
            )
        {
            BL_UNUSED( argc );
            BL_UNUSED( argv );

            base_type::m_isNoDefaultInit = true;
        }

        void appMain(
            SAA_in                      std::size_t                         argc,
            SAA_in_ecount( argc )       const char* const*                  argv
            )
        {
            BL_UNUSED( argc );
            BL_UNUSED( argv );

            if( m_body )
            {
                m_body();
            }
        }

        /*
         * The two try* helpers are protected on the base, so they are exposed here to let
         * the test body drive them from inside appMain()
         */

        auto callTryParseCommandLine(
            SAA_in                      std::size_t                         argc,
            SAA_in_ecount( argc )       const char* const*                  argv,
            SAA_in                      bl::cmdline::CmdLineBase&           cmdLine
            )
            -> bl::cmdline::CommandBase*
        {
            return base_type::tryParseCommandLine( argc, argv, cmdLine );
        }

        void callTryExecuteCommand(
            SAA_in                      bl::cmdline::CmdLineBase&           cmdLine,
            SAA_in                      bl::cmdline::CommandBase*           command
            )
        {
            base_type::tryExecuteCommand( cmdLine, command );
        }
    };

    typedef TestCmdLineAppT<> TestCmdLineApp;

    /**
     * @brief A CmdLineBase probe with one required option, an overridable authentication
     * failure and an overridable command body
     *
     * Note that checkNonApplicableOptions() is a private virtual on the base, which is
     * still overridable - that is what makes the UserAuthenticationException arm of
     * tryParseCommandLine() reachable at all
     */

    class TestAuthCmdLine : public bl::cmdline::CmdLineBase
    {
    public:

        typedef bl::cmdline::CmdLineBase                                    base_type;

        bl::cmdline::StringOption                                           m_value;

        bool                                                                m_failAuth;

        bl::cpp::void_callback_t                                            m_executeBody;

        TestAuthCmdLine()
            :
            bl::cmdline::CmdLineBase( "TestAuthCmdLine command line options" ),
            m_value( "value", "Some required value", bl::cmdline::Required ),
            m_failAuth( false )
        {
            addOption( m_value );
        }

        virtual bl::cmdline::Result executeCommand( bl::cmdline::CommandBase* command ) OVERRIDE
        {
            if( m_executeBody )
            {
                m_executeBody();
            }

            return base_type::executeCommand( command );
        }

        virtual void checkNonApplicableOptions(
            SAA_in                      const bl::cmdline::CommandBase*     command
            ) const OVERRIDE
        {
            BL_UNUSED( command );

            if( m_failAuth )
            {
                BL_THROW(
                    bl::UserAuthenticationException(),
                    BL_MSG()
                        << "The user login has expired"
                    );
            }
        }
    };

} // __unnamed

/************************************************************************
 * CmdLineAppBase::main() maps an escaping exception to an exit code
 *
 * The exit code is what CI scripts and operators observe, so reordering or removing a
 * catch clause changes the contract invisibly
 */

UTF_AUTO_TEST_CASE( CmdLineApp_MainExitCodeMapping )
{
    using namespace bl;
    using namespace bl::cmdline;

    const CmdLineAppExpectedErrorsScope suppressExpectedErrors;

    const char* argv[] = { "utest" };

    /*
     * (1) The clean path
     */

    {
        TestCmdLineApp app;

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 0 );
    }

    /*
     * (2) po::error and (3) UserMessageException each have their own catch clause and both
     * yield ERROR_CODE_DEFAULT
     */

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            throw bl::po::error( "bad" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 1 );
    }

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            BL_THROW_USER( BL_MSG() << "user facing" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 1 );
    }

    /*
     * (4) main() does not know about UserAuthenticationException - it is caught by the
     * UserMessageException clause it derives from and yields 1, not 13; only the try*
     * helpers map it to UserLoginExpired
     */

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            BL_THROW( bl::UserAuthenticationException(), BL_MSG() << "expired" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 1 );
    }

    /*
     * (5) An exception carrying no error info at all leaves ERROR_CODE_DEFAULT in place
     */

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 1 );
    }

    /*
     * (6) and (7) The errinfo_system_code before errinfo_errno ladder - SystemException::create
     * attaches the former for a system category code and the latter for a generic one
     */

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            BL_THROW_EC( eh::error_code( 42, eh::system_category() ), BL_MSG() << "sys" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 42 );
    }

    {
        TestCmdLineApp app;

        app.m_body = []() -> void
        {
            BL_THROW_EC( eh::error_code( 7, eh::generic_category() ), BL_MSG() << "errno" );
        };

        UTF_REQUIRE_EQUAL( app.main( BL_ARRAY_SIZE( argv ), argv ), 7 );
    }
}

/************************************************************************
 * The try* helpers use a different exit code ladder than main() does
 */

UTF_AUTO_TEST_CASE( CmdLineApp_TryParseAndTryExecuteExitCodes )
{
    using namespace bl;
    using namespace bl::cmdline;

    const CmdLineAppExpectedErrorsScope suppressExpectedErrors;

    const char* argvApp[] = { "utest" };

    /*
     * Note that a CmdLineBase parser instance is single use - OptionBase::m_hasValue is set
     * by the notifier and never cleared - so every sub-block below constructs a fresh one
     */

    /*
     * (8) An unknown option is a po::error - InvalidCommandLine, and the helper reports the
     * failure by returning nullptr
     */

    {
        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        const char* args[] = { "utest", "--nosuchoption" };

        CommandBase* parsed = &cmdLine;

        app.m_body = [ & ]() -> void
        {
            parsed = app.callTryParseCommandLine( BL_ARRAY_SIZE( args ), args, cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::InvalidCommandLine )
            );

        UTF_REQUIRE( nullptr == parsed );
    }

    /*
     * (9) A missing required option is a UserMessageException - InvalidCommand
     */

    {
        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        const char* args[] = { "utest" };

        app.m_body = [ & ]() -> void
        {
            ( void ) app.callTryParseCommandLine( BL_ARRAY_SIZE( args ), args, cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::InvalidCommand )
            );
    }

    /*
     * (10) UserAuthenticationException derives from UserMessageException, so this assertion
     * is what pins the order of the two catch clauses - if they were swapped the exit code
     * would silently become InvalidCommand
     */

    {
        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        cmdLine.m_failAuth = true;

        const char* args[] = { "utest", "--value", "x" };

        app.m_body = [ & ]() -> void
        {
            ( void ) app.callTryParseCommandLine( BL_ARRAY_SIZE( args ), args, cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::UserLoginExpired )
            );
    }

    /*
     * (11) and (12) The same two clauses on tryExecuteCommand
     */

    {
        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        cmdLine.m_executeBody = []() -> void
        {
            BL_THROW( bl::UserAuthenticationException(), BL_MSG() << "expired" );
        };

        app.m_body = [ & ]() -> void
        {
            app.callTryExecuteCommand( cmdLine, &cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::UserLoginExpired )
            );
    }

    {
        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        cmdLine.m_executeBody = []() -> void
        {
            BL_THROW_USER( BL_MSG() << "user facing" );
        };

        app.m_body = [ & ]() -> void
        {
            app.callTryExecuteCommand( cmdLine, &cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::InvalidCommand )
            );
    }

    /*
     * (13) The last resort clause runs EhUtils::processException, which writes a crash dump
     * file into the temporary directory - it is redirected at a test directory here so the
     * run leaves nothing behind
     */

    {
        utest::TestDirectory dir;

        const auto tmpDirPath = dir.path().string();

        const auto originalTmpDir = os::tryGetEnvironmentVariable( "TMPDIR" );
        const auto originalTmp = os::tryGetEnvironmentVariable( "TMP" );
        const auto originalTemp = os::tryGetEnvironmentVariable( "TEMP" );

        const auto restore = [](
            SAA_in          const std::string&                      name,
            SAA_in          const os::string_ptr&                   original
            )
            -> void
        {
            if( original )
            {
                os::setEnvironmentVariable( name, *original );
            }
            else
            {
                os::unsetEnvironmentVariable( name );
            }
        };

        BL_SCOPE_EXIT(
            {
                restore( "TMPDIR", originalTmpDir );
                restore( "TMP", originalTmp );
                restore( "TEMP", originalTemp );
            }
            );

        os::setEnvironmentVariable( "TMPDIR", tmpDirPath );
        os::setEnvironmentVariable( "TMP", tmpDirPath );
        os::setEnvironmentVariable( "TEMP", tmpDirPath );

        TestCmdLineApp app;
        TestAuthCmdLine cmdLine;

        cmdLine.m_executeBody = []() -> void
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "boom" );
        };

        app.m_body = [ & ]() -> void
        {
            app.callTryExecuteCommand( cmdLine, &cmdLine );
        };

        UTF_REQUIRE_EQUAL(
            app.main( BL_ARRAY_SIZE( argvApp ), argvApp ),
            static_cast< int >( ExitCode::UnhandledError )
            );
    }

    /*
     * CmdLineBase::executeCommand( nullptr ) is the 'nothing to execute' contract and yields
     * -1, while a real command runs CommandBase::execute(), which prints the help message
     * and returns a default constructed Result
     */

    {
        TestAuthCmdLine cmdLine;

        UTF_REQUIRE_EQUAL( cmdLine.executeCommand( nullptr ).getReturnCode(), -1 );

        UTF_REQUIRE_EQUAL( cmdLine.executeCommand( &cmdLine ).getReturnCode(), 0 );
    }
}
