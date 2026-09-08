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

#include <baselib/crypto/RsaKey.h>
#include <baselib/crypto/RsaSignVerify.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

namespace utest
{
    template
    <
        typename E = void
    >
    class LocalTestRsaSignVerifyHelpersT
    {
        BL_DECLARE_STATIC( LocalTestRsaSignVerifyHelpersT )

    public:

        static std::string createEncryptedPemKeyAsText(
            SAA_in              const std::string&                                          password,
            SAA_in_opt          const bool                                                  print = false
            )
        {
            const auto rsaKey = bl::crypto::RsaKey::createInstance();
            rsaKey -> generate();

            auto privateKeyPemString = bl::security::JsonSecuritySerialization::getPrivateKeyAsPemString(
                rsaKey,
                bl::security::KeyProtection::Encrypted,
                password
                );

            if( print )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "PEM key as text:\n"
                        << privateKeyPemString
                    );
            }

            return privateKeyPemString;
        }
    };

    typedef LocalTestRsaSignVerifyHelpersT<> LocalTestRsaSignVerifyHelpers;

} // utest

UTF_AUTO_TEST_CASE( TestRsaSignVerifyPositive )
{
    const auto privateKeyPemString = utest::LocalTestRsaSignVerifyHelpers::createEncryptedPemKeyAsText(
        "1234" /* password */,
        true /* print */
        );

    /*
     * Verify that the key is actually encrypted by trying to decrypt it with the incorrect password
     */

    UTF_REQUIRE_THROW(
        bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString( privateKeyPemString, "12345678" ),
        bl::SystemException
        );

    const std::string message( "C++ is a wonderful language!" );

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        privateKeyPemString,
        "1234" /* password */
        );

    const auto signatureBase64Url = bl::crypto::RsaSignVerify::signAsBase64Url(
        rsaKey,
        message
        );

    const auto verifyResult = bl::crypto::RsaSignVerify::tryVerify(
        rsaKey,
        message,
        signatureBase64Url
        );

    UTF_REQUIRE( verifyResult == 1 );

    UTF_CHECK_NO_THROW(
        bl::crypto::RsaSignVerify::verify(
            rsaKey,
            message,
            signatureBase64Url
            )
        );
}

UTF_AUTO_TEST_CASE( TestRsaSignVerifyNegative )
{
    const auto privateKeyPemString =
        utest::LocalTestRsaSignVerifyHelpers::createEncryptedPemKeyAsText( "1234" /* password */ );;

    const std::string message( "C++ is a wonderful language!" );

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        privateKeyPemString,
        "1234" /* password */
        );

    auto signatureBase64Url = bl::crypto::RsaSignVerify::signAsBase64Url(
        rsaKey,
        message
        );

    /*
     * Break signature with random modification
     */

    for( std::size_t i = 0U, count = signatureBase64Url.size() - 1U; i < count; ++i )
    {
        /*
         * Note that i + 1U is always valid index as we iterate only to the one
         * before the last
         */

        if( signatureBase64Url[ i ] != signatureBase64Url[ i + 1U ] )
        {
            std::swap( signatureBase64Url[ i ], signatureBase64Url[ i + 1U ] );
            break;
        }
    }

    const auto verifyResult = bl::crypto::RsaSignVerify::tryVerify(
        rsaKey,
        message,
        signatureBase64Url
        );

    UTF_REQUIRE( verifyResult == 0 );

    UTF_CHECK_THROW(
        bl::crypto::RsaSignVerify::verify(
            rsaKey,
            message,
            signatureBase64Url
            ),
        std::exception
        );
}

