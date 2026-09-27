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

#ifndef __UTEST_TESTHTTPCLIENTREQUESTTASKSINKACCOUNTING_H_
#define __UTEST_TESTHTTPCLIENTREQUESTTASKSINKACCOUNTING_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ConnectionPool.h>

#include <baselib/core/ThreadPool.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * What a request task records about the caller's BodySink - D4 of astra's second review ( R04 )
 *
 * THE DEFECT. offerToSink( ) added up what the sink accepted in a local and recorded it only after
 * its loop, so a later callback of the same offer which threw skipped the record: the session then
 * read zero from sinkDelivered( ), and with retryIdempotentOnConnectionLoss on and the connection
 * lost in the same batch it replayed the request onto a sink which already held a prefix. The same
 * batch also offered the block the sink had just thrown on to it again. Decided
 * ( notes/plans/issues/astra-second-review-decisions.md, D4 ): each block's accepted bytes are
 * recorded before the next callback runs, and once a sink throws no further delivery reaches it and
 * no replay is made onto it for that logical request.
 *
 * THE BATCH IS ARRANGED AND NOT HOPED FOR. What the defect needs is two blocks and a connection loss
 * applied in ONE batch with nothing of the sink's committed before it - which BatchThrowingSinkT
 * cannot give, because its first block commits in an earlier batch. So the connection below HOLDS
 * THE DRAIN INSIDE ITS FIRST submit( ), which runs in the apply phase of the batch carrying the
 * pool's answer: every event the case delivers while it is held lands in the mailbox, post( ) takes
 * only the mailbox lock, and the drain's next turn takes them all as one batch. A sleep in place of
 * the gate would be the flake src/utests/AGENTS.md names.
 *
 * The probes this builds on - ProbeConnectionT, ProbePool, runTask( ) and the rest - are
 * TestHttpClientRequestTask.h's, which this module's Main.cpp includes ahead of this file.
 */

namespace utest
{
    namespace sinkaccounting
    {
        /**
         * @brief A probe connection whose FIRST submit( ) holds the drain until the case opens a
         * gate, and which answers a SECOND submit( ) - a replay - by itself
         *
         * THE SECOND HALF IS WHAT MAKES A REPLAY OBSERVABLE WITHOUT WAITING FOR ITS ABSENCE. A
         * replay which the case had to drive would need the case to wait for a second submit( )
         * that, on a correct tree, never comes - and so to spend a timeout on every green run. The
         * replayed stream is instead answered from a pool thread, the way a driver's strand would
         * answer it, with the whole body in one block: a chain which replays completes on its own,
         * and the sink shows the duplicated prefix
         */

        template
        <
            typename E = void
        >
        class GatedProbeConnectionT : public utest::requesttask::ProbeConnectionT<>
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( GatedProbeConnectionT, bl::httpclient::ClientConnection )

        public:

            typedef utest::requesttask::ProbeConnectionT<>                      base_type;
            typedef GatedProbeConnectionT< E >                                  this_type;

        protected:

            mutable bl::os::mutex                                               m_gateLock;
            mutable bl::os::condition_variable                                  m_cvGate;

            bool                                                                m_isGateOpen;
            std::size_t                                                         m_submits;

            const std::string                                                   m_replayBody;

            GatedProbeConnectionT(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_in          std::string                                     replayBody
                )
                :
                base_type( BL_PARAM_FWD( negotiated ), false /* isSubmitRefused */ ),
                m_isGateOpen( false ),
                m_submits( 0U ),
                m_replayBody( BL_PARAM_FWD( replayBody ) )
            {
            }

        public:

            /**
             * @brief Lets the held submit( ) return, and with it the drain
             */

            void openTheGate()
            {
                BL_MUTEX_GUARD( m_gateLock );

                m_isGateOpen = true;

                m_cvGate.notify_all();
            }

            std::size_t submits() const
            {
                BL_MUTEX_GUARD( m_gateLock );

                return m_submits;
            }

            virtual auto submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                )
                -> bl::httpclient::stream_handle_t OVERRIDE
            {
                const auto handle = base_type::submit( request, eventSink );

                bl::os::mutex_unique_lock guard( m_gateLock );

                ++m_submits;

                if( 1U == m_submits )
                {
                    /*
                     * THE DRAIN IS INSIDE THIS CALL - applyAcquired( ), in the apply phase - and the
                     * handle and the sink are already published, so the case can deliver. Bounded,
                     * so that a case which never opens the gate fails rather than hangs
                     */

                    ( void ) m_cvGate.wait_for(
                        guard,
                        bl::os::chrono::milliseconds(
                            static_cast< std::size_t >( utest::requesttask::DEFAULT_WAIT_IN_MILLISECONDS )
                            ),
                        [ this ]() -> bool
                        {
                            return m_isGateOpen;
                        }
                        );

                    return handle;
                }

                const auto self = bl::om::ObjPtrCopyable< this_type >::acquireRef( this );

                bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::GeneralPurpose ) -> aioService().post(
                    [ self ]() -> void
                    {
                        self -> deliverHeaders( 200U, bl::http::HeaderList(), false /* isInterim */ );
                        self -> deliverData( self -> m_replayBody );
                        self -> deliverClosed();
                    }
                    );

