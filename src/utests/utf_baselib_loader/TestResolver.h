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

#include <utf/plugins/calculator/Calculator.h>

#include <baselib/loader/Manifest.h>
#include <baselib/loader/Platform.h>
#include <baselib/loader/PluginAccess.h>
#include <baselib/loader/Resolver.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/LoggerUtils.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/UtfPluginFixture.h>
#include <utests/baselib/TestUtils.h>

#include <unordered_map>

struct ResolverFixture
{
    typedef ResolverFixture this_type;

    const utest::TestDirectory      m_dir;
    const utest::TestPlugin         m_plugin;

    ResolverFixture()
        :
        m_dir( true /* ignoreErrors */ ),
        m_plugin()
    {
    }
};

/**
 * @brief A locally counted object used to observe the reference counting contract of
 * ResolverImplT::getCoreServices / setCoreServices
 *
 * Every type the resolver component itself instantiates is declared om::detail::LttLeaked
 * and is therefore excluded from om::outstandingObjectRefs(), so the assertions in the core
 * services case below are made against this counter instead
 */

template
<
    typename E = void
>
class CoreServicesTestObjT : public bl::om::Object
{
    BL_DECLARE_OBJECT_IMPL_ONEIFACE_NO_DESTRUCTOR( CoreServicesTestObjT, bl::om::Object )

protected:

    CoreServicesTestObjT() NOEXCEPT
    {
        ++g_live;
    }

    ~CoreServicesTestObjT() NOEXCEPT
    {
        --g_live;
    }

public:

    static long g_live;
};

template
<
    typename E
>
long
CoreServicesTestObjT< E >::g_live = 0L;

typedef bl::om::ObjectImpl< CoreServicesTestObjT<> > CoreServicesTestObj;

UTF_AUTO_TEST_CASE( TestResolver_CheckFailure )
{
    using namespace bl;
    using namespace bl::loader;

    UTF_CHECK_EQUAL( 0L, om::outstandingObjectRefs() );

    {
        auto manifest = Manifest::createInstance(
            uuids::create() /* serverid */,
            1 /* versionMajor */,
            0 /* versionMinor */,
            0 /* versionPatch */,
            std::set< om::clsid_t >() /* clsids */,
            uuids::nil() /* pluginClassId */,
            "name",
            "description",
            false /* isClient */,
            true /* isServer */,
            Platform::get( "os", "arch", "toolchain" ),
            uuids::create() /* cppCompatibilityId */
            );

        std::unordered_map< fs::path, om::ObjPtr< Manifest > > plugins;
        plugins.emplace( fs::path( "plugin" ), std::move( manifest ) );

        const auto resolver = ResolverImplDefault::createInstance< om::Resolver >(
            PluginAccess::createInstance( plugins )
            );

        /*
         * Verify failure when server cannot be found
         */

        const auto unknownClassId = uuids::create();
        const auto unknownServerId = uuids::create();

        {
            Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

            om::serverid_t serverid;
            const auto rc = resolver -> resolveServer( unknownClassId, serverid );

            UTF_CHECK( rc != 0 );
        }

        /*
         * Verify failure when requested server cannot / has not be loaded
         */

        {
            Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

            om::objref_t factoryRef;
            const auto rc = resolver -> getFactory( unknownServerId, factoryRef, true );

            UTF_CHECK( rc != 0 );
        }

        {
            Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

            om::objref_t factoryRef;
            const auto rc = resolver -> getFactory( unknownServerId, factoryRef, false );

            UTF_CHECK( rc != 0 );
        }
    }

    UTF_CHECK_EQUAL( 0L, om::outstandingObjectRefs() );
}

