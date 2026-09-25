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

#ifndef __BL_BASELIB_TASKS_ASIOSSLSTREAMWRAPPER_H_
#define __BL_BASELIB_TASKS_ASIOSSLSTREAMWRAPPER_H_

#include <baselib/crypto/CryptoBase.h>
#include <baselib/crypto/TlsPeerVerification.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace tasks
    {
        /**
         * @brief class AsioSslStreamWrapper - a wrapper for Boost ASIO SSL stream class that will
         * carry other useful state necessary to manage the stream and dispose of it
         */

        template
        <
            typename E = void
        >
        class AsioSslStreamWrapperT :
            public crypto::CryptoBase
        {
            BL_NO_COPY_OR_MOVE( AsioSslStreamWrapperT )

        public:

            typedef AsioSslStreamWrapperT< E >                                          this_type;

            typedef cpp::function< void ( SAA_in const eh::error_code& ec ) >           completion_callback_t;

            typedef cpp::function
            <
                bool (
                    SAA_in          const std::string&                              hostName,
                    SAA_in          const bool                                      preVerified,
                    SAA_inout       asio::ssl::verify_context&                      ctx
                    ) NOEXCEPT
            >
            rfc2818_verify_callback_t;

            static auto rfc2818NoVerifyCallback() NOEXCEPT -> rfc2818_verify_callback_t
            {
                return &rfc2818NoVerifyImplementation;
            }

            typedef asio::ssl::stream< asio::ip::tcp::socket >                      sslstream_t;
            typedef typename sslstream_t::lowest_layer_type                         lowest_layer_type;

            #if ( ( BOOST_VERSION / 100 ) >= 1072 )
            typedef typename sslstream_t::executor_type                             executor_type;
            #endif

            static rfc2818_verify_callback_t                                        g_rfc2818VerifyCallback;

        protected:

            const std::string                                                       m_hostName;
            const std::string                                                       m_serviceName;
            const cpp::ScalarTypeIniter< bool >                                     m_isServer;
            const cpp::SafeUniquePtr< sslstream_t >                                 m_sslStream;

            cpp::ScalarTypeIniter< bool >                                           m_hasHandshakeCompletedSuccessfully;
            cpp::ScalarTypeIniter< bool >                                           m_hasShutdownCompletedSuccessfully;
            cpp::ScalarTypeIniter< bool >                                           m_wasShutdownInvoked;

            cpp::ScalarTypeIniter< bool >                                           m_verifyFailed;
            cpp::ScalarTypeIniter< int >                                            m_lastVerifyError;
            std::string                                                             m_lastVerifyErrorString;
            std::string                                                             m_lastVerifyErrorMessage;
            std::string                                                             m_lastVerifySubjectName;

            /*
             * The ClientHello this stream sent, captured only if enableClientHelloCapture() armed
             * it; it is empty otherwise, and a ClientHello is never empty, so emptiness is also
             * how the callback recognizes that it has not captured one yet
             */

            std::vector< unsigned char >                                            m_capturedClientHello;

            static bool rfc2818NoVerifyImplementation(
                SAA_in          const std::string&                                  hostName,
                SAA_in          const bool                                          preVerified,
                SAA_inout       asio::ssl::verify_context&                          ctx
                ) NOEXCEPT
            {
                BL_UNUSED( hostName );
                BL_UNUSED( preVerified );
                BL_UNUSED( ctx );

                return true;
            }

            static bool verifyCertificateDummy(
                SAA_in          const bool                                          preVerified,
                SAA_inout       asio::ssl::verify_context&                          ctx
                ) NOEXCEPT
            {
                BL_UNUSED( preVerified );
                BL_UNUSED( ctx );

                return false;
            }

            bool verifyCertificate(
                SAA_in          const asio::ssl::rfc2818_verification&              rfc2818,
                SAA_in          const bool                                          preVerified,
                SAA_inout       asio::ssl::verify_context&                          ctx
                ) NOEXCEPT
            {
                bool ok = false;

                try
                {

                const int depth = ::X509_STORE_CTX_get_error_depth( ctx.native_handle() );

                {
                    X509* cert = ::X509_STORE_CTX_get_current_cert( ctx.native_handle() );
                    BL_CHK_CRYPTO_API_NM( cert );

                    X509_NAME* subjectName = ::X509_get_subject_name( cert );
                    BL_CHK_CRYPTO_API_NM( subjectName );

                    char subjectNameBuffer[ 1024 ];

                    BL_CHK_CRYPTO_API_NM(
                        ::X509_NAME_oneline( subjectName, subjectNameBuffer, BL_ARRAY_SIZE( subjectNameBuffer ) )
                        );

                    m_lastVerifySubjectName = subjectNameBuffer;
                }

                if( preVerified )
                {
                    /*
                     * Verify the host name matches what is specified in the certificate
                     *
                     * Note that this will only match the certificate at the end of the
                     * chain (i.e. when depth = 0)
                     *
                     * Note also that the bound 'rfc2818' object is deliberately NOT used for the
                     * match. On Boost 1.89+ that name is a typedef for host_name_verification,
                     * which delegates to ::X509_check_host() and therefore cannot match an IP
                     * address literal - see baselib/crypto/TlsPeerVerification.h. It is retained in
                     * the signature so this remains a source compatible change for anyone who has
                     * overridden the callback, and so the object's lifetime keeps working as before
                     */

                    BL_UNUSED( rfc2818 );

                    if( crypto::TlsPeerVerification::verifyPeerName( preVerified, m_hostName, ctx ) )
                    {
                        ok = true;
                    }
                    else
                    {
                        m_verifyFailed = true;

#ifdef X509_V_ERR_HOSTNAME_MISMATCH
                        /*
                         * The name check is this library's, not OpenSSL's, so the store carries
                         * no error for it; the code OpenSSL itself uses for the same outcome is
                         * recorded so that the error info fields are meaningful for this case too
                         */

                        m_lastVerifyError = X509_V_ERR_HOSTNAME_MISMATCH;
                        m_lastVerifyErrorString = ::X509_verify_cert_error_string( m_lastVerifyError );
#endif

                        m_lastVerifyErrorMessage =
                            "Peer verification failed due to the subject name not matching the host name";

                        BL_LOG(
                            Logging::trace(),
                            BL_MSG()
                                << "Peer verification of subject name '"
                                << m_lastVerifySubjectName
                                << "' has failed for host name '"
                                << m_hostName
                                << "'; [depth="
                                << depth
                                << "]"
                            );
                    }
                }
                else
                {
                    /*
                     * preVerified is false when the certificate cannot be checked against
                     * well known root certificates or if some intermediate certificate is
                     * not found in the store, but also not sent from server as well
                     *
                     * In this case we just log the verify error and the subject name and
                     * return false
                     */

                    BL_LOG(
                        Logging::trace(),
                        BL_MSG()
                            << "Pre-verifying of subject name has failed: '"
                            << m_lastVerifySubjectName
                            << "'; [depth="
                            << depth
                            << "]"
                        );

                    m_verifyFailed = true;
                    m_lastVerifyError = ::X509_STORE_CTX_get_error( ctx.native_handle() );

                    if( X509_V_OK != m_lastVerifyError )
                    {
                        const char* errorString = ::X509_verify_cert_error_string( m_lastVerifyError );
                        BL_CHK_CRYPTO_API_NM( errorString );

                        m_lastVerifyErrorString = errorString;
                    }

                    m_lastVerifyErrorMessage =
                        resolveMessage(
                            BL_MSG()
                                << "SSL verify error: "
                                << m_lastVerifyError
                                << "; ['"
                                << (
                                        m_lastVerifyErrorString.empty() ?
                                            std::string( "<N/A>" ) : m_lastVerifyErrorString
                                    )
                                << "']; [depth="
                                << depth
                                << "]"
                            );

                    BL_LOG(
                        Logging::trace(),
                        BL_MSG()
                            << m_lastVerifyErrorMessage
                        );
                }

                }
                catch( std::exception& e )
                {
                    /*
                     * The verify context accessors above can only fail on a corrupted context,
                     * but that is still a verification failure and it is reported as one: the
                     * fields below are what enhanceException() attaches to the handshake error,
                     * so without them the caller would see a bare handshake failure with no
                     * indication that verification was involved. The code is the one OpenSSL
                     * documents for failures raised by the application's own callback
                     */

                    ok = false;
                    m_verifyFailed = true;
                    m_lastVerifyError = X509_V_ERR_APPLICATION_VERIFICATION;
                    m_lastVerifyErrorString = ::X509_verify_cert_error_string( m_lastVerifyError );
                    m_lastVerifyErrorMessage =
                        resolveMessage(
                            BL_MSG()
                                << "Exception in the certificate verify callback: "
                                << e.what()
                            );

                    BL_LOG_MULTILINE(
                        Logging::warning(),
                        BL_MSG()
                            << "AsioSslStreamWrapperT<...>::verifyCertificate"
                            << ": NOEXCEPT block threw an exception, details:\n"
                            << eh::diagnostic_information( e )
                        );
                }

                /*
                 * By default a certificate which could not be verified fails the handshake
                 *
                 * crypto::CryptoBase::allowUntrustedCertificates( true ) restores the previous
                 * behavior, in which the failure is only recorded in the untrusted endpoints map
                 * and logged, so that an application which wants to treat it as a soft error and
                 * prompt the user can do so; see the comment on that method
                 */

                return ( ok || allowUntrustedCertificates() );
            }

            void onHandshakeInternal(
                SAA_in          const completion_callback_t&                        transferCallback,
                SAA_in          const eh::error_code&                               ec
                ) NOEXCEPT
            {
                /*
                 * Record successful handshake and transfer to the callback
                 */

                m_hasHandshakeCompletedSuccessfully = ! ec;

                transferCallback( ec );
            }

            void onShutdownInternal(
                SAA_in          const completion_callback_t&                        transferCallback,
                SAA_in          const eh::error_code&                               ec
                ) NOEXCEPT
            {
                /*
                 * Record successful shutdown and transfer to the callback
                 */

                /*
                 * "eof" is expected error during SSL shutdown as explained at
                 * http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
                 */

                m_hasShutdownCompletedSuccessfully = asio::error::eof == ec ? true : ! ec;

                m_wasShutdownInvoked = true;

                transferCallback( ec );
            }

            /**
             * @brief The OpenSSL message callback which captures the ClientHello this stream sends
             *
             * OpenSSL calls this for every protocol message in both directions, so everything
             * which is not the first handshake message of type ClientHello that we write is
             * ignored. Capturing is a diagnostic - it is what makes a fidelity report state what
             * was actually sent rather than what was requested - so a failure here logs and
             * leaves the buffer empty instead of taking the process down
             */

            static void onSslMessageCallback(
                SAA_in          const int                                           writeP,
                SAA_in          const int                                           version,
                SAA_in          const int                                           contentType,
                SAA_in          const void*                                         buffer,
                SAA_in          const std::size_t                                   size,
                SAA_in          SSL*                                                ssl,
                SAA_in_opt      void*                                               arg
                ) NOEXCEPT
            {
                BL_UNUSED( version );
                BL_UNUSED( ssl );

                BL_WARN_NOEXCEPT_BEGIN()

                if(
                    0 == writeP ||
                    SSL3_RT_HANDSHAKE != contentType ||
                    nullptr == buffer ||
                    nullptr == arg ||
                    size < static_cast< std::size_t >( SSL3_HM_HEADER_LENGTH )
                    )
                {
                    return;
                }

                const auto* const bytes = static_cast< const unsigned char* >( buffer );

                if( SSL3_MT_CLIENT_HELLO != bytes[ 0 ] )
                {
                    return;
                }

                auto* const wrapper = static_cast< this_type* >( arg );

                if( ! wrapper -> m_capturedClientHello.empty() )
                {
                    /*
                     * Only the first ClientHello is kept: a TLS 1.3 HelloRetryRequest makes the
                     * client send a second one, and it is the first which JA3 and JA4 are defined
                     * on and which a peer fingerprints
                     */

                    return;
                }

                wrapper -> m_capturedClientHello.assign( bytes, bytes + size );

                BL_WARN_NOEXCEPT_END( "AsioSslStreamWrapperT<...>::onSslMessageCallback" )
            }

            /**
             * @brief Whether the connection's parameters have actually been negotiated
             *
             * ::SSL_get_version cannot answer this on its own, and reading it as if it could is
             * the trap this exists to close: on a stream which has not handshaked it reports the
             * highest version the method *could* negotiate - OpenSSL 3.5.4 says "TLSv1.3" for a
             * fresh stream on TLS_method - which is indistinguishable from a negotiated result
             *
             * The current cipher is the honest signal: OpenSSL has one exactly when it has a
             * session, which is once the peer's ServerHello has been processed and therefore
             * once the version is settled too
             */

            bool hasNegotiatedSession() const NOEXCEPT
            {
                return nullptr != ::SSL_get_current_cipher( getStream().native_handle() );
            }

        public:

            AsioSslStreamWrapperT(
                SAA_inout       asio::io_service&                                   aioService,
                SAA_in          const std::string&                                  hostName,
                SAA_in          const std::string&                                  serviceName,
                SAA_in_opt      asio::ssl::context*                                 sslServerContextPtr
                )
                :
                m_hostName( hostName ),
                m_serviceName( serviceName ),
                m_isServer( sslServerContextPtr != nullptr ),
                m_sslStream(
                    cpp::SafeUniquePtr< sslstream_t >::attach(
                        new sslstream_t(
                            aioService,
                            sslServerContextPtr ? *sslServerContextPtr : crypto::CryptoBase::getAsioSslContext()
                            )
                        )
                    )
            {
            }

            #if ( ( BOOST_VERSION / 100 ) >= 1072 )

            /**
             * @brief The same, but with the stream bound to a strand
             *
             * asio::ssl::stream forwards its first constructor argument to the next layer, so the
             * socket underneath is constructed on the strand and the strand becomes the default
             * executor for every handler of every operation on it - including the intermediate
             * handlers of the composed operations, which no call site can wrap by hand
             *
             * The guard is on the capability rather than on a devenv version: asio::strand_t is
             * the executor strand only from Boost 1.72 onwards (core/detail/OSBoostImports.h),
             * which is the same condition the executor_type typedef above is guarded by
             */

            AsioSslStreamWrapperT(
                SAA_in          const asio::strand_t&                               strand,
                SAA_in          const std::string&                                  hostName,
                SAA_in          const std::string&                                  serviceName,
                SAA_in_opt      asio::ssl::context*                                 sslServerContextPtr
                )
                :
                m_hostName( hostName ),
                m_serviceName( serviceName ),
                m_isServer( sslServerContextPtr != nullptr ),
                m_sslStream(
                    cpp::SafeUniquePtr< sslstream_t >::attach(
                        new sslstream_t(
                            strand,
                            sslServerContextPtr ? *sslServerContextPtr : crypto::CryptoBase::getAsioSslContext()
                            )
                        )
                    )
            {
            }

            #endif /* ( ( BOOST_VERSION / 100 ) >= 1072 ) */

            /**
             * @brief The client role with a context of its own
             *
             * The constructor above takes the context as an optional pointer and reads a non-null
             * one as the server's, which is why a client that wants its own context - a per-profile
             * one, say - cannot express that through it. Here the context is taken by reference:
             * it is not optional, the role is always the client's, and the pointer/reference
             * asymmetry is what keeps the two constructors apart without a tag argument
             */

            AsioSslStreamWrapperT(
                SAA_inout       asio::io_service&                                   aioService,
                SAA_in          const std::string&                                  hostName,
                SAA_in          const std::string&                                  serviceName,
                SAA_in          asio::ssl::context&                                 sslClientContext
                )
                :
                m_hostName( hostName ),
                m_serviceName( serviceName ),
                m_isServer( false ),
                m_sslStream(
                    cpp::SafeUniquePtr< sslstream_t >::attach(
                        new sslstream_t(
                            aioService,
                            sslClientContext
                            )
                        )
                    )
            {
            }

            ~AsioSslStreamWrapperT() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * Before we get destroyed we have to reset back
                 * the verify callback to verifyCertificateDummy to
                 * ensure that the callback into the internal SSL
                 * socket object no longer refers to the 'this'
                 * pointer of this specific HTTP task object
                 */

                m_sslStream -> set_verify_callback( &this_type::verifyCertificateDummy );

                BL_NOEXCEPT_END()
            }

            /*
             * THE HANDLER IS TAKEN BY FORWARDING REFERENCE AND NEVER BY VALUE, as asio's own streams
             * take it, and a by-value parameter here is a data-loss defect rather than a style
             * choice. asio's composed operations call these two as
             *
             *     stream_.async_write_some( buffers_.prepare( max_size ), static_cast< write_op&& >( *this ) );
             *
             * (impl/write.hpp, and impl/read.hpp makes the same call for a read), where 'buffers_' is
             * a member of the very handler being passed. A by-value parameter MOVE-CONSTRUCTS that
             * handler while the call's arguments are evaluated, and the order in which they are
             * evaluated is unspecified. MSVC and clang-cl, on every target, move the handler first
             * in this call - measured with asio's own async_write( ) over a mock stream - so
             * prepare( ) reads a buffer sequence already moved out of it; clang on Linux evaluates
             * prepare( ) first and never showed it, and GCC has not been measured. A single buffer
             * survives the move unchanged. A std::vector< const_buffer > is left empty, the TLS
             * engine is handed zero octets, and the composed write completes SUCCESSFULLY having
             * sent nothing - which is what every HTTP/1.1 request over TLS did on Windows, because
             * the driver hands async_write( ) a vector, even a GET's holding the head alone
             *
             * Taken by reference, nothing is moved until the stream builds its own operation inside
             * the call, by which time every argument - prepare( )'s included - has been evaluated
             */

            template
            <
                typename MutableBufferSequence,
                typename ReadHandler
            >
            void async_read_some(
                SAA_in          const MutableBufferSequence&                        buffers,
                SAA_in          ReadHandler&&                                       handler
                )
            {
                getStream().async_read_some( buffers, BL_PARAM_FWD( handler ) );
            }

            template
            <
                typename ConstBufferSequence,
                typename WriteHandler
            >
            void async_write_some(
                SAA_in          const ConstBufferSequence&                          buffers,
                SAA_in          WriteHandler&&                                      handler
                )
            {
                getStream().async_write_some( buffers, BL_PARAM_FWD( handler ) );
            }

            bool isChannelOpen() const NOEXCEPT
            {
                return m_sslStream && m_sslStream -> lowest_layer().is_open();
            }

            lowest_layer_type& getSocket() const NOEXCEPT
            {
                BL_ASSERT( m_sslStream );

                return m_sslStream -> lowest_layer();
            }

            lowest_layer_type& lowest_layer() const NOEXCEPT
            {
                return getSocket();
            }

            sslstream_t& getStream() const NOEXCEPT
            {
                BL_ASSERT( m_sslStream );

                return *m_sslStream;
            }

            std::string endpointId() const
            {
                return resolveMessage(
                    BL_MSG()
                        << m_hostName
                        << ":"
                        << m_serviceName
                        );
            }

            bool hasHandshakeCompletedSuccessfully() const NOEXCEPT
            {
                return m_hasHandshakeCompletedSuccessfully;
            }

            bool hasShutdownCompletedSuccessfully() const NOEXCEPT
            {
                return m_hasShutdownCompletedSuccessfully;
            }

            bool wasShutdownInvoked() const NOEXCEPT
            {
                return m_wasShutdownInvoked;
            }

            void enhanceException( SAA_in eh::exception& exception ) const
            {
                /*
                 * Check to enhance the exception with the SSL verify error info if
                 * such is available
                 */

                if( m_verifyFailed )
                {
                    exception
                        << eh::errinfo_ssl_is_verify_failed( m_verifyFailed.value() )
                        << eh::errinfo_ssl_is_verify_error( m_lastVerifyError.value() )
                        << eh::errinfo_ssl_is_verify_error_string( m_lastVerifyErrorString )
                        << eh::errinfo_ssl_is_verify_error_message( m_lastVerifyErrorMessage )
                        << eh::errinfo_ssl_is_verify_subject_name( m_lastVerifySubjectName )
                        ;
                }
            }

            void beginProtocolHandshake( SAA_in const completion_callback_t& transferCallback )
            {
                /*
                 * Before we attempt to download data we need to configure the SSL
                 * stream and do the SSL handshake
                 */

                /*
                 * Peer verification is requested for the client role only
                 *
                 * In the server role asio::ssl::verify_peer means 'request a client certificate'
                 * and the verify callback then decides whether a certificate which was supplied
                 * is acceptable; this library has never authenticated clients by certificate -
                 * the callback used to return true unconditionally - so requesting one and
                 * accepting whatever arrives is equivalent to not requesting one at all, and
                 * once the callback fails closed the two stop being equivalent: a client which
                 * volunteers a certificate the server cannot chain would be rejected
                 *
                 * Mutual TLS is a separate feature and would need an explicit policy, a trust
                 * anchor set for client certificates and a way to surface the client identity
                 */

                getStream().set_verify_mode(
                    m_isServer ? asio::ssl::verify_none : asio::ssl::verify_peer
                    );

                /*
                 * Clear the last verify error info state in case the object has been reused
                 */

                m_hasHandshakeCompletedSuccessfully = false;
                m_hasShutdownCompletedSuccessfully = false;
                m_wasShutdownInvoked = false;

                m_verifyFailed = false;
                m_lastVerifyError = 0;
                m_lastVerifyErrorString.clear();
                m_lastVerifyErrorMessage.clear();
                m_lastVerifySubjectName.clear();

                /*
                 * If global verify callback is not provided then we use the std
                 * rfc2818 verify code provided by Boost ASIO
                 *
                 * The verify callback can acquire a weak reference to the 'this'
                 * pointer as its lifetime is going to be the same as the
                 * underlying SSL stream object associated with this task
                 *
                 * We actually cannot acquire a strong reference to 'this' pointer
                 * here because this will cause a circular reference and because
                 * the object can't be copied
                 *
                 * After we are done with the connection we will reset back the
                 * callback to this_type::verifyCertificateDummy which always will
                 * return false and won't hold weak reference to the object
                 */

                if( g_rfc2818VerifyCallback )
                {
                    getStream().set_verify_callback(
                        cpp::bind(
                            g_rfc2818VerifyCallback,
                            m_hostName,
                            _1 /* preVerified */,
                            _2 /* ctx */
                            )
                        );
                }
                else
                {
                    getStream().set_verify_callback(
                        cpp::bind(
                            &this_type::verifyCertificate,
                            this,
                            asio::ssl::rfc2818_verification( m_hostName ),
                            _1 /* preVerified */,
                            _2 /* ctx */
                            )
                        );
                }

                getStream().async_handshake(
                    m_isServer ? sslstream_t::server : sslstream_t::client,
                    cpp::bind(
                        &this_type::onHandshakeInternal,
                        this,
                        transferCallback,
                        asio::placeholders::error
                        )
                    );
            }

            void beginProtocolShutdown( SAA_in const completion_callback_t& transferCallback )
            {
                /*
                 * This should only be called if the protocol handshake has completed
                 * successfully
                 */

                BL_CHK(
                    false,
                    m_hasHandshakeCompletedSuccessfully.value(),
                    BL_MSG()
                        << "Protocol shutdown should not be called on a stream which did not complete handshake"
                    );

                BL_CHK(
                    true,
                    m_wasShutdownInvoked.value(),
                    BL_MSG()
                        << "Protocol shutdown should not be called more than once"
                    );

                getStream().async_shutdown(
                    cpp::bind(
                        &this_type::onShutdownInternal,
                        this,
                        transferCallback,
                        asio::placeholders::error
                        )
                    );
            }

            void notifyOnSuccessfulHandshakeOrShutdown( SAA_in const bool isHandshake )
            {
                BL_UNUSED( isHandshake );

                /*
                 * On successful shutdown we need to set or clear the untrusted
                 * endpoint info for that endpoint
                 *
                 * This method is to be called by the shutdown handler of the caller
                 */

                if( m_verifyFailed )
                {
                    crypto::CryptoBase::setUntrustedEndpointInfo(
                        endpointId(),
                        cpp::copy( m_lastVerifyErrorMessage )
                        );
                }
                else
                {
                    crypto::CryptoBase::clearUntrustedEndpointInfo( endpointId() );
                }
            }

            /**
             * @brief Offers the given protocol names via ALPN, in descending order of preference
             *
             * To be called before the handshake is started; the offer is a property of the stream
             * and not of the context, so two streams on the same context can offer different lists
             */

            void setAlpnProtocolOffer( SAA_in const std::vector< std::string >& protocolNames )
            {
                BL_CHK(
                    true,
                    protocolNames.empty(),
                    BL_MSG()
                        << "An ALPN protocol offer must name at least one protocol"
                    );

                /*
                 * The wire format is the sequence of names, each prefixed with its length in a
                 * single byte (RFC 7301), so a name outside 1 .. 255 bytes cannot be encoded at
                 * all and is rejected here rather than silently truncated into a malformed
                 * extension - profiles are loadable from data, so this list is not always ours
                 */

                std::vector< unsigned char > wireFormat;

                for( const auto& protocolName : protocolNames )
                {
                    BL_CHK(
                        false,
                        ( ! protocolName.empty() ) && protocolName.size() <= 255U,
                        BL_MSG()
                            << "An ALPN protocol name must be between 1 and 255 bytes long: '"
                            << protocolName
                            << "'"
                        );

                    wireFormat.push_back( static_cast< unsigned char >( protocolName.size() ) );

                    wireFormat.insert( wireFormat.end(), protocolName.cbegin(), protocolName.cend() );
                }

                /*
                 * ::SSL_set_alpn_protos returns *zero on success* and non-zero on failure, which is
                 * the inverse of nearly every other OpenSSL call. It must therefore never be handed
                 * to BL_CHK_CRYPTO_API_NM, which would read every success as a failure; what is
                 * checked is the comparison below, so the OpenSSL error queue still ends up in the
                 * exception exactly as it would for any other crypto call
                 */

                BL_CHK_CRYPTO_API(
                    0 == ::SSL_set_alpn_protos(
                        getStream().native_handle(),
                        wireFormat.data(),
                        static_cast< unsigned int >( wireFormat.size() )
                        ),
                    "Failed to set the ALPN protocol offer on the SSL stream"
                    );
            }

            /**
             * @brief The protocol the peer selected from the ALPN offer
             *
             * Empty before the handshake, and also after one in which the peer selected nothing -
             * which for this library's purposes means HTTP/1.1
             */

            std::string getAlpnSelected() const
            {
                const unsigned char* data = nullptr;
                unsigned int size = 0U;

                ::SSL_get0_alpn_selected( getStream().native_handle(), &data, &size );

                if( nullptr == data || 0U == size )
                {
                    return std::string();
                }

                return std::string( reinterpret_cast< const char* >( data ), size );
            }

            /**
             * @brief The negotiated TLS version, empty before the handshake
             */

            std::string getNegotiatedVersion() const
            {
                /*
                 * Reported only once something really has been negotiated, so that "nothing yet"
                 * reads the same way across all three of these getters - see
                 * hasNegotiatedSession() for why this call cannot simply be forwarded
                 */

                if( ! hasNegotiatedSession() )
                {
                    return std::string();
                }

                const char* const version = ::SSL_get_version( getStream().native_handle() );

                return nullptr == version ? std::string() : std::string( version );
            }

            /**
             * @brief The negotiated cipher suite name, empty before the handshake
             */

            std::string getNegotiatedCipher() const
            {
                /*
                 * The same rule as above, applied to the call it is derived from: a null cipher
                 * is the "nothing has been negotiated yet" case rather than an error
                 */

                const auto* const cipher = ::SSL_get_current_cipher( getStream().native_handle() );

                if( nullptr == cipher )
                {
                    return std::string();
                }

                const char* const name = ::SSL_CIPHER_get_name( cipher );

                return nullptr == name ? std::string() : std::string( name );
            }

            /**
             * @brief Arms the capture of the ClientHello this stream sends
             *
             * Arming discards whatever an earlier handshake on this object captured, so a wrapper
             * which is reused reports the ClientHello of its current handshake
             *
             * Unlike the verify callback, which the destructor resets because it is handed a
             * reference to this object that OpenSSL would otherwise keep, this one needs no
             * teardown: the SSL object it is installed on is owned by this object and dies with
             * it, and ::SSL_free sends no protocol message, so the callback cannot be entered
             * after this object is gone
             */

            void enableClientHelloCapture()
            {
                m_capturedClientHello.clear();

                ::SSL_set_msg_callback( getStream().native_handle(), &this_type::onSslMessageCallback );

                ( void ) SSL_set_msg_callback_arg( getStream().native_handle(), this );
            }

            /**
             * @brief The exact bytes of the ClientHello this stream sent
             *
             * Empty unless enableClientHelloCapture() was called before the handshake; the bytes
             * are the complete handshake message, its four byte header included
             */

            const std::vector< unsigned char >& getCapturedClientHello() const NOEXCEPT
            {
                return m_capturedClientHello;
            }
        };

        BL_DEFINE_STATIC_MEMBER(
            AsioSslStreamWrapperT,
            typename AsioSslStreamWrapperT< TCLASS >::rfc2818_verify_callback_t,
            g_rfc2818VerifyCallback );

        typedef AsioSslStreamWrapperT<> AsioSslStreamWrapper;

    } // tasks

} // bl

#endif /* __BL_BASELIB_TASKS_ASIOSSLSTREAMWRAPPER_H_ */
