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

#define UTF_TEST_MODULE utf_baselib_tasks4
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_tasks - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set CS-6 of astra's second review, for D3: the connection establisher's connect
 * over several endpoints, which the change-set moves from asio's ranged connect to a loop of its own
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. utf_baselib_tasks is 67.7MB at win-x86 debug,
 * utf_baselib_tasks2 instantiates no TLS stream policy, and utf_baselib_tasks3, which carries the four
 * stream policies' teardown cases of CS-6, is 36.7 MB at a64 clang debug - near the 40 MB target,
 * where no case is added
 *
 * WHAT THIS MODULE PAYS FOR. The connection establisher over the four stream policies - cleartext
 * and TLS, each with and without a strand - and loopback listeners, some with a full accept queue.
 * No TLS peer: nothing here reaches a handshake
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed. Some cases
 * address 127.0.0.2, where nothing listens, which Linux routes over loopback; one case reads
 * /proc/net/tcp, and runs only where that file exists
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

#include "TestTcpConnectLoop.h"
