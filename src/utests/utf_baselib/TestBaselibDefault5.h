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
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/UtfBaseLibCommon.h>

#include <baselib/core/Checksum.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#if ! defined( _WIN32 )

/*
 * ::pthread_kill is needed to interrupt a thread which is blocked in semop( ... ) and
 * <pthread.h> is not included by any of the baselib headers
 */

#include <pthread.h>

#endif // ! defined( _WIN32 )

/************************************************************************
 * os::validateProcessRedirectFlags negative tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSValidateProcessRedirectFlagsTests )
{
    /*
     * validateProcessRedirectFlags( ... ) is the single gate which decides whether a
     * redirect configuration is legal; it is called by both createProcess( ... )
     * implementations before anything is spawned, so every rejection below prevents a
     * downstream assertion or a null callback dereference
     */

    const bl::os::process_redirect_callback_ios_t cbIos = [](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::istream*                   out,
        SAA_in_opt          std::istream*                   err,
        SAA_in_opt          std::ostream*                   in
        ) -> void
    {
        BL_UNUSED( process );
        BL_UNUSED( out );
        BL_UNUSED( err );
        BL_UNUSED( in );
    };

    const bl::os::process_redirect_callback_file_t cbFile = [](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::FILE*                      outFile,
        SAA_in_opt          std::FILE*                      errFile,
        SAA_in_opt          std::FILE*                      inFile
        ) -> void
    {
        BL_UNUSED( process );
        BL_UNUSED( outFile );
        BL_UNUSED( errFile );
        BL_UNUSED( inFile );
    };

    /*
     * A non-redirect bit with no redirect bit set at all
     */

    UTF_REQUIRE_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
            cbIos
            ),
        bl::ArgumentException
        );

    /*
     * MergeStdoutAndStderr requires both RedirectStdout and RedirectStderr
     */

    UTF_REQUIRE_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdout | bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
            cbIos
            ),
        bl::ArgumentException
        );

    /*
     * CloseStdin requires RedirectStdin
     */

    UTF_REQUIRE_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdout | bl::os::ProcessCreateFlags::CloseStdin,
            cbIos
            ),
        bl::ArgumentException
        );

    /*
     * Redirect flags with no callback and a callback with no redirect flags are both
     * rejected, but with a different message than the flag combinations above
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdout
            ),
        bl::ArgumentException,
        "validateProcessRedirectFlags: the callback parameter is invalid"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::NoRedirect,
            cbIos
            ),
        bl::ArgumentException,
        "validateProcessRedirectFlags: the callback parameter is invalid"
        );

    /*
     * The positive controls - the file callback is accepted wherever the ios one is
     */

    UTF_REQUIRE_NO_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdout |
                bl::os::ProcessCreateFlags::RedirectStderr |
                bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
            cbIos
            )
        );

    UTF_REQUIRE_NO_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdout |
                bl::os::ProcessCreateFlags::RedirectStderr |
                bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
            bl::os::process_redirect_callback_ios_t(),
            cbFile
            )
        );

    UTF_REQUIRE_NO_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags(
            bl::os::ProcessCreateFlags::RedirectStdin | bl::os::ProcessCreateFlags::CloseStdin,
            cbIos
            )
        );

    UTF_REQUIRE_NO_THROW(
        bl::os::detail::OSImplBase::validateProcessRedirectFlags( bl::os::ProcessCreateFlags::NoRedirect )
        );

    /*
     * ... and one end to end assertion through the public API, so the wiring of the gate
     * into createProcess( ... ) is covered too
     */

    UTF_REQUIRE_THROW(
        bl::os::createProcess( "true", bl::os::ProcessCreateFlags::RedirectStdout ),
        bl::ArgumentException
        );
}

