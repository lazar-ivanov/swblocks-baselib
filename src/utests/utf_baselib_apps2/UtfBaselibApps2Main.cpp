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

#define UTF_TEST_MODULE utf_baselib_apps2
#include <utests/baselib/UtfMain.h>

/*
 * The messaging HTTP gateway app tests; split out of utf_baselib_apps so that no single test
 * translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * utf_baselib_apps covered two unrelated applications. This header is the only one reaching
 * apps/bl-messaging-http-gateway, and the two which remain are the only ones reaching apps/bl-tool,
 * so the split follows the application boundary rather than cutting across it
 */

#include "TestMessagingApps.h"
