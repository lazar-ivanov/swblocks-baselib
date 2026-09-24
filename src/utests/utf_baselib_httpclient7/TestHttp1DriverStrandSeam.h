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

#ifndef __UTEST_TESTHTTP1DRIVERSTRANDSEAM_H_
#define __UTEST_TESTHTTP1DRIVERSTRANDSEAM_H_

#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <utests/baselib/Http1DriverTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * H01 - THE DETERMINISTIC CONTROL FOR THE SPURIOUS REUSE REFUSAL, AND ITS NEGATIVE CONTROL
 *
 * The defect is a race between two completions inside our own scheduler - the write's and the
 * read's. The read wins, finishStream( ) reads a stale m_isWriteInFlight and refuses to reuse a
 * connection with nothing whatever wrong with it. No peer-side lever reaches that window: every
 * knob a test has moves the WRITE, and the racing party is the scheduler thread carrying the
 * write's completion. What CAN be steered is the stream the driver writes through, because the
 * driver is a template over its stream policy.
 *
 * THE SEAM. A test-only policy hands the driver a stream object of its own instead of the socket.
 * That object performs the real write on the socket and then HOLDS the completion, releasing it
 * onto the socket's executor at a moment the case chooses. Releasing it from inside the read's own
 * completion, before the driver's read handler runs, makes the window certain: the driver reaches
 * finishStream( ) with m_isWriteInFlight still true, and the write's completion is sitting in the
 * strand's queue behind the handler that is running.
 *
 * WHAT THE TWO CASES ARE. They are the two bands the fix's one strand hop separates, and between
 * them they state both what it does and what it does not do:
 *
 *   - released EARLY, inside the read's completion: the deferred verdict lands behind the write's
 *     completion, finds the write done and publishes Ready. THE RED - against a tree without the
 *     deferral this case fails, because that tree publishes Draining
 *   - released LATE, forty strand hops on: the deferred verdict runs first and finds the write
 *     still in flight, so the verdict is exactly today's. GREEN ON BOTH SIDES - it is what pins
 *     that the fix does not overreach into the case the refusal is right for
 *
 * WHY 'released late' IS NOT A WEAKER 'released early'. They are physically different populations
 * and the measurement says so: for a covered occurrence the write handler ran a median 51us after
 * the post, and the uncovered ones were still off-strand eight immediate hops later. This case
 * stands for the second, which is also the shape of the two write-barrier cases in this module -
 * a write genuinely blocked behind 8MB against a parked peer.
 *
 * See notes/plans/issues/h01-reuse-verdict-design.md, sections 8.5 and 11.
 */

namespace utest
{
    namespace http1seam
    {
        enum : std::size_t
        {
            /**
             * @brief How many strand hops the LATE variant waits before releasing the write
             *
             * Ordering rather than timing decides it: the strand is FIFO, so a release enqueued at
             * hop 40 cannot run before a continuation enqueued at hop 1. No sleep and no timer is
             * involved, and the number only has to be comfortably more than one
             */

            LATE_RELEASE_HOPS                   = 40U,
        };

        /**
         * @brief The seam's control block - process global, exactly as the h2 accounting seam's
         *
         * One connection at a time is all these cases run, and a global is what a stream policy
         * built by a factory can reach: the driver is created by ClientDriverFactoryT and there
         * is no path by which a case could hand it a per-connection control
         */

        struct SeamControl
        {
            std::atomic< bool >                                                 holdNextWrite;
            std::atomic< bool >                                                 releaseArmed;
            std::atomic< bool >                                                 releaseEarly;

            /**
             * @brief The code the held write completion is released WITH, in place of its own
             *
             * The seam already owns the completion, so handing it a different code costs nothing
             * and is the only way to reach the driver's 'the write ended badly' branch on demand -
             * see the third case. It is a code injected at the stream, not a reset on the wire,
             * and the case says so
             */

            std::atomic< bool >                                                 releaseAsReset;

