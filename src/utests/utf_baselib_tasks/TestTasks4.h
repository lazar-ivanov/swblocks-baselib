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

#include <baselib/tasks/utils/DirectoryScannerControlToken.h>
#include <baselib/tasks/utils/ExcludedPathsControlToken.h>
#include <baselib/tasks/utils/Pinger.h>
#include <baselib/tasks/utils/ScanDirectoryTask.h>
#include <baselib/tasks/utils/ShutdownTask.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/TasksUtils.h>
#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/ExecutionQueueImpl.h>

#include <baselib/core/BoxedObjects.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/Logging.h>
#include <baselib/core/NumberUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if ! defined( _WIN32 )
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif // ! defined( _WIN32 )

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfConcurrent.h>
#include <utests/baselib/TestFsUtils.h>

/************************************************************************
 * Tasks library code tests (continued)
 *
 * Note that this header is included after TestTasks.h in the same translation unit,
 * so the file local helpers declared in the anonymous namespace of TestTasks.h - in
 * particular ExecutionQueueCompletionControl and createControlledCompletionTask - are
 * visible here and are deliberately reused rather than duplicated
 */

namespace
{
    /**
     * @brief Creates an execution queue which is owned by a std::shared_ptr, so it can be
     * captured safely by a worker thread which may have to be detached
     */

    std::shared_ptr< bl::om::ObjPtrDisposable< bl::tasks::ExecutionQueue > > createSharedQueue(
        SAA_in          const unsigned                                              options
        )
    {
        return std::make_shared< bl::om::ObjPtrDisposable< bl::tasks::ExecutionQueue > >(
            bl::tasks::ExecutionQueueImpl::createInstance< bl::tasks::ExecutionQueue >( options )
            );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ExecutionQueuePopWaitOnDrainedQueueTest )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * ExecutionQueueImplT::getFirstReady() only waits when there is something in the queue
     * to wait for - 'wait && m_ready.empty() && false == isEmptyInternal()' - and its wait
     * predicate also returns as soon as the queue drains
     *
     * Both of these are what keeps every consumer loop in the library from hanging, and
     * neither is exercised anywhere else in the suite, so they are pinned here with an
     * explicitly bounded handshake
     */

    {
        /*
         * (1) A wholly empty queue - pop( true ) and top( true ) must both return nullptr
         * without ever blocking
         */

        const auto eq = createSharedQueue( ExecutionQueue::OptionKeepNone );

        const auto popResult = std::make_shared< om::ObjPtr< Task > >();
        const auto popReturned = std::make_shared< utest::TestSignal >();

        os::thread popThread(
            [ eq, popResult, popReturned ]() -> void
            {
                *popResult = ( *eq ) -> pop( true /* wait */ );

                popReturned -> signal();
            }
            );

        const bool popReturnedInTime = popReturned -> wait();

        /*
         * A regression makes the blocking call never return, so joining it would hang the
         * test process - the failure path detaches it instead and every object the thread
         * captured is owned by a std::shared_ptr and therefore stays alive
         */

        if( popReturnedInTime )
        {
            popThread.join();
        }
        else
        {
            popThread.detach();
        }

        const auto topResult = std::make_shared< om::ObjPtr< Task > >();
        const auto topReturned = std::make_shared< utest::TestSignal >();

        os::thread topThread(
            [ eq, topResult, topReturned ]() -> void
            {
                *topResult = ( *eq ) -> top( true /* wait */ );

                topReturned -> signal();
            }
            );

        const bool topReturnedInTime = topReturned -> wait();

        if( topReturnedInTime )
        {
            topThread.join();
        }
        else
        {
            topThread.detach();
        }

        const bool queueStillEmpty = ( *eq ) -> isEmpty();

        UTF_REQUIRE( popReturnedInTime );
        UTF_REQUIRE( ! *popResult );
        UTF_REQUIRE( topReturnedInTime );
        UTF_REQUIRE( ! *topResult );
        UTF_REQUIRE( queueStillEmpty );
    }

