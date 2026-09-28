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

#define UTF_TEST_MODULE utf_baselib_tasks3
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_tasks - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set CS-6 of astra's second review, for the two fixes it makes to the TCP and TLS
 * stream policies of tasks/TcpBaseTasks.h and tasks/TcpSslBaseTasks.h (owed-list rows I13 and I2)
 *
 * WHY THIS MODULE EXISTS. utf_baselib_tasks is 67.7MB at win-x86 debug, well past the 40MB target
 * (UtfBaselibTasks2Main.cpp), and utf_baselib_tasks2 instantiates no TLS stream policy. These cases
 * drive the four stream policies - cleartext and TLS, each with and without a strand - through the
 * library's own connection establisher, so every one of them is a fresh instantiation which neither
 * sibling pays for today
 *
 * WHAT THIS MODULE PAYS FOR. The connection establisher over each of the four policies, and a TLS
 * peer - utests/baselib/TlsEndingPeer.h, a raw asio::ssl::stream server which ends the stream in a
 * chosen way. Not an HTTP driver, a request task, a session or a pool
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 36.9 MB clang debug (a64) at
 * d13859c, which by the ratio win-x86 debug has shown over a64 clang debug (1.11 to 1.17,
 * notes/plans/issues/windows-matrix-handoff.md and row 5c of astra-remediation-owed-work.md) is
 * about 41 to 43 MB on x86 - inferred, not measured; the Windows handoff measures it. The weight is
 * the connection establisher over the four stream policies, and the TLS peer. This module takes no
 * further cases
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
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

#include "TestTcpForcedCancelLinger.h"
#include "TestTlsShutdownEndings.h"
#include "TestTlsLostCancel.h"
