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

#include <baselib/security/JsonSecuritySerialization.h>

#include <baselib/crypto/ErrorHandling.h>
#include <baselib/crypto/RsaKey.h>
#include <baselib/core/SerializationUtils.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/TestUtils.h>

namespace utest
{
    /**
     * @brief The fixtures below are all derived from the same RSA key, so a key which was
     * loaded from any of them must expose identical components; that is what pins the
     * legacy PEM formats as readable regardless of the version of OpenSSL we are built with
     */

    template
    <
        typename E = void
    >
    class LocalTestPemKeyFormatsHelpersT
    {
        BL_DECLARE_STATIC( LocalTestPemKeyFormatsHelpersT )

    public:

        enum : std::size_t
        {
            /*
             * The position at which a PEM label is expected to be found in the key text
             */

            labelPos = 0U,
        };

        static auto legacyPublicKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-public-key.pem" );
        }

        static auto spkiPublicKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-public-key-spki.pem" );
        }

        static auto legacyPrivateKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-private-key.pem" );
        }

        static auto pkcs8PrivateKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-private-key-pkcs8.pem" );
        }

        static auto encryptedPkcs8PrivateKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-private-key-pkcs8-enc.pem" );
        }

        static auto encryptedPkcs8Password() -> std::string
        {
            return "Password1";
        }

        static auto rsa2047PrivateKeyFixture() -> std::string
        {
            /*
             * A 2047-bit modulus: one bit below the floor, but 256 bytes long, so a floor
             * computed from the size in whole bytes would have accepted it
             */

            return TestUtils::loadDataFile( "test-private-key-2047.pem" );
        }

        static auto ecPrivateKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-ec-private-key.pem" );
        }

        static auto ecPublicKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-ec-public-key.pem" );
        }

        static auto inconsistentPrivateKeyFixture() -> std::string
        {
            /*
             * test-private-key.pem with one bit of its 'exponent1' (d mod (p - 1)) CRT
             * parameter flipped and re-encoded as PKCS#1; it parses, its modulus and public
             * exponent are those of the original key, and 'openssl rsa -check' reports
             * "dmp1 not congruent to d"
             */

            return TestUtils::loadDataFile( "test-private-key-inconsistent.pem" );
        }

        static auto smallExponentPrivateKeyFixture() -> std::string
        {
            /*
             * A 2048-bit key generated with the public exponent 3 (openssl genrsa -3)
             */

            return TestUtils::loadDataFile( "test-private-key-e3.pem" );
        }

        static auto smallExponentPublicKeyFixture() -> std::string
        {
            return TestUtils::loadDataFile( "test-public-key-e3.pem" );
        }

        static auto getModulus( SAA_in const bl::om::ObjPtr< bl::crypto::RsaKey >& rsaKey ) -> std::string
        {
            return bl::security::JsonSecuritySerialization::getPublicKeyAsJsonObject( rsaKey ) -> modulus();
        }

        static auto getExponent( SAA_in const bl::om::ObjPtr< bl::crypto::RsaKey >& rsaKey ) -> std::string
        {
            return bl::security::JsonSecuritySerialization::getPublicKeyAsJsonObject( rsaKey ) -> exponent();
        }

        static auto getPrivateExponent( SAA_in const bl::om::ObjPtr< bl::crypto::RsaKey >& rsaKey ) -> std::string
        {
            return bl::security::JsonSecuritySerialization::getPrivateKeyAsJsonObject( rsaKey ) -> privateExponent();
        }

        /**
         * @brief Verifies the OpenSSL "error queue" was left clean
         *
         * A read attempt which fails without throwing (e.g. the first attempt of the public
         * key loader when the input is in the legacy format and we are built against OpenSSL
         * 1.x) leaves entries behind which survive into later operations and would then be
         * reported as the reason for an unrelated failure
         */

        static bool isErrorQueueClean()
        {
            return 0 == bl::crypto::detail::getFirstError().value();
        }
    };

    typedef LocalTestPemKeyFormatsHelpersT<> LocalTestPemKeyFormatsHelpers;

} // utest