    {
        /*
         * (2) A queue which holds nothing but a task that will never be retained - the
         * blocking pop must be released when the queue drains and must return nullptr
         *
         * Note that it is deliberately not asserted that the thread is still blocked before
         * the completion is delivered - that cannot be done race free
         */

        const auto eq = createSharedQueue( ExecutionQueue::OptionKeepNone );
        const auto control = std::make_shared< ExecutionQueueCompletionControl >();

        ( *eq ) -> push_back( om::qi< Task >( createControlledCompletionTask( *control ) ) );

        const bool taskWasScheduled = control -> waitUntilScheduled( 1U );

        const auto popResult = std::make_shared< om::ObjPtr< Task > >();
        const auto popReturned = std::make_shared< utest::TestSignal >();

        os::thread popThread(
            [ eq, popResult, popReturned ]() -> void
            {
                *popResult = ( *eq ) -> pop( true /* wait */ );

                popReturned -> signal();
            }
            );

        const bool completionSucceeded = control -> completeNext();

        const bool popReturnedInTime = popReturned -> wait();

        if( popReturnedInTime )
        {
            popThread.join();
        }
        else
        {
            popThread.detach();
        }

        const bool queueDrained = ( *eq ) -> isEmpty();

        UTF_REQUIRE( taskWasScheduled );
        UTF_REQUIRE( completionSucceeded );
        UTF_REQUIRE( popReturnedInTime );
        UTF_REQUIRE( ! *popResult );
        UTF_REQUIRE( queueDrained );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ProcessPingerInvalidHostTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * The host name is the last argv element of the ping command line, so an empty host or
     * one which starts with '-' would be parsed as an option by the ping tool
     *
     * The guard fires before os::createRedirectedProcessMergeOutputAndWait( ... ), so no
     * process is spawned - which is also why no MachineGlobalTestLock is needed here
     */

    const auto cbTestInvalidHost = []( SAA_in std::string&& host ) -> void
    {
        const auto pinger = ProcessPingerTaskImpl::createInstance< PingerTask >(
            BL_PARAM_FWD( host ),
            1000 /* timeoutInMilliseconds */
            );

        scheduleAndExecuteInParallel(
            [ &pinger ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                /*
                 * The failed task must not be retained, as otherwise the flush which
                 * scheduleAndExecuteInParallel( ... ) performs on the way out would rethrow
                 * the very exception this case is about to assert on
                 */

                eq -> setOptions( ExecutionQueue::OptionKeepNone );

                eq -> push_back( om::qi< Task >( pinger ) );

                eq -> flushNoThrowIfFailed();
            }
            );

        UTF_REQUIRE( pinger -> isFailed() );
        UTF_REQUIRE( pinger -> isUnreachable() );
        UTF_REQUIRE( ! pinger -> isReachable() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( pinger -> exception() ),
            bl::UnexpectedException,
            "Invalid host name"
            );
    };

    cbTestInvalidHost( std::string( "-c" ) );
    cbTestInvalidHost( std::string() );
    cbTestInvalidHost( std::string( "-f" ) );
}

namespace
{
    /**
     * @brief A directory scanner control token whose entry budget, entry filter and error
     * policy are all configurable, so the branches of ScanDirectoryTaskT::onExecute() which
     * utest::ScanningControlT can never reach become testable
     *
     * Note that isEntryAllowed is not NOEXCEPT (unlike isErrorAllowed), so it can throw and
     * fail the scan part way through
     */

    template
    <
        typename E = void
    >
    class ConfigurableScanningControlT : public bl::tasks::DirectoryScannerControlToken
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( ConfigurableScanningControlT, bl::tasks::DirectoryScannerControlToken )

    public:

        typedef bl::cpp::function< bool ( SAA_in const bl::fs::path& path ) >           entry_filter_t;

    protected:

        std::atomic< int >                                                              m_allowedEntries;
        entry_filter_t                                                                  m_entryFilter;
        bl::cpp::ScalarTypeIniter< bool >                                               m_isErrorAllowed;

        ConfigurableScanningControlT()
            :
            m_allowedEntries( std::numeric_limits< int >::max() )
        {
        }

    public:

        void setEntryBudget( SAA_in const int allowedEntries ) NOEXCEPT
        {
            m_allowedEntries = allowedEntries;
        }

        void setEntryFilter( SAA_in const entry_filter_t& entryFilter )
        {
            m_entryFilter = entryFilter;
        }

        void setErrorAllowed( SAA_in const bool isErrorAllowed ) NOEXCEPT
        {
            m_isErrorAllowed = isErrorAllowed;
        }

        virtual bool isCanceled() const NOEXCEPT OVERRIDE
        {
            return false;
        }

