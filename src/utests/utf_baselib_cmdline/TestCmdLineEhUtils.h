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

#include <baselib/cmdline/EhUtils.h>

#include <baselib/core/FileEncoding.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/OS.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfDirectoryFixture.h>

/*
 * Newer versions of OpenSSL do not define this value, exactly as TcpSslBaseTasks.h has to
 * handle it - any non-zero packed reason works equally well here
 */

#ifndef SSL_R_SHORT_READ
#define SSL_R_SHORT_READ 219
#endif

namespace
{
    /**
     * @brief Lowers the global logging level below errors and warnings for the scope
     *
     * EhUtils::processException logs the failure it handles at error level by design, and
     * the test binaries route every error and warning line to BOOST_ERROR (UtfMain.h:121),
     * so a case which provokes one on purpose would otherwise fail on its own fixture.
     * LL_NOTIFY is the highest level which suppresses exactly those two channels
     */

    class EhUtilsExpectedErrorsScope
    {
        BL_NO_COPY_OR_MOVE( EhUtilsExpectedErrorsScope )

    private:

        const int                                                       m_saved;

    public:

        EhUtilsExpectedErrorsScope()
            :
            m_saved( bl::Logging::setLevel( bl::Logging::LL_NOTIFY, true /* global */ ) )
        {
        }

        ~EhUtilsExpectedErrorsScope() NOEXCEPT
        {
            BL_NOEXCEPT_BEGIN()

            bl::Logging::setLevel( m_saved, true /* global */ );

            BL_NOEXCEPT_END()
        }
    };

    /**
     * @brief Lists the regular files directly under a directory
     */

    inline std::vector< bl::fs::path > ehUtilsListRegularFiles( SAA_in const bl::fs::path& root )
    {
        std::vector< bl::fs::path > result;

        for( bl::fs::directory_iterator end, it( root ); it != end; ++it )
        {
            if( bl::fs::is_regular_file( it -> status() ) )
            {
                result.push_back( it -> path() );
            }
        }

        return result;
    }

    /**
     * @brief Restores an environment variable to the value it was captured with
     */

    inline void ehUtilsRestoreEnvironmentVariable(
        SAA_in          const std::string&                          name,
        SAA_in          const bl::os::string_ptr&                   original
        )
    {
        if( original )
        {
            bl::os::setEnvironmentVariable( name, *original );
        }
        else
        {
            bl::os::unsetEnvironmentVariable( name );
        }
    }

} // __unnamed

/************************************************************************
 * EhUtils::processException writes the exception dump into a private file
 *
 * The dump carries the full diagnostic information - URLs, request and response bodies -
 * and the temporary directory is shared with every other user of the machine, so the file
 * has to be created with owner only access; and because the function is the last resort
 * handler of tryExecuteCommand, its NOEXCEPT guarantee under a failing temporary directory
 * is what keeps an application crash from becoming a crash on crash
 */

