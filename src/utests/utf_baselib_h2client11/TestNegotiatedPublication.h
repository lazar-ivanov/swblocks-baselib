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

#ifndef __UTEST_TESTNEGOTIATEDPUBLICATION_H_
#define __UTEST_TESTNEGOTIATEDPUBLICATION_H_

#include <baselib/http2/Http2ConnectionTask.h>

#include <baselib/httpclient/HttpClientRequestTask.h>
#include <baselib/httpclient/ClientConnectionTaskBase.h>
#include <baselib/httpclient/ClientConnection.h>
#include <baselib/httpclient/ClientTypes.h>

#include <baselib/tasks/TcpSslStrandedStreams.h>
#include <baselib/tasks/TcpTunnelStage.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/Uri.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <utests/baselib/Http2DriverTlsProbe.h>
#include <utests/baselib/TlsEndingPeer.h>
#include <utests/baselib/TlsTeardownTestUtils.h>
#include <utests/baselib/Utf.h>

/************************************************************************
 * THE NEGOTIATED VALUE, READ BEFORE IT IS PUBLISHED (U01, change-set CS-9 of Astra's fourth review)
 *
 * WHAT IS UNDER TEST. A request can ride an HTTP/2 connection which is still completing its TLS
 * handshake, and when it fails first - a cancel, a deadline, a submit( ) which throws - the request
 * task copies the connection's negotiated( ) while the handshake may be writing it
 * (HttpClientRequestTask.h, completeResponse( )). The value is written by the establishment base, in
 * continueAfterConnected( ), and the getter handed out a reference to it. D1 of
 * notes/plans/issues/astra-fourth-review-decisions.md, shape (a''): the connection publishes the value
 * itself, behind a flag set after its single write, and both getters read the flag first - the design
 * is notes/plans/issues/astra4-cs9-negotiated-publication-design.md.
 *
 * THE REDS, committed before the fix:
 *
 *   - NegotiatedPublication_AValueWrittenButNotPublishedIsNotReadTests - the contract, from a pure
 *     input. An unstarted establishment base, and an unstarted HTTP/2 driver, each have their value
 *     written and not published. Before the fix both getters returned what was written.
 *   - NegotiatedPublication_AReaderOffTheStrandSeesTheSettledValueTests - the race, under
 *     ThreadSanitizer. A thread ordered after nothing the strand did reads the real driver's
 *     negotiated( ) once a real handshake has settled it. Before the fix that read raced the
 *     write (ClientConnectionTaskBase.h, continueAfterConnected( )). The case itself passes on both
 *     sides; its verdict is the sanitizer's report, read from the run's own output.
 *
 * THE CHARACTERIZATIONS, green on both sides of the fix - U01's own route, a request task over the real
 * driver, and its two controls:
 *
 *   - NegotiatedPublication_ACancelDuringAHeldHandshakeReportsUnknownTests - the request rides a driver
 *     whose handshake is held, and is cancelled: Unknown, no identifier, the cancel intact, one release.
 *   - NegotiatedPublication_ACancelAfterTheHandshakeReportsH2Tests - the same cancel once the handshake
 *     has settled h2 reports h2: what separates the fix from one which blanks every failed response.
 *   - NegotiatedPublication_AFallbackToHttp11ReportsHttp11Tests - a peer which selects http/1.1 sends
 *     the task down the factory's fallback, and the request it bounces reports Http11.
 *
 * THE POOL IS ONE DRIVER. What puts a request on an establishing connection is the pool, under
 * ridePreface, and what it hands the request is that connection. A pool which answers every acquire( )
 * with the one driver, posted as the contract requires, stands in for it, as utf_baselib_httpclient8's
 * does: the real pool with the real request task and driver is a session module's weight, and those are
 * over the size target. The request task is pushed first and the driver only once the request has
 * submitted to it, so the request rides the preface by construction - a submit to a driver which has not
 * started is queued, as it is to one the pool has just scheduled.
 */

namespace utest
{
    namespace negotiatedpublication
    {
        using tlsteardown::WAIT_IN_MILLISECONDS;
        using tlsteardown::OneShotSignal;
        using tlsteardown::TlsEndingPeer;
        using tlsteardown::chkOrFail;
        using tlsteardown::describeCode;
        using tlsteardown::makeTlsKey;

        using h2driverprobe::tls_stream_t;
        using h2driverprobe::Http2DriverProbe;
        using h2driverprobe::Http2DriverProbeImpl;

        typedef bl::httpclient::ClientDriverFactoryT< tls_stream_t >            factory_t;

        inline auto protocolName( SAA_in const bl::httpclient::HttpProtocol protocol ) -> std::string
        {
            switch( protocol )
            {
                case bl::httpclient::HttpProtocol::Unknown:
                    return "Unknown";

                case bl::httpclient::HttpProtocol::Http11:
                    return "Http11";

                case bl::httpclient::HttpProtocol::Http2:
                    return "Http2";
            }

            return "?";
        }

        inline auto describe( SAA_in const bl::httpclient::NegotiatedProtocol& negotiated ) -> std::string
        {
            return protocolName( negotiated.protocol() ) + " '" + negotiated.alpn() + "'";
        }

        inline auto stateName( SAA_in const bl::httpclient::ConnectionState state ) -> std::string
        {
            switch( state )
            {
                case bl::httpclient::ConnectionState::Connecting:
                    return "Connecting";

                case bl::httpclient::ConnectionState::Ready:
                    return "Ready";

                case bl::httpclient::ConnectionState::Draining:
                    return "Draining";

                case bl::httpclient::ConnectionState::Closed:
                    return "Closed";
            }

            return "?";
        }