UTF_FIXTURE_TEST_CASE( TestResolver_CheckSuccess, ResolverFixture )
{
    using namespace bl;
    using namespace bl::loader;

    UTF_CHECK_EQUAL( 0L, om::outstandingObjectRefs() );

    {
        /*
         * Create manifest and plug-in access for calculator plug-in built for fixture
         */

        ManifestFactory::createAndWriteForLibrary(
            m_plugin.path(),
            Platform::get( "windows", "x64", "msvc12" )
            );

        std::vector< std::string > plugins;
        plugins.push_back( m_plugin.path() );

        const auto resolver = ResolverImplDefault::createInstance< om::Resolver >(
            PluginAccess::createInstance( plugins )
            );

        /*
         * Actual calculator plug-in info
         */

        const auto iid = iids::Calculator();
        const auto clsid = clsids::CalculatorObj();
        const auto serverid = uuids::string2uuid( "8e213524-8c75-4622-8273-6a5eeaa26250" );

        /*
         * Verify successful server resolution
         */

        {
            om::serverid_t serveridOut;
            const auto rc = resolver -> resolveServer( clsid, serveridOut );

            UTF_CHECK_EQUAL( rc, 0 );
            UTF_CHECK_EQUAL( serverid, serveridOut );
        }

        /*
         * Verify first-time factory look-up fails if not previously loaded
         */

        {
            Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

            om::objref_t factoryRefOut;
            const auto rc = resolver -> getFactory( serverid, factoryRefOut, true );

            UTF_CHECK( rc != 0 );
        }

        /*
         * Verify successful factory resolution
         *
         * Note that the reference handed back by getFactory() is owned by the caller - the
         * resolver keeps its own cached one - so it has to be wrapped and released here just
         * as it is in the block below, otherwise a missing addRef() in getFactory() would be
         * masked rather than detected
         */

        {
            om::objref_t factoryRefOut = nullptr;
            const auto rc = resolver -> getFactory( serverid, factoryRefOut, false );

            UTF_CHECK( rc == 0 );

            const auto factory = om::wrap< om::Factory >( factoryRefOut );

            UTF_CHECK( factory );
        }

        /*
         * Verify subsequent factory look-up is successful when previously loaded
         */

        {
            om::objref_t factoryRefOut;
            const auto rc = resolver -> getFactory( serverid, factoryRefOut, true );

            UTF_CHECK( rc == 0 );

            /*
             * Test execution of code from plug-in binary
             */

            const auto factory = om::wrap< om::Factory >( factoryRefOut );
            const auto instance = factory -> createInstance( clsid, iid );
            const auto calc = om::wrap< utest::plugins::Calculator >( instance );

            UTF_CHECK_EQUAL( 5, calc -> add( 2, 3 ) );
            UTF_CHECK_EQUAL( 3, calc -> subtract( 7, 4 ) );
        }
    }

    UTF_CHECK_EQUAL( 0L, om::outstandingObjectRefs() );
}