            /**
             * @brief The NEXT read arm fails in its initiator, and only that one
             *
             * A SECOND ARM ON THE SAME SEAM, used by no case above. It is here rather than in a
             * stream of its own because a second stream means a second driver instantiation in
             * this module's one translation unit, and this stream is already the thing the driver
             * arms its reads on - see TestHttp1DriverScheduleThrow.h, which is the only arm of it
             *
             * One shot, because the driver re-arms the read from its own handler for the life of
             * the connection and a seam which stayed armed would fail every one of those too
             */

            std::atomic< bool >                                                 throwOnNextReadArm;

            mutable bl::os::mutex                                               lock;
            bl::cpp::void_callback_t                                            held;

            /**
             * @brief A read completion parked until the write's has been captured
             *
             * THE ONE ORDERING THIS SEAM MUST NOT LEAVE TO CHANCE, and leaving it to chance HUNG
             * this module about one run in ten. Both completions arrive on the strand, and which
             * arrives first is exactly H01's own race: when the READ won, the release below ran
             * with nothing held and consumed its arming, the write's completion was then captured
             * with nobody left to release it, and the composed asio::async_write never finished -
             * so the driver's write operation was never accounted, MultiOperationTask could never
             * reach a zero pending count, and the task could never take its terminal path. A hang,
             * not a failure, with an idle reactor and every thread parked.
             *
             * A RENDEZVOUS AND NOT A BOUND. The read's delivery is parked here and the write's
             * capture posts it, so "write captured, then read delivered" holds by construction
             * with no spin, no timer and nothing to tune. The write's completion is always coming:
             * the peer cannot answer until it has read the whole request, so the socket write has
             * finished by the time any response exists and only its handler is in flight
             */

            bl::cpp::void_callback_t                                            pendingRead;

            SeamControl()
                :
                holdNextWrite( false ),
                releaseArmed( false ),
                releaseEarly( true ),
                releaseAsReset( false ),
                throwOnNextReadArm( false )
            {
            }
        };

        inline auto seam() NOEXCEPT -> SeamControl&
        {
            static SeamControl control;

            return control;
        }

        inline void armSeam(
            SAA_in          const bool                                          releaseEarly,
            SAA_in          const bool                                          releaseAsReset = false
            )
        {
            BL_MUTEX_GUARD( seam().lock );

            seam().held = bl::cpp::void_callback_t();
            seam().pendingRead = bl::cpp::void_callback_t();
            seam().holdNextWrite = true;
            seam().releaseArmed = true;
            seam().releaseEarly = releaseEarly;
            seam().releaseAsReset = releaseAsReset;
        }

        /**
         * @brief Posts the held write completion onto the strand, if one is held
         */

        inline void releaseHeldWrite( SAA_in const bl::asio::ip::tcp::socket::executor_type& ex )
        {
            bl::cpp::void_callback_t held;

            {
                BL_MUTEX_GUARD( seam().lock );

                held = seam().held;

                seam().held = bl::cpp::void_callback_t();
            }

            if( held )
            {
                bl::asio::post( ex, held );
            }
        }

        /**
         * @brief The LATE variant's delay - a chain of strand hops, released at the end of it
         *
         * It holds no reference to the driver task deliberately: an executor is copyable and
         * outlives the socket, so the chain cannot dangle on a task which has already ended
         */

        struct HopChain
        {
            /*
             * NOT NAMED executor_type. That name is asio's trait for a handler which carries its
             * OWN executor, and a handler which declares it must answer get_executor( ) too -
             * this one is posted to an executor rather than carrying one
             */

            bl::asio::ip::tcp::socket::executor_type                            ex;
            std::size_t                                                         remaining;

            void operator()() const
            {
                if( 0U == remaining )
                {
                    releaseHeldWrite( ex );

                    return;
                }

                HopChain next = { ex, remaining - 1U };

                bl::asio::post( ex, next );
            }
        };

        /**
         * @brief The stream the driver writes through - the socket, with the write's completion
         * interceptable
         *
         * It models only what this driver asks of its stream: get_executor( ), async_write_some( )
         * and async_read_some( ). asio::async_write( ) is a composed loop over the first of
         * those, so holding the completion of one step holds the whole composed write - and the
         * driver's own handler, which is what clears m_isWriteInFlight, never runs until we let
         * it
         */

