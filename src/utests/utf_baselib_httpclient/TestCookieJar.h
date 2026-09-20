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

#include <baselib/httpclient/CookieJar.h>

#include <baselib/core/Uri.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * Slice S2.7 - the RFC 6265 cookie jar
 *
 * The matching rules below are a SECURITY boundary and are tested as one. Each of the three
 * conditions of the domain-match rule, and the third condition of the path-match rule, exists to
 * stop a specific leak, so each is pinned from BOTH sides - the case it must accept and the case
 * it must refuse - rather than by a sample of matches
 *
 * Every case drives the jar with an explicit 'now' rather than the wall clock, so that expiry is a
 * deterministic assertion and not a sleep
 */

namespace utest
{
    namespace cookiejar
    {
        inline bl::time::ptime fixedNow()
        {
            /*
             * An arbitrary but fixed instant; every relative time below is expressed against it
             */

            return bl::time::ptime( bl::time::date( 2026, 9, 19 ), bl::time::hours( 12 ) );
        }

        inline bl::net::Uri uri( const std::string& text )
        {
            return bl::net::Uri::parse( text );
        }
    }

} // utest

UTF_AUTO_TEST_CASE( CookieJar_DomainMatchRuleTests )
{
    using namespace bl::httpclient;

    /*
     * RFC 6265 section 5.1.3 - all three conditions, each from both sides
     */

    /*
     * (1) identical strings
     */

    UTF_CHECK( CookieJar::domainMatches( "example.com", "example.com" ) );
    UTF_CHECK( CookieJar::domainMatches( "EXAMPLE.com", "example.COM" ) );

    /*
     * (2) the domain is a suffix of the host AND the character before it is a '.'
     *
     * The second half is the one that matters: without it "notexample.com" would match
     * "example.com", so a cookie set by example.com would be sent to an attacker's domain whose
     * name merely ends with it
     */

    UTF_CHECK( CookieJar::domainMatches( "www.example.com", "example.com" ) );
    UTF_CHECK( CookieJar::domainMatches( "a.b.example.com", "example.com" ) );

    UTF_CHECK( ! CookieJar::domainMatches( "notexample.com", "example.com" ) );
    UTF_CHECK( ! CookieJar::domainMatches( "badexample.com", "example.com" ) );

    /*
     * A domain longer than the host never matches, and neither does the other direction of the
     * suffix relation
     */

    UTF_CHECK( ! CookieJar::domainMatches( "example.com", "www.example.com" ) );
    UTF_CHECK( ! CookieJar::domainMatches( "example.com", "" ) );

    /*
     * (3) the host is not an IP address
     *
     * Without this, "1.2.3.4" would have "2.3.4" as a dot-preceded suffix, so a cookie scoped to
     * the name 2.3.4 would be sent to the address 1.2.3.4
     */

    UTF_CHECK( ! CookieJar::domainMatches( "1.2.3.4", "2.3.4" ) );
    UTF_CHECK( ! CookieJar::domainMatches( "192.168.0.1", "168.0.1" ) );

    /*
     * ... while an address still matches itself, which is the identical-strings case and is how a
     * host-only cookie on an address works at all
     */

    UTF_CHECK( CookieJar::domainMatches( "1.2.3.4", "1.2.3.4" ) );

    /*
     * An IPv6 literal reaches here without its brackets, because net::Uri::host() strips them
     */

    UTF_CHECK( CookieJar::isIpAddressLike( "::1" ) );
    UTF_CHECK( CookieJar::isIpAddressLike( "2001:db8::1" ) );
    UTF_CHECK( CookieJar::isIpAddressLike( "10.0.0.7" ) );
    UTF_CHECK( ! CookieJar::isIpAddressLike( "example.com" ) );
    UTF_CHECK( ! CookieJar::isIpAddressLike( "1.2.3.4a" ) );
}

UTF_AUTO_TEST_CASE( CookieJar_PathMatchAndDefaultPathTests )
{
    using namespace bl::httpclient;

    /*
     * RFC 6265 section 5.1.4
     */

    UTF_CHECK( CookieJar::pathMatches( "/a/b", "/a/b" ) );
    UTF_CHECK( CookieJar::pathMatches( "/a/b/c", "/a/b/" ) );
    UTF_CHECK( CookieJar::pathMatches( "/a/b/c", "/a/b" ) );
    UTF_CHECK( CookieJar::pathMatches( "/anything", "/" ) );

    /*
     * The third condition: a cookie path which does not end in '/' must be followed by a '/' in
     * the request path. Without it a cookie scoped to /admin would be sent to /administrator -
     * which on a shared host is one application reading another's session cookie
     */

    UTF_CHECK( ! CookieJar::pathMatches( "/administrator", "/admin" ) );
    UTF_CHECK( CookieJar::pathMatches( "/admin/users", "/admin" ) );

    UTF_CHECK( ! CookieJar::pathMatches( "/a", "/a/b" ) );
    UTF_CHECK( ! CookieJar::pathMatches( "/b/c", "/a" ) );

    /*
     * default-path: the request path up to but not including the rightmost '/'
     */

    UTF_CHECK_EQUAL( CookieJar::defaultPath( "/a/b/c" ), std::string( "/a/b" ) );
    UTF_CHECK_EQUAL( CookieJar::defaultPath( "/a/b/" ), std::string( "/a/b" ) );
    UTF_CHECK_EQUAL( CookieJar::defaultPath( "/index.html" ), std::string( "/" ) );
    UTF_CHECK_EQUAL( CookieJar::defaultPath( "/" ), std::string( "/" ) );
    UTF_CHECK_EQUAL( CookieJar::defaultPath( "" ), std::string( "/" ) );
    UTF_CHECK_EQUAL( CookieJar::defaultPath( "relative" ), std::string( "/" ) );
}

