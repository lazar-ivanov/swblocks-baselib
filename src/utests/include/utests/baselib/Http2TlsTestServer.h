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

#ifndef __UTEST_HTTP2TLSTESTSERVER_H_
#define __UTEST_HTTP2TLSTESTSERVER_H_

#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/SimpleTaskControlToken.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <string>
#include <vector>

#include <utests/baselib/Http2TestServer.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The peer of design 8.2 over TLS, choosing from an ALPN preference the case gives it - the one such
 * peer in the tree
 *
 * It lives here and not in a module directory because three modules run it and a test header may never
 * be included across module directories (src/utests/AGENTS.md): utf_baselib_h2client3, whose HTTP/2
 * driver cases it was written for by S4.2, and utf_baselib_httpclient5 and utf_baselib_httpclient10,
 * whose session cases over TLS each carried a copy of it until CS-5 of astra's second review (owed-list
 * row I3) moved the one class here.
 *
 * THE STEERING IS THE PREFERENCE AND NOTHING ELSE. A peer may select only from what it was offered
 * (RFC 7301 3.1), so a case chooses what a connection speaks by the preference it gives this peer and
 * the offer the client makes: "h2" first selects HTTP/2 wherever it is offered, "http/1.1" first sends
 * a client offering both to the HTTP/1.1 fallback, and no overlap selects nothing at all.
 *
 * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev root
 * CA for every test binary and the test server certificate is issued for that name.
 */

namespace utest
{
    namespace h2peer
    {
        /**
         * @brief class TlsHttp2TestServerT - the peer of design 8.2 over TLS
         *
         * Choosing "h2" is the SERVER half of ALPN, and crypto::CryptoBase::setAlpnServerPreference is
         * its entry point. Everything else is the cleartext peer - which is the point, since
         * TcpServerBase is what performs the server side handshake and Http2TestConnectionT is written
         * against a stream policy
         */

        template
        <
            typename E = void
        >
        class TlsHttp2TestServerT :
            public Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( TlsHttp2TestServerT )

        public:

            typedef Http2TestServerT< bl::tasks::TcpSslSocketAsyncBase >        base_type;

        protected:

            TlsHttp2TestServerT(
                SAA_in          const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >& controlToken,
                SAA_in          const std::vector< std::string >&                preference
                )
                :
                base_type(
                    controlToken,
                    "localhost",
                    0U /* ephemeral */,
                    test::UtfCrypto::getDefaultServerKey(),
                    test::UtfCrypto::getDefaultServerCertificate()
                    )
            {
                /*
                 * The base constructor has built the server context by now - initServerContext( )
                 * runs from TcpServerBase's own constructor when the policy needs a handshake
                 */

                UTF_REQUIRE( nullptr != base_type::m_serverContext.get() );

                bl::crypto::CryptoBase::setAlpnServerPreference(
                    *base_type::m_serverContext,
                    preference
                    );
            }
        };

        typedef bl::om::ObjectImpl< TlsHttp2TestServerT<> >                     TlsHttp2TestServer;

        inline auto makeTlsPeer( SAA_in const std::vector< std::string >& preference )
            -> bl::om::ObjPtr< TlsHttp2TestServer >
        {
            using namespace bl::tasks;

            const auto controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            return TlsHttp2TestServer::createInstance<>( controlToken, preference );
        }

    } // h2peer

} // utest

#endif /* __UTEST_HTTP2TLSTESTSERVER_H_ */
