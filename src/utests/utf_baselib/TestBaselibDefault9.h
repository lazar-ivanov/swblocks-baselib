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

#include <baselib/data/DataBlock.h>

#include <baselib/core/AppInitDone.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/FsUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/OS.h>
#include <baselib/core/Pool.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/Uuid.h>
#include <baselib/core/BaseIncludes.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#if ! defined( _WIN32 )

#include <sys/resource.h>
#include <unistd.h>

/************************************************************************
 * os::detail::OS::splitCommandLineArguments tests
 */

namespace
{
    void checkSplitCommandLine(
        SAA_in          const std::string&                          commandLine,
        SAA_in          const std::vector< std::string >&           expected
        )
    {
        const auto actual = bl::os::detail::OS::splitCommandLineArguments( commandLine );

        /*
         * The size is asserted first so a mismatch produces a readable failure rather
         * than an out of range access below
         */

        UTF_REQUIRE_EQUAL( expected.size(), actual.size() );

        for( std::size_t i = 0U; i < expected.size(); ++i )
        {
            UTF_CHECK_EQUAL( expected[ i ], actual[ i ] );
        }
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_OSSplitCommandLineArgumentsTests )
{
    /*
     * splitCommandLineArguments( ... ) is what turns every string form createProcess( ... )
     * into an argv, and it is a public static, so it can be driven directly with no
     * process spawned at all
     *
     * The separator set is " \t" and the quote set is "\"'" with '\\' as the escape; empty
     * tokens - which adjacent separators produce - are skipped so they never become empty
     * argv entries
     */

    checkSplitCommandLine( "a b c", std::vector< std::string >{ "a", "b", "c" } );

    /*
     * The tab is a separator too
     */

    checkSplitCommandLine( "a\tb", std::vector< std::string >{ "a", "b" } );

    /*
     * Adjacent separators produce empty tokens which must be skipped - the size being
     * exactly 2 is the assertion, an unskipped run would give 5
     */

    checkSplitCommandLine( "a  \t  b", std::vector< std::string >{ "a", "b" } );

    /*
     * Both quote characters group, and the escape joins across a separator
     */

    checkSplitCommandLine(
        "prog \"quoted arg\" tail",
        std::vector< std::string >{ "prog", "quoted arg", "tail" }
        );

    checkSplitCommandLine(
        "prog 'single quoted' tail",
        std::vector< std::string >{ "prog", "single quoted", "tail" }
        );

    checkSplitCommandLine(
        "prog escaped\\ space",
        std::vector< std::string >{ "prog", "escaped space" }
        );

    checkSplitCommandLine( bl::str::empty(), std::vector< std::string >() );

    /*
     * A trailing escape is a tokenization error, and the exception must NOT carry the
     * command line - it may carry credentials. Only the first token which was produced
     * before the failure is attached
     */

    UTF_REQUIRE_THROW(
        bl::os::detail::OS::splitCommandLineArguments( "foo bar\\" ),
        bl::ArgumentException
        );

    try
    {
        bl::os::detail::OS::splitCommandLineArguments( "foo bar\\" );

        UTF_FAIL( "splitCommandLineArguments must throw for a trailing escape" );
    }
    catch( bl::ArgumentException& e )
    {
        UTF_CHECK( ! bl::cpp::contains( std::string( e.what() ), std::string( "bar" ) ) );

        const auto* const value = bl::eh::get_error_info< bl::eh::errinfo_string_value >( e );

        UTF_REQUIRE( value );
        UTF_CHECK_EQUAL( *value, std::string( "foo" ) );
    }
}

/************************************************************************
 * os::getPhysicalMemorySize / os::getFileDescriptorSoftLimit tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSResourceLimitsTests )
{
    /*
     * Both APIs document zero as "unknown / not applicable"; their sole consumer,
     * TcpBaseTasks.h::getDerivedMaxConnections, skips terms whose value is zero. Reporting
     * RLIM_INFINITY as a real limit, or returning the hard limit instead of the soft one,
     * would silently corrupt the derived maximum connection count of every TCP server
     */

    UTF_REQUIRE( bl::os::getPhysicalMemorySize() > 0U );

    UTF_CHECK_EQUAL(
        bl::os::getPhysicalMemorySize(),
        static_cast< std::uint64_t >( ::sysconf( _SC_PHYS_PAGES ) ) *
            static_cast< std::uint64_t >( ::sysconf( _SC_PAGESIZE ) )
        );

    struct ::rlimit saved;

    UTF_REQUIRE_EQUAL( 0, ::getrlimit( RLIMIT_NOFILE, &saved ) );

