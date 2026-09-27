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

#define UTF_TEST_MODULE utf_baselib_httpclient12
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_httpclient - see notes/plans/http2-design.md 8.1 (D20) for what
 * the base module covers, and src/utests/AGENTS.md for the numbering scheme. Reserved for change-set
 * CS-1 of astra's second review; utf_baselib_httpclient9 and 10 are CS-2's
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS. Decision D2 (notes/plans/issues/astra2-cs1-d2-startup-handler-design.md)
 * starts the HTTP/1.1 driver in one accounted strand handler, and its cases need a HOOK in the
 * driver's first read start - which only a test stream policy can give, and a new policy is a new
 * driver instantiation, once per transport. utf_baselib_httpclient8, which carries D1's TLS cases,
 * was already at about the 40MB x86 target with those alone, and utf_baselib_httpclient7, which
 * carries the cleartext seams, is near it too; so D2's cases went to utf_baselib_httpclient11. All
 * four measured 39.9MB a64 clang debug there, over the 36MB the orchestrator set for it, so the
 * cleartext pair stays in utf_baselib_httpclient11 and this module carries the TLS pair
 *
 * WHAT THIS MODULE PAYS FOR. The driver over the TLS hook policy of
 * utests/baselib/Http1DriverStartupHook.h, the TLS establisher, and the server-role TLS engine of
 * the peer in utests/baselib/Http1DriverTlsTestUtils.h. Not the request task, the session, the pool
 * or the HTTP/2 driver: the cases speak to the driver directly
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

#include "TestHttp1DriverStartupTls.h"
