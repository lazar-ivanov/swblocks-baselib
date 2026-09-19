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

#ifndef __UTEST_TESTASIOSSLSTREAMWRAPPER_H_
#define __UTEST_TESTASIOSSLSTREAMWRAPPER_H_

#include <baselib/tasks/AsioSslStreamWrapper.h>
#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/*
 * Offline tests for AsioSslStreamWrapper
 *
 * TestTlsHandshakeVerification.h covers verifyCertificate's two fail-closed branches and the
 * errinfo_ssl_is_verify_* payload end to end against a real server. Nothing else in the file is
 * touched by any test, although the class is a plain class with a public constructor and a
 * public surface from the stream accessors down - the shutdown preconditions, the state
 * accessors on a fresh object, the endpointId() format which keys the untrusted endpoints map
 * and the no-verify-failure path of enhanceException are all deterministic and need neither a
 * network, a port nor any om plumbing
 *
 * The same is true of the additions of slice S1.6 which are covered below - the ALPN offer, the
 * negotiated version and cipher getters, the ClientHello capture hook and the two new
 * constructors: what each of them does before a handshake is a property of the object and of
 * OpenSSL's own state, so it is asserted here rather than against a peer
 *
 * crypto::CryptoBase::init() has already run from UtfMain.h before any case executes, so the
 * client context the wrapper picks up when it is given no server context cannot BL_RIP_MSG
 */

namespace utest
{
    namespace sslstreamwrapper
    {
        /**
         * @brief Exposes the protected state of the wrapper so a fresh object can be asserted
         *
         * m_hasHandshakeCompletedSuccessfully and m_wasShutdownInvoked are only ever set by
         * the private handshake and shutdown completion handlers, so the second of the two
         * beginProtocolShutdown preconditions can be reached from a test in no other way
         */

        class StreamWrapperProbe : public bl::tasks::AsioSslStreamWrapper
        {
            BL_NO_COPY_OR_MOVE( StreamWrapperProbe )

        public:

            typedef bl::tasks::AsioSslStreamWrapper                 base_type;

            StreamWrapperProbe(
                SAA_inout       bl::asio::io_service&               ioService,
                SAA_in          const std::string&                  hostName,
                SAA_in          const std::string&                  serviceName
                )
                :
                base_type( ioService, hostName, serviceName, nullptr /* sslServerContextPtr */ )
            {
            }

            #if ( ( BOOST_VERSION / 100 ) >= 1072 )

            /**
             * @brief Exercises the strand-taking constructor overload
             */

            StreamWrapperProbe(
                SAA_in          const bl::asio::strand_t&           strand,
                SAA_in          const std::string&                  hostName,
                SAA_in          const std::string&                  serviceName
                )
                :
                base_type( strand, hostName, serviceName, nullptr /* sslServerContextPtr */ )
            {
            }

            #endif /* ( ( BOOST_VERSION / 100 ) >= 1072 ) */

            /**
             * @brief Exercises the constructor which takes a client context by reference
             */

            StreamWrapperProbe(
                SAA_inout       bl::asio::io_service&               ioService,
                SAA_in          const std::string&                  hostName,
                SAA_in          const std::string&                  serviceName,
                SAA_in          bl::asio::ssl::context&             sslClientContext
                )
                :
                base_type( ioService, hostName, serviceName, sslClientContext )
            {
            }

            bool isServer() const NOEXCEPT
            {
                return base_type::m_isServer.value();
            }

            /**
             * @brief Hands the message callback a message as OpenSSL would
             *
             * The callback is what captures the ClientHello, and a handshake is the only thing
             * which would otherwise reach it; driving it directly is what lets the filtering it
             * does - direction, content type, message type, first one only - be asserted with no
             * network, no peer and no key material
             */

            void feedSslMessage(
                SAA_in          const int                           writeP,
                SAA_in          const int                           contentType,
                SAA_in          const std::vector< unsigned char >& message
                )
            {
                base_type::onSslMessageCallback(
                    writeP,
                    0 /* version */,
                    contentType,
                    message.empty() ? nullptr : message.data(),
                    message.size(),
                    nullptr /* ssl */,
                    this
                    );
            }

