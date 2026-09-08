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

#include <baselib/core/OS.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfArgsParser.h>


/************************************************************************
 * Abstract process priority tests
 *
 * NOTE:
 * on Unix, it is possible to call nice/setpriority and lower a process'
 * priority, but one can't raise it back to what it was at process creation
 * unless its owner has superuser privileges.
 *
 * for this reason, the test below has a side effect on the process (i.e., it
 * lowers its priority) and, hence, we must make it a standalone test.
 *
 */

UTF_AUTO_TEST_CASE( BaseLib_AbstractPriorityTests )
{
    using namespace bl;

    /*
     * Call the API twice to ensure it is idempotent (i.e. if the abstract priority is
     * already set to the requested value the call should be NOP)
     */

    os::setAbstractPriority( os::AbstractPriority::Background );
    os::setAbstractPriority( os::AbstractPriority::Background );

#if ! defined( _WIN32 )

    /*
     * The observable effect of the API - Background maps to the nice value NZERO - 1
     *
     * errno must be cleared first, because -1 is itself a legal nice value and is therefore
     * not distinguishable from a getpriority() failure by the return value alone
     */

    errno = 0;

    const int backgroundPriority = ::getpriority( PRIO_PROCESS, 0 );

    UTF_REQUIRE_EQUAL( 0, errno );
    UTF_REQUIRE_EQUAL( 19, backgroundPriority );

#endif // ! defined( _WIN32 )

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Abstract priority expected to be set to 'Background'"
        );

    if( test::UtfArgsParser::isClient() )
    {
        os::sleep( time::seconds( 30 ) );
    }

    bool ok = true;

    /*
     * Call the API twice to ensure it is idempotent (i.e. if the abstract priority is
     * already set to the requested value the call should be NOP)
     */

    ok = ok && os::trySetAbstractPriority( os::AbstractPriority::Normal );
    ok = ok && os::trySetAbstractPriority( os::AbstractPriority::Normal );

    if( ! ok )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "Ignoring expected failures from os::trySetAbstractPriority( ... )"
            );
    }

#if ! defined( _WIN32 )

    /*
     * Whether raising the priority back is permitted at all depends on the privileges of the
     * process, and containerised CI frequently runs as root - so both outcomes are asserted
     * rather than only the one this machine happens to take
     */

    if( os::isUserAdministrator() )
    {
        UTF_REQUIRE( ok );
        UTF_REQUIRE_EQUAL( 0, ::getpriority( PRIO_PROCESS, 0 ) );
    }
    else
    {
        /*
         * trySetAbstractPriority swallows only EPERM / EACCES from eh::generic_category and
         * reports the failure by returning false; a failed attempt must also leave the nice
         * value exactly where it was, i.e. it must not partially apply
         */

        UTF_REQUIRE( ! ok );
        UTF_REQUIRE( ! os::trySetAbstractPriority( os::AbstractPriority::Normal ) );
        UTF_REQUIRE_EQUAL( 19, ::getpriority( PRIO_PROCESS, 0 ) );

        /*
         * ... and the non-swallowing entry point propagates it
         */

        UTF_REQUIRE_THROW(
            os::setAbstractPriority( os::AbstractPriority::Normal ),
            bl::SystemException
            );
    }

#endif // ! defined( _WIN32 )

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Abstract priority expected to be set to 'Normal'"
        );

    if( test::UtfArgsParser::isClient() )
    {
        os::sleep( time::seconds( 30 ) );
    }

    ok = true;

    /*
     * Call the API twice to ensure it is idempotent (i.e. if the abstract priority is
     * already set to the requested value the call should be NOP)
     */

    ok = ok && os::trySetAbstractPriority( os::AbstractPriority::Greedy );
    ok = ok && os::trySetAbstractPriority( os::AbstractPriority::Greedy );

    if( ! ok )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "Ignoring expected failures from os::trySetAbstractPriority( ... )"
            );
    }

#if ! defined( _WIN32 )

    /*
     * Greedy maps to the other end of the range, 0 - NZERO - which pins the mapping against
     * Background and Greedy being swapped
     */

    if( os::isUserAdministrator() )
    {
        UTF_REQUIRE( ok );
        UTF_REQUIRE_EQUAL( -20, ::getpriority( PRIO_PROCESS, 0 ) );
    }

#endif // ! defined( _WIN32 )

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Abstract priority expected to be set to 'Greedy'"
        );

    if( test::UtfArgsParser::isClient() )
    {
        os::sleep( time::seconds( 30 ) );
    }

    /*
     * The default accessor pair is just a round trip over a static and is platform agnostic
     *
     * It is restored to Normal at the end, which is what UtfMain.h's DefaultUtfConfigT sets
     * at startup
     */

    os::setAbstractPriorityDefault( os::AbstractPriority::Greedy );
    UTF_REQUIRE_EQUAL( os::AbstractPriority::Greedy, os::getAbstractPriorityDefault() );

    os::setAbstractPriorityDefault( os::AbstractPriority::Normal );
    UTF_REQUIRE_EQUAL( os::AbstractPriority::Normal, os::getAbstractPriorityDefault() );
}
