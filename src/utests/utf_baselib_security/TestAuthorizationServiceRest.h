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
     * (4) Escaping is on, but the content type is not a JSON one - the content escaper is
     * installed only for JSON content types, so the raw value reaches the request body; this
     * pins the current behavior, so that a change to it has to be a deliberate one
     */

    {
        const auto config = prepareConfig();

        config -> contentType( "text/plain" );

        const auto result = pathAndContentFor(
            AuthorizationServiceRest::create( om::copy( config ) ),
            quotingToken
            );

        UTF_CHECK( cpp::contains( result.second, "::tokenProperty1::va\"lue" ) );
    }

    /*
     * (5) The token does not carry the property the URL template refers to - the template is
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