UTF_AUTO_TEST_CASE( TestResolver_CoreServices )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * This is a raw pointer ownership contract across the plug-in ABI and both plausible
     * regressions are silent in-process and fatal out-of-process - dropping the addRef() in
     * getCoreServices() makes every host which wraps the result destroy the core services
     * object out from under the resolver, and storing a non-owning raw pointer in
     * setCoreServices() leaves the resolver with a dangling pointer as soon as the caller's
     * ObjPtr dies
     *
     * The case deliberately uses no fixture - it never loads the plug-in library, so it
     * cannot interact with the loader reset in ~TestPluginT
     */

    auto manifest = Manifest::createInstance(
        uuids::create() /* serverid */,
        1 /* versionMajor */,
        0 /* versionMinor */,
        0 /* versionPatch */,
        std::set< om::clsid_t >() /* clsids */,
        uuids::nil() /* pluginClassId */,
        "name",
        "description",
        false /* isClient */,
        true /* isServer */,
        Platform::get( "os", "arch", "toolchain" ),
        uuids::create() /* cppCompatibilityId */
        );

    std::unordered_map< fs::path, om::ObjPtr< Manifest > > plugins;
    plugins.emplace( fs::path( "plugin" ), std::move( manifest ) );

    const auto resolver = ResolverImplDefault::createInstance< om::Resolver >(
        PluginAccess::createInstance( plugins )
        );

    /*
     * Nothing has been set yet - the out parameter must be written as nullptr and the call
     * must still report success
     */

    om::objref_t coreServicesRef = nullptr;

    UTF_REQUIRE_EQUAL( resolver -> getCoreServices( coreServicesRef ), 0 );
    UTF_REQUIRE( nullptr == coreServicesRef );

    UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 0L );

    /*
     * om::detail::ppv() makes the objref_t without an extra addRef, so the only reference
     * the resolver can be holding once the scope below closes is one it took itself
     */

    {
        const auto services = CoreServicesTestObj::createInstance();

        UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 1L );

        UTF_REQUIRE_EQUAL( resolver -> setCoreServices( om::detail::ppv( services.get() ) ), 0 );
    }

    UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 1L );

    /*
     * getCoreServices() hands back a reference which the *caller* owns, so wrapping it -
     * om::wrap attaches rather than takes an extra reference - must not destroy the object;
     * repeat it a few times, since a missing addRef() would only be visible once the
     * resolver's own reference has been consumed
     */

    for( std::size_t i = 0U; i < 3U; ++i )
    {
        UTF_REQUIRE_EQUAL( resolver -> getCoreServices( coreServicesRef ), 0 );
        UTF_REQUIRE( nullptr != coreServicesRef );

        {
            const auto wrapped = om::wrap< om::Object >( coreServicesRef );

            UTF_REQUIRE( wrapped );
            UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 1L );
        }

        UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 1L );
    }

    /*
     * A null argument clears the slot rather than crashing, and releases the resolver's own
     * reference
     */

    UTF_REQUIRE_EQUAL( resolver -> setCoreServices( nullptr ), 0 );
    UTF_REQUIRE_EQUAL( CoreServicesTestObj::g_live, 0L );

    UTF_REQUIRE_EQUAL( resolver -> getCoreServices( coreServicesRef ), 0 );
    UTF_REQUIRE( nullptr == coreServicesRef );
}

