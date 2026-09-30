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

#ifndef __UTEST_TESTBROWSERPROFILES_H_
#define __UTEST_TESTBROWSERPROFILES_H_

#include <baselib/httpclient/BrowserProfiles.h>

#include <baselib/crypto/TlsNameRules.h>
#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>

#include <openssl/objects.h>
#include <openssl/ssl.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/*
 * L7-C - the browser profile loader and registry (httpclient/BrowserProfiles.h) and the TLS
 * profile name rule (crypto/TlsNameRules.h); notes/plans/http2-l7-execution-plan.md 4.2 and
 * notes/plans/http2-design.md 6.2
 *
 * Every document here is a synthetic fixture. The TLS names are real OpenSSL and IANA names,
 * because the rules are about real names, but no case asserts anything about a real browser:
 * the built-in profiles' content is derived from captures, which is L7-E's
 *
 * EVERY REFUSAL IS ATTRIBUTED. A document built to break one rule is refused with the loader's
 * one exception, InvalidDataFormatException, and the case asserts the message names the property
 * and the rule - otherwise a document refused for some other reason would pass for this one. Each
 * rule also has a case which accepts its boundary, so the rule is not simply refusing everything
 *
 * A ProfileDocument holds each property's JSON text separately, so a case changes exactly the
 * property it is about and the rest of the document stays the valid fixture
 */

namespace utest
{
    namespace browserprofiles
    {
        /**
         * @brief A browser profile document, one member per property
         *
         * Each member is the JSON text of the property's value; an empty member omits the
         * property. The defaults are one valid profile, which every case starts from
         */

        class ProfileDocument
        {
        public:

            std::string id = R"json("test-profile")json";
            std::string family = R"json("TestFamily")json";
            std::string grade = R"json("Ja4Candidate")json";
            std::string deviations = R"json([ "first deviation", "second deviation" ])json";

            bool hasTls = true;
            std::string cipherSuitesTls12 =
                R"json([ "ECDHE-ECDSA-AES128-GCM-SHA256", "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256" ])json";
            std::string cipherSuitesTls13 = R"json([ "TLS_AES_128_GCM_SHA256", "TLS_CHACHA20_POLY1305_SHA256" ])json";
            std::string groups =
                R"json([ { "name": "X25519MLKEM768", "keyShare": true }, { "name": "secp256r1", "keyShare": false } ])json";
            std::string signatureAlgorithms = R"json([ "ecdsa_secp256r1_sha256", "rsa_pss_rsae_sha256" ])json";
            std::string alpnProtocols = R"json([ "h2", "http/1.1" ])json";

            bool hasHttp2 = true;
            std::string settings =
                R"json([ { "id": 1, "value": 111 }, { "id": 2, "value": 0 }, { "id": 4, "value": 222 },)json"
                R"json( { "id": 5, "value": 16385 }, { "id": 65535, "value": 333 } ])json";
            std::string connectionWindowUpdateIncrement = "444";
            std::string windowUpdateThreshold = "55";
            std::string idleStreamPriorities =
                R"json([ { "streamId": 3, "streamDependency": 0, "weight": 200, "exclusive": false },)json"
                R"json( { "streamId": 5, "streamDependency": 3, "weight": 100, "exclusive": true } ])json";
            std::string headersPriority =
                R"json({ "isSet": true, "streamDependency": 3, "weight": 255, "exclusive": true })json";
            std::string pseudoHeaderOrder = R"json([ "method", "authority", "scheme", "path" ])json";
            std::string hpackEncoderTableSize = "666";
            std::string hpackIndexingPolicy = R"json("WithoutIndexing")json";

            bool hasHeaders = true;
            std::string navigation =
                R"json({ "defaultHeaders": [)json"
                R"json( { "name": "host", "isComputed": true },)json"
                R"json( { "name": "connection", "value": "keep-alive" },)json"
                R"json( { "name": "sec-ch-ua" },)json"
                R"json( { "name": "sec-ch-ua-mobile", "value": "?0" },)json"
                R"json( { "name": "sec-ch-ua-platform" },)json"
                R"json( { "name": "user-agent" },)json"
                R"json( { "name": "accept", "value": "text/test,*/*;q=0.8" },)json"
                R"json( { "name": "sec-fetch-site", "value": "none", "isComputed": true },)json"
                R"json( { "name": "accept-encoding" },)json"
                R"json( { "name": "accept-language", "value": "en-US,en;q=0.9" },)json"
                R"json( { "name": "priority", "value": "u=0, i" } ],)json"
                R"json( "callerHeaderPlacement": "BeforeAnchor", "callerHeaderAnchor": "accept-encoding",)json"
                R"json( "http1CaseMap": { "host": "Host", "connection": "Connection", "user-agent": "User-Agent" },)json"
                R"json( "priorityHeaderValue": "u=0, i" })json";
            std::string fetch =
                R"json({ "defaultHeaders": [)json"
                R"json( { "name": "sec-ch-ua-platform" },)json"
                R"json( { "name": "user-agent" },)json"
                R"json( { "name": "sec-ch-ua" },)json"
                R"json( { "name": "accept", "value": "*/*" } ],)json"
                R"json( "callerHeaderPlacement": "Appended", "http1CaseMap": {}, "priorityHeaderValue": "u=1, i" })json";
            std::string subresource =
                R"json({ "defaultHeaders": [ { "name": "user-agent" }, { "name": "accept", "value": "image/test" } ],)json"
                R"json( "callerHeaderPlacement": "Prepended", "priorityHeaderValue": "u=2" })json";
            std::string acceptEncoding = R"json([ "gzip", "br" ])json";
            std::string acceptLanguageQValues = R"json([ "0.9", "0.8" ])json";

            std::string userAgent = R"json("test-user-agent/1.0 (synthetic)")json";
            std::string secChUaBrands =
                R"json([ { "brand": "Not)A;Brand", "version": "8" }, { "brand": "TestBrowser", "version": "1" } ])json";
            std::string platform = R"json("TestPlatform")json";

            /*
             * Appended verbatim as the last top-level properties, for a case about a property the
             * data model does not know
             */

            std::string extraTopLevel;

            std::string json() const
            {
                std::string tls = "{";

                append( tls, "cipherSuitesTls12", cipherSuitesTls12 );
                append( tls, "cipherSuitesTls13", cipherSuitesTls13 );
                append( tls, "groups", groups );
                append( tls, "signatureAlgorithms", signatureAlgorithms );
                append( tls, "alpnProtocols", alpnProtocols );
                append( tls, "sessionTicket", "true" );
                append( tls, "statusRequest", "true" );
                append( tls, "signedCertificateTimestamp", "false" );
                append( tls, "padding", "true" );

                tls += " }";

                std::string http2 = "{";

                append( http2, "settings", settings );
                append( http2, "connectionWindowUpdateIncrement", connectionWindowUpdateIncrement );
                append( http2, "windowUpdateThreshold", windowUpdateThreshold );
                append( http2, "idleStreamPriorities", idleStreamPriorities );
                append( http2, "headersPriority", headersPriority );
                append( http2, "pseudoHeaderOrder", pseudoHeaderOrder );
                append( http2, "hpackEncoderTableSize", hpackEncoderTableSize );
                append( http2, "hpackIndexingPolicy", hpackIndexingPolicy );
                append( http2, "cookieCrumbling", "true" );

                http2 += " }";

                std::string headers = "{";

                append( headers, "navigation", navigation );
                append( headers, "fetch", fetch );
                append( headers, "subresource", subresource );
                append( headers, "acceptEncoding", acceptEncoding );
                append( headers, "acceptLanguageQValues", acceptLanguageQValues );

                headers += " }";

                std::string document = "{";

                append( document, "id", id );
                append( document, "family", family );
                append( document, "grade", grade );
                append( document, "deviations", deviations );
                append( document, "tls", hasTls ? tls : std::string() );
                append( document, "http2", hasHttp2 ? http2 : std::string() );
                append( document, "headers", hasHeaders ? headers : std::string() );
                append( document, "userAgent", userAgent );
                append( document, "secChUaBrands", secChUaBrands );
                append( document, "platform", platform );

                if( ! extraTopLevel.empty() )
                {
                    document += ", ";
                    document += extraTopLevel;
                }

                document += " }";

                return document;
            }

        private:

            static void append(
                SAA_inout       std::string&                                    object,
                SAA_in          const std::string&                              name,
                SAA_in          const std::string&                              value
                )
            {
                if( value.empty() )
                {
                    return;
                }

                if( object.size() > 1U )
                {
                    object += ",";
                }

                object += " \"";
                object += name;
                object += "\": ";
                object += value;
            }
        };

        /**
         * @brief The text with its one occurrence of 'from' replaced by 'to' - and a failure if
         * 'from' does not occur exactly once, so that a case never edits a place it did not mean to
         */

        inline std::string replaced(
            SAA_in              const std::string&                              text,
            SAA_in              const std::string&                              from,
            SAA_in              const std::string&                              to
            )
        {
            const auto pos = text.find( from );

            UTF_REQUIRE( pos != std::string::npos );
            UTF_REQUIRE( text.find( from, pos + 1U ) == std::string::npos );

            auto result = text;

            result.replace( pos, from.size(), to );

            return result;
        }

        /**
         * @brief A JSON array of 'count' elements, element i rendered by 'render( i )'
         */

        template
        <
            typename RENDER
        >
        std::string jsonArray(
            SAA_in              const std::size_t                               count,
            SAA_in              const RENDER&                                   render
            )
        {
            std::string result = "[";

            for( std::size_t i = 0U; i < count; ++i )
            {
                result += 0U == i ? " " : ", ";
                result += render( i );
            }

            result += " ]";

            return result;
        }

        inline std::string quoted( SAA_in const std::string& text )
        {
            return "\"" + text + "\"";
        }

        inline std::string numbered( SAA_in const std::string& prefix, SAA_in const std::size_t i )
        {
            return prefix + std::to_string( i + 1U );
        }

        /**
         * @brief A JSON array of 'count' distinct strings, prefix1, prefix2, ...
         */

        inline std::string stringArray( SAA_in const std::string& prefix, SAA_in const std::size_t count )
        {
            return jsonArray(
                count,
                [ &prefix ]( SAA_in const std::size_t i ) -> std::string
                {
                    return quoted( numbered( prefix, i ) );
                }
                );
        }

        inline bl::httpclient::BrowserProfile requireLoads( SAA_in const ProfileDocument& document )
        {
            return bl::httpclient::BrowserProfiles::load( document.json() );
        }