        class HeldWriteStream
        {
        public:

            typedef bl::asio::ip::tcp::socket::executor_type                    executor_type;

            HeldWriteStream( SAA_in bl::asio::ip::tcp::socket& socket ) NOEXCEPT
                :
                m_socket( socket )
            {
            }

            auto get_executor() NOEXCEPT -> executor_type
            {
                return m_socket.get_executor();
            }

            template
            <
                typename ConstBufferSequence,
                typename Handler
            >
            void async_write_some(
                SAA_in          const ConstBufferSequence&                      buffers,
                SAA_in          Handler&&                                       handler
                )
            {
                typedef typename std::decay< Handler >::type                    handler_t;

                const auto shared =
                    std::make_shared< handler_t >( std::forward< Handler >( handler ) );

                HeldWriteStream* const self = this;

                m_socket.async_write_some(
                    buffers,
                    [ self, shared ](
                        SAA_in      const bl::eh::error_code&                   ec,
                        SAA_in      const std::size_t                           bytesTransferred
                        ) -> void
                    {
                        /*
                         * THE CODE IS SUBSTITUTED HERE AND NOWHERE ELSE, so a case which does not
                         * ask for one gets the transport's own byte for byte
                         */

                        const auto released = ( ! ec && seam().releaseAsReset.load() ) ?
                            bl::eh::error_code( bl::asio::error::connection_reset )
                            :
                            ec;

                        self -> onWriteSomeCompleted(
                            [ shared, released, bytesTransferred ]() -> void
                            {
                                ( *shared )( released, bytesTransferred );
                            }
                            );
                    }
                    );
            }

            template
            <
                typename MutableBufferSequence,
                typename Handler
            >
            void async_read_some(
                SAA_in          const MutableBufferSequence&                    buffers,
                SAA_in          Handler&&                                       handler
                )
            {
                /*
                 * BEFORE THE SOCKET IS TOUCHED AND BEFORE THE HANDLER IS COPIED, because what the
                 * armed case is about is an initiator which fails having started nothing - the
                 * allocation an initiating call makes is the only thing that can throw out of a
                 * real one, and no test can arrange that from the outside
                 */

                if( seam().throwOnNextReadArm.exchange( false ) )
                {
                    BL_THROW(
                        bl::UnexpectedException(),
                        BL_MSG()
                            << "The read initiator was made to fail"
                        );
                }

                typedef typename std::decay< Handler >::type                    handler_t;

                const auto shared =
                    std::make_shared< handler_t >( std::forward< Handler >( handler ) );

                HeldWriteStream* const self = this;

                m_socket.async_read_some(
                    buffers,
                    [ self, shared ](
                        SAA_in      const bl::eh::error_code&                   ec,
                        SAA_in      const std::size_t                           bytesTransferred
                        ) -> void
                    {
                        /*
                         * ON THE STRAND, AND AHEAD OF THE DRIVER'S OWN READ HANDLER. The release
                         * onReadSomeCompleted( ) enqueues is therefore in front of anything that
                         * handler posts, which is the whole of what makes the window certain -
                         * but only once the write's completion is here to be released, which is
                         * what deliverRead( ) waits for
                         */

                        self -> deliverRead(
                            [ self, shared, ec, bytesTransferred ]() -> void
                            {
                                self -> onReadSomeCompleted();

                                ( *shared )( ec, bytesTransferred );
                            }
                            );
                    }
                    );
            }

            /**
             * @brief Delivers a read completion, once the write's has been captured
             *
             * See SeamControl::pendingRead for why this is not optional. Both completions run on
             * the strand, so the test-and-park below is atomic against the capture it races
             */

            void deliverRead( SAA_in bl::cpp::void_callback_t delivery )
            {
                {
                    BL_MUTEX_GUARD( seam().lock );

                    if( seam().releaseArmed.load() && seam().holdNextWrite.load() )
                    {
                        BL_LOG(
                            bl::Logging::trace(),
                            BL_MSG()
                                << "SEAM parking the read - the write's completion is not here yet"
                            );

                        seam().pendingRead = delivery;

                        return;
                    }
                }

                delivery();
            }

