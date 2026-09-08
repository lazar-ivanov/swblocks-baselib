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

UTF_AUTO_TEST_CASE( BlTool_ProcessAllFilesFilterTests )
{
    utest::TestDirectory dir;

    /*
     * These four filters are the entire blast radius control of every processfiles
     * sub-command, and every one of those commands is destructive and in place, so a
     * filter which is broader than the operator expects rewrites files which were meant
     * to be excluded
     */

    const auto root = dir.path();

    bl::fs::safeMkdirs( root / "skipme" );
    bl::fs::safeMkdirs( root / "sub.d" );

    writeAllBytes( root / "a.cpp", "a" );
    writeAllBytes( root / "b.CPP", "b" );
    writeAllBytes( root / "c.txt", "c" );
    writeAllBytes( root / "noext", "n" );
    writeAllBytes( root / "skipme" / "d.cpp", "d" );
    writeAllBytes( root / "sub.d" / "README", "r" );
    writeAllBytes( root / "sub.d" / "e.cpp", "e" );

    std::set< std::string > visited;

    const bltool::commands::ProcessFilesUtils::file_processor_callback_t callback =
        [ &visited ]( SAA_in const bl::fs::path& path ) -> void
        {
            visited.insert( path.filename().string() );
        };

    /*
     * processAllFiles derives the extension with rfind( '.' ) over the *absolute* path,
     * so the two assertions which depend on that are only meaningful when the temporary
     * directory itself contains no '.'
     */

    const bool tempPathHasDot = ( root.string().find( '.' ) != std::string::npos );

    {
        /*
         * No filters at all
         */

        visited.clear();

        bltool::commands::ProcessFilesUtils::processAllFiles( root, callback );

        UTF_REQUIRE_EQUAL( visited.count( "a.cpp" ), 1U );
        UTF_REQUIRE_EQUAL( visited.count( "b.CPP" ), 1U );
        UTF_REQUIRE_EQUAL( visited.count( "c.txt" ), 1U );
        UTF_REQUIRE_EQUAL( visited.count( "d.cpp" ), 1U );
        UTF_REQUIRE_EQUAL( visited.count( "e.cpp" ), 1U );

        if( tempPathHasDot )
        {
            UTF_MESSAGE(
                "Skipping the extension-less asymmetry assertions - the temporary directory "
                "path contains a '.', which makes every entry under it look like it has an "
                "extension to processAllFiles"
                );
        }
        else
        {
            /*
             * This asymmetry is the observable consequence of deriving the extension from
             * the whole path rather than from the file name, and it is exactly what a
             * future fix would change: 'sub.d/README' looks like it has the extension
             * '.d/readme' and is processed, while 'noext' really has none and is skipped
             */

            UTF_REQUIRE_EQUAL( visited.count( "README" ), 1U );
            UTF_REQUIRE_EQUAL( visited.count( "noext" ), 0U );
        }
    }

    {
        /*
         * The entry side is lowered by to_lower_copy, so an upper case file extension is
         * matched by a lower case filter
         */

        visited.clear();

        bltool::commands::ProcessFilesUtils::processAllFiles(
            root,
            callback,
            std::vector< std::string >(),
            std::set< std::string >{ ".cpp" }
            );

        const std::set< std::string > expected{ "a.cpp", "b.CPP", "d.cpp", "e.cpp" };

        UTF_REQUIRE( visited == expected );
    }

    {
        /*
         * The filter side, however, is not lowered by any caller, so an upper case
         * '--extension' matches nothing at all
         *
         * This records the CURRENT behaviour; if the filter is ever lowered where it is
         * built, this assertion flips to a match and that is a deliberate, visible
         * decision point rather than a silent change
         */

        visited.clear();

        bltool::commands::ProcessFilesUtils::processAllFiles(
            root,
            callback,
            std::vector< std::string >(),
            std::set< std::string >{ ".CPP" }
            );

        UTF_REQUIRE( visited.empty() );
    }

    {
        /*
         * The ignore fragments are matched as a substring of the whole path
         */

        visited.clear();

        bltool::commands::ProcessFilesUtils::processAllFiles(
            root,
            callback,
            std::vector< std::string >{ "skipme" },
            std::set< std::string >{ ".cpp" }
            );

        const std::set< std::string > expected{ "a.cpp", "b.CPP", "e.cpp" };

        UTF_REQUIRE( visited == expected );
        UTF_REQUIRE_EQUAL( visited.count( "d.cpp" ), 0U );
    }

    {
        /*
         * A root which is not a directory short-circuits and bypasses both filters -
         * 'noext' matches neither of them and is still handed to the callback
         */

        std::size_t callbackCount = 0U;

        const bltool::commands::ProcessFilesUtils::file_processor_callback_t countingCallback =
            [ &visited, &callbackCount ]( SAA_in const bl::fs::path& path ) -> void
            {
                visited.insert( path.filename().string() );

                ++callbackCount;
            };

        visited.clear();

        bltool::commands::ProcessFilesUtils::processAllFiles(
            root / "noext",
            countingCallback,
            std::vector< std::string >{ "skipme" },
            std::set< std::string >{ ".cpp" }
            );

        UTF_REQUIRE_EQUAL( callbackCount, 1U );
        UTF_REQUIRE_EQUAL( visited.count( "noext" ), 1U );
    }

    {
        /*
         * A root which does not exist is rejected by fs::ensurePathExists
         */

        UTF_REQUIRE_THROW(
            bltool::commands::ProcessFilesUtils::processAllFiles( root / "nope", callback ),
            std::exception
            );
    }
}