/************************************************************************
 * os::createProcess merged output with the FILE* callback tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessMergedWithFileCallbackTests )
{
    if( bl::os::onWindows() && test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * TODO: CreateProcess is triggering some weird application verifier
         * breaks in apphelp.dll which do not appear to be issue with the code
         *
         * The breaks also happen only if we turn on the unaligned flag on
         * page heap
         */

        return;
    }

    /*
     * RedirectStdout | RedirectStderr | MergeStdoutAndStderr with a
     * process_redirect_callback_file_t and no ios callback is a legal combination -
     * validateProcessRedirectFlags( ... ) accepts either callback - but the merge branch
     * of both createProcess( ... ) implementations used to assert on the ios stream,
     * which is only constructed when the ios callback is supplied
     */

    std::string merged;

    const bl::os::process_redirect_callback_file_t cbFile = [ &merged ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::FILE*                      outFile,
        SAA_in_opt          std::FILE*                      errFile,
        SAA_in_opt          std::FILE*                      inFile
        ) -> void
    {
        UTF_REQUIRE( process );
        UTF_REQUIRE( outFile );

        /*
         * There is no separate standard error pipe in the merged case and the standard
         * input was not redirected at all
         */

        UTF_REQUIRE( ! errFile );
        UTF_REQUIRE( ! inFile );

        const auto out = bl::os::fileptr2istream( outFile );

        std::string line;

        while( std::getline( *out, line ) )
        {
            merged += line;
            merged += '\n';
        }
    };

    const auto proc = bl::os::createProcess(
        bl::os::onWindows() ?
            std::vector< std::string >{ "cmd.exe", "/c", "echo out & echo err 1>&2" }
            :
            std::vector< std::string >{ "bash", "-c", "echo out; echo err 1>&2" },
        bl::os::ProcessCreateFlags::RedirectStdout |
            bl::os::ProcessCreateFlags::RedirectStderr |
            bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
        bl::os::process_redirect_callback_ios_t(),
        cbFile
        );

    UTF_REQUIRE( proc );

    /*
     * The standard error of the child really was merged into the standard output pipe
     */

    UTF_CHECK( std::string::npos != merged.find( "out" ) );
    UTF_CHECK( std::string::npos != merged.find( "err" ) );

    UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
}

/************************************************************************
 * fs::SafeInputFileStreamWrapper seek tests
 */

UTF_AUTO_TEST_CASE( FsUtils_SafeFileStreamWrapperSeekTests )
{
    /*
     * The file stream wrappers are backed by os::detail::stdio_file_device_base, whose
     * seek( ... ) maps beg / cur / end onto SEEK_SET / SEEK_CUR / SEEK_END, uses the 64
     * bit seek primitives (_fseeki64 / fseeko rather than std::fseek, whose long offset
     * is 32 bit on Windows) and returns the position it reads back from the stream
     */

    bl::fs::TmpDir tmpDir;

    const auto filePath = tmpDir.path() / "seek-test.txt";

    const std::string content( "abcdefghijklmnopqrstuvwxyz" );

    {
        bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );

        outputFile.stream() << content;

        outputFile.flushAndCheck();
    }

    bl::fs::SafeInputFileStreamWrapper inputFile( filePath );

    auto& is = inputFile.stream();

    /*
     * std::ios_base::end at offset zero must land on the end of the file
     */

    is.seekg( 0, std::ios::end );

    UTF_REQUIRE_EQUAL( 26, static_cast< std::int64_t >( is.tellg() ) );

    /*
     * ... and with a negative offset it must land that many bytes before it
     */

    is.seekg( -3, std::ios::end );

    {
        char buffer[ 3 ] = { 0, 0, 0 };

        is.read( buffer, 3 );

        UTF_REQUIRE( ! is.fail() );
        UTF_REQUIRE_EQUAL( std::string( "xyz" ), std::string( buffer, 3 ) );
    }

    is.seekg( 0, std::ios::beg );

    {
        char buffer[ 5 ] = { 0, 0, 0, 0, 0 };

        is.read( buffer, 5 );

        UTF_REQUIRE( ! is.fail() );
        UTF_REQUIRE_EQUAL( std::string( "abcde" ), std::string( buffer, 5 ) );
    }

    /*
     * std::ios_base::cur in both directions; the position is pinned as well, not just
     * the side effect of the seek
     */

    is.seekg( 5, std::ios::cur );

    UTF_REQUIRE_EQUAL( 10, static_cast< std::int64_t >( is.tellg() ) );

    UTF_REQUIRE_EQUAL( 'k', static_cast< char >( is.get() ) );

    is.seekg( -2, std::ios::cur );

    UTF_REQUIRE_EQUAL( 'j', static_cast< char >( is.get() ) );

    /*
     * Reading to the end of the file sets the EOF indicator of the underlying stdio
     * stream, which makes stdio_file_source::read( ... ) return -1 immediately; a seek
     * must clear it, otherwise every wrapper which is read twice returns nothing the
     * second time
     */

    const std::string rest(
        ( std::istreambuf_iterator< char >( is ) ),
        std::istreambuf_iterator< char >()
        );

    UTF_REQUIRE_EQUAL( std::string( "klmnopqrstuvwxyz" ), rest );

    is.clear();
    is.seekg( 0, std::ios::beg );

    UTF_REQUIRE( is.good() );

    UTF_REQUIRE_EQUAL( 'a', static_cast< char >( is.get() ) );
}

