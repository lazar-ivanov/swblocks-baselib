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

#include <baselib/data/models/ErrorHandling.h>
#include <baselib/data/models/Functions.h>
#include <baselib/data/models/JsonMessaging.h>

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestUtils.h>

namespace utest
{
    namespace dm
    {
        /*
         * ContainedTestObjectBase
         */

        BL_DM_DEFINE_CLASS_BEGIN( ContainedTestObjectBase )

            BL_DM_DECLARE_STRING_PROPERTY               ( strValue )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( strValue )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( ContainedTestObjectBase )

        BL_DM_DEFINE_PROPERTY( ContainedTestObjectBase, strValue )

        /*
         * ContainedTestObject
         */

        BL_DM_DEFINE_CLASS_BEGIN( ContainedTestObject )

            BL_DM_DECLARE_STRING_PROPERTY               ( strValue )
            BL_DM_DECLARE_BOOL_PROPERTY                 ( boolValue )
            BL_DM_DECLARE_INT_PROPERTY                  ( intValue )
            BL_DM_DECLARE_UINT64_PROPERTY               ( uint64Value )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( strValue )
                BL_DM_IMPL_PROPERTY( boolValue )
                BL_DM_IMPL_PROPERTY( intValue )
                BL_DM_IMPL_PROPERTY( uint64Value )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( ContainedTestObject )

        BL_DM_DEFINE_PROPERTY( ContainedTestObject, strValue )
        BL_DM_DEFINE_PROPERTY( ContainedTestObject, boolValue )
        BL_DM_DEFINE_PROPERTY( ContainedTestObject, intValue )
        BL_DM_DEFINE_PROPERTY( ContainedTestObject, uint64Value )

        /*
         * RequiredPropsTestObject
         *
         * The one model in the repository which declares required properties of both kinds -
         * a string one (where emptiness is absence) and scalar ones (which carry an IsSet flag)
         */

        BL_DM_DEFINE_CLASS_BEGIN( RequiredPropsTestObject )

            BL_DM_DECLARE_STRING_REQUIRED_PROPERTY          ( requiredStr )
            BL_DM_DECLARE_INT_REQUIRED_PROPERTY             ( requiredInt )
            BL_DM_DECLARE_BOOL_REQUIRED_PROPERTY            ( requiredBool )
            BL_DM_DECLARE_STRING_ALTERNATE_REQUIRED_PROPERTY( requiredAlt, "json_required_alt" )
            BL_DM_DECLARE_STRING_PROPERTY                   ( optionalStr )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( requiredStr )
                BL_DM_IMPL_PROPERTY( requiredInt )
                BL_DM_IMPL_PROPERTY( requiredBool )
                BL_DM_IMPL_PROPERTY( requiredAlt )
                BL_DM_IMPL_PROPERTY( optionalStr )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( RequiredPropsTestObject )

        BL_DM_DEFINE_PROPERTY( RequiredPropsTestObject, requiredStr )
        BL_DM_DEFINE_PROPERTY( RequiredPropsTestObject, requiredInt )
        BL_DM_DEFINE_PROPERTY( RequiredPropsTestObject, requiredBool )
        BL_DM_DEFINE_PROPERTY( RequiredPropsTestObject, requiredAlt )
        BL_DM_DEFINE_PROPERTY( RequiredPropsTestObject, optionalStr )

        /*
         * ContainerTestObject
         *
         * Note the two different conventions for the alternate JSON name - the simple container
         * macros stringify their argument, so aliasVector takes a bare identifier, while the
         * string-or-array macro uses its argument verbatim and so takes a quoted literal
         */

        BL_DM_DEFINE_CLASS_BEGIN( ContainerTestObject )

            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY            ( intVector, int, get_int )
            BL_DM_DECLARE_SIMPLE_SET_PROPERTY               ( intSet, int, get_int )
            BL_DM_DECLARE_SIMPLE_SET_PROPERTY               ( stringSet, std::string, get_string )
            BL_DM_DECLARE_SIMPLE_VECTOR_ALTERNATE_PROPERTY  ( aliasVector, json_alias_vector, std::string, get_string )
            BL_DM_DECLARE_STRING_OR_ARRAY_ALTERNATE_PROPERTY( audienceLike, "json_aud" )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( intVector )
                BL_DM_IMPL_PROPERTY( intSet )
                BL_DM_IMPL_PROPERTY( stringSet )
                BL_DM_IMPL_PROPERTY( aliasVector )
                BL_DM_IMPL_PROPERTY( audienceLike )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( ContainerTestObject )

        BL_DM_DEFINE_PROPERTY( ContainerTestObject, intVector )
        BL_DM_DEFINE_PROPERTY( ContainerTestObject, intSet )
        BL_DM_DEFINE_PROPERTY( ContainerTestObject, stringSet )
        BL_DM_DEFINE_PROPERTY( ContainerTestObject, aliasVector )
        BL_DM_DEFINE_PROPERTY( ContainerTestObject, audienceLike )

        /*
         * TestObjectBase
         */

        BL_DM_DEFINE_CLASS_BEGIN( TestObjectBase )

            BL_DM_DECLARE_UINT64_PROPERTY              ( id )
            BL_DM_DECLARE_COMPLEX_PROPERTY             ( complex, ContainedTestObjectBase )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( id )
                BL_DM_IMPL_PROPERTY( complex )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( TestObjectBase )

        BL_DM_DEFINE_PROPERTY( TestObjectBase, id )
        BL_DM_DEFINE_PROPERTY( TestObjectBase, complex )

        /*
         * TestObject
         */

        BL_DM_DEFINE_CLASS_BEGIN( TestObject )

            BL_DM_DECLARE_UINT64_PROPERTY              ( id )
            BL_DM_DECLARE_MAP_PROPERTY                 ( userData, std::string )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY       ( numbers, int, get_int )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY       ( strings, std::string, get_str )
            BL_DM_DECLARE_INT_ALTERNATE_PROPERTY       ( number, "json_number" )
            BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY    ( str, "json_str" )
            BL_DM_DECLARE_CUSTOM_PROPERTY              ( custom )
            BL_DM_DECLARE_COMPLEX_PROPERTY             ( complex, ContainedTestObject )
            BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY      ( complexVector, ContainedTestObject )
            BL_DM_DECLARE_COMPLEX_MAP_PROPERTY         ( complexMap, ContainedTestObject )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( id )
                BL_DM_IMPL_PROPERTY( userData )
                BL_DM_IMPL_PROPERTY( numbers )
                BL_DM_IMPL_PROPERTY( strings )
                BL_DM_IMPL_PROPERTY( number )
                BL_DM_IMPL_PROPERTY( str )
                BL_DM_IMPL_PROPERTY( custom )
                BL_DM_IMPL_PROPERTY( complex )
                BL_DM_IMPL_PROPERTY( complexVector )
                BL_DM_IMPL_PROPERTY( complexMap )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( TestObject )

        BL_DM_DEFINE_PROPERTY( TestObject, id )
        BL_DM_DEFINE_PROPERTY( TestObject, userData )
        BL_DM_DEFINE_PROPERTY( TestObject, numbers )
        BL_DM_DEFINE_PROPERTY( TestObject, strings )
        BL_DM_DEFINE_PROPERTY( TestObject, number )
        BL_DM_DEFINE_PROPERTY( TestObject, str )
        BL_DM_DEFINE_PROPERTY( TestObject, custom )
        BL_DM_DEFINE_PROPERTY( TestObject, complex )
        BL_DM_DEFINE_PROPERTY( TestObject, complexVector )
        BL_DM_DEFINE_PROPERTY( TestObject, complexMap )

    } // dm

} // utest

