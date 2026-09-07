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

#include <utests/baselib/TestAuthorizationCacheImplUtils.h>

namespace utest
{
    namespace security
    {
        template
        <
            typename E = void
        >
        class TestAuthorizationServiceImplT : public bl::om::ObjectDefaultBase
        {
            BL_CTR_DEFAULT( TestAuthorizationServiceImplT, protected )

        private:

            static const std::string                            g_tokenType;
            static const std::string                            g_tokenUniqueSeedSeparator;

        public:

            typedef bl::tasks::SimpleHttpSslGetTaskImpl         task_impl_t;

            auto getTokenType() NOEXCEPT -> const std::string&
            {
                return g_tokenType;
            }

            auto createAuthorizationTask( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& authenticationToken )
                -> bl::om::ObjPtr< bl::tasks::Task >
            {
                using namespace bl;
                using namespace bl::tasks;
                using namespace utest::http;

                typename task_impl_t::HeadersMap headers;

                headers[ task_impl_t::HttpHeader::g_contentType ] = task_impl_t::HttpHeader::g_contentTypeDefault;

                const std::string tokenData( authenticationToken -> begin(), authenticationToken -> end() );

                const auto pos = tokenData.find( g_tokenUniqueSeedSeparator );

                auto taskImpl = task_impl_t::createInstance(
                    cpp::copy( test::UtfArgsParser::host() ),
                    test::UtfArgsParser::port(),
                    pos == std::string::npos ?
                        tokenData : tokenData.substr( pos + g_tokenUniqueSeedSeparator.size() ),
                    uuids::uuid2string( uuids::create() ) /* content */,
                    std::move( headers )
                );

                /*
                 * Ensure that sensitive secure information is omitted
                 */

                taskImpl -> isSecureMode( true );

                return om::moveAs< Task >( taskImpl );
            }

            auto extractSecurityPrincipal( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& executedAuthorizationTask )
                -> bl::om::ObjPtr< bl::security::SecurityPrincipal >
            {
                using namespace bl;
                using namespace bl::tasks;
                using namespace bl::security;

                BL_ASSERT(
                    Task::Created != executedAuthorizationTask -> getState() &&
                    Task::Running != executedAuthorizationTask -> getState()
                    );

                BL_ASSERT( ! executedAuthorizationTask -> isFailed() );

                const auto taskImpl = om::qi< task_impl_t >( executedAuthorizationTask );

                const auto response = taskImpl -> getResponse();

                auto authenticationToken =
                    AuthorizationCache::createAuthenticationToken(
                        resolveMessage(
                            BL_MSG()
                                << "{"
                                << taskImpl -> getContent()
                                << "}"
                                << g_tokenUniqueSeedSeparator
                                << taskImpl -> getPath()
                            )
                        );

                return bl::security::SecurityPrincipal::createInstance(
                    cpp::copy( taskImpl -> getResponse() )          /* secureIdentity */,
                    "givenName"                                     /*givenName*/,
                    "familyName"                                    /* familyName */,
                    "user@host.com"                                 /* email */,
                    "type_0"                                        /* typeId */,
                    std::move( authenticationToken )
                    );
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( TestAuthorizationServiceImplT, g_tokenType ) = "dummyTokenType";
        BL_DEFINE_STATIC_CONST_STRING( TestAuthorizationServiceImplT, g_tokenUniqueSeedSeparator ) = "::::";

        typedef bl::om::ObjectImpl< TestAuthorizationServiceImplT<> > TestAuthorizationServiceImpl;

    } // security

} // utest

UTF_AUTO_TEST_CASE( AuthorizationCacheImplBasicTests )
{
    using namespace utest;
    using namespace utest::http;
    using namespace utest::security;

    TestAuthorizationCacheImplUtils::basicTests< TestAuthorizationServiceImpl /* SERVICE */ >(
        g_requestUri            /* validAuthenticationTokenData1 */,
        g_desiredResult         /* expectedSecureIdentity1 */
        );
}

UTF_AUTO_TEST_CASE( AuthorizationCacheImplFullTests )
{
    using namespace utest;
    using namespace utest::http;
    using namespace utest::security;

    TestAuthorizationCacheImplUtils::fullTests< TestAuthorizationServiceImpl /* SERVICE */ >(
        g_requestUri            /* validAuthenticationTokenData1 */,
        g_requestPerfUri        /* validAuthenticationTokenData2 */,
        g_notFoundUri           /* invalidAuthenticationTokenData */,
        g_desiredResult         /* expectedSecureIdentity1 */,
        g_desiredPerfResult     /* expectedSecureIdentity2 */
        );
}


namespace utest
{
    namespace security
    {
        /**
         * @brief An authorization service mock which needs no server
         *
         * The principal it returns carries no authentication token, which is what an
         * authorization service that does not rotate (or echo) the token produces
         */

        template
        <
            typename E = void
        >
        class NoTokenAuthorizationServiceImplT : public bl::om::ObjectDefaultBase
        {
            BL_CTR_DEFAULT( NoTokenAuthorizationServiceImplT, protected )

        private:

            static const std::string                            g_tokenType;

        public:

            typedef bl::tasks::SimpleTaskImpl                   task_impl_t;

            auto getTokenType() NOEXCEPT -> const std::string&
            {
                return g_tokenType;
            }

