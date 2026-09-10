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

/*
 * This is a second translation unit of the utf_baselib module - projects/make/common.mk
 * picks it up automatically through the wildcard over every .cpp in this directory -
 * and it exists for two
 * reasons which a Test*.h header included from UtfBaselibMain.cpp could not serve:
 *
 * 1) Self-containment. UtfBaselibMain.cpp includes <utests/baselib/UtfMain.h> - and hence
 *    Utf.h, core/OS.h, core/Logging.h, core/BaseIncludes.h and much more - before any
 *    Test*.h, so a header included from there is compiled with all of that already in
 *    scope and cannot fail on a missing include of its own. Here the production headers
 *    come first and <utests/baselib/Utf.h> comes last, so none of them may lean on the
 *    test framework's include closure.
 *
 *    ORDER IS LOAD BEARING. Do not move <utests/baselib/Utf.h> up and do not add any
 *    other include above the production headers - either silently disarms this half of
 *    the check. Note also what this does NOT prove: only the first header below is
 *    compiled with nothing in scope; every later one additionally sees the closure of
 *    the headers above it. Proving each header standalone needs one translation unit per
 *    header, which this module does not have.
 *
 * 2) Instantiation. Most of these headers are class templates, and a member of a class
 *    template is only type-checked when it is instantiated - so merely including them
 *    would prove that they parse and nothing more. The case below therefore odr-uses at
 *    least one entity from every header included here.
 *
 * Deliberately NOT included:
 *
 *  - core/GrammarUtils.h, which #defines BOOST_SPIRIT_THREADSAFE and BOOST_SPIRIT_DEBUG
 *    unconditionally before pulling Boost.Spirit and would change Spirit's behaviour for
 *    the rest of this translation unit
 *  - data/models/Jwt.h and data/models/JsonSecurity.h, which are compiled by the
 *    utf_baselib_data module instead
 */

#include <baselib/httpserver/HttpServerPorts.h>
#include <baselib/messaging/server/BaseServerPorts.h>
#include <baselib/cmdline/ExitCodes.h>
#include <baselib/cmdline/GlobalOptions.h>
#include <baselib/cmdline/CmdLineAppBase.h>
#include <baselib/core/DateTimeValidationUtils.h>
#if defined( _WIN32 )
#include <baselib/core/specific/ComUtils.h>
#include <baselib/core/specific/WindowsShellShortcut.h>
#endif
#include <baselib/loader/Version.h>
#include <baselib/jni/JvmHelpers.h>
#include <baselib/crypto/HmacSha256.h>

#include <utests/baselib/Utf.h>

#include <algorithm>
#include <string>

namespace
{
    /**
     * @brief A minimal CmdLineBase which owns a bl::cmdline::GlobalOptions
     *
     * GlobalOptionsT's constructor is the only thing which registers the four switches,
     * so constructing this probe is what instantiates it
     */

    class GlobalOptionsProbe : public bl::cmdline::CmdLineBase
    {
        BL_NO_COPY_OR_MOVE( GlobalOptionsProbe )

    public:

        bl::cmdline::GlobalOptions                                      m_globalOptions;

        GlobalOptionsProbe()
            :
            bl::cmdline::CmdLineBase( "GlobalOptionsProbe [options]" ),
            m_globalOptions( this )
        {
        }
    };

    /**
     * @brief A CmdLineAppBase CRTP probe with an empty body
     *
     * m_isNoDefaultInit must be set - otherwise main() would construct a second
     * AppInitDoneDefault (and a second thread pool) inside a running test binary - and
     * m_isServer must stay false, or main() would raise the global logging level and the
     * default abstract priority for every case which runs after this one
     */

    template
    <
        typename E = void
    >
    class CmdLineAppProbeT : public bl::cmdline::CmdLineAppBase< CmdLineAppProbeT< E > >
    {
        BL_CTR_DEFAULT( CmdLineAppProbeT, public )
        BL_NO_COPY_OR_MOVE( CmdLineAppProbeT )

    public:

        typedef bl::cmdline::CmdLineAppBase< CmdLineAppProbeT< E > >    base_type;

        bl::cpp::ScalarTypeIniter< bool >                               m_appMainCalled;

