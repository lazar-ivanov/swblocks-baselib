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

#include <baselib/security/AuthorizationServiceRest.h>

#include <baselib/data/DataModelObject.h>

#include <baselib/core/SerializationUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/TestUtils.h>

/*
 * The helpers below exercise the real production code without a server, without a thread pool
 * and without an execution queue - the authorization task is constructed and then inspected (or
 * handed a response directly), but it is never scheduled
 */

static auto loadConfig() -> bl::om::ObjPtr< bl::dm::config::AuthorizationServiceRestConfig >
{
    /*
     * A fresh object is loaded for each sub-case because AuthorizationServiceRest::create()
     * calls readOnly( true ) on the config it is handed and om::copy shares the same object
     */

    return bl::dm::DataModelUtils::loadFromFile< bl::dm::config::AuthorizationServiceRestConfig >(
        utest::TestUtils::resolveDataFilePath( "authorization_service_rest_config.json" )
        );
}

static auto pathAndContentFor(
    SAA_in          const bl::om::ObjPtr< bl::security::AuthorizationServiceRest >&     service,
    SAA_in          const std::string&                                                  tokenText
    )
    -> std::pair< std::string, std::string >
{
    const auto token = bl::security::AuthorizationCache::createAuthenticationToken( tokenText );

    const auto task = service -> createAuthorizationTask( token );

    const auto taskImpl =
        bl::om::qi< bl::security::AuthorizationServiceRest::task_impl_t >( task );

    /*
     * The token is client controlled, so it must be redacted from any exception the task raises
     */

    UTF_CHECK( taskImpl -> isSecureMode() );

    return std::make_pair( taskImpl -> getPath(), taskImpl -> getContent() );
}

