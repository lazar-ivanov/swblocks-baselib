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

#ifndef __UTEST_TESTCLIENTSESSIONTLSNARROWING_H_
#define __UTEST_TESTCLIENTSESSIONTLSNARROWING_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/SimpleTaskControlToken.h>
#include <baselib/tasks/Task.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Http2TlsTestServer.h>
#include <utests/baselib/HttpClientSessionTlsTestUtils.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * The narrowing of a streaming upload's ALPN offer, shown to DISCRIMINATE - E1 of astra's second
 * review's change-set CS-2 ( the L6 review, second pass, and the owed list's E1 )
 *
 * THE RULE UNDER TEST. The HTTP/1.1 driver refuses every request carrying a BodySource, so a session
 * whose transport negotiates routes such a request to a key of its own, and its connection factory
 * narrows the ALPN offer for that key to "h2" alone ( ClientSessionT::narrowToHttp2( ) ). A peer may
 * select only from what it was offered ( RFC 7301 3.1 ), so that connection cannot be HTTP/1.1.
 *
 * WHY THE CASE WHICH EXISTED PINS LESS THAN THIS. ClientSessionTls_StreamingUploadTakesAnHttp2Only
 * ConnectionTests ( utf_baselib_httpclient5 ) pins the KEY SPLIT - a second connection for the
 * upload - but its peer prefers [ "h2", "http/1.1" ], so it selects h2 whatever it is offered, and
 * deleting narrowToHttp2( ) leaves it green. The case below offers the choice to a peer which
 * PREFERS http/1.1: with the narrowing it can only select h2 and the upload succeeds; without it it
 * selects http/1.1, and the HTTP/1.1 driver refuses the source. Its discrimination was shown with a
 * local probe which made narrowToHttp2( ) do nothing - red with the probe, green without.
 *
 * The session helpers are utest::tlssession's, which is this module's other header and is included
 * before this one ( the append convention in the module's Main.cpp ).
 */

namespace utest
{
    namespace tlssession
    {
        /**
         * @brief The TLS peer of design 8.2 with an ALPN preference -
         * utests/baselib/Http2TlsTestServer.h's, the one peer of this kind in the tree
         */

        using h2peer::makeTlsPeer;

        /**
         * @brief The rewindable streaming upload - utests/baselib/HttpClientSessionTlsTestUtils.h's
         * StringBodySource, under the name this case was written against
         */

        typedef sessiontls::StringBodySource                                    UploadSource;

    } // tlssession

} // utest

/**
 * @brief A streaming PUT, alone, to a peer which PREFERS http/1.1 - carried over h2, because the
 * session offered that request h2 alone
 *
 * ALONE, so that no earlier request has already opened a connection for the origin: the upload's
 * own key is the only one there is, and the connection it gets is the one its narrowed offer built.
 *
 * GREEN: the peer, offered only "h2", selects it; the upload is carried and answered. RED WITHOUT
 * THE NARROWING, measured with a probe which made narrowToHttp2( ) do nothing: offered both, the
 * peer selects its preference, http/1.1, the connection falls back to the HTTP/1.1 driver, and that
 * driver refuses the source - so the request fails.
 */

UTF_AUTO_TEST_CASE( ClientSessionTls_StreamingUploadAloneIsOfferedHttp2OnlyTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest;
    using namespace utest::tlssession;

    const std::string upload( "streamed-payload" );

    std::vector< std::string > preference;

    preference.push_back( "http/1.1" );
    preference.push_back( "h2" );

    const auto peer = makeTlsPeer( preference );

    peer -> setResponder(
        []( SAA_in const h2peer::Http2TestRequest& request ) -> h2peer::Http2ResponseScript
        {
            return h2peer::Http2ResponseScript()
                .headers( 200U )
                .data( request.path )
                .endStream();
        }
        );

    h2driver::withPeer(
        peer,
        [ & ]( SAA_in const unsigned short port ) -> void
        {
            const auto session = makeSession();

            BL_SCOPE_EXIT_WARN_ON_FAILURE(
                {
                    session -> dispose();
                },
                "utest::tlssession::ClientSessionTls_StreamingUploadAloneIsOfferedHttp2OnlyTests"
                );

            auto request = makeRequest( port, "/streamed", "PUT" );

            request.bodySource(
                om::ObjPtrCopyable< httpclient::BodySource >(
                    om::qi< httpclient::BodySource >(
                        UploadSource::createInstance( cpp::copy( upload ) )
                        )
                    )
                );

            const auto requestTask = session -> createRequestTask( request );

            const auto task = om::qi< Task >( requestTask );

            runSessionTask( task );

            requireTaskSucceeded( task );

            UTF_REQUIRE_EQUAL( requestTask -> response().status(), 200U );
            UTF_REQUIRE_EQUAL( bodyOf( requestTask -> response() ), std::string( "/streamed" ) );

            /*
             * THE SELECTION, asserted: the peer chose h2 although it prefers http/1.1, which it can
             * only have done because h2 was all it was offered
             */

            UTF_REQUIRE( httpclient::HttpProtocol::Http2 == requestTask -> response().protocol() );
            UTF_REQUIRE_EQUAL( requestTask -> response().negotiatedAlpn(), std::string( "h2" ) );

            /*
             * One connection and one dispatch: the rider rode the h2 preface and was carried, with
             * nothing bounced
             */

            const auto stats = statsOf( session );

            UTF_REQUIRE_EQUAL( stats.connectionsCreated.value(), 1U );
            UTF_REQUIRE_EQUAL( stats.dispatched.value(), 1U );

            /*
             * And the upload really arrived - waited for on the record the peer makes at the end of
             * the request body, because it answers on the HEADERS and may do so before it has
             * processed a single DATA frame ( the rendezvous ClientSessionTls_StreamingUploadTakesAn
             * Http2OnlyConnectionTests found it needs )
             */

            h2driver::requireRecorded(
                peer -> recorder(),
                "end of request body on stream 1, " +
                    utils::lexical_cast< std::string >( upload.size() ) +
                    " bytes"
                );

            UTF_REQUIRE_EQUAL( peer -> recorder().bodyOf( 1U ), upload );

            UTF_REQUIRE( peer -> recorder().failure().empty() );
        }
        );
}

#endif /* __UTEST_TESTCLIENTSESSIONTLSNARROWING_H_ */
