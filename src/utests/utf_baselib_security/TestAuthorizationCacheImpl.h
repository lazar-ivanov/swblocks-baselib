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

        /**
         * @brief An authorization service mock which records the token bytes it was handed
         *
         * The principal it returns carries a fresh, distinct authentication token on every
         * call, which is what makes the cache's token selection observable
         */

        template
        <
            typename E = void
        >
        class RecordingAuthorizationServiceImplT : public bl::om::ObjectDefaultBase
        {
            BL_CTR_DEFAULT( RecordingAuthorizationServiceImplT, protected )

        private:

            static const std::string                            g_tokenType;

            std::vector< std::string >                          m_tokensSeen;
            bl::cpp::ScalarTypeIniter< std::size_t >            m_counter;

        public:

            typedef bl::tasks::SimpleTaskImpl                   task_impl_t;

            auto getTokenType() NOEXCEPT -> const std::string&
            {
                return g_tokenType;
            }

            /**
             * @brief The token bytes which were presented to the service, in order
             *
             * No lock is needed - the cases which use this mock are single threaded (update
             * schedules the no-op task on a private queue and waits for it before returning)
             */

            auto tokensSeen() const NOEXCEPT -> const std::vector< std::string >&
            {
                return m_tokensSeen;
            }

            auto createAuthorizationTask( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& authenticationToken )
                -> bl::om::ObjPtr< bl::tasks::Task >
            {
                m_tokensSeen.emplace_back( authenticationToken -> begin(), authenticationToken -> end() );

                return bl::om::qi< bl::tasks::Task >(
                    task_impl_t::createInstance( []() -> void {} )
                    );
            }

            auto extractSecurityPrincipal( SAA_in const bl::om::ObjPtr< bl::tasks::Task >& executedAuthorizationTask )
                -> bl::om::ObjPtr< bl::security::SecurityPrincipal >
            {
                BL_UNUSED( executedAuthorizationTask );

                return bl::security::SecurityPrincipal::createInstance(
                    "sid"                                       /* secureIdentity */,
                    "givenName"                                 /* givenName */,
                    "familyName"                                /* familyName */,
                    "user@host.com"                             /* email */,
                    "type_0"                                    /* typeId */,
                    bl::security::AuthorizationCache::createAuthenticationToken(
                        bl::resolveMessage( BL_MSG() << "rotated-" << ++m_counter.lvalue() )
                        )                                       /* authenticationToken */
                    );
            }
        };

        BL_DEFINE_STATIC_CONST_STRING( RecordingAuthorizationServiceImplT, g_tokenType ) = "recordingTokenType";

        typedef bl::om::ObjectImpl< RecordingAuthorizationServiceImplT<> > RecordingAuthorizationServiceImpl;

        typedef bl::om::ObjectImpl
        <
            bl::security::AuthorizationCacheImpl< RecordingAuthorizationServiceImpl >
        >
        RecordingAuthorizationCacheImpl;

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

    {
        /*
         * A cache which has reached its cap must recover: the overflow branch sweeps the
         * stale entries once and, if that made room, caches normally; only when the sweep
         * frees nothing is the principal returned uncached
         *
         * The clock is moved by backdating the stored timestamps, so there are no sleeps
         */

        const std::size_t cappedEntries = 4U;

        const auto cache = NoTokenAuthorizationCacheImpl::createInstance< AuthorizationCache >(
            NoTokenAuthorizationServiceImpl::createInstance(),
            time::hours( 1 )                                        /* freshnessInterval */,
            cappedEntries
            );

        const auto cacheImpl = om::qi< NoTokenAuthorizationCacheImpl >( cache );

        /*
         * The one time warning about the cache being full is expected here
         */

        const Logging::LevelPusher pushLevel( Logging::LL_ERROR );

        std::vector< om::ObjPtr< data::DataBlock > > tokens;

        for( std::size_t i = 0U; i < 7U; ++i )
        {
            tokens.push_back(
                AuthorizationCache::createAuthenticationToken(
                    resolveMessage( BL_MSG() << "sweep-token-" << i )
                    )
                );
        }

        for( std::size_t i = 0U; i < cappedEntries; ++i )
        {
            UTF_REQUIRE( cache -> update( tokens[ i ] ) );
        }

        UTF_REQUIRE_EQUAL( cacheImpl -> size(), 4U );

        /*
         * Two of the four entries are made stale, so the sweep which the overflow branch runs
         * has room to free
         */

        const auto backdate = [ & ]( SAA_in const std::size_t index ) -> void
        {
            cacheImpl -> setTimestampForTesting(
                tokens[ index ],
                time::microsec_clock::universal_time() - time::hours( 2 )
                );
        };

        backdate( 0U );
        backdate( 1U );

        /*
         * A new key at the cap - the sweep frees the two stale entries and the principal is
         * then cached normally
         */

        UTF_REQUIRE( cache -> update( tokens[ 4 ] ) );

        UTF_REQUIRE_EQUAL( cacheImpl -> size(), 3U );

        UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( tokens[ 0 ] ) );
        UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( tokens[ 1 ] ) );
        UTF_REQUIRE( cache -> tryGetAuthorizedPrinciplal( tokens[ 2 ] ) );
        UTF_REQUIRE( cache -> tryGetAuthorizedPrinciplal( tokens[ 4 ] ) );

        /*
         * Back to the cap
         */

        UTF_REQUIRE( cache -> update( tokens[ 5 ] ) );

        UTF_REQUIRE_EQUAL( cacheImpl -> size(), 4U );

        /*
         * A new key at the cap with nothing stale to sweep - the authorization itself still
         * succeeds, the caching of it is simply declined
         */

        UTF_REQUIRE( cache -> update( tokens[ 6 ] ) );

        UTF_REQUIRE_EQUAL( cacheImpl -> size(), 4U );
        UTF_REQUIRE( ! cache -> tryGetAuthorizedPrinciplal( tokens[ 6 ] ) );

        /*
         * An entry which is already in the cache skips the overflow branch entirely and is
         * always refreshed, cap or no cap - it would come back null here if the branch had
         * short circuited it
         */

        backdate( 2U );

        UTF_REQUIRE( cache -> update( tokens[ 2 ] ) );

        UTF_REQUIRE_EQUAL( cacheImpl -> size(), 4U );
        UTF_REQUIRE( cache -> tryGetAuthorizedPrinciplal( tokens[ 2 ] ) );
    }
}

