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

#if defined( _WIN32 )

/*
 * ::CommandLineToArgvW is the documented parser the argv quoting rules are defined by and
 * is used as the oracle of the quoting case below; neither <shellapi.h> nor the import
 * library it lives in is pulled in by any of the baselib headers
 */

#include <shellapi.h>

#pragma comment( lib, "Shell32.lib" )

#endif // defined( _WIN32 )

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

    /*
     * The parentheses around ::sigemptyset are required and must not be removed
     *
     * On macOS <signal.h> declares sigemptyset() as a function but then shadows it with a
     * function-like macro, so a plain ::sigemptyset( ... ) would expand to ::(*(&sa.sa_mask) = 0, 0)
     * and fail to compile with 'expected unqualified-id'; wrapping the name in parentheses
     * suppresses the macro expansion and calls the real POSIX function on all platforms
     */

    UTF_REQUIRE_EQUAL( 0, ( ::sigemptyset )( &sa.sa_mask ) );
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
    if( ! bl::os::onWindows() )
    {
        return;
    }

    #if defined( _WIN32 )

    /*
     * The std::vector< std::string > overload of createProcess( ... ) joins its arguments
     * into one command line which the child's CRT - or anything else which follows the
     * documented rules - takes apart again with CommandLineToArgvW. The quoting is
     * therefore only correct if that round trip is the identity, and the assembly helper
     * is a public static which can be driven without spawning anything, the same way
     * BaseLib_OSShellEscapeForRunAsUserTests drives the UNIX twin
     *
     * Using CommandLineToArgvW itself as the oracle is deliberate: it is the parser the
     * rule is defined by, so the test cannot encode the same mistake twice. A batch file
     * harness cannot play that role - cmd.exe applies its own, different parsing, and its
     * shift loop cannot even represent an empty argument
     */

    const auto roundTrip = []( SAA_in const std::vector< std::string >& arguments ) -> std::vector< std::string >
    {
        bl::cpp::SafeOutputStringStream commandLine;

        /*
         * The FIRST token of a command line is the program name and CommandLineToArgvW
         * parses it by different rules - it ends at the first space unless it is quoted and
         * a backslash is never an escape inside it - so a program name is always put in
         * front here and the comparison starts after it. createProcess( ) has the same
         * shape: the first element of its vector is the executable
         */

        commandLine << "prog";

        for( std::size_t i = 0U; i < arguments.size(); ++i )
        {
            commandLine << ' ';

            bl::os::detail::OS::appendQuotedArgument( commandLine, arguments[ i ] );
        }

        bl::cpp::wstring_convert_t conv;

        const auto wcommandLine = conv.from_bytes( commandLine.str() );

        int count = 0;

        const auto argv = ::CommandLineToArgvW( wcommandLine.c_str(), &count );

        UTF_REQUIRE( argv );

        BL_SCOPE_EXIT( ::LocalFree( argv ); );

        UTF_REQUIRE( count >= 1 );

        UTF_REQUIRE_EQUAL( std::string( "prog" ), conv.to_bytes( argv[ 0 ] ) );

        std::vector< std::string > result;

        for( int i = 1; i < count; ++i )
        {
            result.push_back( conv.to_bytes( argv[ i ] ) );
        }

        return result;
    };

    /*
     * Every one of these fails against the pre-fix assembly, which quoted an argument
     * only when it contained a space and then doubled every backslash inside it:
     *
     *  - the empty argument produced nothing at all, so it did not occupy a slot and
     *    every later index shifted down by one
     *  - the argument containing a tab was not quoted, so the child split it in two
     *  - the argument containing a quote but no space was emitted raw, so the quote was
     *    consumed as a delimiter and the argument merged with its neighbour
     *  - the Program Files path was quoted and came back with doubled backslashes
     *  - a trailing backslash inside quotes escaped the closing quote itself
     */

    const std::vector< std::string > arguments
    {
        "plain",
        "with space",
        "C:\\Program Files\\x",
        "trailing\\",
        "quote\"inside",
        "",
        "a\\\\b",
        "tab\there",
        "\"fully quoted\"",
        "a\\\\\"b",
    };

    const auto parsed = roundTrip( arguments );

    UTF_REQUIRE_EQUAL( arguments.size(), parsed.size() );

    for( std::size_t i = 0U; i < arguments.size(); ++i )
    {
        UTF_CHECK_EQUAL( arguments[ i ], parsed[ i ] );
    }

    /*
     * Each argument on its own as well, so a failure names the one which broke rather
     * than only the aggregate, and a single empty argument still yields exactly one slot
     */

    for( std::size_t i = 0U; i < arguments.size(); ++i )
    {
        const std::vector< std::string > single{ arguments[ i ] };

        const auto singleParsed = roundTrip( single );

        UTF_REQUIRE_EQUAL( 1U, singleParsed.size() );
        UTF_CHECK_EQUAL( arguments[ i ], singleParsed[ 0 ] );
    }

    /*
     * An argument which needs no quoting must not acquire any - the command line stays
     * readable and, more importantly, an executable name is still recognised
     */

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "plain" );

        UTF_CHECK_EQUAL( std::string( "plain" ), commandLine.str() );
    }

    /*
     * The exact texts the documented algorithm is specified to produce, so a future
     * rewrite which still round trips but by some other encoding is noticed
     */

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "" );

        UTF_CHECK_EQUAL( std::string( "\"\"" ), commandLine.str() );
    }

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "C:\\Program Files\\x" );

        /*
         * The backslashes are not followed by a quote, so they stay single; only a run
         * which meets the closing quote is doubled - and there is none here
         */

        UTF_CHECK_EQUAL( std::string( "\"C:\\Program Files\\x\"" ), commandLine.str() );
    }

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "trailing\\" );

        /*
         * A trailing backslash is NOT by itself a reason to quote - a backslash is only
         * special next to a quote - so this argument is emitted verbatim
         */

        UTF_CHECK_EQUAL( std::string( "trailing\\" ), commandLine.str() );
    }

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "C:\\dir x\\" );

        /*
         * Here the space forces the quotes and the trailing backslash then does meet the
         * closing quote, so that run is doubled (2n) while the interior one is left alone.
         * The pre-fix code doubled both
         */

        UTF_CHECK_EQUAL( std::string( "\"C:\\dir x\\\\\"" ), commandLine.str() );
    }

    {
        bl::cpp::SafeOutputStringStream commandLine;

        bl::os::detail::OS::appendQuotedArgument( commandLine, "a\\\\\"b" );

        /*
         * Two backslashes followed by a literal quote become 2n + 1 = five backslashes
         * and then the escaped quote
         */

        UTF_CHECK_EQUAL( std::string( "\"a\\\\\\\\\\\"b\"" ), commandLine.str() );
    }

    /*
     * And finally the end to end path - a real child, spawned through the vector
     * overload, echoing back the arguments a batch harness can represent. This proves
     * the assembly above is what actually reaches CreateProcessW
     */

    if( test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * See the note in BaseLib_OSCreateProcessTests about CreateProcess and the
         * application verifier
         */

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

    /*
     * No empty argument and no embedded quote here - the harness above terminates on the
     * first empty one and cmd.exe's own parsing of a quote is not CommandLineToArgvW's.
     * What this half is for is the pair which the pre-fix code corrupted on the real
     * path: a path with single backslashes and a space, and one ending in a backslash
     */

    const std::vector< std::string > echoed
    {
        "plain",
        "with space",
        "C:\\Program Files\\x",
        "C:\\dir x\\f",
    };

    std::vector< std::string > commandArguments;

    /*
     * ::CreateProcessW runs executables only - it does not hand a .bat to the command
     * interpreter the way ::ShellExecute would - so cmd.exe is named explicitly. Note that
     * this makes cmd.exe, not the CRT, the thing which splits the arguments again, and
     * cmd.exe's rules are its own; that is why the authoritative half of this case is the
     * CommandLineToArgvW round trip above and why the list below carries neither an empty
     * argument nor an embedded quote
     */

    commandArguments.push_back( "cmd.exe" );
    commandArguments.push_back( "/c" );

    /*
     * The long file name prefix has to come off the batch path. fs::TmpDir hands out a
     * prefixed path on Windows - fs::path adds it through WinLfnUtils::chk2AddPrefix, and
     * makeHidden( ) keeps it - and while cmd.exe resolves a \\?\ path perfectly well in,
     * say, 'if exist', it refuses to EXECUTE a program named by one and answers 'The system
     * cannot find the path specified'. That is a property of cmd.exe and nothing to do with
     * the quoting under test, so it is removed here rather than worked around
     */

    {
        auto batPathText = batPath.string();

        const std::string lfnPrefix( "\\\\?\\" );

        if( 0U == batPathText.find( lfnPrefix ) )
        {
            batPathText.erase( 0U, lfnPrefix.size() );
        }

        commandArguments.push_back( batPathText );
    }

    for( std::size_t i = 0U; i < echoed.size(); ++i )
    {
        commandArguments.push_back( echoed[ i ] );
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

    /*
     * The assembled command line is logged, and stderr is merged into the stream which is
     * read, so that a failure here names the reason instead of only reporting that nothing
     * arrived - cmd.exe reports 'cannot find the path' and the like on stderr
     */

    {
        bl::cpp::SafeOutputStringStream assembled;

        for( std::size_t i = 0U; i < commandArguments.size(); ++i )
        {
            if( i )
            {
                assembled << ' ';
            }

            bl::os::detail::OS::appendQuotedArgument( assembled, commandArguments[ i ] );
        }

        UTF_MESSAGE(
            BL_MSG()
                << "Assembled command line is '"
                << assembled.str()
                << "'"
            );
    }

    const auto proc = bl::os::createProcess(
        commandArguments,
        bl::os::ProcessCreateFlags::RedirectStdout |
            bl::os::ProcessCreateFlags::RedirectStderr |
            bl::os::ProcessCreateFlags::MergeStdoutAndStderr,
        cbIos
        );

    UTF_REQUIRE( proc );

    for( std::size_t i = 0U; i < lines.size(); ++i )
    {
        UTF_MESSAGE(
            BL_MSG()
                << "Child line ["
                << i
                << "] is '"
                << lines[ i ]
                << "'"
            );
    }

    UTF_REQUIRE_EQUAL( echoed.size(), lines.size() );

    for( std::size_t i = 0U; i < echoed.size(); ++i )
    {
        UTF_CHECK_EQUAL( echoed[ i ], lines[ i ] );
    }

    UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

    #endif // defined( _WIN32 )
}

#if defined( _WIN32 )

/************************************************************************
 * os::RobustNamedMutex non-ASCII name tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSNamedMutexNonAsciiNameWindowsTests )
{
    /*
     * The mutex name reaches ::CreateMutexW, so it has to be converted from UTF-8 rather
     * than byte-widened: a byte-widened name is a DIFFERENT kernel object name, so two
     * processes which agree on the UTF-8 name would each create their own mutex and both
     * would acquire it - the mutual exclusion would be silently gone
     *
     * The oracle is the kernel namespace itself: the name is converted here with the
     * wide-string converter the rest of the tests use and ::OpenMutexW is asked for that
     * exact name. Before the fix the object existed under the mojibake name only, so the
     * open failed with ERROR_FILE_NOT_FOUND
     */

    const std::string name =
        "Global\\swblocks-baselib-utf-\xC3\xA9-" + bl::uuids::uuid2string( bl::uuids::create() );

    bl::cpp::wstring_convert_t conv;

    const auto wname = conv.from_bytes( name );

    {
        bl::os::RobustNamedMutex mutex( name );

        const auto opened = ::OpenMutexW( SYNCHRONIZE, FALSE /* bInheritHandle */, wname.c_str() );

        const auto lastError = ::GetLastError();

        if( NULL == opened )
        {
            /*
             * ERROR_FILE_NOT_FOUND (2) here is the pre-fix behaviour - the object exists
             * under the byte-widened name instead
             */

            UTF_MESSAGE(
                BL_MSG()
                    << "OpenMutexW failed for the converted name with error "
                    << lastError
                );
        }

        UTF_REQUIRE( NULL != opened );

        UTF_REQUIRE_EQUAL( TRUE, ::CloseHandle( opened ) );

        /*
         * ... and the mutex is usable, i.e. the name round trip did not produce some
         * object which merely happens to exist
         */

        mutex.lock();
        mutex.unlock();
    }

    /*
     * Once the last handle is gone the name is gone with it
     */

    const auto reopened = ::OpenMutexW( SYNCHRONIZE, FALSE /* bInheritHandle */, wname.c_str() );

    UTF_CHECK( NULL == reopened );

    if( reopened )
    {
        ::CloseHandle( reopened );
    }
}

