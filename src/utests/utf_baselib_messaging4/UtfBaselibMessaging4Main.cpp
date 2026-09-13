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

#define UTF_TEST_MODULE utf_baselib_messaging4
#include <utests/baselib/UtfMain.h>

/*
 * IO_MessagingProxyBackendTests alone; split out of utf_baselib_messaging so that no single
 * test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * This one case dominates whichever module holds it: measured on its own it instantiates
 * 71.18 MB of x86 debug object, very nearly what all 31 cases of the original header cost
 * together. It therefore gets a module to itself, and the exception throw hooks it arms
 * travel with it - nothing else referenced them
 */

#include "TestMessagingProxyBackend.h"