/************************************************************************
 * os::RobustNamedMutex robustness tests
 */

namespace
{

#if ! defined( _WIN32 )

    /*
     * The key of the System V semaphore which backs a named mutex is the CRC-32 of its
     * name - see RobustNamedMutex::semOpenOrCreate in OSImplUNIX.h
     */

    ::key_t namedMutexSemaphoreKey( SAA_in const std::string& name )
    {
        bl::cs::crc_32_type crcc;

        crcc.process_bytes( name.c_str(), name.size() );

        return static_cast< ::key_t >( crcc.checksum() );
    }

    void removeNamedMutexSemaphoreNothrow( SAA_in const std::string& name )
    {
        const int semid = ::semget( namedMutexSemaphoreKey( name ), 0, 0 );

        if( -1 != semid )
        {
            ( void ) ::semctl( semid, 0, IPC_RMID );
        }
    }

    void noopSignalHandler( SAA_in int signalId )
    {
        BL_UNUSED( signalId );
    }

#endif // ! defined( _WIN32 )

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_NamedMutexRobustnessTests )
{
    /*
     * BaseLib_NamedMutexTests is left in place as the smoke test - it takes a lock and
     * releases it without a single assertion, so it cannot detect any regression; this
     * case is the one which pins the contract of RobustNamedMutex
     *
     * Both names are unique, so unlike BaseLib_NamedMutexTests this case can remove the
     * objects it creates instead of leaking one per machine forever
     */

    const auto name = "BL-Test-Mutex-" + bl::uuids::uuid2string( bl::uuids::create() );
    const auto nameOther = "BL-Test-Mutex-" + bl::uuids::uuid2string( bl::uuids::create() );

#if ! defined( _WIN32 )

    BL_SCOPE_EXIT(
        {
            removeNamedMutexSemaphoreNothrow( name );
            removeNamedMutexSemaphoreNothrow( nameOther );
        }
        );

    /*
     * A no-op SIGUSR1 handler installed *without* SA_RESTART, so that a signal delivered
     * to the worker thread while it is blocked in semop( ... ) makes that call fail with
     * EINTR - the retry loop in RobustNamedMutex::semOp is what has to absorb it, and
     * every asio based service installs signal handlers of its own
     */

    struct ::sigaction oldSa;
    struct ::sigaction sa;

    std::memset( &oldSa, 0, sizeof( oldSa ) );
    std::memset( &sa, 0, sizeof( sa ) );

    sa.sa_handler = &noopSignalHandler;
    sa.sa_flags = 0;

    UTF_REQUIRE_EQUAL( 0, ::sigemptyset( &sa.sa_mask ) );
    UTF_REQUIRE_EQUAL( 0, ::sigaction( SIGUSR1, &sa, &oldSa ) );

    BL_SCOPE_EXIT(
        {
            ( void ) ::sigaction( SIGUSR1, &oldSa, nullptr );
        }
        );

#endif // ! defined( _WIN32 )

    bl::os::RobustNamedMutex mutex( name );

    /*
     * The worker thread only touches atomics
     */

    std::atomic< bool > acquired( false );
    std::atomic< bool > failed( false );

    bl::os::thread worker;

    /*
     * This guard is declared before the scope which holds the lock, so the thread is
     * joined only after the lock has been released - also on the exception path
     */

    BL_SCOPE_EXIT(
        {
            bl::os::safeThreadJoin( worker );
        }
        );

    {
        bl::os::ipc::scoped_lock< bl::os::RobustNamedMutex > guard( mutex );

        UTF_REQUIRE( guard.owns() );

        worker = bl::os::thread(
            [ &acquired, &failed, &name ]() -> void
            {
                try
                {
                    /*
                     * A second instance of the same named object - the exclusion is
                     * between the underlying named objects, not between the C++ ones
                     */

                    bl::os::RobustNamedMutex second( name );

                    bl::os::ipc::scoped_lock< bl::os::RobustNamedMutex > secondGuard( second );

                    acquired = true;
                }
                catch( std::exception& )
                {
                    failed = true;
                }
            }
            );

#if ! defined( _WIN32 )

        for( std::size_t i = 0U; i < 20U; ++i )
        {
            ( void ) ::pthread_kill( worker.native_handle(), SIGUSR1 );

            bl::os::sleep( bl::time::milliseconds( 20 ) );
        }

#else // ! defined( _WIN32 )

        bl::os::sleep( bl::time::milliseconds( 400 ) );

#endif // ! defined( _WIN32 )

        /*
         * The lock is still held here, so the worker cannot have acquired it - this is
         * the mutual exclusion assertion itself
         *
         * On UNIX the ~20 interrupting SIGUSR1 deliveries above must not have surfaced
         * as an error either - that is the EINTR restart assertion
         */

        UTF_REQUIRE( ! acquired );
        UTF_REQUIRE( ! failed );

        /*
         * A distinct name must map to a distinct object, so it can be locked while the
         * first one is held
         */

        {
            bl::os::RobustNamedMutex mutexOther( nameOther );

            bl::os::ipc::scoped_lock< bl::os::RobustNamedMutex > guardOther( mutexOther );

            UTF_REQUIRE( guardOther.owns() );
        }
    }

    bl::os::safeThreadJoin( worker );

    UTF_REQUIRE( acquired );
    UTF_REQUIRE( ! failed );

#if ! defined( _WIN32 )

    /*
     * The semaphore must be owner only - a world writable one can be incremented by any
     * local user, which both breaks the exclusion and allows a denial of service on it
     */

    {
        const int semid = ::semget( namedMutexSemaphoreKey( name ), 0, 0 );

        UTF_REQUIRE( -1 != semid );

        struct ::semid_ds ds;

        std::memset( &ds, 0, sizeof( ds ) );

        /*
         * union semun is not declared by glibc, so it has to be declared here
         */

        union
        {
            int                                 val;
            struct ::semid_ds*                  buf;
            unsigned short*                     array;
        }
        arg;

        arg.buf = &ds;

        UTF_REQUIRE_EQUAL( 0, ::semctl( semid, 0, IPC_STAT, arg ) );

        UTF_CHECK_EQUAL( 0600, static_cast< int >( ds.sem_perm.mode & 0777 ) );
    }

#else // ! defined( _WIN32 )

    /*
     * The Windows mutex is thread owned, so unlock( ) from a thread which does not own it
     * must fail the ReleaseMutex check rather than silently succeed
     *
     * Note that the WAIT_FAILED branch of lock( ) cannot be asserted - the check there is
     * inverted, so a correct assertion cannot pass against the current code
     */

    {
        bl::os::RobustNamedMutex mutexNotOwned( name );

        UTF_REQUIRE_THROW( mutexNotOwned.unlock(), bl::SystemException );
    }

#endif // ! defined( _WIN32 )
}

