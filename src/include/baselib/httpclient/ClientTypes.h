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

#ifndef __BL_HTTPCLIENT_CLIENTTYPES_H_
#define __BL_HTTPCLIENT_CLIENTTYPES_H_

#include <baselib/httpclient/HeaderProfile.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/Uri.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <string>

BL_IID_DECLARE( BodySource, "7fba12d0-9957-4e42-b0df-5960b87dcb93" )
BL_IID_DECLARE( BodySink, "f81600a3-7a8d-4c95-a0e8-1c5ade384cf8" )

namespace bl
{
    namespace httpclient
    {
        /*
         * The value objects and the body seams of the HTTP client - notes/plans/http2-design.md
         * 5.3, and the work order of slice S2.6
         *
         * THE CONTRACT THIS FILE EXISTS TO FREEZE. S4.1-S4.3, S5.1 and S5.2 are built in parallel
         * against these declarations and against ClientConnection.h. If they are vague those five
         * slices drift apart and meet in an integration failure, so they are published first and
         * then frozen: a change here is a change to every consumer and is negotiated rather than
         * made unilaterally
         *
         * DECOUPLED FROM THE SESSION ENGINE, DELIBERATELY AND WITHOUT EXCEPTION. Nothing declared
         * in this file or in ClientConnection.h names an http2::Session type - not an event, not a
         * command, not a stream id, not an error code of the protocol's own enumeration. Everything
         * crossing these interfaces is a plain value: a std::string, a net::Uri, an
         * http::HeaderList, a data::DataBlock, an eh::error_code, a time::time_duration or one of
         * the small enumerations below
         *
         * That is not tidiness. The I/O shell (S4.x, S5.x) and the session engine (S3.1) are built
         * against each other; if the contracts the shell is written against were expressed in the
         * engine's types then neither could be compiled, tested or reviewed until the other
         * existed. A plain value is what breaks that cycle, and it is also what lets the HTTP/1.1
         * driver - which has no Session at all - implement the very same interfaces (design 5.5)
         */

        /**
         * @brief The wire protocol a response arrived over, and which a connection speaks
         *
         * 'Unknown' is the state of a connection before ALPN has resolved; it is never the
         * protocol of a delivered response
         */

        enum class HttpProtocol : std::uint8_t
        {
            Unknown,
            Http11,
            Http2,
        };

        /**
         * @brief How a connection's protocol was settled, and the ALPN identifier that settled it
         *
         * WHY THIS IS ONE VALUE AND NOT TWO QUERIES. ClientResponse carries the protocol AND the
         * identifier the peer selected verbatim, and the two are not independent - a non-empty
         * identifier names the protocol. Published as two accessors on ClientConnection they could
         * disagree, and a consumer holding Http2 beside "http/1.1" would have no defined answer as
         * to which to believe. Here disagreement is UNREPRESENTABLE rather than merely discouraged:
         * the only door which sets a non-empty identifier is fromAlpn( ... ), which derives the
         * protocol FROM that identifier, so the two cannot be set independently at all and there is
         * no need to nominate one of them as authoritative
         *
         * THE EMPTY IDENTIFIER IS A STATEMENT AND NOT A GAP. It means ALPN did not decide this
         * connection: either no TLS took place - a cleartext HTTP/1.1 connection, or HTTP/2 by
         * prior knowledge - or the peer completed the handshake without selecting anything and we
         * fell back to HTTP/1.1. That last case is precisely why ClientResponse::negotiatedAlpn()
         * says "verbatim": a connection which fell back must NOT report "http/1.1" as though the
         * peer had chosen it, and a value derived from the protocol alone says exactly that. It is
         * the one case such a derivation gets wrong, and it is the case the field exists for
         *
         * A default constructed value is the state of a connection before ALPN has resolved -
         * HttpProtocol::Unknown, with no identifier
         */

        class NegotiatedProtocol FINAL
        {
        private:

            cpp::ScalarTypeIniter< HttpProtocol >                               m_protocol;
            std::string                                                         m_alpn;

            NegotiatedProtocol(
                SAA_in          const HttpProtocol                              protocol,
                SAA_in          std::string                                     alpn
                )
                :
                m_alpn( BL_PARAM_FWD( alpn ) )
            {
                m_protocol = protocol;
            }

        public:

            NegotiatedProtocol() NOEXCEPT
            {
            }

            /**
             * @brief The protocol an ALPN identifier names, or Unknown for one we do not speak
             *
             * The single place the mapping lives, so that the connection establisher which picks
             * a driver and the driver which reports the identifier cannot disagree about what it
             * means. 'h2c' is deliberately absent: it identifies cleartext upgrade and RFC 7540
             * section 3.3 forbids sending it in an ALPN extension over TLS
             */

