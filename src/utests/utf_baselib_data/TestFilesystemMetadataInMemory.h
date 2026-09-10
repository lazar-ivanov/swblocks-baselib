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

#include <baselib/core/PreCompiled.h>
#include <baselib/data/PreCompiled.h>

#include <utests/baselib/PreCompiled.h>

/************************************************************************
 * FilesystemMetadataInMemoryImpl tests
 */

UTF_AUTO_TEST_CASE( TestFilesystemMetadataInMemoryImpl )
{
    using namespace bl;

    const auto uuidIteratorVerifyCount = [](
        SAA_in      const bl::om::ObjPtr< bl::UuidIterator >&       iter,
        SAA_in      const size_t                                    countExpected
        )
        -> void
    {
        size_t countEntries = 0;

        for( ; iter -> hasCurrent() ; iter -> loadNext() )
        {
            ++countEntries;
        }

        UTF_REQUIRE_EQUAL( countEntries, countExpected );

        iter -> reset();
        countEntries = 0;

        for( ; iter -> hasCurrent() ; iter -> loadNext() )
        {
            ++countEntries;
        }

        UTF_REQUIRE_EQUAL( countEntries, countExpected );
    };

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    const auto randomId = bl::uuids::create();

    {
        const auto fsmd = fsmd_t::createInstance< bl::data::FilesystemMetadataRO >();

        /*
         * The object is not finalized, so any method calls should throw
         */

        UTF_REQUIRE_THROW( fsmd -> queryAllEntries(), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> queryAllChunks(), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> queryChunks( randomId ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> loadEntryInfo( randomId ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> loadChunkInfo( randomId ), bl::UnexpectedException );

        const auto wo = bl::om::qi< bl::data::FilesystemMetadataWO >( fsmd );

        wo -> finalize();

        /*
         * Now the object is finalized RO interface should work
         */

        {
            const auto iter = fsmd -> queryAllEntries();
            UTF_REQUIRE( iter );
            UTF_REQUIRE( ! iter -> hasCurrent() );
        }

        {
            const auto iter = fsmd -> queryAllChunks();
            UTF_REQUIRE( iter );
            UTF_REQUIRE( ! iter -> hasCurrent() );
        }

        /*
         * These methods should throw since there is still no data in it
         */

        UTF_REQUIRE_THROW( fsmd -> queryChunks( randomId ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> loadEntryInfo( randomId ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> loadChunkInfo( randomId ), bl::UnexpectedException );

        /*
         * Since the object is now immutable any calls to the WO methods should throw
         */

        UTF_REQUIRE_THROW( wo -> createEntry( fsmd_t::EntryInfo() ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( wo -> createChunk( randomId, fsmd_t::ChunkInfo() ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( wo -> finalize(), bl::UnexpectedException );
    }

    {
        std::set< bl::uuid_t > s;

        const auto fsmd = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        const auto now = std::time( nullptr );
        BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

        const std::uint64_t pos64 = 5ULL * std::numeric_limits< std::uint32_t >::max();

        /*
         * EntryInfo::relPath is a refcounted handle to a mutable bo::path and createEntry( ... )
         * stores the handle as-is, so every entry must be given its own box - sharing one box
         * across the two entries would leave both of them pointing at whatever the box holds
         * last, and the read-back below would not be able to tell them apart
         */

        const auto makeEntry = [ & ]( SAA_in const std::string& relPath ) -> fsmd_t::EntryInfo
        {
            fsmd_t::EntryInfo info;

            info.size         = 4321U;
            info.type         = fsmd_t::File;
            info.lastModified = now;
            info.timeCreated  = now;
            info.relPath      = bl::bo::path::createInstance();

            bl::fs::path path( relPath );
            info.relPath -> lvalue().swap( path );

            return info;
        };

        fsmd_t::ChunkInfo chunk;
        chunk.pos = pos64;
        chunk.size = 1234;

        std::map< bl::uuid_t, std::string > expectedPaths;

        /*
         * The expectations are spelled through fs::path because makeEntry( ... ) stores the
         * relative path as one, and on Windows fs::path normalizes the separators when it is
         * constructed, so these round-trip as 'foo\bar1\baz' there and unchanged elsewhere.
         * Forward slashes are used on the way in deliberately - that is what a package
         * produced on a UNIX host carries
         */

        const auto entryId1 = fsmd -> createEntry( makeEntry( "foo/bar1/baz" ) );
        expectedPaths[ entryId1 ] = bl::fs::path( "foo/bar1/baz" ).string();

        const auto entryId2 = fsmd -> createEntry( makeEntry( "foo/bar/baz2" ) );
        expectedPaths[ entryId2 ] = bl::fs::path( "foo/bar/baz2" ).string();

        UTF_REQUIRE_EQUAL( expectedPaths.size(), 2U );

        UTF_REQUIRE( s.insert( entryId1 ).second );
        UTF_REQUIRE( s.insert( entryId2 ).second );

        const auto chunkId1 = fsmd -> createChunk( entryId1, bl::cpp::copy( chunk ) );
        const auto chunkId2 = fsmd -> createChunk( entryId1, bl::cpp::copy( chunk ) );
        const auto chunkId3 = fsmd -> createChunk( entryId2, bl::cpp::copy( chunk ) );
        const auto chunkId4 = fsmd -> createChunk( entryId2, bl::cpp::copy( chunk ) );

        UTF_REQUIRE( s.insert( chunkId1 ).second );
        UTF_REQUIRE( s.insert( chunkId2 ).second );
        UTF_REQUIRE( s.insert( chunkId3 ).second );
        UTF_REQUIRE( s.insert( chunkId4 ).second );

        fsmd -> finalize();

        {
            const auto stats = om::qi< fsmd_t >( fsmd ) -> computeStatistics();

            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "Filesystem metadata statistics (1):\n"
                    << "\nentriesCount: "
                    << stats.entriesCount
                    << "\nsymlinksCount: "
                    << stats.symlinksCount
                    << "\ndirectoriesCount: "
                    << stats.directoriesCount
                    << "\nfilesCount: "
                    << stats.filesCount
                    << "\ntotalSize: "
                    << stats.totalSize
                    << "\n\n"
                );

            UTF_REQUIRE_EQUAL( stats.entriesCount, 2U );
            UTF_REQUIRE_EQUAL( stats.symlinksCount, 0U );
            UTF_REQUIRE_EQUAL( stats.directoriesCount, 0U );
            UTF_REQUIRE_EQUAL( stats.filesCount, 2U );
            UTF_REQUIRE_EQUAL( stats.totalSize, 2U * 4321U );
        }

        /*
         * Since the object is now immutable any calls to the WO methods should throw
         */

        UTF_REQUIRE_THROW( fsmd -> createEntry( fsmd_t::EntryInfo() ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> createChunk( randomId, fsmd_t::ChunkInfo() ), bl::UnexpectedException );
        UTF_REQUIRE_THROW( fsmd -> finalize(), bl::UnexpectedException );

        /*
         * Now the object is finalized RO interface should work
         */

        const auto ro = bl::om::qi< bl::data::FilesystemMetadataRO >( fsmd );

        s.clear();

        uuidIteratorVerifyCount( ro -> queryAllEntries(), 2 );
        uuidIteratorVerifyCount( ro -> queryAllChunks(), 4 );

        const auto iter = ro -> queryAllEntries();

        size_t countEntries = 0;

        for( ; iter -> hasCurrent() ; iter -> loadNext() )
        {
            const auto entryId = iter -> current();
            UTF_REQUIRE( s.insert( entryId ).second );
            ++countEntries;

            const auto entryInfo = ro -> loadEntryInfo( entryId );

            UTF_REQUIRE( fsmd_t::File == entryInfo.type );
            UTF_REQUIRE( now == entryInfo.lastModified );
            UTF_REQUIRE( now == entryInfo.timeCreated );

            /*
             * The store must return the path the entry was created with and not merely one of
             * the paths which were handed to it - a disjunction over both would be satisfied by
             * a createEntry( ... ) which stored a constant or swapped the two entries around
             */

            const auto posExpected = expectedPaths.find( entryId );

            UTF_REQUIRE( posExpected != expectedPaths.end() );
            UTF_REQUIRE_EQUAL( entryInfo.relPath -> value().string(), posExpected -> second );

            expectedPaths.erase( posExpected );

            const auto iterChunks = ro -> queryChunks( entryId );

            size_t countChunks = 0;

            for( ; iterChunks -> hasCurrent() ; iterChunks -> loadNext() )
            {
                const auto chunkId = iterChunks -> current();
                UTF_REQUIRE( s.insert( chunkId ).second );
                ++countChunks;

                const auto chunkInfo = ro -> loadChunkInfo( chunkId );

                UTF_REQUIRE( pos64 == chunkInfo.pos );
                UTF_REQUIRE( 1234 == chunkInfo.size );
            }

            UTF_REQUIRE_EQUAL( 2U, countChunks );
        }

        UTF_REQUIRE_EQUAL( 2U, countEntries );

        UTF_REQUIRE( expectedPaths.empty() );
    }
}

UTF_AUTO_TEST_CASE( TestFilesystemMetadataEntryPathValidation )
{
    using namespace bl;

    /*
     * The metadata can be deserialized from an artifact produced by a remote peer, and every
     * consumer joins the relative path of an entry onto the directory it unpacks into - so a
     * path which is absolute or which contains a parent directory reference must be rejected
     * when the entry is created
     */

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    const auto createEntryWithPath = []( SAA_in const std::string& relPath ) -> void
    {
        const auto fsmd = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        fsmd_t::EntryInfo entry;

        entry.size = 4321U;
        entry.type = fsmd_t::File;
        entry.relPath = bl::bo::path::createInstance();

        bl::fs::path path( relPath );
        entry.relPath -> lvalue().swap( path );

        ( void ) fsmd -> createEntry( std::move( entry ) );
    };

    /*
     * An ordinary relative path is accepted, including one with a name which is legal on
     * UNIX but not on Windows
     */

    UTF_REQUIRE_NO_THROW( createEntryWithPath( "foo/bar/baz.txt" ) );
    UTF_REQUIRE_NO_THROW( createEntryWithPath( "foo/DirNameWith\"quotes\" /baz.txt" ) );

    UTF_REQUIRE_THROW( createEntryWithPath( "" ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( createEntryWithPath( "../../.ssh/authorized_keys" ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( createEntryWithPath( "foo/../../etc/passwd" ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( createEntryWithPath( "/etc/passwd" ), bl::UnexpectedException );

    {
        /*
         * A symlink entry must carry a target
         */

        const auto fsmd = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        fsmd_t::EntryInfo entry;

        entry.type = fsmd_t::Symlink;
        entry.relPath = bl::bo::path::createInstance();

        bl::fs::path path( "foo/link" );
        entry.relPath -> lvalue().swap( path );

        UTF_REQUIRE_THROW( fsmd -> createEntry( std::move( entry ) ), bl::UnexpectedException );
    }

    {
        /*
         * chkEntryInfo() opens with
         *
         *     const auto& relPath = info.relPath ? info.relPath -> value() : fs::path();
         *
         * and the ternary is the only thing standing between a NULL relPath handle - which a
         * remote peer's metadata can perfectly well carry - and a null dereference on the very
         * next line. The branch is unreachable from createEntryWithPath() above, which always
         * allocates a bo::path box, and unreachable on a finalized store, where chkUnlocked()
         * throws before chkEntryInfo() is ever called; it needs a FRESH, unfinalized store and
         * an entry whose relPath was left default constructed
         */

        const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        {
            fsmd_t::EntryInfo info;

            info.type = fsmd_t::File;

            UTF_REQUIRE( ! info.relPath );

            UTF_REQUIRE_THROW_MESSAGE(
                wo -> createEntry( std::move( info ) ),
                bl::UnexpectedException,
                "The relative path of a filesystem metadata entry is empty"
                );
        }

        {
            /*
             * The other arm of the ternary - a non null box which holds an empty path - must be
             * rejected with the very same message, so the two arms cannot be collapsed silently
             */

            fsmd_t::EntryInfo info;

            info.type = fsmd_t::File;
            info.relPath = bl::bo::path::createInstance();

            UTF_REQUIRE_THROW_MESSAGE(
                wo -> createEntry( std::move( info ) ),
                bl::UnexpectedException,
                "The relative path of a filesystem metadata entry is empty"
                );
        }

        /*
         * The rejections must not have left the store in a state which refuses well formed
         * entries afterwards
         */

        {
            fsmd_t::EntryInfo info;

            info.size    = 4321U;
            info.type    = fsmd_t::File;
            info.relPath = bl::bo::path::createInstance();

            bl::fs::path path( "ok/path" );
            info.relPath -> lvalue().swap( path );

            UTF_REQUIRE_NO_THROW( ( void ) wo -> createEntry( std::move( info ) ) );
        }

        wo -> finalize();

        UTF_REQUIRE_EQUAL(
            om::qi< bl::data::FilesystemMetadataRO >( wo ) -> queryEntriesCount(),
            1U
            );
    }
}

UTF_AUTO_TEST_CASE( TestFilesystemMetadataIteratorOutlivesStoreHandle )
{
    using namespace bl;

    /*
     * UuidIteratorImplT captures &data.front() and &data.front() + data.size() as raw pointers
     * and holds them for its whole life. What keeps those pointers valid is the
     * om::ObjPtr< om::Object > back-reference which all three factories pass -
     * om::qi< om::Object >( static_cast< FilesystemMetadataRO* >( this ) ) - documented in the
     * owner-contract comment on queryAllEntries()
     *
     * Dropping that second argument from all three createInstance() calls compiles cleanly and
     * passes the rest of the suite, because every other test holds the store handle for at
     * least as long as the iterators it obtained from it. The two production holders -
     * FilesUnpackagerUnit::m_entriesIterator and ChunksReceiverDeleterBase::m_chunksIterator -
     * each sit next to their own m_fsmd, so a regression here is a use-after-free inside a
     * NOEXCEPT task handler
     */

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    om::ObjPtr< bl::UuidIterator > entriesIter;
    om::ObjPtr< bl::UuidIterator > chunksIter;
    om::ObjPtr< bl::UuidIterator > perEntryIter;
    om::ObjPtr< bl::UuidIterator > emptyIter;

    std::set< bl::uuid_t > expectedEntries;
    std::set< bl::uuid_t > expectedChunks;

    {
        const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        const auto makeEntry = []( SAA_in const std::string& relPath ) -> fsmd_t::EntryInfo
        {
            fsmd_t::EntryInfo info;

            info.size    = 4321U;
            info.type    = fsmd_t::File;
            info.relPath = bl::bo::path::createInstance();

            bl::fs::path path( relPath );
            info.relPath -> lvalue().swap( path );

            return info;
        };

        const auto entryId1 = wo -> createEntry( makeEntry( "a/one.txt" ) );
        const auto entryId2 = wo -> createEntry( makeEntry( "a/two.txt" ) );

        UTF_REQUIRE( expectedEntries.insert( entryId1 ).second );
        UTF_REQUIRE( expectedEntries.insert( entryId2 ).second );

        fsmd_t::ChunkInfo chunk;
        chunk.pos  = 0U;
        chunk.size = 1234U;

        UTF_REQUIRE( expectedChunks.insert( wo -> createChunk( entryId1, bl::cpp::copy( chunk ) ) ).second );
        UTF_REQUIRE( expectedChunks.insert( wo -> createChunk( entryId1, bl::cpp::copy( chunk ) ) ).second );
        UTF_REQUIRE( expectedChunks.insert( wo -> createChunk( entryId2, bl::cpp::copy( chunk ) ) ).second );
        UTF_REQUIRE( expectedChunks.insert( wo -> createChunk( entryId2, bl::cpp::copy( chunk ) ) ).second );

        wo -> finalize();

        const auto ro = bl::om::qi< bl::data::FilesystemMetadataRO >( wo );

        entriesIter  = ro -> queryAllEntries();
        chunksIter   = ro -> queryAllChunks();
        perEntryIter = ro -> queryChunks( entryId1 );

        /*
         * A finalized but EMPTY store yields an iterator whose begin / end pair is
         * ( nullptr, nullptr ) - obtained here so that pair is walked without an owner too
         */

        const auto empty = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();
        empty -> finalize();

        emptyIter = bl::om::qi< bl::data::FilesystemMetadataRO >( empty ) -> queryAllEntries();
    }

    /*
     * Both store handles are gone at this point - only the iterators keep the vectors alive
     */

    const auto collect = []( SAA_in const bl::om::ObjPtr< bl::UuidIterator >& iter )
        -> std::set< bl::uuid_t >
    {
        std::set< bl::uuid_t > result;

        for( ; iter -> hasCurrent(); iter -> loadNext() )
        {
            UTF_REQUIRE( result.insert( iter -> current() ).second );
        }

        return result;
    };

    UTF_REQUIRE( expectedEntries == collect( entriesIter ) );

    /*
     * reset() returns m_pos to the captured m_begin, so a second walk re-reads the very same
     * memory - which is only safe because the store is still alive
     */

    entriesIter -> reset();

    UTF_REQUIRE( expectedEntries == collect( entriesIter ) );

    UTF_REQUIRE( expectedChunks == collect( chunksIter ) );

    {
        const auto perEntry = collect( perEntryIter );

        UTF_REQUIRE_EQUAL( perEntry.size(), 2U );

        for( const auto& chunkId : perEntry )
        {
            UTF_REQUIRE( bl::cpp::contains( expectedChunks, chunkId ) );
        }
    }

    /*
     * A cheap check that the END pointer is still the right one - loadNext() BL_CHKs
     * m_pos != m_end once the iterator is exhausted
     */

    UTF_REQUIRE( ! entriesIter -> hasCurrent() );
    UTF_REQUIRE_THROW( entriesIter -> loadNext(), bl::UnexpectedException );

    UTF_REQUIRE( ! emptyIter -> hasCurrent() );
}

UTF_AUTO_TEST_CASE( TestFilesystemMetadataRejectedMutationsLeaveStoreConsistent )
{
    using namespace bl;

    /*
     * createEntry( ... ) and createChunk( ... ) register the new id in the store before they
     * validate anything and roll back with scope guards, so a rejected mutation must leave all
     * five containers of the store consistent - a guard which is dismissed early or dropped
     * would leave a phantom id which queryAllEntries() / queryAllChunks() still yield but which
     * loadEntryInfo() / loadChunkInfo() can no longer resolve, and the failure would then land
     * in a NOEXCEPT task handler far away from the cause
     */

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    /*
     * Note that a fresh bo::path box per call is mandatory here - createEntry( ... ) moves from
     * its argument and stores the refcounted handle as-is
     */

    const auto makeEntry = [](
        SAA_in          const fsmd_t::EntryType                     type,
        SAA_in          const std::string&                          relPath,
        SAA_in_opt      const std::string&                          targetPath
        )
        -> fsmd_t::EntryInfo
    {
        fsmd_t::EntryInfo info;

        info.size    = 4321U;
        info.type    = type;
        info.relPath = bl::bo::path::createInstance();

        bl::fs::path path( relPath );
        info.relPath -> lvalue().swap( path );

        if( ! targetPath.empty() )
        {
            info.targetPath = bl::bo::path::createInstance();

            bl::fs::path target( targetPath );
            info.targetPath -> lvalue().swap( target );
        }

        return info;
    };

    const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

    const auto goodId = wo -> createEntry( makeEntry( fsmd_t::File, "a/b.txt" , bl::str::empty() ) );

    /*
     * A duplicate relative path, a path which escapes the tree and a symlink without a target
     * are all rejected - and none of them may consume a slot in the store
     */

    UTF_REQUIRE_THROW( wo -> createEntry( makeEntry( fsmd_t::File, "a/b.txt" , bl::str::empty() ) ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( wo -> createEntry( makeEntry( fsmd_t::File, "../escape" , bl::str::empty() ) ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( wo -> createEntry( makeEntry( fsmd_t::Symlink, "a/link" , bl::str::empty() ) ), bl::UnexpectedException );

    std::map< bl::uuid_t /* chunkId */, bl::uuid_t /* entryId */ > chunk2entry;

    const auto chunkId = wo -> createChunk( goodId, fsmd_t::ChunkInfo() );
    chunk2entry[ chunkId ] = goodId;

    /*
     * createChunk( ... ) pushes the new chunk id before it resolves the entry, so an unknown
     * entry id must be rolled back by the guard rather than left behind in m_chunkIds
     */

    UTF_REQUIRE_THROW( wo -> createChunk( bl::uuids::create(), fsmd_t::ChunkInfo() ), bl::UnexpectedException );

    /*
     * The symlink above was rejected before m_relPaths was touched, so its path must still be
     * available to a later entry
     */

    const auto goodId2 = wo -> createEntry( makeEntry( fsmd_t::File, "a/link" , bl::str::empty() ) );

    const auto goodId3 = wo -> createEntry( makeEntry( fsmd_t::File, "a/c.txt" , bl::str::empty() ) );

    const auto chunkId2 = wo -> createChunk( goodId3, fsmd_t::ChunkInfo() );
    chunk2entry[ chunkId2 ] = goodId3;

    const auto chunkId3 = wo -> createChunk( goodId3, fsmd_t::ChunkInfo() );
    chunk2entry[ chunkId3 ] = goodId3;

    wo -> finalize();

    const auto ro = bl::om::qi< bl::data::FilesystemMetadataRO >( wo );

    /*
     * m_fileIds has no phantoms, and m_files agrees with it - a one-sided rollback shows up as a
     * mismatch between these two counts
     */

    UTF_REQUIRE_EQUAL( ro -> queryEntriesCount(), 3U );
    UTF_REQUIRE_EQUAL( om::qi< fsmd_t >( wo ) -> computeStatistics().entriesCount, 3U );

    {
        std::set< bl::uuid_t > entryIds;

        const auto iter = ro -> queryAllEntries();

        for( ; iter -> hasCurrent() ; iter -> loadNext() )
        {
            const auto entryId = iter -> current();

            UTF_REQUIRE( entryIds.insert( entryId ).second );
            UTF_REQUIRE_NO_THROW( ro -> loadEntryInfo( entryId ) );
        }

        std::set< bl::uuid_t > expectedEntryIds;
        expectedEntryIds.insert( goodId );
        expectedEntryIds.insert( goodId2 );
        expectedEntryIds.insert( goodId3 );

        UTF_REQUIRE( entryIds == expectedEntryIds );
    }

    {
        std::set< bl::uuid_t > chunkIds;

        const auto iter = ro -> queryAllChunks();

        for( ; iter -> hasCurrent() ; iter -> loadNext() )
        {
            const auto id = iter -> current();

            UTF_REQUIRE( chunkIds.insert( id ).second );
            UTF_REQUIRE_NO_THROW( ro -> loadChunkInfo( id ) );

            /*
             * The chunk to entry join is what the unpackaging path uses to decide which file a
             * chunk is written into
             */

            const auto pos = chunk2entry.find( id );

            UTF_REQUIRE( pos != chunk2entry.end() );
            UTF_REQUIRE_EQUAL( ro -> queryEntryId( id ), pos -> second );
        }

        UTF_REQUIRE_EQUAL( chunkIds.size(), 3U );
    }

    {
        /*
         * The per-entry view must agree with the global one
         */

        const auto verifyEntryChunks = [ & ]( SAA_in const bl::uuid_t& entryId ) -> void
        {
            std::set< bl::uuid_t > expectedChunkIds;

            for( const auto& pair : chunk2entry )
            {
                if( pair.second == entryId )
                {
                    expectedChunkIds.insert( pair.first );
                }
            }

            std::set< bl::uuid_t > chunkIds;

            const auto iter = ro -> queryChunks( entryId );

            for( ; iter -> hasCurrent() ; iter -> loadNext() )
            {
                UTF_REQUIRE( chunkIds.insert( iter -> current() ).second );
            }

            UTF_REQUIRE( chunkIds == expectedChunkIds );
            UTF_REQUIRE_EQUAL( ro -> queryChunksCount( entryId ), expectedChunkIds.size() );
        };

        verifyEntryChunks( goodId );
        verifyEntryChunks( goodId2 );
        verifyEntryChunks( goodId3 );

        UTF_REQUIRE_EQUAL( ro -> queryChunksCount( goodId ), 1U );
        UTF_REQUIRE_EQUAL( ro -> queryChunksCount( goodId2 ), 0U );
        UTF_REQUIRE_EQUAL( ro -> queryChunksCount( goodId3 ), 2U );
    }

    UTF_REQUIRE_THROW( ro -> queryEntryId( bl::uuids::create() ), bl::UnexpectedException );

    /*
     * Entry ids and chunk ids live in different maps and must not be interchangeable - a
     * container which is keyed or valued the wrong way round would answer these
     */

    UTF_REQUIRE_THROW( ro -> queryEntryId( goodId ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( ro -> queryChunksCount( chunkId ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( ro -> loadChunkInfo( goodId ), bl::UnexpectedException );

    {
        /*
         * The uniqueness rule must be distinguishable from the containment rules, which all
         * throw the same exception type; a separate store is needed because wo is finalized now
         */

        const auto wo2 = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        ( void ) wo2 -> createEntry( makeEntry( fsmd_t::File, "a/b.txt" , bl::str::empty() ) );

        UTF_REQUIRE_THROW_MESSAGE(
            wo2 -> createEntry( makeEntry( fsmd_t::File, "a/b.txt" , bl::str::empty() ) ),
            bl::UnexpectedException,
            "relPath must be unique"
            );
    }
}

UTF_AUTO_TEST_CASE( TestFilesystemMetadataChecksumAndHashAssociation )
{
    using namespace bl;

    /*
     * isChecksumSet is a silent on / off switch for integrity checking - the unpackager only
     * accumulates and compares the chunk CRCs when it is set - so associateChecksum( ... )
     * forgetting to raise it would skip verification for every file without any other signal
     */

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    const auto makeEntry = []( SAA_in const std::string& relPath ) -> fsmd_t::EntryInfo
    {
        fsmd_t::EntryInfo info;

        info.size    = 4321U;
        info.type    = fsmd_t::File;
        info.relPath = bl::bo::path::createInstance();

        bl::fs::path path( relPath );
        info.relPath -> lvalue().swap( path );

        return info;
    };

    const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

    const auto withChecksum = wo -> createEntry( makeEntry( "a/with.txt" ) );
    const auto withoutChecksum = wo -> createEntry( makeEntry( "a/without.txt" ) );

    UTF_REQUIRE( ! wo -> isFinalized() );

    wo -> associateChecksum( withChecksum, 0xDEADBEEFU );

    wo -> associateHash( withChecksum, "sha256-abc" );

    /*
     * A second call must replace the hash, not append to it
     */

    wo -> associateHash( withChecksum, "sha256-def" );

    UTF_REQUIRE_THROW( wo -> associateChecksum( bl::uuids::create(), 1U ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( wo -> associateHash( bl::uuids::create(), "x" ), bl::UnexpectedException );

    wo -> finalize();

    UTF_REQUIRE( wo -> isFinalized() );

    /*
     * A finalized store is immutable, so both mutators must refuse to run behind an outstanding
     * RO iterator
     */

    UTF_REQUIRE_THROW( wo -> associateChecksum( withChecksum, 2U ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( wo -> associateHash( withChecksum, "sha256-ghi" ), bl::UnexpectedException );

    const auto ro = bl::om::qi< bl::data::FilesystemMetadataRO >( wo );

    {
        const auto a = ro -> loadEntryInfo( withChecksum );

        UTF_REQUIRE( a.isChecksumSet );
        UTF_REQUIRE_EQUAL( a.checksum.value(), 0xDEADBEEFU );
        UTF_REQUIRE( a.hash );
        UTF_REQUIRE_EQUAL( a.hash -> value(), "sha256-def" );
    }

    {
        /*
         * The entry which was never touched must be unaffected by either call, which is what
         * catches getEntry( ... ) resolving to the wrong slot
         */

        const auto b = ro -> loadEntryInfo( withoutChecksum );

        UTF_REQUIRE( ! b.isChecksumSet );
        UTF_REQUIRE_EQUAL( b.checksum.value(), 0U );
        UTF_REQUIRE( ! b.hash );
    }
}

UTF_AUTO_TEST_CASE( TestFilesystemMetadataComputeStatistics )
{
    using namespace bl;

    /*
     * computeStatistics() has only ever been called on a store which holds File entries, so the
     * Symlink and Directory arms of its switch, its default: throw and its chkLocked() guard
     * have never executed - swapping or merging two of the arms would pass the whole suite
     */

    typedef bl::data::FilesystemMetadataInMemoryImpl fsmd_t;

    const auto makeEntry = [](
        SAA_in          const fsmd_t::EntryType                     type,
        SAA_in          const std::string&                          relPath,
        SAA_in          const std::uint64_t                         size,
        SAA_in_opt      const std::string&                          targetPath
        )
        -> fsmd_t::EntryInfo
    {
        fsmd_t::EntryInfo info;

        info.size    = size;
        info.type    = type;
        info.relPath = bl::bo::path::createInstance();

        bl::fs::path path( relPath );
        info.relPath -> lvalue().swap( path );

        if( ! targetPath.empty() )
        {
            info.targetPath = bl::bo::path::createInstance();

            bl::fs::path target( targetPath );
            info.targetPath -> lvalue().swap( target );
        }

        return info;
    };

    {
        const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        /*
         * Statistics may not be read off a store which is still being populated
         */

        UTF_REQUIRE_THROW( om::qi< fsmd_t >( wo ) -> computeStatistics(), bl::UnexpectedException );

        ( void ) wo -> createEntry( makeEntry( fsmd_t::File, "a/f1.txt", 100U , bl::str::empty() ) );
        ( void ) wo -> createEntry( makeEntry( fsmd_t::File, "a/f2.txt", 200U , bl::str::empty() ) );
        ( void ) wo -> createEntry( makeEntry( fsmd_t::File, "a/f3.txt", 300U , bl::str::empty() ) );

        ( void ) wo -> createEntry( makeEntry( fsmd_t::Directory, "a/d1", 0U , bl::str::empty() ) );
        ( void ) wo -> createEntry( makeEntry( fsmd_t::Directory, "a/d2", 0U , bl::str::empty() ) );

        ( void ) wo -> createEntry( makeEntry( fsmd_t::Symlink, "a/link", 7U, "a/f1.txt" ) );

        wo -> finalize();

        const auto ro = bl::om::qi< bl::data::FilesystemMetadataRO >( wo );

        const auto stats = om::qi< fsmd_t >( wo ) -> computeStatistics();

        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "Filesystem metadata statistics (mixed entry types):\n"
                << "\nentriesCount: "
                << stats.entriesCount
                << "\nsymlinksCount: "
                << stats.symlinksCount
                << "\ndirectoriesCount: "
                << stats.directoriesCount
                << "\nfilesCount: "
                << stats.filesCount
                << "\ntotalSize: "
                << stats.totalSize
                << "\n\n"
            );

        UTF_REQUIRE_EQUAL( stats.entriesCount, 6U );
        UTF_REQUIRE_EQUAL( stats.filesCount, 3U );
        UTF_REQUIRE_EQUAL( stats.directoriesCount, 2U );
        UTF_REQUIRE_EQUAL( stats.symlinksCount, 1U );

        /*
         * Every entry type contributes to totalSize, including the symlink and the directories
         */

        UTF_REQUIRE_EQUAL( stats.totalSize, 607ULL );

        UTF_REQUIRE_EQUAL( stats.entriesCount, ( std::uint32_t ) ro -> queryEntriesCount() );
    }

    {
        /*
         * chkEntryInfo() does not validate the entry type, so an out of range one is accepted by
         * createEntry( ... ) and only surfaces in the default: arm of the switch; if type
         * validation is ever added there this block moves to the createEntry( ... ) call instead
         */

        const auto wo = fsmd_t::createInstance< bl::data::FilesystemMetadataWO >();

        fsmd_t::EntryInfo info;

        info.type    = static_cast< fsmd_t::EntryType >( 42 );
        info.relPath = bl::bo::path::createInstance();

        bl::fs::path path( "a/bogus.txt" );
        info.relPath -> lvalue().swap( path );

        ( void ) wo -> createEntry( std::move( info ) );

        wo -> finalize();

        UTF_REQUIRE_THROW( om::qi< fsmd_t >( wo ) -> computeStatistics(), bl::UnexpectedException );
    }
}
