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

#define UTF_TEST_MODULE utf_baselib_httpclient10
#include <utests/baselib/UtfMain.h>

/*
 * The SESSION OVER TLS, continued - the ninth numbered sibling of utf_baselib_httpclient, created by
 * CS-2 of astra's second review ( notes/plans/issues/astra-second-review-decisions.md )
 *
 * WHY THIS MODULE EXISTS. utf_baselib_httpclient5 is the TLS session's module and is over the 40 MB
 * target src/utests/AGENTS.md sets ( its own Main.cpp records 46.9 MB, a64 clang debug ), so the TLS
 * session cases CS-2 owes come here instead: the ALPN fallback exchange with a sink ( E3 ) and the
 * narrowing of a streaming upload's ALPN offer, shown to discriminate ( E1 ). The cleartext session's
 * CS-2 cases are utf_baselib_httpclient9's.
 *
 * WHAT IT PAYS FOR is what utf_baselib_httpclient5 pays for, and for the same reason: a session over
 * the TLS policy instantiates BOTH drivers over it - the HTTP/2 one it builds and the HTTP/1.1 one
 * its factory registers for the fallback - and its peers bring the server side. The cases
 * themselves cost almost nothing next to those instantiations.
 *
 * SIZE, MEASURED AND OVER TARGET, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 42.8 MB
 * clang debug (a64) with the E3 case alone, against a 40 MB target and a 75 MB debug ceiling which
 * only win-x86-*-debug enforces. That is the TLS session and nothing else - 21.8 MB over the empty
 * module floor - so no split of the cases could bring it under the target.
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

#include "TestClientSessionTlsSinkFallback.h"