/************************************************************************
 * os::setWindowsPathPermissions non-ASCII path tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSPathPermissionsNonAsciiWindowsTests )
{
    /*
     * setWindowsPathPermissions( ... ) used to narrow the already wide native path to a
     * std::string and then byte-widen it back before handing it to
     * ::GetNamedSecurityInfoW, so for any path containing a non-ASCII character the API
     * was asked about a path which does not exist and the call failed with
     * ERROR_FILE_NOT_FOUND. An empty group name is passed so that only the owner entry is
     * built and no account lookup is involved
     */

    bl::fs::TmpDir tmpDir;

    const auto nonAsciiDir = tmpDir.path() / "perm-\xC3\xA9-dir";

    bl::fs::safeMkdirs( nonAsciiDir );

    const auto filePath = nonAsciiDir / "file.txt";

    {
        const auto file = bl::os::fopen( filePath, "wb" );
    }

    UTF_REQUIRE( bl::fs::path_exists( filePath ) );

    UTF_REQUIRE_NO_THROW(
        bl::os::setWindowsPathPermissions(
            filePath,
            bl::fs::perms::owner_all,
            bl::str::empty()                /* groupName */,
            bl::fs::perms::group_all
            )
        );

    /*
     * The owner must still be able to open it, which is the whole point of granting
     * GENERIC_ALL to the owner
     */

    {
        const auto file = bl::os::fopen( filePath, "rb" );
    }
}