        virtual bool isErrorAllowed( SAA_in const bl::eh::error_code& code ) const NOEXCEPT OVERRIDE
        {
            BL_UNUSED( code );

            return m_isErrorAllowed;
        }

        virtual bool isEntryAllowed( SAA_in const bl::fs::directory_entry& entry ) OVERRIDE
        {
            if( m_entryFilter && ! m_entryFilter( entry.path() ) )
            {
                return false;
            }

            if( --m_allowedEntries < 0 )
            {
                BL_THROW(
                    bl::UnexpectedException(),
                    BL_MSG()
                        << "The entry budget of the scanning control token is exhausted at "
                        << entry.path()
                    );
            }

            return true;
        }
    };

    typedef bl::om::ObjectImpl< ConfigurableScanningControlT<> > ConfigurableScanningControlImpl;

    /**
     * @brief Pushes a single seed scanner for 'root' into an OptionKeepAll queue, drains it
     * with pop( true ) and hands every popped scanner to the caller supplied callback
     *
     * @return The number of scanner tasks which came out of the queue
     */

    std::size_t drainDirectoryScan(
        SAA_in          const bl::fs::path&                                             root,
        SAA_in_opt      const bl::om::ObjPtr< bl::tasks::DirectoryScannerControlToken >& cbControl,
        SAA_in          const bl::cpp::function
                            <
                                void ( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& task )
                            >&                                                          cbTask,
        SAA_inout       bool&                                                           queueWasEmptyAfterDrain
        )
    {
        using namespace bl;
        using namespace bl::tasks;

        std::size_t poppedCount = 0U;

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                {
                    const auto boxedRootPath = bo::path::createInstance();
                    boxedRootPath -> swapValue( fs::path( root ) );

                    eq -> push_back(
                        om::qi< Task >(
                            ScanDirectoryTaskImpl::createInstance( root, boxedRootPath, cbControl )
                            )
                        );
                }

                for( ;; )
                {
                    const auto scannerTask = eq -> pop( true /* wait */ );

                    if( ! scannerTask )
                    {
                        break;
                    }

                    ++poppedCount;

                    cbTask( scannerTask );
                }

                /*
                 * A child scanner which a regression schedules from the failure path is
                 * pushed by the parent's own handler *after* the parent has been notified
                 * ready, so the drain loop above can win the race and see an empty queue
                 *
                 * A short bounded settle followed by a flush closes that window, so the
                 * emptiness sampled below is a real assertion and not a coin toss
                 */

                os::sleep( time::milliseconds( 250 ) );

                eq -> flushNoThrowIfFailed();

                queueWasEmptyAfterDrain = eq -> isEmpty();

                eq -> flush(
                    false /* discardPending */,
                    true  /* nothrowIfFailed */,
                    true  /* discardReady */,
                    false /* cancelExecuting */
                    );
            }
            );

        return poppedCount;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_ScanDirectoryTaskFailureTests )
{
    using namespace bl;
    using namespace bl::tasks;

    {
        /*
         * A scan which fails part way through must not schedule child scanners for the
         * directories it had already collected - the parent has been notified ready with an
         * exception by then, so anything pushed after that lands on a queue nobody is
         * waiting on any longer
         */

        fs::TmpDir tmpDir;

        for( std::size_t i = 0U; i < 5U; ++i )
        {
            fs::safeCreateDirectory( tmpDir.path() / ( "d" + std::to_string( i ) ) );
        }

        const auto token = ConfigurableScanningControlImpl::createInstance();

        token -> setEntryBudget( 1 );

        std::vector< om::ObjPtr< Task > > popped;
        bool queueWasEmptyAfterDrain = false;

        const auto poppedCount = drainDirectoryScan(
            tmpDir.path(),
            om::qi< DirectoryScannerControlToken >( token ),
            [ &popped ]( SAA_in const om::ObjPtr< Task >& task ) -> void
            {
                popped.push_back( om::copy( task ) );
            },
            queueWasEmptyAfterDrain
            );

        UTF_REQUIRE_EQUAL( 1U, poppedCount );
        UTF_REQUIRE_EQUAL( 1U, popped.size() );
        UTF_REQUIRE( popped[ 0 ] -> isFailed() );

        /*
         * The one entry which was allowed had already been recorded when the token threw
         */

        UTF_REQUIRE_EQUAL(
            1U,
            om::qi< ScanDirectoryTaskImpl >( popped[ 0 ] ) -> entries().size()
            );

        /*
         * This is the assertion which pins the scanSucceeded early return
         */

        UTF_REQUIRE( queueWasEmptyAfterDrain );
    }

#if ! defined( _WIN32 )

    {
        /*
         * Without a control token the default policy rejects anything which is not a
         * regular file, a directory or a symlink - a FIFO is the cheapest way to reach it
         */

        fs::TmpDir tmpDir;

        fs::safeCreateDirectory( tmpDir.path() / "aDirectory" );

        const auto fifoPath = tmpDir.path() / "aFifo";

        UTF_REQUIRE_EQUAL( 0, ::mkfifo( fifoPath.string().c_str(), 0600 ) );

        std::vector< om::ObjPtr< Task > > popped;
        bool queueWasEmptyAfterDrain = false;

        const auto poppedCount = drainDirectoryScan(
            tmpDir.path(),
            nullptr /* cbControl */,
            [ &popped ]( SAA_in const om::ObjPtr< Task >& task ) -> void
            {
                popped.push_back( om::copy( task ) );
            },
            queueWasEmptyAfterDrain
            );

        UTF_REQUIRE_EQUAL( 1U, poppedCount );
        UTF_REQUIRE_EQUAL( 1U, popped.size() );
        UTF_REQUIRE( popped[ 0 ] -> isFailed() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( popped[ 0 ] -> exception() ),
            bl::UnexpectedException,
            "not a regular file, directory or a symlink"
            );

        UTF_REQUIRE( queueWasEmptyAfterDrain );

        /*
         * fs::safeDeletePathNothrow refuses to delete an entry which is not a regular file,
         * a directory or a symlink, so the FIFO must be unlinked here or the TmpDir
         * destructor would report the failure through the NOEXCEPT guard
         */

        UTF_REQUIRE_EQUAL( 0, ::unlink( fifoPath.string().c_str() ) );
    }

#endif // ! defined( _WIN32 )
}

