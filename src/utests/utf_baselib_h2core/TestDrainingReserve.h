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

#ifndef __UTEST_TESTDRAININGRESERVE_H_
#define __UTEST_TESTDRAININGRESERVE_H_

#include <baselib/http2/Session.h>

#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/************************************************************************
 * S5.2 - the draining reserve, from the pool's policy to the stream registry
 *
 * THE MARGIN ITSELF IS CHOSEN ELSEWHERE. Design 4.3 says a connection "approaching 2^31-1" is
 * marked draining and deliberately does not say how close; how much room is needed is a function
 * of what the pool has already committed to a connection, so the number is
 * httpclient::ConnectionPoolPolicy::DEFAULT_DRAINING_RESERVE and the argument for it is there.
 * What is missing without this case is the PATH: the registry's setDrainingReserve( ) is public
 * but the registry is private inside a session, so until SessionLimits carried the value nothing
 * above the engine could set the margin at all and design 4.3 described a behaviour no
 * configuration could reach.
 *
 * The registry's own boundary is already pinned by S2.4 in TestStreamStates.h. What is pinned here
 * is that a limit set from outside arrives, and that a session which is over the margin refuses
 * to open another stream rather than merely reporting something
 */

UTF_AUTO_TEST_CASE( H2Session_DrainingReserveFromLimitsTests )
{
    using namespace bl;
    using namespace bl::http2;

    const auto now = time::microsec_clock::universal_time();

    const auto makeRequest = []() -> SessionRequest
    {
        SessionRequest request;

        request.method = "GET";
        request.scheme = "https";
        request.authority = "example.com";
        request.path = "/";

        return request;
    };

    /*
     * The default is NONE - a session says nothing until the identifier space is spent, which is
     * what the registry does when nobody sets a reserve. That is the behaviour every existing
     * case was written against and it must not move
     */

    {
        const SessionLimits limits;

        UTF_REQUIRE_EQUAL( limits.drainingReserve, 0U );

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        UTF_REQUIRE( ! session.isDraining() );
        UTF_REQUIRE_EQUAL( session.submitRequest( makeRequest() ), 1U );
    }

    /*
     * A reserve of everything that is left puts the session where a connection which has spent
     * its identifiers is, without opening a billion streams to get there - and the consequence is
     * a REFUSAL rather than a flag: the pool is told through isDraining( ) and the engine will not
     * open another stream whatever the pool does with it
     */

    {
        SessionLimits limits;

        limits.drainingReserve = 1073741824U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        UTF_REQUIRE( session.isDraining() );
        UTF_REQUIRE_THROW( session.submitRequest( makeRequest() ), UnexpectedException );
    }

    /*
     * And the margin is a margin: one identifier short of the whole space leaves room for exactly
     * one more stream, which is the boundary the pool's own reserve sits on - it is sized so that
     * what the pool has already committed still has identifiers to be opened with
     */

    {
        SessionLimits limits;

        limits.drainingReserve = 1073741824U - 1U;

        Session session( StreamRole::Client, now, Http2Profile(), limits );

        UTF_REQUIRE( ! session.isDraining() );

        UTF_REQUIRE_EQUAL( session.submitRequest( makeRequest() ), 1U );

        UTF_REQUIRE( session.isDraining() );
        UTF_REQUIRE_THROW( session.submitRequest( makeRequest() ), UnexpectedException );
    }
}

#endif /* __UTEST_TESTDRAININGRESERVE_H_ */