            void onWriteSomeCompleted( SAA_in bl::cpp::void_callback_t completion )
            {
                if( ! seam().holdNextWrite.exchange( false ) )
                {
                    completion();

                    return;
                }

                bl::cpp::void_callback_t pendingRead;

                {
                    BL_MUTEX_GUARD( seam().lock );

                    seam().held = completion;

                    pendingRead = seam().pendingRead;

                    seam().pendingRead = bl::cpp::void_callback_t();
                }

                if( pendingRead )
                {
                    bl::asio::post( m_socket.get_executor(), pendingRead );
                }
            }

            void onReadSomeCompleted()
            {
                if( ! seam().releaseArmed.exchange( false ) )
                {
                    return;
                }

                if( seam().releaseEarly )
                {
                    releaseHeldWrite( m_socket.get_executor() );

                    return;
                }

                HopChain chain =
                {
                    m_socket.get_executor(),
                    static_cast< std::size_t >( LATE_RELEASE_HOPS )
                };

                bl::asio::post( m_socket.get_executor(), chain );
            }

        private:

            bl::asio::ip::tcp::socket&                                          m_socket;
        };

        /**
         * @brief The cleartext stranded policy, with getStream( ) answering the seam
         *
         * Hiding rather than overriding, which is the mechanism the stranded policy itself uses
         * for createSocket( ): the stream policy is a static interface resolved by template
         * composition, so the hiding definition is the one the driver finds. stream_t and
         * stream_ref are deliberately NOT redefined - the establisher creates and hands over a
         * real socket, and this policy only changes what the driver is given to write through
         *
         * AND onWriteCompleted( ) IS NOT REACHABLE ANY OTHER WAY. It is non-virtual and bound by
         * cpp::bind, so subclassing the driver cannot intercept it; the stream is the only lever
         */

        template
        <
            typename E = void
        >
        class HeldWritePolicyT : public bl::tasks::TcpSocketAsyncStrandedBase
        {
            BL_CTR_DEFAULT( HeldWritePolicyT, protected )
            BL_DECLARE_OBJECT_IMPL( HeldWritePolicyT )

        public:

            typedef HeldWritePolicyT< E >                                       this_type;
            typedef bl::tasks::TcpSocketAsyncStrandedBase                       base_type;

        protected:

            bl::cpp::SafeUniquePtr< HeldWriteStream >                           m_seamStream;

            auto getStream() NOEXCEPT -> HeldWriteStream&
            {
                if( ! m_seamStream )
                {
                    m_seamStream.reset( new HeldWriteStream( base_type::getSocket() ) );
                }

                return *m_seamStream;
            }
        };

        typedef HeldWritePolicyT<> HeldWritePolicy;

        typedef bl::tasks::Http1ConnectionTaskImpl< HeldWritePolicy >           SeamDriverImpl;

        /**
         * @brief makeHttp1Factory( )'s sibling, building the driver over the seam policy
         *
         * The factory's stream_ref is cpp::SafeUniquePtr< tcp::socket > whichever of the two
         * policies names it, because the seam policy inherits both typedefs unchanged - which is
         * what makes the establisher reusable as it stands
         */

        inline auto makeSeamFactory(
            SAA_in          const std::shared_ptr< bl::om::ObjPtr< bl::httpclient::ClientConnection > >& slot
            )
            -> std::shared_ptr< bl::httpclient::ClientDriverFactoryT< utest::http1driver::plain_stream_t > >
        {
            typedef bl::httpclient::ClientDriverFactoryT
            <
                utest::http1driver::plain_stream_t
            >
            factory_t;

            auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                bl::httpclient::HttpProtocol::Http11,
                [ slot ](
                    SAA_in      const bl::httpclient::NegotiatedProtocol&       negotiated,
                    SAA_inout   utest::http1driver::plain_stream_t::stream_ref&& connectedStream,
                    SAA_in      const bl::httpclient::ConnectionKey&            key
                    )
                    -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    auto driver = SeamDriverImpl::createInstance(
                        bl::cpp::copy( negotiated ),
                        BL_PARAM_FWD( connectedStream ),
                        bl::cpp::copy( key )
                        );

                    auto result = bl::om::qi< bl::httpclient::ClientConnection >( driver );

                    *slot = bl::om::copy( result );

                    return result;
                }
                );

