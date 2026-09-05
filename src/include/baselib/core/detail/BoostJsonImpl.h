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
#include <limits>
#include <cmath>
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

        inline std::int64_t get_int64( SAA_in const value& v )
        {
            /*
             * A value constructed in memory from a std::uint64_t has the unsigned kind whatever
             * its magnitude (the parser only produces that kind above INT64_MAX), and
             * boost::json::value::as_int64() refuses the kind rather than the magnitude; a value
             * which fits is accepted here, as value_to< std::int64_t > and the json-spirit backend
             * accept it, and one which does not is refused as out of range
             */

            if( v.is_uint64() )
            {
                const auto val = v.as_uint64();

                if( val > static_cast< std::uint64_t >( std::numeric_limits< std::int64_t >::max() ) )
                {
                    BL_THROW(
                        JsonException(),
                        BL_MSG()
                            << "JSON integer value '"
                            << val
                            << "' is out of range for the requested integer type"
                        );
                }

                return static_cast< std::int64_t >( val );
            }

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
            /*
             * JSON has a single number type, so an integer valued number is accepted here and
             * widened; this matches the json-spirit backend, whose get_real() has always done
             * the same, and boost::json::value_to< double >, which also accepts every number
             * kind - only boost::json::value::as_double() is strict about the stored kind
             */

            if( v.is_int64() )
            {
                return static_cast< double >( v.as_int64() );
            }

            if( v.is_uint64() )
            {
                return static_cast< double >( v.as_uint64() );
            }

            return v.as_double();
        }

        /*
         * Template accessor for type-safe value extraction
         *
         * Forwards to boost::json::value_to<T>(), which supports arithmetic types, bool,
         * std::string, and other common types out of the box, with one policy added on top: an
         * integral C++ type is never filled from a JSON number which is stored as a double, not
         * even when the conversion would be exact (3.0 into an int). Boost.JSON's own
         * to_number() accepts that; json-spirit stores 3.0 as a real and its integer getters
         * refuse it, and get_int() above refuses it on this backend as well, so this keeps the
         * numeric policy identical between the two backends and within this one - see the
         * matching note in JsonSpiritImpl.h. The rejection is reported as Boost.JSON's own
         * not_integer error, so that it takes the same path as every other kind mismatch.
         */

        namespace detail
        {
            template
            <
                typename T
            >
            inline void checkIntegralKind(
                SAA_in          const value&                            v,
                SAA_in          std::true_type                          /* integral, non-bool */
                )
            {
                if( v.is_double() )
                {
                    throw boost::system::system_error(
                        boost::system::error_code( boost::json::error::not_integer )
                        );
                }
            }

            template
            <
                typename T
            >
            inline void checkIntegralKind(
                SAA_in          const value&                            /* v */,
                SAA_in          std::false_type                         /* not integral */
                ) NOEXCEPT
            {
            }

        } // detail

        template
        <
            typename T
        >
        inline T value_to( SAA_in const value& v )
        {
            detail::checkIntegralKind< T >(
                v,
                std::integral_constant<
                    bool,
                    std::is_integral< T >::value && ! std::is_same< T, bool >::value
                    >()
                );

            return boost::json::value_to< T >( v );
        }

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

                /*
                 * The maximum object / array nesting depth accepted when parsing
                 *
                 * Boost.JSON applies a default of 32, which is low for this library because the
                 * data model carries opaque caller-supplied payloads whose depth it does not
                 * control - BrokerProtocol::passThroughUserData and FunctionInputData::arguments
                 * are declared with BL_DM_DECLARE_CUSTOM_PROPERTY and hold whatever the caller put
                 * there. The library's own models nest around five levels
                 *
                 * Setting it explicitly raises the limit rather than lowering it, so nothing which
                 * parses today stops parsing. See the note on bl::json::readFromString in
                 * baselib/core/JsonUtils.h for the contract this establishes and for why the
                 * json-spirit backend, which applies no limit, is deliberately left alone
                 */

                static const std::size_t MAX_PARSE_DEPTH = 512;

                static boost::json::parse_options parseOptions() NOEXCEPT
                {
                    boost::json::parse_options options;

                    options.max_depth = MAX_PARSE_DEPTH;

                    /*
                     * Boost.JSON's default number mode is 'imprecise': a double is computed as
                     * mantissa times a power of ten with two roundings, so a literal with more
                     * than 17 significant digits can land one ULP away from the correctly
                     * rounded value every other parser (and this library's own serializer, which
                     * emits the shortest round-trip text) would produce. A payload passed through
                     * unchanged would then re-serialize as a different literal and hash
                     * differently on each hop. The precise mode is correctly rounded
                     */

                    options.numbers = boost::json::number_precision::precise;

                    return options;
                }

            public:

                static value readFromString( SAA_in const std::string& input )
                {
                    try
                    {
                        /*
                         * Note that when an object contains the same member name more than once
                         * Boost.JSON keeps the last of the equal members; that is the documented
                         * contract of this library - see the comment on bl::json::readFromString
                         * in baselib/core/JsonUtils.h
                         */

                        return boost::json::parse(
                            input,
                            boost::json::storage_ptr(),
                            parseOptions()
                            );
                    }
                    catch( const boost::system::system_error& e )
                    {
                        /*
                         * The rejected document is dumped at trace level only: it is whatever
                         * arrived on the wire and may carry credentials, so it must not reach a
                         * log which is enabled in normal operation
                         */

                        BL_LOG_MULTILINE(
                            Logging::trace(),
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
                        /*
                         * The duplicate member name handling is the same as on the one-shot path
                         * above - the last of the equal members wins
                         */

                        boost::json::stream_parser parser( boost::json::storage_ptr(), parseOptions() );
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
                 *
                 * Note that this is a PROJECT SPECIFIC stable ordering and NOT RFC 8785 / JCS
                 * canonical JSON: keys are ordered by their UTF-8 bytes whereas JCS orders them
                 * by UTF-16 code units, which differ for some Unicode keys, and none of the JCS
                 * number and string normalization rules are applied here
                 *
                 * It is therefore suitable for making a hash reproducible within a single
                 * backend, which is what it is used for, and it must not be described as or
                 * relied upon to be interoperable with a JCS implementation. Adopting a fully
                 * specified canonical format is a separate decision with a data migration
                 * attached to it - see notes/plans/issues/medium-severity-findings-f11-f17-plan.md
                 * (F-11)
                 */

                /**
                 * @brief Refuses a value tree which holds a double that is not finite
                 *
                 * JSON has no representation for an infinity or a NaN; Boost.JSON's serializer
                 * would emit an out-of-range literal or null in their place, which silently
                 * changes the value, so they are refused before anything is written. This is
                 * one linear pass over the tree, like the serialization which follows it
                 */

                static void chkNoNonFiniteDoubles( SAA_in const value& val )
                {
                    switch( val.kind() )
                    {
                        case kind::object:
                            for( const auto& kvp : val.as_object() )
                            {
                                chkNoNonFiniteDoubles( kvp.value() );
                            }
                            break;

                        case kind::array:
                            for( const auto& elem : val.as_array() )
                            {
                                chkNoNonFiniteDoubles( elem );
                            }
                            break;

                        case kind::double_:
                            if( ! std::isfinite( val.as_double() ) )
                            {
                                BL_THROW(
                                    JsonException(),
                                    BL_MSG()
                                        << "A JSON document cannot carry a double which is not finite ("
                                        << val.as_double()
                                        << ")"
                                    );
                            }
                            break;

                        default:
                            break;
                    }
                }

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
                                const auto& obj = jv.as_object();

                                /*
                                 * An empty object is emitted as {} rather than as an open brace, a
                                 * blank line and a close brace. Note that this is one of the
                                 * documented pretty print divergences between the backends:
                                 * json-spirit emits an open brace, a newline and a close brace for
                                 * the same value (see JsonPrettyPrintEmptyContainers in
                                 * utf_baselib_data, which pins both spellings)
                                 */

                                if( obj.empty() )
                                {
                                    os << "{}";
                                    break;
                                }

                                os << "{\n";
                                indent.append( 4, ' ' );

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

                                os << "\n";
                                indent.resize( indent.size() - 4 );
                                os << indent << "}";
                            }
                            break;

                        case kind::array:
                            {
                                const auto& arr = jv.as_array();

                                /*
                                 * As for the empty object above - [] rather than an open bracket, a
                                 * blank line and a close bracket, and likewise a divergence from
                                 * json-spirit
                                 */

                                if( arr.empty() )
                                {
                                    os << "[]";
                                    break;
                                }

                                os << "[\n";
                                indent.append( 4, ' ' );

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
                    /*
                     * Boost.JSON always serializes string content as raw UTF-8; see the note on
                     * rawUtf8 in baselib/core/JsonUtils.h
                     */

                    BL_UNUSED( rawUtf8 );

                    /*
                     * At this layer 'canonicalize' means one thing only: sort object keys, so the
                     * serialized bytes do not depend on insertion order. It is spelled the same as
                     * the data model flag of the same name, which carries a different meaning - see
                     * the note on getJsonString() in baselib/data/DataModelObject.h
                     *
                     * The mutual exclusion below is a DELIBERATE NARROWING and not an implementation
                     * limitation. The two options compose trivially - prettyPrint( canonicalizeValue(
                     * val ) ) would be a few lines - but the combination has no caller, canonical
                     * output exists to be hashed rather than read, and rejecting it keeps one
                     * meaning per call. Do not re-file this as a defect; if a caller ever genuinely
                     * needs sorted pretty output, removing the throw is the whole change
                     */

                    if( canonicalize && prettyPrint )
                    {
                        BL_THROW(
                            ArgumentException(),
                            "Cannot use both prettyPrint and canonicalize options together"
                            );
                    }

                    chkNoNonFiniteDoubles( val );

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
                    SAA_in          const bool                                rawUtf8,
                    SAA_in_opt      const bool                                canonicalize = false
                    )
                {
                    output << saveToString( val, prettyPrint, rawUtf8, canonicalize );
                }

                /**
                 * @brief Returns a readable description for the Boost.JSON conversion errors
                 * which the accessors above and the data model layer can produce, or nullptr
                 * for anything else
                 *
                 * Boost.JSON reports a type mismatch as a boost::system::system_error (which
                 * derives from std::runtime_error) carrying a boost::json::error code, and its
                 * own text ("value is not a std::int64_t number [boost.json:9]") is written for
                 * a developer, not for the user of an application; only the codes recognized
                 * here are rewritten and marked as user friendly, the raw text stays available
                 * in the nested exception
                 */

                static const char* friendlyConversionText( SAA_in const std::runtime_error& e ) NOEXCEPT
                {
                    static const struct
                    {
                        boost::json::error                  code;
                        const char*                         text;
                    }
                    table[] =
                    {
                        { boost::json::error::not_number,       "expected a number" },
                        { boost::json::error::not_double,       "expected a number" },
                        { boost::json::error::not_integer,      "expected an integer" },
                        { boost::json::error::not_int64,        "expected an integer" },
                        { boost::json::error::not_uint64,       "expected an unsigned integer" },
                        { boost::json::error::not_exact,        "number is out of range or not exact for the requested type" },
                        { boost::json::error::not_string,       "expected a string" },
                        { boost::json::error::not_bool,         "expected a boolean" },
                        { boost::json::error::not_object,       "expected an object" },
                        { boost::json::error::not_array,        "expected an array" },
                        { boost::json::error::not_null,         "expected null" },
                        { boost::json::error::size_mismatch,    "array size does not match the expected size" },
                    };

                    const auto* systemError = dynamic_cast< const boost::system::system_error* >( &e );

                    if( systemError )
                    {
                        for( const auto& entry : table )
                        {
                            if( systemError -> code() == entry.code )
                            {
                                return entry.text;
                            }
                        }
                    }

                    return nullptr;
                }

                static void remapIncorrectValueTypeException(
                    SAA_in      const std::runtime_error&           e,
                    SAA_in      const std::exception_ptr&           eptr,
                    SAA_in      const std::string&                  context,
                    SAA_in_opt  const bool                          userException = false
                    )
                {
                    /*
                     * Re-throw with more context; the message is marked as user friendly only
                     * when the error is one of the recognized conversion errors, for which a
                     * readable text is substituted (see friendlyConversionText above), and the
                     * original exception is nested either way
                     */

                    const char* friendlyText = friendlyConversionText( e );

                    const std::string message = resolveMessage(
                        BL_MSG()
                            << "JSON parsing error: "
                            << ( friendlyText ? friendlyText : e.what() )
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
                    else if( friendlyText )
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
            };

            typedef JsonUtilsImplT<> JsonUtilsImpl;

        } // detail

    } // json

} // bl

#endif /* __BL_BOOSTJSONIMPL_H_ */
