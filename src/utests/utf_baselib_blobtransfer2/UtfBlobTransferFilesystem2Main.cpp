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

#define UTF_TEST_MODULE utf_baselib_blobtransfer2
#include <utests/baselib/UtfMain.h>

/*
 * The packager and unpackager unit tests; split out of utf_baselib_blobtransfer so that no single
 * test translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * The cut follows the lock boundary. These cases drive the unpackager and packager units standalone
 * - no blob server, no fixed test port - so all but one of them run free of the machine global test
 * lock, while every case left in utf_baselib_blobtransfer takes it. The exception is
 * BlobTransfer_FilesPackagerInMemoryProxyTests, which lives here and does take the lock
 */

#include "TestBlobTransferFilesystem2.h"
