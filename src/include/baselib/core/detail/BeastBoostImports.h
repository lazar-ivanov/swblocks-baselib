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

#ifndef __BL_BEASTBOOSTIMPORTS_H_
#define __BL_BEASTBOOSTIMPORTS_H_

/*
 * The one and only file which includes Boost.Beast
 *
 * notes/plans/http2-design.md 5.5 (D15) makes this the first of three isolation layers, in the
 * form this library already uses for its other Boost interfaces. The rule which follows from it,
 * and which a grep can check: no boost::beast name appears outside this file and the compatibility
 * shim beside it, and no bl::beast name appears outside the HTTP/1.1 codec backend
 *
 * Three properties of this file are load bearing:
 *
 *   - it includes the NARROW Beast headers, never the boost/beast.hpp umbrella, so what is
 *     compiled is what is used
 *   - it is not reachable from core/BaseIncludes.h or from any PreCompiled.h, exactly as
 *     core/AsioSSL.h is not. Only the codec backend includes it, so nothing else in the library
 *     and no test module pays for Beast
 *   - it brings in the names one at a time, so the surface this library depends on can be read off
 *     this one file rather than inferred from the code which uses it
 *
 * Everything imported below is the surface of http::basic_parser used sans-I/O, and nothing else:
 * Beast's fields, message, body types, serializer and stream algorithms are deliberately not used
 * (5.5). Adding a name is one line here; it must never become an include somewhere else
 *
 * No Boost version guard. A guard states a capability some supported configuration lacks, and
 * there is none here: Beast has been part of Boost since 1.66 and the oldest Boost any supported
 * devenv uses is 1.72 (projects/make/platform.mk), so the condition could not fire. The presence
 * of the headers was probed on the devenv7 dist only (Boost 1.90.0, all four build variants) -
 * that probe is what decision D15 rests on and it is recorded with the slice
 */

#include <baselib/core/detail/BoostIncludeGuardPush.h>
#include <boost/optional.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/string_type.hpp>
#include <boost/beast/http/basic_parser.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <baselib/core/detail/BoostIncludeGuardPop.h>

namespace bl
{
    namespace beast
    {
        /*
         * The string type every basic_parser callback delivers its text in - the status line's
         * reason phrase, each field name and value, body and chunk bytes, and the chunk extensions
         *
         * It is a view into the parser's own buffer and does not own what it points at, so a codec
         * which keeps any of it has to copy it into the library's own storage first
         */

        using boost::beast::string_view;

        /*
         * Not a Beast name, and imported here for one reason: basic_parser::on_body_init_impl
         * takes 'boost::optional< std::uint64_t > const&', so a codec deriving from the parser has
         * to name this type in an override. It is part of the parser's interface surface whether
         * or not it belongs to Beast, and the point of this file is that the whole of that surface
         * can be read off it
         *
         * This is emphatically not a general-purpose optional for the library. If one is ever
         * wanted, it belongs in a generic import header and not in a namespace named after Beast
         */

        using boost::optional;

        /*
         * beast::error_code is deliberately NOT imported. It is an alias of
         * boost::system::error_code, which this library already names eh::error_code
         * (core/ErrorHandling.h), and a second name for one type is exactly what an isolation
         * header should not create. A codec's overrides spell that parameter eh::error_code
         */

        namespace http
        {
            /*
             * The sans-I/O parser core, and the whole of what this library takes from Beast
             *
             * It is a CRTP-free abstract base: a codec derives from basic_parser< false > for
             * responses and implements every virtual. Worth knowing before writing that codec -
             * the virtuals are declared unconditionally on the primary template, so a
             * response-only parser must still override on_request_impl, which is why 'verb' below
             * is part of the surface despite this library serializing its own requests by hand
             */

            using boost::beast::http::basic_parser;

            /*
             * The known-field enumeration a callback receives alongside the field's exact name as
             * it arrived. This library builds its own http::HeaderList from the exact name, so the
             * enumeration is used for recognition only - never to render a name back out, which is
             * why to_string and string_to_field are not imported
             */

            using boost::beast::http::field;

            /*
             * The request-method enumeration. Present because on_request_impl's signature names
             * it, not because anything here parses a request
             */

            using boost::beast::http::verb;

            /*
             * The parser's own error enumeration - what an eh::error_code set by the parser is
             * compared against, need_more above all. The matching make_error_code is found by
             * argument-dependent lookup on the enumeration's own namespace, so it needs no import
             */

            using boost::beast::http::error;

        } // http

    } // beast

} // bl

#endif /* __BL_BEASTBOOSTIMPORTS_H_ */
