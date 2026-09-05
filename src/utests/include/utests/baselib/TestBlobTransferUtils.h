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

#ifndef __UTESTS_BASELIB_TESTBLOBTRANSFERUTILS_H_
#define __UTESTS_BASELIB_TESTBLOBTRANSFERUTILS_H_

#include <baselib/messaging/AsyncDataChunkStorage.h>
#include <baselib/messaging/TcpBlockTransferClient.h>
#include <baselib/messaging/TcpBlockServerDataChunkStorage.h>
#include <baselib/messaging/ProxyDataChunkStorageImpl.h>
#include <baselib/messaging/BlobServerFacade.h>

#include <baselib/data/FilesystemMetadata.h>

#include <baselib/http/SimpleHttpTask.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/utils/ScanDirectoryTask.h>

#include <baselib/reactive/ObservableBase.h>
#include <baselib/reactive/ObserverBase.h>
#include <baselib/reactive/Observer.h>
#include <baselib/reactive/ProcessingUnit.h>

#include <baselib/transfer/ChunksTransmitter.h>
#include <baselib/transfer/ChunksReceiver.h>
#include <baselib/transfer/ChunksDeleter.h>
#include <baselib/transfer/FilesPackagerUnit.h>
#include <baselib/transfer/FilesUnpackagerUnit.h>
#include <baselib/transfer/RecursiveDirectoryScanner.h>
#include <baselib/transfer/SendRecvContext.h>

#include <baselib/data/FilesystemMetadata.h>
#include <baselib/data/FilesystemMetadataInMemoryImpl.h>

#include <baselib/core/Utils.h>
#include <baselib/core/FileEncoding.h>
#include <baselib/core/EndpointSelector.h>
#include <baselib/core/EndpointSelectorImpl.h>
#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/PathUtils.h>
#include <baselib/core/Random.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include <utests/baselib/MachineGlobalTestLock.h>
#include <utests/baselib/TestTaskUtils.h>
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/Utf.h>
#include <utests/baselib/TestFsUtils.h>

BL_IID_DECLARE( FilesystemMetadataStore, "0d6210c2-f359-4550-b94f-280b58eb4dfe" )

namespace utest
{
    /**
     * @brief interface FilesystemMetadataStore - artifact store abstraction
     */

    class FilesystemMetadataStore :
        public bl::om::Object
    {
        BL_DECLARE_INTERFACE( FilesystemMetadataStore )

    public:

        virtual auto name() const NOEXCEPT -> const std::string& = 0;

        virtual auto createMetadata() -> bl::om::ObjPtr< bl::data::FilesystemMetadataWO > = 0;

        virtual auto saveArtifact( SAA_in const bl::om::ObjPtr< bl::data::FilesystemMetadataWO >& fsmd )
            -> std::string = 0;

        virtual auto loadArtifact( SAA_in const std::string& artifactId )
            -> bl::om::ObjPtr< bl::data::FilesystemMetadataRO > = 0;
    };

    /**
     * @brief class FilesystemMetadataStoreInMemory - an in-memory implementation of the artifact
     * store abstraction
     */

    template
    <
        typename E = void
    >
    class FilesystemMetadataStoreInMemoryT :
        public FilesystemMetadataStore
    {
        BL_CTR_DEFAULT( FilesystemMetadataStoreInMemoryT, protected )
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( FilesystemMetadataStoreInMemoryT, FilesystemMetadataStore )

        static const std::string                                                            g_inMemoryImplName;

        std::unordered_map< std::string, bl::om::ObjPtr< bl::data::FilesystemMetadataRO > > m_artifacts;

    public:

        virtual auto name() const NOEXCEPT -> const std::string& OVERRIDE
        {
            return g_inMemoryImplName;
        }

        virtual auto createMetadata() -> bl::om::ObjPtr< bl::data::FilesystemMetadataWO > OVERRIDE
        {
            return bl::data::FilesystemMetadataInMemoryImpl::createInstance< bl::data::FilesystemMetadataWO >();
        }

        virtual auto saveArtifact( SAA_in const bl::om::ObjPtr< bl::data::FilesystemMetadataWO >& fsmd )
            -> std::string OVERRIDE
        {
            BL_CHK(
                false,
                fsmd -> isFinalized(),
                BL_MSG()
                    << "Filesystem metadata object must be finalized before storing"
                );

            auto artifactId = bl::uuids::uuid2string( bl::uuids::create() );

            m_artifacts.emplace( artifactId, bl::om::qi< bl::data::FilesystemMetadataRO >( fsmd ) );

            return artifactId;
        }

        virtual auto loadArtifact( SAA_in const std::string& artifactId )
            -> bl::om::ObjPtr< bl::data::FilesystemMetadataRO > OVERRIDE
        {
            const auto pos = m_artifacts.find( artifactId );

            BL_CHK(
                false,
                pos != std::end( m_artifacts ),
                BL_MSG()
                    << "Filesystem metadata for artifact with id "
                    << artifactId
                    << " cannot be found"
                );

            return bl::om::copy( pos -> second );
        }
    };

    BL_DEFINE_STATIC_CONST_STRING( FilesystemMetadataStoreInMemoryT, g_inMemoryImplName ) = "in-memory metadata store";

    typedef FilesystemMetadataStoreInMemoryT<> FilesystemMetadataStoreInMemory;
    typedef bl::om::ObjectImpl< FilesystemMetadataStoreInMemory > FilesystemMetadataStoreInMemoryImpl;

    /**
     * @brief class ChunksFilter - a pass-through unit which withholds the chunk with the
     * requested index from its subscribers (fault injection for the unpackager tests)
     */

    template
    <
        typename E = void
    >
    class ChunksFilterT :
        public bl::reactive::ObservableBase
    {
        BL_DECLARE_OBJECT_IMPL( ChunksFilterT )

    protected:

        typedef bl::reactive::ObservableBase                                                base_type;

        const std::size_t                                                                   m_withheldChunkIndex;
        std::size_t                                                                         m_chunksSeen;
        std::deque< bl::cpp::any >                                                          m_pending;
        bl::cpp::ScalarTypeIniter< bool >                                                   m_inputDisconnected;

        ChunksFilterT( SAA_in const std::size_t withheldChunkIndex )
            :
            m_withheldChunkIndex( withheldChunkIndex ),
            m_chunksSeen( 0U )
        {
            m_name = "success:Chunks_Filter";
        }

        virtual void tryStopObservable() OVERRIDE
        {
            /*
             * Nothing to stop; the pending events are simply not forwarded
             */
        }

        virtual bl::time::time_duration chk2LoopUntilFinished() OVERRIDE
        {
            using namespace bl;

            base_type::chk2ThrowIfStopped();

            while( ! m_pending.empty() )
            {
                if( ! base_type::notifyOnNext( cpp::any( m_pending.front() ) ) )
                {
                    /*
                     * The subscriber queues are full; wait and retry
                     */

                    return time::milliseconds( 20 );
                }

                m_pending.pop_front();
            }

            if( m_inputDisconnected )
            {
                return time::neg_infin;
            }

            return time::milliseconds( 20 );
        }

    public:

        bool onChunkArrived( SAA_in const bl::cpp::any& value )
        {
            BL_MUTEX_GUARD( m_lock );

            base_type::chk2ThrowIfStopped();

            const auto chunkIndex = m_chunksSeen++;

            if( chunkIndex != m_withheldChunkIndex )
            {
                m_pending.push_back( value );
            }

            return true;
        }

        void onInputCompleted()
        {
            BL_MUTEX_GUARD( m_lock );

            m_inputDisconnected = true;
        }
    };

