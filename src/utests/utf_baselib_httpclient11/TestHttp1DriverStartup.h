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

#ifndef __UTEST_TESTHTTP1DRIVERSTARTUP_H_
#define __UTEST_TESTHTTP1DRIVERSTARTUP_H_

#include <baselib/core/BaseIncludes.h>

#include <string>

#include <utests/baselib/Http1DriverStartupHook.h>
#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>


/************************************************************************
 * D2 OVER THE CLEARTEXT STRANDED POLICY
 *
 * The race D2 closes is a TLS one - two threads in one OpenSSL engine - but the ORDERING it rests on
 * is the driver's and not the transport's: a request's write must never start before the first
 * read's start has returned, and a cancel which lands before the start must end the connection.
 * Both are properties of the start handler, so both are pinned here as well as over TLS, and here
 * they cost no TLS peer.
 */

/**
 * @brief D2-a - NO WRITE STARTS BEFORE THE FIRST READ'S START HAS RETURNED
 *
 * The hook holds the first read's start; a request is submitted while it is held; the record must
 * read begin, end, write-start. See runBarrierExchange( ) for how the hold is released without a
 * race on either tree.
 *
 * RED ON THE UNFIXED DRIVER, deterministically: scheduleTask( ) publishes m_started before it arms
 * the read, so the submit made while the read's start is held posts onStartRequest( ) to a strand
 * nothing holds, the probe behind it runs only after the write has started, and the record is
 * begin, write-start, end on every run. On the fixed driver the start handler holds the strand
 * through the read's start and starts the request itself afterwards.
 */

UTF_AUTO_TEST_CASE( Http1Driver_NoWriteStartsBeforeTheFirstReadStartReturnsTests )
{
    using namespace bl;
    using namespace utest::http1driver;
    using namespace utest::http1startup;

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            const auto request = ScriptedPeer::readRequest( socket );

            self.record( ScriptedPeer::requestLineOf( request ) );

            ScriptedPeer::send(
                socket,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
                );

            /*
             * Held open until the case has read everything it asserts on; the peer then ends the
             * connection by closing, which the driver's idle read ends on
             */

            self.waitForRelease();
        }
        );

    const auto port = peer.port();

    const auto result = runBarrierExchange(
        [ port ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq )
            -> om::ObjPtr< httpclient::ClientConnection >
        {
            return establishPlainHookDriver( eq, port );
        },
        makeRequest( port, "/barrier", "GET" ),
        [ &peer ]() -> void
        {
            peer.release();
        }
        );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    chkNoWriteBeforeTheReadStartReturns( result, "cleartext" );
}

/**
 * @brief D2-b - A CANCEL BEFORE THE START ENDS THE CONNECTION, without the idle lifetime or the peer
 *
 * submit( ), cancel( ), the sink told operation_aborted, and only then the push. The peer reads until
 * its stream ends and never ends the connection itself; the driver's idle lifetime is past the bound.
 *
 * RED ON THE UNFIXED DRIVER, deterministically: the run's reset erases the m_closing the cancel set,
 * scheduleTask( ) arms the read and nothing will ever end the task - so the bound expires. On the
 * fixed driver the start handler finds the connection no longer Ready, re-asserts the close, arms
 * nothing, and its epilog takes the terminal on its first strand turn. The peer's end is eof either
 * way over cleartext - the teardown's shutdown_send - so over this transport the task's ending is the
 * discriminator on its own
 */

UTF_AUTO_TEST_CASE( Http1Driver_CancelBeforeStartEndsTheConnectionTests )
{
    using namespace bl;
    using namespace utest::http1driver;
    using namespace utest::http1startup;

    ScriptedPeer peer(
        []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
        {
            char buffer[ 1024 ];

            eh::error_code ec;

            for( ;; )
            {
                ( void ) socket.read_some( asio::buffer( buffer, sizeof( buffer ) ), ec );

                if( ec )
                {
                    break;
                }
            }

            self.record(
                asio::error::eof == ec ?
                    std::string( "peer-end:eof" )
                    :
                    "peer-end:" + std::string( ec.category().name() ) + ":" +
                        utils::lexical_cast< std::string >( ec.value() )
                );
        }
        );

    const auto port = peer.port();

    const auto result = runCancelBeforeStart(
        [ port ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq )
            -> om::ObjPtr< httpclient::ClientConnection >
        {
            return establishPlainHookDriver(
                eq,
                port,
                time::seconds( static_cast< long >( IDLE_LIFETIME_IN_SECONDS ) )
                );
        },
        makeRequest( port, "/cancelled", "GET" )
        );

    chkOrFail( peer.waitForRecords( 1U ), "the peer never saw its stream end" );

    UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

    chkCancelBeforeStartEndedTheConnection( result, "cleartext" );

    chkOrFail(
        std::string( "peer-end:eof" ) == peer.records().front(),
        "cleartext: the peer did not see an orderly end of stream: " + joinEvents( peer.records() )
        );
}

#endif /* __UTEST_TESTHTTP1DRIVERSTARTUP_H_ */