UTF_AUTO_TEST_CASE( CookieJar_DomainAttributeRejectionTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    CookieJar jar;

    const auto now = fixedNow();
    const auto request = uri( "https://www.example.com/app/page" );

    /*
     * A Domain attribute which does not domain-match the request host is refused outright. This is
     * the check which stops a page on one site setting a cookie for another
     */

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( request, "a=1; Domain=evil.test", true /* isHttpApi */, now )
        );

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( request, "a=1; Domain=other-example.com", true /* isHttpApi */, now )
        );

    /*
     * A bare TLD, and a Domain with no embedded dot. These stand in for a public suffix list -
     * incompletely, which is why the jar's header says so
     */

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( request, "a=1; Domain=com", true /* isHttpApi */, now )
        );

    /*
     * A single label the request host merely sits BENEATH is refused exactly as "com" is above -
     * "localhost" is to "app.localhost" what "com" is to "www.example.com". This is the refusal
     * which keeps RFC 6265 section 5.3 step 5's exception, pinned in
     * CookieJar_SingleLabelDomainAttributeTests below, from widening anything: the exception asks
     * for equality with the request host, and this is the nearest case which is not equal
     */

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( uri( "http://app.localhost/p" ), "a=1; Domain=localhost", true, now )
        );

    /*
     * An IP address is never a DOMAIN cookie, because an address has no hierarchy for one to span.
     * An address which is not the request host is refused outright - and an address has no parent
     * case either, so the nearest thing to one, a dot-preceded suffix of the host, is refused too.
     * The accepted counterpart, the address identical to the request host, is pinned in
     * CookieJar_AddressDomainAttributeTests below
     */

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( uri( "http://5.6.7.8/p" ), "a=1; Domain=1.2.3.4", true, now )
        );

    UTF_CHECK(
        CookieStoreResult::RejectedDomain ==
            jar.setCookie( uri( "http://1.2.3.4/p" ), "a=1; Domain=2.3.4", true, now )
        );

    UTF_CHECK_EQUAL( jar.size(), 0U );

    /*
     * What IS accepted: a parent domain of the request host, with or without the leading dot the
     * RFC says to ignore
     */

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( request, "a=1; Domain=example.com", true /* isHttpApi */, now )
        );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( request, "b=2; Domain=.example.com", true /* isHttpApi */, now )
        );

    const auto stored = jar.allCookies();

    UTF_CHECK_EQUAL( stored.size(), 2U );
    UTF_CHECK_EQUAL( stored[ 0 ].domain, std::string( "example.com" ) );
    UTF_CHECK_EQUAL( stored[ 1 ].domain, std::string( "example.com" ) );
    UTF_CHECK( ! stored[ 0 ].isHostOnly.value() );
    UTF_CHECK( ! stored[ 1 ].isHostOnly.value() );

    /*
     * THE DOCUMENTED RESIDUAL RISK, PINNED RATHER THAN HIDDEN. Without a public suffix list a
     * multi-label suffix has an embedded dot and domain-matches a host beneath it, so this IS
     * accepted and a later request to another site under co.uk DOES receive it. The case asserts
     * the gap so that closing it - which needs the Mozilla list as data - shows up here as a
     * deliberate change rather than as a surprise
     */

    CookieJar supercookie;

    UTF_CHECK(
        CookieStoreResult::Stored ==
            supercookie.setCookie(
                uri( "https://a.co.uk/p" ),
                "tracker=1; Domain=co.uk",
                true    /* isHttpApi */,
                now
                )
        );

    /*
     * The gap is a DOMAIN cookie and stays one. "co.uk" has an embedded dot, so it never reaches
     * section 5.3 step 5's exception at all - which is how this case shows that the exception did
     * not widen the residual risk: if it ever did, the flag below would go the other way and the
     * reach assertion after it would fail rather than pass
     */

    const auto supercookieStored = supercookie.allCookies();

    UTF_CHECK_EQUAL( supercookieStored.size(), 1U );
    UTF_CHECK( ! supercookieStored[ 0 ].isHostOnly.value() );

    UTF_CHECK_EQUAL(
        supercookie.cookieHeaderValue( uri( "https://b.co.uk/p" ), true /* isHttpApi */, now ),
        std::string( "tracker=1" )
        );

    /*
     * A malformed set-cookie-string is a different rejection, and the jar says which
     */

    UTF_CHECK(
        CookieStoreResult::RejectedMalformed ==
            jar.setCookie( request, "novalue", true /* isHttpApi */, now )
        );

    UTF_CHECK(
        CookieStoreResult::RejectedMalformed ==
            jar.setCookie( request, "  =1; Path=/", true /* isHttpApi */, now )
        );
}

