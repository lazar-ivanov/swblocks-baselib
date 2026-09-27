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

#define UTF_TEST_MODULE utf_baselib_httpclient8
#include <utests/baselib/UtfMain.h>

/*
 * The seventh numbered sibling of utf_baselib_httpclient - see notes/plans/http2-design.md 8.1
 * (D20) for what the base module covers, and src/utests/AGENTS.md for the numbering scheme
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS. Astra's second review, change-set CS-1
 * (notes/plans/issues/astra-second-review-decisions.md), adds HTTP/1.1 driver cases OVER TLS, and
 * the only TLS HTTP/1.1 peer in the suite lived in utf_baselib_httpclient5 - over the 40MB target
 * src/utests/AGENTS.md sets, and closed to new cases for that reason. utf_baselib_httpclient7, which
 * carries the cleartext driver seams, is near the target itself. So the cases come here, with a TLS
 * peer CS-1 wrote - now in utests/baselib/Http1DriverTlsTestUtils.h, shared with CS-1's D2 module
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP/1.1 driver over the TLS stranded policy, the TLS establisher,
 * the server-role TLS engine of its peer, and the request task that runs over them. What it does
 * NOT pay for is the session, the pool and the HTTP/2 driver - the request task is given its one
 * connection by a test pool, which is the whole of what makes a request's result observable here
 * without instantiating both drivers the way utf_baselib_httpclient5 has to
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

#include "TestHttp1DriverTlsTruncation.h"
