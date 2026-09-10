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
#include <utests/baselib/LoggerUtils.h>

#include <baselib/core/ErrorHandling.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/SecureStringWrapper.h>
#include <baselib/core/BaseIncludes.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/************************************************************************
 * os::fread / os::fwrite error handling tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSStdioReadWriteErrorTests )
{
    bl::fs::TmpDir tmpDir;

    const auto path = tmpDir.path() / "stdio-read-write.bin";
    const auto writeOnlyPath = tmpDir.path() / "stdio-write-only.bin";
    const auto noflushPath = tmpDir.path() / "stdio-noflush.bin";

    const char data[] = { 'a', 'b', 'c', 'd' };

    char buffer[ 16 ];

    {
        const auto file = bl::os::fopen( path, "wb" );

        bl::os::fwrite( file, data, sizeof( data ) );
    }

    UTF_REQUIRE_EQUAL( bl::fs::file_size( path ), 4U );

    /*
     * A short read at the end of the file is not a system error - os::fread( ... )
     * clears errno before the call and snapshots it immediately after, so a stale
     * errno left behind by an unrelated call earlier on this thread must not be
     * reported as the cause
     */

    {
        const auto file = bl::os::fopen( path, "rb" );

        errno = EACCES;

        try
        {
            bl::os::fread( file, buffer, sizeof( buffer ) );

            UTF_FAIL( "os::fread must throw when reading past the end of file" );
        }
        catch( bl::SystemException& e )
        {
            const auto* errNo = bl::eh::get_error_info< bl::eh::errinfo_errno >( e );

            UTF_REQUIRE( errNo );

            /*
             * i.e. operation_not_permitted and not the planted EACCES
             */

            UTF_REQUIRE_EQUAL( EPERM, *errNo );

            UTF_REQUIRE(
                bl::cpp::contains( std::string( e.what() ), "Reading past the end of file" )
                );
        }
    }

    /*
     * An exact size read must not throw, even with errno set beforehand
     */

    {
        const auto file = bl::os::fopen( path, "rb" );

        errno = EACCES;

        UTF_REQUIRE_NO_THROW( bl::os::fread( file, buffer, sizeof( data ) ) );
    }

    /*
     * The real error branch - reading from a handle which was opened for writing only
     *
     * Note that the errno value is platform specific, so only its presence is asserted
     * here, together with the message which distinguishes this branch from the end of file
     * one above
     *
     * The branch is decided by std::ferror and NOT by errno - that is the whole point of
     * the distinction, and it is why this case is meaningful on both platforms. glibc sets
     * EBADF here, so the reported cause is EBADF; the Windows CRT sets no errno at all for
     * a stream level failure (measured with vc143 / UCRT: std::fread on a stream opened
     * "wb" returns 0 with errno == 0 and std::ferror == 1), so the generic io_error is
     * reported instead. Before that fix, testing errno made Windows misreport this genuine
     * error as "Reading past the end of file" carrying EPERM
     */

    {
        const auto file = bl::os::fopen( writeOnlyPath, "wb" );

        try
        {
            bl::os::fread( file, buffer, sizeof( data ) );

            UTF_FAIL( "os::fread must throw when reading from a write only handle" );
        }
        catch( bl::SystemException& e )
        {
            const auto* errNo = bl::eh::get_error_info< bl::eh::errinfo_errno >( e );

            UTF_REQUIRE( errNo );
            UTF_REQUIRE( 0 != *errNo );

            UTF_REQUIRE(
                bl::cpp::contains(
                    std::string( e.what() ),
                    "An error occurred while reading from a file with std::fread"
                    )
                );

            /*
             * And the cause is the platform's own where it has one, the generic io_error
             * where it has not - never the EPERM of the end-of-file branch, which is what
             * this case would have seen on Windows before the discriminator was fixed
             */

            UTF_REQUIRE( EPERM != *errNo );
        }
    }

    /*
     * The noflush parameter has no caller anywhere in the repository; with it set the
     * caller owns the flush and the content only becomes visible afterwards
     */

    {
        {
            const auto file = bl::os::fopen( noflushPath, "wb" );

            UTF_REQUIRE_NO_THROW( bl::os::fwrite( file, data, sizeof( data ), true /* noflush */ ) );

            UTF_REQUIRE_EQUAL( 0, std::fflush( file.get() ) );
        }

        const auto file = bl::os::fopen( noflushPath, "rb" );

        char readBack[ sizeof( data ) ];

        UTF_REQUIRE_NO_THROW( bl::os::fread( file, readBack, sizeof( readBack ) ) );

        UTF_REQUIRE( 0 == std::memcmp( data, readBack, sizeof( data ) ) );
    }
}

