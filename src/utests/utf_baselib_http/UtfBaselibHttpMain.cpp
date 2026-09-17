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

#define UTF_TEST_MODULE utf_baselib_http
#include <utests/baselib/UtfMain.h>

/*
 * The HTTP server and client tests. The TLS policy, peer verification and stream wrapper cases now
 * live in utf_baselib_http2, so that no single test translation unit exhausts a 32-bit compiler
 * host - see notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * Everything left here reaches HttpServerHelpers.h, which stands up a server on the fixed test port
 * under the machine global test lock, so these cases serialize against every other port using module
 * and are deliberately kept together
 *
 * This module is 54.9MB - inside the 55MB ceiling but above the 40MB target, and the target is not
 * reachable by moving files. Standing up a server through HttpServerHelpers.h costs about 30MB of
 * template instantiation which every case here shares, the same shape the utf_baselib_security pilot
 * measured for the authorization cache. Splitting TestClientHttpTasks.h into a fourth module was
 * tried and reverted: it moved this object by 2.5MB while adding a 51.1MB one, leaving two modules
 * near the ceiling instead of one. See notes/plans/issues/test-instantiation-weight-deferral.md
 */

#include "TestClientHttpTasks.h"
#include "TestHttpServer.h"
#include "TestTlsHandshakeVerification.h"