        /**
         * @brief Records a failure with a diagnosis and goes on, so that one run shows every half of a
         * case which fails; otherwise counts the assertion
         */

        inline void chkOrReport(
            SAA_in          const bool                                          condition,
            SAA_in          const std::string&                                  message
            )
        {
            if( ! condition )
            {
                UTF_ERROR_MESSAGE( message );

                return;
            }

            UTF_CHECK( condition );
        }

        /*************************************************************************************
         * The contract: two unstarted test types which write the value and do not publish it
         */

        /**
         * @brief The establishment base, never started, with a value written the way the strand
         * writes it and nothing done after the write
         */

        class UnpublishedTaskBase : public bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( UnpublishedTaskBase )

        public:

            typedef bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >        base_type;

        protected:

            UnpublishedTaskBase(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          base_type::factory_ptr_t                        driverFactory
                )
                :
                base_type(
                    BL_PARAM_FWD( key ),
                    BL_PARAM_FWD( driverFactory ),
                    bl::tasks::ProxyConfig::none(),
                    bl::tasks::ClientConnectionConfig(),
                    false /* logExceptions */
                    )
            {
            }

        public:

            void writeUnpublished( SAA_in bl::httpclient::NegotiatedProtocol negotiated )
            {
                base_type::m_negotiated = BL_PARAM_FWD( negotiated );
            }

            /**
             * @brief Publishes a value, through the protected helper continueAfterConnected( ) uses
             */

            void publish( SAA_in bl::httpclient::NegotiatedProtocol negotiated )
            {
                base_type::publishNegotiated( BL_PARAM_FWD( negotiated ) );
            }
        };

        typedef bl::om::ObjectImpl< UnpublishedTaskBase >                       UnpublishedTaskBaseImpl;

        /**
         * @brief The HTTP/2 driver, never started, with its establishment base's value written and
         * nothing done after the write
         */

        class UnpublishedDriver : public bl::tasks::Http2ConnectionTaskT< tls_stream_t >
        {
            BL_DECLARE_OBJECT_IMPL( UnpublishedDriver )

        public:

            typedef bl::tasks::Http2ConnectionTaskT< tls_stream_t >             base_type;
            typedef bl::tasks::ClientConnectionTaskBaseT< tls_stream_t >        establisher_type;

        protected:

            UnpublishedDriver(
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
                    )
            {
            }

        public:

            void writeUnpublished( SAA_in bl::httpclient::NegotiatedProtocol negotiated )
            {
                establisher_type::m_negotiated = BL_PARAM_FWD( negotiated );
            }

            /**
             * @brief Publishes a value, through the protected helper continueAfterConnected( ) uses
             */

            void publish( SAA_in bl::httpclient::NegotiatedProtocol negotiated )
            {
                establisher_type::publishNegotiated( BL_PARAM_FWD( negotiated ) );
            }
        };

        typedef bl::om::ObjectImpl< UnpublishedDriver >                         UnpublishedDriverImpl;

        /*************************************************************************************
         * The race: a reader off the strand
         */

        /**
         * @brief A thread which reads a connection's negotiated( ) just after the handshake has
         * settled it, with nothing of its own ordering the read after the write
         *
         * THE READ FOLLOWS THE WRITE IN TIME, AND ONLY THE FIXED GETTER ORDERS IT AFTER THE WRITE. The
         * thread is created before the driver is scheduled, and from then until its read it acquires
         * nothing the strand, or anything ordered after the strand's write, has released:
         *
         *   - it is let go by go( ), a RELAXED store, which the case makes as soon as the strand says
         *     the value is written ( NegotiatedSignalProbe ) - after the write, in time, by tens of
         *     microseconds. A relaxed store and load are not synchronization, to the C++ model or to
         *     ThreadSanitizer, and that is the only reason they are relaxed;
         *   - while it waits, and before each read, it locks and unlocks a mutex which no other thread
         *     ever takes. That orders it after nothing but its own past. What it is for is
         *     ThreadSanitizer's own bookkeeping: see below;
         *   - then it reads protocol( ) until it is not Unknown - once, on either side of the fix -
         *     records it and signals.
         *
         * WHY IT IS BUILT THIS WAY, measured and read at the runtime's source (compiler-rt 20.1.0,
         * tsan_rtl.cpp, FindSlotAndLock( ) and SlotAttachAndLock( )). ThreadSanitizer v3 shares 256
         * slots among the threads. A slot handed from one thread to another keeps its identity and its
         * epoch, and when every slot is spent the whole shadow is reset. So an access can lose the
         * record which would show it racing: a thread which synchronizes with nothing for a while has
         * its slot taken over, and a reset can wipe the write. Both are the churn of a TLS handshake,
         * and both need time:
         *
         *   - read in a loop while the handshake runs, the race reported in 1 run of 54;
         *   - read once, let go when the driver's opening write ended - a round trip after the write -
         *     it reported in 101 of 102;
         *   - so the read now follows the write by as little as the case can make it, and the waiting
         *     thread keeps taking a slot of its own. The runs this was measured by are in
         *     logs/astra4/cs9/
         *
         * protocol( ) AND NOT A COPY OF THE WHOLE VALUE: a torn copy of the std::string could crash
         * where a read of the enum only reports, and a report is the verdict. The flag which publishes
         * the value gates the whole object, so what this read shows holds for a copy
         */

        class OffStrandReader
        {
            BL_NO_COPY_OR_MOVE( OffStrandReader )

        public:

            enum : long
            {
                WAIT_STEP_IN_MICROSECONDS           = 20L,
            };

