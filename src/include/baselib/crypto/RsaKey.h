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

#ifndef __BL_CRYPTO_RSAKEY_H_
#define __BL_CRYPTO_RSAKEY_H_

#include <baselib/core/ObjModel.h>

#include <baselib/crypto/BignumBase64Url.h>
#include <baselib/crypto/CryptoBase.h>
#include <baselib/crypto/ErrorHandling.h>
#include <baselib/crypto/OpenSSLTypes.h>

#include <baselib/core/BaseIncludes.h>

#include <openssl/evp.h>

namespace bl
{
    namespace crypto
    {
        template
        <
            typename E = void
        >
        class RsaKeyT :
            public CryptoBase,
            public om::ObjectDefaultBase
        {
        public:

            enum
            {
                /*
                 * 2K for RSA key size should be good enough
                 */

                RSA_KEY_SIZE_DEFAULT                            = 2048,

                /*
                 * RSA_F4 is defined as 0x10001L (65537) in rsa.h
                 *
                 * The exponent must not be too small (e.g. 3) otherwise
                 * the key may be vulnerable to certain attacks:
                 *
                 * http://en.wikipedia.org/wiki/Coppersmith%27s_Attack
                 */

                RSA_KEY_EXPONENT_DEFAULT                        = RSA_F4,

                /*
                 * The smallest RSA modulus size which is accepted when a key is imported
                 *
                 * Keys below this size are not considered to provide meaningful security and
                 * are rejected rather than downgraded
                 */

                RSA_KEY_SIZE_MINIMUM                            = 2048,
            };

        private:

            rsakey_ptr_t m_rsaKey;

        protected:

            RsaKeyT()
            {
                /*
                 * Both OpenSSL 1.x and 3.x+ need to initialize m_rsaKey with RSA_new()
                 * For OpenSSL 3.x+, the key will be populated later via generate() or
                 * by the second constructor that takes an rsakey_ptr_t
                 */
                BL_CHK_CRYPTO_API_NM(
                    m_rsaKey = rsakey_ptr_t::attach( ::RSA_new() )
                    );
            }

            RsaKeyT( SAA_in rsakey_ptr_t&& rsaKey )
                : m_rsaKey( BL_PARAM_FWD( rsaKey ) )
            {
            }

        public:

            void generate()
            {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use EVP_PKEY_keygen APIs
                 */
                EVP_PKEY_CTX* ctx = ::EVP_PKEY_CTX_new_id( EVP_PKEY_RSA, nullptr );
                BL_CHK_CRYPTO_API_NM( ctx );

                BL_SCOPE_EXIT(
                    {
                        ::EVP_PKEY_CTX_free( ctx );
                    }
                    );

                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_keygen_init( ctx ) > 0 );
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_CTX_set_rsa_keygen_bits( ctx, RSA_KEY_SIZE_DEFAULT ) > 0 );

                /*
                 * The public exponent is set explicitly rather than left at the provider's default,
                 * so that RSA_KEY_EXPONENT_DEFAULT governs key generation on this branch as it does
                 * on the 1.x branch below and a change of the OpenSSL default cannot go unnoticed
                 */

                bignum_ptr_t exponent = nullptr;

                BL_CHK_CRYPTO_API_NM(
                    exponent = bignum_ptr_t::attach( ::BN_new() )
                    );

                BL_CHK_CRYPTO_API_NM(
                    ::BN_set_word(
                        exponent.get(),
                        RSA_KEY_EXPONENT_DEFAULT
                        ) == 1
                    );

                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_CTX_set1_rsa_keygen_pubexp( ctx, exponent.get() ) > 0 );

                EVP_PKEY* pkeyRaw = nullptr;
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_keygen( ctx, &pkeyRaw ) > 0 );
                auto pkey = evppkey_ptr_t::attach( pkeyRaw );

                /*
                 * Extract the RSA key from the EVP_PKEY
                 */
                m_rsaKey = rsakey_ptr_t::attach( ::EVP_PKEY_get1_RSA( pkey.get() ) );
                BL_CHK_CRYPTO_API_NM( m_rsaKey );
#else
                bignum_ptr_t exponent = nullptr;

                BL_CHK_CRYPTO_API_NM(
                    exponent = bignum_ptr_t::attach( ::BN_new() )
                    );

                BL_CHK_CRYPTO_API_NM(
                    ::BN_set_word(
                        exponent.get(),
                        RSA_KEY_EXPONENT_DEFAULT
                        ) == 1
                    );

                BL_CHK_CRYPTO_API_NM(
                    ::RSA_generate_key_ex(
                        m_rsaKey.get(),
                        RSA_KEY_SIZE_DEFAULT,
                        exponent.get(),
                        nullptr /* cb_arg */
                        ) == 1
                    );
#endif
            }

            /**
             * @brief Obtains the key as an EVP_PKEY
             *
             * This is the preferred accessor and new code should use it rather than get()
             *
             * OpenSSL 3.x deprecates the low-level RSA APIs in favor of the provider-backed
             * EVP interfaces, and an EVP_PKEY is also the only representation which can refer
             * to a key which is not extractable (a key held in a provider or in hardware)
             *
             * Note that this returns a new EVP_PKEY which holds its own reference to the
             * underlying RSA key rather than a handle onto a stored one; the stored type is
             * still ::RSA and migrating it is tracked separately - see
             * notes/plans/issues/pr-review-residual-cxx-findings-plan.md
             */

            auto evpKey() const -> evppkey_ptr_t
            {
                auto pkey = evppkey_ptr_t::attach( ::EVP_PKEY_new() );

                BL_CHK_CRYPTO_API_NM( pkey );

                BL_CHK_CRYPTO_API_NM(
                    ::EVP_PKEY_set1_RSA( pkey.get(), const_cast< ::RSA* >( m_rsaKey.get() ) )
                    );

                return pkey;
            }

            /**
             * @brief Obtains the underlying RSA key
             *
             * This accessor is legacy; prefer evpKey() above
             *
             * No new call site should be added which operates on the ::RSA object directly,
             * because every such call site has to be rewritten when the stored key type moves
             * to EVP_PKEY
             */

            ::RSA& get() NOEXCEPT
            {
                return *m_rsaKey;
            }

            const ::RSA& get() const NOEXCEPT
            {
                return *m_rsaKey;
            }

            /**
             * @brief Releases the ownership of the underlying RSA key
             *
             * This accessor is legacy; see the note on get() above
             */

            ::RSA* releaseRsa()
            {
                return m_rsaKey.release();
            }
        };

        typedef om::ObjectImpl< RsaKeyT<> > RsaKey;

    } // crypto

} // bl

#endif /* __BL_CRYPTO_RSAKEY_H_ */