UTF_AUTO_TEST_CASE( TestRsaSignVerifyDegenerateSignatures )
{
    /*
     * tryVerify() is documented as returning true or false and pointing the caller at
     * bl::crypto::getException for the reason, but its very first action is
     * SerializationUtils::base64UrlDecodeVector( signatureBase64Url ), which throws a
     * bl::ArgumentException for an input whose length is 1 modulo 4, which carries more than
     * two '=' characters, or which contains any character outside the base64 alphabet - so
     * for a malformed (i.e. attacker controlled) signature the 'try' form *throws*
     *
     * TestRsaSignVerifyNegative corrupts the signature by swapping two adjacent characters,
     * which keeps it valid base64url of the same length, so only the 'well formed but wrong'
     * branch has ever been exercised
     */

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        utest::LocalTestRsaSignVerifyHelpers::createEncryptedPemKeyAsText( "1234" /* password */ ),
        "1234" /* password */
        );

    /*
     * Generating an RSA key is slow, so the second key is generated once and reused for both
     * the wrong key rows and the error queue rows
     */

    const auto otherKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        utest::LocalTestRsaSignVerifyHelpers::createEncryptedPemKeyAsText( "1234" /* password */ ),
        "1234" /* password */
        );

    const std::string message( "C++ is a wonderful language!" );

    const auto signatureBase64Url = bl::crypto::RsaSignVerify::signAsBase64Url( rsaKey, message );

    UTF_REQUIRE( signatureBase64Url.size() > 4U );

    /*
     * (1) An empty signature decodes to an empty vector and ::RSA_verify fails cleanly
     */

    UTF_REQUIRE( ! bl::crypto::RsaSignVerify::tryVerify( rsaKey, message, bl::str::empty() ) );

    /*
     * (2) A truncated signature - dropping four characters keeps the length legal, so this
     * one still goes through the decoder and comes back as a plain false
     */

    UTF_REQUIRE(
        ! bl::crypto::RsaSignVerify::tryVerify(
            rsaKey,
            message,
            signatureBase64Url.substr( 0U, signatureBase64Url.size() - 4U )
            )
        );

    /*
     * (3) Not base64url at all, and a length which is 1 modulo 4 - this is the assertion
     * which documents the real contract; if tryVerify is subsequently made total, this is
     * the place which has to record the change
     */

    UTF_REQUIRE_THROW(
        bl::crypto::RsaSignVerify::tryVerify( rsaKey, message, "!!!!" ),
        bl::ArgumentException
        );

    UTF_REQUIRE_THROW(
        bl::crypto::RsaSignVerify::tryVerify( rsaKey, message, std::string( "AQABA" ) ),
        bl::ArgumentException
        );

    /*
     * (4) The wrong key - the 'try' form returns false and the checked form throws
     */

    UTF_REQUIRE( ! bl::crypto::RsaSignVerify::tryVerify( otherKey, message, signatureBase64Url ) );

    UTF_REQUIRE_THROW(
        bl::crypto::RsaSignVerify::verify( otherKey, message, signatureBase64Url ),
        bl::SystemException
        );

    /*
     * (5) The error queue contract - nothing drains the queue after a false return (that is
     * by design, per the doc comment), so the reason stays retrievable, and retrieving it
     * through bl::crypto::getException is what cleans the queue up
     */

    ( void ) ::ERR_clear_error();

    UTF_REQUIRE( ! bl::crypto::RsaSignVerify::tryVerify( otherKey, message, signatureBase64Url ) );

    UTF_REQUIRE( 0 != bl::crypto::detail::getFirstError().value() );

    ( void ) bl::crypto::getException( "verification failed" );

    UTF_REQUIRE( 0 == bl::crypto::detail::getFirstError().value() );

    /*
     * (6) A tampered message rather than a tampered signature
     */

    UTF_REQUIRE( ! bl::crypto::RsaSignVerify::tryVerify( rsaKey, message + "!", signatureBase64Url ) );

    UTF_REQUIRE( 0 != bl::crypto::detail::getFirstError().value() );

    /*
     * (7) ... and the queue is drained again, so nothing bleeds into the next case
     */

    ( void ) bl::crypto::getException( "verification failed" );

    UTF_CHECK( 0 == bl::crypto::detail::getFirstError().value() );
}