        void parseArgs(
            SAA_in                      std::size_t                     argc,
            SAA_in_ecount( argc )       const char* const*              argv
            )
        {
            BL_UNUSED( argc );
            BL_UNUSED( argv );

            base_type::m_isNoDefaultInit = true;
        }

        void appMain(
            SAA_in                      std::size_t                     argc,
            SAA_in_ecount( argc )       const char* const*              argv
            )
        {
            BL_UNUSED( argc );
            BL_UNUSED( argv );

            m_appMainCalled = true;
        }
    };

    typedef CmdLineAppProbeT<> CmdLineAppProbe;

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_PublicHeadersAreSelfContainedAndInstantiable )
{
    using namespace bl;

    /************************************************************************
     * httpserver/HttpServerPorts.h and messaging/server/BaseServerPorts.h
     *
     * The enums are typed as os::port_t, which HttpServerPorts.h did not reach on its own
     * until an explicit include of core/OS.h was added to it
     */

    UTF_REQUIRE_EQUAL( 80U, ( unsigned ) httpserver::HttpDefaultInboundPort );
    UTF_REQUIRE_EQUAL( 443U, ( unsigned ) httpserver::HttpDefaultSecureInboundPort );
    UTF_REQUIRE_EQUAL( 29300U, ( unsigned ) messaging::MessagingBrokerDefaultInboundPort );

    /************************************************************************
     * cmdline/ExitCodes.h
     *
     * These are the values operators and CI scripts observe, so they are a contract
     */

    UTF_REQUIRE_EQUAL( 0, ( int ) cmdline::ExitCode::Ok );
    UTF_REQUIRE_EQUAL( 1, ( int ) cmdline::ExitCode::UnhandledError );
    UTF_REQUIRE_EQUAL( 2, ( int ) cmdline::ExitCode::InvalidCommandLine );
    UTF_REQUIRE_EQUAL( 3, ( int ) cmdline::ExitCode::InvalidCommand );
    UTF_REQUIRE_EQUAL( 13, ( int ) cmdline::ExitCode::UserLoginExpired );

    /************************************************************************
     * cmdline/GlobalOptions.h
     *
     * Constructing the probe instantiates GlobalOptionsT and registers its four switches;
     * parsing all four proves each one really made it into the option set. A CmdLineBase
     * instance is single-use, so this is one parse of one instance
     */

    {
        GlobalOptionsProbe probe;

        UTF_REQUIRE_NO_THROW( probe.parseCommandLine( "--help --verbose --dryrun --debug" ) );

        UTF_REQUIRE( probe.m_globalOptions.m_help.getValue() );
        UTF_REQUIRE( probe.m_globalOptions.m_verbose.getValue() );
        UTF_REQUIRE( probe.m_globalOptions.m_dryRun.getValue() );
        UTF_REQUIRE( probe.m_globalOptions.m_debug.getValue() );
    }

    /************************************************************************
     * core/DateTimeValidationUtils.h
     *
     * One accept and one reject, purely to instantiate the class template - the full
     * accept / reject tables live in TestDateTimeValidationUtils.h. BL_THROW_USER raises
     * bl::UserMessageException
     */

    UTF_REQUIRE_NO_THROW( time::DateTimeValidationUtils::validateTime( "23:59:59" ) );
    UTF_REQUIRE_THROW( time::DateTimeValidationUtils::validateTime( "24:00:00" ), UserMessageException );

    /************************************************************************
     * loader/Version.h
     *
     * fromString returns void and writes three SAA_out std::size_t& parameters
     */

    {
        std::size_t majorVersion = 0U;
        std::size_t minorVersion = 0U;
        std::size_t patchVersion = 0U;

        loader::Version::fromString( "1.2.3", majorVersion, minorVersion, patchVersion );

        UTF_REQUIRE_EQUAL( 1U, majorVersion );
        UTF_REQUIRE_EQUAL( 2U, minorVersion );
        UTF_REQUIRE_EQUAL( 3U, patchVersion );

        UTF_REQUIRE_EQUAL( std::string( "1.2.3" ), loader::Version::toString( 1U, 2U, 3U ) );

        UTF_REQUIRE_THROW( loader::Version::fromString( "1.2", majorVersion, minorVersion, patchVersion ), ArgumentException );
    }

    /************************************************************************
     * jni/JvmHelpers.h
     *
     * buildClassPath requires the main JAR to exist and always iterates a 'lib' directory
     * beside it, so the layout is created here rather than pointed at a real JVM install
     */

    {
        fs::TmpDir tmpDir;

        const auto& basePath = tmpDir.path();
        const auto libPath = basePath / "lib";

        fs::safeMkdirs( libPath );

        const auto mainJar = basePath / "probe.jar";
        const auto depJar = libPath / "dependency.jar";
        const auto notAJar = libPath / "readme.txt";

        for( const auto& filePath : { mainJar, depJar, notAJar } )
        {
            fs::SafeOutputFileStreamWrapper outputFile( filePath );
            outputFile.stream() << "probe";
        }

        const auto classPath = jni::JvmHelpers::buildClassPath( basePath, "probe.jar" );

        const std::string separator( 1U /* count */, os::pathVarSeparator );

        UTF_REQUIRE( cpp::contains( classPath, mainJar.filename().string() ) );
        UTF_REQUIRE( cpp::contains( classPath, depJar.filename().string() ) );

        /*
         * Only '.jar' files from 'lib' are appended, so the sibling text file must not be
         * there and there must be exactly one separator - the one before the dependency
         */

        UTF_REQUIRE( ! cpp::contains( classPath, notAJar.filename().string() ) );
        UTF_REQUIRE_EQUAL( 1, std::count( classPath.begin(), classPath.end(), os::pathVarSeparator ) );
        UTF_REQUIRE( cpp::contains( classPath, separator ) );
    }

    /************************************************************************
     * crypto/HmacSha256.h
     *
     * The digest is the 32 bytes of HMAC-SHA-256 rendered as uppercase hex, i.e. 64
     * characters - the header's own length guard is on the raw 32 bytes
     */

    {
        typedef crypto::detail::HmacSha256 hmac_t;

        const auto digest = hmac_t::calculateMessageDigest( "" /* message */, "" /* key */ );

        UTF_REQUIRE_EQUAL( 64U, digest.size() );

        UTF_REQUIRE(
            std::all_of(
                digest.begin(),
                digest.end(),
                []( SAA_in const char ch ) -> bool
                {
                    return ( ch >= '0' && ch <= '9' ) || ( ch >= 'A' && ch <= 'F' );
                }
                )
            );

        /*
         * The digest must depend on both inputs and be reproducible
         */

        UTF_REQUIRE_EQUAL( digest, hmac_t::calculateMessageDigest( "", "" ) );
        UTF_REQUIRE( digest != hmac_t::calculateMessageDigest( "message", "" ) );
        UTF_REQUIRE( digest != hmac_t::calculateMessageDigest( "", "key" ) );
    }

    /************************************************************************
     * core/specific/ComUtils.h and core/specific/WindowsShellShortcut.h
     *
     * Both are #error guarded to _WIN32, so they can only be compiled - and this block
     * can only run - on Windows
     */

#if defined( _WIN32 )

    {
        UTF_REQUIRE( os::onWindows() );

        BL_COM_INIT_SINGLETHREADED();

        const fs::path missing( "c:\\this-path-does-not-exist\\probe.lnk" );

        UTF_REQUIRE_THROW(
            os::specific::WindowsShellShortcut::queryShortcut( missing ),
            UnexpectedException
            );
    }

#endif // defined( _WIN32 )

    /************************************************************************
     * cmdline/CmdLineAppBase.h
     *
     * main() is what instantiates the class template, and it installs a process global
     * ASIO error callback without restoring it, so the previous one is put back here
     */

    {
        auto previousCallback = BoostAsioErrorCallback::installCallback(
            BoostAsioErrorCallback::asio_error_callback_t()
            );

        BL_SCOPE_EXIT(
            {
                BoostAsioErrorCallback::installCallback( std::move( previousCallback ) );
            }
            );

        const char* argv[] = { "probe" };

        CmdLineAppProbe probe;

        UTF_REQUIRE_EQUAL( 0, probe.main( BL_ARRAY_SIZE( argv ), argv ) );
        UTF_REQUIRE( probe.m_appMainCalled );
    }
}
