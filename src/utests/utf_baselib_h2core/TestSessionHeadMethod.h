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

#ifndef __UTEST_TESTSESSIONHEADMETHOD_H_
#define __UTEST_TESTSESSIONHEADMETHOD_H_

#include <baselib/http2/Session.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * THE BYTE-SCRIPT HELPERS THIS CASE USES LIVE IN THE SESSION HEADER, and are included rather than
 * relied on to have been included first - the module's append convention puts this line at the
 * end of Main.cpp, but a header which needs another one should say so
 */

#include "TestSession.h"

/**
 * @brief D8 (R07) - a request is HEAD only when its method is exactly "HEAD"
 *
 * THE DEFECT. isHeadMethod( ) folded case, so a request whose method is 'head' set expectsNoContent
 * - and since H17 a response which may not carry content and sends some is refused as malformed.
 * So the explanation a 501 sends back for 'head' was reset instead of delivered. RFC 9110 9.1: "The
 * method token is case-sensitive". 'head' is a method, only not HEAD; the HTTP/1.1 driver and the
 * pool already compare "HEAD" exactly.
 *
 * THE RULE, decided 2026-09-27: exact "HEAD". Every script is the same 501 with a declared body, so
 * the method is the only thing that varies: HEAD's response still may not carry content (9.3.2),
 * which is the control that the fix did not simply switch H17's rule off; GET is the control that
 * the script itself delivers; and 'head' and 'HeAd' are red before the fix and green after.
 */

UTF_AUTO_TEST_CASE( Session_HeadIsMatchedExactlyTests )
{
    using namespace bl;
    using namespace bl::http2;
    using namespace utest::session;

    const auto now = baseTime();

    /*
     * Every event of a stream in one line, so that a failing check prints the whole sequence; the
     * reason a stream was closed for goes to the log beside it
     */

    const auto describe = []( SAA_in const std::vector< SessionEvent >& events ) -> std::string
    {
        std::string result;

        for( std::size_t i = 0U; i < events.size(); ++i )
        {
            const auto& event = events[ i ];

            switch( event.type.value() )
            {
                case SessionEventType::Headers:
                    result += "headers " + std::to_string( event.status.value() );
                    break;

                case SessionEventType::Data:
                    result += "data '" + event.data + "'";
                    break;

                case SessionEventType::StreamClosed:
                    result += "closed " + std::to_string( event.errorCode.value() );

                    if( event.isMessageComplete.value() )
                    {
                        result += " complete";
                    }
                    break;

                default:
                    result += "other";
                    break;
            }

            if( event.endStream.value() )
            {
                result += " end";
            }

            result += "; ";

            if( ! event.reason.empty() )
            {
                UTF_MESSAGE( "the stream was closed because " + event.reason );
            }
        }

        return result;
    };

    struct Script
    {
        const char*     method;
        const char*     events;
    };

    static const Script scripts[] =
    {
        { "HEAD",       "headers 501; closed 1; "                                           },
        { "GET",        "headers 501; data 'not implemented' end; closed 0 complete; "      },
        { "head",       "headers 501; data 'not implemented' end; closed 0 complete; "      },
        { "HeAd",       "headers 501; data 'not implemented' end; closed 0 complete; "      },
    };

    for( std::size_t i = 0U; i < sizeof( scripts ) / sizeof( scripts[ 0 ] ); ++i )
    {
        const auto& script = scripts[ i ];

        UTF_MESSAGE( std::string( "method '" ) + script.method + "' answered by a 501 with a body" );

        Session session( StreamRole::Client, now );
        PeerEncoder peer;

        settle( session, now );

        const auto streamId = session.submitRequest( makeRequest( false /* hasBody */, script.method ) );

        ( void ) produceText( session, now );

        HpackFieldList extra;

        extra.push_back( field( "content-length", "15" ) );

        feedText( session, headersFrame( streamId, peer.response( "501", extra ), false, true ), now );
        feedText( session, dataFrame( streamId, "not implemented", true /* endStream */ ), now );

        /*
         * Either way it is a stream's business and never the connection's
         */

        UTF_CHECK( ! session.isClosed() );

        UTF_CHECK_EQUAL( describe( drain( session ) ), std::string( script.events ) );
    }
}

#endif /* __UTEST_TESTSESSIONHEADMETHOD_H_ */