/************************************************************************
 * os::createProcess argv quoting tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessArgvQuotingWindowsTests )
{
    /*
     * This case is deliberately NOT enabled - it is a regression test for a production
     * defect which has not been fixed, not a characterisation of the current behavior
     *
     * The command line assembly of the std::vector< std::string > overload of
     * createProcess( ... ) in OSImplWindows.h quotes an argument only when it contains a
     * space and then escapes *every* backslash inside it; per CommandLineToArgvW a
     * backslash is only special when it immediately precedes a '"', so doubling all of
     * them corrupts every Windows path argument which contains a space - and bl::fs::path
     * produces \\?\ prefixed paths where duplicate separators are not collapsed by the
     * kernel, which makes the corruption fatal rather than cosmetic
     *
     * An argument which contains '"' but no space is emitted completely raw and an empty
     * argument produces nothing at all, shifting every later argv index
     *
     * Enabling the assertions below requires the quoting to follow the CommandLineToArgvW
     * rule first: double backslashes only when they immediately precede a '"' or the
     * closing quote, and always quote an empty argument as ""
     *
     * The batch harness below also terminates its loop on the first empty argument, so
     * the empty slot assertion additionally needs a child which can echo an empty
     * argument back - that is a second reason the case is not enabled
     */

    const bool productionArgvQuotingIsFixed = false;

    if( ! productionArgvQuotingIsFixed || ! bl::os::onWindows() )
    {
        return;
    }

    bl::fs::TmpDir tmpDir;

    const auto batPath = tmpDir.path() / "echo-argv.bat";

    {
        bl::fs::SafeOutputFileStreamWrapper outputFile( batPath );

        auto& os = outputFile.stream();

        os << "@echo off" << std::endl;
        os << ":loop" << std::endl;
        os << "if \"%~1\"==\"\" goto :eof" << std::endl;
        os << "echo %~1" << std::endl;
        os << "shift" << std::endl;
        os << "goto loop" << std::endl;

        outputFile.flushAndCheck();
    }

    const std::vector< std::string > arguments
    {
        "plain",
        "with space",
        "C:\\Program Files\\x",
        "trailing\\",
        "quote\"inside",
        "",
        "a\\\\b",
    };

    std::vector< std::string > commandArguments;

    commandArguments.push_back( batPath.string() );

    for( std::size_t i = 0U; i < arguments.size(); ++i )
    {
        commandArguments.push_back( arguments[ i ] );
    }

    std::vector< std::string > lines;

    const bl::os::process_redirect_callback_ios_t cbIos = [ &lines ](
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

        std::string line;

        while( std::getline( *out, line ) )
        {
            /*
             * cmd.exe terminates its lines with CRLF
             */

            if( ! line.empty() && '\r' == line[ line.size() - 1U ] )
            {
                line.erase( line.size() - 1U );
            }

            lines.push_back( line );
        }
    };

    const auto proc = bl::os::createProcess(
        commandArguments,
        bl::os::ProcessCreateFlags::RedirectStdout,
        cbIos
        );

    UTF_REQUIRE( proc );

    /*
     * An empty argument must still occupy its own slot
     */

    UTF_REQUIRE_EQUAL( arguments.size(), lines.size() );

    /*
     * ... and every argument must come back byte for byte, in particular the path with a
     * space in it (no doubled backslashes) and the one with a trailing backslash
     */

    for( std::size_t i = 0U; i < arguments.size(); ++i )
    {
        UTF_CHECK_EQUAL( arguments[ i ], lines[ i ] );
    }

    UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
}

