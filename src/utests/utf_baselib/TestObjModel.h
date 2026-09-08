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

#include "examples/objmodel/MyInterfaces.h"
#include "examples/objmodel/MyObjectImpl.h"

#include <baselib/tasks/Task.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueNotify.h>
#include <baselib/tasks/TaskBase.h>

#include <baselib/reactive/Observable.h>
#include <baselib/reactive/Observer.h>

#include <baselib/data/FilesystemMetadata.h>

#include <baselib/core/UuidIteratorImpl.h>
#include <baselib/core/UuidIterator.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <set>
#include <unordered_set>
#include <vector>

#include <utests/baselib/Utf.h>

BL_IID_DECLARE( TestInterface1234, "f8c57376-0d16-4161-976c-b97d30874c53" )
BL_IID_DECLARE( TestInterface1235, "43ca7a70-c55e-4b3d-b333-21fbb71be7fd" )

class TestInterface1234 : public bl::om::Object
{
    BL_DECLARE_INTERFACE( TestInterface1234 )

public:

    virtual int    getValue() = 0;
    virtual void   incValue() = 0;
};

class TestInterface1235 : public bl::om::Object
{
    BL_DECLARE_INTERFACE( TestInterface1235 )

public:

    virtual int    getValue2() = 0;
};

template
<
    typename T = void
>
class MyObjSimpleT : public bl::om::Object
{
    BL_DECLARE_OBJECT_IMPL_DEFAULT( MyObjSimpleT )

private:

    int m_value;

protected:

    MyObjSimpleT( SAA_in const int value )
        :
        m_value( value )
    {
    }

public:

    int getTheValue() const NOEXCEPT
    {
        return m_value;
    }
};

typedef bl::om::ObjectImpl< MyObjSimpleT<> > MyObjSimple;

template
<
    typename T = void
>
class MyObjEncapsulatedT : public TestInterface1234
{
    BL_DECLARE_OBJECT_IMPL_ONEIFACE( MyObjEncapsulatedT, TestInterface1234 )

private:

    int m_value1;
    int m_value2;

protected:

    MyObjEncapsulatedT( SAA_in const int value1, SAA_in const int value2 )
        :
        m_value1( value1 ),
        m_value2( value2 )
    {
    }

public:

    virtual int getValue() OVERRIDE
    {
        return m_value1;
    }

    virtual void incValue() OVERRIDE
    {
        m_value1 += m_value2;
    }
};

typedef bl::om::ObjectImpl< MyObjEncapsulatedT<> > MyObjEncapsulated;

/************************************************************************
 * A file local counted test object, shared by the factory, proxy, shared_ptr
 * and refcount balance cases below
 *
 * om::outstandingObjectRefs() is process wide - it is shared with the thread pools and it
 * is sensitive to the order in which the cases execute, which is why ObjModel_BasicTests
 * has to spin waiting for it to settle - so the lifetime assertions below count their own
 * instances with a private counter instead
 */

namespace
{
    long g_liveCounted = 0L;

    template
    <
        typename E = void
    >
    class CountedT :
        public bl::cpp::noncopyable,
        public utest::MyInterface1,
        public utest::MyInterface2,
        public bl::om::Disposable
    {
        BL_QITBL_BEGIN()
            BL_QITBL_ENTRY( utest::MyInterface1 )
            BL_QITBL_ENTRY( utest::MyInterface2 )
            BL_QITBL_ENTRY( bl::om::Disposable )
        BL_QITBL_END( utest::MyInterface1 )

    private:

        long m_value;

    protected:

        CountedT()
            :
            m_value( 0L )
        {
            ++g_liveCounted;
        }

        ~CountedT() NOEXCEPT
        {
            --g_liveCounted;
        }

    public:

        virtual long getValue() OVERRIDE
        {
            return m_value;
        }

        virtual void incValue( SAA_in const long step ) OVERRIDE
        {
            m_value += step;
        }

        virtual void dispose() OVERRIDE
        {
        }
    };