    /*
     * The original limit must be put back on every exit path - other cases in this module,
     * BaseLib_OSCreateProcessDescriptorHygieneTests in particular, depend on it
     */

    BL_SCOPE_EXIT(
        {
            ::setrlimit( RLIMIT_NOFILE, &saved );
        }
        );

    const ::rlim_t target = std::min< ::rlim_t >( 4096U, saved.rlim_max );

    {
        struct ::rlimit lowered = saved;

        lowered.rlim_cur = target;

        UTF_REQUIRE_EQUAL( 0, ::setrlimit( RLIMIT_NOFILE, &lowered ) );
    }

    UTF_CHECK_EQUAL( static_cast< std::uint64_t >( target ), bl::os::getFileDescriptorSoftLimit() );

    if( RLIM_INFINITY == saved.rlim_max )
    {
        struct ::rlimit infinite = saved;

        infinite.rlim_cur = RLIM_INFINITY;

        UTF_REQUIRE_EQUAL( 0, ::setrlimit( RLIMIT_NOFILE, &infinite ) );

        /*
         * An infinite soft limit is reported as "unknown", not as a huge number
         */

        UTF_CHECK_EQUAL( 0U, bl::os::getFileDescriptorSoftLimit() );
    }
    else
    {
        UTF_MESSAGE(
            "Skipping the RLIM_INFINITY soft limit assertion - the hard limit of this process is finite"
            );
    }
}

/************************************************************************
 * passwd database wrappers and pre-fork user validation tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSUserLookupTests )
{
    bl::fs::TmpDir tmpDir;

    const auto filePath = tmpDir.path() / "owned.txt";

    {
        const auto file = bl::os::fopen( filePath, "wb" );

        BL_UNUSED( file );
    }

    /*
     * getFileOwner( ... ) stats the file and maps st_uid back through getpwuid_r, which is
     * the same lookup getUserName() performs for the effective uid - so for a file this
     * process has just created the two must agree
     */

    UTF_CHECK_EQUAL( bl::os::getUserName(), bl::os::getFileOwner( filePath.string() ) );

    UTF_CHECK_THROW(
        bl::os::getFileOwner( ( tmpDir.path() / "missing.txt" ).string() ),
        bl::SystemException
        );

    const auto unknownUser = "nosuchuser-" + bl::uuids::uuid2string( bl::uuids::create() );

    /*
     * createProcess( ... ) validates the user name BEFORE the fork, so a missing user must
     * be reported without any child being created
     *
     * The common base is caught deliberately: glibc reports "not found" as result ==
     * nullptr, i.e. an UnexpectedException, while another libc may return an errno, i.e. a
     * SystemException
     *
     * A callWithPasswdBuffer( ... ) regression which treated the getpw*_r return value as
     * -1 style would report success for a missing entry and this would stop throwing
     */

    bool userLookupThrew = false;
    bool userLookupIsBaselibException = false;

    try
    {
        bl::os::createProcess( unknownUser, "true" );
    }
    catch( std::exception& e )
    {
        userLookupThrew = true;

        /*
         * bl::BaseException is the common base but it has no what( ... ) of its own, so it
         * cannot be named in UTF_REQUIRE_THROW - the identity is checked here instead,
         * which is also a stronger statement than "some std::exception was thrown"
         */

        userLookupIsBaselibException = ( nullptr != dynamic_cast< const bl::BaseException* >( &e ) );
    }

    UTF_REQUIRE( userLookupThrew );
    UTF_REQUIRE( userLookupIsBaselibException );

    if( ! bl::os::isUserAdministrator() )
    {
        /*
         * The root check is also made before the fork, and after the user lookup
         */

        UTF_REQUIRE_THROW_MESSAGE(
            bl::os::createProcess( bl::os::getUserName(), "true" ),
            bl::UnexpectedException,
            "Only root can execute processes under a different user account"
            );
    }
    else
    {
        UTF_MESSAGE( "Skipping the non-root guard assertion - this process is running as an administrator" );
    }
}

#endif // ! defined( _WIN32 )

/************************************************************************
 * ThreadPoolDefaultT two slot registry tests
 */

namespace
{
    /*
     * ThreadPoolDefaultT< E > keeps its lock array and its pointer array as PER
     * INSTANTIATION statics, so a private tag type gives this case a registry of its own
     * and the process wide ThreadPoolDefault owned by the global UTF fixture is never
     * touched. Never construct a second AppInitDoneDefault to get the same effect
     */

    struct UtfThreadPoolRegistryTag
    {
    };

