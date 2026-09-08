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

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/UtfDirectoryFixture.h>

#include <apps/bl-tool/commands/ProcessFilesUtils.h>

namespace
{
    /**
     * @brief Writes the exact bytes of 'content' into 'path'
     *
     * Note that bl::encoding::writeTextFile would prepend a UTF-8 preamble, so the file
     * is written directly here to keep the on-disk bytes exactly as specified
     */

    void writeAllBytes(
        SAA_in          const bl::fs::path&                             path,
        SAA_in          const std::string&                              content
        )
    {
        const auto file = bl::os::fopen( path, "wb" );

        if( ! content.empty() )
        {
            bl::os::fwrite( file, content.c_str(), content.size() );
        }
    }

    /**
     * @brief Reads the exact bytes of 'path' - the oracle for the byte level assertions
     */

    std::string readAllBytes( SAA_in const bl::fs::path& path )
    {
        const auto size = static_cast< std::size_t >( bl::fs::file_size( path ) );

        std::string content;

        if( size )
        {
            content.resize( size );

            const auto file = bl::os::fopen( path, "rb" );

            bl::os::fread( file, &content[ 0 ], size );
        }

        return content;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BlTool_ProcessFilesUnterminatedCommentTests )
{
    utest::TestDirectory dir;

    {
        /*
         * A file which ends while still inside an open block comment
         *
         * The comment is never closed, so it is not an empty comment and it must be
         * flushed back into the file verbatim - in particular the trailing flush must
         * not read one past the end of the lines vector and append a stray line
         */

        const auto path = dir.testFile( "unterminated-empty.h" );

        const std::string original = "int a = 1;\n/*\n*\n";

        writeAllBytes( path, original );

        std::uint64_t count = 0U;

        UTF_REQUIRE_NO_THROW(
            bltool::commands::ProcessFilesUtils::fileRemoveEmptyComments( path, count )
            );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), original );
        UTF_REQUIRE_EQUAL( count, 0U );
    }

    {
        /*
         * The same for the marker based comment removal - the marked comment is never
         * closed, so it is not a marked comment which can be removed and the file must
         * be preserved unchanged and not counted
         */

        const auto path = dir.testFile( "unterminated-marker.h" );

        const std::string original = "int a = 1;\n/*\n * GENERATED\n";

        writeAllBytes( path, original );

        std::uint64_t count = 0U;

        UTF_REQUIRE_NO_THROW(
            bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "generated" }, count )
            );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), original );
        UTF_REQUIRE_EQUAL( count, 0U );
    }
}

UTF_AUTO_TEST_CASE( BlTool_ProcessFilesUpdateHeaderCommentTests )
{
    utest::TestDirectory dir;

    const std::string header = "/*\n * NEW HEADER\n */\n\n";

    {
        /*
         * The normal case - the existing header comment is replaced
         */

        const auto path = dir.testFile( "replace.h" );

        writeAllBytes( path, "/*\n * OLD\n */\n\nint a = 1;\n" );

        bltool::commands::ProcessFilesUtils::fileUpdateFileHeaderComment( path, header );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), header + "int a = 1;\n" );
        UTF_REQUIRE( bl::fs::file_size( path ) > 0U );
    }

    {
        /*
         * A file with no header comment at all - the header is inserted, which must
         * converge on exactly the same content as the replace case above
         */

        const auto path = dir.testFile( "insert.h" );

        writeAllBytes( path, "int a = 1;\n" );

        bltool::commands::ProcessFilesUtils::fileUpdateFileHeaderComment( path, header );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), header + "int a = 1;\n" );
        UTF_REQUIRE( bl::fs::file_size( path ) > 0U );
    }

    {
        /*
         * A file which contains nothing but a header comment
         *
         * There is no file body, but the replacement header must still be written -
         * otherwise the file is simply truncated to zero bytes
         */

        const auto path = dir.testFile( "header-only.h" );

        writeAllBytes( path, "/*\n * OLD\n */\n" );

        bltool::commands::ProcessFilesUtils::fileUpdateFileHeaderComment( path, header );

        const auto content = readAllBytes( path );

        UTF_REQUIRE( ! content.empty() );
        UTF_REQUIRE_EQUAL( content, header );
        UTF_REQUIRE( bl::fs::file_size( path ) > 0U );
    }

    {
        /*
         * A file whose header comment is never closed
         *
         * The end of the header can't be determined, so the file is left untouched -
         * what must never happen is the entire content being consumed and discarded
         */

        const auto path = dir.testFile( "unterminated.h" );

        const std::string original = "/*\n * OLD\n\nint a = 1;\n";

        writeAllBytes( path, original );

        bltool::commands::ProcessFilesUtils::fileUpdateFileHeaderComment( path, header );

        const auto content = readAllBytes( path );

        UTF_REQUIRE( ! content.empty() );
        UTF_REQUIRE_EQUAL( content, original );
        UTF_REQUIRE( bl::fs::file_size( path ) > 0U );
    }
}
