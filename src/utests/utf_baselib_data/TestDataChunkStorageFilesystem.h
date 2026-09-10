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

#include <baselib/messaging/DataChunkStorageFilesystem.h>

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestTaskUtils.h>

/************************************************************************
 * DataChunkStorageFilesystem tests
 */

namespace utest
{
    template
    <
        typename E = void
    >
    class TestDataChunkStorageFilesystemLocalHelperT
    {
        BL_DECLARE_STATIC( TestDataChunkStorageFilesystemLocalHelperT )

    public:

        template
        <
            typename STORAGEIMPL
        >
        static void executeTests( SAA_in std::string&& testsName )
        {
            using namespace bl;
            using namespace bl::data;
            using namespace utest;

            const std::string testData( "This is test data" );

            const auto dataBlock = DataBlock::createInstance( 128 );

            std::memcpy( dataBlock -> begin(), testData.c_str(), testData.size() );

            dataBlock -> setSize( testData.size() );
            dataBlock -> setOffset1( 0U );

            const auto sessionId = uuids::create();

            const auto testNotFoundException = [](
                SAA_in          const cpp::void_callback_t&                 callback,
                SAA_in          const bl::uuid_t&                           chunkId,
                SAA_in          const bool                                  dumpException
                ) -> void
            {
                const auto exceptionPredicate = [ & ]( SAA_in const ServerErrorException& exception ) -> bool
                {
                    if( dumpException )
                    {
                        BL_LOG_MULTILINE(
                            Logging::debug(),
                            BL_MSG()
                                << eh::diagnostic_information( exception )
                            );
                    }

                    const std::string message = exception.what();

                    const auto expectedMessage = resolveMessage(
                        BL_MSG()
                            << "Chunk with id "
                            << chunkId
                            << " does not exist"
                        );

                    if( message != expectedMessage )
                    {
                        return false;
                    }

                    const auto* exceptionChunkId =
                        eh::get_error_info< eh::errinfo_error_uuid >( exception );

                    if( ! exceptionChunkId || chunkId != *exceptionChunkId )
                    {
                        return false;
                    }

                    const auto* errorCode =
                        eh::get_error_info< eh::errinfo_error_code >( exception );

                    if(
                        ! errorCode ||
                        eh::errc::make_error_code( eh::errc::no_such_file_or_directory ) != *errorCode
                        )
                    {
                        return false;
                    }

                    return true;
                };

                UTF_REQUIRE_EXCEPTION(
                    callback(),
                    ServerErrorException,
                    exceptionPredicate
                    );
            };

            const auto runBasicStorageTests = [ & ]( SAA_in const om::ObjPtr< DataChunkStorage >& storage ) -> void
            {
                storage -> flushPeerSessions( uuids::create() /* peerId */ );

                /*
                 * Test exceptions with non-existing chunks
                 */

                const auto testChunkNotFound = [ & ](
                    SAA_in          const bl::uuid_t&                           chunkId,
                    SAA_in          const bool                                  dumpException
                    ) -> void
                {
                    testNotFoundException(
                        [ & ]() -> void
                        {
                            const auto newBlock = DataBlock::createInstance( 128 );

                            storage -> load( sessionId, chunkId, newBlock );
                        },
                        chunkId,
                        dumpException
                        );

                    testNotFoundException(
                        [ & ]() -> void
                        {
                            storage -> remove( sessionId, chunkId );
                        },
                        chunkId,
                        dumpException
                        );
                };

                testChunkNotFound( uuids::create() /* chunkId */, true /* dumpException */ );

                /*
                 * Test save, load and removal of a single chunk... and verify that it
                 * doesn't exist after removal
                 */

                {
                    const auto chunkId = uuids::create();

                    storage -> save( sessionId, chunkId, dataBlock );

                    const auto newBlock = DataBlock::createInstance( 128 );

                    storage -> load( sessionId, chunkId, newBlock );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, newBlock ) );

                    storage -> remove( sessionId, chunkId );

                    testChunkNotFound( uuids::create() /* chunkId */, false /* dumpException */ );

                    /*
                     * Now save and load again the same chunk and verify that it works
                     */

                    storage -> save( sessionId, chunkId, dataBlock );

                    newBlock -> setSize( 0U );

                    storage -> load( sessionId, chunkId, newBlock );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, newBlock ) );

                    storage -> remove( sessionId, chunkId );

