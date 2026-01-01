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

#ifndef __BL_BOOSTJSONIMPL_H_
#define __BL_BOOSTJSONIMPL_H_

/*
 * Boost.JSON implementation details
 *
 * This file provides direct typedefs to native Boost.JSON types for zero-overhead access.
 * It is included by JsonUtils.h when BL_USE_JSON_SPIRIT is not defined (the default).
 */

#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

#include <boost/json.hpp>

#include <algorithm>
#include <array>
#include <sstream>
#include <vector>

/*
 * JSON accessor macros for Boost.JSON
 *
 * Boost.JSON uses boost::json::object with key_value_pair providing .key()/.value() methods.
 * These macros provide a unified interface that works with both json-spirit and Boost.JSON
 */

#define BL_JSON_ITER_VALUE( iter )                  ( iter ) -> value()
#define BL_JSON_PAIR_KEY( pair )                    ( pair ).key()
#define BL_JSON_PAIR_VALUE( pair )                  ( pair ).value()

namespace bl
{
    namespace json
    {
        /*
         * Core type aliases - lowercase to match Boost.JSON naming
         */

        using value = boost::json::value;
        using object = boost::json::object;
        using array = boost::json::array;

        /*
         * Legacy type aliases (JSON Spirit compatibility)
         */

        typedef value                                           Value;
        typedef object                                          Object;
        typedef array                                           Array;

        /*
         * Implementation name for diagnostics and logging
         */

        inline const char* implName() NOEXCEPT
        {
            return "Boost.JSON";
        }

        /*
         * Convenience accessors for Boost.JSON values
         *
         * These provide json-spirit style get_* methods as free functions
         * for backward compatibility with code expecting these patterns.
         */

        inline std::string get_str( SAA_in const value& v )
        {
            const auto& s = v.as_string();
            return std::string( s.c_str(), s.size() );
        }

        inline bool get_bool( SAA_in const value& v )
        {
            return v.as_bool();
        }

        inline int get_int( SAA_in const value& v )
        {
            return static_cast< int >( v.as_int64() );
        }

        inline std::int64_t get_int64( SAA_in const value& v )
        {
            return v.as_int64();
        }

        inline std::uint64_t get_uint64( SAA_in const value& v )
        {
            if( v.is_uint64() )
            {
                return v.as_uint64();
            }
            if( v.is_int64() )
            {
                const auto val = v.as_int64();

                if( val < 0 )
                {
                    BL_THROW(
                        JsonException(),
                        BL_MSG()
                            << "JSON integer value '"
                            << val
                            << "' is negative while unsigned value is expected"
                        );
                }

                return static_cast< std::uint64_t >( val );
            }

            return v.as_uint64();
        }

        inline double get_real( SAA_in const value& v )
        {
            return v.as_double();
        }

        /*
         * Template accessor for type-safe value extraction
         *
         * Uses boost::json::value_to<T>() which supports arithmetic types,
         * bool, std::string, and other common types out of the box.
         */

        using boost::json::value_to;

        namespace detail
        {
            /*
             * Boost.JSON implementation details
             */

            /*
             * kind type alias - only used internally for switch statements
             */

            using kind = boost::json::kind;

            /**
             * @brief class JsonUtilsImpl - JSON utility code (Boost.JSON implementation)
             */

            template
            <
                typename E = void
            >
            class JsonUtilsImplT
            {
                BL_DECLARE_STATIC( JsonUtilsImplT )

            private:

                enum
                {
                    MAX_DUMP_STRING_LENGTH = 1024
                };

            public:

                static value readFromString( SAA_in const std::string& input )
                {
                    try
                    {
                        return boost::json::parse( input );
                    }
                    catch( const boost::system::system_error& e )
                    {
                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "Invalid JSON string:\n"
                                << ( input.size() < MAX_DUMP_STRING_LENGTH ?
                                        input :
                                        input.substr( 0, MAX_DUMP_STRING_LENGTH ) + "..."
                                        )
                            );

                        BL_THROW(
                            JsonException(),
                            BL_MSG()
                                << "JSON parser error: "
                                << e.what()
                            );
                    }
                }

