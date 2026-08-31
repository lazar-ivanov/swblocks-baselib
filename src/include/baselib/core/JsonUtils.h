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

        template
        <
            typename STREAM
        >
        inline void saveToStream(
            SAA_in          const json::value&                      val,
            SAA_inout       STREAM&                                 output,
            SAA_in_opt      const bool                              prettyPrint = false,
            SAA_in_opt      const bool                              rawUtf8 = false
            )
        {
            detail::JsonUtilsImpl::saveToStream( val, output, prettyPrint, rawUtf8 );
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