static auto extractFromResponse(
    SAA_in          const bl::om::ObjPtr< bl::security::AuthorizationServiceRest >&     service,
    SAA_in          const std::string&                                                  tokenText,
    SAA_in          const std::string&                                                  responseText
    )
    -> bl::om::ObjPtr< bl::security::SecurityPrincipal >
{
    const auto token = bl::security::AuthorizationCache::createAuthenticationToken( tokenText );

    const auto task = service -> createAuthorizationTask( token );

    bl::om::qi< bl::security::AuthorizationServiceRest::task_impl_t >( task ) -> getResponseLvalue() =
        responseText;

    return service -> extractSecurityPrincipal( task );
}

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_TemplateEscapingTests )
{
    using namespace bl;
    using namespace bl::security;
    using namespace utest;

    typedef dm::config::AuthorizationServiceRestConfig rest_config_t;

    const auto templateText =
        TestUtils::loadDataFile( "content_template.txt", true /* normalizeLineBreaks */ );

    /*
     * The create( config ) overload never reads contentTemplateFilePath, so the content
     * template has to be handed over through contentTemplateBase64
     */

    const auto prepareConfig = [ & ]() -> om::ObjPtr< rest_config_t >
    {
        auto config = loadConfig();

        config -> readOnly( false );
        config -> contentTemplateBase64( SerializationUtils::base64EncodeString( templateText ) );

        return config;
    };

    /*
     * The token below carries an '&' and an '=', which is exactly what is needed in order to
     * append a query parameter of one's own to the authorization request; str::parsePropertiesList
     * splits on ';' and then on the FIRST '=', so 'tokenId' ends up with the value 'a&b=c'
     */

    const std::string injectingToken( "tokenId=a&b=c;tokenProperty1=v1" );

    /*
     * (1) Escaping is on - that is the default when 'escapeTemplateVariables' is absent from
     * the config, which is the case for the config fixture; the '&' and the '=' are percent
     * encoded, so the token value cannot start a new query parameter
     */

    {
        const auto config = prepareConfig();

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            injectingToken
            );

        UTF_REQUIRE_EQUAL( result.first, std::string( "/test/authorize?tokenId=a%26b%3Dc" ) );
    }

    /*
     * (2) Escaping is explicitly off - the raw value is substituted and the token does inject a
     * second query parameter; this is what makes (1) an assertion about the escaper being
     * installed rather than an assertion about what str::uriEncode does
     */

    {
        const auto config = prepareConfig();

        config -> escapeTemplateVariables( false );

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            injectingToken
            );

        UTF_REQUIRE_EQUAL( result.first, std::string( "/test/authorize?tokenId=a&b=c" ) );
    }

    /*
     * The token below carries a double quote, which would terminate the string it is substituted
     * into if the JSON body was built out of it without escaping
     */

    const std::string quotingToken( "tokenId=t1;tokenProperty1=va\"lue" );

    /*
     * (3) Escaping is on and the content type is a JSON one (the config fixture declares
     * 'application/json; charset=UTF-8'), so the quote is escaped in the request body
     */

    {
        const auto config = prepareConfig();

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            quotingToken
            );

        UTF_CHECK( cpp::contains( result.second, "::tokenProperty1::va\\\"lue" ) );
        UTF_CHECK( ! cpp::contains( result.second, "::tokenProperty1::va\"lue" ) );
    }

    /*
     * (4) Escaping is on and the content type is an XML one - the quote and the angle brackets
     * of a token which would otherwise close the element it is substituted into are escaped
     */

    {
        const auto config = prepareConfig();

        config -> contentType( "application/xml" );

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            "tokenId=t1;tokenProperty1=v<a>&\"lue"
            );

        UTF_CHECK( cpp::contains( result.second, "::tokenProperty1::v&lt;a&gt;&amp;&quot;lue" ) );
        UTF_CHECK( ! cpp::contains( result.second, "::tokenProperty1::v<a>" ) );
    }

    /*
     * (5) Escaping is on and the content type is a form encoded one - the '&' and the '=' of the
     * token are percent encoded, so it cannot append a field of its own to the body
     */

    {
        const auto config = prepareConfig();

        config -> contentType( "application/x-www-form-urlencoded" );

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            "tokenId=t1;tokenProperty1=a&b=c"
            );

        UTF_CHECK( cpp::contains( result.second, "::tokenProperty1::a%26b%3Dc" ) );
    }

    /*
     * (6) Escaping is on, but the content type has no escaper of its own - the service refuses
     * to be created rather than placing a client controlled value into a structured body
     * unencoded, so the failure is at startup with a clear message
     */

    {
        const auto config = prepareConfig();

        config -> contentType( "text/plain" );

        UTF_REQUIRE_THROW_MESSAGE(
            ( void ) AuthorizationServiceRest::create( om::copy( config ) ),
            ArgumentException,
            "has no escaper for the substituted template variables"
            );
    }

    /*
     * (7) The same content type is accepted once escaping is turned off - the explicit opt out
     * is what keeps a deployment with an unusual body structure working
     */

    {
        const auto config = prepareConfig();

        config -> contentType( "text/plain" );
        config -> escapeTemplateVariables( false );

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            quotingToken
            );

        UTF_CHECK( cpp::contains( result.second, "::tokenProperty1::va\"lue" ) );
    }

    /*
     * (8) The token does not carry the property the URL template refers to - the template is
     * resolved with skipUndefined and the whole URL template is a single block, so the request
     * path resolves to an empty string
     */

    {
        const auto config = prepareConfig();

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            "other=x"
            );

        UTF_CHECK( result.first.empty() );
    }
}

/*
 * The responses below are inline because the status category gate is decided entirely by the
 * 'Status' and the 'StatusCategory' lines, so a data file would only hide which line matters
 */

static const char* const g_okCategory5 =
    "Status::2\nStatusCategory::5\nStatusMessage::ok\nSid::ID_1234\n::tokenId::tokenId1\n";

static const char* const g_okCategory4 =
    "Status::2\nStatusCategory::4\nStatusMessage::ok\nSid::ID_5678\n::tokenId::tokenId1\n";

