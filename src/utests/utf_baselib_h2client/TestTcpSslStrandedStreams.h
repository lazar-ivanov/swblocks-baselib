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

#ifndef __UTEST_TESTTCPSSLSTRANDEDSTREAMS_H_
#define __UTEST_TESTTCPSSLSTRANDEDSTREAMS_H_

/*
 * LoopbackPeerBase, StrandedFullDuplexProbeT and both case bodies live in
 * TestTcpStrandedStreams.h, in the named namespace utest::strandedstreams. They are INCLUDED
 * rather than copied - a second copy of a helper in a named namespace of the same module is an
 * ODR violation rather than mere duplication (src/utests/AGENTS.md, invariant C6) - and they were
 * written as templates on the stream policy precisely so that this header adds a TLS peer and
 * nothing else
 *
 * That is also the claim these cases make: "S3.3 is S3.2 but over TLS" is true of the DELIVERED
 * policies and not only of the prose, because the task, the probe, the assertions and both case
 * bodies here are literally the same code running over TcpSslSocketAsyncStrandedBase
 */

#include "TestTcpStrandedStreams.h"

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpSslBaseTasks.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

namespace utest
{
    namespace strandedstreams
    {
        /**
         * @brief The TLS echo peer - the cleartext one with a handshake in front of it
         *
         * The server side of the handshake is driven synchronously from the test thread, exactly
         * as the TLS peer of utf_baselib_http2 does it, and the handshaken stream is kept alive by
         * the peer rather than by the accepting call so the client is never raced by the socket
         * going away underneath it
         *
         * The shutdown observable reads the LOWEST layer rather than the TLS stream. What the
         * stranded cancelTask() does is shut the socket down - there is no close_notify, because
         * a forcefully shut down stream reports isShutdownNeeded() false - so the end of stream to
         * look for is the raw one
         */

        class TlsEchoLoopbackPeer :
            public LoopbackPeerBase
        {
            BL_NO_COPY_OR_MOVE( TlsEchoLoopbackPeer )

        public:

            typedef bl::asio::ssl::stream< bl::asio::ip::tcp::socket >          sslstream_t;

            TlsEchoLoopbackPeer()
                :
                m_serverContext(
                    bl::crypto::CryptoBase::createAsioSslServerContext(
                        test::UtfCrypto::getDefaultServerKey(),
                        test::UtfCrypto::getDefaultServerCertificate()
                        )
                    )
            {
            }

            /**
             * @brief Accepts one connection, completes the handshake and then holds it open
             */

            void acceptAndIdle()
            {
                acceptAndHandshake();
            }

            /**
             * @brief Accepts one connection, completes the handshake and echoes exactly
             * totalBytes back
             */

            void runEchoSession( SAA_in const std::size_t totalBytes )
            {
                acceptAndHandshake();

                echoExactly( *m_stream, totalBytes );
            }

            /**
             * @brief Whether the client has shut the connection down, within the given deadline
             */

            bool sawClientShutdownWithin( SAA_in const bl::time::time_duration& timeout )
            {
                BL_ASSERT( m_stream );

                return readsEofWithin( m_stream -> next_layer(), timeout );
            }

        private:

            void acceptAndHandshake()
            {
                m_stream = bl::cpp::SafeUniquePtr< sslstream_t >::attach(
                    new sslstream_t( m_ioService, *m_serverContext )
                    );

                acceptOne( m_stream -> next_layer() );

                bl::eh::error_code ec;

                m_stream -> handshake( bl::asio::ssl::stream_base::server, ec );

                UTF_REQUIRE_EQUAL( bl::eh::error_code(), ec );
            }

            bl::cpp::SafeUniquePtr< bl::asio::ssl::context >                    m_serverContext;
            bl::cpp::SafeUniquePtr< sslstream_t >                               m_stream;
        };

    } // strandedstreams

} // utest

UTF_AUTO_TEST_CASE( TcpSslStrandedStreams_TlsFullDuplexTests )
{
    using namespace utest::strandedstreams;

    /*
     * S3.3's acceptance: a concurrent read, write and timer over TLS, with Asio's internal SSL
     * handlers serialized. Those handlers are the point - asio::ssl::stream's read and write are
     * composed operations over one shared engine, and the only thing which can serialize their
     * intermediate handlers is the executor the stream was constructed on
     *
     * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev
     * root CA as a trusted root for every test binary, and the test server certificate is issued
     * for that name
     */

    TlsEchoLoopbackPeer peer;

    runFullDuplexCase
        <
            StrandedFullDuplexProbeT< bl::tasks::TcpSslSocketAsyncStrandedBase >
        >
        ( peer, std::string( "localhost" ) );
}

UTF_AUTO_TEST_CASE( TcpSslStrandedStreams_TlsCancelPostsShutdownToStrandTests )
{
    using namespace utest::strandedstreams;

    /*
     * The other half of S3.3, and the same case body as the cleartext one: cancelTask() posts the
     * forced shutdown to the strand rather than calling into the socket from whichever thread
     * requested the cancel
     */

    TlsEchoLoopbackPeer peer;

    runCancelPostsShutdownCase
        <
            StrandedFullDuplexProbeT< bl::tasks::TcpSslSocketAsyncStrandedBase >
        >
        ( peer, std::string( "localhost" ) );
}

#endif /* __UTEST_TESTTCPSSLSTRANDEDSTREAMS_H_ */
