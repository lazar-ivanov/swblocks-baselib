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

#ifndef __BL_HTTP2_PRECOMPILED_H_
#define __BL_HTTP2_PRECOMPILED_H_

#include <baselib/http2/Globals.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/http2/FrameCodec.h>
#include <baselib/http2/FlowControlWindow.h>

/*
 * The HTTP/2 umbrella. Each slice which adds an http2/ header appends its #include above, in the
 * form of http/PreCompiled.h
 *
 * This header is included by the devenv7-only test modules and by nothing else. In particular it
 * is deliberately NOT added to utests/baselib/UtfBaseLibCommon.h: every test module on every
 * devenv includes that one, so adding it there would break devenv2-6 and would pull this code
 * into every existing module's translation unit
 *
 * Optional dependencies like OpenSSL should not be added to the
 * pre-compiled headers
 */

#endif /* __BL_HTTP2_PRECOMPILED_H_ */
