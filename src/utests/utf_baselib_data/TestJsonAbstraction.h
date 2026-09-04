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

#ifndef __UTEST_TESTJSONABSTRACTION_H_
#define __UTEST_TESTJSONABSTRACTION_H_

#include <baselib/core/JsonUtils.h>
#include <baselib/data/DataModelObjectDefs.h>
#include <utests/baselib/Utf.h>

#include <sstream>
#include <cmath>
#include <limits>

namespace utest
{
    namespace json
    {
        /**
         * @brief Test helpers for JSON abstraction testing
         */

        /*
         * Log which JSON implementation is being used
         */
        inline void logImplementation()
        {
            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "Testing with JSON implementation: "
                    << bl::json::implName()
                );
        }

        /*
         * UTF_REQUIRE reports only the stringized expression, which for a recursive comparison is
         * the same text at every level and therefore useless. This logs the path first so a
         * failure says where in the document it happened
         */
        inline void requireAtPath(
            SAA_in          const bool                  condition,
            SAA_in          const std::string&          message
            )
        {
            if( ! condition )
            {
                UTF_MESSAGE( "JSON comparison failed: " + message );
            }

            UTF_REQUIRE( condition );
        }

        /*
         * Compare two JSON values for deep equality
         *
         * This is written out rather than using operator== so it works identically on both
         * backends, and so a mismatch reports the path at which it occurred rather than just
         * 'not equal'. Numbers are compared through their serialized form, which is what makes
         * this usable as a round-trip check: a value which survived a trip through text is equal
         * here if and only if it produces the same text
         */
        inline void verifyDeepEqual(
            SAA_in          const bl::json::value&      lhs,
            SAA_in          const bl::json::value&      rhs,
            SAA_in_opt      const std::string&          path = std::string( "$" )
            )
        {
            requireAtPath(
                lhs.is_object() == rhs.is_object() &&
                    lhs.is_array() == rhs.is_array() &&
                    lhs.is_string() == rhs.is_string() &&
                    lhs.is_bool() == rhs.is_bool() &&
                    lhs.is_null() == rhs.is_null(),
                "value kind differs at " + path
                );

            if( lhs.is_object() )
            {
                const auto& lhsObject = lhs.as_object();
                const auto& rhsObject = rhs.as_object();

                requireAtPath(
                    lhsObject.size() == rhsObject.size(),
                    "object size differs at " + path
                    );

                for( const auto& pair : lhsObject )
                {
                    const auto key = std::string( BL_JSON_PAIR_KEY( pair ) );

                    const auto pos = rhsObject.find( key );

                    requireAtPath(
                        pos != rhsObject.end(),
                        "object member '" + key + "' missing at " + path
                        );

                    verifyDeepEqual(
                        BL_JSON_PAIR_VALUE( pair ),
                        BL_JSON_ITER_VALUE( pos ),
                        path + "." + key
                        );
                }

                return;
            }

            if( lhs.is_array() )
            {
                const auto& lhsArray = lhs.as_array();
                const auto& rhsArray = rhs.as_array();

                requireAtPath(
                    lhsArray.size() == rhsArray.size(),
                    "array size differs at " + path
                    );

                for( std::size_t i = 0U; i < lhsArray.size(); ++i )
                {
                    verifyDeepEqual(
                        lhsArray[ i ],
                        rhsArray[ i ],
                        path + "[" + bl::utils::lexical_cast< std::string >( i ) + "]"
                        );
                }

                return;
            }

            /*
             * Primitives: compare the serialized form, which covers strings, bools, null and every
             * numeric kind without having to branch on which numeric kind the backend chose
             */

            requireAtPath(
                bl::json::saveToString( lhs ) == bl::json::saveToString( rhs ),
                "value differs at " + path +
                    ": '" + bl::json::saveToString( lhs ) +
                    "' vs '" + bl::json::saveToString( rhs ) + "'"
                );
        }

        /*
         * Verify JSON round-trip: parse -> serialize -> parse again
         *
         * Note that this asserts DEEP equality of the two parsed values, not merely that the
         * top level container kind and element count agree. A round trip which corrupted every
         * string value, mangled every number or dropped a nested key would pass the weaker check
         * and fails this one
         */
        inline void verifyRoundTrip( SAA_in const std::string& jsonText )
        {
            const auto parsed = bl::json::readFromString( jsonText );
            const auto serialized = bl::json::saveToString( parsed );
            const auto reparsed = bl::json::readFromString( serialized );

            verifyDeepEqual( parsed, reparsed );

            /*
             * Serialization is a pure function of the value, so a value which survived one round
             * trip must serialize identically on the next one; this pins the text as well as the
             * structure
             */

            UTF_REQUIRE_EQUAL( serialized, bl::json::saveToString( reparsed ) );
        }

        /*
         * Verify canonical serialization actually orders object keys
         *
         * Note that calling the same pure function twice and comparing the results, which is what
         * this helper used to do, asserts nothing: canonicalizeValue() has no state and no
         * iteration over an unordered container, so the two calls cannot disagree whether or not
         * the ordering is correct.
         *
         * What is asserted instead is the property canonical output exists for: that the bytes
         * depend only on the content and not on the order in which members were inserted. The
         * caller supplies two values which are equal as documents but were built in different
         * orders
         */
        inline void verifyCanonicalOrderIndependent(
            SAA_in          const bl::json::value&      lhs,
            SAA_in          const bl::json::value&      rhs
            )
        {
            const auto canonicalLhs =
                bl::json::saveToString( lhs, false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ );

            const auto canonicalRhs =
                bl::json::saveToString( rhs, false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ );

            UTF_REQUIRE_EQUAL( canonicalLhs, canonicalRhs );
        }

        /*
         * Assert the exact canonical bytes for a value
         *
         * This pins ordering, escaping and number formatting together, which is the only way any
         * of them is actually verified - the structural helpers above are all satisfied by output
         * which is correctly shaped but wrongly spelled
         */
        inline void verifyCanonicalText(
            SAA_in          const bl::json::value&      val,
            SAA_in          const std::string&          expected
            )
        {
            UTF_REQUIRE_EQUAL(
                bl::json::saveToString( val, false /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ ),
                expected
                );
        }

        /*
         * Create a simple test object
         */
        inline bl::json::object createSimpleTestObject()
        {
            bl::json::object obj;
            obj[ "name" ] = "test";
            obj[ "value" ] = 42;
            obj[ "flag" ] = true;
            return obj;
        }

        /*
         * Create a nested test structure
         */
        inline bl::json::value createNestedStructure()
        {
            bl::json::object root;

            bl::json::array items;
            items.push_back( bl::json::value( "item1" ) );
            items.push_back( bl::json::value( "item2" ) );
            items.push_back( bl::json::value( 123 ) );

            bl::json::object nested;
            nested[ "id" ] = 1;
            nested[ "active" ] = true;

            root[ "items" ] = items;
            root[ "config" ] = nested;
            root[ "title" ] = "Test";

            return root;
        }

        /*
         * Helper to compare doubles with tolerance
         */
        inline bool doubleEquals( SAA_in const double a, SAA_in const double b, SAA_in const double epsilon = 0.00001 )
        {
            return std::abs( a - b ) < epsilon;
        }

    } // json

} // utest

/********************************************************************************************
 * 1. Basic Type System Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonValueTypeCreation )
{
    utest::json::logImplementation();

    /*
     * Test creating values of each type
     */
    const auto nullVal = bl::json::value();
    const auto boolVal = bl::json::value( true );
    const auto int64Val = bl::json::value( static_cast< std::int64_t >( -123456 ) );
    const auto uint64Val = bl::json::value( static_cast< std::uint64_t >( 123456 ) );
    const auto doubleVal = bl::json::value( 3.14159 );
    const auto stringVal = bl::json::value( "hello" );
    const auto objectVal = bl::json::value( bl::json::object() );
    const auto arrayVal = bl::json::value( bl::json::array() );

    UTF_REQUIRE( nullVal.is_null() );
    UTF_REQUIRE( boolVal.is_bool() );
    UTF_REQUIRE( int64Val.is_int64() );
    UTF_REQUIRE( uint64Val.is_uint64() );
    UTF_REQUIRE( doubleVal.is_double() );
    UTF_REQUIRE( stringVal.is_string() );
    UTF_REQUIRE( objectVal.is_object() );
    UTF_REQUIRE( arrayVal.is_array() );
}

