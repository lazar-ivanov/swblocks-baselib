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

#ifndef __UTEST_TESTHMACSHA256_H_
#define __UTEST_TESTHMACSHA256_H_

#include <baselib/crypto/HmacSha256.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>

/*
 * Known answer tests for bl::crypto::detail::HmacSha256
 *
 * The OpenSSL 3.x implementation of this class was migrated from the deprecated HMAC_* interface to
 * EVP_MAC. HMAC-SHA-256 output is fully determined by the algorithm, so the migration is verifiable
 * against published vectors rather than against the previous implementation - these are the test
 * cases from RFC 4231 section 4, which is what makes them a real check and not a snapshot of
 * whatever the code happened to produce.
 *
 * Note that calculateMessageDigest() returns UPPERCASE hex, so the RFC's lowercase vectors are
 * transcribed in upper case here.
 */

UTF_AUTO_TEST_CASE( Crypto_HmacSha256KnownAnswerTests )
{
    using namespace bl;

    typedef crypto::detail::HmacSha256 hmac_t;

    /*
     * RFC 4231 test case 1: key is 20 bytes of 0x0b, data is "Hi There"
     */

    {
        const std::string key( 20U, static_cast< char >( 0x0b ) );

        UTF_REQUIRE_EQUAL(
            hmac_t::calculateMessageDigest( "Hi There", key ),
            std::string( "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7" )
            );
    }

    /*
     * RFC 4231 test case 2: a short ASCII key, which is the shape most callers will use
     */

    UTF_REQUIRE_EQUAL(
        hmac_t::calculateMessageDigest( "what do ya want for nothing?", "Jefe" ),
        std::string( "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843" )
        );

    /*
     * RFC 4231 test case 3: key is 20 bytes of 0xaa, data is 50 bytes of 0xdd - a binary key and a
     * binary message, which is what would break if the implementation ever treated either as a
     * NUL terminated C string
     */

    {
        const std::string key( 20U, static_cast< char >( 0xaa ) );
        const std::string message( 50U, static_cast< char >( 0xdd ) );

        UTF_REQUIRE_EQUAL(
            hmac_t::calculateMessageDigest( message, key ),
            std::string( "773EA91E36800E46854DB8EBD09181A72959098B3EF8C122D9635514CED565FE" )
            );
    }

    /*
     * RFC 4231 test case 6: a key longer than the 64 byte SHA-256 block size, which the algorithm
     * must hash down rather than truncate
     */

    {
        const std::string key( 131U, static_cast< char >( 0xaa ) );

        UTF_REQUIRE_EQUAL(
            hmac_t::calculateMessageDigest(
                "Test Using Larger Than Block-Size Key - Hash Key First",
                key
                ),
            std::string( "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54" )
            );
    }

    /*
     * An empty message and an empty key are both legal inputs and must not be confused with an
     * error; verified against the vector produced by the algorithm for that pair
     */

    UTF_REQUIRE_EQUAL(
        hmac_t::calculateMessageDigest( str::empty(), str::empty() ),
        std::string( "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD" )
        );

    /*
     * The digest is always the full 32 bytes, i.e. 64 hex characters
     */

    UTF_REQUIRE_EQUAL(
        hmac_t::calculateMessageDigest( "any message", "any key" ).size(),
        64U
        );
}

#endif /* __UTEST_TESTHMACSHA256_H_ */