#if ! defined( _WIN32 )

/************************************************************************
 * fs::trySafeRemoveAll / fs::safeDeletePathNothrow unsupported file type tests
 */

UTF_AUTO_TEST_CASE( FsUtils_UnsupportedFileTypeRemovalTests )
{
    /*
     * A FIFO is one of the file types trySafeRemoveAll( ... ) does not support, so the
     * recursion into a directory which contains one reaches the default: arm of the
     * switch, where reportUnexpectedFileType( ... ) throws - out of a function whose
     * signature promises to report failures through an eh::error_code
     *
     * safeDeletePathNothrow( ... ) logs a warning in that case and UtfMain.h maps
     * LL_WARNING onto BOOST_ERROR, so the warning has to be absorbed here
     *
     * Note that the line logger is pushed before the temporary directory is created, so
     * it is still in place while the directory is being torn down
     */

    bl::Logging::LineLoggerPusher pushLogger( &utest::warningToDebugLineLogger );

    bl::fs::TmpDir tmpDir;

    const auto dir = tmpDir.path() / "with-fifo";

    bl::fs::safeMkdirs( dir );

    const auto fifoPath = dir / "pipe";

    UTF_REQUIRE( 0 == ::mkfifo( fifoPath.string().c_str(), 0600 ) );

    /*
     * The try* overload throws instead of returning an error code
     */

    UTF_REQUIRE_THROW( bl::fs::trySafeRemoveAll( dir ), bl::UnexpectedException );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::fs::safeRemoveAll( dir ),
        bl::UnexpectedException,
        "is of unexpected type"
        );

    UTF_REQUIRE( bl::fs::path_exists( fifoPath ) );

    /*
     * safeDeletePathNothrow( ... ) reports the real outcome - BL_WARN_NOEXCEPT_END( ... )
     * logs the escaping exception and the result stays 'false', which is what the three
     * in-tree callers treat as "log and continue"
     */

    UTF_CHECK_EQUAL( false, bl::fs::safeDeletePathNothrow( dir ) );

    UTF_REQUIRE( bl::fs::path_exists( dir ) );
    UTF_REQUIRE( bl::fs::path_exists( fifoPath ) );

    /*
     * The positive control - a directory which CAN be deleted still reports success, so
     * the assertion above is not satisfied by a function which simply always fails
     */

    {
        const auto plainDir = tmpDir.path() / "plain";

        bl::fs::safeMkdirs( plainDir );

        UTF_CHECK_EQUAL( true, bl::fs::safeDeletePathNothrow( plainDir ) );

        UTF_REQUIRE( ! bl::fs::path_exists( plainDir ) );
    }

    /*
     * Clean up by hand, so the destructor of TmpDir does not hit the same path
     */

    UTF_REQUIRE( 0 == ::unlink( fifoPath.string().c_str() ) );

    bl::fs::safeRemove( dir );

    UTF_REQUIRE( ! bl::fs::path_exists( dir ) );
}

