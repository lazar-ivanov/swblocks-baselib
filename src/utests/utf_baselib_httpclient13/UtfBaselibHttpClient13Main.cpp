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

#define UTF_TEST_MODULE utf_baselib_httpclient13
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_httpclient - see src/utests/AGENTS.md for the numbering scheme.
 * Reserved for change-set CS-6 of astra's second review
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS. I2 (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md)
 * changes how every TLS task's teardown ends after the peer truncated, and its note asks for the
 * change to be shown on the real consumers as well as on utf_baselib_tasks3's probes: both HTTP
 * drivers, and the legacy SimpleHttpTask. This module carries two of them; the HTTP/2 driver is
 * utf_baselib_h2client9's, because the three together measured 45.0 MB a64 clang debug.
 * utf_baselib_tasks3 carries no HTTP code and is kept that way; the TLS HTTP modules which exist are
 * at or near the 40MB target, or belong to other change-sets
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP/1.1 driver over the stranded TLS policy, the plain connection
 * establisher over the same policy, and SimpleHttpSslTask: 36.9 MB a64 clang debug. Not the HTTP/2
 * driver, the request task, the session or the pool. The TLS peer is utests/baselib/TlsEndingPeer.h,
 * shared with utf_baselib_tasks3 and utf_baselib_h2client9, and so is the run of a consumer to the
 * end of its teardown, utests/baselib/TlsTeardownTestUtils.h
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 36.9 MB clang debug (a64) at
 * d13859c, which by the ratio win-x86 debug has shown over a64 clang debug (1.11 to 1.17,
 * notes/plans/issues/windows-matrix-handoff.md and row 5c of astra-remediation-owed-work.md) is
 * about 41 to 43 MB on x86 - inferred, not measured; the Windows handoff measures it. The weight is
 * the HTTP/1.1 driver, the plain connection establisher and SimpleHttpSslTask. This module takes no
 * further cases. MB above are 10^6 bytes; as utf_objsize.py counts: 35.2 MB, about 40-41 on x86
 *
 * The module is devenv7+ only: the stranded policies #error on a Boost older than 1.72, and the
 * devenv7_only marker next to this file is what keeps it out of the build on devenv2-6
 * (projects/make/common.mk)
 *
 * APPEND CONVENTION - a change-set adds its own Test<Feature>.h files in THIS directory and appends
 * their #include lines at the END of the include block below, never reordering or editing another
 * change-set's; and it appends a notes.txt recipe per case it adds
 */

/*
 * Test headers - appended at the end
 */

#include "TestTlsTruncationTeardown.h"
