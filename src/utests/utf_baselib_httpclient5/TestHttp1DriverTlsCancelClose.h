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

#ifndef __UTEST_TESTHTTP1DRIVERTLSCANCELCLOSE_H_
#define __UTEST_TESTHTTP1DRIVERTLSCANCELCLOSE_H_

#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/Http1ConnectionTask.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>

#include <utests/baselib/Http2DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * A1-tls, FACES 1 AND 2 - an ending we caused, read as the peer's, over TLS
 *
 * THE SYMPTOM IS ONE AND THE ENDINGS ARE THREE (driver-read-write-arms-design.md section 12.5): a
 * close-delimited message completed on an ending that was NOT the peer's orderly close. Face 3 -
 * the reset the write consumed - landed with A1-cleartext. The two here are:
 *
 *   FACE 1, our own teardown: the armed read observes an ending while this task is already
 *   closing, and onPeerClosed( ) frames whatever arrived as a complete message.
 *
 *   FACE 2, the external cancel: the same, while an external cancelTask( ) is in flight. m_closing
 *   has three writers and cancelTask( ) is none of them (MultiOperationTask.h), and the read
 *   handler's CHK_CANCEL_IMPL( ) sits in the else branch AFTER the end-of-stream arm - so neither
 *   the closing question nor the cancel question is asked where the ending is observed.
 *
 * WHY TWO OF THE THREE ARE ONE ARRANGEMENT, AND THE RE-DERIVATION THAT SAYS SO. Section 3.2 lists
 * three sites which can raise a first error outside the read handler with a response still in
 * flight - onWriteCompleted( ), onStartRequest( )'s initiating catch and chkArmIdleTimer( )'s. The
 * timer's runs before any response. onStartRequest( )'s does NOT run without a parser - the parser
 * is built earlier in that same function - but it clears m_isWriteInFlight before it completes the
 * operation, so the initiateClose( ) it reaches shuts nothing down, and the parser it leaves behind
 * was built moments before on this strand and is empty. finishStream( ) resets the parser on every
 * path that reaches it. So m_closing can only be true with a LIVE parser and an ARMED read through
 * onWriteCompleted( ), whose TWO failing macros are CHK_EC( ) and, after it, CHK_CANCEL_IMPL( ).
 * A2 excuses every peer-close code and our own teardown's broken_pipe at the first, and
 * isOurOwnTeardown excuses initiateClose( )'s own operation_aborted - so what a FIXTURE can arrange
 * there is an EXTERNAL cancel, reaching CHK_EC( ) as operation_aborted when the cancel reaped the
 * write and CHK_CANCEL_IMPL( ) when our shutdown woke it broken_pipe instead. MEASURED: 4 of 5 the
 * first, 1 of 5 the second. The first two cases below are therefore one arrangement differing by a
 * write in flight; the third removes the cancel and is what gives face 1's gate a red of its own.
 *
 * "A FIXTURE CAN ARRANGE" IS NOT "IS REACHABLE", AND THE DIFFERENCE IS THE WHOLE REASON THE GATE
 * EXISTS. A write code which is neither a peer close, nor our own teardown, nor a cancel does fail
 * the task from that handler with the parser live and the read armed - timed_out once the
 * retransmit budget is spent, or an error out of the TLS engine - and there face 1's gate acts
 * where the cancel check cannot. None of those can be produced from a loopback peer, which is what
 * the third case stands in for; it is not a claim that the state never happens.
 *
 * WHY THE ENDING IS OURS AND NOT THE PEER'S, WHICH IS THE WHOLE POINT. cancelTask( ) posts
 * shutdownOnStreamExecutor( ), which is shutdownSocket( force ) = linger + shutdown_send + cancel.
 * The peer below declares no length and sends one chunk; it then reads to the end of its stream and
 * closes only because OUR FIN arrived. So a message completed on that close is a message completed
 * on our own teardown, and the body the caller is handed is short by the rest of a response the
 * peer never finished. What the cases assert of that is the peer's OWN ending code: no close_notify
 * went out, which is cancelTask( )'s forceful shutdown and never a close this client chose.
 *
 * HOW THE READ SURVIVES THE CANCEL, MADE CERTAIN RATHER THAN HOPED FOR. Section 3.1's window is
 * real and is asio's: ssl::stream::async_read_some is composed (boost/asio/ssl/detail/io.hpp,
 * 1.90.0), and on want_input_and_retry it re-arms next_layer_.async_read_some( ) from an
 * intermediate handler. Between the transport read completing in the reactor and that handler
 * running on the strand, NOTHING of this read is registered and a cancel( ) reaps nothing. Holding
 * the strand is what turns that window from microseconds into a rendezvous, and the probe below is
 * the only door onto it: a sink cannot be used for this, because the driver calls a sink from
 * inside a handler which holds TaskBase::m_lock, and requestCancel( ) takes that same lock - so a
 * cancel issued against a held sink blocks until the read has already re-armed.
 *
 * WHAT OPENS THE WINDOW IS FIVE OCTETS. The peer writes a bare TLS record header - type, version,
 * length - on the transport and nothing else. That is exactly what a TCP segment boundary in the
 * middle of a record looks like to the client: the engine takes the five octets, wants the rest,
 * and re-arms. It cannot be a full record and it cannot be application data, because a data
 * completion after a cancel is caught by the read handler's own CHK_CANCEL_IMPL( ) and the task
 * ends correctly - which is the tree behaving, not the defect.
 *
 * THE BOUNDS, AND WHAT GOES WRONG IF ONE IS TOO SHORT. Every step is behind a rendezvous except
 * two settles, each of which bounds one reactor hop that nothing outside the reactor can be asked
 * about: the transport read taking octets the peer has already put on the wire, and the client's
 * read completing on a FIN the peer has already sent. Too short, and the read is still registered
 * when the cancel runs, or the ending has not been observed yet - in both the run goes GREEN
 * against the unfixed tree. Neither can produce a false red.
 */