            explicit OffStrandReader( SAA_in const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection )
                :
                m_connection( bl::om::copy( connection ) ),
                m_isGo( false ),
                m_isAbandoned( false ),
                m_seen( bl::httpclient::HttpProtocol::Unknown )
            {
                m_thread.reset( new bl::os::thread( [ this ]() -> void { run(); } ) );
            }

            ~OffStrandReader() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                stop();

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Ends the reader, if it has not read yet, and joins it
             */

            void stop()
            {
                m_isAbandoned.store( true, std::memory_order_relaxed );

                bl::os::safeThreadJoin( *m_thread );
            }

            /**
             * @brief Lets the reader make its one read - relaxed, so that it is ordered after nothing
             */

            void go() NOEXCEPT
            {
                m_isGo.store( true, std::memory_order_relaxed );
            }

            bool waitForRead() const
            {
                return m_read.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            /**
             * @brief What it read - valid once waitForRead( ) has returned true
             */

            bl::httpclient::HttpProtocol seen() const NOEXCEPT
            {
                return m_seen;
            }

        private:

            /**
             * @brief Takes a ThreadSanitizer slot of its own, ordered after nothing but this thread
             */

            void touchOwnLock()
            {
                BL_MUTEX_GUARD( m_ownLock );
            }

            void run()
            {
                /*
                 * WAITING, paced by a short sleep: so that this thread neither starves the handshake
                 * on a small host nor spends its own slot's epochs, and still answers go( ) within tens
                 * of microseconds
                 */

                while( ! m_isGo.load( std::memory_order_relaxed ) )
                {
                    if( m_isAbandoned.load( std::memory_order_relaxed ) )
                    {
                        return;
                    }

                    touchOwnLock();

                    std::this_thread::sleep_for( std::chrono::microseconds( WAIT_STEP_IN_MICROSECONDS ) );
                }

                /*
                 * READING, until the value is settled - the first read is, on either side of the
                 * fix: the probe signals after the write and, with the fix, after the publication
                 */

                for( ;; )
                {
                    if( m_isAbandoned.load( std::memory_order_relaxed ) )
                    {
                        return;
                    }

                    touchOwnLock();

                    const auto protocol = m_connection -> negotiated().protocol();

                    if( bl::httpclient::HttpProtocol::Unknown != protocol )
                    {
                        m_seen = protocol;

                        m_read.signal();

                        return;
                    }

                    std::this_thread::yield();
                }
            }

            const bl::om::ObjPtr< bl::httpclient::ClientConnection >            m_connection;

            std::atomic< bool >                                                 m_isGo;
            std::atomic< bool >                                                 m_isAbandoned;

            /*
             * Taken by the reader thread and by no other
             */

            bl::os::mutex                                                       m_ownLock;

            OneShotSignal                                                       m_read;

            /*
             * Written by the reader thread only, before it signals m_read
             */

            bl::httpclient::HttpProtocol                                        m_seen;

            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        /**
         * @brief The shared driver probe, which also signals as soon as its value is written
         *
         * onProtocolNegotiated( ) is what continueAfterConnected( ) calls once it has written the value
         * - and, with the fix, published it - on the strand, in the same handler. So the signal follows
         * the write by a function call, and a case which waits for it can let a reader go within tens
         * of microseconds of the write
         */

        class NegotiatedSignalProbe : public Http2DriverProbe
        {
            BL_DECLARE_OBJECT_IMPL( NegotiatedSignalProbe )

        public:

            typedef Http2DriverProbe                                            base_type;

        protected:

            OneShotSignal                                                       m_negotiated;

            NegotiatedSignalProbe(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          base_type::factory_ptr_t                        driverFactory
                )
                :
                base_type( BL_PARAM_FWD( key ), BL_PARAM_FWD( driverFactory ) )
            {
            }

            virtual bool onProtocolNegotiated() OVERRIDE
            {
                m_negotiated.signal();

                return base_type::onProtocolNegotiated();
            }

        public:

            bool waitForNegotiated() const
            {
                return m_negotiated.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }
        };

        typedef bl::om::ObjectImpl< NegotiatedSignalProbe >                     NegotiatedSignalProbeImpl;

        /*************************************************************************************
         * U01's route: the request task over the real driver
         */

        /**
         * @brief The shared driver probe, which also signals once a submit( ) has returned and once its
         * handshake has begun
         */

        class RidingDriverProbe : public Http2DriverProbe
        {
            BL_DECLARE_OBJECT_IMPL( RidingDriverProbe )

        public:

            typedef Http2DriverProbe                                            base_type;

        protected:

            OneShotSignal                                                       m_submitted;
            OneShotSignal                                                       m_handshakeBegun;

            /*
             * Written before m_submitted is signalled, and read by the case after waiting for it
             */

            bl::httpclient::stream_handle_t                                     m_handle;

            RidingDriverProbe(
                SAA_in          bl::httpclient::ConnectionKey                   key,
                SAA_in          base_type::factory_ptr_t                        driverFactory
                )
                :
                base_type( BL_PARAM_FWD( key ), BL_PARAM_FWD( driverFactory ) ),
                m_handle( bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE )
            {
            }

            /**
             * @brief The base arms the connect deadline and, with no proxy, starts the handshake before
             * it returns
             */

            virtual bool beginPreHandshakeStage( SAA_in const bl::cpp::bool_callback_t& continueCallback ) OVERRIDE
            {
                const bool result = base_type::beginPreHandshakeStage( continueCallback );

                m_handshakeBegun.signal();

                return result;
            }

        public:

            virtual bl::httpclient::stream_handle_t submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                ) OVERRIDE
            {
                const auto handle = base_type::submit( request, eventSink );

                m_handle = handle;

                m_submitted.signal();

                return handle;
            }

            bool waitForSubmit() const
            {
                return m_submitted.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            bool waitForHandshakeBegun() const
            {
                return m_handshakeBegun.waitFor( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
            }

            bl::httpclient::stream_handle_t submittedHandle() const NOEXCEPT
            {
                return m_handle;
            }
        };

        typedef bl::om::ObjectImpl< RidingDriverProbe >                         RidingDriverProbeImpl;

        /**
         * @brief A pool of one connection: every acquire( ) is answered with it, posted as the contract
         * requires, and every releaseStream( ) is recorded
         */

        class OneDriverPool : public bl::httpclient::ConnectionPool
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( OneDriverPool, bl::httpclient::ConnectionPool )

        protected:

            const bl::om::ObjPtrCopyable< bl::httpclient::ClientConnection >    m_connection;

            mutable bl::os::mutex                                               m_lock;
            mutable bl::os::condition_variable                                  m_cv;
            std::size_t                                                         m_acquires;
            std::vector< bl::httpclient::RequestOutcome >                       m_releases;

            OneDriverPool( SAA_in const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection )
                :
                m_connection( connection ),
                m_acquires( 0U )
            {
            }

        public:

            virtual void acquire(
                SAA_in          const bl::httpclient::ConnectionKey&            key,
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          on_ready_callback_t&&                           onReady
                ) OVERRIDE
            {
                BL_UNUSED( key );
                BL_UNUSED( request );

                {
                    BL_MUTEX_GUARD( m_lock );

                    ++m_acquires;
                }

                const auto connection = m_connection;
                const on_ready_callback_t callback( BL_PARAM_FWD( onReady ) );

                bl::ThreadPoolDefault::getDefault( bl::ThreadPoolId::GeneralPurpose ) ->
                    aioService().post(
                        [ connection, callback ]() -> void
                        {
                            callback( connection, nullptr );
                        }
                        );
            }

            virtual void releaseStream(
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientConnection >& connection,
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::httpclient::RequestOutcome            outcome
                ) NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                BL_UNUSED( connection );
                BL_UNUSED( handle );

                BL_MUTEX_GUARD( m_lock );

                m_releases.push_back( outcome );

                m_cv.notify_all();

                BL_NOEXCEPT_END()
            }

            /**
             * @brief Waits for the first release - signalled inside the lock, so the case has a
             * happens-before with everything the request did before it released
             */

            bool waitForRelease() const
            {
                bl::os::mutex_unique_lock guard( m_lock );

                return m_cv.wait_for(
                    guard,
                    bl::os::chrono::milliseconds( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) ),
                    [ this ]() -> bool
                    {
                        return ! m_releases.empty();
                    }
                    );
            }