            bool verifyFailed() const NOEXCEPT
            {
                return base_type::m_verifyFailed.value();
            }

            int lastVerifyError() const NOEXCEPT
            {
                return base_type::m_lastVerifyError.value();
            }

            const std::string& lastVerifyErrorString() const NOEXCEPT
            {
                return base_type::m_lastVerifyErrorString;
            }

            const std::string& lastVerifyErrorMessage() const NOEXCEPT
            {
                return base_type::m_lastVerifyErrorMessage;
            }

            const std::string& lastVerifySubjectName() const NOEXCEPT
            {
                return base_type::m_lastVerifySubjectName;
            }

            void markHandshakeCompleted() NOEXCEPT
            {
                base_type::m_hasHandshakeCompletedSuccessfully = true;
            }

            void markShutdownInvoked() NOEXCEPT
            {
                base_type::m_wasShutdownInvoked = true;
            }

            /**
             * @brief Records the state verifyCertificate would have left behind on a failure
             */

            void recordVerifyFailure()
            {
                base_type::m_verifyFailed = true;
                base_type::m_lastVerifyError = 19 /* X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN */;
                base_type::m_lastVerifyErrorString = "self signed certificate in certificate chain";
                base_type::m_lastVerifyErrorMessage = "SSL verify error: 19";
                base_type::m_lastVerifySubjectName = "/CN=test-host";
            }
        };

    } // sslstreamwrapper

} // utest

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_FreshStateTests )
{
    using namespace bl;

    asio::io_service ioService;

    utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

    /*
     * Nothing has been attempted on this stream yet, so every piece of state the wrapper
     * carries must read as "not done" - beginProtocolHandshake() clears all of it again, so
     * these are also the values the wrapper is documented to be reset to on reuse
     */

    UTF_REQUIRE( ! wrapper.hasHandshakeCompletedSuccessfully() );
    UTF_REQUIRE( ! wrapper.hasShutdownCompletedSuccessfully() );
    UTF_REQUIRE( ! wrapper.wasShutdownInvoked() );

    UTF_REQUIRE( ! wrapper.verifyFailed() );
    UTF_REQUIRE_EQUAL( wrapper.lastVerifyError(), 0 );
    UTF_REQUIRE( wrapper.lastVerifyErrorString().empty() );
    UTF_REQUIRE( wrapper.lastVerifyErrorMessage().empty() );
    UTF_REQUIRE( wrapper.lastVerifySubjectName().empty() );

    /*
     * The socket underneath the SSL stream is created but never opened by the constructor
     */

    UTF_REQUIRE( ! wrapper.isChannelOpen() );

    /*
     * endpointId() is the key under which the untrusted endpoints map records a peer, so its
     * exact shape is consumed by notifyOnSuccessfulHandshakeOrShutdown, by the soft-fail
     * assertions in TestTlsHandshakeVerification.h and by every operator tool reading the map
     */

    UTF_REQUIRE_EQUAL( wrapper.endpointId(), "test-host:12345" );
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_BeginProtocolShutdownPreconditionTests )
{
    using namespace bl;

    typedef bl::tasks::AsioSslStreamWrapper::completion_callback_t   completion_callback_t;

    asio::io_service ioService;

    /*
     * Both preconditions fail before the callback is ever used, so an empty one is enough -
     * and it is what makes it safe to call this at all on a stream with no socket
     */

    {
        /*
         * A shutdown on a stream which never completed its handshake would be an
         * async_shutdown on a stream OpenSSL has no session for
         */

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        UTF_REQUIRE_THROW_MESSAGE(
            wrapper.beginProtocolShutdown( completion_callback_t() ),
            bl::UnexpectedException,
            "Protocol shutdown should not be called on a stream which did not complete handshake"
            );
    }

    {
        /*
         * ... and a second shutdown would post a second async_shutdown on the same stream,
         * which is what a relaxed precondition here would silently allow
         */

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        wrapper.markHandshakeCompleted();
        wrapper.markShutdownInvoked();

        UTF_REQUIRE_THROW_MESSAGE(
            wrapper.beginProtocolShutdown( completion_callback_t() ),
            bl::UnexpectedException,
            "Protocol shutdown should not be called more than once"
            );
    }
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_EnhanceExceptionNoVerifyFailureTests )
{
    using namespace bl;

    asio::io_service ioService;

    /*
     * The complement of what TestTlsHandshakeVerification.h asserts on the failure path: with
     * no verification failure recorded the exception must come back with none of the five
     * errinfo_ssl_is_verify_* items attached, otherwise every handshake error would look like
     * a certificate problem
     */

    {
        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        UTF_REQUIRE( ! wrapper.verifyFailed() );

        auto exception = BL_EXCEPTION( UnexpectedException(), "No verification was attempted" );

        wrapper.enhanceException( exception );

        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_ssl_is_verify_failed >( exception ) );
        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_ssl_is_verify_error >( exception ) );
        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_ssl_is_verify_error_string >( exception ) );
        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_ssl_is_verify_error_message >( exception ) );
        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_ssl_is_verify_subject_name >( exception ) );
    }

    {
        /*
         * The positive control on the very same call - without it the assertions above would
         * also pass against an enhanceException() which attached nothing at all
         */

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        wrapper.recordVerifyFailure();

        auto exception = BL_EXCEPTION( UnexpectedException(), "Verification failed" );

        wrapper.enhanceException( exception );

        const auto* const failed = eh::get_error_info< eh::errinfo_ssl_is_verify_failed >( exception );

        UTF_REQUIRE( nullptr != failed );
        UTF_REQUIRE( *failed );

        const auto* const error = eh::get_error_info< eh::errinfo_ssl_is_verify_error >( exception );

        UTF_REQUIRE( nullptr != error );
        UTF_REQUIRE_EQUAL( *error, wrapper.lastVerifyError() );

        const auto* const errorString =
            eh::get_error_info< eh::errinfo_ssl_is_verify_error_string >( exception );

        UTF_REQUIRE( nullptr != errorString );
        UTF_REQUIRE_EQUAL( *errorString, wrapper.lastVerifyErrorString() );

        const auto* const message =
            eh::get_error_info< eh::errinfo_ssl_is_verify_error_message >( exception );

        UTF_REQUIRE( nullptr != message );
        UTF_REQUIRE_EQUAL( *message, wrapper.lastVerifyErrorMessage() );

        const auto* const subject =
            eh::get_error_info< eh::errinfo_ssl_is_verify_subject_name >( exception );

        UTF_REQUIRE( nullptr != subject );
        UTF_REQUIRE_EQUAL( *subject, wrapper.lastVerifySubjectName() );
    }
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_NegotiatedParametersAreEmptyBeforeHandshakeTests )
{
    using namespace bl;

    asio::io_service ioService;

    utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

    /*
     * Nothing has been negotiated on a stream which has not handshaked, and all three getters
     * have to say so with the same value - the empty string - rather than with the placeholders
     * OpenSSL uses for it. A caller attaching these to an exception or to a fidelity report
     * decides "was anything negotiated" by asking whether the string is empty, and must not have
     * to know which magic word belongs to which call
     */

    UTF_REQUIRE( wrapper.getNegotiatedVersion().empty() );
    UTF_REQUIRE( wrapper.getNegotiatedCipher().empty() );
    UTF_REQUIRE( wrapper.getAlpnSelected().empty() );

    /*
     * The positive control, and the reason the version getter cannot simply forward the OpenSSL
     * call: OpenSSL names a version for this stream even though it has never handshaked - it is
     * the highest the method could negotiate, "TLSv1.3" on the 3.5.4 this is built against - and
     * that value is indistinguishable from a negotiated one. What is actually absent is the
     * session, which is what the getter keys on
     *
     * The assertion is deliberately "non-empty" rather than the string itself, because the exact
     * word differs between OpenSSL flavors and the second one is owed, not tested
     */

    UTF_REQUIRE(
        ! std::string( ::SSL_get_version( wrapper.getStream().native_handle() ) ).empty()
        );

    UTF_REQUIRE( nullptr == ::SSL_get_current_cipher( wrapper.getStream().native_handle() ) );

    /*
     * ... and nothing was captured, because the capture was never armed
     */

    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_AlpnProtocolOfferTests )
{
    using namespace bl;

    asio::io_service ioService;

    {
        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        /*
         * ::SSL_set_alpn_protos returns zero on success, which is the inverse of nearly every
         * other OpenSSL call, so an implementation which routed it through BL_CHK_CRYPTO_API_NM
         * would throw on exactly this call - the one which worked. That is what this asserts
         */

        UTF_REQUIRE_NO_THROW(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >{ "h2", "http/1.1" } )
            );

        /*
         * The offer is what we send; nothing is selected until a peer selects it, so this is
         * still empty and an empty selection is what this library reads as HTTP/1.1
         */

        UTF_REQUIRE( wrapper.getAlpnSelected().empty() );

        /*
         * ... and the offer can be replaced before the handshake is started
         */

        UTF_REQUIRE_NO_THROW(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >{ "http/1.1" } )
            );
    }

    {
        /*
         * The wire format prefixes every name with its length in a single byte, so a name outside
         * 1 .. 255 bytes cannot be encoded; profiles are loadable from data, so these lists are
         * not always this library's own and a bad one must be refused rather than truncated into
         * a malformed extension
         */

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        UTF_REQUIRE_THROW_MESSAGE(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >() ),
            bl::UnexpectedException,
            "An ALPN protocol offer must name at least one protocol"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >{ "h2", "" } ),
            bl::UnexpectedException,
            "An ALPN protocol name must be between 1 and 255 bytes long: ''"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >{ std::string( 256U, 'x' ) } ),
            bl::UnexpectedException,
            "An ALPN protocol name must be between 1 and 255 bytes long:"
            );

        /*
         * A name of exactly the longest encodable length is accepted, so the bound above is the
         * encoding's and not an arbitrary one
         */

        UTF_REQUIRE_NO_THROW(
            wrapper.setAlpnProtocolOffer( std::vector< std::string >{ std::string( 255U, 'x' ) } )
            );
    }
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_ClientHelloCaptureTests )
{
    using namespace bl;

    asio::io_service ioService;

    /*
     * A ClientHello as OpenSSL hands it to the message callback: the handshake message type,
     * its 24 bit length, and the body - the bytes are arbitrary because nothing here parses them
     */

    const std::vector< unsigned char > clientHello
    {
        SSL3_MT_CLIENT_HELLO, 0x00, 0x00, 0x04, 0x03, 0x03, 0xAA, 0xBB
    };

    const std::vector< unsigned char > serverHello
    {
        SSL3_MT_SERVER_HELLO, 0x00, 0x00, 0x04, 0x03, 0x03, 0xCC, 0xDD
    };

    utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );

    /*
     * Arming is the only thing which installs the callback on the stream, so what is asserted
     * below - by driving the callback directly - is the filtering it does once it is installed,
     * not that it is reached
     */

    wrapper.enableClientHelloCapture();

    /*
     * Everything which is not a ClientHello we wrote is ignored: what we received, what is not a
     * handshake record, and the handshake messages of any other type
     */

    wrapper.feedSslMessage( 0 /* writeP */, SSL3_RT_HANDSHAKE, clientHello );
    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );

    wrapper.feedSslMessage( 1 /* writeP */, SSL3_RT_APPLICATION_DATA, clientHello );
    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );

    wrapper.feedSslMessage( 1 /* writeP */, SSL3_RT_HANDSHAKE, serverHello );
    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );

    /*
     * ... and the ClientHello we wrote is kept verbatim, header included, because what makes a
     * fidelity report honest is that it states the bytes which went out
     */

    wrapper.feedSslMessage( 1 /* writeP */, SSL3_RT_HANDSHAKE, clientHello );

    UTF_REQUIRE( wrapper.getCapturedClientHello() == clientHello );

    /*
     * A TLS 1.3 HelloRetryRequest makes the client send a second ClientHello; the first is the
     * one JA3 and JA4 are defined on, so it is the one which survives
     */

    const std::vector< unsigned char > secondClientHello
    {
        SSL3_MT_CLIENT_HELLO, 0x00, 0x00, 0x04, 0x03, 0x03, 0x11, 0x22
    };

    wrapper.feedSslMessage( 1 /* writeP */, SSL3_RT_HANDSHAKE, secondClientHello );

    UTF_REQUIRE( wrapper.getCapturedClientHello() == clientHello );

    /*
     * Arming again is what a reused wrapper does, and it discards what the previous handshake
     * captured rather than reporting it against the next one
     */

    wrapper.enableClientHelloCapture();

    UTF_REQUIRE( wrapper.getCapturedClientHello().empty() );

    wrapper.feedSslMessage( 1 /* writeP */, SSL3_RT_HANDSHAKE, secondClientHello );

    UTF_REQUIRE( wrapper.getCapturedClientHello() == secondClientHello );
}

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_ClientContextConstructorTests )
{
    using namespace bl;

    asio::io_service ioService;

    {
        /*
         * The rule the existing constructor carries is that a non-null context is the server's,
         * and a null one means the client role on the global client context. It is unchanged
         */

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345" );

        UTF_REQUIRE( ! wrapper.isServer() );

        UTF_REQUIRE(
            ::SSL_get_SSL_CTX( wrapper.getStream().native_handle() ) ==
                crypto::CryptoBase::getAsioSslContext().native_handle()
            );
    }

    {
        /*
         * ... which is why a client that wants a context of its own - a per-profile one, say -
         * needs a constructor of its own. It takes the context by reference rather than by
         * pointer: it is not optional, and that asymmetry is what tells the two apart with no
         * tag argument. The role stays the client's, and the stream is on the context given
         */

        asio::ssl::context clientContext( asio::ssl::context::tls_client );

        UTF_REQUIRE(
            clientContext.native_handle() != crypto::CryptoBase::getAsioSslContext().native_handle()
            );

        utest::sslstreamwrapper::StreamWrapperProbe wrapper( ioService, "test-host", "12345", clientContext );

        UTF_REQUIRE( ! wrapper.isServer() );

        UTF_REQUIRE(
            ::SSL_get_SSL_CTX( wrapper.getStream().native_handle() ) == clientContext.native_handle()
            );

        /*
         * ... and everything the wrapper carries reads exactly as it does for the constructor
         * above, because nothing but the context and the role selection differs
         */

        UTF_REQUIRE( ! wrapper.hasHandshakeCompletedSuccessfully() );
        UTF_REQUIRE( ! wrapper.isChannelOpen() );
        UTF_REQUIRE_EQUAL( wrapper.endpointId(), "test-host:12345" );
    }
}

