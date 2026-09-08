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

#include <utests/baselib/TestBlobTransferFilesystemUtils.h>

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
            utest::TestBlobTransferUtils::CancelType::NoCancel
            )
        );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryCancelUploadTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
            utest::TestBlobTransferUtils::CancelType::CancelUpload
            )
        );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryCancelDownloadTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
            utest::TestBlobTransferUtils::CancelType::CancelDownload
            )
        );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryCancelRemoveTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
            utest::TestBlobTransferUtils::CancelType::CancelRemove
            )
        );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryWithheldChunkTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    /*
     * Withhold the first chunk from the unpackager: the download must fail instead
     * of publishing an incomplete tree
     */

    utest::TestBlobTransferUtils::PipelineFaultOptions faultOptions;
    faultOptions.withheldChunkIndex = 0U;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWithFaultsWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
            utest::TestBlobTransferUtils::CancelType::NoCancel,
            faultOptions
            )
        );
}

/************************************************************************
 * Reconnect and re-authentication fault injection tests
 *
 * The blob server requires authentication and drops every client connection instead of
 * serving the first save / load / remove; the client must reconnect, re-authenticate and
 * then retransmit (or re-request) the postponed chunk even though no further input arrives
 */

namespace
{
    utest::PipelineFaultOptions createReauthenticationFaultOptions(
        SAA_in          const utest::PipelineFaultOptions::ServerFault              serverFault
        )
    {
        utest::PipelineFaultOptions faultOptions;

        faultOptions.authenticationToken = "blob-transfer-test-token";
        faultOptions.disablePeerSessionsTracking = true;    /* the transmitter reconnects only without sessions */
        faultOptions.singleFileInput = true;                /* exactly one chunk, so the drop hits the last one */
        faultOptions.serverFault = serverFault;

        return faultOptions;
    }

