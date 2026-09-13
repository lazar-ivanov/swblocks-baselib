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

#define UTF_TEST_MODULE utf_baselib_security3
#include <utests/baselib/UtfMain.h>

/*
 * The authorization cache and authorization service tests; split out of utf_baselib_security so
 * that no single test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These are the cases which stand up an HTTPS server through HttpServerHelpers and therefore take
 * the machine global test lock, and which read the JSON and response text fixtures from this
 * module's own data directory. Keeping them together confines the lock to this one security
 * module, so utf_baselib_security and utf_baselib_security2 run free of it
 *
 * This module is 52.4MB and so remains above the 40MB ceiling, which no rearrangement of these
 * three headers can fix: measured on x86 vc143 debug, TestAuthorizationCacheImpl.h alone is 47.3MB
 * and TestAuthorizationCacheRestImpl.h alone is 49.4MB, because both instantiate the same
 * AuthorizationCache machinery reached through utests/baselib/TestAuthorizationCacheImplUtils.h.
 * Splitting TestAuthorizationServiceRest.h out into a fourth module was tried and reverted: it
 * moved this object by 0.4MB while adding a 37.7MB one. Getting this cluster under the ceiling is
 * a separate piece of work on that shared helper, not a file move
 */

#include "TestAuthorizationCacheImpl.h"
#include "TestAuthorizationCacheRestImpl.h"
#include "TestAuthorizationServiceRest.h"

