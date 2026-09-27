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

#define UTF_TEST_MODULE utf_baselib_httpclient9
#include <utests/baselib/UtfMain.h>

/*
 * The CLEARTEXT SESSION, continued - the eighth numbered sibling of utf_baselib_httpclient, created
 * by CS-2 of astra's second review ( notes/plans/issues/astra-second-review-decisions.md )
 *
 * WHY THIS MODULE EXISTS. utf_baselib_httpclient4 is the cleartext session's module and is over the
 * 40 MB target src/utests/AGENTS.md sets, so the cleartext session cases CS-2 owes come here
 * instead: what the session decodes and what it hands back untouched ( D5 ), and the establishment
 * failure through a real pool and session ( E2 ). The TLS session's CS-2 cases are
 * utf_baselib_httpclient10's.
 *
 * ITS PEER IS THE SCRIPTED HTTP/1.1 ONE, utests/baselib/Http1DriverTestUtils.h's ScriptedPeer,
 * because every case here is a statement about bytes a server writes - header fields, a length, a
 * response cut short - and that peer writes them byte for byte. What the module pays for is the
 * cleartext session, which instantiates both drivers over the cleartext stranded policy.
 *
 * SIZE, MEASURED: 39.8 MB clang debug (a64) with D5's seven cases, E2's one and D3's two, against a
 * 40 MB target - which is the session's instantiations and very little else ( 18.8 MB over the empty
 * module floor; E2 and D3 added 0.1 MB each, since neither brings a type the module did not already
 * have ). It is AT the target, so a slice which adds here measures first, as src/utests/AGENTS.md
 * asks.
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock
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

#include "TestClientSessionDecoding.h"
#include "TestClientSessionEstablishmentFailure.h"
#include "TestClientSessionOutstandingCap.h"