static const char* const g_okNoCategory =
    "Status::2\nStatusMessage::ok\nSid::ID_1234\n::tokenId::tokenId1\n";

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_StatusCategoryGateTests )
{
    using namespace bl;
    using namespace bl::security;

    /*
     * The status carried by all three responses is the configured success status, so the status
     * category gate is the only thing which can reject any of them
     */

    const std::string tokenText( "tokenId=tokenId1;tokenProperty1=tokenPropertyValue1" );

    /*
     * (1) The configured success status category matches the one carried by the response
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> successStatusCategory( 5 );

        const auto principal = extractFromResponse(
            AuthorizationServiceRest::create( om::copy( config ) ),
            tokenText,
            g_okCategory5
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_1234" ) );
    }

    /*
     * (2) The configured success status category does not match the one carried by the response -
     * the request is reported as failed even though the status itself is a success one
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> successStatusCategory( 5 );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                g_okCategory4
                ),
            SecurityException,
            "Authorization service failed with the following error details: "
            "[status='2'; statusCategory='4'; message='ok']"
            );
    }

    /*
     * (3) A success status category is configured, but the response does not carry one at all -
     * a configured category cannot be silently ignored
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> successStatusCategory( 5 );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                g_okNoCategory
                ),
            SecurityException,
            "A property with name 'StatusCategory' could not be parsed from an authorization response"
            );
    }

    /*
     * (4) The config does not ask for a success status category, so the one carried by the
     * response is not part of the decision; this is what keeps (2) attributable to the gate
     * rather than to the category simply being parsed out of the response
     */

    {
        const auto config = loadConfig();

        const auto principal = extractFromResponse(
            AuthorizationServiceRest::create( om::copy( config ) ),
            tokenText,
            g_okCategory4
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_5678" ) );
    }
}

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_TokenTextRejectionTests )
{
    using namespace bl;
    using namespace bl::security;

    /*
     * For a non-binary token every byte is screened and '\r', '\n' and '\0' are rejected
     * before anything at all is built out of the token - before str::parsePropertiesList,
     * before either template is resolved and before the HTTP task exists; these are the bytes
     * which would break the HTTP request line and the JSON body under any encoding applied
     * downstream of the screen
     */

    const auto stockService = []() -> om::ObjPtr< AuthorizationServiceRest >
    {
        return AuthorizationServiceRest::create( loadConfig() );
    };

    const std::string crlfToken( "tokenId=t1\r\n" );

    UTF_REQUIRE_THROW_MESSAGE(
        pathAndContentFor( stockService(), crlfToken ),
        SecurityException,
        "The authentication token contains an invalid character"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        pathAndContentFor( stockService(), "tokenId=t1\nHost: evil" ),
        SecurityException,
        "The authentication token contains an invalid character"
        );

    /*
     * The explicit length constructor is required here - a NUL terminated literal would lose
     * the byte long before it ever reached createAuthenticationToken( ... )
     */

    const std::string nulToken( "tokenId=t1\0x", 12U );

    UTF_REQUIRE_EQUAL( nulToken.size(), 12U );

    UTF_REQUIRE_THROW_MESSAGE(
        pathAndContentFor( stockService(), nulToken ),
        SecurityException,
        "The authentication token contains an invalid character"
        );

    /*
     * The negative control - a token of the very same shape without any of the rejected bytes
     * goes through, which is what makes the three assertions above attributable to the screen
     */

    UTF_CHECK_NO_THROW( pathAndContentFor( stockService(), "tokenId=t1;tokenProperty1=v" ) );

    /*
     * The screen is skipped for a binary token deliberately, because such a token is base64
     * encoded on its way into the request; the very same CRLF bearing bytes are accepted here
     */

    {
        auto config = loadConfig();

        config -> readOnly( false );
        config -> isTokenBinary( true );
        config -> isTokenMultiProperties( false );
        config -> urlPathTemplate( "/a?t={{$binaryToken}}" );

        const auto service = AuthorizationServiceRest::create( om::copy( config ) );

        std::pair< std::string, std::string > result;

        UTF_CHECK_NO_THROW( result = pathAndContentFor( service, crlfToken ) );

        UTF_REQUIRE_EQUAL(
            result.first,
            "/a?t=" + str::uriEncode(
                SerializationUtils::base64Encode( crlfToken.c_str(), crlfToken.size() )
                )
            );

        /*
         * The CR and the LF really are in the token ...
         */

        UTF_REQUIRE( cpp::contains( crlfToken, std::string( "\r\n" ) ) );

        /*
         * ... they do not reach the request path ...
         */

        UTF_REQUIRE( std::string::npos == result.first.find( '\r' ) );
        UTF_REQUIRE( std::string::npos == result.first.find( '\n' ) );

        /*
         * ... and nothing was dropped on the way, the encoding is what neutralised them
         */

        UTF_REQUIRE_EQUAL(
            SerializationUtils::base64DecodeString( str::uriDecode( result.first.substr( 5U ) ) ),
            crlfToken
            );
    }
}

