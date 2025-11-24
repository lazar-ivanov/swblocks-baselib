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

#ifndef __BL_SECURITY_JSONSECURITYSERIALIZATIONIMPL_H_
#define __BL_SECURITY_JSONSECURITYSERIALIZATIONIMPL_H_

#include <baselib/crypto/RsaKey.h>

#include <baselib/core/StringUtils.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace security
    {
        template
        <
            typename POLICY
        >
        class JsonSecuritySerializationImpl
        {
            BL_DECLARE_STATIC( JsonSecuritySerializationImpl )

        private:

            typedef crypto::BignumBase64Url                                     BignumBase64Url;

            typedef typename POLICY::RsaPublicKey                               RsaPublicKey;
            typedef typename POLICY::RsaPrivateKey                              RsaPrivateKey;
            typedef typename POLICY::KeyType                                    KeyType;
            typedef typename POLICY::SigningAlgorithm                           SigningAlgorithm;
            typedef typename POLICY::PublicKeyUse                               PublicKeyUse;

        public:

            static auto getPublicKeyAsJsonObject( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> om::ObjPtr< RsaPublicKey >
            {
                const auto& rsaKeyImpl = rsaKey -> get();
                auto rsaPublicKey = RsaPublicKey::template createInstance<>();

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use RSA_get0_key() to access components
                 */
                const BIGNUM* n = nullptr;
                const BIGNUM* e = nullptr;
                const BIGNUM* d = nullptr;
                ( void ) ::RSA_get0_key( &rsaKeyImpl, &n, &e, &d );

                BL_CHK_CRYPTO_API_NM( n && e );

                rsaPublicKey -> exponent( BignumBase64Url::bignumToBase64Url( e ) );
                rsaPublicKey -> modulus( BignumBase64Url::bignumToBase64Url( n ) );
#else
                rsaPublicKey -> exponent( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.e ) );
                rsaPublicKey -> modulus( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.n ) );
#endif

                rsaPublicKey -> keyType( KeyType::toString( KeyType::RSA ) );
                rsaPublicKey -> algorithm( SigningAlgorithm::toString( SigningAlgorithm::RS512 ) );
                rsaPublicKey -> publicKeyUse( PublicKeyUse::toString( PublicKeyUse::sig ) );

                return rsaPublicKey;
            }

            static auto getPublicKeyAsJsonString( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> std::string
            {
                return BL_DM_GET_AS_PRETTY_JSON_STRING( getPublicKeyAsJsonObject( rsaKey ) );
            }

            static auto getPrivateKeyAsJsonObject( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> om::ObjPtr< RsaPrivateKey >
            {
                const auto& rsaKeyImpl = rsaKey -> get();
                auto rsaPrivateKey = RsaPrivateKey::template createInstance<>();

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use RSA_get0_* functions to access components
                 */
                const BIGNUM* n = nullptr;
                const BIGNUM* e = nullptr;
                const BIGNUM* d = nullptr;
                ( void ) ::RSA_get0_key( &rsaKeyImpl, &n, &e, &d );

                BL_CHK_CRYPTO_API_NM( n && e && d );

                rsaPrivateKey -> exponent( BignumBase64Url::bignumToBase64Url( e ) );
                rsaPrivateKey -> modulus( BignumBase64Url::bignumToBase64Url( n ) );
                rsaPrivateKey -> privateExponent( BignumBase64Url::bignumToBase64Url( d ) );

                const BIGNUM* p = nullptr;
                const BIGNUM* q = nullptr;
                ( void ) ::RSA_get0_factors( &rsaKeyImpl, &p, &q );

                if( p )
                {
                    rsaPrivateKey -> firstPrimeFactor( BignumBase64Url::bignumToBase64Url( p ) );
                }

                if( q )
                {
                    rsaPrivateKey -> secondPrimeFactor( BignumBase64Url::bignumToBase64Url( q ) );
                }

                const BIGNUM* dmp1 = nullptr;
                const BIGNUM* dmq1 = nullptr;
                const BIGNUM* iqmp = nullptr;
                ( void ) ::RSA_get0_crt_params( &rsaKeyImpl, &dmp1, &dmq1, &iqmp );

                if( dmp1 )
                {
                    rsaPrivateKey -> firstFactorCrtExponent( BignumBase64Url::bignumToBase64Url( dmp1 ) );
                }

                if( dmq1 )
                {
                    rsaPrivateKey -> secondFactorCrtExponent( BignumBase64Url::bignumToBase64Url( dmq1 ) );
                }

                if( iqmp )
                {
                    rsaPrivateKey -> firstCrtCoefficient( BignumBase64Url::bignumToBase64Url( iqmp ) );
                }
#else
                rsaPrivateKey -> exponent( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.e ) );
                rsaPrivateKey -> modulus( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.n ) );
                rsaPrivateKey -> privateExponent( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.d ) );

                if( rsaKeyImpl.p )
                {
                    rsaPrivateKey -> firstPrimeFactor( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.p ) );
                }

                if( rsaKeyImpl.q )
                {
                    rsaPrivateKey -> secondPrimeFactor( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.q ) );
                }

                if( rsaKeyImpl.dmp1 )
                {
                    rsaPrivateKey -> firstFactorCrtExponent( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.dmp1 ) );
                }

                if( rsaKeyImpl.dmq1 )
                {
                    rsaPrivateKey -> secondFactorCrtExponent( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.dmq1 ) );
                }

                if( rsaKeyImpl.iqmp )
                {
                    rsaPrivateKey -> firstCrtCoefficient( BignumBase64Url::bignumToBase64Url( rsaKeyImpl.iqmp ) );
                }
