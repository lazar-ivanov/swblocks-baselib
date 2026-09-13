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

#endif /* __UTEST_TESTASIOSSLSTREAMWRAPPER_H_ */
