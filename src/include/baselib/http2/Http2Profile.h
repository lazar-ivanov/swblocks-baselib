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

#ifndef __BL_HTTP2_HTTP2PROFILE_H_
#define __BL_HTTP2_HTTP2PROFILE_H_

#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <vector>

namespace bl
{
    namespace http2
    {
        /*
         * The shape of what an HTTP/2 client puts on the wire before and with its first request -
         * the "HTTP/2 layer" of a browser profile (notes/plans/http2-design.md 6.4)
         *
         * This is pure data and depends on nothing. In particular it does not include
         * http2/Globals.h: a setting id here is whatever the profile says it is, including ids
         * this library does not itself interpret, so the profile must not be limited to the
         * enumeration the engine knows about
         *
         * Content - the actual settings, increments and orders of a given browser - is captured
         * ground truth and belongs to the profile data, not to this header (6.7)
         */

        /**
         * @brief One SETTINGS entry, in the order the profile sends it
         *
         * The id is held as its wire type rather than an enumeration precisely so that a profile
         * can carry ids the engine passes through without understanding
         */

        struct Http2Setting
        {
            cpp::ScalarTypeIniter< std::uint16_t >                              id;
            cpp::ScalarTypeIniter< std::uint32_t >                              value;
        };

        /**
         * @brief A PRIORITY frame sent on an idle stream after SETTINGS
         */

        struct Http2PriorityFrame
        {
            cpp::ScalarTypeIniter< std::uint32_t >                              streamId;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamDependency;
            cpp::ScalarTypeIniter< std::uint8_t >                               weight;
            cpp::ScalarTypeIniter< bool >                                       exclusive;
        };

        /**
         * @brief The optional priority fields carried on a HEADERS frame
         *
         * 'isSet' is what distinguishes "no priority on HEADERS" from "priority with every field
         * zero", which are different frames on the wire
         */

        struct Http2HeadersPriority
        {
            cpp::ScalarTypeIniter< bool >                                       isSet;
            cpp::ScalarTypeIniter< std::uint32_t >                              streamDependency;
            cpp::ScalarTypeIniter< std::uint8_t >                               weight;
            cpp::ScalarTypeIniter< bool >                                       exclusive;
        };

        /**
         * @brief The request pseudo-headers of RFC 9113 section 8.3.1, as the fingerprint names them
         *
         * A profile carries the order it emits them in - the 'm,a,s,p' component of the
         * conventional HTTP/2 fingerprint string
         */

        enum class Http2PseudoHeader : std::uint8_t
        {
            Method,
            Authority,
            Scheme,
            Path,
        };

        /**
         * @brief How the HPACK encoder represents a header field it is not required to send literally
         *
         * The three representations of RFC 7541 section 6.2. This is a policy, not a per-header
         * decision: a field whose value must never be indexed is the encoder's own business
         */

        enum class HpackIndexingPolicy : std::uint8_t
        {
            Incremental,
            WithoutIndexing,
            NeverIndexed,
        };

        /**
         * @brief The shaping knobs of an HTTP/2 connection
         */

        struct Http2Profile
        {
            /*
             * Ordered - the order is part of the fingerprint, so this is a vector and not a map
             */

            std::vector< Http2Setting >                                         settings;

            /*
             * The connection-level WINDOW_UPDATE sent immediately after SETTINGS, and the
             * threshold at which a further one is sent for a stream or the connection
             */

            cpp::ScalarTypeIniter< std::uint32_t >                              connectionWindowUpdateIncrement;
            cpp::ScalarTypeIniter< std::uint32_t >                              windowUpdateThreshold;

            std::vector< Http2PriorityFrame >                                   idleStreamPriorities;
            Http2HeadersPriority                                                headersPriority;

            std::vector< Http2PseudoHeader >                                    pseudoHeaderOrder;

            cpp::ScalarTypeIniter< std::uint32_t >                              hpackEncoderTableSize;
            cpp::ScalarTypeIniter< HpackIndexingPolicy >                        hpackIndexingPolicy;

            /*
             * Whether a cookie header is split into one field per crumb (RFC 9113 section 8.2.3)
             */

            cpp::ScalarTypeIniter< bool >                                       cookieCrumbling;

            /*
             * NOT here, deliberately: the RFC 9218 'priority' header value by request kind, which
             * the design lists under this struct (6.4). It is keyed by the request kind, whose
             * enumeration lives in httpclient/ per the S1.4 work order, and the dependency
             * direction is one way - httpclient/ depends on http2/, never the reverse (2.2). It
             * is also, literally, a header value, so it lives with the other per-kind header data
             * in httpclient::HeaderProfile
             */
        };

    } // http2

} // bl

#endif /* __BL_HTTP2_HTTP2PROFILE_H_ */