UTF_AUTO_TEST_CASE( DataModelPropertyErrorsCarryPropertyContext )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * Whatever a property deserializer throws - the backend's own conversion error (a
     * std::runtime_error on both backends) or the JsonException of the library's checked
     * accessors - surfaces as a JsonException whose message names the property
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< ContainedTestObject >( R"({"intValue":3000000000})" ),
        JsonException,
        "property 'intValue'"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< ContainedTestObject >( R"({"uint64Value":-1})" ),
        JsonException,
        "property 'uint64Value'"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< ContainedTestObject >( R"({"intValue":"abc"})" ),
        JsonException,
        "property 'intValue'"
        );

    /*
     * An exact double into an integer property is refused on both backends (see
     * JsonNumericDoubleIntoIntegralIsRejected), with the property named as well
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< ContainedTestObject >( R"({"intValue":3.0})" ),
        JsonException,
        "property 'intValue'"
        );

    /*
     * A kind mismatch is reported in readable words and marked as user friendly; the raw
     * library text (which on Boost.JSON reads like 'not a string [boost.json:N]') is nested
     * rather than shown
     */

    UTF_CHECK_EXCEPTION(
        dmu::loadFromJsonText< ContainedTestObject >( R"({"strValue":42})" ),
        JsonException,
        []( SAA_in const JsonException& e ) -> bool
        {
            const std::string message( e.what() );

            return
                eh::isUserFriendly( e ) &&
                cpp::contains( message, "property 'strValue'" ) &&
                ! cpp::contains( message, "boost.json" );
        }
        );
}

