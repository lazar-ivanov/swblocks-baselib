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

#ifndef __BL_TASKS_TCPSTRANDEDSTREAMS_H_
#define __BL_TASKS_TCPSTRANDEDSTREAMS_H_

#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TasksIncludes.h>

/*
 * An I/O object has carried an executor since Boost 1.70, but asio::strand_t is the executor
 * strand only from 1.72 onwards (core/detail/OSBoostImports.h), and that is the type this header
 * is written against
 *
 * The guard is on the capability rather than on a devenv version: only the project makefiles
 * define BL_DEVENV_VERSION and no public header may require it
 * (notes/plans/issues/devenv7-breaking-changes-release-notes.md)
 */

#if ( ( BOOST_VERSION / 100 ) < 1072 )
#error TcpStrandedStreams.h requires Boost 1.72 or later for executor-bound I/O objects (asio::make_strand)
#endif

namespace bl
{
    namespace tasks
    {
        /******************************************************************************************
         * ============================== TcpSocketAsyncStrandedBase ==============================
         */

        /**
         * @brief TcpSocketAsyncStrandedBase - the plain TCP stream policy with the socket bound
         * to a strand
         *
         * This is design 3.1 (D13) for the cleartext half. The socket is constructed on
         * asio::make_strand( aioService ) rather than on the I/O service directly, which makes
         * that strand the default executor for every handler of every operation on it - including
         * the intermediate handlers of Asio's composed operations, which no call site can wrap by
         * hand and which the task lock therefore cannot reach. A task built on this policy can run
         * a read, a write and a timer at the same time and have all of their handlers serialized
         * without a single asio::bind_executor at any call site, so there is no wrap left to
         * forget
         *
         * It derives from the plain policy and HIDES createSocket. Hiding rather than overriding
         * is the right mechanism because the stream policy is a static interface resolved by
         * template composition - TcpConnectionEstablisherBase< STREAM > and the server base call
         * createSocket through the derived type, not through a virtual - so the hiding definition
         * is the one they find. Everything else composing on STREAM works unchanged
         *
         * What it adds to that static interface is getStrand(), createTimer() and postToStrand():
         * a task's own asynchronous objects have to be built on the same executor as the socket,
         * or their handlers run off the strand and the guarantee above is only half of one
         *
         * Two properties worth knowing before this is used:
         *
         *  - the socket is installed through the public attachStream() because the base holds it
         *    in a PRIVATE member (TcpBaseTasks.h:443). The visible consequence is that the
         *    onStreamChanging( streamNew ) hook is invoked once per socket creation, which the
         *    base's own createSocket does not do. That is consistent with what the hook is for -
         *    resetting session state associated with the stream, which a freshly created socket
         *    also warrants - and it is stated here rather than left to be discovered
         *
         *  - the strand belongs to the socket createSocket built, and a retry
         *    (TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation restarts the whole
         *    resolve / connect / handshake transaction) creates a new one. A task must therefore
         *    build its timers per attempt, exactly as it must not carry any other per-attempt
         *    state across a retry
         *
         * A stream ATTACHED from elsewhere was not created here and is not on this policy's
         * strand; getStrand() then answers for a socket which is gone and cancelTask() falls back
         * to the base's synchronous shutdown. This policy is meant for a task which creates its
         * own socket, which is what every client establisher does
         */

        template
        <
            typename E = void
        >
        class TcpSocketAsyncStrandedBaseT :
            public TcpSocketAsyncBase
        {
            BL_CTR_DEFAULT( TcpSocketAsyncStrandedBaseT, protected )
            BL_DECLARE_OBJECT_IMPL( TcpSocketAsyncStrandedBaseT )

        public:

            typedef TcpSocketAsyncStrandedBaseT< E >                                    this_type;
            typedef TcpSocketAsyncBase                                                  base_type;

        protected:

            cpp::SafeUniquePtr< asio::strand_t >                                        m_strand;

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
             * createSocket( ... )          - the socket is built on the strand
             * getStrand() NOEXCEPT         - the strand the socket was built on
             * createTimer()                - a deadline timer on that same strand
             * postToStrand( handler )      - run a handler on that same strand
             */

            void createSocket(
                SAA_inout       asio::io_service&                                   aioService,
                SAA_in          const std::string&                                  hostName,
                SAA_in          const std::string&                                  serviceName
                )
            {
                BL_UNUSED( hostName );
                BL_UNUSED( serviceName );

                m_strand.reset( new asio::strand_t( asio::make_strand( aioService ) ) );

                base_type::attachStream(
                    base_type::stream_ref::attach( new tcp::socket( *m_strand ) )
                    );
            }

            auto getStrand() NOEXCEPT -> asio::strand_t&
            {
                BL_ASSERT( m_strand );

                return *m_strand;
            }

            /**
             * @brief A deadline timer on the socket's own strand
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
             * @brief Runs a handler on the socket's own strand
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
             * cancel, which is a socket call racing Asio's own handlers. Posting it serializes it
             * with them - it is the last thing the strand does to this socket rather than a call
             * from the side
             *
             * m_wasSocketShutdownForcefully is still set synchronously, because
             * TcpSocketCommonBase::onTaskStoppedNothrow (TcpBaseTasks.h:123) reads it to turn a
             * cancelled task into an operation_aborted exception; setting it on the strand would
             * make that observable depend on the order the strand happens to run things in
             */

            virtual void cancelTask() OVERRIDE
            {
                if( ! m_strand )
                {
                    /*
                     * No socket of ours to shut down on a strand - a stream attached from
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

        typedef TcpSocketAsyncStrandedBaseT<> TcpSocketAsyncStrandedBase;

    } // tasks

} // bl

#endif /* __BL_TASKS_TCPSTRANDEDSTREAMS_H_ */
