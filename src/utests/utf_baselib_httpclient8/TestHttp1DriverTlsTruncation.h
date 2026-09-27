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

#ifndef __UTEST_TESTHTTP1DRIVERTLSTRUNCATION_H_
#define __UTEST_TESTHTTP1DRIVERTLSTRUNCATION_H_

#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Http1DriverTlsTestUtils.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * D1 - A TLS TRUNCATION MUST NOT COMPLETE A CLOSE-DELIMITED BODY (astra's second review, R01)
 *
 * WHAT THE DEFECT IS. A response with neither Content-Length nor chunked framing is delimited by
 * the connection closing (RFC 9112 section 6.3), so whether it is COMPLETE is decided entirely by
 * how the byte stream ended. Over TLS there are two endings a client can tell apart: the peer's
 * close_notify, which asio reports to a read as eof, and a transport which simply ended, which it
 * reports as a truncation - engine::map_error_code( ) turns the eof into asio.ssl.stream:1 exactly
 * when no close_notify was received. The driver's isCleanEndOfStream( ) admitted both, so a
 * close-delimited response cut short by anyone able to end the transport was delivered to the
 * caller as a complete success. RFC 9112 section 9.8 is explicit: "A response that has neither
 * chunked transfer coding nor Content-Length is complete only if a valid closure alert has been
 * received."
 *
 * WHAT THE CASES ASSERT, AND AT WHICH LAYER. The decision is about what a CALLER is told, so the
 * cases run a real HttpClientRequestTask with a streaming BodySink over the real HTTP/1.1 driver on
 * the TLS stranded policy, against a real TLS peer. They assert the request's result - failed or
 * not, and with which code - and whether the sink was told the body is complete, which are the two
 * things a caller sees. The driver's stream event alone would not do: the request task is what
 * turns that event into onComplete( ) or a failure.
 *
 * WHAT THEY DELIBERATELY DO NOT ASSERT IS THE DRIVER TASK'S OWN ENDING, and that is a measured
 * property of the tree rather than a gap in the cases. After a peer's truncation the driver closes
 * the connection and its TLS finish continuation sends a close_notify and waits for the peer's - and
 * that wait is parked on a socket whose end of stream the truncating read has already consumed:
 * asio reports a zero-octet recv as done_and_exhausted and stops trying that descriptor's reads
 * speculatively (Boost 1.90, detail/reactive_socket_recv_op.hpp and detail/impl/epoll_reactor.ipp),
 * so the edge-triggered reactor waits for an event which never comes, and only the 60 second
 * protocol timer ends the task - failed, as a cancel. That is the same on both sides of D1 and is
 * not D1's to change. The cases therefore wait for the peer's script to finish - which it does only
 * once the driver has observed the ending and sent its close_notify - and then cancel the driver
 * themselves, so the module does not spend a minute per case on it
 *
 * THE FOUR ENDINGS. The red case is the one D1 changes: a close-delimited response ended by the
 * peer's transport with no close_notify, and no local cancel anywhere near it. The three controls
 * pin what D1 must NOT change - the same response ended by a close_notify, and a Content-Length and
 * a chunked response ended by a truncation AFTER the message was already complete. A length or a
 * last chunk delimits its own message, so the ending that follows it is about the connection and
 * not about the response.
 *
 * THE POOL IS ONE CONNECTION. The request task asks a ConnectionPool for its connection and hands
 * the stream slot back to it, and nothing about either is under test here - so a test pool answers
 * every acquire( ) with the one driver the case established, posted as the contract requires. That
 * keeps the session, the real pool and the HTTP/2 driver out of this module's object entirely.
 *
 * See notes/plans/issues/astra-second-review-decisions.md section 3, D1.
 */

namespace utest
{
    namespace h1tlstrunc
    {
        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,
        };

        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        typedef bl::om::ObjectImpl
        <
            bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >
        >
        TlsEstablisherImpl;

        typedef bl::tasks::Http1ConnectionTaskImpl< tls_stream_t >              TlsDriverImpl;

        /*
         * The TLS peer lives in utests/baselib/Http1DriverTlsTestUtils.h, which two CS-1 modules share
         */

        using http1drivertls::TlsPeer;

        /*************************************************************************
         * Establishing the driver the way the session's fallback does
         */

        inline auto makeTlsKey( SAA_in const bl::os::port_t port ) -> bl::httpclient::ConnectionKey
        {
            bl::httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = "localhost";
            key.port = port;

            return key;
        }

