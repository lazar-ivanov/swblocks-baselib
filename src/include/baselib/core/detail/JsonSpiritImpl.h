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

#ifndef __BL_JSONSPIRITIMPL_H_
#define __BL_JSONSPIRITIMPL_H_

/*
 * JSON Spirit Implementation using Template Wrapper Classes
 *
 * This header provides wrapper template classes that inherit from STL containers
 * and json_spirit::Value_impl, adding Boost.JSON-compatible interface methods.
 * The wrapper templates are used in a custom ConfigMap to instantiate json-spirit
 * with our extended types.
 *
 * Key design:
 * - value_wrapper<Config> inherits from Value_impl<Config> and adds as_*() / is_*() methods
 * - array_wrapper<T, Alloc> inherits from std::vector<T, Alloc>
 * - object_wrapper<Key, T, Comp, Alloc> inherits from std::map<...> and adds contains()
 *
 * This design stores wrapper types directly in json-spirit's variant, enabling
 * zero-copy reference returns from as_object() / as_array().
 *
 * Note: Unlike Boost.JSON which provides .key()/.value() accessors on pairs,
 * json-spirit uses std::map with std::pair, so .first/.second are used instead.
 * The BL_JSON_PAIR_KEY/BL_JSON_PAIR_VALUE macros (defined near the top of the file)
 * abstract this difference for code that needs to work with both implementations.
 */

#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

#define BOOST_SPIRIT_THREADSAFE

/*
 * GCC 15+ produces false positive -Wrestrict warnings in optimized builds
 * when analyzing json_spirit template code. Suppress this warning only
 * when including json_spirit headers.
 */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
#endif

#include <json_spirit/json_spirit_reader_template.h>
#include <json_spirit/json_spirit_writer_template.h>

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic pop
#endif

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <limits>

/*
 * JSON accessor macros for json-spirit
 *
 * json-spirit uses std::map with std::pair, so we access keys/values via .first/.second
 * These macros provide a unified interface that works with both json-spirit and Boost.JSON
 */

#define BL_JSON_ITER_VALUE( iter )                  ( iter ) -> second
#define BL_JSON_PAIR_KEY( pair )                    ( pair ).first
#define BL_JSON_PAIR_VALUE( pair )                  ( pair ).second

namespace bl
{
    namespace json
    {
        /*
         * Implementation name for diagnostics and logging
         */

        inline const char* implName() NOEXCEPT
        {
            return "json-spirit";
        }

        /*
         * ValueType compatibility enum (legacy)
         */

        typedef json_spirit::Value_type                             ValueType;

        /*
         * Template wrapper classes
         *
         * These are defined generically first, then instantiated by WrapperConfig.
         */

        /*
         * value_wrapper - wraps json_spirit::Value_impl, adds Boost.JSON interface
         *
         * Template parameter is the Config type
         */

        template< typename Config >
        class value_wrapper : public json_spirit::Value_impl< Config >
        {
            typedef json_spirit::Value_impl< Config >               base_type;

        public:

            using base_type::base_type;
            using base_type::type;
            using base_type::is_null;
            using base_type::is_uint64;
            using base_type::get_str;
            using base_type::get_obj;
            using base_type::get_array;
            using base_type::get_bool;
            using base_type::get_int;
            using base_type::get_int64;
            using base_type::get_uint64;
            using base_type::get_real;
            using base_type::get_value;

            /*
             * Default constructor creates null value
             */

            value_wrapper()
                :   base_type()
            {
            }

            /*
             * Copy constructor from base type
             */

            value_wrapper( SAA_in const base_type& other )
                :   base_type( other )
            {
            }

            /*
             * Boost.JSON-style type checking
             */

            bool is_string() const NOEXCEPT
            {
                return type() == json_spirit::str_type;
            }

            bool is_object() const NOEXCEPT
            {
                return type() == json_spirit::obj_type;
            }

            bool is_array() const NOEXCEPT
            {
                return type() == json_spirit::array_type;
            }

            bool is_bool() const NOEXCEPT
            {
                return type() == json_spirit::bool_type;
            }

            bool is_int64() const NOEXCEPT
            {
                return type() == json_spirit::int_type;
            }

            bool is_double() const NOEXCEPT
            {
                return type() == json_spirit::real_type;
            }

            /*
             * Boost.JSON-style accessors
             */

            const std::string& as_string() const
            {
                return get_str();
            }

            typename Config::Object_type& as_object()
            {
                return get_obj();
            }

