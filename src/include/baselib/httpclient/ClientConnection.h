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

#ifndef __BL_HTTPCLIENT_CLIENTCONNECTION_H_
#define __BL_HTTPCLIENT_CLIENTCONNECTION_H_

#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <map>
#include <string>

BL_IID_DECLARE( ClientStreamEventSink, "f2642719-a67a-42c5-8b26-7385bcfa9979" )
BL_IID_DECLARE( ClientConnection, "075efa1e-c818-480a-9698-59abb3b6501c" )
BL_IID_DECLARE( ConnectionPool, "60c0d216-2279-4246-aa9c-04dbbdaa08c9" )

namespace bl
{
    namespace httpclient
    {
        /*
         * The role interfaces of the client's I/O shell - notes/plans/http2-design.md 5.2 and 5.4,
         * and the work order of slice S2.6
         *
         * The decoupling statement at the top of ClientTypes.h governs this file too, and this is
         * where it does the most work: a ClientConnection is implemented by the HTTP/2 connection
         * task, which owns an http2::Session, AND by the HTTP/1.1 connection task, which has no
         * Session at all. Not one signature below could name a Session type and still be
         * implementable by both
         *
         * EVERY CALL ON ClientConnection IS ASYNCHRONOUS (design 5.2 rule L3). An implementation
         * posts to its own strand and returns; none of them may block, and none may call the
         * caller back before returning. The event sink is fed from the request's mailbox by a
         * posted drain, never from inside one of these calls and never under the connection's
         * lock (rules L2 and L4). That is a deadlock-avoidance rule with a worked example in
         * design 5.2, not a style preference
         */

        /**
         * @brief The handle a connection gives back for one submitted request
         *
         * Opaque to everything above the driver. It is NOT an HTTP/2 stream id: an HTTP/1.1
         * connection has no stream ids and still has to hand back something the request task can
         * name itself with, and an h2 driver is free to make it an id or an index into its own
         * stream table. Treating it as a protocol value is exactly the coupling this contract
         * exists to prevent
         */

        typedef std::uint64_t                                                   stream_handle_t;

        /**
         * @brief The lifecycle of a connection, as the pool sees it (design 5.4)
         */

        enum class ConnectionState : std::uint8_t
        {
            /**
             * A placeholder the pool inserted before the connection task was created, so that
             * concurrent requests for one key queue behind it instead of each opening a connection
             */

            Connecting,

            /**
             * Usable; freeStreamSlots() decides whether it can take another request now
             */

            Ready,

            /**
             * GOAWAY received, or a graceful close started. In-flight streams finish; nothing new
             * is dispatched to it
             */

            Draining,

            /**
             * Finished. Nothing is in flight and nothing may be submitted
             */

            Closed,
        };

        /**
         * @brief How one request ended, as told to the pool when its slot is released
         *
         * The pool needs this and not the status code: what it decides is whether the connection
         * is still reusable and whether a waiter may have the slot
         */

        enum class RequestOutcome : std::uint8_t
        {
            /**
             * The response was fully received and the connection is reusable
             */

            Completed,

            /**
             * The request failed but the connection itself is intact - a stream error
             */

            Failed,

            /**
             * The request failed and the connection cannot be used again
             */

            ConnectionUnusable,
        };

        /**
         * @brief The stream events a driver delivers to one request (design 5.2 rule L3)
         *
         * Implemented by the request task; fed through that task's mailbox, which is what makes
         * the order below a guarantee rather than a hope - posting two handlers to a
         * multi-threaded io_service does not order them
         *
         * The order is: zero or more onHeaders( ..., isInterim = true ), then exactly one
         * onHeaders( ..., isInterim = false ), then zero or more onData( ... ), then at most one
         * onTrailers( ... ), then exactly one onClosed( ... ). onClosed( ... ) arrives even when
         * the stream failed before any header, and it is always the last event
         */

        class ClientStreamEventSink : public om::Object
        {
            BL_DECLARE_INTERFACE( ClientStreamEventSink )

        public:

            /**
             * @brief A response header block, and the status of the response it belongs to
             *
             * 'status' is the response status code - the three-digit ':status' pseudo-header for
             * HTTP/2 and the status-line code for HTTP/1.1. It is a PARAMETER and not a field of
             * 'headers' because http::HeaderList cannot hold ':status': a colon is not a token
             * character, so the list refuses that name by design (S1.2), and the session engine
             * strips the pseudo-headers when it validates a decoded block in any case. Without it
             * ClientResponse::status() has nothing to be filled from, which is how this contract
             * was first published and what the L2 review found
             *
             * EVERY header block carries its OWN status, interim ones included: a 103 Early Hints
             * block arrives as onHeaders( handle, 103, hints, true ) and the response that follows
             * as onHeaders( handle, 200, headers, false ). The consumer takes the status of the
             * final block as the response's; an interim status is that interim response's own and
             * never overwrites it
             *
             * 'isInterim' marks a 1xx response other than 101 (RFC 9110 section 15.2), which is
             * followed by a further header block rather than by the body. It stays although the
             * status makes it derivable - it is true exactly when 'status' is in [100, 199] and is
             * not 101 - because it states the STRUCTURAL fact the ordering guarantee above is
             * written in terms of, that another header block follows, and a consumer should not
             * have to re-derive that from a number. A driver states both, and the two must agree
             */

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          http::HeaderList&&                              headers,
                SAA_in          const bool                                      isInterim
                ) = 0;