        /**
         * @brief The document is refused, and the message names the property and the rule
         */

        inline void requireRefused(
            SAA_in              const std::string&                              json,
            SAA_in              const std::string&                              expected
            )
        {
            UTF_REQUIRE_THROW_MESSAGE(
                bl::httpclient::BrowserProfiles::load( json ),
                bl::InvalidDataFormatException,
                "Invalid browser profile: " + expected
                );
        }

        inline void requireRefused(
            SAA_in              const ProfileDocument&                          document,
            SAA_in              const std::string&                              expected
            )
        {
            requireRefused( document.json(), expected );
        }

        inline const bl::httpclient::HeaderProfileForKind& kindOf(
            SAA_in              const bl::httpclient::BrowserProfile&           profile,
            SAA_in              const bl::httpclient::HttpRequestKind           kind
            )
        {
            const auto pos = profile.headers.byRequestKind.find( kind );

            UTF_REQUIRE( pos != profile.headers.byRequestKind.end() );

            return pos -> second;
        }

        inline std::string headerNames( SAA_in const bl::httpclient::HeaderProfileForKind& kind )
        {
            std::string result;

            for( std::size_t i = 0U; i < kind.defaultHeaders.size(); ++i )
            {
                result += 0U == i ? "" : " ";
                result += kind.defaultHeaders[ i ].name;
            }

            return result;
        }

    } // browserprofiles

} // utest

UTF_AUTO_TEST_CASE( TlsNameRules_PlainNameRuleIsTheBuildersRuleTests )
{
    using namespace bl;

    /*
     * The shared rule must be the context builder's own, exactly, so that the builder can move to
     * it without refusing or admitting anything it did not before. So each name's verdict is
     * pinned, and pinned against CryptoBase::isCipherSuiteNameSafe( ) as well - the first is what
     * makes this case fail if both rules were wrong the same way
     */

    const std::vector< std::pair< std::string, bool > > names =
    {
        { "", false },
        { "@SECLEVEL=0", false },
        { "DEFAULT:@SECLEVEL=0", false },
        { "ECDHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0", false },
        { "ALL,@SECLEVEL=0", false },
        { "ALL @STRENGTH", false },
        { "!aNULL", false },
        { "-ALL", false },
        { "+RC4", false },
        { "ECDHE_RSA/AES128", false },
        { "_leading-underscore", false },
        { "*X25519", false },                           /* OpenSSL 3.5's key share mark */
        { "?X25519", false },                           /* its ignore-if-unknown mark */
        { "X25519/secp256r1", false },                  /* its tuple separator */
        { "RSA+SHA256", false },                        /* an algorithm pair */
        { "TLSv1.2", false },
        { "line\nbreak", false },
        { "carriage\rreturn", false },
        { std::string( "nul\0inside", 10U ), false },
        { "caf\xC3\xA9", false },
        { "x", true },
        { "0x0a0a", true },
        { "trailing-", true },
        { "ECDHE-RSA-AES128-GCM-SHA256", true },
        { "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256", true },
        { "TLS_AES_128_GCM_SHA256", true },
        { "AES128-SHA", true },
        { "X25519MLKEM768", true },
        { "P-256", true },
        { "secp256r1", true },
        { "rsa_pss_rsae_sha256", true },
        { "ed25519", true },
    };

    for( const auto& entry : names )
    {
        const auto& name = entry.first;

        UTF_REQUIRE_EQUAL( crypto::TlsNameRules::isNameSafe( name ), entry.second );
        UTF_REQUIRE_EQUAL( crypto::TlsNameRules::isNameSafe( name ), crypto::CryptoBase::isCipherSuiteNameSafe( name ) );

        /*
         * Groups and signature algorithms are judged by the same rule and nothing more
         */

        UTF_REQUIRE_EQUAL( crypto::TlsNameRules::isGroupNameAllowed( name ), entry.second );
        UTF_REQUIRE_EQUAL( crypto::TlsNameRules::isSignatureAlgorithmNameAllowed( name ), entry.second );
    }
}

UTF_AUTO_TEST_CASE( TlsNameRules_AnonymousAndNullSuitesAreRefusedTests )
{
    using namespace bl;

    typedef crypto::TlsNameRules rules;

    /*
     * Anonymous, in both spellings, and the OpenSSL aliases for the class; case does not matter
     */

    const std::vector< std::string > anonymous =
    {
        "ADH-AES128-GCM-SHA256",
        "AECDH-AES256-SHA",
        "TLS_DH_anon_WITH_AES_128_GCM_SHA256",
        "TLS_ECDH_anon_WITH_AES_256_CBC_SHA",
        "ADH",
        "AECDH",
        "aNULL",
        "adh-aes128-gcm-sha256",
    };

    for( const auto& name : anonymous )
    {
        UTF_REQUIRE( rules::isNameSafe( name ) );
        UTF_REQUIRE( rules::isAnonymousCipherSuiteName( name ) );
        UTF_REQUIRE( ! rules::isNullCipherSuiteName( name ) );
        UTF_REQUIRE( ! rules::isCipherSuiteNameAllowed( name ) );
    }

    /*
     * NULL, in both spellings, the aliases, and the TLS 1.3 integrity-only suites of RFC 9150,
     * whose names carry no NULL at all
     */

    const std::vector< std::string > null =
    {
        "NULL-SHA256",
        "ECDHE-RSA-NULL-SHA",
        "TLS_RSA_WITH_NULL_SHA256",
        "TLS_ECDHE_ECDSA_WITH_NULL_SHA",
        "NULL",
        "eNULL",
        "TLS_SHA256_SHA256",
        "TLS_SHA384_SHA384",
        "tls_sha256_sha256",
    };

    for( const auto& name : null )
    {
        UTF_REQUIRE( rules::isNameSafe( name ) );
        UTF_REQUIRE( rules::isNullCipherSuiteName( name ) );
        UTF_REQUIRE( ! rules::isAnonymousCipherSuiteName( name ) );
        UTF_REQUIRE( ! rules::isCipherSuiteNameAllowed( name ) );
    }

    UTF_REQUIRE( rules::isAnonymousCipherSuiteName( "AECDH-NULL-SHA" ) );
    UTF_REQUIRE( rules::isNullCipherSuiteName( "AECDH-NULL-SHA" ) );

    /*
     * And the boundary: authenticated, encrypting suites pass - including PSK, which is not
     * anonymous - and the rule reads components, not substrings or shapes loosely: NULLX and
     * ANONYMOUS are not components it refuses, and TLS_SHA256 or TLS_SHA256_SHA256_X is not the
     * integrity-only shape
     */

    const std::vector< std::string > allowed =
    {
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "DHE-RSA-AES256-GCM-SHA384",
        "PSK-AES128-GCM-SHA256",
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_128_CCM_8_SHA256",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
        "NULLX-SHA",
        "ANONYMOUS-SHA",
        "TLS_SHA256",
        "TLS_SHA256_SHA256_X",
        "TLS_SHA_SHA256",
    };

    for( const auto& name : allowed )
    {
        UTF_REQUIRE( ! rules::isAnonymousCipherSuiteName( name ) );
        UTF_REQUIRE( ! rules::isNullCipherSuiteName( name ) );
        UTF_REQUIRE( rules::isCipherSuiteNameAllowed( name ) );
    }

    UTF_REQUIRE( ! rules::isCipherSuiteNameAllowed( "@SECLEVEL=0" ) );
}

UTF_AUTO_TEST_CASE( TlsNameRules_AgreesWithTheLinkedOpenSslTests )
{
    using namespace bl;

    typedef crypto::TlsNameRules rules;

    /*
     * The rule reads names; OpenSSL knows what each suite is. Every suite the linked OpenSSL
     * knows - found by id, all 65536 of them, so that no list of names is assumed - must be
     * judged anonymous exactly when OpenSSL says it authenticates nobody, and NULL exactly when it
     * says it encrypts nothing, in both of its spellings
     *
     * NID_auth_null is SSL_aNULL and NID_undef the cipher of SSL_eNULL (ssl/ssl_ciph.c's tables).
     * The two signalling values - TLS_EMPTY_RENEGOTIATION_INFO_SCSV, 0x00FF (RFC 5746 3.3), and
     * TLS_FALLBACK_SCSV, 0x5600 (RFC 7507 6) - are found by id too and are not suites, so they are
     * set aside by those code points. Not by their key exchange: an SCSV's is 0, which is also
     * SSL_kANY (ssl/ssl_local.h), so it reports NID_kx_any exactly as a TLS 1.3 suite does
     */

    std::unique_ptr< ::SSL_CTX, void ( * )( ::SSL_CTX* ) > context(
        ::SSL_CTX_new( ::TLS_client_method() ),
        []( ::SSL_CTX* ctx ) { ::SSL_CTX_free( ctx ); }
        );

    UTF_REQUIRE( context );

    std::unique_ptr< ::SSL, void ( * )( ::SSL* ) > ssl(
        ::SSL_new( context.get() ),
        []( ::SSL* s ) { ::SSL_free( s ); }
        );

    UTF_REQUIRE( ssl );

    std::size_t suites = 0U;
    std::size_t signalling = 0U;
    std::size_t anonymousSuites = 0U;
    std::size_t nullSuites = 0U;

    for( unsigned int id = 0U; id <= 0xFFFFU; ++id )
    {
        const unsigned char bytes[ 2 ] =
        {
            static_cast< unsigned char >( id >> 8 ),
            static_cast< unsigned char >( id & 0xFFU ),
        };

        const ::SSL_CIPHER* const cipher = ::SSL_CIPHER_find( ssl.get(), bytes );

        if( nullptr == cipher )
        {
            continue;
        }

        const std::string name = ::SSL_CIPHER_get_name( cipher );

        if( 0x00FFU == id || 0x5600U == id )
        {
            UTF_REQUIRE( name.size() > 5U && 0 == name.compare( name.size() - 5U, 5U, "_SCSV" ) );

            ++signalling;

            continue;
        }

        const bool isAnonymous = NID_auth_null == ::SSL_CIPHER_get_auth_nid( cipher );
        const bool isNull = NID_undef == ::SSL_CIPHER_get_cipher_nid( cipher );

        std::vector< std::string > spellings( 1U, name );

        const char* const standardName = ::SSL_CIPHER_standard_name( cipher );

        if( nullptr != standardName )
        {
            spellings.push_back( standardName );
        }

        for( const auto& spelling : spellings )
        {
            const bool isAgreed =
                rules::isNameSafe( spelling ) &&
                rules::isAnonymousCipherSuiteName( spelling ) == isAnonymous &&
                rules::isNullCipherSuiteName( spelling ) == isNull &&
                rules::isCipherSuiteNameAllowed( spelling ) == ( ! isAnonymous && ! isNull );

            if( ! isAgreed )
            {
                UTF_FAIL(
                    BL_MSG()
                        << "The name rule and OpenSSL disagree about suite "
                        << id
                        << " '"
                        << spelling
                        << "': OpenSSL says anonymous="
                        << isAnonymous
                        << " null="
                        << isNull
                        << ", the rule says safe="
                        << rules::isNameSafe( spelling )
                        << " anonymous="
                        << rules::isAnonymousCipherSuiteName( spelling )
                        << " null="
                        << rules::isNullCipherSuiteName( spelling )
                    );
            }
        }

        ++suites;

        anonymousSuites += isAnonymous ? 1U : 0U;
        nullSuites += isNull ? 1U : 0U;
    }

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "The linked OpenSSL knows "
            << suites
            << " suites, of which "
            << anonymousSuites
            << " are anonymous and "
            << nullSuites
            << " NULL, and "
            << signalling
            << " signalling values"
        );

    /*
     * Not vacuous: a build which knew no anonymous or no NULL suite would prove nothing above
     */

    UTF_REQUIRE( suites >= 100U );
    UTF_REQUIRE( anonymousSuites >= 1U );
    UTF_REQUIRE( nullSuites >= 1U );
    UTF_REQUIRE_EQUAL( signalling, 2U );
}

