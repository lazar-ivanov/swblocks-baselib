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

    /**
     * @brief Parses a PEM encoded certificate back through a memory BIO
     *
     * This is what makes the exact length read inside X509Cert.h's bioBufferToString()
     * load bearing - a truncated PEM does not parse
     */

    auto parseCertificateFromPem( SAA_in const std::string& pem ) -> bl::crypto::x509cert_ptr_t
    {
        const auto buffer = bl::crypto::bio_ptr_t::attach(
            ::BIO_new_mem_buf( const_cast< char* >( pem.c_str() ), bl::crypto::toIntSize( pem.size() ) )
            );

        UTF_REQUIRE( buffer );

        return bl::crypto::x509cert_ptr_t::attach(
            ::PEM_read_bio_X509(
                buffer.get(),
                nullptr     /* X509 certificate out pointer */,
                nullptr     /* password callback */,
                nullptr     /* password bytes */
                )
            );
    }

    /**
     * @brief Parses a trusted root exactly the way CryptoBase::loadTrustedRootFromPem() does
     */

    auto parseTrustedRootFromPem( SAA_in const std::string& pem ) -> bl::crypto::x509cert_ptr_t
    {
        const auto buffer = bl::crypto::bio_ptr_t::attach(
            ::BIO_new_mem_buf( const_cast< char* >( pem.c_str() ), bl::crypto::toIntSize( pem.size() ) )
            );

        UTF_REQUIRE( buffer );

        return bl::crypto::x509cert_ptr_t::attach(
            ::PEM_read_bio_X509_AUX(
                buffer.get(),
                nullptr     /* X509 certificate out pointer */,
                nullptr     /* password callback */,
                nullptr     /* password bytes */
                )
            );
    }

    /**
     * @brief The subject of a certificate as one line of text
     */

    std::string certificateSubjectAsText( SAA_in const bl::crypto::x509cert_ptr_t& certificate )
    {
        ::X509_NAME* const subject = ::X509_get_subject_name( certificate.get() );

        UTF_REQUIRE( nullptr != subject );

        char text[ 512 ];

        const char* const result = ::X509_NAME_oneline( subject, text, static_cast< int >( sizeof( text ) ) );

        UTF_REQUIRE( nullptr != result );

        return std::string( result );
    }

    /*
     * The mutable notBefore / notAfter accessors, version gated exactly the way X509Cert.h
     * gates the ones it uses to *set* them
     */

    ::ASN1_TIME* certificateNotBefore( SAA_in const bl::crypto::x509cert_ptr_t& certificate )
    {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        return ::X509_getm_notBefore( certificate.get() );
#else
        return X509_get_notBefore( certificate.get() );
#endif
    }

    ::ASN1_TIME* certificateNotAfter( SAA_in const bl::crypto::x509cert_ptr_t& certificate )
    {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        return ::X509_getm_notAfter( certificate.get() );
#else
        return X509_get_notAfter( certificate.get() );
#endif
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

    /*
     * Nothing above asserts that anything which was asked for actually reached the
     * certificate - a validitySeconds() which returned 0, a serial which never landed, or a
     * subject / issuer mix up would all still emit a string carrying the PEM banner
     *
     * Parsing the emitted PEM back is also what makes bioBufferToString()'s exact length
     * read load bearing: a truncated PEM does not parse
     */

    const auto parsed = parseCertificateFromPem( x509certPem );

    UTF_REQUIRE( nullptr != parsed );

    /*
     * version3 is set as the numeric value 2 (X.509 versions are zero based)
     */

    UTF_REQUIRE_EQUAL( ::X509_get_version( parsed.get() ), 2L );

    UTF_REQUIRE_EQUAL(
        ::ASN1_INTEGER_get( ::X509_get_serialNumber( parsed.get() ) ),
        static_cast< long >( serial )
        );

    {
        ::X509_NAME* const subject = ::X509_get_subject_name( parsed.get() );

        UTF_REQUIRE( nullptr != subject );

        const auto textByNid = [ &subject ]( SAA_in const int nid ) -> std::string
        {
            char text[ 256 ];

            const int length = ::X509_NAME_get_text_by_NID( subject, nid, text, static_cast< int >( sizeof( text ) ) );

            UTF_REQUIRE( length > 0 );

            return std::string( text, static_cast< std::size_t >( length ) );
        };

        UTF_REQUIRE_EQUAL( textByNid( NID_commonName ), commonName );
        UTF_REQUIRE_EQUAL( textByNid( NID_countryName ), country );
        UTF_REQUIRE_EQUAL( textByNid( NID_organizationName ), organization );

        /*
         * The subject is used as the issuer as well - a mix up here would break
         * ::X509_check_host() matching for every consumer of this helper
         */

        UTF_REQUIRE_EQUAL( 0, ::X509_NAME_cmp( subject, ::X509_get_issuer_name( parsed.get() ) ) );
    }

    {
        ::ASN1_TIME* const notBefore = certificateNotBefore( parsed );
        ::ASN1_TIME* const notAfter = certificateNotAfter( parsed );

        UTF_REQUIRE( nullptr != notBefore );
        UTF_REQUIRE( nullptr != notAfter );

        UTF_REQUIRE( ::X509_cmp_current_time( notBefore ) < 0 );
        UTF_REQUIRE( ::X509_cmp_current_time( notAfter ) > 0 );

        /*
         * ... and the real assertion: daysValid must reach the certificate. The window is
         * bracketed to within a day, which catches a validitySeconds() returning 0, a wrong
         * unit, or a truncated int product - all of which the banner check above cannot see
         */

        const long secondsPerDay = 60L * 60L * 24L;

        std::time_t almost = ::time( nullptr ) + ( daysValid - 1 ) * secondsPerDay;
        std::time_t beyond = ::time( nullptr ) + ( daysValid + 1 ) * secondsPerDay;

        UTF_REQUIRE( ::X509_cmp_time( notAfter, &almost ) > 0 );
        UTF_REQUIRE( ::X509_cmp_time( notAfter, &beyond ) < 0 );
    }

    /*
     * The certificate is self signed with the very key which was handed in
     */

    UTF_REQUIRE_EQUAL( 1, ::X509_verify( parsed.get(), evpPkey.get() ) );

    /*
     * ... and the emitted PEM key parses back too
     */

    UTF_REQUIRE( ! evpPkeyPem.empty() );

    {
        const auto buffer = bl::crypto::bio_ptr_t::attach(
            ::BIO_new_mem_buf(
                const_cast< char* >( evpPkeyPem.c_str() ),
                bl::crypto::toIntSize( evpPkeyPem.size() )
                )
            );

        UTF_REQUIRE( buffer );

        const auto parsedKey = bl::crypto::evppkey_ptr_t::attach(
            ::PEM_read_bio_PrivateKey(
                buffer.get(),
                nullptr     /* EVP_PKEY out pointer */,
                nullptr     /* password callback */,
                nullptr     /* password bytes */
                )
            );

        UTF_REQUIRE( parsedKey );
    }
}

