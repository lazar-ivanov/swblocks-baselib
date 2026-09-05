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

