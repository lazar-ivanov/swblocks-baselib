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

#include <baselib/data/models/Jwt.h>

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestUtils.h>

/************************************************************************
 * JWT claims set models
 *
 * bl::dm::jwt is the only user of BL_DM_DECLARE_STRING_OR_ARRAY_ALTERNATE_PROPERTY in the
 * repository, and Jwt.h is included by nothing except JsonSecurity.h, which is itself included
 * by nothing - so without this file none of the four "aud" declarations is ever instantiated
 * and neither branch of the serializer, neither branch of the deserializer nor the canonicalize
 * interaction is ever executed against the production models
 *
 * DataModelStringOrArrayPropertyTests covers the same macro against a test model; this case
 * covers the shipped declarations and their RFC 7519 JSON names
 */

UTF_AUTO_TEST_CASE( JsonSecurityModels_JwtClaimsSetTests )
{
    using namespace bl;
    using namespace bl::dm;
    using namespace bl::dm::jwt;

    typedef DataModelUtils dmu;

    const auto packed = []( SAA_in const om::ObjPtr< CoreClaimsSet >& obj ) -> std::string
    {
        return dmu::getDocAsPackedJsonString( obj );
    };

    {
        /*
         * A scalar "aud" in stays a scalar "aud" out - the asymmetry is deliberate, so a token
         * which used the scalar form round-trips unchanged (RFC 7519 section 4.1.3)
         *
         * The other core claims are asserted here as well, so a typo in any of the RFC JSON
         * names is caught by the same block
         */

        const auto obj = dmu::loadFromJsonText< CoreClaimsSet >(
            R"({"iss":"iss1","sub":"sub1","aud":"aud1","exp":4102444800,"nbf":100,"iat":50,"jti":"jti1"})"
            );

        UTF_REQUIRE_EQUAL( obj -> audience().size(), 1U );
        UTF_REQUIRE_EQUAL( obj -> audience()[ 0 ], "aud1" );

        UTF_CHECK_EQUAL( obj -> issuer(), "iss1" );
        UTF_CHECK_EQUAL( obj -> subject(), "sub1" );
        UTF_CHECK_EQUAL( obj -> tokenId(), "jti1" );
        UTF_CHECK_EQUAL( obj -> expiresAt(), 4102444800ULL );
        UTF_CHECK_EQUAL( obj -> notBefore(), 100ULL );
        UTF_CHECK_EQUAL( obj -> issuedAt(), 50ULL );

        const auto text = packed( obj );

        UTF_CHECK( cpp::contains( text, "\"aud\":\"aud1\"" ) );
        UTF_CHECK( ! cpp::contains( text, "[" ) );

        UTF_CHECK( cpp::contains( text, "\"iss\":" ) );
        UTF_CHECK( cpp::contains( text, "\"sub\":" ) );
        UTF_CHECK( cpp::contains( text, "\"exp\":4102444800" ) );
        UTF_CHECK( cpp::contains( text, "\"nbf\":" ) );
        UTF_CHECK( cpp::contains( text, "\"iat\":" ) );
        UTF_CHECK( cpp::contains( text, "\"jti\":\"jti1\"" ) );
    }

    {
        /*
         * Two or more elements are an array, in order
         */

        const auto obj = dmu::loadFromJsonText< CoreClaimsSet >( R"({"aud":["a1","a2"]})" );

        UTF_REQUIRE_EQUAL( obj -> audience().size(), 2U );

        UTF_CHECK( cpp::contains( packed( obj ), "\"aud\":[\"a1\",\"a2\"]" ) );
    }

    {
        /*
         * A JSON null is treated exactly like an absent member as far as the PROPERTY is
         * concerned - the deserializer returns before it records "aud" as processed
         *
         * Note that the plan's assertion for this sub-case ("aud" absent from the serialized
         * text) does not match the implementation and is not written here: because "aud" is
         * never marked as processed, the null lands in the unmapped bag and is re-emitted
         * verbatim on the way out. DataModelStringOrArrayPropertyTests already pins that same
         * behaviour for the test model, and DataModelUnmappedInteractionTests explains why
         */

        const auto obj = dmu::loadFromJsonText< CoreClaimsSet >( R"({"aud":null})" );

        UTF_REQUIRE( obj -> audience().empty() );
        UTF_REQUIRE_EQUAL( obj -> unmapped().size(), 1U );

        UTF_CHECK( cpp::contains( packed( obj ), "\"aud\":null" ) );
    }

    {
        /*
         * An absent "aud" emits nothing at all when canonicalize is off ...
         */

        const auto obj = dmu::loadFromJsonText< CoreClaimsSet >( R"({"iss":"iss1"})" );

        UTF_REQUIRE( obj -> audience().empty() );
        UTF_REQUIRE( obj -> unmapped().empty() );

        UTF_CHECK( ! cpp::contains( packed( obj ), "aud" ) );

        /*
         * ... and an EMPTY ARRAY when it is on - which is the shape
         * DataModelUtils::getObjectHashCanonical() hashes, so changing it changes every
         * canonical hash taken over a claims set
         */

        const auto canonical = dmu::getJsonString(
            obj,
            false           /* prettyPrint */,
            true            /* canonicalize */
            );

        UTF_CHECK( cpp::contains( canonical, "\"aud\":[]" ) );
    }

    {
        /*
         * The scalar shape is a property of the VALUE and not of the input document - one
         * element set programmatically serializes as a bare string too
         */

        const auto obj = CoreClaimsSet::createInstance();

        obj -> audienceLvalue().push_back( "only" );

        UTF_REQUIRE_EQUAL( obj -> audience().size(), 1U );

        const auto text = packed( obj );

        UTF_CHECK( cpp::contains( text, "\"aud\":\"only\"" ) );
        UTF_CHECK( ! cpp::contains( text, "[" ) );
    }

    {
        /*
         * BL_DM_DEFINE_CLASS_BEGIN generates a standalone class, so the repeated property
         * blocks in Jwt.h are copy-paste rather than inheritance and a member-name test on one
         * class protects only that class - the remaining three declarations are instantiated
         * and round-tripped here
         */

        {
            const auto obj = dmu::loadFromJsonText< SafeToReplicateCoreClaimsSet >( R"({"aud":"a-safe"})" );

            UTF_REQUIRE_EQUAL( obj -> audience().size(), 1U );
            UTF_REQUIRE_EQUAL( obj -> audience()[ 0 ], "a-safe" );

            UTF_CHECK( cpp::contains( dmu::getDocAsPackedJsonString( obj ), "\"aud\":\"a-safe\"" ) );
        }

        {
            const auto obj = dmu::loadFromJsonText< WellKnownClaimsSet >( R"({"aud":"a-wellknown"})" );

            UTF_REQUIRE_EQUAL( obj -> audience().size(), 1U );
            UTF_REQUIRE_EQUAL( obj -> audience()[ 0 ], "a-wellknown" );

            UTF_CHECK( cpp::contains( dmu::getDocAsPackedJsonString( obj ), "\"aud\":\"a-wellknown\"" ) );
        }

        {
            const auto obj = dmu::loadFromJsonText< ClaimsSetBase >( R"({"aud":"a-base"})" );

            UTF_REQUIRE_EQUAL( obj -> audience().size(), 1U );
            UTF_REQUIRE_EQUAL( obj -> audience()[ 0 ], "a-base" );

            UTF_CHECK( cpp::contains( dmu::getDocAsPackedJsonString( obj ), "\"aud\":\"a-base\"" ) );
        }
    }
}