/************************************************************************
 * os::createProcess non-ASCII command line tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessNonAsciiCommandLineWindowsTests )
{
    if( test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * See the note in BaseLib_OSCreateProcessTests about CreateProcess and the
         * application verifier
         */

        return;
    }

    /*
     * The command line overload used to widen the UTF-8 text byte by byte with std::copy,
     * so every non-ASCII byte reached ::CreateProcessW as mojibake
     *
     * The non-ASCII text is carried in a PATH which the child is asked to test for
     * existence, and the child answers with a pure ASCII word. Two things are deliberate
     * about that shape: nothing non-ASCII is ever printed, so the OEM code page of the
     * console cannot influence the result, and cmd.exe receives the path as part of the
     * wide command line and tests it through the wide API, so the only thing which decides
     * the answer is whether the conversion in createProcess( ) was correct
     *
     * Before the fix cmd.exe was handed the byte-widened path, which names nothing, and the
     * child printed nothing
     */

    bl::fs::TmpDir tmpDir;

    const auto nonAsciiDir = tmpDir.path() / "cmdline-\xC3\xA9-dir";

    bl::fs::safeMkdirs( nonAsciiDir );

    const auto probePath = nonAsciiDir / "probe.txt";

    {
        const auto file = bl::os::fopen( probePath, "wb" );
    }

    UTF_REQUIRE( bl::fs::path_exists( probePath ) );

    bl::cpp::SafeOutputStringStream commandLine;

    commandLine
        << "cmd.exe /c if exist \""
        << probePath.string()
        << "\" echo FOUND";

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
            if( ! line.empty() && '\r' == line[ line.size() - 1U ] )
            {
                line.erase( line.size() - 1U );
            }

            if( ! line.empty() )
            {
                lines.push_back( line );
            }
        }
    };

    const auto proc = bl::os::createProcess(
        commandLine.str(),
        bl::os::ProcessCreateFlags::RedirectStdout,
        cbIos
        );

    UTF_REQUIRE( proc );

    UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

    UTF_REQUIRE_EQUAL( 1U, lines.size() );
    UTF_CHECK_EQUAL( std::string( "FOUND" ), lines[ 0 ] );

    /*
     * The negative control - the same child asked about a path which really does not
     * exist prints nothing, so the assertion above is not satisfied by some unrelated
     * output of cmd.exe
     */

    {
        bl::cpp::SafeOutputStringStream missingCommandLine;

        missingCommandLine
            << "cmd.exe /c if exist \""
            << ( nonAsciiDir / "no-such-probe.txt" ).string()
            << "\" echo FOUND";

        lines.clear();

        const auto missingProc = bl::os::createProcess(
            missingCommandLine.str(),
            bl::os::ProcessCreateFlags::RedirectStdout,
            cbIos
            );

        UTF_REQUIRE( missingProc );

        UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( missingProc ) );

        UTF_CHECK( lines.empty() );
    }
}