                    testChunkNotFound( uuids::create() /* chunkId */, false /* dumpException */ );
                }

                /*
                 * Test save, load and removal of an empty chunk... and verify that it
                 * doesn't exist after removal
                 */

                {
                    const auto chunkId = uuids::create();

                    const auto emptyBlock = DataBlock::createInstance( 128 );
                    emptyBlock -> setSize( 0U );

                    storage -> save( sessionId, chunkId, emptyBlock );

                    const auto newBlock = DataBlock::createInstance( 128 );
                    newBlock -> setSize( newBlock -> capacity() );

                    storage -> load( sessionId, chunkId, newBlock );

                    UTF_REQUIRE( 0U == newBlock -> size() );

                    storage -> remove( sessionId, chunkId );

                    testChunkNotFound( uuids::create() /* chunkId */, false /* dumpException */ );
                }

                /*
                 * Test saving over a chunk which is still active (i.e. without removing it
                 * first) - the data which was saved last must supersede the previous one
                 * both in size and in content and in both directions
                 */

                {
                    const auto chunkId = uuids::create();

                    const std::string testData2( "short" );

                    const auto shortBlock = DataBlock::createInstance( 128 );

                    std::memcpy( shortBlock -> begin(), testData2.c_str(), testData2.size() );

                    shortBlock -> setSize( testData2.size() );
                    shortBlock -> setOffset1( 0U );

                    storage -> save( sessionId, chunkId, dataBlock );

                    /*
                     * Note that the chunk is deliberately not removed before it is saved again
                     */

                    storage -> save( sessionId, chunkId, shortBlock );

                    const auto readBack = DataBlock::createInstance( 128 );
                    readBack -> setSize( readBack -> capacity() );

                    storage -> load( sessionId, chunkId, readBack );

                    UTF_REQUIRE_EQUAL( readBack -> size(), testData2.size() );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( shortBlock, readBack ) );

                    /*
                     * Now overwrite in the other direction - a longer chunk over a shorter one
                     */

                    storage -> save( sessionId, chunkId, dataBlock );

                    const auto readBack2 = DataBlock::createInstance( 128 );
                    readBack2 -> setSize( readBack2 -> capacity() );

                    storage -> load( sessionId, chunkId, readBack2 );

                    UTF_REQUIRE_EQUAL( readBack2 -> size(), testData.size() );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, readBack2 ) );

                    storage -> remove( sessionId, chunkId );

                    testChunkNotFound( chunkId, false /* dumpException */ );
                }

                /*
                 * Test loading a chunk into a data block which is too small to hold it - the
                 * capacity check must refuse the load before the block is resized and before
                 * anything is read into it, since without it the read would run past the end
                 * of the block's buffer and straight into the heap
                 */

                {
                    const auto chunkId = uuids::create();

                    const auto bigBlock = DataBlock::createInstance( 256 );

                    std::memset( bigBlock -> begin(), 'x', 200U );

                    bigBlock -> setSize( 200U );
                    bigBlock -> setOffset1( 0U );

                    storage -> save( sessionId, chunkId, bigBlock );

                    const auto smallBlock = DataBlock::createInstance( 128 );
                    smallBlock -> setSize( 0U );

                    UTF_REQUIRE_THROW_MESSAGE(
                        storage -> load( sessionId, chunkId, smallBlock ),
                        bl::UnexpectedException,
                        "Data block capacity is too small"
                        );

                    /*
                     * The block must be left untouched, which is what proves the check ran
                     * before the size of the block was updated
                     */

                    UTF_REQUIRE_EQUAL( 0U, smallBlock -> size() );

                    /*
                     * The positive control - a block which is large enough loads the chunk
                     */

                    const auto ok = DataBlock::createInstance( 256 );

                    storage -> load( sessionId, chunkId, ok );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( bigBlock, ok ) );

                    storage -> remove( sessionId, chunkId );

                    testChunkNotFound( chunkId, false /* dumpException */ );
                }
            };

            const std::size_t noOfChunks = 100;

            const auto populateStorage = [ & ](
                SAA_in              const om::ObjPtr< DataChunkStorage >&                       storage,
                SAA_inout           std::vector< bl::uuid_t >&                                  chunks,
                SAA_inout           std::unordered_map< bl::uuid_t, bool /* deleted */ >&       chunksDeletedState
                ) -> void
            {
                for( std::size_t i = 0U; i < noOfChunks; ++i )
                {
                    const auto chunkId = uuids::create();

                    storage -> save( sessionId, chunkId, dataBlock );

                    chunks.push_back( chunkId );
                    chunksDeletedState.emplace( chunkId, false /* deleted */ );
                }
            };

            const auto runSimpleLoadAndSaveTests = [ & ](
                SAA_in              const om::ObjPtr< DataChunkStorage >&                       storage,
                SAA_inout           std::vector< bl::uuid_t >&                                  chunks
                ) -> void
            {
                /*
                 * Load all chunks sequentially and then load them at random
                 */

                for( const auto& chunkId : chunks )
                {
                    const auto newBlock = DataBlock::createInstance( 128 );

                    storage -> load( sessionId, chunkId, newBlock );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, newBlock ) );
                }

                for( std::size_t i = 0U; i < noOfChunks; ++i )
                {
                    const auto pos = random::getUniformRandomUnsignedValue< std::size_t >( chunks.size() - 1 );

                    const auto& chunkId = chunks[ pos ];

                    const auto newBlock = DataBlock::createInstance( 128 );

                    storage -> load( sessionId, chunkId, newBlock );

                    UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, newBlock ) );
                }
            };

            const auto runRandomDeleteAndRecreateTests = [ & ](
                SAA_in              const om::ObjPtr< DataChunkStorage >&                       storage,
                SAA_inout           std::vector< bl::uuid_t >&                                  chunks,
                SAA_inout           std::unordered_map< bl::uuid_t, bool /* deleted */ >&       chunksDeletedState
                ) -> void
            {
                /*
                 * Delete some chunks and then verify that the deleted chunks are indeed deleted
                 */

                for( std::size_t i = 0U; i < noOfChunks; ++i )
                {
                    const auto pos = random::getUniformRandomUnsignedValue< std::size_t >( chunks.size() - 1 );

                    const auto& chunkId = chunks[ pos ];

                    if( ! chunksDeletedState[ chunkId ] )
                    {
                        storage -> remove( sessionId, chunkId );
                    }

                    testNotFoundException(
                        [ & ]() -> void
                        {
                            storage -> remove( sessionId, chunkId );
                        },
                        chunkId,
                        false /* dumpException */
                        );

                    /*
                     * Based on a coin toss either leave the chunk deleted or restore it
                     */

                    const bool coinToss =
                        0U == ( random::getUniformRandomUnsignedValue( 1024U /* maxValue */ ) % 2U );

                    if( coinToss )
                    {
                        storage -> save( sessionId, chunkId, dataBlock );
                    }

                    chunksDeletedState[ chunkId ] = ! coinToss;
                }

                /*
                 * Now simply verify all chunks that are not deleted and delete them
                 */

                std::size_t noOfDeleted = 0U;

                for( const auto& chunkId : chunks )
                {
                    if( ! chunksDeletedState[ chunkId ] )
                    {
                        const auto newBlock = DataBlock::createInstance( 128 );

                        storage -> load( sessionId, chunkId, newBlock );

                        UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( dataBlock, newBlock ) );

                        storage -> remove( sessionId, chunkId );

                        chunksDeletedState[ chunkId ] = true;
                    }
                    else
                    {
                        ++noOfDeleted;
                    }

                    testNotFoundException(
                        [ & ]() -> void
                        {
                            storage -> remove( sessionId, chunkId );
                        },
                        chunkId,
                        false /* dumpException */
                        );
                }

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "Number of deleted chunks is "
                        << noOfDeleted
                        << " and the number of not deleted is "
                        << chunks.size() - noOfDeleted
                    );
            };

            const auto runAllStorageTests = [ & ]( SAA_in const om::ObjPtr< DataChunkStorage >& storage ) -> void
            {
                runBasicStorageTests( storage );

                /*
                 * Test save, load and removal of many chunks...
                 */

                std::vector< bl::uuid_t > chunks;
                std::unordered_map< bl::uuid_t, bool /* deleted */ > chunksDeletedState;

                populateStorage( storage, chunks, chunksDeletedState );
                runSimpleLoadAndSaveTests( storage, chunks );
                runRandomDeleteAndRecreateTests( storage, chunks, chunksDeletedState );
            };

            {
                utils::ExecutionTimer timer( BL_PARAM_FWD( testsName ) );

                {
                    const auto storage = om::lockDisposable(
                        STORAGEIMPL::template createInstance< DataChunkStorage >()
                        );

                    runAllStorageTests( storage );
                }

                {
                    /*
                     * Now run the separate phases of all tests on different storage objects,
                     * but the same real file system storage
                     */

                    fs::TmpDir tempDir;
                    std::vector< bl::uuid_t > chunks;
                    std::unordered_map< bl::uuid_t, bool /* deleted */ > chunksDeletedState;

                    {
                        const auto storage = om::lockDisposable(
                            STORAGEIMPL::template createInstance< DataChunkStorage >(
                                cpp::copy( tempDir.path() ) /* rootPath */
                                )
                            );

                        populateStorage( storage, chunks, chunksDeletedState );
                    }

                    {
                        const auto storage = om::lockDisposable(
                            STORAGEIMPL::template createInstance< DataChunkStorage >(
                                cpp::copy( tempDir.path() ) /* rootPath */
                                )
                            );

                        runSimpleLoadAndSaveTests( storage, chunks );
                    }

                    {
                        const auto storage = om::lockDisposable(
                            STORAGEIMPL::template createInstance< DataChunkStorage >(
                                cpp::copy( tempDir.path() ) /* rootPath */
                                )
                            );

                        runRandomDeleteAndRecreateTests( storage, chunks, chunksDeletedState );
                    }
                }

                {
                    /*
                     * Test that every public method of the storage refuses to run after the
                     * object has been disposed - the single file storage has already released
                     * its FILE* by then and the multi files one its temporary directory, so
                     * without the check the calls below would dereference a null FILE* or
                     * operate on a directory which no longer exists
                     *
                     * Note that the storage must be owned here rather than borrowed from
                     * om::lockDisposable( ... ), which would dispose it a second time at the
                     * end of the block
                     *
                     * Note also that the single file storage takes its lock before it checks
                     * the disposed state, so a regression which disposes under that lock would
                     * show up as a hang in the probes below rather than as a failure
                     */

                    const auto storage = STORAGEIMPL::template createInstance< DataChunkStorage >();

                    const auto chunkId = uuids::create();

                    storage -> save( sessionId, chunkId, dataBlock );

                    const auto probe = DataBlock::createInstance( 128 );

                    storage -> dispose();

                    UTF_REQUIRE_THROW_MESSAGE(
                        storage -> load( sessionId, chunkId, probe ),
                        bl::UnexpectedException,
                        "after it has been disposed"
                        );

                    UTF_REQUIRE_THROW_MESSAGE(
                        storage -> save( sessionId, chunkId, dataBlock ),
                        bl::UnexpectedException,
                        "after it has been disposed"
                        );

                    UTF_REQUIRE_THROW_MESSAGE(
                        storage -> remove( sessionId, chunkId ),
                        bl::UnexpectedException,
                        "after it has been disposed"
                        );

                    UTF_REQUIRE_THROW_MESSAGE(
                        storage -> flushPeerSessions( uuids::create() /* peerId */ ),
                        bl::UnexpectedException,
                        "after it has been disposed"
                        );

                    /*
                     * Disposing an already disposed object is documented to be a no-op
                     */

                    UTF_REQUIRE_NO_THROW( storage -> dispose() );
                }
            }
        }
    };

    typedef TestDataChunkStorageFilesystemLocalHelperT<> TestDataChunkStorageFilesystemLocalHelper;

} // utest

