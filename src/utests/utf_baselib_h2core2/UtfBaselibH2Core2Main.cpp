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

#define UTF_TEST_MODULE utf_baselib_h2core2
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_h2core - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set L7-D of the HTTP/2 client's layer L7, browser impersonation, for the cases
 * of http2/Fingerprint.h: the HTTP/2 fingerprint of the frames a session produced
 * (notes/plans/http2-l7-execution-plan.md 4.2; notes/plans/http2-design.md 6.4 and 6.6)
 *
 * None of these cases touches a socket, so the module runs in parallel with everything else
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. The fingerprint's cases need the protocol
 * core and a Session, which utf_baselib_h2core compiles already, but that module is past the
 * 40 MB target - 46.8 MB at a64 gcc debug - and takes no further cases
 *
 * WHAT THIS MODULE PAYS FOR. The sans-I/O protocol core - the frame codec, HPACK, and the Session
 * engine which two of the cases drive - and the fingerprint over it. No socket, no driver and no
 * OpenSSL
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 22.9 MB clang debug (a64) with its
 * ten cases, when L7-D built it. Its x86 size is the Windows section's to measure; an a64-to-x86
 * estimate by the measured 1.15 to 1.25 puts it at about 26 to 29 MB - inferred, not measured.
 * Under the 40 MB target, so the module has room for further cases of the protocol core
 *
 * The module is devenv7+ only, as utf_baselib_h2core is: the devenv7_only marker next to this
 * file is what keeps it out of the build on devenv2-6 (projects/make/common.mk). Headers never
 * test BL_DEVENV_VERSION; they guard on the capability they need - BOOST_VERSION,
 * OPENSSL_VERSION_NUMBER - with a clear #error
 *
 * APPEND CONVENTION - a change-set adds its own Test<Feature>.h files in THIS directory and appends
 * their #include lines at the END of the include block below, never reordering or editing another
 * change-set's; and it appends a notes.txt recipe per case it adds, since notes.txt declares
 * itself a complete index
 */

#include <baselib/http2/PreCompiled.h>

/*
 * Test headers - appended at the end
 */

#include "TestHttp2Fingerprint.h"