UTF_AUTO_TEST_CASE( CoreDataModelTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    const auto printTestObject = []( SAA_in const om::ObjPtr< TestObject >& testObj ) -> void
    {
        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "\nTest object JSON:\n"
                << dmu::getDocAsPrettyJsonString( testObj )
            );
    };

    const auto printTestObjectBase = []( SAA_in const om::ObjPtr< TestObjectBase >& baseObj ) -> void
    {
        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "\nTest base object JSON:\n"
                << dmu::getDocAsPrettyJsonString( baseObj )
            );
    };

    const auto printPayloadObject = []( SAA_in const om::ObjPtr< dm::Payload >& payloadObj ) -> void
    {
        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "\nTest payload object JSON:\n"
                << dmu::getDocAsPrettyJsonString( payloadObj )
            );
    };

    const std::string author( "A test author" );
    const std::string description( "A test description" );
    const std::uint64_t uint64Value = 1000000000ULL * 42;

    const auto verifyTestObject = [ & ]( SAA_in const om::ObjPtr< TestObject >& testObj ) -> void
    {
        UTF_REQUIRE_EQUAL( testObj -> id(), 1234U );

        UTF_REQUIRE_EQUAL( testObj -> userData().size(), 2U );
        UTF_REQUIRE_EQUAL( testObj -> userData().at( "author" ), author );
        UTF_REQUIRE_EQUAL( testObj -> userData().at( "description" ), description );

        UTF_REQUIRE_EQUAL( testObj -> number(), 42 );

        UTF_REQUIRE_EQUAL( testObj -> numbers().size(), 1U );
        UTF_REQUIRE_EQUAL( testObj -> numbers()[ 0 ], 42 );

        UTF_REQUIRE_EQUAL( testObj -> strings().size(), 2U );
        UTF_REQUIRE_EQUAL( testObj -> strings()[ 0 ], "foo" );
        UTF_REQUIRE_EQUAL( testObj -> strings()[ 1 ], "bar" );

        UTF_REQUIRE( ! testObj -> custom().is_null() );
        {
            const auto& s = testObj -> custom().as_object().at( "name" ).as_string();
            UTF_REQUIRE_EQUAL( std::string( s.c_str(), s.size() ), "value" );
        }

        const auto& complexObj = testObj -> complex();

        UTF_REQUIRE_EQUAL( complexObj -> strValue(), "strValue3" );
        UTF_REQUIRE_EQUAL( complexObj -> boolValue(), true );
        UTF_REQUIRE_EQUAL( complexObj -> intValue(), 420 );
        UTF_REQUIRE_EQUAL( complexObj -> uint64Value(), uint64Value / 2 );

        UTF_REQUIRE_EQUAL( testObj -> complexVector().size(), 2U );
        UTF_REQUIRE_EQUAL( testObj -> complexMap().size(), 2U );

        {
            {
                const auto& obj1 = testObj -> complexVector()[ 0U ];

                UTF_REQUIRE_EQUAL( obj1 -> strValue(), "strValue1" );
                UTF_REQUIRE_EQUAL( obj1 -> boolValue(), true );
                UTF_REQUIRE_EQUAL( obj1 -> intValue(), 42 );
                UTF_REQUIRE_EQUAL( obj1 -> uint64Value(), uint64Value );
            }

            {
                const auto& obj2 = testObj -> complexVector()[ 1U ];

                UTF_REQUIRE_EQUAL( obj2 -> strValue(), "strValue2" );
                UTF_REQUIRE_EQUAL( obj2 -> boolValue(), true );
                UTF_REQUIRE_EQUAL( obj2 -> intValue(), 42 );
                UTF_REQUIRE_EQUAL( obj2 -> uint64Value(), uint64Value );
            }
        }

        {
            {
                const auto& obj1 = testObj -> complexMap().at( "obj1" );

                UTF_REQUIRE_EQUAL( obj1 -> strValue(), "strValue1" );
                UTF_REQUIRE_EQUAL( obj1 -> boolValue(), true );
                UTF_REQUIRE_EQUAL( obj1 -> intValue(), 42 );
                UTF_REQUIRE_EQUAL( obj1 -> uint64Value(), uint64Value );
            }

            {
                const auto& obj2 = testObj -> complexMap().at( "obj2" );

                UTF_REQUIRE_EQUAL( obj2 -> strValue(), "strValue2" );
                UTF_REQUIRE_EQUAL( obj2 -> boolValue(), true );
                UTF_REQUIRE_EQUAL( obj2 -> intValue(), 42 );
                UTF_REQUIRE_EQUAL( obj2 -> uint64Value(), uint64Value );
            }
        }
    };

    const auto populateTestObject = [ & ]( SAA_in const om::ObjPtr< TestObject >& testObj ) -> void
    {
        testObj -> id( 1234U );

        testObj -> userDataLvalue()[ "author" ] = author;
        testObj -> userDataLvalue()[ "description" ] = description;

        testObj -> number( 42 );

        testObj -> numbersLvalue().push_back( 42 );

        testObj -> stringsLvalue().push_back( "foo" );
        testObj -> stringsLvalue().push_back( "bar" );

        testObj -> number( 42 );
        testObj -> str( "string property" );

        UTF_REQUIRE( testObj -> custom().is_null() );
        testObj -> custom( json::readFromString( "{ \"name\": \"value\" }" ) );
        UTF_REQUIRE( ! testObj -> custom().is_null() );

        const auto complexObj = ContainedTestObject::createInstance();

        complexObj -> strValue( "strValue3" );
        complexObj -> boolValue( true );
        complexObj -> intValue( 420 );
        complexObj -> uint64Value( uint64Value / 2 );

        testObj -> complexLvalue() = om::copy( complexObj );

        const auto obj1 = ContainedTestObject::createInstance();
        obj1 -> strValue( "strValue1" );
        obj1 -> boolValue( true );
        obj1 -> intValue( 42 );
        obj1 -> uint64Value( uint64Value );

        const auto obj2 = ContainedTestObject::createInstance();
        obj2 -> strValue( "strValue2" );
        obj2 -> boolValue( true );
        obj2 -> intValue( 42 );
        obj2 -> uint64Value( uint64Value );

        testObj -> complexVectorLvalue().push_back( om::copy( obj1 ) );
        testObj -> complexVectorLvalue().push_back( om::copy( obj2 ) );

        testObj -> complexMapLvalue().emplace( "obj1", om::copy( obj1 ) );
        testObj -> complexMapLvalue().emplace( "obj2", om::copy( obj2 ) );
    };

    auto testObj = TestObject::createInstance();

    printTestObject( testObj );

    populateTestObject( testObj );

    printTestObject( testObj );

    const auto testObjFromFile = DataModelUtils::loadFromFile< TestObject >(
        utest::TestUtils::resolveDataFilePath( "serialized_object.json" )
        );

    printTestObject( testObjFromFile );

    UTF_REQUIRE_EQUAL(
        DataModelUtils::getObjectHashCanonical( testObj ),
        DataModelUtils::getObjectHashCanonical( testObjFromFile )
        );

    UTF_REQUIRE_EQUAL(
        DataModelUtils::getObjectHash( testObj, str::empty() /* salt */, false /* canonicalize */ ),
        DataModelUtils::getObjectHash( testObjFromFile, str::empty() /* salt */, false /* canonicalize */ )
        );

    {
        const auto jsonText = dmu::getDocAsPackedJsonString( testObj );

        testObj = dmu::loadFromJsonText< TestObject >( jsonText );

        verifyTestObject( testObj );

        printTestObject( testObj );
    }

    {
        const auto jsonText = dmu::getDocAsPrettyJsonString( testObj );

        testObj = dmu::loadFromJsonText< TestObject >( jsonText );

        verifyTestObject( testObj );

        printTestObject( testObj );

        /*
         * Verify base object and casting behavior
         */

        const auto baseObj = dmu::loadFromJsonText< TestObjectBase >( jsonText );

        printTestObjectBase( baseObj );

        UTF_REQUIRE_EQUAL( baseObj -> id(), 1234U );
        UTF_REQUIRE( baseObj -> complex() );
        UTF_REQUIRE_EQUAL( baseObj -> complex() -> strValue(), "strValue3" );

        testObj = dmu::castTo< TestObject >( baseObj );

        verifyTestObject( testObj );

        printTestObject( testObj );

        /*
         * Verify base object and casting behavior with the placeholder object
         */

        const auto payloadObj = dmu::loadFromJsonText< Payload >( jsonText );

        printPayloadObject( payloadObj );

        testObj = dmu::castTo< TestObject >( payloadObj );

        verifyTestObject( testObj );

        printTestObject( testObj );
    }

    {
        /*
         * Test the read-only behavior for each property type
         *
            BL_DM_DECLARE_UINT64_PROPERTY              ( id )
            BL_DM_DECLARE_MAP_PROPERTY                 ( userData, std::string )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY       ( numbers, int, get_int )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY       ( strings, std::string, get_str )
            BL_DM_DECLARE_INT_ALTERNATE_PROPERTY       ( number, "json_number" )
            BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY    ( str, "json_str" )
            BL_DM_DECLARE_CUSTOM_PROPERTY              ( custom )
            BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY      ( complexVector, ContainedTestObject )
            BL_DM_DECLARE_COMPLEX_MAP_PROPERTY         ( complexMap, ContainedTestObject )
        */

        auto testObj = TestObject::createInstance();

        UTF_REQUIRE( ! testObj -> readOnly() );
        testObj -> readOnly( true );
        UTF_REQUIRE( testObj -> readOnly() );

        /*
         * Test *Lvalue() setters
         */

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> userDataLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> numbersLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> stringsLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> strLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> customLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> complexVectorLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            testObj -> complexMapLvalue(),
            UnexpectedException,
            "Trying to modify a read only object"
            );

        {
            /*
             * Test const (reference) setters
             */

            const auto stringValue = std::string( "foo" );

            UTF_REQUIRE_THROW_MESSAGE(
                testObj -> number( 42 ),
                UnexpectedException,
                "Trying to modify a read only object"
                );

            UTF_REQUIRE_THROW_MESSAGE(
                testObj -> str( stringValue ),
                UnexpectedException,
                "Trying to modify a read only object"
                );
        }

        {
            /*
             * Test move semantics setters
             */

            auto stringValue = std::string( "foo" );
            auto customValue = json::readFromString( "{ \"name\": \"value\" }" );

            UTF_REQUIRE_THROW_MESSAGE(
                testObj -> number( 42 ),
                UnexpectedException,
                "Trying to modify a read only object"
                );

            UTF_REQUIRE_THROW_MESSAGE(
                testObj -> str( std::move( stringValue ) ),
                UnexpectedException,
                "Trying to modify a read only object"
                );

            UTF_REQUIRE_THROW_MESSAGE(
                testObj -> custom( std::move( customValue ) ),
                UnexpectedException,
                "Trying to modify a read only object"
                );
        }

        {
            /*
             * readOnly( true ) is SHALLOW
             *
             * BL_DM_DEFINE_CHECK_READ_ONLY() is emitted only in the owning object's setters,
             * *Lvalue() accessors and *Reset() methods, and the complex property getters return
             * a const reference to a container of om::ObjPtr< T > whose POINTEES are non-const -
             * so a caller can reach through a frozen parent and mutate a child model freely.
             * readOnly( true ) does not propagate the flag to children
             *
             * This records the CURRENT behaviour rather than a guarantee. The only production
             * user of the flag is AuthorizationServiceRest::create ( AuthorizationServiceRest.h
             * :250 and :282 ), whose safety rests on the config it freezes never being reached
             * through in this way. Anyone who deepens the guarantee has to edit the assertions
             * below deliberately
             *
             * A separate instance is used because testObj is reused by the unmapped() sub-block
             * below, which calls populateTestObject() and verifyTestObject() and therefore
             * requires exactly two complexVector elements and two complexMap entries
             */

            const auto obj = TestObject::createInstance();

            {
                const auto child1 = ContainedTestObject::createInstance();
                child1 -> strValue( "nested-1" );

                const auto child2 = ContainedTestObject::createInstance();
                child2 -> strValue( "nested-2" );

                const auto child3 = ContainedTestObject::createInstance();
                child3 -> strValue( "nested-3" );

                obj -> complexLvalue() = om::copy( child1 );
                obj -> complexVectorLvalue().push_back( om::copy( child2 ) );
                obj -> complexMapLvalue().emplace( "child3", om::copy( child3 ) );
            }

            obj -> readOnly( true );

            /*
             * The owning object is guarded ...
             */

            UTF_REQUIRE_THROW_MESSAGE(
                obj -> complexLvalue(),
                UnexpectedException,
                "Trying to modify a read only object"
                );

            /*
             * ... and none of its children are
             */

            UTF_REQUIRE_NO_THROW( obj -> complex() -> strValue( "changed-1" ) );
            UTF_REQUIRE_EQUAL( obj -> complex() -> strValue(), "changed-1" );

            UTF_REQUIRE_NO_THROW( obj -> complexVector()[ 0 ] -> strValue( "changed-2" ) );
            UTF_REQUIRE_EQUAL( obj -> complexVector()[ 0 ] -> strValue(), "changed-2" );

            UTF_REQUIRE_NO_THROW( obj -> complexMap().begin() -> second -> strValue( "changed-3" ) );
            UTF_REQUIRE_EQUAL( obj -> complexMap().begin() -> second -> strValue(), "changed-3" );
        }

        {
            /*
             * unmapped() behavior tests
             */

            testObj -> readOnly( false );
            UTF_REQUIRE( ! testObj -> readOnly() );

            UTF_REQUIRE_EQUAL( 0U, testObj -> unmapped().size() );

            populateTestObject( testObj );

            printTestObject( testObj );

            UTF_REQUIRE_EQUAL( 0U, testObj -> unmapped().size() );

            {
                const auto jsonText = dmu::getDocAsPrettyJsonString( testObj );

                testObj = dmu::loadFromJsonText< TestObject >( jsonText );

                UTF_REQUIRE_EQUAL( 0U, testObj -> unmapped().size() );
            }

            {
                auto jsonObj = dmu::getJsonObject( testObj );

                jsonObj[ "unmappedName" ] = "unmappedValue";

                testObj = dmu::loadFromJsonObject< TestObject >( std::move( jsonObj ) );

                UTF_REQUIRE_EQUAL( 1U, testObj -> unmapped().size() );

                verifyTestObject( testObj );

                printTestObject( testObj );
            }

            {
                const auto jsonText = dmu::getDocAsPrettyJsonString( testObj );

                testObj = dmu::loadFromJsonText< TestObject >( jsonText );

                UTF_REQUIRE_EQUAL( 1U, testObj -> unmapped().size() );
            }

            {
                auto jsonObj = dmu::getJsonObject( testObj );

                {
                    const auto& s = jsonObj.at( "unmappedName" ).as_string();
                    UTF_REQUIRE_EQUAL( std::string( s.c_str(), s.size() ), "unmappedValue" );
                }

                testObj = dmu::loadFromJsonObject< TestObject >( std::move( jsonObj ) );

                UTF_REQUIRE_EQUAL( 1U, testObj -> unmapped().size() );
            }
        }
    }
}


