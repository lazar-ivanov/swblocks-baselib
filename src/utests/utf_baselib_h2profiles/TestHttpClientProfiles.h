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

#ifndef __UTEST_TESTHTTPCLIENTPROFILES_H_
#define __UTEST_TESTHTTPCLIENTPROFILES_H_

#include <baselib/crypto/TlsClientProfile.h>
#include <baselib/http2/Http2Profile.h>
#include <baselib/httpclient/HeaderProfile.h>

#include <baselib/data/models/HttpClientProfiles.h>
#include <baselib/data/DataModelObject.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

/*
 * Slice S1.4 - the profile shape types and their JSON data models
 *
 * These are pure-data config types and the models which load them, and that is the whole of the
 * slice: no content and no loader (they are S7.2's). So every value used below is deliberately
 * synthetic - "SUITE-A", 111, "test-family" - and no test here asserts anything about any real
 * browser. A test which did would be pinning content this slice has not earned the right to write,
 * and would have to be rewritten the moment the ground truth of design 6.7 is captured
 *
 * What is worth pinning, and is pinned here:
 *
 *   - a default-constructed shape struct is zeroed, not garbage, so a profile which leaves a knob
 *     unset gets a defined value rather than whatever the stack held
 *   - the fields whose ORDER is the fingerprint keep it - SETTINGS, the pseudo-header order, the
 *     default header list (design 6.4, 6.5)
 *   - the data model round-trips through JSON, which is the slice's acceptance, and reaches a fixed
 *     point: every property survives both directions, including the nested and the repeated ones
 *   - a SETTINGS value spanning the full uint32 range survives, which is why that one property is
 *     declared 64-bit
 *   - the required properties are enforced on load, at the top level and inside a nested object
 */

namespace
{
    /**
     * @brief A fully populated browser profile document, with synthetic values throughout
     *
     * Every property of every model is present, so a property which fails to deserialize shows up
     * as a difference in the round trip rather than passing unnoticed as an absent one
     */

    const char* g_browserProfileJson =
        "{"
        "    \"id\": \"test-profile-1\","
        "    \"family\": \"test-family\","
        "    \"grade\": \"Approximate\","
        "    \"deviations\": [ \"first deviation\", \"second deviation\" ],"
        "    \"tls\":"
        "    {"
        "        \"cipherSuitesTls12\": [ \"SUITE-A\", \"SUITE-B\" ],"
        "        \"cipherSuitesTls13\": [ \"SUITE-C\" ],"
        "        \"groups\":"
        "        ["
        "            { \"name\": \"GROUP-A\", \"keyShare\": true },"
        "            { \"name\": \"GROUP-B\", \"keyShare\": false }"
        "        ],"
        "        \"signatureAlgorithms\": [ \"SIGALG-A\", \"SIGALG-B\" ],"
        "        \"alpnProtocols\": [ \"h2\", \"http/1.1\" ],"
        "        \"sessionTicket\": true,"
        "        \"statusRequest\": true,"
        "        \"signedCertificateTimestamp\": false,"
        "        \"padding\": true"
        "    },"
        "    \"http2\":"
        "    {"
        "        \"settings\":"
        "        ["
        "            { \"id\": 1, \"value\": 111 },"
        "            { \"id\": 2, \"value\": 0 },"
        "            { \"id\": 65535, \"value\": 222 }"
        "        ],"
        "        \"connectionWindowUpdateIncrement\": 333,"
        "        \"windowUpdateThreshold\": 44,"
        "        \"idleStreamPriorities\":"
        "        ["
        "            { \"streamId\": 3, \"streamDependency\": 0, \"weight\": 11, \"exclusive\": false },"
        "            { \"streamId\": 5, \"streamDependency\": 3, \"weight\": 22, \"exclusive\": true }"
        "        ],"
        "        \"headersPriority\":"
        "            { \"isSet\": true, \"streamDependency\": 7, \"weight\": 33, \"exclusive\": true },"
        "        \"pseudoHeaderOrder\": [ \"method\", \"authority\", \"scheme\", \"path\" ],"
        "        \"hpackEncoderTableSize\": 555,"
        "        \"hpackIndexingPolicy\": \"Incremental\","
        "        \"cookieCrumbling\": true"
        "    },"
        "    \"headers\":"
        "    {"
        "        \"navigation\":"
        "        {"
        "            \"defaultHeaders\":"
        "            ["
        "                { \"name\": \"test-first\", \"value\": \"one\", \"isComputed\": false },"
        "                { \"name\": \"test-second\", \"value\": \"two\", \"isComputed\": true }"
        "            ],"
        "            \"callerHeaderPlacement\": \"BeforeAnchor\","
        "            \"callerHeaderAnchor\": \"test-second\","
        "            \"http1CaseMap\": { \"test-first\": \"Test-First\" },"
        "            \"priorityHeaderValue\": \"u=0, i\""
        "        },"
        "        \"fetch\":"
        "        {"
        "            \"defaultHeaders\": [ { \"name\": \"test-third\", \"value\": \"three\" } ],"
        "            \"callerHeaderPlacement\": \"Appended\","
        "            \"callerHeaderAnchor\": \"\","
        "            \"http1CaseMap\": {},"
        "            \"priorityHeaderValue\": \"u=1, i\""
        "        },"
        "        \"subresource\":"
        "        {"
        "            \"defaultHeaders\": [ { \"name\": \"test-fourth\", \"value\": \"four\" } ],"
        "            \"callerHeaderPlacement\": \"Prepended\","
        "            \"callerHeaderAnchor\": \"\","
        "            \"http1CaseMap\": {},"
        "            \"priorityHeaderValue\": \"u=3\""
        "        },"
        "        \"acceptEncoding\": [ \"enc-a\", \"enc-b\" ],"
        "        \"acceptLanguageQValues\": [ \"0.9\", \"0.8\" ]"
        "    },"
        "    \"userAgent\": \"test-user-agent/1.0\","
        "    \"secChUaBrands\":"
        "    ["
        "        { \"brand\": \"TestBrand\", \"version\": \"1\" },"
        "        { \"brand\": \"TestNotABrand\", \"version\": \"99\" }"
        "    ],"
        "    \"platform\": \"TestPlatform\""
        "}";