                return handle;
            }
        };

        typedef bl::om::ObjectImpl< GatedProbeConnectionT<> > GatedProbeConnection;

        /**
         * @brief A caller's BodySink which takes every block but one, and throws on that one
         *
         * It refuses a block by its CONTENT, so that it refuses it wherever and however often it is
         * offered: the re-offer the defect makes in the same batch throws exactly as the first
         * offer did, and a replayed body which carries the same bytes in a different block is
         * taken - which is what makes a duplicated prefix visible rather than a second failure
         */

        template
        <
            typename E = void
        >
        class RefusingSinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( RefusingSinkT, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;

            const std::string                                                   m_refused;

            std::string                                                         m_received;
            std::size_t                                                         m_offers;
            bool                                                                m_completeCalled;

            RefusingSinkT( SAA_in std::string refused )
                :
                m_refused( BL_PARAM_FWD( refused ) ),
                m_offers( 0U ),
                m_completeCalled( false )
            {
            }

        public:

            virtual std::size_t onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) OVERRIDE
            {
                const std::string block(
                    data -> begin() + data -> offset1(),
                    data -> begin() + data -> size()
                    );

                {
                    BL_MUTEX_GUARD( m_lock );

                    ++m_offers;

                    if( block != m_refused )
                    {
                        m_received += block;

                        return block.size();
                    }
                }

                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "The body sink refused a block of the response"
                    );
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                m_completeCalled = true;
            }

            auto received() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_received;
            }

            std::size_t offers() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_offers;
            }

            bool completeCalled() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_completeCalled;
            }
        };

        typedef bl::om::ObjectImpl< RefusingSinkT<> > RefusingSink;

        /**
         * @brief Delivers the one batch both cases below are about, while the drain is held
         *
         * "first" is taken, "second" is refused, and the connection is lost behind them - published
         * as Draining before the close, so the request task reads ConnectionUnusable, which is what
         * feeds retryIdempotentOnConnectionLoss
         */

        inline void deliverTheBatch( SAA_in const bl::om::ObjPtr< GatedProbeConnection >& connection )
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, bl::http::HeaderList(), false /* isInterim */ );

            connection -> deliverData( "first" );
            connection -> deliverData( "second" );

            connection -> publishState( bl::httpclient::ConnectionState::Draining );

            connection -> deliverClosed(
                bl::eh::errc::make_error_code( bl::eh::errc::connection_reset ),
                false /* isRetryable */
                );

            connection -> openTheGate();
        }

    } // sinkaccounting

} // utest

/**
 * @brief D4 at the request task - a sink which threw keeps its count and is offered nothing more
 *
 * One batch carries [ headers, "first", "second", a connection loss ]. The two data events queue
 * two offers; the first takes "first" and throws on "second" inside the same loop.
 *
 * RED BEFORE D4, on two counts: sinkDelivered( ) read ZERO, because the throw skipped the record of
 * the five bytes the sink had taken; and the second offer handed "second" to the sink again - three
 * offers where there should be two. GREEN AFTER: five, and two.
 *
 * WHAT MUST NOT MOVE, and is asserted with it: H07's first-failure precedence - the connection loss
 * is applied in the apply phase and fails the request first, so the caller sees the network error
 * and not the sink's - and the pool's slot, given back once and marked unusable
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_SinkWhichThrowsKeepsItsCountAndIsOfferedNothingMoreTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;
    using namespace utest::sinkaccounting;

    const auto connection = GatedProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        std::string( "firstsecond" )
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = RefusingSink::createInstance( std::string( "second" ) );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        HttpClientRequestConfig(),
        om::ObjPtrCopyable< BodySink >( om::qi< BodySink >( sink ) )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            deliverTheBatch( connection );
        }
        );

    requireTrue( task -> isFailed(), "a stream which ended in an error should have failed the request" );

    /*
     * H07 - THE FIRST FAILURE IS THE ONE THE CALLER SEES, and the network's came first
     */

    requireTrue(
        std::string::npos != messageOf( task ).find( "The HTTP request failed" ),
        "the request should have failed with the connection loss, and the task reports: " +
            messageOf( task )
        );

    /*
     * THE COUNT SURVIVED THE THROW - the five bytes of "first" were taken before "second" was
     * offered, so they are recorded
     *
     * CHECKED AND NOT REQUIRED, this and the offers below, so that a red run shows both of the
     * defect's halves rather than stopping at the first
     */

    UTF_CHECK_EQUAL( taskImpl -> sinkDelivered(), 5U );

    /*
     * AND THE SINK HEARD NOTHING AFTER IT THREW: two offers, "first" and "second", and not the
     * third the second data event's own offer used to make
     */

    UTF_CHECK_EQUAL( sink -> offers(), 2U );
    UTF_REQUIRE_EQUAL( sink -> received(), std::string( "first" ) );

    requireTrue( ! sink -> completeCalled(), "the sink was told the body was complete" );

    UTF_REQUIRE_EQUAL( connection -> submits(), 1U );

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
    UTF_REQUIRE_EQUAL( pool -> releases()[ 0 ], std::string( "42:unusable" ) );
}

