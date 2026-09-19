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

#ifndef __BL_HTTPCLIENT_CLIENTCONNECTIONTASKBASE_H_
#define __BL_HTTPCLIENT_CLIENTCONNECTIONTASKBASE_H_

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/TcpTunnelStage.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

/*
 * The floor check of design 3.3 (D4) is crypto::CryptoBase::chkNegotiatedParametersMeetFloor,
 * which that header declares only from OpenSSL 1.1.0 onwards. The guard is on the capability
 * rather than on a devenv version, which is what every header here does - only the project
 * makefiles define BL_DEVENV_VERSION and no public header may require it
 */

#if ( OPENSSL_VERSION_NUMBER < 0x10100000L )
#error ClientConnectionTaskBase.h requires OpenSSL 1.1.0 or later for the negotiated parameter floor check (design 3.3, D4)
#endif

namespace bl
{
    namespace tasks
    {
        /**
         * @brief What a client connection task offers and assumes before ALPN has spoken
         *
         * It is a value the caller copies into the task, so two connections under two profiles do
         * not share it - which is also why the ALPN offer is a per connection list and not a
         * property of the TLS context (AsioSslStreamWrapper::setAlpnProtocolOffer says the same)
         */

        class ClientConnectionConfig FINAL
        {
        public:

            enum : long
            {
                /**
                 * Design 5.7's "connect: resolve through preface" deadline. It is the only thing
                 * which bounds a proxy that accepts the TCP connection and then never answers -
                 * the tunnel stage owns no timer of its own, deliberately (TcpTunnelStage.h, the
                 * three obligations), and the establisher has no connect deadline either
                 */

                DEFAULT_CONNECT_TIMEOUT_IN_SECONDS = 60L,
            };

            enum : std::size_t
            {
                /**
                 * @brief The handshake retry budget of a client connection task
                 *
                 * TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation restarts the
                 * WHOLE resolve / connect / handshake transaction on a retryable handshake error,
                 * immediately and with no delay between attempts, and its own MAX_RETRY_COUNT of 5
                 * therefore costs six handshakes against a peer which rejects consistently. Since
                 * the classifier was widened after L0 that is reachable with a real peer, so this
                 * budget is deliberately ONE: a single immediate retry is what catches the
                 * genuinely transient case the retry exists for - a peer which went away or
                 * truncated between the connect and the ServerHello - and anything beyond it, with
                 * no backoff, is amplification against a peer which is refusing. A retry policy
                 * which weighs the request and the origin belongs to the pool (design 5.4), not
                 * here
                 */

                DEFAULT_HANDSHAKE_RETRY_COUNT = 1U,
            };

            /**
             * @brief The ALPN identifiers offered, in descending order of preference
             *
             * Empty offers no ALPN at all. ALPN is a TLS extension, so this says nothing about a
             * cleartext connection - what such a connection speaks is cleartextProtocol below
             */

            std::vector< std::string >                                          alpnOffer;

            /**
             * @brief What a CLEARTEXT connection speaks, since nothing negotiates it
             *
             * Http11, or Http2 for a peer known to speak it by prior knowledge (RFC 9113 section
             * 3.3). It is never consulted on a TLS connection
             */

            cpp::ScalarTypeIniter< httpclient::HttpProtocol >                   cleartextProtocol;

            /**
             * @brief The deadline above; a zero or special duration disables it
             */

            time::time_duration                                                 connectTimeout;

            cpp::ScalarTypeIniter< std::size_t >                                maxHandshakeRetries;

            ClientConnectionConfig()
                :
                connectTimeout( time::seconds( DEFAULT_CONNECT_TIMEOUT_IN_SECONDS ) )
            {
                alpnOffer.push_back( "h2" );
                alpnOffer.push_back( "http/1.1" );

                cleartextProtocol = httpclient::HttpProtocol::Http11;
                maxHandshakeRetries = DEFAULT_HANDSHAKE_RETRY_COUNT;
            }

            /**
             * @brief The forced HTTP/1.1 configuration - "h2" is not offered, so the peer cannot
             * select it
             *
             * Forcing is done by not offering rather than by refusing what the peer chose: a peer
             * may only select from what it was offered (RFC 7301 section 3.1), so an offer of
             * "http/1.1" alone settles the protocol before a byte of it is spoken. Refusing an
             * "h2" the peer legitimately selected would be a connection failure instead
             */