namespace utest
{
    namespace tlsh1cancel
    {
        enum : std::size_t
        {
            /**
             * @brief How long anything here waits for something that IS coming
             */

            WAIT_IN_MILLISECONDS                = 30000U,

            /**
             * @brief One reactor hop, after a rendezvous has established what it is waiting for
             */

            SETTLE_IN_MILLISECONDS              = 300U,

            /**
             * @brief How long the composed write is given to park, with the strand left alone
             *
             * The same instrument, the same reason and the same number as A2's case in
             * utf_baselib_httpclient7: nothing outside the reactor can be asked whether a write
             * is parked, and the peer cannot see it either. Too short is a green run
             */

            WRITE_PARKS_IN_MILLISECONDS         = 500U,

            /**
             * @brief The request body the peer never reads, for the face 1 case
             *
             * The size and the reason are TestClientSessionTlsHttp1.h's: above the client's send
             * buffer plus the peer's receive buffer, and the peer's receive buffer is deliberately
             * NOT shrunk, because this peer has to reach the END of its stream and the FIN is
             * queued behind the unsent tail of the upload
             */

            BLOCKED_BODY_SIZE                   = 8U * 1024U * 1024U,
        };

        /**
         * @brief A one-shot rendezvous between the test thread and a handler on the driver's strand
         *
         * Two of them make one hand-over: the handler says it is holding the strand, the test
         * thread does what it needs the strand held for, and then lets the handler go
         */

        class Latch
        {
            BL_NO_COPY_OR_MOVE( Latch )

        public:

            Latch()
                :
                m_set( false )
            {
            }

            void set()
            {
                BL_MUTEX_GUARD( m_lock );

                m_set = true;

                m_cv.notify_all();
            }

            bool wait( SAA_in const std::size_t timeoutInMilliseconds = WAIT_IN_MILLISECONDS ) const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( timeoutInMilliseconds ),
                    [ this ]() -> bool
                    {
                        return m_set;
                    }
                    );
            }

