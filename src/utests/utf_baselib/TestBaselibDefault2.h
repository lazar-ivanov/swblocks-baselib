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

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestFsUtils.h>

#include <baselib/core/FsUtils.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <ctime>

/************************************************************************
 * Self-tests for the comparators in utests/baselib/TestFsUtils.h
 *
 * These comparators are the content oracle of the blob transfer round trip and of the
 * base64 to file round trip, so a comparator which cannot report a mismatch silently
 * disables every assertion which depends on it
 */

UTF_AUTO_TEST_CASE( BaseLib_TestFsUtilsComparatorSelfTests )
{
    /*
     * The two trees compared below are generated independently, so the timestamps of their
     * files differ; normalize the timestamps, so the only difference the comparator can see
     * is the one the test has introduced deliberately
     *
     * Symlinks are skipped because setting the timestamp of a link follows the link, which
     * would throw for the dangling link the fixture creates
     */

    const auto normalizeFileTimestamps = []( SAA_in const bl::fs::path& root ) -> void
    {
        const std::time_t fixedTime = 1000000000;

        for( bl::fs::recursive_directory_iterator i( root ), end; i != end; ++i )
        {
            const auto& path = i -> path();

            if( ! bl::fs::is_symlink( path ) && bl::fs::is_regular_file( path ) )
            {
                bl::fs::last_write_time( path, fixedTime );
            }
        }
    };

    const auto createComparableTree = [ & ]( SAA_in const bl::fs::path& root ) -> void
    {
        utest::TestFsUtils dummyCreator;

        /*
         * The long file names part of the fixture is a 400 levels deep directory tree on
         * Windows; it adds nothing to a comparator test and it is expensive to create
         */

        dummyCreator.enableLongFileNames( false );
        dummyCreator.createDummyTestDir( root );

        normalizeFileTimestamps( root );
    };

    const auto writeOneByteFile = [](
        SAA_in          const bl::fs::path&             path,
        SAA_in          const unsigned char             value
        )
        -> void
    {
        const auto file = bl::os::fopen( path, "wb" );
        bl::os::fwrite( file, &value, sizeof( value ) );
    };

    /*
     * Two independently generated trees with normalized timestamps must compare equal, so
     * the checks below fail for the reason the test states and not because the comparator
     * was over-tightened
     */

    {
        bl::fs::TmpDir leftDir;
        bl::fs::TmpDir rightDir;

        createComparableTree( leftDir.path() );
        createComparableTree( rightDir.path() );

        UTF_REQUIRE( utest::TestFsUtils::compareFolders( leftDir.path(), rightDir.path() ) );
    }

    /*
     * A right hand side which does not exist at all, and one which exists but is empty,
     * must both be reported as a mismatch - a download which has produced nothing must
     * never compare equal to its own input
     */

    {
        bl::fs::TmpDir leftDir;
        bl::fs::TmpDir rightDir;

        createComparableTree( leftDir.path() );
        createComparableTree( rightDir.path() );

        const auto right = rightDir.path();

        bl::fs::safeRemoveAllIfExists( right );
        UTF_REQUIRE( ! bl::fs::path_exists( right ) );

        UTF_REQUIRE( ! utest::TestFsUtils::compareFolders( leftDir.path(), right ) );

        bl::fs::safeMkdirs( right );
        UTF_REQUIRE( bl::fs::is_directory( right ) );

        UTF_REQUIRE( ! utest::TestFsUtils::compareFolders( leftDir.path(), right ) );
    }

    /*
     * A single flipped byte in the middle of a file, with the size and the timestamp of the
     * file restored, must be reported as a mismatch
     */

    {
        bl::fs::TmpDir leftDir;
        bl::fs::TmpDir rightDir;

        createComparableTree( leftDir.path() );
        createComparableTree( rightDir.path() );

        const auto leftFile = leftDir.path() / "foo" / "bar" / "normalFile.bin";
        const auto rightFile = rightDir.path() / "foo" / "bar" / "normalFile.bin";

        UTF_REQUIRE( utest::TestFsUtils::compareFileContents( leftFile, rightFile ) );

        const std::uint64_t flipOffset = 8192U;

        const auto originalSize = bl::fs::file_size( rightFile );
        const auto originalTime = bl::fs::last_write_time( rightFile );

        {
            const auto file = bl::os::fopen( rightFile, "r+b" );

            unsigned char value = 0U;

            bl::os::fseek( file, flipOffset, SEEK_SET );
            bl::os::fread( file, &value, sizeof( value ) );

            value = ( unsigned char ) ( value ^ 0xFF );

            bl::os::fseek( file, flipOffset, SEEK_SET );
            bl::os::fwrite( file, &value, sizeof( value ) );
        }

        UTF_REQUIRE_EQUAL( originalSize, bl::fs::file_size( rightFile ) );

        bl::fs::last_write_time( rightFile, originalTime );

        UTF_REQUIRE(
            ! utest::TestFsUtils::compareFileContents(
                leftFile,
                rightFile,
                true /* ignoreTimestamp */,
                true /* ignoreName */
                )
            );

        UTF_REQUIRE( ! utest::TestFsUtils::compareFolders( leftDir.path(), rightDir.path() ) );
    }

    /*
     * A difference in the very first byte of a one byte file must be reported too - the
     * comparator used to skip the byte at offset zero entirely
     */

    {
        bl::fs::TmpDir tmpDir;

        const auto left = tmpDir.path() / "oneByteLeft.bin";
        const auto right = tmpDir.path() / "oneByteRight.bin";
        const auto same = tmpDir.path() / "oneByteSame.bin";

        writeOneByteFile( left, 'a' );
        writeOneByteFile( right, 'b' );
        writeOneByteFile( same, 'a' );

        UTF_REQUIRE_EQUAL( 1U, bl::fs::file_size( left ) );

        UTF_REQUIRE(
            utest::TestFsUtils::compareFileContents(
                left,
                same,
                true /* ignoreTimestamp */,
                true /* ignoreName */
                )
            );

        UTF_REQUIRE(
            ! utest::TestFsUtils::compareFileContents(
                left,
                right,
                true /* ignoreTimestamp */,
                true /* ignoreName */
                )
            );
    }

    /*
     * A directory on the left and a regular file of the same name on the right is a
     * mismatch; the comparator used to check the type of the left hand side only
     *
     * The leaf directory is replaced rather than 'foo/bar', because nothing else in the
     * fixture refers to a leaf directory - so the only difference between the two trees
     * really is the type of that one entry
     */

    {
        bl::fs::TmpDir leftDir;
        bl::fs::TmpDir rightDir;

        createComparableTree( leftDir.path() );
        createComparableTree( rightDir.path() );

        const auto rightLeafDir = rightDir.path() / "foo" / "bar" / "DirNameWithOne Space";

        UTF_REQUIRE( bl::fs::is_directory( rightLeafDir ) );

        bl::fs::safeRemoveAllIfExists( rightLeafDir );
        utest::TestFsUtils::createDummyFile( rightLeafDir, 1024U );

        UTF_REQUIRE( bl::fs::is_regular_file( rightLeafDir ) );

        UTF_REQUIRE( ! utest::TestFsUtils::compareFolders( leftDir.path(), rightDir.path() ) );
    }

    /*
     * The same mismatch at the top of the tree - here the right hand side 'foo/bar' is a
     * regular file, so every path below it disappears as well
     */

    {
        bl::fs::TmpDir leftDir;
        bl::fs::TmpDir rightDir;

        createComparableTree( leftDir.path() );
        createComparableTree( rightDir.path() );

        const auto rightBar = rightDir.path() / "foo" / "bar";

        bl::fs::safeRemoveAllIfExists( rightBar );
        utest::TestFsUtils::createDummyFile( rightBar, 1024U );

        UTF_REQUIRE( bl::fs::is_regular_file( rightBar ) );

        UTF_REQUIRE( ! utest::TestFsUtils::compareFolders( leftDir.path(), rightDir.path() ) );
    }
}
