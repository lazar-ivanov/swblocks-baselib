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

#define UTF_TEST_MODULE utf_baselib_h2client5
#include <utests/baselib/UtfMain.h>

/*
 * THE POOL COMPOSED WITH A REAL DRIVER AND A REAL PEER - the fourth numbered sibling of
 * utf_baselib_h2client
 *
 * WHAT IT IS FOR. Everything the pool decides about CAPACITY is decided from a number a driver
 * publishes - freeStreamSlots( ) - and the pool's own cases publish that number from a stub. That
 * is the right way to pin the rule and it cannot pin the composition: whether a real driver
 * publishes what the pool's inference assumes it publishes, and whether it publishes it at the
 * moment the peer's SETTINGS arrive rather than only when a response completes. The L6 review
 * found that every composed case in the feature runs its requests SEQUENTIALLY, where the pool
 * learns the peer's limit from releaseStream( )'s markPeerLimitKnown( ) and none of the
 * concurrency logic has to decide anything. This module is the regime that does.
 *
 * WHY IT IS NOT IN EITHER MODULE WHICH ALREADY HAS HALF OF IT, and the reason is recorded in both
 * of them rather than invented here:
 *
 *   utf_baselib_h2client2 (the driver, 37.9 MB clang debug) - its own main says S5.2's pool and
 *                         the stress suite of design 8.3 go elsewhere, and it is near the 40 MB
 *                         target that src/utests/AGENTS.md says not to add to.
 *   utf_baselib_h2client4 (the pool, 25.8 MB) - its own main says "No other case here may touch
 *                         the driver", with one named exception for reading an enum, because the
 *                         pool is protocol agnostic by construction and a driver plus a peer in
 *                         that translation unit is the weight the module was created to avoid.
 *
 * So a numbered sibling, which src/utests/AGENTS.md calls the intended answer and which needs no
 * makefile change. What it pays for is one h2 driver, one in-process peer and one pool, over the
 * TU floor.
 *
 * MEASURED: 37.6 MB clang debug (a64) with the one case below, against a 40 MB target and a 75 MB
 * ceiling - under the target, and not by much. Almost all of it is the driver and the peer, which
 * is what utf_baselib_h2client2's 37.9 MB for thirteen driver cases says too: the pool and this
 * case together are the small part. So there is room for another case about the same composition
 * and none for a second peer or a session. Measure before adding one; do not convert from another
 * module's figure - the rule in src/utests/AGENTS.md exists because such a conversion was once
 * off by 40x, and L6 finding 8 is the feature's own example of the same arithmetic failing
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

#include "TestConnectionPoolConcurrency.h"