UTF_AUTO_TEST_CASE( TestDataChunkStorageFilesystemMultiFiles )
{
    using namespace bl::data;
    using namespace utest;

    TestDataChunkStorageFilesystemLocalHelper::executeTests< DataChunkStorageFilesystemMultiFiles >(
        "DataChunkStorageFilesystemMultiFiles tests"
        );
}

UTF_AUTO_TEST_CASE( TestDataChunkStorageFilesystemSingleFile )
{
    using namespace bl::data;
    using namespace utest;

    TestDataChunkStorageFilesystemLocalHelper::executeTests< DataChunkStorageFilesystemSingleFile >(
        "DataChunkStorageFilesystemSingleFile tests"
        );
}

UTF_AUTO_TEST_CASE( TestDataChunkStorageSingleFileDeleteAndReopen )
{
    using namespace bl;
    using namespace bl::data;

    /*
     * A chunk which is removed must stay removed after the storage is disposed and opened
     * again - with the file opened in append mode the delete marker which removeChunk( ... )
     * rewrites in place was appended instead, so the deletion was lost and the stray header
     * made the storage impossible to open at all
     */

    fs::TmpDir tempDir;

    const std::string testData( "This is test data" );

    const auto dataBlock = DataBlock::createInstance( 128 );

    std::memcpy( dataBlock -> begin(), testData.c_str(), testData.size() );

    dataBlock -> setSize( testData.size() );
    dataBlock -> setOffset1( 0U );

    const auto sessionId = uuids::create();

    const auto chunkIdKept = uuids::create();
    const auto chunkIdRemoved = uuids::create();

    {
        const auto storage = om::lockDisposable(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                )
            );

        storage -> save( sessionId, chunkIdKept, dataBlock );
        storage -> save( sessionId, chunkIdRemoved, dataBlock );

        storage -> remove( sessionId, chunkIdRemoved );
    }

    {
        /*
         * Opening the storage again must succeed and the removed chunk must be gone
         */

        const auto storage = om::lockDisposable(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                )
            );

        const auto loaded = DataBlock::createInstance( 128 );

        storage -> load( sessionId, chunkIdKept, loaded );

        UTF_REQUIRE_EQUAL( loaded -> size(), testData.size() );

        UTF_REQUIRE_THROW(
            storage -> load( sessionId, chunkIdRemoved, loaded ),
            bl::ServerErrorException
            );
    }
}