UTF_AUTO_TEST_CASE( JsonValueTypeChecking )
{
    utest::json::logImplementation();

    const auto stringVal = bl::json::value( "test" );

    /*
     * Verify correct type returns true
     */
    UTF_REQUIRE( stringVal.is_string() );

    /*
     * Verify incompatible types return false
     */
    UTF_REQUIRE( ! stringVal.is_null() );
    UTF_REQUIRE( ! stringVal.is_bool() );
    UTF_REQUIRE( ! stringVal.is_int64() );
    UTF_REQUIRE( ! stringVal.is_uint64() );
    UTF_REQUIRE( ! stringVal.is_double() );
    UTF_REQUIRE( ! stringVal.is_object() );
    UTF_REQUIRE( ! stringVal.is_array() );

    /*
     * Test empty containers
     */
    const auto emptyObj = bl::json::value( bl::json::object() );
    const auto emptyArr = bl::json::value( bl::json::array() );

    UTF_REQUIRE( emptyObj.is_object() );
    UTF_REQUIRE( ! emptyObj.is_array() );
    UTF_REQUIRE( emptyArr.is_array() );
    UTF_REQUIRE( ! emptyArr.is_object() );
}

UTF_AUTO_TEST_CASE( JsonValueTypeExtraction )
{
    utest::json::logImplementation();

    /*
     * Test extracting values using as_*() methods
     */
    const auto boolVal = bl::json::value( true );
    const auto int64Val = bl::json::value( static_cast< std::int64_t >( -42 ) );
    const auto uint64Val = bl::json::value( static_cast< std::uint64_t >( 42 ) );
    const auto doubleVal = bl::json::value( 2.71828 );
    const auto stringVal = bl::json::value( "world" );

    UTF_REQUIRE_EQUAL( boolVal.as_bool(), true );
    UTF_REQUIRE_EQUAL( int64Val.as_int64(), -42 );
    UTF_REQUIRE_EQUAL( uint64Val.as_uint64(), 42U );
    UTF_REQUIRE( utest::json::doubleEquals( doubleVal.as_double(), 2.71828 ) );
    UTF_REQUIRE_EQUAL( bl::json::get_str( stringVal ), "world" );

    /*
     * Test object and array extraction
     */
    auto obj = utest::json::createSimpleTestObject();
    const auto objVal = bl::json::value( obj );
    const auto& extractedObj = objVal.as_object();
    UTF_REQUIRE_EQUAL( extractedObj.size(), obj.size() );

    bl::json::array arr;
    arr.push_back( bl::json::value( 1 ) );
    arr.push_back( bl::json::value( 2 ) );
    const auto arrVal = bl::json::value( arr );
    const auto& extractedArr = arrVal.as_array();
    UTF_REQUIRE_EQUAL( extractedArr.size(), 2U );
}

/********************************************************************************************
 * 2. Parsing Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonParseSimpleObject )
{
    utest::json::logImplementation();

    const std::string jsonText = R"({"name":"test","value":42,"flag":true})";
    const auto parsed = bl::json::readFromString( jsonText );

    UTF_REQUIRE( parsed.is_object() );
    const auto& obj = parsed.as_object();

    UTF_REQUIRE_EQUAL( obj.size(), 3U );
    UTF_REQUIRE( obj.contains( "name" ) );
    UTF_REQUIRE( obj.contains( "value" ) );
    UTF_REQUIRE( obj.contains( "flag" ) );

    UTF_REQUIRE_EQUAL( bl::json::get_str( obj.at( "name" ) ), "test" );
    UTF_REQUIRE_EQUAL( obj.at( "value" ).as_int64(), 42 );
    UTF_REQUIRE_EQUAL( obj.at( "flag" ).as_bool(), true );
}

UTF_AUTO_TEST_CASE( JsonParseSimpleArray )
{
    utest::json::logImplementation();

    const std::string jsonText = R"([1,2,3,"four",true,null])";
    const auto parsed = bl::json::readFromString( jsonText );

    UTF_REQUIRE( parsed.is_array() );
    const auto& arr = parsed.as_array();

    UTF_REQUIRE_EQUAL( arr.size(), 6U );
    UTF_REQUIRE_EQUAL( arr.at( 0 ).as_int64(), 1 );
    UTF_REQUIRE_EQUAL( arr.at( 1 ).as_int64(), 2 );
    UTF_REQUIRE_EQUAL( arr.at( 2 ).as_int64(), 3 );
    UTF_REQUIRE_EQUAL( bl::json::get_str( arr.at( 3 ) ), "four" );
    UTF_REQUIRE_EQUAL( arr.at( 4 ).as_bool(), true );
    UTF_REQUIRE( arr.at( 5 ).is_null() );
}

UTF_AUTO_TEST_CASE( JsonParseNestedStructures )
{
    utest::json::logImplementation();

    const std::string jsonText = R"({
        "outer": {
            "inner": {
                "value": 123
            }
        },
        "list": [1, [2, 3], {"nested": true}]
    })";

    const auto parsed = bl::json::readFromString( jsonText );
    UTF_REQUIRE( parsed.is_object() );

    const auto& root = parsed.as_object();
    UTF_REQUIRE( root.contains( "outer" ) );
    UTF_REQUIRE( root.contains( "list" ) );

    const auto& outer = root.at( "outer" ).as_object();
    const auto& inner = outer.at( "inner" ).as_object();
    UTF_REQUIRE_EQUAL( inner.at( "value" ).as_int64(), 123 );

    const auto& list = root.at( "list" ).as_array();
    UTF_REQUIRE_EQUAL( list.size(), 3U );
    UTF_REQUIRE( list.at( 1 ).is_array() );
    UTF_REQUIRE( list.at( 2 ).is_object() );
}

UTF_AUTO_TEST_CASE( JsonParsePrimitives )
{
    utest::json::logImplementation();

    /*
     * Test parsing all primitive types
     */
    UTF_REQUIRE( bl::json::readFromString( "null" ).is_null() );
    UTF_REQUIRE_EQUAL( bl::json::readFromString( "true" ).as_bool(), true );
    UTF_REQUIRE_EQUAL( bl::json::readFromString( "false" ).as_bool(), false );
    UTF_REQUIRE_EQUAL( bl::json::readFromString( "42" ).as_int64(), 42 );
    UTF_REQUIRE_EQUAL( bl::json::readFromString( "0" ).as_int64(), 0 );
    UTF_REQUIRE_EQUAL( bl::json::readFromString( "-123" ).as_int64(), -123 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::readFromString( "3.14" ).as_double(), 3.14, 0.001 ) );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::readFromString( "1.23e10" ).as_double(), 1.23e10, 1e6 ) );
    UTF_REQUIRE_EQUAL( bl::json::get_str( bl::json::readFromString( "\"hello\"" ) ), "hello" );
}

UTF_AUTO_TEST_CASE( JsonParseEmptyValues )
{
    utest::json::logImplementation();

    /*
     * Test parsing empty containers and strings
     */
    const auto emptyObj = bl::json::readFromString( "{}" );
    UTF_REQUIRE( emptyObj.is_object() );
    UTF_REQUIRE_EQUAL( emptyObj.as_object().size(), 0U );

    const auto emptyArr = bl::json::readFromString( "[]" );
    UTF_REQUIRE( emptyArr.is_array() );
    UTF_REQUIRE_EQUAL( emptyArr.as_array().size(), 0U );

    const auto emptyStr = bl::json::readFromString( "\"\"" );
    UTF_REQUIRE( emptyStr.is_string() );
    UTF_REQUIRE_EQUAL( bl::json::get_str( emptyStr ).length(), 0U );
}

