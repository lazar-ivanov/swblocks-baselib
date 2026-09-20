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

#ifndef __BL_HTTP2_HTTP2CONNECTIONTASK_H_
#define __BL_HTTP2_HTTP2CONNECTIONTASK_H_

#include <baselib/http2/Session.h>
#include <baselib/http2/Globals.h>
#include <baselib/http2/Http2Profile.h>

#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace bl
{
    namespace tasks
    {
        /**
         * @brief Everything an HTTP/2 connection needs which is not the session engine's
         *
         * A value the caller copies into the task, for the same reason ClientConnectionConfig is
         * one: two connections under two profiles must not share it
         *
         * THE DURATIONS ARE OFF BY DEFAULT WHERE DESIGN 5.7 GIVES THEM NO OWNER HERE. The
         * keepalive PING is optional by design, and the connection idle lifetime's VALUE is the
         * POOL's (design 5.4 lists it as a pool policy knob and 5.7's table, as the L6 fix round
         * corrected it, has the pool set it and each driver enforce it), so this task takes it as
         * a parameter rather than inventing the five minutes the table quotes. A special or
         * non-positive duration disables the timer it belongs to
         *
         * drainTimeout is the exception and is ON by default: it is this task's own backstop
         * rather than a policy anyone above it sets, and a connection with it off can wait on a
         * write the peer controls for as long as TCP will let it - see armDrainDeadline( )
         */

        class Http2ConnectionConfig FINAL
        {
        public:

            enum : long
            {
                /**
                 * Design 5.7's keepalive PING reply deadline, which is only armed when the
                 * keepalive interval is set at all
                 */

                DEFAULT_PING_REPLY_TIMEOUT_IN_SECONDS       = 15L,

                /**
                 * How long the GOAWAY of a close may take to reach a peer before the connection
                 * is cancelled instead - see armDrainDeadline( ). Ten seconds is the SETTINGS
                 * acknowledgement deadline of design 5.7, and for the same reason: a peer which
                 * cannot absorb a few hundred bytes of control frame in that time is gone,
                 * whatever its socket still says
                 */

                DEFAULT_DRAIN_TIMEOUT_IN_SECONDS           = 10L,
            };

            enum : std::size_t
            {
                /**
                 * One pooled block is held for the life of the connection and read into
                 * repeatedly; it is not the frame size and does not have to be a multiple of it,
                 * because FrameReader owns re-assembly across reads
                 */

                DEFAULT_READ_BUFFER_SIZE                    = 32U * 1024U,
            };

            http2::Http2Profile                                                 profile;
            http2::SessionLimits                                                limits;

            time::time_duration                                                 keepAliveInterval;
            time::time_duration                                                 keepAlivePingReplyTimeout;
            time::time_duration                                                 idleTimeout;
            time::time_duration                                                 drainTimeout;

            cpp::ScalarTypeIniter< std::size_t >                                readBufferSize;

            /**
             * Optional; a null pool means every block is freshly allocated, which is what the
             * unit tests and a caller who has not set one up get
             */

            om::ObjPtrCopyable< data::datablocks_pool_type >                    dataBlocksPool;

            Http2ConnectionConfig()
                :
                keepAliveInterval( time::neg_infin ),
                keepAlivePingReplyTimeout(
                    time::seconds( DEFAULT_PING_REPLY_TIMEOUT_IN_SECONDS )
                    ),
                idleTimeout( time::neg_infin ),
                drainTimeout( time::seconds( DEFAULT_DRAIN_TIMEOUT_IN_SECONDS ) )
            {
                readBufferSize = DEFAULT_READ_BUFFER_SIZE;
            }
        };

        /******************************************************************************************
         * ================================= Http2ConnectionTaskT =================================
         */

        /**
         * @brief The HTTP/2 I/O shell of design 5.1 - a connection task which IS the connection
         *
         * It derives from S4.1's establishment base and takes over at onProtocolNegotiated( ): the
         * base resolves, connects, tunnels, handshakes, checks the floor and reads ALPN, and this
         * class keeps the stream and starts the three loops. For any protocol other than h2 it
         * falls back to the base, which hands the connected stream to the driver factory - that is
         * design 5.5's fallback and the only thing the factory is for on this path.
         *
         * IT DOES NOT GO THROUGH THE FACTORY FOR ITSELF, and it cannot: a task which attachStream( )s
         * a stream created elsewhere loses that stream policy's strand (TcpStrandedStreams.h says
         * so), and the strand is the whole of design 3.1. So the h2 driver has to BE the object
         * which created the socket.
         *
         * ------------------------------------------------------------------------------------
         * THE OPENING WRITE IS ONE WRITE, AND THAT IS A FINGERPRINT STATEMENT
         * ------------------------------------------------------------------------------------
         *
         * The client preface, our SETTINGS, the connection WINDOW_UPDATE, the profile's PRIORITY
         * frames and the first request's HEADERS leave in a single async_write. Session's
         * constructor queues the first four (queueOpeningFrames), the pending-command drain adds
         * the fifth, and exactly one produce( ) then gathers all of them - so the coalescing is a
         * property of the ORDER things are done in here, not of a buffer someone remembered to
         * fill. It is one write because that is what a browser looks like on the wire; efficiency
         * is a side effect. Nothing on the wire can tell one write from two, so onWriteScheduled( )
         * exists to let a test see it.
         *
         * ------------------------------------------------------------------------------------
         * THE CONCURRENCY MODEL - DESIGN 5.2 RULES L1 TO L4
         * ------------------------------------------------------------------------------------
         *
         * L1. The session, the stream table, the write buffer and every timer belong to the
         *     strand. Only strand handlers touch them, and every one which touches SESSION state
         *     also holds the task lock (BL_TASKS_HANDLER_BEGIN) - Session's single-threaded
         *     contract. The handler cancelTask( ) posts is the one exception: no lock, timers only
         *
         * L2/L3. Every ClientConnection call - submit, cancel, consumed, provideBody - arrives
         *     from another thread, appends to THIS connection's command mailbox under a leaf lock
         *     and posts a drain. It is the mirror image of the mailbox a request task keeps, and
         *     it exists for the same reason: posting two handlers to a multi-threaded io_service
         *     does not order them, so a cancel posted after a submit could otherwise overtake it.
         *     ONE queue drained in order is the only thing which makes "cancel what I just
         *     submitted" mean anything.
         *
         * L4. m_commandsLock is a leaf. Nothing is called while it is held - not the session, not
         *     a sink, not even beginOperation( ), with the single deliberate exception documented
         *     at postCommand( ), where taking the accounting lock under it is what closes the
         *     window between "the connection accepted my command" and "the connection took its
         *     terminal path".
         *
         * Events travel the other way by calling the request's ClientStreamEventSink from the
         * strand. That is the append to the REQUEST's mailbox; the request task's own drain is
         * what runs its handlers (design 5.3), so no request handler ever runs on an I/O thread.
         *
         * ------------------------------------------------------------------------------------
         * WHAT negotiated( ) RESTS ON, AND WHY IT IS NOT A CONST MEMBER HERE
         * ------------------------------------------------------------------------------------
         *
         * A driver built by the factory receives the value at construction and stores it const,
         * which is what makes the reference-returning accessor safe under off-strand reads. This
         * driver cannot: it IS the establishing task, so the value is settled by the handshake and
         * not by a constructor argument. What replaces the const is a publication order - the
         * strand writes m_negotiated and only afterwards stores Ready into the atomic state - and
         * a rule: READ state( ) FIRST. A reader which observes anything other than Connecting has
         * a happens-before with the write, and one which observes Connecting must not look.
         * freeStreamSlots( ) is an atomic for the same reason: it answers a question about the
         * session, which is strand state a caller may not touch.
         */

        template
        <
            typename STREAM
        >
        class Http2ConnectionTaskT :
            public ClientConnectionTaskBaseT< STREAM >,
            public httpclient::ClientConnection
        {
        public:

            typedef Http2ConnectionTaskT< STREAM >                              this_type;
            typedef ClientConnectionTaskBaseT< STREAM >                         base_type;

            typedef httpclient::stream_handle_t                                 stream_handle_t;
            typedef httpclient::ConnectionState                                 ConnectionState;

        private:

            BL_DECLARE_OBJECT_IMPL( Http2ConnectionTaskT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( httpclient::ClientConnection )
                BL_QITBL_ENTRY_CHAIN_BASE( base_type )
            BL_QITBL_END( Task )

        public:

            /**
             * @brief The reference this task's own handlers hold on it
             *
             * om::Object is a base of this type TWICE - once through the task chain and once
             * through ClientConnection - so a plain om::ObjPtrCopyable< this_type > cannot be
             * formed at all: its acquireRef( ) converts to om::Object to call addRef( ) and that
             * conversion is ambiguous. Naming the interface half of the pointer resolves it to the
             * task subobject, which is the one the object model's implementation lives in, and the
             * pointer still points at this_type so a handler can be a member of it
             *
             * It is the same answer TcpServerBase reaches, which has the same double base and
             * spells om::ObjPtrCopyable< Task > at its one call site
             */

            typedef om::ObjPtrCopyable< this_type, Task >                       self_ref_t;

            enum : std::size_t
            {
                /**
                 * What freeStreamSlots( ) answers for a peer which HAS spoken and set no limit of
                 * its own - SETTINGS_MAX_CONCURRENT_STREAMS has no initial value and is unlimited
                 * until a peer sends one ( RFC 9113 6.5.2 ), so this is OUR ceiling for such a
                 * peer rather than the peer's, and design 5.4's assumed 100 is where it comes from
                 */

                ASSUMED_MAX_CONCURRENT_STREAMS      = 100U,

                /**
                 * What freeStreamSlots( ) answers UNTIL the peer's SETTINGS have arrived - design
                 * 5.1's own rule, "exactly one rides the preface, because until the peer's
                 * SETTINGS arrive nothing else is known", published by the layer which knows
                 * whether the peer has spoken ( L5 finding 5(c) ).
                 *
                 * AND IT IS WHAT THE POOL INFERS FROM, which is why it may not be exceeded.
                 * ConnectionPoolPolicy::UNCONFIRMED_MAX_CONCURRENT_STREAMS is the same number and
                 * has to be: the pool cannot ask whether the peer has spoken, so the only thing
                 * which tells it that a reading is the PEER's is that this driver could not have
                 * published that reading before the peer spoke. Anything above this number,
                 * published before the peer's SETTINGS arrive, is believed
                 */

                UNCONFIRMED_MAX_CONCURRENT_STREAMS  = 1U,
            };

        protected:

            /**
             * @brief What one ClientConnection call became, waiting for the strand
             *
             * One flat type rather than a hierarchy, for the reason SessionEvent gives: the drain
             * is a switch on the kind, and a hierarchy would put an allocation in the path of
             * every body chunk
             */

            enum class CommandKind : std::uint8_t
            {
                Submit,
                Cancel,
                Consumed,
                ProvideBody,
            };

            struct Command
            {
                cpp::ScalarTypeIniter< CommandKind >                            kind;
                cpp::ScalarTypeIniter< stream_handle_t >                        handle;
                cpp::ScalarTypeIniter< std::size_t >                            bytes;
                cpp::ScalarTypeIniter< bool >                                   endStream;
                cpp::ScalarTypeIniter< bool >                                   hasBodySource;

                http2::SessionRequest                                           request;
                eh::error_code                                                  errorCode;

                om::ObjPtrCopyable< data::DataBlock >                           data;
                om::ObjPtrCopyable< httpclient::ClientStreamEventSink >         sink;
            };

            /**
             * @brief What the driver keeps about one submitted request
             *
             * The session keeps the protocol state of the stream; this is the part which is the
             * DRIVER's - who to tell about it, and the upload which has been handed over but not
             * yet placed
             */

            struct StreamState
            {
                om::ObjPtrCopyable< httpclient::ClientStreamEventSink >         sink;

                cpp::ScalarTypeIniter< std::uint32_t >                          streamId;
                cpp::ScalarTypeIniter< bool >                                   isEndQueued;
                cpp::ScalarTypeIniter< bool >                                   uploadEnded;

                /*
                 * Whether the stream's opening header block has been handed to a write yet - see
                 * applyCancel( ) for the ordering this exists to keep
                 */

                cpp::ScalarTypeIniter< bool >                                   isHeadersProduced;
                cpp::ScalarTypeIniter< bool >                                   isCancelPending;

                /*
                 * Whether an onBodyWanted( ) is out and unanswered - see raiseBodyWanted( ). It is
                 * what keeps the driver to at most one un-placed chunk per stream
                 */

                cpp::ScalarTypeIniter< bool >                                   isBodyWantedOutstanding;

                std::string                                                     pendingUpload;
            };

            typedef std::map< stream_handle_t, StreamState >                    streams_t;
            typedef std::map< std::uint32_t, stream_handle_t >                  handles_t;

            const Http2ConnectionConfig                                         m_h2config;

            /*
             * Strand state - design 5.2 rule L1. Nothing below this line is touched off the strand
             */

            cpp::SafeUniquePtr< http2::Session >                                m_session;

            om::ObjPtr< data::DataBlock >                                       m_readBlock;
            http2::Session::wire_buffer_t                                       m_writeBuffer;

            streams_t                                                           m_streams;
            handles_t                                                           m_handles;

            cpp::SafeUniquePtr< asio::deadline_timer >                          m_settingsTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_keepAliveTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_pingDeadlineTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_idleTimer;
            cpp::SafeUniquePtr< asio::deadline_timer >                          m_drainTimer;

            cpp::ScalarTypeIniter< bool >                                       m_isWriteInFlight;
            cpp::ScalarTypeIniter< bool >                                       m_isPrefaceWritePending;
            cpp::ScalarTypeIniter< bool >                                       m_isCloseWhenDrained;
            cpp::ScalarTypeIniter< bool >                                       m_isSettingsTimerArmed;
            cpp::ScalarTypeIniter< bool >                                       m_isDrainingEvents;

            /**
             * Whether the peer's SETTINGS have arrived at this driver - strand state, read by
             * publishFreeStreamSlots( ) and set by the event which carries them. It is not the
             * same question as Session::peerLimitsConcurrentStreams( ), which a peer that sends
             * no SETTINGS_MAX_CONCURRENT_STREAMS leaves false for ever
             */

            cpp::ScalarTypeIniter< bool >                                       m_isPeerSettingsSeen;

            cpp::ScalarTypeIniter< std::uint64_t >                              m_pingCounter;
            cpp::ScalarTypeIniter< std::uint32_t >                              m_connectionErrorCode;

            std::string                                                         m_connectionErrorReason;

            /*
             * The command mailbox and its leaf lock - design 5.2 rules L3 and L4
             */

            mutable os::mutex                                                   m_commandsLock;
            std::deque< Command >                                               m_commands;
            stream_handle_t                                                     m_nextHandle;
            bool                                                                m_isDrainScheduled;
            bool                                                                m_isStrandReady;
            bool                                                                m_isSubmitClosed;

            /*
             * What an off-strand caller is allowed to read - see the class comment
             */

            std::atomic< ConnectionState >                                      m_connectionState;
            std::atomic< std::size_t >                                          m_freeStreamSlots;

            Http2ConnectionTaskT(
                SAA_in              httpclient::ConnectionKey                   key,
                SAA_in              typename base_type::factory_ptr_t           driverFactory,
                SAA_in_opt          Http2ConnectionConfig                       h2config = Http2ConnectionConfig(),
                SAA_in_opt          ProxyConfig                                 proxyConfig = ProxyConfig::none(),
                SAA_in_opt          ClientConnectionConfig                      config = ClientConnectionConfig(),
                SAA_in_opt          const bool                                  logExceptions = true
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    BL_PARAM_FWD( proxyConfig ),
                    BL_PARAM_FWD( config ),
                    logExceptions
                    ),
                m_h2config( BL_PARAM_FWD( h2config ) ),
                m_nextHandle( httpclient::ClientConnection::INVALID_STREAM_HANDLE ),
                m_isDrainScheduled( false ),
                m_isStrandReady( false ),
                m_isSubmitClosed( false ),
                m_connectionState( ConnectionState::Connecting ),
                m_freeStreamSlots( 0U )
            {
                /*
                 * The inherited shutdown of design 3.2 - for a TLS policy this is what performs
                 * the SSL shutdown as the task's finish continuation, once no operation is left
                 * pending. The graceful close of design 5.1 is GOAWAY( NO_ERROR ), then this
                 */

                base_type::isCloseStreamOnTaskFinish( true );
            }

            static auto now() NOEXCEPT -> time::ptime
            {
                return time::microsec_clock::universal_time();
            }

            static bool isEnabled( SAA_in const time::time_duration& duration ) NOEXCEPT
            {
                return ! duration.is_special() && duration.total_milliseconds() > 0;
            }

            /*************************************************************************************
             * The command mailbox - design 5.2 rules L3 and L4
             */

            /**
             * @brief Appends one command and, if the strand is not already going to look, wakes it
             *
             * @return false when the connection will never look again, in which case the caller
             * has to answer its own request - nothing was queued
             *
             * THE ACCOUNTING LOCK IS TAKEN UNDER THE MAILBOX LOCK, and this is the one place that
             * happens. beginOperation( ) has to be inside the same critical section which decides
             * the connection is still open, or a command could be accepted by a connection whose
             * terminal path had already been taken between the two - and that command's stream
             * would never be answered at all. The order is mailbox then accounting and there is no
             * path which takes them the other way round: MultiOperationTaskT releases the
             * accounting lock before it calls anything (applyDecision), and nothing under the
             * accounting lock knows this mailbox exists
             */

            bool postCommand( SAA_inout Command&& command )
            {
                bool needsPost = false;

                {
                    BL_MUTEX_GUARD( m_commandsLock );

                    if( m_isSubmitClosed )
                    {
                        return false;
                    }

                    m_commands.push_back( BL_PARAM_FWD( command ) );

                    if( m_isStrandReady && ! m_isDrainScheduled )
                    {
                        m_isDrainScheduled = true;
                        needsPost = true;

                        base_type::beginOperation();
                    }
                }

                if( needsPost )
                {
                    base_type::postToStrand(
                        cpp::bind(
                            &this_type::onCommandsPosted,
                            self_ref_t::acquireRef( this )
                            )
                        );
                }

                return true;
            }

            /**
             * @brief Takes everything the mailbox holds; the strand owns it from here
             */

            void takeCommands( SAA_inout std::deque< Command >& commands )
            {
                BL_MUTEX_GUARD( m_commandsLock );

                m_isDrainScheduled = false;

                commands.swap( m_commands );
            }

            void onCommandsPosted() NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                applyCommands();

                pumpWrites();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            void applyCommands()
            {
                std::deque< Command > commands;

                takeCommands( commands );

                while( ! commands.empty() )
                {
                    applyCommand( commands.front() );

                    commands.pop_front();
                }
            }

            void applyCommand( SAA_inout Command& command )
            {
                switch( command.kind.value() )
                {
                    case CommandKind::Submit:

                        applySubmit( command );
                        break;

                    case CommandKind::Cancel:

                        applyCancel( command );
                        break;

                    case CommandKind::Consumed:

                        applyConsumed( command );
                        break;

                    case CommandKind::ProvideBody:

                        applyProvideBody( command );
                        break;

                    default:

                        BL_RIP_MSG( "An unknown HTTP/2 connection command was queued" );
                        break;
                }
            }

            /*************************************************************************************
             * The commands, on the strand
             */

            /**
             * @brief Whether the session can still be asked to open another stream
             */

            bool canOpenStream() const NOEXCEPT
            {
                return
                    m_session &&
                    ! m_isCloseWhenDrained &&
                    ! m_session -> isClosed() &&
                    ! m_session -> isDraining() &&
                    ! base_type::isClosing();
            }

            void applySubmit( SAA_inout Command& command )
            {
                if( ! canOpenStream() )
                {
                    /*
                     * Nothing of this request was written, which is exactly the third limb of the
                     * "provably unprocessed" rule of design 5.4 - so it is retryable on another
                     * connection whatever else is true
                     */

                    failSubmission(
                        command,
                        eh::errc::make_error_code( eh::errc::connection_aborted ),
                        true /* isRetryable */
                        );

                    chkPublishDraining();

                    return;
                }

                std::uint32_t streamId = 0U;

                try
                {
                    streamId = m_session -> submitRequest( command.request );
                }
                catch( std::exception& )
                {
                    /*
                     * A request the session refused - the connection started draining between the
                     * mailbox check and here, or the request was malformed. It is answered rather
                     * than thrown, because one bad request must not take the connection down
                     */

                    failSubmission(
                        command,
                        eh::errc::make_error_code( eh::errc::invalid_argument ),
                        true /* isRetryable */
                        );

                    return;
                }

                auto& state = m_streams[ command.handle ];

                state.sink = command.sink;
                state.streamId = streamId;
                state.uploadEnded = ! command.hasBodySource;

                /*
                 * A request with no body at all had END_STREAM on its HEADERS, so the stream is
                 * already ended locally and the engine refuses - loudly, and rightly - any body
                 * offered for it afterwards. Only a request which declared one is ever pumped
                 */

                state.isEndQueued = ! command.request.hasBody;

                if( command.data )
                {
                    state.pendingUpload.assign(
                        command.data -> begin() + command.data -> offset1(),
                        command.data -> begin() + command.data -> size()
                        );
                }

                m_handles[ streamId ] = command.handle;

                cancelIdleTimer();

                pumpBody( command.handle );

                chkPublishDraining();

                publishFreeStreamSlots();
            }

            /**
             * @brief Resets one stream - design 5.7: cancelling a request never closes the
             * connection
             *
             * A CANCEL WHICH ARRIVES BEFORE THE REQUEST HAS BEEN WRITTEN IS HELD BACK, and that is
             * not an optimization. Session::produce( ) writes the control queue before the header
             * block queue, so a RST_STREAM queued while the stream's own HEADERS are still waiting
             * would OVERTAKE them - the peer would see a RST_STREAM on a stream it has never heard
             * of, which RFC 9113 5.1 makes a connection error of type PROTOCOL_ERROR. "Submit,
             * then immediately cancel" is the ordinary shape of a request whose deadline expired
             * while it was queued, so this is reachable rather than theoretical
             *
             * The reset is queued instead by onHeaderBlocksProduced( ), which runs when the block
             * really has gone into a write buffer
             */

            void applyCancel( SAA_inout Command& command )
            {
                const auto it = m_streams.find( command.handle );

                if( it == m_streams.end() || ! m_session )
                {
                    return;
                }

                if( ! it -> second.isHeadersProduced )
                {
                    it -> second.isCancelPending = true;

                    return;
                }

                /*
                 * THE WIRE CODE IS ALWAYS CANCEL and never the caller's, which design 5.7 states
                 * outright: a request timeout or a requestCancel( ) "posts a RST_STREAM( CANCEL )".
                 * The error code the caller passed is the reason IT will complete its request
                 * with, and it is the only layer which knows that reason - a TimeoutException
                 * reads differently from a user cancel, and neither is an HTTP/2 error code. So it
                 * is deliberately not forwarded to the peer and not substituted for the closure
                 * the session is about to emit
                 *
                 * That closure is what answers the sink, so nothing is delivered from here
                 */

                m_session -> resetStream(
                    it -> second.streamId,
                    http2::Globals::ERROR_CODE_CANCEL
                    );

                drainSessionEvents();
            }

            /**
             * @brief Called once produce( ) has drained the header block queue into a write
             *
             * produce( ) takes the WHOLE queue, so every stream which had a block waiting has it
             * in the buffer which is about to go on the wire. That is the point at which a held
             * back cancel becomes sendable, and the point at which a later one may go straight out
             */

            void onHeaderBlocksProduced()
            {
                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    it -> second.isHeadersProduced = true;
                }

                std::vector< std::uint32_t > toReset;

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    if( it -> second.isCancelPending )
                    {
                        it -> second.isCancelPending = false;

                        toReset.push_back( it -> second.streamId );
                    }
                }

                for( std::size_t i = 0U; i < toReset.size(); ++i )
                {
                    m_session -> resetStream( toReset[ i ], http2::Globals::ERROR_CODE_CANCEL );
                }

                /*
                 * The events that produced are drained by the handler which called the pump - it
                 * drains before it pumps again, and this cannot be the last thing that happens:
                 * the write it belongs to still has to complete
                 */
            }

            void applyConsumed( SAA_inout Command& command )
            {
                const auto it = m_streams.find( command.handle );

                if( it == m_streams.end() || ! m_session )
                {
                    return;
                }

                m_session -> consumed( it -> second.streamId, command.bytes );
            }

            void applyProvideBody( SAA_inout Command& command )
            {
                const auto it = m_streams.find( command.handle );

                if( it == m_streams.end() )
                {
                    return;
                }

                auto& state = it -> second;

                /*
                 * This answers whatever pull was outstanding, whether or not one was: a caller
                 * which pushes body unasked is still served, it simply does not get the bound the
                 * pull gives
                 */

                state.isBodyWantedOutstanding = false;

                const bool isEmptyAnswer =
                    ! command.data || command.data -> size() == command.data -> offset1();

                if( command.data )
                {
                    state.pendingUpload.append(
                        command.data -> begin() + command.data -> offset1(),
                        command.data -> begin() + command.data -> size()
                        );
                }

                if( command.endStream )
                {
                    state.uploadEnded = true;
                }

                if( isEmptyAnswer && ! command.endStream && state.pendingUpload.empty() )
                {
                    /*
                     * THE SOURCE HAD NOTHING RIGHT NOW, which ClientStreamEventSink documents as a
                     * legal answer, AND there is nothing waiting to be placed. Pumping here would
                     * place nothing and then immediately raise the pull again, and the two layers
                     * would ping-pong across two thread pools for as long as the source stayed
                     * empty. The re-raise is left to the next pumpAllBodies( ) instead - a read or
                     * a write completion, which is to say the next moment at which either the room
                     * or the source can really have changed
                     *
                     * The third condition is what keeps this from swallowing progress: an empty
                     * answer arriving on top of bytes a caller pushed unasked still pumps those
                     * bytes, and only a stream with genuinely nothing to place waits
                     */

                    return;
                }

                pumpBody( command.handle );
            }

            /**
             * @brief Places as much of a stream's upload into the session as it asks for, then
             * asks the layer above for more when it has room and nothing left to place
             *
             * THE PULL OF DESIGN 4.5 IS OBEYED HERE AND NOWHERE ELSE: bodyBytesWanted( ) is the
             * contract, and handing over more than it asks for is a BL_CHK in the engine. S5.1
             * carried that pull one hop further up, to ClientStreamEventSink::onBodyWanted( ) -
             * before it this loop could not ask the layer above for anything, so what a caller
             * handed over was held here until the windows took it, and a streaming upload was
             * buffered whole inside the driver
             *
             * The raise is AFTER the loop and not inside it, because what makes the event worth
             * having is that it is raised only when this stream has actually run dry: mid-loop
             * there is still pending upload to place and asking for more would refill the very
             * buffer the event exists to keep empty
             */

            void pumpBody( SAA_in const stream_handle_t handle )
            {
                const auto it = m_streams.find( handle );

                if( it == m_streams.end() || ! m_session )
                {
                    return;
                }

                auto& state = it -> second;

                while( ! state.isEndQueued )
                {
                    const auto wanted = m_session -> bodyBytesWanted( state.streamId );

                    const auto take = std::min< std::size_t >( wanted, state.pendingUpload.size() );

                    const bool isLast =
                        state.uploadEnded && take == state.pendingUpload.size();

                    if( 0U == take && ! isLast )
                    {
                        break;
                    }

                    m_session -> provideBody(
                        state.streamId,
                        0U == take ?
                            nullptr :
                            reinterpret_cast< const std::uint8_t* >( state.pendingUpload.data() ),
                        take,
                        isLast
                        );

                    state.pendingUpload.erase( 0U, take );

                    if( isLast )
                    {
                        state.isEndQueued = true;
                        break;
                    }

                    if( 0U == take )
                    {
                        break;
                    }
                }

                raiseBodyWanted( handle );
            }

            /**
             * @brief Asks this stream's request for more body, when there is room for it
             *
             * Four conditions, and each one is load-bearing. The stream must still have an upload
             * coming ( uploadEnded is set at submit( ) for a request with no BodySource, so a
             * buffered body is never pulled ); nothing may be waiting to be placed, or the pull
             * would refill a buffer which is not yet empty; there must be room in the windows to
             * send into, since an event raised with nothing wanted only moves the wait; and no
             * pull may already be outstanding, which is what bounds the driver to AT MOST ONE
             * un-placed chunk per stream and is the whole reason this is a request/answer and not
             * a notification
             *
             * The sink call goes out on the strand, like every other delivery in this file: it
             * appends to the request's mailbox and returns ( design 5.2 rule L3 ), so it re-enters
             * nothing and takes no lock of the layer above
             */

            void raiseBodyWanted( SAA_in const stream_handle_t handle )
            {
                const auto it = m_streams.find( handle );

                if( it == m_streams.end() || ! m_session )
                {
                    return;
                }

                auto& state = it -> second;

                if(
                    state.isEndQueued ||
                    state.uploadEnded ||
                    state.isBodyWantedOutstanding ||
                    ! state.pendingUpload.empty()
                    )
                {
                    return;
                }

                const auto wanted = m_session -> bodyBytesWanted( state.streamId );

                if( 0U == wanted || ! state.sink )
                {
                    return;
                }

                /*
                 * The sink is copied out and the flag is set BEFORE the call, so that neither
                 * depends on 'state' surviving it - every other delivery in this file takes the
                 * sink by value for the same reason
                 */

                const auto sink = state.sink;

                state.isBodyWantedOutstanding = true;

                sink -> onBodyWanted( handle, wanted );
            }

            void pumpAllBodies()
            {
                /*
                 * A completed write is where the windows can have moved, so every stream with
                 * something left is offered to the session again. The handles are copied first
                 * because pumpBody( ) is free to change the table
                 */

                std::vector< stream_handle_t > handles;

                handles.reserve( m_streams.size() );

                for( auto it = m_streams.begin(); it != m_streams.end(); ++it )
                {
                    if( ! it -> second.isEndQueued )
                    {
                        handles.push_back( it -> first );
                    }
                }

                for( std::size_t i = 0U; i < handles.size(); ++i )
                {
                    pumpBody( handles[ i ] );
                }
            }

            /*************************************************************************************
             * Delivering to a request - design 5.2 rule L3
             */

            /**
             * @brief The regular fields of a decoded block, as http::HeaderList holds them
             *
             * The pseudo-headers are dropped and not renamed: http::HeaderList refuses a colon in
             * a field name by design (3.5), the status travels as its own parameter for exactly
             * that reason (S2.6), and a response carries no other pseudo-header the session would
             * have let through
             */

            static auto headerListOf( SAA_in const http2::HpackFieldList& fields ) -> http::HeaderList
            {
                http::HeaderList headers;

                for( std::size_t i = 0U; i < fields.size(); ++i )
                {
                    const auto& name = fields[ i ].name();

                    if( ! name.empty() && ':' == name[ 0 ] )
                    {
                        continue;
                    }

                    headers.append(
                        cpp::copy( name ),
                        cpp::copy( fields[ i ].value() )
                        );
                }

                return headers;
            }

            /**
             * @brief One DATA payload in a pooled block - the copy design 5.1 budgets for
             *
             * The engine's own SessionEvent carries the payload as a std::string and frontEvent( )
             * hands it out by const reference, so it cannot be moved from; that copy is S3.1's
             * shape and moving it would be a change to landed core. What this driver owns is
             * exactly one copy, and it is this one
             */

            auto blockOf( SAA_in const std::string& payload ) -> om::ObjPtr< data::DataBlock >
            {
                auto block = data::DataBlock::get(
                    m_h2config.dataBlocksPool,
                    std::max< std::size_t >( payload.size(), data::DataBlock::defaultCapacity() )
                    );

                if( block -> capacity() < payload.size() )
                {
                    /*
                     * A block which came out of the pool is whatever size it was made, and a peer
                     * may advertise a frame size larger than that
                     */

                    block = data::DataBlock::createInstance( payload.size() );
                }

                if( ! payload.empty() )
                {
                    block -> write( payload.c_str(), payload.size() );
                }

                return block;
            }

            /**
             * @brief What one closed stream means to the layer above
             *
             * A default constructed code means it ended normally, which is what the sink's
             * contract says. Everything else is a diagnosis and not a decision - the decision the
             * pool makes is carried by the retryable flag beside it
             */

            static auto errorCodeOf( SAA_in const http2::SessionEvent& event ) -> eh::error_code
            {
                if( http2::Globals::ERROR_CODE_NO_ERROR == event.errorCode )
                {
                    return event.isMessageComplete ?
                        eh::error_code() :
                        eh::errc::make_error_code( eh::errc::connection_aborted );
                }

                if( http2::Globals::ERROR_CODE_CANCEL == event.errorCode )
                {
                    return asio::error::operation_aborted;
                }

                if( http2::Globals::ERROR_CODE_REFUSED_STREAM == event.errorCode )
                {
                    return eh::errc::make_error_code( eh::errc::connection_refused );
                }

                return eh::errc::make_error_code( eh::errc::protocol_error );
            }

            /**
             * @brief Answers a request which never reached a stream at all
             */

            void failSubmission(
                SAA_inout           Command&                                    command,
                SAA_in              const eh::error_code&                       errorCode,
                SAA_in              const bool                                  isRetryable
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( command.sink )
                {
                    command.sink -> onClosed( command.handle, errorCode, isRetryable );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Closes one stream out, tells its sink and forgets it
             */

            void closeStream(
                SAA_in              const stream_handle_t                       handle,
                SAA_in              const eh::error_code&                       errorCode,
                SAA_in              const bool                                  isRetryable
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                const auto it = m_streams.find( handle );

                if( it == m_streams.end() )
                {
                    return;
                }

                const auto sink = it -> second.sink;

                m_handles.erase( it -> second.streamId );
                m_streams.erase( it );

                /*
                 * BEFORE THE SINK IS TOLD, AND NOT AFTER, because onClosed( ) is what gives the
                 * pool its slot back ( HttpClientRequestTaskImpl ) and the pool re-reads
                 * freeStreamSlots( ) in the same breath. Publishing afterwards leaves a window in
                 * which this driver reports a stream it has already closed out as open, and a
                 * release landing in that window is read by the pool at slotsInUse == 0 - the one
                 * moment it takes a reading as the peer's limit outright. The pool then stores a
                 * number one below the peer's and only another idle moment can raise it again.
                 *
                 * It also makes true what the pool's own comment says of this number, that it is
                 * stale HIGH while dispatches are in flight and never stale low
                 */

                publishFreeStreamSlots();

                if( sink )
                {
                    sink -> onClosed( handle, errorCode, isRetryable );
                }

                BL_NOEXCEPT_END()
            }

            void closeAllStreams(
                SAA_in              const eh::error_code&                       errorCode,
                SAA_in              const bool                                  isRetryable
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                while( ! m_streams.empty() )
                {
                    closeStream( m_streams.begin() -> first, errorCode, isRetryable );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Closes every live stream, deciding retryability per stream
             *
             * Design 5.4's third limb is "the connection failed before any byte of the request was
             * written", and isHeadersProduced is exactly that fact: a stream whose opening header
             * block is still in the header block queue - which is what a submit made while a write
             * was in flight leaves behind - has had nothing of it put on the wire, so it is
             * PROVABLY unprocessed and the layer above may replay it. closeSubmissions( ) already
             * says the same thing one step earlier, for a command which never reached a stream
             *
             * The flag is the conservative side of the line and not a guess: a block which WAS
             * handed to a write, and that write then failed part way, is not provably unwritten,
             * and such a stream stays non-retryable
             */

            void closeAllStreamsUnwrittenRetryable(
                SAA_in              const eh::error_code&                       errorCode
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                while( ! m_streams.empty() )
                {
                    const auto it = m_streams.begin();

                    const auto handle = it -> first;
                    const bool isRetryable = ! it -> second.isHeadersProduced;

                    closeStream( handle, errorCode, isRetryable );
                }

                BL_NOEXCEPT_END()
            }

            /*************************************************************************************
             * The session event queue, drained after every feed( ) and after every write
             */

            /**
             * @brief Delivers everything the engine has queued, and is RE-ENTRANT BY DESIGN
             *
             * Dispatching an event can drive the connection far enough to produce more of them -
             * the last stream closing is what starts the graceful close, and the GOAWAY that queues
             * reaps whatever is left. Without the guard the inner drain would consume the very
             * event the outer loop is still holding a reference to, and the outer popEvent( ) would
             * then be asked for an event which no longer exists; the engine says so out loud, which
             * is how this was found rather than lived with
             *
             * The reference dispatchEvent( ) holds stays valid because only this loop ever pops and
             * because appending to a std::deque does not invalidate references to its elements
             */

            void drainSessionEvents()
            {
                if( m_isDrainingEvents )
                {
                    return;
                }

                m_isDrainingEvents = true;

                try
                {
                    while( m_session -> hasEvents() )
                    {
                        dispatchEvent( m_session -> frontEvent() );

                        m_session -> popEvent();
                    }
                }
                catch( ... )
                {
                    m_isDrainingEvents = false;

                    throw;
                }

                m_isDrainingEvents = false;
            }

            void dispatchEvent( SAA_in const http2::SessionEvent& event )
            {
                switch( event.type.value() )
                {
                    case http2::SessionEventType::Headers:

                        onHeadersEvent( event );
                        break;

                    case http2::SessionEventType::Data:

                        onDataEvent( event );
                        break;

                    case http2::SessionEventType::StreamClosed:

                        onStreamClosedEvent( event );
                        break;

                    case http2::SessionEventType::SettingsReceived:

                        /*
                         * The peer has spoken, whether or not its SETTINGS named a concurrency
                         * limit - which is the fact publishFreeStreamSlots( ) needs and which the
                         * session's peerLimitsConcurrentStreams( ) does not answer
                         */

                        m_isPeerSettingsSeen = true;

                        publishFreeStreamSlots();
                        break;

                    case http2::SessionEventType::SettingsAcknowledged:

                        chkDisarmSettingsTimer();
                        break;

                    case http2::SessionEventType::PingAcknowledged:

                        onPingAcknowledged();
                        break;

                    case http2::SessionEventType::GoAwayReceived:

                        onGoAwayReceived();
                        break;

                    case http2::SessionEventType::ConnectionError:

                        onConnectionErrorEvent( event );
                        break;

                    default:

                        break;
                }
            }

            auto sinkOf( SAA_in const std::uint32_t streamId ) const
                -> om::ObjPtrCopyable< httpclient::ClientStreamEventSink >
            {
                const auto handleIt = m_handles.find( streamId );

                if( handleIt == m_handles.end() )
                {
                    return nullptr;
                }

                const auto it = m_streams.find( handleIt -> second );

                return it == m_streams.end() ? nullptr : it -> second.sink;
            }

            auto handleOf( SAA_in const std::uint32_t streamId ) const NOEXCEPT -> stream_handle_t
            {
                const auto handleIt = m_handles.find( streamId );

                return handleIt == m_handles.end() ?
                    httpclient::ClientConnection::INVALID_STREAM_HANDLE : handleIt -> second;
            }

            void onHeadersEvent( SAA_in const http2::SessionEvent& event )
            {
                const auto sink = sinkOf( event.streamId );

                if( ! sink )
                {
                    return;
                }

                const auto handle = handleOf( event.streamId );

                if( event.isTrailers )
                {
                    sink -> onTrailers( handle, headerListOf( event.fields ) );

                    return;
                }

                sink -> onHeaders(
                    handle,
                    event.status,
                    headerListOf( event.fields ),
                    event.isInformational
                    );
            }

            void onDataEvent( SAA_in const http2::SessionEvent& event )
            {
                const auto sink = sinkOf( event.streamId );

                if( ! sink )
                {
                    return;
                }

                if( event.data.empty() )
                {
                    /*
                     * An empty DATA frame carries only END_STREAM, and the closure which follows
                     * is what says so - there is nothing to deliver
                     */

                    return;
                }

                sink -> onData( handleOf( event.streamId ), blockOf( event.data ) );
            }

            void onStreamClosedEvent( SAA_in const http2::SessionEvent& event )
            {
                const auto handle = handleOf( event.streamId );

                if( httpclient::ClientConnection::INVALID_STREAM_HANDLE == handle )
                {
                    return;
                }

                closeStream( handle, errorCodeOf( event ), event.isRetryable );

                chkArmIdleTimer();

                if( m_streams.empty() && ConnectionState::Draining == m_connectionState.load() )
                {
                    /*
                     * The GOAWAY drain of design 5.1: in-flight streams at or below the peer's
                     * last-stream-id were allowed to finish, and the last of them just has
                     */

                    closeGracefully();
                }
            }

            void onGoAwayReceived()
            {
                publishState( ConnectionState::Draining );

                publishFreeStreamSlots();

                if( m_streams.empty() )
                {
                    closeGracefully();
                }
            }

            void onConnectionErrorEvent( SAA_in const http2::SessionEvent& event )
            {
                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "An HTTP/2 connection to '"
                        << base_type::key().host
                        << "' failed: "
                        << event.reason
                    );

                /*
                 * The session has already queued its GOAWAY and closed every stream it knew about,
                 * so what is left is to let that GOAWAY out and then shut down. The streams were
                 * answered by the StreamClosed events which preceded this one
                 *
                 * The CONNECTION, however, did not end because it was done, and the pool must not
                 * be told it did - so the reason is remembered and thrown from chkFinishClose( ),
                 * AFTER the GOAWAY which RFC 9113 5.4.1 asks for has gone out. Throwing here would
                 * take the task down with the GOAWAY still in the queue
                 */

                publishState( ConnectionState::Draining );

                m_isCloseWhenDrained = true;
                m_connectionErrorCode = event.errorCode;
                m_connectionErrorReason = event.reason;

                cancelKeepAliveTimers();
                cancelIdleTimer();

                armDrainDeadline();
            }

            /*************************************************************************************
             * The read loop
             */

            void scheduleRead()
            {
                if( base_type::isClosing() )
                {
                    return;
                }

                base_type::beginOperation();

                base_type::getStream().async_read_some(
                    asio::buffer( m_readBlock -> begin(), m_readBlock -> capacity() ),
                    cpp::bind(
                        &this_type::onRead,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred
                        )
                    );
            }

            /**
             * @brief Whether an error code means the peer ended the conversation
             *
             * operation_aborted is deliberately NOT here: it is what our own initiateClose( ) and
             * an external cancelTask( ) produce, and design 3.2's accounting is what tells those
             * two apart
             */

            bool isPeerClosed( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                return
                    asio::error::eof == ec ||
                    asio::error::connection_reset == ec ||
                    base_type::isStreamTruncationError( ec );
            }

            void onRead(
                SAA_in              const eh::error_code&                       ec,
                SAA_in              const std::size_t                           bytesTransferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ec )
                {
                    if( isPeerClosed( ec ) )
                    {
                        onPeerClosed();
                    }
                    else
                    {
                        BL_TASKS_HANDLER_CHK_EC( ec );
                    }
                }
                else
                {
                    m_session -> feed(
                        reinterpret_cast< const std::uint8_t* >( m_readBlock -> begin() ),
                        bytesTransferred,
                        now()
                        );

                    drainSessionEvents();

                    applyCommands();

                    pumpAllBodies();

                    chkArmSettingsTimer();

                    pumpWrites();

                    scheduleRead();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /**
             * @brief The peer went away without a GOAWAY, or after one
             *
             * A stream whose header block reached a write may have been processed, so it is not
             * provably unprocessed and it is not retryable - that is the rule of design 5.4 and
             * not a pessimism. A stream whose block never reached one is the rule's third limb and
             * IS retryable, which is what closeAllStreamsUnwrittenRetryable( ) decides per stream.
             * A stream the peer DID prove unprocessed, by putting it above a GOAWAY's
             * last-stream-id, was already closed with its retryable flag when the GOAWAY arrived
             */

            void onPeerClosed()
            {
                publishState( ConnectionState::Draining );

                closeAllStreamsUnwrittenRetryable(
                    eh::errc::make_error_code( eh::errc::connection_aborted )
                    );

                closeSubmissions();

                cancelTimers();

                publishState( ConnectionState::Closed );

                base_type::beginClose();
            }

            /*************************************************************************************
             * The write pump - exactly one write in flight
             */

            /**
             * @brief Called immediately before each async_write with what is about to go out
             *
             * Virtual because nothing on the wire can distinguish one write from two - TCP does
             * not preserve write boundaries - so the opening write being ONE write is only
             * checkable from in here. Default does nothing
             */

            virtual void onWriteScheduled( SAA_in const http2::Session::wire_buffer_t& buffer )
            {
                BL_UNUSED( buffer );
            }

            void pumpWrites()
            {
                if( m_isWriteInFlight || base_type::isClosing() || ! m_session )
                {
                    return;
                }

                if( ! m_session -> wantsWrite() )
                {
                    chkFinishClose();

                    return;
                }

                m_writeBuffer.clear();

                m_session -> produce( m_writeBuffer, now() );

                onHeaderBlocksProduced();

                if( m_writeBuffer.empty() )
                {
                    chkFinishClose();

                    return;
                }

                chkArmSettingsTimer();

                onWriteScheduled( m_writeBuffer );

                m_isWriteInFlight = true;

                base_type::beginOperation();

                asio::async_write(
                    base_type::getStream(),
                    asio::buffer( &m_writeBuffer[ 0 ], m_writeBuffer.size() ),
                    cpp::bind(
                        &this_type::onWrite,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred
                        )
                    );
            }

            void onWrite(
                SAA_in              const eh::error_code&                       ec,
                SAA_in              const std::size_t                           bytesTransferred
                ) NOEXCEPT
            {
                BL_UNUSED( bytesTransferred );

                BL_TASKS_HANDLER_BEGIN_CHK_EC()

                m_isWriteInFlight = false;

                if( m_isPrefaceWritePending )
                {
                    /*
                     * The preface is away, which is exactly where design 5.7's connect deadline
                     * ends - see onProtocolNegotiated( ) for why it is not disarmed there
                     */

                    m_isPrefaceWritePending = false;

                    base_type::cancelConnectDeadline();
                }

                /*
                 * produce( ) is what reaps a stream our own last frame closed, so its events are
                 * drained here as well as on the read path - a peer which says nothing further
                 * would otherwise leave that closure undelivered
                 */

                drainSessionEvents();

                applyCommands();

                pumpAllBodies();

                pumpWrites();

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************************
             * The connection level timers of design 5.7, every one of them on the strand
             */

            /**
             * @brief Arms the SETTINGS acknowledgement deadline while a frame of ours is unacked
             *
             * NOT a periodic tick. Session::onTimer( ) measures the deadline from the OLDEST
             * unacknowledged frame and this is armed as soon as one exists, so one expiry is
             * always at or after the engine's own deadline and the engine is the thing which
             * decides. A connection whose SETTINGS have been acknowledged therefore holds no timer
             * at all, which is what every steady-state connection is
             *
             * The value is SessionLimits::settingsTimeoutInSeconds and this task does not have a
             * second one - see the note on the reconciliation in design 5.7
             */

            void chkArmSettingsTimer()
            {
                if( ! m_session || base_type::isClosing() || m_isCloseWhenDrained )
                {
                    return;
                }

                if(
                    m_session -> isClosed() ||
                    0U == m_session -> unacknowledgedSettingsCount() ||
                    m_isSettingsTimerArmed
                    )
                {
                    return;
                }

                m_settingsTimer = base_type::createTimer();

                m_settingsTimer -> expires_from_now(
                    time::seconds(
                        static_cast< long >( m_session -> limits().settingsTimeoutInSeconds )
                        )
                    );

                m_isSettingsTimerArmed = true;

                base_type::beginOperation();

                m_settingsTimer -> async_wait(
                    cpp::bind(
                        &this_type::onSettingsDeadline,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            void chkDisarmSettingsTimer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_session && 0U != m_session -> unacknowledgedSettingsCount() )
                {
                    return;
                }

                if( m_settingsTimer )
                {
                    eh::error_code ec;

                    m_settingsTimer -> cancel( ec );
                }

                m_isSettingsTimerArmed = false;

                BL_NOEXCEPT_END()
            }

            void onSettingsDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                m_isSettingsTimerArmed = false;

                if( ! ec && m_session && ! base_type::isClosing() )
                {
                    /*
                     * The engine decides, not this timer: onTimer( ) raises SETTINGS_TIMEOUT only
                     * when the oldest unacknowledged frame really has run out of time, and it
                     * queues the GOAWAY and the ConnectionError event which the drain below turns
                     * into a close
                     */

                    m_session -> onTimer( now() );

                    drainSessionEvents();

                    chkArmSettingsTimer();

                    pumpWrites();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /**
             * @brief The optional keepalive PING of design 5.7, and its reply deadline
             */

            void chkArmKeepAlive()
            {
                if(
                    ! isEnabled( m_h2config.keepAliveInterval ) ||
                    base_type::isClosing() ||
                    m_isCloseWhenDrained
                    )
                {
                    return;
                }

                m_keepAliveTimer = base_type::createTimer();

                m_keepAliveTimer -> expires_from_now( m_h2config.keepAliveInterval );

                base_type::beginOperation();

                m_keepAliveTimer -> async_wait(
                    cpp::bind(
                        &this_type::onKeepAlive,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            void onKeepAlive( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ! ec && m_session && ! base_type::isClosing() && ! m_isCloseWhenDrained )
                {
                    std::uint8_t opaque[ 8 ];

                    m_pingCounter = m_pingCounter.value() + 1U;

                    const auto value = m_pingCounter.value();

                    for( std::size_t i = 0U; i < sizeof( opaque ); ++i )
                    {
                        opaque[ i ] =
                            static_cast< std::uint8_t >( ( value >> ( 8U * ( 7U - i ) ) ) & 0xFFU );
                    }

                    m_session -> ping( opaque );

                    armPingDeadline();

                    pumpWrites();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            void armPingDeadline()
            {
                if( ! isEnabled( m_h2config.keepAlivePingReplyTimeout ) )
                {
                    return;
                }

                m_pingDeadlineTimer = base_type::createTimer();

                m_pingDeadlineTimer -> expires_from_now( m_h2config.keepAlivePingReplyTimeout );

                base_type::beginOperation();

                m_pingDeadlineTimer -> async_wait(
                    cpp::bind(
                        &this_type::onPingDeadline,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            /**
             * @brief The peer answered our PING - the connection is alive, so start waiting again
             *
             * Any acknowledgement disarms the deadline rather than only the matching one. A PING
             * ack is only ever produced for a PING WE sent (one the peer sends is answered by the
             * engine and produces no event), so a mismatch would mean a peer echoing the wrong
             * eight octets - a protocol defect which says nothing about liveness, which is the one
             * thing this timer is asking about
             */

            void onPingAcknowledged()
            {
                cancelPingDeadline();

                chkArmKeepAlive();
            }

            void onPingDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ! ec && ! base_type::isClosing() )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "An HTTP/2 connection to '"
                            << base_type::key().host
                            << "' did not answer a keepalive PING within "
                            << m_h2config.keepAlivePingReplyTimeout
                        );

                    /*
                     * A peer which does not answer a PING is not a peer we close gracefully with:
                     * the GOAWAY would go into the same silence. Every live stream is failed and
                     * the task is cancelled, which is the external-cancel path of design 3.2 and
                     * completes the connection FAILED - which is what the pool has to see
                     *
                     * PUBLISHED BEFORE ANYBODY IS ANSWERED, and that order is the contract: a
                     * request task reads state( ) when it applies onClosed( ) and reports its
                     * connection unusable on anything but Ready, so answering first would let it
                     * read the Ready this route is entered from. It costs nothing to publish here
                     * - publishState( ) is monotone and neither closeAllStreams( ) nor
                     * closeSubmissions( ) reads the state
                     */

                    publishState( ConnectionState::Closed );

                    closeAllStreams(
                        eh::errc::make_error_code( eh::errc::timed_out ),
                        false /* isRetryable */
                        );

                    closeSubmissions();

                    TaskBase::requestCancelInternal();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            void cancelPingDeadline() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_pingDeadlineTimer )
                {
                    eh::error_code ec;

                    m_pingDeadlineTimer -> cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            void cancelKeepAliveTimers() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                eh::error_code ec;

                if( m_keepAliveTimer )
                {
                    m_keepAliveTimer -> cancel( ec );
                }

                if( m_pingDeadlineTimer )
                {
                    m_pingDeadlineTimer -> cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief The connection idle timer - armed only while no stream is in flight
             *
             * Idle is a STATE and not an elapsed time here: the timer is armed when the last
             * stream closes and cancelled when a stream opens, so what it measures is exactly the
             * span design 5.4 calls the connection's idle lifetime. Its value is the pool's, which
             * is why it is a parameter of this task and not a constant in it
             */

            void chkArmIdleTimer()
            {
                if(
                    ! isEnabled( m_h2config.idleTimeout ) ||
                    ! m_streams.empty() ||
                    base_type::isClosing() ||
                    m_isCloseWhenDrained
                    )
                {
                    return;
                }

                m_idleTimer = base_type::createTimer();

                m_idleTimer -> expires_from_now( m_h2config.idleTimeout );

                base_type::beginOperation();

                m_idleTimer -> async_wait(
                    cpp::bind(
                        &this_type::onIdleDeadline,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            void cancelIdleTimer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_idleTimer )
                {
                    eh::error_code ec;

                    m_idleTimer -> cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            void onIdleDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ! ec && m_streams.empty() && ! base_type::isClosing() )
                {
                    BL_LOG(
                        Logging::trace(),
                        BL_MSG()
                            << "Closing an idle HTTP/2 connection to '"
                            << base_type::key().host
                            << "'"
                        );

                    closeGracefully();

                    pumpWrites();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /**
             * @brief Disarms all five timers, ON THE STRAND - the only context allowed to touch
             * them
             *
             * boost::asio::basic_deadline_timer is documented "Distinct objects: Safe. Shared
             * objects: Unsafe", and the cost of ignoring it is not formal: cancel( ) returns
             * early on impl.might_have_pending_waits, so an update lost between the strand's arm
             * and another thread's cancel leaves a timer which should have been disarmed armed -
             * and here the drain deadline cancels the task while the PING deadline closes every
             * stream
             *
             * THE INVARIANT IS THAT EVERY CALLER IS ON THE STRAND. onPeerClosed( ),
             * closeGracefully( ) and every arm run in strand handler bodies; initiateClose( )
             * runs in a strand handler's epilog; cancelTask( ) arrives on any thread and
             * therefore posts. onTaskStoppedNothrow( ) is the one caller which is on the strand
             * by argument rather than by construction, and that argument is at its call site
             *
             * notes/plans/issues/http2-driver-timer-cancel-cross-thread-race-record.md is the
             * ThreadSanitizer report which found this with two contexts
             */

            void cancelTimers() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                eh::error_code ec;

                if( m_settingsTimer )
                {
                    m_settingsTimer -> cancel( ec );
                }

                if( m_keepAliveTimer )
                {
                    m_keepAliveTimer -> cancel( ec );
                }

                if( m_pingDeadlineTimer )
                {
                    m_pingDeadlineTimer -> cancel( ec );
                }

                if( m_idleTimer )
                {
                    m_idleTimer -> cancel( ec );
                }

                if( m_drainTimer )
                {
                    m_drainTimer -> cancel( ec );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Bounds the drain, so that a close is never waiting on a write the peer
             * controls
             *
             * THE CLOSE IS TAKEN THROUGH THE WRITE PUMP AND ONLY THROUGH IT: chkFinishClose( ) is
             * called from pumpWrites( ), and pumpWrites( ) returns immediately while a write is in
             * flight. That is fine for the GOAWAY and for a peer whose socket accepts the few
             * hundred bytes of it - but a peer which stops reading with our send buffer full
             * leaves the async_write outstanding for as long as TCP keeps retransmitting, and
             * against a zero-window peer indefinitely. Nothing else would end the task: the idle
             * timer's closeGracefully( ), a request's cancel and a connection error all end in
             * pumpWrites( ) returning
             *
             * So the drain has a deadline of its own, armed wherever m_isCloseWhenDrained is set
             * and disarmed by cancelTimers( ) once the close is taken. On expiry the task is
             * cancelled, which is the PING deadline's ending and for the same reason - a peer
             * which is not reading will not read a GOAWAY either
             */

            void armDrainDeadline()
            {
                /*
                 * m_drainTimer is created here and nowhere else, so its presence is what says the
                 * drain has already been bounded - a connection drains once
                 */

                if(
                    m_drainTimer ||
                    ! isEnabled( m_h2config.drainTimeout ) ||
                    base_type::isClosing()
                    )
                {
                    return;
                }

                m_drainTimer = base_type::createTimer();

                m_drainTimer -> expires_from_now( m_h2config.drainTimeout );

                base_type::beginOperation();

                m_drainTimer -> async_wait(
                    cpp::bind(
                        &this_type::onDrainDeadline,
                        self_ref_t::acquireRef( this ),
                        asio::placeholders::error
                        )
                    );
            }

            void onDrainDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( ! ec && ! base_type::isClosing() )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "An HTTP/2 connection to '"
                            << base_type::key().host
                            << "' did not drain within "
                            << m_h2config.drainTimeout
                        );

                    /*
                     * Every live stream is failed and the task is cancelled, which completes the
                     * connection FAILED - it did not end because it was done, it ended because the
                     * peer stopped taking what we had already decided to say
                     */

                    closeAllStreamsUnwrittenRetryable(
                        eh::errc::make_error_code( eh::errc::timed_out )
                        );

                    closeSubmissions();

                    publishState( ConnectionState::Closed );

                    TaskBase::requestCancelInternal();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************************
             * The graceful close of design 5.1
             */

            /**
             * @brief Says GOAWAY( NO_ERROR ) and closes once it, and everything queued before it,
             * has gone out
             *
             * This is the DELIBERATE door of design 3.2 and the task completes SUCCESSFULLY
             * through it - which is what lets the pool tell a connection that ended because it was
             * done from one that failed. cancelTask( ) is the other door and still reports failed,
             * deliberately
             */

            void closeGracefully()
            {
                if( m_isCloseWhenDrained || base_type::isClosing() )
                {
                    return;
                }

                m_isCloseWhenDrained = true;

                publishState( ConnectionState::Draining );

                cancelTimers();
                closeSubmissions();

                armDrainDeadline();

                if( m_session && ! m_session -> isClosed() )
                {
                    m_session -> goAway( http2::Globals::ERROR_CODE_NO_ERROR );

                    drainSessionEvents();
                }
            }

            /**
             * @brief Takes the deliberate close once there is nothing left to write
             */

            void chkFinishClose()
            {
                if( ! m_isCloseWhenDrained || base_type::isClosing() )
                {
                    return;
                }

                closeAllStreams(
                    eh::errc::make_error_code( eh::errc::connection_aborted ),
                    false /* isRetryable */
                    );

                publishState( ConnectionState::Closed );

                base_type::beginClose();

                if( ! m_connectionErrorReason.empty() )
                {
                    /*
                     * beginClose( ) first and the throw second, on purpose. The deliberate close
                     * is what excuses the operation_aborted of everything initiateClose( ) is
                     * about to cancel; this exception is not an abort, so it is still recorded as
                     * the task's first error and the connection completes FAILED - which is the
                     * distinction design 3.2 asks for, applied to the one case that is neither a
                     * clean end nor an external cancel
                     */

                    const auto reason = m_connectionErrorReason;
                    const auto errorCode = m_connectionErrorCode.value();

                    m_connectionErrorReason.clear();

                    BL_THROW(
                        Http2ProtocolException()
                            << eh::errinfo_http2_error_code( errorCode ),
                        BL_MSG()
                            << "The HTTP/2 connection failed: "
                            << reason
                        );
                }
            }

            /**
             * @brief Refuses anything further and hands back what the mailbox still holds
             *
             * Answered rather than dropped: a request whose command never reached a stream was
             * never written, so it is provably unprocessed and the layer above may replay it
             */

            void closeSubmissions() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                std::deque< Command > commands;

                {
                    BL_MUTEX_GUARD( m_commandsLock );

                    m_isSubmitClosed = true;

                    commands.swap( m_commands );
                }

                m_freeStreamSlots.store( 0U );

                while( ! commands.empty() )
                {
                    auto& command = commands.front();

                    if( CommandKind::Submit == command.kind )
                    {
                        failSubmission(
                            command,
                            eh::errc::make_error_code( eh::errc::connection_aborted ),
                            true /* isRetryable */
                            );
                    }

                    commands.pop_front();
                }

                BL_NOEXCEPT_END()
            }

            /*************************************************************************************
             * What an off-strand caller may read
             */

            void publishState( SAA_in const ConnectionState state ) NOEXCEPT
            {
                /*
                 * Monotone: a connection never goes back to Ready once it is Draining, and never
                 * leaves Closed. Publishing it out of order would let the pool dispatch onto a
                 * connection which had already said GOAWAY
                 */

                const auto current = m_connectionState.load();

                if( static_cast< std::uint8_t >( state ) <= static_cast< std::uint8_t >( current ) )
                {
                    return;
                }

                m_connectionState.store( state );

                if( ConnectionState::Ready != state )
                {
                    m_freeStreamSlots.store( 0U );
                }
            }

            /**
             * @brief Turns a session which has begun draining into a Draining CONNECTION
             *
             * The registry begins draining on its own, without an event and without telling
             * anybody: isDraining( ) goes true the moment the identifiers left fall to the reserve
             * design 4.3 leaves to the pool, and that happens INSIDE submitRequest( ). Every other
             * way a session drains - our own goAway( ) and a GOAWAY received - already publishes
             * Draining from the path which caused it, so this is the one transition nothing else
             * announces.
             *
             * WITHOUT IT THE RESERVE BUYS NOTHING. The pool retires a connection it sees Draining,
             * and has no other way to learn that this one will refuse the next request: the state
             * would still read Ready with slots free, so the pool would keep dispatching and the
             * driver would bounce every one of them back through canOpenStream( ) - which is the
             * stream of retryable bounces the reserve exists to prevent, arriving one reserve
             * later rather than not at all.
             *
             * It is called at BOTH answers of applySubmit( ): after a submission which took the
             * identifier that crossed the margin, and after one refused because the margin had
             * already been crossed - the second of which is what a session configured draining
             * from birth would otherwise never publish
             */

            void chkPublishDraining() NOEXCEPT
            {
                if( m_session && m_session -> isDraining() )
                {
                    publishState( ConnectionState::Draining );
                }
            }

            void publishFreeStreamSlots() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * canOpenStream( ) and not just the published state, so that a connection which
                 * will refuse the next submission never offers a slot for it. The state is a
                 * coarser thing and lags this by design - Draining is published once, from the
                 * strand, while the refusal is decided per submission
                 */

                if( ! canOpenStream() || ConnectionState::Ready != m_connectionState.load() )
                {
                    m_freeStreamSlots.store( 0U );

                    return;
                }

                /*
                 * ONE UNTIL THE PEER HAS SPOKEN ( design 5.1, L5 finding 5(c) ), and the three
                 * answers are three different facts. A peer which has named a concurrency limit
                 * is honoured at its own number; a peer which has sent its SETTINGS without that
                 * setting has told us it has no limit, so our own ceiling applies; and a peer
                 * which has not spoken at all has told us nothing, so exactly one stream - the one
                 * which rides the preface - is offered.
                 *
                 * THE THIRD ANSWER IS WHAT THE POOL READS THE PEER'S LIMIT FROM. Reporting ASSUMED
                 * there instead, as this did, publishes a number the pool cannot tell apart from a
                 * peer which allows exactly that many - which is what the pool's settle window
                 * existed for and what it no longer needs
                 */

                const std::size_t limit = m_session -> peerLimitsConcurrentStreams() ?
                    static_cast< std::size_t >( m_session -> peerMaxConcurrentStreams() ) :
                    (
                        m_isPeerSettingsSeen ?
                            static_cast< std::size_t >( ASSUMED_MAX_CONCURRENT_STREAMS ) :
                            static_cast< std::size_t >( UNCONFIRMED_MAX_CONCURRENT_STREAMS )
                    );

                const auto used = m_streams.size();

                m_freeStreamSlots.store( used >= limit ? 0U : limit - used );

                BL_NOEXCEPT_END()
            }

            /*************************************************************************************
             * Taking over from the establishment base
             */

            /**
             * @brief The handshake settled on h2 - keep the stream and start the loops
             *
             * Everything this class ever begins is begun from here or later, which is what keeps
             * the multi-operation accounting out of the establisher's retry: a retry restarts the
             * whole resolve / connect / handshake transaction IN PLACE and never passes through
             * scheduleNothrow, so an operation begun before the handshake would leave the next
             * attempt with a pending count carried over
             */

            virtual bool onProtocolNegotiated() OVERRIDE
            {
                if( httpclient::HttpProtocol::Http2 != base_type::m_negotiated.protocol() )
                {
                    /*
                     * Design 5.5's fallback. The base hands the connected stream to the factory,
                     * which is the one thing the factory is for on this path, and this task then
                     * completes - establishment really was all it did
                     */

                    return base_type::onProtocolNegotiated();
                }

                /*
                 * THE CONNECT DEADLINE IS NOT DISARMED HERE, AND THAT IS THE WHOLE POINT OF THE
                 * FLAG. Design 5.7's row ends at the preface and the base says the same ("a
                 * derived driver calls it once the preface is away"), so cancelling at the top of
                 * this function would end the deadline before the read block exists, before the
                 * session exists and before the opening write has been issued - and on a cleartext
                 * connection with no proxy the arm and the cancel sit in ONE synchronous chain, so
                 * the deadline would cover nothing at all. onWrite( ) disarms it instead, which is
                 * the first moment the preface really is away
                 *
                 * If that write never completes, the deadline is what ends the task; if it fails,
                 * the handler completes the task and the base disarms from onTaskStoppedNothrow( )
                 */

                m_isPrefaceWritePending = true;

                m_readBlock = data::DataBlock::get(
                    m_h2config.dataBlocksPool,
                    m_h2config.readBufferSize
                    );

                if( m_readBlock -> capacity() < m_h2config.readBufferSize )
                {
                    m_readBlock = data::DataBlock::createInstance( m_h2config.readBufferSize );
                }

                m_session.reset(
                    new http2::Session(
                        http2::StreamRole::Client,
                        now(),
                        m_h2config.profile,
                        m_h2config.limits
                        )
                    );

                /*
                 * Ready BEFORE the commands are drained, so that the first request's HEADERS can
                 * join the preface which the session queued in its constructor. This is the only
                 * write of m_negotiated's publication order (see the class comment): it has been
                 * written by continueAfterConnected above, and this store is what releases it
                 */

                publishState( ConnectionState::Ready );

                {
                    BL_MUTEX_GUARD( m_commandsLock );

                    m_isStrandReady = true;
                }

                publishFreeStreamSlots();

                applyCommands();

                pumpAllBodies();

                /*
                 * ONE produce( ), ONE async_write: preface, SETTINGS, connection WINDOW_UPDATE,
                 * the profile's PRIORITY frames and the first HEADERS
                 */

                pumpWrites();

                scheduleRead();

                chkArmKeepAlive();
                chkArmIdleTimer();

                return true;
            }

            /**
             * @brief Cancels what is in flight so the one terminal path can be taken
             *
             * It must NOT go through cancelTask( ): that sets the cancel flag, and design 3.2's
             * accounting excuses the operation_aborted of a deliberate close only while the task
             * has not been cancelled - so a connection which closed because it was done would
             * otherwise complete isFailed( ) and the pool would count a clean shutdown as a
             * failure
             */

            virtual void initiateClose() OVERRIDE
            {
                cancelTimers();

                if( base_type::isSocketCreated() )
                {
                    eh::error_code ec;

                    base_type::getSocket().cancel( ec );
                }
            }

            /**
             * @brief The external cancel - it arrives on ANY thread, so the timers are POSTED
             *
             * Calling cancelTimers( ) here gave it a second serialisation domain which excludes
             * nothing: the task lock this is always called under (requestCancelInternal( )) is
             * not held by initiateClose( ), which cancels the same five timers from the strand.
             * That is the race of the record above, observed on the drain deadline
             *
             * The shape is the h1 driver's - its cancelTask( ) posts shutdownOnStreamExecutor( )
             * rather than touching its timer - and the stranded stream policy's underneath this
             * one, which posts shutdownSocketOnStrand( ) for the same reason (TcpStrandedStreams.h)
             *
             * The guard is isSocketCreated( ), the same predicate initiateClose( ) above asks for
             * the socket: both stranded policies create the strand and the socket in one call,
             * strand first, so a created socket means there is a strand to post to - and with no
             * socket there is no timer either, since createTimer( ) is built on that same strand
             *
             * The post carries a reference to the task, so the handler cannot outlive it, and it
             * begins NO operation - the accounting is for operations the task waits on, and a
             * cancel must not add one to a task which is ending
             */

            virtual void cancelTask() OVERRIDE
            {
                if( base_type::isSocketCreated() )
                {
                    base_type::postToStrand(
                        cpp::bind(
                            &this_type::cancelTimers,
                            self_ref_t::acquireRef( this )
                            )
                        );
                }

                base_type::cancelTask();
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * The last word on every stream, and it runs on the strand: the terminal
                 * notifyReady of the mix-in is taken from the handler of the operation which
                 * completed last, which for this task is always a strand handler. A sink which
                 * was never told its stream ended would leave a request task waiting for an event
                 * that can no longer come
                 */

                /*
                 * NOT POSTED, and the one caller of cancelTimers( ) which is not: a handler posted
                 * from here may never run - the task is completing - so a post could lose the
                 * disarm altogether. It is a disarm which needs no wake, and the first one on the
                 * cleartext route; it is not the only one. Without it they would still be disarmed
                 * when the stream policy's onTaskStoppedNothrow( ) shuts the socket down and the
                 * read that wakes - aborted or eof - reaches cancelTimers( ) on the strand. Kept
                 * because it needs nothing to wake, costs nothing, and is safe
                 *
                 * Safe because it runs from notifyReadyImpl( ) under the TASK LOCK (TaskBase.h
                 * says so where onTaskStoppedNothrow( ) is declared), which excludes every handler
                 * body - every arm, onPeerClosed( ), closeGracefully( ) - and cancelTask( ),
                 * always reached under that same lock. What is left holds NO lock: initiateClose( )
                 * and the handler cancelTask( ) posts. Both run on the strand, and so does every
                 * notifyReady( ) which can reach here with a timer armed - the mix-in's terminal
                 * one, and the BL_TASKS_HANDLER_END( ) of the establishment handlers
                 * (onConnectionEstablished( ), onHandshakeCompleted( ), and on a proxied cleartext
                 * connection the tunnel stage's own two) - the route that arrives with operations
                 * still pending, after a throw out of onProtocolNegotiated( ). The one off-strand
                 * route, scheduleNothrow( )'s catch posting notifyReadyImpl( ) to the execution
                 * queue's pool, is taken only before the handshake, where all five timers are null
                 */

                cancelTimers();

                /*
                 * PUBLISHED BEFORE ANYBODY IS ANSWERED, and on this route that matters most: it is
                 * where every write error ends up, and every read error other than a peer close,
                 * and an external cancel - and the state last published on all of those is Ready.
                 * A request task reads state( ) when it applies onClosed( ), so publishing after
                 * the answers would make "the connection was lost" a race against a mailbox post
                 */

                publishState( ConnectionState::Closed );

                closeSubmissions();

                const eh::error_code errorCode = ( eptrIn || TaskBase::isCanceled() ) ?
                    eh::error_code( asio::error::operation_aborted ) :
                    eh::errc::make_error_code( eh::errc::connection_aborted );

                closeAllStreamsUnwrittenRetryable( errorCode );

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }

        public:

            /*************************************************************************************
             * httpclient::ClientConnection - every one of these posts and returns (rule L3)
             */

            virtual stream_handle_t submit(
                SAA_in          const httpclient::ClientRequest&                request,
                SAA_in          const om::ObjPtr< httpclient::ClientStreamEventSink >& eventSink
                ) OVERRIDE
            {
                BL_CHK_T(
                    false,
                    nullptr != eventSink,
                    ArgumentException(),
                    BL_MSG()
                        << "An HTTP/2 request cannot be submitted without an event sink"
                    );

                Command command;

                command.kind = CommandKind::Submit;
                command.request = toSessionRequest( request );
                command.data = request.body();
                command.hasBodySource = ( nullptr != request.bodySource() );
                command.sink = eventSink;

                {
                    /*
                     * The handle is minted under the mailbox lock so that it is minted in the
                     * order the commands are queued in - which is what makes a cancel of a handle
                     * that has not reached the strand yet meaningful
                     */

                    BL_MUTEX_GUARD( m_commandsLock );

                    command.handle = ++m_nextHandle;
                }

                const auto handle = command.handle.value();

                if( ! postCommand( std::move( command ) ) )
                {
                    return httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                }

                return handle;
            }

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                Command command;

                command.kind = CommandKind::Cancel;
                command.handle = handle;
                command.errorCode = errorCode;

                ( void ) postCommand( std::move( command ) );

                BL_NOEXCEPT_END()
            }

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                if( 0U == bytes )
                {
                    return;
                }

                Command command;

                command.kind = CommandKind::Consumed;
                command.handle = handle;
                command.bytes = bytes;

                ( void ) postCommand( std::move( command ) );
            }

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const om::ObjPtr< data::DataBlock >&            data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                Command command;

                command.kind = CommandKind::ProvideBody;
                command.handle = handle;
                command.data = data;
                command.endStream = endStream;

                ( void ) postCommand( std::move( command ) );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                return m_freeStreamSlots.load();
            }

            virtual ConnectionState state() const NOEXCEPT OVERRIDE
            {
                return m_connectionState.load();
            }

            /**
             * @brief What this connection speaks - valid once state( ) is not Connecting
             *
             * See the class comment for why this is not the const member a factory-built driver
             * has, and what takes its place
             */

            virtual auto negotiated() const NOEXCEPT -> const httpclient::NegotiatedProtocol& OVERRIDE
            {
                return base_type::m_negotiated;
            }

            /**
             * @brief The HTTP/2 request one ClientRequest is, without touching any session state
             *
             * Static and pure, so it runs on the CALLER's thread inside submit( ) rather than on
             * the strand - the strand's time belongs to I/O. The pseudo-header ORDER is not
             * decided here; it is the profile's, and the engine applies it (design 6.4)
             */

            static auto toSessionRequest( SAA_in const httpclient::ClientRequest& request )
                -> http2::SessionRequest
            {
                http2::SessionRequest result;

                result.method = request.method();
                result.scheme = request.url().scheme();
                result.authority = request.url().authority();
                result.path = request.url().pathAndQuery();
                result.headers = request.headers();
                result.hasBody = request.hasBody();

                result.priority.urgency = request.priority().urgency;
                result.priority.incremental = request.priority().isIncremental;

                return result;
            }
        };

    } // tasks

} // bl

#endif /* __BL_HTTP2_HTTP2CONNECTIONTASK_H_ */
