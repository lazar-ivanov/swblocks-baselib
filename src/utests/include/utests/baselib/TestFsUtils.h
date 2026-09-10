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

#ifndef __UTESTS_TESTFSUTILS_H_
#define __UTESTS_TESTFSUTILS_H_

#include <baselib/core/OS.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/BaseIncludes.h>
#include <baselib/core/PathUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/TlsState.h>
#include <baselib/core/CPP.h>

#include <utests/baselib/TestUtils.h>

#include <algorithm>
#include <random>
#include <cstddef>

namespace utest
{
    /**
     * class TestFsUtilsT
     */

    template
    <
        typename E = void
    >
    class TestFsUtilsT
    {
    private:

        typedef std::vector< bl::fs::path >                                     list_t;
        typedef std::vector< std::pair< bl::fs::path, std::uint64_t > >         fileList_t;
        typedef std::vector< std::pair< bl::fs::path, bl::fs::path > >          symlinkList_t;

        bool                m_supportLargeFiles;
        bool                m_supportLongFileNames;

        list_t              m_fixedDirTree;
        fileList_t          m_fixedFilePaths;
        symlinkList_t       m_fixedDirSymlinkPaths;
        symlinkList_t       m_fixedFileSymlinkPaths;

    public:

        TestFsUtilsT()
            :
            m_supportLargeFiles( false ),
            m_supportLongFileNames( true )
        {
        }

        void createDummyTestDir( SAA_in const bl::fs::path& root )
        {
            initFixedDirStructure();
            createFixedDir( root );
        }

        void enableLargeFileSupport( SAA_in const bool enable)
        {
            m_supportLargeFiles = enable;
        }

        void enableLongFileNames( SAA_in const bool enable )
        {
            m_supportLongFileNames = enable;
        }

        static bool compareFolders(
             SAA_in              const bl::fs::path&     left,
             SAA_in              const bl::fs::path&     right
             )
        {
            const auto printFolders = [](
                SAA_in              const bl::fs::path&     left,
                SAA_in              const bl::fs::path&     right,
                SAA_in              const bool              isSimilar
                )
            {
                const auto getFileList = []( SAA_in const bl::fs::path& path )
                    -> std::string
                {
                    bl::cpp::SafeOutputStringStream oss;
                    for( const auto& elem: path )
                    {
                        oss
                            << "["
                            << elem.string()
                            << "]\n";
                    }
                    return oss.str();
                };

                if ( ! isSimilar )
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "\nFolders are different\n"
                            << "Left folder : ["
                            << left
                            << "]\nRight folder: ["
                            << right
                            << "]\nLeft folder files:\n"
                            << getFileList( left )
                            << "Right folder files:\n"
                            << getFileList( right )
                        );
                }
            };

            if( ! exists( left ) || ! exists( right ) || ! is_directory( left ) || ! is_directory( right ) )
            {
                /*
                 * A missing path, or a path which is not a directory, is a mismatch and it
                 * must never be reported as an agreement - otherwise a transfer which has
                 * produced nothing at all would compare equal to its own input
                 */

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "TestFsUtils: Comparison failed because one of the paths does not exist "
                        << "or is not a directory "
                        << left
                        << " "
                        << right
                    );