        private:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            bool                                                                m_set;
        };

        /**
         * @brief The ordinary recording sink with ONE rendezvous added - the first body chunk
         *
         * utests/baselib/Http2DriverTestUtils.h's sink notifies only on onClosed( ), and what
         * these cases have to wait for is the moment the driver holds a live parser on a response
         * it has BEGUN. It delegates rather than derives, so every accessor on the recording sink
         * is unchanged and the case reads its verdict from that one
         */

        class BodyLatchSink : public bl::httpclient::ClientStreamEventSink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE(
                BodyLatchSink,
                bl::httpclient::ClientStreamEventSink
                )

        protected:

            typedef bl::httpclient::stream_handle_t                             stream_handle_t;

            bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >             m_inner;
            Latch                                                               m_firstChunk;

            BodyLatchSink()
            {
            }

        public:

            /**
             * @brief Handed over before the task is scheduled, so nothing here races the strand
             */

            void attachTo(
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& inner
                )
            {
                m_inner = bl::om::copy( inner );
            }

            bool waitForFirstChunk() const
            {
                return m_firstChunk.wait();
            }

            virtual void onBodyWanted(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                m_inner -> onBodyWanted( handle, bytes );
            }

            virtual void onHeaders(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                m_inner -> onHeaders( handle, status, BL_PARAM_FWD( headers ), isInterim );
            }

            virtual void onData(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    data
                ) OVERRIDE
            {
                m_inner -> onData( handle, data );

                m_firstChunk.set();
            }

            virtual void onTrailers(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          bl::http::HeaderList&&                          trailers
                ) OVERRIDE
            {
                m_inner -> onTrailers( handle, BL_PARAM_FWD( trailers ) );
            }

            virtual void onClosed(
                SAA_in          const stream_handle_t                           handle,
                SAA_in          const bl::eh::error_code&                       errorCode,
                SAA_in          const bool                                      isRetryable
                ) NOEXCEPT OVERRIDE
            {
                m_inner -> onClosed( handle, errorCode, isRetryable );
            }
        };

        typedef bl::om::ObjectImpl< BodyLatchSink >                            BodyLatchSinkImpl;

        /**
         * @brief The driver under test with a door onto its OWN strand - and nothing else
         *
         * It adds no behaviour: one public method which posts a handler to the executor the
         * connected stream was built on. That handler does NOT take the task lock, which is the
         * whole reason it exists - every other door onto this strand (the sink, the read handler,
         * the write handler) is inside BL_TASKS_HANDLER_BEGIN( ), and requestCancel( ) takes the
         * same lock, so a cancel issued while one of those holds the strand cannot land until the
         * read has already re-armed
         */

        class Http1TlsStrandProbe : public bl::tasks::Http1ConnectionTaskT< sessiontls::tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( Http1TlsStrandProbe )

        public:

            typedef bl::tasks::Http1ConnectionTaskT< sessiontls::tls_stream_t > base_type;

        protected:

            Http1TlsStrandProbe(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_inout       sessiontls::tls_stream_t::stream_ref&&          connectedStream,
                SAA_in          bl::httpclient::ConnectionKey                   key
                )
                :
                base_type(
                    BL_PARAM_FWD( negotiated ),
                    BL_PARAM_FWD( connectedStream ),
                    BL_PARAM_FWD( key )
                    )
            {
            }

        public:

            /**
             * @brief Runs 'body' on the driver's strand, holding it until 'body' returns
             */

            void holdStrand( SAA_in const bl::cpp::function< void () >& body )
            {
                const auto ref = base_type::selfRef();

                base_type::postToStreamExecutor(
                    [ ref, body ]() -> void
                    {
                        body();
                    }
                    );
            }

            /**
             * @brief Ends this connection the way a handler's OWN EPILOG ends it, and adds nothing
             *
             * closeConnection( ) then initiateClose( ) is exactly the pair MultiOperationTask runs
             * when beginClose( ) or a first error reaches onOperationCompleted( ) with
             * m_closeInitiated still false - the accounting is the only thing skipped, and the
             * accounting is not what the gate under test reads. Calling it from a strand handler is
             * also where it really runs: initiateClose( ) is documented as running in a handler's
             * epilog and nowhere else
             *
             * IT MUST BE CALLED ON THE STRAND, which holdStrand( ) is what provides. The cancel
             * inside initiateClose( ) reaps what is REGISTERED, so the whole value of calling it
             * from a held strand is that the composed read's intermediate handler is queued behind
             * the caller and there is nothing of that read to reap
             */

            void closeAsAnEpilogWould()
            {
                base_type::closeConnection();

                base_type::initiateClose();
            }
        };

        typedef bl::om::ObjectImpl< Http1TlsStrandProbe >                       Http1TlsStrandProbeImpl;

        /**
         * @brief makeHttp1TlsFactory( )'s sibling, building the probe above instead of the driver
         */

        inline auto makeProbeTlsFactory(
            SAA_in          const std::shared_ptr< bl::om::ObjPtr< Http1TlsStrandProbeImpl > >& slot
            )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< sessiontls::tls_stream_t > >
        {
            typedef bl::httpclient::ClientDriverFactoryT< sessiontls::tls_stream_t > factory_t;

            auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                bl::httpclient::HttpProtocol::Http11,
                [ slot ](
                    SAA_in      const bl::httpclient::NegotiatedProtocol&       negotiated,
                    SAA_inout   sessiontls::tls_stream_t::stream_ref&&          connectedStream,
                    SAA_in      const bl::httpclient::ConnectionKey&            key
                    )
                    -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    auto driver = Http1TlsStrandProbeImpl::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        bl::cpp::copy( key )
                        );

                    auto result = bl::om::qi< bl::httpclient::ClientConnection >( driver );

                    *slot = bl::om::copy( driver );

                    return result;
                }
                );

            return factory;
        }

        inline auto establishTlsProbeDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::om::ObjPtr< Http1TlsStrandProbeImpl >
        {
            using namespace bl;
            using namespace bl::tasks;

            httpclient::ConnectionKey key;

            key.scheme = "https";
            key.host = "localhost";
            key.port = port;

            const auto slot =
                std::make_shared< om::ObjPtr< Http1TlsStrandProbeImpl > >();

            const auto establisher = sessiontlsh1::TlsEstablisherImpl::createInstance(
                std::move( key ),
                makeProbeTlsFactory( slot ),
                ProxyConfig::none(),
                ClientConnectionConfig(),
                false /* logExceptions */
                );

            const auto establisherTask = om::qi< Task >( establisher );

            eq -> push_back( establisherTask );
            eq -> wait( establisherTask );

            h2driver::chkTaskSucceeded( establisherTask );

            UTF_REQUIRE( nullptr != slot -> get() );

            return om::copy( *slot );
        }

        /**
         * @brief What the exchange settled on, read on the test thread
         */

        struct TlsCancelResult
        {
            bl::eh::error_code                                                  errorCode;
            unsigned                                                            status;
            std::string                                                         body;
            std::string                                                         events;
            std::string                                                         peerRecords;
            bl::eh::error_code                                                  peerEndCode;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            TlsCancelResult()
                :
                status( 0U ),
                taskFailed( false )
            {
            }
        };

        inline auto joinRecords( SAA_in const std::vector< std::string >& records ) -> std::string
        {
            std::string result;

            for( std::size_t i = 0U; i < records.size(); ++i )
            {
                if( ! result.empty() )
                {
                    result += "|";
                }

                result += records[ i ];
            }

            return result;
        }

        /**
         * @brief The five octets which leave the client's TLS engine wanting more
         *
         * A record header and no payload: application_data, the TLS 1.2 record version every
         * TLS 1.3 record still carries, and a length of 32 octets which never arrive
         */

        inline auto partialRecordHeader() -> std::string
        {
            std::string header;

            header.push_back( static_cast< char >( 0x17 ) );
            header.push_back( static_cast< char >( 0x03 ) );
            header.push_back( static_cast< char >( 0x03 ) );
            header.push_back( static_cast< char >( 0x00 ) );
            header.push_back( static_cast< char >( 0x20 ) );

            return header;
        }

        /**
         * @brief The one exchange all three cases run, whichever door the teardown comes through
         *
         * THE STEPS, AND WHICH OF THEM IS A RENDEZVOUS.
         *
         *   1. the peer answers the request head with a CLOSE-DELIMITED response and one body
         *      chunk, and then stops - holding a second chunk it never sends
         *   2. the case waits for that chunk to reach the sink, which is a happens-before with the
         *      driver holding a live parser. With a body in flight it then leaves the strand alone
         *      for the composed write to park
         *   3. the probe takes the strand. While it is held the teardown is issued - either
         *      requestCancel( ) from the case, which posts shutdownOnStreamExecutor( ) BEHIND the
         *      probe, or the first hold's own closeConnection( ) plus initiateClose( ) - and the
         *      peer is released, which puts five octets of a record header on the wire. The armed
         *      read takes them in the reactor and its intermediate handler queues behind the probe,
         *      so when the teardown's shutdown_send and cancel( ) finally run there is no read op
         *      registered at all
         *   4. the probe takes the strand a SECOND time, so that the ending our own FIN provokes is
         *      observed while the write handler the teardown reaped is still queued. Without a body
         *      in flight nothing was reaped and the second hold changes nothing; with one, it is
         *      what puts the write's completion - and, on the cancel's door, the m_closing that
         *      completion sets - ahead of the read's ending, which is face 1
         *   5. the peer reads its stream to the end and only THEN closes, so the ending is the one
         *      our teardown provoked and not a close the peer chose
         */

        /**
         * @brief Which door the teardown comes through - the one axis the three cases differ on
         */

        enum Teardown
        {
            /**
             * @brief requestCancel( ), which sets m_isCanceled and leaves m_closing alone
             */

            ByExternalCancel,

            /**
             * @brief The epilog's own closeConnection( ) plus initiateClose( ), which sets
             * m_closing and leaves m_isCanceled alone - the only door which separates face 1's
             * question from face 2's
             */

            ByDeliberateClose,
        };

        inline auto runTeardownDuringCloseDelimitedResponse(
            SAA_in          const Teardown                                      teardown,
            SAA_in          const bool                                          isWriteInFlight
            )
            -> TlsCancelResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::sessiontlsh1;

            TlsCancelResult result;

            const std::string chunkOne( "part-one" );

            Latch peerPartialSent;
            Latch peerMayDrain;
            Latch peerClosed;

            /*
             * AT FUNCTION SCOPE AND NOT INSIDE THE PARALLEL BLOCK, so that they outlive the
             * execution queue which owns the handlers blocked on them. A chkOrFail( ) inside that
             * block throws, the queue is torn down on the way out, and a hold still parked in
             * wait( ) returns through its condition variable AFTER the block's own locals are
             * gone - so latches declared in there would be destroyed under a live waiter, and the
             * run that told us something had gone wrong would end in undefined behaviour instead
             * of a diagnosis. No measured run took that path; this is where it would have bitten
             */

            Latch firstHoldEntered;
            Latch firstHoldRelease;
            Latch secondHoldEntered;
            Latch secondHoldRelease;

            Http1TlsPeer peer(
                [ &chunkOne, &peerPartialSent, &peerMayDrain, &peerClosed ](
                    SAA_inout   Http1TlsPeer&                                   self,
                    SAA_inout   Http1TlsPeer::sslstream_t&                      stream
                    ) -> void
                {
                    const auto head = self.readRequestHead( stream );

                    self.record( "head:" + Http1TlsPeer::requestLineOf( head ) );

                    /*
                     * NO Content-Length, so the close is the only framing there is (RFC 9112
                     * section 6.3) and whether this message may be declared complete is decided
                     * entirely by HOW the byte stream ended. The second chunk is never sent, so a
                     * message declared complete here is a message declared complete on a body the
                     * peer had not finished
                     */

                    Http1TlsPeer::send(
                        stream,
                        "HTTP/1.1 200 OK\r\n"
                        "\r\n" +
                        chunkOne
                        );

                    self.record( "chunk-one:sent" );

                    self.waitForRelease();

                    /*
                     * ON THE TRANSPORT AND NOT THROUGH THE ENGINE, because what this has to be is
                     * an INCOMPLETE record - see the header comment. It is written after the case
                     * has taken the strand, so the client's armed read takes it in the reactor and
                     * its intermediate handler waits
                     */

                    {
                        eh::error_code ec;

                        const auto header = partialRecordHeader();

                        ( void ) asio::write(
                            stream.next_layer(),
                            asio::buffer( header ),
                            ec
                            );

                        self.record( ec ? "partial:failed" : "partial:sent" );
                    }

                    peerPartialSent.set();

                    /*
                     * AND IT DOES NOT READ ONE OCTET UNTIL THE CANCEL HAS RUN, which is what keeps
                     * a request write PARKED IN THE REACTOR for cancel( ) to reap. Measured
                     * without this gate: the peer began draining while the strand was held, the
                     * composed write woke, its next step was a handler QUEUED on that strand
                     * rather than an op registered with the reactor, the cancel found nothing to
                     * reap and the write later completed broken_pipe - which A2 excuses, so
                     * m_closing was never set from the write handler and the run was face 2 a
                     * second time rather than face 1
                     */

                    ( void ) peerMayDrain.wait();

                    /*
                     * IT READS TO THE END OF ITS STREAM BEFORE IT CLOSES, which is what makes the
                     * ending the client sees OUR OWN. Nothing the peer decides ends this
                     * connection; the FIN of cancelTask( )'s shutdown_send does, and this is the
                     * peer answering it. With a body in flight this also drains the part of the
                     * upload which did go out, because the FIN is queued behind it
                     */

                    self.observeStreamEnd( stream );

                    self.record( "peer-end:" + Http1TlsPeer::describe( self.streamEndCode() ) );

                    {
                        eh::error_code ec;

                        stream.next_layer().close( ec );
                    }

                    self.record( "closed" );

                    peerClosed.set();
                }
                );

            const auto sink = h2driver::RecordingSink::createInstance();
            const auto latched = BodyLatchSinkImpl::createInstance();

            latched -> attachTo( om::qi< httpclient::ClientStreamEventSink >( sink ) );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto probe = establishTlsProbeDriver( eq, peer.port() );

                    const auto driver = om::qi< httpclient::ClientConnection >( probe );
                    const auto driverTask = om::qi< Task >( probe );

                    eq -> push_back( driverTask );

                    auto request = sessiontls::makeRequest(
                        peer.port(),
                        "/cut-short",
                        isWriteInFlight ? "POST" : "GET"
                        );

                    if( isWriteInFlight )
                    {
                        const auto block =
                            data::DataBlock::createInstance(
                                static_cast< std::size_t >( BLOCKED_BODY_SIZE )
                                );

                        std::memset(
                            block -> pv(),
                            'x',
                            static_cast< std::size_t >( BLOCKED_BODY_SIZE )
                            );

                        block -> setSize( static_cast< std::size_t >( BLOCKED_BODY_SIZE ) );

                        request.body( om::ObjPtrCopyable< data::DataBlock >( block ) );
                    }

                    const auto handle = driver -> submit(
                        request,
                        om::qi< httpclient::ClientStreamEventSink >( latched )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    chkOrFail(
                        latched -> waitForFirstChunk(),
                        "the first body chunk never reached the sink, so the driver never held a "
                            "live parser on a response it had begun; the sink recorded: " +
                            joinRecords( sink -> records() )
                        );

                    if( isWriteInFlight )
                    {
                        os::sleep(
                            time::milliseconds(
                                static_cast< long >( WRITE_PARKS_IN_MILLISECONDS )
                                )
                            );
                    }

                    probe -> holdStrand(
                        [ & ]() -> void
                        {
                            firstHoldEntered.set();

                            ( void ) firstHoldRelease.wait();

                            /*
                             * POSTED FROM INSIDE THE FIRST HOLD, so that it lands behind the
                             * teardown's own post and behind the read's intermediate handler, and
                             * ahead of the write completion the teardown is about to produce
                             *
                             * AND AHEAD OF THE CLOSE BELOW, WHICH IS NOT AN ORDERING OF
                             * CONVENIENCE. initiateClose( ) reaps the parked write SYNCHRONOUSLY,
                             * so a second hold posted after it would land BEHIND that write's
                             * completion - and the write's own epilog runs initiateClose( ) a
                             * second time, whose cancel would then reap the read this case needs
                             * to survive. Posted first, the order on the strand is the read's
                             * intermediate handler, this hold, and only then the write
                             */

                            probe -> holdStrand(
                                [ & ]() -> void
                                {
                                    secondHoldEntered.set();

                                    ( void ) secondHoldRelease.wait();
                                }
                                );

                            if( ByDeliberateClose == teardown )
                            {
                                /*
                                 * THE ONLY DOOR WHICH SETS m_closing WITHOUT SETTING m_isCanceled,
                                 * and therefore the only one which asks face 1's question on its
                                 * own. It is issued HERE, on a held strand with the read's
                                 * intermediate handler already queued, because that is the state
                                 * section 3.1 describes: initiateClose( ) runs in an epilog and
                                 * its cancel finds nothing of a composed read to reap
                                 */

                                probe -> closeAsAnEpilogWould();
                            }
                        }
                        );

                    chkOrFail(
                        firstHoldEntered.wait(),
                        "the probe never took the driver's strand"
                        );

                    /*
                     * THE CANCEL, ISSUED WHILE THE STRAND IS HELD BY A HANDLER WHICH TAKES NO TASK
                     * LOCK - which is the only way it can land ahead of the read's next arm. The
                     * other door needs no help from this thread at all: it is a call the hold
                     * makes for itself, once the read's window is open
                     */

                    if( ByExternalCancel == teardown )
                    {
                        driverTask -> requestCancel();
                    }

                    peer.release();

                    chkOrFail(
                        peerPartialSent.wait(),
                        "the peer never put the partial record on the wire"
                        );

                    os::sleep(
                        time::milliseconds( static_cast< long >( SETTLE_IN_MILLISECONDS ) )
                        );

                    firstHoldRelease.set();

                    chkOrFail(
                        secondHoldEntered.wait(),
                        "the probe never took the driver's strand a second time"
                        );

                    /*
                     * THE CANCEL HAS RUN BY NOW - the second hold was posted from inside the first
                     * and therefore behind shutdownOnStreamExecutor( ), so a strand which is
                     * running it has already shut our send side down and reaped what was
                     * registered. Only now may the peer start reading
                     */

                    peerMayDrain.set();

                    chkOrFail(
                        peerClosed.wait(),
                        "the peer never closed, so the ending this case is about never happened"
                        );

                    os::sleep(
                        time::milliseconds( static_cast< long >( SETTLE_IN_MILLISECONDS ) )
                        );

                    secondHoldRelease.set();

                    sink -> waitForClosed( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    eq -> wait( driverTask );

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = h2driver::exceptionMessageOf( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            result.errorCode = sink -> errorCode();
            result.status = sink -> status();
            result.body = sink -> body();
            result.events = joinRecords( sink -> records() );
            result.peerRecords = joinRecords( peer.records() );
            result.peerEndCode = peer.streamEndCode();

            return result;
        }

        /**
         * @brief What all three cases assert, and each line of it refuses a different outcome
         *
         *   - the status and the body say the arrangement HAPPENED: the response head and one body
         *     chunk reached the sink, so the driver held a live parser when the ending arrived and
         *     a fix which failed the request before either would pass here vacuously otherwise.
         *     'part-one' exactly is also what says nothing was lost - the case is about the
         *     completion rule and not about octets the teardown discarded
         *   - the peer's own ending code says the ending was OURS: a truncated TLS stream is a
         *     transport which ended with no close_notify, which is the forceful shutdown_send
         *     either door reaches; an eof there would mean the client had closed in an orderly way
         *     and this case would be about a peer's close rather than about our teardown
         *   - AND THE ONE THIS EXISTS FOR: the stream's verdict. A message framed by an ending we
         *     caused must not be declared complete, and which error a correct tree reports instead
         *     is deliberately not pinned - asserting one particular code would fix this case to
         *     today's answer rather than to the defect
         */

        inline void chkNotReportedComplete(
            SAA_in          const TlsCancelResult&                              result,
            SAA_in          const std::string&                                  which
            )
        {
            const auto what =
                ". status " +
                bl::utils::lexical_cast< std::string >( result.status ) +
                ", body " +
                bl::str::quoteString( result.body ) +
                ", sink " +
                result.events +
                ", peer " +
                result.peerRecords +
                ", task " +
                ( result.taskFailed ? result.taskFailure : std::string( "clean" ) );

            sessiontlsh1::chkOrFail(
                200U == result.status,
                which + ": the response head never reached the sink" + what
                );

            sessiontlsh1::chkOrFail(
                std::string( "part-one" ) == result.body,
                which + ": the body the peer did send is not what the sink holds" + what
                );

            sessiontlsh1::chkOrFail(
                sessiontlsh1::Http1TlsPeer::isTruncated( result.peerEndCode ),
                which +
                    ": the peer's stream did not end TRUNCATED, so the ending this case is about "
                    "was not our own forceful teardown" +
                    what
                );

            sessiontlsh1::chkOrFail(
                !! result.errorCode,
                which +
                    ": a close-delimited response cut short by our own teardown was reported to "
                    "the caller as a COMPLETE success" +
                    what
                );
        }

    } // tlsh1cancel

} // utest

