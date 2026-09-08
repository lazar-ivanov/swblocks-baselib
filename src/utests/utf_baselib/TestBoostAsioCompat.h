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

#include <atomic>
#include <cstddef>
#include <iterator>
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
         *
         * Gated on BOOST_VERSION rather than BL_DEVENV_VERSION for the reason given at the
         * same gate in baselib/core/NetUtils.h
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
#if BOOST_VERSION >= 106600
            return resolver.resolve( query, ec ).begin();
#else
            return resolver.resolve( query, ec );
#endif
        }

        /*
         * The throwing form of resolve( query ) - the one which reports failures through
         * boost::asio::detail::throw_error( ... ) and therefore through the baselib error
         * callback which replaces it
         *
         * It carries the same BOOST_VERSION gate as resolveQuery( ... ) above and for the
         * same reason - only the result type differs between the two branches
         */

        template
        <
            typename RESOLVER
        >
        typename RESOLVER::iterator resolveQueryThrow(
            SAA_in          RESOLVER&                                   resolver,
            SAA_in          const typename RESOLVER::query&             query
            )
        {
#if BOOST_VERSION >= 106600
            return resolver.resolve( query ).begin();
#else
            return resolver.resolve( query );
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

    /*
     * The query constructors default to address_configured, which glibc implements as
     * AI_ADDRCONFIG and which ignores loopback, so on a host with no configured non-loopback
     * IPv4 address this resolve fails and there is nothing to verify (the same shape as the
     * IPv6 branch below)
     */

    if( ec )
    {
        UTF_MESSAGE( "No configured IPv4 address on this host (address_configured); skipping the checks" );

        return;
    }

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

    if( ec )
    {
        UTF_MESSAGE( "No configured IPv4 address on this host (address_configured); skipping the checks" );

        return;
    }

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

    if( ec )
    {
        UTF_MESSAGE( "No configured IPv4 address on this host (address_configured); skipping the checks" );

        return;
    }

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

    if( ec )
    {
        UTF_MESSAGE( "No configured IPv4 address on this host (address_configured); skipping the checks" );

        return;
    }

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

UTF_AUTO_TEST_CASE( BoostAsioCompat_AsyncConnectIteratorForm )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The single iterator form is the one used by TcpBaseTasks.h. Boost 1.89+ removed it and
     * the compatibility overload in BoostAsioCompat.h reinstates it; on the older versions
     * this exercises Boost's own overload. A listening loopback acceptor on an ephemeral
     * port is enough for a connect to succeed: the kernel completes the connection into the
     * backlog without accept() being called. Loopback is always configured, so unlike the
     * localhost resolves above this needs no skip.
     */

    asio::io_service ioService;

    asio::ip::tcp::acceptor acceptor(
        ioService,
        asio::ip::tcp::endpoint( asio::ip::address_v4::loopback(), 0 /* ephemeral port */ )
        );

    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query query(
        "127.0.0.1"                                         /* host_name */,
        std::to_string( acceptor.local_endpoint().port() )  /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    const auto endpoints = utest_asio_compat::resolveQuery( resolver, query, ec );

    UTF_REQUIRE( ! ec );

    asio::ip::tcp::socket socket( ioService );

    bool completed = false;
    eh::error_code connectEc;

    asio::async_connect(
        socket,
        endpoints,
        [ & ]( SAA_in const eh::error_code& code, SAA_in resolver_t::iterator /* connected */ ) -> void
        {
            completed = true;
            connectEc = code;
        }
        );

    ioService.run();

    UTF_REQUIRE( completed );
    UTF_REQUIRE( ! connectEc );
    UTF_CHECK( socket.is_open() );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverThrowingOverloadRoutesThroughErrorCallback )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The throwing resolve( query ) overload reports its failures through
     * boost::asio::detail::throw_error( ec, "resolve" ), which BoostAsioErrorCallback.h
     * replaces with its own inline overloads before any asio header is included
     *
     * Utf.h installs bl::cmdline::EhUtils::asioErrorCallback at start-up, so a failure here
     * must surface as a bl::SystemException carrying the asio error code and the location
     * string. The derived type is asserted deliberately - with the override gone the thrown
     * type would be exactly boost::system::system_error, which bl::SystemException derives
     * from, and all of the baselib error enrichment would be silently lost
     *
     * The numeric_host flag on a non numeric host is used to force the failure because it is
     * deterministic and environment independent, the same shape which
     * BoostAsioCompat_ResolverNumericHostFlag relies upon
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );

    const resolver_t::query nonNumeric(
        "localhost"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    UTF_REQUIRE_THROW(
        utest_asio_compat::resolveQueryThrow( resolver, nonNumeric ),
        SystemException
        );

    bool hasErrorCode = false;
    bool errorCodeIsSet = false;
    bool locationSurvived = false;

    UTF_REQUIRE_EXCEPTION(
        utest_asio_compat::resolveQueryThrow( resolver, nonNumeric ),
        SystemException,
        [ & ]( SAA_in const SystemException& ex ) -> bool
        {
            const auto* const code = eh::get_error_info< eh::errinfo_error_code >( ex );

            hasErrorCode = ( nullptr != code );
            errorCodeIsSet = hasErrorCode && ( *code != eh::error_code() );
            locationSurvived = cpp::contains( std::string( ex.what() ), "resolve" );

            return true;
        }
        );

    UTF_REQUIRE( hasErrorCode );
    UTF_REQUIRE( errorCodeIsSet );
    UTF_REQUIRE( locationSurvived );

    /*
     * The throwing compat overload branches on q.has_protocol(), so both branches must route
     * their failures the same way
     */

    const resolver_t::query nonNumericWithProtocol(
        asio::ip::tcp::v4()                                 /* protocol */,
        "localhost"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    UTF_REQUIRE_THROW(
        utest_asio_compat::resolveQueryThrow( resolver, nonNumericWithProtocol ),
        SystemException
        );

    /*
     * The positive control - the throwing overload must not throw when the resolve succeeds
     */

    const resolver_t::query numericLiteral(
        "127.0.0.1"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    UTF_REQUIRE_NO_THROW( utest_asio_compat::resolveQueryThrow( resolver, numericLiteral ) );

    UTF_REQUIRE(
        0U != utest_asio_compat::countEndpoints(
            utest_asio_compat::resolveQueryThrow( resolver, numericLiteral )
            )
        );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_IoContextPostDefersHandler )
{
    using namespace bl;

    /*
     * post() always defers, dispatch() would run the handler inline when the caller is
     * already executing inside the io_context. That difference is the exact semantic
     * tasks::TaskBase::scheduleNothrow depends upon, and swapping the two would surface
     * there as a lock re-entrancy or a hang rather than as a failed assertion
     */

    {
        asio::io_service ioService;

        bool innerRan = false;
        bool innerRanBeforeOuterReturned = false;

        ioService.post(
            [ &ioService, &innerRan, &innerRanBeforeOuterReturned ]() -> void
            {
                ioService.post(
                    [ &innerRan ]() -> void
                    {
                        innerRan = true;
                    }
                    );

                innerRanBeforeOuterReturned = innerRan;
            }
            );

        UTF_REQUIRE( ! innerRan );

        ioService.run();

        UTF_REQUIRE( innerRan );
        UTF_REQUIRE( ! innerRanBeforeOuterReturned );
    }

    /*
     * The handler is forwarded, so a move-only handler must be accepted
     *
     * A second io_service is used rather than restart() so the case stays buildable on the
     * pre-1.66 branch, and the handler is a small function object rather than a lambda
     * because a lambda cannot capture by move in C++11 - init captures are C++14
     */

    {
        struct MoveOnlyHandler
        {
            cpp::SafeUniquePtr< int >                       m_value;
            int*                                            m_observed;

            MoveOnlyHandler(
                SAA_inout       cpp::SafeUniquePtr< int >&&             value,
                SAA_in          int*                                    observed
                )
                :
                m_value( std::move( value ) ),
                m_observed( observed )
            {
            }

            MoveOnlyHandler( SAA_inout MoveOnlyHandler&& other )
                :
                m_value( std::move( other.m_value ) ),
                m_observed( other.m_observed )
            {
            }

            void operator()()
            {
                *m_observed = *m_value;
            }
        };

        asio::io_service ioService;

        int observed = 0;

        ioService.post(
            MoveOnlyHandler( cpp::SafeUniquePtr< int >::attach( new int( 42 ) ), &observed )
            );

        UTF_REQUIRE_EQUAL( 0, observed );

        ioService.run();

        UTF_REQUIRE_EQUAL( 42, observed );
    }
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverAsyncNumericHostFlagAndFailureIterator )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The asynchronous path forwards the query flags separately from the synchronous one and
     * it has two branches of its own, so both are driven here with the deterministic
     * numeric_host shape
     *
     * The failure path also pins that the handler wrapper hands out an end iterator when the
     * resolve failed - TcpConnectionEstablisherBase::getEndpoint and IcmpPingerTaskT::onResolved
     * guard the dereference of that iterator with a BL_ASSERT only, i.e. with nothing at all
     * in a release build
     */

    {
        asio::io_service ioService;
        resolver_t resolver( ioService );

        bool completed = false;
        eh::error_code ec;
        bool endpointsAreEnd = false;

        const resolver_t::query nonNumeric(
            "localhost"                                     /* host_name */,
            "80"                                            /* service_name */,
            asio::ip::resolver_query_base::numeric_host     /* flags */
            );

        resolver.async_resolve(
            nonNumeric,
            [ &completed, &ec, &endpointsAreEnd ](
                SAA_in          const eh::error_code&       code,
                SAA_in          resolver_t::iterator        endpoints
                ) -> void
            {
                completed = true;
                ec = code;
                endpointsAreEnd = ( endpoints == resolver_t::iterator() );
            }
            );

        ioService.run();

        UTF_REQUIRE( completed );
        UTF_REQUIRE( !! ec );
        UTF_CHECK( endpointsAreEnd );
    }

    /*
     * The same, through the q.has_protocol() branch of the shim
     */

    {
        asio::io_service ioService;
        resolver_t resolver( ioService );

        bool completed = false;
        eh::error_code ec;
        bool endpointsAreEnd = false;

        const resolver_t::query nonNumeric(
            asio::ip::tcp::v4()                             /* protocol */,
            "localhost"                                     /* host_name */,
            "80"                                            /* service_name */,
            asio::ip::resolver_query_base::numeric_host     /* flags */
            );

        resolver.async_resolve(
            nonNumeric,
            [ &completed, &ec, &endpointsAreEnd ](
                SAA_in          const eh::error_code&       code,
                SAA_in          resolver_t::iterator        endpoints
                ) -> void
            {
                completed = true;
                ec = code;
                endpointsAreEnd = ( endpoints == resolver_t::iterator() );
            }
            );

        ioService.run();

        UTF_REQUIRE( completed );
        UTF_REQUIRE( !! ec );
        UTF_CHECK( endpointsAreEnd );
    }
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_IoServiceWorkHoldsAndReleases )
{
    using namespace bl;

    /*
     * io_service::work is the compatibility name for io_service_work_compat, which holds
     * an executor_work_guard released by its destructor. ThreadPoolImpl.h is the only
     * consumer and it depends on both halves: if the guard held nothing, every
     * ThreadPoolImplT::run() thread would return from io_service::run() immediately and
     * exit - which utf_baselib_basictask would observe as a HANG at eq -> pop( wait ),
     * not as a failed assertion
     */

    asio::io_service ioService;

    UTF_REQUIRE( ! ioService.stopped() );

    {
        const asio::io_service::work work( ioService );

        BL_UNUSED( work );

        /*
         * Nothing has been posted, so there is no ready handler to dispatch; the point of
         * the call is that scheduler::poll() does NOT take its
         * "if( outstanding_work_ == 0 ) { stop(); return 0; }" early exit
         */

        UTF_CHECK_EQUAL( 0U, ioService.poll() );

        /*
         * The "holds" half
         */

        UTF_CHECK( ! ioService.stopped() );
    }

    /*
     * The "releases" half - work_finished() calls stop() the moment the outstanding work
     * count reaches zero, so this must be observable BEFORE run() is ever called; run()
     * returning 0 is only a consequence of it
     */

    UTF_CHECK( ioService.stopped() );

    UTF_CHECK_EQUAL( 0U, ioService.run() );

    /*
     * The same property in the shape ThreadPoolImpl actually uses it - the guard lives in
     * a cpp::SafeUniquePtr and a separate thread is parked inside run()
     */

    asio::io_service io2;

    auto work2 = cpp::SafeUniquePtr< asio::io_service::work >::attach(
        new asio::io_service::work( io2 )
        );

    std::atomic< bool > done( false );

    os::thread runner(
        [ &io2, &done ]() -> void
        {
            io2.run();
            done = true;
        }
        );

    os::sleep( time::milliseconds( 200 ) );

    /*
     * This can only be false if the work guard failed to hold - a thread which has not
     * been scheduled yet leaves it true, so there is no false failure here
     */

    UTF_CHECK( ! done );

    work2.reset();

    UTF_REQUIRE( runner.timed_join( os::get_system_time() + time::seconds( 30 ) ) );

    UTF_CHECK( done );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_AsyncConnectIteratorFormFailurePaths )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The compatibility overload default-constructs the end iterator and forwards to
     * Boost's range form. Its two failure shapes are what production actually meets:
     * an empty range is what TcpConnectionEstablisherClient::continueAfterResolved gets
     * from an empty resolve, and "nothing is listening" is the ordinary failure of every
     * outbound TCP task
     */

    {
        asio::io_service ioService;
        asio::ip::tcp::socket socket( ioService );

        bool completed = false;
        eh::error_code connectEc;
        resolver_t::iterator handedBack;

        asio::async_connect(
            socket,
            resolver_t::iterator()                              /* begin == end */,
            [ & ]( SAA_in const eh::error_code& code, SAA_in resolver_t::iterator connected ) -> void
            {
                completed = true;
                connectEc = code;
                handedBack = connected;
            }
            );

        ioService.run();

        /*
         * A well formed empty range completes rather than hanging or dereferencing
         */

        UTF_REQUIRE( completed );
        UTF_CHECK( connectEc == asio::error::not_found );
        UTF_CHECK( ! socket.is_open() );
        UTF_CHECK( handedBack == resolver_t::iterator() );
    }

    {
        /*
         * The acceptor is opened and bound but deliberately NOT put into the listening
         * state: the port stays reserved for the lifetime of this socket, so no other
         * process can take it, while a connect to it is refused by the kernel. That is
         * strictly more deterministic than binding, reading the port and closing the
         * acceptor, which leaves a window in which the ephemeral port could be re-bound
         */

        asio::io_service ioService;

        asio::ip::tcp::acceptor acceptor( ioService );

        acceptor.open( asio::ip::tcp::v4() );
        acceptor.bind( asio::ip::tcp::endpoint( asio::ip::address_v4::loopback(), 0 /* ephemeral port */ ) );

        resolver_t resolver( ioService );
        eh::error_code ec;

        const resolver_t::query query(
            "127.0.0.1"                                         /* host_name */,
            std::to_string( acceptor.local_endpoint().port() )  /* service_name */,
            asio::ip::resolver_query_base::numeric_host         /* flags */
            );

        const auto endpoints = utest_asio_compat::resolveQuery( resolver, query, ec );

        UTF_REQUIRE( ! ec );

        asio::ip::tcp::socket socket( ioService );

        bool completed = false;
        eh::error_code connectEc;
        resolver_t::iterator handedBack;

        asio::async_connect(
            socket,
            endpoints,
            [ & ]( SAA_in const eh::error_code& code, SAA_in resolver_t::iterator connected ) -> void
            {
                completed = true;
                connectEc = code;
                handedBack = connected;
            }
            );

        ioService.run();

        UTF_REQUIRE( completed );
        UTF_CHECK( !! connectEc );

        /*
         * No connection was established
         *
         * Note that - unlike the empty range case above - the socket is left OPEN here:
         * Boost's iterator connect op closes it only before trying the NEXT endpoint and
         * breaks out of the loop after the last failure without closing ( see
         * boost/asio/impl/connect.hpp, iterator_connect_op::operator() ). That is Boost's
         * own behaviour rather than anything the compatibility overload does, so the
         * "not connected" property is asserted instead of is_open()
         */

        eh::error_code remoteEc;

        socket.remote_endpoint( remoteEc );

        UTF_CHECK( !! remoteEc );

        /*
         * Every endpoint failed, so the iterator handed back is the end of the range the
         * compatibility overload built
         */

        UTF_CHECK( handedBack == resolver_t::iterator() );
    }
}