            static HttpProtocol protocolOfAlpn( SAA_in const std::string& selected ) NOEXCEPT
            {
                if( "h2" == selected )
                {
                    return HttpProtocol::Http2;
                }

                if( "http/1.1" == selected )
                {
                    return HttpProtocol::Http11;
                }

                return HttpProtocol::Unknown;
            }

            /**
             * @brief The peer selected this identifier through ALPN; the protocol follows from it
             *
             * @throw NotSupportedException when it names nothing this build speaks - a refusal
             * rather than a fallback, for the reason ClientDriverFactoryT::createDriver gives
             */

            static NegotiatedProtocol fromAlpn( SAA_in std::string selected )
            {
                const auto protocol = protocolOfAlpn( selected );

                /*
                 * The identifier is not echoed into the message: it is what a peer put on the
                 * wire, and this library already declines to log a rejected header name for the
                 * same reason ( http::HeaderList::validateHeader )
                 */

                BL_CHK_T(
                    true,
                    HttpProtocol::Unknown == protocol,
                    NotSupportedException(),
                    BL_MSG()
                        << "The peer selected an ALPN protocol which this build does not speak"
                    );

                return NegotiatedProtocol( protocol, BL_PARAM_FWD( selected ) );
            }

            /**
             * @brief ALPN did not decide this connection, so it carries no identifier
             *
             * Both a cleartext connection and a TLS connection whose peer selected nothing are
             * this; the protocol was settled some other way and the identifier stays empty
             */

            static NegotiatedProtocol withoutAlpn( SAA_in const HttpProtocol protocol )
            {
                return NegotiatedProtocol( protocol, std::string() );
            }

            HttpProtocol protocol() const NOEXCEPT
            {
                return m_protocol;
            }

            /**
             * @brief The identifier the peer selected, verbatim, or empty when ALPN did not decide
             */

            const std::string& alpn() const NOEXCEPT
            {
                return m_alpn;
            }

            bool hasAlpn() const NOEXCEPT
            {
                return ! m_alpn.empty();
            }
        };

        /**
         * @brief The RFC 9218 priority signal of one request
         *
         * Carried as the two parameters of the scheme rather than as a rendered header value,
         * because HTTP/2 sends them in a PRIORITY_UPDATE frame and HTTP/1.1 sends them not at all;
         * the browser profile's own per-kind 'priority' header value is a separate thing and lives
         * in HeaderProfileForKind
         */

        struct RequestPriority
        {
            /**
             * The urgency, 0 (most urgent) through 7; RFC 9218 section 4.1 makes 3 the default
             */

            cpp::ScalarTypeIniter< std::uint8_t >                               urgency;

            /**
             * Whether the response is usable incrementally (RFC 9218 section 4.2)
             */

            cpp::ScalarTypeIniter< bool >                                       isIncremental;

            enum : std::uint8_t
            {
                DEFAULT_URGENCY                     = 3U,
                MAX_URGENCY                         = 7U,
            };

            RequestPriority() NOEXCEPT
            {
                urgency = DEFAULT_URGENCY;
            }
        };

        /**
         * @brief What one pull from a BodySource produced
         *
         * The size and the end-of-stream flag are separate because they are independent: a source
         * may legitimately produce zero bytes without being finished (it is waiting on something),
         * and it may produce the last bytes and know it is finished in the same pull. Returning
         * "zero means end of stream" would conflate the two and would make the common case - the
         * final non-empty chunk - cost one extra pull
         */

        struct BodyReadResult
        {
            cpp::ScalarTypeIniter< std::size_t >                                size;
            cpp::ScalarTypeIniter< bool >                                       isEndOfStream;
        };

        /**
         * @brief The pull side of a streaming request body (design 5.3)
         *
         * The driver pulls when it has window to send into, so the source is the point at which a
         * slow producer slows the request rather than buffering the whole upload
         *
         * canRewind() is what decides replayability, and through it whether a request may be
         * retried on another connection after a provably unprocessed failure (design 5.4, D6). A
         * source which cannot rewind makes the request unretryable; that is a correctness
         * statement about the source, not a hint, so it is asked before a retry and never guessed
         */

        class BodySource : public om::Object
        {
            BL_DECLARE_INTERFACE( BodySource )

        public:

            /**
             * @brief Fills 'target' with up to its remaining capacity and reports what was
             * produced
             *
             * The source appends at target -> size() and grows it; it never touches offset1(),
             * which belongs to the consumer
             */

