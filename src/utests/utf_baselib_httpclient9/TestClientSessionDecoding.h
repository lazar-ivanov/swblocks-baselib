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

#ifndef __UTEST_TESTCLIENTSESSIONDECODING_H_
#define __UTEST_TESTCLIENTSESSIONDECODING_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ContentDecoder.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/HttpClientSessionTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * What the session decodes, and what it hands back untouched - D5 of astra's second review ( R08 )
 *
 * TWO DEFECTS THE DECODER DEFERRAL CALLED LATENT, and they are live because registerDecoder( ) is
 * public ( notes/plans/issues/astra-second-review-decisions.md, D5 ):
 *
 *   - H24: decodeBody( ) decoded the FIRST Content-Encoding field and then removed all of them, so
 *     a body coded twice came back decoded once and labelled as uncoded;
 *   - H25: continuationTask( ) ran absorbResponse( ) - and so decodeBody( ) - before it looked at
 *     the hop's exception, so failed responses and responses with no content were decoded, and a
 *     decoder's error could take the place of the network failure that really happened.
 *
 * Decided: the Content-Encoding list is read across every field, and a response is decoded only
 * when it carries exactly one coding in total; and only when the hop succeeded and the response can
 * carry content - not a response to HEAD, and not a 204 or a 304. Everything else is handed back
 * with its body and all its headers untouched.
 *
 * WHY OVER HTTP/1.1, AND WHY THIS PEER. Every case here is a statement about header fields the peer
 * writes - two fields of one name, a list in one field, a HEAD answered with a length, a response
 * cut short - and the scripted HTTP/1.1 peer writes them byte for byte. The session and the pool are
 * real, and so is the HTTP/1.1 driver behind the cleartext session's fallback. The transform is
 * "the existing test transform" the decision names, UtestDecoderT, hoisted into
 * utests/baselib/HttpClientSessionTestUtils.h so this module can use it.
 */

namespace utest
{
    namespace plainsession
    {
        typedef bl::tasks::TcpSocketAsyncStrandedBase                           plain_stream_t;

        typedef bl::httpclient::ClientSessionImplT< plain_stream_t >            PlainSessionImpl;

        typedef utest::http1driver::ScriptedPeer                                ScriptedPeer;

        /**
         * @brief A cleartext session with the default configuration - which speaks HTTP/1.1
         */

        inline auto makeSession(
            SAA_in_opt      bl::httpclient::ClientSessionConfig                 config =
                                bl::httpclient::ClientSessionConfig()
            )
            -> bl::om::ObjPtr< PlainSessionImpl >
        {
            return PlainSessionImpl::createInstance( BL_PARAM_FWD( config ) );
        }

