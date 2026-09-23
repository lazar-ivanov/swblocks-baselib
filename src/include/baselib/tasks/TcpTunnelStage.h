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

#ifndef __BL_TASKS_TCPTUNNELSTAGE_H_
#define __BL_TASKS_TCPTUNNELSTAGE_H_

#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/core/SerializationUtils.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bl
{
    namespace tasks
    {
        /*
         * The tunnel stage - notes/plans/http2-design.md 3.6 (D5, D18, D19), slice S3.5
         *
         * A tunnel runs after the TCP connect and before the protocol handshake, in cleartext on
         * the LOWEST layer of the stream, and turns the connection to the proxy into a connection
         * to the origin. Two protocols: HTTP CONNECT, and SOCKS5 per RFC 1928 with the
         * username/password subnegotiation of RFC 1929
         *
         * WHERE IT RUNS. TcpConnectionEstablisherConnector::beginPreHandshakeStage is the hook
         * (TcpBaseTasks.h, added by S0.2); the whole of this file's task-facing part is the
         * override of it in TcpTunnelStageT below. Nothing else about connection establishment
         * moves
         *
         * THE ONE THING THAT SPLITS. With a proxy the resolver targets the PROXY - that is what
         * the establisher's host and port are - while SNI and certificate verification stay with
         * the ORIGIN. The whole of that split is one argument:
         * TcpConnectionEstablisherConnector::continueAfterResolved calls
         * createSocket( aioService, m_query.host_name(), m_query.service_name() ), and for the TLS
         * policy that host name is both the SNI sent (TcpSslBaseTasks.h, SSL_set_tlsext_host_name
         * in configureClientStream) and the name the peer certificate is verified against
         * (AsioSslStreamWrapper.h, TlsPeerVerification::verifyPeerName( ..., m_hostName, ... )).
         * So overriding that one call with the origin name is the entire mechanism, and it is why
         * design 3.6 records only that continueAfterResolved has to be virtual
         *
         * CLEARTEXT ON THE LOWEST LAYER, which is getSocket() and never getStream().
         * TcpSocketAsyncBaseT::getSocket() is the tcp::socket itself and
         * TcpSslSocketAsyncBaseT::getSocket() forwards to the wrapper's lowest_layer_type, so the
         * same expression is the raw socket under both policies - which is also why the base's own
         * continueAfterResolved can async_connect on it. getStream() under the TLS policy is the
         * SSL stream, and writing a CONNECT request into it would encrypt it for a handshake that
         * has not happened
         *
         * HTTPS PROXIES ARE OUT OF SCOPE (D5). Speaking TLS to the proxy and TLS again to the
         * origin is a stream layered on a stream, and AsioSslStreamWrapper is hard-wired to a TCP
         * socket
         *
         * SANS-I/O, LIKE THE PROTOCOL CORES OF 2.1. The negotiation itself - the bytes out, the
         * bytes in, the state machine, every bound and every refusal - is in TunnelNegotiation
         * below and knows nothing of Asio, of tasks or of locks. TcpTunnelStageT is the thin shell
         * that moves bytes between that engine and the socket. The engine is therefore testable
         * byte for byte without a socket, and only the shell is a template
         */

        /**
         * @brief The tunnelling protocol a proxy speaks
         */

        enum class ProxyProtocol : std::uint8_t
        {
            None,
            HttpConnect,
            Socks5,
        };

        namespace detail
        {
            /**
             * @brief Refuses a host name which cannot be put on a tunnel's wire safely
             *
             * THIS IS A SECURITY CHECK AND NOT TIDINESS. The HTTP tunnel writes the host into a
             * request line and into a Host field, so a CR or an LF in it is request splitting: the
             * caller would be choosing what the proxy reads as the next header, or as the next
             * request. A NUL, a space or a tab breaks the framing the same way with less ceremony.
             * The SOCKS5 tunnel is length-prefixed rather than delimited and so is not injectable,
             * but a host which is refused on one protocol and accepted on the other is a trap for
             * whoever changes the proxy later, so both refuse the same set
             *
             * The rule is stated positively, which is what makes it safe: every byte must be
             * printable ASCII, 0x21 to 0x7E. That admits a DNS name, an IPv4 literal and a
             * bracketed IPv6 literal, and refuses controls, whitespace and everything with the
             * high bit set. An internationalized name must arrive already encoded as A-labels;
             * this is not the place to do IDNA
             *
             * The five additional refusals are not framing, they are a wrong-value check: a host
             * carrying '/', '\', '@', '?' or '#' is a URL or a userinfo that reached here instead
             * of a host, and failing at configuration time is better than asking a proxy to
             * resolve it
             *
             * @throw ArgumentException
             */

            inline void chkTunnelHostName(
                SAA_in                  const std::string&                      hostName,
                SAA_in                  const char*                             what
                )
            {
                BL_CHK_T(
                    true,
                    hostName.empty(),
                    ArgumentException(),
                    BL_MSG()
                        << "A tunnel requires a non-empty "
                        << what
                    );

                for( const char ch : hostName )
                {
                    const auto value = static_cast< unsigned char >( ch );

                    if(
                        value < 0x21 ||
                        value > 0x7E ||
                        '/' == ch ||
                        '\\' == ch ||
                        '@' == ch ||
                        '?' == ch ||
                        '#' == ch
                        )
                    {
                        BL_THROW(
                            ArgumentException(),
                            BL_MSG()
                                << "A tunnel "
                                << what
                                << " contains a character which is not allowed in a host name"
                            );
                    }
                }
            }

            /**
             * @brief Refuses a credential which cannot be put on a tunnel's wire safely
             *
             * The length bound is RFC 1929's: ULEN and PLEN are single octets, so a username or a
             * password longer than 255 bytes has no representation at all. It is applied to the
             * HTTP credentials too, where base64 would have hidden an over-long value rather than
             * corrupted it, because a credential which works through one proxy and is silently
             * rejected by the other is worse than one refused by both
             *
             * The value is never logged and never put in an exception message, here or anywhere
             * else in this file - design 10 requires proxy credentials to be redacted, and the way
             * to redact something reliably is not to write it down
             *
             * @throw ArgumentException
             */

            inline void chkTunnelCredential(
                SAA_in                  const std::string&                      value,
                SAA_in                  const char*                             what,
                SAA_in                  const bool                              refuseColon
                )
            {
                BL_CHK_T(
                    true,
                    value.empty() || value.size() > 255U,
                    ArgumentException(),
                    BL_MSG()
                        << "A tunnel "
                        << what
                        << " must be between 1 and 255 bytes long"
                    );

                for( const char ch : value )
                {
                    const auto value8 = static_cast< unsigned char >( ch );

                    if( value8 < 0x20 || 0x7F == value8 || ( refuseColon && ':' == ch ) )
                    {
                        /*
                         * The colon is refused for the HTTP credentials only, where RFC 7617
                         * section 2 makes it the separator of the user-id from the password: a
                         * user-id containing one would move the split and authenticate as a
                         * different principal. SOCKS5 length-prefixes both, so a colon is just a
                         * byte there
                         */

                        BL_THROW(
                            ArgumentException(),
                            BL_MSG()
                                << "A tunnel "
                                << what
                                << " contains a character which is not allowed in it"
                            );
                    }
                }
            }

        } // detail

        /**
         * @brief How to reach a proxy, and how to authenticate to it
         *
         * A value object with no default-constructible invalid state worth speaking of: the three
         * factories below are the only doors, and each one validates everything it accepts, so a
         * configuration which cannot be put on the wire is refused where it is written rather than
         * in an I/O handler on a thread pool
         */

        class ProxyConfig FINAL
        {
        private:

            cpp::ScalarTypeIniter< ProxyProtocol >                              m_protocol;
            std::string                                                         m_host;
            cpp::ScalarTypeIniter< os::port_t >                                 m_port;
            std::string                                                         m_user;
            std::string                                                         m_password;

            ProxyConfig(
                SAA_in                  const ProxyProtocol                     protocol,
                SAA_in                  std::string&&                           host,
                SAA_in                  const os::port_t                        port,
                SAA_in                  std::string&&                           user,
                SAA_in                  std::string&&                           password
                )
                :
                m_host( BL_PARAM_FWD( host ) ),
                m_user( BL_PARAM_FWD( user ) ),
                m_password( BL_PARAM_FWD( password ) )
            {
                m_protocol = protocol;
                m_port = port;

                detail::chkTunnelHostName( m_host, "proxy host name" );

                BL_CHK_T(
                    true,
                    0U == m_port,
                    ArgumentException(),
                    BL_MSG()
                        << "A tunnel requires a non-zero proxy port"
                    );

                BL_CHK_T(
                    true,
                    m_user.empty() != m_password.empty(),
                    ArgumentException(),
                    BL_MSG()
                        << "A tunnel requires either both or neither of the proxy credentials"
                    );

                if( ! m_user.empty() )
                {
                    const bool isHttp = ProxyProtocol::HttpConnect == protocol;

                    detail::chkTunnelCredential( m_user, "proxy user name", isHttp );
                    detail::chkTunnelCredential( m_password, "proxy password", false /* refuseColon */ );
                }
            }

        public:

            ProxyConfig() NOEXCEPT
            {
                m_protocol = ProxyProtocol::None;
            }

            /**
             * @brief No proxy; the connection goes straight to the origin
             */

            static auto none() -> ProxyConfig
            {
                return ProxyConfig();
            }

            /**
             * @throw ArgumentException when the host, the port or the credentials cannot be used
             */

            static auto httpConnect(
                SAA_in                  std::string                             host,
                SAA_in                  const os::port_t                        port,
                SAA_in                  std::string                             user = std::string(),
                SAA_in                  std::string                             password = std::string()
                )
                -> ProxyConfig
            {
                return ProxyConfig(
                    ProxyProtocol::HttpConnect,
                    BL_PARAM_FWD( host ),
                    port,
                    BL_PARAM_FWD( user ),
                    BL_PARAM_FWD( password )
                    );
            }

            /**
             * @throw ArgumentException when the host, the port or the credentials cannot be used
             */

            static auto socks5(
                SAA_in                  std::string                             host,
                SAA_in                  const os::port_t                        port,
                SAA_in                  std::string                             user = std::string(),
                SAA_in                  std::string                             password = std::string()
                )
                -> ProxyConfig
            {
                return ProxyConfig(
                    ProxyProtocol::Socks5,
                    BL_PARAM_FWD( host ),
                    port,
                    BL_PARAM_FWD( user ),
                    BL_PARAM_FWD( password )
                    );
            }

            ProxyProtocol protocol() const NOEXCEPT
            {
                return m_protocol;
            }

            bool isEnabled() const NOEXCEPT
            {
                return ProxyProtocol::None != m_protocol;
            }

            bool hasCredentials() const NOEXCEPT
            {
                return ! m_user.empty();
            }

            const std::string& host() const NOEXCEPT
            {
                return m_host;
            }

            os::port_t port() const NOEXCEPT
            {
                return m_port;
            }

            const std::string& user() const NOEXCEPT
            {
                return m_user;
            }

            const std::string& password() const NOEXCEPT
            {
                return m_password;
            }

            /**
             * @brief The stable identity string httpclient::ConnectionKey::proxyId holds; empty
             * for a direct connection
             *
             * The producer lives here because the key's consumer and the code which actually
             * speaks to the proxy must agree on what "the same proxy" means, and two places
             * inventing that independently is how they come to disagree
             *
             * The user name IS part of the identity and the password is NOT. Two requests
             * authenticating to one proxy as different principals must not share a connection -
             * the second would inherit the first one's authorization silently - so the name has to
             * separate them. The password separates nothing that the name does not already
             * separate, and a key is a thing which gets logged, compared and put in diagnostics,
             * so a secret has no business in one
             */

            auto proxyId() const -> std::string
            {
                if( ! isEnabled() )
                {
                    return std::string();
                }

                cpp::SafeOutputStringStream os;

                os
                    << ( ProxyProtocol::HttpConnect == m_protocol ? "http-connect://" : "socks5://" );

                if( ! m_user.empty() )
                {
                    os
                        << m_user
                        << "@";
                }

                os
                    << m_host
                    << ":"
                    << m_port.value();

                return os.str();
            }
        };

        /**
         * @brief What the negotiation wants done to the socket next
         *
         * The engine never touches a socket, so it says what it needs in these four words and the
         * shell obeys. 'Done' means the tunnel is established and the bytes which follow on the
         * socket belong to the origin
         */

        class TunnelStep FINAL
        {
        public:

            enum class Action : std::uint8_t
            {
                Write,                  /* send everything TunnelNegotiation::outgoing() holds */
                ReadExactly,            /* read exactly length() bytes */
                ReadSome,               /* read whatever arrives, at most length() bytes */
                Done,                   /* the tunnel is established */
            };

        private:

            Action                                                              m_action;
            std::size_t                                                         m_length;

            TunnelStep(
                SAA_in                  const Action                            action,
                SAA_in                  const std::size_t                       length
                ) NOEXCEPT
                :
                m_action( action ),
                m_length( length )
            {
            }

        public:

            static auto write() NOEXCEPT -> TunnelStep
            {
                return TunnelStep( Action::Write, 0U );
            }

            static auto readExactly( SAA_in const std::size_t length ) NOEXCEPT -> TunnelStep
            {
                return TunnelStep( Action::ReadExactly, length );
            }

            static auto readSome( SAA_in const std::size_t length ) NOEXCEPT -> TunnelStep
            {
                return TunnelStep( Action::ReadSome, length );
            }

            static auto done() NOEXCEPT -> TunnelStep
            {
                return TunnelStep( Action::Done, 0U );
            }

            Action action() const NOEXCEPT
            {
                return m_action;
            }

            std::size_t length() const NOEXCEPT
            {
                return m_length;
            }
        };

        /**
         * @brief The sans-I/O tunnel negotiation - bytes in, bytes out, and a refusal when the
         * proxy says no
         *
         * NO STATE OUTLIVES ONE ATTEMPT, structurally. TcpTunnelStageT constructs a fresh
         * negotiation every time the stage is entered and drops it when the stage finishes, so
         * "the stage carries no state from one attempt to the next" - which
         * TcpConnectionEstablisherConnector::beginPreHandshakeStage requires of every override -
         * is a property of the object graph rather than a discipline somebody has to remember in a
         * reset method. See the note on beginPreHandshakeStage below for why that matters
         *
         * Every read bound lives in here, so a proxy cannot make the client allocate without limit
         * by never terminating what it is sending
         */

        class TunnelNegotiation
        {
            BL_NO_COPY_OR_MOVE( TunnelNegotiation )

        protected:

            std::string                                                         m_outgoing;

            TunnelNegotiation()
            {
            }

        public:

            enum : std::size_t
            {
                /*
                 * The largest read either protocol ever asks for: a SOCKS5 reply whose BND.ADDR is
                 * a 255-byte domain name, plus the two bytes of BND.PORT. The HTTP tunnel reads in
                 * chunks of at most this, so one buffer of this size serves both
                 */

                MAX_READ_SIZE = 257U,
            };

            virtual ~TunnelNegotiation() NOEXCEPT
            {
            }

            /**
             * @brief The bytes a Write step is to send
             */

            const std::string& outgoing() const NOEXCEPT
            {
                return m_outgoing;
            }

            /**
             * @brief The first step; always a Write
             */

            virtual auto start() -> TunnelStep = 0;

            /**
             * @brief Everything outgoing() held has been sent
             */

            virtual auto onWriteCompleted() -> TunnelStep = 0;

            /**
             * @brief 'size' bytes arrived from the proxy
             *
             * @throw HttpException, SecurityException, ServerErrorException when the proxy refuses
             * the tunnel; InvalidDataFormatException when what it sent is not the protocol
             */

            virtual auto onDataRead(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep = 0;
        };

        /**
         * @brief The HTTP CONNECT tunnel of RFC 9110 section 9.3.6
         *
         * One request, one response, and the response is read with a deliberately minimal reader
         * rather than with httpclient::Http1Codec. Two reasons, and the second is the one that
         * decides it: this header is in tasks/ and the codec is in httpclient/, so depending on it
         * would invert the layering design 2.2 fixes; and a CONNECT response is a status line and
         * headers with no body, no chunking and no trailers, which is a strictly smaller problem
         * than the one the codec solves
         *
         * WHAT IS SENT is the request line, Host, and Proxy-Authorization when there are
         * credentials. Nothing else. A CONNECT carries no body and needs no other field, and every
         * field added here is a field the proxy sees which a caller did not ask for
         *
         * WHAT IS ACCEPTED is strict on purpose. The header section ends at CRLF CRLF and at
         * nothing else - a bare LF is not accepted, because a reader which accepts both is a
         * reader two peers can disagree with about where the response ended, which is the shape of
         * every request smuggling defect. Success is 2xx, per RFC 9110; a 1xx is a failure here
         * rather than an interim response to wait past, since a CONNECT has no body to be
         * continued and a proxy sending one is not doing something this client should guess about
         */

        template
        <
            typename E = void
        >
        class HttpConnectNegotiationT : public TunnelNegotiation
        {
        public:

            enum : std::size_t
            {
                /*
                 * Design 3.6: the status line and headers are bounded to 64KB. A proxy which has
                 * not finished its header section by then is not going to
                 */

                MAX_RESPONSE_SIZE = 64U * 1024U,

                READ_CHUNK_SIZE = TunnelNegotiation::MAX_READ_SIZE,

                /*
                 * A reason phrase is echoed into the exception so a failure says why, and it is
                 * the proxy's text rather than ours, so it is bounded before it is used
                 */

                MAX_REASON_PHRASE_SIZE = 128U,
            };

        private:

            typedef TunnelNegotiation                                           base_type;

            std::string                                                         m_response;
            std::size_t                                                         m_searchFrom;

            /**
             * @brief The authority these bytes carry - an IPv6 literal in the brackets RFC 3986
             * section 3.2.2 requires of one
             *
             * THE HOST ARRIVES BARE AND MUST STAY BARE EVERYWHERE ELSE. ConnectionKey::fromUri
             * stores uri.host( ), which the parser has already stripped the brackets from, and
             * that unbracketed form is what the resolver takes, what certificate verification
             * matches against the iPAddress SAN entries, and what the SNI decision is computed
             * from - TcpSslBaseTasks tests make_address( hostName ) and omits SNI entirely for a
             * host which parses as an address (RFC 6066 3 forbids a literal there). A bracketed
             * host would not parse, so normalising the stored form would send SNI WITH brackets,
             * which is worse than the defect this fixes. Hence: at render time only, here
             */

            static std::string renderAuthority(
                SAA_in                  const std::string&                      host,
                SAA_in                  const os::port_t                        port
                )
            {
                cpp::SafeOutputStringStream os;

                if( std::string::npos != host.find( ':' ) )
                {
                    os << "[" << host << "]";
                }
                else
                {
                    os << host;
                }

                os << ":" << port;

                return os.str();
            }

        public:

            HttpConnectNegotiationT(
                SAA_in                  const ProxyConfig&                      config,
                SAA_in                  const std::string&                      originHost,
                SAA_in                  const os::port_t                        originPort
                )
                :
                m_searchFrom( 0U )
            {
                const auto authority = renderAuthority( originHost, originPort );

                cpp::SafeOutputStringStream os;

                os
                    << "CONNECT "
                    << authority
                    << " HTTP/1.1\r\nHost: "
                    << authority
                    << "\r\n";

                if( config.hasCredentials() )
                {
                    /*
                     * RFC 7617: the user-id and the password joined by a colon, base64 of the
                     * result. The colon is why ProxyConfig refuses one inside the user name of an
                     * HTTP proxy
                     */

                    os
                        << "Proxy-Authorization: Basic "
                        << SerializationUtils::base64EncodeString( config.user() + ":" + config.password() )
                        << "\r\n";
                }

                os
                    << "\r\n";

                base_type::m_outgoing = os.str();
            }

            virtual auto start() -> TunnelStep OVERRIDE
            {
                return TunnelStep::write();
            }

            virtual auto onWriteCompleted() -> TunnelStep OVERRIDE
            {
                return nextRead();
            }

            virtual auto onDataRead(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep OVERRIDE
            {
                BL_CHK_T(
                    true,
                    0U == size,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "The proxy sent nothing in response to a CONNECT request"
                    );

                m_response.append( data, size );

                const auto terminator = m_response.find( "\r\n\r\n", m_searchFrom );

                if( std::string::npos == terminator )
                {
                    /*
                     * Only the last three bytes of what is already here can begin a terminator
                     * which the next read completes, so the search never re-scans the whole buffer
                     */

                    m_searchFrom = m_response.size() > 3U ? m_response.size() - 3U : 0U;

                    return nextRead();
                }

                const auto endOfHeaders = terminator + 4U;

                /*
                 * THE STATUS IS JUDGED FIRST, so the trailing-bytes check below only ever runs on
                 * a 2xx. A refusal is entitled to a body - a 407 asking for credentials or a 403
                 * refusing them normally explains itself in HTML, and since the reader asks for at
                 * most READ_CHUNK_SIZE octets at a time the read which completes the header
                 * section usually carries the first bytes of that body with it. None of the
                 * reasoning below applies to a refusal: no tunnel was established, nothing is
                 * about to handshake, and the connection is finished. What the failure has to
                 * carry is the status, and judging the trailing bytes first would replace it with
                 * a framing error
                 */

                chkStatusIsSuccess();

                /*
                 * THE PROXY MUST NOT HAVE SENT ANYTHING PAST THE HEADER SECTION, and this is the
                 * one place that can be noticed. Everything after CRLF CRLF on an established
                 * tunnel belongs to the ORIGIN, and the bytes are already consumed from the socket
                 * - there is nowhere to put them back for the handshake which is about to read.
                 * They also cannot be legitimate: the client has sent nothing to the origin yet,
                 * and neither TLS nor HTTP/1.1 has the server speak first, so a proxy with origin
                 * bytes to forward has invented them. Refusing is therefore both the safe answer
                 * and the correct one, where silently dropping them would corrupt the handshake in
                 * a way that surfaces much later as an unexplained TLS failure
                 */

                BL_CHK_T(
                    false,
                    endOfHeaders == m_response.size(),
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "The proxy sent data past the end of the CONNECT response headers"
                    );

                return TunnelStep::done();
            }

        private:

            auto nextRead() const -> TunnelStep
            {
                BL_CHK_T(
                    true,
                    m_response.size() >= MAX_RESPONSE_SIZE,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "The CONNECT response headers exceed the "
                        << static_cast< std::size_t >( MAX_RESPONSE_SIZE )
                        << " byte limit"
                    );

                const auto remaining = MAX_RESPONSE_SIZE - m_response.size();

                return TunnelStep::readSome(
                    remaining < static_cast< std::size_t >( READ_CHUNK_SIZE ) ?
                        remaining : static_cast< std::size_t >( READ_CHUNK_SIZE )
                    );
            }

            /**
             * @brief Parses the status line and throws unless it is a 2xx
             *
             * Digits are compared against '0' and '9' rather than classified, because the C
             * classification functions are locale sensitive and this has to answer the same thing
             * in every locale the library is embedded in
             *
             * @throw HttpException with eh::errinfo_http_status_code when the proxy refuses;
             * InvalidDataFormatException when the status line is not one
             */

            void chkStatusIsSuccess() const
            {
                const auto eol = m_response.find( "\r\n" );

                BL_ASSERT( std::string::npos != eol );

                const auto line = m_response.substr( 0U, eol );

                bool isWellFormed =
                    line.size() >= 12U &&
                    0 == line.compare( 0U, 7U, "HTTP/1." ) &&
                    ( '0' == line[ 7U ] || '1' == line[ 7U ] ) &&
                    ' ' == line[ 8U ] &&
                    ( 12U == line.size() || ' ' == line[ 12U ] );

                if( isWellFormed )
                {
                    for( std::size_t i = 9U; i < 12U; ++i )
                    {
                        if( line[ i ] < '0' || line[ i ] > '9' )
                        {
                            isWellFormed = false;

                            break;
                        }
                    }
                }

                BL_CHK_T(
                    false,
                    isWellFormed,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "The proxy did not answer a CONNECT request with an HTTP status line"
                    );

                const int statusCode =
                    100 * ( line[ 9U ] - '0' ) + 10 * ( line[ 10U ] - '0' ) + ( line[ 11U ] - '0' );

                if( 200 <= statusCode && statusCode < 300 )
                {
                    return;
                }

                auto reason = 12U < line.size() ? line.substr( 13U ) : std::string();

                if( reason.size() > static_cast< std::size_t >( MAX_REASON_PHRASE_SIZE ) )
                {
                    reason.resize( static_cast< std::size_t >( MAX_REASON_PHRASE_SIZE ) );
                }

                BL_THROW(
                    HttpException() << eh::errinfo_http_status_code( statusCode ),
                    BL_MSG()
                        << "The proxy refused a CONNECT request with status "
                        << statusCode
                        << " '"
                        << reason
                        << "'"
                    );
            }
        };

        typedef HttpConnectNegotiationT<> HttpConnectNegotiation;

        /**
         * @brief The SOCKS5 tunnel of RFC 1928, with the username/password subnegotiation of
         * RFC 1929
         *
         * DOMAINNAME ADDRESSING, ALWAYS (design 3.6). The origin host goes to the proxy as a name
         * and the proxy resolves it, which is the point of a SOCKS5 proxy: resolving locally and
         * sending an address leaks to the local resolver exactly what the proxy was there to hide,
         * and it resolves in the wrong network. An address literal is sent as a DOMAINNAME too -
         * every SOCKS5 implementation resolves one through the same call that handles names - so
         * ATYP 0x01 and 0x04 are never produced by this client, only understood in a reply
         *
         * THE METHODS OFFERED ARE EXACTLY THE ONES THAT CAN BE HONOURED: NO AUTHENTICATION alone
         * when there are no credentials, and NO AUTHENTICATION plus USERNAME/PASSWORD when there
         * are. A server which selects a method that was not offered is refused, rather than
         * followed into a subnegotiation this client did not agree to
         */

        template
        <
            typename E = void
        >
        class Socks5NegotiationT : public TunnelNegotiation
        {
        public:

            enum : std::uint8_t
            {
                VERSION = 0x05U,
                AUTH_VERSION = 0x01U,

                METHOD_NONE = 0x00U,
                METHOD_USER_PASSWORD = 0x02U,
                METHOD_UNACCEPTABLE = 0xFFU,

                COMMAND_CONNECT = 0x01U,
                RESERVED = 0x00U,

                ADDRESS_IPV4 = 0x01U,
                ADDRESS_DOMAIN_NAME = 0x03U,
                ADDRESS_IPV6 = 0x04U,

                REPLY_SUCCEEDED = 0x00U,
            };

        private:

            typedef TunnelNegotiation                                           base_type;

            /**
             * @brief What the negotiation is waiting to read; the name is what it expects next
             */

            enum class State : std::uint8_t
            {
                MethodReply,
                AuthReply,
                ReplyHeader,
                ReplyAddressLength,
                ReplyAddress,
                Established,
            };

            const std::string                                                   m_originHost;
            const os::port_t                                                    m_originPort;
            const std::string                                                   m_user;
            const std::string                                                   m_password;

            State                                                               m_state;
            std::size_t                                                         m_pendingAddressSize;

        public:

            Socks5NegotiationT(
                SAA_in                  const ProxyConfig&                      config,
                SAA_in                  const std::string&                      originHost,
                SAA_in                  const os::port_t                        originPort
                )
                :
                m_originHost( originHost ),
                m_originPort( originPort ),
                m_user( config.user() ),
                m_password( config.password() ),
                m_state( State::MethodReply ),
                m_pendingAddressSize( 0U )
            {
                BL_CHK_T(
                    true,
                    m_originHost.size() > 255U,
                    ArgumentException(),
                    BL_MSG()
                        << "A SOCKS5 tunnel cannot address an origin host name longer than 255 bytes"
                    );

                base_type::m_outgoing.clear();

                appendByte( base_type::m_outgoing, VERSION );

                if( m_user.empty() )
                {
                    appendByte( base_type::m_outgoing, 1U );
                    appendByte( base_type::m_outgoing, METHOD_NONE );
                }
                else
                {
                    appendByte( base_type::m_outgoing, 2U );
                    appendByte( base_type::m_outgoing, METHOD_NONE );
                    appendByte( base_type::m_outgoing, METHOD_USER_PASSWORD );
                }
            }

            virtual auto start() -> TunnelStep OVERRIDE
            {
                return TunnelStep::write();
            }

            virtual auto onWriteCompleted() -> TunnelStep OVERRIDE
            {
                switch( m_state )
                {
                    default:
                        break;

                    case State::MethodReply:
                    case State::AuthReply:

                        /*
                         * Both the method selection of RFC 1928 section 3 and the status of
                         * RFC 1929 section 2 are exactly two bytes
                         */

                        return TunnelStep::readExactly( 2U );

                    case State::ReplyHeader:

                        /*
                         * VER, REP, RSV and ATYP; how much follows depends on the ATYP in them
                         */

                        return TunnelStep::readExactly( 4U );
                }

                BL_THROW(
                    UnexpectedException(),
                    BL_MSG()
                        << "A SOCKS5 tunnel wrote in a state which sends nothing"
                    );
            }

            virtual auto onDataRead(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep OVERRIDE
            {
                switch( m_state )
                {
                    default:
                        break;

                    case State::MethodReply:
                        return onMethodReply( data, size );

                    case State::AuthReply:
                        return onAuthReply( data, size );

                    case State::ReplyHeader:
                        return onReplyHeader( data, size );

                    case State::ReplyAddressLength:
                        return onReplyAddressLength( data, size );

                    case State::ReplyAddress:

                        chkSize( size, m_pendingAddressSize );

                        m_state = State::Established;

                        return TunnelStep::done();
                }

                BL_THROW(
                    UnexpectedException(),
                    BL_MSG()
                        << "A SOCKS5 tunnel read in a state which expects nothing"
                    );
            }

        private:

            static void appendByte(
                SAA_inout               std::string&                            buffer,
                SAA_in                  const std::uint8_t                      value
                )
            {
                buffer.push_back( static_cast< char >( value ) );
            }

            static std::uint8_t byteAt(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       index
                ) NOEXCEPT
            {
                return static_cast< std::uint8_t >( data[ index ] );
            }

            static void chkSize(
                SAA_in                  const std::size_t                       size,
                SAA_in                  const std::size_t                       expected
                )
            {
                BL_CHK_T(
                    false,
                    size == expected,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "A SOCKS5 proxy sent "
                        << size
                        << " bytes where "
                        << expected
                        << " were expected"
                    );
            }

            auto onMethodReply(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep
            {
                chkSize( size, 2U );

                chkVersion( byteAt( data, 0U ), VERSION );

                const auto method = byteAt( data, 1U );

                if( METHOD_NONE == method )
                {
                    return beginConnectRequest();
                }

                if( METHOD_USER_PASSWORD == method && ! m_user.empty() )
                {
                    base_type::m_outgoing.clear();

                    appendByte( base_type::m_outgoing, AUTH_VERSION );
                    appendByte( base_type::m_outgoing, static_cast< std::uint8_t >( m_user.size() ) );
                    base_type::m_outgoing.append( m_user );
                    appendByte( base_type::m_outgoing, static_cast< std::uint8_t >( m_password.size() ) );
                    base_type::m_outgoing.append( m_password );

                    m_state = State::AuthReply;

                    return TunnelStep::write();
                }

                if( METHOD_UNACCEPTABLE == method )
                {
                    BL_THROW(
                        SecurityException(),
                        BL_MSG()
                            << "The SOCKS5 proxy accepts none of the authentication methods offered"
                        );
                }

                BL_THROW(
                    SecurityException(),
                    BL_MSG()
                        << "The SOCKS5 proxy selected authentication method "
                        << static_cast< unsigned >( method )
                        << ", which was not offered"
                    );
            }

            auto onAuthReply(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep
            {
                chkSize( size, 2U );

                /*
                 * RFC 1929 section 2 puts the version of the SUBNEGOTIATION here, which is 1 and
                 * not the 5 of the SOCKS protocol itself. Servers which confuse the two exist;
                 * accepting both would mean accepting a reply the server did not mean to send, so
                 * the specified value is what is required
                 */

                chkVersion( byteAt( data, 0U ), AUTH_VERSION );

                BL_CHK_T(
                    false,
                    0U == byteAt( data, 1U ),
                    SecurityException(),
                    BL_MSG()
                        << "The SOCKS5 proxy rejected the credentials supplied"
                    );

                return beginConnectRequest();
            }

            auto onReplyHeader(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep
            {
                chkSize( size, 4U );

                chkVersion( byteAt( data, 0U ), VERSION );

                const auto reply = byteAt( data, 1U );

                if( REPLY_SUCCEEDED != reply )
                {
                    /*
                     * The bound address which follows is not read. The tunnel has failed, the task
                     * is about to fail with it and the socket is closed as it unwinds, so there is
                     * nothing left to keep in sync and nothing to gain by draining bytes first
                     */

                    BL_THROW(
                        ServerErrorException(),
                        BL_MSG()
                            << "The SOCKS5 proxy refused the connection with reply code "
                            << static_cast< unsigned >( reply )
                            << " ("
                            << replyCodeMeaning( reply )
                            << ")"
                        );
                }

                const auto addressType = byteAt( data, 3U );

                if( ADDRESS_DOMAIN_NAME == addressType )
                {
                    m_state = State::ReplyAddressLength;

                    return TunnelStep::readExactly( 1U );
                }

                m_state = State::ReplyAddress;

                if( ADDRESS_IPV4 == addressType )
                {
                    m_pendingAddressSize = 4U + 2U;
                }
                else if( ADDRESS_IPV6 == addressType )
                {
                    m_pendingAddressSize = 16U + 2U;
                }
                else
                {
                    BL_THROW(
                        InvalidDataFormatException(),
                        BL_MSG()
                            << "The SOCKS5 proxy replied with unknown address type "
                            << static_cast< unsigned >( addressType )
                        );
                }

                return TunnelStep::readExactly( m_pendingAddressSize );
            }

            auto onReplyAddressLength(
                SAA_in_bcount( size )   const char*                             data,
                SAA_in                  const std::size_t                       size
                )
                -> TunnelStep
            {
                chkSize( size, 1U );

                m_pendingAddressSize = static_cast< std::size_t >( byteAt( data, 0U ) ) + 2U;

                m_state = State::ReplyAddress;

                return TunnelStep::readExactly( m_pendingAddressSize );
            }

            auto beginConnectRequest() -> TunnelStep
            {
                base_type::m_outgoing.clear();

                appendByte( base_type::m_outgoing, VERSION );
                appendByte( base_type::m_outgoing, COMMAND_CONNECT );
                appendByte( base_type::m_outgoing, RESERVED );
                appendByte( base_type::m_outgoing, ADDRESS_DOMAIN_NAME );
                appendByte( base_type::m_outgoing, static_cast< std::uint8_t >( m_originHost.size() ) );
                base_type::m_outgoing.append( m_originHost );

                /*
                 * RFC 1928 section 4: DST.PORT is two bytes in network byte order
                 */

                appendByte( base_type::m_outgoing, static_cast< std::uint8_t >( ( m_originPort >> 8 ) & 0xFFU ) );
                appendByte( base_type::m_outgoing, static_cast< std::uint8_t >( m_originPort & 0xFFU ) );

                m_state = State::ReplyHeader;

                return TunnelStep::write();
            }

            static void chkVersion(
                SAA_in                  const std::uint8_t                      actual,
                SAA_in                  const std::uint8_t                      expected
                )
            {
                BL_CHK_T(
                    false,
                    actual == expected,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "A SOCKS5 proxy replied with version "
                        << static_cast< unsigned >( actual )
                        << " where "
                        << static_cast< unsigned >( expected )
                        << " was expected"
                    );
            }

            static auto replyCodeMeaning( SAA_in const std::uint8_t reply ) NOEXCEPT -> const char*
            {
                /*
                 * RFC 1928 section 6
                 */

                switch( reply )
                {
                    default:
                        return "unassigned";

                    case 0x01U:
                        return "general SOCKS server failure";

                    case 0x02U:
                        return "connection not allowed by ruleset";

                    case 0x03U:
                        return "network unreachable";

                    case 0x04U:
                        return "host unreachable";

                    case 0x05U:
                        return "connection refused";

                    case 0x06U:
                        return "TTL expired";

                    case 0x07U:
                        return "command not supported";

                    case 0x08U:
                        return "address type not supported";
                }
            }
        };

        typedef Socks5NegotiationT<> Socks5Negotiation;

        /**
         * @brief The negotiation a configuration calls for
         *
         * @throw ArgumentException when the configuration names no proxy, or when the origin
         * cannot be addressed by the protocol it names
         */

        inline auto createTunnelNegotiation(
            SAA_in                      const ProxyConfig&                      config,
            SAA_in                      const std::string&                      originHost,
            SAA_in                      const os::port_t                        originPort
            )
            -> cpp::SafeUniquePtr< TunnelNegotiation >
        {
            cpp::SafeUniquePtr< TunnelNegotiation > result;

            switch( config.protocol() )
            {
                default:
                    break;

                case ProxyProtocol::HttpConnect:
                    result.reset( new HttpConnectNegotiation( config, originHost, originPort ) );
                    break;

                case ProxyProtocol::Socks5:
                    result.reset( new Socks5Negotiation( config, originHost, originPort ) );
                    break;
            }

            BL_CHK_T(
                nullptr,
                result.get(),
                ArgumentException(),
                BL_MSG()
                    << "A tunnel cannot be negotiated without a proxy"
                );

            return result;
        }

        namespace detail
        {
            /**
             * @brief The cleartext byte stream a tunnel is negotiated over, selected at compile
             * time from BASE::isProtocolHandshakeNeeded
             *
             * A tunnel is spoken IN CLEARTEXT ON THE LOWEST LAYER of the stream, before any
             * handshake (design 3.6), and the object which carries it has to be an AsyncReadStream
             * and an AsyncWriteStream. getSocket() is that object for a cleartext policy, where it
             * is the asio::ip::tcp::socket itself - but NOT for a TLS one, where it is
             * AsioSslStreamWrapper::lowest_layer(), an asio::basic_socket with no async_read_some
             * and no async_write_some at all. The socket underneath the TLS engine is the ssl
             * stream's NEXT layer, and that is what this returns
             *
             * The same mechanism, and for the same reason, as detail::HandshakeTaskHelper in
             * TcpBaseTasks.h: the stream policy is a static interface resolved by template
             * composition, so a runtime branch on the constant would not compile
             */

            template
            <
                typename BASE,
                bool ProtocolHandshakeNeeded = BASE::isProtocolHandshakeNeeded
            >
            class TunnelCleartextLayer;

            template
            <
                typename BASE
            >
            class TunnelCleartextLayer< BASE, false >
            {
            public:

                static auto get( SAA_inout typename BASE::stream_t& stream ) NOEXCEPT
                    -> asio::ip::tcp::socket&
                {
                    return stream;
                }
            };

            template
            <
                typename BASE
            >
            class TunnelCleartextLayer< BASE, true >
            {
            public:

                static auto get( SAA_inout typename BASE::stream_t& stream ) NOEXCEPT
                    -> asio::ip::tcp::socket&
                {
                    return stream.getStream().next_layer();
                }
            };

        } // detail

        /******************************************************************************************
         * ======================================= TcpTunnelStage =================================
         */

        /**
         * @brief The pre-handshake stage which negotiates a tunnel through a proxy
         *
         * Mixed in over TcpConnectionEstablisherConnector< STREAM > - or over anything derived
         * from it - and parameterized on its base for the same reason MultiOperationTaskT is: a
         * connection task already has that establisher in its chain, and hard-wiring it here would
         * give such a task two of them
         *
         * With ProxyConfig::none() it is transparent: the establisher's host and port are the
         * origin's, continueAfterResolved is the base's and beginPreHandshakeStage is the base's,
         * which invokes the continuation synchronously. Nothing about a direct connection changes
         * because this class is in the chain
         *
         * ------------------------------------------------------------------------------------
         * THE THREE OBLIGATIONS AN OVERRIDE OF beginPreHandshakeStage HAS, AND HOW THIS MEETS THEM
         * ------------------------------------------------------------------------------------
         *
         * The first two are on the hook's own doc comment in TcpBaseTasks.h, where they were
         * recorded after the exit-time leak check found them the hard way in S0.2.
         *
         * 1. THE CONTINUATION HOLDS A REFERENCE TO THE TASK. It is bound over an
         *    om::ObjPtrCopyable of it, so a stage which parks it in a member - which is exactly
         *    what a stage doing asynchronous work must do - and a task which owns the stage refer
         *    to each other and neither is ever destroyed. onTaskStoppedNothrow below releases it,
         *    which is the same discipline TcpConnectionEstablisherAcceptor applies to its acceptor
         *    and its back-off timer. A leak of this kind is not visible at the point of the defect:
         *    the task runs correctly, passes its assertions, and the suite reports leaked objects
         *    at process exit.
         *
         * 2. A STAGE WITH NO SOCKET I/O IN FLIGHT IS NEVER WOKEN BY cancelTask(). The base
         *    implementation shuts the socket down, which aborts pending socket operations and
         *    nothing else, so a stage parked on a timer or a resolver of its own has to cancel that
         *    itself. THIS STAGE OWNS NO SUCH OBJECT: from the first write to the last read it has
         *    exactly one socket operation outstanding at all times, each handler starting the next
         *    one inside itself, under the task lock, before it returns. cancelTask() therefore
         *    needs no override - not because the obligation does not apply, but because it is
         *    discharged by there being nothing else to cancel. A later change which gives this
         *    stage a timer of its own - a tunnel deadline, say - acquires the obligation with it.
         *
         * 3. THE MULTI-OPERATION ACCOUNTING IS KEPT OUT OF THE STAGE ENTIRELY. MultiOperationTaskT
         *    clears its accounting in scheduleNothrow, while the handshake retry of
         *    scheduleTaskFinishContinuation restarts the resolve-and-connect transaction IN PLACE
         *    without going through it. A stage which called beginOperation(), or ended a handler
         *    with BL_TASKS_HANDLER_END_MULTIOP(), would therefore be restarted by a retry with a
         *    pending count left over from the attempt before - and if the terminal path had already
         *    been taken, the task would never complete at all. The handlers below call neither and
         *    end with BL_TASKS_HANDLER_END(), which is precisely what onConnectionEstablished - the
         *    handler this stage runs inside - already does. The pre-handshake phase sits outside
         *    the accounting by construction, and this stage stays there.
         *
         * ------------------------------------------------------------------------------------
         * ONCE PER ATTEMPT, WRITTEN TO THE CONTRACT
         * ------------------------------------------------------------------------------------
         *
         * beginPreHandshakeStage is entered once per attempt and must carry nothing over from the
         * attempt before, because scheduleTaskFinishContinuation restarts the whole transaction in
         * place on a retryable handshake error. Every piece of this stage's per-attempt state -
         * the negotiation with its buffers, its parse position and its state machine - lives in
         * m_negotiation, which is CONSTRUCTED FRESH on entry. That is deliberate in preference to a
         * reset method: a reset method has to be kept in step with every field that is ever added,
         * and the way that is discovered to have been missed is a retry which behaves differently
         * from a first attempt, in production, rarely.
         *
         * This was written to the contract rather than to what the retry does today, on the
         * instruction of the slice. At the time that instruction was written the retry was
         * unreachable with a real peer; it has since been made reachable
         * (notes/plans/issues/tls-handshake-retry-unreachable-record.md, closed 2026-09-18), and
         * the cases of this slice do drive the stage through a second attempt. The reasoning stands
         * on its own either way: it is what the hook requires of an override, not what a particular
         * version of the retry happens to exercise.
         *
         * ------------------------------------------------------------------------------------
         * THE TUNNEL IS SPOKEN ON THE STREAM'S LOWEST CLEARTEXT LAYER, AND getSocket() IS NOT IT
         * ------------------------------------------------------------------------------------
         *
         * Every read and write below goes through tunnelStream(), never through getSocket(). For a
         * cleartext policy the two are the same object; for a TLS one getSocket() is the ssl
         * stream's lowest_layer(), an asio::basic_socket with no async_read_some and no
         * async_write_some, and a stage which read or wrote through it did not compile for such a
         * policy at all. It was written that way and nothing noticed, because the suite of the
         * slice which delivered it instantiates exactly one probe and that probe is cleartext - so
         * these members were never instantiated over a TLS policy until S4.1 put the stage into a
         * connection task which is. See detail::TunnelCleartextLayer.
         */

        template
        <
            typename BASE
        >
        class TcpTunnelStageT : public BASE
        {
            BL_DECLARE_OBJECT_IMPL( TcpTunnelStageT )

        public:

            typedef BASE                                                        base_type;
            typedef TcpTunnelStageT< BASE >                                     this_type;

        protected:

            typedef typename base_type::tcp_resolver_type                       tcp_resolver_type;
            typedef detail::TunnelCleartextLayer< base_type >                   cleartext_layer_t;

            const ProxyConfig                                                   m_proxyConfig;
            const std::string                                                   m_originHost;
            const os::port_t                                                    m_originPort;
            const std::string                                                   m_originService;

            cpp::SafeUniquePtr< TunnelNegotiation >                             m_negotiation;
            cpp::bool_callback_t                                                m_continueCallback;
            std::vector< char >                                                 m_readBuffer;

            TcpTunnelStageT(
                SAA_in                  std::string                             originHost,
                SAA_in                  const os::port_t                        originPort,
                SAA_in                  ProxyConfig                             proxyConfig,
                SAA_in                  const bool                              logExceptions = true
                )
                :
                base_type(
                    proxyConfig.isEnabled() ?
                        std::string( proxyConfig.host() ) : std::string( originHost ),
                    proxyConfig.isEnabled() ? proxyConfig.port() : originPort,
                    logExceptions
                    ),
                m_proxyConfig( BL_PARAM_FWD( proxyConfig ) ),
                m_originHost( BL_PARAM_FWD( originHost ) ),
                m_originPort( originPort ),
                m_originService( utils::lexical_cast< std::string >( originPort ) )
            {
                /*
                 * The origin is validated here and not at the first read, so a caller which cannot
                 * be tunnelled finds out where it wrote the mistake. The base has already been
                 * constructed with the proxy's host and port above, which is the resolver's target
                 * and the only place those two are used
                 */

                detail::chkTunnelHostName( m_originHost, "origin host name" );

                BL_CHK_T(
                    true,
                    0U == m_originPort,
                    ArgumentException(),
                    BL_MSG()
                        << "A tunnel requires a non-zero origin port"
                    );

                if( m_proxyConfig.isEnabled() )
                {
                    m_readBuffer.resize( static_cast< std::size_t >( TunnelNegotiation::MAX_READ_SIZE ) );
                }
            }

            /**
             * @brief Connects to the proxy, but creates the socket for the ORIGIN
             *
             * This is the base implementation with one argument changed, and duplicating the
             * handful of lines is what design 3.6 calls for: the host name createSocket is given
             * is the SNI and the verified peer name under the TLS policy, and there is no other
             * way to make it differ from the name the resolver was given. The endpoints passed in
             * are the proxy's, because m_query - which the base resolved - is the proxy's
             */

            virtual bool continueAfterResolved( SAA_in typename tcp_resolver_type::iterator endpoints ) OVERRIDE
            {
                if( ! m_proxyConfig.isEnabled() )
                {
                    return base_type::continueAfterResolved( endpoints );
                }

                const auto threadPool = ThreadPoolDefault::getDefault( base_type::getThreadPoolId() );
                BL_ASSERT( threadPool );

                base_type::createSocket( threadPool -> aioService(), m_originHost, m_originService );

                asio::async_connect(
                    base_type::getSocket(),
                    endpoints,
                    cpp::bind(
                        /*
                         * Named through this_type and not through base_type: the member is
                         * protected, and [class.protected] allows a derived class to form a
                         * pointer to it only when the naming class is the derived one
                         */

                        &this_type::onConnectionEstablished,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error,
                        asio::placeholders::iterator
                        )
                    );

                return true;
            }

            virtual bool beginPreHandshakeStage( SAA_in const cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                if( ! m_proxyConfig.isEnabled() )
                {
                    return base_type::beginPreHandshakeStage( continueCallback );
                }

                /*
                 * Everything this attempt will use is created here, so nothing the attempt before
                 * it left behind can be reached - see the note on the class above
                 */

                m_negotiation = createTunnelNegotiation( m_proxyConfig, m_originHost, m_originPort );
                m_continueCallback = continueCallback;

                BL_LOG(
                    Logging::trace(),
                    BL_MSG()
                        << "Negotiating a tunnel to '"
                        << m_originHost
                        << ":"
                        << m_originPort
                        << "' through proxy '"
                        << m_proxyConfig.proxyId()
                        << "'"
                    );

                beginTunnelStep( m_negotiation -> start() );

                return true;
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt              const std::exception_ptr&               eptrIn = nullptr,
                SAA_inout_opt           bool*                                   isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * Obligation 1 on the class above: the continuation holds a reference to this
                 * task, so holding it past the end of the task is a cycle neither side breaks
                 */

                m_continueCallback = cpp::bool_callback_t();

                m_negotiation.reset();

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }

        private:

            /**
             * @brief The socket the tunnel is spoken on - the stream's lowest CLEARTEXT layer
             *
             * Not getSocket(). Under a TLS policy that is the ssl stream's lowest_layer(), an
             * asio::basic_socket which is neither an AsyncReadStream nor an AsyncWriteStream, and
             * a stage which reads and writes through it does not compile at all for such a policy
             * - see detail::TunnelCleartextLayer above
             */

            auto tunnelStream() NOEXCEPT -> asio::ip::tcp::socket&
            {
                return cleartext_layer_t::get( base_type::getStream() );
            }

            /**
             * @brief Starts the socket operation a step asks for
             */

            void beginTunnelStep( SAA_in const TunnelStep& step )
            {
                BL_ASSERT( m_negotiation );

                switch( step.action() )
                {
                    default:
                        break;

                    case TunnelStep::Action::Write:
                    {
                        const auto& outgoing = m_negotiation -> outgoing();

                        asio::async_write(
                            tunnelStream(),
                            asio::buffer( outgoing.c_str(), outgoing.size() ),
                            base_type::untilCanceled(),
                            cpp::bind(
                                &this_type::onTunnelDataWritten,
                                om::ObjPtrCopyable< this_type >::acquireRef( this ),
                                outgoing.size(),
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred
                                )
                            );

                        return;
                    }

                    case TunnelStep::Action::ReadExactly:
                    {
                        chkReadLength( step.length() );

                        asio::async_read(
                            tunnelStream(),
                            asio::buffer( m_readBuffer.data(), step.length() ),
                            base_type::untilCanceled(),
                            cpp::bind(
                                &this_type::onTunnelDataRead,
                                om::ObjPtrCopyable< this_type >::acquireRef( this ),
                                step.length(),
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred
                                )
                            );

                        return;
                    }

                    case TunnelStep::Action::ReadSome:
                    {
                        chkReadLength( step.length() );

                        /*
                         * Zero is passed as the expected size, which onTunnelDataRead reads as
                         * "whatever arrived": a short read is the normal case here, where for a
                         * ReadExactly step it is the connection ending mid-message
                         */

                        tunnelStream().async_read_some(
                            asio::buffer( m_readBuffer.data(), step.length() ),
                            cpp::bind(
                                &this_type::onTunnelDataRead,
                                om::ObjPtrCopyable< this_type >::acquireRef( this ),
                                0U /* bytesExpected */,
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred
                                )
                            );

                        return;
                    }
                }

                BL_THROW(
                    UnexpectedException(),
                    BL_MSG()
                        << "A tunnel step cannot be started"
                    );
            }

            void chkReadLength( SAA_in const std::size_t length ) const
            {
                BL_CHK(
                    false,
                    length > 0U && length <= m_readBuffer.size(),
                    BL_MSG()
                        << "A tunnel asked for a read of "
                        << length
                        << " bytes, which does not fit its buffer"
                    );
            }

            /**
             * @brief Moves the negotiation on, and returns true if the task must not complete
             */

            bool advanceTunnel( SAA_in const TunnelStep& step )
            {
                if( TunnelStep::Action::Done == step.action() )
                {
                    BL_LOG(
                        Logging::trace(),
                        BL_MSG()
                            << "The tunnel to '"
                            << m_originHost
                            << ":"
                            << m_originPort
                            << "' is established"
                        );

                    /*
                     * The handshake starts here, from inside this handler and under the task lock,
                     * which is where it would have started had there been no stage at all
                     */

                    return m_continueCallback();
                }

                beginTunnelStep( step );

                return true;
            }

            void onTunnelDataWritten(
                SAA_in                  const std::size_t                       bytesExpected,
                SAA_in                  const eh::error_code&                   ec,
                SAA_in                  const std::size_t                       bytesTransferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                chkFullTransfer( bytesExpected, bytesTransferred );

                if( advanceTunnel( m_negotiation -> onWriteCompleted() ) )
                {
                    return;
                }

                BL_TASKS_HANDLER_END()
            }

            void onTunnelDataRead(
                SAA_in                  const std::size_t                       bytesExpected,
                SAA_in                  const eh::error_code&                   ec,
                SAA_in                  const std::size_t                       bytesTransferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                if( bytesExpected )
                {
                    chkFullTransfer( bytesExpected, bytesTransferred );
                }

                if(
                    advanceTunnel(
                        m_negotiation -> onDataRead( m_readBuffer.data(), bytesTransferred )
                        )
                    )
                {
                    return;
                }

                BL_TASKS_HANDLER_END()
            }

            /**
             * @brief A partial transfer means the proxy stopped talking in the middle of a message
             *
             * asio::async_write and asio::async_read only complete short when the completion
             * condition stops them, and untilCanceled() does that when the task is cancelled - in
             * which case the handler prolog has already thrown operation_aborted and this is not
             * reached
             */

            static void chkFullTransfer(
                SAA_in                  const std::size_t                       bytesExpected,
                SAA_in                  const std::size_t                       bytesTransferred
                )
            {
                BL_CHK_T(
                    false,
                    bytesExpected == bytesTransferred,
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "A tunnel transferred "
                        << bytesTransferred
                        << " bytes where "
                        << bytesExpected
                        << " were expected"
                    );
            }
        };

    } // tasks

} // bl

#endif /* __BL_TASKS_TCPTUNNELSTAGE_H_ */
