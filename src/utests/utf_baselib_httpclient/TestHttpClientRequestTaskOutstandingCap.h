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

#ifndef __UTEST_TESTHTTPCLIENTREQUESTTASKOUTSTANDINGCAP_H_
#define __UTEST_TESTHTTPCLIENTREQUESTTASKOUTSTANDINGCAP_H_

#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The cap on what a request holds of a body and has not handed on - D3 of astra's second review
 * ( R02 ), implemented to notes/plans/issues/astra2-cs2-d3-unread-bytes-cap-design.md
 *
 * Three of the note's five cases, the ones the request task's own probes can drive ( section 8 ):
 * the mailbox, the per-block allowance, and the guard which stops a block reaching a request
 * which has already failed. The two which need a real HTTP/1.1 driver are utf_baselib_httpclient9's.
 *
 * THE MAILBOX IS HELD BY THE SINK, and that is what makes the first two deterministic: the sink
 * parks inside its FIRST offer, in the drain's deferred phase, and every block the case delivers
 * from then on lands in the mailbox, where post( ) - the one place the charge rises - counts it on
 * the case's own thread. So the block which crosses the cap is fixed by the arithmetic below and by
 * nothing else.
 *
 * The probes these build on - ProbeConnection, ProbePool, runTask( ) and the rest - are
 * TestHttpClientRequestTask.h's, which this module's Main.cpp includes ahead of this file.
 */

namespace utest
{
    namespace outstandingcap
    {
        typedef bl::httpclient::HttpClientRequestTaskT<>                        task_t;

        /**
         * @brief What one block of this payload is charged against the cap - the task's own rule
         */

        inline std::size_t chargeOf( SAA_in const std::size_t payload ) NOEXCEPT
        {
            return payload + static_cast< std::size_t >( task_t::OUTSTANDING_BLOCK_ALLOWANCE );
        }

        inline bool isBufferTooSmall( SAA_in const std::exception_ptr& eptr )
        {
            if( ! eptr )
            {
                return false;
            }

            try
            {
                std::rethrow_exception( eptr );
            }
            catch( bl::BufferTooSmallException& )
            {
                return true;
            }
            catch( std::exception& )
            {
            }

            return false;
        }

        /**
         * @brief A caller's BodySink which takes everything it is offered, and can park its FIRST
         * offer until the case lets it go
         *
         * Parked, it holds the drain in the deferred phase, off the task lock, so the case can
         * deliver behind it into the mailbox; not parked, it only counts, which is what a case
         * about a block which must never reach it needs
         */

        template
        <
            typename E = void
        >
        class ParkingSinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( ParkingSinkT, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;

            const bool                                                          m_isParking;

            std::string                                                         m_received;
            std::size_t                                                         m_offers;
            bool                                                                m_released;
            bool                                                                m_completeCalled;

            ParkingSinkT( SAA_in const bool isParking ) NOEXCEPT
                :
                m_isParking( isParking ),
                m_offers( 0U ),
                m_released( false ),
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

                bl::os::mutex_unique_lock guard( m_lock );

                ++m_offers;

                m_cv.notify_all();

                if( m_isParking && 1U == m_offers )
                {
                    ( void ) m_cv.wait_for(
                        guard,
                        bl::os::chrono::milliseconds(
                            static_cast< std::size_t >( utest::requesttask::DEFAULT_WAIT_IN_MILLISECONDS )
                            ),
                        [ this ]() -> bool
                        {
                            return m_released;
                        }
                        );
                }

                m_received += block;

                return block.size();
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                m_completeCalled = true;
            }

            /**
             * @brief Blocks until the first offer is INSIDE the sink - from then on, everything the
             * case delivers lands in the mailbox and is counted there
             */

