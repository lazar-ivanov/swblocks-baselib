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

#define UTF_TEST_MODULE utf_baselib2
#include <utests/baselib/UtfMain.h>

/*
 * The core utility tests - time zones, date/time validation, transactions, net utils and the
 * Boost.Asio compatibility surface; split out of utf_baselib so that no single test translation
 * unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These six headers are the ones in utf_baselib which reach neither UtfBaseLibCommon.h - the
 * umbrella which pulls the whole messaging, http, tasks and data stack - nor the examples/objmodel
 * fixtures. That is what makes them separable: they carry only baselib/core includes, so they take
 * none of the instantiation weight with them and leave none behind
 */

/*
 * Kept deliberately, to match the compilation context these headers had in utf_baselib: several of
 * them use bl:: sub-namespaces unqualified, relying on this being in scope before they are included
 */

using namespace bl;

#include "TestTimeZoneData.h"
#include "TestDateTimeValidationUtils.h"
#include "TestTransaction.h"
#include "TestNetUtils.h"
#include "TestBoostAsioCompat.h"
#include "TestBoostAsioErrorCallback.h"
