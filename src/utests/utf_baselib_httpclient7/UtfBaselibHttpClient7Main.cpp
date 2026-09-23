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

#define UTF_TEST_MODULE utf_baselib_httpclient7
#include <utests/baselib/UtfMain.h>

/*
 * The sixth numbered sibling of utf_baselib_httpclient - see notes/plans/http2-design.md 8.1
 * (D20) for what the base module covers, and src/utests/AGENTS.md for the numbering scheme
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS. utf_baselib_httpclient3, which carries the S4.3 HTTP/1.1 driver cases,
 * measured 40.3MB on the a64 clang debug object after S6R.1 - already past the 40MB target
 * src/utests/AGENTS.md sets, and its own file note says it is closed to new slices. S6R.2 adds
 * HTTP/1.1 driver cases, so they come here instead. The harness they share with that module -
 * ScriptedPeer, RecordingSink, the establisher and the driver typedefs - moved to
 * utests/baselib/Http1DriverTestUtils.h when this module was created, because a test header may
 * never be included across module directories and copying the block into both is worse
 *
 * WHAT THIS MODULE DELIBERATELY DOES NOT PAY FOR. utests/baselib/HttpServerHelpers.h and the
 * library's own HTTP server behind it, which utf_baselib_httpclient3 measured at 5.8MB of its
 * object, and the explicit instantiation of the driver over the TLS stranded policy. Both stay in
 * utf_baselib_httpclient3 with the cases that need them
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

#include <baselib/httpclient/PreCompiled.h>

/*
 * Test headers - one appended line per slice, at the end
 */

#include "TestHttp1DriverWriteBarrier.h"