    typedef ChunksFilterT<> ChunksFilter;

    /**
     * @brief PipelineFaultCounters - observations made by the fault injection code; shared by all
     * copies of the options object, so the test can assert on them after the pipeline has completed
     */

    struct PipelineFaultCounters
    {
        std::atomic< std::size_t >                                                          authentications;
        std::atomic< std::size_t >                                                          savesForwarded;
        std::atomic< std::size_t >                                                          loadsForwarded;
        std::atomic< std::size_t >                                                          removesForwarded;
        std::atomic< std::size_t >                                                          connectionDrops;

        PipelineFaultCounters()
            :
            authentications( 0U ),
            savesForwarded( 0U ),
            loadsForwarded( 0U ),
            removesForwarded( 0U ),
            connectionDrops( 0U )
        {
        }
    };

    /**
     * @brief PipelineFaultOptions - fault injection settings for the transfer pipeline tests
     */

    struct PipelineFaultOptions
    {
        enum : std::size_t
        {
            NoWithheldChunk = std::size_t( -1 ),
        };

        /*
         * Faults injected by the blob server side storage decorator; the drop faults close
         * every client connection instead of serving the first call of the respective kind,
         * so the client sees a dropped connection (retried) rather than a server error
         */

        enum class ServerFault
        {
            None,
            DropConnectionOnFirstSave,
            DropConnectionOnFirstLoad,
            DropConnectionOnFirstRemove,
            FailFirstSave,
        };

        /*
         * The expected outcome of the upload; when it is not Success the download and the
         * removal are skipped and the metadata object is expected to remain mutable
         */

        enum class UploadOutcome
        {
            Success,
            ServerError,
            ConnectionError,
        };

        /*
         * When different than NoWithheldChunk the download pipeline withholds the chunk
         * with this index from the unpackager and the download is expected to fail
         */

        std::size_t                                                                         withheldChunkIndex;

        /*
         * When not empty the blob server requires authentication with this token and the
         * transfer context is configured to send it
         */

        std::string                                                                         authenticationToken;

        bool                                                                                disablePeerSessionsTracking;
        bool                                                                                forcePeerSessionsTracking;
        bool                                                                                singleFileInput;
        UploadOutcome                                                                       expectedUploadOutcome;
        ServerFault                                                                         serverFault;
        std::shared_ptr< PipelineFaultCounters >                                            counters;

        PipelineFaultOptions()
            :
            withheldChunkIndex( NoWithheldChunk ),
            disablePeerSessionsTracking( false ),
            forcePeerSessionsTracking( false ),
            singleFileInput( false ),
            expectedUploadOutcome( UploadOutcome::Success ),
            serverFault( ServerFault::None ),
            counters( std::make_shared< PipelineFaultCounters >() )
        {
        }

        bool isServerFaultInjectionRequested() const NOEXCEPT
        {
            return ! authenticationToken.empty() || ServerFault::None != serverFault;
        }

        static bool isConnectionDrop( SAA_in const ServerFault fault ) NOEXCEPT
        {
            return
                ServerFault::DropConnectionOnFirstSave == fault ||
                ServerFault::DropConnectionOnFirstLoad == fault ||
                ServerFault::DropConnectionOnFirstRemove == fault;
        }
    };

    /**
     * @brief class FaultInjectingDataChunkStorage - a data chunk storage decorator which injects
     * the configured server fault once and forwards everything else to the wrapped storage
     */

    template
    <
        typename E = void
    >
    class FaultInjectingDataChunkStorageT : public bl::data::DataChunkStorage
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE_DISPOSABLE( FaultInjectingDataChunkStorageT, bl::data::DataChunkStorage )

    protected:

        typedef PipelineFaultOptions::ServerFault                                           ServerFault;

        const bl::om::ObjPtr< bl::data::DataChunkStorage >                                  m_storage;
        const PipelineFaultOptions                                                          m_options;
        bl::cpp::void_callback_t                                                            m_dropCallback;
        bl::os::mutex                                                                       m_lock;
        bl::cpp::ScalarTypeIniter< bool >                                                   m_faultInjected;

        FaultInjectingDataChunkStorageT(
            SAA_in              const bl::om::ObjPtr< bl::data::DataChunkStorage >&        storage,
            SAA_in              const PipelineFaultOptions&                                 options
            )
            :
            m_storage( bl::om::copy( storage ) ),
            m_options( options )
        {
        }

        /*
         * Returns true if the current call must be skipped (the fault was injected now)
         */

        bool chk2InjectFault( SAA_in const ServerFault fault )
        {
            BL_MUTEX_GUARD( m_lock );

            if( m_faultInjected || fault != m_options.serverFault )
            {
                return false;
            }

            m_faultInjected = true;

            if( PipelineFaultOptions::isConnectionDrop( fault ) && m_dropCallback )
            {
                m_dropCallback();

                ++m_options.counters -> connectionDrops;
            }

            return true;
        }

    public:

        void dropCallback( SAA_in bl::cpp::void_callback_t&& callback )
        {
            BL_MUTEX_GUARD( m_lock );

            m_dropCallback = std::move( callback );
        }

        virtual void dispose() NOEXCEPT OVERRIDE
        {
            /*
             * The wrapped storage is owned and disposed by the caller
             */
        }

        virtual void load(
            SAA_in                  const bl::uuid_t&                                       sessionId,
            SAA_in                  const bl::uuid_t&                                       chunkId,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&            data
            ) OVERRIDE
        {
            if( chk2InjectFault( ServerFault::DropConnectionOnFirstLoad ) )
            {
                return;
            }

            m_storage -> load( sessionId, chunkId, data );

            ++m_options.counters -> loadsForwarded;
        }

        virtual void save(
            SAA_in                  const bl::uuid_t&                                       sessionId,
            SAA_in                  const bl::uuid_t&                                       chunkId,
            SAA_in                  const bl::om::ObjPtr< bl::data::DataBlock >&            data
            ) OVERRIDE
        {
            if( chk2InjectFault( ServerFault::DropConnectionOnFirstSave ) )
            {
                return;
            }

            if( chk2InjectFault( ServerFault::FailFirstSave ) )
            {
                /*
                 * A ServerErrorException is reported to the client as a server error (which
                 * the client does not retry); any other exception is fatal for the server
                 */

                BL_THROW(
                    bl::ServerErrorException()
                        << bl::eh::errinfo_error_code( bl::eh::errc::make_error_code( bl::eh::errc::io_error ) )
                        << bl::eh::errinfo_error_uuid( chunkId ),
                    BL_MSG()
                        << "Injected save failure for chunk "
                        << chunkId
                    );
            }

            m_storage -> save( sessionId, chunkId, data );

            ++m_options.counters -> savesForwarded;
        }

        virtual void remove(
            SAA_in                  const bl::uuid_t&                                       sessionId,
            SAA_in                  const bl::uuid_t&                                       chunkId
            ) OVERRIDE
        {
            if( chk2InjectFault( ServerFault::DropConnectionOnFirstRemove ) )
            {
                return;
            }

            m_storage -> remove( sessionId, chunkId );

            ++m_options.counters -> removesForwarded;
        }

