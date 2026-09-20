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

#ifndef __BL_HTTPCLIENT_CLIENTSESSION_H_
#define __BL_HTTPCLIENT_CLIENTSESSION_H_

#include <baselib/httpclient/ConnectionPool.h>
#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/Http1ConnectionTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ContentDecoder.h>
#include <baselib/httpclient/CookieJar.h>
#include <baselib/httpclient/RedirectPolicy.h>
#include <baselib/httpclient/HeaderProfile.h>
#include <baselib/httpclient/ClientTypes.h>
#include <baselib/httpclient/ClientConnection.h>

#include <baselib/http2/Http2ConnectionTask.h>

#include <baselib/tasks/TaskBase.h>

#include <baselib/core/Uri.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <string>
#include <vector>

BL_IID_DECLARE( ClientRequestTask, "60ee6482-6177-439f-a13b-b9ce8df9a09a" )
BL_IID_DECLARE( ClientSession, "a4f21ad6-e0fc-4d1a-9ca5-0a13c2bba35a" )

namespace bl
{
    namespace httpclient
    {
        /**
         * @brief Everything one session applies to every request it makes
         *
         * The four configuration values below are the ones the layers underneath already take, and
         * they are held here rather than merged into one flat struct so that a knob keeps the name
         * and the documentation it has where it is enforced
         *
         * WHAT THE SESSION DERIVES RATHER THAN COPIES. ConnectionPoolPolicy::drainingReserve and
         * ::idleTimeout are pool policy which only a driver can enforce, so the session's
         * connection factory writes them into the Http2ConnectionConfig of every connection it
         * builds - see makeConnectionFactory( ). Setting them in http2Config here instead would be
         * overwritten, and that is deliberate: there is one place each of those two numbers lives
         */

        struct ClientSessionConfig
        {
            ConnectionPoolPolicy                                                poolPolicy;
            HttpClientRequestConfig                                             requestConfig;

            tasks::ClientConnectionConfig                                       connectionConfig;
            tasks::Http2ConnectionConfig                                        http2Config;
            tasks::ProxyConfig                                                  proxyConfig;

            Http1ResponseLimits                                                 http1Limits;

            /**
             * @brief The header shape of design 6.5, by request kind
             *
             * A default constructed profile has no kind table at all, and that is the ordinary
             * non-impersonating session: the caller's headers are sent as the caller wrote them
             */

            HeaderProfile                                                       headerProfile;

            /**
             * @brief STRICT content-encoding - design 6.5 and the decoder deferral record
             *
             * Off: the accept-encoding sent is the profile's list intersected with the REGISTERED
             * decoders, so every body which comes back can be decoded, and with no decoder
             * registered (D9) the header is omitted altogether.
             *
             * On: the profile's list is sent exactly as the profile writes it and the body is
             * handed back in whatever coding the server chose, with its content-encoding intact -
             * for a caller who decodes it themselves. This is the mode which keeps the header
             * layer exact under impersonation while no decoder exists
             */

            cpp::ScalarTypeIniter< bool >                                       isStrictContentEncoding;

            /**
             * @brief The three identity fields of ConnectionKey which are the session's
             *
             * They are part of the pool's key because two requests which disagree about any of
             * them must not share a connection
             */

            std::string                                                         tlsProfileId;
            std::string                                                         http2ProfileId;

            cpp::ScalarTypeIniter< std::uint32_t >                              verificationFlags;
        };

        /**
         * @brief What a caller holds after createRequestTask( ), and reads after it has run
         *
         * It is an interface and not the task class because the task is one of two things: a
         * single request, or a redirect chain of them. A caller schedules it as a tasks::Task and
         * reads the result through this
         */

        class ClientRequestTask : public om::Object
        {
            BL_DECLARE_INTERFACE( ClientRequestTask )

        public:

            /**
             * @brief The request as it was finally sent - after any redirect, with the session's
             * own headers on it
             */

            virtual const ClientRequest& request() const NOEXCEPT = 0;

            /**
             * @brief The response of the last hop
             */

            virtual const ClientResponse& response() const NOEXCEPT = 0;

            /**
             * @brief How many redirects were followed to reach it; zero for the ordinary request
             */

            virtual std::size_t redirectHops() const NOEXCEPT = 0;
        };

        /**
         * @brief The session of design 5.6 and 5.8
         *
         * An interface over ClientSessionT< STREAM >, so that the transport the session speaks is
         * a property of the object and not of every type which holds one
         */

        class ClientSession : public om::Object
        {
            BL_DECLARE_INTERFACE( ClientSession )

        public:

            /**
             * @brief Makes the task for one request; it is not scheduled
             *
             * @throw ArgumentException when the URL is not an absolute http or https URL
             * @throw NotSupportedException when the URL's scheme is not the one this session's
             * transport speaks, or when the request cannot be carried at all - see the
             * BodySource rule at ClientSessionT
             */

            virtual auto createRequestTask(
                SAA_in          const ClientRequest&                            request,
                SAA_in_opt      const om::ObjPtrCopyable< BodySink >&           bodySink =
                                    om::ObjPtrCopyable< BodySink >()
                )
                -> om::ObjPtr< ClientRequestTask > = 0;

            /**
             * @brief The scheme this session's transport speaks - "http" or "https"
             */

            virtual const std::string& transportScheme() const NOEXCEPT = 0;

            virtual CookieJar& cookieJar() NOEXCEPT = 0;

            virtual ContentDecoderRegistry& decoders() NOEXCEPT = 0;

            virtual RedirectPolicy& redirectPolicy() NOEXCEPT = 0;

            /**
             * @brief The active header profile - design 6.5
             */

            virtual const HeaderProfile& profile() const NOEXCEPT = 0;

            virtual void profile( SAA_in HeaderProfile profile ) = 0;

            virtual const om::ObjPtr< ConnectionPool >& pool() const NOEXCEPT = 0;

            virtual const ClientSessionConfig& config() const NOEXCEPT = 0;
        };

        /**
         * @brief The mutable state one session shares with every request task it makes
         *
         * A counted object of its own, and not the session, so that a request task in flight keeps
         * exactly what it reads alive - the jar it writes a Set-Cookie into and the registry it
         * asks for a decoder - without keeping the pool and the driver factory alive with it
         */

        template
        <
            typename E = void
        >
        class SessionStateT : public om::ObjectDefaultBase
        {
        public:

            CookieJar                                                           cookieJar;
            ContentDecoderRegistry                                              decoders;
        };

        typedef SessionStateT<>                                                 SessionState;
        typedef om::ObjectImpl< SessionState >                                  SessionStateImpl;

        /**
         * @brief Everything a request task needs from the session it came from, as ONE value
         *
         * Snapshotted at createRequestTask( ) so that a session reconfigured while a request is in
         * flight does not change what that request is doing halfway through. The two things which
         * are deliberately NOT snapshotted are the cookie jar and the decoder registry: the jar is
         * per session, thread safe and meant to accumulate across requests, and the registry is
         * populated once at set-up
         */

        struct SessionRequestPlan
        {
            om::ObjPtrCopyable< ConnectionPool >                                pool;
            om::ObjPtrCopyable< SessionState >                                  state;

            HttpClientRequestConfig                                             requestConfig;
            RedirectPolicy                                                      redirectPolicy;
            HeaderProfile                                                       headerProfile;

