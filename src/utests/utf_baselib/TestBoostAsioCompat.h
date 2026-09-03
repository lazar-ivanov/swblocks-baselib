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

#include <string>

#include <utests/baselib/Utf.h>

/*
 * These tests verify that the resolver query semantics - the protocol constraint and the
 * resolve flags - are preserved end to end
 *
 * They are deliberately written against the API surface which is common to all supported
 * Boost versions (query construction, resolve, async_resolve and endpoint iteration), so
 * the same tests execute both against the Boost.Asio compatibility layer (Boost 1.89+) and
 * against the native Boost query API of the older environments. Identical results on both
 * is what proves the compatibility layer is semantically equivalent
 *
 * All the host names used below are local, so the tests do not depend on external DNS
 */

namespace
{
    namespace utest_asio_compat
    {
        /*
         * Boost 1.66+ (devenv4+): resolve() returns results_type with begin()/end()
         * Boost <=1.63 (devenv2-3): resolve() returns the iterator directly
         *
         * The iterator holds a shared reference to the resolved values, so it remains
         * valid after the results object goes out of scope
         */

        template
        <
            typename RESOLVER
        >
        typename RESOLVER::iterator resolveQuery(
            SAA_in          RESOLVER&                                   resolver,
            SAA_in          const typename RESOLVER::query&             query,
            SAA_out         bl::eh::error_code&                         ec
            )
        {
#if BL_DEVENV_VERSION >= 4
            return resolver.resolve( query, ec ).begin();
#else
            return resolver.resolve( query, ec );
#endif
        }

        template
        <
            typename ITERATOR
        >
        std::size_t countEndpoints( SAA_in ITERATOR pos )
        {
            const ITERATOR end;

            std::size_t count = 0U;

            for( ; pos != end; ++pos )
            {
                ++count;
            }

            return count;
        }

        /*
         * Verifies that every resolved endpoint honors the protocol constraint which was
         * requested by the query - i.e. that the constraint was not dropped on the way to
         * the resolver
         */

        template
        <
            typename ITERATOR
        >
        void checkAllEndpointsAreV4( SAA_in ITERATOR pos )
        {
            const ITERATOR end;

            for( ; pos != end; ++pos )
            {
                UTF_CHECK( pos -> endpoint().address().is_v4() );
            }
        }