        virtual void flushPeerSessions( SAA_in const bl::uuid_t& peerId ) OVERRIDE
        {
            m_storage -> flushPeerSessions( peerId );
        }
    };

    typedef bl::om::ObjectImpl< FaultInjectingDataChunkStorageT<> > FaultInjectingDataChunkStorage;

    /**
     * @brief class FaultInjectingBlobServer - the blob server acceptor which remembers its
     * connection tasks, so a test can drop every client connection on demand
     */

    template
    <
        typename E = void
    >
    class FaultInjectingBlobServerT :
        public bl::tasks::TcpBlockServerT< bl::tasks::TcpSocketAsyncBase, bl::AsyncDataChunkStorage >
    {
        BL_DECLARE_OBJECT_IMPL( FaultInjectingBlobServerT )

    public:

        typedef bl::tasks::TcpBlockServerT< bl::tasks::TcpSocketAsyncBase, bl::AsyncDataChunkStorage > base_type;
        typedef typename base_type::isauthenticationrequired_callback_t                     isauthenticationrequired_callback_t;

    protected:

        bl::os::mutex                                                                       m_connectionsLock;
        std::vector< bl::om::ObjPtr< bl::tasks::Task > >                                    m_connections;

        FaultInjectingBlobServerT(
            SAA_in              const bl::om::ObjPtr< bl::tasks::TaskControlTokenRW >&     controlToken,
            SAA_in              const bl::om::ObjPtr< bl::data::datablocks_pool_type >&    dataBlocksPool,
            SAA_in              std::string&&                                               host,
            SAA_in              const unsigned short                                        port,
            SAA_in              const bl::om::ObjPtr< bl::AsyncDataChunkStorage >&          storage,
            SAA_in              isauthenticationrequired_callback_t&&                       isAuthenticationRequiredCallback
            )
            :
            base_type(
                controlToken,
                dataBlocksPool,
                BL_PARAM_FWD( host ),
                port,
                bl::str::empty()                                /* privateKeyPem */,
                bl::str::empty()                                /* certificatePem */,
                storage,
                bl::uuids::nil()                                /* peerId */,
                BL_PARAM_FWD( isAuthenticationRequiredCallback )
                )
        {
        }

        virtual bl::om::ObjPtr< bl::tasks::Task > createConnection(
            SAA_inout           bl::tasks::TcpSocketAsyncBase::stream_ref&&                 connectedStream
            ) OVERRIDE
        {
            auto connection = base_type::createConnection( BL_PARAM_FWD( connectedStream ) );

            BL_MUTEX_GUARD( m_connectionsLock );

            m_connections.push_back( bl::om::copy( connection ) );

            return connection;
        }

    public:

        void dropAllConnections()
        {
            std::vector< bl::om::ObjPtr< bl::tasks::Task > > connections;

            {
                BL_MUTEX_GUARD( m_connectionsLock );

                connections.swap( m_connections );
            }

            for( const auto& connection : connections )
            {
                connection -> requestCancel();
            }
        }
    };

    typedef bl::om::ObjectImpl< FaultInjectingBlobServerT<> > FaultInjectingBlobServer;

    /**
     * @brief class TestBlobTransferUtils - shared test code for the blob transfer code testing
     */

    template
    <
        typename E = void
    >
    class TestBlobTransferUtilsT
    {
        BL_DECLARE_ABSTRACT( TestBlobTransferUtilsT )

    public:

        enum class CancelType
        {
            NoCancel,
            CancelUpload,
            CancelDownload,
            CancelRemove,
        };

        typedef utest::PipelineFaultOptions                                                     PipelineFaultOptions;

        typedef bl::data::DataChunkStorage                                                      DataChunkStorage;

    protected:

        typedef bl::cpp::function
        <
            void (
                SAA_in          const bl::cpp::void_callback_t&                                 cbTransferTest,
                SAA_in          const bl::om::ObjPtrCopyable< bl::tasks::TaskControlTokenRW >&  controlToken,
                SAA_in          const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  context,
                SAA_in          const unsigned short                                            blobServerPort
                )
        >
        execute_transfer_tests_callback_t;

        typedef bl::cpp::function
        <
            void (
                SAA_in          const DataChunkStorage::data_storage_callback_t&                cbProxyTransferTests,
                SAA_in          const bl::om::ObjPtrCopyable< bl::tasks::TaskControlTokenRW >&  controlToken,
                SAA_in          const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  context,
                SAA_in          const unsigned short                                            blobServerPrivatePort
                )
        >
        proxy_storage_transfer_callback_t;

        static void executeTheFilesPackagerAndTransmitterPipelineInternal(
            SAA_in              const std::size_t                                               noOfDownloads,
            SAA_in              const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  contextIn,
            SAA_in              const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&        metadataStore,
            SAA_in_opt          const std::string&                                              host = "localhost",
            SAA_in_opt          const unsigned short                                            port = test::UtfArgsParser::PORT_DEFAULT,
            SAA_in_opt          const CancelType                                                cancelType = CancelType::NoCancel,
            SAA_in_opt          const bl::cpp::void_callback_t&                                 cancelCallback = bl::cpp::void_callback_t(),
            SAA_in_opt          const bl::om::ObjPtr< bl::tasks::ExecutionQueue >&              executionQueue = nullptr,
            SAA_in_opt          const PipelineFaultOptions&                                     faultOptions = PipelineFaultOptions()
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::transfer;
            using namespace bl::reactive;
            using namespace bl::fs;

            UTF_REQUIRE( CancelType::NoCancel == cancelType || cancelCallback );

            cpp::SafeUniquePtr< TmpDir > tmpDir;
            fs::path root = test::UtfArgsParser::path();

            if( root.empty() || faultOptions.singleFileInput )
            {
                /*
                 * Use system generated temporary directory if parameter is not provided
                 *
                 * The fault injection tests always use a generated input with a single small
                 * file, so the transfer consists of exactly one chunk
                 */
                tmpDir = cpp::SafeUniquePtr< TmpDir >::attach( new TmpDir );
                root = tmpDir -> path();

                if( faultOptions.singleFileInput )
                {
                    encoding::writeTextFile( root / "single-chunk-file.txt", std::string( 1024U, 'x' ) );
                }
                else
                {
                    TestFsUtils dummyCreator;
                    dummyCreator.createDummyTestDir( root );
                }
            }

            BL_LOG_MULTILINE(
                Logging::info(),
                BL_MSG()
                    << "******************* Files packager processing unit's download/upload tests ******************* \n"
                );

            const char* hosts[] =
            {
                host.c_str(),
                host.c_str(),
                host.c_str(),
                host.c_str(),
            };

            const auto selector = EndpointSelectorImpl::createInstance< EndpointSelector >(
                port,
                hosts,
                hosts + BL_ARRAY_SIZE( hosts )
                );

            std::string artifactId;

            auto context = om::copy( contextIn );

            if( context )
            {
                context = bl::transfer::SendRecvContext::createInstance(
                    SimpleEndpointSelectorImpl::createInstance< EndpointSelector >( cpp::copy( host ), port )
                    );

                if( ! faultOptions.authenticationToken.empty() )
                {
                    context -> setAuthenticationToken( cpp::copy( faultOptions.authenticationToken ) );
                }
            }

            const bool enableSessions =
                ( test::UtfArgsParser::isEnableSessions() || faultOptions.forcePeerSessionsTracking ) &&
                ! faultOptions.disablePeerSessionsTracking;

            {
                BL_LOG_MULTILINE(
                    Logging::info(),
                    BL_MSG()
                        << "\n\n\n******************* upload tests ******************* \n"
                    );

                const auto t1 = time::microsec_clock::universal_time();

                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        /*
                         * Create and start the directory scanner unit
                         */

                        const auto controlToken =
                            test::UtfArgsParser::isRelaxedScanMode() ?
                                utest::ScanningControlImpl::createInstance< DirectoryScannerControlToken >() :
                                nullptr;

                        const auto scanner = RecursiveDirectoryScannerImpl::createInstance(
                            root,
                            controlToken
                            );

                        const auto fsmd = metadataStore -> createMetadata();

                        /*
                         * Create the file packager unit
                         */

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< FilesPackagerUnit, Observable >,
                                true /* enableSharedPtr */
                            > unit_packager_t;

                        const auto unitPackager = unit_packager_t::createInstance( context, fsmd );

                        scanner -> subscribe(
                            unitPackager -> bindInputConnector< unit_packager_t >(
                                &unit_packager_t::onFilesBatchArrived,
                                &unit_packager_t::onInputCompleted
                                )
                            );

                        /*
                         * Create the chunks transmitter unit
                         */

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< ChunksTransmitter, Observable >,
                                true /* enableSharedPtr */
                            > unit_transmitter_t;

                        const auto unitChunksTransmitter =
                                unit_transmitter_t::createInstance(
                                    selector,
                                    context,
                                    fsmd,
                                    test::UtfArgsParser::connections()
                                    );

                        if( enableSessions )
                        {
                            unitChunksTransmitter -> enablePeerSessionsTracking();
                        }

                        unitPackager -> subscribe(
                            unitChunksTransmitter -> bindInputConnector< unit_transmitter_t >(
                                &unit_transmitter_t::onChunkArrived,
                                &unit_transmitter_t::onInputCompleted
                                )
                            );

                        /*
                         * For the transmitter unit it is ok to have no subscribers
                         */

                        unitChunksTransmitter -> allowNoSubscribers( true );

                        /*
                         * Start the reactive units in the correct order and wait
                         * for completion
                         */

                        eq -> push_back( om::qi< Task >( unitChunksTransmitter ) );
                        eq -> push_back( om::qi< Task >( unitPackager ) );
                        eq -> push_back( om::qi< Task >( scanner ) );

                        if( CancelType::CancelUpload == cancelType )
                        {
                            cancelCallback();
                        }

                        if( PipelineFaultOptions::UploadOutcome::Success != faultOptions.expectedUploadOutcome )
                        {
                            /*
                             * The injected fault fails the upload with the expected error: a
                             * server error is never retried, and a dropped connection is not
                             * retried when the peer sessions tracking is enabled. In both cases
                             * the metadata must remain mutable, so the caller could retry with
                             * the same object
                             */

                            if( PipelineFaultOptions::UploadOutcome::ServerError == faultOptions.expectedUploadOutcome )
                            {
                                UTF_REQUIRE_THROW( executeQueueAndCancelOnFailure( eq ), ServerErrorException );
                            }
                            else
                            {
                                UTF_REQUIRE_THROW( executeQueueAndCancelOnFailure( eq ), eh::system_error );
                            }

                            UTF_REQUIRE( ! fsmd -> isFinalized() );

                            return;
                        }

                        executeQueueAndCancelOnFailure( eq );

                        BL_ASSERT( fsmd -> isFinalized() );

                        artifactId = metadataStore -> saveArtifact( fsmd );
                    },
                    executionQueue
                    );

                if( PipelineFaultOptions::UploadOutcome::Success != faultOptions.expectedUploadOutcome )
                {
                    return;
                }

                const auto duration = time::microsec_clock::universal_time() - t1;

                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n\nFiles upload test pushing to blob storage for path "
                        << fs::path( root )
                        << " took "
                        << durationInSeconds
                        << " seconds"
                    );
            }

            cpp::SafeUniquePtr< TmpDir > tmpDirOut;
            auto outputPath = test::UtfArgsParser::outputPath();

            const bool verifyOnly = test::UtfArgsParser::isVerifyOnly();

            if( false == verifyOnly && outputPath.empty() )
            {
                /*
                 * Skip the download part if parameter 'output-path' is not provided
                 */

                tmpDirOut = cpp::SafeUniquePtr< TmpDir >::attach( new TmpDir );
                outputPath = tmpDirOut -> path().string();
            }

            const auto cbDownloadTest = [ & ]( SAA_in const std::size_t downloadId ) -> void
            {
                BL_LOG_MULTILINE(
                    Logging::info(),
                    BL_MSG()
                        << "\n\n\n******************* download tests ******************* \n"
                        << "****** downloadId: "
                        << downloadId
                    );

                /*
                 * The withheld chunk fault applies to the regular downloads only, not to the
                 * download which is expected to fail after the chunks have been deleted
                 */

                const bool withholdChunk =
                    PipelineFaultOptions::NoWithheldChunk != faultOptions.withheldChunkIndex &&
                    downloadId < noOfDownloads;

                fs::safeRemoveAllIfExists( outputPath );

                const auto t1 = time::microsec_clock::universal_time();

                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        BL_ASSERT( ! artifactId.empty() );

                        const auto fsmd = metadataStore -> loadArtifact( artifactId );;

                        /*
                         * Create the pipeline of chunks receiver and unpackager and
                         * execute it
                         */

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< ChunksReceiver, Observable >,
                                true /* enableSharedPtr */
                            > unit_receiver_t;

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< FilesUnpackagerUnit, Observable >,
                                true /* enableSharedPtr */
                            > unit_unpackager_t;

                        typedef utest::BlocksReceiver<> receiver_t;

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< receiver_t, receiver_t >,
                                true /* enableSharedPtr */
                            > unit_blocks_receiver_t;

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< ChunksFilter, Observable >,
                                true /* enableSharedPtr */
                            > unit_chunks_filter_t;

                        const auto unitChunksReceiver =
                                unit_receiver_t::createInstance(
                                    selector,
                                    context,
                                    fsmd,
                                    test::UtfArgsParser::connections()
                                    );

                        if( enableSessions )
                        {
                            unitChunksReceiver -> enablePeerSessionsTracking();
                        }

                        if( verifyOnly )
                        {
                            const auto unitBlocksReceiver =
                                    unit_blocks_receiver_t::createInstance(
                                        context -> dataBlocksPool()
                                        );

                            unitChunksReceiver -> subscribe(
                                unitBlocksReceiver -> bindInputConnector< unit_blocks_receiver_t >(
                                    &unit_blocks_receiver_t::onDataArrived,
                                    &unit_blocks_receiver_t::logResults
                                    )
                                );

                            /*
                             * Start the receiver unit and wait for completion
                             */

                            eq -> push_back( om::qi< Task >( unitChunksReceiver ) );

                            executeQueueAndCancelOnFailure( eq );
                        }
                        else
                        {
                            const auto unitUnpackager =
                                    unit_unpackager_t::createInstance(
                                        unit_unpackager_t::SuaError,
                                        context,
                                        fsmd,
                                        outputPath
                                        );

                            /*
                             * For the unpackager unit it is ok to have no subscribers
                             */

                            unitUnpackager -> allowNoSubscribers( true );

                            try
                            {
                                const auto unpackagerInput =
                                    unitUnpackager -> bindInputConnector< unit_unpackager_t >(
                                        &unit_unpackager_t::onChunkArrived,
                                        &unit_unpackager_t::onInputCompleted
                                        );

                                if( withholdChunk )
                                {
                                    /*
                                     * Interpose a filter which withholds one chunk, so the unpackager
                                     * ends up with an incomplete entry once the input has completed
                                     *
                                     * The unpackager must fail (and discard its staging directory)
                                     * instead of reporting success with a partial tree
                                     */

                                    const auto unitChunksFilter =
                                        unit_chunks_filter_t::createInstance( faultOptions.withheldChunkIndex );

                                    unitChunksFilter -> subscribe( unpackagerInput );

                                    unitChunksReceiver -> subscribe(
                                        unitChunksFilter -> template bindInputConnector< unit_chunks_filter_t >(
                                            &unit_chunks_filter_t::onChunkArrived,
                                            &unit_chunks_filter_t::onInputCompleted
                                            )
                                        );

                                    eq -> push_back( om::qi< Task >( unitUnpackager ) );
                                    eq -> push_back( om::qi< Task >( unitChunksFilter ) );
                                    eq -> push_back( om::qi< Task >( unitChunksReceiver ) );

                                    UTF_REQUIRE_THROW( executeQueueAndCancelOnFailure( eq ), UnexpectedException );

                                    UTF_REQUIRE( unitUnpackager -> targetTmpDir().empty() );

                                    return;
                                }

                                unitChunksReceiver -> subscribe( unpackagerInput );

                                /*
                                 * Start the units in the right order and wait for completion
                                 */

                                eq -> push_back( om::qi< Task >( unitUnpackager ) );
                                eq -> push_back( om::qi< Task >( unitChunksReceiver ) );

                                if( CancelType::CancelDownload == cancelType )
                                {
                                    cancelCallback();
                                }

                                executeQueueAndCancelOnFailure( eq );

                                const auto& tmpDir = unitUnpackager -> targetTmpDir();
                                BL_ASSERT( false == tmpDir.empty() && fs::exists( tmpDir ) );

                                fs::renameToMakeVisibleAs( tmpDir, outputPath );
                            }
                            catch( std::exception& )
                            {
                                const auto& tmpDir = unitUnpackager -> targetTmpDir();

                                if( false == tmpDir.empty() && fs::exists( tmpDir ) )
                                {
                                    /*
                                     * The pipeline has failed, but the unpackager itself
                                     * did not. We need to cleanup the temporary directory
                                     * it has created
                                     */

                                    fs::safeDeletePathNothrow( tmpDir );
                                }

                                throw;
                            }
                        }
                    },
                    executionQueue
                    );

                if( withholdChunk )
                {
                    /*
                     * The download was expected to fail; there is nothing to compare
                     */

                    return;
                }

                const auto duration = time::microsec_clock::universal_time() - t1;

                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n\nFiles download test pushing from blob storage to path "
                        << fs::path( outputPath )
                        << " took "
                        << durationInSeconds
                        << " seconds"
                    );

                const auto isSameDir = TestFsUtils::compareFolders( root, outputPath );

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n\nComparing files from root: "
                        << fs::path( root )
                        << " to path: "
                        << fs::path( outputPath )
                        << "; result: "
                        << isSameDir
                    );

                BL_RT_ASSERT( isSameDir, "Files downloaded do not match the files uploaded" );
            };

            for( std::size_t downloadId = 0U; downloadId < noOfDownloads; ++downloadId )
            {
                cbDownloadTest( downloadId );
            }

            {
                BL_LOG_MULTILINE(
                    Logging::info(),
                    BL_MSG()
                        << "\n\n\n******************* deleter tests ******************* \n"
                    );

                const auto t1 = time::microsec_clock::universal_time();

                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        BL_ASSERT( ! artifactId.empty() );

                        const auto fsmd = metadataStore -> loadArtifact( artifactId );;

                        /*
                         * Create the pipeline of chunks deleter and execute it
                         */

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< ChunksDeleter, Observable >,
                                true /* enableSharedPtr */
                            > unit_deleter_t;

                        typedef utest::BlocksReceiver<> receiver_t;

                        typedef om::ObjectImpl
                            <
                                ProcessingUnit< receiver_t, receiver_t >,
                                true /* enableSharedPtr */
                            > unit_blocks_receiver_t;

                        const auto unitChunksDeleter =
                                unit_deleter_t::createInstance(
                                    selector,
                                    context,
                                    fsmd,
                                    test::UtfArgsParser::connections()
                                    );

                        if( enableSessions )
                        {
                            unitChunksDeleter -> enablePeerSessionsTracking();
                        }

                        const auto unitBlocksReceiver =
                                unit_blocks_receiver_t::createInstance(
                                    context -> dataBlocksPool()
                                    );

                        unitChunksDeleter -> subscribe(
                            unitBlocksReceiver -> bindInputConnector< unit_blocks_receiver_t >(
                                &unit_blocks_receiver_t::onDataArrived,
                                &unit_blocks_receiver_t::logResults
                                )
                            );

                        /*
                         * Start the deleter unit and wait for completion
                         */

                        eq -> push_back( om::qi< Task >( unitChunksDeleter ) );

                        if( CancelType::CancelRemove == cancelType )
                        {
                            cancelCallback();
                        }

                        executeQueueAndCancelOnFailure( eq );
                    },
                    executionQueue
                    );

                const auto duration = time::microsec_clock::universal_time() - t1;

                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n\nDeleting the chunks from blob storage took "
                        << durationInSeconds
                        << " seconds"
                    );
            }

            try
            {
                cbDownloadTest( noOfDownloads );
            }
            catch( ServerErrorException& e )
            {
                /*
                 * Since we deleted the chunks we now expect ServerErrorException exception on the client
                 * with error code attached to it matching eh::errc::no_such_file_or_directory condition
                 */

                const auto expectedEC = eh::errc::make_error_code( eh::errc::no_such_file_or_directory );

                const eh::error_code* ec = e.errorCode();
                const int* errNo = e.errNo();

                UTF_REQUIRE( ec );
                UTF_REQUIRE( errNo );

                UTF_REQUIRE_EQUAL( *ec, expectedEC );
                UTF_REQUIRE_EQUAL( *errNo, expectedEC.value() );
            }
        }

        static void executeTheFilesPackagerAndTransmitterPipelineExpectFailure(
            SAA_in              const std::size_t                                               noOfDownloads,
            SAA_in              const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  contextIn,
            SAA_in              const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&        metadataStore,
            SAA_in_opt          const std::string&                                              host = "localhost",
            SAA_in_opt          const unsigned short                                            port = test::UtfArgsParser::PORT_DEFAULT,
            SAA_in_opt          const CancelType                                                cancelType = CancelType::NoCancel
            )
        {
            using namespace bl;
            using namespace bl::tasks;

            UTF_REQUIRE( CancelType::NoCancel != cancelType );

            const auto executeOnce = [ & ]( SAA_in const std::size_t iterationNo ) -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eqTopLevel ) -> void
                    {
                        scheduleAndExecuteInParallel(
                            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eqLocal ) -> void
                            {
                                const bool coinToss =
                                    0U == ( random::getUniformRandomUnsignedValue( 1024U /* maxValue */ ) % 2U );

                                executeTheFilesPackagerAndTransmitterPipelineInternal(
                                    noOfDownloads,
                                    contextIn,
                                    metadataStore,
                                    host,
                                    port,
                                    cancelType,
                                    [ & ]() -> void /* cancelCallback */
                                    {
                                        const auto timerTaskImpl = SimpleTimerTask::createInstance(
                                            [ &eqTopLevel ]() -> bool
                                            {
                                                eqTopLevel -> cancelAll( false /* wait */ );
                                                return true;
                                            },
                                            time::milliseconds( coinToss ? 100U : 500U ) /* duration */,
                                            time::milliseconds( iterationNo * 1000 ) /* initDelay */
                                            );

                                        eqLocal -> push_back( om::qi< Task >( timerTaskImpl ) );
                                    },
                                    eqTopLevel
                                    );

                                eqLocal -> forceFlushNoThrow();
                            }
                            );
                    }
                    );
            };

            const std::size_t retryCount = 5;

            for( std::size_t i = 0U; i < retryCount; ++i )
            {
                try
                {
                    executeOnce( i /* iterationNo */ );
                }
                catch( eh::system_error& e )
                {
                    BL_LOG_MULTILINE(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "Expected eh::system_error exception:\n"
                            << bl::eh::diagnostic_information( e )
                        );

                    UTF_REQUIRE_EQUAL( e.code(), bl::asio::error::operation_aborted );
                }
            }
        }

        static void executeTransferTests(
            SAA_in                  const bl::cpp::void_callback_t&                                 cbTransferTest,
            SAA_in                  const bl::om::ObjPtrCopyable< DataChunkStorage >&               syncStorage,
            SAA_in                  const bl::om::ObjPtrCopyable< bl::tasks::TaskControlTokenRW >&  controlToken,
            SAA_in                  const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  context,
            SAA_in                  const unsigned short                                            blobServerPort,
            SAA_in_opt              const std::size_t                                               threadsCount = 0U,
            SAA_in_opt              const std::size_t                                               maxConcurrentTasks = 0U
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::transfer;
            using namespace test;

            const auto storage = om::lockDisposable(
                AsyncDataChunkStorage::createInstance(
                    syncStorage /* writeStorage */,
                    syncStorage /* readStorage */,
                    threadsCount,
                    om::qi< TaskControlToken >( controlToken ),
                    maxConcurrentTasks,
                    context -> dataBlocksPool()
                    )
                );

            if( test::UtfArgsParser::isUseLocalBlobServer() )
            {
                cbTransferTest();
            }
            else
            {
                TestTaskUtils::createAcceptorAndExecute< TcpBlockServerDataChunkStorage >(
                    controlToken,
                    cbTransferTest,
                    context -> dataBlocksPool(),
                    storage,
                    std::string( UtfArgsParser::host() ),
                    blobServerPort
                    );
            }
        }

        static void executeTransferTestsWithFaults(
            SAA_in                  const bl::cpp::void_callback_t&                                 cbTransferTest,
            SAA_in                  const bl::om::ObjPtrCopyable< DataChunkStorage >&               syncStorage,
            SAA_in                  const bl::om::ObjPtrCopyable< bl::tasks::TaskControlTokenRW >&  controlToken,
            SAA_in                  const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  context,
            SAA_in                  const unsigned short                                            blobServerPort,
            SAA_in                  const std::size_t                                               threadsCount,
            SAA_in                  const std::size_t                                               maxConcurrentTasks,
            SAA_in                  const PipelineFaultOptions&                                     faultOptions
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace test;

            if( ! faultOptions.isServerFaultInjectionRequested() )
            {
                executeTransferTests(
                    cbTransferTest,
                    syncStorage,
                    controlToken,
                    context,
                    blobServerPort,
                    threadsCount,
                    maxConcurrentTasks
                    );

                return;
            }

            /*
             * Server side fault injection needs the in-process blob server: the sync storage is
             * wrapped in the fault injecting decorator, the server requires authentication when
             * a token is configured and the acceptor can drop all client connections on demand
             */

            UTF_REQUIRE( ! UtfArgsParser::isUseLocalBlobServer() );

            const auto faultStorage = FaultInjectingDataChunkStorage::createInstance( syncStorage, faultOptions );

            const auto expectedToken = faultOptions.authenticationToken;
            const auto counters = faultOptions.counters;

            AsyncDataChunkStorage::datablock_callback_t authenticationCallback =
                [ expectedToken, counters ]( SAA_in const om::ObjPtr< DataBlock >& dataBlock ) -> void
                {
                    const std::string token(
                        reinterpret_cast< const char* >( dataBlock -> begin() ),
                        dataBlock -> size()
                        );

                    BL_CHK(
                        false,
                        token == expectedToken,
                        BL_MSG()
                            << "Unexpected authentication token was received by the blob server"
                        );

                    ++counters -> authentications;
                };

            const auto storage = om::lockDisposable(
                AsyncDataChunkStorage::createInstance(
                    om::qi< DataChunkStorage >( faultStorage )  /* writeStorage */,
                    om::qi< DataChunkStorage >( faultStorage )  /* readStorage */,
                    threadsCount,
                    om::qi< TaskControlToken >( controlToken ),
                    maxConcurrentTasks,
                    context -> dataBlocksPool(),
                    std::move( authenticationCallback )
                    )
                );

            FaultInjectingBlobServer::isauthenticationrequired_callback_t isAuthenticationRequiredCallback =
                [ expectedToken ](
                    SAA_in      const BlockTransferDefs::BlockType                          blockType,
                    SAA_in      const std::uint16_t                                         cntrlCode
                    ) -> bool
                {
                    if( expectedToken.empty() || BlockTransferDefs::BlockType::Normal != blockType )
                    {
                        return false;
                    }

                    switch( cntrlCode )
                    {
                        case tasks::detail::CommandBlock::CntrlCodeGetDataBlock:
                        case tasks::detail::CommandBlock::CntrlCodePutDataBlock:
                        case tasks::detail::CommandBlock::CntrlCodeRemoveDataBlock:
                            return true;

                        default:
                            return false;
                    }
                };

            const auto acceptor = FaultInjectingBlobServer::createInstance(
                controlToken,
                context -> dataBlocksPool(),
                std::string( UtfArgsParser::host() ),
                blobServerPort,
                storage,
                std::move( isAuthenticationRequiredCallback )
                );

            faultStorage -> dropCallback(
                cpp::bind(
                    &FaultInjectingBlobServer::dropAllConnections,
                    om::ObjPtrCopyable< FaultInjectingBlobServer >::acquireRef( acceptor.get() )
                    )
                );

            TestTaskUtils::startAcceptorAndExecuteCallback( cbTransferTest, acceptor );
        }

        static void filesPackagerTestsWrapInternal(
            SAA_in                  const execute_transfer_tests_callback_t&                            cbExecuteTests,
            SAA_in                  const unsigned short                                                blobServerPort,
            SAA_in                  const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&            metadataStore,
            SAA_in                  const CancelType                                                    cancelType,
            SAA_in_opt              const PipelineFaultOptions&                                         faultOptions =
                PipelineFaultOptions()
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::transfer;

            const om::ObjPtrCopyable< TaskControlTokenRW > controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            const auto context = bl::transfer::SendRecvContext::createInstance(
                SimpleEndpointSelectorImpl::createInstance< EndpointSelector >(
                    cpp::copy( test::UtfArgsParser::host() ),
                    blobServerPort
                    )
                );

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Executing file packager with proxy tests using "
                    << metadataStore -> name()
                    << " implementation"
                );

            const auto cbTransferTest = cpp::bind(
                &executeTheFilesPackagerAndTransmitterPipelineWithFaults,
                1U /* noOfDownloads */,
                om::ObjPtrCopyable< bl::transfer::SendRecvContext >::acquireRef( context.get() ),
                metadataStore,
                test::UtfArgsParser::host(),
                blobServerPort,
                cancelType,
                faultOptions
                );

            cbExecuteTests(
                cbTransferTest,
                controlToken,
                context,
                blobServerPort
                );
        }

        static void filesPackagerTestsWithProxyWrapInternal(
            SAA_in                  const proxy_storage_transfer_callback_t&                            transferCallback,
            SAA_in                  const unsigned short                                                blobServerPort,
            SAA_in                  const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&            metadataStore
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::messaging;
            using namespace bl::transfer;

            static const char* defaultHosts[] =
            {
                test::UtfArgsParser::host().c_str(),
            };

            /*
             * The blobServerPrivatePort is the actual blob server backed with the real backend
             * where blobServerPort will be the new blob server proxy port
             */

            const unsigned short blobServerPrivatePort = blobServerPort + 1;

            const auto endpointSelectorImpl = bl::EndpointSelectorImpl::createInstance(
                blobServerPrivatePort,
                defaultHosts,
                defaultHosts + BL_ARRAY_SIZE( defaultHosts )
                );

            const auto endpointSelector = bl::om::qi< bl::EndpointSelector >( endpointSelectorImpl );

            const om::ObjPtrCopyable< TaskControlTokenRW > controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            const auto context = bl::transfer::SendRecvContext::createInstance( bl::om::copy( endpointSelector ) );

            /*
             * When we test the proxy we request the # of downloads to be 2, so we test
             * the caching functionality
             */

            const auto cbTransferTest = cpp::bind(
                &executeTheFilesPackagerAndTransmitterPipeline,
                2U /* noOfDownloads */,
                om::ObjPtrCopyable< bl::transfer::SendRecvContext >::acquireRef( context.get() ),
                metadataStore,
                test::UtfArgsParser::host(),
                blobServerPort,
                CancelType::NoCancel
                );

            const auto cbProxyTransferTests =
                [ & ]( SAA_in const om::ObjPtr< DataChunkStorage >& syncCacheStorage ) -> void
            {
                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Executing file packager with proxy tests using "
                        << metadataStore -> name()
                        << " implementation"
                    );

                const auto syncStorage = om::lockDisposable(
                    ProxyDataChunkStorageImpl::createInstance< DataChunkStorage >(
                        endpointSelector,
                        syncCacheStorage /* storage */,
                        context -> dataBlocksPool(),
                        om::qi< TaskControlToken >( controlToken )
                        )
                    );

                {
                    const auto storage = om::lockDisposable(
                        AsyncDataChunkStorage::createInstance(
                            syncStorage /* writeStorage */,
                            syncStorage /* readStorage */,
                            0U /* threadsCount */,
                            om::qi< TaskControlToken >( controlToken ),
                            0U /* maxConcurrentTasks */,
                            context -> dataBlocksPool()
                            )
                        );

                    {
                        if( test::UtfArgsParser::isUseLocalBlobServer() )
                        {
                            cbTransferTest();
                        }
                        else
                        {
                            TestTaskUtils::createAcceptorAndExecute< TcpBlockServerDataChunkStorage >(
                                controlToken,
                                cbTransferTest,
                                context -> dataBlocksPool(),
                                storage,
                                std::string( test::UtfArgsParser::host() ),
                                blobServerPort
                                );
                        }
                    }
                }
            };

            transferCallback(
                cbProxyTransferTests,
                controlToken,
                context,
                blobServerPrivatePort
                );
        }

        static void proxyDataChunkBackendImplTestsInternal(
            SAA_in                  const proxy_storage_transfer_callback_t&                            transferCallback
            )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::messaging;
            using namespace bl::transfer;

            test::MachineGlobalTestLock lockBlobServer;

            static const char* defaultHosts[] =
            {
                test::UtfArgsParser::host().c_str(),
            };

            const auto endpointSelectorImpl = bl::EndpointSelectorImpl::createInstance(
                test::UtfArgsParser::port(),
                defaultHosts,
                defaultHosts + BL_ARRAY_SIZE( defaultHosts )
                );

            const auto endpointSelector = bl::om::qi< bl::EndpointSelector >( endpointSelectorImpl );

            const om::ObjPtrCopyable< TaskControlTokenRW > controlToken =
                SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

            const auto context = bl::transfer::SendRecvContext::createInstance( bl::om::copy( endpointSelector ) );

            const auto cbProxyTransferTests =
                [ & ]( SAA_in const om::ObjPtr< DataChunkStorage >& syncCacheStorage ) -> void
            {
                utils::ExecutionTimer timer( "proxy: total time" );

                const auto proxyStorage = om::lockDisposable(
                    ProxyDataChunkStorageImpl::createInstance< DataChunkStorage >(
                        endpointSelector,
                        syncCacheStorage,
                        context -> dataBlocksPool(),
                        om::qi< TaskControlToken >( controlToken )
                        )
                    );

                {
                    /*
                     * Create a small thread pool with few threads, so all operations can execute
                     * from there and then we can dispose the thread pool which will close the
                     * outstanding connections
                     */

                    auto threadPool = om::lockDisposable(
                        ThreadPoolImpl::createInstance< ThreadPool >( os::getAbstractPriorityDefault() )
                        );

                    {
                        /*
                         * The first test is just simple save/load/remove sequence
                         */

                        scheduleAndExecuteInParallel(
                            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                            {
                                eq -> setOptions( ExecutionQueue::OptionKeepNone );
                                eq -> setLocalThreadPool( threadPool.get() );

                                const auto chunkId = uuids::create();
                                const auto dataBlockIn = BackendImplTestImpl::initDataBlock();

                                eq -> wait(
                                    eq -> push_back(
                                        [ & ]() -> void
                                        {
                                            proxyStorage -> save( uuids::nil(), chunkId, dataBlockIn );
                                        }
                                        )
                                    );

                                {
                                    auto g = BL_SCOPE_GUARD(
                                        {
                                            BL_WARN_NOEXCEPT_BEGIN()

                                            eq -> wait(
                                                eq -> push_back(
                                                    [ & ]() -> void
                                                    {
                                                        proxyStorage -> remove( uuids::nil(), chunkId );
                                                    }
                                                    )
                                                );

                                            BL_WARN_NOEXCEPT_END( "Tasks_ProxyDataChunkBackendImplTests" )
                                        }
                                        );

                                    const auto cbTestBlock = [ & ]() -> void
                                    {
                                        const auto dataBlockOut = bl::data::DataBlock::createInstance();

                                        dataBlockOut -> setSize( 0 );
                                        char* psz = dataBlockOut -> begin();
                                        psz[ 0 ] = 42;
                                        psz[ 1 ] = 42;
                                        psz[ 3 ] = 42;

                                        proxyStorage -> load( uuids::nil(), chunkId, dataBlockOut );
                                        BackendImplTestImpl::verifyData( dataBlockOut );
                                    };

                                    eq -> wait(
                                        eq -> push_back(
                                            [ & ]() -> void
                                            {
                                                cbTestBlock();
                                            }
                                            )
                                        );

                                    const std::size_t noOfIterations = 10;

                                    {
                                        utils::ExecutionTimer timer( "proxy: load calls (first)" );

                                        for( std::size_t i = 0; i < noOfIterations; ++i )
                                        {
                                            eq -> push_back(
                                                [ & ]() -> void
                                                {
                                                    cbTestBlock();
                                                }
                                                );
                                        }

                                        eq -> flush();
                                    }

                                    {
                                        utils::ExecutionTimer timer( "proxy: save calls" );

                                        for( std::size_t i = 0; i < noOfIterations; ++i )
                                        {
                                            eq -> push_back(
                                                [ & ]() -> void
                                                {
                                                    proxyStorage -> save( uuids::nil(), chunkId, dataBlockIn );
                                                }
                                                );
                                        }

                                        eq -> flush();
                                    }

                                    {
                                        utils::ExecutionTimer timer( "proxy: load calls (second time)" );

                                        for( std::size_t i = 0; i < noOfIterations; ++i )
                                        {
                                            eq -> push_back(
                                                [ & ]() -> void
                                                {
                                                    cbTestBlock();
                                                }
                                                );
                                        }

                                        eq -> flush();
                                    }

                                    eq -> wait(
                                        eq -> push_back(
                                            [ & ]() -> void
                                            {
                                                proxyStorage -> remove( uuids::nil(), chunkId );
                                            }
                                            )
                                        );

                                    g.dismiss();
                                }
                            }
                            );
                    }
                }
            };

            transferCallback(
                cbProxyTransferTests,
                controlToken,
                context,
                test::UtfArgsParser::port() /* blobServerPrivatePort */
                );
        }

    public:

        static auto getInMemoryMetadataStore() -> bl::om::ObjPtrCopyable< FilesystemMetadataStore >
        {
            return bl::om::ObjPtrCopyable< FilesystemMetadataStore >(
                FilesystemMetadataStoreInMemoryImpl::createInstance< FilesystemMetadataStore >()
                );
        }

        static void executeTheFilesPackagerAndTransmitterPipelineWithFaults(
            SAA_in              const std::size_t                                               noOfDownloads,
            SAA_in              const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  contextIn,
            SAA_in              const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&        metadataStore,
            SAA_in              const std::string&                                              host,
            SAA_in              const unsigned short                                            port,
            SAA_in              const CancelType                                                cancelType,
            SAA_in              const PipelineFaultOptions&                                     faultOptions
            )
        {
            switch( cancelType )
            {
                default:
                    UTF_FAIL( "Invalid cancelType value" );
                    break;

                case CancelType::NoCancel:
                    {
                        executeTheFilesPackagerAndTransmitterPipelineInternal(
                            noOfDownloads,
                            contextIn,
                            metadataStore,
                            host,
                            port,
                            CancelType::NoCancel,
                            bl::cpp::void_callback_t()      /* cancelCallback */,
                            nullptr                         /* executionQueue */,
                            faultOptions
                            );
                    }
                    break;

                case CancelType::CancelUpload:
                case CancelType::CancelDownload:
                case CancelType::CancelRemove:
                    {
                        executeTheFilesPackagerAndTransmitterPipelineExpectFailure(
                            noOfDownloads,
                            contextIn,
                            metadataStore,
                            host,
                            port,
                            cancelType
                            );
                    }
                    break;
            }
        }

        static void executeTheFilesPackagerAndTransmitterPipeline(
            SAA_in              const std::size_t                                               noOfDownloads,
            SAA_in              const bl::om::ObjPtrCopyable< bl::transfer::SendRecvContext >&  contextIn,
            SAA_in              const bl::om::ObjPtrCopyable< FilesystemMetadataStore >&        metadataStore,
            SAA_in_opt          const std::string&                                              host = "localhost",
            SAA_in_opt          const unsigned short                                            port = test::UtfArgsParser::PORT_DEFAULT,
            SAA_in_opt          const CancelType                                                cancelType = CancelType::NoCancel
            )
        {
            executeTheFilesPackagerAndTransmitterPipelineWithFaults(
                noOfDownloads,
                contextIn,
                metadataStore,
                host,
                port,
                cancelType,
                PipelineFaultOptions()
                );
        }

        static void startBlobClient()
        {
            using namespace bl;
            using namespace test;

            if( ! UtfArgsParser::isClient() )
            {
                return;
            }

            const auto context = bl::transfer::SendRecvContext::createInstance(
                SimpleEndpointSelectorImpl::createInstance< EndpointSelector >(
                    cpp::copy( UtfArgsParser::host() ),
                    UtfArgsParser::port()
                    )
                );

            executeTheFilesPackagerAndTransmitterPipeline(
                1U /* noOfDownloads */,
                om::ObjPtrCopyable< bl::transfer::SendRecvContext >::acquireRef( context.get() ),
                getInMemoryMetadataStore(),
                UtfArgsParser::host(),
                UtfArgsParser::port()
                );
        }

        static void startBlobServerProxy()
        {
            using namespace bl;
            using namespace bl::data;
            using namespace bl::tasks;
            using namespace bl::messaging;
            using namespace bl::transfer;
            using namespace test;

            if( ! UtfArgsParser::isServer() )
            {
                return;
            }

            cpp::SafeUniquePtr< test::MachineGlobalTestLock > lock;

            if( UtfArgsParser::port() == UtfArgsParser::PORT_DEFAULT )
            {
                /*
                 * We only acquire the global lock if we use the default
                 * port (UtfArgsParser::PORT_DEFAULT); otherwise this is
                 * a private instance and there is no need for locking
                 */

                lock.reset( new test::MachineGlobalTestLock );
            }

            static const char* defaultHosts[] =
            {
                UtfArgsParser::host().c_str(),
            };

            const auto endpointSelectorImpl = bl::EndpointSelectorImpl::createInstance(
                UtfArgsParser::port(),
                defaultHosts,
                defaultHosts + BL_ARRAY_SIZE( defaultHosts )
                );

            const auto endpointSelector = bl::om::qi< bl::EndpointSelector >( endpointSelectorImpl );

            const auto proxyStorage = om::lockDisposable(
                ProxyDataChunkStorageImpl::createInstance< DataChunkStorage >( endpointSelector )
                );

            BlobServerFacade::startForSyncStorage(
                UtfArgsParser::threadsCount(),
                0U /* maxConcurrentTasks */,
                proxyStorage /* writeStorage */,
                proxyStorage /* readStorage */,
                bl::str::empty() /* privateKeyPem */,
                bl::str::empty() /* certificatePem */,
                UtfArgsParser::port() != UtfArgsParser::PORT_DEFAULT ?
                    UtfArgsParser::PORT_DEFAULT :
                    UtfArgsParser::PORT_DEFAULT + 1
                );
        }
    };

    typedef TestBlobTransferUtilsT<> TestBlobTransferUtils;

} // utest

#endif /* __UTESTS_BASELIB_TESTBLOBTRANSFERUTILS_H_ */