            /**
             * @brief The pool's own policy, for the DISPATCHED half of the retry
             *
             * The pool replays a request which was still queued behind a failed connection; a
             * request which had already been dispatched comes back through releaseStream( ) and a
             * fresh acquire( ), and the frozen S2.6 contract gives the pool no identity to count
             * those against. That half is this task's - see chkPrepareRetry( ) - and
             * chkRequestMayBeReplayed( ) is the one rule both halves apply
             */

            ConnectionPoolPolicy                                                policy;

            /**
             * @brief The identity fields of every key this request uses, with the host and the
             * port left to the hop's own URL
             */

            ConnectionKey                                                       templateKey;

            /**
             * @brief The one scheme this session speaks - ClientSessionT::transportScheme( )
             *
             * Carried as a field of its own rather than in templateKey.scheme, which keyFor( )
             * deliberately leaves to the hop's own URL. A REDIRECT TARGET IS A URL THIS SESSION
             * WAS NOT GIVEN BY ITS CALLER, so the rule createRequestTask( ) enforces has to be
             * enforced against it too - see chkPrepareNextHop( )
             */

            std::string                                                         transportScheme;

            cpp::ScalarTypeIniter< bool >                                       isStrictContentEncoding;

            /**
             * @brief Whether a request carrying a BodySource has to be routed to a key of its own
             *
             * See the BodySource rule at ClientSessionT: true when this session's transport may
             * produce an HTTP/1.1 connection, which is the only case in which the routing buys
             * anything
             */

            cpp::ScalarTypeIniter< bool >                                       isProtocolNegotiated;
        };

        /******************************************************************************************
         * ============================== The session's header work ===============================
         */

        /**
         * @brief The pure functions which turn a caller's request into the one which goes out
         *
         * Free of the session, of the pool and of any task, so that every one of them is testable
         * on its own and none of them can reach for state it was not given
         */

        template
        <
            typename E = void
        >
        class SessionHeadersT FINAL
        {
            BL_DECLARE_STATIC( SessionHeadersT )

        public:

            /**
             * @brief The marker this session puts in ConnectionKey::http2ProfileId for a request
             * which must not be carried over HTTP/1.1 - see the BodySource rule at ClientSessionT
             */

            static const std::string& h2OnlyKeyMarker() NOEXCEPT
            {
                return g_h2OnlyKeyMarker;
            }

            static bool isH2OnlyKey( SAA_in const ConnectionKey& key )
            {
                const auto& marker = g_h2OnlyKeyMarker;

                return
                    key.http2ProfileId.size() >= marker.size() &&
                    0 == key.http2ProfileId.compare(
                        key.http2ProfileId.size() - marker.size(),
                        marker.size(),
                        marker
                        );
            }

            /**
             * @brief The key one request's URL resolves to
             *
             * @param templateKey the session's own identity fields - proxy, profiles and
             * verification flags; its host and port are ignored, because they are the URL's
             * @param isProtocolNegotiated whether this session's transport may produce an
             * HTTP/1.1 connection as well as an HTTP/2 one
             *
             * THE h2-ONLY MARKER, and why it is part of the key rather than a flag beside it. The
             * HTTP/1.1 driver refuses every request carrying a BodySource, and the session is the
             * only layer which knows both the request and what a connection for a key will speak.
             * A connection which does not offer http/1.1 is a DIFFERENT connection from one which
             * does - a peer may select only from what it was offered ( RFC 7301 3.1 ) - so two
             * requests which disagree about the offer must not share one, which is exactly what a
             * key is for. The session's connection factory reads the marker back off the key and
             * narrows the offer for it, so such a request cannot be dispatched to HTTP/1.1 at all
             *
             * It is appended rather than substituted so that a session which really does have two
             * HTTP/2 profiles keeps them apart, and a marker of its own is what makes the ordinary
             * requests to the same origin stay on the ordinary connection
             */

            static auto keyFor(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const ConnectionKey&                            templateKey,
                SAA_in          const bool                                      isProtocolNegotiated
                )
                -> ConnectionKey
            {
                auto key = ConnectionKey::fromUri( request.url() );

                key.proxyId = templateKey.proxyId;
                key.tlsProfileId = templateKey.tlsProfileId;
                key.http2ProfileId = templateKey.http2ProfileId;
                key.verificationFlags = templateKey.verificationFlags;

                if( isProtocolNegotiated && nullptr != request.bodySource() )
                {
                    key.http2ProfileId += g_h2OnlyKeyMarker;
                }

                return key;
            }

            /**
             * @brief The content codings this session may advertise
             *
             * Design 6.5: the value actually sent is the profile's list INTERSECTED WITH THE
             * REGISTERED DECODERS, and with none registered (D9) the header is omitted. Strict
             * mode sends the profile's list exactly and takes the raw body back
             *
             * The intersection keeps the PROFILE's order, because the order is the fingerprint;
             * the registry's order is an artefact of who registered first
             */

            static std::string acceptEncodingValue(
                SAA_in          const std::vector< std::string >&               profileCodings,
                SAA_in          const std::vector< std::string >&               registeredCodings,
                SAA_in          const bool                                      isStrict
                )
            {
                std::string result;

                for( std::size_t i = 0U; i < profileCodings.size(); ++i )
                {
                    const auto& coding = profileCodings[ i ];

                    if( ! isStrict && ! contains( registeredCodings, coding ) )
                    {
                        continue;
                    }

                    if( ! result.empty() )
                    {
                        result += ", ";
                    }

                    result += coding;
                }

                return result;
            }

            /**
             * @brief Splits a Cookie field value into its name=value pairs, trimming OWS
             *
             * The pairs are kept as they were written - this is a merge and not a re-encode, so a
             * value is never normalized on its way through
             */

            static std::vector< std::string > splitCookiePairs( SAA_in const std::string& value )
            {
                std::vector< std::string > result;

                std::size_t pos = 0U;

                while( pos <= value.size() )
                {
                    const auto end = value.find( ';', pos );

                    const auto piece = trimOws(
                        value.substr(
                            pos,
                            std::string::npos == end ? std::string::npos : end - pos
                            )
                        );

                    if( ! piece.empty() )
                    {
                        result.push_back( piece );
                    }

                    if( std::string::npos == end )
                    {
                        break;
                    }

                    pos = end + 1U;
                }

                return result;
            }

            static std::string cookiePairName( SAA_in const std::string& pair )
            {
                const auto pos = pair.find( '=' );

                return std::string::npos == pos ? pair : pair.substr( 0U, pos );
            }

            /**
             * @brief ONE Cookie field, merged from the caller's and the jar's - RFC 6265 5.4
             *
             * AN HTTP/1.1 REQUEST CARRIES EXACTLY ONE Cookie FIELD, and a request which carries
             * two is malformed. The jar and the caller both produce one, so they are merged here
             * rather than both appended - and the place the two meet is specific: on a SAME-ORIGIN
             * redirect the caller's header survives RedirectPolicy::dropCredentialHeaders( ) while
             * the jar recomputes for the new target, so both are present and both are in scope
             *
             * THE CALLER'S PAIR WINS for a name they both carry. A caller who wrote a cookie by
             * hand named it deliberately, and the jar's value for that name would otherwise
             * silently override an explicit instruction - or, worse, be sent beside it, which is
             * a Cookie field with the same name twice
             */