        inline auto makeRequest(
            SAA_in          const bl::os::port_t                                port,
            SAA_in_opt      const std::string&                                  target = "/",
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( bl::cpp::copy( method ) );

            request.url(
                bl::net::Uri::parse(
                    "http://127.0.0.1:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        inline void runSessionTask( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );
        }

        inline auto bodyOf( SAA_in const bl::httpclient::ClientResponse& response ) -> std::string
        {
            const auto& block = response.body();

            if( ! block )
            {
                return std::string();
            }

            return std::string(
                block -> begin() + block -> offset1(),
                block -> begin() + block -> size()
                );
        }

        inline auto statsOf( SAA_in const bl::om::ObjPtr< PlainSessionImpl >& session )
            -> bl::httpclient::ConnectionPoolImpl::Stats
        {
            return bl::om::qi< bl::httpclient::ConnectionPoolImpl >( session -> pool() ) -> stats();
        }

        /**
         * @brief How a task failed, whole - its exception's diagnostic information, which names every
         * nested cause - or a marker when it did not fail
         */

        inline auto failureOf( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task ) -> std::string
        {
            if( ! task -> exception() )
            {
                return std::string( "<no exception>" );
            }

            return bl::eh::diagnostic_information( task -> exception() );
        }

        /**
         * @brief Fails with the reason the task failed - a function and not a UTF macro argument,
         * because UTF_FAIL( msg ) takes the globals lock before it evaluates msg and bl::os::mutex
         * is not recursive ( Utf.h )
         */

        inline void requireTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            if( ! task -> isFailed() )
            {
                return;
            }

            UTF_FAIL( "the session request task failed: " + failureOf( task ) );
        }

        inline void requireTrue(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( ! condition )
            {
                UTF_FAIL( message );
            }
        }

        /**
         * @brief A script which answers the one request it reads with a canned response, and then
         * holds the connection open until the peer is released
         *
         * HELD OPEN so that nothing about how the connection ENDS can reach a case which is about
         * how a response was decoded: the peer's destructor releases it, after the session has
         * already let the connection go
         */

        inline auto answerWith( SAA_in const std::string& response ) -> ScriptedPeer::script_t
        {
            return [ response ](
                SAA_inout       ScriptedPeer&                                   self,
                SAA_inout       bl::asio::ip::tcp::socket&                      socket
                ) -> void
            {
                const auto request = ScriptedPeer::readRequest( socket );

                self.record( "request:" + ScriptedPeer::requestLineOf( request ) );

                ScriptedPeer::send( socket, response );

                self.waitForRelease();
            };
        }

        /**
         * @brief A script which sends a response and then CLOSES - so a response shorter than its
         * own Content-Length arrives cut short, and the stream fails
         */

        inline auto answerAndCloseWith( SAA_in const std::string& response ) -> ScriptedPeer::script_t
        {
            return [ response ](
                SAA_inout       ScriptedPeer&                                   self,
                SAA_inout       bl::asio::ip::tcp::socket&                      socket
                ) -> void
            {
                const auto request = ScriptedPeer::readRequest( socket );

                self.record( "request:" + ScriptedPeer::requestLineOf( request ) );

                ScriptedPeer::send( socket, response );
            };
        }

        /**
         * @brief A decoder for "x-utest-terminated", which passes its input through and requires
         * it to end in '$'
         *
         * IT IS THE CONTRACT'S TRUNCATION CHECK AND NOTHING MORE. ContentDecoder::finish( ) must
         * throw InvalidDataFormatException when the coded stream is truncated, because a decoder
         * which accepts one hands its caller a truncated body - so this one does exactly that, and
         * a response cut short on the wire is what makes it throw. Its error is then a decoder's
         * own, which the caller must not see in place of the network failure which truncated it
         */

        template
        <
            typename E = void
        >
        class TerminatedDecoderT : public bl::httpclient::ContentDecoder
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( TerminatedDecoderT, bl::httpclient::ContentDecoder )

        protected:

            char                                                                m_last;

            TerminatedDecoderT() NOEXCEPT
                :
                m_last( '\0' )
            {
            }

        public:

            static const std::string& coding() NOEXCEPT
            {
                return g_coding;
            }

            virtual const std::string& contentCoding() const NOEXCEPT OVERRIDE
            {
                return g_coding;
            }

            virtual void write(
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&     input,
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                )
                OVERRIDE
            {
                const std::string text(
                    input -> begin() + input -> offset1(),
                    input -> begin() + input -> size()
                    );

                if( ! text.empty() )
                {
                    m_last = text[ text.size() - 1U ];
                }

                output( h2driver::blockOf( text ) );
            }

            virtual void finish(
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                )
                OVERRIDE
            {
                BL_UNUSED( output );

                BL_CHK_T(
                    false,
                    '$' == m_last,
                    bl::InvalidDataFormatException(),
                    BL_MSG()
                        << "The test decoder found the coded body truncated"
                    );
            }

        private:

            static const std::string                                            g_coding;
        };

        BL_DEFINE_STATIC_CONST_STRING( TerminatedDecoderT, g_coding ) = "x-utest-terminated";

        typedef bl::om::ObjectImpl< TerminatedDecoderT<> >                      TerminatedDecoder;

        /**
         * @brief Registers the two test transforms on a session
         */

        inline void registerTestDecoders( SAA_in const bl::om::ObjPtr< PlainSessionImpl >& plainSession )
        {
            using namespace bl;

            plainSession -> decoders().registerDecoder(
                utest::session::UtestDecoderT<>::coding(),
                []() -> om::ObjPtr< httpclient::ContentDecoder >
                {
                    return om::qi< httpclient::ContentDecoder >(
                        utest::session::UtestDecoder::createInstance()
                        );
                }
                );

            plainSession -> decoders().registerDecoder(
                TerminatedDecoderT<>::coding(),
                []() -> om::ObjPtr< httpclient::ContentDecoder >
                {
                    return om::qi< httpclient::ContentDecoder >( TerminatedDecoder::createInstance() );
                }
                );
        }

        /**
         * @brief One request's result: the task, as the caller holds it, and what the peer read
         */

        struct Fetched
        {
            bl::om::ObjPtr< bl::httpclient::ClientRequestTask >                 requestTask;
            bl::om::ObjPtr< bl::tasks::Task >                                   task;
            std::vector< std::string >                                          peerRecords;
            std::string                                                         peerFailure;
        };

