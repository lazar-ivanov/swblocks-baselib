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

#include <baselib/loader/Cookie.h>

#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <utests/baselib/UtfDirectoryFixture.h>
#include <utests/baselib/Utf.h>

UTF_AUTO_TEST_CASE( TestCookie )
{
    using namespace bl;
    using namespace bl::loader;
    using namespace utest;

    utest::TestDirectory dir;

    const auto file = dir.testFile( "cookie" );

    /*
     * Check exception thrown when cookie does not exist
     */

    UTF_CHECK_THROW( CookieFactory::getCookie( file, false ), UnexpectedException );

    /*
     * Check cookie can be created from file
     */

    {
        bl::fs::SafeOutputFileStreamWrapper outputFile( file );
        auto& os = outputFile.stream();

        os << "4f082035-e301-4cce-94f0-68f1c99f9223" << std::endl;
    }

    const auto cookie = CookieFactory::getCookie( file, false );
    UTF_CHECK_EQUAL( cookie -> getId(), uuids::string2uuid( "4f082035-e301-4cce-94f0-68f1c99f9223" ) );

    /*
     * Check new cookie creation
     */

    const auto newFile = dir.testFile( "new_cookie" );
    CookieFactory::getCookie( newFile, true );

    UTF_CHECK( fs::exists( newFile ) );

    /*
     * Check the created cookie is exactly the one which was persisted, and that it is
     * stable - the same path must yield the same id forever, so asking to create again
     * on an existing file must not rewrite it
     */

    {
        const auto createdFile = dir.testFile( "created_cookie" );

        const auto created = CookieFactory::getCookie( createdFile, true );

        std::string lineFromFile;

        {
            bl::fs::SafeInputFileStreamWrapper inputFile( createdFile );
            auto& is = inputFile.stream();

            std::getline( is, lineFromFile );
        }

        UTF_REQUIRE_EQUAL( created -> getId(), uuids::string2uuid( lineFromFile ) );

        const auto reread = CookieFactory::getCookie( createdFile, false );
        const auto reCreated = CookieFactory::getCookie( createdFile, true );

        UTF_REQUIRE_EQUAL( created -> getId(), reread -> getId() );
        UTF_REQUIRE_EQUAL( created -> getId(), reCreated -> getId() );

        /*
         * The string form must never disagree with the uuid it is derived from
         */

        UTF_REQUIRE_EQUAL( created -> getIdStr(), uuids::uuid2string( created -> getId() ) );
        UTF_REQUIRE_EQUAL( reread -> getIdStr(), uuids::uuid2string( reread -> getId() ) );
        UTF_REQUIRE_EQUAL( reCreated -> getIdStr(), uuids::uuid2string( reCreated -> getId() ) );
    }

    /*
     * Check the parent path of a new cookie is created on demand
     */

    {
        const auto nested = dir.testFile( "a" ) / "b" / "cookie";

        const auto nestedCookie = CookieFactory::getCookie( nested, true );

        UTF_REQUIRE( fs::path_exists( nested ) );

        const auto reread2 = CookieFactory::getCookie( nested, false );

        UTF_REQUIRE_EQUAL( nestedCookie -> getId(), reread2 -> getId() );
    }

    /*
     * Check a cookie path which exists but is a directory - this is what the
     * is_regular_file guard is for and what distinguishes it from the plain
     * 'does not exist' guard, in both polarities of createIfNecessary
     */

    {
        const auto asDir = dir.testFile( "cookie_dir" );

        fs::safeMkdirs( asDir );

        UTF_REQUIRE_THROW_MESSAGE(
            CookieFactory::getCookie( asDir, false ),
            UnexpectedException,
            "is not a regular file"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            CookieFactory::getCookie( asDir, true ),
            UnexpectedException,
            "is not a regular file"
            );
    }

    /*
     * Check a malformed cookie file is rejected rather than read as a nil uuid; an
     * existing file is never rewritten, so createIfNecessary makes no difference
     */

    {
        struct MalformedCookieInfo
        {
            const char*         m_name;
            const char*         m_content;
        };

        const MalformedCookieInfo malformedCookies[] =
        {
            { "empty_cookie",               ""                                          },
            { "garbage_cookie",             "not-a-uuid"                                },
            { "trailing_garbage_cookie",    "4f082035-e301-4cce-94f0-68f1c99f9223x"     },
        };

        for( const auto& info : malformedCookies )
        {
            const auto path = dir.testFile( info.m_name );

            const std::string content( info.m_content );

            {
                const auto cookieFile = os::fopen( path, "w" );

                if( ! content.empty() )
                {
                    os::fwrite( cookieFile, content.c_str(), content.size() );
                }
            }

            UTF_REQUIRE_THROW( CookieFactory::getCookie( path, false ), ArgumentException );
            UTF_REQUIRE_THROW( CookieFactory::getCookie( path, true ), ArgumentException );
        }
    }
}