        inline auto makeTlsRequest(
            SAA_in          const bl::os::port_t                                port,
            SAA_in          const std::string&                                  target
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( std::string( "GET" ) );

            request.url(
                bl::net::Uri::parse(
                    "https://localhost:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        inline auto makeTlsFactory(
            SAA_in          const std::shared_ptr< bl::om::ObjPtr< bl::httpclient::ClientConnection > >& slot
            )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< tls_stream_t > >
        {
            typedef bl::httpclient::ClientDriverFactoryT< tls_stream_t >        factory_t;

            auto factory = std::make_shared< factory_t >();

            /*
             * HTTP/1.1 ALONE, so that a peer which somehow selected "h2" fails loudly in
             * createDriver( ) rather than quietly running a case which is not the one it claims
             */

            factory -> registerDriver(
                bl::httpclient::HttpProtocol::Http11,
                [ slot ](
                    SAA_in      const bl::httpclient::NegotiatedProtocol&       negotiated,
                    SAA_inout   tls_stream_t::stream_ref&&                      connectedStream,
                    SAA_in      const bl::httpclient::ConnectionKey&            key
                    )
                    -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    auto driver = TlsDriverImpl::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        bl::cpp::copy( key )
                        );

                    auto result = bl::om::qi< bl::httpclient::ClientConnection >( driver );

                    *slot = bl::om::copy( result );

                    return result;
                }
                );

            return factory;
        }

        inline auto establishTlsDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
        {
            using namespace bl;
            using namespace bl::tasks;

            const auto slot =
                std::make_shared< om::ObjPtr< httpclient::ClientConnection > >();

            const auto establisher = TlsEstablisherImpl::createInstance(
                makeTlsKey( port ),
                makeTlsFactory( slot ),
                ProxyConfig::none(),
                ClientConnectionConfig(),
                false /* logExceptions */
                );

            const auto establisherTask = om::qi< Task >( establisher );

            eq -> push_back( establisherTask );
            eq -> wait( establisherTask );

            utest::http1driver::chkTaskSucceeded( establisherTask );

            UTF_REQUIRE( nullptr != slot -> get() );

            return om::copy( *slot );
        }

        /**
         * @brief A pool of exactly one connection - the driver the case established
         *
         * acquire( ... ) POSTS its answer, as the contract requires: a pool which answered inline
         * would be calling into the request task from inside its own call. releaseStream( ... ) is
         * recorded, so that a failure message can say what the request task reported
         */

        class OneConnectionPool : public bl::httpclient::ConnectionPool
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( OneConnectionPool, bl::httpclient::ConnectionPool )

        protected:

            const bl::om::ObjPtrCopyable< bl::httpclient::ClientConnection >    m_connection;

            mutable bl::os::mutex                                               m_lock;
            std::vector< std::string >                                          m_events;

            OneConnectionPool( SAA_in const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection )
                :
                m_connection( connection )
            {
            }

            void record( SAA_in std::string&& what )
            {
                BL_MUTEX_GUARD( m_lock );

                m_events.push_back( BL_PARAM_FWD( what ) );
            }

        public:

