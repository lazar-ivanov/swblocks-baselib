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

#ifndef __BL_TASKS_TCPSSLSTRANDEDSTREAMS_H_
#define __BL_TASKS_TCPSSLSTRANDEDSTREAMS_H_

#include <baselib/tasks/TcpSslBaseTasks.h>
#include <baselib/tasks/AsioSslStreamWrapper.h>
#include <baselib/tasks/TasksIncludes.h>

/*
 * The same capability guard as the cleartext policy in TcpStrandedStreams.h - asio::strand_t is
 * the executor strand only from Boost 1.72 onwards, and it is also the condition the wrapper's
 * strand-taking constructor is compiled under
 *
 * The guard is on the capability rather than on a devenv version: only the project makefiles
 * define BL_DEVENV_VERSION and no public header may require it
 * (notes/plans/issues/devenv7-breaking-changes-release-notes.md)
 */

#if ( ( BOOST_VERSION / 100 ) < 1072 )
#error TcpSslStrandedStreams.h requires Boost 1.72 or later for executor-bound I/O objects (asio::make_strand)
#endif

namespace bl
{
    namespace tasks
    {
        /******************************************************************************************
         * ============================ TcpSslSocketAsyncStrandedBase =============================
         */

        /**
         * @brief TcpSslSocketAsyncStrandedBase - the TLS stream policy with the stream bound to
         * a strand
         *
         * This is design 3.1 (D13) for the TLS half, and it is the cleartext policy of
         * TcpStrandedStreams.h over an SSL stream. asio::ssl::stream forwards its first
         * constructor argument to the next layer, so constructing the wrapper on the strand puts
         * the socket underneath it on the strand as well, and the strand becomes the default
         * executor for every handler of every operation on the stream
         *
         * The TLS half is where that matters most. asio::ssl::stream's read and write are composed
         * operations over an engine the two share, and a task which has a read and a write
         * outstanding together is relying on Asio's own internal handlers - the ones which drive
         * that engine and which no call site can reach - being serialized. Binding the stream to
         * a strand is what serializes them; there is no call site at which they could be wrapped
         *
         * It derives from the TLS policy and HIDES createSocket, which is the same mechanism the
         * cleartext policy uses and for the same reason - the stream policy is a static interface
         * resolved by template composition, not a virtual one. The hiding definition constructs
         * the wrapper through its strand-taking constructor and then calls configureClientStream(),
         * so the SNI rule is the base's single copy of it rather than a second one here
         *
         * Two notes which are specific to TLS:
         *
         *  - m_wasSocketShutdownForcefully is set SYNCHRONOUSLY by cancelTask() below, and here
         *    that is not only about the cancelled task's exception. isShutdownNeeded()
         *    (TcpSslBaseTasks.h:648) and scheduleTaskFinishContinuation (:455) both read it to
         *    decide whether a TLS shutdown is still owed. Setting it on the strand would let the
         *    terminal path read it stale and start an async_shutdown on a stream whose socket is
         *    about to be shut down from under it
         *
         *  - the protocol deadline (m_protocolTimer) stays on the thread pool's io_service and
         *    needs no change. Its handler touches no stream state: it takes the task lock and
         *    calls requestCancelInternal(), which reaches the cancelTask() below and therefore
         *    posts. That is what keeps this policy purely additive
         *
         * Everything the cleartext policy says about attachStream(), about the strand belonging to
         * the stream createSocket built, and about a retry creating a new one applies here
         * unchanged - and the retry is not hypothetical for TLS: a retryable handshake error
         * restarts the whole resolve / connect / handshake transaction
         */