UTF_AUTO_TEST_CASE( TestDataChunkStorageSingleFileOverwriteAndReopen )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    /*
     * A chunk which is saved over while it is still active must supersede the previous
     * record after the storage is disposed and opened again - save( ... ) marks the old
     * record deleted in place before it appends the new one, so loadChunksData( ... ) must
     * not resurrect the older (i.e. the first) record for the same chunk id
     */

    fs::TmpDir tempDir;

    const std::string testData( "This is test data" );

    const auto dataBlock = DataBlock::createInstance( 128 );

    std::memcpy( dataBlock -> begin(), testData.c_str(), testData.size() );

    dataBlock -> setSize( testData.size() );
    dataBlock -> setOffset1( 0U );

    const std::string testData2( "short" );

    const auto shortBlock = DataBlock::createInstance( 128 );

    std::memcpy( shortBlock -> begin(), testData2.c_str(), testData2.size() );

    shortBlock -> setSize( testData2.size() );
    shortBlock -> setOffset1( 0U );

    const auto sessionId = uuids::create();

    const auto chunkIdKept = uuids::create();

    {
        const auto storage = om::lockDisposable(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                )
            );

        storage -> save( sessionId, chunkIdKept, dataBlock );
        storage -> save( sessionId, chunkIdKept, shortBlock );
    }

    {
        /*
         * Opening the storage again must succeed and the chunk must carry the data
         * which was saved last
         */

        om::ObjPtrDisposable< DataChunkStorage > storage;

        UTF_REQUIRE_NO_THROW(
            storage = om::lockDisposable(
                DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                    cpp::copy( tempDir.path() ) /* rootPath */
                    )
                )
            );

        const auto loaded = DataBlock::createInstance( 128 );
        loaded -> setSize( loaded -> capacity() );

        storage -> load( sessionId, chunkIdKept, loaded );

        UTF_REQUIRE_EQUAL( loaded -> size(), testData2.size() );

        UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( shortBlock, loaded ) );
    }
}

