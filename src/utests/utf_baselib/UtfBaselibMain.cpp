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

#define UTF_TEST_MODULE utf_baselib
#include <utests/baselib/UtfMain.h>

/*
 * TODO: bringing all definitions of bl into the global namespace like this is of course typically not
 * a good idea, but unfortunately many tests here are written under the assumption that the bl namespace
 * is visible and thus we have to do it
 *
 * In the future if/when we cleanup the tests this using 'using namespace bl;' line can be removed
 */

using namespace bl;

/*
 * The time zone, date/time validation, transaction, net utils and Boost.Asio compatibility cases
 * now live in utf_baselib2, so that no single test translation unit exhausts a 32-bit compiler host
 * - see notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * What remains is the group which reaches UtfBaseLibCommon.h - the umbrella pulling the whole
 * messaging, http, tasks and data stack - plus the object model cases, which are welded to
 * UtfLoaderInit.h and the examples/objmodel fixtures and cannot be separated from them
 */

#include "TestBaselibDefault.h"
#include "TestBaselibDefault2.h"
#include "TestBaselibDefault3.h"
#include "TestBaselibDefault4.h"
#include "TestBaselibDefault5.h"
#include "TestBaselibDefault6.h"
#include "TestBaselibDefault7.h"
#include "TestBaselibDefault8.h"
#include "TestBaselibDefault9.h"
#include "TestBaselibDefault10.h"
#include "TestObjModel.h"
#include "TestWatchdog.h"

/*
 * Global fixtures are included last
 */

#include "UtfLoaderInit.h"