    /**
     * @brief The canonical serialized form of a data model object
     *
     * Canonical, so that a property left at its default is still written out - an absent property
     * and a defaulted one must not be allowed to compare equal, or a property which silently fails
     * to deserialize would pass the round trip
     */

    template
    <
        typename T
    >
    std::string canonicalJson( SAA_in const bl::om::ObjPtr< T >& dataObject )
    {
        return bl::dm::DataModelUtils::getJsonString(
            dataObject,
            false           /* prettyPrint */,
            true            /* canonicalize */
            );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( HttpClientProfiles_ShapeTypesDefaultToDefinedValuesTests )
{
    using namespace bl;

    /*
     * Every scalar knob is wrapped in cpp::ScalarTypeIniter precisely so this holds. A profile
     * which omits a knob must get a defined value, because these structs are filled field by field
     * by a loader from a document which may name any subset of them
     */

    const crypto::TlsClientProfile tls;

    UTF_REQUIRE( tls.cipherSuitesTls12.empty() );
    UTF_REQUIRE( tls.cipherSuitesTls13.empty() );
    UTF_REQUIRE( tls.groups.empty() );
    UTF_REQUIRE( tls.signatureAlgorithms.empty() );
    UTF_REQUIRE( tls.alpnProtocols.empty() );
    UTF_REQUIRE_EQUAL( tls.sessionTicket.value(), false );
    UTF_REQUIRE_EQUAL( tls.statusRequest.value(), false );
    UTF_REQUIRE_EQUAL( tls.signedCertificateTimestamp.value(), false );
    UTF_REQUIRE_EQUAL( tls.padding.value(), false );

    const crypto::TlsGroup group;

    UTF_REQUIRE( group.name.empty() );
    UTF_REQUIRE_EQUAL( group.keyShare.value(), false );

    const http2::Http2Profile h2;

    UTF_REQUIRE( h2.settings.empty() );
    UTF_REQUIRE_EQUAL( h2.connectionWindowUpdateIncrement.value(), 0U );
    UTF_REQUIRE_EQUAL( h2.windowUpdateThreshold.value(), 0U );
    UTF_REQUIRE( h2.idleStreamPriorities.empty() );
    UTF_REQUIRE( h2.pseudoHeaderOrder.empty() );
    UTF_REQUIRE_EQUAL( h2.hpackEncoderTableSize.value(), 0U );
    UTF_REQUIRE_EQUAL( h2.cookieCrumbling.value(), false );

    /*
     * A default Http2HeadersPriority is 'not set' rather than 'set, all zero'. Those are different
     * frames on the wire, so the distinction has to survive default construction
     */

    UTF_REQUIRE_EQUAL( h2.headersPriority.isSet.value(), false );
    UTF_REQUIRE_EQUAL( h2.headersPriority.streamDependency.value(), 0U );
    UTF_REQUIRE_EQUAL( h2.headersPriority.weight.value(), 0U );
    UTF_REQUIRE_EQUAL( h2.headersPriority.exclusive.value(), false );

    /*
     * The two enumerations are zero-initialized to their first enumerator, which is the value a
     * loader would otherwise have to remember to write
     */

    UTF_REQUIRE( h2.hpackIndexingPolicy.value() == http2::HpackIndexingPolicy::Incremental );

    const httpclient::HeaderProfile headers;

    UTF_REQUIRE( headers.byRequestKind.empty() );
    UTF_REQUIRE( headers.acceptEncoding.empty() );
    UTF_REQUIRE( headers.acceptLanguageQValues.empty() );

    const httpclient::HeaderProfileForKind forKind;

    UTF_REQUIRE( forKind.defaultHeaders.empty() );
    UTF_REQUIRE( forKind.callerHeaderAnchor.empty() );
    UTF_REQUIRE( forKind.http1CaseMap.empty() );
    UTF_REQUIRE( forKind.priorityHeaderValue.empty() );
    UTF_REQUIRE( forKind.callerHeaderPlacement.value() == httpclient::CallerHeaderPlacement::Appended );

    const httpclient::ProfileHeader header;

    UTF_REQUIRE( header.name.empty() );
    UTF_REQUIRE( header.value.empty() );
    UTF_REQUIRE_EQUAL( header.isComputed.value(), false );
}

UTF_AUTO_TEST_CASE( HttpClientProfiles_ShapeTypesPreserveOrderTests )
{
    using namespace bl;

    /*
     * The fields below are ordered containers and not maps or sets, because for each of them the
     * order is itself observable - it is the thing being impersonated (design 6.4, 6.5). This case
     * is what stops a later change quietly turning one of them into a map
     */

    http2::Http2Profile h2;

    h2.settings.push_back( http2::Http2Setting() );
    h2.settings.push_back( http2::Http2Setting() );
    h2.settings.push_back( http2::Http2Setting() );

    h2.settings[ 0 ].id = 4U;
    h2.settings[ 1 ].id = 1U;
    h2.settings[ 2 ].id = 2U;

    UTF_REQUIRE_EQUAL( h2.settings.size(), 3U );
    UTF_REQUIRE_EQUAL( h2.settings[ 0 ].id.value(), 4U );
    UTF_REQUIRE_EQUAL( h2.settings[ 1 ].id.value(), 1U );
    UTF_REQUIRE_EQUAL( h2.settings[ 2 ].id.value(), 2U );

    h2.pseudoHeaderOrder.push_back( http2::Http2PseudoHeader::Method );
    h2.pseudoHeaderOrder.push_back( http2::Http2PseudoHeader::Path );
    h2.pseudoHeaderOrder.push_back( http2::Http2PseudoHeader::Authority );
    h2.pseudoHeaderOrder.push_back( http2::Http2PseudoHeader::Scheme );

    UTF_REQUIRE_EQUAL( h2.pseudoHeaderOrder.size(), 4U );
    UTF_REQUIRE( h2.pseudoHeaderOrder[ 0 ] == http2::Http2PseudoHeader::Method );
    UTF_REQUIRE( h2.pseudoHeaderOrder[ 1 ] == http2::Http2PseudoHeader::Path );
    UTF_REQUIRE( h2.pseudoHeaderOrder[ 2 ] == http2::Http2PseudoHeader::Authority );
    UTF_REQUIRE( h2.pseudoHeaderOrder[ 3 ] == http2::Http2PseudoHeader::Scheme );

    httpclient::HeaderProfileForKind navigation;

    navigation.defaultHeaders.push_back( httpclient::ProfileHeader() );
    navigation.defaultHeaders.push_back( httpclient::ProfileHeader() );

    navigation.defaultHeaders[ 0 ].name = "test-second";
    navigation.defaultHeaders[ 1 ].name = "test-first";

    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].name, "test-second" );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 1 ].name, "test-first" );

    /*
     * The per-kind tables are keyed by the enumeration, so a kind is named rather than positional
     */

    httpclient::HeaderProfile headers;

    headers.byRequestKind[ httpclient::HttpRequestKind::Navigation ] = navigation;

    UTF_REQUIRE_EQUAL( headers.byRequestKind.size(), 1U );
    UTF_REQUIRE( headers.byRequestKind.find( httpclient::HttpRequestKind::Navigation ) != headers.byRequestKind.end() );
    UTF_REQUIRE( headers.byRequestKind.find( httpclient::HttpRequestKind::Fetch ) == headers.byRequestKind.end() );

    UTF_REQUIRE_EQUAL(
        headers.byRequestKind[ httpclient::HttpRequestKind::Navigation ].defaultHeaders[ 0 ].name,
        "test-second"
        );
}