            return factory;
        }

        inline auto establishSeamDriver(
            SAA_in          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&  eq,
            SAA_in          const bl::os::port_t                                port
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            const auto slot =
                std::make_shared< om::ObjPtr< httpclient::ClientConnection > >();

            const auto establisher = PlainEstablisherImpl::createInstance(
                makeKey( std::string( "127.0.0.1" ), port ),
                makeSeamFactory( slot ),
                ProxyConfig::none(),
                ClientConnectionConfig(),
                false /* logExceptions */
                );

            const auto establisherTask = om::qi< Task >( establisher );

            eq -> push_back( establisherTask );
            eq -> wait( establisherTask );

            chkTaskSucceeded( establisherTask );

            UTF_REQUIRE( nullptr != slot -> get() );

            return om::copy( *slot );
        }

        /**
         * @brief What the seam exchange settled on, read on the test thread
         */

        struct SeamResult
        {
            bl::httpclient::ConnectionState                                     state;
            std::size_t                                                         freeSlots;
            unsigned                                                            status;
            bool                                                                closed;
            bool                                                                taskFailed;
            std::string                                                         taskFailure;

            bl::eh::error_code                                                  errorCode;
            std::size_t                                                         closedEvents;

            SeamResult()
                :
                state( bl::httpclient::ConnectionState::Closed ),
                freeSlots( 0U ),
                status( 0U ),
                closed( false ),
                taskFailed( false ),
                closedEvents( 0U )
            {
            }
        };

        /**
         * @brief The recording sink, cancelling its own stream from inside onHeaders( )
         *
         * THE ONE THING THAT PUTS A CANCEL INSIDE THE ONE-HOP WINDOW, and it does so by ordering
         * rather than by timing. onHeaders( ) is delivered from deliverHeaders( ), inside the read
         * handler and before finishStream( ) is reached at all, so cancel( )'s post of
         * onCancelStream( ) is enqueued on the strand AHEAD of the continuation that same handler
         * is about to post. The window keeps m_handle allocated, which is what lets the cancel find
         * a stream to end at all - and what the continuation's handle guard then has to notice.
         *
         * It cancels on the FINAL header block only: a 1xx arrives through the same call, and
         * cancelling on one would end the stream before the response this exchange is about
         */

        class CancellingSink : public utest::http1driver::RecordingSink
        {
            BL_CTR_DEFAULT( CancellingSink, protected )
            BL_DECLARE_OBJECT_IMPL( CancellingSink )

        public:

            typedef utest::http1driver::RecordingSink                           base_type;

            bl::om::ObjPtr< bl::httpclient::ClientConnection >                  m_connection;

            virtual void onHeaders(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const unsigned                                  status,
                SAA_in          bl::http::HeaderList&&                          headers,
                SAA_in          const bool                                      isInterim
                ) OVERRIDE
            {
                base_type::onHeaders( handle, status, BL_PARAM_FWD( headers ), isInterim );

                if( m_connection && ! isInterim )
                {
                    /*
                     * THE HANDLE THE EVENT CARRIES, and not one the test thread stored, so there
                     * is no window in which this sink could cancel a handle submit( ) has not
                     * returned yet
                     */

                    m_connection -> cancel( handle, bl::eh::error_code() );
                }
            }
        };

        typedef bl::om::ObjectImpl< CancellingSink >                            CancellingSinkImpl;

        /**
         * @brief One ordinary keep-alive exchange, with the write's completion held across the
         * read's
         *
         * THE VERDICT IS READ AFTER onClosed( ) RETURNS, WHICH IS A RENDEZVOUS AND NOT A POLL, and
         * it is the same rendezvous every reuse case in utf_baselib_httpclient3 uses. It is also
         * what makes the assertions below mean what they say: the deferral moves the terminal
         * callback WITH the verdict, so a sink which has been told the stream ended is a sink
         * whose connection has already been published
         */

        inline auto runSeamExchange(
            SAA_in          const bool                                          releaseEarly,
            SAA_in          const bool                                          releaseAsReset = false,
            SAA_in          const bool                                          cancelOnHeaders = false
            )
            -> SeamResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace utest::http1driver;

            SeamResult result;

            ScriptedPeer peer(
                []( SAA_inout ScriptedPeer& self, SAA_inout asio::ip::tcp::socket& socket ) -> void
                {
                    const auto request = ScriptedPeer::readRequest( socket );

                    /*
                     * A complete final response saying nothing about the connection, so the only
                     * thing that can stand in the way of reuse is the write held by the seam
                     */

                    ScriptedPeer::send(
                        socket,
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n"
                        );

                    self.record( bl::cpp::copy( request ) );

                    self.waitForRelease();
                }
                );

            const auto sink = CancellingSinkImpl::createInstance();

            armSeam( releaseEarly, releaseAsReset );

            scheduleAndExecuteInParallel(
                [ &peer, &sink, &result, cancelOnHeaders ](
                    SAA_in      const om::ObjPtr< ExecutionQueue >&             eq
                    ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto driver = establishSeamDriver( eq, peer.port() );
                    const auto driverTask = om::qi< Task >( driver );

                    eq -> push_back( driverTask );

                    /*
                     * SET BEFORE submit( ), because after it the driver may already be delivering
                     */

                    if( cancelOnHeaders )
                    {
                        sink -> m_connection = om::copy( driver );
                    }

                    const auto handle = driver -> submit(
                        makeRequest( peer.port(), "/seam", "GET" ),
                        om::qi< httpclient::ClientStreamEventSink >( sink )
                        );

                    UTF_REQUIRE( httpclient::ClientConnection::INVALID_STREAM_HANDLE != handle );

                    result.closed = sink -> waitForClosed();

                    chkOrFail(
                        result.closed,
                        "the stream never ended; events so far: " + joinEvents( sink -> events() )
                        );

                    /*
                     * READ BEFORE THE PEER IS RELEASED, as every reuse case does - a connection
                     * the driver may reuse is one it leaves open
                     */

                    result.state = driver -> state();
                    result.freeSlots = driver -> freeStreamSlots();
                    result.status = sink -> finalStatus();
                    result.errorCode = sink -> errorCode();

                    peer.release();

                    eq -> wait( driverTask );

                    /*
                     * COUNTED HERE AND NOT ABOVE, AND THAT IS THE RENDEZVOUS RATHER THAN A DETAIL.
                     * The terminal event is owed exactly once, and a continuation publishing an
                     * ending for a stream somebody else had already ended is how a second one
                     * would appear - after the first. The continuation is an ACCOUNTED operation,
                     * so the task cannot end until it has run, which makes this wait exactly the
                     * event "everything that could deliver has delivered"
                     */

                    for( const auto& event : sink -> events() )
                    {
                        if( 0U == event.compare( 0U, 7U, "closed:" ) )
                        {
                            ++result.closedEvents;
                        }
                    }

                    result.taskFailed = driverTask -> isFailed();
                    result.taskFailure = taskFailureText( driverTask );

                    eq -> forceFlushNoThrow();
                }
                );

            UTF_REQUIRE_EQUAL( peer.failure(), std::string() );

            return result;
        }

    } // http1seam

} // utest

