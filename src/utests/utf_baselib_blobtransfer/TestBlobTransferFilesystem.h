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

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobServer )
{
    utest::TestBlobTransferFilesystemUtilsImpl::startBlobServer();
}

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobClient )
{
    utest::TestBlobTransferFilesystemUtilsImpl::startBlobClient();
}

/************************************************************************
 * Tests for the ProxyDataChunkStorageImpl
 */

UTF_AUTO_TEST_CASE( BlobTransfer_ProxyDataChunkBackendImplTests )
{
    utest::TestBlobTransferFilesystemUtilsImpl::proxyDataChunkBackendImplTests();
}

UTF_AUTO_TEST_CASE( BlobTransfer_StartBlobServerProxy )
{
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