    typedef bl::ThreadPoolDefaultT< UtfThreadPoolRegistryTag > TestThreadPoolRegistry;

    /*
     * A minimal ThreadPool whose dispose() looks back at the registry
     *
     * disposeGlobalThreadPool( ... ) does, in this exact order: dispose the pool, clear the
     * global pointer, release the caller's reference - and the header explains why the
     * pointer must be cleared BEFORE the object is released, so a concurrent getDefault()
     * cannot add a reference to freed memory
     *
     * That ordering is invisible from the outside: every post-condition of the call holds
     * whichever order the two middle steps are performed in. Recording the registry state
     * from inside dispose() is what makes it falsifiable
     */

    template
    <
        typename E = void
    >
    class TestThreadPoolT :
        public bl::cpp::noncopyable,
        public bl::ThreadPool
    {
        BL_QITBL_DECLARE_DISPOSABLE( bl::ThreadPool )

    private:

        bl::asio::io_service                                m_ioService;
        long                                                m_disposeCount;
        bool                                                m_defaultRegisteredDuringDispose;

    protected:

        TestThreadPoolT()
            :
            m_disposeCount( 0L ),
            m_defaultRegisteredDuringDispose( false )
        {
        }

    public:

        long disposeCount() const NOEXCEPT
        {
            return m_disposeCount;
        }

        bool defaultRegisteredDuringDispose() const NOEXCEPT
        {
            return m_defaultRegisteredDuringDispose;
        }

        virtual std::size_t size() const NOEXCEPT OVERRIDE
        {
            return 0U;
        }

        virtual std::size_t resize( SAA_in const std::size_t threadCount ) OVERRIDE
        {
            BL_UNUSED( threadCount );

            return 0U;
        }

        virtual bl::asio::io_service& aioService() OVERRIDE
        {
            return m_ioService;
        }

        virtual std::exception_ptr lastException() const OVERRIDE
        {
            return std::exception_ptr();
        }

        virtual void dispose() OVERRIDE
        {
            /*
             * Only the FIRST call is what the ordering is about - see the note about the
             * second one in the case below
             */

            if( 0L == m_disposeCount )
            {
                m_defaultRegisteredDuringDispose =
                    ( nullptr != TestThreadPoolRegistry::getDefault( bl::ThreadPoolId::NonBlocking ) );
            }

            ++m_disposeCount;
        }
    };