        /**
         * @brief Runs one request through a cleartext session, with the test transforms registered,
         * against a peer running 'script' - and hands back how it ended, succeeded or not
         *
         * The peer is declared first so that it outlives the session: the session is disposed on
         * the way out, which lets the connection go, and only then is the peer released and joined
         */

        inline auto fetch(
            SAA_in          ScriptedPeer::script_t&&                            script,
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> Fetched
        {
            using namespace bl;

            ScriptedPeer peer( BL_PARAM_FWD( script ) );

            Fetched fetched;

            {
                const auto session = makeSession();

                BL_SCOPE_EXIT_WARN_ON_FAILURE(
                    {
                        session -> dispose();
                    },
                    "utest::plainsession::fetch"
                    );

                registerTestDecoders( session );

                fetched.requestTask = session -> createRequestTask(
                    makeRequest( peer.port(), "/coded", method )
                    );

                fetched.task = om::qi< tasks::Task >( fetched.requestTask );

                runSessionTask( fetched.task );
            }

            fetched.peerRecords = peer.records();
            fetched.peerFailure = peer.failure();

            return fetched;
        }

        /**
         * @brief Every value of one field, in the order they arrived, as one readable line
         */

        inline auto fieldsNamed(
            SAA_in          const bl::httpclient::ClientResponse&               response,
            SAA_in          const std::string&                                  name
            )
            -> std::string
        {
            const auto values = response.headers().getAll( name );

            std::string result;

            for( std::size_t i = 0U; i < values.size(); ++i )
            {
                result += "[";
                result += values[ i ];
                result += "]";
            }

            return result;
        }

    } // plainsession

} // utest

/**
 * @brief The control - ONE coding in ONE field is still decoded, and its labels come off
 *
 * Green before D5 and after it. What it guards against is a fix which reads the list correctly and
 * decodes nothing: the transform turns '~' into a space, and once the body is decoded neither its
 * content-encoding nor its content-length describes it any more, so both go
 */

UTF_AUTO_TEST_CASE( ClientSession_ASingleCodingIsDecodedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 200 OK\r\n"
            "Content-Encoding: x-utest\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello~world"
            )
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_REQUIRE_EQUAL( response.status(), 200U );
    UTF_REQUIRE_EQUAL( bodyOf( response ), std::string( "hello world" ) );

    UTF_REQUIRE_EQUAL( fieldsNamed( response, "content-encoding" ), std::string() );
    UTF_REQUIRE_EQUAL( fieldsNamed( response, "content-length" ), std::string() );

    UTF_REQUIRE_EQUAL( fetched.peerRecords.size(), 1U );
    UTF_REQUIRE_EQUAL( fetched.peerRecords[ 0 ], std::string( "request:GET /coded HTTP/1.1" ) );
}

/**
 * @brief H24 - two Content-Encoding fields are two codings, and the response is handed back
 * untouched
 *
 * "x-utest" in one field and "gzip" in the next is the list "x-utest, gzip": x-utest applied first,
 * gzip over it. RED BEFORE D5: the first field alone was read, x-utest was undone from what is
 * really gzip's output, and every Content-Encoding field was then removed - a body decoded in the
 * wrong order and labelled as uncoded. GREEN AFTER: the body and both fields exactly as they came
 */

UTF_AUTO_TEST_CASE( ClientSession_TwoContentEncodingFieldsAreHandedBackUntouchedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 200 OK\r\n"
            "Content-Encoding: x-utest\r\n"
            "Content-Encoding: gzip\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello~world"
            )
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_CHECK_EQUAL( bodyOf( response ), std::string( "hello~world" ) );

    UTF_CHECK_EQUAL( fieldsNamed( response, "content-encoding" ), std::string( "[x-utest][gzip]" ) );
    UTF_CHECK_EQUAL( fieldsNamed( response, "content-length" ), std::string( "[11]" ) );
}

/**
 * @brief The other shape of two codings - one field listing both - also handed back untouched
 *
 * NOT RED BEFORE D5, and it is here as the control for the shape which is: the old lookup asked the
 * registry for the whole field value, "x-utest, gzip", matched nothing and so decoded nothing - by
 * accident rather than by rule. After D5 the list is split, the two codings are counted, and the
 * answer is the same one by rule
 */

UTF_AUTO_TEST_CASE( ClientSession_OneFieldListingTwoCodingsIsHandedBackUntouchedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 200 OK\r\n"
            "Content-Encoding: x-utest, gzip\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "hello~world"
            )
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_REQUIRE_EQUAL( bodyOf( response ), std::string( "hello~world" ) );

    UTF_REQUIRE_EQUAL( fieldsNamed( response, "content-encoding" ), std::string( "[x-utest, gzip]" ) );
    UTF_REQUIRE_EQUAL( fieldsNamed( response, "content-length" ), std::string( "[11]" ) );
}

