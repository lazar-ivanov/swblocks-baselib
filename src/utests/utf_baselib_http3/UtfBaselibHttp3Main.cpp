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

#define UTF_TEST_MODULE utf_baselib_http3
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_http - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set CS-6 of astra's second review, for D-L3-1's cases on the HTTP server's tasks,
 * and for D2's cases on SimpleHttpTask's deadline over its own TLS handshake
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed - unlike
 * utf_baselib_http, whose cases stand up a server on the fixed test port
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. utf_baselib_http is 54.9 MB, above the 40 MB
 * target, and utf_baselib_http2 carries the TLS policy's own cases. D-L3-1
 * (notes/plans/issues/astra2-cs6-lost-forced-cancel-design.md) makes the deadlines which survive a
 * cancel issue a lost cancel again, and two of them are the HTTP server's: the receive task's idle
 * timer and the send task's response timer
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP server's receive and send tasks over the cleartext stream
 * policy, driven directly over a connected loopback pair - 24.5 MB a64 clang debug with those alone -
 * and SimpleHttpTask over the cleartext and TLS policies against utests/baselib/TlsEndingPeer.h, which
 * D2's cases added. Not a server, an acceptor task or a backend - HttpServerHelpers.h is what costs
 * utf_baselib_http about 30 MB, and it is not used here
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 34.4 MB clang debug (a64) at
 * d13859c, which by the ratio win-x86 debug has shown over a64 clang debug (1.13 to 1.17,
 * notes/plans/issues/windows-matrix-handoff.md and row 5c of astra-remediation-owed-work.md) is
 * about 39 to 40 MB on x86 - inferred, not measured; the Windows handoff measures it. The weight is
 * the server tasks (24.5 MB alone), plus SimpleHttpTask over both policies and the TLS peer. This
 * module takes no further cases
 *
 * The module is devenv7+ only - the devenv7_only marker next to this file keeps it out of the build
 * on devenv2-6 (projects/make/common.mk) - because it was written and verified against devenv7 alone,
 * as every module change-set CS-6 created was
 *
 * APPEND CONVENTION - a change-set adds its own Test<Feature>.h files in THIS directory and appends
 * their #include lines at the END of the include block below, never reordering or editing another
 * change-set's; and it appends a notes.txt recipe per case it adds
 */

/*
 * Test headers - appended at the end
 */

#include "TestHttpServerLostCancel.h"
#include "TestSimpleHttpHandshakeDeadline.h"
