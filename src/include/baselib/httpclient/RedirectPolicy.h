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

#ifndef __BL_HTTPCLIENT_REDIRECTPOLICY_H_
#define __BL_HTTPCLIENT_REDIRECTPOLICY_H_

#include <baselib/http/HeaderList.h>

#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief What the client does with a response which may be a redirect
         */

        enum class RedirectAction : std::uint8_t
        {
            /**
             * Not a redirect, or redirects are off - the response is the answer and the caller
             * deals with it. This is the DEFAULT behavior of this client, unchanged from the
             * existing one, which reports the target in errinfo_http_redirect_url and stops
             */

            DoNotFollow,

            /**
             * Follow it, with the method, body and headers the decision describes
             */

            Follow,

            /**
             * It IS a redirect, redirects ARE on, and this one will not be followed. The reason is
             * in 'refusal' and is never merely "no": every refusal below exists to stop something
             */

            Refused,
        };

        /**
         * @brief Why a redirect was not followed
         */

        enum class RedirectRefusal : std::uint8_t
        {
            None,

            /**
             * Redirect following is off, which is the default
             */

            Disabled,

            /**
             * The chain is longer than the hop limit - a redirect loop, or a server making the
             * client do unbounded work
             */

            HopLimitExceeded,

            /**
             * A 3xx with no Location header at all
             */

            NoLocation,

            /**
             * The Location header is not a well formed URI reference, so there is nothing to
             * resolve. Treated as data rather than as a defect, which is what net::Uri::tryParse
             * exists for
             */

            MalformedLocation,

            /**
             * The target's scheme is neither http nor https. A redirect to file:, ftp: or a custom
             * scheme is a standard way to turn a fetch into something else entirely, so it is
             * refused rather than handed to whatever would resolve it
             */

            UnsupportedScheme,

            /**
             * An https request redirected to http. The transport a caller asked for is a security
             * property of the request and a server does not get to downgrade it
             */

            ProtocolDowngrade,

            /**
             * A 307 or a 308 preserves the method AND the body, and this request's body cannot be
             * produced a second time. Sending the request with no body, or with a truncated one,
             * would be worse than not following it
             */

            BodyNotReplayable,
        };

        /**
         * @brief The answer: whether to follow, where to, and as what
         */

        struct RedirectDecision
        {
            cpp::ScalarTypeIniter< RedirectAction >                             action;
            cpp::ScalarTypeIniter< RedirectRefusal >                            refusal;

            /**
             * The resolved target, filled in whenever the Location resolved - even when the
             * decision is not to follow, because a caller which reports the redirect rather than
             * following it needs exactly this
             */

            net::Uri                                                            target;

            /**
             * The method of the next request; the original one unless a rewrite applied
             */

            std::string                                                         method;

            /**
             * Whether the body is dropped - always together with a method rewrite to GET
             */

            cpp::ScalarTypeIniter< bool >                                       dropBody;

            /**
             * Whether the hop crosses an origin, per net::Uri::origin()
             */

            cpp::ScalarTypeIniter< bool >                                       isCrossOrigin;

            /**
             * Whether Authorization and the cookies of the previous origin must be dropped - true
             * exactly when the hop is cross-origin. dropCredentialHeaders( ... ) does it
             */

            cpp::ScalarTypeIniter< bool >                                       dropCredentials;

            RedirectDecision()
            {
                action = RedirectAction::DoNotFollow;
                refusal = RedirectRefusal::None;
            }

            bool shouldFollow() const NOEXCEPT
            {
                return RedirectAction::Follow == action.value();
            }
        };

        /**
         * @brief class RedirectPolicyT - the redirect rules of design 5.6, as a pure decision
         *
         * OFF BY DEFAULT, as the design requires and as the existing client behaves: a 3xx is
         * reported to the caller rather than followed. Turning it on is a caller's decision
         * because following a redirect means sending the caller's request somewhere the caller did
         * not name.
         *
         * A PURE DECISION AND NOTHING ELSE. evaluate( ... ) takes the request it is about and
         * returns what to do; it performs no I/O, holds no state and does not count hops itself -
         * the hop count comes in as a parameter because the request task is what owns the chain.
         * That is what makes every rule below directly testable, which for rules of this kind is
         * the point.
         *
         * THE METHOD REWRITE IS THE PART IMPLEMENTATIONS GET WRONG, so it is stated exactly:
         *
         *  - 301 and 302 rewrite to GET and drop the body ONLY for POST. For PUT, DELETE, PATCH
         *    and the rest the method and the body are preserved. Rewriting those would turn a
         *    caller's DELETE into a GET of the same resource, which is a different request that
         *    silently succeeds.
         *  - 303 rewrites to GET for every method EXCEPT HEAD, which stays HEAD. This is the rule
         *    of WHATWG Fetch ("HTTP-redirect fetch" step 11), which is what browsers and curl do.
         *    Turning a HEAD into a GET would make a request whose whole purpose is to avoid
         *    downloading a body download one.
         *  - 307 and 308 preserve the method AND the body, which is why they need a replayable
         *    body and are refused without one.
         *
         * THE CREDENTIAL AND DOWNGRADE RULES ARE SECURITY PROPERTIES rather than conveniences:
         *
         *  - Authorization and the cookies of the old origin are dropped on a CROSS-ORIGIN hop.
         *    Without this an open redirect on a site the caller authenticates to hands the
         *    caller's credentials to whatever the redirect names, which is one of the most
         *    reliable ways credentials leak.
         *  - An https request redirected to http is refused unless the caller explicitly allowed
         *    it. The caller chose a transport; the server does not get to undo that choice.
         *  - A target whose scheme is neither http nor https is refused, so a redirect cannot turn
         *    a fetch into a file: read or hand a URL to some other resolver.
         *
         * Origins are compared with net::Uri::origin(), which throws for a reference without one -
         * and the resolved target always has one, because a target is resolved against an absolute
         * request URI and the request URI is checked first.
         */

        template
        <
            typename E = void
        >
        class RedirectPolicyT FINAL
        {
        public:

            typedef RedirectPolicyT< E >                                        this_type;

            enum : std::size_t
            {
                /**
                 * The limit WHATWG Fetch sets, which is what browsers enforce. No legitimate chain
                 * is anywhere near it and it bounds the work a server can make a client do
                 */

                DEFAULT_MAX_HOPS                    = 20U,
            };

        private:

            cpp::ScalarTypeIniter< bool >                                       m_isEnabled;
            cpp::ScalarTypeIniter< bool >                                       m_allowHttpsToHttpDowngrade;
            cpp::ScalarTypeIniter< std::size_t >                                m_maxHops;

            static const std::string                                            g_schemeHttp;
            static const std::string                                            g_schemeHttps;
            static const std::string                                            g_methodGet;
            static const std::string                                            g_methodHead;
            static const std::string                                            g_methodPost;
            static const std::string                                            g_headerLocation;
            static const std::string                                            g_headerAuthorization;
            static const std::string                                            g_headerProxyAuthorization;
            static const std::string                                            g_headerCookie;

        public:

            RedirectPolicyT()
            {
                m_maxHops = DEFAULT_MAX_HOPS;
            }

            bool isEnabled() const NOEXCEPT
            {
                return m_isEnabled;
            }

            void isEnabled( SAA_in const bool value ) NOEXCEPT
            {
                m_isEnabled = value;
            }

            bool allowHttpsToHttpDowngrade() const NOEXCEPT
            {
                return m_allowHttpsToHttpDowngrade;
            }

            void allowHttpsToHttpDowngrade( SAA_in const bool value ) NOEXCEPT
            {
                m_allowHttpsToHttpDowngrade = value;
            }

            std::size_t maxHops() const NOEXCEPT
            {
                return m_maxHops;
            }

            void maxHops( SAA_in const std::size_t value ) NOEXCEPT
            {
                m_maxHops = value;
            }

            /**
             * @brief The five statuses which redirect
             *
             * 300 (Multiple Choices) and 304 (Not Modified) are deliberately not among them: 300
             * has no single target to follow and 304 is a cache response rather than a redirect,
             * although both carry a Location in some deployments
             */

            static bool isRedirectStatus( SAA_in const unsigned status ) NOEXCEPT
            {
                return
                    301U == status ||
                    302U == status ||
                    303U == status ||
                    307U == status ||
                    308U == status;
            }

            /**
             * @brief The method of the next request, and whether the body goes with it
             *
             * See the class note for why each branch is what it is
             */

            static std::string rewriteMethod(
                SAA_in          const unsigned                                  status,
                SAA_in          const std::string&                              method,
                SAA_out         bool&                                           dropBody
                )
            {
                /*
                 * The comparisons below are case SENSITIVE, because RFC 9110 section 9.1 makes the
                 * method token case sensitive: "post" is not POST and must not be rewritten as if
                 * it were
                 */

                dropBody = false;

                if( 303U == status )
                {
                    if( method == g_methodGet || method == g_methodHead )
                    {
                        return method;
                    }

                    dropBody = true;

                    return g_methodGet;
                }

                if( ( 301U == status || 302U == status ) && method == g_methodPost )
                {
                    dropBody = true;

                    return g_methodGet;
                }

                return method;
            }

            /**
             * @brief Removes the headers which must not survive a cross-origin hop
             *
             * Cookies are removed rather than recomputed: the cookies of the NEW origin are the
             * jar's to supply for the new request, and carrying the old origin's Cookie header
             * across would send them to a host they were never scoped to
             */

            static void dropCredentialHeaders( SAA_inout http::HeaderList& headers )
            {
                ( void ) headers.removeAll( g_headerAuthorization );
                ( void ) headers.removeAll( g_headerProxyAuthorization );
                ( void ) headers.removeAll( g_headerCookie );
            }

            /**
             * @brief Decides what to do with one response
             *
             * @param requestUri the URI of the request which produced this response; it is also
             * the base the Location is resolved against, so it must be absolute
             * @param method that request's method
             * @param status the response status
             * @param location the Location header value, empty when the response carries none
             * @param isBodyReplayable ClientRequest::isReplayable() of that request
             * @param hopsSoFar how many redirects this chain has already followed
             *
             * @throw ArgumentException when the request URI is not an absolute URI with a host
             */

            RedirectDecision evaluate(
                SAA_in          const net::Uri&                                 requestUri,
                SAA_in          const std::string&                              method,
                SAA_in          const unsigned                                  status,
                SAA_in          const std::string&                              location,
                SAA_in          const bool                                      isBodyReplayable,
                SAA_in          const std::size_t                               hopsSoFar
                ) const
            {
                RedirectDecision decision;

                decision.method = method;

                if( ! isRedirectStatus( status ) )
                {
                    return decision;
                }

                /*
                 * Called for the refusal: the base of a resolution must be an absolute URI, and a
                 * cross-origin comparison is meaningless without an origin to compare
                 */

                const auto requestOrigin = requestUri.origin();

                if( ! m_isEnabled )
                {
                    /*
                     * Off. The target is still resolved where it can be, because a caller which
                     * reports the redirect instead of following it wants exactly that URL - which
                     * is what the existing client does with errinfo_http_redirect_url
                     */

                    decision.refusal = RedirectRefusal::Disabled;

                    net::Uri parsed;

                    if( ! location.empty() && net::Uri::tryParse( location, parsed ) )
                    {
                        decision.target = net::Uri::resolve( requestUri, parsed );
                    }

                    return decision;
                }

                decision.action = RedirectAction::Refused;

                if( location.empty() )
                {
                    decision.refusal = RedirectRefusal::NoLocation;

                    return decision;
                }

                net::Uri parsed;

                if( ! net::Uri::tryParse( location, parsed ) )
                {
                    decision.refusal = RedirectRefusal::MalformedLocation;

                    return decision;
                }

                decision.target = net::Uri::resolve( requestUri, parsed );

                /*
                 * The hop limit is checked after the target is resolved, so that a caller which
                 * stops here still learns where it was being sent
                 */

                if( hopsSoFar >= m_maxHops )
                {
                    decision.refusal = RedirectRefusal::HopLimitExceeded;

                    return decision;
                }

                if(
                    decision.target.scheme() != g_schemeHttp &&
                    decision.target.scheme() != g_schemeHttps
                    )
                {
                    decision.refusal = RedirectRefusal::UnsupportedScheme;

                    return decision;
                }

                /*
                 * "http:/foo" is a well formed reference which carries a scheme and no authority,
                 * so it survives tryParse and resolution and then has NO ORIGIN. It is refused
                 * here rather than being allowed to reach origin() below, which would throw out of
                 * a function whose job is to decide - a server must not be able to turn a response
                 * into an exception
                 */

                if( decision.target.host().empty() )
                {
                    decision.refusal = RedirectRefusal::MalformedLocation;

                    return decision;
                }

                if(
                    requestUri.scheme() == g_schemeHttps &&
                    decision.target.scheme() == g_schemeHttp &&
                    ! m_allowHttpsToHttpDowngrade
                    )
                {
                    decision.refusal = RedirectRefusal::ProtocolDowngrade;

                    return decision;
                }

                bool dropBody = false;

                decision.method = rewriteMethod( status, method, dropBody );
                decision.dropBody = dropBody;

                /*
                 * A 307 or a 308 keeps the body, so it needs one which can be produced again. This
                 * is checked only when the body actually survives the hop - a 303 which rewrites a
                 * POST to a bodiless GET does not care whether the source could rewind
                 */

                if( ! dropBody && ! isBodyReplayable )
                {
                    decision.refusal = RedirectRefusal::BodyNotReplayable;

                    return decision;
                }

                /*
                 * The target always has an origin: it was resolved against an absolute base, so it
                 * carries the base's scheme and authority wherever the reference supplied none
                 */

                decision.isCrossOrigin = ( requestOrigin != decision.target.origin() );
                decision.dropCredentials = decision.isCrossOrigin.value();

                decision.action = RedirectAction::Follow;
                decision.refusal = RedirectRefusal::None;

                return decision;
            }

            /**
             * @brief The same decision, reading the Location out of a response header list
             *
             * A response carrying two Location headers is refused as malformed rather than having
             * one of them picked: which one a client picks is precisely the kind of difference
             * that lets a response be interpreted two ways by two components
             */

            RedirectDecision evaluate(
                SAA_in          const net::Uri&                                 requestUri,
                SAA_in          const std::string&                              method,
                SAA_in          const unsigned                                  status,
                SAA_in          const http::HeaderList&                         responseHeaders,
                SAA_in          const bool                                      isBodyReplayable,
                SAA_in          const std::size_t                               hopsSoFar
                ) const
            {
                const auto count = responseHeaders.count( g_headerLocation );

                if( count > 1U && isRedirectStatus( status ) && m_isEnabled )
                {
                    RedirectDecision decision;

                    decision.method = method;
                    decision.action = RedirectAction::Refused;
                    decision.refusal = RedirectRefusal::MalformedLocation;

                    return decision;
                }

                const auto* value = responseHeaders.tryGet( g_headerLocation );

                return evaluate(
                    requestUri,
                    method,
                    status,
                    value ? *value : std::string(),
                    isBodyReplayable,
                    hopsSoFar
                    );
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_schemeHttp )              = "http";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_schemeHttps )             = "https";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_methodGet )               = "GET";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_methodHead )              = "HEAD";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_methodPost )              = "POST";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_headerLocation )          = "location";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_headerAuthorization )     = "authorization";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_headerProxyAuthorization ) = "proxy-authorization";
        BL_DEFINE_STATIC_CONST_STRING( RedirectPolicyT, g_headerCookie )            = "cookie";

        typedef RedirectPolicyT<> RedirectPolicy;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_REDIRECTPOLICY_H_ */