UTF_AUTO_TEST_CASE( CmdLineEhUtils_ProcessExceptionWritesPrivateDumpFile )
{
    using namespace bl;

    const EhUtilsExpectedErrorsScope suppressExpectedErrors;

    utest::TestDirectory dir;

    const auto originalTmpDir = os::tryGetEnvironmentVariable( "TMPDIR" );
    const auto originalTmp = os::tryGetEnvironmentVariable( "TMP" );
    const auto originalTemp = os::tryGetEnvironmentVariable( "TEMP" );

    BL_SCOPE_EXIT(
        {
            ehUtilsRestoreEnvironmentVariable( "TMPDIR", originalTmpDir );
            ehUtilsRestoreEnvironmentVariable( "TMP", originalTmp );
            ehUtilsRestoreEnvironmentVariable( "TEMP", originalTemp );
        }
        );

    const auto redirectTempDirectory = []( SAA_in const fs::path& path ) -> void
    {
        /*
         * POSIX honours TMPDIR while Windows' GetTempPath honours TMP and then TEMP, so
         * all three are pointed at the same place
         */

        const auto value = path.string();

        os::setEnvironmentVariable( "TMPDIR", value );
        os::setEnvironmentVariable( "TMP", value );
        os::setEnvironmentVariable( "TEMP", value );
    };

    redirectTempDirectory( dir.path() );

    /*
     * The default dump - the diagnostic information of the exception is what is written
     */

    fs::path firstPath;

    {
        try
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "distinctive-marker" );
        }
        catch( std::exception& e )
        {
            cmdline::EhUtils::processException( e, "user text", "debug context" );
        }

        const auto files = ehUtilsListRegularFiles( dir.path() );

        UTF_REQUIRE_EQUAL( files.size(), 1U );

        firstPath = files[ 0 ];

        const auto fileName = firstPath.filename().string();

        /*
         * The uuid and the pid in the name are what keeps concurrent processes from
         * colliding in the shared temporary directory
         */

        UTF_REQUIRE( str::starts_with( fileName, "error." ) );
        UTF_REQUIRE( str::ends_with( fileName, ".txt" ) );
        UTF_REQUIRE( cpp::contains( fileName, std::to_string( os::getPid() ) ) );

        const auto content = encoding::readTextFile( firstPath );

        UTF_REQUIRE( cpp::contains( content, "debug context:" ) );
        UTF_REQUIRE( cpp::contains( content, "distinctive-marker" ) );

        /*
         * eh::diagnostic_information always emits this marker, which is what makes the
         * negative assertion in the explicit dump sub-block below discriminating
         */

        UTF_REQUIRE( cpp::contains( content, "Throw in function" ) );

#if ! defined( _WIN32 )

        /*
         * No group and no other access at all - this is the security property the
         * os::createNewFilePrivate call in processException exists for
         */

        UTF_REQUIRE(
            fs::status( firstPath ).permissions() ==
                ( fs::perms::owner_read | fs::perms::owner_write )
            );

#endif // ! defined( _WIN32 )
    }

    /*
     * An explicitly supplied dump is used verbatim instead of the diagnostic information
     */

    {
        try
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "distinctive-marker" );
        }
        catch( std::exception& e )
        {
            cmdline::EhUtils::processException(
                e,
                "user text",
                "debug context",
                std::string( "EXPLICIT-DUMP-TEXT" )
                );
        }

        const auto files = ehUtilsListRegularFiles( dir.path() );

        UTF_REQUIRE_EQUAL( files.size(), 2U );

        fs::path secondPath;

        for( const auto& path : files )
        {
            if( path != firstPath )
            {
                secondPath = path;
            }
        }

        UTF_REQUIRE( ! secondPath.empty() );

        const auto content = encoding::readTextFile( secondPath );

        UTF_REQUIRE( cpp::contains( content, "EXPLICIT-DUMP-TEXT" ) );
        UTF_REQUIRE( ! cpp::contains( content, "Throw in function" ) );
    }

    /*
     * A temporary directory the dump file cannot be created in - the BL_CHK on
     * createNewFilePrivate fails, the inner catch-all handles it and NOEXCEPT holds
     */

    {
#if defined( _WIN32 )

        /*
         * A 0500 directory does not deny the owner on Windows
         */

        const bool canDenyFileCreation = false;

#else

        /*
         * ... and neither does it when the test is run as root
         */

        const bool canDenyFileCreation = ( 0 != ::geteuid() );

#endif // defined( _WIN32 )

        fs::path unwritableDir;

        if( canDenyFileCreation )
        {
            unwritableDir = dir.path() / "readonly";

            fs::safeMkdirs( unwritableDir );

            fs::permissions( unwritableDir, fs::perms::owner_read | fs::perms::owner_exe );
        }
        else
        {
            /*
             * In this configuration the sub-block pins the NOEXCEPT contract only:
             * fs::temp_directory_path() itself throws inside the try, so the BL_CHK on
             * createNewFilePrivate is never reached
             */

            unwritableDir = dir.path() / "no-such-directory";
        }

        BL_SCOPE_EXIT(
            {
                if( canDenyFileCreation )
                {
                    fs::permissions(
                        unwritableDir,
                        fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exe
                        );
                }
            }
            );

        redirectTempDirectory( unwritableDir );

        try
        {
            BL_THROW( bl::UnexpectedException(), BL_MSG() << "distinctive-marker" );
        }
        catch( std::exception& e )
        {
            UTF_REQUIRE_NO_THROW( cmdline::EhUtils::processException( e, "user text", "debug" ) );
        }

        if( canDenyFileCreation )
        {
            UTF_REQUIRE_EQUAL( ehUtilsListRegularFiles( unwritableDir ).size(), 0U );
        }
    }
}

