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
#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/TestUtils.h>

#include <baselib/security/JsonSecuritySerialization.h>

#include <baselib/core/SerializationUtils.h>

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

    /**
     * @brief Parse and execute an already tokenized bl-tool command line
     *
     * bl::cmdline::CmdLineBase::parseCommandLine( const std::string& ) tokenizes with
     * bl::po::split_unix, which drops empty tokens, so an argument whose value is the
     * empty string can only be expressed the way a real shell delivers it - as its own
     * entry in argv
     */

    bl::cmdline::Result runBlToolArgs( SAA_in const std::vector< std::string >& args )
    {
        bltool::CmdLine cmdLine;

        auto argsCopy = args;

        const auto command = cmdLine.parseCommandLine( argsCopy );

        UTF_REQUIRE( command != nullptr );

        return cmdLine.executeCommand( command );
    }

    /**
     * @brief Reads the exact bytes of 'path'
     *
     * The tool writes its outputs through bl::encoding::writeTextFile, so they must be
     * read back as raw bytes for a byte level comparison to mean anything
     */

    std::string readFileBytes( SAA_in const bl::fs::path& path )
    {
        const auto size = static_cast< std::size_t >( bl::fs::file_size( path ) );

        std::string content;

        if( size )
        {
            content.resize( size );

            const auto file = bl::os::fopen( path, "rb" );

            bl::os::fread( file, &content[ 0 ], size );
        }

        return content;
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

UTF_AUTO_TEST_CASE( BlTool_CryptoRsaKeyProtectionTests )
{
    using namespace bl::security;

    utest::TestDirectory dir;

    /*
     * The security critical branch of the tool: getKeyAsText() derives isEncrypted from
     * the *resolved password*, not from --encrypt, and it is bl-tool's job to always hand
     * JsonSecuritySerialization a (protection, password) pair which satisfies that API's
     * own invariant
     *
     * Every sub-block is deliberately non-interactive - --encrypt is always paired with an
     * explicit --password, so no path here can reach os::readPasswordFromInput and block
     */

    {
        /*
         * (1) a password produces an encrypted PEM which only that password opens
         */

        const auto pem1 = dir.testFile( "encrypted.pem" );

        UTF_REQUIRE_EQUAL(
            runBlTool( "crypto rsakeygenerate --pemformat --password secret --path " + pem1.string() ).getReturnCode(),
            0
            );

        const auto text = readFileBytes( pem1 );

        UTF_REQUIRE( bl::cpp::contains( text, std::string( "-----BEGIN ENCRYPTED PRIVATE KEY-----" ) ) );

        UTF_REQUIRE_NO_THROW( JsonSecuritySerialization::loadPrivateKeyFromPemString( text, "secret" ) );
        UTF_REQUIRE_THROW( JsonSecuritySerialization::loadPrivateKeyFromPemString( text, "wrong" ), std::exception );
    }

    {
        /*
         * (2) no password at all produces a plaintext PEM - which is a deliberate
         * capability of the tool, not the consequence of an omitted password
         */

        const auto pem2 = dir.testFile( "plaintext.pem" );

        {
            /*
             * Writing a private key in the clear is warned about, and that warning is
             * expected here
             */

            bl::Logging::LineLoggerPusher pushLineLogger( &utest::warningToDebugLineLogger );

            UTF_REQUIRE_EQUAL(
                runBlTool( "crypto rsakeygenerate --pemformat --path " + pem2.string() ).getReturnCode(),
                0
                );
        }

        const auto text = readFileBytes( pem2 );

        UTF_REQUIRE( bl::cpp::contains( text, std::string( "-----BEGIN PRIVATE KEY-----" ) ) );
        UTF_REQUIRE( ! bl::cpp::contains( text, std::string( "ENCRYPTED" ) ) );

        UTF_REQUIRE_NO_THROW( JsonSecuritySerialization::loadPrivateKeyFromPemString( text, "" ) );
    }

    {
        /*
         * (3) asking for encryption with an empty password is refused, and nothing is
         * written - the key must never end up on disk in the clear on this path
         */

        const auto pem3 = dir.testFile( "rejected.pem" );

        const std::vector< std::string > args
        {
            "crypto",
            "rsakeygenerate",
            "--pemformat",
            "--encrypt",
            "--password",
            "",
            "--path",
            pem3.string(),
        };

        UTF_REQUIRE_THROW_MESSAGE(
            runBlToolArgs( args ),
            bl::UserMessageException,
            "password cannot be empty"
            );

        UTF_REQUIRE( ! bl::fs::path_exists( pem3 ) );
    }

    {
        /*
         * (4) and (5) the JOSE branch rejects both ways of asking for a password; (5) in
         * particular must be reached before any prompt, so the case cannot block
         */

        const auto jose1 = dir.testFile( "rejected1.json" );
        const auto jose2 = dir.testFile( "rejected2.json" );

        UTF_REQUIRE_THROW_MESSAGE(
            runBlTool( "crypto rsakeygenerate --password secret --path " + jose1.string() ),
            bl::UserMessageException,
            "not supported for JOSE type keys"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            runBlTool( "crypto rsakeygenerate --encrypt --path " + jose2.string() ),
            bl::UserMessageException,
            "not supported for JOSE type keys"
            );

        UTF_REQUIRE( ! bl::fs::path_exists( jose1 ) );
        UTF_REQUIRE( ! bl::fs::path_exists( jose2 ) );
    }

    {
        /*
         * (6) the default format is JOSE
         */

        const auto jose3 = dir.testFile( "plain.json" );

        UTF_REQUIRE_EQUAL(
            runBlTool( "crypto rsakeygenerate --path " + jose3.string() ).getReturnCode(),
            0
            );

        UTF_REQUIRE_NO_THROW(
            JsonSecuritySerialization::loadPrivateKeyFromJsonString( readFileBytes( jose3 ) )
            );
    }
}

UTF_AUTO_TEST_CASE( BlTool_CryptoRsaKeyExportRoundTripTests )
{
    using namespace bl::security;

    utest::TestDirectory dir;

    const auto srcJson = dir.testFile( "src.json" );
    const auto midPem = dir.testFile( "mid.pem" );
    const auto outJson = dir.testFile( "out.json" );

    /*
     * (1) a JOSE key to start from
     */

    UTF_REQUIRE_EQUAL(
        runBlTool( "crypto rsakeygenerate --path " + srcJson.string() ).getReturnCode(),
        0
        );

    /*
     * (2) export it as an encrypted PEM and (3) bring it back as JOSE
     */

    UTF_REQUIRE_EQUAL(
        runBlTool(
            "crypto rsakeyexport --sourcepath " + srcJson.string() +
            " --destinationpemformat --encrypt --destinationpassword pw --destinationpath " + midPem.string()
            ).getReturnCode(),
        0
        );

    UTF_REQUIRE( bl::cpp::contains( readFileBytes( midPem ), std::string( "-----BEGIN ENCRYPTED PRIVATE KEY-----" ) ) );

    UTF_REQUIRE_EQUAL(
        runBlTool(
            "crypto rsakeyexport --sourcepath " + midPem.string() +
            " --sourcepemformat --sourcepassword pw --destinationpath " + outJson.string()
            ).getReturnCode(),
        0
        );

    /*
     * (4) the JOSE representation survives the PEM encrypted round trip byte for byte
     */

    UTF_REQUIRE_EQUAL( readFileBytes( srcJson ), readFileBytes( outJson ) );

    {
        /*
         * (5) there is nothing to encrypt in a public key, so asking is refused
         */

        const auto pub = dir.testFile( "pub-rejected" );

        UTF_REQUIRE_THROW_MESSAGE(
            runBlTool(
                "crypto rsakeyexport --sourcepath " + srcJson.string() +
                " --publickeyonly --encrypt --destinationpassword pw --destinationpath " + pub.string()
                ),
            bl::UserMessageException,
            "not supported for public keys"
            );

        UTF_REQUIRE( ! bl::fs::path_exists( pub ) );
    }

    {
        /*
         * (6) the same rejection on the input side of the JOSE branch
         */

        const auto rejected = dir.testFile( "src-rejected" );

        UTF_REQUIRE_THROW_MESSAGE(
            runBlTool(
                "crypto rsakeyexport --sourcepath " + srcJson.string() +
                " --sourcepassword pw --destinationpath " + rejected.string()
                ),
            bl::UserMessageException,
            "not supported for JOSE type keys"
            );

        UTF_REQUIRE( ! bl::fs::path_exists( rejected ) );
    }

    {
        /*
         * (7) the public key comes out in whichever of the two formats was asked for
         */

        const auto pubJson = dir.testFile( "pub.json" );
        const auto pubPem = dir.testFile( "pub.pem" );

        UTF_REQUIRE_EQUAL(
            runBlTool(
                "crypto rsakeyexport --sourcepath " + srcJson.string() +
                " --publickeyonly --destinationpath " + pubJson.string()
                ).getReturnCode(),
            0
            );

        UTF_REQUIRE_NO_THROW( bl::json::readFromString( readFileBytes( pubJson ) ) );

        UTF_REQUIRE_EQUAL(
            runBlTool(
                "crypto rsakeyexport --sourcepath " + srcJson.string() +
                " --publickeyonly --destinationpemformat --destinationpath " + pubPem.string()
                ).getReturnCode(),
            0
            );

        UTF_REQUIRE( bl::cpp::contains( readFileBytes( pubPem ), std::string( "-----BEGIN PUBLIC KEY-----" ) ) );
    }
}

UTF_AUTO_TEST_CASE( BlTool_PathRemoveTests )
{
    utest::TestDirectory dir;

    /*
     * The force / non-force selection is the operator's only signal that a typo'd path
     * removed nothing, so swapping the two branches must not go unnoticed
     */

    const auto tree = dir.path() / "tree";

    bl::fs::safeMkdirs( tree / "nested" );

    ( void ) bl::os::fopen( tree / "nested" / "file.txt", "wb" );

    UTF_REQUIRE( bl::fs::path_exists( tree / "nested" / "file.txt" ) );

    const std::string removeTree = "path remove --path " + tree.string();

    /*
     * (1) the whole tree goes
     */

    UTF_REQUIRE_EQUAL( runBlTool( removeTree ).getReturnCode(), 0 );
    UTF_REQUIRE( ! bl::fs::path_exists( tree ) );

    /*
     * (2) without --force the now missing path is an error
     */

    UTF_REQUIRE_THROW( runBlTool( removeTree ), std::exception );

    /*
     * (3) with --force it is a no-op, and (4) the '-f' alias is registered
     */

    UTF_REQUIRE_NO_THROW( runBlTool( "path remove --force --path " + tree.string() ) );
    UTF_REQUIRE_EQUAL( runBlTool( "path remove --force --path " + tree.string() ).getReturnCode(), 0 );

    UTF_REQUIRE_NO_THROW( runBlTool( "path remove -f --path " + tree.string() ) );
    UTF_REQUIRE_EQUAL( runBlTool( "path remove -f --path " + tree.string() ).getReturnCode(), 0 );

    {
        /*
         * (5) a single file rather than a directory
         */

        const auto one = dir.testFile( "one.txt" );

        ( void ) bl::os::fopen( one, "wb" );

        UTF_REQUIRE( bl::fs::path_exists( one ) );

        UTF_REQUIRE_EQUAL( runBlTool( "path remove --path " + one.string() ).getReturnCode(), 0 );

        UTF_REQUIRE( ! bl::fs::path_exists( one ) );
    }
}

UTF_AUTO_TEST_CASE( BlTool_GenerateBase64ResourceTests )
{
    utest::TestDirectory dir;

    /*
     * The command emits the encoded payload twice - once as a run of string literals for
     * the non-Windows half of the template and once as dataParts.push_back() calls for the
     * _WIN32 half. The two halves are only ever compiled on mutually exclusive platforms,
     * so a divergence between them produces a resource header which works on one platform
     * and yields corrupt data on the other with no build error anywhere
     */

    const auto in = dir.testFile( "input.bin" );
    const auto out = dir.testFile( "TestRes.h" );

    /*
     * 250 bytes encodes to 336 base64 characters, which is not a multiple of the 100
     * character chunk size, so the partial final chunk is exercised
     */

    std::string inputData;
    inputData.reserve( 250U );

    for( std::size_t i = 0U; i < 250U; ++i )
    {
        inputData.push_back( static_cast< char >( ( i * 7U + 3U ) % 256U ) );
    }

    /*
     * A binary resource must survive both of these bytes
     */

    inputData[ 10U ] = static_cast< char >( 0x00 );
    inputData[ 20U ] = static_cast< char >( 0xFF );

    {
        const auto file = bl::os::fopen( in, "wb" );

        bl::os::fwrite( file, inputData.c_str(), inputData.size() );
    }

    UTF_REQUIRE_EQUAL(
        runBlTool(
            "generate base64resource --inputfile " + in.string() +
            " --classname TestRes --outputfile " + out.string()
            ).getReturnCode(),
        0
        );

    const auto text = readFileBytes( out );

    /*
     * Both class name substitutions landed and no placeholder survived
     */

    UTF_REQUIRE( bl::cpp::contains( text, std::string( "#ifndef __BL_RESOURCE_TESTRES_H_" ) ) );
    UTF_REQUIRE( bl::cpp::contains( text, std::string( "typedef TestResT<> TestRes;" ) ) );
    UTF_REQUIRE( ! bl::cpp::contains( text, std::string( "{{" ) ) );

    /*
     * The helpers below deliberately search rather than match whole lines, so a change to
     * the indentation of the template does not break the test
     */

    const auto collectQuoted = [](
        SAA_in          const std::string&                              region,
        SAA_out         std::size_t&                                    count
        ) -> std::string
    {
        std::string result;

        count = 0U;

        std::size_t pos = 0U;

        for( ;; )
        {
            const auto open = region.find( '"', pos );

            if( open == std::string::npos )
            {
                break;
            }

            const auto close = region.find( '"', open + 1U );

            UTF_REQUIRE( close != std::string::npos );

            result += region.substr( open + 1U, close - open - 1U );

            ++count;

            pos = close + 1U;
        }

        return result;
    };

    std::size_t literalChunkCount = 0U;
    std::string joinedNonWindows;

    {
        const std::string marker = "const char* encoded =";

        const auto markerPos = text.find( marker );

        UTF_REQUIRE( markerPos != std::string::npos );

        const auto endPos = text.find( ';', markerPos );

        UTF_REQUIRE( endPos != std::string::npos );

        const auto begin = markerPos + marker.size();

        joinedNonWindows = collectQuoted( text.substr( begin, endPos - begin ), literalChunkCount );
    }

    std::size_t pushBackCount = 0U;
    std::string joinedWindows;

    {
        const std::string marker = "dataParts.push_back( \"";

        std::size_t pos = 0U;

        for( ;; )
        {
            const auto found = text.find( marker, pos );

            if( found == std::string::npos )
            {
                break;
            }

            const auto valueStart = found + marker.size();
            const auto valueEnd = text.find( '"', valueStart );

            UTF_REQUIRE( valueEnd != std::string::npos );

            joinedWindows += text.substr( valueStart, valueEnd - valueStart );

            ++pushBackCount;

            pos = valueEnd;
        }
    }

    /*
     * The real oracle - the emitted payload decodes back to the exact input bytes
     */

    UTF_REQUIRE_EQUAL( bl::SerializationUtils::base64DecodeString( joinedNonWindows ), inputData );

    /*
     * ... and the two independently emitted payloads are identical
     */

    UTF_REQUIRE_EQUAL( joinedNonWindows, joinedWindows );

    UTF_REQUIRE_EQUAL( pushBackCount, ( joinedNonWindows.size() + 99U ) / 100U );
    UTF_REQUIRE_EQUAL( literalChunkCount, pushBackCount );

    {
        /*
         * The reserve() argument is only required to be big enough - the template computes
         * it as 1 + length / 100, which over-reserves by one at exact multiples, and this
         * test must not enshrine that off-by-one
         */

        const std::string marker = "dataParts.reserve( ";

        const auto markerPos = text.find( marker );

        UTF_REQUIRE( markerPos != std::string::npos );

        const auto begin = markerPos + marker.size();
        const auto endPos = text.find( 'U', begin );

        UTF_REQUIRE( endPos != std::string::npos );

        const auto reserveCount =
            bl::utils::lexical_cast< std::size_t >( text.substr( begin, endPos - begin ) );

        UTF_REQUIRE( reserveCount >= pushBackCount );
    }

    {
        /*
         * Without --outputfile the resource goes to std::cout and no file is created
         */

        const auto notWritten = dir.testFile( "NotWritten.h" );

        UTF_REQUIRE_NO_THROW(
            runBlTool( "generate base64resource --inputfile " + in.string() + " --classname TestRes" )
            );

        UTF_REQUIRE( ! bl::fs::path_exists( notWritten ) );
    }
}