            /**
             * @brief Body bytes, between data -> offset1() and data -> size()
             *
             * The receiver credits flow control by calling ClientConnection::consumed( ... ) with
             * what it took; until it does, the window it was given is spent (design 5.3)
             */

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const om::ObjPtr< data::DataBlock >&            data
                ) = 0;

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          http::HeaderList&&                              trailers
                ) = 0;

            /**
             * @brief The stream ended; a default constructed error code means it ended normally
             *
             * 'isRetryable' is the connection's answer to the "provably unprocessed" half of the
             * retry rule of design 5.4 - the stream id was above a GOAWAY's last-stream-id, or the
             * reset was REFUSED_STREAM, or nothing was written. It is NOT a judgement about the
             * body, which is the request's own (ClientRequest::isReplayable); a replay needs both
             */

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT = 0;
        };

        /**
         * @brief What a request task and the pool can ask of a connection, whatever it speaks
         *
         * Implemented by both drivers (design 5.5). Every call posts and returns
         */

        class ClientConnection : public om::Object
        {
            BL_DECLARE_INTERFACE( ClientConnection )

        public:

            enum : stream_handle_t
            {
                /**
                 * The handle value no submitted request ever has
                 */

                INVALID_STREAM_HANDLE               = 0U,
            };

            /**
             * @brief Submits a request and returns the handle its events will carry
             *
             * The handle is returned synchronously - the caller needs something to cancel with
             * before any event can arrive - but nothing is written synchronously. A connection
             * which cannot take the request returns INVALID_STREAM_HANDLE and delivers nothing
             *
             * The sink is held for the life of the stream and released after onClosed( ... )
             */

            virtual stream_handle_t submit(
                SAA_in          const ClientRequest&                            request,
                SAA_in          const om::ObjPtr< ClientStreamEventSink >&      eventSink
                ) = 0;

            /**
             * @brief Cancels one stream; the connection itself stays up (design 5.7)
             */

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode
                ) NOEXCEPT = 0;

            /**
             * @brief Reports bytes the request has consumed, which is what credits the stream's
             * flow control window (design 5.3)
             */

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) = 0;

            /**
             * @brief Hands the next chunk of a streaming request body to the driver
             *
             * 'endStream' marks the last chunk; a nullptr block with endStream true ends a body
             * which has no further bytes
             */

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const om::ObjPtr< data::DataBlock >&            data,
                SAA_in          const bool                                      endStream
                ) = 0;

            /**
             * @brief How many more requests this connection can take right now
             *
             * Bounded by the peer's SETTINGS_MAX_CONCURRENT_STREAMS, or by one in flight for
             * HTTP/1.1. Zero for a connection which is Connecting, Draining or Closed
             */

            virtual std::size_t freeStreamSlots() const NOEXCEPT = 0;

            virtual ConnectionState state() const NOEXCEPT = 0;

            /**
             * @brief What this connection speaks, and the ALPN identifier which settled it
             *
             * ONE QUERY AND NOT TWO, so that the protocol and the identifier cannot disagree -
             * NegotiatedProtocol's own note says why, and says why an empty identifier is a
             * statement rather than a gap. Unknown with no identifier until ALPN has resolved
             *
             * This is what fills BOTH ClientResponse::protocol() and
             * ClientResponse::negotiatedAlpn(). Publishing only the protocol left the second of
             * those unfillable: a request task can derive "h2" or "http/1.1" from the protocol and
             * the URL scheme, but that derivation is wrong for a TLS connection whose peer
             * selected nothing, which must report empty and would derive as "http/1.1" - the one
             * case the field exists to distinguish. The same defect as the missing status, found
             * in the same review
             */

            virtual const NegotiatedProtocol& negotiated() const NOEXCEPT = 0;
        };

        /**
         * @brief What decides that two requests may share a connection (design 5.4)
         *
         * The HTTP/2 profile is part of the key because the connection-level fingerprint - the
         * SETTINGS, the window and the PRIORITY frames of the preface - is per connection, so two
         * requests under different profiles cannot share one however identical their origins are.
         * The TLS profile is in it for the same reason one layer down
         *
         * The scheme and the host are the lowercased ones net::Uri normalizes to, and the port is
         * the effective port rather than the written one, so that a URL which spells out the
         * default port and one which leaves it out land on the same connection - which is the
         * reason net::Uri::origin() renders the effective port at all
         */

        struct ConnectionKey
        {
            std::string                                                         scheme;
            std::string                                                         host;
            cpp::ScalarTypeIniter< os::port_t >                                 port;

            /**
             * The proxy this connection goes through, as a stable identity string; empty for a
             * direct connection
             */

            std::string                                                         proxyId;

            std::string                                                         tlsProfileId;
            std::string                                                         http2ProfileId;

            /**
             * The peer verification flags in force, as a stable identity value. Two requests which
             * disagree about verification must not share a connection: the second would inherit
             * the first one's verification decision silently
             */

            cpp::ScalarTypeIniter< std::uint32_t >                              verificationFlags;

            /**
             * @brief The total order the pool indexes by
             */

            bool operator<( SAA_in const ConnectionKey& other ) const
            {
                if( scheme != other.scheme )
                {
                    return scheme < other.scheme;
                }

                if( host != other.host )
                {
                    return host < other.host;
                }

                if( port != other.port )
                {
                    return port < other.port;
                }

                if( proxyId != other.proxyId )
                {
                    return proxyId < other.proxyId;
                }

                if( tlsProfileId != other.tlsProfileId )
                {
                    return tlsProfileId < other.tlsProfileId;
                }

                if( http2ProfileId != other.http2ProfileId )
                {
                    return http2ProfileId < other.http2ProfileId;
                }

                return verificationFlags < other.verificationFlags;
            }

            bool operator==( SAA_in const ConnectionKey& other ) const
            {
                return
                    scheme == other.scheme &&
                    host == other.host &&
                    port == other.port &&
                    proxyId == other.proxyId &&
                    tlsProfileId == other.tlsProfileId &&
                    http2ProfileId == other.http2ProfileId &&
                    verificationFlags == other.verificationFlags;
            }

            bool operator!=( SAA_in const ConnectionKey& other ) const
            {
                return ! ( *this == other );
            }

            /**
             * @brief The key of the origin a URI names, with no proxy and no profile
             *
             * net::Uri::origin() is called for its refusal and not for its value: it throws for a
             * relative reference and for one with no host, which are exactly the two cases that
             * would otherwise produce a key with an empty host - and a key with an empty host
             * collides with every other such key, which is one request reusing another's
             * connection. The caller fills in the proxy and the profile fields afterwards
             *
             * @throw ArgumentException when the URI is not an absolute URI with a host
             */

            static ConnectionKey fromUri( SAA_in const net::Uri& uri )
            {
                ( void ) uri.origin();

                ConnectionKey key;

                key.scheme = uri.scheme();
                key.host = uri.host();
                key.port = uri.effectivePort();

                return key;
            }
        };

        /**
         * @brief What a request task asks of the connection pool (design 5.4)
         *
         * acquire( ... ) never returns a connection: it posts one. A pool which answered inline
         * would be calling into the request task under the pool lock, which is rule L4
         */

        class ConnectionPool : public om::Object
        {
            BL_DECLARE_INTERFACE( ConnectionPool )

        public:

            /**
             * @brief Called with a connection, or with an exception when none could be had
             *
             * Exactly one of the two is set. Posted, never called from inside acquire( ... )
             */

            typedef cpp::function
            <
                void (
                    SAA_in_opt      const om::ObjPtr< ClientConnection >&       connection,
                    SAA_in_opt      const std::exception_ptr&                   exception
                    )
            >
            on_ready_callback_t;

            /**
             * @brief Asks for a connection for 'key', queueing behind a Connecting placeholder or
             * behind a busy connection if it has to
             *
             * The request is a parameter because the pool's decision depends on it - its deadline
             * bounds the FIFO wait, and its replayability decides what may be moved to another
             * connection - and not because the pool submits it; submitting is the request task's
             */

            virtual void acquire(
                SAA_in          const ConnectionKey&                            key,
                SAA_in          const ClientRequest&                            request,
                SAA_in          on_ready_callback_t&&                           onReady
                ) = 0;

            /**
             * @brief Gives the stream slot back and tells the pool what became of the request
             *
             * Named releaseStream( ... ) and not release( ... ): om::Object's reference counting
             * IS a release() (om::detail::DeleterT calls ptr -> release()), so an interface which
             * declares its own release( ... ) hides it and every ObjPtr to that interface fails to
             * compile at its destructor. The collision is not specific to this contract - no
             * om interface in this library may carry a member of that name
             */

            virtual void releaseStream(
                SAA_in          const om::ObjPtr< ClientConnection >&           connection,
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const RequestOutcome                            outcome
                ) NOEXCEPT = 0;
        };

        /**
         * @brief The factory S4.1 dispatches through once ALPN has chosen a protocol
         *
         * WHY THIS IS A CLASS TEMPLATE AND NOT AN om INTERFACE. It is unavoidably parameterized by
         * the stream policy: the two drivers are Http2ConnectionTaskT< STREAM > and
         * Http1ConnectionTaskT< STREAM > (design 5.1, 5.5), and what is handed over is that
         * policy's own STREAM::stream_ref. There is no templated om interface anywhere in this
         * library and there cannot be one - BL_DECLARE_INTERFACE needs an interface id per type
         * and an id cannot be generated per instantiation. The precedent for a factory over STREAM
         * here is tasks::detail::HandshakeTaskHelper< STREAM, bool > (TcpBaseTasks.h), a class
         * template with static creators taking typename STREAM::stream_ref&&
         *
         * WHY THE CREATORS ARE REGISTERED RATHER THAN NAMED. This slice publishes the contract
         * before either driver exists, and it may not name them; S4.1 registers the h2 creator and
         * S2.5/S4.3 the http/1.1 one. That also makes the dispatch testable now, against a stub
         * creator and a stub stream, which is the point of publishing the contract first
         *
         * Not thread safe, and deliberately so: a factory is populated once at session
         * construction and read afterwards, so a lock here would be a lock on the path of every
         * connection for a race that does not exist
         */

        template
        <
            typename STREAM
        >
        class ClientDriverFactoryT FINAL
        {
        public:

            typedef typename STREAM::stream_ref                                 stream_ref;

            /**
             * @brief Builds the driver for one already connected stream
             *
             * The stream is passed by rvalue reference because ownership moves: after the call the
             * driver owns it and the connection establisher does not
             *
             * The WHOLE NegotiatedProtocol travels, not the protocol it dispatches on, because the
             * driver has to answer negotiated() afterwards and this is the only moment at which the
             * identifier the peer actually selected is in hand. Passing the enum alone is where
             * that identifier used to be dropped, and it cannot be recovered downstream: the
             * creator is registered once per session and cannot capture a per-connection value.
             * Being handed the value is also what stops a driver inventing one which disagrees
             */

            typedef cpp::function
            <
                om::ObjPtr< ClientConnection > (
                    SAA_in          const NegotiatedProtocol&                   negotiated,
                    SAA_inout       stream_ref&&                                connectedStream,
                    SAA_in          const ConnectionKey&                        key
                    )
            >
            creator_t;

        private:

            std::map< HttpProtocol, creator_t >                                 m_creators;

        public:

            /**
             * @brief Registers the creator for one protocol, replacing any previous one
             *
             * @throw ArgumentException for HttpProtocol::Unknown, which is the value that means
             * "ALPN has not resolved" and can therefore never name a driver
             */

            void registerDriver(
                SAA_in          const HttpProtocol                              protocol,
                SAA_in          creator_t&&                                     creator
                )
            {
                BL_CHK_T(
                    true,
                    HttpProtocol::Unknown == protocol,
                    ArgumentException(),
                    BL_MSG()
                        << "A driver cannot be registered for HttpProtocol::Unknown, which is the "
                        << "state of a connection before ALPN has resolved"
                    );

                BL_CHK_T(
                    false,
                    !! creator,
                    ArgumentException(),
                    BL_MSG()
                        << "A driver creator must not be empty"
                    );

                m_creators[ protocol ] = BL_PARAM_FWD( creator );
            }

            bool hasDriver( SAA_in const HttpProtocol protocol ) const NOEXCEPT
            {
                return m_creators.find( protocol ) != m_creators.end();
            }

            /**
             * @brief Creates the driver for the negotiated protocol
             *
             * @throw NotSupportedException when no driver is registered for it - which is what a
             * peer selecting an ALPN protocol this build does not speak looks like, and is a
             * refusal rather than a fallback on purpose: quietly speaking something other than
             * what was negotiated is how a client ends up sending HTTP/1.1 into an h2 connection
             */

            om::ObjPtr< ClientConnection > createDriver(
                SAA_in          const NegotiatedProtocol&                       negotiated,
                SAA_inout       stream_ref&&                                    connectedStream,
                SAA_in          const ConnectionKey&                            key
                )
            {
                const auto pos = m_creators.find( negotiated.protocol() );

                BL_CHK_T(
                    true,
                    pos == m_creators.end(),
                    NotSupportedException(),
                    BL_MSG()
                        << "No HTTP client driver is registered for the negotiated protocol"
                    );

                return pos -> second( negotiated, BL_PARAM_FWD( connectedStream ), key );
            }
        };

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_CLIENTCONNECTION_H_ */
