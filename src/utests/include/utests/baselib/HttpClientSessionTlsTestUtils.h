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

#ifndef __UTEST_HTTPCLIENTSESSIONTLSTESTUTILS_H_
#define __UTEST_HTTPCLIENTSESSIONTLSTESTUTILS_H_

#include <baselib/httpclient/ClientSession.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

#include <utests/baselib/Utf.h>

/************************************************************************
 * The helpers the cases of the client session over TLS share
 *
 * The type they build is httpclient::ClientSessionT< tasks::TcpSslSocketAsyncStrandedBase >. They live
 * here and not in a module directory because two modules run that session - utf_baselib_httpclient5,
 * whose S6.1 cases they were written for, and utf_baselib_httpclient10, which carried a copy of them -
 * and a test header may never be included across module directories (src/utests/AGENTS.md). CS-5 of
 * astra's second review (owed-list row I3) moved the one copy here.
 *
 * The host is "localhost" because the client verifies the peer name: UtfMain registers the dev root
 * CA for every test binary and the test server certificate is issued for that name.
 */

namespace utest
{
    namespace sessiontls
    {
        typedef bl::tasks::TcpSslSocketAsyncStrandedBase                        tls_stream_t;

        typedef bl::httpclient::ClientSessionImplT< tls_stream_t >              TlsSessionImpl;

        inline auto makeSession(
            SAA_in_opt      bl::httpclient::ClientSessionConfig                 config =
                                bl::httpclient::ClientSessionConfig()
            )
            -> bl::om::ObjPtr< TlsSessionImpl >
        {
            return TlsSessionImpl::createInstance( BL_PARAM_FWD( config ) );
        }

        inline auto makeRequest(
            SAA_in          const bl::os::port_t                                port,
            SAA_in_opt      const std::string&                                  target = "/",
            SAA_in_opt      const std::string&                                  method = "GET"
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( bl::cpp::copy( method ) );

            request.url(
                bl::net::Uri::parse(
                    "https://localhost:" +
                    bl::utils::lexical_cast< std::string >( port ) +
                    target
                    )
                );

            return request;
        }

        /**
         * @brief Runs one session task to completion; the queue keeps it, so a case can look at how
         * it ended
         */

        inline void runSessionTask( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            using namespace bl;
            using namespace bl::tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    eq -> push_back( task );

                    eq -> wait( task );
                }
                );
        }

        /**
         * @brief Fails with the reason the task failed - a function and not a UTF macro argument,
         * because UTF_FAIL( msg ) takes the globals lock before it evaluates msg and bl::os::mutex
         * is not recursive ( Utf.h )
         */

        inline void requireTaskSucceeded( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
        {
            if( ! task -> isFailed() )
            {
                return;
            }

            UTF_FAIL(
                "the session request task failed: " +
                bl::eh::diagnostic_information( task -> exception() )
                );
        }

        inline auto bodyOf( SAA_in const bl::httpclient::ClientResponse& response ) -> std::string
        {
            const auto& block = response.body();

            if( ! block )
            {
                return std::string();
            }

            return std::string(
                block -> begin() + block -> offset1(),
                block -> begin() + block -> size()
                );
        }

        inline auto runRequest(
            SAA_in          const bl::om::ObjPtr< TlsSessionImpl >&             session,
            SAA_in          const bl::httpclient::ClientRequest&                request
            )
            -> bl::om::ObjPtr< bl::httpclient::ClientRequestTask >
        {
            auto requestTask = session -> createRequestTask( request );

            const auto task = bl::om::qi< bl::tasks::Task >( requestTask );

            runSessionTask( task );

            if( task -> isFailed() )
            {
                UTF_FAIL(
                    "the session request task failed: " +
                    bl::eh::diagnostic_information( task -> exception() )
                    );
            }

            return requestTask;
        }

        inline auto statsOf( SAA_in const bl::om::ObjPtr< TlsSessionImpl >& session )
            -> bl::httpclient::ConnectionPoolImpl::Stats
        {
            return bl::om::qi< bl::httpclient::ConnectionPoolImpl >( session -> pool() ) -> stats();
        }

        /**
         * @brief class StringBodySourceT - a streaming upload which really does produce bytes and
         * really can rewind
         *
         * Http2DriverTestUtils' StubBodySource exists to make a request LOOK streaming for the
         * driver's own cases and never produces anything, which is right there and useless here:
         * what the session's cases need is a source the request task can drain to completion. And
         * rewindable, so that nothing but the protocol can fail an upload: a request whose source
         * could not rewind would be refused a replay for that reason instead
         */

        template
        <
            typename E = void
        >
        class StringBodySourceT : public bl::httpclient::BodySource
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( StringBodySourceT, bl::httpclient::BodySource )

        protected:

            const std::string                                                   m_payload;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_offset;

            StringBodySourceT( SAA_in std::string payload )
                :
                m_payload( BL_PARAM_FWD( payload ) )
            {
            }

        public:

            virtual auto read( SAA_inout bl::data::DataBlock& target )
                -> bl::httpclient::BodyReadResult OVERRIDE
            {
                bl::httpclient::BodyReadResult result;

                const auto room = target.capacity() - target.size();
                const auto left = m_payload.size() - m_offset;
                const auto count = std::min< std::size_t >( room, left );

                if( count )
                {
                    std::memcpy( target.begin() + target.size(), m_payload.c_str() + m_offset, count );

                    target.setSize( target.size() + count );

                    m_offset = m_offset.value() + count;
                }

                result.size = count;
                result.isEndOfStream = ( m_offset == m_payload.size() );

                return result;
            }

            virtual bool canRewind() const NOEXCEPT OVERRIDE
            {
                return true;
            }

            virtual void rewind() OVERRIDE
            {
                m_offset = 0U;
            }
        };

        typedef bl::om::ObjectImpl< StringBodySourceT<> >                       StringBodySource;

    } // sessiontls

} // utest

#endif /* __UTEST_HTTPCLIENTSESSIONTLSTESTUTILS_H_ */