    void executeFaultInjectionTest( SAA_in const utest::PipelineFaultOptions& faultOptions )
    {
        utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
            bl::cpp::bind(
                &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWithFaultsWrap,
                test::UtfArgsParser::port() /* blobServerPort */,
                utest::TestBlobTransferUtils::getInMemoryMetadataStore(),
                utest::TestBlobTransferUtils::CancelType::NoCancel,
                faultOptions
                )
            );
    }
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnSaveTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    const auto faultOptions = createReauthenticationFaultOptions(
        utest::PipelineFaultOptions::ServerFault::DropConnectionOnFirstSave
        );

    executeFaultInjectionTest( faultOptions );

    /*
     * The pipeline verifies the downloaded tree; the counters prove that the chunk was
     * transmitted exactly once after the drop and that the drop actually happened
     */

    UTF_REQUIRE_EQUAL( faultOptions.counters -> connectionDrops.load(), std::size_t( 1 ) );
    UTF_REQUIRE_EQUAL( faultOptions.counters -> savesForwarded.load(), std::size_t( 1 ) );
    UTF_REQUIRE( faultOptions.counters -> authentications.load() >= std::size_t( 2 ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnLoadTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    const auto faultOptions = createReauthenticationFaultOptions(
        utest::PipelineFaultOptions::ServerFault::DropConnectionOnFirstLoad
        );

    executeFaultInjectionTest( faultOptions );

    UTF_REQUIRE_EQUAL( faultOptions.counters -> connectionDrops.load(), std::size_t( 1 ) );
    UTF_REQUIRE_EQUAL( faultOptions.counters -> loadsForwarded.load(), std::size_t( 1 ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryReauthAfterDropOnRemoveTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    const auto faultOptions = createReauthenticationFaultOptions(
        utest::PipelineFaultOptions::ServerFault::DropConnectionOnFirstRemove
        );

    executeFaultInjectionTest( faultOptions );

    /*
     * The pipeline verifies that the download after the removal fails with 'not found'
     */

    UTF_REQUIRE_EQUAL( faultOptions.counters -> connectionDrops.load(), std::size_t( 1 ) );
    UTF_REQUIRE_EQUAL( faultOptions.counters -> removesForwarded.load(), std::size_t( 1 ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryUploadFailureKeepsMetadataMutableTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    /*
     * A server error fails the upload; the metadata object must not be finalized
     */

    utest::PipelineFaultOptions faultOptions;

    faultOptions.singleFileInput = true;
    faultOptions.expectedUploadOutcome = utest::PipelineFaultOptions::UploadOutcome::ServerError;
    faultOptions.serverFault = utest::PipelineFaultOptions::ServerFault::FailFirstSave;

    executeFaultInjectionTest( faultOptions );

    UTF_REQUIRE_EQUAL( faultOptions.counters -> savesForwarded.load(), std::size_t( 0 ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryDropWithSessionsFailsUploadTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    /*
     * With the peer sessions tracking enabled the transmitter must not reconnect after a
     * dropped connection (see ChunksTransmitter::isSafeToReconnect()): the upload fails
     * loudly with the socket error, the chunk is not retransmitted and the metadata is
     * not finalized
     */

    auto faultOptions = createReauthenticationFaultOptions(
        utest::PipelineFaultOptions::ServerFault::DropConnectionOnFirstSave
        );

    faultOptions.disablePeerSessionsTracking = false;
    faultOptions.forcePeerSessionsTracking = true;
    faultOptions.expectedUploadOutcome = utest::PipelineFaultOptions::UploadOutcome::ConnectionError;

    executeFaultInjectionTest( faultOptions );

    UTF_REQUIRE_EQUAL( faultOptions.counters -> connectionDrops.load(), std::size_t( 1 ) );
    UTF_REQUIRE_EQUAL( faultOptions.counters -> savesForwarded.load(), std::size_t( 0 ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryThrottledSubscribersTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    /*
     * The default throttle limit is 1024 pending events and the fixed input tree never gets
     * anywhere near it, so notifyOnNext() has never returned false in an automated run and the
     * 'the subscriber is full, offer the same value again later' branch of the scanner, the
     * packager, the transmitter, the receiver and the deleter is dead code
     *
     * A limit of one forces every one of them to execute. These are exactly the branches where
     * a value can be lost rather than retried (a data block returned to the pool twice, a chunk
     * id popped from the scheduler queue, m_current reset before a successful publish), and the
     * shared pipeline's own end to end tree comparison detects any such loss immediately
     */

    utest::PipelineFaultOptions faultOptions;

    faultOptions.subscriberThrottleLimit = 1U;

    executeFaultInjectionTest( faultOptions );
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryPeerSessionsTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    /*
     * With the peer sessions tracking enabled each unit re-purposes one transfer task to send
     * CommandId::FlushPeerSessions after its last chunk, and the deleter must not report that
     * completion as a deleted chunk
     *
     * The tracking is off by default and the only other case which enables it expects the
     * upload to fail, which forces the unwind and skips the flush - so this is the only
     * automated coverage of the successful handshake
     *
     * The authentication token is what routes the run through the fault injecting storage,
     * which is where the flush is counted; the server does not require authentication for the
     * flush control code itself
     */

    utest::PipelineFaultOptions faultOptions;

    faultOptions.forcePeerSessionsTracking = true;
    faultOptions.authenticationToken = "blob-transfer-test-token";

    executeFaultInjectionTest( faultOptions );

    UTF_REQUIRE( faultOptions.counters -> sessionFlushes.load() >= std::size_t( 1 ) );
}

/************************************************************************
 * Symlink target policy tests for the unpackager unit
 *
 * The symlink targets are recorded verbatim by the packager, so a package can carry a
 * target which escapes the tree being unpacked; the policy is the only defence against
 * that. These tests need no blob server and transfer no chunks - the metadata is built
 * by hand and the unpackager unit is driven standalone, so they must not acquire the
 * machine global test lock
 */

namespace
{
    typedef bl::transfer::FilesUnpackagerUnit                                       unpackager_unit_t;

    bl::om::ObjPtr< bl::data::FilesystemMetadataRO > createSymlinkTargetPolicyMetadata()
    {
        using namespace bl;

        typedef data::FilesystemMetadata                                            fsmd_t;
        typedef utest::TestBlobTransferUtils                                        utils_t;

        const auto now = std::time( nullptr );
        BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

        const auto fsmdWO =
            data::FilesystemMetadataInMemoryImpl::createInstance< data::FilesystemMetadataWO >();

        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "d", fs::path() /* targetPath */, 0U /* size */, now );
        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "d/sub", fs::path(), 0U, now );

        /*
         * Two targets which resolve inside the tree being unpacked, one which escapes it
         * via '..' and one which is absolute
         *
         * Note that the traversal can only be placed in the target path - the metadata
         * object itself rejects a relative path containing '..'
         */

        utils_t::createEntry( fsmdWO, fsmd_t::Symlink, "d/okRelative", "sub", 0U, now );
        utils_t::createEntry( fsmdWO, fsmd_t::Symlink, "d/okDotDot", "../d/sub", 0U, now );
        utils_t::createEntry( fsmdWO, fsmd_t::Symlink, "d/escapes", "../../outside", 0U, now );
        utils_t::createEntry( fsmdWO, fsmd_t::Symlink, "d/absolute", "/etc/passwd", 0U, now );

        fsmdWO -> finalize();

        return om::qi< data::FilesystemMetadataRO >( fsmdWO );
    }

    void chkSymlinkTargetPolicy(
        SAA_in          const unpackager_unit_t::SymlinkTargetPolicy                symlinkTargetPolicy,
        SAA_in          const bool                                                  escapingLinkExpected,
        SAA_in          const bool                                                  absoluteLinkExpected
        )
    {
        using namespace bl;

        /*
         * The one line warning the unit logs for each link it rejects is expected here and
         * it is emitted from the worker threads, so the level has to be pushed globally
         */

        const Logging::LevelPusher pushLevel( Logging::LL_ERROR, true /* global */ );

        const fs::TmpDir tmpDir;

        const auto targetDir = tmpDir.path() / "out";

        const auto fsmdRO = createSymlinkTargetPolicyMetadata();

        if( ! os::onUNIX() )
        {
            /*
             * Symlinks are not supported on Windows, so with SuaError the unit fails before
             * the target policy is ever consulted
             */

            UTF_REQUIRE_THROW(
                utest::TestBlobTransferUtils::runStandaloneUnpackager(
                    fsmdRO,
                    targetDir,
                    symlinkTargetPolicy
                    ),
                NotSupportedException
                );

            return;
        }

        const auto staging = utest::TestBlobTransferUtils::runStandaloneUnpackager(
            fsmdRO,
            targetDir,
            symlinkTargetPolicy
            );

        /*
         * A rejected link is not an error - the unit must have completed successfully and
         * it must have kept its staging directory
         */

        UTF_REQUIRE( ! staging.empty() );

        UTF_REQUIRE( fs::is_symlink( fs::symlink_status( staging / "d/okRelative" ) ) );
        UTF_REQUIRE( fs::is_symlink( fs::symlink_status( staging / "d/okDotDot" ) ) );

        /*
         * The target is recorded verbatim
         */

        UTF_REQUIRE_EQUAL( fs::read_symlink( staging / "d/okRelative" ), fs::path( "sub" ) );

        if( escapingLinkExpected )
        {
            UTF_REQUIRE( fs::is_symlink( fs::symlink_status( staging / "d/escapes" ) ) );
        }
        else
        {
            UTF_REQUIRE( ! fs::path_exists( staging / "d/escapes" ) );
        }

        if( absoluteLinkExpected )
        {
            UTF_REQUIRE( fs::is_symlink( fs::symlink_status( staging / "d/absolute" ) ) );
        }
        else
        {
            UTF_REQUIRE( ! fs::path_exists( staging / "d/absolute" ) );
        }
    }
}

UTF_AUTO_TEST_CASE( BlobTransfer_UnpackagerSymlinkTargetPolicyTests )
{
    /*
     * StpContained: only the targets which resolve inside the tree being unpacked survive
     */

    chkSymlinkTargetPolicy(
        unpackager_unit_t::StpContained,
        false /* escapingLinkExpected */,
        false /* absoluteLinkExpected */
        );

    /*
     * StpRelativeOnly: only the absolute target is rejected
     */

    chkSymlinkTargetPolicy(
        unpackager_unit_t::StpRelativeOnly,
        true /* escapingLinkExpected */,
        false /* absoluteLinkExpected */
        );

    /*
     * StpAllow is the default and it must keep creating every link, which is the
     * behaviour the unpackager always had
     */

    chkSymlinkTargetPolicy(
        unpackager_unit_t::StpAllow,
        true /* escapingLinkExpected */,
        true /* absoluteLinkExpected */
        );
}

/************************************************************************
 * Chunk integrity tests for the unpackager unit
 *
 * The pipeline always feeds the unpackager exactly what the packager produced, so the three
 * integrity checks compare two values computed by the same code and have never failed in a
 * test. The metadata is built by hand here and the chunks are fed into a standalone unit, so
 * the corruption is genuine. No blob server and no machine global test lock are needed
 */

namespace
{
    typedef utest::TestBlobTransferUtils::unpackager_unit_t                          unpackager_pu_t;

    /*
     * The corruption injected into the metadata of the file entry
     */

    enum ChunkIntegrityFault
    {
        CifNone,
        CifChunkChecksum,               /* the chunk checksum does not match the chunk payload */
        CifChunkPosition,               /* the second chunk does not start where the first ends */
        CifFileChecksum,                /* the file checksum does not match the chunk checksums */
    };

    /*
     * The same position dependent pattern TestFsUtils::createDummyFile writes, so a chunk
     * written at the wrong offset, in the wrong order or twice is detectable
     */

    char patternByte( SAA_in const std::uint64_t offset )
    {
        return static_cast< char >( ( offset * 31U + 7U ) % 251U );
    }

    bl::om::ObjPtr< bl::data::DataBlock > createChunkData(
        SAA_in          const std::uint64_t                                          pos,
        SAA_in          const std::size_t                                            size
        )
    {
        auto block = bl::data::DataBlock::createInstance();

        UTF_REQUIRE( size <= block -> capacity() );

        block -> setSize( size );

        char* const data = block -> begin();

        for( std::size_t i = 0U; i < size; ++i )
        {
            data[ i ] = patternByte( pos + i );
        }

        return block;
    }

    std::uint32_t computeChunkChecksum( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& block )
    {
        bl::cs::crc_32_type crcc;

        crcc.process_bytes( block -> pv(), block -> size() );

        return crcc.checksum();
    }

    /**
     * @brief Feeds a hand built package of one directory and one multi chunk file into a
     * standalone unpackager, with the requested corruption injected into the metadata
     *
     * When expectedMessage is not nullptr the unit is required to fail with that message and
     * to have discarded its staging directory; otherwise it must unpackage the file correctly
     */

    void chkChunkIntegrity(
        SAA_in          const ChunkIntegrityFault                                    fault,
        SAA_in          const std::size_t                                            noOfChunks,
        SAA_in_opt      const char* const                                            expectedMessage
        )
    {
        using namespace bl;

        typedef data::FilesystemMetadata                                             fsmd_t;
        typedef utest::TestBlobTransferUtils                                         utils_t;

        const std::size_t chunkSize = 1024U;

        const auto now = std::time( nullptr );
        BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

        const auto fsmdWO =
            data::FilesystemMetadataInMemoryImpl::createInstance< data::FilesystemMetadataWO >();

        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "d", fs::path() /* targetPath */, 0U /* size */, now );

        const auto fileSize = chunkSize * noOfChunks;

        const auto entryId = utils_t::createEntry(
            fsmdWO,
            fsmd_t::File,
            "d/f.bin",
            fs::path()                                          /* targetPath */,
            fileSize,
            now
            );

        std::vector< uuid_t > chunkIds;
        std::vector< om::ObjPtr< data::DataBlock > > chunkBlocks;

        cs::crc_32_type fileCrc;

        for( std::size_t i = 0U; i < noOfChunks; ++i )
        {
            const std::uint64_t pos = chunkSize * i;

            auto block = createChunkData( pos, chunkSize );

            fsmd_t::ChunkInfo chunkInfo;

            chunkInfo.pos = pos;
            chunkInfo.size = static_cast< std::uint32_t >( chunkSize );
            chunkInfo.checksum = computeChunkChecksum( block );

            if( CifChunkChecksum == fault && 0U == i )
            {
                chunkInfo.checksum = chunkInfo.checksum.value() + 1U;
            }

            if( CifChunkPosition == fault && 1U == i )
            {
                chunkInfo.pos = pos + 1U;
            }

            /*
             * The file level checksum is the CRC over the chunk checksums in file position
             * order - exactly what the packager computes and what the unpackager recomputes
             * from the very same chunk infos, so a corrupted chunk checksum stays consistent
             * here and only the chunk level check fires
             */

            const std::uint32_t chunkChecksumValue = chunkInfo.checksum;

            fileCrc.process_bytes( &chunkChecksumValue, sizeof( chunkChecksumValue ) );

            chunkIds.push_back( fsmdWO -> createChunk( entryId, std::move( chunkInfo ) ) );
            chunkBlocks.push_back( std::move( block ) );
        }

        fsmdWO -> associateChecksum(
            entryId,
            CifFileChecksum == fault ? fileCrc.checksum() + 1U : fileCrc.checksum()
            );

        fsmdWO -> finalize();

        fs::path staging;

        const utils_t::unpackager_feed_callback_t feedCallback =
            [ & ]( SAA_inout unpackager_pu_t& unit ) -> void
            {
                staging = unit.targetTmpDir();

                UTF_REQUIRE( ! staging.empty() );

                /*
                 * onChunkArrived() does not verify that the unit has been started, so no chunk
                 * may be pushed before the unit's first loop iteration has created its worker
                 * queue; a created directory proves that an IoOperationTask has already run
                 */

                const std::size_t maxWaitIterations = 500U;

                std::size_t waited = 0U;

                for( ; waited < maxWaitIterations; ++waited )
                {
                    if( fs::exists( staging / "d" ) )
                    {
                        break;
                    }

                    os::sleep( time::milliseconds( 20 ) );
                }

                if( maxWaitIterations == waited )
                {
                    UTF_FAIL( "The unpackager unit did not create the package directories in time" );
                }

                const auto input = unit.bindInputConnector< unpackager_pu_t >(
                    &unpackager_pu_t::onChunkArrived,
                    &unpackager_pu_t::onInputCompleted
                    );

                for( std::size_t i = 0U; i < chunkIds.size(); ++i )
                {
                    data::DataChunkBlock chunkBlock;

                    chunkBlock.chunkId = chunkIds[ i ];
                    chunkBlock.data = chunkBlocks[ i ];

                    while( ! input -> onNext( cpp::any( chunkBlock ) ) )
                    {
                        os::sleep( time::milliseconds( 20 ) );
                    }
                }

                input -> onCompleted();
            };

        const fs::TmpDir tmpDir;

        const auto targetDir = tmpDir.path() / "out";

        const auto fsmdRO = om::qi< data::FilesystemMetadataRO >( fsmdWO );

        if( expectedMessage )
        {
            UTF_REQUIRE_THROW_MESSAGE(
                utils_t::runStandaloneUnpackager(
                    fsmdRO,
                    targetDir,
                    unpackager_unit_t::StpAllow,
                    feedCallback
                    ),
                UnexpectedException,
                expectedMessage
                );

            /*
             * flushAllPendingTasks() discards the staging directory when the unit fails, so
             * nothing partial is ever left behind
             */

            UTF_REQUIRE( ! staging.empty() );
            UTF_REQUIRE( ! fs::path_exists( staging ) );

            return;
        }

        const auto result = utils_t::runStandaloneUnpackager(
            fsmdRO,
            targetDir,
            unpackager_unit_t::StpAllow,
            feedCallback
            );

        UTF_REQUIRE( ! result.empty() );
        UTF_REQUIRE_EQUAL( result, staging );

        const auto filePath = staging / "d" / "f.bin";

        UTF_REQUIRE( fs::path_exists( filePath ) );
        UTF_REQUIRE_EQUAL( fs::file_size( filePath ), static_cast< std::uint64_t >( fileSize ) );

        /*
         * Byte for byte - every chunk landed at the right offset and in the right order
         */

        std::vector< char > content( fileSize );

        {
            const auto infile = os::fopen( filePath, "rb" );

            os::fread( infile, &content[ 0 ], fileSize );
        }

        std::size_t invalidPos = fileSize;

        for( std::size_t i = 0U; i < fileSize; ++i )
        {
            if( content[ i ] != patternByte( i ) )
            {
                invalidPos = i;
                break;
            }
        }

        UTF_REQUIRE_EQUAL( invalidPos, fileSize );
    }
}

UTF_AUTO_TEST_CASE( BlobTransfer_UnpackagerChunkIntegrityTests )
{
    /*
     * A single chunk file whose recorded chunk checksum does not match its payload
     */

    chkChunkIntegrity(
        CifChunkChecksum,
        1U /* noOfChunks */,
        "Integrity check failed. Invalid chunk checksum"
        );

    /*
     * A two chunk file whose chunk offsets do not tile the file exactly
     */

    chkChunkIntegrity(
        CifChunkPosition,
        2U /* noOfChunks */,
        "Some chunks have not arrived properly or duplicated chunks were received"
        );

    /*
     * A two chunk file whose per chunk checksums are correct but whose file level checksum
     * is not
     */

    chkChunkIntegrity(
        CifFileChecksum,
        2U /* noOfChunks */,
        "Integrity check failed. Invalid file checksum"
        );

    /*
     * The positive control - everything consistent, so the very same harness proves that the
     * three failures above are caused by the corruption and not by the harness itself
     */

    chkChunkIntegrity(
        CifNone,
        2U /* noOfChunks */,
        nullptr /* expectedMessage */
        );
}

/************************************************************************
 * Directory timestamps tests for the unpackager unit
 *
 * Restoring the directory timestamps is the entire third phase of the unpackager scheduler
 * and nothing has ever asserted it: TestFsUtils::compareFolders only compares the names of
 * the directories it recurses into. No chunks, no blob server, no machine global test lock
 */

namespace
{
    void chkUnpackagerDirectoryTimestamps( SAA_in const bool fileEntryFirst )
    {
        using namespace bl;

        typedef data::FilesystemMetadata                                             fsmd_t;
        typedef utest::TestBlobTransferUtils                                         utils_t;

        const auto now = std::time( nullptr );
        BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

        /*
         * Well in the past and far enough apart that the filesystem timestamp granularity
         * cannot make any of the assertions below flaky
         */

        const std::time_t aTime = now - 30000;
        const std::time_t bTime = now - 20000;
        const std::time_t cTime = now - 10000;
        const std::time_t fileTime = now - 5000;

        const auto fsmdWO =
            data::FilesystemMetadataInMemoryImpl::createInstance< data::FilesystemMetadataWO >();

        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "a", fs::path() /* targetPath */, 0U /* size */, aTime );

        if( fileEntryFirst )
        {
            utils_t::createEntry( fsmdWO, fsmd_t::File, "a/b/f.txt", fs::path(), 0U, fileTime );
            utils_t::createEntry( fsmdWO, fsmd_t::Directory, "a/b", fs::path(), 0U, bTime );
        }
        else
        {
            utils_t::createEntry( fsmdWO, fsmd_t::Directory, "a/b", fs::path(), 0U, bTime );
            utils_t::createEntry( fsmdWO, fsmd_t::File, "a/b/f.txt", fs::path(), 0U, fileTime );
        }

        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "a/b/c", fs::path(), 0U, cTime );

        fsmdWO -> finalize();

        const fs::TmpDir tmpDir;

        const auto staging = utils_t::runStandaloneUnpackager(
            om::qi< data::FilesystemMetadataRO >( fsmdWO ),
            tmpDir.path() / "out"
            );

        UTF_REQUIRE( ! staging.empty() );

        /*
         * The zero length file is created through handleFileInternal()'s '! chunksExpected'
         * branch, which nothing else reaches deliberately
         */

        UTF_REQUIRE( fs::path_exists( staging / "a" / "b" / "f.txt" ) );
        UTF_REQUIRE_EQUAL( fs::file_size( staging / "a" / "b" / "f.txt" ), std::uint64_t( 0 ) );
        UTF_REQUIRE_EQUAL( fs::last_write_time( staging / "a" / "b" / "f.txt" ), fileTime );

        /*
         * updateDirsTimestamps() must have run for every directory of the package
         */

        UTF_REQUIRE_EQUAL( fs::last_write_time( staging / "a" ), aTime );
        UTF_REQUIRE_EQUAL( fs::last_write_time( staging / "a" / "b" / "c" ), cTime );

        if( fileEntryFirst )
        {
            /*
             * This pins the CURRENT behaviour, not a desirable one: the scheduler keys the
             * map by the directory path and keeps the FIRST entry it sees for each key, and
             * the entries are iterated in creation order - so when a file of a directory is
             * recorded before the directory's own entry the directory ends up carrying the
             * file's timestamp rather than its own
             *
             * The unpackager has always behaved this way and the packager always emits the
             * directory entry first, so nothing in the product depends on the other outcome;
             * it is pinned here so that a future change of the 'first key wins' rule is a
             * deliberate one
             */

            UTF_REQUIRE_EQUAL( fs::last_write_time( staging / "a" / "b" ), fileTime );
        }
        else
        {
            UTF_REQUIRE_EQUAL( fs::last_write_time( staging / "a" / "b" ), bTime );
        }
    }
}

UTF_AUTO_TEST_CASE( BlobTransfer_UnpackagerDirectoryTimestampsTests )
{
    /*
     * The ordinary case - every directory entry precedes the entries it contains
     */

    chkUnpackagerDirectoryTimestamps( false /* fileEntryFirst */ );

    /*
     * The same package with the file entry recorded before its parent directory entry, which
     * is what makes the 'first key wins' rule of the scheduler observable
     */

    chkUnpackagerDirectoryTimestamps( true /* fileEntryFirst */ );
}

/************************************************************************
 * Unreachable blob server test for the chunks transmitter
 */

UTF_AUTO_TEST_CASE( BlobTransfer_TransmitterServerUnreachableTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::reactive;
    using namespace bl::tasks;
    using namespace bl::transfer;

    /*
     * An unreachable blob server is the single most likely production failure of this
     * component and its contract is asserted nowhere: one specific exception type, carrying
     * the underlying socket error, and no metadata finalization
     *
     * No blob server is started here, so the machine global test lock must not be taken; the
     * endpoint retry budget is cut down to a single 50 ms attempt, which is the only reason
     * this case is affordable at all
     */

    const auto unusedPort =
        static_cast< unsigned short >( test::UtfArgsParser::PORT_DEFAULT + 7 );

    const fs::TmpDir tmpDir;

    encoding::writeTextFile( tmpDir.path() / "single-chunk-file.txt", std::string( 1024U, 'x' ) );

    const auto selector = SimpleEndpointSelectorImpl::createInstance< EndpointSelector >(
        std::string( "localhost" ),
        unusedPort,
        1U                                                  /* maxRetryCount */,
        time::milliseconds( 50 )                            /* retryTimeout */
        );

    const auto context = SendRecvContext::createInstance(
        SimpleEndpointSelectorImpl::createInstance< EndpointSelector >(
            std::string( "localhost" ),
            unusedPort
            )
        );

    const auto fsmd = FilesystemMetadataInMemoryImpl::createInstance< FilesystemMetadataWO >();

    typedef om::ObjectImpl
        <
            ProcessingUnit< FilesPackagerUnit, Observable >,
            true /* enableSharedPtr */
        > unit_packager_t;

    typedef om::ObjectImpl
        <
            ProcessingUnit< ChunksTransmitter, Observable >,
            true /* enableSharedPtr */
        > unit_transmitter_t;

    const auto scanner = RecursiveDirectoryScannerImpl::createInstance(
        tmpDir.path(),
        nullptr /* cbControl */
        );

    const auto unitPackager = unit_packager_t::createInstance( context, fsmd );

    scanner -> subscribe(
        unitPackager -> bindInputConnector< unit_packager_t >(
            &unit_packager_t::onFilesBatchArrived,
            &unit_packager_t::onInputCompleted
            )
        );

    const auto unitChunksTransmitter = unit_transmitter_t::createInstance(
        selector,
        context,
        fsmd,
        2U /* tasksPoolSize */
        );

    unitPackager -> subscribe(
        unitChunksTransmitter -> bindInputConnector< unit_transmitter_t >(
            &unit_transmitter_t::onChunkArrived,
            &unit_transmitter_t::onInputCompleted
            )
        );

    unitChunksTransmitter -> allowNoSubscribers( true );

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> push_back( om::qi< Task >( unitChunksTransmitter ) );
            eq -> push_back( om::qi< Task >( unitPackager ) );
            eq -> push_back( om::qi< Task >( scanner ) );

            /*
             * Which task executeQueueAndCancelOnFailure() reports first is not
             * deterministic, so the transmitter itself is inspected below
             */

            UTF_REQUIRE_THROW( executeQueueAndCancelOnFailure( eq ), std::exception );
        }
        );

    UTF_REQUIRE( om::qi< Task >( unitChunksTransmitter ) -> isFailed() );

    UTF_REQUIRE_THROW_MESSAGE(
        cpp::safeRethrowException( om::qi< Task >( unitChunksTransmitter ) -> exception() ),
        ServerNoConnectionException,
        "An error has occurred while trying to connect to a blob server node"
        );

    /*
     * unwindWorkerTasks() must not commit metadata which references chunks that never
     * reached a server
     */

    UTF_REQUIRE( ! fsmd -> isFinalized() );
}

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobServer )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual run test)" );

    utest::TestBlobTransferFilesystemUtilsImpl::startBlobServer();
}

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobClient )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isClient(), "requires --is-client (manual run test)" );

    utest::TestBlobTransferFilesystemUtilsImpl::startBlobClient();
}

