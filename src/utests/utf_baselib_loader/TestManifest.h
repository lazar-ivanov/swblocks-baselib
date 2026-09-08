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

#include <baselib/loader/Manifest.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/JsonUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/UtfPluginFixture.h>
#include <utests/baselib/TestUtils.h>
#include <utests/baselib/Utf.h>

struct ManifestFixture
{
    utest::TestDirectory m_dir;
    utest::TestPlugin m_plugin;

    ManifestFixture()
        :
        m_dir( true /* ignoreErrors */ ),
        m_plugin()
    {
    }
};

UTF_FIXTURE_TEST_CASE( TestManifestRead, ManifestFixture )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    auto platform = Platform::get( "windows", "x64", "msvc12" );
    const auto manifest = ManifestFactory::create( m_plugin.path(), std::move( platform ) );

    UTF_CHECK_EQUAL( uuids::uuid2string( manifest -> serverId() ), "8e213524-8c75-4622-8273-6a5eeaa26250" );
    UTF_CHECK_EQUAL( manifest -> versionMajor(), 1U );
    UTF_CHECK_EQUAL( manifest -> versionMinor(), 2U );

    const auto clsids = manifest -> classIds();

    UTF_CHECK_EQUAL( clsids.size(), 2U );
    UTF_CHECK( clsids.find( uuids::string2uuid( "3fd48332-db97-42bd-9cf6-f6a332895d92" ) ) != clsids.end() );
    UTF_CHECK( clsids.find( uuids::string2uuid( "c76d0949-92a2-4925-8c9f-890f05484474" ) ) != clsids.end() );

    UTF_CHECK_EQUAL( uuids::uuid2string( manifest -> pluginClassId() ), "3fd48332-db97-42bd-9cf6-f6a332895d92" );
    UTF_CHECK_EQUAL( manifest -> pluginName(), "calculator" );
    UTF_CHECK_EQUAL( manifest -> pluginDescription(), "Simple calculator plug-in" );
    UTF_CHECK_EQUAL( manifest -> isClientPlugin(), false );
    UTF_CHECK_EQUAL( manifest -> isServerPlugin(), true );

    UTF_CHECK_EQUAL( manifest -> platform() -> os(), "windows" );
    UTF_CHECK_EQUAL( manifest -> platform() -> architecture(), "x64" );
    UTF_CHECK_EQUAL( manifest -> platform() -> toolchain(), "msvc12" );

    /*
     * These two properties are reported by the plug-in through its extern "C" registerPlugin
     * boundary, so pinning them here pins the values all the way out through the plug-in ABI
     * rather than only through the manifest codec
     */

    UTF_CHECK_EQUAL( manifest -> cppCompatibilityId(), uuids::string2uuid( BL_PLUGINS_CPP_COMPATIBILITY_ID ) );
    UTF_CHECK_EQUAL( manifest -> versionPatch(), ( std::size_t ) BL_PLUGINS_BUILD_ID );
}

UTF_FIXTURE_TEST_CASE( TestManifestUnsupportedVersionIsRejected, ManifestFixture )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    /*
     * A manifest which declares a version this reader does not know is refused rather than
     * read as if it were version 1 with its unknown properties ignored
     */

    const auto output = ( m_dir.path() / "plugin.mf" ).string();
    const auto platform = Platform::get( "linux", "x64", "gcc48" );

    ManifestFactory::write(
        ManifestFactory::create( m_plugin.path(), bl::om::copy( platform ) ),
        cpp::copy( output )
        );

    UTF_CHECK_NO_THROW( ManifestFactory::read( output ) );

    json::value value;

    {
        bl::fs::SafeInputFileStreamWrapper inputFile( output );
        auto& is = inputFile.stream();

        value = json::readFromStream( is );
    }

    auto json = cpp::copy( value.as_object() );
    json[ "manifestVersion" ] = 2;

    const auto outputV2 = ( m_dir.path() / "plugin-v2.mf" ).string();

    {
        bl::fs::SafeOutputFileStreamWrapper outputFile( outputV2 );
        auto& os = outputFile.stream();

        json::saveToStream( json::value( json ), os, true /* prettyPrint */ );
    }

    UTF_REQUIRE_THROW( ManifestFactory::read( outputV2 ), bl::UnexpectedException );
}