            const typename Config::Object_type& as_object() const
            {
                return get_obj();
            }

            typename Config::Array_type& as_array()
            {
                return get_array();
            }

            const typename Config::Array_type& as_array() const
            {
                return get_array();
            }

            bool as_bool() const
            {
                return get_bool();
            }

            std::int64_t as_int64() const
            {
                return get_int64();
            }

            std::uint64_t as_uint64() const
            {
                return get_uint64();
            }

            double as_double() const
            {
                return get_real();
            }

            /*
             * Comparison operators
             */

            bool operator==( SAA_in const std::string& other ) const
            {
                return is_string() && get_str() == other;
            }

            bool operator==( SAA_in const char* other ) const
            {
                return is_string() && get_str() == other;
            }

            bool operator!=( SAA_in const std::string& other ) const
            {
                return !( *this == other );
            }

            bool operator!=( SAA_in const char* other ) const
            {
                return !( *this == other );
            }
        };

        /*
         * array_wrapper - wraps std::vector, adds Boost.JSON interface
         *
         * Template parameters match std::vector
         */

        template< typename T, typename Allocator = std::allocator< T > >
        class array_wrapper : public std::vector< T, Allocator >
        {
            typedef std::vector< T, Allocator >                     base_type;

        public:

            using base_type::base_type;

            /*
             * All std::vector methods inherited:
             * size(), empty(), clear(), reserve()
             * at(), operator[]
             * push_back(), emplace_back()
             * begin(), end()
             */
        };

        /*
         * object_wrapper - wraps std::map, adds Boost.JSON interface
         *
         * Template parameters match std::map.
         * Unlike Boost.JSON, iteration yields std::pair with .first/.second (not .key()/.value()).
         * Use BL_JSON_PAIR_KEY/BL_JSON_PAIR_VALUE macros for portable code.
         */

        template<
            typename Key,
            typename T,
            typename Compare = std::less< Key >,
            typename Allocator = std::allocator< std::pair< const Key, T > >
        >
        class object_wrapper : public std::map< Key, T, Compare, Allocator >
        {
            typedef std::map< Key, T, Compare, Allocator >          base_type;

        public:

            using base_type::base_type;

            /*
             * Boost.JSON-style contains() (C++20 added this to std::map, but we support older standards)
             */

            bool contains( SAA_in const Key& key ) const
            {
                return base_type::find( key ) != base_type::end();
            }
        };

        namespace detail
        {
            /*
             * Forward declare WrapperConfig for use in wrapper templates
             */

            struct WrapperConfig;

            /*
             * WrapperConfig - custom json-spirit Config using our wrapper classes
             *
             * Note: Value_type uses value_wrapper<WrapperConfig>, which inherits from
             * Value_impl<WrapperConfig> and adds Boost.JSON interface methods.
             */

            struct WrapperConfig
            {
                typedef std::string                                             String_type;
                typedef value_wrapper< WrapperConfig >                          Value_type;
                typedef array_wrapper< Value_type >                             Array_type;
                typedef object_wrapper< String_type, Value_type >               Object_type;
                typedef std::pair< const String_type, Value_type >              Pair_type;

                /**
                 * @brief Inserts a member into an object while the parser builds it
                 *
                 * Note that rejecting a repeated member name is specific to this backend and is
                 * NOT part of the library contract; the default Boost.JSON backend keeps the
                 * last of the equal members instead. See the comment on bl::json::readFromString
                 * in baselib/core/JsonUtils.h
                 *
                 * Note also that the exception thrown here is bl::UserMessageException rather
                 * than bl::JsonException, which the rest of the parse error paths in this file
                 * use
                 */

                static Value_type& add(
                    SAA_inout   Object_type&                                    obj,
                    SAA_in      const String_type&                              name,
                    SAA_in      const Value_type&                               val
                    )
                {
                    auto pair = obj.emplace( name, val );

                    if( ! pair.second )
                    {
                        BL_THROW_USER(
                            BL_MSG()
                                << "Duplicate entry encountered for property with name '"
                                << name
                                << "' while parsing a JSON object"
                            );
                    }

                    return pair.first -> second;
                }

                static const String_type& get_name(
                    SAA_in      const Pair_type&                                pair
                    )
                {
                    return pair.first;
                }

                static const Value_type& get_value(
                    SAA_in      const Pair_type&                                pair
                    )
                {
                    return pair.second;
                }
            };

            /*
             * Internal typedefs
             */