#if BOOST_VERSION >= 106600

UTF_AUTO_TEST_CASE( BoostAsioCompat_AsyncConnectRangeForm )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * The modern range form, with the resolver results passed directly. On Boost 1.89+ this
     * is the compile time regression test for the compatibility overload above: without its
     * is_endpoint_sequence constraint this call is ambiguous between that overload and
     * Boost's own
     */

    asio::io_service ioService;

    asio::ip::tcp::acceptor acceptor(
        ioService,
        asio::ip::tcp::endpoint( asio::ip::address_v4::loopback(), 0 /* ephemeral port */ )
        );

    resolver_t resolver( ioService );
    eh::error_code ec;

    const resolver_t::query query(
        "127.0.0.1"                                         /* host_name */,
        std::to_string( acceptor.local_endpoint().port() )  /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

    const auto results = resolver.resolve( query, ec );

    UTF_REQUIRE( ! ec );

    asio::ip::tcp::socket socket( ioService );

    bool completed = false;
    eh::error_code connectEc;

    asio::async_connect(
        socket,
        results,
        [ & ]( SAA_in const eh::error_code& code, SAA_in const asio::ip::tcp::endpoint& /* connected */ ) -> void
        {
            completed = true;
            connectEc = code;
        }
        );

    ioService.run();

    UTF_REQUIRE( completed );
    UTF_REQUIRE( ! connectEc );
    UTF_CHECK( socket.is_open() );
}