UTF_AUTO_TEST_CASE( CoreDataModelCanonicalNestedCollectionsTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    /*
     * Canonicalization must reach the children of the complex vector and map properties, not
     * just a complex property which hangs directly off the model
     *
     * Canonical serialization emits every property including the ones which were never set, so
     * that two objects with the same logical content serialize identically regardless of which
     * properties happen to have been assigned. Before the canonicalize flag was forwarded
     * through the collection macros, a child inside a vector or a map was serialized
     * non-canonically and silently dropped its unset properties, so the same child produced
     * different output depending only on where it was attached
     */

    const auto makePartiallyPopulatedChild = []() -> om::ObjPtr< ContainedTestObject >
    {
        auto child = ContainedTestObject::createInstance();

        /*
         * Note that boolValue, intValue and uint64Value are deliberately left unset
         */

        child -> strValue( "same content everywhere" );

        return child;
    };

    auto testObj = TestObject::createInstance();

    testObj -> complexLvalue() = makePartiallyPopulatedChild();
    testObj -> complexVectorLvalue().push_back( makePartiallyPopulatedChild() );
    testObj -> complexMapLvalue().emplace( "key", makePartiallyPopulatedChild() );

    const auto canonicalText =
        DataModelUtils::getJsonString( testObj, false /* prettyPrint */, true /* canonicalize */ );

    const auto canonical = json::readFromString( canonicalText );

    UTF_REQUIRE( canonical.is_object() );

    const auto& root = canonical.as_object();

    const auto& direct = root.at( "complex" ).as_object();
    const auto& fromVector = root.at( "complexVector" ).as_array().at( 0 ).as_object();
    const auto& fromMap = root.at( "complexMap" ).as_object().at( "key" ).as_object();

    /*
     * All four properties of the child must be present in every one of the three positions
     */

    UTF_REQUIRE_EQUAL( direct.size(), 4U );
    UTF_REQUIRE_EQUAL( fromVector.size(), direct.size() );
    UTF_REQUIRE_EQUAL( fromMap.size(), direct.size() );

    for( const auto& name : { "strValue", "boolValue", "intValue", "uint64Value" } )
    {
        UTF_REQUIRE( direct.contains( name ) );
        UTF_REQUIRE( fromVector.contains( name ) );
        UTF_REQUIRE( fromMap.contains( name ) );
    }

    /*
     * The same child content must therefore serialize to exactly the same bytes wherever it is
     * attached
     */

    UTF_REQUIRE_EQUAL(
        json::saveToString( json::value( direct ), false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ ),
        json::saveToString( json::value( fromVector ), false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ )
        );

    UTF_REQUIRE_EQUAL(
        json::saveToString( json::value( direct ), false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ ),
        json::saveToString( json::value( fromMap ), false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ )
        );
}

UTF_AUTO_TEST_CASE( DataModelLoadRequiresObjectAtTopLevel )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * A syntactically valid document whose top level is not an object is refused with a
     * JsonException which says so, on both backends, rather than surfacing as the backend's
     * own conversion error
     */

    const char* const documents[] = { "[1,2,3]", "42", "\"text\"", "null", "true" };

    for( const char* const document : documents )
    {
        UTF_REQUIRE_THROW_MESSAGE(
            dmu::loadFromJsonText< ContainedTestObject >( document ),
            JsonException,
            "must be an object at the top level"
            );
    }

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonValue< ContainedTestObject >( json::readFromString( "[1]" ) ),
        JsonException,
        "must be an object at the top level"
        );

    UTF_REQUIRE_NO_THROW( dmu::loadFromJsonText< ContainedTestObject >( "{}" ) );
}

UTF_AUTO_TEST_CASE( DataModelNullComplexCollectionsMeanAbsent )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * A null in place of any complex property, including the complex map, means "absent", the
     * same as for every other property kind; it must not reach the backend's object accessor
     */

    const auto obj = dmu::loadFromJsonText< TestObject >(
        R"({"complex":null,"complexVector":null,"complexMap":null,"userData":null,"numbers":null})"
        );

    UTF_REQUIRE( ! obj -> complex() );
    UTF_REQUIRE( obj -> complexVector().empty() );
    UTF_REQUIRE( obj -> complexMap().empty() );
    UTF_REQUIRE( obj -> userData().empty() );
    UTF_REQUIRE( obj -> numbers().empty() );

    UTF_REQUIRE_NO_THROW( dmu::getDocAsPrettyJsonString( obj ) );
}