/************************************************************************
 * Tests for the ProxyDataChunkStorageImpl
 */

UTF_AUTO_TEST_CASE( BlobTransfer_ProxyDataChunkBackendImplTests )
{
    utest::TestBlobTransferFilesystemUtilsImpl::proxyDataChunkBackendImplTests();
}

UTF_AUTO_TEST_CASE( BlobTransfer_ProxyDataChunkStorageCanceledTokenTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::messaging;
    using namespace bl::tasks;

    /*
     * A cancelled control token is observed by createClient() before it touches an endpoint,
     * which is what stops a shutdown from being blocked for the full endpoint retry budget
     * (15 attempts x 6 seconds) per calling thread and what makes the failure a cancellation
     * rather than an ordinary connection error
     *
     * The endpoint deliberately points at a port nothing listens on - this test must never
     * reach it - so no blob server and no machine global test lock are needed
     */

    static const char* hosts[] =
    {
        test::UtfArgsParser::host().c_str(),
    };

    const auto endpointSelector = om::qi< EndpointSelector >(
        EndpointSelectorImpl::createInstance(
            static_cast< unsigned short >( test::UtfArgsParser::port() + 7 ) /* nothing listening */,
            hosts,
            hosts + BL_ARRAY_SIZE( hosts )
            )
        );

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

    controlToken -> requestCancel();

    UTF_REQUIRE( controlToken -> isCanceled() );

    const auto proxy = om::lockDisposable(
        ProxyDataChunkStorageImpl::createInstance< DataChunkStorage >(
            endpointSelector,
            nullptr                                     /* storage - proxy only mode */,
            nullptr                                     /* dataBlocksPool */,
            om::qi< TaskControlToken >( controlToken )
            )
        );

    const auto chunkId = uuids::create();
    const auto block = utest::BackendImplTestImpl::initDataBlock();

    const auto t0 = time::microsec_clock::universal_time();

    UTF_REQUIRE_THROW_MESSAGE(
        proxy -> load( uuids::nil(), chunkId, block ),
        SystemException,
        "shutdown is already in progress"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        proxy -> save( uuids::nil(), chunkId, block ),
        SystemException,
        "shutdown is already in progress"
        );

    UTF_REQUIRE_EXCEPTION(
        proxy -> remove( uuids::nil(), chunkId ),
        SystemException,
        []( const SystemException& ex ) -> bool
        {
            const auto* ec = eh::get_error_info< eh::errinfo_error_code >( ex );

            return nullptr != ec && asio::error::operation_aborted == *ec;
        }
        );

    const auto elapsed = time::microsec_clock::universal_time() - t0;

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "proxy: the cancelled control token was reported in "
            << elapsed.total_milliseconds()
            << " ms"
        );

    /*
     * The load bearing assertion: a single failed connect plus the retry ladder would blow
     * well past this, so it fails if the check ever moves below the connector
     */

    UTF_REQUIRE( elapsed < time::seconds( 10 ) );

    /*
     * flushPeerSessions() is a documented NOP which deliberately does not check the token -
     * the one place where the proxy diverges from the filesystem storages
     */

    UTF_REQUIRE_NO_THROW( proxy -> flushPeerSessions( uuids::create() ) );
}

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobServerProxy )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual run test)" );

    utest::TestBlobTransferFilesystemUtilsImpl::startBlobServerProxy();
}

