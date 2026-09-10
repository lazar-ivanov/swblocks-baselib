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

#ifndef __UTEST_TESTTASKUTILS_H_
#define __UTEST_TESTTASKUTILS_H_

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>

#include <baselib/transfer/SendRecvContext.h>

#include <baselib/messaging/AsyncDataChunkStorage.h>
#include <baselib/messaging/DataChunkStorage.h>
#include <baselib/messaging/TcpBlockTransferClient.h>
#include <baselib/messaging/TcpBlockServerDataChunkStorage.h>

#include <baselib/tasks/utils/DirectoryScannerControlToken.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/SimpleTaskControlToken.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <unordered_map>
#include <cstring>

namespace utest
{
    template
    <
        typename E = void
    >
    class BackendImplTestT :
        public bl::messaging::BackendProcessingBase,
        public bl::data::DataChunkStorage
    {
        BL_DECLARE_OBJECT_IMPL( BackendImplTestT )

        BL_QITBL_BEGIN()
            BL_QITBL_ENTRY_CHAIN_BASE( bl::messaging::BackendProcessingBase )
            BL_QITBL_ENTRY( bl::data::DataChunkStorage )
        BL_QITBL_END( bl::data::DataChunkStorage )

    private:

        typedef BackendImplTestT< E >                                       this_type;
        typedef bl::messaging::BackendProcessingBase                        base_type;

        typedef bl::messaging::BackendProcessing                            BackendProcessing;

        typedef BackendProcessing::CommandId                                CommandId;
        typedef BackendProcessing::OperationId                              OperationId;

        typedef bl::data::DataBlock                                         DataBlock;

        typedef bl::om::ObjectImpl< bl::om::BoxedValueObject< std::atomic< long > > >
            session_refs_t;

        typedef std::unordered_map< bl::uuid_t, bl::om::ObjPtr< session_refs_t > >
            session_map_t;

        const bl::om::ObjPtr< bl::data::DataBlock >                         m_data;

        /*
         * The failure injection hook - when this is not nil, every storage call for that
         * chunk id fails with eh::errc::no_such_file_or_directory before it does any work
         */

        bl::uuid_t                                                          m_invalidChunkId;
        std::atomic< std::size_t >                                          m_injectedFailures;
        bl::cpp::ScalarTypeIniter< bool >                                   m_expectRealData;
        bl::cpp::ScalarTypeIniter< bool >                                   m_noisyMode;
        bl::cpp::ScalarTypeIniter< bool >                                   m_storageDisabled;

        std::atomic< std::size_t >                                          m_loadCalls;
        std::atomic< std::size_t >                                          m_saveCalls;
        std::atomic< std::size_t >                                          m_removeCalls;
        std::atomic< std::size_t >                                          m_flushCalls;

        bl::uuid_t                                                          m_sourcePeerId;
        bl::uuid_t                                                          m_targetPeerId;

        /*
         * The storage interface below is invoked on thread pool worker threads, where a
         * REQUIRE-level Boost.Test assertion would terminate the process instead of failing
         * the test case, so the invariants are recorded here and asserted by the consumer
         * on the main test thread via assertions().requireNone()
         */

        DeferredAssertions                                                  m_assertions;

    protected:

        BackendImplTestT( SAA_in_opt const std::size_t blockCapacity = bl::data::DataBlock::defaultCapacity() )
            :
            m_data( initDataBlock( bl::data::DataBlock::createInstance( blockCapacity ) ) ),
            m_invalidChunkId( bl::uuids::nil() ),
            m_injectedFailures( 0U ),
            m_loadCalls( 0U ),
            m_saveCalls( 0U ),
            m_removeCalls( 0U ),
            m_flushCalls( 0U ),
            m_sourcePeerId( bl::uuids::nil() ),
            m_targetPeerId( bl::uuids::nil() )
        {
        }

        void chkStorageDisabled()
        {
            if( m_storageDisabled )
            {
                BL_THROW_EC(
                    bl::eh::errc::make_error_code( bl::eh::errc::operation_not_permitted ),
                    BL_MSG()
                        << "The storage interface is disabled"
                    );
            }
        }

        void chkInvalidChunkId( SAA_in const bl::uuid_t& chunkId )
        {
            if( m_invalidChunkId != bl::uuids::nil() && chunkId == m_invalidChunkId )
            {
                ++m_injectedFailures;

                /*
                 * Note that this must be a ServerErrorException and not just any exception
                 * carrying the error code: TcpBlockTransferServerConnection::chk4ServerErrors()
                 * propagates only ServerErrorException back to the client and treats every other
                 * exception as a fatal server condition which takes the acceptor down
                 *
                 * This mirrors DataChunkStorageFilesystem::throwChunkDoesNotExist() exactly, which
                 * is what a real storage backend does for a chunk which is not there
                 */

                BL_THROW(
                    bl::ServerErrorException()
                        << bl::eh::errinfo_error_code(
                            bl::eh::errc::make_error_code( bl::eh::errc::no_such_file_or_directory )
                            )
                        << bl::eh::errinfo_error_uuid( chunkId ),
                    BL_MSG()
                        << "Injected failure for chunk "
                        << chunkId
                    );
            }
        }

    public:

        static bl::om::ObjPtr< bl::data::DataBlock > initDataBlock(
            SAA_in bl::om::ObjPtr< bl::data::DataBlock >&& dataOut = bl::data::DataBlock::createInstance()
            )
        {
            dataOut -> setSize( dataOut -> capacity() );

            const auto size = dataOut -> size();
            const auto data = dataOut -> begin();

            for( std::size_t i = 0; i < size; ++i )
            {
                data[ i ] = i % 128;
            }

            return std::move( dataOut );
        }

        static void verifyData( SAA_in const bl::om::ObjPtrCopyable< bl::data::DataBlock >& dataIn )
        {
            BL_ASSERT( dataIn );

            /*
             * Check the block over its full length rather than just a prefix of it - a framing
             * or truncation regression past the first few bytes, and a recycled block whose head
             * happens to match, are both invisible to a prefix check
             */

            const auto data = dataIn -> begin();
            const auto size = dataIn -> size();

            std::size_t invalidPos = size;

            for( std::size_t i = 0; i < size; ++i )
            {
                 if( ( i % 128 ) != ( std::size_t )( data[ i ] ) )
                 {
                     invalidPos = i;
                     break;
                 }
            }

            BL_CHK(
                false,
                size == invalidPos,
                BL_MSG()
                    << "Data block is invalid at offset "
                    << invalidPos
                    << " of "
                    << size
                    << "; expected "
                    << ( invalidPos % 128 )
                    << " but the actual value is "
                    << ( std::size_t )( data[ invalidPos ] )
                );
        }

        static bool areBlocksEqual(
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&    block1,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&    block2
            )
        {
            if( block1 -> size() != block2 -> size() )
            {
                return false;
            }

            return ( 0 == std::memcmp( block1 -> pv(), block2 -> pv(), block1 -> size() ) );
        }

        const bl::om::ObjPtr< bl::data::DataBlock >& getData() const NOEXCEPT
        {
            return m_data;
        }

        void setExpectRealData( SAA_in const bool expectRealData ) NOEXCEPT
        {
            m_expectRealData = expectRealData;
        }

        void setNoisyMode( SAA_in const bool noisyMode ) NOEXCEPT
        {
            m_noisyMode = noisyMode;
        }

        void setInvalidChunkId( SAA_in const bl::uuid_t& invalidChunkId ) NOEXCEPT
        {
            m_invalidChunkId = invalidChunkId;
        }

        const bl::uuid_t& invalidChunkId() const NOEXCEPT
        {
            return m_invalidChunkId;
        }

        std::size_t injectedFailures() const NOEXCEPT
        {
            return m_injectedFailures;
        }

        void setStorageDisabled( SAA_in const bool storageDisabled ) NOEXCEPT
        {
            m_storageDisabled = storageDisabled;
        }

        void resetStats() NOEXCEPT
        {
            m_loadCalls = 0U;
            m_saveCalls = 0U;
            m_removeCalls = 0U;
            m_flushCalls = 0U;
            m_injectedFailures = 0U;
        }

        std::size_t loadCalls() const NOEXCEPT
        {
            return m_loadCalls;
        }

        std::size_t saveCalls() const NOEXCEPT
        {
            return m_saveCalls;
        }

        std::size_t removeCalls() const NOEXCEPT
        {
            return m_removeCalls;
        }

        std::size_t flushCalls() const NOEXCEPT
        {
            return m_flushCalls;
        }

        const DeferredAssertions& assertions() const NOEXCEPT
        {
            return m_assertions;
        }

        const bl::uuid_t& sourcePeerId() const NOEXCEPT
        {
            return m_sourcePeerId;
        }

        void sourcePeerId( SAA_in const bl::uuid_t& sourcePeerId ) NOEXCEPT
        {
            m_sourcePeerId = sourcePeerId;
        }

        const bl::uuid_t& targetPeerId() const NOEXCEPT
        {
            return m_targetPeerId;
        }

        void targetPeerId( SAA_in const bl::uuid_t& targetPeerId ) NOEXCEPT
        {
            m_targetPeerId = targetPeerId;
        }

        /*
         * Storage support (load/save)
         */

        virtual void load(
            SAA_in                  const bl::uuid_t&                               sessionId,
            SAA_in                  const bl::uuid_t&                               chunkId,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&    data
            ) OVERRIDE
        {
            BL_UNUSED( sessionId );

            chkStorageDisabled();
            chkInvalidChunkId( chunkId );

            UTF_RECORD( m_assertions, chunkId != bl::uuids::nil() );

            if( m_noisyMode )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Data chunk '"
                        << chunkId <<
                        "' loaded from the store"
                    );
            }