UTF_AUTO_TEST_CASE( DataModelRequiredPropertyEnforcementTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * The required-property check is the model layer's only validation mechanism and it behaves
     * differently for the two property kinds:
     *
     * -- a scalar carries an IsSet flag, so an explicit 0 or false satisfies the requirement
     * -- a string has no IsSet, so emptiness IS absence and a key which is present but holds ""
     *    is rejected exactly like a missing one
     *
     * Note that it throws bl::UserMessageException, which is neither a bl::JsonException nor a
     * std::runtime_error, so it does not go through the property-context catch clauses and it
     * propagates unwrapped
     */

    /*
     * Loading - all required properties missing; the first one in declaration order wins
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< RequiredPropsTestObject >( "{}" ),
        bl::UserMessageException,
        "Required property 'requiredStr' is not provided when loading"
        );

    /*
     * An explicit null for a required scalar means absent
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< RequiredPropsTestObject >(
            R"({"requiredStr":"a","json_required_alt":"b","requiredInt":null,"requiredBool":true})"
            ),
        bl::UserMessageException,
        "Required property 'requiredInt' is not provided when loading"
        );

    /*
     * A required string which is present but empty is rejected too - the check sits outside the
     * found-branch of the deserializer, which is what a refactor is most likely to get wrong
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< RequiredPropsTestObject >(
            R"({"requiredStr":"","requiredInt":1,"requiredBool":false,"json_required_alt":"b"})"
            ),
        bl::UserMessageException,
        "Required property 'requiredStr' is not provided when loading"
        );

    /*
     * A required scalar which is present as 0 / false is accepted - it is the IsSet flag and not
     * the value which is tested
     */

    {
        om::ObjPtr< RequiredPropsTestObject > obj;

        UTF_REQUIRE_NO_THROW(
            obj = dmu::loadFromJsonText< RequiredPropsTestObject >(
                R"({"requiredStr":"a","requiredInt":0,"requiredBool":false,"json_required_alt":"b"})"
                )
            );

        UTF_REQUIRE( obj -> requiredIntIsSet() );
        UTF_REQUIRE( obj -> requiredBoolIsSet() );
        UTF_REQUIRE_EQUAL( obj -> requiredInt(), 0 );
        UTF_REQUIRE_EQUAL( obj -> requiredBool(), false );
    }

    /*
     * The alternate JSON name is the only name the property answers to - the C++ property name
     * is not a fallback
     */

    UTF_REQUIRE_THROW_MESSAGE(
        dmu::loadFromJsonText< RequiredPropsTestObject >(
            R"({"requiredStr":"a","requiredInt":0,"requiredBool":false,"requiredAlt":"b"})"
            ),
        bl::UserMessageException,
        "Required property 'requiredAlt' is not provided when loading"
        );

    /*
     * Saving - a fresh object is refused in both the packed and the pretty form
     */

    {
        const auto obj = RequiredPropsTestObject::createInstance();

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::getDocAsPackedJsonString( obj ),
            bl::UserMessageException,
            "Required property 'requiredStr' is not provided when saving"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::getDocAsPrettyJsonString( obj ),
            bl::UserMessageException,
            "Required property 'requiredStr' is not provided when saving"
            );
    }

    /*
     * The scalar half of the save check - both required strings are set, the required scalars
     * are not
     */

    {
        const auto obj = RequiredPropsTestObject::createInstance();

        obj -> requiredStr( "a" );
        obj -> requiredAlt( "b" );

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::getDocAsPackedJsonString( obj ),
            bl::UserMessageException,
            "Required property 'requiredInt' is not provided when saving"
            );
    }

    /*
     * A required string which is set back to an empty value becomes 'not provided' again
     */

    {
        const auto obj = RequiredPropsTestObject::createInstance();

        obj -> requiredStr( "x" );
        obj -> requiredAlt( "b" );
        obj -> requiredInt( 1 );
        obj -> requiredBool( true );

        UTF_REQUIRE_NO_THROW( dmu::getDocAsPackedJsonString( obj ) );

        obj -> requiredStr( str::empty() );

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::getDocAsPackedJsonString( obj ),
            bl::UserMessageException,
            "Required property 'requiredStr' is not provided when saving"
            );
    }

    {
        /*
         * Canonical serialization emits every property before it ever reaches the required
         * check, so the check is suppressed entirely - see the note above
         * BL_DM_DECLARE_SCALAR_SERIALIZATION in baselib/data/DataModelObjectDefs.h. A canonical
         * hash can therefore be taken over an object which packed serialization would refuse
         */

        const auto obj = RequiredPropsTestObject::createInstance();

        std::string canonicalText;

        UTF_REQUIRE_NO_THROW(
            canonicalText = dmu::getJsonString( obj, false /* prettyPrint */, true /* canonicalize */ )
            );

        UTF_REQUIRE_NO_THROW( dmu::getObjectHashCanonical( obj ) );

        const auto canonical = json::readFromString( canonicalText );

        UTF_REQUIRE( canonical.is_object() );

        const auto& root = canonical.as_object();

        UTF_REQUIRE_EQUAL( root.size(), 5U );

        for( const auto& name : { "requiredStr", "requiredInt", "requiredBool", "json_required_alt", "optionalStr" } )
        {
            UTF_REQUIRE( root.contains( name ) );
        }

        UTF_REQUIRE_EQUAL( json::value_to< std::string >( root.at( "requiredStr" ) ), str::empty() );
        UTF_REQUIRE_EQUAL( json::value_to< int >( root.at( "requiredInt" ) ), 0 );

        /*
         * And the asymmetry which follows from it - canonical output is not always loadable,
         * because the empty string it emits for an unset required string is rejected on load
         */

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::loadFromJsonText< RequiredPropsTestObject >( canonicalText ),
            bl::UserMessageException,
            "Required property 'requiredStr'"
            );
    }

    {
        /*
         * The same rules on the production models - and in particular the dependency of
         * MessagingHelpersDataIntegrityTest, which serializes an empty BrokerProtocol and only
         * succeeds because it goes through getObjectHashCanonical( ... )
         */

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::loadFromJsonText< bl::dm::ServerErrorResult >( "{}" ),
            bl::UserMessageException,
            "Required property 'message' is not provided when loading"
            );

        const auto bp = bl::dm::messaging::BrokerProtocol::createInstance();

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::getDocAsPackedJsonString( bp ),
            bl::UserMessageException,
            "Required property 'messageType' is not provided when saving"
            );

        UTF_REQUIRE_NO_THROW( dmu::getObjectHashCanonical( bp ) );
    }
}