            std::size_t acquires() const
            {
                BL_MUTEX_GUARD( m_lock );

                return m_acquires;
            }

            auto releases() const -> std::vector< bl::httpclient::RequestOutcome >
            {
                BL_MUTEX_GUARD( m_lock );

                return m_releases;
            }
        };

        typedef bl::om::ObjectImpl< OneDriverPool >                             OneDriverPoolImpl;

        /**
         * @brief What the factory builds on the fallback - it keeps the connected stream and takes no
         * request, which is all the fallback path needs of it: the pool never adopts it here
         */

        class FallbackStubConnection : public bl::httpclient::ClientConnection
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( FallbackStubConnection, bl::httpclient::ClientConnection )

        protected:

            const bl::httpclient::NegotiatedProtocol                            m_negotiated;
            const factory_t::stream_ref                                         m_connectedStream;

            FallbackStubConnection(
                SAA_in          bl::httpclient::NegotiatedProtocol              negotiated,
                SAA_inout       factory_t::stream_ref&&                         connectedStream
                )
                :
                m_negotiated( BL_PARAM_FWD( negotiated ) ),
                m_connectedStream( BL_PARAM_FWD( connectedStream ) )
            {
            }

        public:

            virtual bl::httpclient::stream_handle_t submit(
                SAA_in          const bl::httpclient::ClientRequest&            request,
                SAA_in          const bl::om::ObjPtr< bl::httpclient::ClientStreamEventSink >& eventSink
                ) OVERRIDE
            {
                BL_UNUSED( request );
                BL_UNUSED( eventSink );

                return bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE;
            }

