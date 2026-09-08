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

#include <baselib/data/DataBlock.h>

#include <baselib/core/EcUtils.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/Logging.h>
#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/ThreadPoolImpl.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    /*
     * A line logger which discards everything
     *
     * eh::EcUtils::getErrorCode( ... ) logs the exception it catches at error *and* at
     * debug level and UtfMain.h maps LL_ERROR onto BOOST_ERROR, so every case which
     * drives a failing callback through it must be run under this logger
     */

    void noopLineLogger(
        SAA_in      const std::string&                          prefix,
        SAA_in      const std::string&                          text,
        SAA_in      const bool                                  enableTimestamp,
        SAA_in      const bl::Logging::Level                    level
        )
    {
        BL_UNUSED( prefix );
        BL_UNUSED( text );
        BL_UNUSED( enableTimestamp );
        BL_UNUSED( level );
    }

    /*
     * A bounded poll - returns true if the predicate became true within the budget
     */

    bool waitForPredicate( SAA_in const bl::cpp::function< bool () >& predicate )
    {
        for( std::size_t i = 0U; i < 100U; ++i )
        {
            if( predicate() )
            {
                return true;
            }

            bl::os::sleep( bl::time::milliseconds( 50 ) );
        }

        return predicate();
    }

    /*
     * Verifies that the exception carries both halves of the plug-in boundary protocol -
     * an errinfo_errno and a *generic* category errinfo_error_code with the same value
     */

    bool matchGenericErrnoErrorInfo(
        SAA_in      const bl::UnexpectedException&              exception,
        SAA_in      const int                                   expected
        )
    {
        const auto* errNo = bl::eh::get_error_info< bl::eh::errinfo_errno >( exception );

        if( nullptr == errNo || expected != *errNo )
        {
            return false;
        }

        const auto* errorCode = bl::eh::get_error_info< bl::eh::errinfo_error_code >( exception );

        if( nullptr == errorCode )
        {
            return false;
        }

        return (
            expected == errorCode -> value() &&
            errorCode -> category() == bl::eh::generic_category()
            );
    }

} // __unnamed

/************************************************************************
 * Thread pool exception handling tests
 */

UTF_AUTO_TEST_CASE( BaseLib_ThreadPoolExceptionHandlingTests )
{
    std::atomic< std::size_t > handled( 0U );

    const auto tp = bl::om::lockDisposable(
        bl::ThreadPoolImpl::createInstance< bl::ThreadPool >(
            bl::os::AbstractPriority::Normal,
            2U                                              /* threadsCount */,
            false                                           /* abortIfUnhandled */,
            bl::eh::eh_callback_t(
                [ &handled ]( SAA_in const std::exception_ptr& eptr ) -> bool
                {
                    BL_UNUSED( eptr );

                    ++handled;

                    return true;
                }
                )
            )
        );

    UTF_REQUIRE( ! tp -> lastException() );

    tp -> aioService().post(
        []() -> void
        {
            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "thread pool test exception"
                );
        }
        );

    /*
     * Note that if the exception handling callback wiring ever regresses so that the
     * callback is not consulted at all then handleException( ... ) takes its BL_RIP_MSG
     * branch and aborts the whole test binary - i.e. that particular regression shows up
     * as a crashed module and not as a failed assertion here
     */

    UTF_REQUIRE(
        waitForPredicate(
            [ &handled ]() -> bool
            {
                return 0U != handled.load();
            }
            )
        );

    UTF_REQUIRE_EQUAL( 1U, handled.load() );

    /*
     * The exception is latched into m_lastException *before* the callback is invoked,
     * so it is already visible once the counter has been observed
     */

    const auto eptr = tp -> lastException();

    UTF_REQUIRE( eptr );

    UTF_REQUIRE_THROW_MESSAGE(
        bl::cpp::safeRethrowException( eptr ),
        bl::UnexpectedException,
        "thread pool test exception"
        );

    /*
     * A second handler must be executed - this is what proves the worker resumed its
     * loop rather than exiting and silently shrinking the pool
     */

    std::atomic< bool > secondRan( false );

    tp -> aioService().post(
        [ &secondRan ]() -> void
        {
            secondRan = true;
        }
        );

    UTF_REQUIRE(
        waitForPredicate(
            [ &secondRan ]() -> bool
            {
                return secondRan.load();
            }
            )
        );

    UTF_REQUIRE_EQUAL( 2U, tp -> size() );
}

/************************************************************************
 * Logging level precedence tests
 */

