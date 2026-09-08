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

#include <baselib/crypto/RsaKey.h>
#include <baselib/crypto/CryptoBase.h>
#include <baselib/crypto/X509Cert.h>
#include <baselib/crypto/RsaEncryption.h>

#include <baselib/data/models/Jose.h>

#include <baselib/data/eh/ServerErrorHelpers.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/TestUtils.h>

#include <openssl/err.h>
#include <openssl/pem.h>

namespace
{
    /**
     * @brief A memory BIO over a buffer which holds no certificate
     *
     * Handing it to ::PEM_read_bio_X509() is a deterministic OpenSSL failure - it pushes at
     * least one entry ('no start line') onto the error queue on every version we support -
     * which is the single point of failure injection shared by the crypto error handling
     * cases below, so that the two can never disagree about how the error is produced
     */

    auto createNotACertificateBio() -> bl::crypto::bio_ptr_t
    {
        /*
         * ::BIO_new_mem_buf() does not copy the buffer it is handed, so the text has to
         * outlive every BIO created over it
         */

        static const std::string g_notACertificate( "not a certificate" );

        auto buffer = bl::crypto::bio_ptr_t::attach(
            ::BIO_new_mem_buf(
                const_cast< char* >( g_notACertificate.c_str() ),
                bl::crypto::toIntSize( g_notACertificate.size() )
                )
            );

        UTF_REQUIRE( buffer );

        return buffer;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( CryptoUtils_InitSsl )
{
    bl::crypto::CryptoBase::init();

    /*
     * The initialization is guarded and runs exactly once - an init() which re-initialized
     * would create a new process global client context and throw away every trusted root
     * which had been loaded into the old one
     */

    const auto* contextBefore = &bl::crypto::CryptoBase::getAsioSslContext();

    bl::crypto::CryptoBase::init();

    UTF_REQUIRE_EQUAL( contextBefore, &bl::crypto::CryptoBase::getAsioSslContext() );
}

UTF_AUTO_TEST_CASE( CryptoUtils_RsaTests )
{
    const auto rsaKey = bl::crypto::RsaKey::createInstance();
}

UTF_AUTO_TEST_CASE( CryptoUtils_X509tests )
{
    bl::crypto::evppkey_ptr_t evpPkey = bl::crypto::createPrivateKey();

    UTF_REQUIRE( evpPkey );

    std::string evpPkeyPem = bl::crypto::getEvpPkeyAsPemString( evpPkey );

    UTF_REQUIRE( evpPkeyPem.find( "-----BEGIN PRIVATE KEY-----" ) != std::string::npos );

    const std::string country = "US";
    const std::string organization = "MyOrg";
    const std::string commonName = "localhost";
    int serial = 1;
    int daysValid = 365;

    const auto x509cert =
        bl::crypto::createSelfSignedX509Cert( evpPkey, country, organization, commonName, serial, daysValid );

    UTF_REQUIRE( x509cert );

    const auto x509certPem = bl::crypto::geX509CertAsPemString( x509cert );

    UTF_REQUIRE( x509certPem.find( "-----BEGIN CERTIFICATE-----" ) != std::string::npos );
}

UTF_AUTO_TEST_CASE( RsaEncryption_encryptAsBase64Tests )
{
    const std::string secret = "secret123";

    const auto publicRsaStr = utest::TestUtils::loadDataFile( "test-public-key.pem" );
    const auto publicKey = utest::TestUtils::getRsaKeyFromString( publicRsaStr );

    const auto encrypted = bl::crypto::RsaEncryption::encryptAsBase64Url( publicKey, secret );

    const auto privateRsaStr = utest::TestUtils::loadDataFile( "test-private-key.pem" );
    const auto privateKey = utest::TestUtils::getRsaKeyFromString( privateRsaStr );

    const auto decrypted = bl::crypto::RsaEncryption::decryptBase64Message( privateKey, encrypted );

    UTF_CHECK_EQUAL( decrypted, secret );

    UTF_CHECK( decrypted != encrypted );
}

UTF_AUTO_TEST_CASE( RsaEncryption_encryptTests )
{
    const std::string secret = "secret123";

    const auto publicRsaStr = utest::TestUtils::loadDataFile( "test-public-key.pem" );
    const auto publicKey = utest::TestUtils::getRsaKeyFromString( publicRsaStr );

    unsigned outputSize = 0u;

    const auto out = bl::crypto::RsaEncryption::encrypt( publicKey, secret, outputSize );

    const auto charBuffer = reinterpret_cast< const char* >( out.get() );

    const std::string encrypted( charBuffer, charBuffer + outputSize );

    UTF_CHECK( ! encrypted.empty() );

    UTF_CHECK( encrypted != secret );

    /*
     * An OAEP ciphertext is always exactly the size of the modulus regardless of how short
     * the plaintext was, so the size of the output is a property of the key and not of the
     * message; a padding mode which emitted anything else would show up here
     */

    UTF_CHECK_EQUAL(
        static_cast< std::size_t >( outputSize ),
        static_cast< std::size_t >( ::RSA_size( &publicKey -> get() ) )
        );
}

UTF_AUTO_TEST_CASE( RsaEncryption_MessageSizeBounds )
{
    using namespace bl;

    const auto publicKey = utest::TestUtils::getRsaKeyFromString(
        utest::TestUtils::loadDataFile( "test-public-key.pem" )
        );

    const auto privateKey = utest::TestUtils::getRsaKeyFromString(
        utest::TestUtils::loadDataFile( "test-private-key.pem" )
        );

    const auto rsaSize = static_cast< std::size_t >( ::RSA_size( &publicKey -> get() ) );

    /*
     * The bound below is derived from the key rather than hard coded, so the case survives a
     * change of the fixture; this assertion only documents which fixture is in the tree
     */

    UTF_REQUIRE_EQUAL( rsaSize, 256U );

    /*
     * OAEP with the OpenSSL default digest (SHA-1) leaves k - 2 * hLen - 2 bytes for the
     * plaintext, i.e. 42 bytes of overhead
     */

    const auto maxPlain = rsaSize - 42U;

    const std::string message( maxPlain, 'x' );

    UTF_REQUIRE_EQUAL(
        crypto::RsaEncryption::decryptBase64Message(
            privateKey,
            crypto::RsaEncryption::encryptAsBase64Url( publicKey, message )
            ),
        message
        );

    /*
     * One byte over the OAEP limit has to fail rather than be truncated - PKCS#1 v1.5 leaves
     * only 11 bytes of overhead and would still encrypt this input, so this is the assertion
     * which discriminates the two padding modes
     */

    const std::string tooLong( maxPlain + 1U, 'x' );

    UTF_REQUIRE_THROW(
        crypto::RsaEncryption::encryptAsBase64Url( publicKey, tooLong ),
        SystemException
        );

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );

    /*
     * A payload which carries an embedded NUL and high bit bytes has to round trip byte for
     * byte - the sizes are passed explicitly, so a regression to NUL terminated sizing would
     * silently encrypt only the first six bytes here
     */

    const std::string binaryMessage( "before\0after\xFE\xFF", 14U );

    UTF_REQUIRE_EQUAL( binaryMessage.size(), 14U );

    const auto binaryRoundTrip = crypto::RsaEncryption::decryptBase64Message(
        privateKey,
        crypto::RsaEncryption::encryptAsBase64Url( publicKey, binaryMessage )
        );

    UTF_REQUIRE_EQUAL( binaryRoundTrip.size(), binaryMessage.size() );
    UTF_REQUIRE_EQUAL( binaryRoundTrip, binaryMessage );

    /*
     * OAEP is randomized, so the very same plaintext must never encrypt to the same
     * ciphertext twice
     */

    UTF_REQUIRE(
        crypto::RsaEncryption::encryptAsBase64Url( publicKey, message ) !=
            crypto::RsaEncryption::encryptAsBase64Url( publicKey, message )
        );

    /*
     * A ciphertext which is one byte short is no longer of the modulus size, so decrypting it
     * has to fail instead of returning a shorter plaintext; this is the failure path which is
     * deterministic on every version of OpenSSL
     */

    unsigned outputSize = 0U;

    const auto ciphertext = crypto::RsaEncryption::encrypt( publicKey, message, outputSize );

    UTF_REQUIRE_EQUAL( static_cast< std::size_t >( outputSize ), rsaSize );

    const auto truncated = SerializationUtils::base64UrlEncode( ciphertext.get(), outputSize - 1U );

    UTF_REQUIRE_THROW(
        crypto::RsaEncryption::decryptBase64Message( privateKey, truncated ),
        SystemException
        );

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( CryptoErrorHandling_ExceptionCarriesOpenSslReasonAndDrainsQueue )
{
    using namespace bl;

    /*
     * Handing a buffer which holds no certificate to ::PEM_read_bio_X509() is a deterministic
     * OpenSSL failure - it pushes at least one entry ('no start line') onto the error queue
     * on every version we support
     */

    const auto failOnce = []() -> void
    {
        const auto buffer = createNotACertificateBio();

        UTF_REQUIRE(
            nullptr == ::PEM_read_bio_X509(
                buffer.get(),
                nullptr             /* X509 certificate out pointer (**) */,
                nullptr             /* Password callback */,
                nullptr             /* Password bytes */
                )
            );
    };

    ( void ) ::ERR_clear_error();

    failOnce();

    /*
     * getFirstError() peeks by default, so it has to be repeatable - a peek which consumed
     * would make the second read return zero
     */

    UTF_REQUIRE( 0 != crypto::detail::getFirstError().value() );

    UTF_REQUIRE_EQUAL(
        crypto::detail::getFirstError().value(),
        crypto::detail::getFirstError().value()
        );

    UTF_REQUIRE_EQUAL(
        std::string( crypto::detail::getErrorCategory().name() ),
        std::string( "OpenSSL" )
        );

    const auto ec = crypto::detail::getFirstError( false /* clear */ );

    /*
     * The reason string has to resolve; the literal below is the fallback the category
     * returns when the error strings could not be loaded at all, so requiring the message to
     * differ from it is what asserts that loadErrorStrings() did its job
     */

    UTF_REQUIRE( ! ec.message().empty() );
    UTF_REQUIRE( ec.message() != std::string( "OpenSSL error" ) );

    /*
     * Two failures in a row without clearing in between guarantee at least two entries in the
     * queue, which is what makes the nested exception chain deterministic
     */

    ( void ) ::ERR_clear_error();

    failOnce();
    failOnce();

    const auto exception = crypto::getException( "test message" );

    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_nested_exception_ptr >( exception ) );

    const auto* categoryName = eh::get_error_info< eh::errinfo_category_name >( exception );

    UTF_REQUIRE( nullptr != categoryName );
    UTF_REQUIRE_EQUAL( *categoryName, std::string( "OpenSSL" ) );

    /*
     * The queue has to be drained completely, otherwise a stale reason would be reported
     * against the next unrelated failure
     */

    UTF_REQUIRE( 0 == ::ERR_peek_error() );

    UTF_REQUIRE( cpp::contains( eh::diagnostic_information( exception ), "Nested exception" ) );

    UTF_REQUIRE( 0 == crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( CryptoErrorHandling_OpenSslCategoryDoesNotSurviveServerErrorRoundTrip )
{
    using namespace bl;

    ( void ) ::ERR_clear_error();

    /*
     * A real, library produced OpenSSL failure - crypto::getException() gives it an error code
     * which lives in CryptoErrorCategory, whose name() is the literal "OpenSSL"
     */

    std::exception_ptr eptr;

    {
        const auto buffer = createNotACertificateBio();

        UTF_REQUIRE_EXCEPTION(
            BL_CHK_CRYPTO_API_NM(
                ::PEM_read_bio_X509(
                    buffer.get(),
                    nullptr             /* X509 certificate out pointer (**) */,
                    nullptr             /* Password callback */,
                    nullptr             /* Password bytes */
                    )
                ),
            SystemException,
            [ &eptr ]( SAA_in const SystemException& e ) -> bool
            {
                eptr = std::current_exception();

                const auto* categoryName = eh::get_error_info< eh::errinfo_category_name >( e );

                return nullptr != categoryName && std::string( "OpenSSL" ) == *categoryName;
            }
            );
    }

    UTF_REQUIRE( eptr );

    /*
     * The category name reaches the wire document verbatim - this is what ties the crypto
     * error category to the ServerError schema
     */

    const auto json = dm::ServerErrorHelpers::createServerErrorObject( eptr );

    UTF_REQUIRE( json -> result() );
    UTF_REQUIRE( json -> result() -> exceptionProperties() );

    UTF_REQUIRE_EQUAL( json -> result() -> exceptionType(), std::string( "bl::SystemException" ) );

    UTF_REQUIRE_EQUAL(
        json -> result() -> exceptionProperties() -> categoryName(),
        std::string( "OpenSSL" )
        );

    /*
     * Because the OpenSSL category is neither eh::system_category() nor eh::generic_category(),
     * SystemException::create attaches neither errinfo_errno nor errinfo_system_code - so
     * errNo is simply absent and systemCode carries nothing beyond the value already derived
     * from errinfo_error_code, i.e. neither of the two fields which updateHttpStatusFromException
     * and CmdLineAppBase key on says anything meaningful about this failure
     *
     * The reason value itself is deliberately not pinned - it differs across OpenSSL 1.1.x
     * and 3.x - only that it is non zero
     */

    UTF_REQUIRE( ! json -> result() -> exceptionProperties() -> errNoIsSet() );

    UTF_REQUIRE( json -> result() -> exceptionProperties() -> errorCodeIsSet() );
    UTF_REQUIRE( 0 != json -> result() -> exceptionProperties() -> errorCode() );

    UTF_REQUIRE_EQUAL(
        json -> result() -> exceptionProperties() -> systemCode(),
        json -> result() -> exceptionProperties() -> errorCode()
        );

    /*
     * This assertion pins a defect rather than a desirable behavior
     *
     * createExceptionFromObject() inspects categoryName before it dispatches on exceptionType,
     * and its chain only knows "generic", "system" and the empty string - so a peer which is
     * handed a perfectly well formed server error document describing a TLS failure gets an
     * ArgumentException naming an internal category instead of the real error. The reachability
     * is not hypothetical: EhUtils::asioErrorCallback re-tags every asio SSL error into the
     * OpenSSL category and is installed process wide by CmdLineAppBase::main and by Utf.h
     *
     * The fix is to teach the chain the "OpenSSL" category (or to stop copying a category name
     * which is neither generic nor system into the document); when it lands, this assertion has
     * to flip to a successful round trip
     */

    UTF_REQUIRE_EXCEPTION(
        ( void ) dm::ServerErrorHelpers::createExceptionFromObject( json ),
        ArgumentException,
        []( SAA_in const ArgumentException& e ) -> bool
        {
            return std::string( "Unknown error category: 'OpenSSL'" ) == e.what();
        }
        );

    /*
     * The positive control - the document itself round trips through JSON text without loss,
     * so the failure above is unambiguously in the category dispatch and not in serialization
     */

    UTF_REQUIRE_NO_THROW(
        ( void ) dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >(
            dm::ServerErrorHelpers::getServerErrorAsJson( eptr )
            )
        );

    /*
     * Leave the process global error queue clean for the next case
     */

    ( void ) ::ERR_clear_error();
}

UTF_AUTO_TEST_CASE( CryptoUtils_TrustedRootRegistrationOrdering )
{
    using namespace bl;

    /*
     * The process global client context is created once, when the crypto layer is
     * initialized, and its certificate store is filled from the trusted root set at that
     * moment; a root which is registered afterwards is picked up by every server context
     * created from then on, but never by the client context - which is why the registration
     * has to happen before init(), the way UtfMain.h does it
     */

    crypto::CryptoBase::init();

    const auto key = crypto::createPrivateKey();

    const auto cert = crypto::createSelfSignedX509Cert(
        key,
        "US"                                    /* country */,
        "W17 Test"                              /* organization */,
        "w17-late-root.test"                    /* commonName */,
        4242                                    /* serial */,
        1                                       /* daysValid */
        );

    const auto pem = crypto::geX509CertAsPemString( cert );

    const auto before = crypto::trustedRoots();

    UTF_REQUIRE( ! before.empty() );
    UTF_REQUIRE( before.count( std::string( test::UtfCrypto::getDevRootCA() ) ) );
    UTF_REQUIRE( ! before.count( pem ) );

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

    const auto storeSize = []( SAA_in ::SSL_CTX* nativeSslContext ) -> int
    {
        return sk_X509_OBJECT_num( ::X509_STORE_get0_objects( ::SSL_CTX_get_cert_store( nativeSslContext ) ) );
    };

    const auto clientBefore = storeSize( crypto::CryptoBase::getAsioSslContext().native_handle() );

    const auto serverBefore = storeSize(
        crypto::CryptoBase::createAsioSslServerContext(
            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */
            ) -> native_handle()
        );

#endif // OPENSSL_VERSION_NUMBER >= 0x10100000L

    crypto::registerTrustedRoot( cpp::copy( pem ) );

    UTF_REQUIRE_EQUAL( crypto::trustedRoots().size(), before.size() + 1U );
    UTF_REQUIRE( crypto::trustedRoots().count( pem ) );

    /*
     * The root set de-duplicates, so registering the identical text again is a no-op
     */

    crypto::registerTrustedRoot( cpp::copy( pem ) );

    UTF_REQUIRE_EQUAL( crypto::trustedRoots().size(), before.size() + 1U );

#if OPENSSL_VERSION_NUMBER >= 0x10100000L

    const auto clientAfter = storeSize( crypto::CryptoBase::getAsioSslContext().native_handle() );

    const auto serverAfter = storeSize(
        crypto::CryptoBase::createAsioSslServerContext(
            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */
            ) -> native_handle()
        );

    /*
     * The late registration did not reach the client context ...
     */

    UTF_REQUIRE_EQUAL( clientAfter, clientBefore );

    /*
     * ... while a server context which is created afterwards does load it, which also proves
     * that loadTrustedRootFromPem() accepted a certificate emitted by geX509CertAsPemString()
     * and that ::X509_STORE_add_cert() did not reject it
     */

    UTF_REQUIRE_EQUAL( serverAfter, serverBefore + 1 );

#endif // OPENSSL_VERSION_NUMBER >= 0x10100000L
}