UTF_AUTO_TEST_CASE( TestDataChunkStorageSingleFileCorruptFileTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace utest;

    /*
     * The single file storage replays the whole of chunks/data.bin when it is opened and
     * enforces three format invariants per record - that a full header still fits in the file,
     * that the position recorded in the header is the position the header was read from, and
     * that the payload size does not run past the end of the file
     *
     * The last one is written as a subtraction precisely so it cannot wrap: a crafted or
     * corrupted chunk size which wraps the position back to zero would otherwise make the
     * replay loop run forever, i.e. the server would hang at startup on a hostile store rather
     * than refuse to open it
     */

    const std::string testData( "This is test data" );

    const auto sessionId = uuids::create();

    const auto makeDataBlock = [ & ]() -> om::ObjPtr< DataBlock >
    {
        auto block = DataBlock::createInstance( 128 );

        std::memcpy( block -> begin(), testData.c_str(), testData.size() );

        block -> setSize( testData.size() );
        block -> setOffset1( 0U );

        return block;
    };

    const auto saveOneChunk = [ & ](
        SAA_in          const fs::path&                             rootPath,
        SAA_in          const bl::uuid_t&                           chunkId
        )
        -> void
    {
        const auto storage = om::lockDisposable(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( rootPath ) /* rootPath */
                )
            );

        storage -> save( sessionId, chunkId, makeDataBlock() );
    };

    const auto appendZeros = [](
        SAA_in          const fs::path&                             filePath,
        SAA_in          const std::size_t                           count
        )
        -> void
    {
        const std::vector< char > zeros( count, '\0' );

        const auto filePtr = os::fopen( filePath, "ab" );

        os::fwrite( filePtr, zeros.data(), zeros.size() );
    };

    {
        /*
         * The positive control - an undamaged file reopens and the chunk loads back byte for
         * byte, so a case which always threw for the wrong reason could not pass silently
         */

        fs::TmpDir tempDir;

        const auto chunkId = uuids::create();

        saveOneChunk( tempDir.path(), chunkId );

        const auto storage = om::lockDisposable(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                )
            );

        const auto loaded = DataBlock::createInstance( 128 );
        loaded -> setSize( loaded -> capacity() );

        storage -> load( sessionId, chunkId, loaded );

        UTF_REQUIRE_EQUAL( loaded -> size(), testData.size() );

        UTF_REQUIRE( BackendImplTestImpl::areBlocksEqual( makeDataBlock(), loaded ) );
    }

    {
        /*
         * A truncated payload - the recorded chunk size no longer fits in what is left of the
         * file
         */

        fs::TmpDir tempDir;

        saveOneChunk( tempDir.path(), uuids::create() /* chunkId */ );

        const auto filePath = tempDir.path() / "chunks" / "data.bin";

        fs::resize_file( filePath, fs::file_size( filePath ) - 1U );

        UTF_REQUIRE_THROW_MESSAGE(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                ),
            bl::UnexpectedException,
            "file format of"
            );
    }

    {
        /*
         * A trailing partial header - eight bytes is less than a chunk header, so the next
         * iteration of the replay loop cannot read a full one
         */

        fs::TmpDir tempDir;

        saveOneChunk( tempDir.path(), uuids::create() /* chunkId */ );

        const auto filePath = tempDir.path() / "chunks" / "data.bin";

        appendZeros( filePath, 8U );

        UTF_REQUIRE_THROW_MESSAGE(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                ),
            bl::UnexpectedException,
            "file format of"
            );
    }

    {
        /*
         * A stray full header at the wrong position - the replay loop reads a header whose
         * recorded position is zero while it is being read from the end of the previous record
         */

        fs::TmpDir tempDir;

        saveOneChunk( tempDir.path(), uuids::create() /* chunkId */ );

        const auto filePath = tempDir.path() / "chunks" / "data.bin";

        appendZeros( filePath, 64U );

        UTF_REQUIRE_THROW_MESSAGE(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                ),
            bl::UnexpectedException,
            "file format of"
            );
    }

    {
        /*
         * A chunk size which would wrap the replay position back to zero
         *
         * Note that this block knowingly depends on the on-disk layout of the chunk header,
         * which is the raw structure as the host compiler lays it out and in host byte order
         * (unlike the wire protocol, which has explicit conversions): the 16 byte chunk uuid at
         * offset 0, the position at offset 16 and the size at offset 24
         *
         * Note also that a regression which rewrote the size invariant into a form which can
         * wrap would manifest here as a hang and not as a failure, which is why the reopen is
         * timed - the log then records that it returned promptly
         */

        fs::TmpDir tempDir;

        saveOneChunk( tempDir.path(), uuids::create() /* chunkId */ );

        const auto filePath = tempDir.path() / "chunks" / "data.bin";

        {
            const std::vector< char > ones( 8U, '\xFF' );

            const auto filePtr = os::fopen( filePath, "rb+" );

            os::fseek( filePtr, 24U /* offset of the size field */, SEEK_SET );

            os::fwrite( filePtr, ones.data(), ones.size() );
        }

        utils::ExecutionTimer timer( "Reopening a single file storage with a wrapping chunk size" );

        UTF_REQUIRE_THROW_MESSAGE(
            DataChunkStorageFilesystemSingleFile::createInstance< DataChunkStorage >(
                cpp::copy( tempDir.path() ) /* rootPath */
                ),
            bl::UnexpectedException,
            "file format of"
            );
    }
}