UTF_AUTO_TEST_CASE( AuthorizationCacheImplTokenRotationTests )
{
    using namespace bl;
    using namespace bl::security;
    using namespace utest::security;

    /*
     * When the cache already holds an entry whose principal carries an authentication token
     * the next authorization request is built out of THAT token rather than out of the one
     * the caller presented - this is how a rotating credential is kept alive - while the
     * cache key remains the hash of the caller's token, so the caller keeps hitting the same
     * entry with the bytes it has
     */

    const auto tokenText = []( SAA_in const om::ObjPtr< data::DataBlock >& token ) -> std::string
    {
        return std::string( token -> begin(), token -> end() );
    };

    /*
     * AuthorizationCacheImpl takes the service by rvalue, so the case has to retain its own
     * reference in order to read back which tokens were presented to it
     */

    const auto service = RecordingAuthorizationServiceImpl::createInstance();

    const auto cache = RecordingAuthorizationCacheImpl::createInstance< AuthorizationCache >(
        om::copy( service ),
        time::hours( 1 )                                        /* freshnessInterval */
        );

    const auto original = AuthorizationCache::createAuthenticationToken( "original-token" );

    const auto principal1 = cache -> update( original );

    UTF_REQUIRE( principal1 );
    UTF_REQUIRE( principal1 -> authenticationToken() );

    UTF_REQUIRE_EQUAL( service -> tokensSeen().size(), 1U );
    UTF_REQUIRE_EQUAL( service -> tokensSeen().at( 0U ), std::string( "original-token" ) );
    UTF_REQUIRE_EQUAL( tokenText( principal1 -> authenticationToken() ), std::string( "rotated-1" ) );

    /*
     * update( ... ) re-authorizes unconditionally - staleness plays no part in the selection -
     * and this refresh must present the ROTATED credential, not the caller's
     */

    UTF_REQUIRE( cache -> update( original ) );

    UTF_REQUIRE_EQUAL( service -> tokensSeen().size(), 2U );
    UTF_REQUIRE_EQUAL( service -> tokensSeen().at( 1U ), std::string( "rotated-1" ) );

    /*
     * The entry is still keyed on the caller's token ...
     */

    const auto cached = cache -> tryGetAuthorizedPrinciplal( original );

    UTF_REQUIRE( cached );
    UTF_REQUIRE( cached -> authenticationToken() );
    UTF_REQUIRE_EQUAL( tokenText( cached -> authenticationToken() ), std::string( "rotated-2" ) );

    /*
     * ... and not on the rotated one, so the rotation did not create a second entry
     */

    UTF_REQUIRE(
        ! cache -> tryGetAuthorizedPrinciplal(
            AuthorizationCache::createAuthenticationToken( "rotated-1" )
            )
        );

    UTF_REQUIRE_EQUAL( om::qi< RecordingAuthorizationCacheImpl >( cache ) -> size(), 1U );

    /*
     * createAuthorizationTask( ... ) is the API the broker itself calls and it makes the very
     * same selection
     */

    UTF_REQUIRE( cache -> createAuthorizationTask( original ) );

    UTF_REQUIRE_EQUAL( service -> tokensSeen().size(), 3U );
    UTF_REQUIRE_EQUAL( service -> tokensSeen().at( 2U ), std::string( "rotated-2" ) );

    /*
     * With no entry left there is nothing to rotate, so the token which the caller presented
     * is the one which is used again
     */

    cache -> evict( original );

    UTF_REQUIRE( cache -> createAuthorizationTask( original ) );

    UTF_REQUIRE_EQUAL( service -> tokensSeen().size(), 4U );
    UTF_REQUIRE_EQUAL( service -> tokensSeen().back(), std::string( "original-token" ) );
}