UTF_AUTO_TEST_CASE( BaseLib_LoggingLevelPrecedenceTests )
{
    using bl::Logging;

    /*
     * UtfMain.h sets the *global* logging level and leaves the thread local level at
     * LL_DEFAULT, so getLevel() reads the global level inside a test case
     */

    Logging::LevelPusher pushGlobal( Logging::LL_ERROR, true /* global */ );

    UTF_REQUIRE_EQUAL( Logging::LL_ERROR, Logging::getLevel() );
    UTF_REQUIRE( Logging::error().isEnabled() );
    UTF_REQUIRE( ! Logging::warning().isEnabled() );
    UTF_REQUIRE( ! Logging::isVerboseModeEnabled() );

    {
        Logging::LevelPusher pushTls( Logging::LL_TRACE, false /* global */ );

        UTF_REQUIRE_EQUAL( Logging::LL_TRACE, Logging::getLevel() );
        UTF_REQUIRE( Logging::trace().isEnabled() );
        UTF_REQUIRE( Logging::isVerboseModeEnabled() );

        {
            /*
             * This scope is kept as short as possible - while the global level is
             * LL_NONE the thread pool threads in this module stop logging
             */

            Logging::LevelPusher pushGlobal2( Logging::LL_NONE, true /* global */ );

            int otherLevel = -1;

            bl::os::thread t(
                [ &otherLevel ]() -> void
                {
                    otherLevel = ( int ) Logging::getLevel();
                }
                );

            t.join();

            /*
             * The thread local override masks the global level on this thread ...
             */

            UTF_REQUIRE_EQUAL( Logging::LL_TRACE, Logging::getLevel() );

            /*
             * ... while a fresh thread with no thread local override sees the global one
             */

            UTF_REQUIRE_EQUAL( ( int ) Logging::LL_NONE, otherLevel );
        }
    }

    /*
     * The nested pushers must have restored LIFO and through the right channel each
     */

    UTF_REQUIRE_EQUAL( Logging::LL_ERROR, Logging::getLevel() );
    UTF_REQUIRE( ! Logging::isVerboseModeEnabled() );
}

/************************************************************************
 * eh::EcUtils::getErrorCode( ... ) mapping tests
 */

UTF_AUTO_TEST_CASE( BaseLib_EcUtilsGetErrorCodeMapping )
{
    bl::Logging::LineLoggerPusher pushLogger( &noopLineLogger );

    /*
     * A callback which returns normally maps onto errc::success
     */

    UTF_CHECK_EQUAL(
        ( int ) bl::eh::errc::success,
        bl::eh::EcUtils::getErrorCode( [](){} )
        );

    /*
     * An attached errinfo_errno is propagated verbatim
     */

    UTF_CHECK_EQUAL(
        EACCES,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW(
                    bl::UnexpectedException()
                        << bl::eh::errinfo_errno( EACCES ),
                    BL_MSG()
                        << "x"
                    );
            }
            )
        );

    /*
     * A generic category code goes through SystemException::create( ... ), which attaches
     * both an errinfo_errno and an errinfo_error_code - the errno branch wins
     */

    UTF_CHECK_EQUAL(
        ENOENT,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW_EC(
                    bl::eh::error_code( ENOENT, bl::eh::generic_category() ),
                    BL_MSG()
                        << "x"
                    );
            }
            )
        );

    /*
     * A generic category code with no errno at all takes the second stage branch
     */

    UTF_CHECK_EQUAL(
        EPERM,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW(
                    bl::UnexpectedException()
                        << bl::eh::errinfo_error_code(
                            bl::eh::error_code( EPERM, bl::eh::generic_category() )
                            ),
                    BL_MSG()
                        << "x"
                    );
            }
            )
        );

    /*
     * A system category code carries an errinfo_system_code and *not* an errinfo_errno,
     * so every system category failure collapses onto errc::no_message here
     */

    UTF_CHECK_EQUAL(
        ( int ) bl::eh::errc::no_message,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW_EC(
                    bl::eh::error_code( 5, bl::eh::system_category() ),
                    BL_MSG()
                        << "x"
                    );
            }
            )
        );

    /*
     * A plain std exception carries no error info at all
     */

    UTF_CHECK_EQUAL(
        ( int ) bl::eh::errc::no_message,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                throw std::runtime_error( "plain" );
            }
            )
        );

    /*
     * The zero boundary - this pins the *current* behavior: an exception which carries a
     * zero errno and a zero generic code is reported as success across the plug-in ABI,
     * i.e. a thrown exception is silently flattened into 'no error'
     *
     * Changing this must be a deliberate edit and not a silent one
     */

    UTF_CHECK_EQUAL(
        0,
        bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW(
                    bl::UnexpectedException()
                        << bl::eh::errinfo_errno( 0 )
                        << bl::eh::errinfo_error_code(
                            bl::eh::error_code( 0, bl::eh::generic_category() )
                            ),
                    BL_MSG()
                        << "x"
                    );
            }
            )
        );
}

/************************************************************************
 * eh::EcUtils::checkErrorCode( ... ) and the getErrorCode round trip tests
 */