            static auto forcedHttp11() -> ClientConnectionConfig
            {
                ClientConnectionConfig config;

                config.alpnOffer.clear();
                config.alpnOffer.push_back( "http/1.1" );

                return config;
            }
        };

        namespace detail
        {
            /**
             * @brief The three things which only a TLS stream policy can do, selected at compile
             * time from STREAM::isProtocolHandshakeNeeded
             *
             * The same mechanism, and for the same reason, as detail::HandshakeTaskHelper in
             * TcpBaseTasks.h: the stream policy is a static interface resolved by template
             * composition, so a cleartext policy simply has no getAlpnSelected() to call and a
             * runtime branch on a compile time constant would not compile
             */

            template
            <
                typename STREAM,
                bool ProtocolHandshakeNeeded = STREAM::isProtocolHandshakeNeeded
            >
            class ClientTlsStreamOps;

            template
            <
                typename STREAM
            >
            class ClientTlsStreamOps< STREAM, true >
            {
            public:

                static void setAlpnOffer(
                    SAA_inout       typename STREAM::stream_t&                  stream,
                    SAA_in          const std::vector< std::string >&           offer
                    )
                {
                    if( offer.empty() )
                    {
                        return;
                    }

                    stream.setAlpnProtocolOffer( offer );
                }

                static void chkFloor( SAA_inout typename STREAM::stream_t& stream )
                {
                    crypto::CryptoBase::chkNegotiatedParametersMeetFloor(
                        stream.getStream().native_handle()
                        );
                }

                /**
                 * @brief What the completed handshake settled, and how
                 *
                 * NegotiatedProtocol::fromAlpn( "" ) throws by design, so an EMPTY selection - the
                 * peer completed the handshake and chose nothing - must not go through it. Such a
                 * connection is HTTP/1.1 with no identifier, which is a statement and not a gap:
                 * a client which fell back must not report "http/1.1" as though the peer had
                 * selected it, and that is the one case ClientResponse::negotiatedAlpn() exists to
                 * distinguish. Only a NON-EMPTY SSL_get0_alpn_selected result goes through
                 * fromAlpn, which is also where an identifier this build does not speak is refused
                 *
                 * cleartextProtocol is not consulted here: nothing about a TLS connection is
                 * settled by prior knowledge
                 */

                static auto negotiatedProtocol(
                    SAA_inout       typename STREAM::stream_t&                  stream,
                    SAA_in          const httpclient::HttpProtocol              cleartextProtocol
                    )
                    -> httpclient::NegotiatedProtocol
                {
                    BL_UNUSED( cleartextProtocol );

                    const auto selected = stream.getAlpnSelected();

                    if( selected.empty() )
                    {
                        return httpclient::NegotiatedProtocol::withoutAlpn(
                            httpclient::HttpProtocol::Http11
                            );
                    }

                    return httpclient::NegotiatedProtocol::fromAlpn( selected );
                }
            };

            template
            <
                typename STREAM
            >
            class ClientTlsStreamOps< STREAM, false >
            {
            public:

                /**
                 * @brief A cleartext connection carries no ALPN extension, so the offer is a
                 * no-op rather than an error
                 *
                 * One configuration describes a connection whatever its scheme turns out to be,
                 * and a caller which offers "h2, http/1.1" has said nothing wrong by connecting in
                 * cleartext - it has said what it would offer if there were a handshake
                 */

                static void setAlpnOffer(
                    SAA_inout       typename STREAM::stream_t&                  stream,
                    SAA_in          const std::vector< std::string >&           offer
                    )
                {
                    BL_UNUSED( stream );
                    BL_UNUSED( offer );
                }

                static void chkFloor( SAA_inout typename STREAM::stream_t& stream )
                {
                    BL_UNUSED( stream );
                }

                static auto negotiatedProtocol(
                    SAA_inout       typename STREAM::stream_t&                  stream,
                    SAA_in          const httpclient::HttpProtocol              cleartextProtocol
                    )
                    -> httpclient::NegotiatedProtocol
                {
                    BL_UNUSED( stream );

                    return httpclient::NegotiatedProtocol::withoutAlpn( cleartextProtocol );
                }
            };

        } // detail

        /******************************************************************************************
         * ================================ ClientConnectionTaskBase ==============================
         */