            typedef WrapperConfig::Value_type                       spirit_value;
            typedef WrapperConfig::Object_type                      spirit_object;
            typedef WrapperConfig::Array_type                       spirit_array;

        } // namespace detail

        /*
         * Public type aliases - these are what users see
         */

        typedef detail::spirit_value                                value;
        typedef detail::spirit_object                               object;
        typedef detail::spirit_array                                array;

        /*
         * Legacy type aliases (JSON Spirit compatibility)
         */

        typedef value                                               Value;
        typedef object                                              Object;
        typedef array                                               Array;

        namespace detail
        {
            /*
             * Checked numeric conversions
             *
             * These exist so that this backend applies the same numeric policy as the
             * Boost.JSON backend, whose value_to<T> is range checked by Boost itself:
             *
             * -- a negative JSON integer is never converted to an unsigned C++ type
             * -- a value which does not fit the requested C++ type is rejected rather than
             *    wrapped or truncated
             * -- an integer valued JSON number may be read as a double, because JSON has a
             *    single number type and the widening is exact for the range which matters
             *
             * Without these the json-spirit accessors cast silently, so a JSON -1 read as an
             * unsigned type used to become 18446744073709551615
             */

            inline std::uint64_t checkedUInt64( SAA_in const value& v )
            {
                if( v.is_uint64() )
                {
                    return v.as_uint64();
                }

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

            inline int checkedInt( SAA_in const value& v )
            {
                if( v.is_uint64() )
                {
                    const auto val = v.as_uint64();

                    if( val > static_cast< std::uint64_t >( std::numeric_limits< int >::max() ) )
                    {
                        BL_THROW(
                            JsonException(),
                            BL_MSG()
                                << "JSON integer value '"
                                << val
                                << "' is out of range for the requested integer type"
                            );
                    }

                    return static_cast< int >( val );
                }

                const auto val = v.as_int64();

                if(
                    val < static_cast< std::int64_t >( std::numeric_limits< int >::min() ) ||
                    val > static_cast< std::int64_t >( std::numeric_limits< int >::max() )
                    )
                {
                    BL_THROW(
                        JsonException(),
                        BL_MSG()
                            << "JSON integer value '"
                            << val
                            << "' is out of range for the requested integer type"
                        );
                }

                return static_cast< int >( val );
            }

        } // namespace detail

        /*
         * Free-standing value_to<T>() template functions - matching Boost.JSON API
         */

        template< typename T >
        inline T value_to( SAA_in const value& v )
        {
            return v.template get_value< T >();
        }

        template<>
        inline bool value_to< bool >( SAA_in const value& v )
        {
            return v.as_bool();
        }

        template<>
        inline int value_to< int >( SAA_in const value& v )
        {
            return detail::checkedInt( v );
        }

        template<>
        inline std::int64_t value_to< std::int64_t >( SAA_in const value& v )
        {
            return v.as_int64();
        }

        template<>
        inline std::uint64_t value_to< std::uint64_t >( SAA_in const value& v )
        {
            return detail::checkedUInt64( v );
        }

        template<>
        inline double value_to< double >( SAA_in const value& v )
        {
            return v.as_double();
        }

        template<>
        inline std::string value_to< std::string >( SAA_in const value& v )
        {
            return v.as_string();
        }

        /*
         * Convenience accessors - free functions matching json-spirit legacy API
         */

        inline std::string get_str( SAA_in const value& v )
        {
            return v.as_string();
        }

        inline bool get_bool( SAA_in const value& v )
        {
            return v.as_bool();
        }

        inline int get_int( SAA_in const value& v )
        {
            return detail::checkedInt( v );
        }

        inline std::int64_t get_int64( SAA_in const value& v )
        {
            return v.as_int64();
        }

        inline std::uint64_t get_uint64( SAA_in const value& v )
        {
            return detail::checkedUInt64( v );
        }

        inline double get_real( SAA_in const value& v )
        {
            return v.as_double();
        }

        namespace detail
        {
            /*
             * These don't need to be exposed directly due to error handling issues,
             * but the readFromString and writeToString wrappers should be used instead
             */

            using json_spirit::Error_position;

            using json_spirit::read_string_or_throw;
            using json_spirit::read_string;
            using json_spirit::write_string;

            using json_spirit::read_stream_or_throw;
            using json_spirit::read_stream;
            using json_spirit::write_stream;

