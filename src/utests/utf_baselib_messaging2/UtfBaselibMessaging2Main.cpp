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

#define UTF_TEST_MODULE utf_baselib_messaging2
#include <utests/baselib/UtfMain.h>

/*
 * The server monitoring and async RPC data model tests; split out of utf_baselib_messaging so that
 * no single test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These two headers were already separate from TestMessagingDefault.h and share nothing with it, so
 * they move as they are
 */

#include "TestServerMonitoring.h"
#include "TestAsyncRpcDataModel.h"