UTF_AUTO_TEST_CASE( CookieJar_SingleLabelDomainAttributeTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    const auto now = fixedNow();

    /*
     * RFC 6265 section 5.3 step 5 - the exception to the dot test of the case above, and the ONLY
     * exception to it
     *
     * The dot test stands in for a public suffix list, and step 5's answer for an attribute which
     * IS a public suffix is not a flat rejection: an attribute identical to the canonicalized
     * request host becomes a host-only cookie, exactly as if no Domain attribute had been sent.
     * Without that, "Domain=localhost" on localhost - the ordinary local development case - loses
     * its cookies silently
     *
     * This is a security rule, so the boundary is pinned from both sides below: what equality with
     * the request host admits, and what it must still refuse
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "http://localhost/p" ), "a=1; Domain=localhost", true, now )
            );

        /*
         * The leading dot the RFC says to ignore, and the case fold, are both part of
         * "canonicalized" - so both of these are identical to the request host too
         */

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "http://localhost/p" ), "b=2; Domain=.localhost", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "http://localhost/p" ), "c=3; Domain=LOCALHOST", true, now )
            );

        const auto stored = jar.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 3U );

        for( std::size_t pos = 0U; pos < stored.size(); ++pos )
        {
            /*
             * HOST-ONLY, not a domain cookie - which is the whole of what step 5 grants
             */

            UTF_CHECK_EQUAL( stored[ pos ].domain, std::string( "localhost" ) );
            UTF_CHECK( stored[ pos ].isHostOnly.value() );
        }

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "http://localhost/p" ), true /* isHttpApi */, now ),
            std::string( "a=1; b=2; c=3" )
            );

        /*
         * ... and host-only is matched by equality on the request host, so nothing beneath
         * localhost and nothing beside it sees the cookies. This is the containment which makes
         * the exception safe rather than a hole in the dot test
         */

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "http://app.localhost/p" ), true /* isHttpApi */, now ),
            std::string()
            );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "http://otherhost/p" ), true /* isHttpApi */, now ),
            std::string()
            );
    }

    /*
     * THE PATHOLOGICAL SHAPE, PINNED FROM BOTH SIDES. "Domain=com" is the case the dot test was
     * written for, and a host literally named com satisfies "identical to the request host" just
     * as localhost does. The exception therefore admits it - and it has to be shown that this
     * grants nothing, because host-only scopes the cookie to that one name and a registry suffix
     * has no host-only reach at all
     */

    {
        CookieJar bareTld;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                bareTld.setCookie( uri( "http://com/p" ), "t=1; Domain=com", true, now )
            );

        const auto stored = bareTld.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 1U );
        UTF_CHECK_EQUAL( stored[ 0 ].domain, std::string( "com" ) );
        UTF_CHECK( stored[ 0 ].isHostOnly.value() );

        UTF_CHECK_EQUAL(
            bareTld.cookieHeaderValue( uri( "http://com/p" ), true /* isHttpApi */, now ),
            std::string( "t=1" )
            );

        /*
         * The scoping the dot test exists to stop. Neither of these is reached, which is what
         * separates "stored host-only" from "scoped to the TLD"
         */

        UTF_CHECK_EQUAL(
            bareTld.cookieHeaderValue( uri( "http://example.com/p" ), true /* isHttpApi */, now ),
            std::string()
            );

        UTF_CHECK_EQUAL(
            bareTld.cookieHeaderValue(
                uri( "http://www.example.com/p" ),
                true    /* isHttpApi */,
                now
                ),
            std::string()
            );

        /*
         * ... and the refusal side of the same pair: a page which merely SITS under com cannot set
         * it, because its attribute is not identical to its host. The two assertions together are
         * the boundary
         */

        UTF_CHECK(
            CookieStoreResult::RejectedDomain ==
                bareTld.setCookie( uri( "http://www.example.com/p" ), "t=2; Domain=com", true, now )
            );

        UTF_CHECK_EQUAL( bareTld.size(), 1U );
    }

    /*
     * EQUALITY ALONE IS NOT WHAT TRIGGERS THE EXCEPTION - failing the dot test is. A Domain
     * identical to a multi-label request host is an ordinary DOMAIN cookie, as section 5.3 step 6
     * says and as it always was here, and it still reaches hosts beneath it. If the fix had keyed
     * on equality instead, every "Domain=example.com" from example.com would silently have stopped
     * reaching www.example.com
     */

    {
        CookieJar multiLabel;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                multiLabel.setCookie(
                    uri( "https://example.com/p" ),
                    "s=1; Domain=example.com",
                    true    /* isHttpApi */,
                    now
                    )
            );

        const auto stored = multiLabel.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 1U );
        UTF_CHECK( ! stored[ 0 ].isHostOnly.value() );

        UTF_CHECK_EQUAL(
            multiLabel.cookieHeaderValue(
                uri( "https://www.example.com/p" ),
                true    /* isHttpApi */,
                now
                ),
            std::string( "s=1" )
            );
    }
}

