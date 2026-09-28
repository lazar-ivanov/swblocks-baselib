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

#define UTF_TEST_MODULE utf_baselib_h2client9
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_h2client - see src/utests/AGENTS.md for the numbering scheme.
 * Reserved for change-set CS-6 of astra's second review
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. I2
 * (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md) is shown on the real
 * consumers as well as on utf_baselib_tasks3's probes, and the HTTP/2 driver over TLS is the heaviest
 * of them. With the HTTP/1.1 driver and SimpleHttpTask in one module the object was 45.0 MB a64 clang
 * debug, over the 40 MB target; the one module which already carries this driver over TLS,
 * utf_baselib_h2client3, measured 41.7 MB at the same toolchain and variant. Alone, the HTTP/2 case
 * is 37.6 MB; the other two live in utf_baselib_httpclient13, at 36.7 MB
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP/2 driver over the stranded TLS policy, and the connection
 * establisher under it. The TLS peer is utests/baselib/TlsEndingPeer.h, and the run of a consumer to
 * the end of its teardown is utests/baselib/TlsTeardownTestUtils.h, both shared with
 * utf_baselib_httpclient13. Not the request task, the session or the pool
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

#include "TestHttp2TlsTruncationTeardown.h"