/************************************************************************
 * os::tryGetRegistryValue hive diagnostics tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSRegistryHiveDiagnosticsWindowsTests )
{
    /*
     * Two defects of tryGetRegistryValue( ... ) are covered here
     *
     * The first is the diagnostic text: 'location' used to be computed as
     * 'HKEY_CURRENT_USER ? ... : ...', i.e. it tested a non-null CONSTANT instead of the
     * currentUser argument, so every HKEY_LOCAL_MACHINE failure named the wrong hive and
     * sent the reader of a support ticket to the wrong place
     *
     * HKEY_LOCAL_MACHINE\SECURITY is the deterministic way to reach that diagnostic
     * without administrator rights and without assuming anything about the content of
     * this machine's registry: it is readable by SYSTEM only, so ::RegOpenKeyExW returns
     * ERROR_ACCESS_DENIED rather than ERROR_FILE_NOT_FOUND - which is returned as 'not
     * found' instead of thrown - for an administrator just as much as for a plain user
     *
     * The second is the handle lifetime: the HKEY used to be attached to its RAII holder
     * BEFORE the open was checked, so a failing open handed an indeterminate handle to
     * ::RegCloseKey inside a NOEXCEPT deleter. This case drives both failing paths - the
     * access-denied one which throws and the not-found one which returns - and the
     * 'not found' assertion below is the one which would have RIPped the process
     */

    /*
     * SystemException composes its what( ) from the API name and the system error text, so
     * the BL_MSG( ) text of the check is carried by errinfo_message instead and has to be
     * read through BaseException::message( ) - UTF_REQUIRE_THROW_MESSAGE matches what( )
     * and would never see it
     */

    const auto openFailureMessage = []( SAA_in const std::string& key, SAA_in const bool currentUser ) -> std::string
    {
        try
        {
            bl::os::tryGetRegistryValue( key, "anything", currentUser );
        }
        catch( bl::SystemException& e )
        {
            const auto* message = e.message();

            UTF_REQUIRE( message );

            /*
             * The error code must be the one RegOpenKeyExW RETURNED. It reports through its
             * return value and is not documented to set the last error at all, so building
             * the code from ::GetLastError() - which is what the shared createException( )
             * overload does - attached whatever unrelated call had set it last. Measured
             * before the fix: ERROR_INVALID_HANDLE (6), where the real cause is
             * ERROR_ACCESS_DENIED (5)
             */

            const auto* errorCode = e.errorCode();

            UTF_REQUIRE( errorCode );

            UTF_MESSAGE(
                BL_MSG()
                    << "tryGetRegistryValue failed with error code "
                    << errorCode -> value()
                    << " ("
                    << errorCode -> message()
                    << ")"
                );

            UTF_CHECK_EQUAL( ( int ) ERROR_ACCESS_DENIED, errorCode -> value() );

            return *message;
        }

        UTF_FAIL( "tryGetRegistryValue must throw for a key which cannot be opened" );

        return std::string();
    };

    UTF_CHECK_EQUAL(
        std::string( "Cannot open registry key HKEY_LOCAL_MACHINE\\SECURITY" ),
        openFailureMessage( "SECURITY", false /* currentUser */ )
        );

    /*
     * The other direction, and the other of the two places the same 'location' text is
     * used - the value read. A REG_SZ longer than the fixed WCHAR buffer[ 1024 ] makes
     * ::RegGetValueW return ERROR_MORE_DATA, which is a throw rather than a 'not found',
     * and the message must then name HKEY_CURRENT_USER
     *
     * Pinning both directions matters because the defect was an expression which happened
     * to yield the right answer for one of the two hives
     */

    {
        const std::string keyName =
            "Software\\swblocks-baselib-utf-hive-" + bl::uuids::uuid2string( bl::uuids::create() );

        const std::string valueName = "long-value";

        bl::cpp::wstring_convert_t conv;

        const auto wkeyName = conv.from_bytes( keyName );
        const auto wvalueName = conv.from_bytes( valueName );

        HKEY hkey = nullptr;

        UTF_REQUIRE_EQUAL(
            ERROR_SUCCESS,
            ::RegCreateKeyExW(
                HKEY_CURRENT_USER               /* hKey */,
                wkeyName.c_str()                /* lpSubKey */,
                0                               /* Reserved */,
                nullptr                         /* lpClass */,
                REG_OPTION_VOLATILE             /* dwOptions */,
                KEY_WRITE                       /* samDesired */,
                nullptr                         /* lpSecurityAttributes */,
                &hkey                           /* phkResult */,
                nullptr                         /* lpdwDisposition */
                )
            );

        BL_SCOPE_EXIT(
            {
                ::RegCloseKey( hkey );
                ::RegDeleteKeyW( HKEY_CURRENT_USER, wkeyName.c_str() );
            }
            );

        const std::wstring wlongData( 2000U, L'x' );

        UTF_REQUIRE_EQUAL(
            ERROR_SUCCESS,
            ::RegSetValueExW(
                hkey                                                                    /* hKey */,
                wvalueName.c_str()                                                      /* lpValueName */,
                0                                                                       /* Reserved */,
                REG_SZ                                                                  /* dwType */,
                reinterpret_cast< const BYTE* >( wlongData.c_str() )                     /* lpData */,
                static_cast< DWORD >( ( wlongData.size() + 1 ) * sizeof( wchar_t ) )     /* cbData */
                )
            );

        std::string valueFailureMessage;

        try
        {
            bl::os::tryGetRegistryValue( keyName, valueName, true /* currentUser */ );

            UTF_FAIL( "tryGetRegistryValue must throw for a value longer than its buffer" );
        }
        catch( bl::SystemException& e )
        {
            const auto* message = e.message();

            UTF_REQUIRE( message );

            valueFailureMessage = *message;
        }

        UTF_CHECK_EQUAL(
            "Cannot open registry value HKEY_CURRENT_USER\\" + keyName + "\\" + valueName,
            valueFailureMessage
            );
    }

    /*
     * A key which does not exist is 'not found' under either hive rather than an error -
     * and this is the path on which the handle used to be attached before it was known to
     * be valid
     */

    UTF_REQUIRE(
        nullptr == bl::os::tryGetRegistryValue(
            "Software\\swblocks-baselib-utf-no-such-key-" + bl::uuids::uuid2string( bl::uuids::create() ),
            "anything",
            true /* currentUser */
            )
        );

    UTF_REQUIRE(
        nullptr == bl::os::tryGetRegistryValue(
            "Software\\swblocks-baselib-utf-no-such-key-" + bl::uuids::uuid2string( bl::uuids::create() ),
            "anything",
            false /* currentUser */
            )
        );

    /*
     * A key which exists but under which the value does not must also be 'not found'
     * rather than an error, and it must not leak the key handle either
     */

    for( std::size_t i = 0U; i < 200U; ++i )
    {
        UTF_REQUIRE(
            nullptr == bl::os::tryGetRegistryValue(
                "Environment",
                "swblocks-baselib-utf-no-such-value",
                true /* currentUser */
                )
            );
    }
}