UTF_AUTO_TEST_CASE( BaseLib_EcUtilsCheckErrorCodeAndRoundTrip )
{
    /*
     * A zero rc is a no-op
     */

    UTF_REQUIRE_NO_THROW( bl::eh::EcUtils::checkErrorCode( 0 ) );

    /*
     * The message is always built from the *generic* category
     */

    UTF_REQUIRE_THROW_MESSAGE(
        bl::eh::EcUtils::checkErrorCode( EACCES ),
        bl::UnexpectedException,
        bl::eh::error_code( EACCES, bl::eh::generic_category() ).message()
        );

    /*
     * Both halves every downstream reader depends on must be attached - eh::
     * errorCodeFromExceptionPtr( ... ) reads the code and BaseException::errNo() the errno
     */

    UTF_REQUIRE_EXCEPTION(
        bl::eh::EcUtils::checkErrorCode( ENOENT ),
        bl::UnexpectedException,
        []( SAA_in const bl::UnexpectedException& exception ) -> bool
        {
            return matchGenericErrnoErrorInfo( exception, ENOENT );
        }
        );

    /*
     * The full round trip across a plug-in boundary - the plug-in flattens the exception
     * into an int with getErrorCode( ... ) and the host inflates it with checkErrorCode( ... )
     */

    int rc = 0;

    {
        bl::Logging::LineLoggerPusher pushLogger( &noopLineLogger );

        rc = bl::eh::EcUtils::getErrorCode(
            []() -> void
            {
                BL_THROW(
                    bl::UnexpectedException()
                        << bl::eh::errinfo_errno( EACCES ),
                    BL_MSG()
                        << "x"
                    );
            }
            );
    }

    UTF_REQUIRE_EQUAL( EACCES, rc );

    UTF_REQUIRE_EXCEPTION(
        bl::eh::EcUtils::checkErrorCode( rc ),
        bl::UnexpectedException,
        []( SAA_in const bl::UnexpectedException& exception ) -> bool
        {
            return matchGenericErrnoErrorInfo( exception, EACCES );
        }
        );
}

/************************************************************************
 * data::DataBlock checked setters tests
 */

UTF_AUTO_TEST_CASE( BaseLib_DataBlockCheckedSettersTests )
{
    using namespace bl;
    using namespace bl::data;

    /*
     * These two setters are the only guards which survive a release build and stand
     * between a peer supplied length and a wrap of size() - offset1()
     */

    const auto block = DataBlock::createInstance( 256U );

    block -> reset();

    UTF_REQUIRE_EQUAL( 256U, block -> capacity() );

    UTF_REQUIRE_NO_THROW( block -> setSizeChecked( 256U ) );
    UTF_REQUIRE_EQUAL( 256U, block -> size() );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> setSizeChecked( 257U ),
        BufferTooSmallException,
        resolveMessage(
            BL_MSG()
                << "Invalid data block size "
                << 257U
                << " (capacity is "
                << 256U
                << " and the protocol data offset is "
                << 0U
                << ")"
            )
        );

    /*
     * The check is made before the assignment, so a rejected call must leave the
     * block untouched
     */

    UTF_REQUIRE_EQUAL( 256U, block -> size() );

    /*
     * offset1 == size is legal - it simply means an empty payload
     */

    UTF_REQUIRE_NO_THROW( block -> setOffset1Checked( 256U ) );
    UTF_REQUIRE_EQUAL( 0U, block -> size() - block -> offset1() );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> setOffset1Checked( 257U ),
        BufferTooSmallException,
        "Invalid data block protocol data offset 257"
        );

    UTF_REQUIRE_EQUAL( 256U, block -> offset1() );

    /*
     * The second conjunct of setSizeChecked( ... ) - m_offset1 <= size - is a separate
     * defence from the capacity one and it is what keeps size() - offset1() from wrapping
     */

    block -> setOffset1Checked( 100U );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> setSizeChecked( 99U ),
        BufferTooSmallException,
        "Invalid data block size 99"
        );

    UTF_REQUIRE_EQUAL( 256U, block -> size() );
    UTF_REQUIRE_EQUAL( 100U, block -> offset1() );

    /*
     * ... and the size == offset1 boundary is accepted
     */

    UTF_REQUIRE_NO_THROW( block -> setSizeChecked( 100U ) );

    /*
     * An absurd wire value must be rejected rather than wrap
     */

    const auto maxSize = std::numeric_limits< std::size_t >::max();

    UTF_REQUIRE_THROW( block -> setSizeChecked( maxSize ), BufferTooSmallException );
    UTF_REQUIRE_THROW( block -> setOffset1Checked( maxSize ), BufferTooSmallException );

    UTF_REQUIRE_NO_THROW( block -> setOffset1Checked( 40U ) );

    UTF_REQUIRE_EQUAL( 100U, block -> size() );
    UTF_REQUIRE_EQUAL( 40U, block -> offset1() );
    UTF_REQUIRE_EQUAL( 60U, block -> size() - block -> offset1() );
}