            ++m_loadCalls;

            UTF_RECORD( m_assertions, m_data -> size() <= data -> capacity() );

            ::memcpy( data -> pv(), m_data -> pv(), m_data -> size() );
            data -> setSize( m_data -> size() );
        }

        virtual void save(
            SAA_in                  const bl::uuid_t&                               sessionId,
            SAA_in                  const bl::uuid_t&                               chunkId,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&    data
            ) OVERRIDE
        {
            BL_UNUSED( sessionId );

            chkStorageDisabled();
            chkInvalidChunkId( chunkId );

            UTF_RECORD( m_assertions, chunkId != bl::uuids::nil() );

            if( ! m_expectRealData )
            {
                UTF_RECORD( m_assertions, m_data -> size() == data -> size() );

                verifyData( data );

                /*
                 * A block which has the right length and follows the pattern can still be the
                 * contents of a different block instance, so also compare it byte for byte
                 * against the reference block
                 */

                UTF_RECORD( m_assertions, areBlocksEqual( m_data, data ) );
            }

            if( m_noisyMode )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Data chunk '"
                        << chunkId
                        << "' stored in the store; block size: "
                        << data -> size()
                    );
            }

            ++m_saveCalls;
        }

        virtual void remove(
            SAA_in                  const bl::uuid_t&                               sessionId,
            SAA_in                  const bl::uuid_t&                               chunkId
            ) OVERRIDE
        {
            BL_UNUSED( sessionId );

            chkStorageDisabled();
            chkInvalidChunkId( chunkId );

            UTF_RECORD( m_assertions, chunkId != bl::uuids::nil() );

            if( m_noisyMode )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Data chunk '"
                        << chunkId
                        << "' removed from the store"
                    );
            }

            ++m_removeCalls;
        }

        virtual void flushPeerSessions( SAA_in const bl::uuid_t& peerId ) OVERRIDE
        {
            chkStorageDisabled();

            if( m_noisyMode )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Flush of peer sessions requested for peerId '"
                        << peerId
                        << "'"
                    );
            }

            ++m_flushCalls;
        }

        /*
         * Implement also BackendProcessing interface
         */

        void backendProcessingSyncStep1(
            SAA_in                  const OperationId                               operationId,
            SAA_in                  const CommandId                                 commandId,
            SAA_in                  const bl::uuid_t&                               sessionId,
            SAA_in                  const bl::uuid_t&                               chunkId,
            SAA_inout               const bl::om::ObjPtrCopyable< DataBlock >&      data
            )
        {
            switch( operationId )
            {
                default:
                    BL_RT_ASSERT( false, "Invalid operation id" );
                    break;

                case OperationId::Get:
                    load( sessionId, chunkId, data );
                    break;

                case OperationId::Put:
                    save( sessionId, chunkId, data );
                    break;

                case OperationId::Command:
                    {
                        switch( commandId )
                        {
                            default:
                                BL_RT_ASSERT( false, "Invalid command id" );
                                break;

                            case CommandId::Remove:
                                remove( sessionId, chunkId );
                                break;

                            case CommandId::FlushPeerSessions:
                                flushPeerSessions( bl::uuids::nil() /* peerId */ );
                                break;
                        }
                    }
                    break;
            }
        }

        void backendProcessingSyncStep2(
            SAA_in_opt              const bl::uuid_t&                               sourcePeerId,
            SAA_in_opt              const bl::uuid_t&                               targetPeerId
            )
        {
            m_sourcePeerId = sourcePeerId;
            m_targetPeerId = targetPeerId;
        }

        virtual auto createBackendProcessingTask(
            SAA_in                  const OperationId                               operationId,
            SAA_in                  const CommandId                                 commandId,
            SAA_in                  const bl::uuid_t&                               sessionId,
            SAA_in                  const bl::uuid_t&                               chunkId,
            SAA_in_opt              const bl::uuid_t&                               sourcePeerId,
            SAA_in_opt              const bl::uuid_t&                               targetPeerId,
            SAA_inout               const bl::om::ObjPtr< DataBlock >&              data
            )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            typedef bl::om::ObjPtrCopyable
            <
                this_type,
                BackendProcessing /* AddRef() interface */
            >
            copyable_ptr_t;

            const auto copySelf = copyable_ptr_t::acquireRef( this );

            auto taskImpl = bl::tasks::SimpleTaskImpl::createInstance(
                bl::cpp::bind(
                    &this_type::backendProcessingSyncStep1,
                    copySelf,
                    operationId,
                    commandId,
                    sessionId,
                    chunkId,
                    bl::om::ObjPtrCopyable< DataBlock >( data )
                    )
                );

            taskImpl -> setContinuationCallback(
                [ = ]( SAA_in bl::tasks::Task* finishedTask ) -> bl::om::ObjPtr< bl::tasks::Task >
                {
                    BL_UNUSED( finishedTask );

                    return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                        bl::cpp::bind(
                            &this_type::backendProcessingSyncStep2,
                            copySelf,
                            sourcePeerId,
                            targetPeerId
                            )
                        );
                }
                );

            return bl::om::moveAs< bl::tasks::Task >( taskImpl );
        }

        /*
         * Implement bl::Disposable interface
         */

        virtual void dispose() NOEXCEPT OVERRIDE
        {
            base_type::dispose();
        }
    };

    typedef bl::om::ObjectImpl< BackendImplTestT<> > BackendImplTestImpl;

    /**
     * @brief class ScanningControlImpl
     */

    template
    <
        typename E = void
    >
    class ScanningControlT : public bl::tasks::DirectoryScannerControlToken
    {
        BL_CTR_DEFAULT( ScanningControlT, protected )
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( ScanningControlT, bl::tasks::DirectoryScannerControlToken )

    public:

        virtual bool isCanceled() const NOEXCEPT OVERRIDE
        {
            return false;
        }

        virtual bool isErrorAllowed( SAA_in const bl::eh::error_code& code ) const NOEXCEPT OVERRIDE
        {
            return ( bl::eh::errc::permission_denied == code.default_error_condition() );
        }

        virtual bool isEntryAllowed( SAA_in const bl::fs::directory_entry& entry ) OVERRIDE
        {
            BL_UNUSED( entry );

            return true;
        }

        static void dumpEntryInfo( SAA_in const bl::fs::directory_entry& entry )
        {
            const auto status = entry.symlink_status();

            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << ( bl::fs::is_other( status ) ? "[O]" : ( bl::fs::is_symlink( status ) ? "[S]" : ( bl::fs::is_directory( status ) ? "[D]" : "[F]" ) ) )
                    << " "
                    << entry.path()
                );
        }

    };

    typedef bl::om::ObjectImpl< ScanningControlT<> > ScanningControlImpl;

    /**
     * @brief BlocksReceiver class
     */

    template
    <
        typename BASE = bl::om::ObjectDefaultBase
    >
    class BlocksReceiver :
        public BASE
    {
        BL_CTR_DEFAULT( BlocksReceiver, protected )
        BL_DECLARE_OBJECT_IMPL( BlocksReceiver )

    protected:

        const bl::om::ObjPtr< bl::data::datablocks_pool_type >                  m_dataBlocksPool;
        bl::cpp::ScalarTypeIniter< std::uint64_t >                              m_totalBlocks;
        bl::cpp::ScalarTypeIniter< std::uint64_t >                              m_totalDataSize;

        BlocksReceiver( SAA_in const bl::om::ObjPtr< bl::data::datablocks_pool_type >& dataBlocksPool )
            :
            m_dataBlocksPool( bl::om::copy( dataBlocksPool ) )
        {
            /*
             * Shared data pool must be provided - see the note at the definition of
             * datablocks_pool_type above.
             */

            BL_ASSERT( m_dataBlocksPool );
        }

    public:

        bool onDataArrived( SAA_in const bl::cpp::any& value ) NOEXCEPT
        {
            auto* chunkId = bl::cpp::any_cast< bl::uuid_t >( &value );

            if( ! chunkId )
            {
                auto dataBlock = bl::cpp::any_cast< bl::data::DataChunkBlock >( value ).data;

                /*
                 * Just collect statistics and return the block in the shared pool
                 */

                m_totalDataSize += dataBlock -> size();
                m_dataBlocksPool -> put( dataBlock.detachAsUnique() );
            }

            ++m_totalBlocks;

            return true;
        }

        void logResults() const
        {
            BL_LOG_MULTILINE(
                bl::Logging::debug(),
                BL_MSG()
                    << "\nBlocksReceiver::logResults():\n"
                    << "\n    totalBlocks: "
                    << m_totalBlocks
                    << "\n    totalDataSize: "
                    << m_totalDataSize
                    << "\n"
                );
        }
    };

    /**
     * @brief class TestTaskUtils
     */

    template
    <
        typename E = void
    >
    class TestTaskUtilsT
    {
        BL_DECLARE_STATIC( TestTaskUtilsT )

    public:

        /**
         * @brief Blocks until an acceptor is ready to serve, or fails the test case
         *
         * An acceptor becomes ready only when it reaches m_acceptor -> listen() in
         * TcpBaseTasks.h, which happens after a DNS resolve, and m_localEndpoint has no public
         * accessor, so there is no in-process readiness observable; a bounded poll of short
         * lived TCP connects is therefore the readiness signal
         *
         * Note that a bare TCP connect is sufficient even for the SSL acceptors because they
         * accept the connection before the protocol handshake begins - the probe deliberately
         * does not attempt a handshake
         */

        static void waitForAcceptorReady(
            SAA_in              const std::string&                                          readinessHost,
            SAA_in              const unsigned short                                        readinessPort,
            SAA_in_opt          const long                                                  timeoutInSeconds = 30L
            )
        {
            using namespace bl;
            using namespace tasks;

            typedef TcpConnectionEstablisherConnectorImpl< TcpSocketAsyncBase >             connector_t;

            const auto startTime = time::second_clock::universal_time();

            for( ;; )
            {
                {
                    /*
                     * The probe connection is kept strictly short lived - the queue and the
                     * connector are both discarded before the next iteration - so the acceptor's
                     * connection bookkeeping is not polluted
                     */

                    const auto eq = om::lockDisposable(
                        ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                        );

                    const auto connector = connector_t::createInstance( cpp::copy( readinessHost ), readinessPort );
                    const auto taskConnector = om::qi< Task >( connector.get() );

                    eq -> push_back( taskConnector );
                    eq -> wait( taskConnector );

                    if( ! taskConnector -> isFailed() )
                    {
                        return;
                    }
                }

                if( ( time::second_clock::universal_time() - startTime ) > time::seconds( timeoutInSeconds ) )
                {
                    UTF_FAIL(
                        BL_MSG()
                            << "The acceptor did not become ready within "
                            << timeoutInSeconds
                            << " seconds at "
                            << readinessHost
                            << ":"
                            << readinessPort
                        );

                    return;
                }

                os::sleep( time::milliseconds( 50 ) );
            }
        }

        template
        <
            typename Acceptor
        >
        static void startAcceptorAndExecuteCallback(
            SAA_in              const bl::cpp::void_callback_t&                             callback,
            SAA_in              const bl::om::ObjPtr< Acceptor >&                           acceptor,
            SAA_in_opt          const std::string&                                          readinessHost = bl::str::empty(),
            SAA_in_opt          const unsigned short                                        readinessPort = 0U
            )
        {
            using namespace bl;
            using namespace tasks;

            scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    /*
                     * Start the acceptor and wait for it to become ready before the callback runs
                     */

                    const auto taskAcceptor = om::qi< Task >( acceptor );
                    eq -> push_back( taskAcceptor );

                    BL_SCOPE_EXIT_WARN_ON_FAILURE(
                        {
                            /*
                             * Shutdown the acceptor gracefully and exit
                             */

                            tasks::cancelAndWaitForSuccess( eq, taskAcceptor );
                        },
                        "TestTaskUtilsT::startAcceptorAndExecuteCallback"
                        );

                    if( 0U != readinessPort )
                    {
                        waitForAcceptorReady( readinessHost, readinessPort );
                    }
                    else
                    {
                        /*
                         * The caller did not provide a readiness endpoint, so fall back to the
                         * historical fixed sleep
                         */

                        os::sleep( time::milliseconds( 5000 ) );
                    }

                    callback();
                }
                );
        }

        template
        <
            typename Acceptor,
            typename AsyncWrapper = typename Acceptor::async_wrapper_t
        >
        static void createAcceptorAndExecute(
            SAA_in              const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&      controlToken,
            SAA_in              const bl::cpp::void_callback_t&                             callback,
            SAA_in              const bl::om::ObjPtr< bl::data::datablocks_pool_type >&     dataBlocksPool,
            SAA_in              const bl::om::ObjPtr< AsyncWrapper >&                       asyncWrapper,
            SAA_in_opt          std::string&&                                               host = "localhost",
            SAA_in_opt          const unsigned short                                        port = 28100U
            )
        {
            using namespace bl;
            using namespace tasks;

            const auto readinessHost = cpp::copy( host );

            const auto acceptor = Acceptor::template createInstance<>(
                controlToken,
                dataBlocksPool,
                std::forward< std::string >( host ),
                port,
                bl::str::empty() /* privateKeyPem */,
                bl::str::empty() /* certificatePem */,
                asyncWrapper
                );

            UTF_REQUIRE( acceptor );

            startAcceptorAndExecuteCallback( callback, acceptor, readinessHost, port );
        }
    };

    typedef TestTaskUtilsT<> TestTaskUtils;

} // utest

#endif /* __UTEST_TESTTASKUTILS_H_ */

