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

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/*
 * The object model of this library is intrusively refcounted, so a test which drops a
 * reference without disposing it leaks silently; ~DefaultUtfConfigT() notices at teardown,
 * but only after Boost.Test has produced its report.
 *
 * This case is a permanent, attributable guard for the loader module and is deliberately
 * registered last, so it runs after the plug-in fixture cases whose ~TestPluginT resets the
 * global loader state - that reset is the thing most likely to regress here.
 */

UTF_AUTO_TEST_CASE( TestNoOutstandingObjectRefs )
{
    UTF_REQUIRE_EQUAL( bl::om::outstandingObjectRefs(), 0L );
}