    typedef bl::om::ObjectImpl< CountedT<> >                                     CountedImpl;
    typedef bl::om::ObjectImpl< CountedT<>, true /* enableSharedPtr */ >         CountedSharedImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( ObjModel_InterfaceDefinitionsTests )
{
    using namespace bl;
    using namespace bl::data;

    std::set< std::string > ids;

    static_assert(
        sizeof( void * ) == sizeof( om::Object ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Object::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::Disposable ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Disposable::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::SharedPtr ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::SharedPtr::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::Factory ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Factory::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::Loader ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Loader::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::Resolver ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Resolver::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( om::Proxy ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( om::Proxy::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( reactive::Observer ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( reactive::Observer::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( reactive::Observable ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( reactive::Observable::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( tasks::Task ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( tasks::Task::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( tasks::ExecutionQueue ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( tasks::ExecutionQueue::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( tasks::ExecutionQueueNotify ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( tasks::ExecutionQueueNotify::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( UuidIterator ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( UuidIterator::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( FilesystemMetadataRO ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( FilesystemMetadataRO::iid() ) ).second );

    static_assert(
        sizeof( void * ) == sizeof( FilesystemMetadataWO ),
        "An interface definition should be just a vtable"
        );
    UTF_CHECK( ids.insert( uuids::uuid2string( FilesystemMetadataWO::iid() ) ).second );
}

namespace
{
    /*
     * The on-zero-refs callback is invoked from a destructor and is stored in a process
     * global std::function, so it must never capture anything whose lifetime is shorter
     * than the process - hence the file scope counter rather than a stack local
     */

    std::atomic< long > g_onZeroRefsCount( 0L );

} // __unnamed

UTF_AUTO_TEST_CASE( ObjModel_BasicTests )
{
    using namespace bl;
    using namespace bl::data;

    /*
     * Depending on what the order of execution of tests is there
     * might be some objects pending destruction in the thread pool
     *
     * Wait a little to give them a chance to go
     */

    std::size_t retries = 0U;
    const std::size_t maxRetries = 120U;

    for( ;; )
    {
        if( 0U == bl::om::outstandingObjectRefs() || retries >= maxRetries )
        {
            UTF_REQUIRE_EQUAL( 0L, bl::om::outstandingObjectRefs() );

            break;
        }

        os::sleep( time::seconds( 1L ) );

        ++retries;
    }

    const auto cbOnZeroRefs = []() -> void
    {
        ++g_onZeroRefsCount;
    };

    /*
     * The callback is process global, so it must be cleared on EVERY exit path from this
     * case - the exceptional one included. Re-installing it from a catch handler and
     * rethrowing (which is what this case used to do) leaves it armed for the rest of the
     * process, to be invoked by the next object count transition to zero
     */

    BL_SCOPE_EXIT(
        {
            bl::om::setOnZeroRefsCallback();
        }
        );

    const auto callbacksBefore = g_onZeroRefsCount.load();

    om::setOnZeroRefsCallback( cbOnZeroRefs );

    {
        {
            auto o1 = MyObjSimple::createInstance( 42 );
            UTF_CHECK( o1 );
            UTF_CHECK_EQUAL( o1 -> getTheValue(), 42 );

            const auto o2 = MyObjEncapsulated::createInstance( 13, 2 );
            UTF_CHECK( o2 );

            UTF_CHECK_EQUAL( o2 -> getValue(), 13 );
            UTF_CHECK_EQUAL( o2 -> getValue(), 13 );

            o2 -> incValue();
            UTF_CHECK_EQUAL( o2 -> getValue(), 15 );
            UTF_CHECK_EQUAL( o2 -> getValue(), 15 );

            auto o3 = std::move( o1 );
            UTF_CHECK( nullptr != o3 );
            UTF_CHECK( nullptr == o1 );

            om::ObjPtr< MyObjSimple > o4( om::ObjPtr< MyObjSimple >::attach( o3.release() ) );
            UTF_CHECK( nullptr == o3 );
            UTF_CHECK( nullptr != o4 );

            {
                const auto i1 = om::tryQI< TestInterface1234 >( o2 );
                UTF_CHECK( i1 );
                UTF_CHECK_EQUAL( i1 -> getValue(), 15 );
                i1 -> incValue();
                UTF_CHECK_EQUAL( i1 -> getValue(), 17 );
                UTF_CHECK_EQUAL( i1 -> getValue(), 17 );

                auto i2 = om::copy( i1 );
                UTF_CHECK_EQUAL( i1, i2 );

                i2.reset();

                const auto i3 = om::copy( i2 );
                UTF_CHECK( nullptr == i3 );
            }

            {
                const auto i1 = om::tryQI< TestInterface1234 >( o2.get() );
                UTF_CHECK( i1 );
                UTF_CHECK_EQUAL( i1 -> getValue(), 17 );
                i1 -> incValue();
                UTF_CHECK_EQUAL( i1 -> getValue(), 19 );
                UTF_CHECK_EQUAL( i1 -> getValue(), 19 );

                auto i2 = om::copy( i1.get() );
                UTF_CHECK_EQUAL( i1, i2 );

                i2.reset();

                const auto i3 = om::copy( i2.get() );
                UTF_CHECK( nullptr == i3 );
            }

            {
                const auto i1 = om::tryQI< TestInterface1235 >( o2 );
                UTF_CHECK( nullptr == i1.get() );
            }

            {
                try
                {
                    const auto i1 = om::qi< TestInterface1235 >( o2 );
                    UTF_FAIL( "Exception should be thrown" );
                }
                catch( InterfaceNotSupportedException& )
                {
                }
            }

            /*
             * Test areEqual( ... ) APIs
             */

            {
                auto i1 = om::qi< TestInterface1234 >( o2 );
                auto c1 = om::copy( o2 );

                /* both are non-null */
                UTF_CHECK( om::areEqual( o2, i1 ) );
                UTF_CHECK( om::areEqual( c1, i1 ) );

                /* one nullptr the other non-null */
                c1.reset();
                UTF_CHECK( ! om::areEqual( c1, i1 ) );

                /* both are nullptr */
                i1.reset();
                UTF_CHECK( om::areEqual( c1, i1 ) );
            }

            {
                const auto i1 = om::qi< TestInterface1234 >( o2 );
                const auto c1 = om::copy( o2 );

                /*
                 * Test various combinations to invoke the overloads
                 */

                UTF_CHECK( om::areEqual( o2, i1 ) );
                UTF_CHECK( om::areEqual( o2, o2 ) );

                UTF_CHECK( om::areEqual( o2.get(), i1 ) );
                UTF_CHECK( om::areEqual( o2, i1.get() ) );
                UTF_CHECK( om::areEqual( o2.get(), i1.get() ) );
            }
        }
    }

    UTF_REQUIRE( g_onZeroRefsCount.load() > callbacksBefore );

    /*
     * Make sure we end up with zero object refs (i.e. where we started) - the boolean
     * alone only says that SOME transition to zero happened, which any unrelated object
     * destruction elsewhere in the process could have produced
     */

    UTF_REQUIRE_EQUAL( 0L, om::outstandingObjectRefs() );

    /*
     * A self contained block for the clear path of setOnZeroRefsCallback( ... ) - its
     * default argument swaps in a default constructed onzerorefs_callback_t, and nothing
     * asserted that this really disarms the callback
     *
     * Note that the second assertion cannot be perturbed by another thread: with the
     * callback cleared, nothing anywhere can increment the counter
     */

    {
        om::setOnZeroRefsCallback( cbOnZeroRefs );

        const auto beforeCreate = g_onZeroRefsCount.load();

        {
            const auto o = MyObjSimple::createInstance( 42 );

            UTF_REQUIRE( o );
        }

        UTF_REQUIRE( g_onZeroRefsCount.load() > beforeCreate );

        om::setOnZeroRefsCallback();

        const auto afterClear = g_onZeroRefsCount.load();

        {
            const auto o = MyObjSimple::createInstance( 42 );

            UTF_REQUIRE( o );
        }

        UTF_REQUIRE_EQUAL( afterClear, g_onZeroRefsCount.load() );
    }
}

UTF_AUTO_TEST_CASE( ObjModel_FactoryTests )
{
    using namespace bl;
    using namespace bl::data;

    using bl::om::detail::FactoryImpl;

    const auto factoryImpl = FactoryImpl::createInstance();
    const auto factory = om::qi< om::Factory >( factoryImpl );

    const om::clsid_t clsid = uuids::create();

    /*
     * Verify the class not yet registered case
     */

    UTF_REQUIRE_THROW(
        factory -> createInstance( clsid, TestInterface1234::iid() ),
        ClassNotFoundException
        );

    const auto cbCreate = []
    (
        SAA_in       const om::iid_t&  iid,
        SAA_inout    om::Object*       identity
    ) -> om::objref_t
    {
        BL_UNUSED( identity );
        return MyObjEncapsulated::createInstance( 13, 2 ) -> queryInterface( iid );
    };

    factoryImpl -> registerClass( clsid, cbCreate );

    {
        const auto o2 = om::wrap< TestInterface1234 >( factory -> createInstance( clsid, TestInterface1234::iid() ) );
        UTF_CHECK( o2 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 13 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 13 );

        o2 -> incValue();
        UTF_CHECK_EQUAL( o2 -> getValue(), 15 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 15 );
    }

    /*
     * Verify the class already registered case
     */

    UTF_REQUIRE_THROW( factoryImpl -> registerClass( clsid, cbCreate ), UnexpectedException );

    /*
     * Verify the invalid callback case
     */

    const om::clsid_t emptyCallbackClsid = uuids::create();

    UTF_REQUIRE_THROW(
        factoryImpl -> registerClass( emptyCallbackClsid, om::register_class_callback_t() ),
        UnexpectedException
        );

    /*
     * A rejected registration must not leave a phantom entry behind - if it did the empty
     * callback would then be invoked and crash on the next createInstance( ... )
     */

    UTF_REQUIRE(
        nullptr == factoryImpl -> tryCreateInstance( emptyCallbackClsid, TestInterface1234::iid() )
        );

    /*
     * Verify the unregistering of a class which was never registered
     */

    UTF_REQUIRE_THROW( factoryImpl -> unregisterClass( uuids::create() ), UnexpectedException );

    factoryImpl -> unregisterClass( clsid );

    /*
     * Verify the class unregistered case
     */

    UTF_REQUIRE_THROW(
        factory -> createInstance( clsid, TestInterface1234::iid() ),
        ClassNotFoundException
        );
}

UTF_AUTO_TEST_CASE( ObjModel_GlobalApiTests )
{
    using namespace bl;
    using namespace bl::data;

    const om::clsid_t clsid = uuids::create();

    /*
     * Verify the class not yet registered case
     */

    UTF_REQUIRE_THROW( om::createInstance< TestInterface1234 >( clsid ), ClassNotFoundException );

    const auto cbCreate = []
    (
        SAA_in       const om::iid_t&  iid,
        SAA_inout    om::Object*       identity
    ) -> om::objref_t
    {
        BL_UNUSED( identity );
        return MyObjEncapsulated::createInstance( 13, 2 ) -> queryInterface( iid );
    };

    om::registerClass( clsid, cbCreate );

    {
        const auto o2 = om::createInstance< TestInterface1234 >( clsid );
        UTF_CHECK( o2 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 13 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 13 );

        o2 -> incValue();
        UTF_CHECK_EQUAL( o2 -> getValue(), 15 );
        UTF_CHECK_EQUAL( o2 -> getValue(), 15 );
    }

    /*
     * Verify the class already registered case
     */

    UTF_REQUIRE_THROW( om::registerClass( clsid, cbCreate ), UnexpectedException );

    /*
     * Verify the invalid callback case
     */

    UTF_REQUIRE_THROW(
        om::registerClass( uuids::create(), om::register_class_callback_t() ),
        UnexpectedException
        );

    om::unregisterClass( clsid );

    /*
     * Verify the class unregistered case
     */

    UTF_REQUIRE_THROW( om::createInstance< TestInterface1234 >( clsid ), ClassNotFoundException );

    /*
     * A registered class which does not implement the requested interface is reported
     * exactly as if the class was never registered - the default factory produced nothing
     * and there is no resolver to fall back on
     *
     * SimpleFactoryImpl< T > builds the object and then queries it, so the object it built
     * must be freed by the temporary ObjPtr when the query fails
     */

    {
        const om::clsid_t countedClsid = uuids::create();

        om::registerClass( countedClsid, &om::SimpleFactoryImpl< CountedImpl >::createInstance );

        BL_SCOPE_EXIT(
            {
                om::unregisterClass( countedClsid );
            }
            );

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

        UTF_REQUIRE_THROW(
            om::createInstance< TestInterface1235 >( countedClsid ),
            ClassNotFoundException
            );

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

        /*
         * The positive control - a supported interface really does resolve through the
         * same registration
         */

        const auto supported = om::createInstance< utest::MyInterface1 >( countedClsid );

        UTF_REQUIRE( supported );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
}

/************************************************************************
 * om::detail::LoaderImplT::reset() contract
 */

namespace
{
    /*
     * A minimal om::Resolver - none of its methods is ever invoked here. It exists only so
     * that the loader really has a resolver installed before reset() is called: asserting
     * that getResolver() is null afterwards is not falsifiable otherwise, because
     * utf_baselib never registers a resolver of its own
     */

    template
    <
        typename E = void
    >
    class TestResolverT :
        public bl::cpp::noncopyable,
        public bl::om::Resolver
    {
        BL_QITBL_DECLARE( bl::om::Resolver )

    public:

        virtual int resolveServer(
            SAA_in          const bl::om::clsid_t&                  clsid,
            SAA_out         bl::om::serverid_t&                     serverid
            ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( clsid );
            BL_UNUSED( serverid );

            return -1;
        }

        virtual int getFactory(
            SAA_in          const bl::om::serverid_t&               serverid,
            SAA_out         bl::om::objref_t&                       factory,
            SAA_in          const bool                              onlyIfLoaded = false
            ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( serverid );
            BL_UNUSED( factory );
            BL_UNUSED( onlyIfLoaded );

            return -1;
        }

        virtual int registerHost() NOEXCEPT OVERRIDE
        {
            return -1;
        }

        virtual int getCoreServices( SAA_out_opt bl::om::objref_t& coreServices ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( coreServices );

            return -1;
        }

        virtual int setCoreServices( SAA_in_opt bl::om::objref_t coreServices ) NOEXCEPT OVERRIDE
        {
            BL_UNUSED( coreServices );

            return -1;
        }
    };

    typedef bl::om::ObjectImpl< TestResolverT<> > TestResolverImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( ObjModel_LoaderResetTests )
{
    using namespace bl;

    /*
     * SNAPSHOT FIRST - utf_baselib's UTF_GLOBAL_FIXTURE ( UtfLoaderInit.h ) registers
     * clsids::MyObjectImpl() into the PROCESS GLOBAL loader for the whole module, and
     * reset() throws away every registration made through om::registerClass. Without the
     * restore below this case would break every later case which resolves that clsid, as
     * well as the fixture's own unregisterClass() at process shutdown
     *
     * The restore tolerates both states - the registration is gone on the happy path but
     * may still be present if an assertion above the reset() throws
     */

    BL_SCOPE_EXIT(
        {
            bl::om::detail::GlobalInit::getLoader() -> setResolver( nullptr );

            try
            {
                bl::om::unregisterClass( clsids::MyObjectImpl() );
            }
            catch( std::exception& )
            {
                /*
                 * Already gone - which is the expected state after reset()
                 */
            }

            bl::om::registerClass(
                clsids::MyObjectImpl(),
                &bl::om::SimpleFactoryImpl< utest::MyObjectImpl >::createInstance
                );
        }
        );

    const auto resolver = TestResolverImpl::createInstance();

    om::detail::GlobalInit::getLoader() -> setResolver( resolver.get() );

    UTF_REQUIRE( nullptr != om::detail::GlobalInit::getLoader() -> getResolver() );

    const om::clsid_t clsid = uuids::create();

    om::registerClass( clsid, &om::SimpleFactoryImpl< CountedImpl >::createInstance );

    {
        const auto instance = om::createInstance< utest::MyInterface1 >( clsid );

        UTF_REQUIRE( instance );
    }

    om::detail::GlobalInit::getLoader() -> reset();

    /*
     * A brand new default factory means every registration is gone, and with the resolver
     * cleared too the lookup ends in the "cannot be resolved, no resolver registered"
     * ClassNotFoundException path
     */

    UTF_REQUIRE_THROW( om::createInstance< utest::MyInterface1 >( clsid ), ClassNotFoundException );

    UTF_REQUIRE( nullptr == om::detail::GlobalInit::getLoader() -> getResolver() );

    /*
     * The SAME clsid must be registrable again - this is exactly what TestPluginT's
     * destructor buys the utf_baselib_loader module: without it the second case which
     * loads the same plug-in would fail with a duplicate clsid UnexpectedException
     */

    UTF_REQUIRE_NO_THROW(
        om::registerClass( clsid, &om::SimpleFactoryImpl< CountedImpl >::createInstance )
        );

    {
        const auto instance = om::createInstance< utest::MyInterface1 >( clsid );

        UTF_REQUIRE( instance );
    }

    om::unregisterClass( clsid );

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
}

UTF_AUTO_TEST_CASE( ObjModel_LoaderResetRestoredModuleStateTests )
{
    using namespace bl;

    /*
     * The positive control for ObjModel_LoaderResetTests above - the module's global
     * registration must have survived it
     */

    const auto instance = om::createInstance< utest::MyInterface1 >( clsids::MyObjectImpl() );

    UTF_REQUIRE( instance );
}

UTF_AUTO_TEST_CASE( ObjModel_MyObjectImplTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    const auto i1 = om::createInstance< MyInterface1 >( clsids::MyObjectImpl() );
    UTF_REQUIRE( i1 );
    UTF_CHECK_EQUAL( 13, i1 -> getValue() );

    UTF_CHECK( 0L != om::outstandingObjectRefs() );

    const auto i2 = om::qi< MyInterface2 >( i1 );
    const auto i3 = om::qi< MyInterface3 >( i1 );

    UTF_REQUIRE( i2 );
    i2 -> incValue( 5 );
    UTF_CHECK_EQUAL( 18, i1 -> getValue() );

    UTF_REQUIRE( i3 );
    i3 -> setValue( 3 );
    UTF_CHECK_EQUAL( 3, i1 -> getValue() );

    UTF_CHECK( om::areEqual( i1, i2 ) );
    UTF_CHECK( om::areEqual( i2, i3 ) );
}

/************************************************************************
 * Tests for std::shared_ptr< T > in the object model wrappers
 */

namespace
{
    template
    <
        typename T,
        typename U
    >
    bool ownerEqual( SAA_in const std::shared_ptr< T >& ptr1, SAA_in const std::shared_ptr< U >& ptr2 )
    {
        return ( false == ptr1.owner_before( ptr2 ) && false == ptr2.owner_before( ptr1 ) );
    }
}

UTF_AUTO_TEST_CASE( ObjModel_SharedPtrTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    typedef om::ObjectImpl< MyObjectImplT<>, true /* enableSharedPtr */ > MyObjectSharedImpl;
    typedef om::ObjectImpl< MyObjectImplT<>, false /* enableSharedPtr */ > MyObjectNonSharedImpl;

    {
        const auto i1 = om::makeShared( MyObjectSharedImpl::createInstance< MyInterface1 >() );
        UTF_REQUIRE( i1 );
        const auto i2 = om::qi< MyInterface2 >( i1 );
        UTF_REQUIRE( i2 );

        UTF_REQUIRE( om::areEqual( i1.get(), i2.get() ) );
        UTF_REQUIRE( ownerEqual( i1, i2 ) );

        const auto i3 = om::makeShared( i1.get() );
        UTF_REQUIRE( om::areEqual( i1.get(), i3.get() ) );
        UTF_REQUIRE( ownerEqual( i1, i3 ) );
    }

    {
        const auto i1 = om::makeShared( MyObjectNonSharedImpl::createInstance< MyInterface1 >() );
        UTF_REQUIRE( i1 );
        const auto i2 = om::qi< MyInterface2 >( i1 );
        UTF_REQUIRE( i2 );

        UTF_REQUIRE( om::areEqual( i1.get(), i2.get() ) );
        UTF_REQUIRE( ownerEqual( i1, i2 ) );

        const auto i3 = om::makeShared( i1.get() );
        UTF_REQUIRE( om::areEqual( i1.get(), i3.get() ) );
        UTF_REQUIRE( ! ownerEqual( i1, i3 ) );
    }

    {
        const auto o = MyObjectSharedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE( om::tryGetSharedPtr( o ) );
        UTF_REQUIRE( om::tryGetSharedPtr( o.get() ) );
        UTF_REQUIRE( om::getSharedPtr( o ) );
        UTF_REQUIRE( om::getSharedPtr( o.get() ) );
    }

    {
        const auto o = MyObjectNonSharedImpl::createInstance< MyInterface1 >();

        UTF_CHECK( nullptr == om::tryGetSharedPtr( o ).get() );
        UTF_CHECK( nullptr == om::tryGetSharedPtr( o.get() ).get() );

        try
        {
            om::getSharedPtr( o );
            UTF_FAIL( BL_MSG() << "Must thrown an exception" );
        }
        catch( InterfaceNotSupportedException& )
        {
        }

        try
        {
            om::getSharedPtr( o.get() );
            UTF_FAIL( BL_MSG() << "Must thrown an exception" );
        }
        catch( InterfaceNotSupportedException& )
        {
        }
    }
}

UTF_AUTO_TEST_CASE( ObjModel_MakeSharedTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    const auto i1 = om::createInstance< MyInterface1 >( clsids::MyObjectImpl() );
    UTF_REQUIRE( i1 );

    const auto si1 = om::makeShared( i1 );
    UTF_REQUIRE( si1 );
    UTF_CHECK_EQUAL( 13, si1 -> getValue() );

    const auto si2 = om::makeShared( i1.get() );
    UTF_REQUIRE( si2 );
    UTF_CHECK_EQUAL( 13, si2 -> getValue() );

    std::shared_ptr< MyInterface1 > si3 = si1;
    UTF_REQUIRE( si3 );
    UTF_CHECK_EQUAL( 13, si3 -> getValue() );

    const auto si4 = om::makeShared( om::qi< MyInterface2 >( i1 ) );
    UTF_REQUIRE( si4 );
    si4 -> incValue( 5 );
    UTF_CHECK_EQUAL( 18, si1 -> getValue() );
}

UTF_AUTO_TEST_CASE( ObjModel_SharedPtrLifetimeTests )
{
    using namespace bl;
    using namespace utest;

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * With the SharedPtr interface enabled the object caches its own shared_ptr weakly, so
     * two getSharedPtr( ... ) results share one control block while the first is alive, and
     * a third one taken after they are both gone must carry a brand new control block
     */

    {
        auto o = CountedSharedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        auto sp1 = om::getSharedPtr( o );
        auto sp2 = om::getSharedPtr( o );

        UTF_REQUIRE( sp1 );
        UTF_REQUIRE( sp2 );
        UTF_REQUIRE( ownerEqual( sp1, sp2 ) );
        UTF_REQUIRE_EQUAL( 2L, sp1.use_count() );

        const std::weak_ptr< om::Object > w = sp1;

        sp1.reset();
        sp2.reset();

        /*
         * The ObjPtr still owns the object, but the cached control block is gone
         */

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
        UTF_REQUIRE( w.expired() );

        auto sp3 = om::getSharedPtr( o );

        UTF_REQUIRE( sp3 );

        /*
         * Still expired - this is what distinguishes the m_this.lock() returns empty branch
         * from the cached hit above. Comparing sp3 against a retained copy of sp1 would be
         * inverted, because a retained copy keeps m_this alive
         */

        UTF_REQUIRE( w.expired() );

        o.reset();

        /*
         * The shared_ptr alone keeps the object alive, i.e. getSharedPtr( ... ) really did
         * take a hard reference of its own
         */

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        sp3.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * The release order does not matter - here the shared_ptr goes first
     */

    {
        auto o = CountedSharedImpl::createInstance< MyInterface1 >();

        auto sp = om::getSharedPtr( o );

        UTF_REQUIRE( sp );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        sp.reset();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        o.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    /*
     * Without the SharedPtr interface makeShared( ... ) falls back to a hard reference plus
     * an independent control block per call
     */

    {
        auto o = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        auto m1 = om::makeShared( o.get() );
        auto m2 = om::makeShared( o.get() );

        UTF_REQUIRE( m1 );
        UTF_REQUIRE( m2 );
        UTF_REQUIRE( ! ownerEqual( m1, m2 ) );

        o.reset();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        m1.reset();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        m2.reset();

        /*
         * Destroyed exactly once, i.e. both fallbacks addRef'ed and both deleters released
         */

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
}

UTF_AUTO_TEST_CASE( ObjModel_RefCountBalanceTests )
{
    using namespace bl;
    using namespace utest;

    /*
     * Every wrapper in the ObjPtr family has its own ownership convention which is invisible
     * at the call site - wrap( ... ) attaches, copy( ... ) / copyAs( ... ) / acquireRef( ... )
     * addRef, moveAs( ... ) transfers - and an imbalance in any of them produces either a leak
     * which is merely logged at teardown or a crash which points nowhere near the cause
     */

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    {
        const auto o = CountedImpl::createInstance();

        UTF_REQUIRE( o );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * wrap( ... ) attaches - it consumes the reference which queryInterface( ... ) returned
     * rather than taking one of its own
     */

    {
        auto o = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        {
            const auto wrapped = om::wrap< MyInterface2 >( o -> queryInterface( MyInterface2::iid() ) );

            UTF_REQUIRE( wrapped );
            UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
        }

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        o.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    /*
     * copy( ... ) and copyAs( ... ) both addRef, so the object outlives every one of them
     * until the last is released
     */

    {
        auto o = CountedImpl::createInstance< MyInterface1 >();

        auto c = om::copy( o );
        auto d = om::copyAs< MyInterface1 >( o.get() );

        UTF_REQUIRE( c );
        UTF_REQUIRE( d );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        o.reset();
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        c.reset();
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        d.reset();
        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    /*
     * moveAs( ... ) transfers the reference - it neither addRefs nor releases
     */

    {
        auto o = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        auto m = om::moveAs< om::Object >( std::move( o ) );

        UTF_REQUIRE( ! o );
        UTF_REQUIRE( m );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        m.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    /*
     * ObjPtrCopyable copies, self assigns through a copy, survives a container round trip
     * and hands its reference away through detachAsUnique( ... ) - all without an imbalance
     */

    {
        const auto o = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        {
            om::ObjPtrCopyable< MyInterface1 > p1( o );

            om::ObjPtrCopyable< MyInterface1 > p2 = p1;

            p1 = p2;

            UTF_REQUIRE( o.get() == p1.get() );
            UTF_REQUIRE( o.get() == p2.get() );

            std::vector< om::ObjPtrCopyable< MyInterface1 > > values;

            values.push_back( p1 );
            values.push_back( p2 );

            UTF_REQUIRE_EQUAL( 2U, values.size() );

            values.erase( values.begin() );

            UTF_REQUIRE_EQUAL( 1U, values.size() );
            UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
        }

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        {
            om::ObjPtrCopyable< MyInterface1 > p3( o );

            auto unique = p3.detachAsUnique();

            UTF_REQUIRE( ! p3 );
            UTF_REQUIRE( unique );
            UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
        }

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        /*
         * The two parameter acquireRef( ... ) form, the one the REST and the HTTP server
         * processing contexts use to take a reference to themselves through om::Disposable
         */

        {
            const auto concrete = CountedImpl::createInstance();

            UTF_REQUIRE_EQUAL( 2L, g_liveCounted );

            {
                typedef om::ObjPtrCopyable< CountedImpl, om::Disposable > counted_ref_t;

                const auto acquired = counted_ref_t::acquireRef( concrete.get() );

                UTF_REQUIRE( acquired );
                UTF_REQUIRE_EQUAL( 2L, g_liveCounted );
            }

            /*
             * If acquireRef( ... ) had not taken a reference the object would already be
             * gone here, while concrete still points at it
             */

            UTF_REQUIRE_EQUAL( 2L, g_liveCounted );
        }

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * The std::hash specialization which makes ObjPtrCopyable usable as an unordered
     * container key - tasks::SimpleTaskControlToken relies on it
     */

    {
        const auto o = CountedImpl::createInstance< MyInterface1 >();

        std::unordered_set< om::ObjPtrCopyable< MyInterface1 > > keys;

        keys.insert( om::ObjPtrCopyable< MyInterface1 >( o ) );

        UTF_REQUIRE_EQUAL( 1U, keys.size() );

        const auto pos = keys.find( om::ObjPtrCopyable< MyInterface1 >( o ) );

        UTF_REQUIRE( pos != keys.end() );

        keys.erase( pos );

        UTF_REQUIRE( keys.empty() );
        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
}

/************************************************************************
 * Tests for ProxyImpl
 */

UTF_AUTO_TEST_CASE( ObjModel_ProxyImplTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    const auto i1 = om::createInstance< MyInterface1 >( clsids::MyObjectImpl() );

    const auto proxy = om::ProxyImpl::createInstance< om::Proxy >();

    const MyInterface1* const nullValue = nullptr;

    UTF_CHECK_EQUAL( nullValue, proxy -> tryAcquireRef< MyInterface1 >().get() );
    proxy -> disconnect();
    UTF_CHECK_EQUAL( nullValue, proxy -> tryAcquireRef< MyInterface1 >().get() );
    proxy -> disconnect();
    UTF_CHECK_EQUAL( nullValue, proxy -> tryAcquireRef< MyInterface1 >().get() );

    proxy -> connect( i1.get() );
    const auto i2 = proxy -> tryAcquireRef< MyInterface1 >();
    UTF_REQUIRE( i2 );

    UTF_REQUIRE( om::areEqual( i1, i2 ) );
    UTF_REQUIRE( proxy -> tryAcquireRef< MyInterface1 >() );

    proxy -> disconnect();
    UTF_CHECK_EQUAL( nullValue, proxy -> tryAcquireRef< MyInterface1 >().get() );
}

UTF_AUTO_TEST_CASE( ObjModel_ProxyImplGuardTests )
{
    using namespace bl;
    using namespace utest;

    /*
     * tryAcquireRefUnsafe( ... ) transfers its internal lock to the caller's guard only when
     * BOTH the query succeeded and a guard was supplied, while disconnect( ... ) transfers it
     * whenever a guard was supplied
     *
     * The messaging and the REST processing paths rely on that transfer for mutual exclusion,
     * and turning the conjunction into a plain 'if( guard )' would leak the proxy lock on
     * every disconnected acquisition and deadlock the next caller
     */

    const auto i1 = om::createInstance< MyInterface1 >( clsids::MyObjectImpl() );

    const auto proxy = om::ProxyImpl::createInstance< om::Proxy >();

    /*
     * Disconnected, with a guard - no reference and no lock transfer
     */

    {
        os::mutex_unique_lock g1;

        UTF_REQUIRE( ! proxy -> tryAcquireRef< MyInterface1 >( MyInterface1::iid(), &g1 ) );
        UTF_REQUIRE( ! g1.owns_lock() );
    }

    proxy -> connect( i1.get() );

    /*
     * ~ProxyImplT calls BL_RT_ASSERT and aborts the process if the proxy is still connected,
     * so the teardown below must survive an early assertion failure
     */

    BL_SCOPE_EXIT(
        {
            proxy -> disconnect();
        }
        );

    /*
     * Connected, but the interface is not supported - still no lock transfer
     */

    {
        os::mutex_unique_lock g2;

        UTF_REQUIRE( ! proxy -> tryAcquireRef< TestInterface1234 >( TestInterface1234::iid(), &g2 ) );
        UTF_REQUIRE( ! g2.owns_lock() );
    }

    /*
     * Connected and supported - the reference is handed out and the lock comes with it
     */

    {
        os::mutex_unique_lock g3;

        const auto i2 = proxy -> tryAcquireRef< MyInterface1 >( MyInterface1::iid(), &g3 );

        UTF_REQUIRE( i2 );
        UTF_REQUIRE( g3.owns_lock() );
        UTF_REQUIRE( om::areEqual( i1, i2 ) );

        /*
         * The transferred lock really is the proxy's lock, so a second acquirer must block
         * on it until the guard is released
         *
         * The worker touches only the atomic - Boost.Test assertions are not thread safe off
         * the main thread - and both assertions are made after the join, so a failure can
         * never leave a joinable thread behind
         */

        std::atomic< bool > secondAcquireDone( false );

        os::thread worker(
            [ &proxy, &secondAcquireDone ]() -> void
            {
                ( void ) proxy -> tryAcquireRef< MyInterface1 >();

                secondAcquireDone = true;
            }
            );

        os::sleep( time::milliseconds( 300 ) );

        const bool doneWhileLocked = secondAcquireDone;

        g3.unlock();

        worker.join();

        const bool doneAfterUnlock = secondAcquireDone;

        UTF_REQUIRE( ! doneWhileLocked );
        UTF_REQUIRE( doneAfterUnlock );
    }

    /*
     * Without a guard nothing is transferred, so two acquisitions in a row on the same
     * thread must not deadlock
     */

    UTF_REQUIRE( proxy -> tryAcquireRef< MyInterface1 >() );
    UTF_REQUIRE( proxy -> tryAcquireRef< MyInterface1 >() );

    /*
     * disconnect( guard ) transfers the lock unconditionally
     */

    {
        os::mutex_unique_lock g4;

        proxy -> disconnect( &g4 );

        UTF_REQUIRE( g4.owns_lock() );

        g4.unlock();
    }

    UTF_REQUIRE( ! proxy -> tryAcquireRef< MyInterface1 >() );

    proxy -> disconnect();
}

UTF_AUTO_TEST_CASE( ObjModel_ProxyImplStrongRefTests )
{
    using namespace bl;
    using namespace utest;

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * A strong reference proxy addRefs on connect and releases on disconnect, and a
     * re-connect must release the previous reference before it takes the new one
     */

    {
        auto a = CountedImpl::createInstance< MyInterface1 >();
        auto b = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 2L, g_liveCounted );

        const auto proxy = om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );

        BL_SCOPE_EXIT(
            {
                proxy -> disconnect();
            }
            );

        proxy -> connect( a.get() );

        /*
         * The proxy alone keeps the object alive now
         *
         * Note that the same raw pointer must never be connected twice while the proxy holds
         * the only strong reference - disconnectInternalNoLock() releases before connect()
         * addRefs - which is why b is kept alive independently across the re-connect below
         */

        a.reset();

        UTF_REQUIRE_EQUAL( 2L, g_liveCounted );
        UTF_REQUIRE( proxy -> tryAcquireRef< MyInterface1 >() );

        proxy -> connect( b.get() );

        /*
         * The re-connect released the previous strong reference
         */

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        proxy -> disconnect();

        b.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * The default proxy is a weak one - connect( ... ) takes no reference, so dropping the
     * last ObjPtr destroys the object even while the proxy is still connected
     *
     * This is safe only because disconnect() on a weak proxy never dereferences m_ref
     */

    {
        auto c = CountedImpl::createInstance< MyInterface1 >();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        const auto proxy = om::ProxyImpl::createInstance< om::Proxy >();

        BL_SCOPE_EXIT(
            {
                proxy -> disconnect();
            }
            );

        proxy -> connect( c.get() );

        UTF_REQUIRE( proxy -> tryAcquireRef< MyInterface1 >() );

        c.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );

    /*
     * And symmetrically - a weak proxy must not release on disconnect
     */

    {
        auto d = CountedImpl::createInstance< MyInterface1 >();

        const auto proxy = om::ProxyImpl::createInstance< om::Proxy >();

        BL_SCOPE_EXIT(
            {
                proxy -> disconnect();
            }
            );

        proxy -> connect( d.get() );

        proxy -> disconnect();

        UTF_REQUIRE_EQUAL( 1L, g_liveCounted );

        d.reset();

        UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
    }

    UTF_REQUIRE_EQUAL( 0L, g_liveCounted );
}

/************************************************************************
 * Tests for ObjPtrDisposable
 */

namespace
{
    class FooDisposable : public bl::om::Disposable
    {
        BL_CTR_DEFAULT( FooDisposable, protected )
        BL_DECLARE_OBJECT_IMPL_ONEIFACE_NO_DESTRUCTOR( FooDisposable, bl::om::Disposable )

    private:

        bl::cpp::ScalarTypeIniter< bool > m_disposed;

    protected:

        ~FooDisposable() NOEXCEPT
        {
            BL_RT_ASSERT( m_disposed, "Object must be disposed" );
        }

    public:

        bool isDisposed() const NOEXCEPT
        {
            return m_disposed;
        }

        virtual void dispose() OVERRIDE
        {
            m_disposed = true;
        }
    };

    typedef bl::om::ObjectImpl< FooDisposable > FooDisposableImpl;

    /*
     * A production shaped disposable service - unlike FooDisposable above, whose IDENTITY
     * interface is om::Disposable itself ( so the tryQI< Disposable > inside
     * ObjPtrDisposable::reset() can never fail there ), this one has a DOMAIN interface as
     * its identity and reaches Disposable only through the extra QI table entry which
     * BL_QITBL_DECLARE_DISPOSABLE emits
     *
     * The dispose count and the live instance count are what every assertion below is
     * expressed in; the dispose() body deliberately contains no UTF_* macro, because it
     * runs from a destructor
     */

    long g_fooSvcLive = 0L;

    template
    <
        typename E = void
    >
    class FooSvcT :
        public bl::cpp::noncopyable,
        public utest::MyInterface1,
        public bl::om::Disposable
    {
        BL_QITBL_DECLARE_DISPOSABLE( utest::MyInterface1 )

    private:

        long m_disposeCount;
        bool m_throwOnDispose;

    protected:

        FooSvcT()
            :
            m_disposeCount( 0L ),
            m_throwOnDispose( false )
        {
            ++g_fooSvcLive;
        }

        ~FooSvcT() NOEXCEPT
        {
            --g_fooSvcLive;
        }

    public:

        long disposeCount() const NOEXCEPT
        {
            return m_disposeCount;
        }

        void throwOnDispose( SAA_in const bool value ) NOEXCEPT
        {
            m_throwOnDispose = value;
        }

        virtual long getValue() OVERRIDE
        {
            return m_disposeCount;
        }

        virtual void dispose() OVERRIDE
        {
            ++m_disposeCount;

            if( m_throwOnDispose )
            {
                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "dispose-failure-marker"
                    );
            }
        }
    };

    typedef bl::om::ObjectImpl< FooSvcT<> > FooSvcImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( ObjModel_ObjPtrDisposableTests )
{
    using namespace bl;

    /*
     * The original coverage - kept as is. Note that its real check was FooDisposable's
     * BL_RT_ASSERT destructor, which calls os::fastAbort() and therefore KILLS the process
     * instead of failing the case; everything below is expressed as ordinary assertions
     */

    {
        auto obj = om::lockDisposable( FooDisposableImpl::createInstance() );

        obj = om::lockDisposable( FooDisposableImpl::createInstance() );
        obj -> dispose();

        UTF_REQUIRE( obj -> isDisposed() );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (2) Scope exit - the destructor routes through reset()
     */

    {
        om::ObjPtr< FooSvcImpl > keepAlive;

        {
            const auto d = om::lockDisposable( FooSvcImpl::createInstance() );

            keepAlive = om::copy( d.get() );

            UTF_REQUIRE_EQUAL( 1L, g_fooSvcLive );
            UTF_REQUIRE_EQUAL( 0L, keepAlive -> disposeCount() );
        }

        /*
         * Exactly once, and through a QI which had to walk to a non identity entry
         */

        UTF_REQUIRE_EQUAL( 1L, keepAlive -> disposeCount() );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (3) An explicit reset() on a live pointer
     */

    {
        om::ObjPtr< FooSvcImpl > keepAlive;

        auto d = om::lockDisposable( FooSvcImpl::createInstance() );

        keepAlive = om::copy( d.get() );

        d.reset();

        UTF_REQUIRE( ! d );
        UTF_REQUIRE_EQUAL( 1L, keepAlive -> disposeCount() );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (4) reset( other ) - the OLD object is disposed, the new one is not
     *
     * REVIEW ITEM: reset( T* ) and operator =( T* ) are public and ATTACH WITHOUT an
     * addRef, unlike ObjPtr, whose equivalent is protected precisely to prevent this
     * ( ObjModel.h:123 vs :898-910 ) - so the reference has to be handed over explicitly
     * with release(), otherwise it would be released twice
     */

    {
        om::ObjPtr< FooSvcImpl > oldKeepAlive;
        om::ObjPtr< FooSvcImpl > newKeepAlive;

        auto d = om::lockDisposable( FooSvcImpl::createInstance() );

        oldKeepAlive = om::copy( d.get() );

        {
            auto newOne = FooSvcImpl::createInstance();

            newKeepAlive = om::copy( newOne.get() );

            d.reset( newOne.release() );
        }

        UTF_REQUIRE_EQUAL( 1L, oldKeepAlive -> disposeCount() );
        UTF_REQUIRE_EQUAL( 0L, newKeepAlive -> disposeCount() );
        UTF_REQUIRE( om::areEqual( d.get(), newKeepAlive.get() ) );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (5) Move assignment from another ObjPtrDisposable
     */

    {
        om::ObjPtr< FooSvcImpl > previous;
        om::ObjPtr< FooSvcImpl > incoming;

        auto d = om::lockDisposable( FooSvcImpl::createInstance() );

        previous = om::copy( d.get() );

        auto other = om::lockDisposable( FooSvcImpl::createInstance() );

        incoming = om::copy( other.get() );

        d = std::move( other );

        UTF_REQUIRE_EQUAL( 1L, previous -> disposeCount() );
        UTF_REQUIRE_EQUAL( 0L, incoming -> disposeCount() );
        UTF_REQUIRE( ! other );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (6) Move assignment from a plain om::ObjPtr< T >&& - the base_type&& overload
     */

    {
        om::ObjPtr< FooSvcImpl > previous;
        om::ObjPtr< FooSvcImpl > incoming;

        auto d = om::lockDisposable( FooSvcImpl::createInstance() );

        previous = om::copy( d.get() );

        auto plain = FooSvcImpl::createInstance();

        incoming = om::copy( plain.get() );

        d = std::move( plain );

        UTF_REQUIRE_EQUAL( 1L, previous -> disposeCount() );
        UTF_REQUIRE_EQUAL( 0L, incoming -> disposeCount() );
        UTF_REQUIRE( ! plain );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (7) detachAsObjPtr() deliberately does NOT dispose - messaging's
     * MessagingClientFactory, ForwardingBackendProcessingImpl and
     * ProxyBrokerBackendProcessingFactory all hand backends off through it, and a
     * detachAsObjPtr() which disposed would tear every one of them down
     */

    {
        auto d = om::lockDisposable( FooSvcImpl::createInstance() );

        const auto detached = d.detachAsObjPtr();

        UTF_REQUIRE( ! d );
        UTF_REQUIRE( detached );
        UTF_REQUIRE_EQUAL( 0L, detached -> disposeCount() );
        UTF_REQUIRE_EQUAL( 1L, g_fooSvcLive );
    }

    /*
     * Destroying the returned ObjPtr released the object without ever disposing it
     */

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (8) d = nullptr - the ASYMMETRY. This overload comes from BL_CTR_COPY_DEFAULT_T
     * ( BaseDefs.h:209-222 ) and forwards straight to ObjPtr::operator =( nullptr ), so
     * it does NOT route through reset() and the reference is released WITHOUT dispose()
     *
     * This contradicts the class's own documentation ( "This smart pointer will invoke
     * dispose() when it goes out of scope" ). Today's behaviour is pinned here rather
     * than changed - flagged to the owner instead
     *
     * The object is kept alive through an independent ObjPtr so the assertion does not
     * depend on the aborting BL_RT_ASSERT path of a never-disposed object
     */

    {
        const auto keepAlive = FooSvcImpl::createInstance();

        om::ObjPtrDisposable< FooSvcImpl > d( om::lockDisposable( keepAlive ) );

        UTF_REQUIRE( d );

        d = nullptr;

        UTF_REQUIRE( ! d );
        UTF_REQUIRE_EQUAL( 0L, keepAlive -> disposeCount() );
        UTF_REQUIRE_EQUAL( 1L, g_fooSvcLive );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );

    /*
     * (9) The null pointer form yields an empty ObjPtrDisposable and disposes nothing
     */

    {
        const auto d = om::lockDisposable( static_cast< FooSvcImpl* >( nullptr ) );

        UTF_REQUIRE( ! d );
        UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );
    }

    /*
     * (10) A throwing dispose() must be logged as a warning and swallowed
     *
     * reset() wraps the dispose in BL_WARN_NOEXCEPT_BEGIN/END - replacing that with
     * BL_NOEXCEPT_* would turn this logged warning into a process abort at every call
     * site, and letting the exception escape would terminate the process from a destructor
     */

    {
        cpp::SafeOutputStringStream os;

        const Logging::line_logger_t ll(
            cpp::bind(
                &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /* addNewLine */, cpp::ref( os )
                )
            );

        Logging::LineLoggerPusher pushLogger( ll );

        Logging::LevelPusher pushLevel( Logging::LL_WARNING, true /* global */ );

        {
            const auto svc = FooSvcImpl::createInstance();

            svc -> throwOnDispose( true );

            {
                const auto d = om::lockDisposable( svc );

                BL_UNUSED( d );
            }

            /*
             * Control reached here, i.e. the exception did not escape and the process was
             * not aborted; the reference was still released by reset()
             */

            UTF_REQUIRE_EQUAL( 1L, svc -> disposeCount() );
        }

        const auto text = os.str();

        UTF_REQUIRE( cpp::contains( text, std::string( "ObjPtrDisposable::~ObjPtrDisposable()" ) ) );
        UTF_REQUIRE( cpp::contains( text, std::string( "NOEXCEPT block threw an exception" ) ) );
        UTF_REQUIRE( cpp::contains( text, std::string( "dispose-failure-marker" ) ) );
    }

    UTF_REQUIRE_EQUAL( 0L, g_fooSvcLive );
}

