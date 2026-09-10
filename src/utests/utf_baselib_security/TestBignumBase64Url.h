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

#include <baselib/crypto/BignumBase64Url.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/TestUtils.h>

UTF_AUTO_TEST_CASE( BignumBase64UrlTest )
{
    using namespace bl::crypto;

    {
        const auto bignum = BignumBase64Url::base64UrlToBignum( "AQAB" );

        const auto actualDecimal = openssl_string_ptr_t::attach(
            BN_bn2dec( bignum.get() )
            );

        BL_CHK_CRYPTO_API_NM( actualDecimal );

        UTF_REQUIRE_EQUAL( actualDecimal.get(), std::string( "65537" ) );
    }

    {
        BIGNUM* bignum = nullptr;
        const auto length = BN_dec2bn( &bignum, "65537" );

        const auto guard = bignum_ptr_t::attach( bignum );

        BL_CHK_CRYPTO_API_NM( length );

        UTF_REQUIRE_EQUAL(
            BignumBase64Url::bignumToBase64Url( bignum ),
            "AQAB"
            );
    }
}

UTF_AUTO_TEST_CASE( BignumBase64Url_DegenerateValues )
{
    using namespace bl::crypto;

    /*
     * This codec serializes and parses every JWK component in JsonSecuritySerializationImpl.h
     * (n, e, d, p, q, dmp1, dmq1, iqmp), on JSON which can come from a remote party, and
     * BignumBase64UrlTest covers exactly one three byte value in each direction
     */

    ( void ) ::ERR_clear_error();

    /*
     * The empty string is *accepted* - it decodes to an empty vector and ::BN_bin2bn returns
     * a valid BIGNUM whose value is zero
     */

    const auto zero = BignumBase64Url::base64UrlToBignum( bl::str::empty() );

    UTF_REQUIRE( zero );
    UTF_REQUIRE( ::BN_is_zero( zero.get() ) );

    /*
     * ... but encoding zero *throws*: BN_num_bytes is 0, so ::BN_bn2bin returns 0 and
     * BL_CHK_CRYPTO_API_NM treats that as a failure. This asymmetry - decode accepts what
     * encode cannot produce - is deliberate here only in the sense that nobody has decided
     * otherwise; a future canonicalization change has to come to this assertion and decide
     */

    UTF_REQUIRE_THROW( BignumBase64Url::bignumToBase64Url( zero ), bl::SystemException );

    /*
     * The decode is not canonical - ::BN_bin2bn drops leading zero bytes, so a non canonical
     * encoding and the canonical one decode to the same value and both re-encode to the
     * canonical form. A JWK which is loaded and re-emitted can therefore change its own text,
     * and with it its DataModelObject::getObjectHash identity
     */

    const auto nonCanonical = BignumBase64Url::base64UrlToBignum( "AAEAAQ" );
    const auto canonical = BignumBase64Url::base64UrlToBignum( "AQAB" );

    UTF_REQUIRE( nonCanonical );
    UTF_REQUIRE( canonical );

    UTF_REQUIRE_EQUAL( 0, ::BN_cmp( nonCanonical.get(), canonical.get() ) );

    UTF_REQUIRE_EQUAL( BignumBase64Url::bignumToBase64Url( nonCanonical ), std::string( "AQAB" ) );

    /*
     * The single byte round trips - '_w' is what pins that the URL alphabet substitution
     * really happens in *both* directions, which the 'AQAB' vector cannot show because it
     * contains neither '-' nor '_'
     */

    {
        const auto one = BignumBase64Url::base64UrlToBignum( "AQ" );

        UTF_REQUIRE( one );
        UTF_REQUIRE( ::BN_is_one( one.get() ) );
        UTF_REQUIRE_EQUAL( BignumBase64Url::bignumToBase64Url( one ), std::string( "AQ" ) );
    }

    {
        const auto maxByte = BignumBase64Url::base64UrlToBignum( "_w" );

        UTF_REQUIRE( maxByte );
        UTF_REQUIRE_EQUAL( ::BN_get_word( maxByte.get() ), 255UL );
        UTF_REQUIRE_EQUAL( BignumBase64Url::bignumToBase64Url( maxByte ), std::string( "_w" ) );
    }

    /*
     * Malformed input is an ArgumentException and *not* a crypto SystemException - that is
     * what a caller which is handed a remote JWK has to catch
     */

    UTF_REQUIRE_THROW( BignumBase64Url::base64UrlToBignum( "A" ), bl::ArgumentException );
    UTF_REQUIRE_THROW( BignumBase64Url::base64UrlToBignum( "AQ!B" ), bl::ArgumentException );

    /*
     * The zero encode throw is the one path here which could have left junk behind
     */

    UTF_CHECK( 0 == bl::crypto::detail::getFirstError().value() );
}

UTF_AUTO_TEST_CASE( BignumBase64Url_FullWidthRoundTrip )
{
    using namespace bl::crypto;

    /*
     * A full width modulus is the component this codec actually carries in production, and
     * nothing has ever round tripped one
     */

    const auto rsaKey = bl::security::JsonSecuritySerialization::loadPrivateKeyFromPemString(
        utest::TestUtils::loadDataFile( "test-private-key.pem" )
        );

    UTF_REQUIRE( rsaKey );

    const auto modulus =
        bl::security::JsonSecuritySerialization::getPublicKeyAsJsonObject( rsaKey ) -> modulus();

    /*
     * 256 bytes encode to 342 base64url characters and SerializationUtils::base64UrlEncode
     * strips the padding
     */

    UTF_REQUIRE_EQUAL( modulus.size(), 342U );
    UTF_REQUIRE( modulus.find( '=' ) == std::string::npos );

    const auto bigNumber = BignumBase64Url::base64UrlToBignum( modulus );

    UTF_REQUIRE( bigNumber );

    UTF_REQUIRE_EQUAL( BN_num_bytes( bigNumber.get() ), 256 );
    UTF_REQUIRE_EQUAL( ::BN_num_bits( bigNumber.get() ), 2048 );

    /*
     * ... and re-encoding reproduces the identical string
     */

    UTF_REQUIRE_EQUAL( BignumBase64Url::bignumToBase64Url( bigNumber ), modulus );

    UTF_CHECK( 0 == bl::crypto::detail::getFirstError().value() );
}