/**
 * @brief THE WINDOW MADE CERTAIN - and one strand hop lands behind the write
 *
 * The seam holds the write's completion until the read's own completion is running on the strand,
 * then posts it there. The driver reads the response and reaches finishStream( ) with
 * m_isWriteInFlight still true: H01, on demand, with no race and no rate.
 *
 * WITHOUT THE DEFERRAL this connection is refused - Draining, no free slot - although the response
 * is complete and final, says nothing about the connection, and the write it is refused for has
 * physically completed. THAT IS THIS CASE'S RED, and it is deterministic rather than measured.
 *
 * With the deferral the verdict is published one hop later, which is behind the write's completion
 * the seam has already enqueued, so the connection is reused.
 */

UTF_AUTO_TEST_CASE( Http1Driver_StrandSeamMakesTheDeferredVerdictReuseTests )
{
    using namespace bl;
    using namespace utest::http1seam;

    const auto result = runSeamExchange( true /* releaseEarly */ );

    UTF_REQUIRE( result.closed );
    UTF_REQUIRE_EQUAL( result.status, 200U );

    /*
     * THE TWO FIELDS TOGETHER ARE THE POOL'S OWN QUESTION, and submit( )'s as well: both ask for
     * Ready and an unallocated handle, which is what freeStreamSlots( ) is. A second submit( )
     * here would re-read the same two fields and buy nothing - the keep-alive reuse case in
     * utf_baselib_httpclient3 is the one that puts a real second request on a reused connection
     *
     * AND NEITHER READ RACES THE VERDICT. onClosed( ) is delivered from the continuation, after
     * the state is published - which is the order finishStream( ) has always stated and which the
     * deferral keeps by moving the callback with the verdict rather than ahead of it
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready == result.state );
    UTF_REQUIRE_EQUAL( result.freeSlots, 1U );

    UTF_CHECK( ! result.taskFailed );
}

/**
 * @brief THE SAME WINDOW, AND THE VERDICT THE REFUSAL IS RIGHT FOR - a hop which lands in front
 * of the write
 *
 * Identical in every respect but one: the write's completion is released forty strand hops later
 * instead of immediately, which is what a write that is GENUINELY still running looks like from
 * the strand - this module's two write-barrier cases, and the 5.5% of occurrences the coverage
 * measurement found still off-strand eight hops on.
 *
 * The deferred verdict therefore runs FIRST, finds the write still in flight and publishes exactly
 * today's: not reusable, close. GREEN ON BOTH SIDES OF THE FIX, deliberately - it is the control
 * that pins the deferral to the case it is for, and a red here would mean the deferral had started
 * excusing a write it cannot account for.
 */

UTF_AUTO_TEST_CASE( Http1Driver_StrandSeamLeavesAGenuineWriteRefusedTests )
{
    using namespace bl;
    using namespace utest::http1seam;

    const auto result = runSeamExchange( false /* releaseEarly */ );

    UTF_REQUIRE( result.closed );
    UTF_REQUIRE_EQUAL( result.status, 200U );

    /*
     * 'NOT Ready' AND NOT 'Draining'. Draining is what the verdict publishes, but the connection
     * it refused is closed immediately after and the task ends within microseconds, so which of
     * the two a read taken after onClosed( ) sees is a race with the teardown. The write-barrier
     * cases can assert Draining because their teardown waits on a write blocked behind 8MB; this
     * one's does not
     */

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );
    UTF_REQUIRE_EQUAL( result.freeSlots, 0U );

    UTF_CHECK( ! result.taskFailed );
}

