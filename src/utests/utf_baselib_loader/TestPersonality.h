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

#include <baselib/loader/Personality.h>
#include <baselib/loader/Manifest.h>

#include <baselib/core/Uuid.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/Utf.h>

#include <set>

struct PersonalityTestFixture
{
    const utest::TestDirectory                  m_dir;

    const bl::fs::path                          m_plugin1;
    const bl::fs::path                          m_plugin2;
    const bl::fs::path                          m_plugin3;
    const bl::fs::path                          m_plugin4;

    bl::om::ObjPtr< bl::loader::Manifest >     m_mf1;
    bl::om::ObjPtr< bl::loader::Manifest >     m_mf2;
    bl::om::ObjPtr< bl::loader::Manifest >     m_mf3;
    bl::om::ObjPtr< bl::loader::Manifest >     m_mf4;

    PersonalityTestFixture()
        :
        m_plugin1( m_dir.testFile( "plugin1" ) ),
        m_plugin2( m_dir.testFile( "plugin2" ) ),
        m_plugin3( m_dir.testFile( "plugin3" ) ),
        m_plugin4( m_dir.testFile( "plugin4" ) )
    {
        using namespace bl;
        using namespace bl::loader;

        /*
         * Create dummy plug-in libraries
         */

        os::fopen( m_plugin1, "w" );
        os::fopen( m_plugin2, "w" );
        os::fopen( m_plugin3, "w" );
        os::fopen( m_plugin4, "w" );

        /*
         * Common plug-in properties
         */

        const std::size_t versionMajor = 1;
        const std::size_t versionMinor = 0;
        const std::size_t versionPatch = 0;

        const auto platform = Platform::get( "os", "arch", "toolchain" );
        const auto cppCompatId = uuids::create();

        /*
         * Plug-in 1
         */

        const auto m1ServerId = uuids::create();
        const auto m1PluginClsId = uuids::create();

        std::set< om::clsid_t > m1ClsIds;
        m1ClsIds.insert( m1PluginClsId );

        m_mf1 = Manifest::createInstance(
            m1ServerId,
            versionMajor,
            versionMinor,
            versionPatch,
            std::move( m1ClsIds ),
            m1PluginClsId,
            "plug-in 1 name",
            "plug-in 1 description",
            true /* isClient */,
            false /* isServer */,
            om::copy( platform ),
            cppCompatId
            );

        /*
         * Plug-in 2
         */

        const auto m2ServerId = uuids::create();
        const auto m2PluginClsId = uuids::create();

        std::set< om::clsid_t > m2ClsIds;
        m2ClsIds.insert( m2PluginClsId );

        m_mf2 = Manifest::createInstance(
            m2ServerId,
            versionMajor,
            versionMinor,
            versionPatch,
            std::move( m2ClsIds ),
            m2PluginClsId,
            "plug-in 2 name",
            "plug-in 2 description",
            false /* isClient */,
            true /* isServer */,
            om::copy( platform ),
            cppCompatId
            );

        /*
         * Plug-in 3
         */

        const auto m3ServerId = uuids::create();
        const auto m3PluginClsId = uuids::create();

        std::set< om::clsid_t > m3ClsIds;
        m3ClsIds.insert( m3PluginClsId );

        m_mf3 = Manifest::createInstance(
            m3ServerId,
            versionMajor,
            versionMinor,
            versionPatch,
            std::move( m3ClsIds ),
            m3PluginClsId,
            "plug-in 3 name",
            "plug-in 3 description",
            true /* isClient */,
            true /* isServer */,
            om::copy( platform ),
            cppCompatId
            );

        /*
         * Plug-in 4 - neither a client nor a server plug-in
         *
         * initPlugins() runs two independent 'if's, so a manifest which sets neither flag
         * must land in neither map; it exists here so that direction is pinned as well as
         * the both-flags direction which plug-in 3 already covers
         */

        const auto m4ServerId = uuids::create();
        const auto m4PluginClsId = uuids::create();

        std::set< om::clsid_t > m4ClsIds;
        m4ClsIds.insert( m4PluginClsId );

        m_mf4 = Manifest::createInstance(
            m4ServerId,
            versionMajor,
            versionMinor,
            versionPatch,
            std::move( m4ClsIds ),
            m4PluginClsId,
            "plug-in 4 name",
            "plug-in 4 description",
            false /* isClient */,
            false /* isServer */,
            om::copy( platform ),
            cppCompatId
            );

        /*
         * Create plug-in manifests
         */

        ManifestFactory::writeForBinary( om::copy( m_mf1 ), cpp::copy( m_plugin1 ) );
        ManifestFactory::writeForBinary( om::copy( m_mf2 ), cpp::copy( m_plugin2 ) );
        ManifestFactory::writeForBinary( om::copy( m_mf3 ), cpp::copy( m_plugin3 ) );
        ManifestFactory::writeForBinary( om::copy( m_mf4 ), cpp::copy( m_plugin4 ) );
    }
};