        template
        <
            typename E = void
        >
        class TcpSslSocketAsyncStrandedBaseT :
            public TcpSslSocketAsyncBase
        {
            BL_DECLARE_OBJECT_IMPL( TcpSslSocketAsyncStrandedBaseT )

        public:

            typedef TcpSslSocketAsyncStrandedBaseT< E >                                 this_type;
            typedef TcpSslSocketAsyncBase                                               base_type;

        protected:

            cpp::SafeUniquePtr< asio::strand_t >                                        m_strand;

            TcpSslSocketAsyncStrandedBaseT( SAA_in_opt std::string&& taskName = std::string() )
                :
                base_type( BL_PARAM_FWD( taskName ) )
            {
            }

            /**
             * @brief The forced shutdown of cancelTask(), executed on the strand
             */

            void shutdownSocketOnStrand() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( base_type::isChannelOpen() )
                {
                    TcpSocketCommonBase::shutdownSocket( base_type::getSocket(), true /* force */ );
                }

                BL_NOEXCEPT_END()
            }

            /*
             * The static interface of the base policy, with createSocket hidden and three
             * members added:
             *
             * createSocket( ... )          - the TLS stream is built on the strand
             * getStrand() NOEXCEPT         - the strand the stream was built on
             * createTimer()                - a deadline timer on that same strand
             * postToStrand( handler )      - run a handler on that same strand
             */

            void createSocket(
                SAA_inout       asio::io_service&                                   aioService,
                SAA_in          const std::string&                                  hostName,
                SAA_in          const std::string&                                  serviceName
                )
            {
                m_strand.reset( new asio::strand_t( asio::make_strand( aioService ) ) );

                base_type::m_sslStream.reset(
                    new base_type::stream_t(
                        *m_strand,
                        hostName,
                        serviceName,
                        base_type::m_serverContext ? base_type::m_serverContext.get() : nullptr
                        )
                    );

                base_type::configureClientStream( hostName );
            }

            auto getStrand() NOEXCEPT -> asio::strand_t&
            {
                BL_ASSERT( m_strand );

                return *m_strand;
            }

            /**
             * @brief A deadline timer on the stream's own strand
             *
             * Note that BOOST_ASIO_DISABLE_STD_CHRONO is defined (OSBoostImports.h:29), so this
             * is the wall clock timer, as everywhere else in the library
             */

            auto createTimer() -> cpp::SafeUniquePtr< asio::deadline_timer >
            {
                BL_ASSERT( m_strand );

                return cpp::SafeUniquePtr< asio::deadline_timer >::attach(
                    new asio::deadline_timer( *m_strand )
                    );
            }

            /**
             * @brief Runs a handler on the stream's own strand
             *
             * asio::post never invokes the handler inline, so this is safe to call from a handler
             * already running on the strand and from any other thread alike
             */

            template
            <
                typename HANDLER
            >
            void postToStrand( SAA_in HANDLER&& handler )
            {
                BL_ASSERT( m_strand );

                asio::post( *m_strand, BL_PARAM_FWD( handler ) );
            }

            /**
             * @brief Cancels the task by shutting the socket down ON THE STRAND
             *
             * The base policy calls shutdownSocket directly, from whichever thread requested the
             * cancel, which races Asio's internal SSL handlers rather than being serialized with
             * them. Posting it makes the shutdown the last thing the strand does to this stream
             */

            virtual void cancelTask() OVERRIDE
            {
                if( ! m_strand )
                {
                    /*
                     * No stream of ours to shut down on a strand - a stream attached from
                     * elsewhere, or a task cancelled before it ever created one
                     */

                    base_type::cancelTask();

                    return;
                }

                if( ! base_type::isChannelOpen() )
                {
                    return;
                }

                TcpSocketCommonBase::m_wasSocketShutdownForcefully = true;

                postToStrand(
                    cpp::bind(
                        &this_type::shutdownSocketOnStrand,
                        om::ObjPtrCopyable< this_type >::acquireRef( this )
                        )
                    );
            }
        };

        typedef TcpSslSocketAsyncStrandedBaseT<> TcpSslSocketAsyncStrandedBase;

    } // tasks

} // bl

#endif /* __BL_TASKS_TCPSSLSTRANDEDSTREAMS_H_ */
