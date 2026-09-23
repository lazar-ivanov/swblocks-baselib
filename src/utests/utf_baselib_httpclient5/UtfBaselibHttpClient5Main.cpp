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

#define UTF_TEST_MODULE utf_baselib_httpclient5
#include <utests/baselib/UtfMain.h>

/*
 * The SESSION OVER TLS - the fourth numbered sibling of utf_baselib_httpclient, created by S6.1
 *
 * WHY THIS MODULE EXISTS, AND IT IS NOT ONLY SIZE. A TEMPLATE NOTHING INSTANTIATES IS NOT COMPILED,
 * and "builds clean" says nothing about it - this project has been bitten by that twice, most
 * recently when TcpTunnelStageT over a TLS stream policy turned out never to have been compiled at
 * all and S4.1 hit it on contact. ClientSessionT is a template over the stream policy, so the
 * claim that it supports a TLS transport is a claim until some translation unit names the type.
 * This module names it, and then runs it: the cleartext half is utf_baselib_httpclient4
 *
 * It is also the only place the h2-only routing of a streaming upload is reachable end to end.
 * That routing exists because the HTTP/1.1 driver refuses every request carrying a BodySource, and
 * it only does anything on a transport which NEGOTIATES - a cleartext session speaks whatever its
 * configuration says and never both, so utf_baselib_httpclient4 can pin the decision and the
 * narrowing as the two pure functions they are, and not their join. Here the join runs
 *
 * And the size seam is the same one utf_baselib_httpclient4's header describes: that module pays
 * for the cleartext peer and the library's own HttpServer, this one for the TLS peer, and neither
 * pays for the other's
 *
 * Sockets: loopback, ephemeral ports, so these cases do not take the machine global test lock
 *
 * SIZE, MEASURED AND OVER TARGET, WITH THE REASON RECORDED, as src/utests/AGENTS.md asks: 46.9 MB
 * clang debug (a64), against a 40 MB target and a 75 MB debug ceiling which only win-x86-*-debug
 * enforces; 93.6 MB gcc release (a64) was measured before the h1-over-TLS cases and has not been
 * re-measured since. The session instantiates BOTH drivers over the TLS policy - the HTTP/2 one it
 * builds and the HTTP/1.1 one its factory registers for the fallback - so the client-role protocol
 * engine is here, and the TLS peer puts the server-role engine here as well. The cases cost almost
 * nothing - the three h1-over-TLS ones added 1.3%, because the types they name were already
 * instantiated - and the instantiations are the whole of it, which is also why splitting this
 * module would buy nothing at all
 *
 * The module is devenv7+ only: the devenv7_only marker next to this file is what keeps it out of
 * the build on devenv2-6 (projects/make/common.mk). Headers never test BL_DEVENV_VERSION; they
 * guard on the capability they need - BOOST_VERSION, OPENSSL_VERSION_NUMBER - with a clear #error
 *
 * WHAT IS OWED: the same cases on OpenSSL 1.1.1w. No dist on this machine carries that flavor and
 * BL_USE_OPENSSL_1X=1 does not build for pre-existing reasons - see
 * notes/plans/issues/openssl-1x-flavor-deferral.md
 *
 * APPEND CONVENTION - read this before adding to this file
 * (notes/plans/http2-implementation-plan.md section 0)
 *
 *   A slice adds its own Test<Feature>.h in THIS directory and appends EXACTLY ONE #include line
 *   at the END of the include block below. It does not reorder, edit or remove another slice's
 *   line, and it does not include a test header from another module's directory
 *   (src/utests/AGENTS.md). Different slices then append different lines, so two lanes landing at
 *   once is a trivial merge rather than a conflict
 *
 *   Sibling files a slice may also add here: a notes.txt line carrying its --run_test= recipe,
 *   and a data/ file if it needs one - data/ is never shared between modules
 */

#include <baselib/http2/PreCompiled.h>
#include <baselib/httpclient/PreCompiled.h>

/*
 * Test headers - one appended line per slice, at the end
 */

#include "TestClientSessionTls.h"
#include "TestClientSessionTlsHttp1.h"