UTF_AUTO_TEST_CASE( BlobTransfer_FilesPackagerInMemoryProxyTests )
{
    test::MachineGlobalTestLock lockBlobServer;

    utest::TestBlobTransferFilesystemUtilsImpl::executeTransferTestsWrap(
        bl::cpp::bind(
            &utest::TestBlobTransferFilesystemUtilsImpl::filesPackagerTestsWithProxyWrap,
            test::UtfArgsParser::port() /* blobServerPort */,
            utest::TestBlobTransferUtils::getInMemoryMetadataStore()
            )
        );
}


/************************************************************************
 * chk2ScheduleChunk's two corruption guards for the unpackager unit
 *
 * The receiver never sends a chunk twice and a zero-chunk entry never has a chunk id
 * pointing at it, so neither guard has ever fired - they are the unpackager's only defence
 * against a malformed package or a duplicating transport, and they are exactly the kind of
 * check which gets 'simplified away' during a refactoring. No blob server and no machine
 * global test lock are needed
 */

namespace
{
    /**
     * @brief A read only metadata decorator which resolves one nominated chunk id to a
     * different entry than the one it was created against, and which can hide one entry from
     * the scheduler's enumeration
     *
     * FilesystemMetadataInMemoryImpl derives queryChunksCount() from the chunks which were
     * actually created against an entry, so a chunk id which resolves to an entry with zero
     * chunks cannot be built with it at all - which is precisely what makes the second
     * BL_CHK in chk2ScheduleChunk a *corruption* guard: only a malformed package can produce
     * that shape
     *
     * Hiding the entry from queryAllEntries() is what makes the second guard reachable
     * deterministically: a zero-chunk File entry which the scheduler does enumerate is
     * created and finalized straight away, and the arriving chunk would then trip the *first*
     * guard instead - the two are checked in that order
     *
     * Note that UuidIteratorImpl does not copy the vector it is handed, so the filtered entry
     * list is held by this object, which the unit keeps alive for its whole lifetime
     */