            /**
             * @brief class JsonUtilsImpl - JSON utility code (json-spirit template wrapper implementation)
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

                static os::mutex                                                        g_lock;
                static const str::regex                                                 g_valueTypeRegex;

                typedef cpp::function< void ( SAA_inout spirit_value& rootValue ) >     read_callback_t;
                typedef cpp::function< bool ( SAA_inout spirit_value& rootValue ) >     fast_read_callback_t;

                static value readWrapper(
                    SAA_in                  const read_callback_t&                      callback,
                    SAA_in                  const fast_read_callback_t&                 fastCallback,
                    SAA_in_opt              const cpp::void_callback_t&                 dumpCallback = cpp::void_callback_t()
                    )
                {
                    /*
                     * As per:
                     * http://www.codeproject.com/Articles/20027/JSON-Spirit-A-C-JSON-Parser-Generator-Implemented
                     *
                     * The fast callback is ~ 3x faster, but does not throw an
                     * exception with the error info, so we need to call the slow
                     * callback when there is an error parsing to get the error information
                     */

                    spirit_value rootValue;

                    if( fastCallback( rootValue ) )
                    {
                        return rootValue;
                    }

                    /*
                     * Even though defining BOOST_SPIRIT_THREADSAFE should make the parser
                     * thread safe, there is still a bug exposed when JSON parsing from
                     * multiple threads using the "slow" callback, hence a lock is used
                     */

                    BL_MUTEX_GUARD( g_lock );

                    try
                    {
                        rootValue = spirit_value();

                        callback( rootValue );
                    }
                    catch( Error_position& e )
                    {
                        if( dumpCallback )
                        {
                            dumpCallback();
                        }

                        /*
                         * The JSON spirit throws Error_position structure in case of
                         * parsing error, convert it to regular exception type
                         */

                        BL_THROW(
                            JsonException()
                                << eh::errinfo_parser_line( e.line_ )
                                << eh::errinfo_parser_column( e.column_ )
                                << eh::errinfo_parser_reason( e.reason_ )
                                ,
                            BL_MSG()
                                << "JSON parser error at line: "
                                << e.line_
                                << ", column: "
                                << e.column_
                                << ", reason: '"
                                << e.reason_
                                << "'"
                            );
                    }
                    catch( std::runtime_error& e )
                    {
                        remapIncorrectValueTypeException( e, std::current_exception(), "JSON string" );
                    }

                    /*
                     * If we are here then something is very wrong because the callback
                     * call above is expected to produce the same result as fastCallback
                     * (i.e. to fail), but throw an exception instead and the catch blocks
                     * are also expected to throw / re-throw
                     */

                    BL_RIP_MSG( "JSON parsing callback is expected to throw" );

                    return json::value();
                }

            public:

                static value readFromString( SAA_in const std::string& input )
                {
                    return readWrapper(
                        cpp::bind( &read_string_or_throw< std::string, spirit_value >, cpp::cref( input ), _1 ),
                        cpp::bind( &read_string< std::string, spirit_value >, cpp::cref( input ), _1 ), /* fast CB */
                        [ &input ]() -> void
                        {
                            /*
                             * This is the dump callback which will be called in case of an error
                             */

                            BL_LOG_MULTILINE(
                                Logging::debug(),
                                BL_MSG()
                                    << "Invalid JSON string:\n"
                                    << ( input.size() < MAX_DUMP_STRING_LENGTH ?
                                            input :
                                            input.substr( 0, MAX_DUMP_STRING_LENGTH ) + "..."
                                            )
                                );
                        }
                        );
                }

                template
                <
                    typename STREAM
                >
                static value readFromStream( SAA_inout STREAM& input )
                {
                    input.exceptions( std::ios::badbit );

                    return readWrapper(
                        cpp::bind( &read_stream_or_throw< STREAM, spirit_value >, cpp::ref( input ), _1 ),
                        cpp::bind( &read_stream< STREAM, spirit_value >, cpp::ref( input ), _1 ), /* fast CB */
                        []() -> void
                        {
                            /*
                             * This is the dump callback which will be called in case of an error
                             */

                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Invalid JSON blob parsed from stream"
                                );
                        }
                        );
                }

