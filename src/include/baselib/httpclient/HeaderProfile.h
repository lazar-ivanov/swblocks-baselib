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

#ifndef __BL_HTTPCLIENT_HEADERPROFILE_H_
#define __BL_HTTPCLIENT_HEADERPROFILE_H_

#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /*
         * The shape of the request headers a client sends - the "header layer" of a browser
         * profile (notes/plans/http2-design.md 6.5)
         *
         * Pure data, and free of any HTTP/2 type: the same profile describes a request sent over
         * HTTP/2 and over HTTP/1.1, which is why the case map below exists at all
         *
         * Content - the actual header names, values and orders of a given browser - is captured
         * ground truth and belongs to the profile data, not to this header (6.7)
         */

        /**
         * @brief What a browser is doing, which decides which headers it sends
         *
         * A browser sends a different header set for a top-level navigation, for a fetch/XHR and
         * for a subresource load, so every per-kind table below is keyed by this
         */

        enum class HttpRequestKind : std::uint8_t
        {
            Navigation,
            Fetch,
            Subresource,
        };

        /**
         * @brief One default header of a profile, in the order the profile sends it
         *
         * A header whose value the session must compute at request time - sec-fetch-site depends
         * on how the request origin relates to the document origin - carries 'isComputed', and
         * then 'value' is what the profile would send absent any other information rather than
         * the literal to put on the wire
         */

        struct ProfileHeader
        {
            std::string                                                         name;
            std::string                                                         value;
            cpp::ScalarTypeIniter< bool >                                       isComputed;
        };

        /**
         * @brief Where a caller's own headers go relative to the profile's default list
         *
         * Placement is part of the fingerprint: a browser does not append everything at the end,
         * so a caller header dropped in the wrong place is itself a signal
         */

        enum class CallerHeaderPlacement : std::uint8_t
        {
            /**
             * After every default header
             */

            Appended,

            /**
             * Before every default header
             */

            Prepended,

            /**
             * Immediately before the default header named by 'callerHeaderAnchor'; appended if
             * that header is not present for this request kind
             */

            BeforeAnchor,
        };

        /**
         * @brief Everything a profile says about the headers of one request kind
         */

        struct HeaderProfileForKind
        {
            std::vector< ProfileHeader >                                        defaultHeaders;

            cpp::ScalarTypeIniter< CallerHeaderPlacement >                      callerHeaderPlacement;
            std::string                                                         callerHeaderAnchor;

            /*
             * Lower-case header name to the casing this profile uses over HTTP/1.1. Under HTTP/2
             * every name is lower case (RFC 9113 section 8.2.1), so this table is consulted on
             * the HTTP/1.1 path only, where casing is observable
             */

            std::map< std::string, std::string >                                http1CaseMap;

            /*
             * The RFC 9218 'priority' header value for this request kind. The design lists it
             * under http2::Http2Profile (6.4); it lives here because it is keyed by the request
             * kind and http2/ may not depend on httpclient/ (2.2)
             */

            std::string                                                         priorityHeaderValue;
        };

        /**
         * @brief The shaping knobs of a client's request headers
         */

        struct HeaderProfile
        {
            std::map< HttpRequestKind, HeaderProfileForKind >                   byRequestKind;

            /*
             * The content codings the profile claims, in order. What is actually sent is this list
             * intersected with the registered decoders, so with none registered the header is
             * omitted and the deviation is reported (6.5). That intersection is the session's, not
             * the profile's
             */

            std::vector< std::string >                                          acceptEncoding;

            /*
             * The q-value ladder this profile renders accept-language with: the q applied to the
             * second and each subsequent language, highest first. The languages themselves are
             * caller-configurable and therefore not profile data (6.5)
             *
             * Held as the token the profile emits ("0.9"), not as a number, because the rendered
             * header has to match the browser byte for byte and formatting a double back into a
             * q-value is exactly where that would be lost
             */

            std::vector< std::string >                                          acceptLanguageQValues;
        };

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_HEADERPROFILE_H_ */
