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

#define UTF_TEST_MODULE utf_baselib_httpclient2
#include <utests/baselib/UtfMain.h>

/*
 * The numbered sibling of utf_baselib_httpclient - see notes/plans/http2-design.md 8.1 (D20) for
 * what the base module covers, and src/utests/AGENTS.md for the numbering scheme
 *
 * Sockets: loopback
 *
 * WHY THIS MODULE EXISTS. The tunnel cases of slice S3.5 instantiate the TCP connection
 * establisher and the execution queue, which the base module did not, and adding them to it
 * measured 25.5 -> 32.7MB on clang debug - 82% of the 40MB target, in a module which design 8.1
 * still has the session, the pool and the ALPN fallback landing in. Split, the two objects are
 * 25.5MB and 26.3MB, each with real headroom. Splitting before the session lands costs one
 * translation unit floor once; not splitting costs a four-way split later, which is what
 * utf_baselib_messaging had to do at 112.7MB
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

#include "TestTcpTunnelStage.h"
