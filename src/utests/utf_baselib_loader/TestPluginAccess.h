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

#include <baselib/loader/PluginAccess.h>
#include <baselib/loader/Manifest.h>
#include <baselib/loader/Platform.h>

#include <baselib/core/OS.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/Utf.h>

#include <unordered_map>

UTF_AUTO_TEST_CASE( TestPluginAccess )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * Fields not used by PluginAccess and not required for test purposes -
     * these can be shared by all Manifest objects
     */

    const std::size_t versionMajor = 1;
    const std::size_t versionMinor = 0;
    const std::size_t versionPatch = 0;

    const std::string pluginName( "name" );
    const std::string pluginDesc( "description" );

    const auto isClient = false;
    const auto isServer = true;

    const auto platform = Platform::get( "os", "arch", "toolchain" );
    const auto cppCompatId = uuids::create();

    /*
     * Create test data for plug-in 1
     */

    const auto m1ServerId = uuids::create();

    const auto m1ClsId1 = uuids::create();
    const auto m1ClsId2 = uuids::create();
    const auto m1ClsId3 = uuids::create();

    std::set< om::clsid_t > m1ClsIds;
    m1ClsIds.insert( m1ClsId1 );
    m1ClsIds.insert( m1ClsId2 );
    m1ClsIds.insert( m1ClsId3 );

    const om::clsid_t m1PluginClsId( m1ClsId1 );

    auto m1 = Manifest::createInstance(
        m1ServerId,
        versionMajor,
        versionMinor,
        versionPatch,
        std::move( m1ClsIds ),
        m1PluginClsId,
        cpp::copy( pluginName ),
        cpp::copy( pluginDesc ),
        isClient,
        isServer,
        om::copy( platform ),
        cppCompatId
        );

    const auto p1Path = fs::path( "plugin1" );

    /*
     * Create test data for plug-in 2
     */

    const auto m2ServerId = uuids::create();

    const auto m2ClsId1 = uuids::create();
    const auto m2ClsId2 = uuids::create();
    const auto m2ClsId3 = uuids::create();

    std::set< om::clsid_t > m2ClsIds;
    m2ClsIds.insert( m2ClsId1 );
    m2ClsIds.insert( m2ClsId2 );
    m2ClsIds.insert( m2ClsId3 );

    const om::clsid_t m2PluginClsId( m2ClsId1 );

    auto m2 = Manifest::createInstance(
        m2ServerId,
        versionMajor,
        versionMinor,
        versionPatch,
        std::move( m2ClsIds ),
        m2PluginClsId,
        cpp::copy( pluginName ),
        cpp::copy( pluginDesc ),
        isClient,
        isServer,
        om::copy( platform ),
        cppCompatId
        );

    const auto p2Path = fs::path( "plugin2" );

    /*
     * Create path -> manifest map
     */

    std::unordered_map< fs::path, om::ObjPtr< Manifest > > plugins;
    plugins.emplace( p1Path, std::move( m1 ) );
    plugins.emplace( p2Path, std::move( m2 ) );

    const auto pluginAccess = PluginAccess::createInstance( plugins );

    /*
     * Test getServerId
     */

    UTF_CHECK_EQUAL( m1ServerId, pluginAccess -> getServerId( m1ClsId1 ) );
    UTF_CHECK_EQUAL( m1ServerId, pluginAccess -> getServerId( m1ClsId2 ) );
    UTF_CHECK_EQUAL( m1ServerId, pluginAccess -> getServerId( m1ClsId3 ) );

    UTF_CHECK_EQUAL( m2ServerId, pluginAccess -> getServerId( m2ClsId1 ) );
    UTF_CHECK_EQUAL( m2ServerId, pluginAccess -> getServerId( m2ClsId2 ) );
    UTF_CHECK_EQUAL( m2ServerId, pluginAccess -> getServerId( m2ClsId3 ) );

    /*
     * Test getPluginClassId
     */

    UTF_CHECK_EQUAL( m1PluginClsId, pluginAccess -> getPluginClassId( m1ServerId ) );

    UTF_CHECK_EQUAL( m2PluginClsId, pluginAccess -> getPluginClassId( m2ServerId ) );

    /*
     * Test getLibrary
     */

    UTF_CHECK_EQUAL( p1Path, pluginAccess -> getLibrary( m1ServerId ) );

    UTF_CHECK_EQUAL( p2Path, pluginAccess -> getLibrary( m2ServerId ) );

    /*
     * Test getPluginClassIds
     */

    std::vector< om::clsid_t > clsids;
    pluginAccess -> getPluginClassIds( clsids );

    UTF_CHECK_EQUAL( 2U, clsids.size() );
    UTF_CHECK_EQUAL( true, std::find( clsids.begin(), clsids.end(), m1PluginClsId ) != clsids.end() );
    UTF_CHECK_EQUAL( true, std::find( clsids.begin(), clsids.end(), m2PluginClsId ) != clsids.end() );

    /*
     * Test error cases
     */

    const auto unknownUuid = uuids::create();

    UTF_CHECK_THROW( pluginAccess -> getServerId( unknownUuid ), ClassNotFoundException );
    UTF_CHECK_THROW( pluginAccess -> getPluginClassId( unknownUuid ), ClassNotFoundException );
    UTF_CHECK_THROW( pluginAccess -> getLibrary( unknownUuid ), ClassNotFoundException );
}

