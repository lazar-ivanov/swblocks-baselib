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
         * THE OPERATIONS IN FLIGHT. At most four: a read, armed for the whole life of the
         * connection, a write, while a request is going out, the idle timer, while no request is,
         * and the deferred reuse verdict, for one strand hop - see finishStream( ). That is why
         * MultiOperationTaskT is mixed in - with one terminal path taken only once all of them
         * have completed or been cancelled - and why the read is armed even
         * when the connection is IDLE. The idle read is not ceremony: it is what notices a pooled
         * keep-alive connection the server closed, which is the single most common thing that
         * happens to one, and it is also what keeps the accounting from falling to zero. A task
         * whose pending count reaches zero while it is not closing can never be completed by
         * beginClose() at all, because the terminal is taken from onOperationCompleted() and
         * nothing would be left to complete.
         *
         * WHAT IS DELIBERATELY NOT HERE. A streaming request body: the request serializer of S2.5
         * writes a head and nothing else, and HTTP/1.1 would need request-side chunked framing
         * which that slice does not have, so submit( ... ) REFUSES a request carrying a BodySource
         * rather than half-supporting it - and provideBody( ... ) therefore never has a live handle
         * to act on. Connection pooling and the retry policy: they are the pool's (design 5.4,
         * S5.2); this driver only reports through state( ) whether it may be reused. Request
         * timeouts: the request task's (design 5.7).
         *
         * THE IDLE LIFETIME WAS IN THAT LIST UNTIL THE L6 REVIEW, AND IT WAS NOT TRUE OF ANYTHING.
         * The pool has no reaper and never had one, so an idle keep-alive connection acquired
         * through a session stayed open until the peer closed it - while the h2 driver, which was
         * given the same knob, closed itself. The lifetime is split rather than owned at one end:
         * the POOL owns the value, which is why it arrives here as a constructor parameter and is
         * not a constant, and THIS DRIVER owns the timer - chkArmIdleTimer( ). It has to. The
         * pool's only lever on a connection is requestCancel( ), which it documents as not a
         * graceful close and has no path by which it could be (ConnectionPool.h,
         * forgetConnection( )), while design 5.4 names the idle lifetime as the graceful path an
         * ordinary connection takes and disposal as the abrupt one. A reaper in the pool would
         * have had to end an idle connection by cancelling it, which is the opposite of what the
         * lifetime is for, and would have raced the h2 driver's own timer for every h2 connection
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
             * The pool's connection idle lifetime - see the class note. A special or non-positive
             * duration disables the timer, which is what a driver built outside a pool gets
             */

            const time::time_duration                                           m_idleTimeout;

            /*
             * Touched only on the stream's executor - the strand under a stranded policy - and in
             * onTaskStoppedNothrow, where the strand is quiescent: either the accounting has
             * established that nothing is in flight, or armRead( ) threw and nothing ever was
             */

            std::vector< char >                                                 m_readBuffer;
            std::size_t                                                         m_readValid = 0U;

            cpp::SafeUniquePtr< httpclient::Http1ResponseParser >               m_parser;

            /*
             * Armed and re-armed on the stream's executor, and cancelled from initiateClose( ),
             * which the accounting calls exactly once and never while the task lock is held
             */

            cpp::SafeUniquePtr< asio::deadline_timer >                          m_idleTimer;

            std::string                                                         m_requestHead;
            om::ObjPtrCopyable< data::DataBlock >                               m_requestBody;
            std::string                                                         m_bodyChunk;

            bool                                                                m_headersDelivered = false;
            /*
             * NOT "bytes were written" - "this request may have been sent", which is a weaker
             * claim and the only one a client can make about a write it has issued. It is set
             * before async_write( ) is initiated and consumed as the negation of retryability,
             * and cleared again by a write which transferred NOTHING - the one case in which
             * "no octet escaped" is proven rather than assumed
             */

            bool                                                                m_requestMayHaveBeenSent = false;

            /*
             * A DIFFERENT QUESTION FROM THE FLAG ABOVE, AND THE TWO MUST NOT BE FOLDED INTO ONE.
             * This one is "is a write handler still owed", which the storage of a write, whether
             * the reuse verdict may be taken at all, and the read's own hand-over of an ending it
             * cannot classify all depend on; the one above is "may bytes have escaped", which a
             * write that never started can still answer yes to. The h2 driver carries the same
             * flag under the same name and clears it in its write handler
             */

            bool                                                                m_isWriteInFlight = false;
            bool                                                                m_requestSaidClose = false;

            /*
             * HOW THE WRITE ENDED, WHICH IS EVIDENCE THE READ'S OWN CODE DOES NOT CARRY. One reset
             * sets the socket's ONE pending error and the first syscall to reach it takes it away,
             * so a send( ) which got connection_reset leaves the pending recv( ) a plain eof - and
             * eof is precisely what a close-delimited message may be completed on. Recorded by
             * every write handler and consulted by TWO readers: onPeerClosed( ), for the sentence
             * above, and onStreamEndDeferred( ), where it is the only evidence left of a write
             * which ended badly and closed nothing - the flag it would otherwise ask has been
             * cleared under it by then. Released by whichever of finishStream( ) and that
             * continuation finds the write over. A write which ended cleanly records an empty
             * code, so the record cannot outlive the ending it describes
             */

            eh::error_code                                                      m_writeEndingCode;

            /*
             * AND THE ENDING THE READ OBSERVED WHILE A WRITE WAS STILL IN FLIGHT - the same
             * question from the other side. The write handler has not run, so the record above is
             * not yet there to consult; the read hands its ending over instead and the write
             * handler delivers it. An empty code means nothing is owed, and an ending the read
             * admits is never the empty code - see onReadCompleted( )
             */

            eh::error_code                                                      m_deferredEndingCode;

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
                                        httpclient::Http1ResponseLimits(),
                SAA_in              time::time_duration                         idleTimeout =
                                        time::neg_infin
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) ),
                m_key( BL_PARAM_FWD( key ) ),
                m_limits( BL_PARAM_FWD( limits ) ),
                m_idleTimeout( BL_PARAM_FWD( idleTimeout ) ),
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

                /*
                 * The connection is no longer idle - see chkArmIdleTimer( ). A timer which has
                 * already fired is harmless, since its handler re-checks the state
                 */

                cancelIdleTimer();

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

                /*
                 * MARKED BEFORE THE WRITE IS ISSUED, AND NOT WHEN IT COMPLETES. From here on this
                 * request is not provably unsent: the octets may be on the wire, and acted on by
                 * the origin, long before onWriteCompleted( ) runs - and the negation of this
                 * flag is what the peer-close path hands the sink as retryability, which
                 * ConnectionPoolPolicy answers from BEFORE it reaches the idempotency gate. A
                 * POST replayed on that answer is a duplicate rather than a retry.
                 *
                 * BEFORE beginOperation( ) and the try, because the initiating call can throw:
                 * its catch completes the operation and fails the TASK, which reaches the same
                 * flag through onTaskStoppedNothrow( ). A write which threw is precisely a case
                 * that cannot be proven unwritten. The h2 driver marks at the same point and for
                 * the same reason - isHeadersProduced at hand-off, "not provably unwritten"
                 *
                 * The two paths which legitimately claim the opposite both return ABOVE this
                 * line: the isClosing( ) check and the render failure, neither of which reached
                 * the socket. The exact answer - "zero octets escaped, so this is safe to
                 * replay" - is answered in onWriteCompleted( ), which is the one place that can
                 * know it, and only once the write has settled
                 *
                 * m_isWriteInFlight GOES WITH IT AND IS NOT THE SAME FLAG. From here until the
                 * completion handler runs, the buffers handed to async_write( ) point into
                 * m_requestHead and m_requestBody, so neither may be released and this
                 * connection may not be published as Ready - see finishStream( )
                 */

                m_requestMayHaveBeenSent = true;
                m_isWriteInFlight = true;

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
                     *
                     * AND THE TWO FLAGS DIVERGE HERE, DELIBERATELY. No handler is owed, so
                     * m_isWriteInFlight is cleared - the same premise this catch already rests
                     * on. m_requestMayHaveBeenSent is NOT: a throw out of the initiator is
                     * precisely a case that cannot be proven unwritten, which is the whole of
                     * why it is set before the try
                     */

                    m_isWriteInFlight = false;

                    base_type::onOperationCompleted( std::current_exception(), false );
                }

                BL_NOEXCEPT_END()
            }

            void onWriteCompleted(
                SAA_in          const eh::error_code&                           ec,
                SAA_in          const std::size_t                               bytesTransferred
                ) NOEXCEPT
            {
                /*
                 * CLASSIFIED BEFORE THE HANDLER PROLOG, the same way onReadCompleted( ) classifies
                 * an end of stream - a write which failed because WE tore the send side down in
                 * initiateClose( ) is not a failure of this task, it is how the teardown reaches a
                 * write no cancel could
                 *
                 * THIS ARM'S PREDICATE IS THIS TASK'S STATE AND NOT THE ERROR'S CODE, which is
                 * where the write side differs from the read side rather than mirrors it. On the
                 * read side the transport is the only witness that the conversation ended, so the
                 * code is the only evidence there is. Here we ended it ourselves, and our state is
                 * better evidence than any code - decisively so, because the codes diverge:
                 * broken_pipe on POSIX, WSAESHUTDOWN on Windows, and whatever an ssl::stream
                 * surfaces on top of either. NetUtils.h states the house rule for exactly this,
                 * and the record behind it says this library has paid for a hand-written
                 * platform comparison three times. A state predicate is right on Windows without
                 * a Windows run; a code predicate cannot be known to be
                 *
                 * isClosing( ) AND NOT A DELIBERATE-CLOSE ACCESSOR, because both of its cases are
                 * right. Closing deliberately is the case this exists for. Closing because
                 * something already failed means m_firstError is already recorded, and
                 * MultiOperationTaskT would have DISCARDED this error anyway - the first error
                 * wins there - so excusing it one step earlier changes nothing observable
                 *
                 * AND CHK_CANCEL_IMPL( ) STAYS OUTSIDE THE GUARD. An external cancelTask( ) must
                 * still fail this task with operation_aborted, which the accounting requires and
                 * deliberately does not excuse; only the two endings classified here are swallowed
                 * here, and a cancel is neither of them
                 */

                const bool isOurOwnTeardown = ec && base_type::isClosing();

                /*
                 * THE SECOND ENDING WHICH IS NOT A FAILURE - the peer hung up while the request
                 * was still going out. Classified here beside the first and for the same reason,
                 * and asked of net:: rather than compared by hand, which is the rule NetUtils.h
                 * states and which the write side needs its own predicate for: both of the
                 * read-side ones refuse broken_pipe, and broken_pipe is one of the two codes this
                 * exact event produces
                 *
                 * SECOND AND NOT FIRST, AND THAT ORDER IS LOAD BEARING. Our own shutdown_send
                 * produces broken_pipe too, so on the write-barrier case both questions answer yes
                 * together on every run - and a connection torn down by us must be explained by
                 * our state and not by a code the peer could also have produced. isClosing( ) is
                 * the better evidence wherever it applies, so it applies first
                 *
                 * AND THE WRITE SIDE CLASSIFIES NOTHING - no onPeerClosed( ) of its own, no
                 * closeConnection( ). The read has been armed since the task was scheduled and the
                 * same ending reaches it with a READ-side code; the read side is the one holding
                 * the parser and the bytes, so ending the stream from here would reset that parser
                 * under a response which may still be arriving, and would put the verdict back on
                 * whichever handler ran first - the very thing this arm exists to remove
                 *
                 * WHAT THE WRITE DOES OWE THE READ IS ITS CODE, WHICH IS NOT A CLASSIFICATION. The
                 * read's machinery is the only thing that can tell a complete close-delimited
                 * response from a truncated one, and on this ordering it is handed the eof this
                 * write's own reset left behind - so the evidence is here while the verdict stays
                 * there. m_writeEndingCode carries the one to the other; an earlier version of this
                 * paragraph said the read side held the only machinery AND therefore the answer,
                 * which was true of the machinery and false of the outcome
                 *
                 * isStreamTruncationError( ) BESIDE IT exactly as onReadCompleted( ) asks it,
                 * because this class is instantiated over the TLS policy as well and a truncated
                 * TLS stream is spelled by the policy rather than by the transport
                 */

                const bool isPeerClosedOnWrite =
                    net::isPeerClosedOnWriteErrorCode( ec ) || base_type::isStreamTruncationError( ec );

                /*
                 * RECORDED RAW, BEFORE THE PROLOG AND BESIDE THE CLASSIFICATION, and deliberately
                 * not filtered by either arm above. isOurOwnTeardown is TRUE of a peer's reset
                 * whenever the read observed the same ending first and closed this connection in
                 * answer to it - which is exactly the interleaving the deferral below exists for,
                 * so filtering by it would throw the evidence away in the one case that needs it.
                 * What the code MEANS is asked of net:: by the one function that consults this,
                 * and unconditionally is also what keeps a write which ended cleanly from leaving
                 * a verdict behind it
                 */

                m_writeEndingCode = ec;

                BL_TASKS_HANDLER_BEGIN()

                /*
                 * EVERYTHING THE WRITE OWED IS SETTLED HERE, AND AHEAD OF CHK_EC( ). That macro
                 * throws to the epilog, so anything placed after it never runs for a write which
                 * FAILED - and a failed write is exactly the case which must let go of the
                 * caller's DataBlock and, when it transferred nothing, take back the
                 * conservative claim that the request may have been sent
                 */

                m_isWriteInFlight = false;

                if( 0U == bytesTransferred )
                {
                    /*
                     * THE EXACT ANSWER onStartRequest( ) defers to here. async_write( ) reports
                     * the CUMULATIVE total across its internal write_some( ) calls, so zero means
                     * no octet was ever handed to the stream - under the TLS policy, that no
                     * plaintext octet was ever encrypted into a record. It is not a claim about
                     * what the peer did with bytes it received; it is the claim that there were
                     * none, which is the one thing that makes a replay safe rather than a
                     * duplicate. Clearing it is safe only because of the barrier below: a
                     * connection with a write in flight is never published as Ready, so no second
                     * request can have started in between
                     */

                    m_requestMayHaveBeenSent = false;
                }

                /*
                 * THE WRITE'S STORAGE IS RELEASED BY THE WRITE'S OWN HANDLER, which is the
                 * earliest correct point: the buffers async_write( ) was given point into these
                 * two, and until this handler runs they are still being read from. finishStream( )
                 * keeps its copy of these two clears for the paths where no write was ever issued
                 */

                m_requestHead.clear();
                m_requestBody.reset();

                /*
                 * THE READ'S ENDING, DELIVERED HERE BECAUSE ITS OWN HANDLER RAN FIRST. A read
                 * which observed an end of stream while this write was in flight could not
                 * consult a record this handler had not written yet, so it handed the ending over
                 * instead - see onReadCompleted( ). Both handlers are on the strand, so the flag
                 * it decided on was exact and this delivery is owed exactly once
                 *
                 * AHEAD OF CHK_EC( ), for the same reason the storage release is: that macro
                 * throws to the epilog, and an ending the read already observed is owed to the
                 * sink whatever this write's own code turns out to be. Delivering it is the
                 * READ's act performed here, which is why it is not gated by the arms above -
                 * the question those answer is whether this TASK failed, and this one is how the
                 * MESSAGE ended
                 *
                 * AND IT CANNOT WAIT FOREVER, which is the deferral's whole safety: the read's
                 * closeConnection( ) reaches initiateClose( ) through its own epilog, and that
                 * shuts the send side down for a write in flight - so a parked write wakes with
                 * broken_pipe, one which already failed at the syscall carries its own code, and
                 * one the cancel reaped carries operation_aborted
                 */

                if( m_deferredEndingCode )
                {
                    const auto deferredEndingCode = m_deferredEndingCode;

                    m_deferredEndingCode = eh::error_code();

                    onPeerClosed( deferredEndingCode );
                }

                if( ! isOurOwnTeardown && ! isPeerClosedOnWrite )
                {
                    BL_TASKS_HANDLER_CHK_EC( ec );
                }

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
             * @brief Arms the read which is in flight for the whole life of this connection, and
             * lets an initiator which throws out to its caller
             *
             * scheduleRead( ) below is this call with the accounting's guard around it, and which
             * of the two a call site wants turns on one question only - whether the pending count
             * can reach ZERO if the arm fails:
             *
             *   - from scheduleTask( ) it can. Nothing else is outstanding while the very first
             *     read is being armed, so completing the operation there finds the count back at
             *     zero, takes the single terminal path and reaches notifyReady( ) - while
             *     TaskBase::scheduleNothrow( ), which called scheduleTask( ), still holds the task
             *     lock that notifyReadyImpl( ) re-acquires. os::mutex is std::mutex, and it
             *     is not recursive, so that is a self-deadlock on the scheduling thread and not
             *     merely a breach of the rule at MultiOperationTask.h. The throw is let out
             *     instead, to scheduleNothrow( )'s own catch, which completes the task from the
             *     thread pool with no lock held - which is what that catch exists for
             *   - from onReadCompleted( ) it cannot. The completing read is still outstanding
             *     until BL_TASKS_HANDLER_END_MULTIOP( ) runs, so the count cannot reach zero and
             *     no terminal is due there; what the guard buys is the phantom operation being
             *     given back, without which the count never reaches zero AGAIN and the task hangs
             *
             * On the propagating route the count is left AT ONE, deliberately: no one reads it
             * once the task has completed, and MultiOperationTaskT::scheduleNothrow( ) zeroes the
             * whole accounting at the start of every run
             */

            void armRead()
            {
                BL_ASSERT( m_readValid < m_readBuffer.size() );

                base_type::beginOperation();

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

            /**
             * @brief armRead( ), with the accounting's guard - for every re-arm from a handler
             *
             * The try / catch is the accounting's, not the socket's. An operation which was begun
             * and then never started has to be completed here or the pending count never reaches
             * zero again - and a task whose count cannot reach zero can never take its terminal
             * path, which is a hang rather than a failure. The write path is guarded the same way
             */

            void scheduleRead()
            {
                try
                {
                    armRead();
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
             * @brief Whether the CODE THIS READ WAS HANDED is one a message framed by the ending
             * may be declared complete on - which is not the whole of that question
             *
             * IT ANSWERS ABOUT THE CODE AND NOT ABOUT THE ENDING, and the gap between the two was
             * a defect this driver shipped. A reset sets the socket's one pending error and the
             * first syscall to reach it takes it, so a send( ) which got there first leaves this
             * read a plain eof - which is admitted here, on purpose and correctly, because eof is
             * how a close-delimited message is framed at all. The other half of the question is
             * what the WRITE ended with, and onPeerClosed( ) asks that before it trusts this
             *
             * TWO PARTS, AND THE SECOND IS NOT OPTIONAL. net::isCleanEndOfStreamErrorCode( ) is
             * eof on every platform and deliberately refuses the Windows reset spellings, which
             * discard whatever was still unread. isStreamTruncationError( ) is the TLS stream
             * ending without close_notify, which the peer-close record files under "orderly close
             * of a TLS stream" and which is the ordinary shape of a close-delimited HTTPS
             * response (RFC 2818 2.2.2) - a predicate admitting eof alone would fail every one of
             * those, which succeed today
             */

            bool isCleanEndOfStream( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                return net::isCleanEndOfStreamErrorCode( ec ) || base_type::isStreamTruncationError( ec );
            }

            /**
             * @brief The peer closed - which COMPLETES a read-until-close body and refuses a
             * truncated message rather than hanging on it
             *
             * 'closeCode' IS HOW THE STREAM ENDED AND NOT ONLY THAT IT ENDED, which is the whole
             * of N2's second part. Everything onReadCompleted( ) admits arrives here, and this is
             * the one function which can declare a close-delimited message COMPLETE - so a
             * classification that admitted the Windows reset spellings would turn an aborted
             * transfer into a short response reported as a success, for exactly the class of
             * message HTTP/1.1 cannot frame any other way. An unclean end therefore does not
             * reach parseEof( ) at all, and the stream is finished with the TRANSPORT'S own code
             * rather than a protocol error: the caller saw "connection reset by peer" before this
             * change and must go on seeing it, because after N2's first part the connection task
             * itself ends cleanly and connectionFailureCause( ) has no exception left to chain
             *
             * AND THE READ'S OWN CODE IS NOT ALWAYS THE ENDING, WHICH THE SENTENCE ABOVE MISSED.
             * Refusing the Windows reset spellings is necessary and is not sufficient: on POSIX
             * the eof this function admits does the very same damage whenever the WRITE consumed
             * the reset, because the kernel hands its one pending error to the first syscall that
             * asks and leaves the other side an ordinary end of stream. Measured, and it reported
             * a body cut short by a RST to the caller as a complete 200. So the write's ending is
             * consulted below before this one is trusted
             *
             * WHICH IS CALLED FROM BOTH HANDLERS. The read's own, with the ending it observed,
             * and the WRITE's, with an ending the read observed while that write was in flight
             * and could not classify yet. A gate on this driver's own teardown belongs where the
             * ending is OBSERVED and not here, because the deferred delivery runs with
             * isClosing( ) already true by construction
             */

            void onPeerClosed( SAA_in const eh::error_code& closeCode )
            {
                if( ! m_parser )
                {
                    return;
                }

                /*
                 * THE WRITE'S CODE DECIDES WHEN IT IS PROOF AND NEVER OTHERWISE, and the rule is
                 * the kernel's: a reset is written into the socket's error from a state which has
                 * received no FIN, and EPIPE from one which has. So a write which completed
                 * connection_reset PROVES the ending was a reset with no orderly close before it,
                 * and the eof this read holds is the residue of that reset rather than a close;
                 * a write which completed broken_pipe proves nothing against the read's own code,
                 * because the read may have taken the reset itself, a FIN may have come first, or
                 * the pipe may be our own shutdown. net::isPeerResetOnWriteErrorCode( ) is that
                 * question, with the reasoning beside it - asked of net:: and never compared here
                 *
                 * AND IT CAN ONLY MAKE AN ENDING UNCLEAN. A write that ended cleanly, or that has
                 * not ended at all, records the empty code, which this refuses
                 */

                const bool isResetTakenByTheWrite = net::isPeerResetOnWriteErrorCode( m_writeEndingCode );

                const eh::error_code endCode = isResetTakenByTheWrite ? m_writeEndingCode : closeCode;

                if( ! isCleanEndOfStream( endCode ) )
                {
                    /*
                     * Whatever already parsed is still delivered - both of these are no-ops
                     * unless a header block or a body chunk arrived and was not handed on yet -
                     * and the message is NOT completed on the strength of a reset
                     */

                    deliverHeaders();
                    deliverBodyChunk();

                    finishStream(
                        endCode,
                        ! m_requestMayHaveBeenSent /* isRetryable */,
                        false /* isConnectionUsable */
                        );

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
                        ! m_requestMayHaveBeenSent /* isRetryable */,
                        false /* isConnectionUsable */
                        );

                    return;
                }

                finishStream(
                    m_parser -> isComplete() ?
                        eh::error_code()
                        :
                        eh::errc::make_error_code( eh::errc::protocol_error ),
                    ! m_requestMayHaveBeenSent /* isRetryable */,
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
                 *
                 * ASKED OF net:: RATHER THAN COMPARED BY HAND - N2, and the rule NetUtils.h
                 * states in as many words. The same peer behaviour reaches us under different
                 * codes on Windows, where a close during a full-duplex transfer is reported as
                 * connection_aborted and a close with unread data as connection_reset; neither is
                 * eof, so the comparison this replaces FAILED a connection the peer had closed
                 * normally, about one time in eight. It is the same defect the HTTP/2 driver had
                 * before it asked the same question here
                 *
                 * THE PREDICATE IS THE WIDE ONE ON PURPOSE - the conversation is over however it
                 * ended, and failing the task is the wrong answer to a peer that went away. What
                 * that admits is then discriminated by onPeerClosed( ), which is where completing
                 * a message on the strength of the close is decided, and which is why this change
                 * cannot ship without that one
                 */

                const bool isEndOfStream =
                    net::isPeerClosedErrorCode( ec ) || base_type::isStreamTruncationError( ec );

                BL_TASKS_HANDLER_BEGIN()

                if( isEndOfStream )
                {
                    /*
                     * DEFERRED WHILE A WRITE IS IN FLIGHT, AND WITHOUT THIS THE CONSULT BELOW IS
                     * HALF A FIX. The evidence onPeerClosed( ) needs is the write's code, and the
                     * write's handler can run AFTER this one - the reactor posts the read op and
                     * completes the write op inline, so an ending observed here may be an ending
                     * whose only witness has not been asked yet. A record not yet written is a
                     * record that cannot be consulted, so the ending is handed to the handler
                     * which will hold it instead
                     *
                     * THE WRITE HANDLER IS GUARANTEED TO RUN, which is what makes this a deferral
                     * and not a hang: closeConnection( ) below reaches initiateClose( ) through
                     * this handler's own epilog, and that shuts the send side down for exactly
                     * the case of a write in flight. Both handlers are on the strand, so the flag
                     * is read here with no race and the hand-over is taken exactly once
                     *
                     * AND NOTHING IS DELIVERED HERE. An end of stream carries no octets, so there
                     * is nothing this read could lose by saying nothing; what it would lose by
                     * speaking is the discrimination itself
                     *
                     * WITH NO WRITE IN FLIGHT NOTHING BUT THIS READ COULD HAVE CONSUMED THE
                     * ENDING, so the ending is classified here and now, exactly as before - and
                     * with no parser there is no message to frame and onPeerClosed( ) returns at
                     * once, which is why an idle connection's close is left on the direct path
                     */

                    if( m_parser && m_isWriteInFlight )
                    {
                        /*
                         * SAID IN THE LOG BECAUSE NOTHING ELSE SAYS IT. Which of these two arms a
                         * run took is invisible otherwise - a case on either side of the hand-over
                         * passes whichever ran - so a later simplification which dropped the
                         * deferral would keep every case green and put back a truncated message
                         * reported as a success. One line, at the level the idle close uses
                         */

                        BL_LOG(
                            Logging::trace(),
                            BL_MSG()
                                << "Deferring a peer close to the write in flight on an HTTP/1.1 "
                                << "connection to '"
                                << m_key.host
                                << "'"
                            );

                        m_deferredEndingCode = ec;
                    }
                    else
                    {
                        onPeerClosed( ec );
                    }

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

                /*
                 * A WRITE STILL IN FLIGHT MAKES THIS CONNECTION UNUSABLE, and that is the exact
                 * MIRROR of the rule deriveIsReusable( ) already applies in the other direction.
                 * A response which left bytes unconsumed makes the connection unusable because
                 * those bytes are either unsolicited or a second message smuggled behind the
                 * first; request bytes still in OUR send buffer are the same sentence with the
                 * arrow reversed - the server has not consumed them, and a second request put
                 * behind them is read as this request's body
                 *
                 * REFUSED RATHER THAN DRAINED. The peer that stopped reading sets the pace, so a
                 * drain is unbounded; refusing states the rule in one predicate
                 *
                 * AND IT DOES NOT HANG - BUT ONLY BECAUSE initiateClose( ) SHUTS THE SEND SIDE
                 * DOWN, which it did not when this barrier first landed. ! isReusable takes
                 * closeConnection( ), which is beginClose( ), and the epilog of the handler which
                 * called it then reaches onOperationCompleted( ) with m_closing set and
                 * m_closeInitiated not - the one call which runs initiateClose( ).
                 *
                 * WHICH HANDLER THAT IS MOVED WITH THE DEFERRAL, AND THE ACCOUNTING IS WHY IT IS
                 * STILL TRUE. On the deferred path the close is the continuation's, so the epilog
                 * which frees the write is onStreamEndDeferred( )'s rather than the read's - and
                 * that continuation is an ACCOUNTED operation precisely so that its epilog reaches
                 * onOperationCompleted( ) at all. An unaccounted one would close and wake nothing,
                 * which in this case is a hang and not a failure
                 *
                 * WHAT FREES IT IS THE SHUTDOWN AND NOT THE CANCEL, and the difference was worth
                 * about three runs in four. A cancel reaps what is REGISTERED with the reactor,
                 * and a composed asio::async_write between two of its internal async_write_some
                 * steps has nothing registered at all - which is precisely the interleaving this
                 * barrier is tripped by. The cancel found nothing, the composed loop armed its
                 * next step afterwards, and initiateClose( ) runs once per run, so nothing was
                 * ever coming back for it. bf115f2's commit message says "cancels the socket and
                 * completes the write it refused to wait for"; that sentence was false and this
                 * one replaces it. See initiateClose( ), and the barrier case in
                 * utf_baselib_httpclient7, which is what measured it
                 */

                const bool isReusable =
                    isConnectionUsable && ! base_type::isClosing() && ! m_isWriteInFlight;

                /*
                 * H01 - A WRITE WHICH HAS FINISHED AND A WRITE WHICH IS STILL RUNNING ARE THE SAME
                 * THREE BITS ABOVE, AND ONE STRAND HOP IS WHAT TELLS THEM APART
                 *
                 * m_isWriteInFlight is cleared by the write's OWN handler, so a read completion
                 * which reaches this strand ahead of that handler reads a stale true and refuses a
                 * connection with nothing whatever wrong with it - measured at 11-18% of whole
                 * module runs of utf_baselib_httpclient3. The distinction the predicate above
                 * cannot make is not a state, it is TIME: the write has physically completed and
                 * its handler has merely not been dequeued yet
                 *
                 * SO THE VERDICT IS PUBLISHED ONE HOP LATER, AND THAT IS NEVER A WAIT. If the
                 * write's completion is already on this strand the hop lands behind it and the
                 * continuation reads a true false; if the write is GENUINELY still running -
                 * utf_baselib_httpclient7's write-barrier cases, 8MB against a parked peer -
                 * nothing is enqueued ahead of us, the continuation runs on the very next strand
                 * turn and publishes exactly the verdict this line computes. 94.5% of the
                 * occurrences measured fall in the first band, which is why this is worth doing:
                 * notes/plans/issues/h01-reuse-verdict-design.md, section 11
                 *
                 * AND THE WHOLE ENDING MOVES AS ONE UNIT IN TODAY'S ORDER - the verdict, the
                 * handle, the sink, onClosed( ) and the close-or-arm together, which is what
                 * publishStreamEnd( ) is. Deferring the verdict ALONE would invert the contract
                 * stated above this function: every observer in the tree reads the verdict at
                 * onClosed( ), so a callback delivered ahead of it races the answer it is the
                 * rendezvous for
                 *
                 * WHAT IS NOT DEFERRED IS THIS MESSAGE'S OWN STATE, below. The read re-armed by
                 * onReadCompleted( ) must never deliver into a message which has ended, and with
                 * the parser already gone the window behaves exactly as a reused connection does -
                 * unsolicited data throws there and a peer close ends nothing
                 */

                const bool isVerdictDeferred =
                    isConnectionUsable && ! base_type::isClosing() && m_isWriteInFlight;

                stream_handle_t handle = httpclient::ClientConnection::INVALID_STREAM_HANDLE;

                {
                    BL_MUTEX_GUARD( m_stateLock );

                    handle = m_handle;

                    /*
                     * RETIRED HERE EVEN WHEN THE VERDICT IS DEFERRED, and nothing can put it back
                     * inside the window: m_handle stays allocated until the verdict is published,
                     * so submit( ) refuses and there is nothing for onStartRequest( ) to start
                     */

                    m_request = httpclient::ClientRequest();
                    m_startPending = false;
                }

                m_parser.reset();

                /*
                 * THE WRITE'S STORAGE IS THE WRITE HANDLER'S TO RELEASE, and these two are only
                 * this function's for the paths which never issued one - the isClosing( ) return
                 * and the render failure of onStartRequest( ), and a cancel arriving before the
                 * request was ever started. Releasing them while a write is in flight is H01
                 * itself: the buffers handed to async_write( ) point into them
                 *
                 * AND THE WRITE'S RECORD GOES WITH THEM, under the same condition and for a
                 * second reason of its own. How a write ended is a fact about THIS message, so a
                 * connection handed back to the pool must not carry it into the next one; and a
                 * write still in flight is one whose record a deferred delivery may yet have to
                 * consult, which no stream ending may clear from under it
                 */

                if( ! m_isWriteInFlight )
                {
                    m_requestHead.clear();
                    m_requestBody.reset();

                    m_writeEndingCode = eh::error_code();
                }

                m_bodyChunk.clear();
                m_headersDelivered = false;
                m_requestMayHaveBeenSent = false;
                m_requestSaidClose = false;

                if( isVerdictDeferred )
                {
                    deferStreamEnd( handle, errorCode, isRetryable );
                }
                else
                {
                    publishStreamEnd( errorCode, isRetryable, isReusable );
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Publishes the connection's verdict and ends the stream - the unit
             * finishStream( ) may defer by one strand hop
             *
             * ONE UNIT AND IN THIS ORDER, which is the contract finishStream( ) states above
             * itself: the state is settled before the sink is told, because every observer of the
             * verdict reads it AT that event - the pool through the request task's onClosed( ),
             * and every case in the two driver modules
             *
             * THE VERDICT IS THE CALLER'S BECAUSE THE TWO CALLERS DO NOT COMPUTE THE SAME ONE, and
             * that difference is the whole of this change. The synchronous caller asks the write's
             * FLAG, as it always has; the deferred one asks the write's recorded OUTCOME, which is
             * the only question left once the flag has been cleared under it - see
             * onStreamEndDeferred( )
             */

            void publishStreamEnd(
                SAA_in          const eh::error_code&                           errorCode,
                SAA_in          const bool                                      isRetryable,
                SAA_in          const bool                                      isReusable
                ) NOEXCEPT
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

                    if( httpclient::ConnectionState::Closed != m_state )
                    {
                        m_state = isReusable ?
                            httpclient::ConnectionState::Ready
                            :
                            httpclient::ConnectionState::Draining;
                    }
                }

                if( sink && httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle )
                {
                    sink -> onClosed( handle, errorCode, isRetryable );
                }

                if( ! isReusable )
                {
                    closeConnection();
                }
                else
                {
                    /*
                     * The connection is keep-alive and nothing is on it, which is the state the
                     * idle lifetime measures - see chkArmIdleTimer( )
                     */

                    chkArmIdleTimer();
                }

                BL_NOEXCEPT_END()
            }

            /**
             * @brief H01 - posts the stream's ending one hop through the strand
             *
             * ACCOUNTED, AND chkArmIdleTimer( ) IS THE MODEL - beginOperation( ) before the post
             * and the initiator in a try whose catch completes the operation. Without it the
             * closeConnection( ) this continuation can reach would be beginClose( ) and nothing
             * else: initiateClose( ) runs only from onOperationCompleted( ), so an unaccounted
             * continuation which closed would wake nothing at all in the write-barrier case - read
             * blocked, write blocked, no timer live - and the task would never end
             *
             * IT IS ALSO WHAT KEEPS THE TERMINAL PATH OFF THE WINDOW. m_sink is still held and
             * m_handle still allocated until the continuation runs, so onTaskStoppedNothrow( )
             * would deliver this stream's ending instead of us; an operation still pending is
             * precisely what forbids the task from taking that path
             *
             * A POST WHICH THREW WOULD LEAVE THE STREAM UNENDED, and that is the one case the
             * catch below hands over deliberately: the failed operation fails the TASK, and the
             * safety net in onTaskStoppedNothrow( ) is what delivers onClosed( ) then
             */

            void deferStreamEnd(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code&                           errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                base_type::beginOperation();

                try
                {
                    postToStreamExecutor(
                        cpp::bind(
                            &this_type::onStreamEndDeferred,
                            selfRef(),
                            handle,
                            errorCode,
                            isRetryable
                            )
                        );
                }
                catch( std::exception& )
                {
                    base_type::onOperationCompleted( std::current_exception(), false );

                    return;
                }

                /*
                 * OUTSIDE THE TRY, BECAUSE THE OPERATION IS ACCOUNTED FOR EXACTLY ONCE. Anything
                 * placed inside it which throws AFTER the post has succeeded would complete an
                 * operation which is still pending, and the continuation would then run on a task
                 * which had already taken its terminal path
                 */

                BL_LOG(
                    Logging::trace(),
                    BL_MSG()
                        << "Deferring the reuse verdict of an HTTP/1.1 connection to '"
                        << m_key.host
                        << "' by one strand hop"
                    );

                BL_NOEXCEPT_END()
            }

            /**
             * @brief H01 - the deferred ending, one strand hop after the response completed
             *
             * @param handle The stream this ending belongs to
             */

            void onStreamEndDeferred(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const eh::error_code                            errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                /*
                 * NOTHING TO DO IF THIS IS NO LONGER THE STREAM WE WERE POSTED FOR. A cancel( )
                 * landing in the window finds the handle still allocated - which is exactly why it
                 * is kept so - posts onCancelStream( ) behind us and ends the stream itself, and
                 * the ending it publishes is the one the sink is owed. This continuation then has
                 * nothing left but to account for itself
                 *
                 * READ WITHOUT TAKING, AND THAT IS EXACT RATHER THAN LUCKY: inside the window
                 * m_handle is allocated, so submit( ) will not touch it, onTaskStoppedNothrow( )
                 * cannot run while this accounted operation is pending, and the only other writer
                 * of it - publishStreamEnd( ) - runs on this strand and therefore never
                 * concurrently with this
                 */

                if( handle != activeHandle() )
                {
                    break;
                }

                /*
                 * THE WRITE'S RECORDED OUTCOME AND NOT THE FLAG ALONE, WHICH IS THE POINT OF
                 * ASKING ONE HOP LATER RATHER THAN ONE BIT DIFFERENTLY
                 *
                 * A write handler which ran inside the window may have learned the connection is
                 * DEAD and closed nothing about it: a peer which answers from the head and resets
                 * with the upload unread is classified in onWriteCompleted( ) as an ending rather
                 * than a failure, and that arm clears the flag, releases the buffers and leaves
                 * the classification to the read. A continuation reading only the flag would
                 * publish Ready on that connection for one strand turn, and submit( ) accepts in
                 * one strand turn
                 *
                 * m_writeEndingCode IS THAT RECORD AND ITS LIFETIME IS ALREADY RIGHT. Every write
                 * handler records it raw ahead of its own prolog, a write which ended cleanly
                 * records the empty code, and finishStream( ) releases it only under
                 * ! m_isWriteInFlight - which is the one branch this path does not take, so the
                 * record is still there to be consulted and is released below instead
                 *
                 * isClosing( ) IS STILL ASKED AND IS NOT REDUNDANT. A write which failed with a
                 * code neither arm excuses has already failed the TASK by the time we run, and
                 * that is a different question from what the write's own code was
                 */

                const bool isWriteEnded = ! m_isWriteInFlight;

                const bool isReusable =
                    isWriteEnded && ! m_writeEndingCode && ! base_type::isClosing();

                BL_LOG(
                    Logging::trace(),
                    BL_MSG()
                        << "The deferred reuse verdict of an HTTP/1.1 connection to '"
                        << m_key.host
                        << "' found the write "
                        << ( isWriteEnded ? "completed" : "still in flight" )
                    );

                /*
                 * WHAT finishStream( ) COULD NOT RELEASE, RELEASED HERE UNDER THE SAME CONDITION
                 * AND FOR THE SAME TWO REASONS - see the block it belongs to. The write handler
                 * has released the two buffers itself by now; the record of how it ended is ours
                 * to clear, and a connection about to be published Ready must not carry this
                 * message's write into the next one
                 */

                if( isWriteEnded )
                {
                    m_requestHead.clear();
                    m_requestBody.reset();

                    m_writeEndingCode = eh::error_code();
                }

                /*
                 * THE IDLE TIMER IS ARMED HERE AND NOWHERE ELSE FOR THIS RESPONSE, and that is
                 * structural rather than lucky: chkArmIdleTimer( ) returns before allocating
                 * anything while activeHandle( ) is allocated, and through the whole window it is
                 */

                publishStreamEnd( errorCode, isRetryable, isReusable );

                BL_TASKS_HANDLER_END_MULTIOP()
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

            static bool isEnabled( SAA_in const time::time_duration& duration ) NOEXCEPT
            {
                return ! duration.is_special() && duration.total_milliseconds() > 0;
            }

            /**
             * @brief The connection idle timer - armed only while no request is in flight
             *
             * IDLE IS A STATE AND NOT AN ELAPSED TIME, the same way it is in the HTTP/2 driver:
             * the timer is armed when a keep-alive response completes and when a connection the
             * pool has not yet given a request is scheduled, and cancelled when a request starts,
             * so what it measures is exactly the span design 5.4 calls the idle lifetime. The
             * value is the pool's and the timer is this driver's - see the class note
             *
             * IT RUNS ON THE STREAM'S EXECUTOR AND IS ARMED FROM THERE, so it does not need the
             * state lock for the timer itself; the handle it reads to decide whether the
             * connection is idle does, because submit( ) writes it from any thread. It is also
             * why scheduleTask( ) POSTS this rather than calling it: that one call would otherwise
             * be the only one racing the read handler it has just armed
             */

            void chkArmIdleTimer() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if(
                    ! isEnabled( m_idleTimeout ) ||
                    base_type::isClosing() ||
                    ! base_type::isChannelOpen() ||
                    httpclient::ClientConnection::INVALID_STREAM_HANDLE != activeHandle()
                    )
                {
                    return;
                }

                m_idleTimer.reset(
                    new asio::deadline_timer(
                        #if ( ( BOOST_VERSION / 100 ) >= 1072 )
                        base_type::getSocket().get_executor()
                        #else
                        base_type::getSocket().get_io_service()
                        #endif
                        )
                    );

                m_idleTimer -> expires_from_now( m_idleTimeout );

                base_type::beginOperation();

                try
                {
                    m_idleTimer -> async_wait(
                        cpp::bind(
                            &this_type::onIdleDeadline,
                            selfRef(),
                            asio::placeholders::error
                            )
                        );
                }
                catch( std::exception& )
                {
                    /*
                     * The operation was begun and will never complete - the same guard the read
                     * and the write carry, and for the same reason
                     */

                    base_type::onOperationCompleted( std::current_exception(), false );
                }

                BL_NOEXCEPT_END()
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

            /**
             * @brief The idle lifetime expired - end the connection the graceful way
             *
             * The guard is re-checked because a request may have been submitted while this
             * handler was already queued, and a cancelled timer arrives here too - neither is an
             * error, so the deadline is not checked with BL_TASKS_HANDLER_CHK_EC( ) and an
             * expiry which finds the connection busy simply accounts for its own operation
             */

            void onIdleDeadline( SAA_in const eh::error_code& ec ) NOEXCEPT
            {
                BL_TASKS_HANDLER_BEGIN()

                if(
                    ! ec &&
                    ! base_type::isClosing() &&
                    httpclient::ClientConnection::INVALID_STREAM_HANDLE == activeHandle()
                    )
                {
                    BL_LOG(
                        Logging::trace(),
                        BL_MSG()
                            << "Closing an idle HTTP/1.1 connection to '"
                            << m_key.host
                            << "'"
                        );

                    closeConnection();
                }

                BL_TASKS_HANDLER_END_MULTIOP()
            }

            virtual void initiateClose() OVERRIDE
            {
                /*
                 * The read and the write are both on the socket, so cancelling it is what wakes
                 * them. It must not take the task lock and must not begin a new operation
                 *
                 * EXCEPT THAT A CANCEL CANNOT REACH A COMPOSED WRITE, which is why the shutdown
                 * below is here. asio::async_write( ) is a resumable loop over async_write_some( ),
                 * and between two of its steps it has NOTHING registered with the reactor;
                 * cancel( ) reaps what is registered, finds nothing of that write, and the loop
                 * arms its next step afterwards. initiateClose( ) runs exactly once per run
                 * (MultiOperationTask.h, m_closeInitiated), so no second cancel is coming and
                 * that write is never woken - a HANG and not a slow failure, because the task
                 * cannot take its terminal path while an operation is still pending
                 *
                 * TcpBaseTasks.h states the rule this misses in as many words: "shutdown() will
                 * prevent new read/write requests and cancel() will stop existing such requests".
                 * shutdown( ) is a property of the socket rather than an entry in the reactor's
                 * table, so it poisons the step the composed loop has not issued yet
                 *
                 * THE LIBRARY'S OWN TEARDOWN AND NOT A LINE WRITTEN HERE. shutdownSocket( ) is
                 * shutdown_send + cancel, the exact pair the PeerCloseErrorCodes_* control cases
                 * pin on every platform, and shutdown_send is the half that matters: shutting the
                 * RECEIVE side down makes OUR OWN socket report eof to the read still armed on
                 * it, which isCleanEndOfStream( ) is built to trust as the peer's orderly close
                 * and parseEof( ) would complete a half-received close-delimited body on
                 *
                 * GATED, because it is owed only where a cancel cannot do the job. The terminal
                 * notifyReady( ) runs the TLS close_notify of scheduleTaskFinishContinuation( )
                 * before the policy's own teardown is reached, so an ungated shutdown here would
                 * precede that close_notify on EVERY deliberate close and cost it. With the gate
                 * this is inert on every path with no write outstanding
                 *
                 * AND THE FLAG IS SET FIRST, as every cancelTask( ) in the library sets it beside
                 * its own call - shutdownSocket( ) is static and touches no task state. It is
                 * m_wasSocketShutdownForcefully that makes isShutdownNeeded( ) answer false, and
                 * a send side we have just shut down cannot carry a close_notify: the attempt
                 * would fail with a code that is neither eof nor expected on a handshaken task,
                 * so without the flag a cleanly closing TLS connection would FAIL
                 */

                if( base_type::isChannelOpen() )
                {
                    if( m_isWriteInFlight )
                    {
                        TcpSocketCommonBase::m_wasSocketShutdownForcefully = true;

                        TcpSocketCommonBase::shutdownSocket( base_type::getSocket() );
                    }

                    eh::error_code ec;

                    base_type::getSocket().cancel( ec );
                }

                /*
                 * The idle timer is not on the socket, so the cancel above does not wake it - and
                 * an operation which is never woken is a task which never reaches its terminal
                 * path. This is the place the base documents for exactly that: called once, with
                 * no task lock, for the sockets AND the timers
                 */

                cancelIdleTimer();
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
                 *
                 * ARMED AND NOT SCHEDULED, which is the one difference: this function is called
                 * by TaskBase::scheduleNothrow( ) with the task lock HELD, so an initiator which
                 * throws has to be let out to that function's catch rather than completed here.
                 * armRead( ) says what completing it here would cost
                 */

                armRead();

                if( hasPending )
                {
                    postToStreamExecutor(
                        cpp::bind(
                            &this_type::onStartRequest,
                            selfRef()
                            )
                        );
                }
                else
                {
                    /*
                     * A driver the ALPN fallback built and the pool then had no request for is
                     * idle from birth, which is the narrow case the retry budget can produce
                     * (L6 finding 4), so its lifetime starts here rather than at the first
                     * response. POSTED, because everything else which touches the timer runs on
                     * the stream's executor and the read armed above may already be completing
                     * there
                     */

                    postToStreamExecutor(
                        cpp::bind(
                            &this_type::chkArmIdleTimer,
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
             * request task would wait for an event which is never coming. It does not race the
             * strand: either the accounting has established that no operation is in flight, or the
             * schedule-path arm threw and none ever was. It is idempotent because delivering the
             * event is what releases the sink
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
                        ! m_requestMayHaveBeenSent /* isRetryable */
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
             *
             * The same refusal is why this driver never raises the upload pull S5.1 added,
             * ClientStreamEventSink::onBodyWanted( ). That event exists for a connection which can
             * take a body in pieces as its windows allow; this one takes the whole body at
             * submit( ) or takes the request not at all, so there is nothing for it to ask for.
             * Stated here rather than left to be rediscovered by the next reader who greps for the
             * event and finds one driver raising it
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