                static std::string saveToString(
                    SAA_in          const value&                              val,
                    SAA_in          const bool                                prettyPrint,
                    SAA_in          const bool                                rawUtf8,
                    SAA_in_opt      const bool                                canonicalize = false
                    )
                {
                    /*
                     * For json-spirit, canonicalize is ignored since std::map already
                     * maintains alphabetically sorted keys. However, we still validate
                     * that prettyPrint and canonicalize are not both true.
                     */

                    if( canonicalize && prettyPrint )
                    {
                        BL_THROW(
                            ArgumentException(),
                            "Cannot use both prettyPrint and canonicalize options together"
                            );
                    }

                    /*
                     * Note that json_spirit::raw_utf8 is requested unconditionally and the
                     * rawUtf8 parameter is deliberately not honored
                     *
                     * With that option off json-spirit escapes every byte above 0x7F
                     * individually as \u00XX, deciding byte by byte via iswprint(), which has
                     * three problems: the output is locale dependent; a multi-byte UTF-8
                     * sequence is emitted as one escape per byte, so a conformant parser reads
                     * back a different string than was written (U+00E9 becomes the two
                     * characters U+00C3 U+00A9); and it therefore does not round trip outside
                     * of json-spirit itself
                     *
                     * RFC 8259 section 8.1 requires JSON exchanged between systems to be
                     * encoded in UTF-8, which is what the Boost.JSON backend always emits, so
                     * emitting it here as well is both correct and what makes the two backends
                     * agree
                     */

                    BL_UNUSED( rawUtf8 );

                    const int options =
                        ( prettyPrint ? json_spirit::pretty_print : json_spirit::none ) |
                        json_spirit::raw_utf8;

                    return write_string( val, options );
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
                    /*
                     * Note that json_spirit::raw_utf8 is requested unconditionally and the
                     * rawUtf8 parameter is deliberately not honored
                     *
                     * With that option off json-spirit escapes every byte above 0x7F
                     * individually as \u00XX, deciding byte by byte via iswprint(), which has
                     * three problems: the output is locale dependent; a multi-byte UTF-8
                     * sequence is emitted as one escape per byte, so a conformant parser reads
                     * back a different string than was written (U+00E9 becomes the two
                     * characters U+00C3 U+00A9); and it therefore does not round trip outside
                     * of json-spirit itself
                     *
                     * RFC 8259 section 8.1 requires JSON exchanged between systems to be
                     * encoded in UTF-8, which is what the Boost.JSON backend always emits, so
                     * emitting it here as well is both correct and what makes the two backends
                     * agree
                     */

                    BL_UNUSED( rawUtf8 );

                    const int options =
                        ( prettyPrint ? json_spirit::pretty_print : json_spirit::none ) |
                        json_spirit::raw_utf8;

                    write_stream( val, output, options );
                }

                static void remapIncorrectValueTypeException(
                    SAA_in      const std::runtime_error&           e,
                    SAA_in      const std::exception_ptr&           eptr,
                    SAA_in      const std::string&                  context,
                    SAA_in_opt  const bool                          userException = false
                    )
                {
                    /*
                     * JSON Spirit parser throws cryptic "value type is X not Y" exception
                     * when the requested type from a getter doesn't match the underlying
                     * variant value - see check_type() in json_spirit_value.h.
                     * Translate this to more informational exception here.
                     */

                    bool isUserFriendly = false;
                    std::string message;
                    str::cmatch results;

                    if( str::regex_match( e.what(), results, g_valueTypeRegex ) )
                    {
                        const std::string actualTypeStr = results[ 2 ];
                        const std::string expectedTypeStr = results[ 1 ];

                        message = resolveMessage(
                            BL_MSG()
                                << "JSON parsing error: expected value type is '"
                                << expectedTypeStr
                                << "' while actual type is '"
                                << actualTypeStr
                                << "' for "
                                << context
                            );

                        isUserFriendly = true;
                    }
                    else
                    {
                        message = e.what();
                    }

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
                        if( isUserFriendly )
                        {
                            BL_THROW_USER_FRIENDLY(
                                JsonException()
                                    << eh::errinfo_nested_exception_ptr( eptr ),
                                message
                                );
                        }
                        else
                        {
                            BL_THROW(
                                JsonException()
                                    << eh::errinfo_nested_exception_ptr( eptr ),
                                message
                                );
                        }
                    }

                    throw e;
                }
            };

            BL_DEFINE_STATIC_MEMBER( JsonUtilsImplT, os::mutex, g_lock );
            BL_DEFINE_STATIC_MEMBER( JsonUtilsImplT, const str::regex, g_valueTypeRegex )( "get_value< (\\w+) > called on (\\w+) Value" );

            typedef JsonUtilsImplT<> JsonUtilsImpl;

        } // detail

    } // json

} // bl

#endif /* __BL_JSONSPIRITIMPL_H_ */
