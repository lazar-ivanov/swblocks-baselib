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

#ifndef __UTEST_TESTH2CLIENTPLACEHOLDER_H_
#define __UTEST_TESTH2CLIENTPLACEHOLDER_H_

#include <baselib/http2/Globals.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/*
 * A deliberate placeholder, and the only case in this module until S4.1
 *
 * S1.8's work order scaffolded the four new test modules empty, and this file is the one
 * deviation from it. A Boost.Test binary with no case at all exits 200, so without this file
 * `make test_utf_baselib_h2client`, `make testutf` and every whole-suite run are red from L1
 * until L4 - nothing else lands in this module before then (implementation plan section 3, the
 * note under L1). A suite which is permanently red teaches people to ignore red, and that costs
 * more than a placeholder does
 *
 * S4.1 (connection establishment + ALPN dispatch) brings this module its first real case and
 * replaces this one. Delete this file then, together with its #include line in
 * UtfBaselibH2ClientMain.cpp and its recipe in notes.txt, in the same commit. Nothing here is a
 * fixture or a helper, and nothing may be built on it
 *
 * What it asserts is the smallest true thing still worth asserting: that the module's umbrella
 * really does reach the shared HTTP/2 constants in this translation unit. A case which asserts
 * nothing is reported differently by the harness, and is worse than one which checks a triviality
 */

UTF_AUTO_TEST_CASE( H2Client_PlaceholderUntilS41Tests )
{
    using namespace bl;

    UTF_MESSAGE(
        "utf_baselib_h2client has no real case until S4.1; this placeholder only keeps the suite green"
        );

    /*
     * RFC 9113 section 4.1 - the fixed nine-octet frame header
     */

    UTF_REQUIRE_EQUAL( http2::Globals::FRAME_HEADER_SIZE, 9U );

    /*
     * RFC 9113 section 3.4 - the 24-octet client connection preface. Unlike the enumeration
     * above this one also proves the BL_DEFINE_STATIC_CONST_STRING definition linked
     */

    UTF_REQUIRE_EQUAL( http2::Globals::g_connectionPreface.size(), 24U );
}

#endif /* __UTEST_TESTH2CLIENTPLACEHOLDER_H_ */