UTF_AUTO_TEST_CASE( TestResolver_RegisterHost )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * registerHost() is how a host process publishes its own classes into the same
     * resolution namespace as its plug-ins - it reads '<current executable>.mf', fans the
     * manifest's whole classIds() set out into the server map and registers the loader's
     * default factory for the host serverid, which is what makes the host resolvable
     * *without* any dlopen()
     *
     * Note that this case writes a file next to the test executable and there is no way to
     * redirect that; unlike TestResolver_CheckSuccess it therefore has to clean up after
     * itself, because a stale manifest left behind would make the negative half of the next
     * run fail
     */

    const auto exePath = fs::path( os::getCurrentExecutablePath() );
    const auto mfPath = Manifest::getManifestPath( exePath );

    UTF_REQUIRE( ! fs::path_exists( mfPath ) );

    BL_SCOPE_EXIT(
        {
            fs::safeRemoveIfExists( mfPath );
        }
        );

    const auto pluginServerId = uuids::create();
    const auto pluginClsid = uuids::create();

    const auto hostServerId = uuids::create();
    const auto hostClsidA = uuids::create();
    const auto hostClsidB = uuids::create();

    const auto createResolver = [ & ]( SAA_in std::set< om::clsid_t >&& pluginClsids )
        -> om::ObjPtr< om::Resolver >
    {
        auto manifest = Manifest::createInstance(
            pluginServerId,
            1 /* versionMajor */,
            0 /* versionMinor */,
            0 /* versionPatch */,
            BL_PARAM_FWD( pluginClsids ),
            uuids::nil() /* pluginClassId */,
            "plug-in name",
            "plug-in description",
            false /* isClient */,
            true /* isServer */,
            Platform::get( "os", "arch", "toolchain" ),
            uuids::create() /* cppCompatibilityId */
            );

        std::unordered_map< fs::path, om::ObjPtr< Manifest > > plugins;
        plugins.emplace( fs::path( "plugin" ), std::move( manifest ) );

        return ResolverImplDefault::createInstance< om::Resolver >(
            PluginAccess::createInstance( plugins )
            );
    };

    std::set< om::clsid_t > pluginOnlyClsids;
    pluginOnlyClsids.insert( pluginClsid );

    const auto resolver = createResolver( std::move( pluginOnlyClsids ) );

    /*
     * The negative half first - with no manifest next to the executable readForBinary()
     * throws and eh::EcUtils::getErrorCode() converts that into a non-zero return
     */

    {
        Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

        UTF_REQUIRE( resolver -> registerHost() != 0 );
    }

    std::set< om::clsid_t > hostClsids;
    hostClsids.insert( hostClsidA );
    hostClsids.insert( hostClsidB );

    ManifestFactory::writeForBinary(
        Manifest::createInstance(
            hostServerId,
            1 /* versionMajor */,
            0 /* versionMinor */,
            0 /* versionPatch */,
            std::move( hostClsids ),
            uuids::nil() /* pluginClassId */,
            "host name",
            "host description",
            false /* isClient */,
            true /* isServer */,
            Platform::get( "os", "arch", "toolchain" ),
            uuids::create() /* cppCompatibilityId */
            ),
        cpp::copy( exePath )
        );

    UTF_REQUIRE_EQUAL( resolver -> registerHost(), 0 );

    /*
     * The whole classIds() set is registered, not just the plug-in clsid
     */

    {
        om::serverid_t serverid;

        UTF_REQUIRE_EQUAL( resolver -> resolveServer( hostClsidA, serverid ), 0 );
        UTF_REQUIRE_EQUAL( hostServerId, serverid );

        UTF_REQUIRE_EQUAL( resolver -> resolveServer( hostClsidB, serverid ), 0 );
        UTF_REQUIRE_EQUAL( hostServerId, serverid );
    }

    /*
     * The default factory was cached, so the host serverid resolves with onlyIfLoaded
     */

    {
        om::objref_t factoryRef = nullptr;

        UTF_REQUIRE_EQUAL( resolver -> getFactory( hostServerId, factoryRef, true /* onlyIfLoaded */ ), 0 );
        UTF_REQUIRE( nullptr != factoryRef );

        const auto factory = om::wrap< om::Factory >( factoryRef );

        UTF_REQUIRE( factory );
    }

    /*
     * The unrelated plug-in clsid still resolves through PluginAccess
     */

    {
        om::serverid_t serverid;

        UTF_REQUIRE_EQUAL( resolver -> resolveServer( pluginClsid, serverid ), 0 );
        UTF_REQUIRE_EQUAL( pluginServerId, serverid );
    }

    /*
     * A second call is idempotent and changes nothing, which is what emplace() rather than
     * insert_or_assign() guarantees
     */

    UTF_REQUIRE_EQUAL( resolver -> registerHost(), 0 );

    {
        om::serverid_t serverid;

        UTF_REQUIRE_EQUAL( resolver -> resolveServer( hostClsidA, serverid ), 0 );
        UTF_REQUIRE_EQUAL( hostServerId, serverid );

        UTF_REQUIRE_EQUAL( resolver -> resolveServer( pluginClsid, serverid ), 0 );
        UTF_REQUIRE_EQUAL( pluginServerId, serverid );
    }

    /*
     * The precedence sub-block - a clsid which the host manifest also claims, but which was
     * already cached against a plug-in, must keep pointing at the plug-in; a host manifest
     * is not allowed to hijack clsids which are already bound
     */

    {
        std::set< om::clsid_t > overlappingClsids;
        overlappingClsids.insert( pluginClsid );
        overlappingClsids.insert( hostClsidA );

        const auto resolverWithOverlap = createResolver( std::move( overlappingClsids ) );

        om::serverid_t serverid;

        UTF_REQUIRE_EQUAL( resolverWithOverlap -> resolveServer( hostClsidA, serverid ), 0 );
        UTF_REQUIRE_EQUAL( pluginServerId, serverid );

        UTF_REQUIRE_EQUAL( resolverWithOverlap -> registerHost(), 0 );

        UTF_REQUIRE_EQUAL( resolverWithOverlap -> resolveServer( hostClsidA, serverid ), 0 );
        UTF_REQUIRE_EQUAL( pluginServerId, serverid );
    }
}

