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

#include <chrono>
#include <iomanip>
#include <sstream>

namespace utest
{
    namespace json_perf
    {
        /*
         * Helper to measure execution time
         */

        template< typename Func >
        inline double measureTimeMs( SAA_in Func func, SAA_in const std::size_t iterations )
        {
            const auto start = std::chrono::high_resolution_clock::now();

            for( std::size_t i = 0; i < iterations; ++i )
            {
                func();
            }

            const auto end = std::chrono::high_resolution_clock::now();

            return std::chrono::duration< double, std::milli >( end - start ).count();
        }

        /*
         * Generate test JSON data of various sizes
         */

        inline std::string generateSimpleJson()
        {
            return R"({
                "name": "test object",
                "value": 42,
                "enabled": true,
                "price": 19.99,
                "tags": ["tag1", "tag2", "tag3"],
                "metadata": {
                    "created": "2024-01-15",
                    "version": 1
                }
            })";
        }

        inline std::string generateMediumJson()
        {
            std::ostringstream oss;
            oss << R"({"items": [)";

            for( std::size_t i = 0; i < 100; ++i )
            {
                if( i > 0 )
                {
                    oss << ",";
                }
                oss << R"({
                    "id": )" << i << R"(,
                    "name": "item_)" << i << R"(",
                    "description": "This is a description for item number )" << i << R"(",
                    "active": )" << ( i % 2 == 0 ? "true" : "false" ) << R"(,
                    "score": )" << std::fixed << std::setprecision( 1 ) << ( i * 1.5 ) << R"(,
                    "tags": ["cat1", "cat2", "cat3"]
                })";
            }

            oss << R"(]})";
            return oss.str();
        }

        inline std::string generateLargeJson()
        {
            std::ostringstream oss;
            oss << R"({"records": [)";

            for( std::size_t i = 0; i < 1000; ++i )
            {
                if( i > 0 )
                {
                    oss << ",";
                }
                oss << R"({
                    "id": )" << i << R"(,
                    "uuid": "550e8400-e29b-41d4-a716-44665544)" << std::setfill( '0' ) << std::setw( 4 ) << i << R"(",
                    "name": "record_)" << i << R"(",
                    "email": "user)" << i << R"(@example.com",
                    "age": )" << ( 20 + i % 50 ) << R"(,
                    "balance": )" << ( i * 100.25 ) << R"(,
                    "verified": )" << ( i % 3 == 0 ? "true" : "false" ) << R"(,
                    "properties": {
                        "level": )" << ( i % 10 ) << R"(,
                        "rating": )" << ( ( i % 5 ) + 1 ) << R"(.0,
                        "category": "category_)" << ( i % 20 ) << R"("
                    }
                })";
            }

            oss << R"(]})";
            return oss.str();
        }

        inline std::string generateDeeplyNestedJson()
        {
            std::ostringstream oss;

            const int depth = 20;

            for( int i = 0; i < depth; ++i )
            {
                oss << R"({"level)" << i << R"(": )";
            }

            oss << R"("leaf_value")";

            for( int i = 0; i < depth; ++i )
            {
                oss << "}";
            }

            return oss.str();
        }

    } // json_perf

} // utest

UTF_AUTO_TEST_CASE( JsonPerformanceBasicParsing )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t warmupIterations = 100;
    const std::size_t testIterations = 1000;

    const auto simpleJson = generateSimpleJson();
    const auto mediumJson = generateMediumJson();

    /*
     * Warmup phase
     */

    for( std::size_t i = 0; i < warmupIterations; ++i )
    {
        bl::json::readFromString( simpleJson );
    }

    /*
     * Test parsing performance
     */

    const auto simpleParseTimeMs = measureTimeMs(
        [ &simpleJson ]()
        {
            auto value = bl::json::readFromString( simpleJson );
            ( void ) value;
        },
        testIterations
        );

    const auto mediumParseTimeMs = measureTimeMs(
        [ &mediumJson ]()
        {
            auto value = bl::json::readFromString( mediumJson );
            ( void ) value;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Parsing Performance (" << implName << ") ===\n"
            << "Simple JSON (" << simpleJson.size() << " bytes):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << simpleParseTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( simpleParseTimeMs / testIterations ) << " ms\n"
            << "Medium JSON (" << mediumJson.size() << " bytes):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << mediumParseTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( mediumParseTimeMs / testIterations ) << " ms"
        );

    /*
     * Just verify the parsing works correctly
     */

    const auto parsed = bl::json::readFromString( simpleJson );
    UTF_REQUIRE( ! parsed.is_null() );
}