UTF_AUTO_TEST_CASE( DataModelScalarIsSetAndResetTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * A scalar property has three states - unset, set to its default value, and set to
     * something else. The setter cannot produce the first one; only nameReset( ... ) and
     * deserialization from a document without the key can. Three production branches switch on
     * this tri-state, so an implementation in which nameIsSet( ... ) simply returned true, or in
     * which the setter forgot to raise the flag, would silently change their behaviour
     */

    const auto obj = ContainedTestObject::createInstance();

    UTF_REQUIRE( ! obj -> boolValueIsSet() );
    UTF_REQUIRE( ! obj -> intValueIsSet() );
    UTF_REQUIRE( ! obj -> uint64ValueIsSet() );

    UTF_REQUIRE_EQUAL( obj -> boolValue(), false );
    UTF_REQUIRE_EQUAL( obj -> intValue(), 0 );
    UTF_REQUIRE_EQUAL( obj -> uint64Value(), 0U );

    /*
     * Setting a scalar to its default value still marks it as set
     */

    obj -> boolValue( false );

    UTF_REQUIRE( obj -> boolValueIsSet() );
    UTF_REQUIRE_EQUAL( obj -> boolValue(), false );

    {
        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos != packed.find( "\"boolValue\":false" ) );
        UTF_REQUIRE( std::string::npos == packed.find( "intValue" ) );
    }

    obj -> boolValueReset();

    UTF_REQUIRE( ! obj -> boolValueIsSet() );
    UTF_REQUIRE_EQUAL( obj -> boolValue(), false );

    {
        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos == packed.find( "boolValue" ) );

        /*
         * A non-canonical round trip preserves the tri-state
         */

        const auto reloaded = dmu::loadFromJsonText< ContainedTestObject >( packed );

        UTF_REQUIRE( ! reloaded -> boolValueIsSet() );
    }

    {
        /*
         * A canonical round trip destroys it - canonicalization emits every property with its
         * default value and the loader then marks it as set; see the note above
         * BL_DM_DECLARE_SCALAR_SERIALIZATION in baselib/data/DataModelObjectDefs.h
         */

        const auto reloaded = dmu::loadFromJsonText< ContainedTestObject >(
            dmu::getJsonString( obj, false /* prettyPrint */, true /* canonicalize */ )
            );

        UTF_REQUIRE( reloaded -> boolValueIsSet() );
        UTF_REQUIRE_EQUAL( reloaded -> boolValue(), false );
    }

    {
        /*
         * An explicit null does not set the property, and since the key was never marked as
         * processed it is retained as unmapped
         */

        const auto o = dmu::loadFromJsonText< ContainedTestObject >( R"({"intValue":null})" );

        UTF_REQUIRE( ! o -> intValueIsSet() );
        UTF_REQUIRE_EQUAL( o -> intValue(), 0 );
        UTF_REQUIRE_EQUAL( o -> unmapped().size(), 1U );
    }

    {
        /*
         * An explicit zero does set it
         */

        const auto o = dmu::loadFromJsonText< ContainedTestObject >( R"({"intValue":0})" );

        UTF_REQUIRE( o -> intValueIsSet() );
        UTF_REQUIRE_EQUAL( o -> intValue(), 0 );
    }

    /*
     * nameReset( ... ) goes through the read-only guard, which is the one arm of the guard the
     * existing read-only coverage does not reach
     */

    obj -> readOnly( true );

    UTF_REQUIRE_THROW_MESSAGE(
        obj -> intValueReset(),
        bl::UnexpectedException,
        "Trying to modify a read only object"
        );

    /*
     * Deserialization writes the member directly, so read-only does NOT guard it - which is
     * what AuthorizationServiceRest::create( ... ) relies on when it flips the flag after the
     * configuration has been loaded
     */

    {
        auto rootValue = json::readFromString( R"({"intValue":9})" );

        bl::dm::SerializationContextBase context( std::move( rootValue.as_object() ) );

        UTF_REQUIRE_NO_THROW( obj -> serializeProperties( context ) );

        UTF_REQUIRE_EQUAL( obj -> intValue(), 9 );
    }
}