UTF_AUTO_TEST_CASE( BrowserProfiles_ValidProfileLoadsFieldByFieldTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    const auto profile = requireLoads( document );

    UTF_REQUIRE_EQUAL( profile.id, "test-profile" );
    UTF_REQUIRE_EQUAL( profile.family, "TestFamily" );
    UTF_REQUIRE( profile.grade.value() == httpclient::BrowserProfileGrade::Ja4Candidate );
    UTF_REQUIRE_EQUAL( profile.deviations.size(), 2U );
    UTF_REQUIRE_EQUAL( profile.deviations[ 1 ], "second deviation" );

    {
        const auto& tls = profile.tls;

        UTF_REQUIRE_EQUAL( tls.cipherSuitesTls12.size(), 2U );
        UTF_REQUIRE_EQUAL( tls.cipherSuitesTls12[ 0 ], "ECDHE-ECDSA-AES128-GCM-SHA256" );
        UTF_REQUIRE_EQUAL( tls.cipherSuitesTls12[ 1 ], "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256" );
        UTF_REQUIRE_EQUAL( tls.cipherSuitesTls13.size(), 2U );
        UTF_REQUIRE_EQUAL( tls.cipherSuitesTls13[ 1 ], "TLS_CHACHA20_POLY1305_SHA256" );

        UTF_REQUIRE_EQUAL( tls.groups.size(), 2U );
        UTF_REQUIRE_EQUAL( tls.groups[ 0 ].name, "X25519MLKEM768" );
        UTF_REQUIRE_EQUAL( tls.groups[ 0 ].keyShare.value(), true );
        UTF_REQUIRE_EQUAL( tls.groups[ 1 ].name, "secp256r1" );
        UTF_REQUIRE_EQUAL( tls.groups[ 1 ].keyShare.value(), false );

        UTF_REQUIRE_EQUAL( tls.signatureAlgorithms.size(), 2U );
        UTF_REQUIRE_EQUAL( tls.signatureAlgorithms[ 1 ], "rsa_pss_rsae_sha256" );
        UTF_REQUIRE_EQUAL( tls.alpnProtocols.size(), 2U );
        UTF_REQUIRE_EQUAL( tls.alpnProtocols[ 0 ], "h2" );
        UTF_REQUIRE_EQUAL( tls.alpnProtocols[ 1 ], "http/1.1" );

        UTF_REQUIRE_EQUAL( tls.sessionTicket.value(), true );
        UTF_REQUIRE_EQUAL( tls.statusRequest.value(), true );
        UTF_REQUIRE_EQUAL( tls.signedCertificateTimestamp.value(), false );
        UTF_REQUIRE_EQUAL( tls.padding.value(), true );
    }

    {
        const auto& h2 = profile.http2;

        /*
         * In the document's order, with the id the library does not interpret passed through
         */

        UTF_REQUIRE_EQUAL( h2.settings.size(), 5U );
        UTF_REQUIRE_EQUAL( h2.settings[ 0 ].id.value(), 1U );
        UTF_REQUIRE_EQUAL( h2.settings[ 0 ].value.value(), 111U );
        UTF_REQUIRE_EQUAL( h2.settings[ 1 ].id.value(), 2U );
        UTF_REQUIRE_EQUAL( h2.settings[ 1 ].value.value(), 0U );
        UTF_REQUIRE_EQUAL( h2.settings[ 3 ].id.value(), 5U );
        UTF_REQUIRE_EQUAL( h2.settings[ 3 ].value.value(), 16385U );
        UTF_REQUIRE_EQUAL( h2.settings[ 4 ].id.value(), 65535U );
        UTF_REQUIRE_EQUAL( h2.settings[ 4 ].value.value(), 333U );

        UTF_REQUIRE_EQUAL( h2.connectionWindowUpdateIncrement.value(), 444U );
        UTF_REQUIRE_EQUAL( h2.windowUpdateThreshold.value(), 55U );

        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities.size(), 2U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 0 ].streamId.value(), 3U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 0 ].streamDependency.value(), 0U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 0 ].weight.value(), 200U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 0 ].exclusive.value(), false );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 1 ].streamId.value(), 5U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 1 ].streamDependency.value(), 3U );
        UTF_REQUIRE_EQUAL( h2.idleStreamPriorities[ 1 ].exclusive.value(), true );

        UTF_REQUIRE_EQUAL( h2.headersPriority.isSet.value(), true );
        UTF_REQUIRE_EQUAL( h2.headersPriority.streamDependency.value(), 3U );
        UTF_REQUIRE_EQUAL( h2.headersPriority.weight.value(), 255U );
        UTF_REQUIRE_EQUAL( h2.headersPriority.exclusive.value(), true );

        UTF_REQUIRE_EQUAL( h2.pseudoHeaderOrder.size(), 4U );
        UTF_REQUIRE( h2.pseudoHeaderOrder[ 0 ] == http2::Http2PseudoHeader::Method );
        UTF_REQUIRE( h2.pseudoHeaderOrder[ 1 ] == http2::Http2PseudoHeader::Authority );
        UTF_REQUIRE( h2.pseudoHeaderOrder[ 2 ] == http2::Http2PseudoHeader::Scheme );
        UTF_REQUIRE( h2.pseudoHeaderOrder[ 3 ] == http2::Http2PseudoHeader::Path );

        UTF_REQUIRE_EQUAL( h2.hpackEncoderTableSize.value(), 666U );
        UTF_REQUIRE( h2.hpackIndexingPolicy.value() == http2::HpackIndexingPolicy::WithoutIndexing );
        UTF_REQUIRE_EQUAL( h2.cookieCrumbling.value(), true );
    }

    {
        const auto& headers = profile.headers;

        UTF_REQUIRE_EQUAL( headers.byRequestKind.size(), 3U );

        const auto& navigation = kindOf( profile, httpclient::HttpRequestKind::Navigation );

        UTF_REQUIRE_EQUAL(
            headerNames( navigation ),
            "host connection sec-ch-ua sec-ch-ua-mobile sec-ch-ua-platform user-agent accept "
            "sec-fetch-site accept-encoding accept-language priority"
            );

        /*
         * host is a marker whose value each request supplies; accept-encoding the session
         * computes; sec-fetch-site keeps its default beside its mark
         */

        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].value, "" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].isComputed.value(), true );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 1 ].value, "keep-alive" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 1 ].isComputed.value(), false );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 3 ].value, "?0" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 6 ].value, "text/test,*/*;q=0.8" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 7 ].value, "none" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 7 ].isComputed.value(), true );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 8 ].value, "" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 9 ].value, "en-US,en;q=0.9" );
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 10 ].value, "u=0, i" );

        UTF_REQUIRE( navigation.callerHeaderPlacement.value() == httpclient::CallerHeaderPlacement::BeforeAnchor );
        UTF_REQUIRE_EQUAL( navigation.callerHeaderAnchor, "accept-encoding" );
        UTF_REQUIRE_EQUAL( navigation.http1CaseMap.size(), 3U );
        UTF_REQUIRE_EQUAL( navigation.http1CaseMap.at( "user-agent" ), "User-Agent" );
        UTF_REQUIRE_EQUAL( navigation.priorityHeaderValue, "u=0, i" );

        const auto& fetch = kindOf( profile, httpclient::HttpRequestKind::Fetch );

        UTF_REQUIRE_EQUAL( headerNames( fetch ), "sec-ch-ua-platform user-agent sec-ch-ua accept" );
        UTF_REQUIRE( fetch.callerHeaderPlacement.value() == httpclient::CallerHeaderPlacement::Appended );
        UTF_REQUIRE( fetch.callerHeaderAnchor.empty() );
        UTF_REQUIRE( fetch.http1CaseMap.empty() );
        UTF_REQUIRE_EQUAL( fetch.priorityHeaderValue, "u=1, i" );

        const auto& subresource = kindOf( profile, httpclient::HttpRequestKind::Subresource );

        UTF_REQUIRE_EQUAL( headerNames( subresource ), "user-agent accept" );
        UTF_REQUIRE( subresource.callerHeaderPlacement.value() == httpclient::CallerHeaderPlacement::Prepended );
        UTF_REQUIRE_EQUAL( subresource.priorityHeaderValue, "u=2" );

        UTF_REQUIRE_EQUAL( headers.acceptEncoding.size(), 2U );
        UTF_REQUIRE_EQUAL( headers.acceptEncoding[ 1 ], "br" );
        UTF_REQUIRE_EQUAL( headers.acceptLanguageQValues.size(), 2U );
        UTF_REQUIRE_EQUAL( headers.acceptLanguageQValues[ 0 ], "0.9" );
    }

    /*
     * An absent enumerated property takes the shape type's default, and an empty list is a valid
     * list - for the suites it means "the library default" (CryptoBase.h)
     */

    auto defaults = document;

    defaults.hpackIndexingPolicy.clear();
    defaults.cipherSuitesTls12 = "[]";
    defaults.cipherSuitesTls13 = "[]";
    defaults.groups.clear();
    defaults.pseudoHeaderOrder = "[]";
    defaults.headersPriority.clear();
    defaults.fetch = replaced( defaults.fetch, R"json("callerHeaderPlacement": "Appended", )json", "" );

    const auto defaulted = requireLoads( defaults );

    UTF_REQUIRE( defaulted.http2.hpackIndexingPolicy.value() == http2::HpackIndexingPolicy::Incremental );
    UTF_REQUIRE( defaulted.tls.cipherSuitesTls12.empty() );
    UTF_REQUIRE( defaulted.tls.groups.empty() );
    UTF_REQUIRE( defaulted.http2.pseudoHeaderOrder.empty() );
    UTF_REQUIRE_EQUAL( defaulted.http2.headersPriority.isSet.value(), false );
    UTF_REQUIRE(
        kindOf( defaulted, httpclient::HttpRequestKind::Fetch ).callerHeaderPlacement.value() ==
            httpclient::CallerHeaderPlacement::Appended
        );

    auto approximate = document;

    approximate.grade = R"json("Approximate")json";

    UTF_REQUIRE( requireLoads( approximate ).grade.value() == httpclient::BrowserProfileGrade::Approximate );
}