UTF_FIXTURE_TEST_CASE( TestPersonalityFileSystem, PersonalityTestFixture )
{
    using namespace utest;
    using namespace bl;
    using namespace bl::loader;

    /*
     * Test client and server plug-in accessors
     */

    const auto personality = PersonalityFileSystem::createInstance< Personality >( m_dir.path() );

    const auto& serverPlugins = personality -> getServerPlugins();
    const auto& clientPlugins = personality -> getClientPlugins();

    UTF_CHECK_EQUAL( 2U, clientPlugins.size() );
    UTF_CHECK_EQUAL( 2U, serverPlugins.size() );

    {
        const auto entry = clientPlugins.find( m_plugin1 );
        UTF_CHECK_EQUAL( true, entry != clientPlugins.end() );
        UTF_CHECK_EQUAL( m_mf1 -> serverId(), entry -> second -> serverId() );
    }

    {
        const auto entry = clientPlugins.find( m_plugin3 );
        UTF_CHECK_EQUAL( true, entry != clientPlugins.end() );
        UTF_CHECK_EQUAL( m_mf3 -> serverId(), entry -> second -> serverId() );
    }

    {
        const auto entry = serverPlugins.find( m_plugin2 );
        UTF_CHECK_EQUAL( true, entry != serverPlugins.end() );
        UTF_CHECK_EQUAL( m_mf2 -> serverId(), entry -> second -> serverId() );
    }

    {
        const auto entry = serverPlugins.find( m_plugin3 );
        UTF_CHECK_EQUAL( true, entry != serverPlugins.end() );
        UTF_CHECK_EQUAL( m_mf3 -> serverId(), entry -> second -> serverId() );
    }

    /*
     * The neither-client-nor-server plug-in is discovered by findPlugins() and read by
     * initPlugins(), but it must not be filed in either map - which is what makes the two
     * 'if's independent rather than an if / else
     */

    UTF_CHECK( clientPlugins.find( m_plugin4 ) == clientPlugins.end() );
    UTF_CHECK( serverPlugins.find( m_plugin4 ) == serverPlugins.end() );

    UTF_CHECK_EQUAL( false, personality -> checkForUpdate() );
}

UTF_AUTO_TEST_CASE( TestPersonalityFileSystemDiscoveryBranches )
{
    using namespace utest;
    using namespace bl;
    using namespace bl::loader;

    /*
     * The four discovery branches which the populated-directory case above never reaches
     *
     * The empty-directory branch is the only path on which initPlugins() is not called at
     * all, and it is the one a fresh install hits, so it must warn rather than throw
     */

    const TestDirectory dir;

    /*
     * (1) The directory does not exist
     */

    UTF_REQUIRE_THROW_MESSAGE(
        PersonalityFileSystem::createInstance< Personality >( dir.testFile( "no-such-dir" ) ),
        bl::UnexpectedException,
        "does not exist"
        );

    /*
     * (2) The path exists, but it is a regular file rather than a directory - note that the
     * message is the same "does not exist" text, which is pinned here deliberately
     */

    {
        const auto afile = dir.testFile( "afile" );

        os::fopen( afile, "w" );

        UTF_REQUIRE_THROW_MESSAGE(
            PersonalityFileSystem::createInstance< Personality >( afile ),
            bl::UnexpectedException,
            "does not exist"
            );
    }

    /*
     * (3) An empty directory - findPlugins() returns nothing, initPlugins() is skipped and
     * the personality is constructed successfully with both maps empty
     */

    {
        const auto empty = dir.testFile( "empty" );

        fs::safeMkdirs( empty );

        /*
         * The constructor logs "No plug-ins found" at warning level, which the module's
         * line logger would otherwise turn into a test failure
         */

        Logging::LineLoggerPusher pushLineLogger( &warningToDebugLineLogger );

        const auto p = PersonalityFileSystem::createInstance< Personality >( empty );

        UTF_REQUIRE( p );
        UTF_CHECK( p -> getClientPlugins().empty() );
        UTF_CHECK( p -> getServerPlugins().empty() );
        UTF_CHECK_EQUAL( false, p -> checkForUpdate() );
    }

    /*
     * (4) A directory whose entries have no manifests next to them - isPlugin() rejects
     * both, so nothing is read and readForBinary() is never given a chance to throw
     */

    {
        const auto nomf = dir.testFile( "nomf" );

        fs::safeMkdirs( nomf );

        os::fopen( nomf / "afile", "w" );
        os::fopen( nomf / "another", "w" );

        Logging::LineLoggerPusher pushLineLogger( &warningToDebugLineLogger );

        const auto p = PersonalityFileSystem::createInstance< Personality >( nomf );

        UTF_REQUIRE( p );
        UTF_CHECK( p -> getClientPlugins().empty() );
        UTF_CHECK( p -> getServerPlugins().empty() );
        UTF_CHECK_EQUAL( false, p -> checkForUpdate() );
    }
}