        template
        <
            typename ITERATOR
        >
        void checkAllEndpointsAreV6( SAA_in ITERATOR pos )
        {
            const ITERATOR end;

            for( ; pos != end; ++pos )
            {
                UTF_CHECK( pos -> endpoint().address().is_v6() );
            }
        }

    } // utest_asio_compat

} // __unnamed

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverProtocolConstraintSync )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query queryV4( asio::ip::tcp::v4(), "localhost", "80" );

    const auto endpointsV4 = utest_asio_compat::resolveQuery( resolver, queryV4, ec );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpointsV4 ) );

    utest_asio_compat::checkAllEndpointsAreV4( endpointsV4 );

    /*
     * IPv6 might not be configured on the host, in which case the resolve is expected
     * to fail and there is nothing to verify
     */

    const resolver_t::query queryV6( asio::ip::tcp::v6(), "localhost", "80" );

    const auto endpointsV6 = utest_asio_compat::resolveQuery( resolver, queryV6, ec );

    if( ec )
    {
        UTF_MESSAGE( "IPv6 is not available on this host; skipping the IPv6 checks" );
    }
    else
    {
        utest_asio_compat::checkAllEndpointsAreV6( endpointsV6 );
    }
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverProtocolConstraintAsync )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The synchronous and the asynchronous resolve paths forward the query separately,
     * so the protocol constraint must be verified for both
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );

    eh::error_code ec;
    std::size_t count = 0U;
    bool completed = false;

    const resolver_t::query query( asio::ip::tcp::v4(), "localhost", "80" );

    resolver.async_resolve(
        query,
        [ & ]( SAA_in const eh::error_code& code, SAA_in resolver_t::iterator endpoints ) -> void
        {
            completed = true;
            ec = code;

            if( ! code )
            {
                count = utest_asio_compat::countEndpoints( endpoints );
                utest_asio_compat::checkAllEndpointsAreV4( endpoints );
            }
        }
        );

    ioService.run();

    UTF_REQUIRE( completed );
    UTF_REQUIRE( ! ec );
    UTF_CHECK( 0U != count );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverCanonicalNameFlag )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * This is the query which net::getCanonicalHostName() relies upon
     *
     * Note that on hosts where the queried name is already canonical this check passes
     * either way; BoostAsioCompat_ResolverNumericHostFlag is the deterministic proof that
     * the flags are forwarded
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query query(
        "localhost"                                         /* host_name */,
        str::empty()                                        /* service_name */,
        asio::ip::resolver_query_base::canonical_name       /* flags */
        );

    const auto endpoints = utest_asio_compat::resolveQuery( resolver, query, ec );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpoints ) );

    UTF_CHECK( ! endpoints -> host_name().empty() );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverNumericHostFlag )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The numeric_host flag has deterministic, environment independent behavior - a non
     * numeric host must not resolve when it is requested - which makes this the strongest
     * proof that the query flags actually reach the resolver
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query nonNumeric(
        "localhost"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    ( void ) utest_asio_compat::resolveQuery( resolver, nonNumeric, ec );

    UTF_CHECK( !! ec );

    const resolver_t::query numeric(
        "127.0.0.1"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    const auto endpoints = utest_asio_compat::resolveQuery( resolver, numeric, ec );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpoints ) );

    UTF_CHECK_EQUAL( endpoints -> endpoint().address().to_string(), std::string( "127.0.0.1" ) );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverPassiveDefaultFlags )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The service only query defaults to 'passive | address_configured' - with the passive
     * flag the resolved endpoints are the wildcard address, without it they are loopback.
     * This verifies that the default flags of the query constructors are preserved
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query passiveQuery( "80" /* service_name */ );

    auto endpoints = utest_asio_compat::resolveQuery( resolver, passiveQuery, ec );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpoints ) );

    {
        const decltype( endpoints ) end;

        for( auto pos = endpoints; pos != end; ++pos )
        {
            UTF_CHECK( pos -> endpoint().address().is_unspecified() );
        }
    }

    const resolver_t::query nonPassiveQuery(
        str::empty()                                        /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::address_configured   /* flags */
        );

    endpoints = utest_asio_compat::resolveQuery( resolver, nonPassiveQuery, ec );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpoints ) );

    {
        const decltype( endpoints ) end;

        for( auto pos = endpoints; pos != end; ++pos )
        {
            UTF_CHECK( pos -> endpoint().address().is_loopback() );
        }
    }
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverAllMatchingFlag )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * This is the query shape used by the TCP connection establisher tasks
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query query(
        "localhost"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::all_matching         /* flags */
        );

    const auto endpoints = utest_asio_compat::resolveQuery( resolver, query, ec );

    UTF_REQUIRE( ! ec );
    UTF_CHECK( 0U != utest_asio_compat::countEndpoints( endpoints ) );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverIcmpProtocolConstraint )
{
    using namespace bl;

    typedef asio::ip::icmp_resolver resolver_t;

    /*
     * This is the query shape used by the ICMP pinger task - the resolved endpoint is
     * passed to a socket which was opened as icmp::v4(), so an IPv6 endpoint here would
     * be unusable
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query query(
        asio::ip::icmp::v4()                                /* protocol */,
        "localhost"                                         /* host_name */,
        str::empty()                                        /* service_name */
        );

    const auto endpoints = utest_asio_compat::resolveQuery( resolver, query, ec );

    if( ec )
    {
        /*
         * Some platforms reject raw socket hints in getaddrinfo; a failure to resolve is
         * acceptable, but resolving to a non-IPv4 endpoint never is
         */

        UTF_MESSAGE( "The ICMP resolve has failed on this host; skipping the endpoint checks" );

        return;
    }

    UTF_REQUIRE( 0U != utest_asio_compat::countEndpoints( endpoints ) );

    utest_asio_compat::checkAllEndpointsAreV4( endpoints );
}