UTF_AUTO_TEST_CASE( Tasks_ScanDirectoryTaskFilteringTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * An entry which the control token rejects is neither recorded in m_entries nor pushed
     * into dirsToScan - and the token itself is forwarded to every child scanner, so the
     * policy applies to the entire recursion and not just to the top level
     */

    fs::TmpDir tmpDir;

    const auto root = tmpDir.path();

    fs::safeCreateDirectory( root / "included" );
    fs::safeCreateDirectory( root / "excluded" );
    fs::safeCreateDirectory( root / "included" / "nested-excluded" );

    utest::TestFsUtils::createDummyFile( root / "included" / "keep.txt", 16U );
    utest::TestFsUtils::createDummyFile( root / "excluded" / "must-not-appear.txt", 16U );
    utest::TestFsUtils::createDummyFile( root / "included" / "nested-excluded" / "deep.txt", 16U );

    const auto token = ConfigurableScanningControlImpl::createInstance();

    token -> setEntryFilter(
        []( SAA_in const fs::path& path ) -> bool
        {
            const auto name = path.filename().string();

            return "excluded" != name && "nested-excluded" != name;
        }
        );

    std::vector< fs::path > collected;
    std::vector< fs::path > rootPaths;
    std::size_t failedCount = 0U;
    bool queueWasEmptyAfterDrain = false;

    const auto poppedCount = drainDirectoryScan(
        root,
        om::qi< DirectoryScannerControlToken >( token ),
        [ &collected, &rootPaths, &failedCount ]( SAA_in const om::ObjPtr< Task >& task ) -> void
        {
            if( task -> isFailed() )
            {
                ++failedCount;

                return;
            }

            const auto scanner = om::qi< ScanDirectoryTaskImpl >( task );

            rootPaths.push_back( scanner -> rootPath() );

            for( const auto& entry : scanner -> entries() )
            {
                collected.push_back( entry.path() );
            }
        },
        queueWasEmptyAfterDrain
        );

    const auto cbCollected = [ &collected ]( SAA_in const fs::path& path ) -> bool
    {
        return std::find( collected.begin(), collected.end(), path ) != collected.end();
    };

    UTF_REQUIRE_EQUAL( 0U, failedCount );

    UTF_REQUIRE( cbCollected( root / "included" ) );
    UTF_REQUIRE( cbCollected( root / "included" / "keep.txt" ) );

    /*
     * The top level rejection was neither reported nor descended into
     */

    UTF_REQUIRE( ! cbCollected( root / "excluded" ) );
    UTF_REQUIRE( ! cbCollected( root / "excluded" / "must-not-appear.txt" ) );

    /*
     * The nested rejection is only reachable by a child scanner, so this is the assertion
     * which fails if the control token is dropped when the child scanner is constructed
     */

    UTF_REQUIRE( ! cbCollected( root / "included" / "nested-excluded" ) );
    UTF_REQUIRE( ! cbCollected( root / "included" / "nested-excluded" / "deep.txt" ) );

    /*
     * The root scanner and the one for 'included' - and nothing for either rejected
     * directory
     */

    UTF_REQUIRE_EQUAL( 2U, poppedCount );
    UTF_REQUIRE_EQUAL( 2U, rootPaths.size() );

    for( const auto& rootPath : rootPaths )
    {
        UTF_REQUIRE_EQUAL( rootPath, root );
    }

    UTF_REQUIRE( queueWasEmptyAfterDrain );
}