#if ! defined( _WIN32 )

/************************************************************************
 * os::< process termination status > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSProcessTerminationStatusTests )
{
    /*
     * A negative exit code means 'killed by signal N' - it is the only way a caller can
     * tell 'exited 9' apart from 'killed by SIGKILL' - and a stopped child must never be
     * reported as terminated
     */

    {
        const auto proc = bl::os::createProcess( std::vector< std::string >{ "bash", "-c", "exit 3" } );

        UTF_REQUIRE( proc );

        UTF_REQUIRE_EQUAL( 3, bl::os::tryAwaitTermination( proc ) );
    }

    {
        const auto proc = bl::os::createProcess( "sleep 300" );

        UTF_REQUIRE( proc );

        bl::os::sendSignal( proc.get(), SIGTERM, false /* includeSubprocesses */ );

        int exitCode = 0;

        UTF_REQUIRE( bl::os::tryTimedAwaitTermination( proc, &exitCode, 10000 /* timeoutMs */ ) );

        UTF_REQUIRE_EQUAL( -SIGTERM, exitCode );
    }

    {
        const auto proc = bl::os::createProcess( "sleep 300" );

        UTF_REQUIRE( proc );

        int exitCode = 0;

        UTF_REQUIRE(
            bl::os::sendProcessStopEventAndWait(
                proc,
                &exitCode,
                false                   /* includeSubprocesses */,
                10000                   /* timeoutMs */
                )
            );

        UTF_REQUIRE_EQUAL( -SIGINT, exitCode );

        /*
         * Unlike sendSignal( ... ) and terminateProcess( ... ), which return silently
         * once the handle has been waited on, sendProcessStopEvent( ... ) throws - the
         * pid of such a handle is zero and ::kill( 0, SIGINT ) would deliver SIGINT to
         * the process group of the caller, i.e. to the test process itself
         */

        UTF_REQUIRE_THROW(
            bl::os::sendProcessStopEvent( proc.get(), false /* includeSubprocesses */ ),
            bl::UnexpectedException
            );

        /*
         * ... whereas sendSignal( ... ) on the very same handle stays a silent no-op; if
         * that regresses the test process is terminated by the SIGTERM below
         */

        UTF_REQUIRE_NO_THROW(
            bl::os::sendSignal( proc.get(), SIGTERM, false /* includeSubprocesses */ )
            );

        UTF_REQUIRE_NO_THROW(
            bl::os::sendSignal( proc.get(), SIGTERM, true /* includeSubprocesses */ )
            );
    }

    {
        const auto proc = bl::os::createProcess( "sleep 300" );

        UTF_REQUIRE( proc );

        /*
         * getPid( ... ) returns zero once the child has been waited on, so the pid has
         * to be captured before any wait
         */

        const auto pid = static_cast< ::pid_t >( bl::os::getPid( proc ) );

        UTF_REQUIRE( 0 < pid );

        UTF_REQUIRE_EQUAL( 0, ::kill( pid, SIGSTOP ) );

        /*
         * WCONTINUED | WUNTRACED make waitpid report a stopped or continued child; the
         * wait loop must keep waiting in that case rather than report a termination with
         * a garbage exit code
         */

        UTF_REQUIRE( false == bl::os::tryTimedAwaitTermination( proc, nullptr, 500 /* timeoutMs */ ) );

        UTF_REQUIRE_EQUAL( 0, ::kill( pid, SIGCONT ) );

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

        UTF_REQUIRE_EQUAL( -SIGKILL, exitCode );
    }
}