/************************************************************************
 * os::createProcess thread handle ownership tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessThreadHandleLeakWindowsTests )
{
    if( test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * See the note in BaseLib_OSCreateProcessTests about CreateProcess and the
         * application verifier
         */

        return;
    }

    /*
     * ::CreateProcessW hands the caller a handle to the child's primary thread on every
     * path and the caller owns it. It used to be attached to a holder only inside the
     * 'assignNewJob' branch, so a spawn which was already in a job, or a detached one
     * which skips that branch altogether, leaked one thread handle per call
     *
     * A detached spawn is used here because it is the form which takes neither branch and
     * therefore leaked unconditionally. The parent's own handle count is the observable:
     * before the fix it grew by one per spawn
     */

    const auto handleCount = []() -> DWORD
    {
        DWORD count = 0;

        UTF_REQUIRE( ::GetProcessHandleCount( ::GetCurrentProcess(), &count ) );

        return count;
    };

    const std::size_t spawns = 50U;

    /*
     * One spawn first, so that whatever one-off allocation the first call through this
     * path makes is not counted as growth
     */

    {
        const auto proc = bl::os::createProcess(
            "cmd.exe /c exit 0",
            bl::os::ProcessCreateFlags::DetachProcess
            );

        UTF_REQUIRE( proc );
        UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
    }

    const auto before = handleCount();

    for( std::size_t i = 0U; i < spawns; ++i )
    {
        const auto proc = bl::os::createProcess(
            "cmd.exe /c exit 0",
            bl::os::ProcessCreateFlags::DetachProcess
            );

        UTF_REQUIRE( proc );
        UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
    }

    const auto after = handleCount();

    /*
     * The assertion is on the absence of growth PROPORTIONAL to the spawn count, not on
     * an exactly equal count - the process is multi-threaded and unrelated handles may
     * legitimately come and go while this runs. A leak of one per spawn is 50; a slack of
     * a handful absorbs the noise without absorbing the defect
     */

    UTF_MESSAGE(
        BL_MSG()
            << "Parent handle count before "
            << before
            << " and after "
            << spawns
            << " detached spawns "
            << after
        );

    UTF_REQUIRE( after < before + ( spawns / 2U ) );
}