UTF_FIXTURE_TEST_CASE( TestManifestWrite, ManifestFixture )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    const auto output = ( m_dir.path() / "plugin.mf" ).string();
    const auto platform = Platform::get( "linux", "x64", "gcc48" );
    const auto manifest = ManifestFactory::create( m_plugin.path(), bl::om::copy( platform ) );

    ManifestFactory::write( bl::om::copy( manifest ), cpp::copy( output ) );

    json::value value;

    {
        bl::fs::SafeInputFileStreamWrapper inputFile( output );
        auto& is = inputFile.stream();

        value = json::readFromStream( is );
    }

    const auto& json = value.as_object();

    UTF_CHECK_EQUAL( json.at( "manifestVersion" ).as_int64(), 1 );
    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "serverId" ) ), "8e213524-8c75-4622-8273-6a5eeaa26250" );
    UTF_CHECK_EQUAL( json.at( "versionMajor" ).as_int64(), 1 );
    UTF_CHECK_EQUAL( json.at( "versionMinor" ).as_int64(), 2 );

    const auto& clsids = json.at( "classIds" ).as_array();

    UTF_CHECK_EQUAL( clsids.size(), 2U );

    /*
     * Check if clsids contains the expected values by iterating
     */

    bool foundClsid1 = false;
    bool foundClsid2 = false;

    for( const auto& clsid : clsids )
    {
        auto clsidStr = bl::json::value_to< std::string >( clsid );
        if( clsidStr == "3fd48332-db97-42bd-9cf6-f6a332895d92" )
        {
            foundClsid1 = true;
        }
        if( clsidStr == "c76d0949-92a2-4925-8c9f-890f05484474" )
        {
            foundClsid2 = true;
        }
    }

    UTF_CHECK( foundClsid1 );
    UTF_CHECK( foundClsid2 );

    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "pluginClassId" ) ), "3fd48332-db97-42bd-9cf6-f6a332895d92" );
    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "pluginName" ) ), std::string( "calculator" ) );
    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "pluginDescription" ) ), "Simple calculator plug-in" );
    UTF_CHECK_EQUAL( json.at( "isClientPlugin" ).as_bool(), false );
    UTF_CHECK_EQUAL( json.at( "isServerPlugin" ).as_bool(), true );

    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "os" ) ), "linux" );
    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "architecture" ) ), "x64" );
    UTF_CHECK_EQUAL( bl::json::value_to< std::string >( json.at( "toolchain" ) ), "gcc48" );
}

