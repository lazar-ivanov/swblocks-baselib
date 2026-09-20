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

#define UTF_TEST_MODULE utf_baselib_h2client4
#include <utests/baselib/UtfMain.h>

/*
 * THE CLIENT ORCHESTRATION MODULE - the pool, and what is built on top of the drivers
 *
 * The layout of this feature's client modules, so that nobody has to re-derive it:
 *
 *   utf_baselib_h2client    S3.2, S3.3, S4.1 - the stranded stream policies and connection
 *                           establishment. CLOSED to new slices since S4.1.
 *   utf_baselib_h2client2   S4.2 cleartext - the driver's shell.
 *   utf_baselib_h2client3   S4.2 TLS - the ALPN outcomes and h2 over the hardened context.
 *                           CLOSED - 39.7 MB clang debug against a 40 MB target.
 *   utf_baselib_h2client4   S5.2 - THIS ONE. The connection pool of design 5.4: queueing behind
 *                           a Connecting placeholder, stream slot accounting, the retry matrix,
 *                           GOAWAY draining, the establishment bound, disposal and design 8.3's
 *                           concurrency. It is the module a client ORCHESTRATION subject belongs
 *                           in - what sits above a driver rather than inside one.
 *
 * WHY IT EXISTS. Every one of its three siblings was at or near the 40 MB target of
 * src/utests/AGENTS.md when this slice started - 37.1, 37.5 and 39.7 - and a new module costs
 * nothing but the ~21 MB translation unit floor, while adding to a module which is already at the
 * target costs the platform where the ceiling is enforced.
 *
 * WHAT IT DELIBERATELY DOES NOT INSTANTIATE. No http2::Session, no driver, no test peer, no
 * stream policy. The pool is protocol agnostic by construction - it talks to ClientConnection and
 * to tasks::Task - so the engine would be weight with nothing to weigh it against. The two cases
 * which pin the draining reserve INTO a session live in utf_baselib_h2core, where a Session is
 * already instantiated and they cost nothing.
 *
 * THE ONE EXCEPTION IS A CONSTANT. H2Pool_PolicyDefaultsTests static_asserts the pool's
 * unconfirmed concurrency limit against the h2 driver's, which the pool's inference depends on and
 * which nothing else can check - the driver does not include the pool, and the pool must not
 * include the driver, which is what keeps it protocol agnostic. Reading that enum instantiates the
 * driver class and no object of it: measured at 0.01 MB of object and about six seconds of compile
 * time. No other case here may touch the driver.
 *
 * The cases here take no socket and no port, so they do not take the machine global test lock.
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

#include "TestConnectionPool.h"
