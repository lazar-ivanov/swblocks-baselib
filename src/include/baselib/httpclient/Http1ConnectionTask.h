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

#ifndef __BL_HTTPCLIENT_HTTP1CONNECTIONTASK_H_
#define __BL_HTTPCLIENT_HTTP1CONNECTIONTASK_H_

#include <baselib/httpclient/Http1Codec.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/MultiOperationTask.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksIncludes.h>

#include <baselib/http/HeaderList.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/Utils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace bl
{
    namespace tasks
    {
        /******************************************************************************************
         * ================================= Http1ConnectionTaskT =================================
         */

        /**
         * @brief The HTTP/1.1 driver of design 5.5 - one request at a time over Http1Codec
         *
         * It is a task AND a httpclient::ClientConnection, the composition TcpBlockTransferClient's
         * auto-push connection already uses: the pool holds it as a ClientConnection and runs it on
         * an execution queue as a Task, which is what gives the pool the shutdown semantics
         * TcpServerBase already relies on for its connection queue (design 5.1).
         *
         * IT IS HANDED AN ALREADY CONNECTED STREAM, AND THAT HAS ONE CONSEQUENCE WHICH MUST NOT BE
         * FORGOTTEN. Unlike the HTTP/2 connection task, which IS the object that created its own
         * socket, this driver is what ClientDriverFactoryT builds after ALPN, so the stream reaches
         * it through attachStream(). A stream attached from elsewhere was not created by this
         * object's stream policy, so under a stranded policy this object's own m_strand is null and
         * getStrand(), createTimer() and postToStrand() all assert - TcpStrandedStreams.h says so in
         * as many words. What is NOT lost is the strand itself: it belongs to the socket, as its
         * executor, and the socket is what moved. So every post in this class goes through
         * getSocket().get_executor(), which is the expression
         * ClientConnectionTaskBaseT::armConnectDeadline() uses, for the same reason.
         *
         * STREAM IS ONE OF THE STRANDED POLICIES OF DESIGN 3.1, AND THE CLAIM IS NARROWED TO THEM
         * DELIBERATELY. getSocket().get_executor() is the strand under a stranded policy and the
         * I/O service under a plain one, and a post to an I/O service is not a serialization:
         * onStartRequest() runs from a plain post holding no task lock and touches m_parser,
         * m_requestHead, m_requestBody and m_bodyChunk, which onReadCompleted() - holding the task
         * lock, on whatever I/O thread the read completed on - touches too. Under a plain policy
         * those two can run at once. Design 5.1 prescribes a strand for exactly this class, the
         * cases below run it over the cleartext stranded policy and the explicit instantiation
         * compiles it over the TLS stranded one, so 'correct over the stranded policies' is what is
         * written here and what is true. Making it correct over a plain policy is not a comment
         * change - it would need the request start to hold the task lock.
         *
         * cancelTask() is overridden for the same reason and nothing else: the stranded policies
         * fall back to a SYNCHRONOUS forced shutdown when m_strand is null, which is a socket call
         * from the cancelling thread racing the strand's own handlers. Posting it to the stream's
         * executor restores what D13 exists to guarantee.
         *
         * THE OPERATIONS IN FLIGHT. At most two: a read, which is armed for the whole life of the
         * connection, and a write, while a request is going out. That is why MultiOperationTaskT is
         * mixed in - with one terminal path taken only once both have completed or been cancelled -
         * and why the read is armed even when the connection is IDLE. The idle read is not
         * ceremony: it is what notices a pooled keep-alive connection the server closed, which is
         * the single most common thing that happens to one, and it is also what keeps the
         * accounting from falling to zero. A task whose pending count reaches zero while it is not
         * closing can never be completed by beginClose() at all, because the terminal is taken from
         * onOperationCompleted() and nothing would be left to complete.
         *
         * WHAT IS DELIBERATELY NOT HERE. A streaming request body: the request serializer of S2.5
         * writes a head and nothing else, and HTTP/1.1 would need request-side chunked framing
         * which that slice does not have, so submit( ... ) REFUSES a request carrying a BodySource
         * rather than half-supporting it - and provideBody( ... ) therefore never has a live handle
         * to act on. Connection pooling, the idle lifetime and the retry policy: they are the
         * pool's (design 5.4, S5.2); this driver only reports through state( ) whether it may be
         * reused. Request timeouts: the request task's (design 5.7).
         */

        template
        <
            typename STREAM
        >
        class Http1ConnectionTaskT :
            public MultiOperationTaskT< STREAM >,
            public httpclient::ClientConnection
        {
        public:

            typedef Http1ConnectionTaskT< STREAM >                              this_type;
            typedef MultiOperationTaskT< STREAM >                               base_type;

        private:

            BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( Http1ConnectionTaskT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY_CHAIN_BASE( base_type )
                BL_QITBL_ENTRY( httpclient::ClientConnection )
            BL_QITBL_END( Task )

        public:

            enum : std::size_t
            {
                /**
                 * @brief The read buffer, which is also the largest single field line this driver
                 * can present to the parser in one piece
                 *
                 * The parser does not buffer: a field line it has not seen the end of is left
                 * unconsumed for the caller to present again in front of the next bytes. So a line
                 * longer than this buffer would never complete, which is why it is sized at the
                 * codec's own header cap (Http1ResponseLimits::DEFAULT_MAX_HEADERS_SIZE, which is
                 * http::SimpleHttpTask's g_maxResponseHeadersSize): a line this buffer cannot hold
                 * is one the parser's own header limit refuses first. The stall is still checked
                 * for rather than assumed away - see onBytesRead( ... )
                 */

                DEFAULT_READ_BUFFER_SIZE =
                    static_cast< std::size_t >( httpclient::Http1ResponseLimits::DEFAULT_MAX_HEADERS_SIZE ),
            };

        protected:

            typedef httpclient::stream_handle_t                                 stream_handle_t;
            typedef httpclient::ClientStreamEventSink                           sink_t;

            /*
             * Written once, at construction, and read from any thread afterwards. negotiated()
             * returns a reference, so this member must be const - one which was ever reassigned
             * would be a data race on an std::string (design 5.3, and the carried L3 note)
             */

            const httpclient::NegotiatedProtocol                                m_negotiated;
            const httpclient::ConnectionKey                                     m_key;
            const httpclient::Http1ResponseLimits                               m_limits;

            /*
             * Touched only on the stream's executor - the strand under a stranded policy - and in
             * onTaskStoppedNothrow, which runs when the multi-operation accounting has already
             * established that nothing is in flight, so the strand is quiescent by then
             */

            std::vector< char >                                                 m_readBuffer;
            std::size_t                                                         m_readValid = 0U;

            cpp::SafeUniquePtr< httpclient::Http1ResponseParser >               m_parser;

            std::string                                                         m_requestHead;
            om::ObjPtrCopyable< data::DataBlock >                               m_requestBody;
            std::string                                                         m_bodyChunk;

            bool                                                                m_headersDelivered = false;
            bool                                                                m_requestBytesWritten = false;
            bool                                                                m_requestSaidClose = false;

            /*
             * The leaf lock of design 5.2 rule L4. It guards what an off-strand caller reads or
             * writes - the state, the handle, and the request a submit( ... ) handed over - and
             * nothing is ever called while it is held
             */

            mutable os::mutex                                                   m_stateLock;

            httpclient::ConnectionState                                         m_state =
                httpclient::ConnectionState::Ready;

            stream_handle_t                                                     m_handle = 0U;
            stream_handle_t                                                     m_nextHandle = 0U;

            bool                                                                m_started = false;
            bool                                                                m_startPending = false;

            httpclient::ClientRequest                                           m_request;
            om::ObjPtr< sink_t >                                                m_sink;

            Http1ConnectionTaskT(
                SAA_in              httpclient::NegotiatedProtocol              negotiated,
                SAA_inout           typename STREAM::stream_ref&&               connectedStream,
                SAA_in              httpclient::ConnectionKey                   key,
                SAA_in              httpclient::Http1ResponseLimits             limits =
                                        httpclient::Http1ResponseLimits()
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) ),
                m_key( BL_PARAM_FWD( key ) ),
                m_limits( BL_PARAM_FWD( limits ) ),
                m_readBuffer( static_cast< std::size_t >( DEFAULT_READ_BUFFER_SIZE ) )
            {
                BL_CHK_T(
                    false,
                    nullptr != connectedStream.get(),
                    ArgumentException(),
                    BL_MSG()
                        << "An HTTP/1.1 driver requires a connected stream"
                    );

                base_type::attachStream( BL_PARAM_FWD( connectedStream ) );

                /*
                 * The driver owns the connection from here on, so the stream is shut down when this
                 * task finishes - which for a TLS policy is the protocol shutdown run as the task's
                 * finish continuation, once no operation is pending (design 3.2)
                 */

                base_type::isCloseStreamOnTaskFinish( true );
            }

            /*************************************************************************
             * The stream's own executor
             */

            /**
             * @brief A counted reference to this task, for the handlers it binds
             *
             * The interface is named EXPLICITLY, and it has to be. This class reaches om::Object
             * down two paths - through the task chain and through ClientConnection - so the
             * ref-counting calls of an om::ObjPtrCopyable< this_type > are ambiguous and the file
             * does not compile. Naming Task picks one of them, which is the right one because it
             * is the task's own lifetime these handlers extend
             */

            auto selfRef() NOEXCEPT -> om::ObjPtrCopyable< this_type, Task >
            {
                return om::ObjPtrCopyable< this_type, Task >::acquireRef( this );
            }

            /**
             * @brief Runs a handler on the executor the connected stream was built on
             *
             * NOT the policy's postToStrand(): this object did not create the socket, so its own
             * m_strand is null and that call would assert. The socket carries the strand as its
             * executor and the socket is what was handed over, so this is both correct and the only
             * thing which is
             */

            void postToStreamExecutor( SAA_in cpp::void_callback_t&& handler )
            {
                asio::post(
                    #if ( ( BOOST_VERSION / 100 ) >= 1072 )
                    base_type::getSocket().get_executor(),
                    #else
                    base_type::getSocket().get_io_service(),
                    #endif
                    BL_PARAM_FWD( handler )
                    );
            }

            /*************************************************************************
             * Reuse - derived HERE and not reported by the codec
             */

            static bool isOws( SAA_in const char ch ) NOEXCEPT
            {
                return ' ' == ch || '\t' == ch;
            }

            /**
             * @brief Whether a Connection field carries the given token
             *
             * Connection is a comma separated list of tokens and may appear more than once, so
             * every field line with that name is scanned and every token in it compared. The
             * comparison is http::HeaderList::equalsIgnoreCase, which folds ASCII only - never
             * str::to_lower_copy, which takes std::locale() and would let an embedder's global
             * locale decide whether this client saw a 'close'. That is the rule the whole codec
             * follows and the reason http/HeaderList.h gives for it
             */

            static bool hasConnectionToken(
                SAA_in          const http::HeaderList&                         headers,
                SAA_in          const std::string&                              token
                )
            {
                for( auto it = headers.begin(); it != headers.end(); ++it )
                {
                    if( ! http::HeaderList::equalsIgnoreCase( it -> name(), "connection" ) )
                    {
                        continue;
                    }

                    const auto& value = it -> value();

                    std::size_t begin = 0U;

                    while( begin <= value.size() )
                    {
                        auto end = value.find( ',', begin );

                        if( end == std::string::npos )
                        {
                            end = value.size();
                        }

                        auto first = begin;
                        auto last = end;

                        while( first < last && isOws( value[ first ] ) )
                        {
                            ++first;
                        }

                        while( last > first && isOws( value[ last - 1U ] ) )
                        {
                            --last;
                        }

                        if(
                            http::HeaderList::equalsIgnoreCase(
                                value.substr( first, last - first ),
                                token
                                )
                            )
                        {
                            return true;
                        }

                        begin = end + 1U;
                    }
                }

                return false;
            }

            /**
             * @brief Whether the connection may go back to the pool after this response
             *
             * THE CODEC DOES NOT ANSWER THIS. Http1ResponseParser publishes httpVersion(),
             * needsEof() and the header list and no keep-alive verdict at all, and the backend's
             * own keep_alive() is deliberately not re-exported through the facade - so the verdict
             * is assembled here, from what the parser does publish, and each input is named:
             *
             *   - an HTTP/1.0 response without 'Connection: keep-alive' - persistence is opt-in
             *     below HTTP/1.1 (RFC 9112 section 9.3)
             *   - any response carrying 'Connection: close', and equally any REQUEST which carried
             *     it: design 5.5 says neither side may have said close, and the request's word
             *     binds this client whatever the server answers. The request's own verdict is read
             *     from m_requestSaidClose rather than from m_request, because m_request is guarded
             *     by m_stateLock and this runs on the stream's executor holding nothing - see
             *     onStartRequest( ), which is the one place the request may be read
             *   - a body framed by the connection closing, which needsEof() reports. There is no
             *     end to such a message other than the close, so there is nothing to reuse
             *   - a 101, which is a final response that hands the connection to another protocol.
             *     Stated rather than inferred from how the backend frames an upgrade: a client
             *     which put a second request onto a switched connection would be writing HTTP into
             *     whatever now owns it
             *
             * It is also false when the response left bytes unconsumed. This driver never
             * pipelines, so there is no next response those bytes can belong to; they are either
             * unsolicited or a second message the server smuggled behind the first, and neither is
             * something to hand to the next request on this connection
             */

            bool deriveIsReusable() const NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_requestSaidClose )
                {
                    return false;
                }

                if( ! m_parser || ! m_parser -> isComplete() )
                {
                    return false;
                }

                if( m_parser -> needsEof() )
                {
                    return false;
                }

                if( 101 == m_parser -> statusCode() )
                {
                    return false;
                }

                if( hasConnectionToken( m_parser -> headers(), "close" ) )
                {
                    return false;
                }

                if( 10 == m_parser -> httpVersion() )
                {
                    if( ! hasConnectionToken( m_parser -> headers(), "keep-alive" ) )
                    {
                        return false;
                    }
                }

                if( 0U != m_readValid )
                {
                    return false;
                }

                BL_NOEXCEPT_END()

                return true;
            }

            /*************************************************************************
             * The request side
             */

            /**
             * @brief The head this driver puts on the wire, with the framing fields as ITS OWN
             *
             * Host is added when the caller did not supply one, because the serializer refuses a
             * request without it and because deriving it from the URL is the only correct answer.
             * The framing is not negotiable: Content-Length is SET from the body which will
             * actually be written, and removed when there is no body, because a declared length
             * this driver does not write is what makes the server read the next request's bytes as
             * this request's body. A caller supplied Transfer-Encoding is refused outright for the
             * same reason - this driver has no request-side chunked framing to honour it with, and
             * writing the head as if it did would be a smuggled request rather than a failure
             */

            std::string serializeRequestHead( SAA_in const httpclient::ClientRequest& request ) const
            {
                auto headers = cpp::copy( request.headers() );

                BL_CHK_T(
                    true,
                    headers.has( "transfer-encoding" ),
                    NotSupportedException(),
                    BL_MSG()
                        << "The HTTP/1.1 driver does not implement a request-side transfer coding"
                    );

                if( ! headers.has( "host" ) )
                {
                    headers.set(
                        "Host",
                        httpclient::Http1RequestSerializer::hostHeaderValue( request.url() )
                        );
                }

                if( request.body() )
                {
                    const auto& body = request.body();

                    headers.set(
                        "Content-Length",
                        utils::lexical_cast< std::string >( body -> size() - body -> offset1() )
                        );
                }
                else
                {
                    ( void ) headers.removeAll( "content-length" );
                }

                return httpclient::Http1RequestSerializer::serialize(
                    request.method(),
                    httpclient::Http1RequestSerializer::requestTarget( request.url() ),
                    headers
                    );
            }

            /**
             * @brief Starts the request a submit( ... ) handed over - on the stream's executor
             *
             * NOT one of the task handler macros, and that is the point of the shape below. This
             * runs from a plain post rather than from the completion of an accounted operation, so
             * the epilogs are both wrong for it: BL_TASKS_HANDLER_END_MULTIOP would account for an
             * operation which was never begun, and an epilog which completes the task would take a
             * terminal path with the read still in flight. Instead the two failures are separated -
             * a request this driver cannot render fails the REQUEST and leaves the connection up,
             * which is right because not one byte of it reached the wire, while a failure to start
             * the write is accounted for as the operation it had already begun
             */

            void onStartRequest() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                httpclient::ClientRequest request;
                bool hasRequest = false;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    if( m_startPending )
                    {
                        m_startPending = false;
                        hasRequest = true;

                        request = m_request;

                        /*
                         * RECORDED HERE BECAUSE THIS IS THE ONE PLACE THE REQUEST MAY BE READ. A
                         * request which says 'Connection: close' goes out with that token on it -
                         * serializeRequestHead( ) does not strip it - and RFC 9112 section 9.6
                         * makes that word binding on this client whatever the server answers: it
                         * MUST NOT send another request on this connection, and the server MUST
                         * close after the final response whether or not it echoes the token back.
                         * deriveIsReusable( ) runs on the stream's executor and takes no lock, so
                         * it cannot read m_request - which m_stateLock guards and finishStream( )
                         * clears - and consults this instead
                         */

                        m_requestSaidClose = hasConnectionToken( request.headers(), "close" );
                    }
                }

                if( ! hasRequest )
                {
                    return;
                }

                if( base_type::isClosing() )
                {
                    /*
                     * The connection went away between the submit and this handler. Nothing was
                     * written, so the request is provably unprocessed and may be replayed
                     */

                    eh::error_code aborted = asio::error::operation_aborted;

                    finishStream(
                        aborted,
                        true /* isRetryable */,
                        false /* isConnectionUsable */
                        );

                    return;
                }

                std::vector< asio::const_buffer > buffers;

                try
                {
                    m_parser.reset(
                        new httpclient::Http1ResponseParser(
                            m_limits,
                            "HEAD" == request.method() /* isResponseToHead */
                            )
                        );

                    m_parser -> bodyCallback(
                        [ this ](
                            SAA_in      const char*                             data,
                            SAA_in      const std::size_t                       size
                            ) -> std::size_t
                        {
                            /*
                             * Accumulated rather than delivered from here, so that onHeaders always
                             * reaches the sink before the first onData of the same message even
                             * when both fall out of one parse( ... ) call. 'this' is captured raw
                             * because the parser is this object's own member and cannot outlive it,
                             * where a counted reference would be a cycle through it
                             */

                            m_bodyChunk.append( data, size );

                            return size;
                        }
                        );

                    m_requestHead = serializeRequestHead( request );
                    m_requestBody = request.body();

                    buffers.push_back( asio::buffer( m_requestHead.c_str(), m_requestHead.size() ) );

                    if( m_requestBody )
                    {
                        buffers.push_back(
                            asio::buffer(
                                reinterpret_cast< const char* >( m_requestBody -> pv() ) +
                                    m_requestBody -> offset1(),
                                m_requestBody -> size() - m_requestBody -> offset1()
                                )
                            );
                    }
                }
                catch( std::exception& e )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "An HTTP/1.1 request could not be rendered: "
                            << e.what()
                        );

                    /*
                     * Not one byte of it reached the socket, so the request is both replayable and
                     * harmless to the connection - the next one may have it
                     */

                    finishStream(
                        eh::errc::make_error_code( eh::errc::invalid_argument ),
                        true /* isRetryable */,
                        true /* isConnectionUsable */
                        );

                    return;
                }

                base_type::beginOperation();

                try
                {
                    asio::async_write(
                        base_type::getStream(),
                        buffers,
                        cpp::bind(
                            &this_type::onWriteCompleted,
                            selfRef(),
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred
                            )
                        );
                }
                catch( std::exception& )
                {
                    /*
                     * The operation was begun and will never complete, so it is completed here -
                     * the accounting must balance or the task can never take its terminal path
                     */

                    base_type::onOperationCompleted( std::current_exception(), false );
                }

                BL_NOEXCEPT_END()
            }

            void onWriteCompleted(
                SAA_in          const eh::error_code&                           ec,
                SAA_in          const std::size_t                               bytesTransferred
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if( 0U != bytesTransferred )
                {
                    m_requestBytesWritten = true;
                }

                BL_TASKS_HANDLER_CHK_EC( ec );
                BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                /*
                 * Nothing further to do - the read loop has been armed since the task was
                 * scheduled and the response will arrive on it
                 */

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************
             * The response side
             */

            /**
             * @brief Arms the read which is in flight for the whole life of this connection
             *
             * The try / catch is the accounting's, not the socket's. An operation which was begun
             * and then never started has to be completed here or the pending count never reaches
             * zero again - and a task whose count cannot reach zero can never take its terminal
             * path, which is a hang rather than a failure. The write path is guarded the same way
             */

            void scheduleRead()
            {
                BL_ASSERT( m_readValid < m_readBuffer.size() );

                base_type::beginOperation();

                try
                {
                    base_type::getStream().async_read_some(
                        asio::buffer(
                            m_readBuffer.data() + m_readValid,
                            m_readBuffer.size() - m_readValid
                            ),
                        cpp::bind(
                            &this_type::onReadCompleted,
                            selfRef(),
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred
                            )
                        );
                }
                catch( std::exception& )
                {
                    base_type::onOperationCompleted( std::current_exception(), false );
                }
            }

            /**
             * @brief Delivers the header blocks of the message, once its final section is complete
             *
             * INTERIM RESPONSES ARRIVE AFTER THE FACT, AND THAT IS ACCEPTED HERE RATHER THAN FIXED.
             * Http1ResponseParser files a 1xx into interimResponses() and restarts on the same
             * buffer; it has no per-interim callback, so the earliest moment this driver can see
             * that a 103 Early Hints happened is when the FINAL header section completes. Each
             * filed interim is therefore delivered here, in order, immediately before the final
             * block - which is what keeps the sink's ordering guarantee and its status/flag
             * invariant (isInterim is true exactly for a 1xx which is not 101). The consequence is
             * that a 103 reaches the request task later than it reached the wire; nothing in this
             * client acts on hints, so that is a property and not a defect, and adding a parser
             * callback to close it would buy nothing
             */

            void deliverHeaders()
            {
                if( m_headersDelivered || ! m_parser -> isHeaderComplete() )
                {
                    return;
                }

                m_headersDelivered = true;

                om::ObjPtr< sink_t > sink;
                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                if( ! tryGetActiveStream( sink, handle ) )
                {
                    return;
                }

                const auto& interims = m_parser -> interimResponses();

                for( std::size_t i = 0U; i < interims.size(); ++i )
                {
                    sink -> onHeaders(
                        handle,
                        static_cast< unsigned >( interims[ i ].statusCode.value() ),
                        cpp::copy( interims[ i ].headers ),
                        true /* isInterim */
                        );
                }

                sink -> onHeaders(
                    handle,
                    static_cast< unsigned >( m_parser -> statusCode() ),
                    cpp::copy( m_parser -> headers() ),
                    false /* isInterim */
                    );
            }

            void deliverBodyChunk()
            {
                if( m_bodyChunk.empty() )
                {
                    return;
                }

                om::ObjPtr< sink_t > sink;
                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                if( tryGetActiveStream( sink, handle ) )
                {
                    const auto block = data::DataBlock::createInstance( m_bodyChunk.size() );

                    std::memcpy( block -> pv(), m_bodyChunk.c_str(), m_bodyChunk.size() );

                    block -> setSize( m_bodyChunk.size() );

                    sink -> onData( handle, block );
                }

                m_bodyChunk.clear();
            }

            /**
             * @brief Delivers the trailer section, if the message had one
             *
             * The codec keeps the trailers OUT of headers() on purpose - merging a trailer into
             * the header list is how a trailer becomes a header injection - and the sink's contract
             * keeps them apart for the same reason, so they travel as their own event.
             *
             * It is called from the two paths which complete a MESSAGE, between the last
             * deliverBodyChunk( ) and the finishStream( ) which delivers onClosed - and deliberately
             * not from inside finishStream( ) itself, although that would be one site rather than
             * two. finishStream( ) is NOEXCEPT, so a sink which threw out of onTrailers( ) there
             * would reach BL_NOEXCEPT_END and take the process down; here it is inside the handler
             * macros, with the other two deliveries, where a throw fails this task and nothing else.
             * Only a complete message has a trailer section, so no other path can be the one which
             * needed it
             */

            void deliverTrailers()
            {
                if( ! m_parser || ! m_parser -> isComplete() || m_parser -> trailers().empty() )
                {
                    return;
                }

                om::ObjPtr< sink_t > sink;
                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                if( tryGetActiveStream( sink, handle ) )
                {
                    sink -> onTrailers( handle, cpp::copy( m_parser -> trailers() ) );
                }
            }

            /**
             * @brief Feeds what was read to the parser and delivers what came out of it
             *
             * @return true when the read loop should be armed again
             */

            bool onBytesRead( SAA_in const std::size_t bytesTransferred )
            {
                m_readValid += bytesTransferred;

                if( ! m_parser )
                {
                    /*
                     * Bytes with no request in flight. HTTP/1.1 has no server-initiated message, so
                     * this is either an unsolicited response or a second one smuggled behind the
                     * first, and in both cases nothing on this connection can be trusted again
                     */

                    BL_THROW(
                        UnexpectedException(),
                        BL_MSG()
                            << "The peer sent data on an idle HTTP/1.1 connection"
                        );
                }

                eh::error_code ec;

                const auto consumed = m_parser -> parse( m_readBuffer.data(), m_readValid, ec );

                if( 0U != consumed && consumed < m_readValid )
                {
                    std::memmove(
                        m_readBuffer.data(),
                        m_readBuffer.data() + consumed,
                        m_readValid - consumed
                        );
                }

                m_readValid -= consumed;

                deliverHeaders();
                deliverBodyChunk();

                if( ec )
                {
                    finishStream(
                        ec,
                        false /* isRetryable */,
                        false /* isConnectionUsable */
                        );

                    return false;
                }

                if( m_parser -> isComplete() )
                {
                    deliverTrailers();

                    finishStream(
                        eh::error_code(),
                        false /* isRetryable */,
                        deriveIsReusable()
                        );

                    return true;
                }

                if( m_readValid == m_readBuffer.size() )
                {
                    /*
                     * The buffer is full, which after the compaction above means the parser took
                     * none of it - so it is waiting for the end of a field line longer than this
                     * whole buffer and no further read can make progress. Checked rather than
                     * assumed away by the buffer being sized at the codec's own header cap
                     */

                    BL_THROW(
                        UnexpectedException(),
                        BL_MSG()
                            << "An HTTP/1.1 response field line exceeds the read buffer of "
                            << m_readBuffer.size()
                            << " bytes"
                        );
                }

                return true;
            }

            /**
             * @brief The peer closed - which COMPLETES a read-until-close body and refuses a
             * truncated message rather than hanging on it
             */

            void onPeerClosed()
            {
                if( ! m_parser )
                {
                    return;
                }

                eh::error_code ec;

                m_parser -> parseEof( ec );

                deliverHeaders();
                deliverBodyChunk();
                deliverTrailers();

                if( ec )
                {
                    finishStream(
                        ec,
                        ! m_requestBytesWritten /* isRetryable */,
                        false /* isConnectionUsable */
                        );

                    return;
                }

                finishStream(
                    m_parser -> isComplete() ?
                        eh::error_code()
                        :
                        eh::errc::make_error_code( eh::errc::protocol_error ),
                    ! m_requestBytesWritten /* isRetryable */,
                    false /* isConnectionUsable - the peer just closed it */
                    );
            }

            void onReadCompleted(
                SAA_in          const eh::error_code&                           ec,
                SAA_in          const std::size_t                               bytesTransferred
                ) NOEXCEPT
            {
                /*
                 * Classified before the handler prolog, exactly as HttpServerReceiveRequestTask
                 * classifies a truncation: an end of stream is not a failure of this task, it is
                 * how a read-until-close body ends and how a pooled idle connection is reclaimed
                 */

                const bool isEndOfStream =
                    asio::error::eof == ec || base_type::isStreamTruncationError( ec );

                BL_TASKS_HANDLER_BEGIN()

                if( isEndOfStream )
                {
                    onPeerClosed();

                    closeConnection();
                }
                else
                {
                    BL_TASKS_HANDLER_CHK_EC( ec );
                    BL_TASKS_HANDLER_CHK_CANCEL_IMPL()

                    if( onBytesRead( bytesTransferred ) && ! base_type::isClosing() )
                    {
                        scheduleRead();
                    }
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            /*************************************************************************
             * Ending a stream, and ending the connection
             */

            auto activeHandle() const NOEXCEPT -> stream_handle_t
            {
                BL_MUTEX_GUARD( m_stateLock );

                return m_handle;
            }

            /**
             * @brief The sink and the handle of the stream in flight, taken together under the one
             * lock which guards them, so that a delivery cannot pair one request's handle with
             * another's sink
             */

            bool tryGetActiveStream(
                SAA_out         om::ObjPtr< sink_t >&                           sink,
                SAA_out         stream_handle_t&                                handle
                ) const
            {
                BL_MUTEX_GUARD( m_stateLock );

                sink = om::copy( m_sink );
                handle = m_handle;

                return
                    nullptr != sink &&
                    httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle;
            }

            /**
             * @brief Ends the stream in flight, if there is one
             *
             * THE STATE IS SETTLED BEFORE THE SINK IS TOLD, and that order is the contract with the
             * pool: the request task calls ConnectionPool::releaseStream from its own handling of
             * onClosed, and the pool then asks this connection whether it may be reused. If the
             * verdict were applied after the event, that question would race the answer
             *
             * 'isConnectionUsable' IS THE CALLER'S TO STATE AND IS NOT DERIVED FROM THE ERROR, which
             * is the distinction a first version of this got wrong: a stream can fail without the
             * connection failing with it. A request this driver refused to render never reached the
             * socket, so the connection underneath it is exactly as it was and the next request may
             * have it - while a response which failed to parse has left bytes on a connection
             * nobody can now make sense of, and that one has to go. Only the response paths consult
             * deriveIsReusable( )
             */

            void finishStream(
                SAA_in          const eh::error_code&                           errorCode,
                SAA_in          const bool                                      isRetryable,
                SAA_in          const bool                                      isConnectionUsable
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                const bool isReusable = isConnectionUsable && ! base_type::isClosing();

                om::ObjPtr< sink_t > sink;
                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    handle = m_handle;
                    sink = std::move( m_sink );

                    m_handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                    m_sink.reset();
                    m_request = httpclient::ClientRequest();
                    m_startPending = false;

                    if( httpclient::ConnectionState::Closed != m_state )
                    {
                        m_state = isReusable ?
                            httpclient::ConnectionState::Ready
                            :
                            httpclient::ConnectionState::Draining;
                    }
                }

                m_parser.reset();
                m_requestHead.clear();
                m_requestBody.reset();
                m_bodyChunk.clear();
                m_headersDelivered = false;
                m_requestBytesWritten = false;
                m_requestSaidClose = false;

                if( sink && httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle )
                {
                    sink -> onClosed( handle, errorCode, isRetryable );
                }

                if( ! isReusable )
                {
                    closeConnection();
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Ends the connection deliberately - a close the task completes SUCCESSFULLY
             *
             * beginClose() records no error, so the operation_aborted its own initiateClose()
             * produces for the read still in flight is excused by the accounting and the task
             * finishes clean. That is what makes 'the server said close' an ordinary end of a
             * connection rather than a failed one the pool would have to interpret
             */

            void closeConnection() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    if( httpclient::ConnectionState::Closed != m_state )
                    {
                        m_state = httpclient::ConnectionState::Draining;
                    }
                }

                base_type::beginClose();

                BL_NOEXCEPT_END()
            }

            virtual void initiateClose() OVERRIDE
            {
                /*
                 * The read and the write are both on the socket, so cancelling it is what wakes
                 * them. It must not take the task lock and must not begin a new operation
                 */

                if( base_type::isChannelOpen() )
                {
                    eh::error_code ec;

                    base_type::getSocket().cancel( ec );
                }
            }

            /*************************************************************************
             * Task
             */

            virtual void scheduleTask( SAA_in const std::shared_ptr< ExecutionQueue >& eq ) OVERRIDE
            {
                BL_UNUSED( eq );

                base_type::ensureChannelIsOpen();

                bool hasPending = false;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    m_started = true;
                    hasPending = m_startPending;
                }

                /*
                 * The read is armed FIRST and unconditionally - it is the operation which is in
                 * flight for the whole life of this connection, and the response of a request
                 * started below arrives on it
                 */

                scheduleRead();

                if( hasPending )
                {
                    postToStreamExecutor(
                        cpp::bind(
                            &this_type::onStartRequest,
                            selfRef()
                            )
                        );
                }
            }

            /**
             * @brief Cancels the task by shutting the socket down ON THE STREAM'S EXECUTOR
             *
             * The stranded policies do this for a socket they created themselves and fall back to a
             * synchronous shutdown when m_strand is null - which is every stream attached from
             * elsewhere, so every driver the factory builds. The strand did not go away with the
             * policy's member though: it is the socket's executor, so posting there is the same
             * serialization the policy would have given a socket of its own
             */

            virtual void cancelTask() OVERRIDE
            {
                if( ! base_type::isChannelOpen() )
                {
                    return;
                }

                TcpSocketCommonBase::m_wasSocketShutdownForcefully = true;

                postToStreamExecutor(
                    cpp::bind(
                        &this_type::shutdownOnStreamExecutor,
                        selfRef()
                        )
                    );
            }

            void shutdownOnStreamExecutor() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( base_type::isChannelOpen() )
                {
                    TcpSocketCommonBase::shutdownSocket(
                        base_type::getSocket(),
                        true /* force */
                        );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief The safety net which keeps the sink's contract when the TASK fails
             *
             * onClosed( ... ) arrives even when the stream failed before any header and it is
             * always the last event (S2.6). A write which failed, or a cancel, completes the task
             * through the handler macros and never through finishStream( ... ), so without this a
             * request task would wait for an event which is never coming. It runs when the
             * multi-operation accounting has established that no operation is in flight, so it does
             * not race the strand, and it is idempotent because delivering the event is what
             * releases the sink
             */

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                om::ObjPtr< sink_t > sink;
                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    handle = m_handle;
                    sink = std::move( m_sink );

                    m_handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                    m_sink.reset();
                    m_request = httpclient::ClientRequest();
                    m_startPending = false;

                    m_state = httpclient::ConnectionState::Closed;
                }

                if( sink && httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle )
                {
                    const auto errorCode = eptrIn ?
                        eh::errorCodeFromExceptionPtr( eptrIn )
                        :
                        eh::errc::make_error_code( eh::errc::connection_aborted );

                    sink -> onClosed(
                        handle,
                        errorCode ?
                            errorCode
                            :
                            eh::errc::make_error_code( eh::errc::connection_aborted ),
                        ! m_requestBytesWritten /* isRetryable */
                        );
                }

                BL_NOEXCEPT_END()

                return base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );
            }

        public:

            /*************************************************************************
             * httpclient::ClientConnection
             */

            /**
             * @brief Takes the one request this connection can have in flight
             *
             * The handle comes back synchronously - the caller needs something to cancel with
             * before any event can arrive - and nothing is written before the posted handler runs
             *
             * A request whose body is a BodySource is REFUSED rather than half-served: HTTP/1.1
             * would need request-side chunked framing for a body of unknown length, the serializer
             * of S2.5 has none, and a driver which accepted the request and then wrote only its
             * head would produce a request the server waits forever to finish
             */

            virtual auto submit(
                SAA_in          const httpclient::ClientRequest&                request,
                SAA_in          const om::ObjPtr< sink_t >&                     eventSink
                )
                -> stream_handle_t OVERRIDE
            {
                if( ! eventSink || nullptr != request.bodySource() )
                {
                    return httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                }

                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                bool post = false;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    if(
                        httpclient::ConnectionState::Ready != m_state ||
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE != m_handle
                        )
                    {
                        return httpclient::ClientConnection::INVALID_STREAM_HANDLE;
                    }

                    handle = ++m_nextHandle;

                    m_handle = handle;
                    m_request = request;
                    m_sink = om::copy( eventSink );
                    m_startPending = true;

                    post = m_started;
                }

                if( post )
                {
                    postToStreamExecutor(
                        cpp::bind(
                            &this_type::onStartRequest,
                            selfRef()
                            )
                        );
                }

                return handle;
            }

            /**
             * @brief Cancels the request in flight - WHICH ENDS THE CONNECTION
             *
             * This is the one place where design 5.7's "cancelling a request never closes the
             * connection" cannot hold, and it is a property of HTTP/1.1 rather than of this
             * implementation: there is no stream-level reset in HTTP/1.1, so a client which stops
             * caring about the response it is receiving has no way to tell the server, and the only
             * thing it can do with the bytes still coming is to stop reading them. Saying so here
             * is better than a cancel which appears to leave a connection usable when the next
             * request on it would read the tail of this response as its own
             */

            virtual void cancel(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    if( handle != m_handle || httpclient::ClientConnection::INVALID_STREAM_HANDLE == handle )
                    {
                        return;
                    }
                }

                eh::error_code code = asio::error::operation_aborted;

                if( errorCode )
                {
                    code = errorCode;
                }

                postToStreamExecutor(
                    cpp::bind(
                        &this_type::onCancelStream,
                        selfRef(),
                        handle,
                        code
                        )
                    );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Nothing to credit - HTTP/1.1 has no flow control window
             *
             * The call is part of the protocol-agnostic contract and a request task makes it
             * whatever the connection speaks, so it is accepted and does nothing rather than being
             * an error. What backpressure there is over HTTP/1.1 is TCP's own
             */

            virtual void consumed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( bytes );
            }

            /**
             * @brief Unreachable by construction - submit( ... ) refuses a streaming body, so no
             * handle this connection ever issued has one
             */

            virtual void provideBody(
                SAA_in          const stream_handle_t                           handle,
                SAA_in_opt      const om::ObjPtr< data::DataBlock >&            data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                BL_UNUSED( data );
                BL_UNUSED( endStream );
                BL_UNUSED( handle );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                BL_MUTEX_GUARD( m_stateLock );

                return
                    (
                        httpclient::ConnectionState::Ready == m_state &&
                        httpclient::ClientConnection::INVALID_STREAM_HANDLE == m_handle
                    )
                    ? 1U : 0U;
            }

            virtual auto state() const NOEXCEPT -> httpclient::ConnectionState OVERRIDE
            {
                BL_MUTEX_GUARD( m_stateLock );

                return m_state;
            }

            virtual auto negotiated() const NOEXCEPT -> const httpclient::NegotiatedProtocol& OVERRIDE
            {
                return m_negotiated;
            }

            auto key() const NOEXCEPT -> const httpclient::ConnectionKey&
            {
                return m_key;
            }

        protected:

            void onCancelStream(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code                            errorCode
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( handle != activeHandle() )
                {
                    return;
                }

                /*
                 * HTTP/1.1 has no stream reset, so the response still coming has nowhere to go -
                 * see cancel( ... )
                 */

                finishStream(
                    errorCode,
                    false /* isRetryable */,
                    false /* isConnectionUsable */
                    );

                BL_NOEXCEPT_END()
            }
        };

        template
        <
            typename STREAM
        >
        using Http1ConnectionTaskImpl = om::ObjectImpl< Http1ConnectionTaskT< STREAM > >;

    } // tasks

} // bl

#endif /* __BL_HTTPCLIENT_HTTP1CONNECTIONTASK_H_ */