/************************************************************************
 * os::createJunction print name tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSJunctionPrintNameWindowsTests )
{
    /*
     * A mount point reparse point carries two names: the substitute name, which is what
     * the kernel follows, and the print name, which is what Windows displays - 'dir',
     * Explorer and 'fsutil reparsepoint query' all read the print name
     *
     * createJunction( ) ensured the trailing backslash of the substitute name only inside
     * the branch which strips the long file name prefix, yet it computed the print name as
     * 'the substitute name minus its last character' unconditionally. For a plain absolute
     * target - which has no trailing backslash to drop - that removed a real character of
     * the path, so the junction worked but displayed a truncated target
     *
     * os::getJunctionTarget( ) reads the SUBSTITUTE name, which is why this was invisible
     * to BaseLib_OSJunctionsTests; the print name is read directly here
     */

    /*
     * Only the mount point form is needed. The layout is the documented
     * REPARSE_DATA_BUFFER one and is declared locally because the production copy is
     * private to the OS implementation
     */

    struct MountPointReparseData
    {
        ULONG       ReparseTag;
        USHORT      ReparseDataLength;
        USHORT      Reserved;
        USHORT      SubstituteNameOffset;
        USHORT      SubstituteNameLength;
        USHORT      PrintNameOffset;
        USHORT      PrintNameLength;
        WCHAR       PathBuffer[ 1 ];
    };

    const auto readNames = []( SAA_in const bl::fs::path& junction ) -> std::pair< std::wstring, std::wstring >
    {
        const auto handle = ::CreateFileW(
            junction.native().c_str()                                           /* lpFileName */,
            FILE_READ_EA                                                        /* dwDesiredAccess */,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE               /* dwShareMode */,
            NULL                                                                /* lpSecurityAttributes */,
            OPEN_EXISTING                                                       /* dwCreationDisposition */,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT            /* dwFlagsAndAttributes */,
            NULL                                                                /* hTemplateFile */
            );

        UTF_REQUIRE( INVALID_HANDLE_VALUE != handle );

        BL_SCOPE_EXIT( ::CloseHandle( handle ); );

        std::vector< BYTE > buffer( MAXIMUM_REPARSE_DATA_BUFFER_SIZE, ( BYTE ) 0 );

        DWORD returned = 0;

        UTF_REQUIRE(
            ::DeviceIoControl(
                handle                              /* hDevice */,
                FSCTL_GET_REPARSE_POINT             /* dwIoControlCode */,
                NULL                                /* lpInBuffer */,
                0                                   /* nInBufferSize */,
                &buffer[ 0 ]                        /* lpOutBuffer */,
                ( DWORD ) buffer.size()             /* nOutBufferSize */,
                &returned                           /* lpBytesReturned */,
                NULL                                /* lpOverlapped */
                )
            );

        const auto* data = reinterpret_cast< const MountPointReparseData* >( &buffer[ 0 ] );

        UTF_REQUIRE_EQUAL( ( ULONG ) IO_REPARSE_TAG_MOUNT_POINT, data -> ReparseTag );

        const std::wstring substituteName(
            data -> PathBuffer + ( data -> SubstituteNameOffset / sizeof( WCHAR ) ),
            data -> SubstituteNameLength / sizeof( WCHAR )
            );

        const std::wstring printName(
            data -> PathBuffer + ( data -> PrintNameOffset / sizeof( WCHAR ) ),
            data -> PrintNameLength / sizeof( WCHAR )
            );

        return std::make_pair( substituteName, printName );
    };

    bl::fs::TmpDir tmpDir;

    const auto subDir = tmpDir.path() / "print-name-target";
    const auto junctionDir = tmpDir.path() / "print-name-junction";

    bl::fs::safeMkdirs( subDir );
    bl::fs::safeMkdirs( junctionDir );

    bl::os::createJunction( subDir, junctionDir );

    UTF_REQUIRE( bl::os::isJunction( junctionDir ) );

    const auto names = readNames( junctionDir );

    /*
     * The expectation is the target path with the long file name prefix removed if it
     * carried one, so the case is correct for both spellings of the same path
     */

    const std::wstring lfnPrefix( L"\\\\?\\" );

    auto expectedPrintName = subDir.native();

    if( 0U == expectedPrintName.find( lfnPrefix ) )
    {
        expectedPrintName.erase( expectedPrintName.begin(), expectedPrintName.begin() + lfnPrefix.size() );
    }

    UTF_MESSAGE(
        BL_MSG()
            << "Junction substitute name is '"
            << bl::cpp::wstring_convert_t().to_bytes( names.first )
            << "' and print name is '"
            << bl::cpp::wstring_convert_t().to_bytes( names.second )
            << "'"
        );

    UTF_CHECK_EQUAL(
        bl::cpp::wstring_convert_t().to_bytes( expectedPrintName ),
        bl::cpp::wstring_convert_t().to_bytes( names.second )
        );

    /*
     * The substitute name is the same text with the no-parse prefix in front and a
     * trailing backslash, which is the form ::mklink /J produces
     */

    UTF_CHECK_EQUAL(
        bl::cpp::wstring_convert_t().to_bytes( L"\\??\\" + expectedPrintName + L"\\" ),
        bl::cpp::wstring_convert_t().to_bytes( names.first )
        );

    /*
     * And the junction still resolves, i.e. none of the above traded a readable print
     * name for a broken link
     */

    const auto filePath = subDir / "file.txt";

    {
        const auto file = bl::os::fopen( filePath, "wb" );
    }

    UTF_REQUIRE( bl::fs::path_exists( junctionDir / "file.txt" ) );

    UTF_REQUIRE_EQUAL( subDir, bl::os::getJunctionTarget( junctionDir ) );

    bl::os::deleteJunction( junctionDir );

    /*
     * A relative target cannot work - the reparse point stores the text verbatim, so
     * whoever follows the link resolves it against the volume root rather than against
     * the current directory of whoever created it - and must be rejected rather than
     * producing a junction which silently points somewhere else
     */

    UTF_REQUIRE_THROW(
        bl::os::createJunction( bl::fs::path( "relative-target" ), junctionDir ),
        bl::ArgumentException
        );

    UTF_REQUIRE( ! bl::os::isJunction( junctionDir ) );
}

