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
        /**
         * @brief Whether an exported private key is encrypted or written in the clear
         *
         * Writing a private key in the clear is a deliberate capability - bl-tool exports keys
         * unencrypted when the caller does not ask for encryption - but it must never be the
         * consequence of an omitted password, which is what it used to be when the password
         * parameter simply defaulted to an empty string
         */

        enum class KeyProtection
        {
            Encrypted,
            PlaintextExplicit,
        };

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

            /**
             * @brief Exports a private key in PKCS#8 PEM format
             *
             * The protection parameter is mandatory on purpose, so that a call site which does
             * not state whether the key material should be encrypted fails to compile rather
             * than silently writing it in the clear; it is placed before the password so that
             * the password keeps its default value and only one parameter becomes mandatory
             */

            static auto getPrivateKeyAsPemString(
                SAA_in          const om::ObjPtr< crypto::RsaKey >&                         rsaKey,
                SAA_in          const KeyProtection                                         protection,
                SAA_in_opt      const std::string&                                          password = str::empty()
                )
                -> std::string
            {
                const auto isEncrypted = ( KeyProtection::Encrypted == protection );

                /*
                 * This invariant closes both of the silent failure modes at once - requesting
                 * encryption without a password, which used to write the key in the clear, and
                 * supplying a password while requesting plaintext, which would encrypt the key
                 * despite the caller having stated otherwise
                 */

                BL_CHK_T(
                    false,
                    isEncrypted != password.empty(),
                    SecurityException(),
                    BL_MSG()
                        << (
                                isEncrypted ?
                                    "An encrypted private key requires a non-empty password" :
                                    "A private key which is exported in the clear must not be given a password"
                            )
                    );

                const auto buffer = crypto::bio_ptr_t::attach( ::BIO_new( ::BIO_s_mem() ) );
                BL_CHK_CRYPTO_API_NM( buffer );

                /*
                 * Private keys are always emitted as PKCS#8 ('-----BEGIN PRIVATE KEY-----' or
                 * '-----BEGIN ENCRYPTED PRIVATE KEY-----') regardless of the version of OpenSSL
                 * we are built with, so the emitted format is a property of the library and not
                 * of the environment which happened to build it
                 *
                 * The legacy PKCS#1 format ('-----BEGIN RSA PRIVATE KEY-----') is still accepted
                 * on read, but it is no longer written; note that its encrypted form derives the
                 * key with a single MD5 iteration while PKCS#8 uses PBES2
                 *
                 * Changing the emitted format is a breaking change and requires a release note
                 */

                auto pkey = crypto::evppkey_ptr_t::attach( ::EVP_PKEY_new() );
                BL_CHK_CRYPTO_API_NM( pkey );
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_set1_RSA( pkey.get(), &rsaKey -> get() ) );

                BL_CHK_CRYPTO_API_NM(
                    ::PEM_write_bio_PrivateKey(
                        buffer.get(),
                        pkey.get(),
                        isEncrypted ? ::EVP_aes_256_cbc() : nullptr             /* AES-256 encryption */,
                        nullptr                                                 /* Key data */,
                        0                                                       /* Key length */,
                        nullptr                                                 /* Password callback */,
                        isEncrypted ? const_cast< char* >( password.c_str() ) : nullptr
                        )
                    );

                return getBufferAsString( buffer );
            }

            static auto getPublicKeyAsPemString( SAA_in const om::ObjPtr< crypto::RsaKey >& rsaKey )
                -> std::string
            {
                const auto buffer = crypto::bio_ptr_t::attach( ::BIO_new( ::BIO_s_mem() ) );
                BL_CHK_CRYPTO_API_NM( buffer );

                /*
                 * Public keys are always emitted as SubjectPublicKeyInfo ('-----BEGIN PUBLIC KEY-----')
                 * regardless of the version of OpenSSL we are built with, so the emitted format is a
                 * property of the library and not of the environment which happened to build it
                 *
                 * The legacy PKCS#1 format ('-----BEGIN RSA PUBLIC KEY-----') is still accepted on
                 * read, but it is no longer written
                 *
                 * Changing the emitted format is a breaking change and requires a release note
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

                chkRsaKeyIsAcceptable( result, KeyCheckDepth::PublicOnly );

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

                /*
                 * Note that only the public half is checked here, deliberately
                 *
                 * The prime factors and the CRT parameters are optional in this representation
                 * (see the loadOptionalProperty calls above), so a JWK which carries only the
                 * modulus, the public exponent and the private exponent is a legal input; the
                 * private key check requires the factors to be present and would start
                 * rejecting such keys
                 */

                chkRsaKeyIsAcceptable( result, KeyCheckDepth::PublicOnly );

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
                const auto buffer = createMemoryBio( pemKeyText );

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

                /*
                 * ::PEM_read_bio_PrivateKey accepts PKCS#8 ('-----BEGIN PRIVATE KEY-----' and
                 * '-----BEGIN ENCRYPTED PRIVATE KEY-----') as well as the legacy PKCS#1 format
                 * ('-----BEGIN RSA PRIVATE KEY-----') which older versions of the library used
                 * to write, so no fallback is required here for any version of OpenSSL
                 *
                 * Note that the legacy format must remain readable indefinitely as the keys
                 * which were persisted in it outlive the toolchain which wrote them
                 */

                const auto pkeyPtr = crypto::evppkey_ptr_t::attach(
                    ::PEM_read_bio_PrivateKey(
                        buffer.get(),
                        nullptr                 /* EVP_PKEY */,
                        nullptr                 /* Password callback */,
                        passwordBytes
                        )
                    );

                BL_CHK_CRYPTO_API_NM( pkeyPtr );
                BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_base_id( pkeyPtr.get() ) == EVP_PKEY_RSA );

                auto rsa = crypto::rsakey_ptr_t::attach( ::EVP_PKEY_get1_RSA( pkeyPtr.get() ) );
                BL_CHK_CRYPTO_API_NM( rsa );

                auto result = crypto::RsaKey::template createInstance< crypto::RsaKey >( std::move( rsa ) );

                /*
                 * Both of the private key encodings which are accepted here (PKCS#1 and PKCS#8)
                 * always carry the prime factors, so the full check can be requested
                 */

                chkRsaKeyIsAcceptable( result, KeyCheckDepth::Full );

                return result;
            }

            static auto loadPublicKeyFromPemString(
                SAA_in          const std::string&                                          pemKeyText,
                SAA_in_opt      const std::string&                                          password = str::empty()
                )
                -> om::ObjPtr< crypto::RsaKey >
            {
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

                /*
                 * Public keys are accepted both as SubjectPublicKeyInfo ('-----BEGIN PUBLIC KEY-----')
                 * and as the legacy PKCS#1 format ('-----BEGIN RSA PUBLIC KEY-----') which older
                 * versions of the library used to write; the legacy format must remain readable
                 * indefinitely as the keys which were persisted in it outlive the toolchain which
                 * wrote them
                 *
                 * The first attempt is expected to fail for the legacy format when we are built
                 * against OpenSSL 1.x where ::PEM_read_bio_PUBKEY only understands the former; for
                 * OpenSSL 3.x+ it is implemented over the decoder APIs and understands both
                 *
                 * A failed attempt leaves entries in the OpenSSL error queue which survive into
                 * later operations, so the queue must be cleared before the second attempt is made
                 * or an unrelated failure later on can be reported with a stale reason string
                 *
                 * Note that a fresh BIO is created for the second attempt rather than rewinding
                 * the one which was used for the first
                 */

                crypto::rsakey_ptr_t rsa;

                {
                    const auto buffer = createMemoryBio( pemKeyText );

                    const auto pkeyPtr = crypto::evppkey_ptr_t::attach(
                        ::PEM_read_bio_PUBKEY(
                            buffer.get(),
                            nullptr                 /* EVP_PKEY */,
                            nullptr                 /* Password callback */,
                            passwordBytes
                            )
                        );

                    if( pkeyPtr )
                    {
                        BL_CHK_CRYPTO_API_NM( ::EVP_PKEY_base_id( pkeyPtr.get() ) == EVP_PKEY_RSA );

                        rsa = crypto::rsakey_ptr_t::attach( ::EVP_PKEY_get1_RSA( pkeyPtr.get() ) );
                    }
                }

                if( ! rsa )
                {
                    BL_CHK_CRYPTO_API_RESET_ERROR();

                    const auto buffer = createMemoryBio( pemKeyText );

                    rsa = crypto::rsakey_ptr_t::attach(
                        ::PEM_read_bio_RSAPublicKey(
                            buffer.get(),
                            nullptr                 /* RSA key */,
                            nullptr                 /* Password callback */,
                            passwordBytes
                            )
                        );
                }

                BL_CHK_CRYPTO_API_NM( rsa );

                auto result = crypto::RsaKey::template createInstance< crypto::RsaKey >( std::move( rsa ) );

                chkRsaKeyIsAcceptable( result, KeyCheckDepth::PublicOnly );

                return result;
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

            /**
             * @brief How thoroughly an imported key is validated
             *
             * Full additionally runs the private key consistency check, which requires the
             * prime factors to be present and must therefore only be requested for the inputs
             * which always carry them
             */

            enum class KeyCheckDepth
            {
                PublicOnly,
                Full,
            };

            /**
             * @brief Rejects an imported RSA key which does not meet the minimum policy
             *
             * The modulus size floor applies on every version of OpenSSL; the structural key
             * checks are only available from OpenSSL 1.1.1 onwards (and the public one only
             * from OpenSSL 3.0), so on the older versions the modulus size is the only thing
             * which is verified
             *
             * Note that ::RSA_size is used rather than ::RSA_bits because the latter does not
             * exist before OpenSSL 1.1.0
             */

            static void chkRsaKeyIsAcceptable(
                SAA_in          const om::ObjPtr< crypto::RsaKey >&                         rsaKey,
                SAA_in          const KeyCheckDepth                                         depth
                )
            {
                const auto modulusBits = ::RSA_size( &rsaKey -> get() ) * 8;

                BL_CHK_T(
                    false,
                    modulusBits >= static_cast< int >( crypto::RsaKey::RSA_KEY_SIZE_MINIMUM ),
                    SecurityException(),
                    BL_MSG()
                        << "The RSA key modulus size of "
                        << modulusBits
                        << " bits is below the minimum of "
                        << static_cast< int >( crypto::RsaKey::RSA_KEY_SIZE_MINIMUM )
                        << " bits"
                    );

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
                /*
                 * ::EVP_PKEY_check and ::EVP_PKEY_public_check were introduced in OpenSSL 1.1.1
                 * and ::EVP_PKEY_private_check in OpenSSL 3.0
                 *
                 * On OpenSSL 3.x the public check also enforces a policy on the public exponent
                 * (it must lie between 2^16 and 2^256), so a key with a small exponent such as
                 * 3 is rejected here; that is intentional and matches the reasoning already
                 * recorded on RsaKeyT::RSA_KEY_EXPONENT_DEFAULT
                 *
                 * OpenSSL 1.1.1 has no RSA implementation of the public key check (the generic
                 * ::EVP_PKEY_public_check reports the operation as unsupported for the key
                 * type), so at the PublicOnly depth the modulus size floor above is the only
                 * thing which is verified there; ::EVP_PKEY_check is the full RSA key check on
                 * that version, which requires the private components and which is what the
                 * Full depth maps to
                 *
                 * The result is compared against 1 explicitly because these functions return a
                 * negative value when the operation is not supported for the key type, which a
                 * plain truthiness check would accept as success
                 */

#if OPENSSL_VERSION_NUMBER < 0x30000000L
                if( KeyCheckDepth::Full != depth )
                {
                    return;
                }
#endif

                const auto pkey = rsaKey -> evpKey();

                const auto ctx = crypto::evppkeyctx_ptr_t::attach(
                    ::EVP_PKEY_CTX_new( pkey.get(), nullptr )
                    );

                BL_CHK_CRYPTO_API_NM( ctx );

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
                const int checkResult =
                    KeyCheckDepth::Full == depth
                        ? ::EVP_PKEY_private_check( ctx.get() )
                        : ::EVP_PKEY_public_check( ctx.get() );
#else
                const int checkResult = ::EVP_PKEY_check( ctx.get() );
#endif

                BL_CHK_CRYPTO_API_NM( 1 == checkResult );
#else
                BL_UNUSED( depth );
#endif
            }

            static auto createMemoryBio( SAA_in const std::string& pemKeyText ) -> crypto::bio_ptr_t
            {
                auto buffer = crypto::bio_ptr_t::attach(
                    ::BIO_new_mem_buf(
                        const_cast< char* >( pemKeyText.c_str() ),
                        static_cast< int >( pemKeyText.size() )
                        )
                    );

                BL_CHK_CRYPTO_API_NM( buffer );

                return buffer;
            }

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