UTF_AUTO_TEST_CASE( TestPluginAccessDuplicateRegistration )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * registerPlugin() implements two independent first-wins rules and the case above cannot
     * enter either of them, as it generates every serverid and clsid independently
     *
     * In a real deployment directory both collisions happen - a plug-in shipped twice, or two
     * vendors reusing a clsid - and which of the two libraries is eventually dlopen()-ed is
     * decided entirely here
     *
     * Note that the std::vector< std::string > constructor is used deliberately rather than
     * the std::unordered_map one - iteration order over an unordered map is unspecified, so
     * only the vector form makes "first wins" a deterministic statement
     */

    utest::TestDirectory dir;

    const std::size_t versionMajor = 1;
    const std::size_t versionMinor = 0;
    const std::size_t versionPatch = 0;

    const auto platform = Platform::get( "os", "arch", "toolchain" );
    const auto cppCompatId = uuids::create();

    const auto createPlugin = [ & ](
        SAA_in      const std::string&                      fileName,
        SAA_in      const om::serverid_t&                   serverId,
        SAA_in      std::set< om::clsid_t >&&               classIds,
        SAA_in      const om::clsid_t&                      pluginClassId
        ) -> fs::path
    {
        const auto plugin = dir.testFile( fileName );

        os::fopen( plugin, "w" );

        ManifestFactory::writeForBinary(
            Manifest::createInstance(
                serverId,
                versionMajor,
                versionMinor,
                versionPatch,
                BL_PARAM_FWD( classIds ),
                pluginClassId,
                cpp::copy( fileName ) /* pluginName */,
                cpp::copy( fileName ) /* pluginDescription */,
                false /* isClient */,
                true /* isServer */,
                om::copy( platform ),
                cppCompatId
                ),
            cpp::copy( plugin )
            );

        return plugin;
    };

    /*
     * Block A - a duplicate serverid causes the whole second plug-in to be dropped, before
     * its clsids and before its library path are recorded
     */

    {
        const auto sharedServerId = uuids::create();

        const auto a1 = uuids::create();
        const auto a2 = uuids::create();
        const auto b1 = uuids::create();

        std::set< om::clsid_t > aClsIds;
        aClsIds.insert( a1 );
        aClsIds.insert( a2 );

        std::set< om::clsid_t > bClsIds;
        bClsIds.insert( b1 );

        const auto dupA = createPlugin( "dupA", sharedServerId, std::move( aClsIds ), a1 );
        const auto dupB = createPlugin( "dupB", sharedServerId, std::move( bClsIds ), b1 );

        /*
         * Rejecting the duplicate is logged as a warning, which the test harness would
         * otherwise turn into a test failure
         */

        const auto pluginAccess = [ & ]() -> om::ObjPtr< PluginAccess >
        {
            Logging::LineLoggerPusher pushLineLogger( &utest::warningToDebugLineLogger );

            return PluginAccess::createInstance(
                std::vector< std::string >{ dupA.string(), dupB.string() }
                );
        }();

        UTF_REQUIRE_EQUAL( sharedServerId, pluginAccess -> getServerId( a1 ) );
        UTF_REQUIRE_EQUAL( sharedServerId, pluginAccess -> getServerId( a2 ) );

        UTF_REQUIRE_THROW( pluginAccess -> getServerId( b1 ), ClassNotFoundException );

        UTF_REQUIRE_EQUAL( a1, pluginAccess -> getPluginClassId( sharedServerId ) );

        /*
         * The *first* library must win - this is what proves the early return runs before
         * m_libraries.emplace(), i.e. that the clsid table and the library path cannot end
         * up pointing at two different plug-ins
         */

        UTF_REQUIRE_EQUAL( dupA.string(), pluginAccess -> getLibrary( sharedServerId ) );

        std::vector< om::clsid_t > clsids;
        pluginAccess -> getPluginClassIds( clsids );

        UTF_REQUIRE_EQUAL( 1U, clsids.size() );
    }

    /*
     * Block B - a duplicate clsid across two different serverids keeps its first owner, but
     * the rest of the second plug-in is still registered
     */

    {
        const auto serverC = uuids::create();
        const auto serverD = uuids::create();

        const auto shared = uuids::create();
        const auto c2 = uuids::create();
        const auto d2 = uuids::create();

        std::set< om::clsid_t > cClsIds;
        cClsIds.insert( shared );
        cClsIds.insert( c2 );

        std::set< om::clsid_t > dClsIds;
        dClsIds.insert( shared );
        dClsIds.insert( d2 );

        const auto dupC = createPlugin( "dupC", serverC, std::move( cClsIds ), c2 );
        const auto dupD = createPlugin( "dupD", serverD, std::move( dClsIds ), d2 );

        /*
         * Keeping the first owner of the duplicate clsid is logged as a warning as well
         */

        const auto pluginAccess = [ & ]() -> om::ObjPtr< PluginAccess >
        {
            Logging::LineLoggerPusher pushLineLogger( &utest::warningToDebugLineLogger );

            return PluginAccess::createInstance(
                std::vector< std::string >{ dupC.string(), dupD.string() }
                );
        }();

        UTF_REQUIRE_EQUAL( serverC, pluginAccess -> getServerId( shared ) );
        UTF_REQUIRE_EQUAL( serverC, pluginAccess -> getServerId( c2 ) );

        UTF_REQUIRE_EQUAL( serverD, pluginAccess -> getServerId( d2 ) );
        UTF_REQUIRE_EQUAL( dupD.string(), pluginAccess -> getLibrary( serverD ) );
        UTF_REQUIRE_EQUAL( d2, pluginAccess -> getPluginClassId( serverD ) );

        std::vector< om::clsid_t > clsids;
        pluginAccess -> getPluginClassIds( clsids );

        UTF_REQUIRE_EQUAL( 2U, clsids.size() );
    }

    /*
     * Block C - a path with no manifest at all reaches readForBinary()'s guard through this
     * constructor
     */

    UTF_REQUIRE_THROW(
        PluginAccess::createInstance( std::vector< std::string >{ dir.testFile( "orphan" ).string() } ),
        bl::UnexpectedException
        );
}