/************************************************************************
 * os::detail::OS::shellEscapeFormatter / transformCommandLineArguments tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSShellEscapeForRunAsUserTests )
{
    /*
     * When a user name is given createProcess( ... ) rewrites the command as
     * "/bin/su --command <joined> <userName>", so the quoting applied while joining the
     * arguments is the only thing which stops a caller supplied argument from being
     * executed as shell code under the account of another user
     *
     * Both functions are public statics and need no process to be tested directly
     */

    const std::vector< std::string > arguments
    {
        "echo",
        "a b",
        "it's",
        "$HOME",
        "`id`",
        "x;y",
        "*"
    };

    const auto args = bl::os::detail::OS::transformCommandLineArguments( "someuser", arguments );

    UTF_REQUIRE_EQUAL( 4U, args.size() );
    UTF_REQUIRE_EQUAL( args[ 0 ], std::string( "/bin/su" ) );
    UTF_REQUIRE_EQUAL( args[ 1 ], std::string( "--command" ) );
    UTF_REQUIRE_EQUAL( args[ 3 ], std::string( "someuser" ) );

    /*
     * Every argument is wrapped in single quotes and an embedded single quote is
     * emitted as the '\'' sequence - i.e. close, escaped quote, re-open
     */

    UTF_REQUIRE_EQUAL(
        args[ 2 ],
        std::string( "'echo' 'a b' 'it'\\''s' '$HOME' '`id`' 'x;y' '*'" )
        );

    /*
     * Now round trip the produced --command string through a real POSIX shell; note
     * that the working directory of the test contains files, so a globbing regression
     * changes the number of output lines
     */

    const std::vector< std::string > payload
    {
        "printf",
        "%s\n",
        "a b",
        "it's",
        "$HOME",
        "`id`",
        "x;y",
        "*"
    };

    const auto suArgs = bl::os::detail::OS::transformCommandLineArguments( "someuser", payload );

    UTF_REQUIRE_EQUAL( 4U, suArgs.size() );

    std::vector< std::string > lines;

    const auto cbRedirectedIos = [ & ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in              std::istream&                   out
        ) -> void
    {
        BL_UNUSED( process );

        std::string line;

        while( std::getline( out, line ) )
        {
            lines.push_back( line );
        }
    };

    const auto proc = bl::os::createRedirectedProcessMergeOutputAndWait(
        std::vector< std::string >{ "bash", "-c", suArgs[ 2 ] },
        cbRedirectedIos
        );

    UTF_REQUIRE( proc );

    /*
     * No variable expansion, no command substitution, no globbing and no word splitting
     */

    UTF_REQUIRE_EQUAL( 6U, lines.size() );

    UTF_CHECK_EQUAL( lines[ 0 ], std::string( "a b" ) );
    UTF_CHECK_EQUAL( lines[ 1 ], std::string( "it's" ) );
    UTF_CHECK_EQUAL( lines[ 2 ], std::string( "$HOME" ) );
    UTF_CHECK_EQUAL( lines[ 3 ], std::string( "`id`" ) );
    UTF_CHECK_EQUAL( lines[ 4 ], std::string( "x;y" ) );
    UTF_CHECK_EQUAL( lines[ 5 ], std::string( "*" ) );

    UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

    /*
     * With an empty user name the arguments are returned unchanged
     */

    UTF_REQUIRE(
        bl::os::detail::OS::transformCommandLineArguments( bl::str::empty(), payload ) == payload
        );
}

/************************************************************************
 * os::createNewFilePrivate / os::createNewFile tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateNewFilePrivateTests )
{
    bl::fs::TmpDir tmpDir;

    const auto privatePath = tmpDir.path() / "private.txt";
    const auto sharedPath = tmpDir.path() / "shared.txt";

    UTF_REQUIRE( bl::os::createNewFilePrivate( privatePath ) );
    UTF_REQUIRE( bl::fs::path_exists( privatePath ) );

    struct ::stat st;

    UTF_REQUIRE_EQUAL( 0, ::stat( privatePath.string().c_str(), &st ) );

    /*
     * createNewFilePrivate( ... ) is used to write full exception dumps into the shared
     * temporary directory, so group and other must have no access at all
     *
     * Asserting the absence of the group and other bits rather than the exact mode is
     * umask independent - the process umask can only clear bits, never add them
     */

    UTF_REQUIRE_EQUAL( 0U, static_cast< unsigned >( st.st_mode & 0077 ) );
    UTF_REQUIRE( st.st_mode & S_IRUSR );
    UTF_REQUIRE( st.st_mode & S_IWUSR );

    const auto privateModeBefore = st.st_mode;

    /*
     * The file exists now, so the second call must report failure and leave the mode
     * of the existing file alone
     */

    UTF_REQUIRE( ! bl::os::createNewFilePrivate( privatePath ) );

    UTF_REQUIRE_EQUAL( 0, ::stat( privatePath.string().c_str(), &st ) );
    UTF_REQUIRE_EQUAL( privateModeBefore, st.st_mode );

    UTF_REQUIRE( bl::os::createNewFile( sharedPath ) );
    UTF_REQUIRE( bl::fs::path_exists( sharedPath ) );

    UTF_REQUIRE_EQUAL( 0, ::stat( sharedPath.string().c_str(), &st ) );

    UTF_REQUIRE_EQUAL( 0U, static_cast< unsigned >( st.st_mode & 0022 ) );
    UTF_REQUIRE( st.st_mode & S_IRUSR );
    UTF_REQUIRE( st.st_mode & S_IWUSR );

    UTF_REQUIRE( ! bl::os::createNewFile( sharedPath ) );

    /*
     * A failed open is reported by returning false and not by throwing - fs::createLockFile
     * relies on that for its single winner logic
     */

    const auto noSuchDirPath = tmpDir.path() / "nodir" / "f.txt";

    UTF_REQUIRE_NO_THROW( bl::os::createNewFilePrivate( noSuchDirPath ) );
    UTF_REQUIRE( ! bl::os::createNewFilePrivate( noSuchDirPath ) );

    UTF_REQUIRE_NO_THROW( bl::os::createNewFile( noSuchDirPath ) );
    UTF_REQUIRE( ! bl::os::createNewFile( noSuchDirPath ) );
}

