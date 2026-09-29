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

#define UTF_TEST_MODULE utf_baselib_h2client10
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_h2client - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set CS-6 of astra's second review, for D-L3-1's cases on the HTTP client
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. D-L3-1
 * (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md) pins the HTTP/2 driver's application
 * phase against the receive shutdown its fix adds, and makes the HTTP client's connect deadline issue a
 * lost cancel again. The one module which carries this driver over TLS for CS-6,
 * utf_baselib_h2client9, is 35.7 MB at a64 clang debug, near the 40 MB target, where no case is added
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP/2 driver over the stranded TLS policy, and under it the client
 * connection task the connect deadline belongs to. The driver probe is
 * utests/baselib/Http2DriverTlsProbe.h and the TLS peer utests/baselib/TlsEndingPeer.h, both shared
 * with utf_baselib_h2client9. Not the request task, the session or the pool
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 35.9 MB clang debug (a64) at
 * d13859c, which by the ratio win-x86 debug has shown over a64 clang debug (1.13 to 1.17,
 * notes/plans/issues/windows-matrix-handoff.md and row 5c of astra-remediation-owed-work.md) is
 * about 41 to 42 MB on x86 - inferred, not measured; the Windows handoff measures it. The weight is
 * the HTTP/2 driver over the stranded TLS policy and the connection task under it, which one case
 * alone costs (utf_baselib_h2client9 is one case at 35.7 MB), so a split cannot lower it. This
 * module takes no further cases
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

#include "TestHttpClientLostCancel.h"
