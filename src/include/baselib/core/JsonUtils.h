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

#ifndef __BL_JSONUTILS_H_
#define __BL_JSONUTILS_H_

/*
 * JSON library abstraction layer
 *
 * By default uses Boost.JSON library with direct typedefs (zero overhead).
 * Define BL_USE_JSON_SPIRIT to use json-spirit instead via compatibility wrappers.
 *
 *
 * LINK REQUIREMENT
 *
 * The default backend is NOT header-only. Boost.JSON is a compiled library and including this
 * header on the default path creates a link dependency on boost_json; the project makefiles add it
 * in projects/make/3rd/boost/common.mk. A consumer who includes this header from their own build
 * system and does not link boost_json gets unresolved symbols with nothing pointing at the cause,
 * which is why it is stated here.
 *
 * Building with BL_USE_JSON_SPIRIT selects json-spirit, which is header-only and adds no link
 * dependency. Note that Boost dropped BOOST_JSON_STANDALONE in 1.81, so there is no header-only
 * mode of the default backend to select instead.
 *
 *
 * THE PORTABLE SUBSET
 *
 * The two backends do not present the same API, and only their intersection is safe to use in code
 * which must build both ways - which is all shared production code in this library. Repository-wide
 * verification confirms the current code stays inside it; nothing enforces that mechanically, so it
 * is written down here.
 *
 * Available on Boost.JSON ONLY - do not use in shared code:
 *
 * -- value::kind(), is_number(), is_primitive(), is_structured()
 * -- object::if_contains(), object::reserve()
 * -- any directly qualified boost::json:: name
 *
 * Available on json-spirit ONLY - do not use in shared code:
 *
 * -- json::ValueType and the member-style getters get_str(), get_obj(), get_array(), get_int(),
 *    get_value< T >()
 *
 * Same spelling, different meaning - do not rely on either:
 *
 * -- as_string() returns const std::string& on json-spirit and boost::json::string& on Boost.JSON,
 *    so there is no common usable type. Portable code must write
 *    std::string( s.c_str(), s.size() ), as the tests do, or use json::value_to< std::string >()
 * -- is_int64() is true for unsigned values on json-spirit, which stores both in int_type, but is
 *    strictly kind::int64 on Boost.JSON
 *
 * Use the BL_JSON_ITER_VALUE, BL_JSON_PAIR_KEY and BL_JSON_PAIR_VALUE macros for iteration, since
 * the iterator and pair shapes differ between the backends.
 *
 * The json-spirit backend has no continuous verification - see
 * notes/plans/issues/json-backend-verification-decision.md for the prescribed manual check and why
 * it is manual.
 */

#include <baselib/core/StringUtils.h>

#ifdef BL_USE_JSON_SPIRIT

/*
 * json-spirit implementation using template wrapper classes
 *
 * Include the implementation header which provides template wrapper classes
 * that inherit from STL containers and json_spirit::Value_impl, adding
 * Boost.JSON-compatible interface methods.
 */

#include <baselib/core/detail/JsonSpiritImpl.h>

#else /* !BL_USE_JSON_SPIRIT - default to Boost.JSON */

/*
 * Boost.JSON implementation (default)
 *
 * Include the implementation header which provides direct typedefs to
 * native Boost.JSON types for zero-overhead access.
 */

#include <baselib/core/detail/BoostJsonImpl.h>

#endif /* BL_USE_JSON_SPIRIT */

namespace bl
{
    namespace json
    {
        /*
         * Public interface functions
         *
         * These functions are shared between both implementations and
         * delegate to the appropriate detail::JsonUtils implementation.
         */