UTF_AUTO_TEST_CASE( JsonParseFromStream )
{
    utest::json::logImplementation();

    const std::string jsonText = R"({"stream":"test","value":999})";
    std::istringstream iss( jsonText );

    const auto parsed = bl::json::readFromStream( iss );
    UTF_REQUIRE( parsed.is_object() );

    const auto& obj = parsed.as_object();
    UTF_REQUIRE_EQUAL( bl::json::get_str( obj.at( "stream" ) ), "test" );
    UTF_REQUIRE_EQUAL( obj.at( "value" ).as_int64(), 999 );
}

UTF_AUTO_TEST_CASE( JsonParseMalformed )
{
    utest::json::logImplementation();

    /*
     * Test that malformed JSON throws exceptions
     */
    UTF_REQUIRE_THROW( bl::json::readFromString( "{invalid}" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "[1,2,]" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "{\"key\":}" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "\"unterminated" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "{" ), bl::JsonException );
}

UTF_AUTO_TEST_CASE( JsonParseUnicode )
{
    utest::json::logImplementation();

    /*
     * Test parsing Unicode characters
     */
    const std::string unicodeJson = R"({"emoji":"😀","chinese":"你好","text":"Café"})";
    const auto parsed = bl::json::readFromString( unicodeJson );

    UTF_REQUIRE( parsed.is_object() );
    const auto& obj = parsed.as_object();

    UTF_REQUIRE( obj.contains( "emoji" ) );
    UTF_REQUIRE( obj.contains( "chinese" ) );
    UTF_REQUIRE( obj.contains( "text" ) );

    /*
     * Just verify we can parse and extract - exact string comparison may vary
     */
    UTF_REQUIRE( obj.at( "emoji" ).is_string() );
    UTF_REQUIRE( obj.at( "chinese" ).is_string() );
    UTF_REQUIRE( obj.at( "text" ).is_string() );
}

UTF_AUTO_TEST_CASE( JsonParseLargeNumbers )
{
    utest::json::logImplementation();

    /*
     * Test parsing boundary values
     */
    const auto int64Max = bl::json::readFromString( "9223372036854775807" );
    UTF_REQUIRE_EQUAL( int64Max.as_int64(), std::numeric_limits< std::int64_t >::max() );

    const auto int64Min = bl::json::readFromString( "-9223372036854775808" );
    UTF_REQUIRE_EQUAL( int64Min.as_int64(), std::numeric_limits< std::int64_t >::min() );

    const auto uint64Max = bl::json::readFromString( "18446744073709551615" );
    UTF_REQUIRE_EQUAL( uint64Max.as_uint64(), std::numeric_limits< std::uint64_t >::max() );

    /*
     * Test large double
     */
    const auto largeDouble = bl::json::readFromString( "1.7976931348623157e+308" );
    UTF_REQUIRE( largeDouble.is_double() );
}

/********************************************************************************************
 * 3. Serialization Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonSerializeCompact )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();
    const auto serialized = bl::json::saveToString( obj, false /* prettyPrint */ );

    /*
     * Verify output is valid JSON and compact (no newlines)
     */
    UTF_REQUIRE( serialized.find( '\n' ) == std::string::npos );

    /*
     * Verify it can be parsed back
     */
    const auto parsed = bl::json::readFromString( serialized );
    UTF_REQUIRE( parsed.is_object() );
    UTF_REQUIRE_EQUAL( parsed.as_object().size(), 3U );
}

UTF_AUTO_TEST_CASE( JsonSerializePretty )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();
    const auto serialized = bl::json::saveToString( obj, true /* prettyPrint */ );

    /*
     * Verify output contains newlines (pretty-printed)
     */
    UTF_REQUIRE( serialized.find( '\n' ) != std::string::npos );

    /*
     * Verify it can be parsed back
     */
    const auto parsed = bl::json::readFromString( serialized );
    UTF_REQUIRE( parsed.is_object() );
    UTF_REQUIRE_EQUAL( parsed.as_object().size(), 3U );
}

UTF_AUTO_TEST_CASE( JsonSerializeCanonical )
{
    utest::json::logImplementation();

    /*
     * Create object with keys in non-alphabetical order
     */
    bl::json::object obj;
    obj[ "zebra" ] = "last";
    obj[ "apple" ] = "first";
    obj[ "middle" ] = "middle";

    const auto canonical = bl::json::saveToString( obj, false, false, true /* canonicalize */ );

    /*
     * In canonical form, keys should be sorted
     * Verify "apple" appears before "middle" which appears before "zebra"
     */
    const auto applePos = canonical.find( "apple" );
    const auto middlePos = canonical.find( "middle" );
    const auto zebraPos = canonical.find( "zebra" );

    UTF_REQUIRE( applePos < middlePos );
    UTF_REQUIRE( middlePos < zebraPos );

    /*
     * Verify the canonical bytes depend only on content, not on insertion order, by building the
     * same document in the opposite order
     */

    bl::json::object reversed;
    reversed[ "middle" ] = "middle";
    reversed[ "apple" ] = "first";
    reversed[ "zebra" ] = "last";

    utest::json::verifyCanonicalOrderIndependent( bl::json::value( obj ), bl::json::value( reversed ) );

    /*
     * And pin the exact bytes, which asserts the ordering, the escaping and the absence of
     * incidental whitespace all at once
     */

    utest::json::verifyCanonicalText(
        bl::json::value( obj ),
        "{\"apple\":\"first\",\"middle\":\"middle\",\"zebra\":\"last\"}"
        );
}

UTF_AUTO_TEST_CASE( JsonSerializeToStream )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();
    std::ostringstream oss;

    bl::json::saveToStream( obj, oss, false /* prettyPrint */ );

    const auto serialized = oss.str();
    UTF_REQUIRE( ! serialized.empty() );

    /*
     * Verify it can be parsed back
     */
    const auto parsed = bl::json::readFromString( serialized );
    UTF_REQUIRE( parsed.is_object() );
}

UTF_AUTO_TEST_CASE( JsonSerializeOptions )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();

    /*
     * Test that canonicalize + prettyPrint throws exception
     */
    UTF_REQUIRE_THROW(
        bl::json::saveToString( obj, true /* prettyPrint */, false /* rawUtf8 */, true /* canonicalize */ ),
        bl::ArgumentException
        );

    /*
     * Test compact serialization
     */
    const auto compact = bl::json::saveToString( obj, false, false, false );
    UTF_REQUIRE( compact.find( '\n' ) == std::string::npos );

    /*
     * Test canonical serialization
     */
    const auto canonical = bl::json::saveToString( obj, false, false, true );
    UTF_REQUIRE( canonical.find( '\n' ) == std::string::npos );

    /*
     * The simple test object is built as name / value / flag, so canonical form must reorder it
     * to flag / name / value
     */

    utest::json::verifyCanonicalText(
        bl::json::value( obj ),
        "{\"flag\":true,\"name\":\"test\",\"value\":42}"
        );
}