/**
 * @brief FACE 2 - an external cancel, and an ending which is ours
 *
 * WHAT IT ESTABLISHES. cancelTask( ) shuts our send side down and cancels the socket; the peer
 * answers our FIN by closing; the composed TLS read re-arms past that cancel and is handed
 * stream_truncated - which isCleanEndOfStream( ) admits ON PURPOSE, because a truncated TLS stream
 * is the ordinary shape of a close-delimited HTTPS response (RFC 2818 section 2.2.2). The read
 * handler's end-of-stream arm sits AHEAD of its CHK_CANCEL_IMPL( ), so nothing asks whether the
 * caller has cancelled, and parseEof( ) completes a body the peer had not finished.
 *
 * THE DISCRIMINATOR IS THE VERDICT AND NOT THE CODE. What is wrong here is that the caller is told
 * the message is complete; which error a correct tree reports instead is a separate question, and
 * asserting one particular code would pin this case to today's answer rather than to the defect.
 *
 * m_closing IS FALSE THROUGHOUT, which is what makes this face 2 and not face 1: its writers are
 * beginClose( ), onOperationCompleted( ) and scheduleNothrow( ), and no operation has failed. The
 * ! isClosing( ) gate cannot see this ending, which is what design section 3.2 point 3 says in as
 * many words.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_CancelledReadCompletesACutShortBodyTests )
{
    using namespace bl;
    using namespace utest::tlsh1cancel;

    const auto result =
        runTeardownDuringCloseDelimitedResponse( ByExternalCancel, false /* isWriteInFlight */ );

    chkNotReportedComplete( result, "face 2, the external cancel" );
}

