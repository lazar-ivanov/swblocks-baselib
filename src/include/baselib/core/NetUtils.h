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

#ifndef __BL_NETUTILS_H_
#define __BL_NETUTILS_H_

#include <baselib/core/OS.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace net
    {
        using asio::ip::tcp;

        typedef cpp::SafeUniquePtr< tcp::socket > socket_ref;

        namespace detail
        {
            /**
             * @brief class NetUtils - networking utility APIs
             */

            template
            <
                typename E = void
            >
            class NetUtilsT FINAL
            {
                BL_DECLARE_STATIC( NetUtilsT )

            private:

                enum
                {
                    /*
                     * 3 seconds ( 100 x 30 milliseconds ) total delay
                     * should be hopefully enough to workaround this
                     * known issue (see comments below)
                     */

                    ENDPOINT_QUERY_WAIT_IN_MILLISECONDS = 100,
                    MAX_ENDPOINT_QUERY_RETRIES = 30,
                };

                template
                <
                    typename T
                >
                static auto safeRemoteEndpoint( SAA_in const T& socket ) -> typename T::endpoint_type
                {
                    tcp::endpoint remoteEndpoint;

                    for( std::size_t i = 0; i < MAX_ENDPOINT_QUERY_RETRIES; ++i )
                    {

                        /*
                         * Save the remote endpoint, so the getHost(), getPort()
                         * and getRemoteEndpoint() APIs below can work safely.
                         */

                        try
                        {
                            remoteEndpoint = socket.remote_endpoint();

                            break;
                        }
                        catch( eh::system_error& e )
                        {
                            /*
                             * We have seen cases, specifically when using RHEL5
                             * generated libs running on RHEL6, where
                             * 'remote_endpoint' is called and it returns not
                             * connected. This occurs in spite of the connection
                             * already being established as the present callback
                             * is invoked after a connection has been established.
                             *
                             * Googling around, others have reported sporadic issues
                             * like this one as a result of 'getpeername' (used by
                             * Boost) returning ENOTCONN. Workarounds include
                             * retrying the call and writing to the socket.
                             */

                            if( asio::error::not_connected != e.code() || ( MAX_ENDPOINT_QUERY_RETRIES - 1 ) == i )
                            {
                                throw;
                            }

                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "failure to retrieve remote endpoint information -- retry #"
                                    << i + 1
                                    << " out of "
                                    << MAX_ENDPOINT_QUERY_RETRIES
                            );

                            os::sleep( time::milliseconds( ENDPOINT_QUERY_WAIT_IN_MILLISECONDS ) );
                        }
                    }

                    return remoteEndpoint;
                }

            public:

                template
                <
                    typename T
                >
                static std::string safeRemoteEndpointId( SAA_in const T& socket )
                {
                    try
                    {
                        return formatEndpointId( safeRemoteEndpoint( socket ) );
                    }
                    catch( eh::system_error& e )
                    {
                        /*
                         * The connection is already gone, so the placeholder below is returned
                         * instead of failing the caller; getpeername reports this as ENOTCONN on
                         * Linux and as EINVAL on macOS and the other BSDs
                         *
                         * Note that EINVAL is deliberately not retried in safeRemoteEndpoint( ... )
                         * above - the retry loop there is for a spurious ENOTCONN on a socket which
                         * is still live, and retrying a genuinely dead one would only sleep
                         */

                        if(
                            asio::error::not_connected != e.code() &&
                            asio::error::invalid_argument != e.code()
                            )
                        {
                            throw;
                        }
                    }

                    return "<unknown_host_name>:<unknown_port>";
                }

                /**
                 * @brief Returns the remote endpoint id of a connected socket without retrying
                 *
                 * Unlike safeRemoteEndpointId( ... ) above this never sleeps and never throws
                 * a system error, so it can be called from a context which must not block
                 * (e.g. a task continuation, which runs while the execution queue lock is held)
                 */

                template
                <
                    typename T
                >
                static std::string remoteEndpointIdNoWait( SAA_in const T& socket )
                {
                    eh::error_code ec;

                    const auto remoteEndpoint = socket.remote_endpoint( ec );

                    if( ec )
                    {
                        return "<unknown>";
                    }

                    return formatEndpointId( remoteEndpoint );
                }

                template
                <
                    typename T
                >
                static std::string formatEndpointId( SAA_in const T& endpoint )
                {
                    return formatEndpointId( endpoint.address().to_string(), endpoint.port() );
                }

                static std::string formatEndpointId(
                    SAA_in      const std::string&                      host,
                    SAA_in      const os::port_t                        port
                    )
                {
                    cpp::SafeOutputStringStream output;

                    output
                        << host
                        << ":"
                        << port;

                    return output.str();
                }

                static std::string getCanonicalHostName( SAA_in const std::string& hostName )
                {
                    using namespace asio::ip;

                    asio::io_service ioService;
                    asio::ip::tcp_resolver resolver( ioService );
                    eh::error_code ec;

                    asio::ip::tcp_resolver::query query(
                        hostName                            /* host_name */,
                        str::empty()                        /* service_name */,
                        resolver_query_base::canonical_name /* flags */
                        );

                    const auto endpoints = resolver.resolve( query, ec );

                    BL_CHK_EC_USER_FRIENDLY(
                        ec,
                        BL_MSG()
                            << "Host '"
                            << hostName
                            << "' look-up has failed"
                        );

                    const decltype( endpoints ) end;

                    /*
                     * All host names in the returned endpoints are identical. Use the first one.
                     *
                     * Even if the getaddrinfo function succeeds, the host name may be empty.
                     */

/*
 * The gate keys on BOOST_VERSION, not BL_DEVENV_VERSION: the thing which changed is Boost's
 * resolver API, BOOSTDIR is overridable, and BL_DEVENV_VERSION is only ever defined by the
 * project makefiles - an external consumer without it would otherwise take the legacy branch
 * against a modern Boost, where results_type derives privately from the iterator and does not
 * compile. BOOST_VERSION arrives via OS.h / OSBoostImports.h above; the guard makes a change of
 * that include graph a build error rather than a silently wrong branch.
 */
#if !defined( BOOST_VERSION ) || 0 == BOOST_VERSION
#error BOOST_VERSION must be defined before the resolver results gate in NetUtils.h
#endif

#if BOOST_VERSION >= 106600
                    /*
                     * Boost 1.66+ (devenv4+): resolver::resolve() returns results_type with begin()/end()
                     */
                    BL_CHK_USER_FRIENDLY(
                        true,
                        endpoints == end || endpoints.begin() -> host_name().empty(),
                        BL_MSG()
                            << "Host '"
                            << hostName
                            << "' has no canonical name"
                        );

                    return endpoints.begin() -> host_name();
#else
                    /*
                     * Boost ≤1.63 (devenv2-3): resolver::resolve() returns iterator directly
                     */
                    BL_CHK_USER_FRIENDLY(
                        true,
                        endpoints == end || endpoints -> host_name().empty(),
                        BL_MSG()
                            << "Host '"
                            << hostName
                            << "' has no canonical name"
                        );

                    return endpoints -> host_name();
#endif
                }
            };

            typedef NetUtilsT<> NetUtils;

        } // detail

        /**
         * @brief THE ONE PLACE which decides whether an asio error code means the peer ended the
         * connection, and the only thing networking code in this library should ask
         *
         * ============================================================================
         * WHY THIS EXISTS - READ BEFORE ADDING A CODE COMPARISON TO NETWORKING CODE
         * ============================================================================
         *
         * The same peer behaviour - "the peer went away" - reaches us under DIFFERENT error codes
         * on Windows than on POSIX, because the divergence is in the TCP stack and in the I/O
         * model, below anything this library writes. Two observables, which may well be one
         * mechanism seen twice:
         *
         *   1. A peer ending the conversation can arrive as WSAECONNRESET (system:10054)
         *      rather than as an end of stream. MEASURED, on a TLS handshake whose peer
         *      accepted and went away, where Linux reported eof or a truncation. An earlier
         *      version of this comment blamed "RST where POSIX sends FIN on unread data";
         *      that is NOT a platform difference - Linux close( ) with unread data also
         *      sends RST. The mechanism is now MEASURED - see the os:: predicates.
         *
         *   2. A peer ending the conversation can arrive as WSAECONNABORTED (system:10053)
         *      rather than as an end of stream, when a send of ours followed the peer's
         *      shutdown. Also MEASURED, and NOT a plain FIN on a pending read - Asio maps that
         *      to eof.
         *
         * Both are ONE mechanism, confirmed by the PeerCloseErrorCodes_* control cases in
         * utf_baselib_http2: shutdown_both leaves the receive side shut, an arrival after that
         * resets the connection on Windows, and the RST surfaces as 10054 or as 10053 depending
         * only on whether a send of ours was outstanding when it landed.
         *
         * The reset also DISCARDS what is still unread. The control measured 0 of 16384 bytes
         * delivered on Windows against all 16384 then eof on Linux, so classifying the close as
         * a peer close does not recover what the reset threw away - a caller which needed those
         * bytes has lost them.
         *
         * Neither code means what its POSIX namesake means for a READ. On POSIX a reset reaching a
         * read still hands over whatever was already queued before reporting the error, and
         * ECONNABORTED is an accept() error a read never produces at all. So code which compares
         * error codes by hand is correct on the platform it was written on and quietly wrong on
         * the other - and because mechanism 2 is a race, being wrong shows up as an INTERMITTENT
         * failure rather than an obvious one.
         *
         * This has now been paid for three times in this library: the TLS handshake retry was
         * unreachable on Windows, the HTTP/2 driver failed a connection its peer had closed
         * normally, and before that the retry was unreachable everywhere for the related reason
         * that a truncated TLS stream has its own spelling again. Each was found by a matrix run
         * and diagnosed from first principles, which is expensive.
         *
         * So: do not compare against asio::error::connection_reset, connection_aborted or eof in
         * networking code. Ask one of the predicates below, and if none of them fits, add the next
         * one HERE with its reasoning rather than open-coding the comparison at the call site.
         * There were two of them when this was written and there are five now, each of which had
         * to say what question it answers that the ones before it do not.
         *
         * See notes/plans/issues/windows-peer-close-error-codes-record.md
         */

        /**
         * @brief What an ORDERLY close by the peer looks like on THIS platform
         *
         * Use where the question is "did the peer finish cleanly, so is this transient and worth
         * another attempt?". On POSIX a reset is deliberately NOT one of these: there it is a
         * genuinely distinct condition and treating it as a clean close would turn a real refusal
         * into an attempt storm. On Windows it is one of these, because the stack has collapsed
         * the clean close into it and the distinction POSIX offers is simply not observable.
         *
         * Note this does NOT cover a truncated TLS stream, which is spelled by the stream policy
         * and not by the transport - ask STREAM::isStreamTruncationError() alongside this.
         */

        inline bool isOrderlyPeerCloseErrorCode( SAA_in const eh::error_code& ec ) NOEXCEPT
        {
            if( asio::error::eof == ec )
            {
                return true;
            }

            if( os::peerCloseWithUnreadDataIsReportedAsReset() && asio::error::connection_reset == ec )
            {
                return true;
            }

            if( os::peerCloseCanBeReportedAsConnectionAborted() && asio::error::connection_aborted == ec )
            {
                return true;
            }

            return false;
        }

        /**
         * @brief Whether the connection has ENDED, however it ended
         *
         * Use where the question is "is the conversation over?" rather than "was it clean?" - a
         * read loop deciding whether to report a failure or simply stop, say. A reset counts on
         * every platform here, because a reset connection is just as over as a closed one; the
         * difference from isOrderlyPeerCloseErrorCode() is only whether the ending was tidy.
         */

        inline bool isPeerClosedErrorCode( SAA_in const eh::error_code& ec ) NOEXCEPT
        {
            return isOrderlyPeerCloseErrorCode( ec ) || asio::error::connection_reset == ec;
        }

        /**
         * @brief Whether the BYTE STREAM ended cleanly, so that a message framed by the close
         * itself may be declared complete on it
         *
         * THE THIRD PREDICATE THE COMMENT ABOVE ASKS FOR, and it exists because neither of the
         * other two answers this question. Use it where the consequence of saying yes is that
         * something is declared COMPLETE - an HTTP/1.1 response whose only framing is the
         * connection closing, which RFC 9112 section 6.3 makes a real and common case and which a
         * client has nothing else to check against.
         *
         * IT ADMITS eof AND NOTHING ELSE, ON EVERY PLATFORM, which is what makes it different
         * from isOrderlyPeerCloseErrorCode(). That one admits the Windows reset spellings
         * DELIBERATELY - the stack has collapsed a clean close into them and a handshake retry
         * asking "is this transient?" is right to treat them as one. Here they are exactly what
         * must be refused: on Windows a reset DISCARDS what was still unread - the control
         * measured 0 of 16384 bytes delivered - so a close-delimited body ended by one is either
         * short or aborted, and there is no third possibility. Declaring it complete would hand
         * the caller a truncated response reported as a success, and the caller would have no way
         * to tell.
         *
         * WHAT THIS DOES NOT COVER is a truncated TLS stream, exactly as the two predicates above
         * do not: that is spelled by the stream policy and not by the transport. A caller which
         * means "the stream ended in a way this message may be completed on" asks
         * STREAM::isStreamTruncationError() alongside this, because a server which closes a TLS
         * connection without close_notify is the ordinary shape of a close-delimited HTTPS
         * response (RFC 2818 section 2.2.2) and refusing it would fail responses which succeed
         * today.
         *
         * See notes/plans/issues/windows-peer-close-error-codes-record.md
         */

        inline bool isCleanEndOfStreamErrorCode( SAA_in const eh::error_code& ec ) NOEXCEPT
        {
            return asio::error::eof == ec;
        }

        /**
         * @brief Whether a WRITE ended because the peer went away
         *
         * THE FOURTH PREDICATE, and it exists because the three above are all READ side. Ask it
         * where a send failed and the question is "is the conversation over?" - a request still
         * going out when the peer hangs up, say, which is an ordinary ending and not a fault of
         * ours. It is isPeerClosedErrorCode() with one code added, and that code is the whole
         * reason it is here.
         *
         * IT ADMITS broken_pipe, WHICH NONE OF THE OTHERS DOES. A peer that closes with our
         * upload still unread puts a RST on the wire (RFC 2525 section 2.17), and a send into
         * that connection completes EPIPE - measured, system:32 out of reactive_socket_send_op,
         * by the write-barrier case in utf_baselib_httpclient7.
         *
         * AND WHY IT ALSO STILL ADMITS connection_reset, WHICH IS NOT A CHOICE BETWEEN THE TWO.
         * One peer close produces BOTH codes, and which one a write gets says only which half of
         * the connection reached the reset first: sock_error() takes the pending error with an
         * exchange, so the FIRST of the two syscalls gets ECONNRESET and the other gets what is
         * left - EPIPE for a send, a plain end of stream for a recv. Measured both ways on this
         * platform, deterministically, with the order fixed by which op the reactor performs
         * first. A predicate admitting one of them would be right about half the time and would
         * look like a race rather than a hole.
         *
         * WHAT IS STILL OWED IS THE WINDOWS SPELLING, and it is deliberately not guessed here.
         * WSAECONNRESET and WSAECONNABORTED already map to codes isPeerClosedErrorCode() admits
         * on that platform, so what is open is only whether a send into a reset connection is
         * spelled a third way there. The matrix answers that; this predicate is not the place to
         * assume it. WSAESHUTDOWN stays OUT either way - that one is OUR own shutdown_send and is
         * a state question, not a code question, which is why onWriteCompleted() asks isClosing()
         * first and this second.
         *
         * NOT THE ORDERLY VARIANT, and not for the reason that looks obvious. The question here is
         * whether the conversation is over, not whether it ended tidily and a retry is worth it -
         * and a write cannot answer the second question at all, because by the time it fails the
         * request is part sent.
         *
         * WHAT THIS DOES NOT COVER is a truncated TLS stream, exactly as the three above do not:
         * that is spelled by the stream policy. A caller on a class which may be instantiated over
         * TLS asks STREAM::isStreamTruncationError() alongside this.
         *
         * See notes/plans/issues/windows-peer-close-error-codes-record.md
         */

        inline bool isPeerClosedOnWriteErrorCode( SAA_in const eh::error_code& ec ) NOEXCEPT
        {
            return isPeerClosedErrorCode( ec ) || asio::error::broken_pipe == ec;
        }

        /**
         * @brief Whether a WRITE's own code is PROOF that the peer reset the connection, with no
         * orderly close before it
         *
         * THE FIFTH PREDICATE, AND THE ONLY ONE WHOSE SUBJECT IS EVIDENCE RATHER THAN ROUTING.
         * The four above ask what to DO about a code. This one asks what a code lets you conclude
         * about the OTHER half of the same connection, and it exists because on a duplex socket
         * the two halves do not both get to see what happened: one pending error, one exchange,
         * and the first syscall to ask takes it away. A read left holding a plain end of stream
         * after its own write consumed the reset cannot tell that ending from an orderly close,
         * and for a message framed BY the close - an HTTP/1.1 response with no length - that is
         * the difference between a complete answer and a truncated one reported as a success.
         *
         * WHAT MAKES IT PROOF IS THE KERNEL'S OWN RULE, and it is the whole reasoning here. On
         * Linux tcp_reset() writes ECONNRESET only from a state which has received no FIN - the
         * ESTABLISHED arm - and EPIPE from CLOSE_WAIT, and tcp_fin() sets SOCK_DONE, after which
         * every empty recv() returns zero before it ever looks at the pending error. So a write
         * which completed connection_reset says the ending was a reset and that nothing orderly
         * preceded it. A write which completed broken_pipe says nothing at all against the read's
         * own code: the read may have taken the reset itself, a FIN may have arrived first, or
         * the pipe may be our own shutdown_send. Verified at the source, kernel 6.8, and measured
         * both ways on a raw-socket probe.
         *
         * IT IS isPeerClosedErrorCode() AND THAT IS THE POINT, not a hand comparison wearing a
         * name. On POSIX that predicate admits connection_reset and refuses broken_pipe, which is
         * exactly the question above; what this adds is the reasoning, at one place, so that no
         * call site has to carry it.
         *
         * WHAT IS OWED TO THE MATRIX, AND IT IS A BEHAVIOUR AND NOT ONLY A SPELLING. On Windows
         * the stack collapses an orderly close into the reset spellings, so the kernel rule above
         * does not hold there and this predicate admits a code a write may have got AFTER a FIN.
         * A peer which half closes and only then aborts would, on that platform alone, have its
         * FIN-framed message reported as a reset rather than completed. Nothing is lost by it on
         * the face this exists for - a reset there reaches the READ as a reset spelling, which
         * isCleanEndOfStreamErrorCode() refuses for itself - so the record is redundant on
         * Windows and this is the only case in which it is not also harmless. Measure it there
         * before relying on either answer.
         *
         * WHAT THIS DOES NOT COVER is a truncated TLS stream: that is the stream policy's
         * spelling and, unlike the predicates above, asking it here would be WRONG - a truncation
         * is what an orderly close-delimited HTTPS response looks like, so it is not evidence of
         * a reset and must not be added beside this one.
         *
         * See notes/plans/issues/windows-peer-close-error-codes-record.md
         */

        inline bool isPeerResetOnWriteErrorCode( SAA_in const eh::error_code& ec ) NOEXCEPT
        {
            return isPeerClosedErrorCode( ec );
        }

        template
        <
            typename T
        >
        inline std::string safeRemoteEndpointId( SAA_in const T& socket )
        {
            return detail::NetUtils::safeRemoteEndpointId< T >( socket );
        }

        template
        <
            typename T
        >
        inline std::string remoteEndpointIdNoWait( SAA_in const T& socket )
        {
            return detail::NetUtils::remoteEndpointIdNoWait< T >( socket );
        }

        template
        <
            typename T
        >
        inline std::string formatEndpointId( SAA_in const T& endpoint )
        {
            return detail::NetUtils::formatEndpointId< T >( endpoint );
        }

        inline std::string formatEndpointId(
            SAA_in      const std::string&                      host,
            SAA_in      const os::port_t                        port
            )
        {
            return detail::NetUtils::formatEndpointId( host, port );
        }

        inline std::string getShortHostName( SAA_in_opt std::string&& hostName = asio::ip::host_name() )
        {
            BL_CHK_ARG( ! hostName.empty(), hostName );

            const auto pos = hostName.find( '.' );

            if( pos == std::string::npos )
            {
                return BL_PARAM_FWD( hostName );
            }
            else
            {
                return hostName.substr( 0, pos );
            }
        }

        inline std::string getCanonicalHostName( SAA_in_opt std::string&& hostName = asio::ip::host_name() )
        {
            BL_CHK_ARG( ! hostName.empty(), hostName );

            return detail::NetUtils::getCanonicalHostName( BL_PARAM_FWD( hostName ) );
        }

        /*
         * @brief: Extract host and port from string in "host:port" format
         */

        inline bool tryParseEndpoint(
            SAA_in      const std::string&      endpoint,
            SAA_inout   std::string&            host,
            SAA_inout   os::port_t&             port
            )
        {
            const auto pos = endpoint.find( ':' );

            if( pos == std::string::npos )
            {
                return false;
            }

            if( pos == 0U || pos + 1U == endpoint.size() )
            {
                return false;
            }

            if( endpoint.size() > pos + 6U )
            {
                return false;
            }

            unsigned long value = 0U;

            try
            {
                value = std::stoul( std::string( endpoint.begin() + pos + 1U, endpoint.end() ) );
            }
            catch( std::invalid_argument& )
            {
                return false;
            }

            if( value > std::numeric_limits< os::port_t >::max() )
            {
                return false;
            }

            host.assign( endpoint.begin(), endpoint.begin() + pos );
            port = static_cast< os::port_t >( value );

            return true;
        }

        /*
         * Network protocol header wrappers
         */

        /**
         * @brief Packet header for IPv4
         *
         * The wire format of an IPv4 header is:
         *
         * @pre
         * 0               8               16                             31
         * +-------+-------+---------------+------------------------------+      ---
         * |       |       |               |                              |       ^
         * |version|header |    type of    |    total length in bytes     |       |
         * |  (4)  | length|    service    |                              |       |
         * +-------+-------+---------------+-+-+-+------------------------+       |
         * |                               | | | |                        |       |
         * |        identification         |0|D|M|    fragment offset     |       |
         * |                               | |F|F|                        |       |
         * +---------------+---------------+-+-+-+------------------------+       |
         * |               |               |                              |       |
         * | time to live  |   protocol    |       header checksum        |   20 bytes
         * |               |               |                              |       |
         * +---------------+---------------+------------------------------+       |
         * |                                                              |       |
         * |                      source IPv4 address                     |       |
         * |                                                              |       |
         * +--------------------------------------------------------------+       |
         * |                                                              |       |
         * |                   destination IPv4 address                   |       |
         * |                                                              |       v
         * +--------------------------------------------------------------+      ---
         * |                                                              |       ^
         * |                                                              |       |
         * /                        options (if any)                      /    0 - 40
         * /                                                              /     bytes
         * |                                                              |       |
         * |                                                              |       v
         * +--------------------------------------------------------------+      ---
         */

        class Ipv4Header FINAL
        {
            std::uint8_t                                                    m_data[ 60 ];

        public:

            Ipv4Header()
            {
                std::fill( m_data, m_data + sizeof( m_data ), 0 );
            }

            std::uint8_t version() const NOEXCEPT                           { return ( m_data[ 0 ] >> 4 ) & 0xF; }
            std::uint16_t headerLength() const NOEXCEPT                     { return ( m_data[ 0 ] & 0xF ) * 4; }
            std::uint8_t typeOfService() const NOEXCEPT                     { return m_data[ 1 ]; }
            std::uint16_t totalLength() const NOEXCEPT                      { return decode( 2, 3 ); }
            std::uint16_t identification() const NOEXCEPT                   { return decode( 4, 5 ); }
            bool dontFragment() const NOEXCEPT                              { return ( m_data[ 6 ] & 0x40 ) != 0; }
            bool moreFragments() const NOEXCEPT                             { return ( m_data[ 6 ] & 0x20 ) != 0; }
            std::uint16_t fragmentOffset() const NOEXCEPT                   { return decode( 6, 7 ) & 0x1FFF; }
            std::uint8_t timeToLive() const NOEXCEPT                        { return m_data[ 8 ]; }
            std::uint8_t protocol() const NOEXCEPT                          { return m_data[ 9 ]; }
            std::uint16_t headerChecksum() const NOEXCEPT                   { return decode( 10, 11 ); }

            asio::ip::address_v4 sourceAddress() const NOEXCEPT
            {
                const asio::ip::address_v4::bytes_type bytes = {
                    { m_data[ 12 ], m_data[ 13 ], m_data[ 14 ], m_data[ 15 ] }
                    };

                return asio::ip::address_v4( bytes );
            }

            asio::ip::address_v4 destinationAddress() const NOEXCEPT
            {
                const asio::ip::address_v4::bytes_type bytes = {
                    { m_data[ 16 ], m_data[ 17 ], m_data[ 18 ], m_data[ 19 ] }
                    };

                return asio::ip::address_v4( bytes );
            }

            friend std::istream& operator>>(
                SAA_inout   std::istream&                                   is,
                SAA_inout   Ipv4Header&                                     header
                )
            {
                is.read( reinterpret_cast< char* >( header.m_data ), 20 );

                if( header.version() != 4 )
                {
                    is.setstate( std::ios::failbit );
                }

                const std::streamsize optionsLength = header.headerLength() - 20;

                if( optionsLength < 0 || optionsLength > 40 )
                {
                    is.setstate( std::ios::failbit );
                }
                else
                {
                    is.read( reinterpret_cast< char* >( header.m_data ) + 20, optionsLength );
                }

                return is;
            }

        private:

            std::uint16_t decode(
                SAA_in      const std::size_t                               a,
                SAA_in      const std::size_t                               b
                ) const NOEXCEPT
            {
                return ( m_data[ a ] << 8 ) + m_data[ b ];
            }
        };

        /**
         * @brief ICMP header for both IPv4 and IPv6
         *
         * The wire format of an ICMP header is:
         *
         * @pre
         * 0               8               16                             31
         * +---------------+---------------+------------------------------+      ---
         * |               |               |                              |       ^
         * |     type      |     code      |          checksum            |       |
         * |               |               |                              |       |
         * +---------------+---------------+------------------------------+    8 bytes
         * |                               |                              |       |
         * |          identifier           |       sequence number        |       |
         * |                               |                              |       v
         * +-------------------------------+------------------------------+      ---
         */

        class IcmpHeader FINAL
        {
            std::uint8_t                                                    m_data[ 8 ];

        public:

            enum : std::uint8_t
            {
                ICMP_ECHO_REPLY                 = 0,
                ICMP_DESTINATION_UNREACHABLE    = 3,
                ICMP_SOURCE_QUENCH              = 4,
                ICMP_REDIRECT                   = 5,
                ICMP_ECHO_REQUEST               = 8,
                ICMP_TIME_EXCEEDED              = 11,
                ICMP_PARAMETER_PROBLEM          = 12,
                ICMP_TIMESTAMP_REQUEST          = 13,
                ICMP_TIMESTAMP_REPLY            = 14,
                ICMP_INFO_REQUEST               = 15,
                ICMP_INFO_REPLY                 = 16,
                ICMP_ADDRESS_REQUEST            = 17,
                ICMP_ADDRESS_REPLY              = 18,
            };

            IcmpHeader()
            {
                std::fill( m_data, m_data + sizeof( m_data ), 0 );
            }

            std::uint8_t type() const NOEXCEPT                              { return m_data[ 0 ]; }
            std::uint8_t code() const NOEXCEPT                              { return m_data[ 1 ]; }
            std::uint16_t checksum() const NOEXCEPT                         { return decode( 2, 3 ); }
            std::uint16_t identifier() const NOEXCEPT                       { return decode( 4, 5 ); }
            std::uint16_t sequenceNumber() const NOEXCEPT                   { return decode( 6, 7 ); }

            void type( SAA_in const std::uint8_t n ) NOEXCEPT               { m_data[ 0 ] = n; }
            void code( SAA_in const std::uint8_t n ) NOEXCEPT               { m_data[ 1 ] = n; }
            void checksum( SAA_in const std::uint16_t n ) NOEXCEPT          { encode( 2, 3, n ); }
            void identifier( SAA_in const std::uint16_t n ) NOEXCEPT        { encode( 4, 5, n ); }
            void sequenceNumber( SAA_in const std::uint16_t n ) NOEXCEPT    { encode( 6, 7, n ); }

            friend std::istream& operator>>(
                SAA_inout   std::istream&                                   is,
                SAA_inout   IcmpHeader&                                     header
                )
            {
                return is.read( reinterpret_cast< char* >( header.m_data ), 8 );
            }

            friend std::ostream& operator<<(
                SAA_inout   std::ostream&                                   os,
                SAA_in      const IcmpHeader&                               header
                )
            {
                return os.write( reinterpret_cast< const char* >( header.m_data ), 8 );
            }

            template
            <
                typename Iterator
            >
            void computeChecksum(
                SAA_in      const Iterator                                  bodyBegin,
                SAA_in      const Iterator                                  bodyEnd
                )
            {
                /*
                 * RFC 1071 computes the checksum over a header whose checksum field is zero;
                 * the sum below simply skips the field, so this only makes that explicit and
                 * keeps a header which is reused rather than built fresh consistent on the wire
                 */

                checksum( 0 );

                std::uint32_t sum = ( type() << 8 ) + code() + identifier() + sequenceNumber();

                Iterator iter = bodyBegin;
                while( iter != bodyEnd )
                {
                    sum += static_cast< std::uint8_t >( *iter++ ) << 8;

                    if( iter != bodyEnd )
                    {
                        sum += static_cast< std::uint8_t >( *iter++ );
                    }
                }

                sum = ( sum >> 16 ) + ( sum & 0xFFFF );
                sum += ( sum >> 16 );

                checksum( static_cast< std::uint16_t >( ~sum ) );
            }

        private:

            std::uint16_t decode(
                SAA_in      const std::size_t                               a,
                SAA_in      const std::size_t                               b
                ) const NOEXCEPT
            {
                return ( m_data[ a ] << 8 ) + m_data[ b ];
            }

            void encode(
                SAA_in      const std::size_t                               a,
                SAA_in      const std::size_t                               b,
                SAA_in      const std::uint16_t                             n
                ) NOEXCEPT
            {
                m_data[ a ] = static_cast< std::uint8_t >( n >> 8 );
                m_data[ b ] = static_cast< std::uint8_t >( n & 0xFF );
            }
        };

    } // net

} // bl

#endif /* __BL_NETUTILS_H_ */