            virtual void cancel(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const bl::eh::error_code&                       errorCode
                ) NOEXCEPT OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( errorCode );
            }

            virtual void consumed(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in          const std::size_t                               bytes
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( bytes );
            }

            virtual void provideBody(
                SAA_in          const bl::httpclient::stream_handle_t           handle,
                SAA_in_opt      const bl::om::ObjPtr< bl::data::DataBlock >&    data,
                SAA_in          const bool                                      endStream
                ) OVERRIDE
            {
                BL_UNUSED( handle );
                BL_UNUSED( data );
                BL_UNUSED( endStream );
            }

            virtual std::size_t freeStreamSlots() const NOEXCEPT OVERRIDE
            {
                return 0U;
            }

            virtual bl::httpclient::ConnectionState state() const NOEXCEPT OVERRIDE
            {
                return bl::httpclient::ConnectionState::Closed;
            }

            virtual auto negotiated() const NOEXCEPT -> const bl::httpclient::NegotiatedProtocol& OVERRIDE
            {
                return m_negotiated;
            }
        };

        typedef bl::om::ObjectImpl< FallbackStubConnection >                    FallbackStubConnectionImpl;

        /**
         * @brief A factory whose one creator is the HTTP/1.1 fallback's, building the stub above
         */

        inline auto makeFallbackFactory() -> std::shared_ptr< factory_t >
        {
            const auto factory = std::make_shared< factory_t >();

            factory -> registerDriver(
                bl::httpclient::HttpProtocol::Http11,
                [](
                    SAA_in          const bl::httpclient::NegotiatedProtocol&   negotiated,
                    SAA_inout       factory_t::stream_ref&&                     connectedStream,
                    SAA_in          const bl::httpclient::ConnectionKey&        key
                    )
                    -> bl::om::ObjPtr< bl::httpclient::ClientConnection >
                {
                    BL_UNUSED( key );

                    return bl::om::qi< bl::httpclient::ClientConnection >(
                        FallbackStubConnectionImpl::createInstance(
                            bl::cpp::copy( negotiated ),
                            BL_PARAM_FWD( connectedStream )
                            )
                        );
                }
                );

            return factory;
        }

        inline auto makeRequest(
            SAA_in          const bl::httpclient::ConnectionKey&                key
            )
            -> bl::httpclient::ClientRequest
        {
            bl::httpclient::ClientRequest request;

            request.method( std::string( "GET" ) );

            request.url(
                bl::net::Uri::parse(
                    "https://" +
                    key.host +
                    ":" +
                    bl::utils::lexical_cast< std::string >( key.port.value() ) +
                    "/"
                    )
                );

            return request;
        }

        /**
         * @brief How the request's connection is taken to the moment the case looks at the request
         */

        enum class Ride
        {
            /**
             * @brief The handshake is held - a listener which never accepts - and the request is
             * cancelled while it is; the driver is cancelled afterwards, and its terminal answers the
             * queued submit
             */

            CancelDuringAHeldHandshake,

            /**
             * @brief The handshake settles h2 and the driver's opening write, the request's HEADERS
             * with it, is over; then the request is cancelled, and its stream's close answers it
             */

            CancelAfterTheHandshake,

            /**
             * @brief The handshake settles http/1.1, the task hands its stream to the factory's
             * fallback and completes, and its terminal bounces the queued submit
             */

            Fallback,
        };

        /**
         * @brief What the request was seen to be, at one moment
         */

        struct RequestReading
        {
            bool                                                                isFailed;
            bl::eh::error_code                                                  code;
            bool                                                                isOwnFailure;
            bool                                                                isRetryable;
            bl::httpclient::HttpProtocol                                        protocol;
            std::string                                                         alpn;

            RequestReading()
                :
                isFailed( false ),
                isOwnFailure( false ),
                isRetryable( false ),
                protocol( bl::httpclient::HttpProtocol::Unknown )
            {
            }

            auto describe() const -> std::string
            {
                return
                    std::string( isFailed ? "failed " + describeCode( code ) : std::string( "succeeded" ) ) +
                    ( isOwnFailure ? ", own failure" : ", not its own failure" ) +
                    ( isRetryable ? ", retryable" : ", not retryable" ) +
                    ", " +
                    protocolName( protocol ) +
                    " '" +
                    alpn +
                    "'";
            }
        };

        inline bool operator==( SAA_in const RequestReading& lhs, SAA_in const RequestReading& rhs )
        {
            return
                lhs.isFailed == rhs.isFailed &&
                lhs.code == rhs.code &&
                lhs.isOwnFailure == rhs.isOwnFailure &&
                lhs.isRetryable == rhs.isRetryable &&
                lhs.protocol == rhs.protocol &&
                lhs.alpn == rhs.alpn;
        }

        /**
         * @brief Reads the request - only once it has completed, which is what makes the read legal on
         * this thread with no lock (HttpClientRequestTask.h, sinkDelivered( ))
         */

        inline auto readRequest(
            SAA_in          const bl::om::ObjPtr< bl::httpclient::HttpClientRequestTaskImpl >& request,
            SAA_in          const bl::om::ObjPtr< bl::tasks::Task >&            task
            )
            -> RequestReading
        {
            RequestReading reading;

            reading.isFailed = task -> isFailed();

            const auto exception = task -> exception();

            if( exception )
            {
                reading.code = bl::eh::errorCodeFromExceptionPtr( exception );
            }

            reading.isOwnFailure = request -> isOwnFailure();
            reading.isRetryable = request -> isRetryable();
            reading.protocol = request -> response().protocol();
            reading.alpn = request -> response().negotiatedAlpn();

            return reading;
        }

        /**
         * @brief What one ridden request came to, read on the test thread
         */

        struct RideResult
        {
            bool                                                                isSubmitted;
            bl::httpclient::stream_handle_t                                     handle;
            bool                                                                isHandshakeBegun;
            bool                                                                isQuiet;
            bl::httpclient::ConnectionState                                     stateBeforeTheEnd;
            bool                                                                isRequestEnded;
            RequestReading                                                      atCompletion;
            bool                                                                isReleased;
            RequestReading                                                      afterRelease;
            std::vector< bl::httpclient::RequestOutcome >                       releases;
            std::size_t                                                         acquires;
            bool                                                                isDriverStopped;
            std::string                                                         peerRecords;

            RideResult()
                :
                isSubmitted( false ),
                handle( bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE ),
                isHandshakeBegun( false ),
                isQuiet( false ),
                stateBeforeTheEnd( bl::httpclient::ConnectionState::Connecting ),
                isRequestEnded( false ),
                isReleased( false ),
                acquires( 0U ),
                isDriverStopped( false )
            {
            }

            auto describe() const -> std::string
            {
                std::string outcomes;

                for( const auto outcome : releases )
                {
                    outcomes +=
                        ( outcomes.empty() ? "" : " " ) +
                        bl::utils::lexical_cast< std::string >( static_cast< unsigned >( outcome ) );
                }

                return
                    std::string( "submitted " ) +
                    ( isSubmitted ? "yes" : "no" ) +
                    ", handle " +
                    bl::utils::lexical_cast< std::string >( handle ) +
                    ", handshake begun " +
                    ( isHandshakeBegun ? "yes" : "no" ) +
                    ", quiet " +
                    ( isQuiet ? "yes" : "no" ) +
                    ", state before the end " +
                    stateName( stateBeforeTheEnd ) +
                    ", request ended " +
                    ( isRequestEnded ? "yes" : "no" ) +
                    ", at completion [" +
                    atCompletion.describe() +
                    "], released " +
                    ( isReleased ? "yes" : "no" ) +
                    ", after the release [" +
                    afterRelease.describe() +
                    "], acquires " +
                    bl::utils::lexical_cast< std::string >( acquires ) +
                    ", releases [" +
                    outcomes +
                    "], driver stopped " +
                    ( isDriverStopped ? "yes" : "no" ) +
                    ", peer " +
                    peerRecords;
            }
        };

        inline auto joinRecords( SAA_in const std::vector< std::string >& records ) -> std::string
        {
            std::string joined;

            for( const auto& record : records )
            {
                joined += ( joined.empty() ? "" : " | " ) + record;
            }

            return joined;
        }

        /**
         * @brief Runs one request over the real driver, taken to the moment 'ride' names, and ends it
         *
         * Every wait is bounded and every one is for the event its reading is about: the submit, the
         * handshake begun, the opening write over, the request's completion, the release, the driver's
         * stop. Nothing sleeps
         */

        inline auto runRide( SAA_in const Ride ride ) -> RideResult
        {
            using namespace bl;
            using namespace bl::tasks;
            using namespace bl::httpclient;

            RideResult result;

            /*
             * The peer the ride names: a listener which never accepts for the held handshake - the
             * kernel completes the connect from its backlog and nothing answers the ClientHello, as
             * utf_baselib_h2client10 holds one - and otherwise a TLS peer selecting h2 or http/1.1
             */

            asio::io_service ioService;

            cpp::SafeUniquePtr< asio::ip::tcp::acceptor > listener;
            cpp::SafeUniquePtr< TlsEndingPeer > peer;

            ConnectionKey key;

            if( Ride::CancelDuringAHeldHandshake == ride )
            {
                listener = cpp::SafeUniquePtr< asio::ip::tcp::acceptor >::attach(
                    new asio::ip::tcp::acceptor(
                        ioService,
                        asio::ip::tcp::endpoint( asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                        )
                    );

                key.scheme = "https";
                key.host = "127.0.0.1";
                key.port = listener -> local_endpoint().port();
            }
            else
            {
                TlsEndingPeer::Script script( TlsEndingPeer::Ending::AwaitTheClient );

                script.alpnPreference.push_back( Ride::Fallback == ride ? "http/1.1" : "h2" );

                peer.reset( new TlsEndingPeer( script ) );

                key = makeTlsKey( peer -> port() );
            }

            const auto driver = RidingDriverProbeImpl::createInstance(
                cpp::copy( key ),
                Ride::Fallback == ride ? makeFallbackFactory() : std::make_shared< factory_t >()
                );

            const auto connection = om::qi< ClientConnection >( driver );
            const auto driverTask = om::qi< Task >( driver );

            const auto pool = OneDriverPoolImpl::createInstance( connection );

            const auto request = HttpClientRequestTaskImpl::createInstance(
                makeRequest( key ),
                cpp::copy( key ),
                om::qi< ConnectionPool >( pool )
                );

            const auto requestTask = om::qi< Task >( request );

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    /*
                     * THE REQUEST FIRST, and the driver only once the request has submitted to it:
                     * the submit is then queued behind the handshake whatever the scheduler does
                     */

                    eq -> push_back( requestTask );

                    result.isSubmitted = driver -> waitForSubmit();
                    result.handle = driver -> submittedHandle();

                    eq -> push_back( driverTask );

                    if( Ride::CancelDuringAHeldHandshake == ride )
                    {
                        result.isHandshakeBegun = driver -> waitForHandshakeBegun();
                    }
                    else if( Ride::CancelAfterTheHandshake == ride )
                    {
                        result.isQuiet =
                            driver -> waitForQuiet( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );
                    }

                    result.stateBeforeTheEnd = connection -> state();

                    if( Ride::Fallback != ride )
                    {
                        requestTask -> requestCancel();
                    }

                    eq -> wait( requestTask );

                    result.isRequestEnded = true;
                    result.atCompletion = readRequest( request, requestTask );

                    /*
                     * The held handshake's slot comes back only from the driver's terminal, which
                     * answers the queued submit; the other two come back from the stream's own end
                     */

                    if( Ride::CancelDuringAHeldHandshake == ride )
                    {
                        driverTask -> requestCancel();
                    }

                    result.isReleased = pool -> waitForRelease();
                    result.afterRelease = readRequest( request, requestTask );

                    if( Ride::CancelAfterTheHandshake == ride )
                    {
                        driverTask -> requestCancel();
                    }

                    result.isDriverStopped =
                        driver -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                    if( peer )
                    {
                        peer -> release();
                    }

                    eq -> wait( driverTask );
                }
                );

            result.releases = pool -> releases();
            result.acquires = pool -> acquires();

            if( peer )
            {
                result.peerRecords = joinRecords( peer -> records() );
            }

            return result;
        }

        /**
         * @brief What all three rides assert about the plumbing, before what each asserts about the
         * value: the request submitted onto the driver, completed, and gave its one slot back once
         */

        inline void chkRodeAndReleasedOnce(
            SAA_in          const RideResult&                                   result,
            SAA_in          const std::string&                                  which
            )
        {
            chkOrFail(
                result.isSubmitted &&
                    bl::httpclient::ClientConnection::INVALID_STREAM_HANDLE != result.handle,
                which + ": the request was never submitted onto the driver; " + result.describe()
                );

            chkOrFail(
                result.isRequestEnded && result.atCompletion.isFailed,
                which + ": the request did not end failed; " + result.describe()
                );

            chkOrFail(
                result.isReleased && 1U == result.releases.size() && 1U == result.acquires,
                which + ": the request did not give its one slot back exactly once; " + result.describe()
                );

            chkOrFail(
                result.atCompletion == result.afterRelease,
                which + ": what the request reported changed after it completed; " + result.describe()
                );

            chkOrFail(
                result.isDriverStopped,
                which + ": the driver never stopped; " + result.describe()
                );
        }

    } // negotiatedpublication

} // utest