UTF_AUTO_TEST_CASE( BrowserProfiles_VersionStringsComposeByteExactTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    const auto profile = requireLoads( document );

    /*
     * Each kind gets the composed values at the positions its own list names them, and nowhere
     * else. sec-ch-ua is RFC 8941's list: each brand a string, its version the 'v' parameter, in
     * the order given, joined by ", " - the greased brand spelled exactly as given
     */

    const std::string secChUa = R"("Not)A;Brand";v="8", "TestBrowser";v="1")";
    const std::string userAgent = "test-user-agent/1.0 (synthetic)";
    const std::string platform = R"("TestPlatform")";

    const auto& navigation = kindOf( profile, httpclient::HttpRequestKind::Navigation );

    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 2 ].value, secChUa );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 4 ].value, platform );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 5 ].value, userAgent );

    for( std::size_t i : { 2U, 4U, 5U } )
    {
        UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ i ].isComputed.value(), false );
    }

    const auto& fetch = kindOf( profile, httpclient::HttpRequestKind::Fetch );

    UTF_REQUIRE_EQUAL( fetch.defaultHeaders[ 0 ].value, platform );
    UTF_REQUIRE_EQUAL( fetch.defaultHeaders[ 1 ].value, userAgent );
    UTF_REQUIRE_EQUAL( fetch.defaultHeaders[ 2 ].value, secChUa );

    const auto& subresource = kindOf( profile, httpclient::HttpRequestKind::Subresource );

    UTF_REQUIRE_EQUAL( subresource.defaultHeaders.size(), 2U );
    UTF_REQUIRE_EQUAL( subresource.defaultHeaders[ 0 ].value, userAgent );

    /*
     * The two characters RFC 8941 escapes, a leading space as an old greased brand had, and a
     * brand with no version, which is sent without the parameter
     */

    auto escaped = document;

    escaped.secChUaBrands =
        R"json([ { "brand": "\\Not;A\"Brand", "version": "99" }, { "brand": " Not A;Brand", "version": "v\"1" },)json"
        R"json( { "brand": "Unversioned" } ])json";

    UTF_REQUIRE_EQUAL(
        kindOf( requireLoads( escaped ), httpclient::HttpRequestKind::Navigation ).defaultHeaders[ 2 ].value,
        R"("\\Not;A\"Brand";v="99", " Not A;Brand";v="v\"1", "Unversioned")"
        );

    auto quotedPlatform = document;

    quotedPlatform.platform = R"json("Test \"Platform\\")json";

    UTF_REQUIRE_EQUAL(
        kindOf( requireLoads( quotedPlatform ), httpclient::HttpRequestKind::Fetch ).defaultHeaders[ 0 ].value,
        R"("Test \"Platform\\")"
        );

    /*
     * A REFRESH IS A JSON EDIT. Changing the three version-string properties, and nothing else,
     * changes exactly the values composed from them: every list keeps its names, its order and
     * every other value, and the TLS and HTTP/2 shapes are untouched
     */

    auto refreshed = document;

    refreshed.userAgent = R"json("test-user-agent/2.0 (refreshed)")json";
    refreshed.secChUaBrands = R"json([ { "brand": "TestBrowser", "version": "2" }, { "brand": "Not.A/Brand", "version": "24" } ])json";
    refreshed.platform = R"json("OtherPlatform")json";

    const auto after = requireLoads( refreshed );

    const std::vector< std::pair< std::string, std::string > > composed =
    {
        { "user-agent", "test-user-agent/2.0 (refreshed)" },
        { "sec-ch-ua", R"("TestBrowser";v="2", "Not.A/Brand";v="24")" },
        { "sec-ch-ua-platform", R"("OtherPlatform")" },
    };

    for( const auto kind :
        {
            httpclient::HttpRequestKind::Navigation,
            httpclient::HttpRequestKind::Fetch,
            httpclient::HttpRequestKind::Subresource,
        } )
    {
        const auto& before = kindOf( profile, kind ).defaultHeaders;
        const auto& now = kindOf( after, kind ).defaultHeaders;

        UTF_REQUIRE_EQUAL( now.size(), before.size() );

        for( std::size_t i = 0U; i < now.size(); ++i )
        {
            UTF_REQUIRE_EQUAL( now[ i ].name, before[ i ].name );
            UTF_REQUIRE_EQUAL( now[ i ].isComputed.value(), before[ i ].isComputed.value() );

            bool isVersionString = false;

            for( const auto& entry : composed )
            {
                if( entry.first == now[ i ].name )
                {
                    UTF_REQUIRE_EQUAL( now[ i ].value, entry.second );

                    isVersionString = true;
                }
            }

            if( ! isVersionString )
            {
                UTF_REQUIRE_EQUAL( now[ i ].value, before[ i ].value );
            }
        }
    }

    UTF_REQUIRE( after.tls.cipherSuitesTls12 == profile.tls.cipherSuitesTls12 );
    UTF_REQUIRE_EQUAL( after.http2.settings.size(), profile.http2.settings.size() );
    UTF_REQUIRE_EQUAL( after.http2.connectionWindowUpdateIncrement.value(), 444U );
}