UTF_AUTO_TEST_CASE( CryptoUtils_X509ValidityBounds )
{
    /*
     * detail::validitySeconds()'s two BL_CHK_ARG guards have never been executed
     *
     * Note that only the negative case is portable: the overflow guard divides
     * std::numeric_limits< long >::max() by 86400, which is ~1.07e14 on LP64 (so it is
     * unreachable from an int there) but 24855 on Windows LLP64 - a large daysValid
     * therefore succeeds on Linux and throws on Windows, and no portable assertion can pin
     * the upper bound until the arithmetic uses a fixed width type
     */

    const auto evpPkey = bl::crypto::createPrivateKey();

    UTF_REQUIRE( evpPkey );

    /*
     * The case asserts that an argument rejection leaves the OpenSSL error queue untouched,
     * which is a statement about the delta - so it has to start from a clean queue
     */

    ( void ) ::ERR_clear_error();

    UTF_REQUIRE_THROW(
        bl::crypto::createSelfSignedX509Cert( evpPkey, "US", "MyOrg", "localhost", 1, -1 /* daysValid */ ),
        bl::ArgumentException
        );

    UTF_CHECK( 0 == bl::crypto::detail::getFirstError().value() );

    /*
     * The guard is 'daysValid >= 0', so zero must succeed and produce a certificate whose
     * validity window collapses onto its notBefore
     */

    const std::time_t before = ::time( nullptr );

    const auto x509cert =
        bl::crypto::createSelfSignedX509Cert( evpPkey, "US", "MyOrg", "localhost", 2, 0 /* daysValid */ );

    const std::time_t after = ::time( nullptr );

    UTF_REQUIRE( x509cert );

    const auto parsed = parseCertificateFromPem( bl::crypto::geX509CertAsPemString( x509cert ) );

    UTF_REQUIRE( nullptr != parsed );

    ::ASN1_TIME* const notBefore = certificateNotBefore( parsed );
    ::ASN1_TIME* const notAfter = certificateNotAfter( parsed );

    /*
     * ::X509_gmtime_adj is called twice, with two separate ::time( nullptr ) reads, so an
     * exact equality assertion would be a race across a second boundary - both bounds are
     * bracketed into the (tiny) wall clock window of the call, widened by two seconds, which
     * still fails loudly for any non zero validity period
     */

    std::time_t lowerBound = before - 2;
    std::time_t upperBound = after + 2;

    UTF_REQUIRE( ::X509_cmp_time( notBefore, &lowerBound ) > 0 );
    UTF_REQUIRE( ::X509_cmp_time( notBefore, &upperBound ) < 0 );

    UTF_REQUIRE( ::X509_cmp_time( notAfter, &lowerBound ) > 0 );
    UTF_REQUIRE( ::X509_cmp_time( notAfter, &upperBound ) < 0 );

    UTF_CHECK( 0 == bl::crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( CryptoUtils_ToIntSizeBoundary )
{
    using namespace bl;

    /*
     * crypto::toIntSize() exists so that an oversized buffer cannot reach an OpenSSL
     * primitive as a *negative* length (::RSA_private_decrypt and thence ::BN_bin2bn would
     * then read out of bounds). Every call site in the suite passes a small size, so the
     * guard has never evaluated to false anywhere and an inverted or removed bound would
     * only ever show up as a heap overrun inside OpenSSL
     */

    ( void ) ::ERR_clear_error();

    UTF_REQUIRE_EQUAL( crypto::toIntSize( 0U ), 0 );
    UTF_REQUIRE_EQUAL( crypto::toIntSize( 1U ), 1 );

    /*
     * The bound is inclusive
     */

    UTF_REQUIRE_EQUAL(
        crypto::toIntSize( static_cast< std::size_t >( std::numeric_limits< int >::max() ) ),
        std::numeric_limits< int >::max()
        );

    /*
     * The over-boundary rows exist only where std::size_t is wider than int; the test is a
     * runtime 'if' rather than a preprocessor guard, so a 32 bit build still type checks the
     * code and the compiler folds the branch away
     */

    if( sizeof( std::size_t ) > sizeof( int ) )
    {
        UTF_REQUIRE_THROW(
            crypto::toIntSize( static_cast< std::size_t >( std::numeric_limits< int >::max() ) + 1U ),
            bl::ArgumentException
            );

        UTF_REQUIRE_THROW(
            crypto::toIntSize( std::numeric_limits< std::size_t >::max() ),
            bl::ArgumentException
            );
    }

    /*
     * An argument rejection must not have touched the OpenSSL error queue
     */

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
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

UTF_AUTO_TEST_CASE( CryptoUtils_AllBundledTrustedRootsAreLoadable )
{
    using namespace bl;

    /*
     * initDefaultGlobalTrustedRoots() registers three roots and is the default callback, so
     * those three are PEM parsed at every process start through
     * loadAllKnownCertificateAuthorities(). initAdditionalCommonTrustedRoots() registers four
     * more and is reached only through initAllGlobalTrustedRoots(), which only bl-tool
     * installs - so a truncated, reflowed or duplicated blob among those four would break
     * every SSL context creation in bl-tool (i.e. all of its HTTP commands) and nothing in
     * the repository would notice
     *
     * NOTE: this case must stay declared AFTER CryptoUtils_TrustedRootRegistrationOrdering -
     * it permanently adds four roots to the process global set, which would break that
     * case's 'before.size() + 1U' arithmetic. Boost.Test runs the cases of a file in
     * declaration order, so the declaration order is the whole of the constraint
     */

    UTF_REQUIRE( crypto::initGlobalTrustedRootsCallback() );

    const auto before = crypto::trustedRoots().size();

    crypto::detail::TrustedRoots::initAllGlobalTrustedRoots();

    /*
     * All four additional roots are distinct strings and none of them duplicates one of the
     * three defaults which are already registered
     */

    UTF_REQUIRE_EQUAL( crypto::trustedRoots().size(), before + 4U );

    for( const auto& pem : crypto::trustedRoots() )
    {
        const auto certificate = parseTrustedRootFromPem( pem );

        const auto subject = certificate ? certificateSubjectAsText( certificate ) : std::string();

        UTF_MESSAGE( "Trusted root: " + subject );

        UTF_REQUIRE( certificate );

        UTF_REQUIRE( ! subject.empty() );

        /*
         * The bundled set is deliberately not required to be v3 throughout: the original
         * 'VeriSign Class 3 Public Primary Certification Authority' root is a v1 certificate
         * (::X509_get_version returns 0 for it), and v1 and v3 are the only versions a real
         * root set carries
         */

        const auto version = ::X509_get_version( certificate.get() );

        UTF_REQUIRE( 0L == version || 2L == version );
    }

    /*
     * The integration assertion - creating a server context loads the whole set through
     * loadAllKnownCertificateAuthorities(), which is also where the duplicate certificate
     * landmine lives: ::X509_STORE_add_cert() fails with CERT_ALREADY_IN_HASH_TABLE when two
     * distinct PEM strings in the set encode the same certificate
     */

    UTF_REQUIRE_NO_THROW(
        ( void ) crypto::CryptoBase::createAsioSslServerContext(
            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */
            )
        );

    UTF_CHECK( 0 == crypto::detail::getFirstError().value() );
}