/********************************************************************************************
 * 4. Accessor Function Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonAccessorGetStr )
{
    utest::json::logImplementation();

    const auto val = bl::json::value( "test string" );
    UTF_REQUIRE_EQUAL( bl::json::get_str( val ), "test string" );

    const auto empty = bl::json::value( "" );
    UTF_REQUIRE_EQUAL( bl::json::get_str( empty ), "" );

    const auto unicode = bl::json::value( "Café ☕" );
    UTF_REQUIRE( bl::json::get_str( unicode ).length() > 0 );
}

UTF_AUTO_TEST_CASE( JsonAccessorGetBool )
{
    utest::json::logImplementation();

    const auto trueVal = bl::json::value( true );
    const auto falseVal = bl::json::value( false );

    UTF_REQUIRE_EQUAL( bl::json::get_bool( trueVal ), true );
    UTF_REQUIRE_EQUAL( bl::json::get_bool( falseVal ), false );
}

UTF_AUTO_TEST_CASE( JsonAccessorGetIntegers )
{
    utest::json::logImplementation();

    /*
     * Test get_int()
     */
    const auto positiveInt = bl::json::value( static_cast< std::int64_t >( 42 ) );
    const auto negativeInt = bl::json::value( static_cast< std::int64_t >( -123 ) );
    const auto zero = bl::json::value( static_cast< std::int64_t >( 0 ) );

    UTF_REQUIRE_EQUAL( bl::json::get_int( positiveInt ), 42 );
    UTF_REQUIRE_EQUAL( bl::json::get_int( negativeInt ), -123 );
    UTF_REQUIRE_EQUAL( bl::json::get_int( zero ), 0 );

    /*
     * Test get_int64()
     */
    const auto largeInt = bl::json::value( std::numeric_limits< std::int64_t >::max() );
    UTF_REQUIRE_EQUAL( bl::json::get_int64( largeInt ), std::numeric_limits< std::int64_t >::max() );

    /*
     * Test get_uint64()
     */
    const auto largeUint = bl::json::value( std::numeric_limits< std::uint64_t >::max() );
    UTF_REQUIRE_EQUAL( bl::json::get_uint64( largeUint ), std::numeric_limits< std::uint64_t >::max() );

    /*
     * Test that get_uint64() rejects negative values
     */
    const auto negative = bl::json::value( static_cast< std::int64_t >( -1 ) );
    UTF_REQUIRE_THROW( bl::json::get_uint64( negative ), bl::JsonException );
}

UTF_AUTO_TEST_CASE( JsonAccessorGetReal )
{
    utest::json::logImplementation();

    const auto pi = bl::json::value( 3.14159 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( pi ), 3.14159 ) );

    const auto e = bl::json::value( 2.71828 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( e ), 2.71828 ) );

    const auto negative = bl::json::value( -1.5 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( negative ), -1.5, 0.001 ) );

    const auto zero = bl::json::value( 0.0 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( zero ), 0.0, 0.001 ) );
}

UTF_AUTO_TEST_CASE( JsonAccessorValueTo )
{
    utest::json::logImplementation();

    /*
     * Test value_to<T>() template with various types
     */
    const auto boolVal = bl::json::value( true );
    UTF_REQUIRE_EQUAL( bl::json::value_to< bool >( boolVal ), true );

    const auto intVal = bl::json::value( static_cast< std::int64_t >( 42 ) );
    UTF_REQUIRE_EQUAL( bl::json::value_to< int >( intVal ), 42 );

    const auto int64Val = bl::json::value( static_cast< std::int64_t >( -999 ) );
    UTF_REQUIRE_EQUAL( bl::json::value_to< std::int64_t >( int64Val ), -999 );

    const auto uint64Val = bl::json::value( static_cast< std::uint64_t >( 12345 ) );
    UTF_REQUIRE_EQUAL( bl::json::value_to< std::uint64_t >( uint64Val ), 12345U );

    const auto doubleVal = bl::json::value( 6.28 );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::value_to< double >( doubleVal ), 6.28, 0.001 ) );

    const auto stringVal = bl::json::value( "convert" );
    UTF_REQUIRE_EQUAL( bl::json::value_to< std::string >( stringVal ), "convert" );
}

/********************************************************************************************
 * 5. Object Operations Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonObjectConstruction )
{
    utest::json::logImplementation();

    /*
     * Create empty object
     */
    bl::json::object obj;
    UTF_REQUIRE_EQUAL( obj.size(), 0U );

    /*
     * Add properties
     */
    obj[ "name" ] = "test";
    obj[ "count" ] = 5;
    obj[ "enabled" ] = true;

    UTF_REQUIRE_EQUAL( obj.size(), 3U );
    UTF_REQUIRE( obj.contains( "name" ) );
    UTF_REQUIRE( obj.contains( "count" ) );
    UTF_REQUIRE( obj.contains( "enabled" ) );
}

UTF_AUTO_TEST_CASE( JsonObjectPropertyAccess )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();

    /*
     * Test .at() for existing keys
     */
    UTF_REQUIRE_EQUAL( bl::json::get_str( obj.at( "name" ) ), "test" );
    UTF_REQUIRE_EQUAL( obj.at( "value" ).as_int64(), 42 );
    UTF_REQUIRE_EQUAL( obj.at( "flag" ).as_bool(), true );

    /*
     * Test .find() with valid key
     */
    auto it = obj.find( "name" );
    UTF_REQUIRE( it != obj.end() );

    /*
     * Test .find() with invalid key
     */
    auto missing = obj.find( "nonexistent" );
    UTF_REQUIRE( missing == obj.end() );

    /*
     * Test .contains()
     */
    UTF_REQUIRE( obj.contains( "name" ) );
    UTF_REQUIRE( ! obj.contains( "missing" ) );
}

UTF_AUTO_TEST_CASE( JsonObjectIteration )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();

    /*
     * Test range-based for loop
     */
    std::size_t count = 0;
    for( const auto& pair : obj )
    {
        /*
         * Verify we can access key and value
         */
        const auto& key = BL_JSON_PAIR_KEY( pair );
        const auto& value = BL_JSON_PAIR_VALUE( pair );

        UTF_REQUIRE( ! std::string( key ).empty() );
        UTF_REQUIRE( ! value.is_null() );

        ++count;
    }

    UTF_REQUIRE_EQUAL( count, obj.size() );
}

UTF_AUTO_TEST_CASE( JsonObjectMacros )
{
    utest::json::logImplementation();

    auto obj = utest::json::createSimpleTestObject();

    /*
     * Test BL_JSON_PAIR_KEY and BL_JSON_PAIR_VALUE macros
     */
    for( const auto& pair : obj )
    {
        const auto& key = BL_JSON_PAIR_KEY( pair );
        const auto& value = BL_JSON_PAIR_VALUE( pair );

        if( std::string( key ) == "name" )
        {
            UTF_REQUIRE( value.is_string() );
            UTF_REQUIRE_EQUAL( bl::json::get_str( value ), "test" );
        }
        else if( std::string( key ) == "value" )
        {
            UTF_REQUIRE( value.is_int64() || value.is_uint64() );
            UTF_REQUIRE_EQUAL( value.as_int64(), 42 );
        }
        else if( std::string( key ) == "flag" )
        {
            UTF_REQUIRE( value.is_bool() );
            UTF_REQUIRE_EQUAL( value.as_bool(), true );
        }
    }

    /*
     * Test BL_JSON_ITER_VALUE macro
     */
    auto it = obj.find( "name" );
    UTF_REQUIRE( it != obj.end() );
    const auto& value = BL_JSON_ITER_VALUE( it );
    UTF_REQUIRE( value.is_string() );
    UTF_REQUIRE_EQUAL( bl::json::get_str( value ), "test" );
}

/********************************************************************************************
 * 6. Array Operations Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonArrayConstruction )
{
    utest::json::logImplementation();

    /*
     * Create empty array
     */
    bl::json::array arr;
    UTF_REQUIRE_EQUAL( arr.size(), 0U );

    /*
     * Add elements
     */
    arr.push_back( bl::json::value( 1 ) );
    arr.push_back( bl::json::value( "two" ) );
    arr.push_back( bl::json::value( true ) );

    UTF_REQUIRE_EQUAL( arr.size(), 3U );
}

UTF_AUTO_TEST_CASE( JsonArrayElementAccess )
{
    utest::json::logImplementation();

    bl::json::array arr;
    arr.push_back( bl::json::value( 10 ) );
    arr.push_back( bl::json::value( 20 ) );
    arr.push_back( bl::json::value( 30 ) );

    /*
     * Test .at() for valid indices
     */
    UTF_REQUIRE_EQUAL( arr.at( 0 ).as_int64(), 10 );
    UTF_REQUIRE_EQUAL( arr.at( 1 ).as_int64(), 20 );
    UTF_REQUIRE_EQUAL( arr.at( 2 ).as_int64(), 30 );

    /*
     * Test subscript operator
     */
    UTF_REQUIRE_EQUAL( arr[ 0 ].as_int64(), 10 );
    UTF_REQUIRE_EQUAL( arr[ 1 ].as_int64(), 20 );
    UTF_REQUIRE_EQUAL( arr[ 2 ].as_int64(), 30 );
}