    template
    <
        typename E = void
    >
    class RemappedChunkMetadataT : public bl::data::FilesystemMetadataRO
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( RemappedChunkMetadataT, bl::data::FilesystemMetadataRO )

    protected:

        const bl::om::ObjPtr< bl::data::FilesystemMetadataRO >                       m_inner;
        const bl::uuid_t                                                             m_chunkId;
        const bl::uuid_t                                                             m_entryId;
        std::vector< bl::uuid_t >                                                    m_visibleEntries;

        RemappedChunkMetadataT(
            SAA_in          bl::om::ObjPtr< bl::data::FilesystemMetadataRO >&&        inner,
            SAA_in          const bl::uuid_t&                                         chunkId,
            SAA_in          const bl::uuid_t&                                         entryId
            )
            :
            m_inner( BL_PARAM_FWD( inner ) ),
            m_chunkId( chunkId ),
            m_entryId( entryId )
        {
            const auto entries = m_inner -> queryAllEntries();

            while( entries -> hasCurrent() )
            {
                if( entries -> current() != m_entryId )
                {
                    m_visibleEntries.push_back( entries -> current() );
                }

                entries -> loadNext();
            }
        }

    public:

        virtual bl::om::ObjPtr< bl::UuidIterator > queryAllEntries() OVERRIDE
        {
            return bl::UuidIteratorImpl::createInstance< bl::UuidIterator >( m_visibleEntries );
        }

