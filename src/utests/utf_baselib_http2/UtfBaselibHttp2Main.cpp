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

#define UTF_TEST_MODULE utf_baselib_http2
#include <utests/baselib/UtfMain.h>

/*
 * The TLS policy and stream wrapper tests; split out of utf_baselib_http so that no single test
 * translation unit exhausts a 32-bit compiler host - see
 * notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These are the cases which drive OpenSSL and the stream wrapper directly, without standing up a
 * server: none of them includes HttpServerHelpers.h, none takes the machine global test lock and
 * none binds the fixed test port. Keeping them apart from the server cases is what lets this module
 * run in parallel with everything else
 */

#include "TestTlsProtocolPolicy.h"
#include "TestTlsPeerVerification.h"
#include "TestAsioSslStreamWrapper.h"
#include "TestTcpPreHandshakeStageTls.h"
#include "TestTlsHandshakeRetryClassifier.h"