UTF_AUTO_TEST_CASE( BlTool_ProcessFilesRemoveMarkedCommentsTests )
{
    utest::TestDirectory dir;

    std::uint64_t filesCount = 0U;

    {
        /*
         * A marked comment is removed and the file is counted
         */

        const auto path = dir.testFile( "marked.h" );

        writeAllBytes( path, "int a = 1;\n/*\n * GENERATED by tool\n */\nint b = 2;\n" );

        bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "generated" }, filesCount );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), "int a = 1;\nint b = 2;\n" );
        UTF_REQUIRE_EQUAL( filesCount, 1U );
    }

    {
        /*
         * Each line is trimmed and lowered before it is matched while the markers coming
         * from '--marker' are not, so an upper case marker silently matches nothing
         *
         * This records the CURRENT behaviour - it is the trap, not the intent
         */

        const auto path = dir.testFile( "marked-uppercase.h" );

        const std::string original = "int a = 1;\n/*\n * GENERATED by tool\n */\nint b = 2;\n";

        writeAllBytes( path, original );

        bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "GENERATED" }, filesCount );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), original );
        UTF_REQUIRE_EQUAL( filesCount, 1U );
    }

    {
        /*
         * A comment which matches no marker is flushed back verbatim
         */

        const auto path = dir.testFile( "unmarked.h" );

        const std::string original = "int a = 1;\n/*\n * keep me\n */\nint b = 2;\n";

        writeAllBytes( path, original );

        bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "generated" }, filesCount );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), original );
        UTF_REQUIRE_EQUAL( filesCount, 1U );
    }

    {
        /*
         * The single line comment branch - it is dropped if and only if it matches
         */

        const auto path = dir.testFile( "singleline.h" );

        writeAllBytes( path, "/* generated */\nint a = 1;\n/* keep */\nint b = 2;\n" );

        bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "generated" }, filesCount );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), "int a = 1;\n/* keep */\nint b = 2;\n" );
    }

    {
        /*
         * Code which follows the end of a block comment on the same line is legal C++
         * which this rewriter refuses, and a refused input must be left exactly as it was
         * found - the file is the user's source code, not a scratch buffer
         */

        const auto path = dir.testFile( "midline.h" );

        const std::string original = "int a = 1;\n/*\n * generated\n */ int b = 2;\n";

        writeAllBytes( path, original );

        UTF_REQUIRE_THROW_MESSAGE(
            bltool::commands::ProcessFilesUtils::removeCommentsWithMarkers( path, { "generated" }, filesCount ),
            bl::UnexpectedException,
            "Text after end of comment on the same line"
            );

        UTF_REQUIRE_EQUAL( readAllBytes( path ), original );
    }
}