/**
 * @brief THE WRITE'S OUTCOME AND NOT ITS FLAG - a connection the write handler learned was dead
 *
 * The window of the first case, with one thing changed: the held completion is released carrying
 * connection_reset instead of its own empty code. The write handler then takes the arm that
 * classifies a peer close on the write as an ENDING rather than a failure - it records the code,
 * clears m_isWriteInFlight, releases the buffers and closes NOTHING, leaving the classification to
 * the read. A deferred verdict which asked only the flag would find it clear, find nothing closing,
 * and publish Ready on a connection whose own write handler has just learned it is dead - for one
 * strand turn, and submit( ) accepts in one strand turn.
 *
 * THE CODE IS INJECTED AT THE STREAM AND IS NOT A RESET ON THE WIRE, and that is deliberate rather
 * than a shortcut: the branch under test is the DRIVER'S - what its verdict does with a write that
 * ended in a reset - and arranging a real RST to land on an 8MB upload while the read wins the
 * strand is a race, not a control. What a real reset adds beyond this is the transport's own
 * timing, which this case does not claim to cover; Http1Driver_PeerResetsWhileRequestWriteIsBlocked
 * Tests is the case that does.
 *
 * ITS RED IS THE ONE LINE IT EXISTS FOR - a continuation asking m_isWriteInFlight without
 * m_writeEndingCode beside it. Green against the tree before the deferral too, where the stale flag
 * refuses reuse for the wrong reason and gets the right answer by accident: that accident is what
 * the deferral removes, and this case is what keeps its removal from costing the answer.
 */

