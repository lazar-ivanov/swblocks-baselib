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

#define UTF_TEST_MODULE utf_baselib_io2
#include <utests/baselib/UtfMain.h>

/*
 * The messaging client and backend processing tests; split out of utf_baselib_io so that no single
 * test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * This range referenced exactly one name from the 3100 line anonymous namespace at the top of
 * TestIO.h - the connector_t typedef - which TestIO2.h now carries its own copy of. Duplicating it
 * across two modules is safe: they are separate binaries with no ODR relationship, and invariant C6
 * rejects such duplication only within a single module
 *
 * Note both halves remain well above the 40MB target. About 30MB of TCP and messaging machinery is
 * instantiated in full by either side, so the split buys compliance with the 75MB ceiling rather
 * than a proportionate reduction - see notes/plans/issues/test-instantiation-weight-deferral.md
 */

#include "TestIO2.h"
