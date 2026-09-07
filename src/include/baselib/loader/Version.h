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

#ifndef __BL_LOADER_VERSION_H_
#define __BL_LOADER_VERSION_H_

#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace loader
    {
        /**
         * @brief class Version
         */
        template
        <
            typename E = void
        >
        class VersionT
        {
        public:

            static std::string toString(
                SAA_in      const std::size_t           majorVersion,
                SAA_in      const std::size_t           minorVersion,
                SAA_in      const std::size_t           patchVersion
                )
            {
                cpp::SafeOutputStringStream version;

                version
                    << majorVersion
                    << "."
                    << minorVersion
                    << "."
                    << patchVersion;

                return version.str();
            }

            static void fromString(
                SAA_in      const std::string&          versionStr,
                SAA_out     std::size_t&                majorVersion,
                SAA_out     std::size_t&                minorVersion,
                SAA_out     std::size_t&                patchVersion
                )
            {
                /*
                 * Note that sscanf can't be used here - "%hu" accepts a sign (which wraps),
                 * it silently truncates a value which does not fit an unsigned short and it
                 * ignores anything which follows the version
                 */

                std::vector< std::string > parts;

                str::split( parts, versionStr, str::is_equal_to( '.' ) );

                const auto chkPart = [ &versionStr ]( SAA_in const std::string& part ) -> std::size_t
                {
                    BL_CHK_T(
                        false,
                        ! part.empty() && part.size() <= 5U &&
                            std::all_of(
                                part.begin(),
                                part.end(),
                                []( SAA_in const char ch ) -> bool
                                {
                                    return ch >= '0' && ch <= '9';
                                }
                                ),
                        ArgumentException(),
                        BL_MSG()
                            << "Invalid version number '"
                            << versionStr
                            << "', expected format <major>.<minor>.<patch>"
                        );

                    return utils::lexical_cast< std::size_t >( part );
                };

                BL_CHK_T(
                    false,
                    3U == parts.size(),
                    ArgumentException(),
                    BL_MSG()
                        << "Invalid version number '"
                        << versionStr
                        << "', expected format <major>.<minor>.<patch>"
                    );

                majorVersion = chkPart( parts[ 0 ] );
                minorVersion = chkPart( parts[ 1 ] );
                patchVersion = chkPart( parts[ 2 ] );
            }
        };

        typedef VersionT<> Version;

    } // loader

} // bl

#endif /* __BL_LOADER_VERSION_H_ */