            bool waitForFirstOffer() const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds(
                        static_cast< std::size_t >( utest::requesttask::DEFAULT_WAIT_IN_MILLISECONDS )
                        ),
                    [ this ]() -> bool
                    {
                        return m_offers >= 1U;
                    }
                    );
            }

            /**
             * @brief Lets the parked first offer return - not called release( ), which is
             * om::Object's own
             */

            void letFirstOfferReturn()
            {
                BL_MUTEX_GUARD( m_lock );

                m_released = true;

                m_cv.notify_all();
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

        typedef bl::om::ObjectImpl< ParkingSinkT<> > ParkingSink;

    } // outstandingcap

} // utest

/**
 * @brief Case 1 - the MAILBOX is capped: a block which would cross the cap is refused at post( ),
 * the stream is reset, and the request fails
 *
 * The cap holds two 4-byte blocks and their allowances, and one byte more. "abcd" is offered and
 * parks the sink; "efgh" is counted behind it; "ijkl" would cross, so it becomes the overflow
 * marker; "mnop" arrives after the latch and is dropped. When the sink lets go, the batch is
 * [ "efgh", marker ]: the prefix is offered in order, then the stream is cancelled.
 *
 * RED BEFORE D3: nothing is counted, so all four blocks reach the sink, no cancel is ever made,
 * and the case fails waiting for one. GREEN AFTER: BufferTooSmallException; the sink holds exactly
 * "abcdefgh", both credited; one cancel, one release; and the sink is never told the body was
 * complete
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_OutstandingCapFailsAtTheMailboxTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;
    using namespace utest::outstandingcap;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::withoutAlpn( HttpProtocol::Http11 ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = ParkingSink::createInstance( true /* isParking */ );

    HttpClientRequestConfig config;

    config.maxOutstandingResponseBodySize = 2U * chargeOf( 4U ) + 1U;

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config,
        om::ObjPtrCopyable< BodySink >( om::qi< BodySink >( sink ) )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverData( "abcd" );

            requireTrue( sink -> waitForFirstOffer(), "the sink was never offered the first block" );

            connection -> deliverData( "efgh" );
            connection -> deliverData( "ijkl" );
            connection -> deliverData( "mnop" );

            sink -> letFirstOfferReturn();

            /*
             * THE CANCEL IS THE MARKER'S, made in the drain which applies it - and the close is
             * delivered only after it, as the driver would answer that cancel
             */

            connection -> waitFor( "cancel:42" );

            connection -> deliverClosed(
                eh::errc::make_error_code( eh::errc::operation_canceled ),
                false /* isRetryable */
                );
        }
        );

    requireTrue( task -> isFailed(), "a request past its outstanding cap should have failed" );

    requireTrue(
        isBufferTooSmall( task -> exception() ),
        "the request should have failed with BufferTooSmallException, and it reports: " +
            messageOf( task )
        );

    UTF_REQUIRE_EQUAL( sink -> received(), std::string( "abcdefgh" ) );
    UTF_REQUIRE_EQUAL( connection -> consumedTotal(), 8U );

    requireTrue( ! sink -> completeCalled(), "the sink was told the body was complete" );

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
}