/**
 * @brief RED FOR THE CONTRACT - a value written and not published is read by neither getter; once
 * published, it is what a reader on another thread gets
 *
 * The establishment base's negotiated( ), and the HTTP/2 driver's through ClientConnection. Each check
 * is made without stopping the case, so a run shows every one. One thread, no timing, until the value
 * is published: before the fix both getters returned what was written
 *
 * THE PUBLISH HALF is read from a thread started after the publication, whose start orders the read
 * after it. On the driver it is also the one control which separates (a'') from the withdrawn (a'):
 * the driver never started, so it still reads Connecting, and the settled value must be read anyway
 * (notes/plans/issues/astra4-cs9-negotiated-publication-design.md, section 7.1)
 */

UTF_AUTO_TEST_CASE( NegotiatedPublication_AValueWrittenButNotPublishedIsNotReadTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::negotiatedpublication;

    const auto factory = std::make_shared< factory_t >();

    const auto taskBase = UnpublishedTaskBaseImpl::createInstance( makeTlsKey( 443U ), factory );

    taskBase -> writeUnpublished( NegotiatedProtocol::fromAlpn( "h2" ) );

    const auto seenByTheBase = taskBase -> negotiated();

    chkOrReport(
        HttpProtocol::Unknown == seenByTheBase.protocol() && ! seenByTheBase.hasAlpn(),
        "the establishment base's negotiated( ) returned a value which was written and never "
            "published: " + describe( seenByTheBase )
        );

    const auto driver = UnpublishedDriverImpl::createInstance( makeTlsKey( 443U ), factory );

    driver -> writeUnpublished( NegotiatedProtocol::fromAlpn( "h2" ) );

    const auto connection = om::qi< ClientConnection >( driver );

    const auto seenByTheDriver = connection -> negotiated();

    chkOrReport(
        HttpProtocol::Unknown == seenByTheDriver.protocol() && ! seenByTheDriver.hasAlpn(),
        "the HTTP/2 driver's negotiated( ), read through ClientConnection, returned a value which was "
            "written and never published: " + describe( seenByTheDriver )
        );

    taskBase -> publish( NegotiatedProtocol::fromAlpn( "h2" ) );
    driver -> publish( NegotiatedProtocol::fromAlpn( "h2" ) );

    const auto stateAtThePublication = connection -> state();

    NegotiatedProtocol publishedByTheBase;
    NegotiatedProtocol publishedByTheDriver;

    {
        os::thread reader(
            [ & ]() -> void
            {
                publishedByTheBase = taskBase -> negotiated();
                publishedByTheDriver = connection -> negotiated();
            }
            );

        os::safeThreadJoin( reader );
    }

    chkOrReport(
        ConnectionState::Connecting == stateAtThePublication,
        "the unstarted driver did not read Connecting, so this is not the control it is meant to be: " +
            stateName( stateAtThePublication )
        );

    chkOrReport(
        HttpProtocol::Http2 == publishedByTheBase.protocol() &&
            std::string( "h2" ) == publishedByTheBase.alpn(),
        "the establishment base's negotiated( ) did not return the value it published: " +
            describe( publishedByTheBase )
        );

    chkOrReport(
        HttpProtocol::Http2 == publishedByTheDriver.protocol() &&
            std::string( "h2" ) == publishedByTheDriver.alpn(),
        "the HTTP/2 driver's negotiated( ), read through ClientConnection while it reads Connecting, did "
            "not return the value it published: " + describe( publishedByTheDriver )
        );
}