UTF_AUTO_TEST_CASE( DataModelSimpleContainerPropertyTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    {
        /*
         * A JSON string element of a non-string container goes through lexical_cast, which is
         * why a hand-written configuration with quoted status codes loads; the coercion is one
         * way, so the values are packed back as JSON numbers
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"intVector":["42",-7,13]})" );

        UTF_REQUIRE_EQUAL( obj -> intVector().size(), 3U );
        UTF_REQUIRE_EQUAL( obj -> intVector()[ 0 ], 42 );
        UTF_REQUIRE_EQUAL( obj -> intVector()[ 1 ], -7 );
        UTF_REQUIRE_EQUAL( obj -> intVector()[ 2 ], 13 );

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos != packed.find( "\"intVector\":[42,-7,13]" ) );
    }

    {
        /*
         * The set macro exists so that the values are always serialized in a canonical / stable
         * order, which is what the object hashing needs
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >(
            R"({"intSet":[3,1,2,1],"stringSet":["b","a","b"]})"
            );

        UTF_REQUIRE_EQUAL( obj -> intSet().size(), 3U );
        UTF_REQUIRE_EQUAL( obj -> stringSet().size(), 2U );

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos != packed.find( "\"intSet\":[1,2,3]" ) );
        UTF_REQUIRE( std::string::npos != packed.find( "\"stringSet\":[\"a\",\"b\"]" ) );

        const auto reloaded = dmu::loadFromJsonText< ContainerTestObject >( packed );

        const auto reversed = ContainerTestObject::createInstance();

        reversed -> intSetLvalue().insert( 2 );
        reversed -> intSetLvalue().insert( 1 );
        reversed -> intSetLvalue().insert( 3 );

        reversed -> stringSetLvalue().insert( "b" );
        reversed -> stringSetLvalue().insert( "a" );

        UTF_REQUIRE_EQUAL(
            dmu::getObjectHash( reloaded, str::empty(), false /* canonicalize */ ),
            dmu::getObjectHash( reversed, str::empty(), false /* canonicalize */ )
            );
    }

    {
        /*
         * The alternate JSON name is the only name which appears on the wire and the only one
         * the deserializer answers to
         */

        const auto obj = ContainerTestObject::createInstance();

        obj -> aliasVectorLvalue().push_back( "x" );

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos != packed.find( "\"json_alias_vector\":[\"x\"]" ) );
        UTF_REQUIRE( std::string::npos == packed.find( "aliasVector" ) );

        const auto loaded = dmu::loadFromJsonText< ContainerTestObject >( R"({"json_alias_vector":["x"]})" );

        UTF_REQUIRE_EQUAL( loaded -> aliasVector().size(), 1U );
        UTF_REQUIRE_EQUAL( loaded -> aliasVector()[ 0 ], "x" );
        UTF_REQUIRE( loaded -> unmapped().empty() );
    }

    {
        /*
         * An empty container is omitted unless the serialization is canonical
         */

        const auto obj = ContainerTestObject::createInstance();

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        for( const auto& name : { "intVector", "intSet", "stringSet", "json_alias_vector" } )
        {
            UTF_REQUIRE( std::string::npos == packed.find( name ) );
        }

        const auto canonical = json::readFromString(
            dmu::getJsonString( obj, false /* prettyPrint */, true /* canonicalize */ )
            );

        const auto& root = canonical.as_object();

        for( const auto& name : { "intVector", "intSet", "stringSet", "json_alias_vector" } )
        {
            UTF_REQUIRE( root.contains( name ) );
            UTF_REQUIRE( root.at( name ).is_array() );
            UTF_REQUIRE( root.at( name ).as_array().empty() );
        }
    }

    {
        /*
         * A null means absent, and since the key is not marked as processed it is retained as
         * unmapped and reappears on the way out
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"intVector":null})" );

        UTF_REQUIRE( obj -> intVector().empty() );
        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );

        std::string packed;

        UTF_REQUIRE_NO_THROW( packed = dmu::getDocAsPackedJsonString( obj ) );

        UTF_REQUIRE( std::string::npos != packed.find( "\"intVector\":null" ) );
    }

    {
        /*
         * An element of the wrong JSON kind goes through json::value_to, whose error is a
         * std::runtime_error, so it is remapped and named with the property it came from
         */

        UTF_REQUIRE_THROW_MESSAGE(
            dmu::loadFromJsonText< ContainerTestObject >( R"({"intVector":[true]})" ),
            bl::JsonException,
            "property 'intVector'"
            );

        /*
         * A coercion failure does NOT get that treatment - boost::bad_lexical_cast derives from
         * std::bad_cast and therefore from std::exception but not from std::runtime_error, so it
         * matches neither catch clause of BL_DM_IMPL_PROPERTY and escapes naming neither the
         * property nor the document. This pins today's behavior deliberately; if the catch list
         * is ever widened, this assertion has to be rewritten to expect a JsonException which
         * names 'intVector'
         */

        UTF_REQUIRE_THROW(
            dmu::loadFromJsonText< ContainerTestObject >( R"({"intVector":["abc"]})" ),
            bl::utils::bad_lexical_cast
            );
    }

    {
        /*
         * The deserializer fills a temporary and swaps it in only once the whole array has been
         * converted, so a failure part way through leaves the property untouched
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"intVector":[1,2]})" );

        UTF_REQUIRE_EQUAL( obj -> intVector().size(), 2U );

        auto rootValue = json::readFromString( R"({"intVector":[3,"abc"]})" );

        bl::dm::SerializationContextBase context( std::move( rootValue.as_object() ) );

        UTF_REQUIRE_THROW( obj -> serializeProperties( context ), bl::utils::bad_lexical_cast );

        UTF_REQUIRE_EQUAL( obj -> intVector().size(), 2U );
        UTF_REQUIRE_EQUAL( obj -> intVector()[ 0 ], 1 );
        UTF_REQUIRE_EQUAL( obj -> intVector()[ 1 ], 2 );
    }
}

UTF_AUTO_TEST_CASE( DataModelStringOrArrayPropertyTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * The value is always held as a vector of strings, but the JSON shape depends on the size at
     * serialization time - a single element is emitted as a bare string and anything else as an
     * array. The size boundary at one is the whole contract and it is wire visible
     */

    {
        /*
         * Scalar in, scalar out
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"json_aud":"one"})" );

        UTF_REQUIRE_EQUAL( obj -> audienceLike().size(), 1U );
        UTF_REQUIRE_EQUAL( obj -> audienceLike()[ 0 ], "one" );

        const auto packed = json::readFromString( dmu::getDocAsPackedJsonString( obj ) );

        UTF_REQUIRE( packed.as_object().at( "json_aud" ).is_string() );
    }

    {
        /*
         * An array of one in, a scalar out - the round trip is deliberately asymmetric
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"json_aud":["one"]})" );

        UTF_REQUIRE_EQUAL( obj -> audienceLike().size(), 1U );
        UTF_REQUIRE_EQUAL( obj -> audienceLike()[ 0 ], "one" );

        const auto packed = json::readFromString( dmu::getDocAsPackedJsonString( obj ) );

        UTF_REQUIRE( packed.as_object().at( "json_aud" ).is_string() );
    }

    {
        /*
         * Two or more elements come out as an array, in order - this is a vector and not a set
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"json_aud":["one","two"]})" );

        UTF_REQUIRE_EQUAL( obj -> audienceLike().size(), 2U );

        {
            const auto packed = json::readFromString( dmu::getDocAsPackedJsonString( obj ) );

            const auto& value = packed.as_object().at( "json_aud" );

            UTF_REQUIRE( value.is_array() );
            UTF_REQUIRE_EQUAL( value.as_array().size(), 2U );
            UTF_REQUIRE_EQUAL( json::value_to< std::string >( value.as_array().at( 0 ) ), "one" );
            UTF_REQUIRE_EQUAL( json::value_to< std::string >( value.as_array().at( 1 ) ), "two" );
        }

        /*
         * Crossing the boundary in memory changes the shape on the way out
         */

        obj -> audienceLikeLvalue().pop_back();

        {
            const auto packed = json::readFromString( dmu::getDocAsPackedJsonString( obj ) );

            UTF_REQUIRE( packed.as_object().at( "json_aud" ).is_string() );
        }
    }

    {
        /*
         * An empty value is omitted, and its canonical form is an empty array - not an empty
         * string and not a null - because the single element test fails and control falls into
         * the array path
         */

        const auto obj = ContainerTestObject::createInstance();

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos == packed.find( "json_aud" ) );

        const auto canonical = json::readFromString(
            dmu::getJsonString( obj, false /* prettyPrint */, true /* canonicalize */ )
            );

        const auto& value = canonical.as_object().at( "json_aud" );

        UTF_REQUIRE( value.is_array() );
        UTF_REQUIRE( value.as_array().empty() );
    }

    {
        /*
         * A null means absent and is retained as unmapped
         */

        const auto obj = dmu::loadFromJsonText< ContainerTestObject >( R"({"json_aud":null})" );

        UTF_REQUIRE( obj -> audienceLike().empty() );
        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );

        const auto packed = dmu::getDocAsPackedJsonString( obj );

        UTF_REQUIRE( std::string::npos != packed.find( "\"json_aud\":null" ) );
    }

    {
        /*
         * There is no IsSet and no const reference setter for this property kind - only the
         * lvalue accessor and the move setter, and both are behind the read-only guard
         */

        const auto obj = ContainerTestObject::createInstance();

        std::vector< std::string > value;
        value.push_back( "a" );
        value.push_back( "b" );

        obj -> audienceLike( std::move( value ) );

        UTF_REQUIRE_EQUAL( obj -> audienceLike().size(), 2U );

        obj -> readOnly( true );

        UTF_REQUIRE_THROW_MESSAGE(
            obj -> audienceLikeLvalue().push_back( "c" ),
            bl::UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            obj -> audienceLike( std::vector< std::string >() ),
            bl::UnexpectedException,
            "Trying to modify a read only object"
            );

        UTF_REQUIRE_EQUAL( obj -> audienceLike().size(), 2U );
    }
}

