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

#ifndef __UTEST_TESTTLSHANDSHAKEVERIFICATION_H_
#define __UTEST_TESTTLSHANDSHAKEVERIFICATION_H_

#include <baselib/http/SimpleHttpSslTask.h>
#include <baselib/httpserver/HttpServer.h>
#include <baselib/tasks/AsioSslStreamWrapper.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/HttpServerHelpers.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>

#include <openssl/x509_vfy.h>

/*
 * End-to-end peer verification tests
 *
 * TestTlsPeerVerification.h pins the name matching decision offline. These cases drive a real
 * handshake through AsioSslStreamWrapper against a local server, so that the two ways a client
 * connection can fail verification - a name which the certificate does not cover, and a chain
 * which does not lead to a registered root - are proved to fail closed AND to surface through
 * the errinfo_ssl_is_verify_* error info which enhanceException() attaches to the task
 * exception. Those fields are what the REST error helpers serialize, so their absence would be
 * an invisible regression.
 */

namespace utest
{
    namespace tlshandshake
    {
        struct VerifyInfo
        {
            bool                                                    present;
            bool                                                    failed;
            int                                                     error;
            std::string                                             errorString;
            std::string                                             message;
            std::string                                             subject;

            VerifyInfo()
                :
                present( false ),
                failed( false ),
                error( 0 )
            {
            }
        };

        /**
         * @brief Installs the library's real verify callback for the duration of the scope
         *
         * The test binaries install a no-verify hook under --no-rfc2818-verify, which would
         * bypass the code under test, so the hook is cleared here and restored on exit
         */

        class RealVerifyCallbackScope
        {
            BL_NO_COPY_OR_MOVE( RealVerifyCallbackScope )

        private:

            const bl::tasks::AsioSslStreamWrapper::rfc2818_verify_callback_t        m_saved;

        public:

            RealVerifyCallbackScope()
                :
                m_saved( bl::tasks::AsioSslStreamWrapper::g_rfc2818VerifyCallback )
            {
                bl::tasks::AsioSslStreamWrapper::g_rfc2818VerifyCallback =
                    bl::tasks::AsioSslStreamWrapper::rfc2818_verify_callback_t();
            }

            ~RealVerifyCallbackScope() NOEXCEPT
            {
                bl::tasks::AsioSslStreamWrapper::g_rfc2818VerifyCallback = m_saved;
            }
        };

        /**
         * @brief Performs one HTTPS GET against the local test server as the given peer name
         *
         * Returns true when the request succeeded; when it failed, the verification error info
         * attached to the task exception (if any) is copied into 'info'
         */

        inline bool attemptConnection(
            SAA_in          const std::string&                      peerName,
            SAA_out         VerifyInfo&                             info
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            RealVerifyCallbackScope realVerifyCallback;

            UTF_REQUIRE( ! crypto::CryptoBase::allowUntrustedCertificates() );

            bool succeeded = false;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const bl::str::SecureStringWrapper content;

                    const auto stask = SimpleSecureHttpSslGetTaskImpl::createInstance(
                        cpp::copy( peerName ),
                        test::UtfArgsParser::port(),
                        utest::http::g_requestUri,
                        content,
                        bl::http::HeadersMap()
                        );

                    const auto task = om::qi< Task >( stask );

                    eq -> push_back( task );
                    ( void ) eq -> pop( true );

                    if( ! stask -> isFailed() )
                    {
                        succeeded = true;

                        return;
                    }

                    UTF_REQUIRE( nullptr != stask -> exception() );

                    try
                    {
                        cpp::safeRethrowException( stask -> exception() );
                    }
                    catch( std::exception& e )
                    {
                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << "\nTLS handshake failure for peer name '"
                                << peerName
                                << "':\n"
                                << eh::diagnostic_information( e )
                            );

                        const auto* failed = eh::get_error_info< eh::errinfo_ssl_is_verify_failed >( e );

                        if( failed )
                        {
                            info.present = true;
                            info.failed = *failed;

                            const auto* error =
                                eh::get_error_info< eh::errinfo_ssl_is_verify_error >( e );
                            const auto* errorString =
                                eh::get_error_info< eh::errinfo_ssl_is_verify_error_string >( e );
                            const auto* message =
                                eh::get_error_info< eh::errinfo_ssl_is_verify_error_message >( e );
                            const auto* subject =
                                eh::get_error_info< eh::errinfo_ssl_is_verify_subject_name >( e );

                            if( error )
                            {
                                info.error = *error;
                            }

                            if( errorString )
                            {
                                info.errorString = *errorString;
                            }

                            if( message )
                            {
                                info.message = *message;
                            }

                            if( subject )
                            {
                                info.subject = *subject;
                            }
                        }
                    }
                }
                );

            return succeeded;
        }

    } // tlshandshake

} // utest

UTF_AUTO_TEST_CASE( TlsHandshake_NameMismatchIsReportedThroughErrorInfo )
{
    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback< bl::httpserver::HttpSslServer >(
        []() -> void
        {
            using namespace utest::tlshandshake;

            /*
             * Positive control first: the same server, the same real callback and a name the
             * certificate covers (the default certificate carries DNS:localhost) must succeed,
             * otherwise the failure below would prove nothing about the name check
             */

            {
                VerifyInfo control;

                UTF_REQUIRE( attemptConnection( "localhost", control ) );
            }

            /*
             * The default certificate has DNS names only, so an address literal cannot match it
             * (RFC 6125 does not allow an address to be matched against DNS names); the chain
             * itself is fine, so this is the name mismatch branch of verifyCertificate()
             */

            VerifyInfo info;

            UTF_REQUIRE( ! attemptConnection( "127.0.0.1", info ) );

            UTF_REQUIRE( info.present );
            UTF_REQUIRE( info.failed );
            UTF_REQUIRE( bl::cpp::contains( info.message, "not matching the host name" ) );
            UTF_REQUIRE( bl::cpp::contains( info.subject, "mycompany.com" ) );

#ifdef X509_V_ERR_HOSTNAME_MISMATCH
            UTF_REQUIRE_EQUAL( info.error, X509_V_ERR_HOSTNAME_MISMATCH );
            UTF_REQUIRE( ! info.errorString.empty() );
#endif
        }
        );
}

UTF_AUTO_TEST_CASE( TlsHandshake_UntrustedChainIsReportedThroughErrorInfo )
{
    /*
     * The server presents the self-signed IP address certificate, whose issuer is not a
     * registered root. The peer name matches its IP:127.0.0.1 SAN, so the only thing which can
     * fail is the chain, i.e. this is the pre-verification branch of verifyCertificate()
     */

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback< bl::httpserver::HttpSslServer >(
        []() -> void
        {
            using namespace utest::tlshandshake;

            VerifyInfo info;

            UTF_REQUIRE( ! attemptConnection( "127.0.0.1", info ) );

            UTF_REQUIRE( info.present );
            UTF_REQUIRE( info.failed );
            UTF_REQUIRE_EQUAL( info.error, X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT );

            /*
             * The text of the error string is OpenSSL's and changed between versions ("self
             * signed" became "self-signed"), so only its presence is asserted
             */

            UTF_REQUIRE( ! info.errorString.empty() );
            UTF_REQUIRE( 0U == info.message.find( "SSL verify error: 18" ) );
        },
        nullptr                                                 /* backend */,
        nullptr                                                 /* controlToken */,
        test::UtfCrypto::getIpAddressServerKey()                /* privateKeyPem */,
        test::UtfCrypto::getIpAddressServerCertificate()        /* certificatePem */
        );
}

#endif /* __UTEST_TESTTLSHANDSHAKEVERIFICATION_H_ */