UTF_AUTO_TEST_CASE( JsonArrayIteration )
{
    utest::json::logImplementation();

    bl::json::array arr;
    arr.push_back( bl::json::value( 1 ) );
    arr.push_back( bl::json::value( 2 ) );
    arr.push_back( bl::json::value( 3 ) );

    /*
     * Test range-based for loop
     */
    int expected = 1;
    for( const auto& item : arr )
    {
        UTF_REQUIRE_EQUAL( item.as_int64(), expected );
        ++expected;
    }

    UTF_REQUIRE_EQUAL( expected, 4 );
}

/********************************************************************************************
 * 7. Nested Structure Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonNestedComplexStructures )
{
    utest::json::logImplementation();

    /*
     * Test objects containing arrays
     */
    bl::json::object obj;
    bl::json::array numbers;
    numbers.push_back( bl::json::value( 1 ) );
    numbers.push_back( bl::json::value( 2 ) );
    obj[ "numbers" ] = numbers;

    UTF_REQUIRE( obj.at( "numbers" ).is_array() );
    UTF_REQUIRE_EQUAL( obj.at( "numbers" ).as_array().size(), 2U );

    /*
     * Test arrays containing objects
     */
    bl::json::array items;
    bl::json::object item1;
    item1[ "id" ] = 1;
    items.push_back( item1 );

    UTF_REQUIRE( items.at( 0 ).is_object() );
    UTF_REQUIRE_EQUAL( items.at( 0 ).as_object().at( "id" ).as_int64(), 1 );
}

UTF_AUTO_TEST_CASE( JsonDeeplyNestedStructures )
{
    utest::json::logImplementation();

    /*
     * Create 20-level deep nested structure
     */
    bl::json::value deepest( "deep value" );
    bl::json::value current = deepest;

    for( int i = 0; i < 20; ++i )
    {
        bl::json::object wrapper;
        wrapper[ "nested" ] = current;
        current = wrapper;
    }

    /*
     * Navigate back down
     */
    bl::json::value nav = current;
    for( int i = 0; i < 20; ++i )
    {
        UTF_REQUIRE( nav.is_object() );
        nav = nav.as_object().at( "nested" );
    }

    UTF_REQUIRE( nav.is_string() );
    UTF_REQUIRE_EQUAL( bl::json::get_str( nav ), "deep value" );
}

UTF_AUTO_TEST_CASE( JsonLargeDocuments )
{
    utest::json::logImplementation();

    /*
     * Create array with 1000 elements
     */
    bl::json::array largeArray;
    for( int i = 0; i < 1000; ++i )
    {
        largeArray.push_back( bl::json::value( i ) );
    }

    UTF_REQUIRE_EQUAL( largeArray.size(), 1000U );
    UTF_REQUIRE_EQUAL( largeArray.at( 0 ).as_int64(), 0 );
    UTF_REQUIRE_EQUAL( largeArray.at( 999 ).as_int64(), 999 );

    /*
     * Create object with 100 properties
     */
    bl::json::object largeObject;
    for( int i = 0; i < 100; ++i )
    {
        largeObject[ "key" + std::to_string( i ) ] = i;
    }

    UTF_REQUIRE_EQUAL( largeObject.size(), 100U );
}

UTF_AUTO_TEST_CASE( JsonRoundTrip )
{
    utest::json::logImplementation();

    /*
     * Test round-trip with various JSON structures
     */
    utest::json::verifyRoundTrip( R"({"simple":"object"})" );
    utest::json::verifyRoundTrip( R"([1,2,3,4,5])" );
    utest::json::verifyRoundTrip( R"({"nested":{"deep":{"value":42}}})" );
    utest::json::verifyRoundTrip( R"({"array":[1,"two",true,null]})" );
    utest::json::verifyRoundTrip( R"({})" );
    utest::json::verifyRoundTrip( R"([])" );
}

/********************************************************************************************
 * 8. Error Handling Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonErrorMalformedInput )
{
    utest::json::logImplementation();

    /*
     * Test various malformed JSON inputs
     */
    UTF_REQUIRE_THROW( bl::json::readFromString( "" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "{" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "}" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "[" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "]" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "{,}" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( "[,]" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( R"({"key":})" ), bl::JsonException );
    UTF_REQUIRE_THROW( bl::json::readFromString( R"({"key"::value})" ), bl::JsonException );
}

UTF_AUTO_TEST_CASE( JsonErrorTypeMismatch )
{
    utest::json::logImplementation();

    const auto stringVal = bl::json::value( "not a number" );

    /*
     * Attempting to access as wrong type should throw
     */
    UTF_REQUIRE_THROW( stringVal.as_int64(), std::exception );
    UTF_REQUIRE_THROW( stringVal.as_bool(), std::exception );
    UTF_REQUIRE_THROW( stringVal.as_object(), std::exception );
    UTF_REQUIRE_THROW( stringVal.as_array(), std::exception );

    const auto numberVal = bl::json::value( 42 );
    UTF_REQUIRE_THROW( numberVal.as_string(), std::exception );
}

UTF_AUTO_TEST_CASE( JsonErrorExceptionContext )
{
    utest::json::logImplementation();

    /*
     * Test that exceptions provide meaningful context
     */
    try
    {
        bl::json::readFromString( "{invalid json}" );
        UTF_FAIL( "Expected JsonException to be thrown" );
    }
    catch( const bl::JsonException& e )
    {
        /*
         * Verify exception was thrown
         * Message content may vary between implementations
         */
        const std::string msg = e.what();
        UTF_REQUIRE( ! msg.empty() );
    }
}

/********************************************************************************************
 * 9. Edge Cases Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonEdgeCasesBoundaryValues )
{
    utest::json::logImplementation();

    /*
     * Test integer boundaries
     */
    const auto int64Max = bl::json::value( std::numeric_limits< std::int64_t >::max() );
    UTF_REQUIRE_EQUAL( int64Max.as_int64(), std::numeric_limits< std::int64_t >::max() );

    const auto int64Min = bl::json::value( std::numeric_limits< std::int64_t >::min() );
    UTF_REQUIRE_EQUAL( int64Min.as_int64(), std::numeric_limits< std::int64_t >::min() );

    const auto uint64Max = bl::json::value( std::numeric_limits< std::uint64_t >::max() );
    UTF_REQUIRE_EQUAL( uint64Max.as_uint64(), std::numeric_limits< std::uint64_t >::max() );

    /*
     * Test empty string
     */
    const auto emptyStr = bl::json::value( "" );
    UTF_REQUIRE_EQUAL( bl::json::get_str( emptyStr ).length(), 0U );
}

UTF_AUTO_TEST_CASE( JsonEdgeCasesSpecialNumbers )
{
    utest::json::logImplementation();

    /*
     * Test zero values
     */
    const auto zero = bl::json::value( 0 );
    UTF_REQUIRE_EQUAL( zero.as_int64(), 0 );

    const auto zeroDouble = bl::json::value( 0.0 );
    UTF_REQUIRE( utest::json::doubleEquals( zeroDouble.as_double(), 0.0, 0.0001 ) );

    /*
     * Test very large and very small doubles
     */
    const auto largeDouble = bl::json::value( 1e100 );
    UTF_REQUIRE( largeDouble.is_double() );

    const auto smallDouble = bl::json::value( 1e-100 );
    UTF_REQUIRE( smallDouble.is_double() );
}

