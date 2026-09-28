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

#ifndef __UTEST_TESTHTTP1DRIVERSTARTUPTLS_H_
#define __UTEST_TESTHTTP1DRIVERSTARTUPTLS_H_

#include <baselib/core/BaseIncludes.h>

#include <string>

#include <utests/baselib/Http1DriverStartupHook.h>
#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Http1DriverTlsTestUtils.h>
#include <utests/baselib/Utf.h>


/************************************************************************
 * D2 OVER THE TLS STRANDED POLICY - where the race it closes is real
 *
 * asio::ssl::stream::async_read_some( ) runs its first engine step on the thread which calls it, and
 * async_write( ) does the same for the write; so a write started on the strand while the first read's
 * start runs on the scheduling thread is two threads in one OpenSSL connection. The barrier case holds
 * the read's start AFTER its engine step has finished, so even its red run does not drive the two at
 * once - it only shows that the ordering which would forbid it is not there.
 *
 * THE PEER IS utests/baselib/Http1DriverTlsTestUtils.h's, the tree's one TLS HTTP/1.1 peer. It ends
 * a connection with a close_notify and waits for ours, and never with a bare FIN: a truncation is not
 * what these cases are about, and until CS-6 (I2) the driver's own TLS shutdown then waited the full
 * 60 second protocol timer (measured in D1's module).
 */

/**
 * @brief D2-a over TLS - NO WRITE STARTS BEFORE THE FIRST READ'S START HAS RETURNED
 *
 * The cleartext case's twin; see it and runBarrierExchange( ) for the arrangement. RED ON THE
 * UNFIXED DRIVER, deterministically, for the cleartext case's reason: the record is begin,
 * write-start, end on every run.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_NoWriteStartsBeforeTheFirstReadStartReturnsTests )
{
    using namespace bl;
    using namespace utest::http1drivertls;
    using namespace utest::http1startup;

    TlsPeer peer(
        []( SAA_inout TlsPeer& self, SAA_inout TlsPeer::sslstream_t& stream ) -> void
        {
            const auto head = self.readRequestHead( stream );

            self.record( "head:" + TlsPeer::requestLineOf( head ) );

            TlsPeer::send(
                stream,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
                );

            self.waitForRelease();

            /*
             * THE CONNECTION ENDS HERE, AND CLEANLY: the driver's idle read ends eof on this alert
             * and its own shutdown finds a peer which answers
             */

            const auto ec = TlsPeer::endWithCloseNotify( stream );

            self.record( "ended-with-close-notify:" + TlsPeer::describe( ec ) );
        }
        );

    const auto port = peer.port();

    const auto result = runBarrierExchange(
        [ port ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq )
            -> om::ObjPtr< httpclient::ClientConnection >
        {
            return establishTlsDriver< TlsHookDriverImpl >( eq, port );
        },
        makeTlsRequest( port, "/barrier" ),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    chkNoWriteBeforeTheReadStartReturns( result, "TLS" );
}

/**
 * @brief D2-b over TLS - A CANCEL BEFORE THE START ENDS THE CONNECTION, with our close_notify
 *
 * The cleartext case's twin, and here the peer's side discriminates as well: on the fixed driver the
 * deliberate close runs the TLS close_notify exchange as the task's finish continuation, so the
 * peer's read ends eof - our close_notify - and the peer answers with its own. On the unfixed driver
 * the task never ends on its own, and the case's teardown cancel is a forceful shutdown with no
 * close_notify at all, so the peer sees the truncation and answers nothing.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_CancelBeforeStartEndsTheConnectionTests )
{
    using namespace bl;
    using namespace utest::http1drivertls;
    using namespace utest::http1startup;

    TlsPeer peer(
        []( SAA_inout TlsPeer& self, SAA_inout TlsPeer::sslstream_t& stream ) -> void
        {
            const auto ec = self.observeStreamEnd( stream );

            self.record(
                asio::error::eof == ec ?
                    std::string( "peer-end:eof" )
                    :
                    "peer-end:" + TlsPeer::describe( ec )
                );

            if( asio::error::eof == ec )
            {
                /*
                 * OUR close_notify, ANSWERED WITH ITS OWN - sent at once, since ours has already
                 * been received, and what lets the driver's shutdown complete rather than wait
                 */

                const auto answer = TlsPeer::endWithCloseNotify( stream );

                self.record( "answered:" + TlsPeer::describe( answer ) );
            }
        }
        );

    const auto port = peer.port();

    const auto result = runCancelBeforeStart(
        [ port ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq )
            -> om::ObjPtr< httpclient::ClientConnection >
        {
            return establishTlsDriver< TlsHookDriverImpl >(
                eq,
                port,
                time::seconds( static_cast< long >( IDLE_LIFETIME_IN_SECONDS ) )
                );
        },
        makeTlsRequest( port, "/cancelled" )
        );

    utest::http1driver::chkOrFail(
        peer.waitForScriptEnd(),
        "TLS: the peer's script never ended: " + utest::http1driver::joinEvents( peer.records() )
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    chkCancelBeforeStartEndedTheConnection( result, "TLS" );

    const auto records = peer.records();

    utest::http1driver::chkOrFail(
        ! records.empty() && std::string( "peer-end:eof" ) == records.front(),
        "TLS: the peer did not see our close_notify: " + utest::http1driver::joinEvents( records )
        );
}

#endif /* __UTEST_TESTHTTP1DRIVERSTARTUPTLS_H_ */