UTF_AUTO_TEST_CASE( CookieJar_AddressDomainAttributeTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    const auto now = fixedNow();

    /*
     * The SAME exception, RFC 6265 section 5.3 step 5, applied to the other attribute this jar
     * refuses as a scope: an IP address
     *
     * An address is refused as a DOMAIN cookie because it has no hierarchy for one to span - but
     * the refusal is of the scope, not of the cookie, and step 5 stores an attribute identical to
     * the request host host-only instead. A client talking to a bare address used to lose its
     * cookies here for exactly the reason localhost did
     *
     * An address has no parent, so there is no "widening" direction to worry about at all; what
     * has to be pinned instead is SPELLING, because the comparison is over two ASCII-lowercased
     * strings and not over two parsed addresses
     */

    {
        CookieJar v4;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                v4.setCookie( uri( "http://1.2.3.4/p" ), "a=1; Domain=1.2.3.4", true, now )
            );

        const auto stored = v4.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 1U );
        UTF_CHECK_EQUAL( stored[ 0 ].domain, std::string( "1.2.3.4" ) );
        UTF_CHECK( stored[ 0 ].isHostOnly.value() );

        UTF_CHECK_EQUAL(
            v4.cookieHeaderValue( uri( "http://1.2.3.4/p" ), true /* isHttpApi */, now ),
            std::string( "a=1" )
            );

        /*
         * Returned to that address and to no other. domainMatches already refuses one address as a
         * suffix of another, so this is belt and braces over the store-side refusals in
         * CookieJar_DomainAttributeRejectionTests - and it is the property that actually matters
         */

        UTF_CHECK_EQUAL(
            v4.cookieHeaderValue( uri( "http://5.6.7.8/p" ), true /* isHttpApi */, now ),
            std::string()
            );

        UTF_CHECK_EQUAL(
            v4.cookieHeaderValue( uri( "http://1.2.3.40/p" ), true /* isHttpApi */, now ),
            std::string()
            );

        /*
         * SPELLING, REFUSED SIDE. "01.2.3.4" is the same address as "1.2.3.4" to inet_aton and a
         * different string here, so it is refused. That is the safe direction - an unfamiliar
         * spelling costs a cookie, it can never widen one - and it is pinned so that a later move
         * to parsed-address equality shows up as a deliberate change
         */

        UTF_CHECK(
            CookieStoreResult::RejectedDomain ==
                v4.setCookie( uri( "http://1.2.3.4/p" ), "b=2; Domain=01.2.3.4", true, now )
            );

        UTF_CHECK_EQUAL( v4.size(), 1U );
    }

    /*
     * IPv6, where net::Uri::host() hands over the literal with its brackets removed - so the
     * attribute which can be identical to it is the UNBRACKETED one
     */

    {
        CookieJar v6;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                v6.setCookie( uri( "http://[::1]/p" ), "a=1; Domain=::1", true, now )
            );

        const auto stored = v6.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 1U );
        UTF_CHECK_EQUAL( stored[ 0 ].domain, std::string( "::1" ) );
        UTF_CHECK( stored[ 0 ].isHostOnly.value() );

        UTF_CHECK_EQUAL(
            v6.cookieHeaderValue( uri( "http://[::1]/p" ), true /* isHttpApi */, now ),
            std::string( "a=1" )
            );

        /*
         * SPELLING, REFUSED SIDE, twice. The expanded form of the same address is a different
         * string, and so is the bracketed form of it - brackets are URI syntax and never reach
         * host()
         */

        UTF_CHECK(
            CookieStoreResult::RejectedDomain ==
                v6.setCookie( uri( "http://[::1]/p" ), "b=2; Domain=0:0:0:0:0:0:0:1", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::RejectedDomain ==
                v6.setCookie( uri( "http://[::1]/p" ), "c=3; Domain=[::1]", true, now )
            );

        UTF_CHECK_EQUAL( v6.size(), 1U );
    }

    /*
     * SPELLING, RECONCILED SIDE - the one difference that IS absorbed. Hex case in an IPv6 literal
     * is folded on both sides, by net::Uri on the host and by the jar's own ASCII lower on the
     * attribute, so these two spellings ARE identical by the time they are compared
     */

    {
        CookieJar v6case;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                v6case.setCookie(
                    uri( "http://[2001:DB8::1]/p" ),
                    "a=1; Domain=2001:db8::1",
                    true    /* isHttpApi */,
                    now
                    )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                v6case.setCookie(
                    uri( "http://[2001:db8::1]/p" ),
                    "b=2; Domain=2001:DB8::1",
                    true    /* isHttpApi */,
                    now
                    )
            );

        const auto stored = v6case.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 2U );

        for( std::size_t pos = 0U; pos < stored.size(); ++pos )
        {
            UTF_CHECK_EQUAL( stored[ pos ].domain, std::string( "2001:db8::1" ) );
            UTF_CHECK( stored[ pos ].isHostOnly.value() );
        }

        UTF_CHECK_EQUAL(
            v6case.cookieHeaderValue( uri( "http://[2001:db8::1]/p" ), true, now ),
            std::string( "a=1; b=2" )
            );
    }
}