UTF_AUTO_TEST_CASE( JsonEdgeCasesEmptyValues )
{
    utest::json::logImplementation();

    /*
     * Test empty object
     */
    const auto emptyObj = bl::json::object();
    UTF_REQUIRE_EQUAL( emptyObj.size(), 0U );
    UTF_REQUIRE( emptyObj.begin() == emptyObj.end() );

    /*
     * Test empty array
     */
    const auto emptyArr = bl::json::array();
    UTF_REQUIRE_EQUAL( emptyArr.size(), 0U );
    UTF_REQUIRE( emptyArr.begin() == emptyArr.end() );

    /*
     * Test null value
     */
    const auto nullVal = bl::json::value();
    UTF_REQUIRE( nullVal.is_null() );
}

UTF_AUTO_TEST_CASE( JsonEdgeCasesUnicode )
{
    utest::json::logImplementation();

    /*
     * Test various Unicode characters
     */
    const std::string emojiText = "Hello 😀 World 🌍";
    const auto emojiVal = bl::json::value( emojiText );
    UTF_REQUIRE( emojiVal.is_string() );

    const std::string chineseText = "你好世界";
    const auto chineseVal = bl::json::value( chineseText );
    UTF_REQUIRE( chineseVal.is_string() );

    const std::string accentText = "Café résumé naïve";
    const auto accentVal = bl::json::value( accentText );
    UTF_REQUIRE( accentVal.is_string() );

    /*
     * Round-trip Unicode through serialization
     */
    bl::json::object obj;
    obj[ "emoji" ] = emojiText;
    obj[ "chinese" ] = chineseText;
    obj[ "accents" ] = accentText;

    const auto serialized = bl::json::saveToString( obj );
    const auto parsed = bl::json::readFromString( serialized );

    UTF_REQUIRE( parsed.is_object() );
    UTF_REQUIRE_EQUAL( parsed.as_object().size(), 3U );
}

/********************************************************************************************
 * 10. Implementation Compatibility Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonImplCompatibilityCanonical )
{
    utest::json::logImplementation();

    /*
     * Create object with keys in random order
     */
    bl::json::object obj;
    obj[ "zebra" ] = 1;
    obj[ "apple" ] = 2;
    obj[ "middle" ] = 3;
    obj[ "banana" ] = 4;

    /*
     * Canonical serialization should always produce same output
     */
    const auto canonical1 = bl::json::saveToString( obj, false, false, true );
    const auto canonical2 = bl::json::saveToString( obj, false, false, true );

    UTF_REQUIRE_EQUAL( canonical1, canonical2 );

    /*
     * Keys should be in alphabetical order
     */
    const auto applePos = canonical1.find( "apple" );
    const auto bananaPos = canonical1.find( "banana" );
    const auto middlePos = canonical1.find( "middle" );
    const auto zebraPos = canonical1.find( "zebra" );

    UTF_REQUIRE( applePos < bananaPos );
    UTF_REQUIRE( bananaPos < middlePos );
    UTF_REQUIRE( middlePos < zebraPos );
}

UTF_AUTO_TEST_CASE( JsonImplCompatibilityRoundTrip )
{
    utest::json::logImplementation();

    /*
     * Create complex nested structure
     */
    auto nested = utest::json::createNestedStructure();

    /*
     * Serialize and parse back
     */
    const auto serialized = bl::json::saveToString( nested );
    const auto parsed = bl::json::readFromString( serialized );

    /*
     * Verify structure is preserved
     */
    UTF_REQUIRE( parsed.is_object() );
    const auto& root = parsed.as_object();

    UTF_REQUIRE( root.contains( "items" ) );
    UTF_REQUIRE( root.contains( "config" ) );
    UTF_REQUIRE( root.contains( "title" ) );

    UTF_REQUIRE( root.at( "items" ).is_array() );
    UTF_REQUIRE( root.at( "config" ).is_object() );
    UTF_REQUIRE( root.at( "title" ).is_string() );

    UTF_REQUIRE_EQUAL( root.at( "items" ).as_array().size(), 3U );
    UTF_REQUIRE_EQUAL( bl::json::get_str( root.at( "title" ) ), "Test" );
}

/********************************************************************************************
 * 11. Integration Tests
 ********************************************************************************************/

UTF_AUTO_TEST_CASE( JsonIntegrationDataModel )
{
    utest::json::logImplementation();

    /*
     * This test validates that the JSON abstraction works correctly
     * with the data model serialization/deserialization system.
     *
     * We use the existing TestDataModel class from TestDataModelDefault.h
     * which already has comprehensive JSON serialization tests.
     *
     * Here we just verify the basic round-trip works with our abstraction.
     */

    const std::string jsonText = R"({
        "stringProperty": "test value",
        "intProperty": 42,
        "boolProperty": true,
        "doubleProperty": 3.14
    })";

    /*
     * Parse JSON
     */
    const auto parsed = bl::json::readFromString( jsonText );
    UTF_REQUIRE( parsed.is_object() );

    const auto& obj = parsed.as_object();

    /*
     * Verify properties can be accessed
     */
    UTF_REQUIRE( obj.contains( "stringProperty" ) );
    UTF_REQUIRE( obj.contains( "intProperty" ) );
    UTF_REQUIRE( obj.contains( "boolProperty" ) );
    UTF_REQUIRE( obj.contains( "doubleProperty" ) );

    UTF_REQUIRE_EQUAL( bl::json::get_str( obj.at( "stringProperty" ) ), "test value" );
    UTF_REQUIRE_EQUAL( obj.at( "intProperty" ).as_int64(), 42 );
    UTF_REQUIRE_EQUAL( obj.at( "boolProperty" ).as_bool(), true );
    UTF_REQUIRE( utest::json::doubleEquals( obj.at( "doubleProperty" ).as_double(), 3.14, 0.001 ) );

    /*
     * Serialize back
     */
    const auto serialized = bl::json::saveToString( obj );
    const auto reparsed = bl::json::readFromString( serialized );

    UTF_REQUIRE( reparsed.is_object() );
    UTF_REQUIRE_EQUAL( reparsed.as_object().size(), obj.size() );
}

/************************************************************************
 * Serialization encoding policy (CXX-08)
 */

UTF_AUTO_TEST_CASE( JsonSerializeAlwaysEmitsRawUtf8 )
{
    utest::json::logImplementation();

    /*
     * The rawUtf8 parameter is retained for source compatibility and has no effect - string
     * content is always emitted as raw UTF-8 on both backends
     *
     * This is asserted on the exact bytes, because a round trip cannot detect the difference:
     * the escaping which the parameter used to select is lossy in a way that only shows up when
     * the serialized text is compared byte for byte or read by a different parser
     */

    bl::json::object obj;
    obj.emplace( "text", "Caf\xC3\xA9" );

    const auto value = bl::json::value( std::move( obj ) );

    const auto withRawOff = bl::json::saveToString( value, false /* prettyPrint */, false /* rawUtf8 */ );
    const auto withRawOn = bl::json::saveToString( value, false /* prettyPrint */, true /* rawUtf8 */ );

    UTF_REQUIRE_EQUAL( withRawOff, withRawOn );

    /*
     * The exact expected bytes - the two UTF-8 bytes of U+00E9 appear literally and there is no
     * \u escape anywhere in the output
     */

    UTF_REQUIRE_EQUAL( withRawOff, std::string( "{\"text\":\"Caf\xC3\xA9\"}" ) );
    UTF_REQUIRE( withRawOff.find( "\\u" ) == std::string::npos );

    /*
     * ... and the value survives a round trip through the parser unchanged
     */

    const auto reparsed = bl::json::readFromString( withRawOff );

    UTF_REQUIRE_EQUAL( bl::json::get_str( reparsed.as_object().at( "text" ) ), std::string( "Caf\xC3\xA9" ) );
}