    typedef bl::om::ObjectImpl< TestThreadPoolT<> > TestThreadPoolImpl;

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_ThreadPoolDefaultRegistryTests )
{
    using namespace bl;

    UTF_REQUIRE( ! TestThreadPoolRegistry::getDefault( ThreadPoolId::GeneralPurpose ) );
    UTF_REQUIRE( ! TestThreadPoolRegistry::getDefault( ThreadPoolId::NonBlocking ) );

    om::ObjPtrDisposable< ThreadPool > tp(
        ThreadPoolImpl::createInstance< ThreadPool >( os::AbstractPriority::Normal, 2U )
        );

    /*
     * A second strong reference which survives the disposal, so the post-conditions of
     * dispose() can be observed on a live object
     */

    const auto keepAlive = om::copy( tp.get() );

    TestThreadPoolRegistry::setDefault( tp.get(), ThreadPoolId::GeneralPurpose );

    UTF_REQUIRE( om::areEqual( TestThreadPoolRegistry::getDefault( ThreadPoolId::GeneralPurpose ), tp.get() ) );

    /*
     * The no-argument overload resolves to GeneralPurpose
     */

    UTF_REQUIRE( om::areEqual( TestThreadPoolRegistry::getDefault(), tp.get() ) );

    /*
     * The two slots are independent - a shared slot or an off-by-one in the
     * static_cast< std::uint16_t >( id ) index would show up right here
     */

    UTF_REQUIRE( ! TestThreadPoolRegistry::getDefault( ThreadPoolId::NonBlocking ) );

    TestThreadPoolRegistry::disposeGlobalThreadPool( tp, ThreadPoolId::GeneralPurpose );

    /*
     * The caller's ObjPtrDisposable was reset and the global pointer was cleared
     */

    UTF_REQUIRE( ! tp );
    UTF_REQUIRE( ! TestThreadPoolRegistry::getDefault( ThreadPoolId::GeneralPurpose ) );

    /*
     * The pool was DISPOSED, not merely unregistered - the header calls out that the
     * pointer must be cleared before the object is released, so a concurrent getDefault()
     * cannot add a reference to freed memory
     */

    UTF_REQUIRE_EQUAL( 0U, keepAlive -> size() );
    UTF_REQUIRE_THROW( keepAlive -> resize( 2U ), bl::UnexpectedException );

    /*
     * The second call is a no-op - the pointer is already empty and the slot is already
     * clear
     */

    UTF_REQUIRE_NO_THROW( TestThreadPoolRegistry::disposeGlobalThreadPool( tp, ThreadPoolId::GeneralPurpose ) );

    /*
     * The dispose-then-clear ORDERING, observed from inside dispose() - and through the
     * other slot, which also exercises setDefault / disposeGlobalThreadPool for
     * ThreadPoolId::NonBlocking
     *
     * None of the assertions above can see this: clearing the global pointer before
     * disposing satisfies every one of them, which is exactly why the use-after-free the
     * ordering exists to prevent could be reintroduced unnoticed
     */

    const auto probe = TestThreadPoolImpl::createInstance();

    {
        om::ObjPtrDisposable< ThreadPool > probeRef( om::qi< ThreadPool >( probe ) );

        TestThreadPoolRegistry::setDefault( probeRef.get(), ThreadPoolId::NonBlocking );

        UTF_REQUIRE( om::areEqual( TestThreadPoolRegistry::getDefault( ThreadPoolId::NonBlocking ), probeRef.get() ) );

        TestThreadPoolRegistry::disposeGlobalThreadPool( probeRef, ThreadPoolId::NonBlocking );

        UTF_REQUIRE( ! probeRef );
    }

    /*
     * The pool was still the registered default at the moment it was disposed, i.e.
     * dispose() really does run BEFORE setDefault( nullptr, id )
     */

    UTF_REQUIRE( probe -> defaultRegisteredDuringDispose() );

    /*
     * PINNED CURRENT BEHAVIOUR - dispose() is invoked TWICE per disposeGlobalThreadPool
     * call: once explicitly, and once more by threadPool.reset(), which is an
     * ObjPtrDisposable::reset() and therefore disposes again on its way out
     *
     * That is harmless only because dispose() is required to be idempotent
     * ( ThreadPoolImplT::disposeInternal is ), and it is invisible with any real pool -
     * which is why it is recorded here
     */

    UTF_CHECK_EQUAL( 2L, probe -> disposeCount() );

    UTF_REQUIRE( ! TestThreadPoolRegistry::getDefault( ThreadPoolId::NonBlocking ) );
}

/************************************************************************
 * AppInitDone default thread pool wiring tests
 */

UTF_AUTO_TEST_CASE( BaseLib_AppInitDoneDefaultPoolsTests )
{
    using namespace bl;

    /*
     * OBSERVE ONLY - the single bl::AppInitDoneDefault of this test binary is owned by the
     * global UTF fixture; nothing here constructs a second one or disposes anything
     *
     * A copy/paste slip in the AppInitDoneT constructor - registering the same pool under
     * both ids, or using threadsCount for both - would leave every NonBlocking task
     * running on the general purpose pool with no test noticing
     */

    const auto gp = ThreadPoolDefault::getDefault( ThreadPoolId::GeneralPurpose );
    const auto nb = ThreadPoolDefault::getDefault( ThreadPoolId::NonBlocking );

    UTF_REQUIRE( gp );
    UTF_REQUIRE( nb );

    UTF_REQUIRE( ! om::areEqual( gp, nb.get() ) );
    UTF_REQUIRE( &gp -> aioService() != &nb -> aioService() );

    UTF_REQUIRE_EQUAL( gp -> size(), test::UtfArgsParser::threadsCount() );

    /*
     * The I/O pool is clamped at IO_THREADS_COUNT and does not grow with threadsCount
     */

    UTF_REQUIRE_EQUAL(
        nb -> size(),
        std::min< std::size_t >( test::UtfArgsParser::threadsCount(), ThreadPoolDefault::IO_THREADS_COUNT )
        );

    UTF_REQUIRE( om::areEqual( ThreadPoolDefault::getDefault(), gp.get() ) );
}

/************************************************************************
 * data::DataBlock pooled get and copy tests
 */