#if ( ( BOOST_VERSION / 100 ) >= 1072 )

UTF_AUTO_TEST_CASE( AsioSslStreamWrapper_StrandConstructorTests )
{
    using namespace bl;

    asio::io_service ioService;

    const auto strand = asio::make_strand( ioService );

    utest::sslstreamwrapper::StreamWrapperProbe wrapper( strand, "test-host", "12345" );

    /*
     * The wrapper is the same wrapper; only the executor the stream was constructed on differs
     */

    UTF_REQUIRE( ! wrapper.isServer() );
    UTF_REQUIRE( ! wrapper.isChannelOpen() );
    UTF_REQUIRE_EQUAL( wrapper.endpointId(), "test-host:12345" );

    /*
     * What the overload exists for: the socket underneath carries the strand as its default
     * executor, so every handler of every operation on it - the intermediate handlers of the
     * composed operations included, which no call site can wrap by hand - runs on that strand.
     * Asserting it through the socket's own executor is what makes this a property of the
     * constructed object rather than of the argument that was passed in
     */

    bool ranOnTheStrand = false;

    asio::post(
        wrapper.getSocket().get_executor(),
        [ &ranOnTheStrand, &strand ]() -> void
        {
            ranOnTheStrand = strand.running_in_this_thread();
        }
        );

    ioService.run();

    UTF_REQUIRE( ranOnTheStrand );
}

#endif /* ( ( BOOST_VERSION / 100 ) >= 1072 ) */

#endif /* __UTEST_TESTASIOSSLSTREAMWRAPPER_H_ */