/**
 * @brief FACE 1 - our own teardown, and an ending which is ours
 *
 * THE SAME EXCHANGE WITH A REQUEST BODY THE PEER NEVER READS, and that one difference is what
 * moves it from face 2 to face 1: the cancel reaps the parked composed write, its operation_aborted
 * is neither our own teardown - m_closing is still false when it runs - nor a code
 * net::isPeerClosedOnWriteErrorCode( ) admits, so it reaches CHK_EC( ), becomes the task's first
 * error and sets m_closing. The read then observes the ending our own shutdown_send provoked with
 * isClosing( ) TRUE and a live parser, which is design section 3.1 exactly.
 *
 * WHY THE SECOND STRAND HOLD IS LOAD-BEARING HERE AND INERT IN THE SIBLING. initiateClose( ) runs
 * INLINE in the epilog of the handler which set m_closing - the write's - and its cancel( ) would
 * reap a read which had already re-armed. The second hold is what lets the ending be observed by
 * the reactor while that write handler is still queued, so the cancel finds nothing and the read
 * handler runs after it with isClosing( ) already true.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_ClosingReadCompletesACutShortBodyTests )
{
    using namespace bl;
    using namespace utest::tlsh1cancel;

    const auto result =
        runTeardownDuringCloseDelimitedResponse( ByExternalCancel, true /* isWriteInFlight */ );

    chkNotReportedComplete( result, "face 1, our own teardown" );
}