UTF_AUTO_TEST_CASE( JsonPerformanceLargeParsing )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 100;

    const auto largeJson = generateLargeJson();

    /*
     * Test large JSON parsing performance
     */

    const auto largeParseTimeMs = measureTimeMs(
        [ &largeJson ]()
        {
            auto value = bl::json::readFromString( largeJson );
            ( void ) value;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Large JSON Parsing Performance (" << implName << ") ===\n"
            << "Large JSON (" << largeJson.size() << " bytes, 1000 records):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << largeParseTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( largeParseTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify parsing and access
     */

    const auto parsed = bl::json::readFromString( largeJson );
    UTF_REQUIRE( ! parsed.is_null() );

    const auto& records = parsed.as_object().at( "records" );
    UTF_REQUIRE_EQUAL( records.as_array().size(), 1000U );
}

UTF_AUTO_TEST_CASE( JsonPerformanceSerialization )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 1000;

    /*
     * Parse JSON first, then test serialization
     */

    const auto simpleJson = generateSimpleJson();
    const auto mediumJson = generateMediumJson();

    const auto simpleValue = bl::json::readFromString( simpleJson );
    const auto mediumValue = bl::json::readFromString( mediumJson );

    /*
     * Test serialization performance (non-canonical)
     */

    const auto simpleSerializeTimeMs = measureTimeMs(
        [ &simpleValue ]()
        {
            auto str = bl::json::saveToString( simpleValue );
            ( void ) str;
        },
        testIterations
        );

    const auto mediumSerializeTimeMs = measureTimeMs(
        [ &mediumValue ]()
        {
            auto str = bl::json::saveToString( mediumValue );
            ( void ) str;
        },
        testIterations
        );

    /*
     * Test canonical serialization performance (sorted keys)
     */

    const auto simpleCanonicalTimeMs = measureTimeMs(
        [ &simpleValue ]()
        {
            auto str = bl::json::saveToString(
                simpleValue,
                false /* prettyPrint */,
                false /* rawUTF8 */,
                true  /* canonicalize */
                );
            ( void ) str;
        },
        testIterations
        );

    const auto mediumCanonicalTimeMs = measureTimeMs(
        [ &mediumValue ]()
        {
            auto str = bl::json::saveToString(
                mediumValue,
                false /* prettyPrint */,
                false /* rawUTF8 */,
                true  /* canonicalize */
                );
            ( void ) str;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Serialization Performance (" << implName << ") ===\n"
            << "Simple JSON (non-canonical):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << simpleSerializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( simpleSerializeTimeMs / testIterations ) << " ms\n"
            << "Medium JSON (non-canonical):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << mediumSerializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( mediumSerializeTimeMs / testIterations ) << " ms\n"
            << "Simple JSON (canonical):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << simpleCanonicalTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( simpleCanonicalTimeMs / testIterations ) << " ms\n"
            << "Medium JSON (canonical):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << mediumCanonicalTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( mediumCanonicalTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify serialization works
     */

    const auto serialized = bl::json::saveToString( simpleValue );
    UTF_REQUIRE( ! serialized.empty() );

    const auto canonicalized = bl::json::saveToString(
        simpleValue,
        false /* prettyPrint */,
        false /* rawUTF8 */,
        true  /* canonicalize */
        );
    UTF_REQUIRE( ! canonicalized.empty() );
}

UTF_AUTO_TEST_CASE( JsonPerformanceObjectAccess )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 10000;

    /*
     * Parse medium-sized JSON for object access testing
     */

    const auto mediumJson = generateMediumJson();
    const auto parsed = bl::json::readFromString( mediumJson );

    /*
     * Test object property access performance
     */

    const auto accessTimeMs = measureTimeMs(
        [ &parsed ]()
        {
            const auto& items = parsed.as_object().at( "items" );
            for( const auto& item : items.as_array() )
            {
                const auto& obj = item.as_object();
                auto id = obj.at( "id" ).as_int64();
                const auto& name = obj.at( "name" ).as_string();
                auto active = obj.at( "active" ).as_bool();
                auto score = obj.at( "score" ).as_double();
                ( void ) id;
                ( void ) name;
                ( void ) active;
                ( void ) score;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Object Access Performance (" << implName << ") ===\n"
            << "Iterating 100 items and accessing 4 properties each:\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << accessTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( accessTimeMs / testIterations ) << " ms"
        );
}

UTF_AUTO_TEST_CASE( JsonPerformanceNestedAccess )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 100000;

    /*
     * Test deeply nested JSON parsing and access
     */

    const auto nestedJson = generateDeeplyNestedJson();

    const auto nestedParseTimeMs = measureTimeMs(
        [ &nestedJson ]()
        {
            auto value = bl::json::readFromString( nestedJson );
            ( void ) value;
        },
        testIterations
        );

    /*
     * Parse once and test nested access
     */

    auto parsed = bl::json::readFromString( nestedJson );

    const auto nestedAccessTimeMs = measureTimeMs(
        [ &parsed ]()
        {
            /*
             * Navigate through 20 levels of nesting
             */

            const bl::json::value* current = &parsed;
            for( int i = 0; i < 20; ++i )
            {
                std::string key = "level" + std::to_string( i );
                const auto& obj = current -> as_object();
                current = &obj.at( key );
            }
            auto result = bl::json::get_str( *current );
            ( void ) result;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Nested Access Performance (" << implName << ") ===\n"
            << "Deeply nested JSON (20 levels):\n"
            << "  Parse time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << nestedParseTimeMs << " ms\n"
            << "  Average parse per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( nestedParseTimeMs / testIterations ) << " ms\n"
            << "  Access time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << nestedAccessTimeMs << " ms\n"
            << "  Average access per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( nestedAccessTimeMs / testIterations ) << " ms"
        );
}

UTF_AUTO_TEST_CASE( JsonPerformanceObjectConstruction )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 10000;

    /*
     * Test object construction performance
     */

    const auto constructionTimeMs = measureTimeMs(
        []()
        {
            bl::json::object obj;
            obj[ "name" ] = "test object";
            obj[ "id" ] = 12345;
            obj[ "active" ] = true;
            obj[ "score" ] = 98.6;

            bl::json::array arr;
            for( int i = 0; i < 10; ++i )
            {
                bl::json::object item;
                item[ "index" ] = i;
                item[ "value" ] = std::string( "item_" ) + std::to_string( i );
                arr.push_back( bl::json::value( std::move( item ) ) );
            }

            obj[ "items" ] = bl::json::value( std::move( arr ) );

            auto val = bl::json::value( std::move( obj ) );
            auto str = bl::json::saveToString( val );
            ( void ) str;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Object Construction Performance (" << implName << ") ===\n"
            << "Constructing object with nested array (10 items):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << constructionTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( constructionTimeMs / testIterations ) << " ms"
        );
}

UTF_AUTO_TEST_CASE( JsonPerformanceRoundTrip )
{
    using namespace utest::json_perf;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 500;

    const auto mediumJson = generateMediumJson();

    /*
     * Test full round-trip: parse -> access -> modify -> serialize
     */

    const auto roundTripTimeMs = measureTimeMs(
        [ &mediumJson ]()
        {
            /*
             * Parse
             */

            auto parsed = bl::json::readFromString( mediumJson );

            /*
             * Access and verify
             */

            const auto& items = parsed.as_object().at( "items" ).as_array();
            const auto count = items.size();
            ( void ) count;

            /*
             * Serialize back
             */

            auto serialized = bl::json::saveToString( parsed );
            ( void ) serialized;
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== JSON Round-Trip Performance (" << implName << ") ===\n"
            << "Parse + Access + Serialize (medium JSON):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << roundTripTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( roundTripTimeMs / testIterations ) << " ms"
        );
}

/*
 * Data Model performance tests using DataModelObject.h and DataModelObjectDefs.h
 */

namespace utest
{
    namespace dm_perf
    {
        /*
         * Simple data model object for performance testing
         */

        BL_DM_DEFINE_CLASS_BEGIN( PerfSimpleObject )

            BL_DM_DECLARE_STRING_PROPERTY               ( name )
            BL_DM_DECLARE_INT_PROPERTY                  ( id )
            BL_DM_DECLARE_BOOL_PROPERTY                 ( active )
            BL_DM_DECLARE_DOUBLE_PROPERTY               ( score )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( name )
                BL_DM_IMPL_PROPERTY( id )
                BL_DM_IMPL_PROPERTY( active )
                BL_DM_IMPL_PROPERTY( score )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( PerfSimpleObject )

        BL_DM_DEFINE_PROPERTY( PerfSimpleObject, name )
        BL_DM_DEFINE_PROPERTY( PerfSimpleObject, id )
        BL_DM_DEFINE_PROPERTY( PerfSimpleObject, active )
        BL_DM_DEFINE_PROPERTY( PerfSimpleObject, score )

        /*
         * Complex data model object with nested properties
         */

        BL_DM_DEFINE_CLASS_BEGIN( PerfNestedObject )

            BL_DM_DECLARE_STRING_PROPERTY               ( description )
            BL_DM_DECLARE_UINT64_PROPERTY               ( timestamp )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( description )
                BL_DM_IMPL_PROPERTY( timestamp )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( PerfNestedObject )

        BL_DM_DEFINE_PROPERTY( PerfNestedObject, description )
        BL_DM_DEFINE_PROPERTY( PerfNestedObject, timestamp )

        BL_DM_DEFINE_CLASS_BEGIN( PerfComplexObject )

            BL_DM_DECLARE_STRING_PROPERTY               ( name )
            BL_DM_DECLARE_INT_PROPERTY                  ( id )
            BL_DM_DECLARE_BOOL_PROPERTY                 ( active )
            BL_DM_DECLARE_DOUBLE_PROPERTY               ( score )
            BL_DM_DECLARE_MAP_PROPERTY                  ( metadata, std::string )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY        ( tags, std::string, get_str )
            BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY        ( scores, int, get_int )
            BL_DM_DECLARE_COMPLEX_PROPERTY              ( nested, PerfNestedObject )
            BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY       ( items, PerfNestedObject )

            BL_DM_PROPERTIES_IMPL_BEGIN()
                BL_DM_IMPL_PROPERTY( name )
                BL_DM_IMPL_PROPERTY( id )
                BL_DM_IMPL_PROPERTY( active )
                BL_DM_IMPL_PROPERTY( score )
                BL_DM_IMPL_PROPERTY( metadata )
                BL_DM_IMPL_PROPERTY( tags )
                BL_DM_IMPL_PROPERTY( scores )
                BL_DM_IMPL_PROPERTY( nested )
                BL_DM_IMPL_PROPERTY( items )
            BL_DM_PROPERTIES_IMPL_END()

        BL_DM_DEFINE_CLASS_END( PerfComplexObject )

        BL_DM_DEFINE_PROPERTY( PerfComplexObject, name )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, id )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, active )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, score )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, metadata )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, tags )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, scores )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, nested )
        BL_DM_DEFINE_PROPERTY( PerfComplexObject, items )

        /*
         * Helper to create a populated simple object
         */

        inline bl::om::ObjPtr< PerfSimpleObject > createSimpleObject( SAA_in const int index )
        {
            auto obj = PerfSimpleObject::createInstance();
            obj -> name( "item_" + std::to_string( index ) );
            obj -> id( index );
            obj -> active( index % 2 == 0 );
            obj -> score( index * 1.5 );
            return obj;
        }

        /*
         * Helper to create a populated complex object
         */

        inline bl::om::ObjPtr< PerfComplexObject > createComplexObject( SAA_in const int index )
        {
            auto obj = PerfComplexObject::createInstance();
            obj -> name( "complex_item_" + std::to_string( index ) );
            obj -> id( index );
            obj -> active( index % 2 == 0 );
            obj -> score( index * 2.5 );

            std::map< std::string, std::string > metadata;
            metadata[ "key1" ] = "value1";
            metadata[ "key2" ] = "value2";
            metadata[ "key3" ] = "value3";
            obj -> metadataLvalue() = std::move( metadata );

            std::vector< std::string > tags;
            tags.push_back( "tag1" );
            tags.push_back( "tag2" );
            tags.push_back( "tag3" );
            obj -> tagsLvalue() = std::move( tags );

            std::vector< int > scores;
            for( int i = 0; i < 5; ++i )
            {
                scores.push_back( i * 10 );
            }
            obj -> scoresLvalue() = std::move( scores );

            auto nested = PerfNestedObject::createInstance();
            nested -> description( "nested description " + std::to_string( index ) );
            nested -> timestamp( static_cast< std::uint64_t >( index ) * 1000000ULL );
            obj -> nestedLvalue() = std::move( nested );

            std::vector< bl::om::ObjPtr< PerfNestedObject > > items;
            for( int i = 0; i < 5; ++i )
            {
                auto item = PerfNestedObject::createInstance();
                item -> description( "item " + std::to_string( i ) );
                item -> timestamp( static_cast< std::uint64_t >( i ) * 500000ULL );
                items.push_back( std::move( item ) );
            }
            obj -> itemsLvalue() = std::move( items );

            return obj;
        }

        /*
         * Generate JSON for simple objects
         */

        inline std::string generateSimpleObjectJson( SAA_in const int index )
        {
            std::ostringstream oss;
            oss << R"({
                "name": "item_)" << index << R"(",
                "id": )" << index << R"(,
                "active": )" << ( index % 2 == 0 ? "true" : "false" ) << R"(,
                "score": )" << std::fixed << std::setprecision( 1 ) << ( index * 1.5 ) << R"(
            })";
            return oss.str();
        }

        /*
         * Generate JSON for complex objects
         */

        inline std::string generateComplexObjectJson( SAA_in const int index )
        {
            std::ostringstream oss;
            oss << R"({
                "name": "complex_item_)" << index << R"(",
                "id": )" << index << R"(,
                "active": )" << ( index % 2 == 0 ? "true" : "false" ) << R"(,
                "score": )" << std::fixed << std::setprecision( 1 ) << ( index * 2.5 ) << R"(,
                "metadata": {
                    "key1": "value1",
                    "key2": "value2",
                    "key3": "value3"
                },
                "tags": ["tag1", "tag2", "tag3"],
                "scores": [0, 10, 20, 30, 40],
                "nested": {
                    "description": "nested description )" << index << R"(",
                    "timestamp": )" << ( static_cast< std::uint64_t >( index ) * 1000000ULL ) << R"(
                },
                "items": [
                    {"description": "item 0", "timestamp": 0},
                    {"description": "item 1", "timestamp": 500000},
                    {"description": "item 2", "timestamp": 1000000},
                    {"description": "item 3", "timestamp": 1500000},
                    {"description": "item 4", "timestamp": 2000000}
                ]
            })";
            return oss.str();
        }

    } // dm_perf

} // utest