/************************************************************************
 * os::createProcess non-detached handle release tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessHandleReleaseTests )
{
    /*
     * Releasing the handle of a running non-detached child - which is the default for
     * every createProcess( ... ) call in the library - must terminate it and reap it,
     * and it must be bounded in time (the destructor waits 2s, sends SIGTERM, waits
     * another 2s and only then sends SIGKILL)
     */

    {
        std::uint64_t pid = 0U;

        const auto start = bl::time::microsec_clock::universal_time();

        {
            const auto proc = bl::os::createProcess( "sleep 300" );

            UTF_REQUIRE( proc );

            pid = bl::os::getPid( proc );

            UTF_REQUIRE( pid );
        }

        const auto elapsed = bl::time::microsec_clock::universal_time() - start;

        UTF_CHECK( elapsed < bl::time::seconds( 10 ) );

        /*
         * The child was reaped by the destructor, so no zombie is left behind; note that
         * this waits on the specific pid and never on -1, so it cannot steal the status
         * of an unrelated child
         */

        int status = 0;

        const auto waitRc = ::waitpid( static_cast< ::pid_t >( pid ), &status, WNOHANG );
        const auto waitErrno = errno;

        UTF_REQUIRE_EQUAL( -1, waitRc );
        UTF_REQUIRE_EQUAL( ECHILD, waitErrno );

        const auto killRc = ::kill( static_cast< ::pid_t >( pid ), 0 );
        const auto killErrno = errno;

        UTF_CHECK( -1 == killRc && ESRCH == killErrno );
    }

    /*
     * The contrast - a detached child is documented as *not* terminated when its handle
     * is released
     */

    {
        std::uint64_t pid = 0U;

        {
            const auto proc = bl::os::createProcess(
                "sleep 300",
                bl::os::ProcessCreateFlags::DetachProcess
                );

            UTF_REQUIRE( proc );

            pid = bl::os::getPid( proc );

            UTF_REQUIRE( pid );
        }

        UTF_CHECK_EQUAL( 0, ::kill( static_cast< ::pid_t >( pid ), 0 ) );

        UTF_REQUIRE_EQUAL( 0, ::kill( static_cast< ::pid_t >( pid ), SIGKILL ) );
    }
}