/************************************************************************
 * os::readFromInputHidden tests under redirected standard input
 */

UTF_AUTO_TEST_CASE( BaseLib_OSReadFromInputHiddenRedirectedTests )
{
    bl::fs::TmpDir tmpDir;

    const auto inputPath = tmpDir.path() / "input.txt";

    {
        const auto file = bl::os::fopen( inputPath, "wb" );

        const std::string content( "s3cr3t\nsecond\n" );

        bl::os::fwrite( file, content.data(), content.size() );
    }

    const int savedStdin = ::dup( STDIN_FILENO );

    UTF_REQUIRE( -1 != savedStdin );

    /*
     * Standard input has to be restored even if an assertion below fails, otherwise the
     * rest of the module would run with a redirected one
     */

    BL_SCOPE_EXIT(
        {
            ::dup2( savedStdin, STDIN_FILENO );
            ::close( savedStdin );
        }
        );

    {
        const int fd = ::open( inputPath.string().c_str(), O_RDONLY );

        UTF_REQUIRE( -1 != fd );

        const int rc = ::dup2( fd, STDIN_FILENO );

        ::close( fd );

        UTF_REQUIRE( -1 != rc );
    }

    /*
     * The terminating newline is consumed and is not part of the secret
     */

    const auto first = bl::os::readFromInputHidden();

    UTF_REQUIRE_EQUAL( std::string( "s3cr3t" ), first.getAsNonSecureString() );

    /*
     * This is the assertion for the _IONBF contract: readFromInputHidden( ... ) reads
     * from a duplicate of the standard input descriptor, and a duplicate shares its
     * file offset with the original, so with stdio buffering left on the first call
     * would have swallowed the whole file and this one would come back empty
     */

    const auto second = bl::os::readFromInputHidden();

    UTF_REQUIRE_EQUAL( std::string( "second" ), second.getAsNonSecureString() );

    /*
     * Reading an exhausted stream is an end of file and not an error
     */

    UTF_REQUIRE_NO_THROW( bl::os::readFromInputHidden() );

    const auto third = bl::os::readFromInputHidden();

    UTF_REQUIRE( third.getAsNonSecureString().empty() );

    {
        /*
         * Standard input is a file rather than a terminal, so the guard must tolerate
         * the ENOTTY / EINVAL from tcgetattr and report the echo as not disabled -
         * otherwise every non-interactive invocation would fail
         */

        bl::os::DisableConsoleEcho guard;

        UTF_REQUIRE( ! guard.isEchoDisabled() );
    }
}

/************************************************************************
 * os::terminateProcess process group signalling tests
 */

