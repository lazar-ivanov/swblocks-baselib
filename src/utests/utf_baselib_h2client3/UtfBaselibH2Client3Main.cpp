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

#define UTF_TEST_MODULE utf_baselib_h2client3
#include <utests/baselib/UtfMain.h>

/*
 * THE HTTP/2 CLIENT OVER TLS - and, from here on, THE MODULE A TLS-SIDE SLICE GOES IN
 *
 * The layout of this feature's client modules, so that nobody has to re-derive it:
 *
 *   utf_baselib_h2client    S3.2, S3.3, S4.1 - the stranded stream policies and connection
 *                           establishment. CLOSED to new slices since S4.1 (37.1 MB clang debug).
 *   utf_baselib_h2client2   S4.2 cleartext - the driver's shell: the coalesced opening write, the
 *                           read loop, the write pump, the connection timers, GOAWAY and cancel.
 *   utf_baselib_h2client3   S4.2 TLS - THIS ONE. It is the module a TLS-side subject belongs in:
 *                           the ALPN outcomes, h2 over the hardened default context, and whatever
 *                           the TLS profiles of design 3.3 add later.
 *
 * BUT READ THE NEXT PARAGRAPH BEFORE ADDING TO IT. This module is 39.4 MB clang debug with two
 * cases in it, which is 0.6 MB short of the 40 MB target. That is not headroom. A TLS-side slice
 * belongs here by SUBJECT, and almost none will fit here by SIZE: anything which instantiates
 * another http2::Session, another peer or another stream policy should start a numbered sibling
 * and say in its own main that it is the TLS-side continuation of this one. The same goes for
 * S5.2's pool, its retry matrix and the stress suite of design 8.3, which are cleartext work and
 * fit in neither this module nor utf_baselib_h2client2 (37.0 MB). A numbered sibling needs no
 * makefile change and src/utests/AGENTS.md has the checklist.
 *
 * WHY THIS MODULE EXISTS AT ALL, AND WHAT THE SPLIT ACTUALLY COST. utf_baselib_h2client2 carried
 * both halves of S4.2 and measured 46.8 MB clang debug, over the target. The seam is the stream
 * policy - four http2::Session instantiations in one translation unit, two peers and two drivers -
 * so that is where the cut went, and both modules are now under the target: 37.0 and 39.4.
 *
 * The leave-one-out figure which argued for the cut said the TLS half cost 9.8 MB, and standalone
 * it costs about 18. Both numbers are right and they measure different things: 9.8 was the
 * MARGINAL cost of adding TLS to a module which had already paid for everything the two halves
 * instantiate in common. `before - ( a + b )` is the figure src/utests/AGENTS.md says decides
 * splittability, and here it is 46.8 - 76.4 = **-29.6 MB** - the split bought two modules under
 * target at the price of paying the floor and the common instantiation weight twice. Worth knowing
 * before splitting either of them again.
 *
 * The helpers the two share live in utests/baselib/Http2DriverTestUtils.h and are templates
 * wherever they cost an instantiation, so each module pays only for the policies it names.
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock.
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

#include "TestHttp2ConnectionTaskTls.h"