UTF_AUTO_TEST_CASE( BoostAsioCompat_ResolverStringOverloadsRemainVisible )
{
    using namespace bl;

    typedef asio::ip::tcp_resolver resolver_t;

    /*
     * basic_resolver_compat declares "using base_type::resolve;" and
     * "using base_type::async_resolve;" precisely so its query taking overloads do not
     * hide every string based overload inherited from basic_resolver< Protocol >
     *
     * Every existing case and every production call site uses the query form, so deleting
     * either using-declaration is invisible today. This is deliberately a COMPILE time
     * guard expressed as a runtime case, in the same spirit as
     * BoostAsioCompat_AsyncConnectRangeForm above - it does not compile without them
     *
     * numeric_host on a loopback literal keeps the whole case independent of the resolver
     * configuration of the host
     */

    asio::io_service ioService;
    resolver_t resolver( ioService );
    eh::error_code ec;

    const auto results = resolver.resolve(
        "127.0.0.1"                                         /* host */,
        "80"                                                /* service */,
        asio::ip::resolver_query_base::numeric_host         /* resolve_flags */,
        ec
        );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( results.begin() != results.end() );

    bool completed = false;
    eh::error_code asyncEc;
    std::size_t count = 0U;

    /*
     * The modern ( ec, results_type ) handler signature - the query based async_resolve
     * hands back an iterator instead, so this also pins which overload was selected
     */

    resolver.async_resolve(
        "127.0.0.1"                                         /* host */,
        "80"                                                /* service */,
        asio::ip::resolver_query_base::numeric_host         /* resolve_flags */,
        [ & ]( SAA_in const eh::error_code& code, SAA_in const resolver_t::results_type& resolved ) -> void
        {
            completed = true;
            asyncEc = code;
            count = static_cast< std::size_t >( std::distance( resolved.begin(), resolved.end() ) );
        }
        );

    ioService.run();

    UTF_REQUIRE( completed );
    UTF_REQUIRE( ! asyncEc );
    UTF_REQUIRE( count > 0U );
}

#endif // BOOST_VERSION >= 106600

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

    /*
     * Only the handler storage is under test here, so the query is made environment
     * independent: numeric_host on a loopback literal never consults the resolver
     * configuration of the host
     */

    const resolver_t::query query(
        "127.0.0.1"                                         /* host_name */,
        "80"                                                /* service_name */,
        asio::ip::resolver_query_base::numeric_host         /* flags */
        );

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