        /**
         * @brief The client's connection establishment task - design 5.1, 5.5 and 5.7
         *
         * Resolve, connect, optional tunnel, TLS handshake, floor check, ALPN, and then the driver
         * the negotiated protocol calls for. STREAM is one of the stranded stream policies of
         * design 3.1. Nothing WRITTEN here requires that - every timer is built on the socket's own
         * executor, which is the strand under a stranded policy and the I/O service under a plain
         * one - but nothing instantiates a plain one either, and a template nothing instantiates is
         * not compiled, let alone correct. So what is claimed is what is compiled: the two STRANDED
         * policies, which the probes of TestClientConnectionTaskBase.h name
         *
         * WHAT THE CHAIN IS, AND WHY IN THAT ORDER. MultiOperationTaskT is the outermost, because
         * its accounting has to see the completion of every operation the task will run; the
         * tunnel stage is under it, because it overrides beginPreHandshakeStage and
         * continueAfterResolved of the establisher; the establisher is under that. Both mix-ins
         * are parameterized on their base for the same reason - a hard-wired base would give this
         * task two TaskBase subobjects, or two establishers
         *
         * THE MULTI-OPERATION ACCOUNTING IS NOT ENTERED BEFORE THE HANDSHAKE, AND THIS CLASS MUST
         * KEEP IT THAT WAY. MultiOperationTaskT clears its accounting in scheduleNothrow, while
         * TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation restarts the whole
         * resolve / connect / handshake transaction IN PLACE and never passes through it. So a
         * beginOperation(), or a handler ending with BL_TASKS_HANDLER_END_MULTIOP(), anywhere
         * before continueAfterConnected would leave a retry with a pending count carried over from
         * the attempt before - and if the terminal path had been taken the task would never
         * complete at all. Nothing in this class calls either: the connect deadline below is
         * deliberately outside the accounting, exactly as the tunnel stage under it is. A derived
         * driver (S4.2) begins its operations from onProtocolNegotiated onwards, which is after
         * the handshake and therefore after the last point a retry can restart from
         *
         * THE TWO PATHS OUT STAY DISTINGUISHABLE (design 3.2). cancelTask() is an external cancel
         * and completes the task failed with operation_aborted; a deliberate beginClose() is the
         * task deciding it is done and completes it successfully. Both converge on the single
         * terminal notifyReady of the mix-in. The connect deadline takes the FIRST of those, not
         * the second: a deadline which expired is not a task which decided it was done
         */