/************************************************************************
 * EhUtils::asioErrorCallback - the SSL branch and the null location fallback
 *
 * The SSL branch is the half which turns an OpenSSL handshake failure into a diagnosable
 * exception carrying the OpenSSL reason string; passing the ASIO code straight through
 * would report every TLS failure as a meaningless system_error over an opaque integer
 */

UTF_AUTO_TEST_CASE( CmdLineEhUtils_AsioErrorCallbackBranches )
{
    using namespace bl;

    /*
     * Keep the OpenSSL error queue empty so that crypto::getException does not attach a
     * nested exception chain and the assertions below stay deterministic
     */

    BL_CHK_CRYPTO_API_RESET_ERROR();

    /*
     * A generic category code - the value is ENOENT by definition of that category
     */

    const auto genericEc = eh::errc::make_error_code( eh::errc::no_such_file_or_directory );

    UTF_REQUIRE_EXCEPTION(
        cmdline::EhUtils::asioErrorCallback( genericEc, "myloc" ),
        SystemException,
        [ &genericEc ]( SAA_in const SystemException& e ) -> bool
        {
            if( ! cpp::contains( std::string( e.what() ), "myloc" ) )
            {
                return false;
            }

            const auto* errNo = eh::get_error_info< eh::errinfo_errno >( e );

            if( ! errNo || *errNo != genericEc.value() )
            {
                return false;
            }

            return nullptr == eh::get_error_info< eh::errinfo_system_code >( e );
        }
        );

    /*
     * The null location fallback - this is what the one argument
     * boost::asio::detail::throw_error( ec ) override passes
     */

    UTF_REQUIRE_EXCEPTION(
        cmdline::EhUtils::asioErrorCallback( genericEc, nullptr ),
        SystemException,
        []( SAA_in const SystemException& e ) -> bool
        {
            return cpp::contains( std::string( e.what() ), "Boost ASIO" );
        }
        );

    /*
     * The SSL branch - the same numeric value re-tagged with the OpenSSL category
     */

    const eh::error_code sslEc(
        ERR_PACK( ERR_LIB_SSL, 0, SSL_R_SHORT_READ ),
        asio::error::get_ssl_category()
        );

    const auto checkSslException = []( SAA_in const SystemException& e ) -> bool
    {
        const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

        if( ! ec || std::string( ec -> category().name() ) != "OpenSSL" )
        {
            return false;
        }

        /*
         * The OpenSSL category is neither the system nor the generic one, so neither of
         * the two numeric error info fields may be attached - together with the category
         * name above this is what discriminates the two branches
         */

        if( nullptr != eh::get_error_info< eh::errinfo_system_code >( e ) )
        {
            return false;
        }

        if( nullptr != eh::get_error_info< eh::errinfo_errno >( e ) )
        {
            return false;
        }

        return nullptr != eh::get_error_info< eh::errinfo_error_code_message >( e );
    };

    UTF_REQUIRE_EXCEPTION(
        cmdline::EhUtils::asioErrorCallback( sslEc, "myloc" ),
        SystemException,
        [ &checkSslException ]( SAA_in const SystemException& e ) -> bool
        {
            if( ! cpp::contains( std::string( e.what() ), "myloc" ) )
            {
                return false;
            }

            return checkSslException( e );
        }
        );

    UTF_REQUIRE_EXCEPTION(
        cmdline::EhUtils::asioErrorCallback( sslEc, nullptr ),
        SystemException,
        [ &checkSslException ]( SAA_in const SystemException& e ) -> bool
        {
            if( ! cpp::contains( std::string( e.what() ), "Boost ASIO" ) )
            {
                return false;
            }

            return checkSslException( e );
        }
        );
}