UTF_AUTO_TEST_CASE( JsonSerializeStreamAlsoEmitsRawUtf8 )
{
    utest::json::logImplementation();

    bl::json::object obj;
    obj.emplace( "text", "\xE4\xBD\xA0\xE5\xA5\xBD" /* two CJK characters */ );

    const auto value = bl::json::value( std::move( obj ) );

    std::ostringstream withRawOff;
    std::ostringstream withRawOn;

    bl::json::saveToStream( value, withRawOff, false /* prettyPrint */, false /* rawUtf8 */ );
    bl::json::saveToStream( value, withRawOn, false /* prettyPrint */, true /* rawUtf8 */ );

    UTF_REQUIRE_EQUAL( withRawOff.str(), withRawOn.str() );

    UTF_REQUIRE_EQUAL(
        withRawOff.str(),
        std::string( "{\"text\":\"\xE4\xBD\xA0\xE5\xA5\xBD\"}" )
        );
}

UTF_AUTO_TEST_CASE( JsonSerializeEscapesControlCharacters )
{
    utest::json::logImplementation();

    /*
     * RFC 8259 section 7 requires the control characters U+0000 to U+001F to be escaped inside
     * a string literal; a serializer which copies them verbatim produces text which a
     * conformant parser rejects. json-spirit's writer does exactly that in the raw UTF-8 mode
     * the library uses, so this is asserted on the exact bytes, on both backends, for an object
     * key as well as for a value
     *
     * The expected text uses the short escape forms where JSON defines one and the six
     * character form with lowercase hex digits otherwise, which is what Boost.JSON emits, so
     * the two backends are byte identical; DEL (0x7F) and everything above it are not control
     * characters in the sense of the RFC and pass through raw
     */

    const std::string key( "k\x01" );

    const std::string text(
        "\x00\x01\x08\x09\x0a\x0c\x0d\x1b\x1f\x7f" "Caf\xC3\xA9",
        15
        );

    const auto unicodeEscape = []( SAA_in const char* hexDigits ) -> std::string
    {
        return std::string( "\\u" ) + hexDigits;
    };

    const std::string expected =
        "{\"k" + unicodeEscape( "0001" ) + "\":\"" +
        unicodeEscape( "0000" ) + unicodeEscape( "0001" ) +
        "\\b\\t\\n\\f\\r" +
        unicodeEscape( "001b" ) + unicodeEscape( "001f" ) +
        "\x7f" "Caf\xC3\xA9\"}";

    bl::json::object obj;
    obj.emplace( key, text );

    const auto value = bl::json::value( std::move( obj ) );

    const auto compact = bl::json::saveToString( value, false /* prettyPrint */, false /* rawUtf8 */ );

    UTF_REQUIRE_EQUAL( compact, expected );

    /*
     * The hex case is pinned in both directions, so that a backend which emits the escape with
     * uppercase digits (json-spirit's own non-raw mode does) is caught as well
     */

    UTF_REQUIRE( compact.find( unicodeEscape( "001b" ) ) != std::string::npos );
    UTF_REQUIRE( compact.find( unicodeEscape( "001B" ) ) == std::string::npos );

    std::ostringstream stream;
    bl::json::saveToStream( value, stream, false /* prettyPrint */, false /* rawUtf8 */ );

    UTF_REQUIRE_EQUAL( stream.str(), expected );

    /*
     * Pretty printed output adds whitespace outside of the literals only, so the literal
     * content must be escaped identically; the exact layout differs between the backends and
     * is deliberately not asserted here
     */

    const auto pretty = bl::json::saveToString( value, true /* prettyPrint */, false /* rawUtf8 */ );

    for( const char ch : text )
    {
        if( static_cast< unsigned char >( ch ) < 0x20U && '\n' != ch )
        {
            UTF_REQUIRE( pretty.find( ch ) == std::string::npos );
        }
    }

    UTF_REQUIRE( pretty.find( unicodeEscape( "0000" ) ) != std::string::npos );
    UTF_REQUIRE( pretty.find( unicodeEscape( "001b" ) ) != std::string::npos );

    /*
     * ... and the value survives a round trip through the parser unchanged
     */

    for( const auto& jsonText : { compact, stream.str(), pretty } )
    {
        const auto reparsed = bl::json::readFromString( jsonText );

        UTF_REQUIRE_EQUAL( reparsed.as_object().size(), 1U );
        UTF_REQUIRE_EQUAL( bl::json::get_str( reparsed.as_object().at( key ) ), text );
    }
}

/************************************************************************
 * Numeric conversion policy (CXX-08)
 */

UTF_AUTO_TEST_CASE( JsonNumericNegativeToUnsignedIsRejected )
{
    utest::json::logImplementation();

    const auto parsed = bl::json::readFromString( R"({"n":-1})" );
    const auto& v = parsed.as_object().at( "n" );

    /*
     * A negative JSON integer must never be converted into an unsigned C++ type; before this
     * was enforced on both backends the json-spirit one returned 18446744073709551615 here
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::json::get_uint64( v ),
        std::exception,
        "is negative while unsigned value is expected"
        );

    /*
     * value_to<> rejects it as well, but the message is backend specific - on the Boost.JSON
     * backend the range check is Boost's own and reports 'not exact' - so only the rejection
     * is asserted here, which is what the policy actually says
     */

    UTF_REQUIRE_THROW( bl::json::value_to< std::uint64_t >( v ), std::exception );

    /*
     * ... while reading it as a signed type is of course fine
     */

    UTF_REQUIRE_EQUAL( bl::json::get_int64( v ), -1 );
    UTF_REQUIRE_EQUAL( bl::json::value_to< std::int64_t >( v ), -1 );
}

UTF_AUTO_TEST_CASE( JsonNumericOutOfRangeIntIsRejected )
{
    utest::json::logImplementation();

    /*
     * 2^40 does not fit an int and must be rejected rather than truncated
     */

    const auto parsed = bl::json::readFromString( R"({"big":1099511627776,"negBig":-1099511627776})" );
    const auto& obj = parsed.as_object();

    UTF_REQUIRE_THROW_MESSAGE(
        bl::json::get_int( obj.at( "big" ) ),
        std::exception,
        "is out of range for the requested integer type"
        );

    /*
     * See the note in JsonNumericNegativeToUnsignedIsRejected on why only the rejection and
     * not the message is asserted for value_to<>
     */

    UTF_REQUIRE_THROW( bl::json::value_to< int >( obj.at( "big" ) ), std::exception );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::json::get_int( obj.at( "negBig" ) ),
        std::exception,
        "is out of range for the requested integer type"
        );

    /*
     * A value which does fit is returned unchanged, including the boundaries
     */

    const auto boundaries = bl::json::readFromString( R"({"max":2147483647,"min":-2147483648})" );

    UTF_REQUIRE_EQUAL( bl::json::get_int( boundaries.as_object().at( "max" ) ), 2147483647 );
    UTF_REQUIRE_EQUAL( bl::json::get_int( boundaries.as_object().at( "min" ) ), -2147483647 - 1 );

    /*
     * The same policy applies to the signed 64-bit accessors: a JSON integer above INT64_MAX is
     * stored as an unsigned value by both backends and must be rejected rather than wrapped to
     * a negative number, which is what json-spirit's own get_int64() used to do (the value below
     * read as -1). The message is backend specific here for get_int64() as well, since on
     * Boost.JSON the refusal comes from boost::json::value::as_int64() itself, so only the
     * rejection is asserted
     */

    const auto wide = bl::json::readFromString(
        R"({"huge":18446744073709551615,"max":9223372036854775807,"min":-9223372036854775807})"
        );

    const auto& wideObj = wide.as_object();

    UTF_REQUIRE_THROW( bl::json::get_int64( wideObj.at( "huge" ) ), std::exception );
    UTF_REQUIRE_THROW( bl::json::value_to< std::int64_t >( wideObj.at( "huge" ) ), std::exception );

    /*
     * ... while the unsigned accessor and the in-range signed values are unaffected
     */

    UTF_REQUIRE( bl::json::get_uint64( wideObj.at( "huge" ) ) == 18446744073709551615ULL );
    UTF_REQUIRE( bl::json::get_int64( wideObj.at( "max" ) ) == 9223372036854775807LL );
    UTF_REQUIRE( bl::json::value_to< std::int64_t >( wideObj.at( "max" ) ) == 9223372036854775807LL );
    UTF_REQUIRE( bl::json::get_int64( wideObj.at( "min" ) ) == -9223372036854775807LL );
}

