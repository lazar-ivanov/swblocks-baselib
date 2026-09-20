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

#ifndef __BL_HTTPCLIENT_PRECOMPILED_H_
#define __BL_HTTPCLIENT_PRECOMPILED_H_

#include <baselib/httpclient/HeaderProfile.h>
#include <baselib/httpclient/ClientTypes.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/CookieJar.h>
#include <baselib/httpclient/RedirectPolicy.h>
#include <baselib/httpclient/ContentDecoder.h>

/*
 * httpclient/Http1Codec.h is NOT here, deliberately. It includes the HTTP/1.1 codec backend, which
 * includes core/detail/BeastBoostImports.h, and design 5.5 states as a property that the Beast
 * import header is not reachable from core/BaseIncludes.h or from any PreCompiled.h - exactly as
 * core/AsioSSL.h is not. Appending it above would make every module which includes this umbrella
 * compile Boost.Beast, whether or not it speaks HTTP/1.1. A consumer of the codec includes
 * <baselib/httpclient/Http1Codec.h> directly, and pays for it there
 */

/*
 * httpclient/ClientConnectionTaskBase.h is NOT here either, for the reason the closing note below
 * gives: it includes crypto/CryptoBase.h for the negotiated-parameter floor check of design 3.3,
 * so it reaches OpenSSL, and an optional dependency does not belong in a pre-compiled header.
 * A consumer includes it directly
 *
 * httpclient/ConnectionPool.h is NOT here either, and its reason is size rather than dependency.
 * It reaches the whole tasks layer - an execution queue of connection tasks is what design 5.4
 * asks the pool to keep - and every test module in this feature includes this umbrella, several
 * of them within a megabyte or two of the 40 MB object target of src/utests/AGENTS.md. A module
 * which does not pool connections should not pay for the machinery that does, so the pool is
 * included by the modules which use it
 *
 * So this umbrella is not a complete index of httpclient/, and there are now three headers it does
 * not list rather than one
 */

/*
 * The version-neutral HTTP client umbrella. Each slice which adds an httpclient/ header appends
 * its #include above, in the form of http/PreCompiled.h
 *
 * It carries httpclient/ headers only, the way every other umbrella here carries its own directory
 * only. httpclient/ depends on http2/ (design 2.2), but that dependency belongs to the headers
 * which have it, not to this file - a module needing both includes both umbrellas
 *
 * This header is included by the devenv7-only test modules and by nothing else. In particular it
 * is deliberately NOT added to utests/baselib/UtfBaseLibCommon.h: every test module on every
 * devenv includes that one, so adding it there would break devenv2-6 and would pull this code
 * into every existing module's translation unit
 *
 * Optional dependencies like OpenSSL should not be added to the
 * pre-compiled headers
 */

#endif /* __BL_HTTPCLIENT_PRECOMPILED_H_ */