            virtual BodyReadResult read( SAA_inout data::DataBlock& target ) = 0;

            /**
             * @brief Whether rewind() can restart this source from its first byte
             */

            virtual bool canRewind() const NOEXCEPT = 0;

            /**
             * @brief Restarts the source from its first byte
             *
             * @throw NotSupportedException when canRewind() is false
             *
             * IT RUNS UNDER TWO LOCKS, AND THE THREE RULES BELOW ARE THE CONTRACT RATHER THAN
             * ADVICE. Both call sites are inside SessionRequestTaskT::continuationTask( ) - the
             * retry's rewind and the redirect's - which ExecutionQueueImpl::onReady( ) calls with
             * the SCHEDULING LOCK of the execution queue the request was pushed to already held,
             * and which takes the session wrapper's own lock before it decides anything. So for as
             * long as this call runs, every push_back( ), wait( ) and pop( ) on that queue blocks,
             * and so does a requestCancel( ) on the wrapper.
             *
             * DO NOT BLOCK - not on I/O, not on a condition variable, not on anything another
             * thread has to run first. DO NOT SUBMIT TO THAT EXECUTION QUEUE, and do not wait on
             * anything already scheduled on it: that deadlocks outright. DO NOT RE-ENTER the
             * wrapper task, whose os::mutex is not recursive. Reopening a handle and seeking to
             * zero is the shape this is written for. Throwing is not a violation - the queue turns
             * a throw out of continuationTask( ) into this request's own failure.
             *
             * ( L6 finding 6 / astra H09. This is the LIVE half of that finding, and it is a
             * contract gap, so writing the contract is what closes it. The other half is the
             * registered ContentDecoder, which runs in the same place with far more CPU behind it;
             * the structural answer to both - moving the inter-hop work into the hop task's
             * deferred phase, which already runs off both locks - is recorded as a prerequisite of
             * the decoder programme in notes/plans/issues/http-content-decoders-deferral.md )
             */

            virtual void rewind() = 0;
        };

        /**
         * @brief The push side of a streaming response body (design 5.3)
         *
         * onData( ... ) returns the number of bytes it consumed, and that number is what the
         * request task reports to the connection as consumed - which is what credits the stream's
         * flow control window. So a sink which consumes less than it was offered applies
         * backpressure all the way to the server, and one which lies about it breaks flow control.
         * A sink which consumes nothing is not an error; the remainder is offered again
         */

        class BodySink : public om::Object
        {
            BL_DECLARE_INTERFACE( BodySink )

        public:

            /**
             * @brief Offers the bytes between data -> offset1() and data -> size()
             *
             * @return the number of bytes consumed, at most the number offered
             */

            virtual std::size_t onData( SAA_in const om::ObjPtr< data::DataBlock >& data ) = 0;

            /**
             * @brief The body is complete; no further onData( ... ) will follow
             */

            virtual void onComplete() = 0;
        };

        /**
         * @brief One client request - a value object, copyable and movable
         *
         * Copyable because the retry rule of design 5.4 replays a request on another connection and
         * a redirect (5.6) derives a second request from the first, so a request is duplicated by
         * the layers above rather than mutated in place. The body block and the body source are
         * therefore held by om::ObjPtrCopyable
         *
         * The body is EITHER a buffered block OR a source, never both, and setting one clears the
         * other. That is an invariant of the type rather than a convention, because a driver which
         * found both would have no defined answer to which one goes on the wire
         */

        class ClientRequest FINAL
        {
        private:

            std::string                                                         m_method;
            net::Uri                                                            m_url;
            http::HeaderList                                                    m_headers;

            om::ObjPtrCopyable< data::DataBlock >                               m_body;
            om::ObjPtrCopyable< BodySource >                                    m_bodySource;

            cpp::ScalarTypeIniter< HttpRequestKind >                            m_kind;
            RequestPriority                                                     m_priority;

            time::time_duration                                                 m_totalTimeout;
            time::time_duration                                                 m_responseHeadersTimeout;

        public:

            ClientRequest()
                :
                m_method( "GET" ),
                m_totalTimeout( time::neg_infin ),
                m_responseHeadersTimeout( time::neg_infin )
            {
                m_kind = HttpRequestKind::Fetch;
            }

            const std::string& method() const NOEXCEPT
            {
                return m_method;
            }

            void method( SAA_in std::string method )
            {
                m_method = BL_PARAM_FWD( method );
            }

            const net::Uri& url() const NOEXCEPT
            {
                return m_url;
            }

