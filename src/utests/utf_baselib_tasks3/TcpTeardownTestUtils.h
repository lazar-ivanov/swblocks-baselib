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

#ifndef __UTEST_TCPTEARDOWNTESTUTILS_H_
#define __UTEST_TCPTEARDOWNTESTUTILS_H_

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/Utf.h>

/*
 * The helpers this module's teardown cases share - kept in one place, because a helper copied into
 * two headers of one module is what src/utests/AGENTS.md forbids. The TLS peer they end their
 * streams against is utests/baselib/TlsEndingPeer.h, shared with utf_baselib_httpclient13
 */

namespace utest
{
    namespace tcpteardown
    {
        using tlsendingpeer::WAIT_IN_MILLISECONDS;
        using tlsendingpeer::describeCode;
        using tlsendingpeer::TlsEndingPeer;


        /**
         * @brief Fails with a diagnosis, and otherwise counts the assertion - so that a passing case
         * says how many it checked, which is what tier 3 compares
         */

        inline void chkOrFail(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( ! condition )
            {
                UTF_FAIL( message );
            }

            UTF_REQUIRE( condition );
        }

        inline auto joinRecords( SAA_in const std::vector< std::string >& records ) -> std::string
        {
            std::string result;

            for( const auto& record : records )
            {
                if( ! result.empty() )
                {
                    result += " | ";
                }

                result += record;
            }

            return result;
        }

    } // tcpteardown

} // utest

#endif /* __UTEST_TCPTEARDOWNTESTUTILS_H_ */