/**
 * @brief Case 4 - each block is charged its ALLOWANCE as well as its payload, so one-byte blocks
 * cross the cap after cap / ( 1 + allowance ) of them
 *
 * The cap is three one-byte blocks' charges, so the fourth crosses whatever the allowance measures
 * on this platform. A cap on payload alone would hold hundreds of them.
 *
 * RED BEFORE D3: nothing crosses and no cancel is made. GREEN AFTER: the sink holds exactly the
 * three blocks which fitted, and the request failed with BufferTooSmallException
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_OutstandingCapChargesEachBlockItsAllowanceTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;
    using namespace utest::outstandingcap;

    const std::size_t fitting = 3U;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::withoutAlpn( HttpProtocol::Http11 ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = ParkingSink::createInstance( true /* isParking */ );

    HttpClientRequestConfig config;

    config.maxOutstandingResponseBodySize = fitting * chargeOf( 1U );

    /*
     * A payload-only cap of the same size would hold more one-byte blocks than the case delivers, so
     * none of them would cross it - which is what makes this case about the allowance
     */

    UTF_REQUIRE( config.maxOutstandingResponseBodySize.value() > fitting + 2U );

    const auto taskImpl = HttpClientRequestTaskImpl::createInstance(
        makeRequest(),
        makeKey(),
        om::qi< ConnectionPool >( pool ),
        config,
        om::ObjPtrCopyable< BodySink >( om::qi< BodySink >( sink ) )
        );

    const auto task = om::qi< tasks::Task >( taskImpl );

    runTask(
        task,
        [ & ]() -> void
        {
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            connection -> deliverData( "a" );

            requireTrue( sink -> waitForFirstOffer(), "the sink was never offered the first block" );

            /*
             * "b" and "c" fit; "d" is the fourth charge and crosses; "e" arrives after the latch
             */

            connection -> deliverData( "b" );
            connection -> deliverData( "c" );
            connection -> deliverData( "d" );
            connection -> deliverData( "e" );

            sink -> letFirstOfferReturn();

            connection -> waitFor( "cancel:42" );

            connection -> deliverClosed(
                eh::errc::make_error_code( eh::errc::operation_canceled ),
                false /* isRetryable */
                );
        }
        );

    requireTrue(
        isBufferTooSmall( task -> exception() ),
        "the request should have failed with BufferTooSmallException, and it reports: " +
            messageOf( task )
        );

    UTF_REQUIRE_EQUAL( sink -> received(), std::string( "abc" ) );

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );
}

/**
 * @brief Case 5 - a block which arrives after the request has FAILED is not offered to the sink
 *
 * The maintainer's third decision of this run, folded into D3: a guard at the top of applyData( ).
 * The case cancels the request and waits for the probe's cancel record, which the drain's deferred
 * phase makes after its apply phase has already failed the request - so the block delivered next is
 * applied to a request whose caller holds its failure. The close delivered behind it is the
 * rendezvous: its release is applied after the block.
 *
 * RED BEFORE D3: the block is offered to the sink, taken, and credited. GREEN AFTER: the sink is
 * offered nothing, nothing is credited, and the request failed with the cancellation
 */

UTF_AUTO_TEST_CASE( HttpClientRequestTask_ABlockAfterTheFailureIsNotOfferedTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::requesttask;
    using namespace utest::outstandingcap;

    const auto connection = ProbeConnection::createInstance(
        NegotiatedProtocol::withoutAlpn( HttpProtocol::Http11 ),
        false /* isSubmitRefused */
        );

    const auto pool = ProbePool::createInstance(
        om::qi< ClientConnection >( connection ),
        true /* isAnswered */
        );

    const auto sink = ParkingSink::createInstance( false /* isParking */ );

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
            connection -> waitFor( "submit" );

            connection -> deliverHeaders( 200U, http::HeaderList(), false /* isInterim */ );

            task -> requestCancel();

            connection -> waitFor( "cancel:42" );

            connection -> deliverData( "late" );

            connection -> deliverClosed(
                eh::errc::make_error_code( eh::errc::operation_canceled ),
                false /* isRetryable */
                );
        }
        );

    requireTrue( pool -> waitForRelease(), "the stream slot never came back" );

    requireTrue( task -> isFailed(), "a cancelled request should have failed" );

    requireTrue(
        std::string::npos != messageOf( task ).find( "was cancelled" ),
        "the request should have failed with its cancellation, and it reports: " + messageOf( task )
        );

    UTF_CHECK_EQUAL( sink -> offers(), 0U );
    UTF_CHECK_EQUAL( connection -> consumedTotal(), 0U );

    UTF_REQUIRE_EQUAL( pool -> releases().size(), 1U );
}

#endif /* __UTEST_TESTHTTPCLIENTREQUESTTASKOUTSTANDINGCAP_H_ */