UTF_AUTO_TEST_CASE( Http1Driver_StrandSeamRefusesReuseAfterAResetWriteTests )
{
    using namespace bl;
    using namespace utest::http1seam;

    const auto result = runSeamExchange( true /* releaseEarly */, true /* releaseAsReset */ );

    /*
     * THE MESSAGE STILL COMPLETED, and it must: the response was whole before anything went wrong
     * with the request's write, and the write side classifies nothing
     */

    UTF_REQUIRE( result.closed );
    UTF_REQUIRE_EQUAL( result.status, 200U );

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );
    UTF_REQUIRE_EQUAL( result.freeSlots, 0U );

    UTF_CHECK( ! result.taskFailed );
}

/**
 * @brief A CANCEL THAT LANDS INSIDE THE ONE-HOP WINDOW - the continuation's handle guard
 *
 * The window keeps m_handle allocated on purpose, so that nothing can see the connection as
 * dispatchable before the verdict is published. The price of keeping it is that cancel( ) still
 * finds a stream to end there, and the sink here ends it: it calls cancel( ) from inside
 * onHeaders( ), which runs in the read handler BEFORE finishStream( ) is reached, so
 * onCancelStream( ) is enqueued on the strand ahead of the continuation by FIFO and nothing about
 * this is a race.
 *
 * WHAT THE DESIGN DECIDED, MADE OBSERVABLE. A cancel in the window WINS over a response which had
 * already completed: onCancelStream( ) ends the stream with the cancel's own code, and the sink is
 * told operation_aborted rather than success. That is what section 5.2.1 specified when it required
 * the continuation to carry the handle it was posted for - and it is a decision rather than an
 * accident, so it is worth a case instead of an argument.
 *
 * AND THE TERMINAL EVENT IS STILL OWED EXACTLY ONCE, which is the guard's own job: the continuation
 * finds m_handle no longer the one it was posted for and does nothing but account for itself.
 *
 * WHAT THIS CASE DOES NOT CLAIM. It is not a red for the guard LINE. On this tree the cancel's own
 * finishStream( ) has already taken the sink and left the connection closing, so a continuation
 * without the guard would find no sink to deliver to and would recompute the same Draining verdict -
 * the guard is defensive here rather than load bearing. What the case does is pin the decision and
 * keep the guard's branch exercised, so that a later change which makes the difference observable
 * fails here rather than in the field.
 */

UTF_AUTO_TEST_CASE( Http1Driver_StrandSeamCancelInTheWindowWinsTests )
{
    using namespace bl;
    using namespace utest::http1seam;

    const auto result = runSeamExchange(
        true  /* releaseEarly */,
        false /* releaseAsReset */,
        true  /* cancelOnHeaders */
        );

    UTF_REQUIRE( result.closed );

    /*
     * The response was complete and its headers were delivered - the cancel is what the sink did
     * WITH them, not something that happened instead of them
     */

    UTF_REQUIRE_EQUAL( result.status, 200U );

    UTF_REQUIRE_EQUAL( result.closedEvents, 1U );

    UTF_REQUIRE( asio::error::operation_aborted == result.errorCode );

    UTF_REQUIRE( httpclient::ConnectionState::Ready != result.state );
    UTF_REQUIRE_EQUAL( result.freeSlots, 0U );

    UTF_CHECK( ! result.taskFailed );
}

#endif /* __UTEST_TESTHTTP1DRIVERSTRANDSEAM_H_ */