UTF_AUTO_TEST_CASE( BrowserProfiles_VersionStringsAreValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    /*
     * A list names a version-string header with no value: a literal there would survive a refresh,
     * and a mark of isComputed would say the session computes it, which it does not
     */

    {
        auto withLiteral = document;

        withLiteral.subresource = replaced(
            withLiteral.subresource,
            R"json({ "name": "user-agent" })json",
            R"json({ "name": "user-agent", "value": "stale-agent/0.1" })json"
            );

        requireRefused(
            withLiteral,
            "headers.subresource.defaultHeaders[0] is user-agent, whose value is composed from "
            "userAgent: it must carry no value and not be isComputed"
            );

        auto computed = document;

        computed.fetch = replaced(
            computed.fetch,
            R"json({ "name": "sec-ch-ua" })json",
            R"json({ "name": "sec-ch-ua", "isComputed": true })json"
            );

        requireRefused(
            computed,
            "headers.fetch.defaultHeaders[2] is sec-ch-ua, whose value is composed from "
            "secChUaBrands: it must carry no value and not be isComputed"
            );
    }

    /*
     * A header which names a version string needs the string
     */

    {
        auto noUserAgent = document;

        noUserAgent.userAgent.clear();

        requireRefused( noUserAgent, "headers.navigation.defaultHeaders[5] is user-agent but userAgent is empty" );

        auto noBrands = document;

        noBrands.secChUaBrands = "[]";

        requireRefused( noBrands, "headers.navigation.defaultHeaders[2] is sec-ch-ua but secChUaBrands is empty" );

        auto noPlatform = document;

        noPlatform.platform.clear();

        requireRefused(
            noPlatform,
            "headers.navigation.defaultHeaders[4] is sec-ch-ua-platform but platform is empty"
            );
    }

    /*
     * userAgent is a field value; the brands and the platform are RFC 8941 strings, printable
     * ASCII - the two characters that section escapes are the boundary, accepted above
     */

    {
        auto badAgent = document;

        badAgent.userAgent = R"json("test-agent\r\nx-injected: 1")json";
        requireRefused( badAgent, "userAgent is not a valid field value" );

        badAgent.userAgent = R"json(" leading-space")json";
        requireRefused( badAgent, "userAgent is not a valid field value" );

        auto badBrand = document;

        /*
         * An empty brand never reaches the loader's rule: brand is a required property of the
         * data model, which counts an empty string as not provided
         */

        badBrand.secChUaBrands = R"json([ { "brand": "", "version": "1" } ])json";
        requireRefused(
            badBrand,
            "document is not a browser profile of the expected shape - Required property 'brand'"
            );

        badBrand.secChUaBrands = R"json([ { "brand": "Test", "version": "1" }, { "brand": "Bell\u0007", "version": "1" } ])json";
        requireRefused( badBrand, "secChUaBrands[1].brand carries a character outside printable ASCII" );

        badBrand.secChUaBrands = R"json([ { "brand": "Caf\u00e9", "version": "1" } ])json";
        requireRefused( badBrand, "secChUaBrands[0].brand carries a character outside printable ASCII" );

        badBrand.secChUaBrands = R"json([ { "brand": "Test", "version": "1\n" } ])json";
        requireRefused( badBrand, "secChUaBrands[0].version carries a character outside printable ASCII" );

        /*
         * A brand once: sec-ch-ua never names one twice, whatever the versions
         */

        badBrand.secChUaBrands = R"json([ { "brand": "Test", "version": "1" }, { "brand": "Test", "version": "2" } ])json";
        requireRefused( badBrand, "secChUaBrands[1].brand repeats an earlier brand" );

        auto badPlatform = document;

        badPlatform.platform = R"json("Test\tPlatform")json";
        requireRefused( badPlatform, "platform carries a character outside printable ASCII" );
    }

    /*
     * sec-ch-ua-mobile is a literal, and a structured-field boolean
     */

    {
        auto mobile = document;

        mobile.navigation = replaced( mobile.navigation, R"json("value": "?0")json", R"json("value": "?1")json" );

        UTF_REQUIRE_EQUAL(
            kindOf( requireLoads( mobile ), httpclient::HttpRequestKind::Navigation ).defaultHeaders[ 3 ].value,
            "?1"
            );

        mobile.navigation = replaced( mobile.navigation, R"json("value": "?1")json", R"json("value": "?2")json" );

        requireRefused( mobile, "headers.navigation.defaultHeaders[3].value is neither ?0 nor ?1" );
    }

    /*
     * The brand list is bounded
     */

    {
        const auto brands = []( SAA_in const std::size_t count ) -> std::string
        {
            return jsonArray(
                count,
                []( SAA_in const std::size_t i ) -> std::string
                {
                    return R"json({ "brand": ")json" + numbered( "Brand", i ) + R"json(", "version": "1" })json";
                }
                );
        };

        auto bounded = document;

        bounded.secChUaBrands = brands( httpclient::BrowserProfiles::MAX_SEC_CH_UA_BRANDS );
        ( void ) requireLoads( bounded );

        bounded.secChUaBrands = brands( httpclient::BrowserProfiles::MAX_SEC_CH_UA_BRANDS + 1U );
        requireRefused( bounded, "secChUaBrands has more than 8 entries" );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_MalformedDocumentsAreRefusedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    const std::string shape = "document is not a browser profile of the expected shape - ";

    /*
     * Not JSON, not an object, a value of the wrong type or out of the property's range, a
     * required property missing: the data model's refusals, carried as the loader's own exception
     */

    requireRefused( "{ \"id\": ", shape );
    requireRefused( "[]", shape + "The JSON document must be an object at the top level" );

    {
        auto wrongType = document;

        wrongType.id = "5";
        requireRefused( wrongType, shape );

        wrongType = document;
        wrongType.settings = R"json([ { "id": 3000000000, "value": 1 } ])json";
        requireRefused( wrongType, shape );

        wrongType = document;
        wrongType.idleStreamPriorities = R"json([ { "streamId": -1 } ])json";
        requireRefused( wrongType, shape );

        wrongType = document;
        wrongType.idleStreamPriorities = R"json([ { "streamId": 3, "weight": 1.5 } ])json";
        requireRefused( wrongType, shape );

        auto missing = document;

        missing.family.clear();
        requireRefused( missing, shape + "Required property 'family'" );
    }

    /*
     * A property the data model does not know is refused at every level - a misspelled list would
     * otherwise load as an empty one - and its name, which is the document's text, is not echoed
     * as it came: a CR or LF in it is made printable first
     */

    {
        auto unknown = document;

        unknown.extraTopLevel = R"json("cipherSuites": [])json";
        requireRefused( unknown, shape + "Unrecognized property 'cipherSuites'" );

        unknown = document;
        unknown.groups = R"json([ { "name": "X25519", "keyshare": true } ])json";
        requireRefused( unknown, shape + "Unrecognized property 'keyshare'" );

        unknown = document;
        unknown.navigation = replaced(
            unknown.navigation,
            R"json("priorityHeaderValue": "u=0, i")json",
            R"json("priorityHeaderValue": "u=0, i", "priorityHeaderValues": "u=0")json"
            );
        requireRefused( unknown, shape + "Unrecognized property 'priorityHeaderValues'" );

        unknown = document;
        unknown.extraTopLevel = R"json("bad\r\nkey": 1)json";
        requireRefused( unknown, shape + "Unrecognized property 'bad??key'" );
    }

    /*
     * Each of the three shapes, and each of the three request kinds, is required
     */

    {
        auto absent = document;

        absent.hasTls = false;
        requireRefused( absent, "tls is missing" );

        absent = document;
        absent.hasHttp2 = false;
        requireRefused( absent, "http2 is missing" );

        absent = document;
        absent.hasHeaders = false;
        requireRefused( absent, "headers is missing" );

        absent = document;
        absent.fetch.clear();
        requireRefused( absent, "headers.fetch is missing" );
    }

    /*
     * The document's size is bounded before it is parsed; the bound itself is accepted
     */

    {
        const std::size_t bound = httpclient::BrowserProfiles::MAX_DOCUMENT_SIZE;

        auto text = document.json();

        UTF_REQUIRE( text.size() < bound );

        text.append( bound - text.size(), ' ' );

        UTF_REQUIRE_EQUAL( httpclient::BrowserProfiles::load( text ).id, "test-profile" );

        text += ' ';

        requireRefused( text, "document is larger than 262144 bytes" );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_IdentityIsValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    const std::string idRule =
        "id is not 1 to 64 characters of a-z, 0-9, '.', '_' and '-', beginning with a letter or a digit";

    /*
     * The id keys the registry and goes into every ConnectionKey, where the session marks an
     * h2-only key by appending "#h2-only" - so an id may carry no '#'
     */

    {
        auto id = document;

        for( const auto& refused :
            {
                std::string( "test-profile#h2-only" ),
                std::string( "Test-Profile" ),
                std::string( "-test" ),
                std::string( "test profile" ),
                std::string( 65U, 'a' ),
            } )
        {
            id.id = quoted( refused );
            requireRefused( id, idRule );
        }

        for( const auto& accepted : { std::string( "a" ), std::string( "a.b_c-1" ), std::string( 64U, 'a' ) } )
        {
            id.id = quoted( accepted );
            UTF_REQUIRE_EQUAL( requireLoads( id ).id, accepted );
        }
    }

    {
        auto family = document;

        const std::string familyRule =
            "family is not 1 to 64 characters of A-Z, a-z, 0-9, '.', '_' and '-', beginning with a "
            "letter or a digit";

        family.family = R"json("Test Family")json";
        requireRefused( family, familyRule );

        family.family = quoted( std::string( 65U, 'F' ) );
        requireRefused( family, familyRule );

        family.family = quoted( std::string( 64U, 'F' ) );
        ( void ) requireLoads( family );
    }

    {
        auto grade = document;

        grade.grade = R"json("Ja4candidate")json";
        requireRefused( grade, "grade is neither Ja4Candidate nor Approximate" );
    }

    /*
     * Deviations reach the fidelity report and its debug log, so they are printable ASCII, and
     * there is a bounded number of them
     */

    {
        auto deviations = document;

        deviations.deviations = R"json([ "fine", "line\nbreak" ])json";
        requireRefused( deviations, "deviations[1] is empty or carries a character outside printable ASCII" );

        deviations.deviations = R"json([ "" ])json";
        requireRefused( deviations, "deviations[0] is empty or carries a character outside printable ASCII" );

        deviations.deviations = stringArray( "deviation ", httpclient::BrowserProfiles::MAX_DEVIATIONS );
        UTF_REQUIRE_EQUAL( requireLoads( deviations ).deviations.size(), 64U );

        deviations.deviations = stringArray( "deviation ", httpclient::BrowserProfiles::MAX_DEVIATIONS + 1U );
        requireRefused( deviations, "deviations has more than 64 entries" );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_TlsShapeIsValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    /*
     * Both suite lists, by the shared name rule: a plain name, no anonymous suite, no NULL suite,
     * no repeat - each refused where it stands in the list
     */

    {
        auto suites = document;

        suites.cipherSuitesTls12 = R"json([ "ECDHE-ECDSA-AES128-GCM-SHA256", "DEFAULT:@SECLEVEL=0" ])json";
        requireRefused( suites, "tls.cipherSuitesTls12[1] is not a plain suite name" );

        suites.cipherSuitesTls12 = R"json([ "ADH-AES128-GCM-SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls12[0] names an anonymous cipher suite" );

        suites.cipherSuitesTls12 = R"json([ "TLS_RSA_WITH_NULL_SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls12[0] names a NULL cipher suite" );

        suites.cipherSuitesTls12 = R"json([ "AES128-SHA", "aes128-sha" ])json";
        requireRefused( suites, "tls.cipherSuitesTls12[1] repeats an earlier suite" );

        suites = document;

        suites.cipherSuitesTls13 = R"json([ "TLS_AES_128_GCM_SHA256", "TLS_SHA256_SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls13[1] names a NULL cipher suite" );

        suites.cipherSuitesTls13 = R"json([ "+TLS_AES_128_GCM_SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls13[0] is not a plain suite name" );

        suites.cipherSuitesTls13 = R"json([ "TLS_DH_anon_WITH_AES_128_GCM_SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls13[0] names an anonymous cipher suite" );

        suites.cipherSuitesTls13 = R"json([ "TLS_AES_128_GCM_SHA256", "TLS_AES_128_GCM_SHA256" ])json";
        requireRefused( suites, "tls.cipherSuitesTls13[1] repeats an earlier suite" );
    }

    {
        auto suites = document;

        suites.cipherSuitesTls12 = stringArray( "SUITE-", httpclient::BrowserProfiles::MAX_CIPHER_SUITES_TLS12 );
        suites.cipherSuitesTls13 = stringArray( "TLS_TEST_", httpclient::BrowserProfiles::MAX_CIPHER_SUITES_TLS13 );

        const auto loaded = requireLoads( suites );

        UTF_REQUIRE_EQUAL( loaded.tls.cipherSuitesTls12.size(), 64U );
        UTF_REQUIRE_EQUAL( loaded.tls.cipherSuitesTls13.size(), 16U );

        suites.cipherSuitesTls12 = stringArray( "SUITE-", httpclient::BrowserProfiles::MAX_CIPHER_SUITES_TLS12 + 1U );
        requireRefused( suites, "tls.cipherSuitesTls12 has more than 64 entries" );

        suites = document;
        suites.cipherSuitesTls13 = stringArray( "TLS_TEST_", httpclient::BrowserProfiles::MAX_CIPHER_SUITES_TLS13 + 1U );
        requireRefused( suites, "tls.cipherSuitesTls13 has more than 16 entries" );
    }

    /*
     * Groups and signature algorithms, by the same rule: OpenSSL 3.5's list syntax - a key share
     * mark, a tuple separator, an algorithm pair - is not a name
     */

    {
        auto groups = document;

        groups.groups = R"json([ { "name": "*X25519", "keyShare": true } ])json";
        requireRefused( groups, "tls.groups[0].name is not a plain group name" );

        groups.groups = R"json([ { "name": "X25519" }, { "name": "X25519/secp256r1" } ])json";
        requireRefused( groups, "tls.groups[1].name is not a plain group name" );

        groups.groups = R"json([ { "name": "X25519" }, { "name": "x25519" } ])json";
        requireRefused( groups, "tls.groups[1].name repeats an earlier group" );

        const auto groupList = []( SAA_in const std::size_t count ) -> std::string
        {
            return jsonArray(
                count,
                []( SAA_in const std::size_t i ) -> std::string
                {
                    return R"json({ "name": ")json" + numbered( "GROUP-", i ) + R"json(" })json";
                }
                );
        };

        groups.groups = groupList( httpclient::BrowserProfiles::MAX_GROUPS );
        UTF_REQUIRE_EQUAL( requireLoads( groups ).tls.groups.size(), 32U );

        groups.groups = groupList( httpclient::BrowserProfiles::MAX_GROUPS + 1U );
        requireRefused( groups, "tls.groups has more than 32 entries" );
    }

    {
        auto algorithms = document;

        algorithms.signatureAlgorithms = R"json([ "RSA+SHA256" ])json";
        requireRefused( algorithms, "tls.signatureAlgorithms[0] is not a plain signature algorithm name" );

        algorithms.signatureAlgorithms = R"json([ "ed25519", "?rsa_pss_rsae_sha256" ])json";
        requireRefused( algorithms, "tls.signatureAlgorithms[1] is not a plain signature algorithm name" );

        algorithms.signatureAlgorithms = R"json([ "ed25519", "ED25519" ])json";
        requireRefused( algorithms, "tls.signatureAlgorithms[1] repeats an earlier signature algorithm" );

        algorithms.signatureAlgorithms = stringArray( "sigalg_", httpclient::BrowserProfiles::MAX_SIGNATURE_ALGORITHMS );
        UTF_REQUIRE_EQUAL( requireLoads( algorithms ).tls.signatureAlgorithms.size(), 32U );

        algorithms.signatureAlgorithms = stringArray( "sigalg_", httpclient::BrowserProfiles::MAX_SIGNATURE_ALGORITHMS + 1U );
        requireRefused( algorithms, "tls.signatureAlgorithms has more than 32 entries" );
    }

    /*
     * ALPN ids: 1 to 255 bytes (RFC 7301) of visible ASCII, no repeat, a bounded list
     */

    {
        auto alpn = document;

        const std::string rule = " is not 1 to 255 bytes of visible ASCII";

        alpn.alpnProtocols = R"json([ "h2", "" ])json";
        requireRefused( alpn, "tls.alpnProtocols[1]" + rule );

        alpn.alpnProtocols = R"json([ "h 2" ])json";
        requireRefused( alpn, "tls.alpnProtocols[0]" + rule );

        alpn.alpnProtocols = R"json([ "h2\r\n" ])json";
        requireRefused( alpn, "tls.alpnProtocols[0]" + rule );

        alpn.alpnProtocols = R"json([ "h\u00e9" ])json";
        requireRefused( alpn, "tls.alpnProtocols[0]" + rule );

        alpn.alpnProtocols = "[ " + quoted( std::string( 256U, 'p' ) ) + " ]";
        requireRefused( alpn, "tls.alpnProtocols[0]" + rule );

        alpn.alpnProtocols = "[ " + quoted( std::string( 255U, 'p' ) ) + " ]";
        UTF_REQUIRE_EQUAL( requireLoads( alpn ).tls.alpnProtocols[ 0 ].size(), 255U );

        alpn.alpnProtocols = R"json([ "h2", "http/1.1", "h2" ])json";
        requireRefused( alpn, "tls.alpnProtocols[2] repeats an earlier protocol" );

        alpn.alpnProtocols = stringArray( "p-", httpclient::BrowserProfiles::MAX_ALPN_PROTOCOLS );
        UTF_REQUIRE_EQUAL( requireLoads( alpn ).tls.alpnProtocols.size(), 8U );

        alpn.alpnProtocols = stringArray( "p-", httpclient::BrowserProfiles::MAX_ALPN_PROTOCOLS + 1U );
        requireRefused( alpn, "tls.alpnProtocols has more than 8 entries" );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_Http2SettingsAreValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    auto settings = document;

    /*
     * The wire's own ranges - a 16-bit id and a 32-bit value (RFC 9113 6.5.1)
     */

    settings.settings = R"json([ { "id": -1, "value": 1 } ])json";
    requireRefused( settings, "http2.settings[0].id is outside 0 to 65535" );

    settings.settings = R"json([ { "id": 1, "value": 1 }, { "id": 65536, "value": 1 } ])json";
    requireRefused( settings, "http2.settings[1].id is outside 0 to 65535" );

    settings.settings = R"json([ { "id": 6, "value": 4294967296 } ])json";
    requireRefused( settings, "http2.settings[0].value is outside 0 to 4294967295" );

    /*
     * The ids 6.5.2 defines and this library interprets, held to that section - and
     * SETTINGS_ENABLE_PUSH to zero, because this client never accepts a push (D11)
     */

    settings.settings = R"json([ { "id": 2, "value": 1 } ])json";
    requireRefused(
        settings,
        "http2.settings[0].value is not 0, and SETTINGS_ENABLE_PUSH must be: this client never accepts a push"
        );

    settings.settings = R"json([ { "id": 4, "value": 2147483648 } ])json";
    requireRefused(
        settings,
        "http2.settings[0].value is above 2147483647, the largest SETTINGS_INITIAL_WINDOW_SIZE (RFC 9113 6.5.2)"
        );

    for( const auto value : { "16383", "16777216" } )
    {
        settings.settings = std::string( R"json([ { "id": 5, "value": )json" ) + value + " } ]";
        requireRefused(
            settings,
            "http2.settings[0].value is outside 16384 to 16777215, the range of SETTINGS_MAX_FRAME_SIZE (RFC 9113 6.5.2)"
            );
    }

    /*
     * The boundaries of all of it, accepted - with ids 0, 8, 9 and 65535, which this library does
     * not interpret, passed through with any value, and a repeated id kept where it stands: 6.5.3
     * gives a repeat its meaning, the later value, and the session implements exactly that
     */

    settings.settings =
        R"json([ { "id": 2, "value": 0 }, { "id": 4, "value": 2147483647 }, { "id": 5, "value": 16384 },)json"
        R"json( { "id": 5, "value": 16777215 }, { "id": 6, "value": 4294967295 }, { "id": 0, "value": 7 },)json"
        R"json( { "id": 8, "value": 2 }, { "id": 9, "value": 4294967295 }, { "id": 65535, "value": 0 } ])json";

    const auto loaded = requireLoads( settings );

    UTF_REQUIRE_EQUAL( loaded.http2.settings.size(), 9U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 1 ].value.value(), 2147483647U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 2 ].value.value(), 16384U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 3 ].value.value(), 16777215U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 4 ].value.value(), 4294967295U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 5 ].id.value(), 0U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 6 ].id.value(), 8U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 6 ].value.value(), 2U );
    UTF_REQUIRE_EQUAL( loaded.http2.settings[ 8 ].id.value(), 65535U );

    const auto settingList = []( SAA_in const std::size_t count ) -> std::string
    {
        return jsonArray(
            count,
            []( SAA_in const std::size_t i ) -> std::string
            {
                return R"json({ "id": )json" + std::to_string( 100U + i ) + R"json(, "value": 1 })json";
            }
            );
    };

    settings.settings = settingList( httpclient::BrowserProfiles::MAX_SETTINGS );
    UTF_REQUIRE_EQUAL( requireLoads( settings ).http2.settings.size(), 32U );

    settings.settings = settingList( httpclient::BrowserProfiles::MAX_SETTINGS + 1U );
    requireRefused( settings, "http2.settings has more than 32 entries" );
}