/**
 * @brief RED FOR THE RACE, UNDER THREADSANITIZER - a reader off the strand, ordered after nothing the
 * strand did, reads the real driver's negotiated( ) once a real handshake has settled it, and sees h2
 *
 * The case passes on both sides of the fix. Its verdict is the sanitizer's: before the fix the reader
 * read the byte continueAfterConnected( ) wrote, just after the write and with no happens-before from
 * it - see OffStrandReader and NegotiatedSignalProbe - so the read was reported against the write.
 * With the fix its load of the published flag reads true, and synchronizes with the store which
 * follows the write, before it reads the value
 */

UTF_AUTO_TEST_CASE( NegotiatedPublication_AReaderOffTheStrandSeesTheSettledValueTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::httpclient;
    using namespace utest::negotiatedpublication;

    TlsEndingPeer::Script script( TlsEndingPeer::Ending::AwaitTheClient );

    script.alpnPreference.push_back( "h2" );

    TlsEndingPeer peer( script );

    const auto driver = NegotiatedSignalProbeImpl::createInstance(
        makeTlsKey( peer.port() ),
        std::make_shared< factory_t >()
        );

    const auto connection = om::qi< ClientConnection >( driver );
    const auto task = om::qi< Task >( driver );

    bool isNegotiated = false;
    bool isRead = false;
    bool isQuiet = false;
    bool isStopped = false;

    HttpProtocol seen = HttpProtocol::Unknown;

    {
        /*
         * Created BEFORE the driver is scheduled, so that nothing the strand does is in its past
         */

        OffStrandReader reader( connection );

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                eq -> push_back( task );

                /*
                 * The strand has written the value: the reader is let go after the write, in time
                 */

                isNegotiated = driver -> waitForNegotiated();

                reader.go();

                isRead = reader.waitForRead();

                if( isRead )
                {
                    seen = reader.seen();
                }

                /*
                 * The driver is cancelled once its opening write is over, which is the ending
                 * utf_baselib_h2client10 characterizes
                 */

                isQuiet = driver -> waitForQuiet( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                task -> requestCancel();

                isStopped = driver -> waitForStop( static_cast< std::size_t >( WAIT_IN_MILLISECONDS ) );

                peer.release();

                eq -> wait( task );
            }
            );

        reader.stop();
    }

    const std::string readings =
        std::string( "negotiated " ) +
        ( isNegotiated ? "yes" : "no" ) +
        ", read " +
        ( isRead ? "yes" : "no" ) +
        ", saw " +
        protocolName( seen ) +
        ", quiet " +
        ( isQuiet ? "yes" : "no" ) +
        ", stopped " +
        ( isStopped ? "yes" : "no" ) +
        ", peer " +
        joinRecords( peer.records() );

    chkOrFail( isNegotiated, "the handshake never settled a protocol; " + readings );

    chkOrFail(
        isRead && HttpProtocol::Http2 == seen,
        "the reader did not read the settled h2; " + readings
        );

    chkOrFail( isQuiet && isStopped, "the driver did not run to its end; " + readings );
}