UTF_AUTO_TEST_CASE( CookieJar_SecureAndHttpOnlyTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    CookieJar jar;

    const auto now = fixedNow();

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( uri( "https://example.com/" ), "sid=secret; Secure", true, now )
        );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( uri( "https://example.com/" ), "plain=ok", true, now )
        );

    /*
     * A Secure cookie is never sent over a scheme which is not secure. This is what stops an
     * active attacker on the plaintext channel from learning a session cookie the site only ever
     * set over TLS
     */

    UTF_CHECK_EQUAL(
        jar.cookieHeaderValue( uri( "http://example.com/" ), true /* isHttpApi */, now ),
        std::string( "plain=ok" )
        );

    UTF_CHECK_EQUAL(
        jar.cookieHeaderValue( uri( "https://example.com/" ), true /* isHttpApi */, now ),
        std::string( "sid=secret; plain=ok" )
        );

    /*
     * HttpOnly, from both sides of RFC 6265 - a non-HTTP API may neither set one (section 5.3
     * step 10) nor read one (section 5.4 step 1)
     */

    CookieJar httpOnlyJar;

    UTF_CHECK(
        CookieStoreResult::RejectedHttpOnly ==
            httpOnlyJar.setCookie(
                uri( "https://example.com/" ),
                "sid=secret; HttpOnly",
                false   /* isHttpApi */,
                now
                )
        );

    UTF_CHECK_EQUAL( httpOnlyJar.size(), 0U );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            httpOnlyJar.setCookie(
                uri( "https://example.com/" ),
                "sid=secret; HttpOnly",
                true    /* isHttpApi */,
                now
                )
        );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            httpOnlyJar.setCookie( uri( "https://example.com/" ), "visible=1", true, now )
        );

    UTF_CHECK_EQUAL(
        httpOnlyJar.cookieHeaderValue( uri( "https://example.com/" ), true /* isHttpApi */, now ),
        std::string( "sid=secret; visible=1" )
        );

    UTF_CHECK_EQUAL(
        httpOnlyJar.cookieHeaderValue( uri( "https://example.com/" ), false /* isHttpApi */, now ),
        std::string( "visible=1" )
        );

    /*
     * The attribute names are case insensitive, as section 5.2 requires
     */

    CookieJar caseJar;

    UTF_CHECK(
        CookieStoreResult::Stored ==
            caseJar.setCookie( uri( "https://example.com/" ), "a=1; sEcUrE; hTtPoNlY", true, now )
        );

    const auto stored = caseJar.allCookies();

    UTF_CHECK_EQUAL( stored.size(), 1U );
    UTF_CHECK( stored[ 0 ].isSecure.value() );
    UTF_CHECK( stored[ 0 ].isHttpOnly.value() );
}