        virtual bl::om::ObjPtr< bl::UuidIterator > queryAllChunks() OVERRIDE
        {
            return m_inner -> queryAllChunks();
        }

        virtual std::size_t queryEntriesCount() OVERRIDE
        {
            return m_visibleEntries.size();
        }

        virtual bl::om::ObjPtr< bl::UuidIterator > queryChunks( SAA_in const bl::uuid_t& entryId ) OVERRIDE
        {
            return m_inner -> queryChunks( entryId );
        }

        virtual std::size_t queryChunksCount( SAA_in const bl::uuid_t& entryId ) OVERRIDE
        {
            return m_inner -> queryChunksCount( entryId );
        }

        virtual bl::uuid_t queryEntryId( SAA_in const bl::uuid_t& chunkId ) OVERRIDE
        {
            return chunkId == m_chunkId ? m_entryId : m_inner -> queryEntryId( chunkId );
        }

        virtual EntryInfo loadEntryInfo( SAA_in const bl::uuid_t& entryId ) OVERRIDE
        {
            return m_inner -> loadEntryInfo( entryId );
        }

        virtual ChunkInfo loadChunkInfo( SAA_in const bl::uuid_t& chunkId ) OVERRIDE
        {
            return m_inner -> loadChunkInfo( chunkId );
        }
    };

    typedef bl::om::ObjectImpl< RemappedChunkMetadataT<> > RemappedChunkMetadata;

    /**
     * @brief One single chunk file entry plus the chunk which belongs to it
     */

    struct LateChunkFile
    {
        bl::uuid_t                                                                   chunkId;
        bl::om::ObjPtr< bl::data::DataBlock >                                        block;
    };

    /**
     * @brief The hand built package both sub-blocks feed
     *
     * It is one directory plus 'noOfFiles' single chunk files; the caller nominates how many,
     * because the first sub-block needs enough legitimate work to cycle the unpackager's
     * fixed worker pool (see the comment in the case below)
     */

    void buildLateChunkPackage(
        SAA_out         bl::om::ObjPtr< bl::data::FilesystemMetadataRO >&            fsmdRO,
        SAA_out         std::vector< LateChunkFile >&                                files,
        SAA_out         bl::uuid_t&                                                  emptyEntryId,
        SAA_in          const std::size_t                                            noOfFiles,
        SAA_in          const std::size_t                                            chunkSize
        )
    {
        using namespace bl;

        typedef data::FilesystemMetadata                                             fsmd_t;
        typedef utest::TestBlobTransferUtils                                         utils_t;

        const auto now = std::time( nullptr );
        BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

        const auto fsmdWO =
            data::FilesystemMetadataInMemoryImpl::createInstance< data::FilesystemMetadataWO >();

        utils_t::createEntry( fsmdWO, fsmd_t::Directory, "d", fs::path() /* targetPath */, 0U /* size */, now );

        for( std::size_t i = 0U; i < noOfFiles; ++i )
        {
            const auto name = resolveMessage( BL_MSG() << "d/f" << i << ".bin" );

            const auto entryId = utils_t::createEntry(
                fsmdWO,
                fsmd_t::File,
                name,
                fs::path()                                          /* targetPath */,
                chunkSize,
                now
                );

            auto block = createChunkData( 0U /* pos */, chunkSize );

            fsmd_t::ChunkInfo chunkInfo;

            chunkInfo.pos = 0U;
            chunkInfo.size = static_cast< std::uint32_t >( chunkSize );
            chunkInfo.checksum = computeChunkChecksum( block );

            const std::uint32_t chunkChecksumValue = chunkInfo.checksum;

            cs::crc_32_type fileCrc;

            fileCrc.process_bytes( &chunkChecksumValue, sizeof( chunkChecksumValue ) );

            LateChunkFile file;

            file.chunkId = fsmdWO -> createChunk( entryId, std::move( chunkInfo ) );
            file.block = std::move( block );

            files.push_back( std::move( file ) );

            fsmdWO -> associateChecksum( entryId, fileCrc.checksum() );
        }

        /*
         * A second File entry which legitimately has size zero and no chunks at all
         */

        emptyEntryId = utils_t::createEntry(
            fsmdWO,
            fsmd_t::File,
            "d/empty.bin",
            fs::path()                                              /* targetPath */,
            0U                                                      /* size */,
            now
            );

        fsmdWO -> finalize();

        fsmdRO = om::qi< data::FilesystemMetadataRO >( fsmdWO );
    }

    /**
     * @brief Blocks until the unpackager unit's first loop iteration has created the package
     * directories, which is the only proof available that its worker queue exists -
     * onChunkArrived() does not verify that the unit has been started
     */

    void waitForUnpackagerStaging( SAA_in const bl::fs::path& staging )
    {
        using namespace bl;

        const std::size_t maxWaitIterations = 500U;

        std::size_t waited = 0U;

        for( ; waited < maxWaitIterations; ++waited )
        {
            if( fs::exists( staging / "d" ) )
            {
                return;
            }

            os::sleep( time::milliseconds( 20 ) );
        }

        UTF_FAIL( "The unpackager unit did not create the package directories in time" );
    }

    /**
     * @brief Pushes one chunk into a bound input connector, retrying while the unit has no
     * idle worker task
     */

    void feedChunk(
        SAA_in          const bl::om::ObjPtr< bl::reactive::Observer >&              input,
        SAA_in          const bl::uuid_t&                                            chunkId,
        SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&                 block
        )
    {
        using namespace bl;

        data::DataChunkBlock chunkBlock;

        chunkBlock.chunkId = chunkId;
        chunkBlock.data = block;

        while( ! input -> onNext( cpp::any( chunkBlock ) ) )
        {
            os::sleep( time::milliseconds( 20 ) );
        }
    }
}

