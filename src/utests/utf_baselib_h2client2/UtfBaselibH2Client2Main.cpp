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

#define UTF_TEST_MODULE utf_baselib_h2client2
#include <utests/baselib/UtfMain.h>

/*
 * The HTTP/2 DRIVER over CLEARTEXT - the numbered sibling of utf_baselib_h2client, created by S4.2
 *
 * WHY THERE IS A SECOND MODULE. utf_baselib_h2client measured 37.1 MB clang debug once S4.1 landed
 * in it, which leaves no room for the driver, its timers, the in-process test peer (about +8.6 MB
 * debug on its own) and a stress suite. A numbered sibling needs no makefile change;
 * src/utests/AGENTS.md has the checklist
 *
 * AND WHY THERE IS A THIRD. This module first carried BOTH halves of S4.2 and measured 46.8 MB
 * clang debug, over the 40 MB target. A leave-one-out measurement put the TLS half at 9.8 MB of
 * that, and the seam is exactly the stream policy - four http2::Session instantiations in one
 * translation unit, two peers and two drivers. The TLS half is utf_baselib_h2client3, which is
 * also where a TLS-side slice goes from here on; its own main says so and says where S5.2's pool
 * and the stress suite of design 8.3 should go instead of here
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock. The
 * peer is utests/baselib/Http2TestServer.h (S4.4), which is an independent USE of the same
 * protocol core rather than an independent implementation of it - design 8.4 says how that gap is
 * closed. The helpers both driver modules share are utests/baselib/Http2DriverTestUtils.h
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

#include "TestHttp2ConnectionTask.h"