namespace
{
    /*
     * Spawns a long running child which has a long running grandchild of its own and
     * returns the reference of the direct child; the pid of the grandchild is read from
     * the first line of output inside the redirect callback, since the pipe is closed
     * as soon as it returns
     *
     * The child closes its standard output right after reporting the pid, and the
     * grandchild never gets hold of it, because the redirected stream reads through
     * std::fread( ... ) (stdio_file_source::read, OSImplPlatformCommon.h) which blocks
     * until the requested buffer is full or the stream hits the end of file - without
     * releasing the pipe the read below would block for the whole lifetime of the child
     */

    bl::os::process_ref spawnProcessWithGrandChild( SAA_out ::pid_t& grandChildPid )
    {
        std::string line;

        const auto callbackIos = [ &line ](
            SAA_in              const bl::os::process_handle_t  process,
            SAA_in_opt          std::istream*                   out,
            SAA_in_opt          std::istream*                   err,
            SAA_in_opt          std::ostream*                   in
            ) -> void
        {
            UTF_REQUIRE( process );
            UTF_REQUIRE( out );
            UTF_REQUIRE( ! err );
            UTF_REQUIRE( ! in );

            std::getline( *out, line );
        };

        auto proc = bl::os::createProcess(
            std::vector< std::string >
            {
                "bash",
                "-c",
                "sleep 300 >/dev/null 2>&1 & echo $!; exec 1>&-; wait"
            },
            bl::os::ProcessCreateFlags::RedirectStdout,
            callbackIos
            );

        UTF_REQUIRE( proc );
        UTF_REQUIRE( ! line.empty() );

        grandChildPid = static_cast< ::pid_t >( std::stoi( line ) );

        UTF_REQUIRE( 0 < grandChildPid );

        return proc;
    }

    /*
     * Polls with a bounded loop instead of sleeping for a fixed interval
     */

    bool waitForProcessToDisappear( SAA_in const ::pid_t pid )
    {
        for( std::size_t i = 0U; i < 100U; ++i )
        {
            if( -1 == ::kill( pid, 0 ) && ESRCH == errno )
            {
                return true;
            }

            bl::os::sleep( bl::time::milliseconds( 50 ) );
        }

        return false;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_OSTerminateProcessGroupTests )
{
    /*
     * Every child calls ::setsid( ... ), so it leads a process group whose id equals its
     * own pid; sendSignalNoLock( ... ) then signals -pid when includeSubprocesses is set
     * and pid alone when it is not
     *
     * The existing BaseLib_OSTerminateProcessTree case passes includeSubprocesses = true
     * but its child is "bash -c cat", where bash execs cat and there is no grandchild at
     * all, so it would pass identically if the flag were ignored
     */

    {
        ::pid_t grandChildPid = 0;

        const auto proc = spawnProcessWithGrandChild( grandChildPid );

        UTF_REQUIRE_EQUAL( 0, ::kill( grandChildPid, 0 ) );

        int exitCode = 0;

        UTF_REQUIRE(
            bl::os::terminateProcess(
                proc,
                &exitCode,
                true                    /* force */,
                true                    /* includeSubprocesses */,
                10000                   /* timeoutMs */
                )
            );

        UTF_CHECK_EQUAL( -SIGKILL, exitCode );

        UTF_REQUIRE( waitForProcessToDisappear( grandChildPid ) );
    }

    /*
     * The opposite direction - the grandchild must survive, otherwise the flag would be
     * killing unrelated processes in the group of the caller
     */

    {
        ::pid_t grandChildPid = 0;

        const auto proc = spawnProcessWithGrandChild( grandChildPid );

        UTF_REQUIRE_EQUAL( 0, ::kill( grandChildPid, 0 ) );

        int exitCode = 0;

        UTF_REQUIRE(
            bl::os::terminateProcess(
                proc,
                &exitCode,
                true                    /* force */,
                false                   /* includeSubprocesses */,
                10000                   /* timeoutMs */
                )
            );

        UTF_CHECK_EQUAL( -SIGKILL, exitCode );

        UTF_CHECK_EQUAL( 0, ::kill( grandChildPid, 0 ) );

        /*
         * The grandchild has been reparented and has to be cleaned up here
         */

        UTF_REQUIRE_EQUAL( 0, ::kill( grandChildPid, SIGKILL ) );
    }
}

#endif // ! defined( _WIN32 )