            auto events() const -> std::vector< std::string >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_events;
            }

            virtual void acquire(
                SAA_in          const bl::httpclient::ConnectionKey&            key,
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          on_ready_callback_t&&                           onReady
                ) OVERRIDE
            {
                BL_UNUSED( key );
                BL_UNUSED( request );

                record( "acquire" );

                const auto connection = m_connection;
                const on_ready_callback_t callback( BL_PARAM_FWD( onReady ) );

                bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::GeneralPurpose ) ->
                    aioService().post(
                        [ connection, callback ]() -> void
                        {
                            callback( connection, nullptr );
                        }
                        );
            }

            virtual void releaseStream(
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection,
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::httpclient::RequestOutcome            outcome
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                BL_UNUSED( connection );
                BL_UNUSED( handle );

                record(
                    "release:" + bl::utils::lexical_cast< std::string >( static_cast< unsigned >( outcome ) )
                    );

                BL_NOEXCEPT_END()
            }
        };

        typedef bl::om::ObjectImpl< OneConnectionPool >                        OneConnectionPoolImpl;

        /**
         * @brief The caller's streaming sink - it keeps every byte and counts onComplete( )
         *
         * onComplete( ) is THE statement this suite is about: "the body is complete; no further
         * onData( ... ) will follow" (ClientTypes.h). Counted rather than flagged, so that a case can
         * also say it was not told twice
         */

        class RecordingBodySink : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( RecordingBodySink, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;
            std::string                                                         m_body;
            std::size_t                                                         m_completions;

            RecordingBodySink()
                :
                m_completions( 0U )
            {
            }

        public:

            virtual std::size_t onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                const auto size = data -> size() - data -> offset1();

                m_body.append(
                    reinterpret_cast< const char* >( data -> pv() ) + data -> offset1(),
                    size
                    );

                return size;
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_completions;
            }

            auto body() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_body;
            }

            std::size_t completions() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_completions;
            }
        };

        typedef bl::om::ObjectImpl< RecordingBodySink >                        RecordingBodySinkImpl;

        /**
         * @brief What one request over the TLS driver came to, read on the test thread
         */

        struct TlsRequestResult
        {
            bool                                                                requestEnded;
            bool                                                                requestFailed;
            std::string                                                         requestFailure;
            bl::eh::error_code                                                  requestCode;
            bool                                                                requestRetryable;
            unsigned                                                            status;

            std::string                                                         body;
            std::size_t                                                         completions;

            bool                                                                driverEnded;
            bool                                                                driverFailed;
            std::string                                                         driverFailure;

            std::string                                                         poolEvents;
            std::string                                                         peerRecords;

            TlsRequestResult()
                :
                requestEnded( false ),
                requestFailed( false ),
                requestRetryable( false ),
                status( 0U ),
                completions( 0U ),
                driverEnded( false ),
                driverFailed( false )
            {
            }

            /**
             * @brief Everything above, for a failure message which has to say WHICH outcome it got
             */

            auto describe() const -> std::string
            {
                return
                    "request " +
                    ( requestFailed ? requestFailure : std::string( "succeeded" ) ) +
                    ", code " +
                    TlsPeer::describe( requestCode ) +
                    ", status " +
                    bl::utils::lexical_cast< std::string >( status ) +
                    ", body " +
                    bl::str::quoteString( body ) +
                    ", onComplete x" +
                    bl::utils::lexical_cast< std::string >( completions ) +
                    ", driver (cancelled by the case) " +
                    ( driverFailed ? driverFailure : std::string( "clean" ) ) +
                    ", pool " +
                    poolEvents +
                    ", peer " +
                    peerRecords;
            }
        };

        /**
         * @brief One GET over a fresh TLS driver, answered by 'script', through a request task
         *
         * THE REQUEST TASK IS THE SUBJECT AND THE DRIVER IS THE MEANS. The case waits for the
         * request task, because its result and its sink are what the caller sees. It then waits for
         * the peer's script to end, which is the rendezvous with the driver having observed the
         * ending and closed, and only then cancels the driver and waits for it - for the reason
         * the header gives, the driver is not left to end on its own. Every wait is bounded, so a
         * regression which loses an ending fails the case with a diagnosis instead of hanging the
         * module. How the driver ended is reported in the failure text and not asserted
         */

        inline auto runTlsRequest(
            SAA_in          TlsPeer::script_t&&                                 script,
            SAA_in          const std::string&                                  target
            )
            -> TlsRequestResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            TlsRequestResult result;

            TlsPeer peer( BL_PARAM_FWD( script ) );

            const auto bodySink = RecordingBodySinkImpl::createInstance();

            om::ObjPtr< OneConnectionPoolImpl > pool;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishTlsDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    pool = OneConnectionPoolImpl::createInstance( driver );

                    const auto requestImpl = httpclient::HttpClientRequestTaskImpl::createInstance(
                        makeTlsRequest( peer.port(), target ),
                        makeTlsKey( peer.port() ),
                        om::qi< httpclient::ConnectionPool >( pool ),
                        httpclient::HttpClientRequestConfig(),
                        om::ObjPtrCopyable< httpclient::BodySink >(
                            om::qi< httpclient::BodySink >( bodySink )
                            )
                        );

                    const auto requestTask = om::qi< Task >( requestImpl );

                    eq -> push_back( requestTask );

                    result.requestEnded = waitForTaskEnd(
                        requestTask,
                        static_cast< std::size_t >( WAIT_IN_MILLISECONDS )
                        );

                    chkOrFail(
                        result.requestEnded,
                        "the request task never ended; peer " + joinEvents( peer.records() )
                        );

                    result.requestFailed = requestTask -> isFailed();
                    result.requestFailure = taskFailureText( requestTask );

                    /*
                     * GUARDED, because the helper rethrows to find the code and a null
                     * exception_ptr is a BL_RIP_MSG there (core/CPP.h) - which is what a request
                     * that succeeded has
                     */

                    const auto requestException = requestTask -> exception();

                    if( requestException )
                    {
                        result.requestCode = eh::errorCodeFromExceptionPtr( requestException );
                    }

                    result.requestRetryable = requestImpl -> isRetryable();
                    result.status = requestImpl -> response().status();

                    chkOrFail(
                        peer.waitForScriptEnd(),
                        "the peer's script never ended, so the driver never answered the ending; "
                            "peer " + joinEvents( peer.records() )
                        );

                    driverTask -> requestCancel();

                    result.driverEnded = waitForTaskEnd(
                        driverTask,
                        static_cast< std::size_t >( WAIT_IN_MILLISECONDS )
                        );

                    chkOrFail(
                        result.driverEnded,
                        "the driver task did not end even when cancelled; peer " +
                            joinEvents( peer.records() )
                        );

                    result.driverFailed = driverTask -> isFailed();
                    result.driverFailure = taskFailureText( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            result.body = bodySink -> body();
            result.completions = bodySink -> completions();
            result.peerRecords = joinEvents( peer.records() );

            if( pool )
            {
                result.poolEvents = joinEvents( pool -> events() );
            }

            return result;
        }

        /**
         * @brief The script every case shares - one request head read, one response sent, and the
         * stream ended the way the case names
         */

        enum class Ending
        {
            /**
             * @brief The transport's FIN with no close_notify - the truncation
             */

            Truncation,

            /**
             * @brief A close_notify, answered by the client's own
             */

            CloseNotify,
        };

        inline auto makeScript(
            SAA_in          const std::string&                                  response,
            SAA_in          const Ending                                        ending
            )
            -> TlsPeer::script_t
        {
            return [ response, ending ](
                SAA_inout   TlsPeer&                                            self,
                SAA_inout   TlsPeer::sslstream_t&                               stream
                ) -> void
            {
                const auto head = TlsPeer::readRequestHead( stream );

                self.record( "head:" + TlsPeer::requestLineOf( head ) );

                TlsPeer::send( stream, response );

                if( Ending::CloseNotify == ending )
                {
                    /*
                     * The alert follows the response on the same stream, so the client reads every
                     * octet of the body before the ending, and then waits for the client's answer
                     */

                    const auto ec = TlsPeer::endWithCloseNotify( stream );

                    self.record( "ended-with-close-notify:" + TlsPeer::describe( ec ) );

                    return;
                }

                TlsPeer::endWithoutCloseNotify( stream );

                self.record( "ended-without-close-notify" );

                /*
                 * THE CLIENT'S ANSWER, READ RATHER THAN CLOSED ON. Whatever the driver decides
                 * about the response, it then closes the connection itself, and over TLS that
                 * close sends a close_notify of its own - which this reads, so the peer closes last
                 * and puts no reset on the wire. eof here means the client closed in an orderly way
                 */

                const auto ec = TlsPeer::observeStreamEnd( stream );

                self.record( "client-ended:" + TlsPeer::describe( ec ) );
            };
        }

        /**
         * @brief What a request which must SUCCEED - the three controls - asserts
         */

        inline void chkSucceeded(
            SAA_in          const TlsRequestResult&                             result,
            SAA_in          const std::string&                                  which
            )
        {
            using utest::http1driver::chkOrFail;

            chkOrFail( ! result.requestFailed, which + ": the request failed. " + result.describe() );

            chkOrFail( 200U == result.status, which + ": the status is not 200. " + result.describe() );

            chkOrFail(
                std::string( "part-one" ) == result.body,
                which + ": the sink does not hold the whole body. " + result.describe()
                );

            chkOrFail(
                1U == result.completions,
                which + ": the sink was not told the body is complete exactly once. " + result.describe()
                );
        }

    } // h1tlstrunc

} // utest

