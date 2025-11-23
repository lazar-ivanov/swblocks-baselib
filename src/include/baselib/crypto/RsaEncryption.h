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

#ifndef __BL_CRYPTO_RSAENCRYPTION_H_
#define __BL_CRYPTO_RSAENCRYPTION_H_

#include <baselib/crypto/RsaKey.h>

#include <baselib/core/BaseIncludes.h>

#include <openssl/rsa.h>
#include <openssl/sha.h>

/*
 * OpenSSL 3.x has deprecated the legacy RSA APIs in favor of provider-based APIs.
 * We suppress these warnings as we continue to use the legacy (but still supported) APIs.
 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #elif defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #elif defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 4996)
    #endif
#endif

namespace bl
{
    namespace crypto
    {
        /**
         * @brief class RsaEncryption
         */

        template
        <
            typename E = void
        >
        class RsaEncryptionT
        {
        public:

            static auto encrypt(
                SAA_in      const om::ObjPtr< RsaKey >&                   rsaKey,
                SAA_in      const std::string&                            message,
                SAA_inout   unsigned&                                     outputSize
                )
                -> std::unique_ptr< unsigned char[] >
            {
                std::unique_ptr< unsigned char[] > outputBuffer(
                    new unsigned char[ ::RSA_size( &rsaKey -> get() ) ]
                    );

                const auto encryptedSize = ::RSA_public_encrypt(
                    static_cast< int >( message.size() ),
                    const_cast< unsigned char* >(
                        reinterpret_cast< const unsigned char* >( message.c_str() )
                        ),
                    outputBuffer.get(),
                    &rsaKey -> get(),
                    RSA_PKCS1_OAEP_PADDING
                    );

                BL_CHK_CRYPTO_API_NM( encryptedSize != -1 );

                outputSize = static_cast< unsigned >( encryptedSize );

                return outputBuffer;
            }

            static auto encryptAsBase64Url(
                SAA_in  const om::ObjPtr< RsaKey >&                   rsaKey,
                SAA_in  const std::string&                            message
                )
                -> std::string
            {
                unsigned outputSize = 0u;

                const auto outputBuffer = encrypt( rsaKey, message, outputSize );

                return SerializationUtils::base64UrlEncode( outputBuffer.get(), outputSize );
            }

            static auto decrypt(
                SAA_in      const om::ObjPtr< RsaKey >&                   rsaKey,
                SAA_in      const std::string&                            message,
                SAA_inout   unsigned&                                     outputSize
                )
                -> std::unique_ptr< unsigned char[] >
            {
                std::unique_ptr< unsigned char[] > outputBuffer(
                    new unsigned char[ ::RSA_size( &rsaKey -> get() ) ]
                    );

                const auto decryptedSize =
                    RSA_private_decrypt(
                        static_cast< int >( message.size() ),
                        const_cast< unsigned char* >(
                        reinterpret_cast< const unsigned char* >( message.c_str() )
                        ),
                        outputBuffer.get(),
                        &rsaKey -> get(),
                        RSA_PKCS1_OAEP_PADDING
                    );

                BL_CHK_CRYPTO_API_NM( decryptedSize != -1 );

                outputSize = static_cast< unsigned >( decryptedSize );

                return outputBuffer;
            }

            static auto decryptBase64Message(
                SAA_in      const om::ObjPtr< RsaKey >&                  rsaKey,
                SAA_in      const std::string&                           message
                )
                -> std::string
            {
                std::unique_ptr< unsigned char[] > outputBuffer(
                    new unsigned char[ ::RSA_size( &rsaKey -> get() ) ]
                    );

                const auto decodedMessage = SerializationUtils::base64UrlDecodeString( message );

                unsigned outputSize = 0u;

                const auto out = decrypt( rsaKey, decodedMessage, outputSize );

                const auto charBuffer = ( const unsigned char * ) out.get();

                return std::string( reinterpret_cast< const char* >( charBuffer ), outputSize );
            }
        };

        typedef RsaEncryptionT<> RsaEncryption;

    } // crypto

} // bl

/*
 * Restore warning settings after OpenSSL 3.x deprecation suppression
 */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    #if defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(__GNUC__)
        #pragma GCC diagnostic pop
    #elif defined(_MSC_VER)
        #pragma warning(pop)
    #endif
#endif

#endif /* __BL_CRYPTO_RSAENCRYPTION_H_ */
