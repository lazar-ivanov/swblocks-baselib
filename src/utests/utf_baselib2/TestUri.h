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

#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <string>

#include <utests/baselib/Utf.h>

/*
 * The expectations in these cases are taken from RFC 3986 itself and not from the parser -
 * the resolution tables below are transcribed verbatim from sections 5.4.1 and 5.4.2, so
 * they hold whatever the implementation chose to do internally and would still hold if the
 * parser were replaced by a different one ( which is the stated reason for keeping the
 * public API of bl::net::Uri backend neutral )
 */

UTF_AUTO_TEST_CASE( Uri_ParseComponentsTests )
{
    {
        const auto uri = bl::net::Uri::parse( "https://Bob:s3cret@WWW.Example.COM:8443/a/b?x=1&y=2#frag" );

        UTF_CHECK_EQUAL( uri.scheme(), std::string( "https" ) );
        UTF_CHECK_EQUAL( uri.userInfo(), std::string( "Bob:s3cret" ) );
        UTF_CHECK_EQUAL( uri.host(), std::string( "www.example.com" ) );
        UTF_CHECK_EQUAL( uri.path(), std::string( "/a/b" ) );
        UTF_CHECK_EQUAL( uri.query(), std::string( "x=1&y=2" ) );
        UTF_CHECK_EQUAL( uri.fragment(), std::string( "frag" ) );

        UTF_CHECK_EQUAL( uri.port(), 8443 );
        UTF_CHECK_EQUAL( uri.effectivePort(), 8443 );

        UTF_CHECK( uri.isAbsolute() );
        UTF_CHECK( uri.hasScheme() );
        UTF_CHECK( uri.hasAuthority() );
        UTF_CHECK( uri.hasUserInfo() );
        UTF_CHECK( uri.hasPort() );
        UTF_CHECK( uri.hasQuery() );
        UTF_CHECK( uri.hasFragment() );
        UTF_CHECK( ! uri.isIpLiteral() );
    }

    /*
     * The five shapes of RFC 3986 section 3 which are not the common one - a relative
     * reference with a path only, a network-path reference, a query only reference, a
     * fragment only reference and a path-rootless URI
     */

    {
        const auto uri = bl::net::Uri::parse( "a/b:c" );

        UTF_CHECK( ! uri.isAbsolute() );
        UTF_CHECK( ! uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.path(), std::string( "a/b:c" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "//host/x" );

        UTF_CHECK( ! uri.isAbsolute() );
        UTF_CHECK( uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.host(), std::string( "host" ) );
        UTF_CHECK_EQUAL( uri.path(), std::string( "/x" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "?q" );

        UTF_CHECK( uri.hasQuery() );
        UTF_CHECK( ! uri.hasFragment() );
        UTF_CHECK( uri.path().empty() );
        UTF_CHECK_EQUAL( uri.query(), std::string( "q" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "#f" );

        UTF_CHECK( ! uri.hasQuery() );
        UTF_CHECK( uri.hasFragment() );
        UTF_CHECK( uri.path().empty() );
        UTF_CHECK_EQUAL( uri.fragment(), std::string( "f" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "urn:isbn:0451450523" );

        UTF_CHECK( uri.isAbsolute() );
        UTF_CHECK( ! uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.scheme(), std::string( "urn" ) );
        UTF_CHECK_EQUAL( uri.path(), std::string( "isbn:0451450523" ) );
    }

    /*
     * An empty reference is a valid relative reference with nothing in it, and an empty
     * authority is valid too - "file:///etc/hosts" is the shape which proves it
     */

    {
        const auto uri = bl::net::Uri::parse( "" );

        UTF_CHECK( ! uri.hasScheme() );
        UTF_CHECK( ! uri.hasAuthority() );
        UTF_CHECK( ! uri.hasQuery() );
        UTF_CHECK( ! uri.hasFragment() );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "file:///etc/hosts" );

        UTF_CHECK( uri.hasAuthority() );
        UTF_CHECK( uri.host().empty() );
        UTF_CHECK_EQUAL( uri.path(), std::string( "/etc/hosts" ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "file:///etc/hosts" ) );
    }

    /*
     * An empty query and an empty fragment are each distinct from an absent one, which is
     * what section 5.2.2 keys on when it decides whether to inherit the base query
     */

    {
        const auto uri = bl::net::Uri::parse( "http://h/?" );

        UTF_CHECK( uri.hasQuery() );
        UTF_CHECK( uri.query().empty() );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://h/?" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "http://h/#" );

        UTF_CHECK( uri.hasFragment() );
        UTF_CHECK( uri.fragment().empty() );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://h/#" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "http://@host/" );

        UTF_CHECK( uri.hasUserInfo() );
        UTF_CHECK( uri.userInfo().empty() );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://@host/" ) );
    }

    /*
     * tryParse( ... ) is the shape a redirect needs - a Location header which cannot be
     * parsed is data rather than a defect, and the output must be left alone
     */

    {
        bl::net::Uri uri;

        UTF_CHECK( bl::net::Uri::tryParse( "http://h/x", uri ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://h/x" ) );

        UTF_CHECK( ! bl::net::Uri::tryParse( "http://h/ x", uri ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://h/x" ) );
    }
}

UTF_AUTO_TEST_CASE( Uri_NormalizationTests )
{
    const auto chkParse = [](
        const std::string&                                  text,
        const std::string&                                  expected
        ) -> void
    {
        UTF_CHECK_EQUAL( bl::net::Uri::parse( text ).toString(), expected );
    };

    /*
     * Section 6.2.2.1 - the scheme and the host are case insensitive and normalize to lower
     * case; nothing else does, and in particular the path does not
     */

    chkParse( "HTTP://Example.COM/A/B", "http://example.com/A/B" );
    chkParse( "HtTpS://EXAMPLE.com/", "https://example.com/" );

    /*
     * Section 6.2.2.2 - a percent-encoded octet which stands for an unreserved character is
     * decoded, and the hexadecimal digits of the ones which stay encoded are upper-cased.
     * The encoded form of a non-ASCII byte is how UTF-8 travels in a URI and must survive,
     * and so must %00
     */

    chkParse( "http://h/%7e%41", "http://h/~A" );
    chkParse( "http://h/a%3fb", "http://h/a%3Fb" );
    chkParse( "http://h/%c3%a4", "http://h/%C3%A4" );
    chkParse( "http://h/%00", "http://h/%00" );
    chkParse( "http://EXAMPLE%2ECOM/", "http://example.com/" );
    chkParse( "http://h/?a%7eb", "http://h/?a~b" );
    chkParse( "http://h/#a%7eb", "http://h/#a~b" );

    /*
     * Section 6.2.2.3 - dot segments are removed from a path which belongs to a URI. Note
     * that "%2E" decodes to "." first, so it is a dot segment as well, which is also what
     * the WHATWG URL specification requires of a browser
     */

    chkParse( "http://h/a/b/../c", "http://h/a/c" );
    chkParse( "http://h/a/./b", "http://h/a/b" );
    chkParse( "http://h/../a", "http://h/a" );
    chkParse( "http://h/%2e%2e/x", "http://h/x" );
    chkParse( "http://h/a/..", "http://h/" );

    /*
     * A relative reference keeps its dot segments - they are meaningful there, and section
     * 5.2.4 removes them at resolution time instead. Without this the resolution table in
     * Uri_Rfc3986NormalResolutionTests would resolve "../g" as if it were "g"
     */

    chkParse( "../g", "../g" );
    chkParse( "./a/../b", "./a/../b" );

    /*
     * An empty port is equivalent to no port at all and normalizes away; a port with leading
     * zeros normalizes to its value
     */

    chkParse( "http://h:/x", "http://h/x" );
    chkParse( "http://h:080/x", "http://h:80/x" );
    chkParse( "http://[::1]:/x", "http://[::1]/x" );

    /*
     * The scheme based normalization of section 6.2.3 is deliberately NOT applied - an empty
     * path stays empty and a port which spells out the scheme default is kept, so that
     * toString( ... ) gives back what the caller passed in
     */

    chkParse( "http://h", "http://h" );
    chkParse( "http://h:80/", "http://h:80/" );
}

UTF_AUTO_TEST_CASE( Uri_IpLiteralTests )
{
    /*
     * tryParse( ... ) swallows InvalidDataFormatException and nothing else, so a false here
     * is already an assertion that the reference was rejected as malformed. The literal is
     * passed at the call site rather than through a loop so that the text of the assertion
     * which fails names the reference which caused it
     */

    const auto isAccepted = []( const std::string& text ) -> bool
    {
        bl::net::Uri uri;

        return bl::net::Uri::tryParse( text, uri );
    };

    /*
     * The IPv6address rule of RFC 3986 section 3.2.2 - eight 16 bit groups, at most one
     * "::" standing for one or more groups of zeros, and an optional trailing dotted-quad
     * which stands for the last two groups
     */

    UTF_CHECK( isAccepted( "http://[::]/" ) );
    UTF_CHECK( isAccepted( "http://[::1]/" ) );
    UTF_CHECK( isAccepted( "http://[1:2:3:4:5:6:7:8]/" ) );
    UTF_CHECK( isAccepted( "http://[1:2:3:4:5:6:7::]/" ) );
    UTF_CHECK( isAccepted( "http://[2001:db8::]/" ) );
    UTF_CHECK( isAccepted( "http://[::ffff:192.0.2.128]/" ) );
    UTF_CHECK( isAccepted( "http://[64:ff9b::192.0.2.33]/" ) );
    UTF_CHECK( isAccepted( "http://[1:2:3:4:5:6:1.2.3.4]/" ) );

    UTF_CHECK( ! isAccepted( "http://[1:2:3:4:5:6:7]/" ) );
    UTF_CHECK( ! isAccepted( "http://[1:2:3:4:5:6:7:8:9]/" ) );
    UTF_CHECK( ! isAccepted( "http://[1:2:3:4:5:6:7:8::]/" ) );
    UTF_CHECK( ! isAccepted( "http://[1::2::3]/" ) );
    UTF_CHECK( ! isAccepted( "http://[1:2:3:4:5:6:7:1.2.3.4]/" ) );
    UTF_CHECK( ! isAccepted( "http://[::00001]/" ) );
    UTF_CHECK( ! isAccepted( "http://[::1.2.3.256]/" ) );
    UTF_CHECK( ! isAccepted( "http://[::1.2.3.04]/" ) );
    UTF_CHECK( ! isAccepted( "http://[::1.2.3]/" ) );
    UTF_CHECK( ! isAccepted( "http://[::g]/" ) );
    UTF_CHECK( ! isAccepted( "http://[]/" ) );

    /*
     * The brackets belong to the authority and not to the host, and the only thing which may
     * follow them is a port
     */

    UTF_CHECK( ! isAccepted( "http://[::1/" ) );
    UTF_CHECK( ! isAccepted( "http://::1/" ) );
    UTF_CHECK( ! isAccepted( "http://[::1]x/" ) );
    UTF_CHECK( ! isAccepted( "http://[::1]:x/" ) );

    /*
     * RFC 6874 zone identifiers are not part of RFC 3986 and are not accepted
     */

    UTF_CHECK( ! isAccepted( "http://[fe80::1%25eth0]/" ) );

    /*
     * IPvFuture = "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
     */

    UTF_CHECK( isAccepted( "http://[v7.anything:goes]/" ) );
    UTF_CHECK( ! isAccepted( "http://[v.x]/" ) );
    UTF_CHECK( ! isAccepted( "http://[v7.]/" ) );
    UTF_CHECK( ! isAccepted( "http://[v7x]/" ) );

    /*
     * The host of an IP literal is stored without its brackets, which is the form a resolver
     * expects, and gets them back wherever it appears inside an authority. The hexadecimal
     * digits are case insensitive and normalize to lower case, but the zero compression of
     * RFC 5952 is deliberately not applied
     */

    {
        const auto uri = bl::net::Uri::parse( "http://[2001:DB8::1]:8080/x" );

        UTF_CHECK( uri.isIpLiteral() );
        UTF_CHECK_EQUAL( uri.host(), std::string( "2001:db8::1" ) );
        UTF_CHECK_EQUAL( uri.authority(), std::string( "[2001:db8::1]:8080" ) );
        UTF_CHECK_EQUAL( uri.origin(), std::string( "http://[2001:db8::1]:8080" ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "http://[2001:db8::1]:8080/x" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "http://[0:0:0:0:0:0:0:1]/" );

        UTF_CHECK_EQUAL( uri.host(), std::string( "0:0:0:0:0:0:0:1" ) );
    }

    /*
     * An IPv4 address which is not a valid dotted-quad is still a valid reg-name, which is
     * what RFC 3986 says about it - there is no separate IPv4 validation to fail
     */

    {
        const auto uri = bl::net::Uri::parse( "http://010.1.1.999/" );

        UTF_CHECK( ! uri.isIpLiteral() );
        UTF_CHECK_EQUAL( uri.host(), std::string( "010.1.1.999" ) );
    }
}

UTF_AUTO_TEST_CASE( Uri_StrictnessTests )
{
    /*
     * A malformed reference is reported as an InvalidDataFormatException, and tryParse( ... )
     * catches that one and nothing else - so every "! isAccepted( ... )" below is also an
     * assertion that the exception was of that type
     */

    UTF_CHECK_THROW( bl::net::Uri::parse( "http://example.com/a b" ), bl::InvalidDataFormatException );

    const auto isAccepted = []( const std::string& text ) -> bool
    {
        bl::net::Uri uri;

        return bl::net::Uri::tryParse( text, uri );
    };

    /*
     * Control characters, whitespace and backslashes are errors rather than being stripped
     * or re-interpreted. The WHATWG URL specification has a browser strip a tab, a newline
     * and a carriage return anywhere in a URL and treat a backslash as a path separator;
     * accepting either makes this parser disagree with a validator which does not, and a
     * parser differential between the component which checks a URL and the component which
     * fetches it is how a filter gets bypassed
     */

    UTF_CHECK( ! isAccepted( "http://example.com/a b" ) );
    UTF_CHECK( ! isAccepted( "http://exa mple.com/" ) );
    UTF_CHECK( ! isAccepted( " http://example.com/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\tb" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\nb" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\rb" ) );
    UTF_CHECK( ! isAccepted( std::string( "http://example.com/a\0b", 22U ) ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\x7f" "b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\\b" ) );
    UTF_CHECK( ! isAccepted( "http:\\\\example.com\\a" ) );

    /*
     * There is no IDNA, so a non-ASCII byte is an error and the caller passes the A-label
     */

    UTF_CHECK( ! isAccepted( "http://ex\xc3\xa4mple.com/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/\xc3\xa4" ) );

    {
        const auto uri = bl::net::Uri::parse( "http://xn--exmple-cua.com/" );

        UTF_CHECK_EQUAL( uri.host(), std::string( "xn--exmple-cua.com" ) );
    }

    /*
     * Percent-encoding which is not a complete triplet of two hexadecimal digits
     */

    UTF_CHECK( ! isAccepted( "http://example.com/%" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/%4" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/%zz" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/%4z" ) );

    /*
     * Characters which are in no RFC 3986 character set at all
     */

    UTF_CHECK( ! isAccepted( "http://example.com/a<b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a>b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a\"b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a{b}" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a|b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a^b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a`b" ) );
    UTF_CHECK( ! isAccepted( "http://example.com/a#b#c" ) );

    /*
     * The authority - "@" is not in the host character set, ":" is not either, and the port
     * is digits only and must fit in 16 bits
     */

    UTF_CHECK( ! isAccepted( "http://a@b@example.com/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com:80:90/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com:8o/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com:65536/" ) );
    UTF_CHECK( ! isAccepted( "http://example.com:999999/" ) );

    {
        const auto uri = bl::net::Uri::parse( "http://example.com:65535/" );

        UTF_CHECK_EQUAL( uri.port(), 65535 );
    }

    /*
     * The scheme rule, and the path-noscheme rule which is its mirror image - a relative
     * reference may not carry a ":" in its first path segment, because that is exactly what
     * would make it look like a scheme
     */

    UTF_CHECK( ! isAccepted( "1http://example.com/" ) );
    UTF_CHECK( ! isAccepted( "ht tp://example.com/" ) );
    UTF_CHECK( ! isAccepted( "://example.com/" ) );
    UTF_CHECK( ! isAccepted( ":/" ) );
    UTF_CHECK( ! isAccepted( "12:34" ) );

    {
        const auto uri = bl::net::Uri::parse( "a+b-c.d://h/" );

        UTF_CHECK_EQUAL( uri.scheme(), std::string( "a+b-c.d" ) );
    }

    /*
     * Once the first ":" does spell a scheme the rest of the reference is a path-rootless
     * one, where a further ":" is an ordinary pchar - so "a:b:c" is a URI and not a relative
     * reference which broke the rule above
     */

    {
        const auto uri = bl::net::Uri::parse( "a:b:c" );

        UTF_CHECK_EQUAL( uri.scheme(), std::string( "a" ) );
        UTF_CHECK_EQUAL( uri.path(), std::string( "b:c" ) );
    }

    /*
     * The characters which ARE allowed, so that the rejections above are not the parser
     * simply refusing everything - the sub-delims and the two extra pchars of section 3.3
     */

    UTF_CHECK_NO_THROW( bl::net::Uri::parse( "http://h/a!$&'()*+,;=" ) );
    UTF_CHECK_NO_THROW( bl::net::Uri::parse( "http://h/a@b:c" ) );
    UTF_CHECK_NO_THROW( bl::net::Uri::parse( "http://h/?a=b&c=/d?e" ) );
    UTF_CHECK_NO_THROW( bl::net::Uri::parse( "http://h/#a/b?c" ) );
    UTF_CHECK_NO_THROW( bl::net::Uri::parse( "mailto:user@example.com" ) );
}

UTF_AUTO_TEST_CASE( Uri_Rfc3986NormalResolutionTests )
{
    /*
     * RFC 3986 section 5.4.1, transcribed verbatim, against the base URI of section 5.4
     */

    const auto base = bl::net::Uri::parse( "http://a/b/c/d;p?q" );

    const auto chkResolve = [ &base ](
        const std::string&                                  reference,
        const std::string&                                  expected
        ) -> void
    {
        UTF_CHECK_EQUAL( base.resolve( reference ).toString(), expected );
    };

    chkResolve( "g:h",          "g:h" );
    chkResolve( "g",            "http://a/b/c/g" );
    chkResolve( "./g",          "http://a/b/c/g" );
    chkResolve( "g/",           "http://a/b/c/g/" );
    chkResolve( "/g",           "http://a/g" );
    chkResolve( "//g",          "http://g" );
    chkResolve( "?y",           "http://a/b/c/d;p?y" );
    chkResolve( "g?y",          "http://a/b/c/g?y" );
    chkResolve( "#s",           "http://a/b/c/d;p?q#s" );
    chkResolve( "g#s",          "http://a/b/c/g#s" );
    chkResolve( "g?y#s",        "http://a/b/c/g?y#s" );
    chkResolve( ";x",           "http://a/b/c/;x" );
    chkResolve( "g;x",          "http://a/b/c/g;x" );
    chkResolve( "g;x?y#s",      "http://a/b/c/g;x?y#s" );
    chkResolve( "",             "http://a/b/c/d;p?q" );
    chkResolve( ".",            "http://a/b/c/" );
    chkResolve( "./",           "http://a/b/c/" );
    chkResolve( "..",           "http://a/b/" );
    chkResolve( "../",          "http://a/b/" );
    chkResolve( "../g",         "http://a/b/g" );
    chkResolve( "../..",        "http://a/" );
    chkResolve( "../../",       "http://a/" );
    chkResolve( "../../g",      "http://a/g" );
}

UTF_AUTO_TEST_CASE( Uri_Rfc3986AbnormalResolutionTests )
{
    /*
     * RFC 3986 section 5.4.2, transcribed verbatim, against the same base URI
     */

    const auto base = bl::net::Uri::parse( "http://a/b/c/d;p?q" );

    const auto chkResolve = [ &base ](
        const std::string&                                  reference,
        const std::string&                                  expected
        ) -> void
    {
        UTF_CHECK_EQUAL( base.resolve( reference ).toString(), expected );
    };

    /*
     * More ".." than there are segments to remove is not an error - the extra ones are
     * discarded
     */

    chkResolve( "../../../g",       "http://a/g" );
    chkResolve( "../../../../g",    "http://a/g" );

    /*
     * A dot segment in a reference whose path is already absolute
     */

    chkResolve( "/./g",             "http://a/g" );
    chkResolve( "/../g",            "http://a/g" );

    /*
     * Only a COMPLETE segment of "." or ".." is a dot segment
     */

    chkResolve( "g.",               "http://a/b/c/g." );
    chkResolve( ".g",               "http://a/b/c/.g" );
    chkResolve( "g..",              "http://a/b/c/g.." );
    chkResolve( "..g",              "http://a/b/c/..g" );

    /*
     * Dot segments in the middle of a reference
     */

    chkResolve( "./../g",           "http://a/b/g" );
    chkResolve( "./g/.",            "http://a/b/c/g/" );
    chkResolve( "g/./h",            "http://a/b/c/g/h" );
    chkResolve( "g/../h",           "http://a/b/c/h" );
    chkResolve( "g;x=1/./y",        "http://a/b/c/g;x=1/y" );
    chkResolve( "g;x=1/../y",       "http://a/b/c/y" );

    /*
     * Dot segment removal applies to the path and to nothing else - a "/../" inside a query
     * or a fragment is data
     */

    chkResolve( "g?y/./x",          "http://a/b/c/g?y/./x" );
    chkResolve( "g?y/../x",         "http://a/b/c/g?y/../x" );
    chkResolve( "g#s/./x",          "http://a/b/c/g#s/./x" );
    chkResolve( "g#s/../x",         "http://a/b/c/g#s/../x" );

    /*
     * The RFC gives two answers for this one. A strict parser returns "http:g", and only a
     * parser which applies the backward compatibility rule of section 5.2.2 returns
     * "http://a/b/c/g". This parser is strict ( design decision D24 ), so the strict answer
     * is the one pinned here
     */

    chkResolve( "http:g",           "http:g" );
}

UTF_AUTO_TEST_CASE( Uri_ResolutionEdgeCaseTests )
{
    const auto chkResolve = [](
        const std::string&                                  baseText,
        const std::string&                                  reference,
        const std::string&                                  expected
        ) -> void
    {
        const auto base = bl::net::Uri::parse( baseText );

        UTF_CHECK_EQUAL( base.resolve( reference ).toString(), expected );
    };

    /*
     * The merge of section 5.2.3 has a special case for a base which has an authority and an
     * empty path
     */

    chkResolve( "http://a", "g", "http://a/g" );
    chkResolve( "http://a", "", "http://a" );
    chkResolve( "http://a?q", "", "http://a?q" );

    /*
     * The base fragment is never inherited, whatever else is
     */

    chkResolve( "http://a/b/c#f", "g", "http://a/b/g" );
    chkResolve( "http://a/b/c#f", "", "http://a/b/c" );

    /*
     * An empty query and an empty fragment on the reference are not the same as absent ones
     */

    chkResolve( "http://a/b?q", "?", "http://a/b?" );
    chkResolve( "http://a/b?q", "g?", "http://a/g?" );
    chkResolve( "http://a/b?q", "g#", "http://a/g#" );

    /*
     * A reference which is already a URI replaces every component, including dropping the
     * userinfo and the port of the base
     */

    chkResolve( "http://u@a:8080/b?q#f", "https://x/y?z#w", "https://x/y?z#w" );

    /*
     * A network-path reference keeps the base scheme and replaces the whole authority
     */

    chkResolve( "https://u@a:8080/b", "//x:9090/y", "https://x:9090/y" );

    /*
     * A percent-encoded dot segment in a relative reference is still a dot segment - it is
     * decoded by the normalization of section 6.2.2.2 before section 5.2.4 runs
     */

    chkResolve( "http://a/b/c/d;p?q", "%2e%2e/g", "http://a/b/g" );
    chkResolve( "http://a/b/c/d;p?q", "%2E/g", "http://a/b/c/g" );

    /*
     * The base of a resolution must be an absolute URI - section 5.2.1
     */

    {
        const auto relative = bl::net::Uri::parse( "b/c" );

        UTF_CHECK_THROW( relative.resolve( "d" ), bl::ArgumentException );
    }

    /*
     * A reference which cannot be parsed fails the resolution rather than being ignored,
     * which is why a redirect handler reaches for tryParse( ... ) first
     */

    {
        const auto base = bl::net::Uri::parse( "http://a/b" );

        UTF_CHECK_THROW( base.resolve( "http://a/ b" ), bl::InvalidDataFormatException );
    }
}

UTF_AUTO_TEST_CASE( Uri_AuthorityOriginAndTargetTests )
{
    /*
     * origin() is the connection pool key, the cookie scope and the cross-origin check, so a
     * reference which spells out the default port for its scheme and one which leaves it out
     * must produce the same string
     */

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://Example.com/x" ).origin(),
        bl::net::Uri::parse( "http://example.com:80/y?z" ).origin()
        );

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://example.com/" ).origin(),
        std::string( "http://example.com:80" )
        );

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "https://example.com/" ).origin(),
        std::string( "https://example.com:443" )
        );

    /*
     * A different scheme, a different host or a different port is a different origin
     */

    UTF_CHECK(
        bl::net::Uri::parse( "http://example.com/" ).origin() !=
        bl::net::Uri::parse( "https://example.com/" ).origin()
        );

    UTF_CHECK(
        bl::net::Uri::parse( "https://example.com/" ).origin() !=
        bl::net::Uri::parse( "https://example.com:8443/" ).origin()
        );

    /*
     * A scheme this library does not speak has no default port, and then the origin carries
     * no port rather than a made up one
     */

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "ftp://example.com/" ).origin(),
        std::string( "ftp://example.com" )
        );

    UTF_CHECK_EQUAL( bl::net::Uri::defaultPort( "http" ), 80 );
    UTF_CHECK_EQUAL( bl::net::Uri::defaultPort( "https" ), 443 );
    UTF_CHECK_EQUAL( bl::net::Uri::defaultPort( "ftp" ), 0 );

    UTF_CHECK_EQUAL( bl::net::Uri::parse( "http://h/" ).effectivePort(), 80 );
    UTF_CHECK_EQUAL( bl::net::Uri::parse( "http://h:8080/" ).effectivePort(), 8080 );
    UTF_CHECK_EQUAL( bl::net::Uri::parse( "http://h/" ).port(), 0 );

    /*
     * authority() is the HTTP/2 ":authority" pseudo-header and the HTTP/1.1 Host header, so
     * it carries the port as written and never the userinfo - RFC 9113 section 8.3.1 forbids
     * the userinfo there, and hasUserInfo() is how a caller detects one
     */

    {
        const auto uri = bl::net::Uri::parse( "https://user:pw@example.com:8443/a" );

        UTF_CHECK( uri.hasUserInfo() );
        UTF_CHECK_EQUAL( uri.authority(), std::string( "example.com:8443" ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "https://user:pw@example.com:8443/a" ) );
    }

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "https://example.com/a" ).authority(),
        std::string( "example.com" )
        );

    /*
     * pathAndQuery() is the HTTP/2 ":path" pseudo-header and the HTTP/1.1 request target - an
     * empty path becomes "/", and the fragment is never part of it
     */

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://h" ).pathAndQuery(),
        std::string( "/" )
        );

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://h?a=b" ).pathAndQuery(),
        std::string( "/?a=b" )
        );

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://h/a/b?c=d#frag" ).pathAndQuery(),
        std::string( "/a/b?c=d" )
        );

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://h/a/b#frag" ).pathAndQuery(),
        std::string( "/a/b" )
        );

    /*
     * An empty query is still a query in a request target
     */

    UTF_CHECK_EQUAL(
        bl::net::Uri::parse( "http://h/a?" ).pathAndQuery(),
        std::string( "/a?" )
        );
}

/*
 * origin() is the one accessor which fabricates structure - the "://" - instead of rendering
 * what the reference actually carries, and what it fabricates is a security decision: which
 * connection is reused ( the pool key ) and which cookies are in scope. So it refuses a
 * reference which has no origin rather than rendering a plausible looking one
 *
 * Its neighbours deliberately do NOT refuse, because an empty string or a zero port is the
 * faithful rendering of what a relative reference carries and the has...() accessors are the
 * documented way to test for it. The second half of this case pins that, so that the guard
 * above is not later "completed" by spreading it over accessors which do not need it
 */

UTF_AUTO_TEST_CASE( Uri_OriginRequiresAbsoluteUriTests )
{
    /*
     * A relative reference has neither a scheme nor a host and would have rendered "://"
     */

    UTF_CHECK_THROW( bl::net::Uri::parse( "b/c" ).origin(), bl::ArgumentException );
    UTF_CHECK_THROW( bl::net::Uri::parse( "?q" ).origin(), bl::ArgumentException );
    UTF_CHECK_THROW( bl::net::Uri::parse( "#f" ).origin(), bl::ArgumentException );

    /*
     * A network-path reference carries a host but no scheme and would have rendered "://host",
     * a pool key and a cookie scope shared by every scheme for that host
     */

    UTF_CHECK_THROW( bl::net::Uri::parse( "//host/x" ).origin(), bl::ArgumentException );
    UTF_CHECK_THROW( bl::net::Uri::parse( "//host:8080/x" ).origin(), bl::ArgumentException );

    /*
     * A URI with a scheme and no authority, and one whose authority has an empty host, would
     * have rendered "mailto://", "http://:80" and "file://" - origins with no host at all,
     * which every other such URI would share
     */

    UTF_CHECK_THROW(
        bl::net::Uri::parse( "mailto:user@example.com" ).origin(),
        bl::ArgumentException
        );

    UTF_CHECK_THROW( bl::net::Uri::parse( "http:///x" ).origin(), bl::ArgumentException );
    UTF_CHECK_THROW( bl::net::Uri::parse( "file:///p" ).origin(), bl::ArgumentException );

    /*
     * The value tryParse( ... ) leaves behind when it fails has no origin either
     */

    {
        bl::net::Uri uri;

        UTF_CHECK( ! bl::net::Uri::tryParse( "http://a/ b", uri ) );
        UTF_CHECK_THROW( uri.origin(), bl::ArgumentException );
    }

    /*
     * A reference with no origin still renders every component it does carry. authority() in
     * particular returns the empty string rather than throwing - it invents no delimiter, and
     * toString() recomposes an empty authority through it ( RFC 3986 section 5.3 )
     */

    {
        const auto uri = bl::net::Uri::parse( "b/c" );

        UTF_CHECK_EQUAL( uri.authority(), std::string( "" ) );
        UTF_CHECK_EQUAL( uri.pathAndQuery(), std::string( "b/c" ) );
        UTF_CHECK_EQUAL( uri.effectivePort(), 0 );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "b/c" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "//host:8080/x" );

        UTF_CHECK( uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.authority(), std::string( "host:8080" ) );
        UTF_CHECK_EQUAL( uri.pathAndQuery(), std::string( "/x" ) );
        UTF_CHECK_EQUAL( uri.effectivePort(), 8080 );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "//host:8080/x" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "file:///p" );

        UTF_CHECK( uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.authority(), std::string( "" ) );
        UTF_CHECK_EQUAL( uri.effectivePort(), 0 );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "file:///p" ) );
    }

    {
        const auto uri = bl::net::Uri::parse( "mailto:user@example.com" );

        UTF_CHECK( ! uri.hasAuthority() );
        UTF_CHECK_EQUAL( uri.authority(), std::string( "" ) );
        UTF_CHECK_EQUAL( uri.pathAndQuery(), std::string( "user@example.com" ) );
        UTF_CHECK_EQUAL( uri.toString(), std::string( "mailto:user@example.com" ) );
    }

    /*
     * ... and a relative reference still resolves against an absolute base, which is how a
     * caller turns one into a URI which does have an origin
     */

    {
        const auto resolved = bl::net::Uri::parse( "https://example.com/a/b" ).resolve( "../c" );

        UTF_CHECK_EQUAL( resolved.origin(), std::string( "https://example.com:443" ) );
        UTF_CHECK_EQUAL( resolved.toString(), std::string( "https://example.com/c" ) );
    }
}
