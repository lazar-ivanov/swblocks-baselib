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

#include <baselib/core/NetUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstring>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * These tests drive bl::BoostAsioErrorCallback directly - the process-global hook which
 * replaces the Boost.Asio default error handling
 *
 * The hook is a mutable process-global with no synchronization and it is read from ASIO
 * handler threads, so these cases must stay in a module which is single threaded at this
 * point and must never run concurrently with any network activity
 */

UTF_AUTO_TEST_CASE( BoostAsioErrorCallback_InstallAndRestore )
{
    using namespace bl;

    std::size_t count = 0U;
    eh::error_code observedEc;
    const char* observedLocation = nullptr;
    bool locationWasNull = false;

    const auto observer = [ &count, &observedEc, &observedLocation, &locationWasNull ](
        SAA_in          const eh::error_code&                       ec,
        SAA_in_opt      const char*                                 location
        ) -> void
    {
        ++count;
        observedEc = ec;
        observedLocation = location;
        locationWasNull = ( nullptr == location );
    };

    auto previous = BoostAsioErrorCallback::installCallback(
        BoostAsioErrorCallback::asio_error_callback_t( observer )
        );

    /*
     * The callback is a process-global, so the previous one - installed by Utf.h at
     * start-up - must be restored unconditionally, otherwise every case which runs after
     * this one silently loses bl::cmdline::EhUtils::asioErrorCallback
     */

    BL_SCOPE_EXIT(
        {
            BoostAsioErrorCallback::installCallback( std::move( previous ) );
        }
        );

    UTF_REQUIRE( !! previous );

    /*
     * A success code short-circuits before the callback is consulted
     */

    BoostAsioErrorCallback::defaultCallback( eh::error_code(), "loc" );

    UTF_REQUIRE_EQUAL( 0U, count );

    const auto ec = eh::errc::make_error_code( eh::errc::permission_denied );

    BoostAsioErrorCallback::defaultCallback( ec, "myloc" );

    UTF_REQUIRE_EQUAL( 1U, count );
    UTF_REQUIRE( observedEc == ec );
    UTF_REQUIRE( nullptr != observedLocation );
    UTF_REQUIRE_EQUAL( 0, std::strcmp( observedLocation, "myloc" ) );

    /*
     * The 1-argument throw_error( ec ) overload passes a null location
     */

    boost::asio::detail::throw_error( ec );

    UTF_REQUIRE_EQUAL( 2U, count );
    UTF_REQUIRE( locationWasNull );

    boost::asio::detail::throw_error( ec, "other" );

    UTF_REQUIRE_EQUAL( 3U, count );
    UTF_REQUIRE( ! locationWasNull );
    UTF_REQUIRE( nullptr != observedLocation );
    UTF_REQUIRE_EQUAL( 0, std::strcmp( observedLocation, "other" ) );

    /*
     * A success code never reaches the callback, not even through throw_error( ... )
     */

    boost::asio::detail::throw_error( eh::error_code() );

    UTF_REQUIRE_EQUAL( 3U, count );
}

UTF_AUTO_TEST_CASE( BoostAsioErrorCallback_DefaultPathWithNullLocation )
{
    using namespace bl;

    /*
     * With no callback installed the default path constructs and throws a plain
     * boost::system::system_error rather than a bl::SystemException
     */

    auto previous = BoostAsioErrorCallback::installCallback(
        BoostAsioErrorCallback::asio_error_callback_t()
        );

    BL_SCOPE_EXIT(
        {
            BoostAsioErrorCallback::installCallback( std::move( previous ) );
        }
        );

    const auto ec = eh::errc::make_error_code( eh::errc::permission_denied );

    /*
     * A null location must be replaced with the "asio" default - std::runtime_error
     * must never be constructed from a null pointer
     */

    UTF_REQUIRE_THROW_MESSAGE(
        BoostAsioErrorCallback::defaultCallback( ec, nullptr ),
        eh::system_error,
        "asio"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        BoostAsioErrorCallback::defaultCallback( ec, "explicit" ),
        eh::system_error,
        "explicit"
        );

    UTF_REQUIRE_NO_THROW( BoostAsioErrorCallback::defaultCallback( eh::error_code(), nullptr ) );
}