UTF_AUTO_TEST_CASE( CookieJar_ExpiryAndMaxAgePrecedenceTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    const auto now = fixedNow();
    const auto request = uri( "https://example.com/" );

    /*
     * The cookie-date algorithm of RFC 6265 section 5.1.1 is delimiter based rather than format
     * based, so it accepts the IMF-fixdate a modern server sends AND the two obsolete formats
     * RFC 9110 still lists - all three naming the same instant
     */

    time::ptime parsed;

    UTF_CHECK( CookieJar::tryParseCookieDate( "Sun, 06 Nov 1994 08:49:37 GMT", parsed ) );
    UTF_CHECK_EQUAL(
        time::to_iso_string( parsed ),
        std::string( "19941106T084937" )
        );

    UTF_CHECK( CookieJar::tryParseCookieDate( "Sunday, 06-Nov-94 08:49:37 GMT", parsed ) );
    UTF_CHECK_EQUAL( time::to_iso_string( parsed ), std::string( "19941106T084937" ) );

    UTF_CHECK( CookieJar::tryParseCookieDate( "Sun Nov  6 08:49:37 1994", parsed ) );
    UTF_CHECK_EQUAL( time::to_iso_string( parsed ), std::string( "19941106T084937" ) );

    /*
     * ... and refuses what it cannot make an instant of, rather than inventing one
     */

    UTF_CHECK( ! CookieJar::tryParseCookieDate( "not a date at all", parsed ) );
    UTF_CHECK( ! CookieJar::tryParseCookieDate( "Sun, 06 Nov 1994", parsed ) );
    UTF_CHECK( ! CookieJar::tryParseCookieDate( "Wed, 31 Feb 2030 00:00:00 GMT", parsed ) );
    UTF_CHECK( ! CookieJar::tryParseCookieDate( "Sun, 06 Nov 1994 99:49:37 GMT", parsed ) );

    /*
     * MAX-AGE TAKES PRECEDENCE OVER EXPIRES when both are present (section 5.3 step 3). Getting
     * this the wrong way round is how a cookie a server meant to DELETE survives, because the
     * deletion idiom is Max-Age=0 alongside an Expires the server left in place
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( request, "a=1", true /* isHttpApi */, now )
            );

        UTF_CHECK_EQUAL( jar.size(), 1U );

        UTF_CHECK(
            CookieStoreResult::Removed ==
                jar.setCookie(
                    request,
                    "a=1; Expires=Sun, 06 Nov 2094 08:49:37 GMT; Max-Age=0",
                    true    /* isHttpApi */,
                    now
                    )
            );

        UTF_CHECK_EQUAL( jar.size(), 0U );
    }

    /*
     * ... and the other direction: a Max-Age which is still in the future keeps a cookie whose
     * Expires is long past
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    request,
                    "a=1; Expires=Sun, 06 Nov 1994 08:49:37 GMT; Max-Age=3600",
                    true    /* isHttpApi */,
                    now
                    )
            );

        UTF_CHECK_EQUAL( jar.size(), 1U );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( request, true /* isHttpApi */, now + time::minutes( 30 ) ),
            std::string( "a=1" )
            );

        /*
         * ... and it goes when its Max-Age runs out
         */

        UTF_CHECK(
            jar.cookieHeaderValue( request, true /* isHttpApi */, now + time::hours( 2 ) ).empty()
            );

        UTF_CHECK_EQUAL( jar.size(), 0U );
    }

    /*
     * A negative Max-Age is a deletion, exactly like zero
     */

    {
        CookieJar jar;

        UTF_CHECK( CookieStoreResult::Stored == jar.setCookie( request, "a=1", true, now ) );

        UTF_CHECK(
            CookieStoreResult::Removed ==
                jar.setCookie( request, "a=1; Max-Age=-1", true /* isHttpApi */, now )
            );

        UTF_CHECK_EQUAL( jar.size(), 0U );
    }

    /*
     * An Expires which does not parse, and a Max-Age which is not a number, are IGNORED rather
     * than making the cookie invalid (sections 5.2.1 and 5.2.2) - so the cookie stays a session
     * cookie rather than being dropped
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( request, "a=1; Expires=tomorrow; Max-Age=soon", true, now )
            );

        const auto stored = jar.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 1U );
        UTF_CHECK( stored[ 0 ].isSessionCookie() );
    }

    /*
     * A Max-Age far larger than a ptime can hold is clamped rather than wrapped. An overflow here
     * would push the expiry into the PAST and delete the cookie instead of keeping it forever
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( request, "a=1; Max-Age=999999999999999999", true, now )
            );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( request, true /* isHttpApi */, now + time::hours( 24 ) ),
            std::string( "a=1" )
            );
    }

    /*
     * An Expires in the past deletes, and one in the future stores
     */

    {
        CookieJar jar;

        UTF_CHECK( CookieStoreResult::Stored == jar.setCookie( request, "a=1", true, now ) );

        UTF_CHECK(
            CookieStoreResult::Removed ==
                jar.setCookie(
                    request,
                    "a=1; Expires=Sun, 06 Nov 1994 08:49:37 GMT",
                    true    /* isHttpApi */,
                    now
                    )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    request,
                    "b=2; Expires=Sun, 06 Nov 2094 08:49:37 GMT",
                    true    /* isHttpApi */,
                    now
                    )
            );

        UTF_CHECK_EQUAL( jar.size(), 1U );
    }
}