namespace
{
    /**
     * @brief Makes a private copy of the calculator plug-in library and returns its path
     *
     * bl::om::detail::GlobalInitT<>::g_loader is a hidden visibility static member of a class
     * template, so every loaded shared object carries its *own* class registry, and
     * ResolverImplT::loadFactory deliberately leaks the library handle, so a plug-in is never
     * unloaded. Together that means the generated registerResolver entry point of any one
     * loaded library file can only ever succeed once per process - and the reset which
     * utest::TestPluginT's destructor performs cannot help, because it resets the *host's*
     * loader rather than the registry the plug-in registered into.
     *
     * TestResolver_CheckSuccess above claims that single successful registration for
     * utf-baselib-plugin itself, so a case which needs one of its own has to load a distinct
     * file: a byte copy has its own inode and no SONAME, so dlopen maps it as an independent
     * object with an independent registry.
     *
     * The copy stays loaded for the lifetime of the process and therefore cannot be deleted,
     * which is exactly why ResolverFixture constructs its TestDirectory with ignoreErrors
     */

    bl::fs::path copyOfCalculatorPlugin(
        SAA_in      const utest::TestDirectory&                     dir,
        SAA_in      const std::string&                              sourcePath,
        SAA_in      const std::string&                              targetName
        )
    {
        const auto target = dir.testFile( targetName );

        bl::fs::copy_file( bl::fs::path( sourcePath ), target );

        UTF_REQUIRE( bl::fs::path_exists( target ) );

        return target;
    }

} // __unnamed

UTF_FIXTURE_TEST_CASE( TestResolver_RegisterResolverReportsErrorCode, ResolverFixture )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * BL_PLUGINS_REGISTER_PLUGIN wraps the generated extern "C" registerResolver entry point
     * in eh::EcUtils::getErrorCode for one reason only - an exception must never unwind
     * through a C linkage function across a shared library boundary
     *
     * Asking the same library to register itself twice is the cheapest way to raise one
     * inside it, because om::registerClass refuses an already registered clsid; what this
     * case asserts is that the failure comes back as a non-zero error code, that no factory
     * is produced, and that the process survives - the last being the actual proof that
     * nothing unwound through the C linkage function
     */

    const auto library = copyOfCalculatorPlugin(
        m_dir,
        m_plugin.path(),
        "utf-baselib-plugin-errorcode.so"
        );

    ManifestFactory::createAndWriteForLibrary(
        fs::path( library ),
        Platform::get( "windows", "x64", "msvc12" )
        );

    std::vector< std::string > plugins;
    plugins.push_back( library.string() );

    const auto serverid = uuids::string2uuid( "8e213524-8c75-4622-8273-6a5eeaa26250" );

    const auto resolverA = ResolverImplDefault::createInstance< om::Resolver >(
        PluginAccess::createInstance( plugins )
        );

    om::objref_t f1 = nullptr;

    UTF_REQUIRE_EQUAL( 0, resolverA -> getFactory( serverid, f1, false /* onlyIfLoaded */ ) );
    UTF_REQUIRE( nullptr != f1 );

    {
        const auto factoryA = om::wrap< om::Factory >( f1 );

        UTF_REQUIRE( factoryA );
    }

    /*
     * A second, independent resolver over the same library starts with an empty factory
     * cache, so it has to call registerResolver again - and the calculator's class ids are
     * already registered in that library's registry at this point
     */

    const auto resolverB = ResolverImplDefault::createInstance< om::Resolver >(
        PluginAccess::createInstance( plugins )
        );

    om::objref_t f2 = nullptr;
    int rc = 0;

    {
        /*
         * eh::EcUtils::getErrorCode logs the converted exception at error level
         */

        Logging::LineLoggerPusher pushLineLogger( &utest::errorToDebugLineLogger );

        rc = resolverB -> getFactory( serverid, f2, false /* onlyIfLoaded */ );
    }

    UTF_REQUIRE( rc != 0 );
    UTF_REQUIRE( nullptr == f2 );
}