/************************************************************************
 * os::< resource limits and private file creation > tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSResourceLimitsWindowsTests )
{
    /*
     * The Windows halves of these three were written on a Linux host as the counterparts
     * of UNIX code which is tested there, and compile only here
     */

    /*
     * getPhysicalMemorySize( ) returns ullTotalPhys from ::GlobalMemoryStatusEx and zero
     * on failure. A real machine has at least 256 MB and less than 64 TB, which is wide
     * enough to be true of anything this builds on and narrow enough to catch a zero, a
     * byte/kilobyte mix-up or an uninitialised dwLength
     */

    const auto physicalMemory = bl::os::getPhysicalMemorySize();

    UTF_MESSAGE(
        BL_MSG()
            << "Physical memory size is "
            << physicalMemory
            << " bytes ("
            << ( physicalMemory / ( 1024ULL * 1024ULL ) )
            << " MB)"
        );

    UTF_REQUIRE( 0U != physicalMemory );
    UTF_REQUIRE( physicalMemory > 256ULL * 1024ULL * 1024ULL );
    UTF_REQUIRE( physicalMemory < 64ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL );

    /*
     * getFileDescriptorSoftLimit( ) returns zero meaning 'not applicable' - Windows bounds
     * the handle count by available kernel memory rather than by a per-process soft limit.
     * Zero is the documented value its consumers test for, so it is pinned here
     */

    UTF_REQUIRE_EQUAL( 0U, bl::os::getFileDescriptorSoftLimit() );

    /*
     * createNewFilePrivate( ) is CREATE_NEW with dwShareMode zero: it must report true
     * exactly once for a given path and false for a path which already exists
     *
     * NOTE - the UNIX implementation also makes the file unreadable by other local users
     * (mode 0600); the Windows one does NOT deliver that property, because a Windows file
     * inherits the DACL of its directory and no explicit security descriptor is passed.
     * This was a deliberate decision - see the S-5 entry of
     * notes/reviews/major/update_2026/whole-library-cxx-review-fable51-decisions.md - so
     * the case asserts the creation semantics only, which is all the Windows side promises
     */

    bl::fs::TmpDir tmpDir;

    const auto privatePath = tmpDir.path() / "private-file.txt";

    UTF_REQUIRE( ! bl::fs::path_exists( privatePath ) );

    UTF_REQUIRE( bl::os::createNewFilePrivate( privatePath ) );
    UTF_REQUIRE( bl::fs::path_exists( privatePath ) );

    UTF_REQUIRE( ! bl::os::createNewFilePrivate( privatePath ) );

    /*
     * A path whose parent does not exist is a failure, not an exception
     */

    UTF_REQUIRE( ! bl::os::createNewFilePrivate( tmpDir.path() / "no-such-dir" / "f.txt" ) );

    /*
     * RobustNamedMutex gained a permissions parameter for signature parity with the UNIX
     * side, where it made the SysV semaphore 0600. The Windows implementation ignores it
     * and defaultPermissions( ) is zero; both spellings of the constructor must work
     */

    UTF_REQUIRE_EQUAL( 0, bl::os::RobustNamedMutex::defaultPermissions() );

    {
        const auto name =
            "Global\\swblocks-baselib-utf-perm-" + bl::uuids::uuid2string( bl::uuids::create() );

        bl::os::RobustNamedMutex mutex( name, bl::os::RobustNamedMutex::defaultPermissions() );

        mutex.lock();
        mutex.unlock();
    }
}


/************************************************************************
 * os::tryGetUserDomain environment contract tests (Windows only)
 */