UTF_AUTO_TEST_CASE( HttpClientProfiles_BrowserProfileRoundTripTests )
{
    using namespace bl;
    using namespace bl::dm::httpclient;

    const auto profile = dm::DataModelUtils::loadFromJsonText< BrowserProfile >( g_browserProfileJson );

    UTF_REQUIRE( profile );

    /*
     * Spot checks of the properties whose shape decisions this slice made, before the round trip
     * itself. A mechanical comparison alone would pass if BOTH directions dropped the same
     * property, so the values are read back through the accessors too
     */

    UTF_REQUIRE_EQUAL( profile -> id(), "test-profile-1" );
    UTF_REQUIRE_EQUAL( profile -> family(), "test-family" );
    UTF_REQUIRE_EQUAL( profile -> grade(), "Approximate" );
    UTF_REQUIRE_EQUAL( profile -> deviations().size(), 2U );
    UTF_REQUIRE_EQUAL( profile -> userAgent(), "test-user-agent/1.0" );
    UTF_REQUIRE_EQUAL( profile -> platform(), "TestPlatform" );

    UTF_REQUIRE_EQUAL( profile -> secChUaBrands().size(), 2U );
    UTF_REQUIRE_EQUAL( profile -> secChUaBrands()[ 0 ] -> brand(), "TestBrand" );
    UTF_REQUIRE_EQUAL( profile -> secChUaBrands()[ 1 ] -> version(), "99" );

    {
        const auto& tls = profile -> tls();
        UTF_REQUIRE( tls );

        UTF_REQUIRE_EQUAL( tls -> cipherSuitesTls12().size(), 2U );
        UTF_REQUIRE_EQUAL( tls -> cipherSuitesTls12()[ 0 ], "SUITE-A" );
        UTF_REQUIRE_EQUAL( tls -> cipherSuitesTls13().size(), 1U );
        UTF_REQUIRE_EQUAL( tls -> alpnProtocols().size(), 2U );
        UTF_REQUIRE_EQUAL( tls -> alpnProtocols()[ 0 ], "h2" );
        UTF_REQUIRE_EQUAL( tls -> alpnProtocols()[ 1 ], "http/1.1" );

        UTF_REQUIRE_EQUAL( tls -> groups().size(), 2U );
        UTF_REQUIRE_EQUAL( tls -> groups()[ 0 ] -> name(), "GROUP-A" );
        UTF_REQUIRE_EQUAL( tls -> groups()[ 0 ] -> keyShare(), true );
        UTF_REQUIRE_EQUAL( tls -> groups()[ 1 ] -> keyShare(), false );

        UTF_REQUIRE_EQUAL( tls -> sessionTicket(), true );
        UTF_REQUIRE_EQUAL( tls -> signedCertificateTimestamp(), false );
    }

    {
        const auto& h2 = profile -> http2();
        UTF_REQUIRE( h2 );

        UTF_REQUIRE_EQUAL( h2 -> settings().size(), 3U );

        /*
         * Order is preserved through JSON, and an id the library does not itself interpret - 65535
         * is not an assigned setting - survives as written
         */

        UTF_REQUIRE_EQUAL( h2 -> settings()[ 0 ] -> id(), 1 );
        UTF_REQUIRE_EQUAL( h2 -> settings()[ 1 ] -> id(), 2 );
        UTF_REQUIRE_EQUAL( h2 -> settings()[ 2 ] -> id(), 65535 );
        UTF_REQUIRE_EQUAL( h2 -> settings()[ 2 ] -> value(), 222U );

        UTF_REQUIRE_EQUAL( h2 -> pseudoHeaderOrder().size(), 4U );
        UTF_REQUIRE_EQUAL( h2 -> pseudoHeaderOrder()[ 0 ], "method" );
        UTF_REQUIRE_EQUAL( h2 -> pseudoHeaderOrder()[ 3 ], "path" );

        UTF_REQUIRE_EQUAL( h2 -> idleStreamPriorities().size(), 2U );
        UTF_REQUIRE_EQUAL( h2 -> idleStreamPriorities()[ 1 ] -> streamId(), 5U );

        UTF_REQUIRE( h2 -> headersPriority() );
        UTF_REQUIRE_EQUAL( h2 -> headersPriority() -> isSet(), true );
        UTF_REQUIRE_EQUAL( h2 -> headersPriority() -> weight(), 33 );

        UTF_REQUIRE_EQUAL( h2 -> hpackIndexingPolicy(), "Incremental" );
        UTF_REQUIRE_EQUAL( h2 -> cookieCrumbling(), true );
    }

    {
        const auto& headers = profile -> headers();
        UTF_REQUIRE( headers );

        UTF_REQUIRE( headers -> navigation() );
        UTF_REQUIRE( headers -> fetch() );
        UTF_REQUIRE( headers -> subresource() );

        const auto& navigation = headers -> navigation();

        UTF_REQUIRE_EQUAL( navigation -> defaultHeaders().size(), 2U );
        UTF_REQUIRE_EQUAL( navigation -> defaultHeaders()[ 0 ] -> name(), "test-first" );
        UTF_REQUIRE_EQUAL( navigation -> defaultHeaders()[ 1 ] -> isComputed(), true );
        UTF_REQUIRE_EQUAL( navigation -> callerHeaderPlacement(), "BeforeAnchor" );
        UTF_REQUIRE_EQUAL( navigation -> callerHeaderAnchor(), "test-second" );
        UTF_REQUIRE_EQUAL( navigation -> priorityHeaderValue(), "u=0, i" );

        UTF_REQUIRE_EQUAL( navigation -> http1CaseMap().size(), 1U );
        UTF_REQUIRE_EQUAL( navigation -> http1CaseMap().at( "test-first" ), "Test-First" );

        UTF_REQUIRE_EQUAL( headers -> acceptEncoding().size(), 2U );

        /*
         * The q-values are the tokens the profile emits, not numbers, so that the rendered header
         * matches byte for byte
         */

        UTF_REQUIRE_EQUAL( headers -> acceptLanguageQValues().size(), 2U );
        UTF_REQUIRE_EQUAL( headers -> acceptLanguageQValues()[ 0 ], "0.9" );
        UTF_REQUIRE_EQUAL( headers -> acceptLanguageQValues()[ 1 ], "0.8" );
    }

    /*
     * The round trip proper: serialize, load the result, serialize again. The two canonical
     * documents must be identical, which is the fixed point every property has to reach in both
     * directions. Comparing against the authored document instead would not work - and would not
     * be the right test - because canonical serialization also writes the properties that document
     * left out
     */

    const auto serialized = canonicalJson( profile );

    const auto reloaded = dm::DataModelUtils::loadFromJsonText< BrowserProfile >( serialized );

    UTF_REQUIRE( reloaded );

    UTF_REQUIRE_EQUAL( canonicalJson( reloaded ), serialized );

    /*
     * And the values themselves after a full trip, so that "identical" cannot be satisfied by two
     * equally empty documents
     */

    UTF_REQUIRE_EQUAL( reloaded -> id(), "test-profile-1" );
    UTF_REQUIRE_EQUAL( reloaded -> tls() -> groups()[ 0 ] -> name(), "GROUP-A" );
    UTF_REQUIRE_EQUAL( reloaded -> http2() -> settings()[ 2 ] -> id(), 65535 );
    UTF_REQUIRE_EQUAL( reloaded -> headers() -> navigation() -> defaultHeaders()[ 0 ] -> name(), "test-first" );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Round-tripped browser profile:\n"
            << dm::DataModelUtils::getDocAsPrettyJsonString( reloaded )
        );
}

