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

#define UTF_TEST_MODULE utf_baselib_tasks2
#include <utests/baselib/UtfMain.h>

/*
 * The plain stream connection establisher tests
 *
 * They are here rather than in utf_baselib_tasks because that module is 67.7MB (vc143) / 67.9MB
 * (ccl16) on win-x86 debug - 90% of the 75MB ceiling and well past the 40MB target - and it
 * instantiates no TCP connection establisher today, so these cases would add a fresh stack to the
 * tightest object in the tree. See src/utests/AGENTS.md and
 * notes/reviews/major/update_2026/test-module-split-ledger.md
 *
 * None of these cases takes the machine global test lock or binds the fixed test port: the peer is
 * a listening socket on an ephemeral loopback port, so the module runs in parallel with everything
 * else
 */

#include "TestTcpPreHandshakeStage.h"