UTF_AUTO_TEST_CASE( BlobTransfer_UnpackagerRejectsLateChunksTests )
{
    using namespace bl;

    typedef utest::TestBlobTransferUtils                                             utils_t;

    const std::size_t chunkSize = 1024U;

    {
        /*
         * (1) The same chunk arrives twice
         *
         * A single chunk entry is finalized as soon as its only chunk has been written, and
         * processTopReadyTask() then moves its id into m_entriesCompleted - any further chunk
         * for it is a duplicating transport and must be rejected rather than silently
         * rewriting a finished file
         *
         * The bookkeeping is only done for the *top* ready task of a fixed worker pool
         * (FixedWorkerPoolUnitBase::DEFAULT_POOL_SIZE is 16 and every worker starts out
         * ready), so 'the file is on disk with the right size' does not yet mean 'the entry
         * has been finalized' - the pool has to cycle first. Feeding a package of 24
         * legitimate single chunk files past a 16 deep pool is what guarantees it, and it is
         * why this sub-block does not simply feed one chunk twice
         */

        const std::size_t noOfFiles = 96U;

        om::ObjPtr< data::FilesystemMetadataRO > fsmdRO;
        std::vector< LateChunkFile > files;
        uuid_t emptyEntryId = uuids::nil();

        buildLateChunkPackage( fsmdRO, files, emptyEntryId, noOfFiles, chunkSize );

        /*
         * The duplicate feed must use a fresh DataBlock - processTopReadyTask() returns the
         * first one to the pool, so reusing it would race the pool
         */

        const auto duplicateBlock = createChunkData( 0U /* pos */, chunkSize );

        fs::path staging;

        om::ObjPtr< utils_t::unpackager_unit_t > unitRef;

        const utils_t::unpackager_feed_callback_t feedCallback =
            [ & ]( SAA_inout utils_t::unpackager_unit_t& unit ) -> void
            {
                /*
                 * The unit is kept alive past runStandaloneUnpackager( ... ) so that its
                 * targetTmpDir() can still be read after the failure
                 */

                unitRef = om::copy( &unit );

                staging = unit.targetTmpDir();

                UTF_REQUIRE( ! staging.empty() );

                waitForUnpackagerStaging( staging );

                const auto input = unit.bindInputConnector< utils_t::unpackager_unit_t >(
                    &utils_t::unpackager_unit_t::onChunkArrived,
                    &utils_t::unpackager_unit_t::onInputCompleted
                    );

                for( const auto& file : files )
                {
                    feedChunk( input, file.chunkId, file.block );
                }

                /*
                 * The first file is written out long before the last one is fed
                 */

                const fs::path filePath = staging / "d" / "f0.bin";

                const std::size_t maxWaitIterations = 500U;

                std::size_t waited = 0U;

                for( ; waited < maxWaitIterations; ++waited )
                {
                    if(
                        fs::path_exists( filePath ) &&
                        fs::file_size( filePath ) == static_cast< std::uint64_t >( chunkSize )
                        )
                    {
                        break;
                    }

                    os::sleep( time::milliseconds( 20 ) );
                }

                if( maxWaitIterations == waited )
                {
                    UTF_FAIL( "The unpackager unit did not write out the first chunk in time" );
                }

                feedChunk( input, files.front().chunkId, duplicateBlock );

                input -> onCompleted();
            };

        const fs::TmpDir tmpDir;

        UTF_REQUIRE_THROW_MESSAGE(
            utils_t::runStandaloneUnpackager(
                fsmdRO,
                tmpDir.path() / "out"                       /* targetDir */,
                unpackager_unit_t::StpAllow,
                feedCallback
                ),
            UnexpectedException,
            "Chunk has arrived for an entry which has been finalized"
            );

        /*
         * flushAllPendingTasks() discards the staging directory when the unit fails, so
         * nothing partial is ever left behind
         */

        UTF_REQUIRE( unitRef );
        UTF_REQUIRE( unitRef -> targetTmpDir().empty() );

        UTF_REQUIRE( ! staging.empty() );
        UTF_REQUIRE( ! fs::path_exists( staging ) );
    }

    {
        /*
         * (2) A chunk arrives for an entry which the package says has no chunks at all
         */

        om::ObjPtr< data::FilesystemMetadataRO > fsmdRO;
        std::vector< LateChunkFile > files;
        uuid_t emptyEntryId = uuids::nil();

        buildLateChunkPackage( fsmdRO, files, emptyEntryId, 1U /* noOfFiles */, chunkSize );

        const auto fsmdRemapped = om::qi< data::FilesystemMetadataRO >(
            RemappedChunkMetadata::createInstance(
                std::move( fsmdRO ),
                files.front().chunkId,
                emptyEntryId
                )
            );

        fs::path staging;

        om::ObjPtr< utils_t::unpackager_unit_t > unitRef;

        const utils_t::unpackager_feed_callback_t feedCallback =
            [ & ]( SAA_inout utils_t::unpackager_unit_t& unit ) -> void
            {
                unitRef = om::copy( &unit );

                staging = unit.targetTmpDir();

                UTF_REQUIRE( ! staging.empty() );

                waitForUnpackagerStaging( staging );

                const auto input = unit.bindInputConnector< utils_t::unpackager_unit_t >(
                    &utils_t::unpackager_unit_t::onChunkArrived,
                    &utils_t::unpackager_unit_t::onInputCompleted
                    );

                feedChunk( input, files.front().chunkId, files.front().block );

                input -> onCompleted();
            };

        const fs::TmpDir tmpDir;

        UTF_REQUIRE_THROW_MESSAGE(
            utils_t::runStandaloneUnpackager(
                fsmdRemapped,
                tmpDir.path() / "out"                       /* targetDir */,
                unpackager_unit_t::StpAllow,
                feedCallback
                ),
            UnexpectedException,
            "Chunk has arrived for an entry which is expected to have zero chunks"
            );

        UTF_REQUIRE( unitRef );
        UTF_REQUIRE( unitRef -> targetTmpDir().empty() );

        UTF_REQUIRE( ! staging.empty() );
        UTF_REQUIRE( ! fs::path_exists( staging ) );
    }
}