#endif

                rsaPrivateKey -> keyType( KeyType::toString( KeyType::RSA ) );
                rsaPrivateKey -> algorithm( SigningAlgorithm::toString( SigningAlgorithm::RS512 ) );

                return rsaPrivateKey;
            }

            static auto getPrivateKeyAsJsonString( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> std::string
            {
                return BL_DM_GET_AS_PRETTY_JSON_STRING( getPrivateKeyAsJsonObject( rsaKey ) );
            }

            static auto getPrivateKeyAsPemString(
                SAA_in          const om::ObjPtr< crypto::RsaKey >&                         rsaKey,
                SAA_in_opt      const std::string&                                          password = str::empty()
                )
                -> std::string
            {
                const auto buffer = crypto::bio_ptr_t::attach( ::BIO_new( ::BIO_s_mem() ) );
                BL_CHK_CRYPTO_API_NM( buffer );

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use EVP_PKEY-based PEM write functions
                 * First convert RSA to EVP_PKEY
                 */
                auto pkey = crypto::evppkey_ptr_t::attach( ::EVP_PKEY_new() );
                BL_CHK_CRYPTO_API_NM( pkey );
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_set1_RSA( pkey.get(), &rsaKey -> get() ) );

                BL_CHK_CRYPTO_API_NM(
                    ::PEM_write_bio_PrivateKey(
                        buffer.get(),
                        pkey.get(),
                        password.empty() ? nullptr : ::EVP_des_ede3_cbc()       /* Triple DES encryption */,
                        nullptr                                                 /* Key data */,
                        0                                                       /* Key length */,
                        nullptr                                                 /* Password callback */,
                        password.empty() ? nullptr : const_cast< char* >( password.c_str() )
                        )
                    );
#else
                BL_CHK_CRYPTO_API_NM(
                    ::PEM_write_bio_RSAPrivateKey(
                        buffer.get(),
                        &rsaKey -> get(),
                        password.empty() ? nullptr : ::EVP_des_ede3_cbc()       /* Triple DES encryption */,
                        nullptr                                                 /* Key data */,
                        0                                                       /* Key length */,
                        nullptr                                                 /* Password callback */,
                        password.empty() ? nullptr : const_cast< char* >( password.c_str() )
                        )
                    );
#endif

                return getBufferAsString( buffer );
            }

            static auto getPublicKeyAsPemString( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> std::string
            {
                const auto buffer = crypto::bio_ptr_t::attach( ::BIO_new( ::BIO_s_mem() ) );
                BL_CHK_CRYPTO_API_NM( buffer );

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use EVP_PKEY-based PEM write functions
                 */
                auto pkey = crypto::evppkey_ptr_t::attach( ::EVP_PKEY_new() );
                BL_CHK_CRYPTO_API_NM( pkey );
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_set1_RSA( pkey.get(), &rsaKey -> get() ) );

                BL_CHK_CRYPTO_API_NM(
                    ::PEM_write_bio_PUBKEY(
                        buffer.get(),
                        pkey.get()
                        )
                    );
#else
                BL_CHK_CRYPTO_API_NM(
                    ::PEM_write_bio_RSAPublicKey(
                        buffer.get(),
                        &rsaKey -> get()
                        )
                    );
#endif

                return getBufferAsString( buffer );
            }

            static auto loadPublicKeyFromJsonObject( SAA_in const om::ObjPtr< RsaPublicKey >& dataObject )
                -> om::ObjPtr< crypto::RsaKey >
            {
                auto result = crypto::RsaKey::template createInstance< crypto::RsaKey >();
                auto& rsa = result -> get();

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use RSA_set0_key() to set components
                 * RSA_set0_key takes ownership on success, so release from smart pointers
                 */
                auto n = BignumBase64Url::base64UrlToBignum( dataObject -> modulus() );
                auto e = BignumBase64Url::base64UrlToBignum( dataObject -> exponent() );

                BL_CHK_CRYPTO_API_NM( ::RSA_set0_key( &rsa, n.get(), e.get(), nullptr ) );
                ( void ) n.release();
                ( void ) e.release();
#else
                loadRequiredProperty( dataObject -> exponent(), rsa, &RSA::e );
                loadRequiredProperty( dataObject -> modulus(), rsa, &RSA::n );
#endif

                return result;
            }

            static auto loadPublicKeyFromJsonString( SAA_in const std::string& jsonText )
                -> om::ObjPtr< crypto::RsaKey >
            {
                const auto rsaPublicKey = BL_DM_LOAD_FROM_JSON_STRING( RsaPublicKey, jsonText );

                return loadPublicKeyFromJsonObject( rsaPublicKey );
            }

            static auto loadPrivateKeyFromJsonObject( SAA_in const om::ObjPtr< RsaPrivateKey >& dataObject )
                -> om::ObjPtr< crypto::RsaKey >
            {
                auto result = crypto::RsaKey::template createInstance< crypto::RsaKey >();
                auto& rsa = result -> get();

                dataObject -> keyType( KeyType::toString( KeyType::RSA ) );

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use RSA_set0_* functions to set components
                 * RSA_set0_key takes ownership on success, so release from smart pointers
                 */
                auto n = BignumBase64Url::base64UrlToBignum( dataObject -> modulus() );
                auto e = BignumBase64Url::base64UrlToBignum( dataObject -> exponent() );
                auto d = BignumBase64Url::base64UrlToBignum( dataObject -> privateExponent() );

                BL_CHK_CRYPTO_API_NM( ::RSA_set0_key( &rsa, n.get(), e.get(), d.get() ) );
                ( void ) n.release();
                ( void ) e.release();
                ( void ) d.release();

                if( ! dataObject -> firstPrimeFactor().empty() && ! dataObject -> secondPrimeFactor().empty() )
                {
                    auto p = BignumBase64Url::base64UrlToBignum( dataObject -> firstPrimeFactor() );
                    auto q = BignumBase64Url::base64UrlToBignum( dataObject -> secondPrimeFactor() );

                    BL_CHK_CRYPTO_API_NM( ::RSA_set0_factors( &rsa, p.get(), q.get() ) );
                    ( void ) p.release();
                    ( void ) q.release();
                }

                if( ! dataObject -> firstFactorCrtExponent().empty() &&
                    ! dataObject -> secondFactorCrtExponent().empty() &&
                    ! dataObject -> firstCrtCoefficient().empty() )
                {
                    auto dmp1 = BignumBase64Url::base64UrlToBignum( dataObject -> firstFactorCrtExponent() );
                    auto dmq1 = BignumBase64Url::base64UrlToBignum( dataObject -> secondFactorCrtExponent() );
                    auto iqmp = BignumBase64Url::base64UrlToBignum( dataObject -> firstCrtCoefficient() );

                    BL_CHK_CRYPTO_API_NM( ::RSA_set0_crt_params( &rsa, dmp1.get(), dmq1.get(), iqmp.get() ) );
                    ( void ) dmp1.release();
                    ( void ) dmq1.release();
                    ( void ) iqmp.release();
                }
#else
                loadRequiredProperty( dataObject -> exponent(), rsa, &::RSA::e );
                loadRequiredProperty( dataObject -> modulus(), rsa, &::RSA::n );
                loadRequiredProperty( dataObject -> privateExponent(), rsa, &::RSA::d );

                loadOptionalProperty( dataObject -> firstPrimeFactor(), rsa, &::RSA::p );
                loadOptionalProperty( dataObject -> secondPrimeFactor(), rsa, &::RSA::q );
                loadOptionalProperty( dataObject -> firstFactorCrtExponent(), rsa, &::RSA::dmp1 );
                loadOptionalProperty( dataObject -> secondFactorCrtExponent(), rsa, &::RSA::dmq1 );
                loadOptionalProperty( dataObject -> firstCrtCoefficient(), rsa, &::RSA::iqmp );
#endif

                return result;
            }

            static auto loadPrivateKeyFromJsonString( SAA_in const std::string& jsonText )
                -> om::ObjPtr< crypto::RsaKey >
            {
                const auto rsaPrivateKey = BL_DM_LOAD_FROM_JSON_STRING( RsaPrivateKey, jsonText );

                return loadPrivateKeyFromJsonObject( rsaPrivateKey );
            }

            static auto loadPrivateKeyFromPemString(
                SAA_in          const std::string&                                          pemKeyText,
                SAA_in_opt      const std::string&                                          password = str::empty()
                )
                -> om::ObjPtr< crypto::RsaKey >
            {
                const auto buffer = crypto::bio_ptr_t::attach(
                    ::BIO_new_mem_buf(
                        const_cast< char* >( pemKeyText.c_str() ),
                        static_cast< int >( pemKeyText.size() )
                        )
                    );

                BL_CHK_CRYPTO_API_NM( buffer );

                auto* passwordBytes = const_cast< char* >( password.c_str() );
                std::string randomPassword;

                if( password.empty() )
                {
                    /*
                     * If the password is empty then we provide random password which is
                     * guaranteed to be invalid to force OpenSSL to fail instead of doing
                     * the prompt for password and the password is of course only used
                     * if the key is encrypted (otherwise it is ignored)
                     *
                     * Maybe there is a smarter way to do this, but I couldn't figure how
                     * to do this more elegantly
                     */

                    randomPassword = uuids::uuid2string( uuids::create() );
                    passwordBytes = const_cast< char* >( randomPassword.c_str() );
                }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use EVP_PKEY-based PEM read, then extract RSA
                 */
                auto pkeyPtr = crypto::evppkey_ptr_t::attach(
                    ::PEM_read_bio_PrivateKey(
                        buffer.get(),
                        nullptr                 /* EVP_PKEY */,
                        nullptr                 /* Password callback */,
                        passwordBytes
                        )
                    );

                BL_CHK_CRYPTO_API_NM( pkeyPtr );

                auto rsa = crypto::rsakey_ptr_t::attach( ::EVP_PKEY_get1_RSA( pkeyPtr.get() ) );
                BL_CHK_CRYPTO_API_NM( rsa );
#else
                auto rsa = crypto::rsakey_ptr_t::attach(
                    ::PEM_read_bio_RSAPrivateKey(
                        buffer.get(),
                        nullptr                 /* RSA key */,
                        nullptr                 /* Password callback */,
                        passwordBytes
                        )
                    );

                BL_CHK_CRYPTO_API_NM( rsa );
#endif

                return crypto::RsaKey::template createInstance< crypto::RsaKey >( std::move( rsa ) );
            }

            static auto loadPublicKeyFromPemString(
                SAA_in          const std::string&                                          pemKeyText,
                SAA_in_opt      const std::string&                                          password = str::empty()
                )
                -> om::ObjPtr< crypto::RsaKey >
            {
                const auto buffer = crypto::bio_ptr_t::attach(
                    ::BIO_new_mem_buf(
                        const_cast< char* >( pemKeyText.c_str() ),
                        static_cast< int >( pemKeyText.size() )
                        )
                    );

                BL_CHK_CRYPTO_API_NM( buffer );

                auto* passwordBytes = const_cast< char* >( password.c_str() );
                std::string randomPassword;

                if( password.empty() )
                {
                    /*
                     * If the password is empty then we provide random password which is
                     * guaranteed to be invalid to force OpenSSL to fail instead of doing
                     * the prompt for password and the password is of course only used
                     * if the key is encrypted (otherwise it is ignored)
                     *
                     * Maybe there is a smarter way to do this, but I couldn't figure how
                     * to do this more elegantly
                     */

                    randomPassword = uuids::uuid2string( uuids::create() );
                    passwordBytes = const_cast< char* >( randomPassword.c_str() );
                }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                /*
                 * OpenSSL 3.x+: Use EVP_PKEY-based PEM read, then extract RSA
                 */
                auto pkeyPtr = crypto::evppkey_ptr_t::attach(
                    ::PEM_read_bio_PUBKEY(
                        buffer.get(),
                        nullptr                 /* EVP_PKEY */,
                        nullptr                 /* Password callback */,
                        passwordBytes
                        )
                    );

                BL_CHK_CRYPTO_API_NM( pkeyPtr );

                auto rsa = crypto::rsakey_ptr_t::attach( ::EVP_PKEY_get1_RSA( pkeyPtr.get() ) );
                BL_CHK_CRYPTO_API_NM( rsa );
#else
                auto rsa = crypto::rsakey_ptr_t::attach(
                    ::PEM_read_bio_RSAPublicKey(
                        buffer.get(),
                        nullptr                 /* RSA key */,
                        nullptr                 /* Password callback */,
                        passwordBytes
                        )
                    );

                BL_CHK_CRYPTO_API_NM( rsa );
#endif

                return crypto::RsaKey::template createInstance< crypto::RsaKey >( std::move( rsa ) );
            }

            static auto getBufferAsString( SAA_in const crypto::bio_ptr_t& buffer ) -> std::string
            {
                const int length = BIO_pending( buffer.get() );

                std::string text;
                text.resize( length, '\0' );

                BL_CHK_CRYPTO_API_NM( ::BIO_read( buffer.get(), const_cast< char* >( text.data() ), length ) );

                return text;
            }

        private:

            static void loadRequiredProperty(
                SAA_in          const std::string&                                          property,
                SAA_in          ::RSA&                                                      rsa,
                SAA_in          ::BIGNUM* ::RSA::*                                          member
                )
            {
                const auto bignum = BignumBase64Url::base64UrlToBignum( property );

                rsa.*member = ::BN_dup( bignum.get() );

                BL_CHK_CRYPTO_API_NM( rsa.*member );
            }

            static void loadOptionalProperty(
                SAA_in          const std::string&                                          property,
                SAA_in          ::RSA&                                                      rsa,
                SAA_in          ::BIGNUM* ::RSA::*                                          member
                )
            {
                if( ! property.empty() )
                {
                    loadRequiredProperty( property, rsa, member );
                }
            }
        };

    } // security

} // bl

#endif /* __BL_SECURITY_JSONSECURITYSERIALIZATIONIMPL_H_ */
