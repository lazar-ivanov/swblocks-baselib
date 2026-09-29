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

#define UTF_TEST_MODULE utf_baselib_h2client11
#include <utests/baselib/UtfMain.h>

/*
 * A numbered sibling of utf_baselib_h2client - see src/utests/AGENTS.md for the numbering scheme.
 * Created by change-set CS-9 of Astra's fourth review, for U01's cases: the negotiated value a
 * connection publishes, and the request task which reads it
 * (notes/plans/issues/astra4-cs9-negotiated-publication-design.md)
 *
 * Sockets: loopback only, on ephemeral ports, so no machine global test lock is needed
 *
 * WHY THIS MODULE EXISTS, and it is the size policy. U01's cases need the HTTP/2 driver over TLS, and
 * the one of them which is U01's own route needs the request task on top of it. The modules which
 * carry this driver over TLS - utf_baselib_h2client3, 9 and 10 - are at or near the 40 MB target and
 * take no further cases, and those which carry the request task over a real driver carry the session
 * too, at 47.5 MB
 *
 * WHAT THIS MODULE PAYS FOR. The HTTP/2 driver over the stranded TLS policy, with the connection task
 * under it, and the request task over that driver. The driver probe is
 * utests/baselib/Http2DriverTlsProbe.h and the TLS peer utests/baselib/TlsEndingPeer.h. Not the pool,
 * which a test pool of one connection stands in for, and not the session
 *
 * SIZE, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 38.0 MB clang debug (a64) with its
 * five cases, when CS-9 closed. By the ratio win-x86 debug has shown over a64 clang debug (1.13 to
 * 1.17, notes/plans/issues/windows-matrix-handoff.md and row 5c of astra-remediation-owed-work.md) that
 * is about 43 to 44 MB on x86 - inferred, not measured; the Windows handoff measures it. The weight is
 * the HTTP/2 driver over the stranded TLS policy: utf_baselib_h2client10 costs 35.9 MB with it and no
 * request task, so the request task is the small part. Every case here instantiates the driver, so a
 * split cannot lower it. This module takes no further cases
 *
 * The module is devenv7+ only: the stranded policies #error on a Boost older than 1.72, and the
 * devenv7_only marker next to this file is what keeps it out of the build on devenv2-6
 * (projects/make/common.mk)
 *
 * APPEND CONVENTION - a change-set adds its own Test<Feature>.h files in THIS directory and appends
 * their #include lines at the END of the include block below, never reordering or editing another
 * change-set's; and it appends a notes.txt recipe per case it adds
 */

/*
 * Test headers - appended at the end
 */

#include "TestNegotiatedPublication.h"