/*
 * The two responses below differ in every field, which is what makes a service instance that
 * carried state across calls observable
 */

static const char* const g_principalResponseA =
    "Status::2\nStatusCategory::5\nStatusMessage::ok\nSid::ID_1111\nGivenName::Ann\n"
    "FamilyName::One\nEmail::ann@host\nTypeId::t1\n::tokenId::tokenA\n";

static const char* const g_principalResponseB =
    "Status::2\nStatusCategory::4\nStatusMessage::ok\nSid::ID_2222\nGivenName::Bob\n"
    "FamilyName::Two\nEmail::bob@host\nTypeId::t2\n::tokenId::tokenB\n";

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_ExtractIsPerCallStateTests )
{
    using namespace bl;
    using namespace bl::security;

    /*
     * A single service instance serves every authorization in a broker process, concurrently,
     * so all of the mutable parsing state has to be per call; were the property map ever
     * shared or cached across calls every extraction after the first would find the properties
     * already found and would return the FIRST caller's principal - cross user authorization
     * confusion which the suite would not notice
     */

    const auto service = AuthorizationServiceRest::create( loadConfig() );

    const std::string tokenText( "tokenId=tokenId1;tokenProperty1=tokenPropertyValue1" );

    const auto rotatedTokenText = []( SAA_in const om::ObjPtr< data::DataBlock >& token ) -> std::string
    {
        return std::string( token -> begin(), token -> end() );
    };

    const auto validateA = [ & ]( SAA_in const om::ObjPtr< SecurityPrincipal >& principal ) -> void
    {
        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_1111" ) );
        UTF_REQUIRE_EQUAL( principal -> givenName(), std::string( "Ann" ) );
        UTF_REQUIRE_EQUAL( principal -> familyName(), std::string( "One" ) );
        UTF_REQUIRE_EQUAL( principal -> email(), std::string( "ann@host" ) );
        UTF_REQUIRE_EQUAL( principal -> typeId(), std::string( "t1" ) );

        UTF_REQUIRE( principal -> authenticationToken() );

        UTF_REQUIRE_EQUAL(
            rotatedTokenText( principal -> authenticationToken() ),
            std::string( "tokenId=tokenA" )
            );
    };

    validateA( extractFromResponse( service, tokenText, g_principalResponseA ) );

    /*
     * The second response, on the very same instance - every field must be the second
     * response's own
     */

    {
        const auto principal = extractFromResponse( service, tokenText, g_principalResponseB );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_2222" ) );
        UTF_REQUIRE_EQUAL( principal -> givenName(), std::string( "Bob" ) );
        UTF_REQUIRE_EQUAL( principal -> familyName(), std::string( "Two" ) );
        UTF_REQUIRE_EQUAL( principal -> email(), std::string( "bob@host" ) );
        UTF_REQUIRE_EQUAL( principal -> typeId(), std::string( "t2" ) );

        UTF_REQUIRE( principal -> authenticationToken() );

        UTF_REQUIRE_EQUAL(
            rotatedTokenText( principal -> authenticationToken() ),
            std::string( "tokenId=tokenB" )
            );
    }

    /*
     * And back to the first response - the instance is stateless across calls in both
     * directions, not merely in the forward one
     */

    validateA( extractFromResponse( service, tokenText, g_principalResponseA ) );

    /*
     * Within one response the FIRST match wins - the appended second 'Sid' line is skipped
     * because the property has already been found
     */

    const auto principalWithTwoSids = extractFromResponse(
        service,
        tokenText,
        std::string( g_principalResponseA ) + "Sid::ID_9999\n"
        );

    UTF_REQUIRE_EQUAL( principalWithTwoSids -> secureIdentity(), std::string( "ID_1111" ) );
}

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_TokenShapeTests )
{
    using namespace bl;
    using namespace bl::security;

    /*
     * The configuration selects one of three mutually exclusive token shapes; only the
     * multi-properties one is reachable through the shared drivers, so the other two are
     * exercised here
     */

    /*
     * (1) The binary shape - the request carries base64( token bytes ) and the response's
     * UpdatedBinaryToken is base64 decoded into a fresh data block, so the rotated token is
     * arbitrary bytes, embedded NULs included
     */

    {
        auto config = loadConfig();

        config -> readOnly( false );
        config -> isTokenBinary( true );
        config -> isTokenMultiProperties( false );
        config -> urlPathTemplate( "/authorize?t={{$binaryToken}}" );

        const auto service = AuthorizationServiceRest::create( om::copy( config ) );

        const std::string tokenBytes( "\x01\x00\xFE\xFF binary", 11U );
        const std::string rotated( "\x00\x01\x02rot", 6U );

        UTF_REQUIRE_EQUAL( tokenBytes.size(), 11U );
        UTF_REQUIRE_EQUAL( rotated.size(), 6U );

        const auto result = pathAndContentFor( service, tokenBytes );

        /*
         * Standard base64 emits '+', '/' and '=', which escapeForUrlPath percent encodes
         */

        UTF_REQUIRE_EQUAL(
            result.first,
            std::string( "/authorize?t=AQD%2B%2FyBiaW5hcnk%3D" )
            );

        UTF_REQUIRE_EQUAL(
            result.first,
            "/authorize?t=" + str::uriEncode(
                SerializationUtils::base64Encode( tokenBytes.c_str(), tokenBytes.size() )
                )
            );

        const auto principal = extractFromResponse(
            service,
            tokenBytes,
            "Status::2\nSid::ID_B\nUpdatedBinaryToken::" +
                SerializationUtils::base64Encode( rotated.c_str(), rotated.size() ) +
                "\n"
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_B" ) );

        UTF_REQUIRE( principal -> authenticationToken() );

        /*
         * The embedded NUL survived and setSize( ... ) was given the decoded length - a
         * reassembly through a std::string would have truncated at the first byte
         */

        UTF_REQUIRE_EQUAL( principal -> authenticationToken() -> size(), rotated.size() );

        UTF_REQUIRE_EQUAL(
            0,
            std::memcmp(
                principal -> authenticationToken() -> pv(),
                rotated.c_str(),
                rotated.size()
                )
            );
    }

    /*
     * (2) The single text shape - the whole token is one variable and the response's
     * UpdatedTextToken becomes the rotated token verbatim
     */

    {
        auto config = loadConfig();

        config -> readOnly( false );
        config -> isTokenBinary( false );
        config -> isTokenMultiProperties( false );
        config -> urlPathTemplate( "/authorize?t={{$textToken}}" );

        const auto service = AuthorizationServiceRest::create( om::copy( config ) );

        const std::string tokenText( "opaque-token-value" );

        const auto result = pathAndContentFor( service, tokenText );

        /*
         * The whole token is substituted (percent encoded - the hyphen is not in the set of
         * characters str::uriEncode leaves alone) rather than exploded into properties
         */

        UTF_REQUIRE_EQUAL(
            result.first,
            std::string( "/authorize?t=opaque%2Dtoken%2Dvalue" )
            );

        /*
         * The response carries a token property line as well; the token property branch is
         * guarded by ( ! isTokenBinary() && isTokenMultiProperties() ), so it must not run
         * here and the rotated token must reflect UpdatedTextToken only
         */

        const auto principal = extractFromResponse(
            service,
            tokenText,
            "Status::2\nSid::ID_T\nUpdatedTextToken::next-token\n::tokenId::x\n"
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_T" ) );

        UTF_REQUIRE( principal -> authenticationToken() );

        UTF_REQUIRE_EQUAL(
            std::string(
                principal -> authenticationToken() -> begin(),
                principal -> authenticationToken() -> end()
                ),
            std::string( "next-token" )
            );
    }
}

UTF_AUTO_TEST_CASE( AuthorizationServiceRest_ResponseParsingGuards )
{
    using namespace bl;
    using namespace bl::security;

    /*
     * extractSecurityPrincipal() normalises the response by erasing every '\r' and then
     * applies four config driven validations, each of which raises a user friendly
     * SecurityException. All four are reachable through the *configuration*, which is
     * operator supplied data, and none of them has ever been executed - the single config
     * fixture has well formed one group and two group regexes and every response in the
     * suite is '\n' delimited
     */

    const std::string tokenText( "tokenId=tokenId1;tokenProperty1=tokenPropertyValue1" );

    /*
     * (1) A CRLF delimited response parses identically to an LF one - real HTTP servers emit
     * CRLF
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );

        const auto principal = extractFromResponse(
            AuthorizationServiceRest::create( om::copy( config ) ),
            tokenText,
            "Status::2\r\nStatusMessage::ok\r\nSid::ID_CRLF\r\n::tokenId::tokenA\r\n"
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_CRLF" ) );
    }

    /*
     * (2) ... and the erase is *not* subsumed by the per line str::trim: a '\r' which sits in
     * the middle of a value is removed too
     *
     * This is the assertion which survives if someone deletes the erase believing that
     * str::trim covers it - with a CRLF-only response the deletion would be invisible
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );

        const auto principal = extractFromResponse(
            AuthorizationServiceRest::create( om::copy( config ) ),
            tokenText,
            "Status::2\nSid::ID_A\rB\n"
            );

        UTF_REQUIRE_EQUAL( principal -> secureIdentity(), std::string( "ID_AB" ) );
    }

    /*
     * (3) A configured unique property regex with no capturing group at all
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> regexSid( "Sid\\:\\:.+" );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                "Status::2\nSid::ID_NO_GROUP\n"
                ),
            SecurityException,
            "Regular expression pattern in the authorization config is expected to have one group only"
            );
    }

    /*
     * (4) A configured token property regex with one capturing group instead of two
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> regexUpdatedTokenProperty( "\\:\\:(.+)" );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                "Status::2\nSid::ID_ONE_GROUP\n::tokenId::tokenA\n"
                ),
            SecurityException,
            "Regular expression pattern for token property in authorization config "
            "is expected to have exactly two groups"
            );
    }

    /*
     * (5) and (6) - the two non-empty guards. The stock config captures with '(.+)', so a
     * match always yields at least one character and neither guard can fire with it; a config
     * which captures with '(.*)' reaches both
     */

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> regexUpdatedTokenProperty( "\\:\\:(.*)\\:\\:(.*)" );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                "Status::2\nSid::ID_EMPTY_NAME\n::::value\n"
                ),
            SecurityException,
            "Token property name parsed from authorization response can't be empty"
            );
    }

    {
        const auto config = loadConfig();

        config -> readOnly( false );
        config -> regexUpdatedTokenProperty( "\\:\\:(.*)\\:\\:(.*)" );

        UTF_REQUIRE_THROW_MESSAGE(
            extractFromResponse(
                AuthorizationServiceRest::create( om::copy( config ) ),
                tokenText,
                "Status::2\nSid::ID_EMPTY_VALUE\n::name::\n"
                ),
            SecurityException,
            "Token property value parsed from authorization response can't be empty"
            );
    }
}
