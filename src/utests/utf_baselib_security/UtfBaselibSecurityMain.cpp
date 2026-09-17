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

#define UTF_TEST_MODULE utf_baselib_security
#include <utests/baselib/UtfMain.h>

/*
 * The hashing and signing tests. The crypto and PEM key format cases now live in
 * utf_baselib_security2, and the authorization cache and service cases in utf_baselib_security3,
 * so that no single test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * Nothing here reads a data file or takes the machine global test lock, which is why this module
 * has no data directory
 */

#include "TestHashUtils.h"
#include "TestHmacSha256.h"
#include "TestRsaSignVerify.h"