        template
        <
            typename STREAM
        >
        class ClientConnectionTaskBaseT :
            public MultiOperationTaskT< TcpTunnelStageT< TcpConnectionEstablisherConnector< STREAM > > >
        {
            BL_DECLARE_OBJECT_IMPL( ClientConnectionTaskBaseT )

        public:

            /*
             * Both are redeclared because the mix-in's are public and hide the establisher's
             * protected ones - which is what every derived task in this library does anyway
             * (SimpleHttpTask.h:57-58), so it is the house idiom rather than a workaround
             */

            typedef ClientConnectionTaskBaseT< STREAM >                         this_type;

            typedef MultiOperationTaskT
            <
                TcpTunnelStageT< TcpConnectionEstablisherConnector< STREAM > >
            >
            base_type;

            typedef httpclient::ClientDriverFactoryT< STREAM >                  factory_t;
            typedef std::shared_ptr< factory_t >                                factory_ptr_t;

        protected:

            typedef detail::ClientTlsStreamOps< STREAM >                        tls_ops_t;

            const httpclient::ConnectionKey                                     m_key;
            const factory_ptr_t                                                 m_driverFactory;
            const ClientConnectionConfig                                        m_config;

            /*
             * Written once, on the strand, in continueAfterConnected, and read afterwards. The
             * DRIVER's own copy is the one which must be const - negotiated() returns a reference
             * and the pool and the request task read it off the strand - and the driver is given
             * the value at construction, below, which is the one moment it is in hand
             */

            httpclient::NegotiatedProtocol                                      m_negotiated;
            om::ObjPtr< httpclient::ClientConnection >                          m_connection;

            cpp::SafeUniquePtr< asio::deadline_timer >                          m_connectTimer;

            ClientConnectionTaskBaseT(
                SAA_in              httpclient::ConnectionKey                   key,
                SAA_in              factory_ptr_t                               driverFactory,
                SAA_in              ProxyConfig                                 proxyConfig = ProxyConfig::none(),
                SAA_in              ClientConnectionConfig                      config = ClientConnectionConfig(),
                SAA_in              const bool                                  logExceptions = true
                )
                :
                base_type(
                    std::string( key.host ),
                    key.port,
                    BL_PARAM_FWD( proxyConfig ),
                    logExceptions
                    ),
                m_key( BL_PARAM_FWD( key ) ),
                m_driverFactory( BL_PARAM_FWD( driverFactory ) ),
                m_config( BL_PARAM_FWD( config ) )
            {
                BL_CHK_T(
                    false,
                    nullptr != m_driverFactory,
                    ArgumentException(),
                    BL_MSG()
                        << "A client connection task requires a driver factory"
                    );

                base_type::m_maxRetryCount = m_config.maxHandshakeRetries;
            }

            /**
             * @brief The deadline of design 5.7, on the socket's own executor
             *
             * Under a stranded policy that executor IS the strand and a thread pool's io_service
             * is not, so a timer built on the pool would run its handler off the strand - the race
             * D13 exists to make unrepresentable. It is built per attempt because the socket is:
             * scheduleTaskFinishContinuation creates a new one on every retry, and a timer which
             * outlived its socket would be armed on the previous attempt's strand
             */

            void armConnectDeadline()
            {
                if(
                    m_config.connectTimeout.is_special() ||
                    m_config.connectTimeout.total_milliseconds() <= 0
                    )
                {
                    return;
                }

                cancelConnectDeadline();

                m_connectTimer.reset(
                    new asio::deadline_timer(
                        #if ( ( BOOST_VERSION / 100 ) >= 1072 )
                        base_type::getSocket().get_executor()
                        #else
                        base_type::getSocket().get_io_service()
                        #endif
                        )
                    );

                m_connectTimer -> expires_from_now( m_config.connectTimeout );

                m_connectTimer -> async_wait(
                    cpp::bind(
                        &this_type::onConnectDeadline,
                        om::ObjPtrCopyable< this_type >::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            /**
             * @brief Disarms the deadline; a derived driver calls it once the preface is away
             */

            void cancelConnectDeadline() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_connectTimer )
                {
                    eh::error_code ec;

                    m_connectTimer -> cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief The deadline expired - cancel the task, exactly as the TLS protocol deadline
             * of TcpSslBaseTasks.h does
             *
             * It is deliberately NOT one of the task handler macros. The macros complete the task
             * from the handler which failed first, and this handler is outside the
             * multi-operation accounting - the task may have no operation in flight at all when it
             * runs, since the tunnel stage is entered from inside the connect handler. What it
             * does instead touches no stream state: it takes the task lock and requests a cancel,
             * which reaches the stranded cancelTask() and is posted to the strand from there
             */

            void onConnectDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( TaskBase::m_lock );

                if( ! ec && Task::Running == TaskBase::m_state )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Cancelling a client connection to '"
                            << m_key.host
                            << ":"
                            << m_key.port.value()
                            << "' which did not establish within "
                            << m_config.connectTimeout
                        );

                    TaskBase::requestCancelInternal();
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Arms the deadline and offers ALPN, BEFORE the pre-handshake stage is entered
             *
             * The deadline has to be armed before the stage and not after it. The stage owns no
             * timer, deliberately - one inside it would acquire obligation 2 of
             * beginPreHandshakeStage, since a stage with no socket I/O in flight is never woken by
             * the base cancelTask() - so this deadline is the only thing which bounds a proxy that
             * accepts the connection and then never answers, and a deadline armed after the stage
             * returned is never reached on that path at all
             *
             * WHY HERE AND NOT IN continueAfterResolved, WHICH WOULD ALSO COVER THE CONNECT. There
             * is no socket to build the timer on, and no stream to offer ALPN on, until the base's
             * continueAfterResolved has created one - and by the time it returns it has an
             * async_connect in flight, so anything which threw after it would complete the task
             * with an operation outstanding and re-enter the completion path from the connect
             * handler. That is the very failure mode MultiOperationTaskT exists to prevent, and
             * the pre-handshake phase is outside its accounting by construction. Here the connect
             * has completed and nothing is in flight, so a throw fails the task once and cleanly,
             * exactly as a tryConfigureConnectedStream failure would
             *
             * What is left outside the deadline is therefore the resolve and the TCP connect. Both
             * are bounded by the operating system, and the establisher has never had a connect
             * deadline of its own; the tunnel is the part which is bounded by nothing else
             *
             * Entered once per attempt, so a retry re-arms the deadline on the new socket and
             * re-offers ALPN on the new stream - neither can be carried over, because
             * scheduleTaskFinishContinuation creates both again
             */

            virtual bool beginPreHandshakeStage( SAA_in const cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                armConnectDeadline();

                tls_ops_t::setAlpnOffer( base_type::getStream(), m_config.alpnOffer );

                return base_type::beginPreHandshakeStage( continueCallback );
            }

            /**
             * @brief Refuses a connection whose negotiated TLS parameters are below the floor
             * (design 3.3, D4)
             *
             * The ORDER is what this task owns and the rule itself is not: it is called from
             * continueAfterConnected before the protocol is read and before any driver exists, so
             * a connection which fails it carries no HTTP byte in either direction. What the floor
             * actually is lives in crypto::CryptoBase and is tested there
             *
             * Virtual so that a profile can tighten it, and so that the ordering above can be
             * exercised without a peer which negotiates a below-floor suite - which, against a
             * client context at security level 2, cannot be produced at all
             */

            virtual void chkNegotiatedParametersMeetFloor()
            {
                tls_ops_t::chkFloor( base_type::getStream() );
            }

            /**
             * @brief The connection is established and the protocol is settled - build the driver
             *
             * The whole NegotiatedProtocol travels to the factory, not the protocol it dispatches
             * on: the driver has to answer negotiated() afterwards and this is the only moment at
             * which the identifier the peer actually selected is in hand. The stream travels with
             * it by rvalue reference, because ownership moves - after the call the driver owns the
             * connected stream and this task does not, which is also why nothing here shuts it
             * down afterwards
             *
             * Returning false completes this task, which is the base behaviour: establishment is
             * all it does. A derived driver which IS the connection (design 5.1) overrides this,
             * keeps the stream, starts its own operations and returns true - and falls back to
             * this implementation for the protocol it does not speak, which is how an h2 task
             * whose peer selected http/1.1 hands the connected stream to the HTTP/1.1 driver
             */

            virtual bool onProtocolNegotiated()
            {
                auto connectedStream = base_type::detachStream();

                m_connection = m_driverFactory -> createDriver(
                    m_negotiated,
                    std::move( connectedStream ),
                    m_key
                    );

                BL_CHK_T(
                    false,
                    nullptr != m_connection.get(),
                    UnexpectedException(),
                    BL_MSG()
                        << "A client driver factory created no connection"
                    );

                return false;
            }

            virtual bool continueAfterConnected() OVERRIDE
            {
                base_type::ensureChannelIsOpen();

                /*
                 * Before anything is read off the connection or written to it
                 */

                chkNegotiatedParametersMeetFloor();

                m_negotiated = tls_ops_t::negotiatedProtocol(
                    base_type::getStream(),
                    m_config.cleartextProtocol
                    );

                BL_LOG(
                    Logging::trace(),
                    BL_MSG()
                        << "Client connection to '"
                        << m_key.host
                        << ":"
                        << m_key.port.value()
                        << "' established; ALPN selected '"
                        << m_negotiated.alpn()
                        << "'"
                    );

                return onProtocolNegotiated();
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * The deadline holds a reference to this task for as long as it is armed, so it is
                 * cancelled here rather than where the connection was established - the TLS
                 * shutdown runs as the finish continuation of the task, while the task is still
                 * Running, which is the same reason TcpSslBaseTasks.h cancels its protocol timer
                 * here
                 */

                cancelConnectDeadline();

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }

        public:

            /**
             * @brief What the connection speaks and the identifier which settled it
             *
             * Default constructed - HttpProtocol::Unknown with no identifier - until the handshake
             * has completed and ALPN has been read
             */

            auto negotiated() const NOEXCEPT -> const httpclient::NegotiatedProtocol&
            {
                return m_negotiated;
            }

            /**
             * @brief The driver the factory built, or nullptr if the task has not got that far
             */

            auto connection() const NOEXCEPT -> const om::ObjPtr< httpclient::ClientConnection >&
            {
                return m_connection;
            }

            auto key() const NOEXCEPT -> const httpclient::ConnectionKey&
            {
                return m_key;
            }
        };

    } // tasks

} // bl

#endif /* __BL_HTTPCLIENT_CLIENTCONNECTIONTASKBASE_H_ */