UTF_AUTO_TEST_CASE( CookieJar_HostOnlyStorageAndOrderTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    const auto now = fixedNow();

    /*
     * No Domain attribute means a HOST-ONLY cookie, which matches the request host exactly and is
     * NOT sent to a subdomain - the difference between the two is the whole reason the flag exists
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "host=1", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "dom=2; Domain=example.com", true, now )
            );

        const auto stored = jar.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 2U );
        UTF_CHECK( stored[ 0 ].isHostOnly.value() );
        UTF_CHECK( ! stored[ 1 ].isHostOnly.value() );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://example.com/" ), true, now ),
            std::string( "host=1; dom=2" )
            );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://www.example.com/" ), true, now ),
            std::string( "dom=2" )
            );
    }

    /*
     * The Cookie header order of section 5.4 step 2: longer paths first, then by creation time.
     * A server which reads only the first occurrence of a name has to see the most specific one
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "a=root; Path=/", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    uri( "https://example.com/deep/page" ),
                    "a=deep; Path=/deep",
                    true    /* isHttpApi */,
                    now + time::seconds( 1 )
                    )
            );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://example.com/deep/page" ), true, now + time::seconds( 2 ) ),
            std::string( "a=deep; a=root" )
            );

        /*
         * ... and the shallow one alone where the deep path does not match
         */

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://example.com/other" ), true, now + time::seconds( 2 ) ),
            std::string( "a=root" )
            );
    }

    /*
     * A cookie of the same name, domain and path REPLACES the old one and keeps the original
     * creation time (section 5.3 step 11), which is what keeps the header order stable when a site
     * refreshes a cookie on every response
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "a=first; Path=/", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    uri( "https://example.com/" ),
                    "b=second; Path=/",
                    true    /* isHttpApi */,
                    now + time::seconds( 1 )
                    )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    uri( "https://example.com/" ),
                    "a=refreshed; Path=/",
                    true    /* isHttpApi */,
                    now + time::seconds( 2 )
                    )
            );

        UTF_CHECK_EQUAL( jar.size(), 2U );

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://example.com/" ), true, now + time::seconds( 3 ) ),
            std::string( "a=refreshed; b=second" )
            );

        const auto stored = jar.allCookies();

        UTF_CHECK_EQUAL( stored.size(), 2U );
        UTF_CHECK( stored[ 0 ].creationTime == now );
    }

    /*
     * Two cookies of one name in DIFFERENT scopes are two cookies, not a replacement - and this
     * case is where RFC 6265 and RFC 6265bis disagree, so it pins which one the jar follows
     *
     * Both cookies below end up with domain "example.com"; they differ only in the host-only
     * flag. RFC 6265 section 5.3 step 11 keys replacement on name, domain and path alone, which
     * would make the second REPLACE the first and silently widen the scope from one host to every
     * host beneath it. RFC 6265bis section 5.6 adds the flag to the key, browsers follow bis, and
     * so does this jar - the reason is written next to indexOf( ... )
     */

    {
        CookieJar jar;

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "a=host", true, now )
            );

        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie( uri( "https://example.com/" ), "a=domain; Domain=example.com", true, now )
            );

        UTF_CHECK_EQUAL( jar.size(), 2U );

        const auto stored = jar.allCookies();

        UTF_CHECK( stored[ 0 ].isHostOnly.value() );
        UTF_CHECK( ! stored[ 1 ].isHostOnly.value() );

        /*
         * ... and a request to the host itself receives both, which is what a browser sends
         */

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://example.com/" ), true, now + time::seconds( 1 ) ),
            std::string( "a=host; a=domain" )
            );

        /*
         * ... while a subdomain receives only the domain-scoped one
         */

        UTF_CHECK_EQUAL(
            jar.cookieHeaderValue( uri( "https://www.example.com/" ), true, now + time::seconds( 1 ) ),
            std::string( "a=domain" )
            );
    }
}

UTF_AUTO_TEST_CASE( CookieJar_CapsAndEvictionTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    const auto now = fixedNow();

    /*
     * The per-domain cap. Eviction is of the LEAST RECENTLY USED and not of the newest: the
     * alternative would let a flood of junk cookies push out the session cookie just set
     */

    CookieJar jar;

    jar.maxCookiesPerDomain( 3U );
    jar.maxCookiesTotal( 5U );

    UTF_CHECK_EQUAL( jar.maxCookiesPerDomain(), 3U );

    for( unsigned index = 0U; index < 3U; ++index )
    {
        UTF_CHECK(
            CookieStoreResult::Stored ==
                jar.setCookie(
                    uri( "https://example.com/" ),
                    "c" + std::to_string( index ) + "=" + std::to_string( index ),
                    true    /* isHttpApi */,
                    now + time::seconds( static_cast< long >( index ) )
                    )
            );
    }

    UTF_CHECK_EQUAL( jar.size(), 3U );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( uri( "https://example.com/" ), "c3=3", true, now + time::seconds( 3 ) )
        );

    /*
     * Still three, and the one that went is c0 - the oldest access
     */

    UTF_CHECK_EQUAL( jar.size(), 3U );

    const auto header = jar.cookieHeaderValue( uri( "https://example.com/" ), true, now + time::seconds( 4 ) );

    UTF_CHECK( std::string::npos == header.find( "c0=" ) );
    UTF_CHECK( std::string::npos != header.find( "c3=" ) );

    /*
     * The total cap is separate and applies across domains
     */

    CookieJar total;

    total.maxCookiesPerDomain( 100U );
    total.maxCookiesTotal( 4U );

    for( unsigned index = 0U; index < 8U; ++index )
    {
        const auto host = "https://s" + std::to_string( index ) + ".example.com/";

        UTF_CHECK(
            CookieStoreResult::Stored ==
                total.setCookie(
                    uri( host ),
                    "a=" + std::to_string( index ),
                    true    /* isHttpApi */,
                    now + time::seconds( static_cast< long >( index ) )
                    )
            );
    }

    UTF_CHECK_EQUAL( total.size(), 4U );

    /*
     * Expired cookies go before any live one is evicted
     */

    CookieJar expiring;

    expiring.maxCookiesPerDomain( 2U );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            expiring.setCookie( uri( "https://example.com/" ), "short=1; Max-Age=10", true, now )
        );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            expiring.setCookie( uri( "https://example.com/" ), "keep=2", true, now )
        );

    UTF_CHECK(
        CookieStoreResult::Stored ==
            expiring.setCookie(
                uri( "https://example.com/" ),
                "fresh=3",
                true    /* isHttpApi */,
                now + time::seconds( 30 )
                )
        );

    UTF_CHECK_EQUAL( expiring.size(), 2U );

    const auto remaining = expiring.cookieHeaderValue(
        uri( "https://example.com/" ),
        true    /* isHttpApi */,
        now + time::seconds( 31 )
        );

    UTF_CHECK( std::string::npos == remaining.find( "short=" ) );
    UTF_CHECK( std::string::npos != remaining.find( "keep=" ) );
    UTF_CHECK( std::string::npos != remaining.find( "fresh=" ) );

    /*
     * removeExpired reports what it dropped
     */

    CookieJar sweeping;

    UTF_CHECK(
        CookieStoreResult::Stored ==
            sweeping.setCookie( uri( "https://example.com/" ), "a=1; Max-Age=10", true, now )
        );

    UTF_CHECK( CookieStoreResult::Stored == sweeping.setCookie( uri( "https://example.com/" ), "b=2", true, now ) );

    UTF_CHECK_EQUAL( sweeping.removeExpired( now + time::seconds( 5 ) ), 0U );
    UTF_CHECK_EQUAL( sweeping.removeExpired( now + time::seconds( 20 ) ), 1U );
    UTF_CHECK_EQUAL( sweeping.size(), 1U );

    sweeping.clear();
    UTF_CHECK_EQUAL( sweeping.size(), 0U );
}