                template
                <
                    typename STREAM
                >
                static value readFromStream( SAA_inout STREAM& input )
                {
                    input.exceptions( std::ios::badbit );

                    try
                    {
                        boost::json::stream_parser parser;
                        eh::error_code ec;
                        std::array< char, 2048 > buffer;

                        for( ;; )
                        {
                            input.read( buffer.data(), buffer.size() );
                            const auto count = input.gcount();

                            if( count > 0 )
                            {
                                parser.write( buffer.data(), static_cast< std::size_t >( count ), ec );

                                if( ec )
                                {
                                    BL_THROW(
                                        JsonException(),
                                        BL_MSG() << "JSON parser error: " << ec.message()
                                        );
                                }
                            }

                            if( ! input )
                            {
                                break;
                            }
                        }

                        parser.finish( ec );

                        if( ec )
                        {
                            BL_THROW(
                                JsonException(),
                                BL_MSG() << "JSON parser error: " << ec.message()
                                );
                        }

                        return parser.release();
                    }
                    catch( const boost::system::system_error& e )
                    {
                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Invalid JSON blob parsed from stream"
                            );

                        BL_THROW(
                            JsonException(),
                            BL_MSG()
                                << "JSON parser error: "
                                << e.what()
                            );
                    }
                }

                /*
                 * Recursively create a new JSON value with all object keys sorted alphabetically.
                 * This ensures deterministic serialization output for hashing and signing operations.
                 *
                 * Collects pointers to key-value pairs, sorts them by key, and rebuilds
                 * the boost::json::object in sorted order.
                 */

                static value canonicalizeValue( SAA_in const value& val )
                {
                    switch( val.kind() )
                    {
                        case kind::object:
                            {
                                const auto& obj = val.as_object();

                                using kvp_t = object::value_type;

                                std::vector< const kvp_t* > items;
                                items.reserve( obj.size() );

                                for( const auto& kvp : obj )
                                {
                                    items.push_back( &kvp );
                                }

                                std::sort(
                                    items.begin(),
                                    items.end(),
                                    []( const kvp_t* lhs, const kvp_t* rhs )
                                    {
                                        return lhs -> key() < rhs -> key();
                                    }
                                    );

                                object sortedObj;
                                sortedObj.reserve( obj.size() );

                                for( const auto* kvp : items )
                                {
                                    sortedObj.emplace( kvp -> key(), canonicalizeValue( kvp -> value() ) );
                                }

                                return sortedObj;
                            }

                        case kind::array:
                            {
                                const auto& arr = val.as_array();

                                array canonicalArr;
                                canonicalArr.reserve( arr.size() );

                                for( const auto& elem : arr )
                                {
                                    canonicalArr.push_back( canonicalizeValue( elem ) );
                                }

                                return canonicalArr;
                            }

                        default:
                            /*
                             * Primitives (string, number, bool, null) are returned as-is
                             */
                            return val;
                    }
                }

                static void prettyPrintImpl(
                    SAA_inout       std::ostream&                             os,
                    SAA_in          const value&                              jv,
                    SAA_inout       std::string&                              indent
                    )
                {
                    switch( jv.kind() )
                    {
                        case kind::object:
                            {
                                os << "{\n";
                                indent.append( 4, ' ' );

                                const auto& obj = jv.as_object();

                                if( ! obj.empty() )
                                {
                                    auto it = obj.begin();

                                    for( ;; )
                                    {
                                        os << indent << boost::json::serialize( it -> key() ) << ": ";
                                        prettyPrintImpl( os, it -> value(), indent );

                                        if( ++it == obj.end() )
                                        {
                                            break;
                                        }

                                        os << ",\n";
                                    }
                                }

                                os << "\n";
                                indent.resize( indent.size() - 4 );
                                os << indent << "}";
                            }
                            break;

                        case kind::array:
                            {
                                os << "[\n";
                                indent.append( 4, ' ' );

                                const auto& arr = jv.as_array();

                                if( ! arr.empty() )
                                {
                                    auto it = arr.begin();

                                    for( ;; )
                                    {
                                        os << indent;
                                        prettyPrintImpl( os, *it, indent );

                                        if( ++it == arr.end() )
                                        {
                                            break;
                                        }

                                        os << ",\n";
                                    }
                                }

                                os << "\n";
                                indent.resize( indent.size() - 4 );
                                os << indent << "]";
                            }
                            break;

                        case kind::string:
                            os << boost::json::serialize( jv.as_string() );
                            break;

                        case kind::uint64:
                            os << boost::json::serialize( jv );
                            break;

                        case kind::int64:
                            os << boost::json::serialize( jv );
                            break;

                        case kind::double_:
                            os << boost::json::serialize( jv );
                            break;

                        case kind::bool_:
                            os << ( jv.as_bool() ? "true" : "false" );
                            break;

                        case kind::null:
                            os << "null";
                            break;
                    }
                }