/**
 * @brief D4 through the session - one network attempt, and no duplicated prefix
 *
 * The same batch, under a SessionRequestTaskT with retryIdempotentOnConnectionLoss on: a GET, a
 * connection loss, nothing about the request which forbids a replay. The session asks the hop what
 * its sink took before it replays ( chkPrepareRetry( ) ), so what the hop recorded is the whole of
 * the decision.
 *
 * RED BEFORE D4: the hop reported zero, the session replayed, the replay was answered in full, and
 * the sink ended holding "first" twice - "firstfirstsecond" - with the chain reported a SUCCESS.
 * GREEN AFTER: one submit( ), "first" alone, and the chain failed with the connection loss.
 *
 * THE SESSION IS COMPOSED FROM PROBES, which is what it is built to allow: SessionRequestTaskT takes
 * its pool through the plan, and the probe pool answers every acquire( ) with the connection the
 * case holds. Only the probe connection can arrange the batch, and only a session makes the replay
 * decision - so neither the request task's own cases nor a real driver could pin this
 */

UTF_AUTO_TEST_CASE( ClientSession_SinkWhichThrewIsNotReplayedOntoTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;
    using namespace utest::sinkaccounting;

    const auto connection = GatedProbeConnection::createInstance(
        NegotiatedProtocol::fromAlpn( "h2" ),
        std::string( "firstsecond" )
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = RefusingSink::createInstance( std::string( "second" ) );

    SessionRequestPlan plan;

    plan.pool = om::ObjPtrCopyable< ConnectionPool >( om::qi< ConnectionPool >( pool ) );
    plan.state = om::ObjPtrCopyable< SessionState >(
        SessionStateImpl::createInstance< SessionState >()
        );
    plan.transportScheme = "https";

    /*
     * THE KNOB THIS IS ABOUT - off by default, and the only route to a replay of a request whose
     * failure does not itself prove the request unprocessed
     */

    plan.policy.retryIdempotentOnConnectionLoss = true;

    const auto chain = om::qi< tasks::Task >(
        SessionRequestTaskImpl::createInstance(
            std::move( plan ),
            makeRequest(),
            om::ObjPtrCopyable< BodySink >( om::qi< BodySink >( sink ) )
            )
        );

    runTask(
        chain,
        [ & ]() -> void
        {
            deliverTheBatch( connection );
        }
        );

    /*
     * ONE NETWORK ATTEMPT - the chain did not replay onto a sink which had taken bytes and thrown
     *
     * CHECKED AND NOT REQUIRED, these three, so that a red run shows what the replay did to the
     * sink as well as that it happened
     */

    UTF_CHECK_EQUAL( connection -> submits(), 1U );

    /*
     * NO DUPLICATED PREFIX, and nothing after the throw
     */

    UTF_CHECK_EQUAL( sink -> received(), std::string( "first" ) );
    UTF_CHECK_EQUAL( sink -> offers(), 2U );

    requireTrue( ! sink -> completeCalled(), "the sink was told the body was complete" );

    /*
     * AND THE CALLER IS TOLD THE REQUEST FAILED, with the connection loss - H07's precedence,
     * seen through the chain, which forwards its verdict to the hop it ended on
     */

    requireTrue( chain -> isFailed(), "the chain should have failed with the connection loss" );

    requireTrue(
        std::string::npos != messageOf( chain ).find( "The HTTP request failed" ),
        "the chain should have failed with the connection loss, and it reports: " +
            messageOf( chain )
        );

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
}

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASKSINKACCOUNTING_H_ */