        /**
         * @brief Parses a JSON document from a string
         *
         * Duplicate object keys - i.e. an object which contains the same member name more than
         * once - are not a supported input shape and the behavior for such a document is
         * BACKEND-DEFINED:
         *
         * -- Boost.JSON, the default backend, keeps the last of the equal members and discards
         *    the earlier ones
         *
         * -- json-spirit, selected by building with BL_USE_JSON_SPIRIT, rejects the document
         *    and throws bl::UserMessageException
         *
         * The Boost.JSON behavior is the one this library documents and it is what RFC 8259
         * permits: it says object member names SHOULD be unique and leaves the handling of
         * documents where they are not to the implementation, which is why parsers disagree.
         * Last-value-wins is what JavaScript, Python and Go do, and RFC 7515 and RFC 7519
         * explicitly allow it for JOSE and JWT. Detecting duplicates is not free - it requires
         * the parser to track the member names it has already seen for every object - which is
         * why Boost.JSON does not offer it even as an option.
         *
         * Do not rely on either behavior. A document with duplicate member names may be read
         * differently by this library and by a peer written against a different parser, so an
         * application for which that difference is security relevant must reject such documents
         * before it hands them here, or must build against the json-spirit backend, which
         * remains supported (see CONTRIBUTING.md) and rejects them during parsing.
         *
         * See notes/plans/issues/json-duplicate-key-contract.md for the full record.
         *
         *
         * DOUBLE PRECISION, backend-defined
         *
         * The Boost.JSON backend parses a double as the correctly rounded value of its literal
         * (number_precision::precise, set explicitly in detail::JsonUtilsImpl::parseOptions);
         * that is what every conformant parser produces and what this library's own serializer
         * inverts, so a document which passes through unchanged re-serializes to the same
         * bytes. The json-spirit backend uses Spirit.Classic's real number parser, which
         * accumulates digits in floating point and can land a few ULPs away from the correctly
         * rounded value for literals with many significant digits. A consumer which needs the
         * exact value of a long literal on that backend has to carry it as a string.
         *
         * A value which is not finite (an infinity or a NaN) cannot be serialized on either
         * backend - see the note on the serialization functions below.
         *
         * MAXIMUM NESTING DEPTH, also backend-defined
         *
         * The Boost.JSON backend rejects a document nested more than 512 objects or arrays deep,
         * set explicitly as detail::JsonUtilsImpl::MAX_PARSE_DEPTH. The json-spirit backend applies
         * no limit at all.
         *
         * The limit exists because a parser with no bound on recursion depth will exhaust the stack
         * on a hostile document, and 512 was chosen because this library carries opaque payloads
         * whose depth it does not control - BrokerProtocol::passThroughUserData and
         * FunctionInputData::arguments hold whatever a caller put there - while its own data models
         * nest around five levels. It is far above any legitimate document this library produces
         * and far below anything that threatens the stack.
         *
         * Note that Boost.JSON's own default is 32, so setting this RAISES the limit: every
         * document between 33 and 512 levels deep is rejected by an unconfigured Boost.JSON build
         * and accepted here. Nothing which parses today stops parsing.
         *
         * The json-spirit backend is deliberately left unbounded rather than being made to match.
         * It is selected automatically on devenv2-6 and only by explicit opt-in
         * (BL_USE_JSON_SPIRIT=1) on devenv7 and later, the two backends are never
         * loaded into the same process, and adding a depth counter to it would mean modifying a
         * third-party parser to defend a configuration which is not the default. An application
         * which parses untrusted input on that backend should bound the document size before
         * calling here.
         */

        inline json::value readFromString( SAA_in const std::string& input )
        {
            return detail::JsonUtilsImpl::readFromString( input );
        }

        template
        <
            typename STREAM
        >
        inline json::value readFromStream( SAA_in STREAM& input )
        {
            return detail::JsonUtilsImpl::readFromStream< STREAM >( input );
        }

        /*
         * Note on the rawUtf8 parameter of the serialization functions below
         *
         * It is retained for source compatibility and it has NO effect - string content is
         * always emitted as raw UTF-8, as RFC 8259 section 8.1 requires of JSON exchanged
         * between systems, on both backends
         *
         * It used to select json-spirit's non-raw mode, which escapes each byte above 0x7F
         * separately as \u00XX; that output is locale dependent and is not read back as the
         * same string by a conformant parser, so it was never a mode worth preserving. Note
         * that a document which was written by an older build in that mode and which contains
         * non-ASCII text will not read back correctly on either backend now
         *
         * The control characters U+0000 to U+001F, on the other hand, are always emitted as
         * JSON escapes on both backends, as RFC 8259 section 7 requires, using the short forms
         * where the grammar defines one and the six character form with lowercase hex digits
         * otherwise; the two backends produce identical bytes for the same string
         *
         * A double which is not finite is refused with a JsonException on both backends: JSON
         * has no representation for an infinity or a NaN (RFC 8259 section 6), json-spirit
         * would write the text 'inf' or 'nan', which no parser accepts, and Boost.JSON would
         * write an out-of-range literal or null, silently changing the value. Such a value can
         * only arise in memory or from a literal which overflowed on parse (1e400), and
         * refusing it at serialization is what keeps every emitted document valid
         */

