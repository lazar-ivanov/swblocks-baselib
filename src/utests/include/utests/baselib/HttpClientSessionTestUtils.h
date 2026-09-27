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

#ifndef __UTEST_HTTPCLIENTSESSIONTESTUTILS_H_
#define __UTEST_HTTPCLIENTSESSIONTESTUTILS_H_

#include <baselib/httpclient/ClientTypes.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <string>

/************************************************************************
 * What more than one CLIENT SESSION module needs - hoisted here by CS-2 of astra's second review
 *
 * CountingBodySinkT was utf_baselib_httpclient4's own and moved here VERBATIM when
 * utf_baselib_httpclient10 needed it for the TLS fallback exchange with a sink (E3 on
 * notes/plans/issues/astra-remediation-owed-work.md): a test header may never be included across
 * module directories - that silently duplicates its cases into two binaries - and a copy in each
 * would be worse (src/utests/AGENTS.md). Its own comment below is the one it had there, so "this
 * suite" in it is utf_baselib_httpclient4's.
 *
 * IT KEEPS ITS NAMESPACE, utest::session, so the module it came from names it exactly as before
 * and nothing there had to change but the include. A module which uses it from here names it
 * utest::session::CountingBodySink.
 */

namespace utest
{
    namespace session
    {
        /**
         * @brief A caller's BodySink at the SESSION level, which counts its terminal callbacks
         *
         * NOTHING IN THIS SUITE INSTALLED ONE UNTIL S6R.3, which is why H08 was invisible: a sink
         * handed to createRequestTask( ) is carried to EVERY hop of the chain ( startHop( ) ), so
         * what a hop tells it is what the CALLER sees, and a chain of two hops used to tell it the
         * body was complete twice. The count is therefore the assertion, exactly as the request
         * task's own case counts credit
         *
         * It takes everything it is offered - the backpressure question is the request task's and
         * has its own cases there; what is under test here is which hop says what to it
         */

        template
        <
            typename E = void
        >
        class CountingBodySinkT : public bl::httpclient::BodySink
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( CountingBodySinkT, bl::httpclient::BodySink )

        protected:

            mutable bl::os::mutex                                               m_lock;

            std::string                                                         m_received;
            std::size_t                                                         m_completions;

            CountingBodySinkT() NOEXCEPT
                :
                m_completions( 0U )
            {
            }

        public:

            virtual std::size_t onData( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) OVERRIDE
            {
                const auto offered = data -> size() - data -> offset1();

                BL_MUTEX_GUARD( m_lock );

                m_received.append(
                    reinterpret_cast< const char* >( data -> pv() ) + data -> offset1(),
                    offered
                    );

                return offered;
            }

            virtual void onComplete() OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                ++m_completions;
            }

            auto received() const -> std::string
            {
                BL_MUTEX_GUARD( m_lock );

                return m_received;
            }

            std::size_t completions() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_completions;
            }
        };

        typedef bl::om::ObjectImpl< CountingBodySinkT<> >                       CountingBodySink;

    } // session

} // utest

#endif /* __UTEST_HTTPCLIENTSESSIONTESTUTILS_H_ */