UTF_FIXTURE_TEST_CASE( TestManifestWriteReadRoundTrip, ManifestFixture )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    /*
     * write() and read() are inverse operations, but nothing in the suite composes them -
     * TestManifestWrite checks the JSON against a manifest and TestManifestRead checks a
     * manifest which never went through JSON, so a *symmetric* mis-keying of two same-typed
     * properties in both directions of the codec survives both cases
     *
     * This case closes them over each other and compares every single property
     */

    const auto verifyRoundTrip = [](
        SAA_in      const om::ObjPtr< Manifest >&                       original,
        SAA_in      const om::ObjPtr< Manifest >&                       reread,
        SAA_in      const bool                                          expectedIsClientPlugin,
        SAA_in      const bool                                          expectedIsServerPlugin
        ) -> void
    {
        UTF_REQUIRE_EQUAL( original -> serverId(), reread -> serverId() );

        UTF_REQUIRE_EQUAL( original -> versionMajor(), reread -> versionMajor() );
        UTF_REQUIRE_EQUAL( original -> versionMinor(), reread -> versionMinor() );
        UTF_REQUIRE_EQUAL( original -> versionPatch(), reread -> versionPatch() );
        UTF_REQUIRE_EQUAL( original -> version(), reread -> version() );

        UTF_REQUIRE( original -> classIds() == reread -> classIds() );

        UTF_REQUIRE_EQUAL( original -> pluginClassId(), reread -> pluginClassId() );
        UTF_REQUIRE_EQUAL( original -> pluginName(), reread -> pluginName() );
        UTF_REQUIRE_EQUAL( original -> pluginDescription(), reread -> pluginDescription() );

        UTF_REQUIRE_EQUAL( original -> isClientPlugin(), reread -> isClientPlugin() );
        UTF_REQUIRE_EQUAL( original -> isServerPlugin(), reread -> isServerPlugin() );

        /*
         * The two flags are also compared against the expected literals, so that a pair
         * which was swapped in both directions of the codec is caught
         */

        UTF_REQUIRE_EQUAL( reread -> isClientPlugin(), expectedIsClientPlugin );
        UTF_REQUIRE_EQUAL( reread -> isServerPlugin(), expectedIsServerPlugin );

        UTF_REQUIRE_EQUAL( original -> cppCompatibilityId(), reread -> cppCompatibilityId() );

        UTF_REQUIRE_EQUAL( original -> platform() -> os(), reread -> platform() -> os() );
        UTF_REQUIRE_EQUAL( original -> platform() -> architecture(), reread -> platform() -> architecture() );
        UTF_REQUIRE_EQUAL( original -> platform() -> toolchain(), reread -> platform() -> toolchain() );
    };

    /*
     * The manifest built from the live calculator plug-in
     */

    {
        const auto platform = Platform::get( "linux", "x64", "gcc48-release" );
        const auto original = ManifestFactory::create( m_plugin.path(), bl::om::copy( platform ) );

        const auto output = ( m_dir.path() / "roundtrip.mf" ).string();

        ManifestFactory::write( bl::om::copy( original ), cpp::copy( output ) );

        const auto reread = ManifestFactory::read( output );

        verifyRoundTrip(
            original,
            reread,
            false       /* expectedIsClientPlugin */,
            true        /* expectedIsServerPlugin */
            );
    }

    /*
     * A hand-built manifest whose properties are all deliberately distinct values, and whose
     * two boolean flags are the inverse of the calculator's
     */

    {
        const auto serverId = uuids::create();
        const auto pluginClassId = uuids::create();
        const auto cppCompatibilityId = uuids::create();

        std::set< om::clsid_t > clsids;
        clsids.insert( uuids::create() );
        clsids.insert( uuids::create() );

        const auto original = Manifest::createInstance(
            serverId,
            7 /* versionMajor */,
            8 /* versionMinor */,
            9 /* versionPatch */,
            std::move( clsids ),
            pluginClassId,
            "round-trip plug-in name",
            "round-trip plug-in description",
            true /* isClient */,
            false /* isServer */,
            Platform::get( "os1", "arch2", "tc3-flavor4" ),
            cppCompatibilityId
            );

        const auto output = ( m_dir.path() / "roundtrip-hand-built.mf" ).string();

        ManifestFactory::write( bl::om::copy( original ), cpp::copy( output ) );

        const auto reread = ManifestFactory::read( output );

        verifyRoundTrip(
            original,
            reread,
            true        /* expectedIsClientPlugin */,
            false       /* expectedIsServerPlugin */
            );

        /*
         * Every value above is distinct, so pin each of them explicitly as well - a
         * symmetric mis-keying would round-trip cleanly but land the wrong value in each
         * of the two properties which were swapped
         */

        UTF_REQUIRE_EQUAL( reread -> serverId(), serverId );
        UTF_REQUIRE_EQUAL( reread -> pluginClassId(), pluginClassId );
        UTF_REQUIRE_EQUAL( reread -> cppCompatibilityId(), cppCompatibilityId );

        UTF_REQUIRE_EQUAL( reread -> versionMajor(), 7U );
        UTF_REQUIRE_EQUAL( reread -> versionMinor(), 8U );
        UTF_REQUIRE_EQUAL( reread -> versionPatch(), 9U );
        UTF_REQUIRE_EQUAL( reread -> version(), std::string( "7.8.9" ) );

        UTF_REQUIRE_EQUAL( reread -> pluginName(), std::string( "round-trip plug-in name" ) );
        UTF_REQUIRE_EQUAL( reread -> pluginDescription(), std::string( "round-trip plug-in description" ) );

        UTF_REQUIRE_EQUAL( reread -> platform() -> os(), std::string( "os1" ) );
        UTF_REQUIRE_EQUAL( reread -> platform() -> architecture(), std::string( "arch2" ) );
        UTF_REQUIRE_EQUAL( reread -> platform() -> toolchain(), std::string( "tc3-flavor4" ) );
    }
}

