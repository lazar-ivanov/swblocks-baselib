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

#define UTF_TEST_MODULE utf_baselib_httpclient6
#include <utests/baselib/UtfMain.h>

/*
 * The SESSION, continued - the fifth numbered sibling of utf_baselib_httpclient
 *
 * WHY THIS MODULE EXISTS, AND IT IS THE SIZE POLICY AND NOTHING ELSE. A session-level case has two
 * homes and both are shut: utf_baselib_httpclient4 is 48.0 MB clang debug and utf_baselib_
 * httpclient5 is 46.0, against a 40 MB target, and src/utests/AGENTS.md says not to add to a
 * module already at or over it. utf_baselib_httpclient3, which owns the HTTP/1.1 driver's own
 * cases, is 39.7 and declared closed. So the answer the same file calls the intended one applies:
 * a numbered sibling, which needs no makefile change
 *
 * WHAT GOES IN IT. Cases about a session. It does not instantiate the library's own HttpServer -
 * httpclient4 measured that at about 5.8 MB - and pays the ~21 MB TU floor plus the session with
 * both of its drivers. That is the seam httpclient4's own note says the cut would go along
 *
 * MEASURED, clang debug (a64): 39.0 MB with the idle case alone, 42.4 MB once H22's two cancel
 * cases brought in the in-process peer of design 8.2. So this module is 2.4 OVER the 40 MB target
 * and the recorded reason is that splitting would not help: the weight IS the session plus the
 * peer, so a sibling holding only those two cases would measure about the same and the ~21 MB
 * floor would be paid a second time for no change in the peak. The 75 MB ceiling is far off
 *
 * THAT PEER COST 3.3 MB HERE AND NOT THE 8.6 httpclient4 MEASURED, because this module already
 * included Http2TestServer.h for its recorder and the session already instantiates most of what
 * the peer needs. The note this replaces predicted from httpclient4's figure that such a case
 * "would not fit", in the same breath as saying not to convert from another module's figure.
 * Measure the grouping; the isolated number is not the marginal one
 *
 * The module is devenv7+ only: the devenv7_only marker next to this file is what keeps it out of
 * the build on devenv2-6 (projects/make/common.mk). Headers never test BL_DEVENV_VERSION; they
 * guard on the capability they need - BOOST_VERSION, OPENSSL_VERSION_NUMBER - with a clear #error
 *
 * APPEND CONVENTION - read this before adding to this file
 * (notes/plans/http2-implementation-plan.md section 0)
 *
 *   A slice adds its own Test<Feature>.h in THIS directory and appends EXACTLY ONE #include line
 *   at the END of the include block below. It does not reorder, edit or remove another slice's
 *   line, and it does not include a test header from another module's directory
 *   (src/utests/AGENTS.md). Different slices then append different lines, so two lanes landing at
 *   once is a trivial merge rather than a conflict
 *
 *   Sibling files a slice may also add here: a notes.txt line carrying its --run_test= recipe,
 *   and a data/ file if it needs one - data/ is never shared between modules
 */

#include <baselib/http2/PreCompiled.h>
#include <baselib/httpclient/PreCompiled.h>

/*
 * Test headers - one appended line per slice, at the end
 */

#include "TestClientSessionIdle.h"
#include "TestClientSessionCancel.h"