                static std::string prettyPrint( SAA_in const value& jv )
                {
                    std::ostringstream os;
                    std::string indent;

                    prettyPrintImpl( os, jv, indent );

                    return os.str();
                }

                static std::string saveToString(
                    SAA_in          const value&                              val,
                    SAA_in          const bool                                prettyPrint,
                    SAA_in          const bool                                rawUtf8,
                    SAA_in_opt      const bool                                canonicalize = false
                    )
                {
                    BL_UNUSED( rawUtf8 );

                    if( canonicalize && prettyPrint )
                    {
                        BL_THROW(
                            ArgumentException(),
                            "Cannot use both prettyPrint and canonicalize options together"
                            );
                    }

                    if( canonicalize )
                    {
                        /*
                         * Create a canonicalized (sorted keys) version and serialize as minified JSON
                         */

                        const auto canonicalValue = canonicalizeValue( val );

                        return boost::json::serialize( canonicalValue );
                    }

                    if( prettyPrint )
                    {
                        return JsonUtilsImplT::prettyPrint( val );
                    }

                    return boost::json::serialize( val );
                }

                static std::string saveToString(
                    SAA_in          const object&                             rootObject,
                    SAA_in          const bool                                prettyPrint,
                    SAA_in          const bool                                rawUtf8,
                    SAA_in_opt      const bool                                canonicalize = false
                    )
                {
                    return saveToString( value( rootObject ), prettyPrint, rawUtf8, canonicalize );
                }

                static std::string saveToString(
                    SAA_in          const array&                              arr,
                    SAA_in          const bool                                prettyPrint,
                    SAA_in          const bool                                rawUtf8,
                    SAA_in_opt      const bool                                canonicalize = false
                    )
                {
                    return saveToString( value( arr ), prettyPrint, rawUtf8, canonicalize );
                }

                template
                <
                    typename STREAM
                >
                static void saveToStream(
                    SAA_in          const value&                              val,
                    SAA_inout       STREAM&                                   output,
                    SAA_in          const bool                                prettyPrint,
                    SAA_in          const bool                                rawUtf8
                    )
                {
                    output << saveToString( val, prettyPrint, rawUtf8 );
                }

                static void remapIncorrectValueTypeException(
                    SAA_in      const std::runtime_error&           e,
                    SAA_in      const std::exception_ptr&           eptr,
                    SAA_in      const std::string&                  context,
                    SAA_in_opt  const bool                          userException = false
                    )
                {
                    /*
                     * Boost.JSON throws std::invalid_argument for type mismatches.
                     * Re-throw with more context.
                     */

                    const std::string message = resolveMessage(
                        BL_MSG()
                            << "JSON parsing error: "
                            << e.what()
                            << " for "
                            << context
                        );

                    if( userException )
                    {
                        BL_THROW(
                            UserMessageException()
                                << eh::errinfo_nested_exception_ptr( eptr ),
                            message
                            );
                    }
                    else
                    {
                        BL_THROW_USER_FRIENDLY(
                            JsonException()
                                << eh::errinfo_nested_exception_ptr( eptr ),
                            message
                            );
                    }
                }
            };

            typedef JsonUtilsImplT<> JsonUtilsImpl;

        } // detail

    } // json

} // bl

#endif /* __BL_BOOSTJSONIMPL_H_ */