UTF_FIXTURE_TEST_CASE( TestManifestMissingOrNullPropertyIsRejected, ManifestFixture )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    /*
     * Every one of the 15 properties read() interprets is required, and getRequiredProperty()
     * rejects a missing one and a null one with the same user-friendly message; nothing in
     * the suite removes a property, nulls one out, or gives one the wrong JSON type
     */

    const auto good = ( m_dir.path() / "good.mf" ).string();

    ManifestFactory::write(
        ManifestFactory::create( m_plugin.path(), Platform::get( "linux", "x64", "gcc48" ) ),
        cpp::copy( good )
        );

    UTF_REQUIRE_NO_THROW( ManifestFactory::read( good ) );

    json::value value;

    {
        bl::fs::SafeInputFileStreamWrapper inputFile( good );
        auto& is = inputFile.stream();

        value = json::readFromStream( is );
    }

    const auto sourceObject = cpp::copy( value.as_object() );

    const auto saveObject = [ this ](
        SAA_in      const std::string&              fileName,
        SAA_in      const json::object&             object
        ) -> std::string
    {
        const auto path = ( m_dir.path() / fileName ).string();

        {
            bl::fs::SafeOutputFileStreamWrapper outputFile( path );
            auto& os = outputFile.stream();

            json::saveToStream( json::value( object ), os, true /* prettyPrint */ );
        }

        return path;
    };

    const std::vector< std::string > required
    {
        "manifestVersion",
        "serverId",
        "versionMajor",
        "versionMinor",
        "versionPatch",
        "classIds",
        "pluginClassId",
        "pluginName",
        "pluginDescription",
        "isClientPlugin",
        "isServerPlugin",
        "os",
        "architecture",
        "toolchain",
        "cppCompatibilityId",
    };

    UTF_REQUIRE_EQUAL( required.size(), sourceObject.size() );

    for( const auto& name : required )
    {
        /*
         * The missing variant - rebuild the object copying every pair except the one under
         * test; the pairs are iterated through the portable macros rather than erased,
         * because the shape of erase() differs between the two JSON backends
         */

        {
            json::object mutated;

            for( const auto& pair : sourceObject )
            {
                const auto key = std::string( BL_JSON_PAIR_KEY( pair ) );

                if( key == name )
                {
                    continue;
                }

                mutated[ key ] = BL_JSON_PAIR_VALUE( pair );
            }

            UTF_REQUIRE_EQUAL( mutated.size() + 1U, sourceObject.size() );

            const auto path = saveObject( "missing-" + name + ".mf", mutated );

            /*
             * The absence of manifestVersion is caught by getRequiredProperty() before the
             * version value is ever compared, so it yields the same message as the rest;
             * it is matched on the shorter substring only because the message for it is
             * produced from the very same code path
             */

            UTF_REQUIRE_THROW_MESSAGE(
                ManifestFactory::read( path ),
                bl::UnexpectedException,
                name == "manifestVersion" ?
                    std::string( "does not contain required property" )
                    :
                    "does not contain required property '" + name + "'"
                );
        }

        /*
         * The null variant
         */

        {
            auto mutated = cpp::copy( sourceObject );
            mutated[ name ] = json::value();

            const auto path = saveObject( "null-" + name + ".mf", mutated );

            UTF_REQUIRE_THROW_MESSAGE(
                ManifestFactory::read( path ),
                bl::UnexpectedException,
                name == "manifestVersion" ?
                    std::string( "does not contain required property" )
                    :
                    "does not contain required property '" + name + "'"
                );
        }
    }

    /*
     * readForBinary() pre-checks that the manifest exists, so that a binary without one
     * yields a clear message rather than a confusing stream-open failure
     */

    UTF_REQUIRE_THROW_MESSAGE(
        ManifestFactory::readForBinary( m_dir.testFile( "no-such-binary" ) ),
        bl::UnexpectedException,
        "Manifest file does not exist"
        );

    /*
     * Structurally wrong documents
     *
     * This block records the CURRENT behavior and is deliberately *not* an endorsement of
     * it - read() calls value.as_object() with no precondition check, and likewise reaches
     * .as_array() on classIds and .as_bool() on the two flags, so all of these escape as a
     * raw JSON backend exception rather than as the user-friendly bl::UnexpectedException
     * every other malformed manifest yields
     *
     * The exception type is therefore deliberately not pinned - it differs between the two
     * supported JSON backends - only the fact that read() throws rather than crashing
     */

    const auto saveText = [ this ](
        SAA_in      const std::string&              fileName,
        SAA_in      const std::string&              text
        ) -> std::string
    {
        const auto path = ( m_dir.path() / fileName ).string();

        {
            bl::fs::SafeOutputFileStreamWrapper outputFile( path );
            auto& os = outputFile.stream();

            os << text;
        }

        return path;
    };

    UTF_REQUIRE_THROW( ManifestFactory::read( saveText( "array.mf", "[ 1, 2, 3 ]" ) ), std::exception );
    UTF_REQUIRE_THROW( ManifestFactory::read( saveText( "string.mf", "\"not a manifest\"" ) ), std::exception );
    UTF_REQUIRE_THROW( ManifestFactory::read( saveText( "number.mf", "42" ) ), std::exception );

    {
        auto mutated = cpp::copy( sourceObject );
        mutated[ "classIds" ] = json::value( std::string( "abc" ) );

        UTF_REQUIRE_THROW(
            ManifestFactory::read( saveObject( "wrong-type-classIds.mf", mutated ) ),
            std::exception
            );
    }

    {
        auto mutated = cpp::copy( sourceObject );
        mutated[ "isClientPlugin" ] = 1;

        UTF_REQUIRE_THROW(
            ManifestFactory::read( saveObject( "wrong-type-isClientPlugin.mf", mutated ) ),
            std::exception
            );
    }
}

UTF_AUTO_TEST_CASE( TestToolchainMatch )
{
    const auto platform = bl::loader::Platform::get( "windows", "x64", "vc12-release" );

    UTF_CHECK_EQUAL( platform -> toolchain(), "vc12-release" );

    UTF_CHECK_EQUAL( platform -> name(), "windows_x64_vc12-release" );
    UTF_CHECK_EQUAL( platform -> name( true /* ignoreCompilerId */ ), "windows_x64_release" );

    UTF_CHECK( platform -> matchesToolchain( "vc12-release", false /* ignoreCompilerId */ ) );
    UTF_CHECK( ! platform -> matchesToolchain( "vc13-release", false /* ignoreCompilerId */ ) );
    UTF_CHECK( platform -> matchesToolchain( "vc13-release", true /* ignoreCompilerId */ ) );
    UTF_CHECK( ! platform -> matchesToolchain( "vc12-debug", true /* ignoreCompilerId */ ) );
}
