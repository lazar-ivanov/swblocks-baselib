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

#include <baselib/loader/Version.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

#include <string>
#include <vector>

/*
 * bl::loader::Version::fromString has no caller anywhere in the repository and
 * Version::toString is reached only from ManifestT::version() inside a log statement, so
 * neither of them has an in-repo consumer which would notice a regression
 *
 * The justification for pinning them anyway is that bl::loader is a published API whose
 * unit tests are its entire safety net, and fromString is pure input validation
 */

UTF_AUTO_TEST_CASE( TestVersionToStringAndRoundTrip )
{
    using namespace bl;
    using namespace bl::loader;

    UTF_REQUIRE_EQUAL( Version::toString( 1U, 2U, 3U ), std::string( "1.2.3" ) );
    UTF_REQUIRE_EQUAL( Version::toString( 0U, 0U, 0U ), std::string( "0.0.0" ) );
    UTF_REQUIRE_EQUAL( Version::toString( 99999U, 99999U, 99999U ), std::string( "99999.99999.99999" ) );

    /*
     * toString and fromString are inverse operations - note that toString applies no bound
     * at all while fromString caps every part at five characters, so the round trip only
     * closes for values which fit that cap
     */

    const auto roundTrip = [](
        SAA_in      const std::size_t                   expectedMajor,
        SAA_in      const std::size_t                   expectedMinor,
        SAA_in      const std::size_t                   expectedPatch
        ) -> void
    {
        std::size_t majorVersion = 0U;
        std::size_t minorVersion = 0U;
        std::size_t patchVersion = 0U;

        Version::fromString(
            Version::toString( expectedMajor, expectedMinor, expectedPatch ),
            majorVersion,
            minorVersion,
            patchVersion
            );

        UTF_REQUIRE_EQUAL( majorVersion, expectedMajor );
        UTF_REQUIRE_EQUAL( minorVersion, expectedMinor );
        UTF_REQUIRE_EQUAL( patchVersion, expectedPatch );
    };

    roundTrip( 0U, 0U, 0U );
    roundTrip( 1U, 2U, 3U );
    roundTrip( 10U, 0U, 66U );
    roundTrip( 99999U, 99999U, 99999U );

    /*
     * Leading zeros are accepted - the validator only requires digits, so lexical_cast sees
     * "00001" and answers 1; pinning the current choice
     */

    {
        std::size_t majorVersion = 0U;
        std::size_t minorVersion = 0U;
        std::size_t patchVersion = 0U;

        Version::fromString( "00001.02.003", majorVersion, minorVersion, patchVersion );

        UTF_REQUIRE_EQUAL( majorVersion, 1U );
        UTF_REQUIRE_EQUAL( minorVersion, 2U );
        UTF_REQUIRE_EQUAL( patchVersion, 3U );
    }
}

UTF_AUTO_TEST_CASE( TestVersionFromStringRejectsMalformedInput )
{
    using namespace bl;
    using namespace bl::loader;

    /*
     * Every one of these is rejected with bl::ArgumentException - not the sibling
     * bl::UnexpectedException, and not boost::bad_lexical_cast, which is what would escape
     * if the all-digits predicate were dropped and lexical_cast were left to do the checking
     */

    const std::vector< std::string > rejected =
    {
        "",
        "1",
        "1.2",
        "1.2.3.4",
        "1.2.",
        ".2.3",
        "1..3",
        "..",
        "1.2.3 ",
        " 1.2.3",
        "1.2.3x",
        "v1.2.3",
        "-1.2.3",
        "+1.2.3",
        "1.2.-3",
        "1,2,3",
        "100000.0.0",
        "0.100000.0",
        "0.0.100000",
        "1.2.3\n",
    };

    for( const auto& versionStr : rejected )
    {
        std::size_t majorVersion = 0U;
        std::size_t minorVersion = 0U;
        std::size_t patchVersion = 0U;

        UTF_REQUIRE_THROW_MESSAGE(
            Version::fromString( versionStr, majorVersion, minorVersion, patchVersion ),
            bl::ArgumentException,
            "Invalid version number"
            );
    }

    /*
     * The exact boundary the five character cap creates - 99999 is the largest value which
     * parses and it is a silent behaviour change from the previous unsigned short parser,
     * whose ceiling was 65535
     */

    {
        std::size_t majorVersion = 0U;
        std::size_t minorVersion = 0U;
        std::size_t patchVersion = 0U;

        Version::fromString( "99999.0.0", majorVersion, minorVersion, patchVersion );

        UTF_REQUIRE_EQUAL( majorVersion, 99999U );
        UTF_REQUIRE_EQUAL( minorVersion, 0U );
        UTF_REQUIRE_EQUAL( patchVersion, 0U );

        UTF_REQUIRE_THROW_MESSAGE(
            Version::fromString( "100000.0.0", majorVersion, minorVersion, patchVersion ),
            bl::ArgumentException,
            "Invalid version number"
            );
    }
}