UTF_AUTO_TEST_CASE( PemKeyFormats_LegacyPkcs1PublicKeyLoads )
{
    using namespace utest;

    const auto pemKeyText = LocalTestPemKeyFormatsHelpers::legacyPublicKeyFixture();

    UTF_REQUIRE(
        pemKeyText.find( "-----BEGIN RSA PUBLIC KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString( pemKeyText );

    UTF_CHECK_EQUAL( LocalTestPemKeyFormatsHelpers::getExponent( rsaKey ), std::string( "AQAB" ) );

    /*
     * On OpenSSL 1.x this key can only be loaded via the fallback attempt, which is the
     * path which must clear the error queue left dirty by the first attempt
     */

    UTF_CHECK( LocalTestPemKeyFormatsHelpers::isErrorQueueClean() );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_SpkiPublicKeyMatchesLegacy )
{
    using namespace utest;

    const auto pemKeyText = LocalTestPemKeyFormatsHelpers::spkiPublicKeyFixture();

    UTF_REQUIRE(
        pemKeyText.find( "-----BEGIN PUBLIC KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto spkiKey = bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString( pemKeyText );

    const auto legacyKey = bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString(
        LocalTestPemKeyFormatsHelpers::legacyPublicKeyFixture()
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getModulus( spkiKey ),
        LocalTestPemKeyFormatsHelpers::getModulus( legacyKey )
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getExponent( spkiKey ),
        LocalTestPemKeyFormatsHelpers::getExponent( legacyKey )
        );

    UTF_CHECK( LocalTestPemKeyFormatsHelpers::isErrorQueueClean() );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_Pkcs8PrivateKeyMatchesLegacy )
{
    using namespace utest;

    const auto pemKeyText = LocalTestPemKeyFormatsHelpers::pkcs8PrivateKeyFixture();

    UTF_REQUIRE(
        pemKeyText.find( "-----BEGIN PRIVATE KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto pkcs8Key = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( pemKeyText );

    const auto legacyKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        LocalTestPemKeyFormatsHelpers::legacyPrivateKeyFixture()
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getModulus( pkcs8Key ),
        LocalTestPemKeyFormatsHelpers::getModulus( legacyKey )
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getPrivateExponent( pkcs8Key ),
        LocalTestPemKeyFormatsHelpers::getPrivateExponent( legacyKey )
        );

    /*
     * All four unencrypted fixtures must describe the very same key
     */

    const auto publicKey = bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString(
        LocalTestPemKeyFormatsHelpers::legacyPublicKeyFixture()
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getModulus( pkcs8Key ),
        LocalTestPemKeyFormatsHelpers::getModulus( publicKey )
        );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_EncryptedPkcs8PrivateKey )
{
    using namespace utest;

    const auto pemKeyText = LocalTestPemKeyFormatsHelpers::encryptedPkcs8PrivateKeyFixture();

    UTF_REQUIRE(
        pemKeyText.find( "-----BEGIN ENCRYPTED PRIVATE KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        pemKeyText,
        LocalTestPemKeyFormatsHelpers::encryptedPkcs8Password()
        );

    const auto legacyKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        LocalTestPemKeyFormatsHelpers::legacyPrivateKeyFixture()
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getPrivateExponent( rsaKey ),
        LocalTestPemKeyFormatsHelpers::getPrivateExponent( legacyKey )
        );

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( pemKeyText, "wrong-password" ),
        bl::SystemException
        );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_EmittedFormatsAreSpkiAndPkcs8 )
{
    using namespace utest;

    const auto rsaKey = bl::crypto::RsaKey::createInstance();
    rsaKey -> generate();

    /*
     * The emitted format must be a property of the library and not of the version of
     * OpenSSL which happened to build it, so these labels must hold on every environment
     */

    const auto publicPem = bl::security::JsonSecuritySerialization::getPublicKeyAsPemString( rsaKey );

    UTF_CHECK(
        publicPem.find( "-----BEGIN PUBLIC KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto privatePem = bl::security::JsonSecuritySerialization::getPrivateKeyAsPemString(
        rsaKey,
        bl::security::KeyProtection::PlaintextExplicit
        );

    UTF_CHECK(
        privatePem.find( "-----BEGIN PRIVATE KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    const auto encryptedPrivatePem = bl::security::JsonSecuritySerialization::getPrivateKeyAsPemString(
        rsaKey,
        bl::security::KeyProtection::Encrypted,
        "1234" /* password */
        );

    UTF_CHECK(
        encryptedPrivatePem.find( "-----BEGIN ENCRYPTED PRIVATE KEY-----" ) ==
            LocalTestPemKeyFormatsHelpers::labelPos
        );

    /*
     * Everything which was just written must read back as the same key
     */

    const auto expectedModulus = LocalTestPemKeyFormatsHelpers::getModulus( rsaKey );
    const auto expectedPrivateExponent = LocalTestPemKeyFormatsHelpers::getPrivateExponent( rsaKey );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getModulus(
            bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString( publicPem )
            ),
        expectedModulus
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getPrivateExponent(
            bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( privatePem )
            ),
        expectedPrivateExponent
        );

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getPrivateExponent(
            bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( encryptedPrivatePem, "1234" )
            ),
        expectedPrivateExponent
        );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_MalformedPemThrows )
{
    using namespace utest;

    const std::string malformed( "-----BEGIN PUBLIC KEY-----\nnot a key at all\n-----END PUBLIC KEY-----\n" );

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString( malformed ),
        bl::SystemException
        );

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( malformed ),
        bl::SystemException
        );

    /*
     * A failure must not poison subsequent operations
     */

    UTF_CHECK_NO_THROW(
        bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::legacyPublicKeyFixture()
            )
        );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_ModulusBelowMinimumIsRejected )
{
    using namespace utest;

    /*
     * The minimum modulus size is enforced on the exact bit length: a 2047-bit key is one
     * bit short of the floor, yet it is 256 bytes long, so this is the regression test for the
     * floor being computed from the byte length and rounding up to 2048. The rejection is a
     * policy decision, so it is a SecurityException and it leaves the OpenSSL error queue
     * clean; the same helper guards all four loaders, so one fixture covers them
     */

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::rsa2047PrivateKeyFixture()
            ),
        bl::SecurityException
        );

    UTF_CHECK( LocalTestPemKeyFormatsHelpers::isErrorQueueClean() );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_NonRsaKeysAreRejected )
{
    using namespace utest;

    /*
     * A well-formed key of another type (here an EC key, which every supported OpenSSL can
     * read as PKCS#8 and as SubjectPublicKeyInfo) is a policy rejection which names the key
     * type, not an OpenSSL failure with an empty error queue
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::ecPrivateKeyFixture()
            ),
        bl::SecurityException,
        "The PEM key is not an RSA key"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::ecPublicKeyFixture()
            ),
        bl::SecurityException,
        "The PEM key is not an RSA key"
        );

    UTF_CHECK( LocalTestPemKeyFormatsHelpers::isErrorQueueClean() );
}

#if OPENSSL_VERSION_NUMBER >= 0x10101000L

UTF_AUTO_TEST_CASE( PemKeyFormats_InconsistentPrivateKeyIsRejected )
{
    using namespace utest;

    /*
     * A private key whose CRT parameters do not agree with its modulus, exponents and factors
     * parses, has an acceptable modulus and exponent, and is not a valid key pair: it would
     * sign wrongly. The full key check refuses it - ::EVP_PKEY_check, which on OpenSSL 3.x
     * validates the whole pair rather than merely that 1 <= d < n, and ::RSA_check_key_ex on
     * 1.1.1. The check exists from OpenSSL 1.1.1, so the case is not built below that
     */

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::inconsistentPrivateKeyFixture()
            ),
        bl::SystemException
        );

    /*
     * A failure must not poison subsequent operations
     */

    UTF_CHECK_NO_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::legacyPrivateKeyFixture()
            )
        );
}