UTF_AUTO_TEST_CASE( Tasks_ScanDirectoryTaskErrorPolicyTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * A non existent path is used rather than a permission denied one, because chmod based
     * fixtures do not behave the same when the tests run as root
     */

    fs::TmpDir tmpDir;

    const auto missingPath = tmpDir.path() / "no-such-dir";

    {
        /*
         * The tolerant arm - the whole 'if( ! ec )' body is skipped, so the task succeeds
         * with no entries at all and schedules nothing
         */

        const auto token = ConfigurableScanningControlImpl::createInstance();

        token -> setErrorAllowed( true );

        std::vector< om::ObjPtr< Task > > popped;
        bool queueWasEmptyAfterDrain = false;

        const auto poppedCount = drainDirectoryScan(
            missingPath,
            om::qi< DirectoryScannerControlToken >( token ),
            [ &popped ]( SAA_in const om::ObjPtr< Task >& task ) -> void
            {
                popped.push_back( om::copy( task ) );
            },
            queueWasEmptyAfterDrain
            );

        UTF_REQUIRE_EQUAL( 1U, poppedCount );
        UTF_REQUIRE_EQUAL( 1U, popped.size() );
        UTF_REQUIRE( ! popped[ 0 ] -> isFailed() );
        UTF_REQUIRE( om::qi< ScanDirectoryTaskImpl >( popped[ 0 ] ) -> entries().empty() );
        UTF_REQUIRE( queueWasEmptyAfterDrain );
    }

    {
        /*
         * The strict arm - with no control token at all the very same failure is fatal
         */

        std::vector< om::ObjPtr< Task > > popped;
        bool queueWasEmptyAfterDrain = false;

        const auto poppedCount = drainDirectoryScan(
            missingPath,
            nullptr /* cbControl */,
            [ &popped ]( SAA_in const om::ObjPtr< Task >& task ) -> void
            {
                popped.push_back( om::copy( task ) );
            },
            queueWasEmptyAfterDrain
            );

        UTF_REQUIRE_EQUAL( 1U, poppedCount );
        UTF_REQUIRE_EQUAL( 1U, popped.size() );
        UTF_REQUIRE( popped[ 0 ] -> isFailed() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( popped[ 0 ] -> exception() ),
            bl::SystemException,
            "Cannot open directory"
            );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ExcludedPathsControlTokenTests )
{
    using namespace bl;
    using namespace bl::tasks;

    fs::TmpDir tmpDir;

    const auto root = tmpDir.path();

    fs::safeCreateDirectory( root / "keep" );
    fs::safeCreateDirectory( root / "drop" );

    const auto cbExclusionKey = []( SAA_in const fs::path& path ) -> std::string
    {
        auto key = fs::normalize( path ).string();

        if( os::onWindows() )
        {
            str::to_lower( key );
        }

        return key;
    };

    {
        /*
         * The unit block - fs::directory_entry is constructible from a path, so none of
         * this needs the scanner or the execution queue
         */

        const auto excludedPath = root / "drop";
        const auto otherPath = root / "keep";

        const auto token = ExcludedPathsControlToken::createInstance(
            std::vector< std::string >{ cbExclusionKey( excludedPath ) }
            );

        UTF_REQUIRE( ! token -> isEntryAllowed( fs::directory_entry( excludedPath ) ) );
        UTF_REQUIRE( token -> isEntryAllowed( fs::directory_entry( otherPath ) ) );

        /*
         * The constructor normalises each entry exactly the way the lookup normalises a
         * scanned path, so an un-normalised exclusion entry excludes what the caller meant
         * rather than silently excluding nothing
         */

        const auto tokenWithNonNormalisedEntry = ExcludedPathsControlToken::createInstance(
            std::vector< std::string >{ excludedPath.string() + "/." }
            );

        UTF_REQUIRE( ! tokenWithNonNormalisedEntry -> isEntryAllowed( fs::directory_entry( excludedPath ) ) );

        UTF_REQUIRE( tokenWithNonNormalisedEntry -> isEntryAllowed( fs::directory_entry( otherPath ) ) );

        /*
         * The error policy is hard coded to false, which makes the whole scan fail on the
         * first unreadable directory, and the token is never canceled
         */

        UTF_REQUIRE(
            ! token -> isErrorAllowed(
                eh::error_code( eh::errc::permission_denied, eh::generic_category() )
                )
            );

        UTF_REQUIRE( ! token -> isCanceled() );

        /*
         * The BL_QITBL block lists both interfaces and nothing else exercises it
         */

        UTF_REQUIRE( om::qi< TaskControlToken >( token ) );
        UTF_REQUIRE( om::qi< DirectoryScannerControlToken >( token ) );

        if( os::onWindows() )
        {
            /*
             * On Windows the entry path is lower cased before the lookup, so an entry which
             * is supplied with different casing is still excluded
             */

            auto mixedCase = excludedPath.string();

            str::to_upper( mixedCase );

            UTF_REQUIRE( ! token -> isEntryAllowed( fs::directory_entry( fs::path( mixedCase ) ) ) );
        }
    }

    {
        /*
         * The end to end block - the excluded directory must not be reported and must not
         * be descended into
         */

        const auto token = ExcludedPathsControlToken::createInstance(
            std::vector< std::string >{ cbExclusionKey( root / "drop" ) }
            );

        utest::TestFsUtils::createDummyFile( root / "keep" / "keep.txt", 16U );
        utest::TestFsUtils::createDummyFile( root / "drop" / "drop.txt", 16U );

        std::vector< fs::path > collected;
        bool queueWasEmptyAfterDrain = false;

        const auto poppedCount = drainDirectoryScan(
            root,
            om::qi< DirectoryScannerControlToken >( token ),
            [ &collected ]( SAA_in const om::ObjPtr< Task >& task ) -> void
            {
                UTF_REQUIRE( ! task -> isFailed() );

                const auto scanner = om::qi< ScanDirectoryTaskImpl >( task );

                for( const auto& entry : scanner -> entries() )
                {
                    collected.push_back( entry.path() );
                }
            },
            queueWasEmptyAfterDrain
            );

        const auto cbCollected = [ &collected ]( SAA_in const fs::path& path ) -> bool
        {
            return std::find( collected.begin(), collected.end(), path ) != collected.end();
        };

        UTF_REQUIRE_EQUAL( 2U, poppedCount );
        UTF_REQUIRE( cbCollected( root / "keep" ) );
        UTF_REQUIRE( cbCollected( root / "keep" / "keep.txt" ) );
        UTF_REQUIRE( ! cbCollected( root / "drop" ) );
        UTF_REQUIRE( ! cbCollected( root / "drop" / "drop.txt" ) );
        UTF_REQUIRE( queueWasEmptyAfterDrain );
    }
}

UTF_AUTO_TEST_CASE( Tasks_ShutdownTaskSignalTests )
{
#if defined( _WIN32 )

    /*
     * On Windows SIGQUIT is compiled out and Boost.Asio implements asio::signal_set through
     * ::signal, so the signal fan-out contract is validated on UNIX first and this case has
     * to be validated separately before it is enabled for the msvc / clang Windows builds
     */

    UTF_SKIP_UNLESS( false, "signal delivery is only validated on UNIX" );

#else // defined( _WIN32 )

    using namespace bl;
    using namespace bl::tasks;

    std::atomic< int > counter1( 0 );
    std::atomic< int > counter2( 0 );

    /*
     * A task which is never pushed anywhere - registerTask( ... ) binds Task::requestCancel
     * on it and the fan-out is the only thing which can cancel it
     */

    const auto victim = SimpleTaskImpl::createInstance( cpp::void_callback_t( []() -> void {} ) );

    const auto shutdownWatcher = ShutdownTaskImpl::createInstance();

    shutdownWatcher -> registerCallback( [ &counter1 ]() -> void { ++counter1; } );
    shutdownWatcher -> registerCallback( [ &counter2 ]() -> void { ++counter2; } );
    shutdownWatcher -> registerTask( om::qi< Task >( victim ) );

    const auto taskShutdownWatcher = om::qi< Task >( shutdownWatcher );

    bool watcherWasRunning = false;
    bool handlerWasInstalled = false;
    bool completedInTime = false;

    scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            eq -> push_back( taskShutdownWatcher );

            for( std::size_t i = 0U; i < 200U; ++i )
            {
                if( Task::Running == taskShutdownWatcher -> getState() )
                {
                    watcherWasRunning = true;

                    break;
                }

                os::sleep( time::milliseconds( 50 ) );
            }

            /*
             * TaskBase::scheduleNothrow sets the state to Running *before* it calls
             * scheduleTask(), so observing Running does not prove that async_wait has been
             * armed yet - hence the wait below
             */

            os::sleep( time::milliseconds( 1000 ) );

            /*
             * If the handler is not installed the default disposition of SIGTERM would kill
             * the test process outright instead of failing this case, so the disposition is
             * probed first and the signal is raised only when it is safe to do so
             */

            struct sigaction currentAction;

            std::memset( &currentAction, 0, sizeof( currentAction ) );

            handlerWasInstalled =
                0 == ::sigaction( SIGTERM, nullptr, &currentAction ) &&
                SIG_DFL != currentAction.sa_handler &&
                SIG_IGN != currentAction.sa_handler;

            if( watcherWasRunning && handlerWasInstalled )
            {
                /*
                 * Exactly once - onStop() destroys the signal set, which restores the
                 * default disposition, so a second raise would kill the test runner
                 */

                ::raise( SIGTERM );

                for( std::size_t i = 0U; i < 300U; ++i )
                {
                    if( Task::Completed == taskShutdownWatcher -> getState() )
                    {
                        completedInTime = true;

                        break;
                    }

                    os::sleep( time::milliseconds( 100 ) );
                }
            }

            if( completedInTime )
            {
                eq -> waitForSuccess( taskShutdownWatcher );
            }
            else
            {
                eq -> cancelAll( true /* wait */ );
            }
        }
        );

    UTF_REQUIRE( watcherWasRunning );
    UTF_REQUIRE( handlerWasInstalled );
    UTF_REQUIRE( completedInTime );

    /*
     * Every registered callback is invoked exactly once
     */

    UTF_REQUIRE_EQUAL( 1, counter1.load() );
    UTF_REQUIRE_EQUAL( 1, counter2.load() );

    /*
     * The registerTask( ... ) binding really did call Task::requestCancel
     */

    UTF_REQUIRE( victim -> isCanceled() );

    UTF_REQUIRE( ! taskShutdownWatcher -> isFailed() );
    UTF_REQUIRE_EQUAL( Task::Completed, taskShutdownWatcher -> getState() );

