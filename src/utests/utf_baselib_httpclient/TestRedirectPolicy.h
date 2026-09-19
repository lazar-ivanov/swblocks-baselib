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

#include <baselib/httpclient/RedirectPolicy.h>

#include <baselib/http/HeaderList.h>

#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * Slice S2.8 - the redirect policy
 *
 * The method rewrite is tested as a full MATRIX rather than by example, because the rule is not
 * uniform: it depends on the status and on the method together, and the cells people get wrong are
 * the ones a sample leaves out - 301 on a DELETE, 303 on a HEAD
 *
 * The credential-drop and downgrade-refusal cases are security properties and are asserted as
 * such: each names what it prevents, and each is checked from both sides so that a rule which
 * stopped firing would fail rather than merely stop being exercised
 */

namespace utest
{
    namespace redirectpolicy
    {
        inline bl::net::Uri uri( const std::string& text )
        {
            return bl::net::Uri::parse( text );
        }

        /**
         * @brief A policy with redirects turned on, which is not the default
         */

        inline bl::httpclient::RedirectPolicy enabled()
        {
            bl::httpclient::RedirectPolicy policy;

            policy.isEnabled( true );

            return policy;
        }
    }

} // utest

UTF_AUTO_TEST_CASE( RedirectPolicy_OffByDefaultTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::redirectpolicy;

    /*
     * Off by default, as design 5.6 requires and as the existing client behaves: a 3xx is reported
     * to the caller rather than followed
     */

    const RedirectPolicy policy;

    UTF_CHECK( ! policy.isEnabled() );
    UTF_CHECK( ! policy.allowHttpsToHttpDowngrade() );
    UTF_CHECK_EQUAL( policy.maxHops(), static_cast< std::size_t >( RedirectPolicy::DEFAULT_MAX_HOPS ) );

    const auto decision = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( ! decision.shouldFollow() );
    UTF_CHECK( RedirectAction::DoNotFollow == decision.action.value() );
    UTF_CHECK( RedirectRefusal::Disabled == decision.refusal.value() );

    /*
     * ... and the target is still resolved, because a caller which reports the redirect instead of
     * following it wants exactly that URL - which is what errinfo_http_redirect_url carries today
     */

    UTF_CHECK_EQUAL( decision.target.toString(), std::string( "https://example.com/b" ) );

    /*
     * A status which is not a redirect is not one whichever way the switch is set, and the five
     * which are are exactly 301, 302, 303, 307 and 308
     */

    UTF_CHECK( RedirectPolicy::isRedirectStatus( 301U ) );
    UTF_CHECK( RedirectPolicy::isRedirectStatus( 302U ) );
    UTF_CHECK( RedirectPolicy::isRedirectStatus( 303U ) );
    UTF_CHECK( RedirectPolicy::isRedirectStatus( 307U ) );
    UTF_CHECK( RedirectPolicy::isRedirectStatus( 308U ) );

    UTF_CHECK( ! RedirectPolicy::isRedirectStatus( 200U ) );
    UTF_CHECK( ! RedirectPolicy::isRedirectStatus( 300U ) );
    UTF_CHECK( ! RedirectPolicy::isRedirectStatus( 304U ) );
    UTF_CHECK( ! RedirectPolicy::isRedirectStatus( 305U ) );
    UTF_CHECK( ! RedirectPolicy::isRedirectStatus( 400U ) );

    const auto notARedirect = enabled().evaluate(
        uri( "https://example.com/a" ),
        "GET",
        200U,
        "https://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( RedirectAction::DoNotFollow == notARedirect.action.value() );
    UTF_CHECK( RedirectRefusal::None == notARedirect.refusal.value() );
}

UTF_AUTO_TEST_CASE( RedirectPolicy_MethodRewriteMatrixTests )
{
    using namespace bl;
    using namespace bl::httpclient;

    /*
     * The whole matrix - five statuses by six methods - written out, because this is the rule
     * implementations get wrong and a sample of it proves nothing about the cells it omits
     *
     *   303  -> GET for every method EXCEPT HEAD, which stays HEAD (WHATWG Fetch, and what
     *           browsers and curl do). A HEAD rewritten to GET would download the body the request
     *           exists to avoid
     *   301
     *   302  -> GET ONLY for POST. A DELETE rewritten to GET becomes a different request which
     *           silently succeeds
     *   307
     *   308  -> method and body preserved, always
     */

    struct Expected
    {
        const char*                                                     method;
        unsigned                                                        status;
        const char*                                                     expectedMethod;
        bool                                                            expectedDropBody;
    };

    static const Expected matrix[] =
    {
        /* 301 */
        { "GET",    301U, "GET",    false },
        { "HEAD",   301U, "HEAD",   false },
        { "POST",   301U, "GET",    true  },
        { "PUT",    301U, "PUT",    false },
        { "DELETE", 301U, "DELETE", false },
        { "PATCH",  301U, "PATCH",  false },

        /* 302 */
        { "GET",    302U, "GET",    false },
        { "HEAD",   302U, "HEAD",   false },
        { "POST",   302U, "GET",    true  },
        { "PUT",    302U, "PUT",    false },
        { "DELETE", 302U, "DELETE", false },
        { "PATCH",  302U, "PATCH",  false },

        /* 303 */
        { "GET",    303U, "GET",    false },
        { "HEAD",   303U, "HEAD",   false },
        { "POST",   303U, "GET",    true  },
        { "PUT",    303U, "GET",    true  },
        { "DELETE", 303U, "GET",    true  },
        { "PATCH",  303U, "GET",    true  },

        /* 307 */
        { "GET",    307U, "GET",    false },
        { "HEAD",   307U, "HEAD",   false },
        { "POST",   307U, "POST",   false },
        { "PUT",    307U, "PUT",    false },
        { "DELETE", 307U, "DELETE", false },
        { "PATCH",  307U, "PATCH",  false },

        /* 308 */
        { "GET",    308U, "GET",    false },
        { "HEAD",   308U, "HEAD",   false },
        { "POST",   308U, "POST",   false },
        { "PUT",    308U, "PUT",    false },
        { "DELETE", 308U, "DELETE", false },
        { "PATCH",  308U, "PATCH",  false },
    };

    const auto policy = utest::redirectpolicy::enabled();

    for( std::size_t index = 0U; index < ( sizeof( matrix ) / sizeof( matrix[ 0 ] ) ); ++index )
    {
        const auto& row = matrix[ index ];

        const std::string context =
            std::string( row.method ) + " on " + std::to_string( row.status );

        bool dropBody = false;

        /*
         * The row is folded into the compared strings rather than passed as a message, so that a
         * failure names the cell - there is no UTF_CHECK_MESSAGE in this library's wrapper, and a
         * bare "false" in a thirty-row loop says nothing about which row it was
         */

        UTF_CHECK_EQUAL(
            context + " -> " + RedirectPolicy::rewriteMethod( row.status, row.method, dropBody ),
            context + " -> " + row.expectedMethod
            );

        UTF_CHECK_EQUAL(
            context + ( dropBody ? " drops the body" : " keeps the body" ),
            context + ( row.expectedDropBody ? " drops the body" : " keeps the body" )
            );

        /*
         * ... and the same through the decision, so the matrix is not merely a property of the
         * helper
         */

        const auto decision = policy.evaluate(
            utest::redirectpolicy::uri( "https://example.com/a" ),
            row.method,
            row.status,
            "https://example.com/b",
            true    /* isBodyReplayable */,
            0U      /* hopsSoFar */
            );

        UTF_CHECK_EQUAL(
            context + ( decision.shouldFollow() ? " follows" : " does not follow" ),
            context + " follows"
            );

        UTF_CHECK_EQUAL( context + " -> " + decision.method, context + " -> " + row.expectedMethod );

        UTF_CHECK_EQUAL(
            context + ( decision.dropBody.value() ? " drops the body" : " keeps the body" ),
            context + ( row.expectedDropBody ? " drops the body" : " keeps the body" )
            );
    }

    /*
     * The method token is case SENSITIVE (RFC 9110 section 9.1), so a lowercase "post" is not the
     * POST the 301/302 rule is about and is left alone
     */

    bool dropBody = true;

    UTF_CHECK_EQUAL(
        RedirectPolicy::rewriteMethod( 302U, "post", dropBody ),
        std::string( "post" )
        );

    UTF_CHECK( ! dropBody );
}

UTF_AUTO_TEST_CASE( RedirectPolicy_CredentialDropIsCrossOriginTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::redirectpolicy;

    const auto policy = enabled();

    /*
     * SECURITY PROPERTY. Without this, an open redirect on a site the caller authenticates to
     * hands the caller's Authorization header and cookies to whatever the redirect names. So the
     * rule is asserted from both sides: it must fire across an origin, and it must NOT fire within
     * one, because dropping credentials on a same-origin hop would break every authenticated
     * redirect there is
     */

    const auto sameOrigin = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( sameOrigin.shouldFollow() );
    UTF_CHECK( ! sameOrigin.isCrossOrigin.value() );
    UTF_CHECK( ! sameOrigin.dropCredentials.value() );
    UTF_CHECK_EQUAL( sameOrigin.target.toString(), std::string( "https://example.com/b" ) );

    /*
     * A different HOST is a different origin
     */

    const auto otherHost = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://evil.test/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( otherHost.shouldFollow() );
    UTF_CHECK( otherHost.isCrossOrigin.value() );
    UTF_CHECK( otherHost.dropCredentials.value() );

    /*
     * ... and so is a different PORT, and a SUBDOMAIN of the same site. Both are places a reader
     * might expect leniency; an origin has none
     */

    const auto otherPort = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://example.com:8443/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( otherPort.isCrossOrigin.value() );
    UTF_CHECK( otherPort.dropCredentials.value() );

    const auto subdomain = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://www.example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( subdomain.isCrossOrigin.value() );
    UTF_CHECK( subdomain.dropCredentials.value() );

    /*
     * ... while the SAME origin written differently is still the same origin: the default port
     * spelled out is the reason net::Uri::origin() renders the effective port
     */

    const auto spelledPort = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://example.com:443/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( ! spelledPort.isCrossOrigin.value() );
    UTF_CHECK( ! spelledPort.dropCredentials.value() );

    /*
     * And the surgery itself - the three headers which must not survive the hop. Cookies are
     * REMOVED rather than recomputed: the cookies of the new origin are the jar's to supply, and
     * carrying the old Cookie header across would send them to a host they were never scoped to
     */

    http::HeaderList headers;

    headers.append( "Host", "example.com" );
    headers.append( "Authorization", "Bearer secret-token" );
    headers.append( "Cookie", "sid=secret" );
    headers.append( "Proxy-Authorization", "Basic abc" );
    headers.append( "Accept", "*/*" );

    RedirectPolicy::dropCredentialHeaders( headers );

    UTF_CHECK( ! headers.has( "authorization" ) );
    UTF_CHECK( ! headers.has( "cookie" ) );
    UTF_CHECK( ! headers.has( "proxy-authorization" ) );

    UTF_CHECK( headers.has( "accept" ) );
    UTF_CHECK( headers.has( "host" ) );
    UTF_CHECK_EQUAL( headers.size(), 2U );

    /*
     * ... and the removal is case insensitive on the name, because a caller which spelled the
     * header differently must not keep its credentials by accident
     */

    http::HeaderList mixedCase;

    mixedCase.append( "AUTHORIZATION", "Bearer secret-token" );
    mixedCase.append( "cOoKiE", "sid=secret" );

    RedirectPolicy::dropCredentialHeaders( mixedCase );

    UTF_CHECK( mixedCase.empty() );
}

UTF_AUTO_TEST_CASE( RedirectPolicy_DowngradeAndSchemeRefusalTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::redirectpolicy;

    auto policy = enabled();

    /*
     * SECURITY PROPERTY. The caller chose https; the server does not get to undo that choice by
     * redirecting to http, which would put the rest of the exchange - including whatever the
     * caller sends next - on the wire in the clear
     */

    const auto downgrade = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "http://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( ! downgrade.shouldFollow() );
    UTF_CHECK( RedirectAction::Refused == downgrade.action.value() );
    UTF_CHECK( RedirectRefusal::ProtocolDowngrade == downgrade.refusal.value() );

    /*
     * ... and the target is still reported, so a caller can say where it refused to go
     */

    UTF_CHECK_EQUAL( downgrade.target.toString(), std::string( "http://example.com/b" ) );

    /*
     * The UPGRADE is always fine - http to https takes nothing away
     */

    const auto upgrade = policy.evaluate(
        uri( "http://example.com/a" ),
        "GET",
        302U,
        "https://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( upgrade.shouldFollow() );

    /*
     * ... and the refusal is a knob, not a law, because some deployments genuinely redirect down.
     * It is off by default, which is the half that matters
     */

    policy.allowHttpsToHttpDowngrade( true );

    const auto allowed = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "http://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( allowed.shouldFollow() );
    UTF_CHECK( allowed.isCrossOrigin.value() );
    UTF_CHECK( allowed.dropCredentials.value() );

    /*
     * A target whose scheme is neither http nor https is refused whatever the knobs say. A
     * redirect to file: or to a custom scheme is a standard way of turning a fetch into something
     * else entirely
     */

    const char* const foreignSchemes[] =
    {
        "file:///etc/passwd",
        "ftp://example.com/x",
        "gopher://example.com/x",
        "custom-scheme://example.com/x",
    };

    for( std::size_t index = 0U; index < ( sizeof( foreignSchemes ) / sizeof( foreignSchemes[ 0 ] ) ); ++index )
    {
        const auto refused = policy.evaluate(
            uri( "https://example.com/a" ),
            "GET",
            302U,
            foreignSchemes[ index ],
            true    /* isBodyReplayable */,
            0U      /* hopsSoFar */
            );

        const std::string context( foreignSchemes[ index ] );

        UTF_CHECK_EQUAL(
            context + ( refused.shouldFollow() ? " is followed" : " is refused" ),
            context + " is refused"
            );

        UTF_CHECK_EQUAL(
            context + " refusal " +
                std::to_string( static_cast< unsigned >( refused.refusal.value() ) ),
            context + " refusal " +
                std::to_string( static_cast< unsigned >( RedirectRefusal::UnsupportedScheme ) )
            );
    }
}

UTF_AUTO_TEST_CASE( RedirectPolicy_HopLimitAndBodyReplayTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::redirectpolicy;

    auto policy = enabled();

    policy.maxHops( 3U );

    for( std::size_t hops = 0U; hops < 3U; ++hops )
    {
        const auto decision = policy.evaluate(
            uri( "https://example.com/a" ),
            "GET",
            302U,
            "https://example.com/b",
            true    /* isBodyReplayable */,
            hops
            );

        UTF_CHECK( decision.shouldFollow() );
    }

    const auto tooMany = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "https://example.com/b",
        true    /* isBodyReplayable */,
        3U      /* hopsSoFar */
        );

    UTF_CHECK( ! tooMany.shouldFollow() );
    UTF_CHECK( RedirectRefusal::HopLimitExceeded == tooMany.refusal.value() );

    /*
     * ... and the target is resolved before the limit is checked, so a caller which stops here
     * still learns where it was being sent
     */

    UTF_CHECK_EQUAL( tooMany.target.toString(), std::string( "https://example.com/b" ) );

    /*
     * A 307 and a 308 keep the BODY as well as the method, so they need one which can be produced
     * a second time. Following without it would send the request with no body, or a truncated one,
     * which is worse than not following
     */

    const auto policyOn = enabled();

    const auto refusedBody = policyOn.evaluate(
        uri( "https://example.com/a" ),
        "POST",
        307U,
        "https://example.com/b",
        false   /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( ! refusedBody.shouldFollow() );
    UTF_CHECK( RedirectRefusal::BodyNotReplayable == refusedBody.refusal.value() );

    const auto refused308 = policyOn.evaluate(
        uri( "https://example.com/a" ),
        "PUT",
        308U,
        "https://example.com/b",
        false   /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( RedirectRefusal::BodyNotReplayable == refused308.refusal.value() );

    /*
     * ... while a 303 which rewrites a POST into a bodiless GET does NOT care whether the source
     * could rewind, because the body does not survive the hop at all. Checking replayability there
     * would refuse a redirect which is perfectly safe to follow
     */

    const auto rewritten = policyOn.evaluate(
        uri( "https://example.com/a" ),
        "POST",
        303U,
        "https://example.com/b",
        false   /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( rewritten.shouldFollow() );
    UTF_CHECK_EQUAL( rewritten.method, std::string( "GET" ) );
    UTF_CHECK( rewritten.dropBody.value() );

    /*
     * ... and the same for a POST on a 302
     */

    const auto rewritten302 = policyOn.evaluate(
        uri( "https://example.com/a" ),
        "POST",
        302U,
        "https://example.com/b",
        false   /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( rewritten302.shouldFollow() );
    UTF_CHECK_EQUAL( rewritten302.method, std::string( "GET" ) );

    /*
     * A GET has no body, so it is replayable by construction and a 307 on one follows
     */

    const auto get307 = policyOn.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        307U,
        "https://example.com/b",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( get307.shouldFollow() );
}

UTF_AUTO_TEST_CASE( RedirectPolicy_TargetResolutionAndLocationTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::redirectpolicy;

    const auto policy = enabled();

    /*
     * Targets resolve through net::Uri, which implements the strict RFC 3986 section 5.2.2
     * algorithm - so a relative Location resolves against the request URI, dot segments are
     * removed, and the query of the reference replaces the base's rather than being merged
     */

    struct Resolution
    {
        const char*                                                     base;
        const char*                                                     location;
        const char*                                                     expected;
    };

    static const Resolution resolutions[] =
    {
        { "https://example.com/a/b/c", "d",            "https://example.com/a/b/d" },
        { "https://example.com/a/b/c", "/d",           "https://example.com/d" },
        { "https://example.com/a/b/c", "../d",         "https://example.com/a/d" },
        { "https://example.com/a/b/c", "?q=1",         "https://example.com/a/b/c?q=1" },
        { "https://example.com/a?q=1", "b",            "https://example.com/b" },
        { "https://example.com/a",     "//other.test/x", "https://other.test/x" },
        { "https://example.com/a",     "https://other.test/x", "https://other.test/x" },
    };

    for( std::size_t index = 0U; index < ( sizeof( resolutions ) / sizeof( resolutions[ 0 ] ) ); ++index )
    {
        const auto& row = resolutions[ index ];

        const auto decision = policy.evaluate(
            uri( row.base ),
            "GET",
            302U,
            row.location,
            true    /* isBodyReplayable */,
            0U      /* hopsSoFar */
            );

        const std::string context( row.base );

        UTF_CHECK_EQUAL(
            context + " + " + row.location + " -> " +
                ( decision.shouldFollow() ? decision.target.toString() : std::string( "refused" ) ),
            context + " + " + row.location + " -> " + row.expected
            );
    }

    /*
     * A 3xx with no Location at all, and one whose Location is not a well formed reference, are
     * two different refusals - and NEITHER is an exception. A server must not be able to turn a
     * response into a thrown exception in the client
     */

    const auto noLocation = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        "",
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( RedirectRefusal::NoLocation == noLocation.refusal.value() );

    const char* const malformed[] =
    {
        "http://exa mple.com/",             /* a space is rejected rather than stripped */
        "http://example.com/\r\nX: 1",      /* a CRLF - the header injection shape */
        "http://[::1/",                     /* an unterminated IP literal */
        "http:/no-authority",               /* parses, but resolves to a target with no origin */
    };

    for( std::size_t index = 0U; index < ( sizeof( malformed ) / sizeof( malformed[ 0 ] ) ); ++index )
    {
        const auto decision = policy.evaluate(
            uri( "https://example.com/a" ),
            "GET",
            302U,
            malformed[ index ],
            true    /* isBodyReplayable */,
            0U      /* hopsSoFar */
            );

        const std::string context( malformed[ index ] );

        UTF_CHECK_EQUAL(
            context + " refusal " +
                std::to_string( static_cast< unsigned >( decision.refusal.value() ) ),
            context + " refusal " +
                std::to_string( static_cast< unsigned >( RedirectRefusal::MalformedLocation ) )
            );

        UTF_CHECK_EQUAL(
            context + ( decision.shouldFollow() ? " is followed" : " is refused" ),
            context + " is refused"
            );
    }

    /*
     * The header-list overload reads the Location itself, and refuses a response carrying TWO of
     * them rather than picking one. Which one a client picks is exactly the kind of difference
     * that lets one response be read two ways by two components
     */

    http::HeaderList single;
    single.append( "Location", "https://example.com/b" );

    const auto fromHeaders = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        single,
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( fromHeaders.shouldFollow() );
    UTF_CHECK_EQUAL( fromHeaders.target.toString(), std::string( "https://example.com/b" ) );

    http::HeaderList duplicated;
    duplicated.append( "Location", "https://example.com/b" );
    duplicated.append( "location", "https://evil.test/b" );

    const auto ambiguous = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        duplicated,
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( ! ambiguous.shouldFollow() );
    UTF_CHECK( RedirectRefusal::MalformedLocation == ambiguous.refusal.value() );

    http::HeaderList none;
    none.append( "Content-Type", "text/html" );

    const auto missing = policy.evaluate(
        uri( "https://example.com/a" ),
        "GET",
        302U,
        none,
        true    /* isBodyReplayable */,
        0U      /* hopsSoFar */
        );

    UTF_CHECK( RedirectRefusal::NoLocation == missing.refusal.value() );

    /*
     * The request URI is the base of the resolution, so it must be absolute - and the policy lets
     * net::Uri say so rather than resolving against nothing
     */

    UTF_CHECK_THROW(
        policy.evaluate(
            uri( "/relative/a" ),
            "GET",
            302U,
            "b",
            true    /* isBodyReplayable */,
            0U      /* hopsSoFar */
            ),
        ArgumentException
        );
}