/**
 * @brief THE RED - a close-delimited response the peer ends with no close_notify, and no local
 * cancel: the request FAILS and the sink is NOT told the body is complete
 *
 * The peer answers with no Content-Length, so the close is the only framing there is, sends one
 * body chunk and then shuts its transport's send side down under the TLS session. The client's read
 * takes the chunk and then the truncation. Nothing on the client side cancels, closes or times out:
 * the ending is the peer's, and it is exactly the ending RFC 9112 section 9.8 says does not complete
 * such a message.
 *
 * WHAT EACH ASSERTION REFUSES.
 *
 *   - the request failed, with the TRUNCATION'S OWN CODE - the decision is that a truncation takes
 *     onPeerClosed( )'s unclean branch exactly as a reset does, and that branch finishes the stream
 *     with the ending's code. It is the code the request task was handed, carried on its exception
 *   - and it is not retryable: the request went out, so a replay would be a second request
 *   - the sink holds 'part-one' - what arrived is still delivered - and was told onComplete( ) NOT
 *     AT ALL, which is the caller's whole view of "this body is complete"
 *
 * RED ON THE UNFIXED DRIVER, where isCleanEndOfStream( ) admits the truncation, parseEof( ) completes
 * the body and the request task reports a 200 with a complete body.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_TruncatedCloseDelimitedResponseFailsTheRequestTests )
{
    using namespace bl;
    using namespace utest::h1tlstrunc;
    using utest::http1driver::chkOrFail;

    const auto result = runTlsRequest(
        makeScript(
            "HTTP/1.1 200 OK\r\n"
            "\r\n"
            "part-one",
            Ending::Truncation
            ),
        "/close-delimited-truncated"
        );

    const std::string which( "a close-delimited response ended without close_notify" );

    chkOrFail(
        result.requestFailed,
        which + ": the request was reported as a SUCCESS. " + result.describe()
        );

    chkOrFail(
        TlsPeer::isTruncated( result.requestCode ),
        which + ": the request did not fail with the truncation's own code. " + result.describe()
        );

    chkOrFail(
        ! result.requestRetryable,
        which + ": a request which went out was reported retryable. " + result.describe()
        );

    chkOrFail(
        std::string( "part-one" ) == result.body,
        which + ": the body which did arrive is not what the sink holds. " + result.describe()
        );

    chkOrFail(
        0U == result.completions,
        which + ": the sink was told the body is COMPLETE. " + result.describe()
        );
}

/**
 * @brief CONTROL - the same close-delimited response ended by a close_notify, which completes it
 *
 * The one ending RFC 9112 section 9.8 accepts for a message framed by the close. Green on both sides
 * of D1: it is what keeps the fix from overreaching into the orderly close, which must go on
 * completing close-delimited HTTPS responses exactly as it did.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_CloseNotifyCompletesACloseDelimitedResponseTests )
{
    using namespace bl;
    using namespace utest::h1tlstrunc;

    const auto result = runTlsRequest(
        makeScript(
            "HTTP/1.1 200 OK\r\n"
            "\r\n"
            "part-one",
            Ending::CloseNotify
            ),
        "/close-delimited-close-notify"
        );

    chkSucceeded( result, "a close-delimited response ended with close_notify" );
}

/**
 * @brief CONTROL - a Content-Length response, then a truncation: the message was already complete
 *
 * A length delimits its own message, so the response completes on its last octet and the truncation
 * which follows it finds no message in flight - it ends an idle keep-alive connection, which is what
 * it always did. Green on both sides of D1.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_TruncationAfterAContentLengthResponseSucceedsTests )
{
    using namespace bl;
    using namespace utest::h1tlstrunc;

    const auto result = runTlsRequest(
        makeScript(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 8\r\n"
            "\r\n"
            "part-one",
            Ending::Truncation
            ),
        "/content-length-then-truncated"
        );

    chkSucceeded( result, "a Content-Length response followed by a truncation" );
}

/**
 * @brief CONTROL - a chunked response, then a truncation: the last chunk already completed it
 *
 * The chunked twin of the case above, and for the same reason green on both sides of D1.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_TruncationAfterAChunkedResponseSucceedsTests )
{
    using namespace bl;
    using namespace utest::h1tlstrunc;

    const auto result = runTlsRequest(
        makeScript(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "8\r\n"
            "part-one\r\n"
            "0\r\n"
            "\r\n",
            Ending::Truncation
            ),
        "/chunked-then-truncated"
        );

    chkSucceeded( result, "a chunked response followed by a truncation" );
}

#endif /* __UTEST_TESTHTTP1DRIVERTLSTRUNCATION_H_ */