/**
 * @brief H25 - a response to HEAD carries no content, so it is not decoded
 *
 * Its Content-Encoding and Content-Length describe the representation a GET would have returned,
 * and they are the caller's answer to the HEAD. RED BEFORE D5: the empty body was "decoded" and both
 * fields were removed. GREEN AFTER: both fields as they came, and no body
 */

UTF_AUTO_TEST_CASE( ClientSession_AResponseToHeadIsNotDecodedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 200 OK\r\n"
            "Content-Encoding: x-utest\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            ),
        "HEAD"
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_REQUIRE_EQUAL( response.status(), 200U );

    UTF_CHECK_EQUAL( fieldsNamed( response, "content-encoding" ), std::string( "[x-utest]" ) );
    UTF_CHECK_EQUAL( fieldsNamed( response, "content-length" ), std::string( "[11]" ) );
    UTF_CHECK_EQUAL( bodyOf( response ), std::string() );

    UTF_REQUIRE_EQUAL( fetched.peerRecords.size(), 1U );
    UTF_REQUIRE_EQUAL( fetched.peerRecords[ 0 ], std::string( "request:HEAD /coded HTTP/1.1" ) );
}

/**
 * @brief H25 - a 304 carries no content, so it is not decoded
 *
 * RED BEFORE D5: its Content-Encoding was removed. GREEN AFTER: as it came
 */

UTF_AUTO_TEST_CASE( ClientSession_ANotModifiedResponseIsNotDecodedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 304 Not Modified\r\n"
            "Content-Encoding: x-utest\r\n"
            "\r\n"
            )
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_REQUIRE_EQUAL( response.status(), 304U );

    UTF_CHECK_EQUAL( fieldsNamed( response, "content-encoding" ), std::string( "[x-utest]" ) );
    UTF_CHECK_EQUAL( bodyOf( response ), std::string() );
}

/**
 * @brief H25 - a 204 carries no content, so it is not decoded
 *
 * The third of the three the decision names. RED BEFORE D5: its Content-Encoding was removed.
 * GREEN AFTER: as it came
 */

UTF_AUTO_TEST_CASE( ClientSession_ANoContentResponseIsNotDecodedTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerWith(
            "HTTP/1.1 204 No Content\r\n"
            "Content-Encoding: x-utest\r\n"
            "\r\n"
            )
        );

    requireTaskSucceeded( fetched.task );

    const auto& response = fetched.requestTask -> response();

    UTF_REQUIRE_EQUAL( response.status(), 204U );

    UTF_CHECK_EQUAL( fieldsNamed( response, "content-encoding" ), std::string( "[x-utest]" ) );
    UTF_CHECK_EQUAL( bodyOf( response ), std::string() );
}

/**
 * @brief H25 - a failed hop is not decoded, and the caller sees the failure which really happened
 *
 * The response declares 100 bytes of a coded body, delivers ten and closes, so the stream fails with
 * the transport's own error. Its coding is one whose decoder, like every real one, refuses a
 * truncated input in finish( ).
 *
 * RED BEFORE D5: the partial body was decoded anyway, the decoder refused it, and its exception
 * escaped continuationTask( ) - which the execution queue turns into the task's failure, REPLACING
 * the network error with the decoder's. GREEN AFTER: the network error, and no decoder's anywhere in
 * the failure
 */

UTF_AUTO_TEST_CASE( ClientSession_AFailedCodedResponseReportsTheNetworkErrorTests )
{
    using namespace bl;
    using namespace utest::plainsession;

    const auto fetched = fetch(
        answerAndCloseWith(
            "HTTP/1.1 200 OK\r\n"
            "Content-Encoding: x-utest-terminated\r\n"
            "Content-Length: 100\r\n"
            "\r\n"
            "0123456789"
            )
        );

    requireTrue(
        fetched.task -> isFailed(),
        "a response cut short on the wire should have failed the request"
        );

    const auto failure = failureOf( fetched.task );

    requireTrue(
        std::string::npos == failure.find( "The test decoder found the coded body truncated" ),
        "the caller was handed the decoder's error in place of the network's: " + failure
        );

    requireTrue(
        std::string::npos != failure.find( "The HTTP request failed" ),
        "the caller should see the network failure, and the task reports: " + failure
        );
}

#endif /* __UTEST_TESTCLIENTSESSIONDECODING_H_ */