UTF_AUTO_TEST_CASE( HttpClientProfiles_SettingValueSpansFullUint32RangeTests )
{
    using namespace bl;
    using namespace bl::dm::httpclient;

    /*
     * A SETTINGS value is an unsigned 32-bit quantity on the wire (RFC 9113 section 6.5.1), and
     * SETTINGS_MAX_HEADER_LIST_SIZE's "unlimited" is literally 2^32-1. The int property of the data
     * model is a signed 32-bit int and would not hold it, which is why 'value' is declared 64-bit.
     * This case is what says so
     */

    const std::uint64_t maxSettingValue = 4294967295ULL;

    const auto document =
        "{ \"settings\": [ { \"id\": 6, \"value\": 4294967295 } ] }";

    const auto profile = dm::DataModelUtils::loadFromJsonText< Http2Profile >( document );

    UTF_REQUIRE( profile );
    UTF_REQUIRE_EQUAL( profile -> settings().size(), 1U );
    UTF_REQUIRE_EQUAL( profile -> settings()[ 0 ] -> value(), maxSettingValue );

    const auto reloaded = dm::DataModelUtils::loadFromJsonText< Http2Profile >( canonicalJson( profile ) );

    UTF_REQUIRE( reloaded );
    UTF_REQUIRE_EQUAL( reloaded -> settings()[ 0 ] -> value(), maxSettingValue );
}