#endif // OPENSSL_VERSION_NUMBER >= 0x10101000L

UTF_AUTO_TEST_CASE( PemKeyFormats_SmallPublicExponentIsRejected )
{
    using namespace utest;

    /*
     * A key with the public exponent 3 is refused on every version of OpenSSL, as a policy
     * rejection which names the reason and leaves the error queue clean; OpenSSL's own public
     * key check bounds the exponent only in a FIPS build, so this is the library's check
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::smallExponentPrivateKeyFixture()
            ),
        bl::SecurityException,
        "public exponent"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::security::JsonSecuritySerialization::loadPublicKeyFromPemString(
            LocalTestPemKeyFormatsHelpers::smallExponentPublicKeyFixture()
            ),
        bl::SecurityException,
        "public exponent"
        );

    UTF_CHECK( LocalTestPemKeyFormatsHelpers::isErrorQueueClean() );
}

UTF_AUTO_TEST_CASE( PemKeyFormats_EncryptedExportUsesStrongKeyDerivation )
{
    using namespace utest;

    /*
     * The PBKDF2 iteration count of an encrypted export is pinned. OpenSSL's default of 2048
     * is what ::PEM_write_bio_PrivateKey applies; the library builds the PBES2 envelope itself
     * with PKCS8_PBKDF2_ITERATIONS (600000). The count is an INTEGER inside the PBKDF2
     * parameters, so its DER encoding (02 03 09 27 c0) must appear in the decoded body and the
     * encoding of the default (02 02 08 00) must not
     */

    const auto rsaKey = bl::crypto::RsaKey::createInstance();
    rsaKey -> generate();

    const auto encryptedPem = bl::security::JsonSecuritySerialization::getPrivateKeyAsPemString(
        rsaKey,
        bl::security::KeyProtection::Encrypted,
        "1234"
        );

    UTF_REQUIRE(
        encryptedPem.find( "-----BEGIN ENCRYPTED PRIVATE KEY-----" ) == LocalTestPemKeyFormatsHelpers::labelPos
        );

    std::string base64;
    std::istringstream is( encryptedPem );
    std::string line;

    while( std::getline( is, line ) )
    {
        if( line.empty() || 0 == line.compare( 0, 5, "-----" ) )
        {
            continue;
        }

        base64 += line;
    }

    const auto der = bl::SerializationUtils::base64DecodeVector( base64 );

    const unsigned char pinnedCount[] = { 0x02, 0x03, 0x09, 0x27, 0xC0 };
    const unsigned char defaultCount[] = { 0x02, 0x02, 0x08, 0x00 };

    UTF_REQUIRE(
        der.end() != std::search( der.begin(), der.end(), pinnedCount, pinnedCount + sizeof( pinnedCount ) )
        );

    UTF_REQUIRE(
        der.end() == std::search( der.begin(), der.end(), defaultCount, defaultCount + sizeof( defaultCount ) )
        );

    /*
     * And the key still reads back with the password
     */

    UTF_CHECK_EQUAL(
        LocalTestPemKeyFormatsHelpers::getPrivateExponent(
            bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( encryptedPem, "1234" )
            ),
        LocalTestPemKeyFormatsHelpers::getPrivateExponent( rsaKey )
        );
}
