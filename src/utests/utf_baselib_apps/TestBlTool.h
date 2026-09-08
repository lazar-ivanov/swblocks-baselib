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

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/TestUtils.h>

#include <apps/bl-tool/CmdLine.h>

namespace
{
    /**
     * @brief Parse and execute a bl-tool command line
     *
     * Note that a bl::cmdline::CmdLineBase instance is single use - the option's
     * bl::cmdline::OptionBase::m_hasValue flag is set by the parser's notifier and is
     * never cleared, so a second parse on the same object would observe stale values.
     * Every invocation must therefore start from a freshly constructed bltool::CmdLine.
     */

    bl::cmdline::Result runBlTool( SAA_in const std::string& args )
    {
        bltool::CmdLine cmdLine;

        const auto command = cmdLine.parseCommandLine( args );

        UTF_REQUIRE( command != nullptr );

        return cmdLine.executeCommand( command );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BlTool_CmdLineTreeTests )
{
    /*
     * Every leaf of the bl-tool command tree, paired with the minimal command line
     * which satisfies the required options of that leaf
     */

    struct LeafCommandInfo
    {
        const char*         m_commandLine;
        const char*         m_fullName;
    };

    const LeafCommandInfo leafCommands[] =
    {
        { "generate uuids",                                                     "generate uuids" },
        { "generate base64resource --inputfile in.bin --classname TestData",    "generate base64resource" },
        { "crypto rsakeyexport --sourcepath key.pem",                           "crypto rsakeyexport" },
        { "crypto rsakeygenerate",                                              "crypto rsakeygenerate" },
        { "http request --host localhost --path /",                             "http request" },
        { "path remove --path somepath",                                        "path remove" },
        { "path spaceused --path somepath",                                     "path spaceused" },
        { "processfiles printfilesbysize --path somepath",                      "processfiles printfilesbysize" },
        { "processfiles removecommentswithmarkers --path somepath --marker M",  "processfiles removecommentswithmarkers" },
        { "processfiles removeemptycomments --path somepath",                   "processfiles removeemptycomments" },
        { "processfiles spacingtrimright --path somepath",                      "processfiles spacingtrimright" },
        { "processfiles tabstospaces --path somepath",                          "processfiles tabstospaces" },
        { "processfiles updateheadercomment --path somepath",                   "processfiles updateheadercomment" },
    };

    for( const auto& leaf : leafCommands )
    {
        UTF_MESSAGE( std::string( "* parsing command line: " ) + leaf.m_commandLine );

        bltool::CmdLine cmdLine;

        const auto command = cmdLine.parseCommandLine( std::string( leaf.m_commandLine ) );

        UTF_REQUIRE( command != nullptr );
        UTF_REQUIRE_EQUAL( command -> getFullName(), leaf.m_fullName );
    }

    /*
     * The required options of a leaf command are enforced when the '--help' override
     * switch is not present on the command line
     */

    {
        bltool::CmdLine cmdLine;

        UTF_REQUIRE_THROW( cmdLine.parseCommandLine( "path remove" ), bl::UserMessageException );
    }

    {
        bltool::CmdLine cmdLine;

        UTF_REQUIRE_THROW( cmdLine.parseCommandLine( "crypto rsakeyexport" ), bl::UserMessageException );
    }

    {
        bltool::CmdLine cmdLine;

        UTF_REQUIRE_THROW(
            cmdLine.parseCommandLine( "processfiles removecommentswithmarkers --path x" ),
            bl::UserMessageException
            );
    }

    /*
     * An unknown command name is rejected
     */

    {
        bltool::CmdLine cmdLine;

        UTF_REQUIRE_THROW( cmdLine.parseCommandLine( "nosuchcommand" ), bl::UserMessageException );
    }
}

UTF_AUTO_TEST_CASE( BlTool_HelpSwitchSuppressesExecutionTests )
{
    utest::TestDirectory dir;

    const auto victim = dir.path() / "victim";

    bl::fs::safeMkdirs( victim );

    UTF_REQUIRE( bl::fs::path_exists( victim ) );

    const std::string removeCommandLine = "path remove --path " + victim.string();

    {
        /*
         * bl::cmdline::CmdLineBase::parseCommandNames stops at the first '-' prefixed
         * token, so the trailing '--help' still resolves to the 'path remove' leaf and
         * sets the root's help switch; bltool::CmdLineT::executeCommand must then print
         * the usage text instead of executing the command - i.e. the directory survives
         */

        bltool::CmdLine cmdLine;

        const auto command = cmdLine.parseCommandLine( removeCommandLine + " --help" );

        UTF_REQUIRE( command != nullptr );
        UTF_REQUIRE_EQUAL( command -> getFullName(), "path remove" );
        UTF_REQUIRE( cmdLine.getGlobalOptions().m_help.getValue() );

        UTF_REQUIRE_EQUAL( cmdLine.executeCommand( command ).getReturnCode(), 0 );

        UTF_REQUIRE( bl::fs::path_exists( victim ) );
    }

    {
        /*
         * The control case - the very same command line without '--help' does remove the
         * directory, which proves the check above would have detected the deletion
         */

        UTF_REQUIRE_EQUAL( runBlTool( removeCommandLine ).getReturnCode(), 0 );

        UTF_REQUIRE( ! bl::fs::path_exists( victim ) );
    }

    {
        /*
         * An empty command line resolves to the root command, which prints the brief
         * help screen and returns zero
         */

        bltool::CmdLine cmdLine;

        const auto command = cmdLine.parseCommandLine( "" );

        UTF_REQUIRE( command != nullptr );
        UTF_REQUIRE_EQUAL( command -> getFullName(), "" );

        UTF_REQUIRE_EQUAL( cmdLine.executeCommand( command ).getReturnCode(), 0 );
    }

    {
        /*
         * The global options are observable through getGlobalOptions() - note that the
         * switch must be placed after the command names, as parseCommandNames stops at
         * the first '-' prefixed token
         */

        bltool::CmdLine cmdLine;

        UTF_REQUIRE( cmdLine.parseCommandLine( "generate uuids" ) != nullptr );
        UTF_REQUIRE( ! cmdLine.getGlobalOptions().m_debug.getValue() );
    }

    {
        bltool::CmdLine cmdLine;

        UTF_REQUIRE( cmdLine.parseCommandLine( "generate uuids --debug" ) != nullptr );
        UTF_REQUIRE( cmdLine.getGlobalOptions().m_debug.getValue() );
    }
}