UTF_AUTO_TEST_CASE( JsonNumericIntegerReadsAsDouble )
{
    utest::json::logImplementation();

    /*
     * JSON has a single number type, so an integer valued number is readable as a double on
     * both backends; only boost::json::value::as_double() is strict about the stored kind
     */

    const auto parsed = bl::json::readFromString( R"({"i":1,"u":18446744073709551615,"d":1.5})" );
    const auto& obj = parsed.as_object();

    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( obj.at( "i" ) ), 1.0, 1e-9 ) );
    UTF_REQUIRE( utest::json::doubleEquals( bl::json::get_real( obj.at( "d" ) ), 1.5, 1e-9 ) );
    UTF_REQUIRE( bl::json::get_real( obj.at( "u" ) ) > 0.0 );
}

UTF_AUTO_TEST_CASE( JsonNumericNegativeZero )
{
    utest::json::logImplementation();

    /*
     * -0 is an integer valued JSON number and both backends drop the sign when they store it
     * as an integer; this is asserted so that a future change of that behavior is deliberate
     * rather than accidental
     */

    const auto parsed = bl::json::readFromString( R"({"z":-0})" );
    const auto& v = parsed.as_object().at( "z" );

    UTF_REQUIRE_EQUAL( bl::json::get_int64( v ), 0 );
    UTF_REQUIRE_EQUAL( bl::json::get_uint64( v ), 0U );

    const auto serialized = bl::json::saveToString( parsed );

    UTF_REQUIRE_EQUAL( serialized, std::string( "{\"z\":0}" ) );
}

/********************************************************************************************
 * 10. Parser Limits and Backend Divergences
 *
 * These cases pin behaviors which differ between the two backends, or which are enforced by
 * this library rather than by the underlying parser. See the contract comments on
 * bl::json::readFromString in baselib/core/JsonUtils.h
 ********************************************************************************************/

namespace
{
    /*
     * Build a document nested 'depth' objects deep: {"n":{"n":{ ... {"n":1} ... }}}
     */
    inline std::string makeNestedJsonText( SAA_in const std::size_t depth )
    {
        std::string text;

        text.reserve( depth * 6U + 8U );

        for( std::size_t i = 0U; i < depth; ++i )
        {
            text += "{\"n\":";
        }

        text += "1";

        text.append( depth, '}' );

        return text;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( JsonParseDepthWithinLimitIsAccepted )
{
    utest::json::logImplementation();

    /*
     * 500 is inside the 512 limit the Boost.JSON backend configures, and json-spirit applies no
     * limit, so this must parse on both backends
     *
     * Note that it is also far beyond Boost.JSON's own default of 32, so this case fails against
     * an unconfigured parser and is the regression test for that configuration being applied
     */

    const auto parsed = bl::json::readFromString( makeNestedJsonText( 500U ) );

    UTF_REQUIRE( parsed.is_object() );

    auto current = parsed;
    std::size_t levels = 0U;

    while( current.is_object() )
    {
        current = current.as_object().at( "n" );
        ++levels;
    }

    UTF_REQUIRE_EQUAL( levels, 500U );
    UTF_REQUIRE_EQUAL( bl::json::get_int64( current ), 1 );
}

#if !defined( BL_USE_JSON_SPIRIT )

UTF_AUTO_TEST_CASE( JsonParseDepthBeyondLimitIsRejected )
{
    utest::json::logImplementation();

    /*
     * Boost.JSON backend only - json-spirit applies no depth limit at all and is deliberately
     * left that way; see the contract note in baselib/core/JsonUtils.h
     */

    UTF_REQUIRE_THROW(
        bl::json::readFromString( makeNestedJsonText( 600U ) ),
        bl::JsonException
        );
}

UTF_AUTO_TEST_CASE( JsonParseTrailingDataIsRejected )
{
    utest::json::logImplementation();

    /*
     * J-9 - trailing content after a complete document. Boost.JSON reports extra_data; json-spirit
     * stops at the end of the first value and ignores the remainder, so this is asserted on the
     * default backend only and the divergence is what the contract note records
     */

    UTF_REQUIRE_THROW(
        bl::json::readFromString( R"({"a":1} {"b":2})" ),
        bl::JsonException
        );

    UTF_REQUIRE_THROW(
        bl::json::readFromString( R"([1,2,3]garbage)" ),
        bl::JsonException
        );
}

UTF_AUTO_TEST_CASE( JsonParseInvalidUtf8IsRejected )
{
    utest::json::logImplementation();

    /*
     * J-3 - an invalid UTF-8 sequence inside a string literal. Boost.JSON validates the encoding
     * and rejects it; json-spirit passes the bytes through untouched, so this too is asserted on
     * the default backend only
     */

    std::string text( "{\"s\":\"" );

    text += static_cast< char >( 0xC3 );     /* a lead byte expecting one continuation byte ... */
    text += static_cast< char >( 0x28 );     /* ... followed by '(' which is not one            */

    text += "\"}";

    UTF_REQUIRE_THROW( bl::json::readFromString( text ), bl::JsonException );
}

UTF_AUTO_TEST_CASE( JsonPrettyPrintEmptyContainers )
{
    utest::json::logImplementation();

    /*
     * An empty object and an empty array pretty print as {} and [] rather than as a brace, a
     * blank line and a closing brace, which is what the json-spirit backend produces for the
     * same values
     */

    UTF_REQUIRE_EQUAL(
        bl::json::saveToString( bl::json::value( bl::json::object() ), true /* prettyPrint */ ),
        std::string( "{}" )
        );

    UTF_REQUIRE_EQUAL(
        bl::json::saveToString( bl::json::value( bl::json::array() ), true /* prettyPrint */ ),
        std::string( "[]" )
        );

    /*
     * And nested inside a non-empty parent, where the indentation of the closing brace matters
     */

    bl::json::object outer;
    outer[ "empty" ] = bl::json::object();

    const auto pretty = bl::json::saveToString( bl::json::value( outer ), true /* prettyPrint */ );

    UTF_REQUIRE( pretty.find( "{}" ) != std::string::npos );
    UTF_REQUIRE( pretty.find( "\n\n" ) == std::string::npos );
}

#endif /* !BL_USE_JSON_SPIRIT */

UTF_AUTO_TEST_CASE( JsonSerializeToStreamCanonical )
{
    utest::json::logImplementation();

    /*
     * saveToStream accepts the same canonicalize flag as saveToString, so a large document can be
     * written to a stream in canonical form without first materializing it as a std::string
     */

    bl::json::object obj;
    obj[ "zebra" ] = "last";
    obj[ "apple" ] = "first";

    std::ostringstream canonicalStream;

    bl::json::saveToStream(
        bl::json::value( obj ),
        canonicalStream,
        false /* prettyPrint */,
        false /* rawUtf8 */,
        true  /* canonicalize */
        );

    UTF_REQUIRE_EQUAL(
        canonicalStream.str(),
        bl::json::saveToString( bl::json::value( obj ), false, false, true /* canonicalize */ )
        );

    UTF_REQUIRE_EQUAL(
        canonicalStream.str(),
        std::string( "{\"apple\":\"first\",\"zebra\":\"last\"}" )
        );

    /*
     * The prettyPrint restriction applies here exactly as it does on saveToString, and both
     * backends enforce it
     */

    std::ostringstream rejected;

    UTF_REQUIRE_THROW(
        bl::json::saveToStream(
            bl::json::value( obj ),
            rejected,
            true  /* prettyPrint */,
            false /* rawUtf8 */,
            true  /* canonicalize */
            ),
        bl::ArgumentException
        );
}

#endif /* __UTEST_TESTJSONABSTRACTION_H_ */
