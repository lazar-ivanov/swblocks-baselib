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

#ifndef __BL_CRYPTO_HMACSHA256_H_
#define __BL_CRYPTO_HMACSHA256_H_

#include <baselib/core/CPP.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/SerializationUtils.h>

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/params.h>
#endif

namespace bl
{
    namespace crypto
    {
        namespace detail
        {
            /**
             * @brief class HmacSha256
             */

            template
            <
                typename E = void
            >
            class HmacSha256T FINAL
            {
                BL_DECLARE_STATIC( HmacSha256T )

            public:

                static std::string calculateMessageDigest(
                    SAA_in  const std::string&                              message,
                    SAA_in  const std::string&                              key
                    )
                {
                    unsigned char messageDigest[ SHA256_DIGEST_LENGTH ];
                    unsigned int digestLength = sizeof( messageDigest );

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                    /*
                     * OpenSSL 3.x+: the whole HMAC_* interface is deprecated in favour of EVP_MAC,
                     * so this branch - which exists specifically for 3.x - is written against
                     * EVP_MAC rather than against APIs 3.x itself deprecates
                     *
                     * EVP_MAC is also the only one of the two which can be backed by a provider,
                     * so unlike the RSA paths this needs no legacy bridge
                     */

                    ::EVP_MAC* mac = ::EVP_MAC_fetch( nullptr, "HMAC", nullptr );
                    BL_CHK_CRYPTO_API_NM( mac );

                    BL_SCOPE_EXIT(
                        {
                            ::EVP_MAC_free( mac );
                        }
                        );

                    ::EVP_MAC_CTX* ctx = ::EVP_MAC_CTX_new( mac );
                    BL_CHK_CRYPTO_API_NM( ctx );

                    BL_SCOPE_EXIT(
                        {
                            ::EVP_MAC_CTX_free( ctx );
                        }
                        );

                    /*
                     * The digest is selected by name through the parameter list rather than by
                     * passing an EVP_MD*, which is how the provider based interface is configured
                     */

                    char digestName[] = "SHA256";

                    ::OSSL_PARAM params[] =
                    {
                        ::OSSL_PARAM_construct_utf8_string(
                            OSSL_MAC_PARAM_DIGEST,
                            digestName,
                            0U
                            ),
                        ::OSSL_PARAM_construct_end(),
                    };

                    BL_CHK_CRYPTO_API_NM(
                        ::EVP_MAC_init(
                            ctx,
                            reinterpret_cast< const unsigned char* >( key.c_str() ),
                            key.length(),
                            params
                            )
                        );

                    BL_CHK_CRYPTO_API_NM(
                        ::EVP_MAC_update(
                            ctx,
                            reinterpret_cast< const unsigned char* >( message.c_str() ),
                            message.length()
                            )
                        );

                    std::size_t macLength = 0U;

                    BL_CHK_CRYPTO_API_NM(
                        ::EVP_MAC_final(
                            ctx,
                            messageDigest,
                            &macLength,
                            sizeof( messageDigest )
                            )
                        );

                    /*
                     * HMAC-SHA-256 is 32 bytes by definition; anything else means the digest was
                     * not the one requested
                     */

                    BL_CHK_CRYPTO_API_NM( macLength == sizeof( messageDigest ) );

                    digestLength = static_cast< unsigned int >( macLength );
#else
                    /*
                     * OpenSSL 1.1.x: HMAC_CTX can be stack-allocated
                     */
                    ::HMAC_CTX ctx;

                    ( void ) ::HMAC_CTX_init( &ctx );

                    BL_SCOPE_EXIT(
                        {
                            ( void ) ::HMAC_CTX_cleanup( &ctx );
                        }
                        );

                    BL_CHK_CRYPTO_API_NM(
                        ::HMAC_Init_ex(
                            &ctx,
                            key.c_str(),
                            static_cast< int >( key.length() ),
                            EVP_sha256(),
                            nullptr
                            )
                        );

                    BL_CHK_CRYPTO_API_NM(
                        ::HMAC_Update(
                            &ctx,
                            reinterpret_cast< const unsigned char* >( message.c_str() ),
                            message.length()
                            )
                        );

                    BL_CHK_CRYPTO_API_NM(
                        ::HMAC_Final(
                            &ctx,
                            messageDigest,
                            &digestLength
                            )
                        );
#endif

                    cpp::SafeOutputStringStream stream;

                    stream
                        << std::hex
                        << std::setfill( '0' )
                        << std::uppercase;

                    for( std::size_t count = 0; count < digestLength; ++count )
                    {
                        stream
                            << std::setw( 2 )
                            << static_cast< unsigned >( messageDigest[ count ] );
                    }

                    return stream.str();
                }
            };

            typedef HmacSha256T< > HmacSha256;

        } // detail

    } // crypto

} // bl

#endif /* __BL_CRYPTO_HMACSHA256_H_ */