/**
 * @brief CHARACTERIZATION, U01's ROUTE - a request riding a driver whose handshake is held, cancelled
 * while it is, reports Unknown with no identifier; its cancel stays the answer, and its slot comes back
 * once, from the driver's terminal
 *
 * Green on both sides of the fix: nothing is ever written here, since the handshake never completes. The
 * race on this route is the reader case's
 */

UTF_AUTO_TEST_CASE( NegotiatedPublication_ACancelDuringAHeldHandshakeReportsUnknownTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::negotiatedpublication;

    const auto result = runRide( Ride::CancelDuringAHeldHandshake );

    chkOrFail(
        result.isHandshakeBegun && ConnectionState::Connecting == result.stateBeforeTheEnd,
        "the request was not cancelled during a held handshake; " + result.describe()
        );

    chkRodeAndReleasedOnce( result, "a cancel during a held handshake" );

    chkOrFail(
        asio::error::operation_aborted == result.atCompletion.code && result.atCompletion.isOwnFailure,
        "the request did not end with its own cancel; " + result.describe()
        );

    chkOrFail(
        HttpProtocol::Unknown == result.atCompletion.protocol && result.atCompletion.alpn.empty(),
        "the request did not report Unknown with no identifier; " + result.describe()
        );
}

/**
 * @brief CHARACTERIZATION, THE CONTROL WHICH SEPARATES THE FIX FROM AN OVER-BROAD ONE - the same cancel,
 * once the handshake has settled h2 and the opening write is over, reports h2
 *
 * Green on both sides: the read follows the write through the probe's quiet signal and the request's
 * mailbox
 */

UTF_AUTO_TEST_CASE( NegotiatedPublication_ACancelAfterTheHandshakeReportsH2Tests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::negotiatedpublication;

    const auto result = runRide( Ride::CancelAfterTheHandshake );

    chkOrFail(
        result.isQuiet && ConnectionState::Ready == result.stateBeforeTheEnd,
        "the request was not cancelled after the handshake; " + result.describe()
        );

    chkRodeAndReleasedOnce( result, "a cancel after the handshake" );

    chkOrFail(
        asio::error::operation_aborted == result.atCompletion.code && result.atCompletion.isOwnFailure,
        "the request did not end with its own cancel; " + result.describe()
        );

    chkOrFail(
        HttpProtocol::Http2 == result.atCompletion.protocol &&
            std::string( "h2" ) == result.atCompletion.alpn,
        "the request did not report h2; " + result.describe()
        );
}

/**
 * @brief CHARACTERIZATION, THE FALLBACK - the peer selects http/1.1, the task hands its stream to the
 * factory's fallback and completes, and the request it bounces reports Http11, retryable
 *
 * Green on both sides: the bounce is posted to the request after the write
 */

UTF_AUTO_TEST_CASE( NegotiatedPublication_AFallbackToHttp11ReportsHttp11Tests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::negotiatedpublication;

    const auto result = runRide( Ride::Fallback );

    chkRodeAndReleasedOnce( result, "the fallback" );

    chkOrFail(
        ! result.atCompletion.isOwnFailure && result.atCompletion.isRetryable,
        "the request was not bounced by the fallback, retryable; " + result.describe()
        );

    chkOrFail(
        HttpProtocol::Http11 == result.atCompletion.protocol &&
            std::string( "http/1.1" ) == result.atCompletion.alpn,
        "the request did not report Http11; " + result.describe()
        );
}

#endif /* __UTEST_TESTNEGOTIATEDPUBLICATION_H_ */