#if BOOST_VERSION >= 108900

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverQueryAccessors )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;
    typedef asio::ip::resolver_query_base flags_t;

    /*
     * Verify that the compatibility query retains everything it was constructed with,
     * including the default flags of each of the constructor forms. This catches a
     * regression in the query forwarding without depending on any resolver behavior
     */

    const resolver_t::query serviceOnly( "80" );

    UTF_CHECK( ! serviceOnly.has_protocol() );
    UTF_CHECK_EQUAL( serviceOnly.host_name(), str::empty() );
    UTF_CHECK_EQUAL( serviceOnly.service_name(), std::string( "80" ) );
    UTF_CHECK( serviceOnly.flags_value() == ( flags_t::passive | flags_t::address_configured ) );

    const resolver_t::query serviceWithProtocol( asio::ip::tcp::v6(), "80" );

    UTF_CHECK( serviceWithProtocol.has_protocol() );
    UTF_CHECK( serviceWithProtocol.protocol() == asio::ip::tcp::v6() );
    UTF_CHECK_EQUAL( serviceWithProtocol.service_name(), std::string( "80" ) );
    UTF_CHECK(
        serviceWithProtocol.flags_value() == ( flags_t::passive | flags_t::address_configured )
        );

    const resolver_t::query hostAndService( "localhost", "80" );

    UTF_CHECK( ! hostAndService.has_protocol() );
    UTF_CHECK_EQUAL( hostAndService.host_name(), std::string( "localhost" ) );
    UTF_CHECK_EQUAL( hostAndService.service_name(), std::string( "80" ) );
    UTF_CHECK( hostAndService.flags_value() == flags_t::address_configured );

    const resolver_t::query fullyQualified(
        asio::ip::tcp::v4(),
        "localhost",
        "80",
        flags_t::canonical_name
        );

    UTF_CHECK( fullyQualified.has_protocol() );
    UTF_CHECK( fullyQualified.protocol() == asio::ip::tcp::v4() );
    UTF_CHECK_EQUAL( fullyQualified.host_name(), std::string( "localhost" ) );
    UTF_CHECK_EQUAL( fullyQualified.service_name(), std::string( "80" ) );
    UTF_CHECK( fullyQualified.flags_value() == flags_t::canonical_name );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverAsyncHandlerIsStoredByValue )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The async_resolve() shim stores the handler inside a wrapper object which it then hands to
     * the asynchronous operation, so the wrapper outlives the call which created it
     *
     * The handler template parameter is deduced from a forwarding reference, so an lvalue
     * handler deduces to a reference type; unless it is decayed the wrapper's member becomes a
     * reference bound to the caller's object rather than a copy of it, and that reference
     * dangles once the caller's scope ends
     *
     * The assertion below is on the handler being COPIED rather than on the dangling read,
     * deliberately: reading through a dangling reference is undefined behavior which in
     * practice usually still returns the expected value, so a test written that way passes
     * whether or not the bug is present. Copying is directly observable and deterministic -
     * storing by value must copy the handler, storing by reference cannot.
     *
     * Note that Asio copies the wrapper itself as it moves the operation around, but in the
     * un-decayed case the wrapper holds only a reference, so copying it still does not copy the
     * handler and the count stays at zero
     */

    struct CopyCountingHandler
    {
        std::size_t*        m_copies;
        bool*               m_completed;
        eh::error_code*     m_ec;

        CopyCountingHandler(
            SAA_in          std::size_t*                            copies,
            SAA_in          bool*                                   completed,
            SAA_in          eh::error_code*                         ec
            )
            :
            m_copies( copies ),
            m_completed( completed ),
            m_ec( ec )
        {
        }

        CopyCountingHandler( SAA_in const CopyCountingHandler& other )
            :
            m_copies( other.m_copies ),
            m_completed( other.m_completed ),
            m_ec( other.m_ec )
        {
            ++( *m_copies );
        }

        void operator()( SAA_in const eh::error_code& code, SAA_in resolver_t::iterator endpoints ) const
        {
            BL_UNUSED( endpoints );

            *m_completed = true;
            *m_ec = code;
        }
    };

    asio::io_service ioService;
    resolver_t resolver( ioService );

    std::size_t copies = 0U;
    bool completed = false;
    eh::error_code ec;

    const resolver_t::query query( asio::ip::tcp::v4(), "localhost", "80" );

    /*
     * The handler is deliberately a named local, i.e. an lvalue
     */

    CopyCountingHandler handler( &copies, &completed, &ec );

    resolver.async_resolve( query, handler );

    UTF_REQUIRE( 0U != copies );

    ioService.run();

    UTF_REQUIRE( completed );
    UTF_REQUIRE( ! ec );
}

#endif // BOOST_VERSION >= 108900