UTF_FIXTURE_TEST_CASE( TestResolver_ServerWithoutPluginClass, ResolverFixture )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * A library which exports classes but has no plug-in object of its own is a legitimate
     * deployment shape - its manifest carries a nil pluginClassId and initPlugin() must skip
     * the om::Plugin creation entirely rather than asking the factory for uuids::nil(), which
     * FactoryImplT::createInstance would answer with a ClassNotFoundException
     *
     * TestResolver_CheckFailure builds such a manifest too, but every getFactory call it
     * makes fails earlier at PluginAccess::getLibrary, so this is the only case which
     * actually reaches initPlugin() with a nil plug-in class id
     *
     * The manifest is built by hand rather than through ManifestFactory::create, whose
     * pluginClassId comes from the library itself; and the library is a private copy, since
     * this case needs a registerResolver call of its own - see copyOfCalculatorPlugin above
     */

    const auto library = copyOfCalculatorPlugin(
        m_dir,
        m_plugin.path(),
        "utf-baselib-plugin-nopluginclass.so"
        );

    const auto serverid = uuids::string2uuid( "8e213524-8c75-4622-8273-6a5eeaa26250" );

    std::set< om::clsid_t > classIds;
    classIds.insert( clsids::PluginObj() );
    classIds.insert( clsids::CalculatorObj() );

    auto manifest = Manifest::createInstance(
        serverid,
        1 /* versionMajor */,
        2 /* versionMinor */,
        0 /* versionPatch */,
        std::move( classIds ),
        uuids::nil() /* pluginClassId */,
        "calculator",
        "Simple calculator plug-in",
        false /* isClient */,
        true /* isServer */,
        Platform::get( "os", "arch", "toolchain" ),
        uuids::create() /* cppCompatibilityId */
        );

    std::unordered_map< fs::path, om::ObjPtr< Manifest > > plugins;
    plugins.emplace( fs::path( library ), std::move( manifest ) );

    const auto resolver = ResolverImplDefault::createInstance< om::Resolver >(
        PluginAccess::createInstance( plugins )
        );

    om::objref_t factoryRef = nullptr;

    UTF_REQUIRE_EQUAL( resolver -> getFactory( serverid, factoryRef, false /* onlyIfLoaded */ ), 0 );
    UTF_REQUIRE( nullptr != factoryRef );

    {
        /*
         * The classes the library exports are usable even though Plugin::init() never ran
         */

        const auto factory = om::wrap< om::Factory >( factoryRef );

        const auto calc = om::wrap< utest::plugins::Calculator >(
            factory -> createInstance( clsids::CalculatorObj(), iids::Calculator() )
            );

        UTF_REQUIRE_EQUAL( 5, calc -> add( 2, 3 ) );
    }

    /*
     * The factory was memoised despite the skipped plug-in init
     */

    om::objref_t ref2 = nullptr;

    UTF_REQUIRE_EQUAL( resolver -> getFactory( serverid, ref2, true /* onlyIfLoaded */ ), 0 );
    UTF_REQUIRE( nullptr != ref2 );

    const auto factory2 = om::wrap< om::Factory >( ref2 );

    UTF_REQUIRE( factory2 );
}