#endif // defined( _WIN32 )
}

namespace
{
    /**
     * @brief A pinger task whose reachability and round trip time are assigned directly, so
     * the pure ordering logic of PingerTask::less can be tested without the network
     *
     * The relevant members are protected on PingerTaskT and BL_DECLARE_OBJECT_IMPL only
     * deletes the copy constructor and makes the destructor protected, so a test subclass
     * can set them
     */

    template
    <
        typename E = void
    >
    class FakePingerTaskT : public bl::tasks::PingerTask
    {
        BL_DECLARE_OBJECT_IMPL( FakePingerTaskT )

    protected:

        const bool                                                                      m_throwOnExecute;

        FakePingerTaskT(
            SAA_in              std::string&&                                           host,
            SAA_in              const bool                                              reachable,
            SAA_in              const double                                            roundTripTimeMs,
            SAA_in              const bool                                              throwOnExecute
            )
            :
            bl::tasks::PingerTask( BL_PARAM_FWD( host ), 1000 /* timeoutInMilliseconds */ ),
            m_throwOnExecute( throwOnExecute )
        {
            m_isReachable = reachable;
            m_roundTripTimeMs = roundTripTimeMs;
        }

        /*
         * SimpleTaskBase::onExecute() is pure virtual, so the override is required
         */