            void url( SAA_in net::Uri url )
            {
                m_url = BL_PARAM_FWD( url );
            }

            const http::HeaderList& headers() const NOEXCEPT
            {
                return m_headers;
            }

            http::HeaderList& headers() NOEXCEPT
            {
                return m_headers;
            }

            void headers( SAA_in http::HeaderList headers )
            {
                m_headers = BL_PARAM_FWD( headers );
            }

            const om::ObjPtrCopyable< data::DataBlock >& body() const NOEXCEPT
            {
                return m_body;
            }

            /**
             * @brief Sets the buffered body, clearing any body source
             */

            void body( SAA_in_opt om::ObjPtrCopyable< data::DataBlock > body ) NOEXCEPT
            {
                m_body = BL_PARAM_FWD( body );
                m_bodySource.reset();
            }

            const om::ObjPtrCopyable< BodySource >& bodySource() const NOEXCEPT
            {
                return m_bodySource;
            }

            /**
             * @brief Sets the streaming body source, clearing any buffered body
             */

            void bodySource( SAA_in_opt om::ObjPtrCopyable< BodySource > bodySource ) NOEXCEPT
            {
                m_bodySource = BL_PARAM_FWD( bodySource );
                m_body.reset();
            }

            bool hasBody() const NOEXCEPT
            {
                return nullptr != m_body || nullptr != m_bodySource;
            }

            HttpRequestKind kind() const NOEXCEPT
            {
                return m_kind;
            }

            void kind( SAA_in const HttpRequestKind kind ) NOEXCEPT
            {
                m_kind = kind;
            }

            const RequestPriority& priority() const NOEXCEPT
            {
                return m_priority;
            }

            void priority( SAA_in const RequestPriority& priority ) NOEXCEPT
            {
                m_priority = priority;
            }

            /**
             * @brief The deadline for the whole request, pool wait included
             *
             * A special value - the default is time::neg_infin - means "unset, apply the session
             * default", which is the sentinel this library already uses for an optional duration
             * ( HttpServerBackendMessagingBridge.h:1125, HttpServer.h:94 ). The session default is
             * 30 minutes ( design 5.7 )
             */

            const time::time_duration& totalTimeout() const NOEXCEPT
            {
                return m_totalTimeout;
            }

            void totalTimeout( SAA_in const time::time_duration& totalTimeout )
            {
                m_totalTimeout = totalTimeout;
            }

            /**
             * @brief The deadline for the response headers alone
             *
             * A special value means unset, and this one is off by default ( design 5.7 )
             */

            const time::time_duration& responseHeadersTimeout() const NOEXCEPT
            {
                return m_responseHeadersTimeout;
            }

            void responseHeadersTimeout( SAA_in const time::time_duration& responseHeadersTimeout )
            {
                m_responseHeadersTimeout = responseHeadersTimeout;
            }

            /**
             * @brief Whether this request may be sent a second time, on another connection
             *
             * The replayability half of the retry rule of design 5.4 - the other half, whether the
             * failure proves the request was unprocessed, belongs to the connection and to the
             * pool. It is a query on the request and NOT on the pool, because only the request
             * knows what its body is: a request with no body or a buffered one can always be
             * written again, and a streaming one can only if its source can rewind
             */

            bool isReplayable() const NOEXCEPT
            {
                return nullptr == m_bodySource || m_bodySource -> canRewind();
            }
        };

        /**
         * @brief One client response - a value object, copyable and movable
         *
         * The body is buffered by default and streamed when the caller installed a sink on the
         * request task, in which case body() is empty and the bytes went to the sink as they
         * arrived (design 5.3). Both forms are representable here so that a consumer does not
         * change shape with the mode
         */

        class ClientResponse FINAL
        {
        private:

            cpp::ScalarTypeIniter< unsigned >                                   m_status;
            http::HeaderList                                                    m_headers;
            http::HeaderList                                                    m_trailers;

            om::ObjPtrCopyable< data::DataBlock >                               m_body;

            /*
             * ONE MEMBER AND NOT TWO, for the reason NegotiatedProtocol's own note gives: held as
             * two fields with two setters, a response could be given Http2 beside "http/1.1" and
             * the property the connection enforces would be lost one hop later. A default
             * constructed value is already HttpProtocol::Unknown with no identifier, so a default
             * constructed response reports no protocol rather than claiming one
             */

            NegotiatedProtocol                                                  m_negotiated;

            /*
             * The fidelity report of design 6.6, which S7.x defines and this layer must not
             * depend on. It is carried as the base object interface and retrieved by the consumer
             * with om::qi< ... >, which is what keeps L2 free of a type that does not exist yet
             * and keeps this contract frozen when it does
             */