UTF_AUTO_TEST_CASE( DataModelPerformanceSimpleSerialization )
{
    using namespace utest::json_perf;
    using namespace utest::dm_perf;
    using namespace bl::dm;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 5000;

    /*
     * Create simple objects for serialization testing
     */

    std::vector< bl::om::ObjPtr< PerfSimpleObject > > simpleObjects;
    for( int i = 0; i < 10; ++i )
    {
        simpleObjects.push_back( createSimpleObject( i ) );
    }

    /*
     * Test simple object serialization
     */

    const auto serializeTimeMs = measureTimeMs(
        [ &simpleObjects ]()
        {
            for( const auto& obj : simpleObjects )
            {
                auto json = DataModelUtils::getDocAsPackedJsonString( obj );
                ( void ) json;
            }
        },
        testIterations
        );

    /*
     * Test simple object serialization (pretty print)
     */

    const auto serializePrettyTimeMs = measureTimeMs(
        [ &simpleObjects ]()
        {
            for( const auto& obj : simpleObjects )
            {
                auto json = DataModelUtils::getDocAsPrettyJsonString( obj );
                ( void ) json;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Data Model Simple Serialization Performance (" << implName << ") ===\n"
            << "Serializing 10 simple objects (packed):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << serializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( serializeTimeMs / testIterations ) << " ms\n"
            << "Serializing 10 simple objects (pretty):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << serializePrettyTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( serializePrettyTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify serialization works
     */

    const auto json = DataModelUtils::getDocAsPackedJsonString( simpleObjects[ 0 ] );
    UTF_REQUIRE( ! json.empty() );
}

UTF_AUTO_TEST_CASE( DataModelPerformanceSimpleDeserialization )
{
    using namespace utest::json_perf;
    using namespace utest::dm_perf;
    using namespace bl::dm;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 5000;

    /*
     * Generate JSON strings for deserialization testing
     */

    std::vector< std::string > jsonStrings;
    for( int i = 0; i < 10; ++i )
    {
        jsonStrings.push_back( generateSimpleObjectJson( i ) );
    }

    /*
     * Test simple object deserialization
     */

    const auto deserializeTimeMs = measureTimeMs(
        [ &jsonStrings ]()
        {
            for( const auto& json : jsonStrings )
            {
                auto obj = DataModelUtils::loadFromJsonText< PerfSimpleObject >( json );
                ( void ) obj;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Data Model Simple Deserialization Performance (" << implName << ") ===\n"
            << "Deserializing 10 simple objects:\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << deserializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( deserializeTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify deserialization works
     */

    const auto obj = DataModelUtils::loadFromJsonText< PerfSimpleObject >( jsonStrings[ 0 ] );
    UTF_REQUIRE_EQUAL( obj -> name(), "item_0" );
    UTF_REQUIRE_EQUAL( obj -> id(), 0 );
}

UTF_AUTO_TEST_CASE( DataModelPerformanceComplexSerialization )
{
    using namespace utest::json_perf;
    using namespace utest::dm_perf;
    using namespace bl::dm;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 2000;

    /*
     * Create complex objects for serialization testing
     */

    std::vector< bl::om::ObjPtr< PerfComplexObject > > complexObjects;
    for( int i = 0; i < 10; ++i )
    {
        complexObjects.push_back( createComplexObject( i ) );
    }

    /*
     * Test complex object serialization
     */

    const auto serializeTimeMs = measureTimeMs(
        [ &complexObjects ]()
        {
            for( const auto& obj : complexObjects )
            {
                auto json = DataModelUtils::getDocAsPackedJsonString( obj );
                ( void ) json;
            }
        },
        testIterations
        );

    /*
     * Test complex object serialization (pretty print)
     */

    const auto serializePrettyTimeMs = measureTimeMs(
        [ &complexObjects ]()
        {
            for( const auto& obj : complexObjects )
            {
                auto json = DataModelUtils::getDocAsPrettyJsonString( obj );
                ( void ) json;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Data Model Complex Serialization Performance (" << implName << ") ===\n"
            << "Serializing 10 complex objects (packed):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << serializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( serializeTimeMs / testIterations ) << " ms\n"
            << "Serializing 10 complex objects (pretty):\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << serializePrettyTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( serializePrettyTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify serialization works
     */

    const auto json = DataModelUtils::getDocAsPackedJsonString( complexObjects[ 0 ] );
    UTF_REQUIRE( ! json.empty() );
}

UTF_AUTO_TEST_CASE( DataModelPerformanceComplexDeserialization )
{
    using namespace utest::json_perf;
    using namespace utest::dm_perf;
    using namespace bl::dm;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 2000;

    /*
     * Generate JSON strings for deserialization testing
     */

    std::vector< std::string > jsonStrings;
    for( int i = 0; i < 10; ++i )
    {
        jsonStrings.push_back( generateComplexObjectJson( i ) );
    }

    /*
     * Test complex object deserialization
     */

    const auto deserializeTimeMs = measureTimeMs(
        [ &jsonStrings ]()
        {
            for( const auto& json : jsonStrings )
            {
                auto obj = DataModelUtils::loadFromJsonText< PerfComplexObject >( json );
                ( void ) obj;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Data Model Complex Deserialization Performance (" << implName << ") ===\n"
            << "Deserializing 10 complex objects:\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << deserializeTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( deserializeTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify deserialization works
     */

    const auto obj = DataModelUtils::loadFromJsonText< PerfComplexObject >( jsonStrings[ 0 ] );
    UTF_REQUIRE_EQUAL( obj -> name(), "complex_item_0" );
    UTF_REQUIRE_EQUAL( obj -> id(), 0 );
    UTF_REQUIRE_EQUAL( obj -> tags().size(), 3U );
    UTF_REQUIRE_EQUAL( obj -> items().size(), 5U );
}

UTF_AUTO_TEST_CASE( DataModelPerformanceRoundTrip )
{
    using namespace utest::json_perf;
    using namespace utest::dm_perf;
    using namespace bl::dm;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t testIterations = 2000;

    /*
     * Create complex objects for round-trip testing
     */

    std::vector< bl::om::ObjPtr< PerfComplexObject > > complexObjects;
    for( int i = 0; i < 10; ++i )
    {
        complexObjects.push_back( createComplexObject( i ) );
    }

    /*
     * Test complete round-trip: serialize -> deserialize
     */

    const auto roundTripTimeMs = measureTimeMs(
        [ &complexObjects ]()
        {
            for( const auto& obj : complexObjects )
            {
                /*
                 * Serialize
                 */

                auto json = DataModelUtils::getDocAsPackedJsonString( obj );

                /*
                 * Deserialize
                 */

                auto loaded = DataModelUtils::loadFromJsonText< PerfComplexObject >( json );
                ( void ) loaded;
            }
        },
        testIterations
        );

    const auto implName = bl::json::implName();

    BL_LOG(
        bl::Logging::notify(),
        BL_MSG()
            << "\n=== Data Model Round-Trip Performance (" << implName << ") ===\n"
            << "Serialize + Deserialize 10 complex objects:\n"
            << "  Total time for " << testIterations << " iterations: "
            << std::fixed << std::setprecision( 2 ) << roundTripTimeMs << " ms\n"
            << "  Average per iteration: "
            << std::fixed << std::setprecision( 4 ) << ( roundTripTimeMs / testIterations ) << " ms"
        );

    /*
     * Verify round-trip works correctly
     */

    const auto json = DataModelUtils::getDocAsPackedJsonString( complexObjects[ 0 ] );
    const auto loaded = DataModelUtils::loadFromJsonText< PerfComplexObject >( json );
    UTF_REQUIRE_EQUAL( loaded -> name(), complexObjects[ 0 ] -> name() );
    UTF_REQUIRE_EQUAL( loaded -> id(), complexObjects[ 0 ] -> id() );
}