        virtual void onExecute() NOEXCEPT OVERRIDE
        {
            BL_TASKS_HANDLER_BEGIN()

            BL_CHK_USER_FRIENDLY(
                true,
                m_throwOnExecute,
                BL_MSG()
                    << "The fake pinger task was asked to fail"
                );

            BL_TASKS_HANDLER_END()
        }
    };

    typedef bl::om::ObjectImpl< FakePingerTaskT<> > FakePingerTaskImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( Tasks_PingerOrderingTests )
{
    using namespace bl;
    using namespace bl::tasks;

    const auto cbCreateFake = [](
        SAA_in          std::string&&                       host,
        SAA_in          const bool                          reachable,
        SAA_in          const double                        roundTripTimeMs
        )
        -> om::ObjPtr< PingerTask >
    {
        return FakePingerTaskImpl::createInstance< PingerTask >(
            BL_PARAM_FWD( host ),
            reachable,
            roundTripTimeMs,
            false /* throwOnExecute */
            );
    };

    const auto alphaFast = cbCreateFake( std::string( "alpha" ), true, 5.0 );
    const auto zuluSlow = cbCreateFake( std::string( "zulu" ), true, 30.0 );
    const auto alphaUnreachable = cbCreateFake( std::string( "alpha" ), false, 0.0 );
    const auto bravoUnreachable = cbCreateFake( std::string( "bravo" ), false, 0.0 );

    {
        /*
         * Block A - all four branches of PingerTask::less
         */

        UTF_REQUIRE( PingerTask::less( *alphaFast, *alphaUnreachable ) );
        UTF_REQUIRE( ! PingerTask::less( *alphaUnreachable, *alphaFast ) );

        UTF_REQUIRE( PingerTask::less( *alphaUnreachable, *bravoUnreachable ) );
        UTF_REQUIRE( ! PingerTask::less( *bravoUnreachable, *alphaUnreachable ) );

        UTF_REQUIRE( PingerTask::less( *alphaFast, *zuluSlow ) );
        UTF_REQUIRE( ! PingerTask::less( *zuluSlow, *alphaFast ) );
    }

    {
        /*
         * Block B - the reachable entries come first ordered by round trip time and the
         * unreachable ones follow ordered by host name
         */

        std::vector< om::ObjPtr< PingerTask > > list;

        list.push_back( om::copy( zuluSlow ) );
        list.push_back( om::copy( bravoUnreachable ) );
        list.push_back( om::copy( alphaFast ) );
        list.push_back( om::copy( alphaUnreachable ) );

        PingerTask::sortPingerTasksByRoundTripTime( list );

        UTF_REQUIRE_EQUAL( 4U, list.size() );

        UTF_REQUIRE_EQUAL( std::string( "alpha" ), list[ 0 ] -> host() );
        UTF_REQUIRE( list[ 0 ] -> isReachable() );
        UTF_REQUIRE( numbers::floatingPointEqual( list[ 0 ] -> roundTripTimeMs(), 5.0 ) );

        UTF_REQUIRE_EQUAL( std::string( "zulu" ), list[ 1 ] -> host() );
        UTF_REQUIRE( list[ 1 ] -> isReachable() );
        UTF_REQUIRE( numbers::floatingPointEqual( list[ 1 ] -> roundTripTimeMs(), 30.0 ) );

        UTF_REQUIRE_EQUAL( std::string( "alpha" ), list[ 2 ] -> host() );
        UTF_REQUIRE( list[ 2 ] -> isUnreachable() );

        UTF_REQUIRE_EQUAL( std::string( "bravo" ), list[ 3 ] -> host() );
        UTF_REQUIRE( list[ 3 ] -> isUnreachable() );
    }

    {
        /*
         * Block C - the '|| isFailed()' half of isUnreachable(), which nothing else covers
         */

        const auto failing = FakePingerTaskImpl::createInstance< PingerTask >(
            std::string( "failing" ),
            true /* reachable */,
            1.0 /* roundTripTimeMs */,
            true /* throwOnExecute */
            );

        scheduleAndExecuteInParallel(
            [ &failing ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepNone );

                eq -> push_back( om::qi< Task >( failing ) );

                eq -> flushNoThrowIfFailed();
            }
            );

        UTF_REQUIRE( failing -> isFailed() );

        /*
         * m_isReachable was set to true, so isUnreachable() can only be true because of the
         * isFailed() term
         */

        UTF_REQUIRE( failing -> isReachable() );
        UTF_REQUIRE( failing -> isUnreachable() );
    }
}