UTF_AUTO_TEST_CASE( HttpClientProfiles_RequiredPropertiesAreEnforcedOnLoadTests )
{
    using namespace bl;
    using namespace bl::dm::httpclient;

    /*
     * A loaded profile is untrusted input, so the properties without which it cannot be identified
     * at all are required and are refused rather than defaulted. The nested case matters as much as
     * the top-level one: a group with no name would otherwise reach the context builder as an empty
     * string
     */

    UTF_REQUIRE_THROW(
        dm::DataModelUtils::loadFromJsonText< BrowserProfile >(
            "{ \"family\": \"test-family\", \"grade\": \"Approximate\" }"
            ),
        UserMessageException
        );

    UTF_REQUIRE_THROW(
        dm::DataModelUtils::loadFromJsonText< BrowserProfile >(
            "{ \"id\": \"test-profile-1\", \"grade\": \"Approximate\" }"
            ),
        UserMessageException
        );

    UTF_REQUIRE_THROW(
        dm::DataModelUtils::loadFromJsonText< TlsClientProfile >(
            "{ \"groups\": [ { \"keyShare\": true } ] }"
            ),
        UserMessageException
        );

    UTF_REQUIRE_THROW(
        dm::DataModelUtils::loadFromJsonText< HeaderProfileForKind >(
            "{ \"defaultHeaders\": [ { \"value\": \"one\" } ] }"
            ),
        UserMessageException
        );

    /*
     * The same documents with the required property present load cleanly, so the rejections above
     * are attributable to it and not to something else in the document
     */

    UTF_REQUIRE(
        dm::DataModelUtils::loadFromJsonText< BrowserProfile >(
            "{ \"id\": \"test-profile-1\", \"family\": \"test-family\", \"grade\": \"Approximate\" }"
            )
        );

    UTF_REQUIRE(
        dm::DataModelUtils::loadFromJsonText< TlsClientProfile >(
            "{ \"groups\": [ { \"name\": \"GROUP-A\", \"keyShare\": true } ] }"
            )
        );
}

#endif /* __UTEST_TESTHTTPCLIENTPROFILES_H_ */