            static std::string mergeCookieValues(
                SAA_in          const std::string&                              callerValue,
                SAA_in          const std::string&                              jarValue
                )
            {
                const auto callerPairs = splitCookiePairs( callerValue );
                const auto jarPairs = splitCookiePairs( jarValue );

                std::vector< std::string > names;

                for( std::size_t i = 0U; i < callerPairs.size(); ++i )
                {
                    names.push_back( cookiePairName( callerPairs[ i ] ) );
                }

                std::string result;

                for( std::size_t i = 0U; i < callerPairs.size(); ++i )
                {
                    appendCookiePair( result, callerPairs[ i ] );
                }

                for( std::size_t i = 0U; i < jarPairs.size(); ++i )
                {
                    if( contains( names, cookiePairName( jarPairs[ i ] ) ) )
                    {
                        continue;
                    }

                    appendCookiePair( result, jarPairs[ i ] );
                }

                return result;
            }

            /**
             * @brief The headers one request goes out with - the profile's set for its kind, the
             * caller's own, and the one Cookie field
             *
             * @param callerHeaders what the caller put on the request, minus anything a redirect
             * has already dropped
             * @param jarCookieValue CookieJar::cookieHeaderValue( ) for this hop's URL, or empty
             */

            static http::HeaderList buildRequestHeaders(
                SAA_in          const http::HeaderList&                         callerHeaders,
                SAA_in          const HeaderProfileForKind*                     kindProfile,
                SAA_in          const std::vector< std::string >&               profileCodings,
                SAA_in          const std::vector< std::string >&               registeredCodings,
                SAA_in          const bool                                      isStrict,
                SAA_in          const std::string&                              jarCookieValue
                )
            {
                http::HeaderList result;

                std::string callerCookieValue;

                if( const auto* const value = callerHeaders.tryGet( g_headerCookie ) )
                {
                    callerCookieValue = *value;
                }

                /*
                 * The profile's defaults first, in the profile's own order, with accept-encoding
                 * computed rather than copied and a default the caller also names taking the
                 * caller's value IN THE PROFILE'S POSITION - placement is part of the fingerprint
                 * (design 6.5), so a caller header which the browser also sends belongs where the
                 * browser sends it
                 */

                std::vector< std::string > placed;

                if( kindProfile )
                {
                    for( std::size_t i = 0U; i < kindProfile -> defaultHeaders.size(); ++i )
                    {
                        const auto& header = kindProfile -> defaultHeaders[ i ];

                        if( isIgnoredCallerHeader( header.name ) )
                        {
                            continue;
                        }

                        if( equalsIgnoreCase( header.name, g_headerAcceptEncoding ) )
                        {
                            const auto value = acceptEncodingValue(
                                profileCodings,
                                registeredCodings,
                                isStrict
                                );

                            if( ! value.empty() )
                            {
                                result.append( cpp::copy( header.name ), cpp::copy( value ) );
                                placed.push_back( toLowerAsciiCopy( header.name ) );
                            }

                            continue;
                        }

                        const auto* const callerValue = callerHeaders.tryGet( header.name );

                        result.append(
                            cpp::copy( header.name ),
                            callerValue ? cpp::copy( *callerValue ) : cpp::copy( header.value )
                            );

                        placed.push_back( toLowerAsciiCopy( header.name ) );
                    }
                }

                /*
                 * Then the caller's own, in the caller's order, wherever the profile says they go
                 */

                const auto placement = kindProfile
                    ? kindProfile -> callerHeaderPlacement.value()
                    : CallerHeaderPlacement::Appended;

                const auto anchor = kindProfile ? kindProfile -> callerHeaderAnchor : std::string();

                http::HeaderList callerRemaining;

                for( std::size_t i = 0U; i < callerHeaders.size(); ++i )
                {
                    const auto& header = callerHeaders.at( i );

                    if( isIgnoredCallerHeader( header.name() ) )
                    {
                        continue;
                    }

                    if( contains( placed, toLowerAsciiCopy( header.name() ) ) )
                    {
                        continue;
                    }

                    callerRemaining.append( cpp::copy( header.name() ), cpp::copy( header.value() ) );
                }

                auto merged = spliceCallerHeaders( result, callerRemaining, placement, anchor );

                /*
                 * And exactly one Cookie field, last, so that a caller reading the wire sees it
                 * where a browser puts it - after the request's own headers
                 */

                const auto cookieValue = mergeCookieValues( callerCookieValue, jarCookieValue );

                if( ! cookieValue.empty() )
                {
                    merged.append( cpp::copy( g_headerCookie ), cpp::copy( cookieValue ) );
                }

                return merged;
            }

            /**
             * @brief The HTTP/1.1 casing of a header list, from the profile's case map
             *
             * Under HTTP/2 every name is lower case (RFC 9113 8.2.1), so this is the HTTP/1.1 path
             * only - which is also the only path on which casing is observable at all
             */

            static http::HeaderList applyHttp1Casing(
                SAA_in          const http::HeaderList&                         headers,
                SAA_in          const std::map< std::string, std::string >&     caseMap
                )
            {
                if( caseMap.empty() )
                {
                    return headers;
                }

                http::HeaderList result;

                for( std::size_t i = 0U; i < headers.size(); ++i )
                {
                    const auto& header = headers.at( i );

                    const auto pos = caseMap.find( toLowerAsciiCopy( header.name() ) );

                    result.append(
                        pos == caseMap.end() ? cpp::copy( header.name() ) : cpp::copy( pos -> second ),
                        cpp::copy( header.value() )
                        );
                }

                return result;
            }

        private:

            static char toLowerAsciiChar( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

            static std::string toLowerAsciiCopy( SAA_in const std::string& text )
            {
                std::string result( text );

                for( std::size_t i = 0U; i < result.size(); ++i )
                {
                    result[ i ] = toLowerAsciiChar( result[ i ] );
                }

                return result;
            }

            static bool equalsIgnoreCase(
                SAA_in          const std::string&                              lhs,
                SAA_in          const std::string&                              rhs
                ) NOEXCEPT
            {
                if( lhs.size() != rhs.size() )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < lhs.size(); ++i )
                {
                    if( toLowerAsciiChar( lhs[ i ] ) != toLowerAsciiChar( rhs[ i ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            static bool contains(
                SAA_in          const std::vector< std::string >&               values,
                SAA_in          const std::string&                              value
                )
            {
                for( std::size_t i = 0U; i < values.size(); ++i )
                {
                    if( equalsIgnoreCase( values[ i ], value ) )
                    {
                        return true;
                    }
                }

                return false;
            }

            static std::string trimOws( SAA_in const std::string& text )
            {
                std::size_t begin = 0U;
                std::size_t end = text.size();

                while( begin < end && ( ' ' == text[ begin ] || '\t' == text[ begin ] ) )
                {
                    ++begin;
                }

                while( end > begin && ( ' ' == text[ end - 1U ] || '\t' == text[ end - 1U ] ) )
                {
                    --end;
                }

                return text.substr( begin, end - begin );
            }

            static void appendCookiePair(
                SAA_inout       std::string&                                    result,
                SAA_in          const std::string&                              pair
                )
            {
                if( ! result.empty() )
                {
                    result += "; ";
                }

                result += pair;
            }

            /**
             * @brief The header names a caller may not set, because the session or the driver owns
             * them
             *
             * cookie is not here: it is merged rather than refused, which is the whole of the
             * merge above.
             *
             * proxy-authorization IS here, and that is the L2 review's second item settled.
             * Proxy credentials in this client are SESSION CONFIGURATION applied by the tunnel
             * stage (tasks::ProxyConfig, design 3.6) and never a request header - and RFC 9110
             * 11.7.1 makes Proxy-Authorization hop-by-hop, so a caller-supplied one on a request
             * which goes through a CONNECT tunnel is a credential sent to the ORIGIN rather than
             * to the proxy. Dropping it here is what makes RedirectPolicy::dropCredentialHeaders(
             * )'s removal of the same field a no-op by construction, so the tunnel stage and the
             * redirect policy agree rather than merely happening not to disagree
             */

            static bool isIgnoredCallerHeader( SAA_in const std::string& name )
            {
                return
                    equalsIgnoreCase( name, g_headerCookie ) ||
                    equalsIgnoreCase( name, g_headerProxyAuthorization );
            }

            static http::HeaderList spliceCallerHeaders(
                SAA_in          const http::HeaderList&                         profileHeaders,
                SAA_in          const http::HeaderList&                         callerHeaders,
                SAA_in          const CallerHeaderPlacement                     placement,
                SAA_in          const std::string&                              anchor
                )
            {
                http::HeaderList result;

                if( CallerHeaderPlacement::Prepended == placement )
                {
                    appendAll( result, callerHeaders );
                    appendAll( result, profileHeaders );

                    return result;
                }

                if( CallerHeaderPlacement::BeforeAnchor == placement && ! anchor.empty() )
                {
                    bool isPlaced = false;

                    for( std::size_t i = 0U; i < profileHeaders.size(); ++i )
                    {
                        const auto& header = profileHeaders.at( i );

                        if( ! isPlaced && equalsIgnoreCase( header.name(), anchor ) )
                        {
                            appendAll( result, callerHeaders );

                            isPlaced = true;
                        }

                        result.append( cpp::copy( header.name() ), cpp::copy( header.value() ) );
                    }

                    if( ! isPlaced )
                    {
                        appendAll( result, callerHeaders );
                    }

                    return result;
                }

                appendAll( result, profileHeaders );
                appendAll( result, callerHeaders );

                return result;
            }

            static void appendAll(
                SAA_inout       http::HeaderList&                               target,
                SAA_in          const http::HeaderList&                         source
                )
            {
                for( std::size_t i = 0U; i < source.size(); ++i )
                {
                    const auto& header = source.at( i );

                    target.append( cpp::copy( header.name() ), cpp::copy( header.value() ) );
                }
            }

            static const std::string                                            g_headerCookie;
            static const std::string                                            g_headerAcceptEncoding;
            static const std::string                                            g_headerProxyAuthorization;
            static const std::string                                            g_h2OnlyKeyMarker;
        };

        BL_DEFINE_STATIC_CONST_STRING( SessionHeadersT, g_headerCookie )            = "cookie";
        BL_DEFINE_STATIC_CONST_STRING( SessionHeadersT, g_headerAcceptEncoding )    = "accept-encoding";
        BL_DEFINE_STATIC_CONST_STRING( SessionHeadersT, g_headerProxyAuthorization ) = "proxy-authorization";
        BL_DEFINE_STATIC_CONST_STRING( SessionHeadersT, g_h2OnlyKeyMarker )         = "#h2-only";

        typedef SessionHeadersT<>                                               SessionHeaders;

        /******************************************************************************************
         * ============================== SessionRequestTaskT =====================================
         */

        /**
         * @brief One request through a session, redirects included - design 5.6
         *
         * A WRAPPER TASK AND NOT ONE REQUEST TASK, because following a redirect means sending a
         * SECOND request: HttpClientRequestTaskT is one request over one connection by
         * construction (design 5.3) and must stay that way. The library's own idiom for "one task
         * which runs a sequence of tasks" is WrapperTaskBase plus continuationTask( ), which is
         * what RetryableWrapperTaskT does for a retry, and this is the same shape for a hop
         *
         * WHAT RUNS BETWEEN TWO HOPS, and it is all of the session's own work: the response's
         * Set-Cookie fields go into the jar, the body is decoded if a decoder is registered for
         * its coding, the redirect policy decides, and the next request is built from the
         * decision - with the credentials dropped on a cross-origin hop and the Cookie field
         * recomputed for the new target
         *
         * A CALLER'S BodySink AND A REDIRECT ARE MUTUALLY EXCLUSIVE, deliberately. BodySink
         * carries no status (ClientTypes.h: onData and onComplete, and nothing else), so a hop's
         * body cannot be told from the final one on the way through, and a streamed 302 body
         * would reach the caller's sink as if it were the answer. So a request with a sink
         * installed does not follow redirects: its 3xx is handed back, which is the behaviour of
         * the existing client and of this one with redirects off
         */

        template
        <
            typename E = void
        >
        class SessionRequestTaskT :
            public tasks::WrapperTaskBase,
            public ClientRequestTask
        {
            BL_DECLARE_OBJECT_IMPL( SessionRequestTaskT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( ClientRequestTask )
                BL_QITBL_ENTRY_CHAIN_BASE( tasks::WrapperTaskBase )
            BL_QITBL_END( tasks::Task )

        public:

            typedef SessionRequestTaskT< E >                                    this_type;
            typedef tasks::WrapperTaskBase                                      base_type;

        protected:

            const SessionRequestPlan                                            m_plan;
            const om::ObjPtrCopyable< BodySink >                                m_bodySink;

            /*
             * The caller's request, carried forward from hop to hop: its URL and method are the
             * next hop's, and its headers are the caller's own with whatever a cross-origin hop
             * has already dropped. The request which actually went out - the caller's, plus the
             * profile's set and the one Cookie field - is m_request
             */

            ClientRequest                                                       m_next;
            ClientRequest                                                       m_request;
            ClientResponse                                                      m_response;

            cpp::ScalarTypeIniter< std::size_t >                                m_hops;

            /**
             * @brief How many attempts THIS hop has already spent - the dispatched half of the
             * retry of design 5.4
             */

            cpp::ScalarTypeIniter< std::size_t >                                m_attempts;

            /**
             * @brief The instant the WHOLE chain must be done by, or not_a_date_time for no bound
             *
             * Design 5.7's "request total, including pool wait" is a deadline for the REQUEST, and
             * a request through a session is a chain of hops and attempts each of which is a fresh
             * HttpClientRequestTaskT arming its own full total timer. Without a deadline chained
             * through them the bound is the per-hop timeout times the retry budget times the hop
             * limit - thirty minutes becomes forty hours, and a peer which answers slowly and then
             * redirects can hold a caller for as long as it likes
             */

            const time::ptime                                                   m_deadline;

            om::ObjPtr< HttpClientRequestTaskImpl >                             m_hop;

            /*
             * The cancel latch of the wrapper itself, for the reason RetryableWrapperTaskT states:
             * the forwarding requestCancel( ) reaches only the hop which happens to be wrapped at
             * the time of the call, and the operation this task represents is the whole chain
             */

            std::atomic< bool >                                                 m_cancelRequested;

            SessionRequestTaskT(
                SAA_in              SessionRequestPlan                          plan,
                SAA_in              ClientRequest                               request,
                SAA_in_opt          om::ObjPtrCopyable< BodySink >              bodySink =
                                        om::ObjPtrCopyable< BodySink >()
                )
                :
                m_plan( BL_PARAM_FWD( plan ) ),
                m_bodySink( BL_PARAM_FWD( bodySink ) ),
                m_next( BL_PARAM_FWD( request ) ),
                m_deadline( deadlineFor( m_next, m_plan.requestConfig ) ),
                m_cancelRequested( false )
            {
                startHop();
            }

            /**
             * @brief When the chain's budget runs out, from the caller's request and the config
             *
             * The effective duration is the request's own when it set one and the session's
             * otherwise, which is HttpClientRequestTaskT::effectiveTimeout( )'s rule - it is not
             * reachable from here, and stating it twice is cheaper than opening that class
             *
             * A duration which would not ARM A TIMER at the hop means no deadline here either:
             * unset, neg_infin and non-positive already mean "no total timeout" one layer down,
             * and a chain deadline which bound what the hop's own timer does not would be a
             * behaviour this client never had
             */

            static auto deadlineFor(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const HttpClientRequestConfig&                  config
                )
                -> time::ptime
            {
                const auto total =
                    request.totalTimeout().is_special()
                        ? config.totalTimeout
                        : request.totalTimeout();

                if( ! HttpClientRequestConfig::isArmed( total ) )
                {
                    return time::ptime();
                }

                return time::microsec_clock::universal_time() + total;
            }

            /**
             * @brief What is left of the chain's budget, or neg_infin when it has none
             *
             * @throw TimeoutException when the budget is spent - which is the chain's own way of
             * ending, and is marked expected for the reason the hop's timeout is: a deadline
             * which was reached is not a defect to be logged as a failure
             */

            auto chkRemainingBudget() const -> time::time_duration
            {
                if( m_deadline.is_special() )
                {
                    return time::neg_infin;
                }

                const auto remaining = m_deadline - time::microsec_clock::universal_time();

                BL_CHK_T(
                    false,
                    HttpClientRequestConfig::isArmed( remaining ),
                    TimeoutException() << eh::errinfo_is_expected( true ),
                    BL_MSG()
                        << "HTTP "
                        << m_next.method()
                        << " request to '"
                        << m_next.url().toString()
                        << "' has timed out - the request's budget was spent by the "
                        << m_hops.value()
                        << " redirect hop(s) and the attempts before it"
                    );

                return remaining;
            }

            /**
             * @brief The key one hop's URL resolves to - see SessionHeaders::keyFor( )
             */

            auto keyFor( SAA_in const ClientRequest& request ) const -> ConnectionKey
            {
                return SessionHeaders::keyFor(
                    request,
                    m_plan.templateKey,
                    m_plan.isProtocolNegotiated.value()
                    );
            }

            /**
             * @brief Builds the request which actually goes out for one hop
             */

            auto prepareRequest( SAA_in const ClientRequest& next ) const -> ClientRequest
            {
                ClientRequest request( next );

                const HeaderProfileForKind* kindProfile = nullptr;

                const auto pos = m_plan.headerProfile.byRequestKind.find( next.kind() );

                if( pos != m_plan.headerProfile.byRequestKind.end() )
                {
                    kindProfile = &pos -> second;
                }

                /*
                 * isHttpApi is TRUE from this client and there is no second answer to give - see
                 * the note at ClientSessionT on RFC 6265 5.3 step 11
                 */

                const auto jarCookieValue =
                    m_plan.state -> cookieJar.cookieHeaderValue( next.url() );

                auto headers = SessionHeaders::buildRequestHeaders(
                    next.headers(),
                    kindProfile,
                    m_plan.headerProfile.acceptEncoding,
                    m_plan.state -> decoders.registeredCodings(),
                    m_plan.isStrictContentEncoding.value(),
                    jarCookieValue
                    );

                request.headers( std::move( headers ) );

                return request;
            }

            void startHop()
            {
                /*
                 * ASKED FOR FIRST, because it can throw and nothing of this task's state may have
                 * moved when it does - the queue turns a throw out of continuationTask( ) into
                 * this task's failure, and a half started hop would be observed by whatever looks
                 * at it afterwards
                 */

                const auto remaining = chkRemainingBudget();

                m_request = prepareRequest( m_next );

                if( HttpClientRequestConfig::isArmed( remaining ) )
                {
                    /*
                     * Stamped on the request which actually GOES OUT and not on m_next, which is
                     * the caller's own and is carried forward from hop to hop: what each hop gets
                     * is what was left when it started, and the caller's own value is what the
                     * whole chain was budgeted from
                     */

                    m_request.totalTimeout( remaining );
                }

                m_hop = HttpClientRequestTaskImpl::createInstance(
                    cpp::copy( m_request ),
                    keyFor( m_request ),
                    om::copy( m_plan.pool ),
                    m_plan.requestConfig,
                    m_bodySink
                    );

                /*
                 * THE WRAPPED TASK IS ASSIGNED DIRECTLY AND NOT THROUGH setWrappedTask( ), and
                 * that is not a shortcut. This runs from continuationTask( ) with the wrapper's
                 * own lock already held - which is what makes the decision and the replacement
                 * atomic against a concurrent requestCancel( ) - and setWrappedTask( ) takes the
                 * same lock, which os::mutex does not allow twice. RetryableWrapperTaskT says the
                 * same thing where it swaps its own; found here by the redirect chain hanging on
                 * its first run rather than by reading
                 */

                base_type::m_wrappedTask = om::qi< tasks::Task >( m_hop );

                m_attempts = m_attempts.value() + 1U;
            }

            /**
             * @brief THE DISPATCHED HALF OF THE RETRY - design 5.4, and it is this slice's
             *
             * The pool replays what was still QUEUED behind a connection which failed; nothing
             * replayed what had already been DISPATCHED, because acquire( ) takes a ClientRequest
             * by reference and releaseStream( ) names a handle the pool never issued, so the pool
             * has no identity to count attempts against. This task has one by construction, and
             * chkRequestMayBeReplayed( ) is the rule both halves share
             *
             * IT IS NOT AN OPTIMIZATION, AND THE ALPN FALLBACK IS WHY. The pool dispatches the
             * first request of a key onto the Connecting placeholder so that its HEADERS ride the
             * preface (design 5.4). When the peer then selects http/1.1, the HTTP/2 task hands the
             * connected stream to the HTTP/1.1 driver and completes - and answers the rider it is
             * holding with connection_aborted, correctly flagged retryable because not a byte of
             * it was written. Without this, EVERY first request over a fallback connection fails,
             * which is what the first end-to-end run against the library's own HttpServer showed
             */

            bool chkPrepareRetry()
            {
                RetryContext context;

                context.isRetryable = m_hop -> isRetryable();
                context.isConnectionLost =
                    ( RequestOutcome::ConnectionUnusable == m_hop -> outcome() );
                context.attempts = m_attempts;

                if( ! chkRequestMayBeReplayed( m_next, context, m_plan.policy ) )
                {
                    return false;
                }

                if( nullptr != m_next.bodySource() )
                {
                    /*
                     * chkRequestMayBeReplayed( ) has already refused a source which cannot rewind
                     */

                    m_next.bodySource() -> rewind();
                }

                return true;
            }

            /**
             * @brief Everything the session does to a response before the caller sees it
             */

            void absorbResponse()
            {
                m_response = m_hop -> response();

                if( 0U == m_response.status() )
                {
                    /*
                     * The hop never got an answer, so there is nothing to store and nothing to
                     * decode; its exception is what the caller will see
                     */

                    return;
                }

                storeCookies();

                decodeBody();
            }

            void storeCookies()
            {
                const auto values = m_response.headers().getAll( g_headerSetCookie );

                for( std::size_t i = 0U; i < values.size(); ++i )
                {
                    ( void ) m_plan.state -> cookieJar.setCookie( m_request.url(), values[ i ] );
                }
            }

            /**
             * @brief Decodes the buffered body when a decoder is registered for its coding
             *
             * NOT DONE IN STRICT MODE and not done for a streamed body. Strict mode exists to hand
             * the caller the exact bytes with their content-encoding (design 6.5), and a streamed
             * body has already reached the caller's sink by the time this runs
             */

            void decodeBody()
            {
                if( m_plan.isStrictContentEncoding || m_bodySink )
                {
                    return;
                }

                const auto* const coding = m_response.headers().tryGet( g_headerContentEncoding );

                if( ! coding || coding -> empty() )
                {
                    return;
                }

                if( ! m_plan.state -> decoders.hasDecoder( *coding ) )
                {
                    /*
                     * A coding this client never advertised. It is NOT decoded and it is NOT
                     * hidden either: the body and its content-encoding are handed on exactly as
                     * they arrived, which is what a caller needs in order to see what happened.
                     * The registry's own createStream( ) refusal is for the path which asked for a
                     * decoder; this path did not
                     */

                    return;
                }

                std::string decoded;

                auto stream = m_plan.state -> decoders.createStream(
                    *coding,
                    [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                    {
                        decoded.append(
                            output -> begin() + output -> offset1(),
                            output -> begin() + output -> size()
                            );
                    }
                    );

                if( m_response.body() )
                {
                    stream.write( m_response.body() );
                }

                stream.finish();

                m_response.body(
                    om::ObjPtrCopyable< data::DataBlock >(
                        data::DataBlock::copy(
                            decoded.c_str(),
                            decoded.size(),
                            nullptr /* dataBlocksPool */,
                            std::max< std::size_t >( decoded.size(), 1U )
                            )
                        )
                    );

                /*
                 * Both fields describe the coded body and neither is true of the decoded one -
                 * a content-length which says what the compressed body measured is worse than no
                 * content-length at all
                 */

                ( void ) m_response.headers().removeAll( g_headerContentEncoding );
                ( void ) m_response.headers().removeAll( g_headerContentLength );
            }

            /**
             * @brief Whether a next hop follows, and what it is
             */

            bool chkPrepareNextHop()
            {
                if( m_bodySink )
                {
                    /*
                     * See the class note: a sink and a redirect cannot both be honoured
                     */

                    return false;
                }

                const auto decision = m_plan.redirectPolicy.evaluate(
                    m_request.url(),
                    m_request.method(),
                    m_response.status(),
                    m_response.headers(),
                    m_next.isReplayable(),
                    m_hops.value()
                    );

                if( ! decision.shouldFollow() )
                {
                    return false;
                }

                if( decision.target.scheme() != m_plan.transportScheme )
                {
                    /*
                     * ONE SESSION SPEAKS ONE SCHEME, AND A SESSION HAS TWO ENTRY POINTS FOR A URL:
                     * the caller's, which createRequestTask( ) refuses, and this one. The policy
                     * refuses only the https-to-http downgrade, so without this an http session
                     * following the commonest redirect on the web would build an https key, hand
                     * it to a factory which only has the CLEARTEXT stream policy, and write the
                     * request head in the clear to port 443 - carrying the Cookie field the jar
                     * computed for the https target, Secure cookies and all
                     *
                     * REFUSED RATHER THAN FAILED, for the reason RedirectPolicy states where it
                     * refuses a target with no origin: a server must not be able to turn a
                     * response into an exception, and a Location is the server's. So this behaves
                     * as every other refusal there does - the chain stops and the caller gets the
                     * 3xx with its Location, which is what the existing client does with a
                     * redirect and is the URL to re-issue on a session of the right scheme
                     */

                    return false;
                }

                auto headers = m_next.headers();

                if( decision.dropCredentials )
                {
                    RedirectPolicy::dropCredentialHeaders( headers );
                }

                m_next.headers( std::move( headers ) );
                m_next.url( cpp::copy( decision.target ) );
                m_next.method( cpp::copy( decision.method ) );

                if( decision.dropBody )
                {
                    m_next.body( om::ObjPtrCopyable< data::DataBlock >() );
                }
                else if( nullptr != m_next.bodySource() )
                {
                    /*
                     * A 307 or a 308 preserves the body, so a streaming source is read a second
                     * time - which the policy only allows for a source which can rewind
                     */

                    m_next.bodySource() -> rewind();
                }

                m_hops = m_hops.value() + 1U;

                return true;
            }

        public:

            virtual void requestCancel() NOEXCEPT OVERRIDE
            {
                m_cancelRequested = true;

                base_type::requestCancel();
            }

            virtual auto continuationTask() -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                auto task = base_type::handleContinuationForward();

                if( task )
                {
                    return task;
                }

                /*
                 * The hop is over. Everything below replaces the wrapped task, so it is under the
                 * same lock which protects it - the reason RetryableWrapperTaskT gives
                 */

                BL_MUTEX_GUARD( base_type::m_lock );

                absorbResponse();

                if( m_cancelRequested )
                {
                    return nullptr;
                }

                if( m_hop -> exception() )
                {
                    if( ! chkPrepareRetry() )
                    {
                        return nullptr;
                    }

                    startHop();

                    return om::copyAs< tasks::Task >( this );
                }

                if( ! chkPrepareNextHop() )
                {
                    return nullptr;
                }

                m_attempts = 0U;

                startHop();

                return om::copyAs< tasks::Task >( this );
            }

            virtual const ClientRequest& request() const NOEXCEPT OVERRIDE
            {
                return m_request;
            }

            virtual const ClientResponse& response() const NOEXCEPT OVERRIDE
            {
                return m_response;
            }

            virtual std::size_t redirectHops() const NOEXCEPT OVERRIDE
            {
                return m_hops;
            }

        private:

            static const std::string                                            g_headerSetCookie;
            static const std::string                                            g_headerContentEncoding;
            static const std::string                                            g_headerContentLength;
        };

        BL_DEFINE_STATIC_CONST_STRING( SessionRequestTaskT, g_headerSetCookie )         = "set-cookie";
        BL_DEFINE_STATIC_CONST_STRING( SessionRequestTaskT, g_headerContentEncoding )   = "content-encoding";
        BL_DEFINE_STATIC_CONST_STRING( SessionRequestTaskT, g_headerContentLength )     = "content-length";

        typedef SessionRequestTaskT<>                                           SessionRequestTask;
        typedef om::ObjectImpl< SessionRequestTask >                            SessionRequestTaskImpl;

        /******************************************************************************************
         * ================================= ClientSessionT =======================================
         */

        /**
         * @brief The session of design 5.6 - the pool, the profile, the proxy configuration, the
         * cookie jar, the redirect policy and the decoder registry, and the thing which makes
         * request tasks out of them
         *
         * ------------------------------------------------------------------------------------
         * WHY IT IS PARAMETERIZED ON THE STREAM POLICY, AND WHAT THAT SETTLES
         * ------------------------------------------------------------------------------------
         *
         * Every layer underneath already is: Http2ConnectionTaskT< STREAM >,
         * Http1ConnectionTaskT< STREAM >, ClientConnectionTaskBaseT< STREAM > and
         * ClientDriverFactoryT< STREAM >. A session which picked the policy per request at run
         * time would have to type-erase all four at this boundary, and every translation unit
         * which named the session would then instantiate BOTH the cleartext and the TLS half of
         * all of them - measured, for the driver alone, at 9.8 MB of object (utf_baselib_h2client3
         * exists because of exactly that number). So the policy is the caller's, as it is for
         * SimpleHttpTask and SimpleHttpSslTask, and a module pays for the transport it names
         *
         * THE CONSEQUENCE IS A PROPERTY AND NOT ONLY A COST: one session speaks ONE scheme, AT
         * BOTH OF THE ENTRY POINTS A URL HAS. createRequestTask( ) refuses a URL whose scheme is
         * not the transport's rather than connecting cleartext to a TLS port, and
         * chkPrepareNextHop( ) refuses a redirect target of another scheme rather than following
         * it there - the redirect policy refuses only the https-to-http downgrade, so http to
         * https, which is the commonest redirect on the web, reaches the session as something it
         * must decide about. That is what settles the third item the L2
         * review left to this slice. CookieJar's isHttpApi is always true from this client -
         * there is no non-HTTP API here for RFC 6265 5.3 step 11 to be about - and the Secure
         * cookie question design 5.6 records (an http response overwriting a Secure cookie) needs
         * one session speaking both schemes to one host, which this type cannot do
         *
         * ------------------------------------------------------------------------------------
         * THE BodySource RULE - "never dispatch a streaming upload to an HTTP/1.1 connection"
         * ------------------------------------------------------------------------------------
         *
         * The HTTP/1.1 driver refuses EVERY request carrying a BodySource ( Http1ConnectionTask.h,
         * tested before it takes its own state lock ), and the request task reports that refusal
         * correctly - but the request has still been dispatched, has burned an attempt against
         * maxRetriesPerRequest and can land on another HTTP/1.1 connection next time. The session
         * is the only layer which knows both the request and what the connection for a key will
         * speak, so the rule is here, in two halves:
         *
         *   - a session whose transport can never produce an HTTP/2 connection REFUSES such a
         *     request outright, in createRequestTask( ), before any attempt is spent;
         *   - a session whose transport negotiates ROUTES it to a key of its own, whose
         *     connections are built with an ALPN offer of "h2" alone. A peer may select only from
         *     what it was offered ( RFC 7301 3.1 ), so such a connection cannot be HTTP/1.1, and a
         *     key of its own is what keeps the ordinary requests to the same origin on the
         *     ordinary connection
         *
         * ------------------------------------------------------------------------------------
         * THE CONNECTION FACTORY IS WHAT CARRIES THE POOL'S POLICY INTO A DRIVER
         * ------------------------------------------------------------------------------------
         *
         * connection_factory_t takes ( key, policy ) for exactly that reason, and this is the only
         * implementation of it outside the pool's own tests. Two of the policy's knobs reach
         * nothing else: drainingReserve becomes Http2ConnectionConfig::limits.drainingReserve and
         * idleTimeout becomes Http2ConnectionConfig::idleTimeout. A knob which reaches nothing is
         * the same defect one layer up, which is what the reserve already was once
         */

        template
        <
            typename STREAM
        >
        class ClientSessionT :
            public ClientSession,
            public om::Disposable
        {
            BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( ClientSessionT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( ClientSession )
                BL_QITBL_ENTRY( om::Disposable )
            BL_QITBL_END( ClientSession )

        public:

            typedef ClientSessionT< STREAM >                                    this_type;

            typedef ClientDriverFactoryT< STREAM >                              driver_factory_t;
            typedef std::shared_ptr< driver_factory_t >                         driver_factory_ptr_t;

            typedef om::ObjectImpl< tasks::Http2ConnectionTaskT< STREAM > >     h2_task_impl_t;
            typedef tasks::Http1ConnectionTaskImpl< STREAM >                    h1_task_impl_t;

        protected:

            ClientSessionConfig                                                 m_config;

            const driver_factory_ptr_t                                          m_driverFactory;
            const om::ObjPtrCopyable< SessionState >                            m_state;

            om::ObjPtr< ConnectionPool >                                        m_pool;

            RedirectPolicy                                                      m_redirectPolicy;

            cpp::ScalarTypeIniter< bool >                                       m_isDisposed;

            ClientSessionT( SAA_in_opt ClientSessionConfig config = ClientSessionConfig() )
                :
                m_config( BL_PARAM_FWD( config ) ),
                m_driverFactory( makeDriverFactory( m_config.http1Limits ) ),
                m_state(
                    om::ObjPtrCopyable< SessionState >(
                        SessionStateImpl::template createInstance< SessionState >()
                        )
                    )
            {
                m_pool = om::qi< ConnectionPool >(
                    ConnectionPoolImpl::createInstance(
                        makeConnectionFactory(
                            m_driverFactory,
                            m_config.http2Config,
                            m_config.connectionConfig,
                            m_config.proxyConfig
                            ),
                        m_config.poolPolicy
                        )
                    );
            }

            ~ClientSessionT() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                disposeInternal();

                BL_NOEXCEPT_END()
            }

            void disposeInternal() NOEXCEPT
            {
                if( m_isDisposed )
                {
                    return;
                }

                m_isDisposed = true;

                if( m_pool )
                {
                    const auto disposable = om::tryQI< om::Disposable >( m_pool );

                    if( disposable )
                    {
                        disposable -> dispose();
                    }
                }
            }

            /**
             * @brief The driver factory of design 5.5 - what the ALPN fallback goes through
             *
             * Only HTTP/1.1 is registered, and that is not an omission: an HTTP/2 connection task
             * IS the connection and never goes through the factory for itself, because a driver
             * which attachStream( )s a stream created elsewhere loses that policy's strand
             */

            static auto makeDriverFactory( SAA_in const Http1ResponseLimits& limits )
                -> driver_factory_ptr_t
            {
                auto factory = std::make_shared< driver_factory_t >();

                factory -> registerDriver(
                    HttpProtocol::Http11,
                    [ limits ](
                        SAA_in          const NegotiatedProtocol&               negotiated,
                        SAA_inout       typename STREAM::stream_ref&&           connectedStream,
                        SAA_in          const ConnectionKey&                    key
                        )
                        -> om::ObjPtr< ClientConnection >
                    {
                        return om::qi< ClientConnection >(
                            h1_task_impl_t::createInstance(
                                cpp::copy( negotiated ),
                                BL_PARAM_FWD( connectedStream ),
                                cpp::copy( key ),
                                limits
                                )
                            );
                    }
                    );

                return factory;
            }

        public:

            /**
             * @brief Narrows a connection configuration to HTTP/2 only - the BodySource rule
             *
             * Both fields are set although only one of them is consulted for a given policy: a
             * cleartext connection reads cleartextProtocol and never the ALPN offer, and a TLS
             * connection the other way round, so setting both is what makes one function serve
             * both policies without a compile-time branch
             */

            static void narrowToHttp2( SAA_inout tasks::ClientConnectionConfig& config )
            {
                config.alpnOffer.clear();
                config.alpnOffer.push_back( g_alpnHttp2 );

                config.cleartextProtocol = HttpProtocol::Http2;
            }

            /**
             * @brief The pool's connection factory - see the class note
             *
             * Static, and capturing only values, deliberately: a factory which captured the
             * session would be a cycle through the pool the session owns, and the pool calls it
             * from a thread of its own after the lock has been dropped
             */

            static auto makeConnectionFactory(
                SAA_in          driver_factory_ptr_t                            driverFactory,
                SAA_in          tasks::Http2ConnectionConfig                    http2Config,
                SAA_in          tasks::ClientConnectionConfig                   connectionConfig,
                SAA_in          tasks::ProxyConfig                              proxyConfig
                )
                -> connection_factory_t
            {
                return [ driverFactory, http2Config, connectionConfig, proxyConfig ](
                    SAA_in          const ConnectionKey&                        key,
                    SAA_in          const ConnectionPoolPolicy&                 policy
                    )
                    -> ConnectionAttempt
                {
                    auto h2config = http2Config;

                    /*
                     * THE TWO POLICY KNOBS WHICH REACH NOTHING ELSE - see the class note
                     */

                    h2config.limits.drainingReserve = policy.drainingReserve;
                    h2config.idleTimeout = policy.idleTimeout;

                    auto config = connectionConfig;

                    if( SessionHeaders::isH2OnlyKey( key ) )
                    {
                        narrowToHttp2( config );
                    }

                    auto task = h2_task_impl_t::createInstance(
                        cpp::copy( key ),
                        driverFactory,
                        std::move( h2config ),
                        proxyConfig,
                        std::move( config )
                        );

                    const om::ObjPtrCopyable< h2_task_impl_t > held( task );

                    ConnectionAttempt attempt;

                    attempt.task = om::ObjPtrCopyable< tasks::Task >(
                        om::qi< tasks::Task >( task )
                        );

                    attempt.driver = [ held ]() -> om::ObjPtr< ClientConnection >
                    {
                        return om::copy( held -> connection() );
                    };

                    return attempt;
                };
            }

            /**
             * @brief Whether this session's transport can ever produce an HTTP/2 connection
             */

            bool canCarryBodySource() const NOEXCEPT
            {
                return mayProduceHttp2();
            }

            virtual auto createRequestTask(
                SAA_in          const ClientRequest&                            request,
                SAA_in_opt      const om::ObjPtrCopyable< BodySink >&           bodySink =
                                    om::ObjPtrCopyable< BodySink >()
                )
                -> om::ObjPtr< ClientRequestTask > OVERRIDE
            {
                /*
                 * Called for the refusal and not for the value: an absolute URL with a host is
                 * what a key needs, and ConnectionKey::fromUri( ) says why
                 */

                ( void ) request.url().origin();

                BL_CHK_T(
                    false,
                    transportScheme() == request.url().scheme(),
                    NotSupportedException(),
                    BL_MSG()
                        << "This session speaks '"
                        << transportScheme()
                        << "' and was given a '"
                        << request.url().scheme()
                        << "' URL - a session speaks one scheme (ClientSessionT)"
                    );

                BL_CHK_T(
                    false,
                    nullptr == request.bodySource() || canCarryBodySource(),
                    NotSupportedException(),
                    BL_MSG()
                        << "A request with a streaming body source needs an HTTP/2 connection, "
                        << "and this session's transport cannot produce one - the HTTP/1.1 "
                        << "driver refuses every such request"
                    );

                SessionRequestPlan plan;

                plan.pool = om::ObjPtrCopyable< ConnectionPool >( m_pool );
                plan.state = m_state;

                plan.requestConfig = m_config.requestConfig;
                plan.policy = m_config.poolPolicy;
                plan.redirectPolicy = m_redirectPolicy;
                plan.headerProfile = m_config.headerProfile;

                plan.templateKey.tlsProfileId = m_config.tlsProfileId;
                plan.templateKey.http2ProfileId = m_config.http2ProfileId;
                plan.templateKey.proxyId = m_config.proxyConfig.proxyId();
                plan.templateKey.verificationFlags = m_config.verificationFlags;

                plan.transportScheme = transportScheme();

                plan.isStrictContentEncoding = m_config.isStrictContentEncoding.value();
                plan.isProtocolNegotiated = mayProduceHttp2() && mayProduceHttp11();

                return om::qi< ClientRequestTask >(
                    SessionRequestTaskImpl::createInstance(
                        std::move( plan ),
                        cpp::copy( request ),
                        bodySink
                        )
                    );
            }

            virtual const std::string& transportScheme() const NOEXCEPT OVERRIDE
            {
                return STREAM::isProtocolHandshakeNeeded ? g_schemeHttps : g_schemeHttp;
            }

            virtual CookieJar& cookieJar() NOEXCEPT OVERRIDE
            {
                return m_state -> cookieJar;
            }

            virtual ContentDecoderRegistry& decoders() NOEXCEPT OVERRIDE
            {
                return m_state -> decoders;
            }

            virtual RedirectPolicy& redirectPolicy() NOEXCEPT OVERRIDE
            {
                return m_redirectPolicy;
            }

            virtual const HeaderProfile& profile() const NOEXCEPT OVERRIDE
            {
                return m_config.headerProfile;
            }

            virtual void profile( SAA_in HeaderProfile profile ) OVERRIDE
            {
                m_config.headerProfile = BL_PARAM_FWD( profile );
            }

            virtual const om::ObjPtr< ConnectionPool >& pool() const NOEXCEPT OVERRIDE
            {
                return m_pool;
            }

            virtual const ClientSessionConfig& config() const NOEXCEPT OVERRIDE
            {
                return m_config;
            }

            virtual void dispose() OVERRIDE
            {
                disposeInternal();
            }

        protected:

            bool mayProduceHttp2() const NOEXCEPT
            {
                if( ! STREAM::isProtocolHandshakeNeeded )
                {
                    return HttpProtocol::Http2 == m_config.connectionConfig.cleartextProtocol.value();
                }

                return offers( g_alpnHttp2 );
            }

            bool mayProduceHttp11() const NOEXCEPT
            {
                if( ! STREAM::isProtocolHandshakeNeeded )
                {
                    return HttpProtocol::Http2 != m_config.connectionConfig.cleartextProtocol.value();
                }

                return m_config.connectionConfig.alpnOffer.empty() || offers( g_alpnHttp11 );
            }

            bool offers( SAA_in const std::string& identifier ) const NOEXCEPT
            {
                const auto& offer = m_config.connectionConfig.alpnOffer;

                return offer.end() != std::find( offer.begin(), offer.end(), identifier );
            }

        private:

            static const std::string                                            g_schemeHttp;
            static const std::string                                            g_schemeHttps;
            static const std::string                                            g_alpnHttp2;
            static const std::string                                            g_alpnHttp11;
        };

        BL_DEFINE_STATIC_CONST_STRING( ClientSessionT, g_schemeHttp )    = "http";
        BL_DEFINE_STATIC_CONST_STRING( ClientSessionT, g_schemeHttps )   = "https";
        BL_DEFINE_STATIC_CONST_STRING( ClientSessionT, g_alpnHttp2 )     = "h2";
        BL_DEFINE_STATIC_CONST_STRING( ClientSessionT, g_alpnHttp11 )    = "http/1.1";

        /**
         * @brief The session of design 5.8, per stream policy
         *
         * The sketch there names one ClientSessionImpl; the type is parameterized for the reason
         * the class note gives, so the cleartext session is
         * ClientSessionImplT< tasks::TcpSocketAsyncStrandedBase > and the TLS one is
         * ClientSessionImplT< tasks::TcpSslSocketAsyncStrandedBase >
         */

        template
        <
            typename STREAM
        >
        using ClientSessionImplT = om::ObjectImpl< ClientSessionT< STREAM > >;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_CLIENTSESSION_H_ */
