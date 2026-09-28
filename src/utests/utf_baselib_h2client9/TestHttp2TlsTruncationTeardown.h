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

#ifndef __UTEST_TESTHTTP2TLSTRUNCATIONTEARDOWN_H_
#define __UTEST_TESTHTTP2TLSTRUNCATIONTEARDOWN_H_

#include <baselib/http2/Http2ConnectionTask.h>
#include <baselib/http2/Session.h>

#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpTunnelStage.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <memory>
#include <string>

#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * I2 ON THE HTTP/2 DRIVER - a truncated TLS stream's teardown must not wait for a close_notify
 * (owed-list row I2, change-set CS-6)
 *
 * WHAT IS UNDER TEST. After a peer ends a TLS stream with no close_notify, a TLS task's teardown
 * sends our close_notify and then waits for the peer's. The read which saw the truncation consumed
 * the socket's end of stream, so on Linux nothing wakes that wait but the protocol timer, which ends
 * the task as a cancel (notes/plans/issues/astra2-cs6-tls-shutdown-after-truncation-design.md, 1).
 * The note's 5 calls this driver's hang INFERRED; this case is where it is measured. Its siblings on
 * the HTTP/1.1 driver and SimpleHttpTask are utf_baselib_httpclient13's.
 *
 * I2'S RED, committed before its fix. The driver's protocol timer is shortened to 3 s, so on today's
 * code the case observes the symptom: the timer cancels the task, and isCanceled( ) is true. After
 * the fix the teardown sends our close_notify, does not wait for the peer's, and the task ends before
 * the timer, uncancelled and clean. The peer selects h2 by ALPN, because otherwise the driver would
 * hand the stream to its fallback and complete (Http2ConnectionTask.h, onProtocolNegotiated( )).
 *
 * THE DRIVER WRITES ITS PREFACE AS SOON AS THE HANDSHAKE IS DONE, AND THAT WRITE MUST BE OVER BEFORE
 * THE PEER ENDS THE STREAM. The driver's initiateClose( ) shuts the socket's send side down when a
 * write is still in flight, and a task whose socket was shut down that way runs no TLS shutdown at
 * all - so an ending which raced the preface would take that path, and the case would be green or red
 * by scheduling. The peer therefore holds its ending, and the probe watches the driver's own strand
 * until no write is in flight: its first write posts a check there, which re-posts itself while the
 * write is outstanding and signals once it is not. The check waits for a state, not for a time, and
 * nothing but the ending can start another write on an idle connection. By then the driver's read is
 * armed and registered with the reactor, so the FIN's own event is what completes it, and no event
 * is left to wake the shutdown's read.
 *
 * THE PEER is utests/baselib/TlsEndingPeer.h: it truncates by shutting its transport's send side down
 * under the TLS session, then reads until the client ends the stream, and keeps its socket open and
 * silent until the case releases it - so nothing but the fix can end the teardown early.
 */

namespace utest
{
    namespace h2truncteardown
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::TlsEndingPeer;
        using tlsteardown::chkEndedWithoutWaiting;
        using tlsteardown::makeTlsKey;
        using tlsteardown::runToTheEnd;
        using tlsteardown::shortProtocolTimeout;

        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        /**
         * @brief The HTTP/2 driver over the stranded TLS policy, which establishes its own
         * connection, with its stop signalled - and its quiet, once its opening write is over
         */

        class Http2DriverProbe : public bl::tasks::Http2ConnectionTaskT< tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( Http2DriverProbe )

        public:

            typedef bl::tasks::Http2ConnectionTaskT< tls_stream_t >             base_type;

        protected:

            OneShotSignal                                                       m_stop;
            OneShotSignal                                                       m_quiet;

            /*
             * Touched only on the strand
             */

            bool                                                                m_isQuietCheckPosted;

            Http2DriverProbe(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          base_type::factory_ptr_t                        driverFactory
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    bl::tasks::Http2ConnectionConfig(),
                    bl::tasks::ProxyConfig::none(),
                    bl::tasks::ClientConnectionConfig(),
                    false /* logExceptions */
                    ),
                m_isQuietCheckPosted( false )
            {
            }

            /**
             * @brief The first write - the preface - starts the watch for the quiet
             *
             * Called on the strand immediately before the write is issued, in the same strand turn
             * which sets m_isWriteInFlight, so a check posted from here never runs before it is set
             */

            virtual void onWriteScheduled( SAA_in const bl::http2::Session::wire_buffer_t& buffer ) OVERRIDE
            {
                base_type::onWriteScheduled( buffer );

                if( ! m_isQuietCheckPosted )
                {
                    m_isQuietCheckPosted = true;

                    postQuietCheck();
                }
            }

            /**
             * @brief Signals the quiet once no write is in flight, and otherwise looks again
             *
             * Each look is its own strand turn, so the write's own handler - the one place which
             * clears m_isWriteInFlight - runs between two of them
             */

            void postQuietCheck()
            {
                const auto ref = base_type::self_ref_t::acquireRef( this );

                base_type::postToStrand(
                    [ this, ref ]() -> void
                    {
                        BL_NOEXCEPT_BEGIN()

                        if( base_type::m_isWriteInFlight )
                        {
                            postQuietCheck();

                            return;
                        }

                        m_quiet.signal();

                        BL_NOEXCEPT_END()
                    }
                    );
            }

            virtual auto onTaskStoppedNothrow(
                SAA_in_opt          const std::exception_ptr&                   eptrIn = nullptr,
                SAA_inout_opt       bool*                                       isExpectedException = nullptr
                ) NOEXCEPT
                -> std::exception_ptr OVERRIDE
            {
                auto result = base_type::onTaskStoppedNothrow( eptrIn, isExpectedException );

                BL_NOEXCEPT_BEGIN()

                m_stop.signal();

                BL_NOEXCEPT_END()

                return result;
            }

        public:

            bool waitForStop( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_stop.waitFor( timeoutInMilliseconds );
            }

            bool waitForQuiet( SAA_in const std::size_t timeoutInMilliseconds ) const
            {
                return m_quiet.waitFor( timeoutInMilliseconds );
            }
        };

        typedef bl::om::ObjectImpl< Http2DriverProbe >                          Http2DriverProbeImpl;

    } // h2truncteardown

} // utest

/**
 * @brief I2'S RED - an idle HTTP/2 driver whose peer truncates ends without waiting
 *
 * The peer's ending is held until the driver's opening write is over - see the header's comment -
 * and the factory is empty, because a peer which selected h2 never reaches it
 */

UTF_AUTO_TEST_CASE( Http2DriverTls_ATruncatedIdleConnectionEndsWithoutWaitingTests )
{
    using namespace bl;
    using namespace utest::h2truncteardown;

    TlsEndingPeer::Script script( TlsEndingPeer::Ending::Truncate );

    script.alpnPreference.push_back( "h2" );
    script.isEndingHeld = true;

    TlsEndingPeer peer( script );

    const auto driver = Http2DriverProbeImpl::createInstance(
        makeTlsKey( peer.port() ),
        std::make_shared< httpclient::ClientDriverFactoryT< tls_stream_t > >()
        );

    driver -> setProtocolTimeout( shortProtocolTimeout() );

    const auto releaseOnceQuiet = [ &driver, &peer ]() -> bool
    {
        const bool isQuiet = driver -> waitForQuiet( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

        peer.releaseEnding();

        return isQuiet;
    };

    chkEndedWithoutWaiting( runToTheEnd( driver, peer, releaseOnceQuiet ), "the HTTP/2 driver" );
}

#endif /* __UTEST_TESTHTTP2TLSTRUNCATIONTEARDOWN_H_ */