UTF_AUTO_TEST_CASE( BrowserProfiles_Http2PrioritiesAndWindowsAreValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    /*
     * The numbers the session adds to or casts into a window: the connection window starts at
     * 65,535 and must stay within 2^31-1 after the increment (RFC 9113 6.9.1); the threshold is
     * held as a signed 32-bit number
     */

    {
        auto windows = document;

        windows.connectionWindowUpdateIncrement = "2147418113";
        requireRefused(
            windows,
            "http2.connectionWindowUpdateIncrement is above 2147418112, which would take the connection window past 2^31-1"
            );

        windows.connectionWindowUpdateIncrement = "2147418112";
        UTF_REQUIRE_EQUAL( requireLoads( windows ).http2.connectionWindowUpdateIncrement.value(), 2147418112U );

        windows = document;
        windows.windowUpdateThreshold = "2147483648";
        requireRefused( windows, "http2.windowUpdateThreshold is above 2147483647" );

        windows.windowUpdateThreshold = "2147483647";
        UTF_REQUIRE_EQUAL( requireLoads( windows ).http2.windowUpdateThreshold.value(), 2147483647U );

        windows = document;
        windows.hpackEncoderTableSize = "4294967296";
        requireRefused( windows, "http2.hpackEncoderTableSize is above 4294967295" );

        windows.hpackEncoderTableSize = "4294967295";
        UTF_REQUIRE_EQUAL( requireLoads( windows ).http2.hpackEncoderTableSize.value(), 4294967295U );

        windows = document;
        windows.hpackIndexingPolicy = R"json("Never")json";
        requireRefused( windows, "http2.hpackIndexingPolicy is none of Incremental, WithoutIndexing and NeverIndexed" );

        windows.hpackIndexingPolicy = R"json("NeverIndexed")json";
        UTF_REQUIRE(
            requireLoads( windows ).http2.hpackIndexingPolicy.value() == http2::HpackIndexingPolicy::NeverIndexed
            );
    }

    /*
     * The pseudo-header order names all four, once each, or none
     */

    {
        auto order = document;

        order.pseudoHeaderOrder = R"json([ "method", "status", "scheme", "path" ])json";
        requireRefused( order, "http2.pseudoHeaderOrder[1] is none of method, authority, scheme and path" );

        order.pseudoHeaderOrder = R"json([ "method", "path", "method", "scheme" ])json";
        requireRefused( order, "http2.pseudoHeaderOrder[2] repeats an earlier pseudo-header" );

        order.pseudoHeaderOrder = R"json([ "method", "authority", "scheme" ])json";
        requireRefused(
            order,
            "http2.pseudoHeaderOrder names only some of the four pseudo-headers; it must name all of them or none"
            );

        order.pseudoHeaderOrder = R"json([ "method", "path", "authority", "scheme" ])json";

        const auto loaded = requireLoads( order );

        UTF_REQUIRE( loaded.http2.pseudoHeaderOrder[ 1 ] == http2::Http2PseudoHeader::Path );
        UTF_REQUIRE( loaded.http2.pseudoHeaderOrder[ 3 ] == http2::Http2PseudoHeader::Scheme );
    }

    /*
     * PRIORITY frames on idle streams: a 31-bit stream id which is not 0, a 31-bit dependency which
     * is not the stream itself, an 8-bit weight - the octet on the wire, so the fingerprint's
     * weight 256 is 255 here - and no stream twice
     */

    {
        auto frames = document;

        const std::string path = "http2.idleStreamPriorities[0]";

        frames.idleStreamPriorities = R"json([ { "streamId": 0 } ])json";
        requireRefused( frames, path + ".streamId is outside 1 to 2147483647" );

        frames.idleStreamPriorities = R"json([ { "streamId": 2147483648 } ])json";
        requireRefused( frames, path + ".streamId is outside 1 to 2147483647" );

        frames.idleStreamPriorities = R"json([ { "streamId": 3, "streamDependency": 2147483648 } ])json";
        requireRefused( frames, path + ".streamDependency is above 2147483647" );

        frames.idleStreamPriorities = R"json([ { "streamId": 3, "streamDependency": 3 } ])json";
        requireRefused( frames, path + ".streamDependency is the stream itself" );

        frames.idleStreamPriorities = R"json([ { "streamId": 3, "weight": 256 } ])json";
        requireRefused( frames, path + ".weight is outside 0 to 255" );

        frames.idleStreamPriorities = R"json([ { "streamId": 3, "weight": -1 } ])json";
        requireRefused( frames, path + ".weight is outside 0 to 255" );

        frames.idleStreamPriorities =
            R"json([ { "streamId": 3 }, { "streamId": 5 }, { "streamId": 3, "streamDependency": 5 } ])json";
        requireRefused( frames, "http2.idleStreamPriorities[2].streamId repeats an earlier stream" );

        frames.idleStreamPriorities =
            R"json([ { "streamId": 3, "streamDependency": 2147483647, "weight": 0 },)json"
            R"json( { "streamId": 2147483647, "streamDependency": 0, "weight": 255, "exclusive": true } ])json";

        const auto loaded = requireLoads( frames );

        UTF_REQUIRE_EQUAL( loaded.http2.idleStreamPriorities[ 0 ].streamDependency.value(), 2147483647U );
        UTF_REQUIRE_EQUAL( loaded.http2.idleStreamPriorities[ 1 ].streamId.value(), 2147483647U );
        UTF_REQUIRE_EQUAL( loaded.http2.idleStreamPriorities[ 1 ].weight.value(), 255U );

        const auto frameList = []( SAA_in const std::size_t count ) -> std::string
        {
            return jsonArray(
                count,
                []( SAA_in const std::size_t i ) -> std::string
                {
                    return R"json({ "streamId": )json" + std::to_string( 2U * i + 1U ) + " }";
                }
                );
        };

        frames.idleStreamPriorities = frameList( httpclient::BrowserProfiles::MAX_IDLE_STREAM_PRIORITIES );
        frames.headersPriority.clear();
        UTF_REQUIRE_EQUAL( requireLoads( frames ).http2.idleStreamPriorities.size(), 16U );

        frames.idleStreamPriorities = frameList( httpclient::BrowserProfiles::MAX_IDLE_STREAM_PRIORITIES + 1U );
        requireRefused( frames, "http2.idleStreamPriorities has more than 16 entries" );
    }

    /*
     * The HEADERS priority is one fixed set of fields for every request, so it may depend on none
     * or on one of the profile's own idle streams; and fields beside isSet: false are refused
     * rather than ignored
     */

    {
        auto priority = document;

        priority.headersPriority = R"json({ "isSet": true, "streamDependency": 7, "weight": 1 })json";
        requireRefused(
            priority,
            "http2.headersPriority.streamDependency is neither 0 nor the stream of one of idleStreamPriorities"
            );

        priority.headersPriority = R"json({ "isSet": true, "streamDependency": 0, "weight": 256 })json";
        requireRefused( priority, "http2.headersPriority.weight is outside 0 to 255" );

        priority.headersPriority = R"json({ "isSet": false, "weight": 10 })json";
        requireRefused( priority, "http2.headersPriority carries priority fields but isSet is false" );

        priority.headersPriority = R"json({ "isSet": false, "streamDependency": 0, "weight": 0, "exclusive": false })json";
        UTF_REQUIRE_EQUAL( requireLoads( priority ).http2.headersPriority.isSet.value(), false );

        priority.headersPriority = R"json({ "isSet": true, "streamDependency": 0, "weight": 0, "exclusive": true })json";

        const auto loaded = requireLoads( priority );

        UTF_REQUIRE_EQUAL( loaded.http2.headersPriority.isSet.value(), true );
        UTF_REQUIRE_EQUAL( loaded.http2.headersPriority.streamDependency.value(), 0U );
        UTF_REQUIRE_EQUAL( loaded.http2.headersPriority.exclusive.value(), true );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_HeaderNamesAndValuesAreValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    const std::string accept = R"json({ "name": "accept", "value": "image/test" })json";

    const auto withSubresourceHeader = [ & ]( SAA_in const std::string& header ) -> ProfileDocument
    {
        auto result = document;

        result.subresource = replaced( result.subresource, accept, header );

        return result;
    };

    /*
     * Names are lower-case tokens: HTTP/2 sends every name in lower case and the HTTP/1.1 casing
     * comes from the case map, keyed by the lower-case name
     */

    for( const auto& name : { "Accept", "bad name", "x:y", "caf\\u00e9" } )
    {
        requireRefused(
            withSubresourceHeader( std::string( R"json({ "name": ")json" ) + name + R"json(", "value": "v" })json" ),
            "headers.subresource.defaultHeaders[1].name is not a lower-case token"
            );
    }

    /*
     * An empty name never reaches that rule: name is a required property of the data model, which
     * counts an empty string as not provided
     */

    requireRefused(
        withSubresourceHeader( R"json({ "name": "", "value": "v" })json" ),
        "document is not a browser profile of the expected shape - Required property 'name'"
        );

    /*
     * Values are field values as the session's http::HeaderList accepts them: no CR, LF or NUL -
     * the header injection design 6.2 names - nor any other control character, nor leading or
     * trailing whitespace; obs-text and inner spaces are the boundary, accepted
     */

    const std::string valueRule =
        "headers.subresource.defaultHeaders[1].value is not a valid field value: it carries a control "
        "character, or begins or ends with whitespace";

    for( const auto& value : { "a\\rb", "a\\nb", "a\\u0000b", "a\\u0007b", "a\\u007fb", " leading", "trailing\\t" } )
    {
        requireRefused(
            withSubresourceHeader( std::string( R"json({ "name": "accept", "value": ")json" ) + value + R"json(" })json" ),
            valueRule
            );
    }

    {
        const auto loaded = requireLoads(
            withSubresourceHeader( R"json({ "name": "accept", "value": "image/test, caf\u00e9;q=0.5" })json" )
            );

        UTF_REQUIRE_EQUAL(
            kindOf( loaded, httpclient::HttpRequestKind::Subresource ).defaultHeaders[ 1 ].value,
            "image/test, caf\xC3\xA9;q=0.5"
            );

        ( void ) requireLoads( withSubresourceHeader( R"json({ "name": "accept", "value": "" })json" ) );
    }

    /*
     * A name once per kind
     */

    requireRefused(
        withSubresourceHeader( accept + R"json(, { "name": "accept", "value": "*/*" })json" ),
        "headers.subresource.defaultHeaders[2].name repeats an earlier header"
        );

    /*
     * The headers the session or a driver owns: the session merges cookie and drops
     * proxy-authorization; authorization is a credential; the drivers own the framing; and the
     * connection-specific fields RFC 9113 8.2.2 forbids are no browser's default
     */

    for( const auto& owned :
        {
            "cookie",
            "authorization",
            "proxy-authorization",
            "content-length",
            "transfer-encoding",
            "keep-alive",
            "proxy-connection",
            "upgrade",
        } )
    {
        requireRefused(
            withSubresourceHeader( std::string( R"json({ "name": ")json" ) + owned + R"json(", "value": "1" })json" ),
            "headers.subresource.defaultHeaders[1] is " + std::string( owned ) + ", which the session or the transport owns"
            );
    }

    /*
     * A bounded list, per kind
     */

    const auto headerList = []( SAA_in const std::size_t count ) -> std::string
    {
        return jsonArray(
            count,
            []( SAA_in const std::size_t i ) -> std::string
            {
                return R"json({ "name": ")json" + numbered( "x-test-", i ) + R"json(", "value": "v" })json";
            }
            );
    };

    auto bounded = document;

    bounded.subresource =
        R"json({ "defaultHeaders": )json" + headerList( httpclient::BrowserProfiles::MAX_DEFAULT_HEADERS ) + " }";
    UTF_REQUIRE_EQUAL(
        kindOf( requireLoads( bounded ), httpclient::HttpRequestKind::Subresource ).defaultHeaders.size(),
        64U
        );

    bounded.subresource =
        R"json({ "defaultHeaders": )json" + headerList( httpclient::BrowserProfiles::MAX_DEFAULT_HEADERS + 1U ) + " }";
    requireRefused( bounded, "headers.subresource.defaultHeaders has more than 64 entries" );
}

UTF_AUTO_TEST_CASE( BrowserProfiles_HttpOneOnlyHeadersAreRepresentedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    /*
     * The plan review's F6, as decided: ONE default list per request kind, for both protocols,
     * with the fields a browser sends over HTTP/1.1 only sitting in it at their HTTP/1.1
     * positions. host is a marker - isComputed, no value - because its value is each request's
     * authority, and a captured Host naming the capture machine must never ship; connection
     * carries its literal. Over HTTP/2 both vanish by rules the driver already applies (RFC 9113
     * 8.2.2 and 8.3.1), so neither needs a mark of its own
     */

    const ProfileDocument document;

    const auto profile = requireLoads( document );

    const auto& navigation = kindOf( profile, httpclient::HttpRequestKind::Navigation );

    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].name, "host" );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].value, "" );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 0 ].isComputed.value(), true );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 1 ].name, "connection" );
    UTF_REQUIRE_EQUAL( navigation.defaultHeaders[ 1 ].value, "keep-alive" );
    UTF_REQUIRE_EQUAL( navigation.http1CaseMap.at( "host" ), "Host" );

    const std::string host = R"json({ "name": "host", "isComputed": true })json";

    const std::string hostRule =
        "headers.navigation.defaultHeaders[0] is host, which must be isComputed and carry no value: it is the "
        "authority of each request's URL";

    {
        auto literal = document;

        literal.navigation = replaced(
            literal.navigation,
            host,
            R"json({ "name": "host", "value": "capture.test", "isComputed": true })json"
            );

        requireRefused( literal, hostRule );

        auto notComputed = document;

        notComputed.navigation = replaced( notComputed.navigation, host, R"json({ "name": "host" })json" );

        requireRefused( notComputed, hostRule );
    }

    /*
     * connection carries keep-alive or close and nothing else - a token naming another field
     * would have the HTTP/2 driver remove that field (RFC 9110 7.6.1)
     */

    {
        const std::string keepAlive = R"json({ "name": "connection", "value": "keep-alive" })json";

        auto connection = document;

        for( const auto& value : { "close", "Keep-Alive" } )
        {
            connection.navigation = replaced(
                document.navigation,
                keepAlive,
                std::string( R"json({ "name": "connection", "value": ")json" ) + value + R"json(" })json"
                );

            UTF_REQUIRE_EQUAL(
                kindOf( requireLoads( connection ), httpclient::HttpRequestKind::Navigation ).defaultHeaders[ 1 ].value,
                value
                );
        }

        for( const auto& value : { "user-agent", "upgrade", "" } )
        {
            connection.navigation = replaced(
                document.navigation,
                keepAlive,
                std::string( R"json({ "name": "connection", "value": ")json" ) + value + R"json(" })json"
                );

            requireRefused( connection, "headers.navigation.defaultHeaders[1].value is neither keep-alive nor close" );
        }
    }

    /*
     * te, which RFC 9113 8.2.2 allows with the value trailers alone
     */

    {
        const std::string accept = R"json({ "name": "accept", "value": "image/test" })json";

        auto te = document;

        te.subresource = replaced( document.subresource, accept, accept + R"json(, { "name": "te", "value": "trailers" })json" );
        UTF_REQUIRE_EQUAL( kindOf( requireLoads( te ), httpclient::HttpRequestKind::Subresource ).defaultHeaders.size(), 3U );

        te.subresource = replaced( document.subresource, accept, accept + R"json(, { "name": "te", "value": "gzip" })json" );
        requireRefused(
            te,
            "headers.subresource.defaultHeaders[2].value is not trailers, the only value te may carry (RFC 9113 8.2.2)"
            );
    }

    /*
     * accept-encoding is a marker too: the session computes its value per request, from
     * headers.acceptEncoding and the registered decoders (6.5)
     */

    {
        auto encoding = document;

        encoding.navigation = replaced(
            encoding.navigation,
            R"json({ "name": "accept-encoding" })json",
            R"json({ "name": "accept-encoding", "value": "gzip, br" })json"
            );

        requireRefused(
            encoding,
            "headers.navigation.defaultHeaders[8] is accept-encoding, whose value the session computes from "
            "headers.acceptEncoding: it must carry no value"
            );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_PlacementCaseMapAndCodingsAreValidatedTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    const ProfileDocument document;

    /*
     * The placement of the caller's headers, and its anchor, which must be one of the kind's own
     * headers when it is used and absent when it is not
     */

    {
        auto placement = document;

        placement.fetch = replaced( document.fetch, R"json("Appended")json", R"json("Before")json" );
        requireRefused( placement, "headers.fetch.callerHeaderPlacement is none of Appended, Prepended and BeforeAnchor" );

        placement.fetch = replaced(
            document.fetch,
            R"json("callerHeaderPlacement": "Appended")json",
            R"json("callerHeaderPlacement": "Appended", "callerHeaderAnchor": "accept")json"
            );
        requireRefused( placement, "headers.fetch.callerHeaderAnchor is set but callerHeaderPlacement is not BeforeAnchor" );

        placement.fetch = replaced(
            document.fetch,
            R"json("callerHeaderPlacement": "Appended")json",
            R"json("callerHeaderPlacement": "BeforeAnchor", "callerHeaderAnchor": "accept")json"
            );
        UTF_REQUIRE_EQUAL( kindOf( requireLoads( placement ), httpclient::HttpRequestKind::Fetch ).callerHeaderAnchor, "accept" );

        placement = document;

        placement.navigation = replaced(
            document.navigation,
            R"json("callerHeaderAnchor": "accept-encoding")json",
            R"json("callerHeaderAnchor": "x-absent")json"
            );
        requireRefused( placement, "headers.navigation.callerHeaderAnchor names no header of defaultHeaders" );

        placement.navigation = replaced(
            document.navigation,
            R"json(, "callerHeaderAnchor": "accept-encoding")json",
            ""
            );
        requireRefused( placement, "headers.navigation.callerHeaderAnchor names no header of defaultHeaders" );
    }

    /*
     * The HTTP/1.1 case map changes a name's case and nothing else: a value spelling another name
     * would rename a header on the wire, and one carrying CR LF would add one
     */

    {
        const std::string caseMap = R"json("user-agent": "User-Agent")json";

        auto map = document;

        map.navigation = replaced( document.navigation, caseMap, R"json("User-Agent": "User-Agent")json" );
        requireRefused( map, "headers.navigation.http1CaseMap has a key which is not a lower-case token" );

        map.navigation = replaced( document.navigation, caseMap, R"json("user-agent": "Accept")json" );
        requireRefused( map, "headers.navigation.http1CaseMap.user-agent is not a token spelling the same name as its key" );

        map.navigation = replaced( document.navigation, caseMap, R"json("user-agent": "User-Agent\r\nX-Evil: 1")json" );
        requireRefused( map, "headers.navigation.http1CaseMap.user-agent is not a token spelling the same name as its key" );

        map.navigation = replaced( document.navigation, caseMap, R"json("user-agent": "USER-AGENT")json" );
        UTF_REQUIRE_EQUAL(
            kindOf( requireLoads( map ), httpclient::HttpRequestKind::Navigation ).http1CaseMap.at( "user-agent" ),
            "USER-AGENT"
            );

        const auto mapOf = []( SAA_in const std::size_t count ) -> std::string
        {
            std::string result = "{";

            for( std::size_t i = 0U; i < count; ++i )
            {
                result += 0U == i ? " " : ", ";
                result += quoted( numbered( "x-test-", i ) ) + ": " + quoted( numbered( "X-Test-", i ) );
            }

            return result + " }";
        };

        map.navigation = replaced(
            document.navigation,
            R"json({ "host": "Host", "connection": "Connection", "user-agent": "User-Agent" })json",
            mapOf( httpclient::BrowserProfiles::MAX_CASE_MAP_ENTRIES )
            );
        UTF_REQUIRE_EQUAL(
            kindOf( requireLoads( map ), httpclient::HttpRequestKind::Navigation ).http1CaseMap.size(),
            64U
            );

        map.navigation = replaced(
            document.navigation,
            R"json({ "host": "Host", "connection": "Connection", "user-agent": "User-Agent" })json",
            mapOf( httpclient::BrowserProfiles::MAX_CASE_MAP_ENTRIES + 1U )
            );
        requireRefused( map, "headers.navigation.http1CaseMap has more than 64 entries" );
    }

    /*
     * The RFC 9218 priority value is a field value
     */

    {
        auto priority = document;

        priority.navigation = replaced(
            document.navigation,
            R"json("priorityHeaderValue": "u=0, i")json",
            R"json("priorityHeaderValue": "u=0\r\nx-evil: 1")json"
            );
        requireRefused( priority, "headers.navigation.priorityHeaderValue is not a valid field value" );
    }

    /*
     * The codings go into a header value joined by ", ", so each must be a token - one carrying a
     * comma would smuggle in another coding - once each, in a bounded list
     */

    {
        auto codings = document;

        codings.acceptEncoding = R"json([ "gzip, deflate" ])json";
        requireRefused( codings, "headers.acceptEncoding[0] is not a token" );

        codings.acceptEncoding = R"json([ "gzip", "br", "GZIP" ])json";
        requireRefused( codings, "headers.acceptEncoding[2] repeats an earlier coding" );

        codings.acceptEncoding = "[]";
        UTF_REQUIRE( requireLoads( codings ).headers.acceptEncoding.empty() );

        codings.acceptEncoding = stringArray( "enc-", httpclient::BrowserProfiles::MAX_ACCEPT_ENCODINGS );
        UTF_REQUIRE_EQUAL( requireLoads( codings ).headers.acceptEncoding.size(), 16U );

        codings.acceptEncoding = stringArray( "enc-", httpclient::BrowserProfiles::MAX_ACCEPT_ENCODINGS + 1U );
        requireRefused( codings, "headers.acceptEncoding has more than 16 entries" );
    }

    /*
     * The q-value ladder is rendered into accept-language, so each is a qvalue exactly as RFC 9110
     * 12.4.2 spells one
     */

    {
        auto qValues = document;

        for( const auto& refused : { "1.001", "0.1234", ".5", "2", "0.9, x", "", "1.5" } )
        {
            qValues.acceptLanguageQValues = "[ " + quoted( refused ) + " ]";
            requireRefused( qValues, "headers.acceptLanguageQValues[0] is not a qvalue (RFC 9110 12.4.2)" );
        }

        qValues.acceptLanguageQValues = R"json([ "1", "1.", "1.000", "0", "0.", "0.9", "0.05", "0.001" ])json";
        UTF_REQUIRE_EQUAL( requireLoads( qValues ).headers.acceptLanguageQValues.size(), 8U );

        const auto ladder = []( SAA_in const std::size_t count ) -> std::string
        {
            return jsonArray(
                count,
                []( SAA_in const std::size_t ) -> std::string
                {
                    return R"json("0.5")json";
                }
                );
        };

        qValues.acceptLanguageQValues = ladder( httpclient::BrowserProfiles::MAX_ACCEPT_LANGUAGE_QVALUES );
        UTF_REQUIRE_EQUAL( requireLoads( qValues ).headers.acceptLanguageQValues.size(), 16U );

        qValues.acceptLanguageQValues = ladder( httpclient::BrowserProfiles::MAX_ACCEPT_LANGUAGE_QVALUES + 1U );
        requireRefused( qValues, "headers.acceptLanguageQValues has more than 16 entries" );
    }
}

UTF_AUTO_TEST_CASE( BrowserProfiles_UnknownBuiltInIdIsNotFoundTests )
{
    using namespace bl;
    using namespace utest::browserprofiles;

    /*
     * The built-in profiles are L7-E's content; until it lands there are none, and every id is a
     * NotFoundException - including the id of a profile which load( ) has just returned, because
     * load( ) hands a profile back and registers nothing
     */

    ( void ) requireLoads( ProfileDocument() );

    for( const auto& id : { "chrome", "", "test-profile" } )
    {
        UTF_REQUIRE_THROW_MESSAGE(
            httpclient::BrowserProfiles::get( id ),
            NotFoundException,
            "There is no built-in browser profile with the id given"
            );
    }
}

#endif /* __UTEST_TESTBROWSERPROFILES_H_ */
