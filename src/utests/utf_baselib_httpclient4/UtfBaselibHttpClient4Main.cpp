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

#define UTF_TEST_MODULE utf_baselib_httpclient4
#include <utests/baselib/UtfMain.h>

/*
 * The SESSION - the third numbered sibling of utf_baselib_httpclient, created by S6.1
 *
 * WHY THIS MODULE EXISTS, and the previous sibling's own header says where to go: utf_baselib_
 * httpclient3 is 39.7MB clang debug and declared closed, and utf_baselib_httpclient was 33.0MB
 * with design 8.1 still expecting more in it. What the session brings is everything at once - the
 * connection pool, the HTTP/2 driver, the HTTP/1.1 driver, the in-process HTTP/2 peer of design
 * 8.2 and the library's own HttpServer - so it could not have gone in either
 *
 * WHAT THESE CASES ARE ABOUT, and it is the reason the session is worth a module rather than a
 * header. Every layer below this one was built against a stub of the layer beside it: the request
 * task against a probe pool and a probe connection, the pool against stub connections, the driver
 * against a recording sink. Stubs agreeing with a contract is not the same as components agreeing
 * with each other, and the L5 review found five defects which were exactly that difference. THIS
 * is the module in which the real pool, the real request task and a real driver are composed and
 * run against a real peer
 *
 * Sockets: loopback, ephemeral ports, and the machine global test lock for the cases which run
 * against the library's own HttpServer on the shared test port
 *
 * SIZE, MEASURED AND OVER TARGET, WITH THE REASON RECORDED - src/utests/AGENTS.md asks for exactly
 * that rather than for silence. 48.0 MB clang debug and 104.3 MB gcc release (a64), against a
 * 40 MB target and a 75 MB debug ceiling which only win-x86-*-debug enforces. What costs it is the
 * composition itself, and every part of it is what these cases are for: the session instantiates
 * BOTH drivers (the HTTP/2 one it builds and the HTTP/1.1 one its factory registers for the
 * fallback), so the client-role protocol engine is in this TU; the in-process peer of design 8.2
 * puts the SERVER-role engine in it as well; and the library's own HttpServer is the third peer.
 *
 * SPLITTING WAS CONSIDERED AND MEASURED, AND IT LOSES. The only seam is peer-versus-HttpServer -
 * about 8.6 MB and 5.8 MB respectively, from the figures h2client2 and httpclient3 recorded - and
 * the session with both drivers is roughly 15 MB which BOTH halves would pay, on top of the ~21 MB
 * TU floor each. So the split produces two modules of about 45 and 42 MB, neither under the target
 * and 37 MB more in total. If this module does grow, that seam is where the cut goes
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

#include "TestClientSession.h"