UTF_AUTO_TEST_CASE( BaseLib_DataBlockPoolAndCopyTests )
{
    using namespace bl;
    using namespace bl::data;

    const auto pool = datablocks_pool_type::createInstance();

    /*
     * NEVER put the same block twice - SimplePoolCheckerIntrusiveImplPtr::markFreed has a
     * BL_RT_ASSERT which ABORTS the process rather than throwing, so only the legal
     * transitions are exercised below
     */

    {
        auto b1 = DataBlock::get( pool );

        UTF_REQUIRE_EQUAL( DataBlock::defaultCapacity(), b1 -> capacity() );
        UTF_REQUIRE_EQUAL( 0U, b1 -> size() );
        UTF_REQUIRE_EQUAL( 0U, b1 -> offset1() );
        UTF_REQUIRE( ! b1 -> freed() );

        /*
         * Dirty the block before handing it back, so that recycling it really has
         * something to reset - and note that with a non zero offset1 in place the reverse
         * reset order would trip setSize's BL_ASSERT( m_offset1 <= size ) in a debug build
         */

        b1 -> write( "abcdef", 6U );
        b1 -> setOffset1( 4U );

        pool -> put( std::move( b1 ) );
    }

    auto b2 = DataBlock::get( pool );

    UTF_REQUIRE_EQUAL( 0U, b2 -> size() );
    UTF_REQUIRE_EQUAL( 0U, b2 -> offset1() );

    /*
     * SimplePool::tryGet owns the freed flag - get( ... ) must not touch it
     */

    UTF_REQUIRE( ! b2 -> freed() );

    /*
     * THE CAPACITY ARGUMENT IS ADVISORY - it is only honoured when the pool has nothing to
     * hand out. Every production site which calls copy( block, pool ) is correct only for
     * as long as the pool holds uniformly sized blocks
     */

    pool -> put( std::move( b2 ) );

    const auto b3 = DataBlock::get( pool, 128U );

    UTF_REQUIRE_EQUAL( DataBlock::defaultCapacity(), b3 -> capacity() );

    while( pool -> tryGet() )
    {
        /*
         * Drain whatever is left, so the next get( ... ) really has to create a block
         */
    }

    const auto b4 = DataBlock::get( pool, 128U );

    UTF_REQUIRE_EQUAL( 128U, b4 -> capacity() );

    /*
     * calculateCapacity treats zero as "use the default" - reachable in production from
     * AuthorizationCache::createAuthenticationToken( "" )
     */

    UTF_REQUIRE_EQUAL( DataBlock::defaultCapacity(), DataBlock::createInstance( 0U ) -> capacity() );

    /*
     * copy( block ) with no pool - the post conditions four production sites depend on
     */

    const auto src = DataBlock::createInstance( 64U );

    src -> reset();
    src -> write( "abcdef", 6U );
    src -> setOffset1( 4U );

    {
        const auto dup = DataBlock::copy( src );

        UTF_REQUIRE_EQUAL( 6U, dup -> size() );
        UTF_REQUIRE_EQUAL( 4U, dup -> offset1() );
        UTF_REQUIRE_EQUAL( 64U, dup -> capacity() );

        UTF_REQUIRE( src -> begin() != dup -> begin() );
        UTF_REQUIRE( 0 == std::memcmp( src -> begin(), dup -> begin(), 6U ) );

        /*
         * A deep copy - losing that would alias the two blocks
         */

        dup -> begin()[ 0 ] = 'Z';

        UTF_REQUIRE_EQUAL( 'a', src -> begin()[ 0 ] );
    }

    /*
     * copy( block, pool ) - same post conditions, and the pooled block is genuinely reused
     */

    {
        auto pooledDup = DataBlock::copy( src, pool );

        UTF_REQUIRE_EQUAL( 6U, pooledDup -> size() );
        UTF_REQUIRE_EQUAL( 4U, pooledDup -> offset1() );
        UTF_REQUIRE( 0 == std::memcmp( src -> begin(), pooledDup -> begin(), 6U ) );

        const auto* const reused = pooledDup -> begin();

        pool -> put( std::move( pooledDup ) );

        const auto pooledDup2 = DataBlock::copy( src, pool );

        UTF_REQUIRE_EQUAL( reused, pooledDup2 -> begin() );
        UTF_REQUIRE_EQUAL( 6U, pooledDup2 -> size() );
        UTF_REQUIRE_EQUAL( 4U, pooledDup2 -> offset1() );
        UTF_REQUIRE( 0 == std::memcmp( src -> begin(), pooledDup2 -> begin(), 6U ) );
    }

    /*
     * The free data overload starts from a clean block - no offset1 to restore
     */

    {
        const auto fromRaw = DataBlock::copy( "xyz", 3U );

        UTF_REQUIRE_EQUAL( 3U, fromRaw -> size() );
        UTF_REQUIRE_EQUAL( 0U, fromRaw -> offset1() );
        UTF_REQUIRE_EQUAL( DataBlock::defaultCapacity(), fromRaw -> capacity() );
    }
}