/************************************************************************
 * os::createProcess child standard input EPIPE tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSChildStdinEpipeTests )
{
    /*
     * The primary assertion of this case is that it completes at all
     *
     * Writing into the standard input of a child which has already exited must not kill
     * the test process with SIGPIPE and must not abort it through the BL_RIP_MSG in
     * closeChildStdinNothrow( ... ), which tolerates EPIPE from the fclose of the write
     * end of a pipe whose reader is gone; that RIP is a crash of the whole test binary,
     * so nothing after it could assert anything
     *
     * OS.h documents ignoring SIGPIPE as part of the contract of these APIs, so the case
     * ignores it for its own duration and restores the previous disposition afterwards -
     * leaving it ignored would change the behavior of every later case in the module
     */

    struct ::sigaction oldSa;
    struct ::sigaction sa;

    std::memset( &oldSa, 0, sizeof( oldSa ) );
    std::memset( &sa, 0, sizeof( sa ) );

    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;

    UTF_REQUIRE_EQUAL( 0, ::sigemptyset( &sa.sa_mask ) );
    UTF_REQUIRE_EQUAL( 0, ::sigaction( SIGPIPE, &sa, &oldSa ) );

    BL_SCOPE_EXIT(
        {
            ( void ) ::sigaction( SIGPIPE, &oldSa, nullptr );
        }
        );

    int exitCode = -1;
    bool reachedEndOfCallback = false;
    bool writeReportedFailure = false;

    {
        /*
         * Everything has to happen inside the callback - the redirected streams are
         * closed by cbCloseObjects( ... ) as soon as it returns
         */

        const bl::os::process_redirect_callback_ios_t cbIos = [ & ](
            SAA_in              const bl::os::process_handle_t  process,
            SAA_in_opt          std::istream*                   out,
            SAA_in_opt          std::istream*                   err,
            SAA_in_opt          std::ostream*                   in
            ) -> void
        {
            UTF_REQUIRE( process );
            UTF_REQUIRE( ! out );
            UTF_REQUIRE( ! err );
            UTF_REQUIRE( in );

            /*
             * Let the child exit before anything is written into its standard input, so
             * the pipe has no reader left at all
             */

            UTF_REQUIRE( bl::os::tryTimedAwaitTermination( process, &exitCode, 10000 /* timeoutMs */ ) );

            /*
             * 1 MiB is comfortably above the usual 64 KiB capacity of a Linux pipe, so
             * at least one write really reaches a pipe with no reader
             *
             * The write either succeeds or reports an ordinary stream failure; note that
             * there is no catch( ... ) here on purpose - anything which is not an
             * std::exception escapes and fails the case
             */

            const std::string payload( 1024U * 1024U, 'x' );

            try
            {
                ( *in ) << payload;

                in -> flush();

                writeReportedFailure = in -> fail();
            }
            catch( std::exception& e )
            {
                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Writing into the standard input of an exited child has failed: "
                        << e.what()
                    );

                writeReportedFailure = true;
            }

            reachedEndOfCallback = true;
        };

        const auto proc = bl::os::createProcess(
            std::vector< std::string >{ "bash", "-c", "exit 0" },
            bl::os::ProcessCreateFlags::RedirectStdin,
            cbIos
            );

        UTF_REQUIRE( proc );
    }

    UTF_REQUIRE( reachedEndOfCallback );

    UTF_CHECK_EQUAL( 0, exitCode );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "The write into the standard input of an exited child "
            << ( writeReportedFailure ? "reported a failure" : "succeeded" )
        );

    /*
     * The parent survived both the closing of the stream and the write itself
     */

    const auto proc = bl::os::createProcess( "true" );

    UTF_REQUIRE( proc );

    UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
}

#endif // ! defined( _WIN32 )