        template
        <
            typename T
        >
        inline std::string saveToString(
            SAA_in          const T&                                json,
            SAA_in_opt      const bool                              prettyPrint = false,
            SAA_in_opt      const bool                              rawUtf8 = false,
            SAA_in_opt      const bool                              canonicalize = false
            )
        {
            return detail::JsonUtilsImpl::saveToString( json, prettyPrint, rawUtf8, canonicalize );
        }

        /**
         * @brief Serializes a JSON value directly into a stream
         *
         * The canonicalize parameter has the same meaning and the same restriction as on
         * saveToString above - it sorts object keys and it cannot be combined with prettyPrint, so
         * canonical output through this function is always compact. The parameter exists for
         * parity with saveToString; note that both backends currently implement this function by
         * serializing to a std::string and writing that to the stream, so it does not (yet) avoid
         * materializing the document - genuinely streaming output is recorded as deferred (R-4 in
         * notes/plans/issues/pr-review-opus5-residual-findings-plan.md). See the note on
         * getJsonString() in baselib/data/DataModelObject.h
         */

        template
        <
            typename STREAM
        >
        inline void saveToStream(
            SAA_in          const json::value&                      val,
            SAA_inout       STREAM&                                 output,
            SAA_in_opt      const bool                              prettyPrint = false,
            SAA_in_opt      const bool                              rawUtf8 = false,
            SAA_in_opt      const bool                              canonicalize = false
            )
        {
            detail::JsonUtilsImpl::saveToStream( val, output, prettyPrint, rawUtf8, canonicalize );
        }

        /*
         * The third argument of saveToStream used to be an OutputOptions bitmask and
         * it is now a bool; deleting the integral overload ensures that old call sites
         * which pass an option value fail to compile instead of silently binding the
         * option value to the 'prettyPrint' argument
         */

        template
        <
            typename STREAM
        >
        inline void saveToStream(
            SAA_in          const json::value&                      val,
            SAA_inout       STREAM&                                 output,
            SAA_in          const unsigned int                      options
            ) = delete;

        inline static void remapIncorrectValueTypeException(
            SAA_in      const std::runtime_error&           e,
            SAA_in      const std::exception_ptr&           eptr,
            SAA_in      const std::string&                  context,
            SAA_in_opt  const bool                          userException = false
            )
        {
            detail::JsonUtilsImpl::remapIncorrectValueTypeException( e, eptr, context, userException );
        }

        /**
         * @brief Re-throws a JsonException with the given context appended to its message
         *
         * This is the counterpart of remapIncorrectValueTypeException() for the exceptions which
         * the library's own checked accessors throw: bl::JsonException derives from
         * std::exception, not from std::runtime_error, so the catch clause which funnels a
         * backend's native conversion errors does not see them. The original exception is nested
         * and its user friendly flag is preserved, so the type and the presentation a caller
         * sees do not change, only the message gains the context
         */

        inline void rethrowWithContext(
            SAA_in      const JsonException&                e,
            SAA_in      const std::exception_ptr&           eptr,
            SAA_in      const std::string&                  context
            )
        {
            const std::string message = resolveMessage(
                BL_MSG()
                    << e.what()
                    << " for "
                    << context
                );

            if( eh::isUserFriendly( e ) )
            {
                BL_THROW_USER_FRIENDLY(
                    JsonException()
                        << eh::errinfo_nested_exception_ptr( eptr ),
                    message
                    );
            }

            BL_THROW(
                JsonException()
                    << eh::errinfo_nested_exception_ptr( eptr ),
                message
                );
        }

    } // json

    /*
     * Overload of str::joinQuoteFormattedKeys for json::object
     * Uses BL_JSON_PAIR_KEY macro to handle the difference between
     * json-spirit (.first) and Boost.JSON (.key())
     */

    namespace str
    {
        inline std::string joinQuoteFormattedKeys(
            SAA_in      const json::object&                         map,
            SAA_in_opt  const std::string&                          separator       = detail::StringUtils::g_defaultSeparator,
            SAA_in_opt  const std::string&                          lastSeparator   = detail::StringUtils::g_defaultLastSeparator
            )
        {
            std::vector< std::string > keys;
            keys.reserve( map.size() );

            for( const auto& pair : map )
            {
                keys.emplace_back( std::string( BL_JSON_PAIR_KEY( pair ) ) );
            }

            return detail::StringUtils::joinFormatted< std::string >( keys, separator, lastSeparator, &quoteFormatter< std::string > );
        }

    } // str

} // bl

#endif /* __BL_JSONUTILS_H_ */
