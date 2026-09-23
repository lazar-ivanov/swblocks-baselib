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

#define UTF_TEST_MODULE utf_baselib_h2client7
#include <utests/baselib/UtfMain.h>

/*
 * The HTTP/2 DRIVER's MULTI-OPERATION ACCOUNTING - A3 of the unscheduled driver arms
 *
 * WHY THERE IS A SEVENTH MODULE, and it is the size policy rather than a preference. The cases
 * here need a stream policy of their own - one seam, so that an initiator can be made to fail -
 * and a stream policy is exactly the seam on which a driver module splits: a distinct STREAM
 * instantiates Http2ConnectionTaskT, ClientConnectionTaskBase and http2::Session again. That was
 * measured at 9.8 MB clang debug when the TLS half left utf_baselib_h2client2, and every module
 * which already carries the driver (h2client2, h2client3, h2client6) is a module this would add
 * that weight to. A numbered sibling needs no makefile change; src/utests/AGENTS.md has the
 * checklist
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock. The
 * peer is utests/baselib/Http2TestServer.h (S4.4) and the shared helpers are
 * utests/baselib/Http2DriverTestUtils.h, exactly as in the sibling driver modules
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

#include "TestHttp2DriverAccounting.h"