UTF_AUTO_TEST_CASE( DataModelUnmappedInteractionTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * BL_DM_PROPERTIES_IMPL_HANDLE_UNMAPPED() is gated on ! isPartial() and it appends the
     * unmapped entries with json::object::emplace, which does not overwrite - so a key which is
     * both mapped and present in m_unmapped is emitted with its MAPPED value
     *
     * Both halves of that are load bearing: DataModelUtils::castTo< ... >() moves a document
     * from a generic carrier to a concrete model through the unmapped bag and only works
     * because bl::dm::Payload is non partial, while bl::dm::FunctionContext is the repository's
     * only partial class and therefore silently drops everything it does not declare
     */

    {
        /*
         * The base default is partial and every generated model overrides it
         */

        UTF_REQUIRE( bl::dm::FunctionContext::isPartial() );
        UTF_REQUIRE( ! bl::dm::Payload::isPartial() );
        UTF_REQUIRE( ! TestObject::isPartial() );
        UTF_REQUIRE( bl::dm::DataModelObject::isPartial() );
    }

    const std::string principalDocument = R"({"securityPrincipal":{"sid":"s1"},"extra":"x"})";

    {
        /*
         * A partial class does not collect and does not re-emit the unknown properties
         */

        const auto fc = dmu::loadFromJsonText< bl::dm::FunctionContext >( principalDocument );

        UTF_REQUIRE( fc -> securityPrincipal() );
        UTF_REQUIRE_EQUAL( fc -> securityPrincipal() -> sid(), "s1" );
        UTF_REQUIRE( fc -> unmapped().empty() );

        UTF_REQUIRE( ! cpp::contains( dmu::getDocAsPackedJsonString( fc ), "extra" ) );
    }

    {
        /*
         * A non partial class preserves everything it does not declare, and that is exactly
         * what the REST pass-through relies on - BrokerProtocol::passThroughUserData is a
         * bl::dm::Payload which declares no property at all
         */

        const auto payload = dmu::loadFromJsonText< bl::dm::Payload >( principalDocument );

        UTF_REQUIRE_EQUAL( payload -> unmapped().size(), 2U );

        UTF_REQUIRE( cpp::contains( dmu::getDocAsPackedJsonString( payload ), "extra" ) );

        UTF_REQUIRE_EQUAL(
            dmu::castTo< bl::dm::FunctionContext >( payload ) -> securityPrincipal() -> sid(),
            "s1"
            );
    }

    {
        /*
         * A collision written through unmappedLvalue(): the mapped value wins on the way out
         * and serialization must not mutate m_unmapped
         */

        const auto obj = TestObject::createInstance();

        obj -> id( 7U );
        obj -> unmappedLvalue().emplace( "id", 999 );

        auto doc = dmu::getJsonObject( obj );

        UTF_REQUIRE_EQUAL( json::value_to< std::uint64_t >( doc.at( "id" ) ), 7U );
        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );

        /*
         * A round trip through that document resolves the collision - "id" is now processed by
         * the mapped deserializer, so it no longer lands in the unmapped bag
         */

        const auto reloaded = dmu::loadFromJsonObject< TestObject >( std::move( doc ) );

        UTF_REQUIRE_EQUAL( reloaded -> id(), 7U );
        UTF_REQUIRE( reloaded -> unmapped().empty() );
    }

    {
        /*
         * The second way of reaching a collision, and the one which is reachable from a peer
         * document: no deserializer marks an explicit null as processed, so a mapped property
         * whose value is null lands in m_unmapped as well
         */

        const auto o = dmu::loadFromJsonText< ContainedTestObject >( R"({"strValue":null,"intValue":3})" );

        UTF_REQUIRE( o -> strValue().empty() );
        UTF_REQUIRE_EQUAL( o -> intValue(), 3 );
        UTF_REQUIRE_EQUAL( o -> unmapped().size(), 1U );

        UTF_REQUIRE( cpp::contains( dmu::getDocAsPackedJsonString( o ), "\"strValue\":null" ) );

        /*
         * ... but the canonical form carries the MAPPED value, because canonicalize emits every
         * property first and the unmapped emplace which follows cannot overwrite it
         */

        const auto canonical = dmu::getJsonString( o, false /* prettyPrint */, true /* canonicalize */ );

        UTF_REQUIRE( cpp::contains( canonical, "\"strValue\":\"\"" ) );
        UTF_REQUIRE( ! cpp::contains( canonical, "\"strValue\":null" ) );
    }

    {
        /*
         * unmappedLvalue() is the one Lvalue accessor which does not emit
         * BL_DM_DEFINE_CHECK_READ_ONLY() - pinning today's behaviour, not endorsing it
         */

        const auto obj = TestObject::createInstance();

        obj -> readOnly( true );

        UTF_REQUIRE_NO_THROW( obj -> unmappedLvalue().emplace( "another", 1 ) );
        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );

        /*
         * ... while every other Lvalue accessor on the same object does check
         */

        UTF_REQUIRE_THROW_MESSAGE(
            obj -> strLvalue().clear(),
            UnexpectedException,
            "Trying to modify a read only object"
            );
    }
}

UTF_AUTO_TEST_CASE( DataModelDetectUnknownPropertiesTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace utest::dm;

    typedef DataModelUtils dmu;

    /*
     * SerializationContextBase::detectUnknownProperties() is a shipped public strict-parsing
     * mode with no production caller. DataModelUtils::loadFrom*() build the context internally
     * and never expose it, so the flag can only be reached by a caller which constructs the
     * context itself and invokes serializeProperties() directly - which is what this case does
     *
     * Note that every sub-block deserializes into a FRESH object: deserializing twice into the
     * same object leaves stale values behind and the assertions must not depend on those
     */

    const auto loadStrict = []( SAA_in const std::string& text ) -> om::ObjPtr< TestObject >
    {
        auto root = json::readFromString( text );

        SerializationContextBase context( std::move( root.as_object() ) );
        context.detectUnknownProperties( true );

        auto obj = TestObject::createInstance();

        obj -> serializeProperties( context );

        return obj;
    };

    {
        /*
         * An unknown property at the top level
         */

        UTF_REQUIRE_THROW_MESSAGE(
            loadStrict( R"({"id":1,"bogus":2})" ),
            UserMessageException,
            "Unrecognized property 'bogus' found while parsing JSON document"
            );
    }

    {
        /*
         * ... and nested inside each of the three complex property shapes, which propagate the
         * flag into the child context one line at a time - each of those three lines is
         * individually removable and only these three blocks notice
         */

        UTF_REQUIRE_THROW_MESSAGE(
            loadStrict( R"({"complex":{"strValue":"a","bogus":2}})" ),
            UserMessageException,
            "Unrecognized property 'bogus' found while parsing JSON document"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            loadStrict( R"({"complexVector":[{"strValue":"a","bogus":2}]})" ),
            UserMessageException,
            "Unrecognized property 'bogus' found while parsing JSON document"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            loadStrict( R"({"complexMap":{"k":{"strValue":"a","bogus":2}}})" ),
            UserMessageException,
            "Unrecognized property 'bogus' found while parsing JSON document"
            );
    }

    {
        /*
         * The flag defaults to off, which is what every DataModelUtils entry point uses - the
         * same four documents then simply retain the unknown property as unmapped
         */

        UTF_REQUIRE_NO_THROW( dmu::loadFromJsonText< TestObject >( R"({"id":1,"bogus":2})" ) );
        UTF_REQUIRE_NO_THROW( dmu::loadFromJsonText< TestObject >( R"({"complex":{"strValue":"a","bogus":2}})" ) );
        UTF_REQUIRE_NO_THROW( dmu::loadFromJsonText< TestObject >( R"({"complexVector":[{"strValue":"a","bogus":2}]})" ) );
        UTF_REQUIRE_NO_THROW( dmu::loadFromJsonText< TestObject >( R"({"complexMap":{"k":{"strValue":"a","bogus":2}}})" ) );

        const auto obj = dmu::loadFromJsonText< TestObject >( R"({"id":1,"bogus":2})" );

        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );
    }

    {
        /*
         * A clean document passes with the flag on, including a property whose JSON name is an
         * alternate one - it is the alternate name and not the member name which is recorded as
         * processed
         */

        om::ObjPtr< TestObject > obj;

        UTF_REQUIRE_NO_THROW( obj = loadStrict( R"({"id":1,"json_str":"s"})" ) );

        UTF_REQUIRE( obj );
        UTF_REQUIRE_EQUAL( obj -> id(), 1U );
        UTF_REQUIRE_EQUAL( obj -> str(), "s" );
    }

    {
        /*
         * The flag has no effect at all on a partial class - the check lives inside the
         * HANDLE_UNMAPPED block, which is gated on ! isPartial()
         */

        auto root = json::readFromString( R"({"securityPrincipal":{"sid":"s1"},"bogus":2})" );

        SerializationContextBase context( std::move( root.as_object() ) );
        context.detectUnknownProperties( true );

        const auto fc = bl::dm::FunctionContext::createInstance();

        UTF_REQUIRE_NO_THROW( fc -> serializeProperties( context ) );

        UTF_REQUIRE( fc -> securityPrincipal() );
        UTF_REQUIRE_EQUAL( fc -> securityPrincipal() -> sid(), "s1" );
    }
}
