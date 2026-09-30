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

#ifndef __BL_HTTPCLIENT_BROWSERPROFILE_H_
#define __BL_HTTPCLIENT_BROWSERPROFILE_H_

#include <baselib/httpclient/HeaderProfile.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/crypto/TlsClientProfile.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /*
         * The browser profile as a type - notes/plans/http2-design.md 6.2, and 5.8 for the API
         *
         * It has a header of its own, apart from the loader which produces it
         * (httpclient/BrowserProfiles.h), so that a caller which only takes a BrowserProfile or
         * hands one on - the session's profile( BrowserProfile ) among them - names it without the
         * loader's Boost.JSON, its data model, and the OpenSSL headers which the data model brings
         * with it. This header includes the three typed profile headers and nothing else of the
         * library, so it is free of OpenSSL and of JSON alike
         */

        /**
         * @brief How closely a profile's TLS layer can match its browser (design 6.1)
         *
         * Approximate is first, so that a profile nobody filled claims the weaker grade
         */

        enum class BrowserProfileGrade : std::uint8_t
        {
            /**
             * The ClientHello cannot match the browser's JA4, for stated reasons
             */

            Approximate,

            /**
             * The ClientHello is expected to match the browser's JA4, subject to the spike
             */

            Ja4Candidate,
        };

        /**
         * @brief One browser profile, loaded and validated - see BrowserProfilesT::load( ) in
         * httpclient/BrowserProfiles.h
         *
         * The version strings are not fields: the loader has composed them into the header
         * lists, which is the only place they are sent from
         */

        struct BrowserProfile
        {
            std::string                                                         id;
            std::string                                                         family;
            cpp::ScalarTypeIniter< BrowserProfileGrade >                        grade;
            std::vector< std::string >                                          deviations;

            bl::crypto::TlsClientProfile                                        tls;
            bl::http2::Http2Profile                                             http2;
            bl::httpclient::HeaderProfile                                       headers;
        };

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_BROWSERPROFILE_H_ */