            auto createAuthorizationTask( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& authenticationToken )
                -> bl::om::ObjPtr< bl::tasks::Task >
            {
                using namespace bl;

                /*
                 * The token is dereferenced here on purpose - this is exactly what the REST
                 * authorization service does with the token the cache hands it
                 */

                const std::string tokenData( authenticationToken -> begin(), authenticationToken -> end() );

                BL_CHK( true, tokenData.empty(), BL_MSG() << "The authentication token is empty" );

                return bl::om::qi< bl::tasks::Task >(
                    task_impl_t::createInstance( []() -> void {} )
                    );
            }

            auto extractSecurityPrincipal( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& executedAuthorizationTask )
                -> bl::om::ObjPtr< bl::security::SecurityPrincipal >
            {
                BL_UNUSED( executedAuthorizationTask );

                return bl::security::SecurityPrincipal::createInstance(
                    "sid"                                           /* secureIdentity */,
                    "givenName"                                     /* givenName */,
                    "familyName"                                    /* familyName */,
                    "user@host.com"                                 /* email */,
                    "type_0"                                        /* typeId */,
                    nullptr                                         /* authenticationToken */
                    );
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( NoTokenAuthorizationServiceImplT, g_tokenType ) = "dummyTokenType";

        typedef bl::om::ObjectImpl< NoTokenAuthorizationServiceImplT<> > NoTokenAuthorizationServiceImpl;

        typedef bl::om::ObjectImpl
        <
            bl::security::AuthorizationCacheImpl< NoTokenAuthorizationServiceImpl >
        >
        NoTokenAuthorizationCacheImpl;

    } // security

} // utest

UTF_AUTO_TEST_CASE( AuthorizationCacheImplNoTokenRotationTests )
{
    using namespace bl;
    using namespace bl::security;
    using namespace utest::security;

    /*
     * An authorization service which does not echo the token back leaves the cached principal
     * without one; once the freshness window expires the refresh must fall back on the token
     * which the caller presented instead of dereferencing the null one
     */

    const auto cache = NoTokenAuthorizationCacheImpl::createInstance< AuthorizationCache >(
        NoTokenAuthorizationServiceImpl::createInstance(),
        time::milliseconds( 250 )                                   /* freshnessInterval */
        );

    const auto token = AuthorizationCache::createAuthenticationToken( "the-token" );

    UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( token ) );

    const auto principal = cache -> update( token );

    UTF_REQUIRE( principal );
    UTF_REQUIRE( ! principal -> authenticationToken() );

    UTF_REQUIRE( cache -> tryGetAuthorizedPrinciplal( token ) );

    os::sleep( time::milliseconds( 500 ) );

    /*
     * The entry is stale now, but it is still in the cache - the refresh below has to fall
     * back on the token which the caller presented (before the fix it took the null token of
     * the cached principal and the authorization service dereferenced it)
     *
     * Note that update( ... ) is called before any lookup on purpose: a lookup erases the
     * stale entry, which would hide the defect
     */

    UTF_REQUIRE( cache -> update( token ) );

    /*
     * A stale entry is reported as such and erased on the way
     */

    const auto staleToken = AuthorizationCache::createAuthenticationToken( "the-stale-token" );

    UTF_REQUIRE( cache -> update( staleToken ) );

    os::sleep( time::milliseconds( 500 ) );

    UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( staleToken ) );
}

UTF_AUTO_TEST_CASE( AuthorizationCacheImplCapAndClockTests )
{
    using namespace bl;
    using namespace bl::security;
    using namespace utest::security;

    /*
     * The cache is bounded and a timestamp in the future (a wall clock stepped backwards)
     * is treated as a stale entry instead of an error
     */

    const std::size_t maxEntries = 8U;

    {
        const auto cache = NoTokenAuthorizationCacheImpl::createInstance< AuthorizationCache >(
            NoTokenAuthorizationServiceImpl::createInstance(),
            time::hours( 1 )                                        /* freshnessInterval */,
            maxEntries
            );

        /*
         * The one time warning about the cache being full is expected here
         */

        const Logging::LevelPusher pushLevel( Logging::LL_ERROR );

        for( std::size_t i = 0U; i < 4U * maxEntries; ++i )
        {
            const auto token = AuthorizationCache::createAuthenticationToken(
                resolveMessage( BL_MSG() << "token-" << i )
                );

            /*
             * The authorization itself always succeeds - beyond the cap the principal is
             * simply not cached
             */

            UTF_REQUIRE( cache -> update( token ) );
        }

        UTF_REQUIRE_EQUAL( om::qi< NoTokenAuthorizationCacheImpl >( cache ) -> size(), maxEntries );
    }

    {
        /*
         * A cached entry whose timestamp is in the future (a wall clock stepped backwards)
         * must simply be treated as stale instead of making every lookup throw
         */

        const auto cache = NoTokenAuthorizationCacheImpl::createInstance< AuthorizationCache >(
            NoTokenAuthorizationServiceImpl::createInstance(),
            time::hours( 1 )                                        /* freshnessInterval */,
            maxEntries
            );

        const auto token = AuthorizationCache::createAuthenticationToken( "future-token" );

        UTF_REQUIRE( cache -> update( token ) );
        UTF_REQUIRE( cache -> tryGetAuthorizedPrinciplal( token ) );

        om::qi< NoTokenAuthorizationCacheImpl >( cache ) -> setTimestampForTesting(
            token,
            time::microsec_clock::universal_time() + time::hours( 24 )
            );

        UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( token ) );
    }
}