                return false;
            }

            bool isSimilar = true;

            list_t leftVec, rightVec;

            copy( bl::fs::directory_iterator( left ), bl::fs::directory_iterator(), back_inserter( leftVec ) );
            copy( bl::fs::directory_iterator( right ), bl::fs::directory_iterator(), back_inserter( rightVec ) );

            isSimilar = isSimilar && ( leftVec.size() == rightVec.size() );

            if( ! isSimilar)
            {
                printFolders( left, right, isSimilar );
                return isSimilar;
            }

            sort( leftVec.begin(), leftVec.end() );
            sort( rightVec.begin(), rightVec.end() );

            auto leftIter = leftVec.begin();
            auto rightIter = rightVec.begin();

            for( ; leftIter != leftVec.end() && rightIter != rightVec.end(); ++leftIter, ++rightIter )
            {
                /*
                 * The type of the right hand side entry must be checked too, otherwise
                 * a directory on the left and a file on the right compare as similar
                 */

                if( is_directory( *leftIter ) )
                {
                    isSimilar = isSimilar &&
                        is_directory( *rightIter ) &&
                        ( leftIter->filename() == rightIter->filename() ) &&
                        compareFolders( *leftIter, *rightIter );
                }
                else if( is_regular_file( *leftIter ) )
                {
                    isSimilar = isSimilar &&
                        is_regular_file( *rightIter ) &&
                        compareFileContents( *leftIter, *rightIter );
                }
                else if( is_symlink( *leftIter ) )
                {
                    isSimilar = isSimilar &&
                        is_symlink( *rightIter ) &&
                        ( read_symlink( *leftIter ).filename() == read_symlink( *rightIter ).filename() );
                }
                else
                {
                    /*
                     * An entry of some other type which the comparator can't verify; it is
                     * reported as a mismatch rather than silently accepted
                     */

                    isSimilar = false;
                }

                if( ! isSimilar )
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "TestFsUtils: Comparison failed "
                            << *leftIter
                            << " "
                            << *rightIter
                        );

                    break;
                }
            }

            printFolders( left, right, isSimilar );
            return isSimilar;
        }

        static bool compareFileContents(
             SAA_in              const bl::fs::path&     leftPath,
             SAA_in              const bl::fs::path&     rightPath,
             SAA_in_opt          const bool              ignoreTimestamp = false,
             SAA_in_opt          const bool              ignoreName = false
             )
        {
            if( ! exists( leftPath ) || ! exists( rightPath ) ||
                ! is_regular_file( leftPath ) || ! is_regular_file( rightPath ) )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "TestFsUtils: File Comparison failed because one of the paths does not exist "
                        << "or is not a regular file "
                        << leftPath
                        << " "
                        << rightPath
                    );

                return false;
            }

            const auto fileSize = file_size( leftPath );

            const bool isMetadataSimilar =
                ( ignoreName || ( leftPath.filename() == rightPath.filename() ) ) &&
                ( fileSize == file_size( rightPath ) ) &&
                ( ignoreTimestamp || ( last_write_time( leftPath ) == last_write_time( rightPath ) ) );

            if( ! isMetadataSimilar )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "TestFsUtils: File Metadata Comparison failed "
                        << leftPath
                        << " "
                        << rightPath
                    );

                return false;
            }

            /*
             * The metadata matches; now compare the entire content of the two files by
             * streaming both of them in fixed size chunks until the end of the file
             *
             * Every byte is compared, including the one at offset zero, and the first
             * difference short-circuits the comparison
             */

            const std::size_t bufferSize = 64U * 1024U;

            const auto leftContents = bl::cpp::SafeUniquePtr< char[] >::attach( new char[ bufferSize ] );
            const auto rightContents = bl::cpp::SafeUniquePtr< char[] >::attach( new char[ bufferSize ] );

            bl::fs::SafeInputFileStreamWrapper leftFileWrapper( leftPath );
            auto& leftFile = leftFileWrapper.stream();

            bl::fs::SafeInputFileStreamWrapper rightFileWrapper( rightPath );
            auto& rightFile = rightFileWrapper.stream();

            std::uint64_t offset = 0U;

            while( offset < fileSize )
            {
                const auto readSize =
                    ( std::size_t ) std::min< std::uint64_t >( bufferSize, fileSize - offset );

                leftFile.read( leftContents.get(), readSize );
                rightFile.read( rightContents.get(), readSize );

                if(
                    leftFile.gcount() != ( std::streamsize ) readSize ||
                    rightFile.gcount() != ( std::streamsize ) readSize
                    )
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "TestFsUtils: File Content Comparison failed because one of the files "
                            << "could not be read at offset "
                            << offset
                            << " "
                            << leftPath
                            << " "
                            << rightPath
                        );

                    return false;
                }

                std::size_t counter = 0U;

                while( counter < readSize && leftContents.get()[ counter ] == rightContents.get()[ counter ] )
                {
                    ++counter;
                }

                if( counter != readSize )
                {
                    BL_LOG(
                        bl::Logging::debug(),
                        BL_MSG()
                            << "TestFsUtils: File Content Comparison failed at offset "
                            << ( offset + counter )
                            << " "
                            << leftPath
                            << " "
                            << rightPath
                        );

                    return false;
                }

                offset += readSize;
            }

            return true;
        }

        static void createDummyFile(
             SAA_in              const bl::fs::path&        path,
             SAA_in              const std::uint64_t        fileSize
             )
        {
            /*
             * The file is filled with a position dependent pattern, so the content is a
             * real oracle for a transfer: the value of a byte is derived from its absolute
             * offset in the file, which makes a block written at the wrong offset, in the
             * wrong order or twice detectable
             *
             * The byte at offset zero is deliberately not zero, and the period of the
             * pattern (251) does not divide the block size used by the transfer code, so
             * swapping two whole blocks does not go unnoticed either
             */

            const std::size_t bufferSize = 64U * 1024U;

            const auto buffer = bl::cpp::SafeUniquePtr< char[] >::attach( new char[ bufferSize ] );

            const auto outfile = bl::os::fopen( path, "wb" );

            std::uint64_t offset = 0U;

            while( offset < fileSize )
            {
                const auto writeSize =
                    ( std::size_t ) std::min< std::uint64_t >( bufferSize, fileSize - offset );

                for( std::size_t k = 0U; k < writeSize; ++k )
                {
                    buffer.get()[ k ] = static_cast< char >( ( ( offset + k ) * 31U + 7U ) % 251U );
                }

                bl::os::fwrite( outfile, buffer.get(), writeSize );

                offset += writeSize;
            }
        }

    private:

        void initFixedDirStructure()
        {
            m_fixedDirTree.clear();
            m_fixedFilePaths.clear();
            m_fixedDirSymlinkPaths.clear();
            m_fixedFileSymlinkPaths.clear();
            bl::fs::path basePath( "foo" );
            bl::fs::path barPath( basePath / "bar" );
            bl::fs::path normalFilePath( barPath / "normalFile.bin" );

            m_fixedDirTree.push_back( basePath );
            m_fixedDirTree.push_back( barPath );
            m_fixedDirTree.push_back( basePath / "emptyDirectory" );
            m_fixedDirTree.push_back( barPath / " DirName with Spaces" );
            m_fixedDirTree.push_back( barPath / "DirNameWithOne Space" );
            m_fixedDirTree.push_back( barPath / "DirNameWith'!£$%^&_-@;,.(WinRestrictedSpecialCharacters)" );
            m_fixedDirTree.push_back( barPath / L"DirNameWithUnicode\u263A" );

            if( bl::os::onUNIX() )
            {
                m_fixedDirTree.push_back( barPath / "DirNameWith\"quotes\" " );
                m_fixedDirTree.push_back( barPath / "DirNameWith'!£$%^&*_-@;:<>,.?|(SpecialCharacters)" );
            }

            /*
             * TODO: UTF8 dir names
             */

            m_fixedFilePaths.push_back( std::make_pair( basePath / "zeroSizeFile.bin", 0U ) );
            m_fixedFilePaths.push_back( std::make_pair( normalFilePath, 20U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / "oneChunkFile.bin", 510U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / "chunkSizedFile.bin", 512U * 1024U ) );

            /*
             * The default capacity of bl::data::DataBlock is 1MB, so exactlyOneBlockFile.bin
             * pins the 'bytes left == capacity' boundary case while multiChunkFile.bin is
             * transferred as three blocks - two full ones and a partial one, because its
             * size is deliberately not a multiple of the block size
             */

            m_fixedFilePaths.push_back( std::make_pair( barPath / "exactlyOneBlockFile.bin", 2U * 512U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / "multiChunkFile.bin", 2U * 1024U * 1024U + 12345U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / " FileName with Spaces.bin", 20U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / "FileNameWithOne Space.bin", 20U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / "FileNameWith'!£$%^&_-@;,.(WinRestrictedSpecialCharacters).bin", 20U * 1024U ) );
            m_fixedFilePaths.push_back( std::make_pair( barPath / L"FileNameWithUnicode\u263A.bin", 20U * 1024U ) );

            if( bl::os::onUNIX() )
            {
                m_fixedFilePaths.push_back( std::make_pair( barPath / "FileNameWith\"quotes\".bin", 20U * 1024U ) );
                m_fixedFilePaths.push_back( std::make_pair( barPath / "FileNameWith'!£$%^&*_-@;:<>,.?|(SpecialCharacters).bin", 20U * 1024U ) );
            }

            if( m_supportLargeFiles )
            {
                std::uint64_t fileSize = 0;
                fileSize += 1024U * 1024U;
                fileSize *= 1024U;
                fileSize *= 5U;
                m_fixedFilePaths.push_back( std::make_pair( barPath / "LargeFile.bin", fileSize ) );
            }

            if( bl::os::onUNIX() )
            {
                m_fixedDirSymlinkPaths.push_back( std::make_pair( barPath, basePath / "linkToBar" ) );
                m_fixedDirSymlinkPaths.push_back( std::make_pair( basePath / "emptyDirectory", basePath / "linkToEmpty" ) );
                m_fixedFileSymlinkPaths.push_back( std::make_pair( normalFilePath, basePath / "linkToNormal.bin" ) );

                /*
                 * A dangling link - its target is never created
                 *
                 * Obtaining the timestamps of a link follows it, so packaging a tree which
                 * contains a broken link must not attempt to stat the target of the link
                 */

                m_fixedFileSymlinkPaths.push_back(
                    std::make_pair( basePath / "noSuchTarget.bin", basePath / "danglingLink.bin" )
                    );
            }
        }

        void createFixedDir( SAA_in const bl::fs::path& path ) const
        {
            for( auto dirIter = m_fixedDirTree.begin(); dirIter != m_fixedDirTree.end(); ++dirIter )
            {
                bl::fs::safeCreateDirectory( path / * dirIter);
            }

            for( auto fileIter = m_fixedFilePaths.begin(); fileIter != m_fixedFilePaths.end(); ++fileIter )
            {
                createDummyFile( path / fileIter -> first, fileIter -> second );
            }

            if( bl::os::onWindows() && m_supportLongFileNames )
            {
                bl::fs::path longPath( path );

                for( std::size_t i = 0; i < 400; ++i )
                {
                    longPath /= "LongName";
                    bl::fs::safeCreateDirectory( longPath );
                }

                longPath /= "LongFileName.bin";
                createDummyFile( longPath, 20U * 1024U );
            }

            if( bl::os::onUNIX() )
            {
                for( auto dirSymlinkIter = m_fixedDirSymlinkPaths.begin(); dirSymlinkIter != m_fixedDirSymlinkPaths.end(); ++dirSymlinkIter )
                {
                    bl::fs::create_directory_symlink( path / dirSymlinkIter -> first, path / dirSymlinkIter -> second );
                }

                for( auto fileSymlinkIter = m_fixedFileSymlinkPaths.begin(); fileSymlinkIter != m_fixedFileSymlinkPaths.end(); ++fileSymlinkIter )
                {
                    bl::fs::create_symlink( path / fileSymlinkIter -> first, path / fileSymlinkIter -> second );
                }
            }
        }
    };

    typedef TestFsUtilsT<> TestFsUtils;

} // utest

#endif /* __UTESTS_TESTFSUTILS_H_ */
