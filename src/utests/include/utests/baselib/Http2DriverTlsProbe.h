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


#ifndef __UTEST_HTTP2DRIVERTLSPROBE_H_
#define __UTEST_HTTP2DRIVERTLSPROBE_H_

#include <baselib/http2/Http2ConnectionTask.h>
#include <baselib/http2/Session.h>

#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpTunnelStage.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>

#include <utests/baselib/TlsTeardownTestUtils.h>

/*
 * The HTTP/2 driver over the stranded TLS policy, with its stop signalled and its quiet - the moment
 * its opening write is over - made observable. The CS-6 cases on this driver share it: I2's in
 * utf_baselib_h2client9, where it was written, and D-L3-1's in utf_baselib_h2client10
 */

namespace utest
{
    namespace h2driverprobe
    {
        using tlsteardown::OneShotSignal;

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

    } // h2driverprobe

} // utest

#endif /* __UTEST_HTTP2DRIVERTLSPROBE_H_ */