UTF_AUTO_TEST_CASE( CookieJar_RelativeRequestUriIsRefusedTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    CookieJar jar;

    const auto now = fixedNow();

    /*
     * The scope of a cookie comes from net::Uri::origin() and from its REFUSAL. A relative
     * reference and a URI with no host are exactly the two cases which would otherwise scope a
     * cookie to the empty host - which domain-matches every other empty-host entry, i.e. hands one
     * site's cookies to another. The jar lets origin() refuse rather than hand-rolling the scope
     * from scheme(), host() and path()
     */

    UTF_CHECK_THROW( jar.setCookie( uri( "/app/page" ), "a=1", true, now ), ArgumentException );
    UTF_CHECK_THROW( jar.setCookie( uri( "//example.com/p" ), "a=1", true, now ), ArgumentException );
    UTF_CHECK_THROW( jar.setCookie( uri( "example.com/p" ), "a=1", true, now ), ArgumentException );

    UTF_CHECK_THROW( jar.cookiesForRequest( uri( "/app/page" ), true, now ), ArgumentException );
    UTF_CHECK_THROW( jar.cookieHeaderValue( uri( "/app/page" ), true, now ), ArgumentException );

    UTF_CHECK_EQUAL( jar.size(), 0U );

    /*
     * ... and the same URI as an absolute one works, so the refusal is about the missing origin
     * and not about the path
     */

    UTF_CHECK(
        CookieStoreResult::Stored ==
            jar.setCookie( uri( "https://example.com/app/page" ), "a=1", true, now )
        );

    UTF_CHECK_EQUAL(
        jar.cookieHeaderValue( uri( "https://example.com/app/page" ), true, now ),
        std::string( "a=1" )
        );

    /*
     * The default path came from the request path, so a sibling directory does not get it
     */

    UTF_CHECK( jar.cookieHeaderValue( uri( "https://example.com/other/page" ), true, now ).empty() );
}

UTF_AUTO_TEST_CASE( CookieJar_ThreadSafetyTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::cookiejar;

    /*
     * The jar is documented as thread safe and is shared by every request of a session, so the
     * claim is exercised rather than asserted: four threads, two writing and two reading, over one
     * jar. What this catches is an unguarded container mutation; it is not a proof of the absence
     * of races, which is what the TSan runs are for
     */

    CookieJar jar;

    jar.maxCookiesPerDomain( 1000U );

    const auto now = fixedNow();

    const std::size_t iterations = 200U;

    const auto writer = [ iterations, &jar, &now ]( const unsigned id ) -> void
    {
        for( std::size_t index = 0U; index < iterations; ++index )
        {
            ( void ) jar.setCookie(
                uri( "https://example.com/" ),
                "w" + std::to_string( id ) + "_" + std::to_string( index % 16U ) + "=1",
                true    /* isHttpApi */,
                now
                );
        }
    };

    const auto reader = [ iterations, &jar, &now ]() -> void
    {
        for( std::size_t index = 0U; index < iterations; ++index )
        {
            ( void ) jar.cookieHeaderValue( uri( "https://example.com/" ), true, now );
            ( void ) jar.size();
        }
    };

    os::thread w1( [ &writer ]() -> void { writer( 1U ); } );
    os::thread w2( [ &writer ]() -> void { writer( 2U ); } );
    os::thread r1( [ &reader ]() -> void { reader(); } );
    os::thread r2( [ &reader ]() -> void { reader(); } );

    w1.join();
    w2.join();
    r1.join();
    r2.join();

    /*
     * Each writer uses 16 distinct names, so the jar ends with exactly the 32 of them
     */

    UTF_CHECK_EQUAL( jar.size(), 32U );
}