UTF_AUTO_TEST_CASE( BaseLib_OSUserDomainEnvironmentWindowsTests )
{
    /*
     * tryGetUserDomain( ) is a pure function of three environment variables and keeps no
     * state, so every leg of its contract can be driven directly by setting them
     *
     * This is what makes the USERDOMAIN leg testable on a host which is not domain joined.
     * BaseLib_GetUserDomainTests compares the implementation against an oracle derived
     * from the same environment, which is the right shape, but on a local account -
     * USERDNSDOMAIN unset and USERDOMAIN equal to COMPUTERNAME - the expected answer is
     * the empty string, and the implementation returned the empty string BEFORE the fix as
     * well. That case therefore cannot tell a fixed implementation from a broken one
     *
     * The defect: the fall-back block read tryGetEnvironmentVariable( "USERDNSDOMAIN" ) a
     * second time where it meant "USERDOMAIN". Since the first block already returns
     * whenever USERDNSDOMAIN is set, the variable in the fall-back was ALWAYS null, so the
     * function returned empty for every machine whose domain is carried only in USERDOMAIN
     * and the computerName == userDomain comparison was unreachable dead code. The third
     * block below is the one which pins it
     */

    const auto savedDnsDomain = bl::os::tryGetEnvironmentVariable( "USERDNSDOMAIN" );
    const auto savedUserDomain = bl::os::tryGetEnvironmentVariable( "USERDOMAIN" );
    const auto savedComputerName = bl::os::tryGetEnvironmentVariable( "COMPUTERNAME" );

    const auto restore = [](
        SAA_in      const std::string&          name,
        SAA_in      const bl::os::string_ptr&   saved
        ) -> void
    {
        if( saved )
        {
            bl::os::setEnvironmentVariable( name, *saved );
        }
        else
        {
            bl::os::unsetEnvironmentVariable( name );
        }
    };

    BL_SCOPE_EXIT(
        {
            restore( "USERDNSDOMAIN", savedDnsDomain );
            restore( "USERDOMAIN", savedUserDomain );
            restore( "COMPUTERNAME", savedComputerName );
        }
        );

    bl::os::setEnvironmentVariable( "COMPUTERNAME", "UTF-HOST" );

    /*
     * USERDNSDOMAIN wins whenever it is set, whatever the other two say
     */

    bl::os::setEnvironmentVariable( "USERDNSDOMAIN", "utf.example.com" );
    bl::os::setEnvironmentVariable( "USERDOMAIN", "UTF-DOMAIN" );

    UTF_CHECK_EQUAL( std::string( "utf.example.com" ), bl::os::tryGetUserDomain() );
    UTF_CHECK_EQUAL( std::string( "utf.example.com" ), bl::os::getUserDomain() );

    /*
     * ... even when it is the only one set
     */

    bl::os::unsetEnvironmentVariable( "USERDOMAIN" );

    UTF_CHECK_EQUAL( std::string( "utf.example.com" ), bl::os::tryGetUserDomain() );

    /*
     * THE DISCRIMINATING CASE - USERDNSDOMAIN unset and USERDOMAIN naming a domain which
     * is not the computer name. Before the fix this returned the empty string
     */

    bl::os::unsetEnvironmentVariable( "USERDNSDOMAIN" );
    bl::os::setEnvironmentVariable( "USERDOMAIN", "UTF-DOMAIN" );

    UTF_CHECK_EQUAL( std::string( "UTF-DOMAIN" ), bl::os::tryGetUserDomain() );
    UTF_CHECK_EQUAL( std::string( "UTF-DOMAIN" ), bl::os::getUserDomain() );

    /*
     * A local account - USERDOMAIN is merely the computer name, so there is no domain.
     * This is the state of the host this runs on and the reason the existing case passes
     * either way
     */

    bl::os::setEnvironmentVariable( "USERDOMAIN", "UTF-HOST" );

    UTF_CHECK( bl::os::tryGetUserDomain().empty() );

    UTF_CHECK_THROW_MESSAGE(
        bl::os::getUserDomain(),
        bl::NotSupportedException,
        "User domain name is not available"
        );

    /*
     * Neither variable set is 'no domain' as well
     *
     * The implementation also guards against an EMPTY USERDOMAIN, which cannot be reached
     * from here: os::setEnvironmentVariable( ) refuses an empty value - putenvWrapper( )
     * validates it and fails with EINVAL - so that guard stays covered by inspection only
     */

    bl::os::unsetEnvironmentVariable( "USERDOMAIN" );

    UTF_CHECK( bl::os::tryGetUserDomain().empty() );

    UTF_CHECK_THROW(
        bl::os::setEnvironmentVariable( "USERDOMAIN", bl::str::empty() ),
        bl::SystemException
        );

    /*
     * And with no COMPUTERNAME to compare against, a USERDOMAIN is taken at face value -
     * the comparison is guarded on the computer name being present
     */

    bl::os::unsetEnvironmentVariable( "COMPUTERNAME" );
    bl::os::setEnvironmentVariable( "USERDOMAIN", "UTF-HOST" );

    UTF_CHECK_EQUAL( std::string( "UTF-HOST" ), bl::os::tryGetUserDomain() );
}
#endif // defined( _WIN32 )

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

    /*
     * The parentheses around ::sigemptyset are required and must not be removed
     * (see the note on the other ::sigemptyset call site above)
     */

    UTF_REQUIRE_EQUAL( 0, ( ::sigemptyset )( &sa.sa_mask ) );
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
