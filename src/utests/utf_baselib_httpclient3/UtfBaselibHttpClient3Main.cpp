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

#define UTF_TEST_MODULE utf_baselib_httpclient3
#include <utests/baselib/UtfMain.h>

/*
 * The second numbered sibling of utf_baselib_httpclient - see notes/plans/http2-design.md 8.1
 * (D20) for what the base module covers, and src/utests/AGENTS.md for the numbering scheme
 *
 * Sockets: loopback, and the machine global test lock for the one case which runs against the
 * library's own HttpServer on the shared test port
 *
 * WHY THIS MODULE EXISTS. Measured, not predicted: the HTTP/1.1 driver cases of slice S4.3 took
 * utf_baselib_httpclient from 25.5MB to 44.5MB on clang debug - past the 40MB target on their own,
 * in a module which design 8.1 still has the session and the pool landing in. What they bring with
 * them is a whole layer the base module did not instantiate: the S4.1 connection establisher, the
 * driver over BOTH stranded stream policies, the execution queue, and utests/baselib's
 * HttpServerHelpers with the library's own HTTP server behind it. The base module is back to
 * 25.5MB with that out of it
 *
 * THIS MODULE IS CLOSED TO NEW SLICES. It is 39.7MB clang debug with S4.3 alone - under the 40MB
 * target and a long way under the 75MB ceiling, but not "comfortably under" it, which is the test
 * src/utests/AGENTS.md sets for adding to a module. The split of that 39.7 was measured rather
 * than guessed: 33.9MB without the HttpServer case, so utests/baselib/HttpServerHelpers.h and the
 * real peer behind it are 5.8MB of it and the driver layer is the rest. A slice which would add
 * here takes utf_baselib_httpclient4 instead - it needs no makefile change (projects/make/common.mk
 * globs src/utests/utf*), which is what makes the numbering scheme cheap enough to use early
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

#include "TestHttp1ConnectionTask.h"