/**
 * @brief FACE 1 ON ITS OWN - a deliberate close, with no cancel anywhere near it
 *
 * WHAT THE OTHER TWO CASES CANNOT SAY. Both reach their ending with isCanceled( ) true, because
 * after A2 the external cancel is the only thing a FIXTURE can arrange which fails the task from
 * the write handler with a response still in flight - so the cancel check alone turns BOTH of them
 * green and face 1's gate has no red of its own between them. This case removes the cancel: the
 * teardown is closeConnection( ) plus initiateClose( ), the pair a handler's epilog runs, so
 * m_closing is true and m_isCanceled is false when the read observes the ending. The cancel check
 * cannot see it and only the ! isClosing( ) gate can, which is what makes this face 1's OWN red.
 *
 * AND THE ENDING IS STILL OURS. initiateClose( ) shuts the send side down because a write is in
 * flight; the peer answers that FIN by reading to the end of its stream and closing, holding the
 * rest of a response it never finished. Without the write there would be no shutdown_send, no FIN
 * and no ending at all - which is why this case cannot be the GET the sibling case is.
 *
 * WHAT IT DOES NOT CLAIM, and the claim would be false. It is not evidence that anything on
 * today's tree PRODUCES this state: the probe reaches it by calling what an epilog calls rather
 * than by arranging a defect, and the write codes which would - timed_out, or an error out of the
 * TLS engine - are the ones no loopback peer can raise. What it pins is the GATE - remove
 * ! isClosing( ) and this case fails - so the redundancy the other two measure is a fact with a
 * test behind it rather than a note somebody must remember to re-check.
 */

UTF_AUTO_TEST_CASE( Http1DriverTls_ClosingWithoutACancelCompletesACutShortBodyTests )
{
    using namespace bl;
    using namespace utest::tlsh1cancel;

    const auto result =
        runTeardownDuringCloseDelimitedResponse( ByDeliberateClose, true /* isWriteInFlight */ );

    chkNotReportedComplete( result, "face 1 alone, a deliberate close" );
}

#endif /* __UTEST_TESTHTTP1DRIVERTLSCANCELCLOSE_H_ */
