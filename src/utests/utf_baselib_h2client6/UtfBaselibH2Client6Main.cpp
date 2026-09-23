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

#define UTF_TEST_MODULE utf_baselib_h2client6
#include <utests/baselib/UtfMain.h>

/*
 * The HTTP/2 driver's TEARDOWN cases - the numbered sibling created by the initiateClose( ) fix
 *
 * WHY THERE IS A SIXTH MODULE. utf_baselib_h2client2 is the cleartext driver module and is where
 * a case like this would naturally go, but it measured 38.1 MB a64 clang debug with its fourteen
 * cases - at the 40 MB target of src/utests/AGENTS.md, which is where utf_baselib_h2client itself
 * was split (37.1 MB) and where utf_baselib_h2client3 was split out of h2client2 (46.8 MB). A
 * numbered sibling needs no makefile change and is the intended answer rather than a last resort
 *
 * AND WHY IT IS A SEPARATE ONE RATHER THAN A CASE IN h2client2 ANYWAY. Its peer is not
 * Http2TestServerT and cannot be: every case in h2client2 speaks HTTP/2 to a peer that reads its
 * socket continuously, and a peer which reads is a second waker for the very write these cases
 * hold open. The peer here is a raw socket which accepts and then refuses to read, which is a
 * different kind of fixture and belongs beside the cases that need it. The teardown cases the
 * owed list still carries - h2's own peer-close arm, and the composed TLS read that can slip a
 * cancel the same way a composed write does - want the same fixture and go here too
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock.
 * The helpers the driver modules share are utests/baselib/Http2DriverTestUtils.h
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

#include "TestHttp2DriverWriteBarrier.h"