            om::ObjPtrCopyable< om::Object >                                    m_impersonationReport;

        public:

            unsigned status() const NOEXCEPT
            {
                return m_status;
            }

            void status( SAA_in const unsigned status ) NOEXCEPT
            {
                m_status = status;
            }

            const http::HeaderList& headers() const NOEXCEPT
            {
                return m_headers;
            }

            http::HeaderList& headers() NOEXCEPT
            {
                return m_headers;
            }

            void headers( SAA_in http::HeaderList headers )
            {
                m_headers = BL_PARAM_FWD( headers );
            }

            const http::HeaderList& trailers() const NOEXCEPT
            {
                return m_trailers;
            }

            http::HeaderList& trailers() NOEXCEPT
            {
                return m_trailers;
            }

            void trailers( SAA_in http::HeaderList trailers )
            {
                m_trailers = BL_PARAM_FWD( trailers );
            }

            const om::ObjPtrCopyable< data::DataBlock >& body() const NOEXCEPT
            {
                return m_body;
            }

            void body( SAA_in_opt om::ObjPtrCopyable< data::DataBlock > body ) NOEXCEPT
            {
                m_body = BL_PARAM_FWD( body );
            }

            /**
             * @brief What this response arrived over, and the ALPN identifier which settled it
             *
             * THE ONLY DOOR, and that is the point. There is no setter for either half on its
             * own, so the only way to populate a response is with a value which was already
             * constructed consistently - by fromAlpn( ... ), which derives the protocol from the
             * identifier, or by withoutAlpn( ... ), which leaves the identifier empty. A request
             * task fills it with the value its connection already publishes, unchanged
             */

            const NegotiatedProtocol& negotiated() const NOEXCEPT
            {
                return m_negotiated;
            }

            void negotiated( SAA_in NegotiatedProtocol negotiated )
            {
                m_negotiated = BL_PARAM_FWD( negotiated );
            }

            HttpProtocol protocol() const NOEXCEPT
            {
                return m_negotiated.protocol();
            }

            /**
             * @brief The ALPN protocol the peer selected, verbatim ("h2", "http/1.1"), or empty
             * for a cleartext connection where no ALPN took place
             */

            const std::string& negotiatedAlpn() const NOEXCEPT
            {
                return m_negotiated.alpn();
            }

            const om::ObjPtrCopyable< om::Object >& impersonationReport() const NOEXCEPT
            {
                return m_impersonationReport;
            }

            void impersonationReport( SAA_in_opt om::ObjPtrCopyable< om::Object > report ) NOEXCEPT
            {
                m_impersonationReport = BL_PARAM_FWD( report );
            }
        };

        /**
         * @brief A request's URL as a MESSAGE HANDED TO A CALLER may carry it - the scheme, the
         * authority and the path, and nothing else
         *
         * NOT net::Uri::toString( ), which recomposes the reference in full and therefore carries
         * three things a caller may have put a secret in: the USERINFO, the QUERY - a bearer or a
         * session token is commonly a query parameter - and the FRAGMENT, which the request never
         * even put on the wire. net::Uri::authority( ) excludes the userinfo by construction, which
         * is what makes this a rendering rather than a filter.
         *
         * WHAT THIS IS, STATED EXACTLY, because overstating it would be its own defect (astra H20).
         * The URL reaches a caller only INSIDE THE EXCEPTION these messages are built for: no
         * BL_LOG in this client emits one, and the framework's exception dump cannot fire for these
         * tasks - it is gated on a task name they never set. So what this closes is an
         * API-CONTRACT gap, that a message handed to a caller was not redaction-safe. It is not a
         * leak that was happening.
         *
         * THE PATH IS KEPT, deliberately: it is what tells one timed-out request from another, and
         * the message shape these callers already recognise - SimpleHttpTask::createTimeoutMessage(
         * )'s - has always carried it. The legacy task redacts the path too, but only under its own
         * isSecureMode, and this client has no such flag yet; L8's compatibility facade is where
         * one arrives, and this renderer is the seam it will need.
         *
         * Additive, and no part of the frozen contract this file's note governs - nothing above is
         * changed by it and no consumer has to know it exists
         */

        inline std::string redactedUrl( SAA_in const net::Uri& url )
        {
            std::string result;

            if( url.hasScheme() )
            {
                result += url.scheme();
                result += ':';
            }

            if( url.hasAuthority() )
            {
                result += "//";
                result += url.authority();
            }

            result += url.path();

            return result;
        }

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_CLIENTTYPES_H_ */
