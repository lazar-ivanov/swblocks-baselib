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

#ifndef __BL_SECURITY_AUTHORIZATIONCACHEIMPL_H_
#define __BL_SECURITY_AUTHORIZATIONCACHEIMPL_H_

#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/Algorithms.h>

#include <baselib/security/SecurityInterfaces.h>

#include <baselib/crypto/HashCalculator.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/OS.h>

#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace security
    {
        /**
         * @brief class AuthorizationCacheImpl - an implementation of the authorization cache interface
         * based on an authorization service implementation
         *
         * The authorization service implementation needs to have the following method signatures below
         * (they can be static or non-static)
         *
         * The authorization service implementation also needs to have a protected default constructor
         * and should be allowed to be inherited from
         *
         * class AUTHORIZATION_SERVICE
         * {
         * protected:
         *
         *     typedef AUTHORIZATION_TASK_IMPL task_impl_t;
         *
         *     [static] auto getTokenType() NOEXCEPT -> const std::string&;
         *
         *     [static] auto createAuthorizationTask( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken )
         *         -> om::ObjPtr< tasks::Task >;
         *
         *     [static] auto extractSecurityPrincipal( SAA_in const om::ObjPtr< tasks::Task >& executedAuthorizationTask )
         *         -> om::ObjPtr< SecurityPrincipal >;
         * };
         */

        template
        <
            typename SERVICE
        >
        class AuthorizationCacheImpl : public AuthorizationCache
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( AuthorizationCacheImpl, AuthorizationCache )

        public:

            enum : std::size_t
            {
                FRESHNESS_INTERVAL_DEFAULT_IN_SECONDS = 15U * 60U,

                /*
                 * The maximum number of entries which will be cached
                 *
                 * The key is the hash of the raw token bytes, so every byte-different
                 * presentation of the same credential is a separate entry - without a bound
                 * a long running broker accumulates every token it has ever authorized
                 */

                MAX_ENTRIES_DEFAULT = 10000U,
            };

            struct AuthorizationInfo
            {
                om::ObjPtrCopyable< SecurityPrincipal >                             principal;
                time::ptime                                                         timestamp;
            };

            const om::ObjPtr< SERVICE >                                             m_authorizationService;
            time::time_duration                                                     m_freshnessInterval;
            const std::size_t                                                       m_maxEntries;
            mutable os::mutex                                                      m_lock;
            std::unordered_map< std::string, AuthorizationInfo >                    m_cache;
            cpp::ScalarTypeIniter< bool >                                           m_cacheFullWasLogged;

            AuthorizationCacheImpl(
                SAA_in              om::ObjPtr< SERVICE >&&                         authorizationService,
                SAA_in_opt          const time::time_duration&                      freshnessInterval = time::neg_infin,
                SAA_in_opt          const std::size_t                               maxEntries = MAX_ENTRIES_DEFAULT
                )
                :
                m_authorizationService( BL_PARAM_FWD( authorizationService ) ),
                m_freshnessInterval(
                    freshnessInterval.is_special() ? freshnessIntervalDefault() : BL_PARAM_FWD( freshnessInterval )
                    ),
                m_maxEntries( maxEntries )
            {
            }

            /**
             * @brief The current time; it is virtual, so the tests can inject a clock
             */

            virtual auto now() const -> time::ptime
            {
                return time::microsec_clock::universal_time();
            }

            /**
             * @brief Whether a cached entry is too old to be used
             *
             * A timestamp in the future is treated as stale as well - the system clock can be
             * stepped backwards (e.g. by NTP), which must not turn every cached lookup into a
             * failure until the clock catches up
             */

            bool isStale( SAA_in const AuthorizationInfo& info ) const
            {
                const auto currentTime = now();

                return info.timestamp > currentTime || ( currentTime - info.timestamp ) > m_freshnessInterval;
            }

            /**
             * @brief Removes every stale entry from the cache; the lock must be held
             */

            void sweepStaleEntriesNoLock()
            {
                for( auto i = m_cache.begin(); i != m_cache.end(); )
                {
                    if( isStale( i -> second ) )
                    {
                        i = m_cache.erase( i );
                    }
                    else
                    {
                        ++i;
                    }
                }
            }

            auto getKey( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken ) -> std::string
            {
                hash::HashCalculatorDefault hash;

                hash.update( authenticationToken -> pv(), authenticationToken -> size() );
                hash.finalize();

                return hash.digestStr();
            }

            auto tryGetAuthorizationInfo( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken )
                -> AuthorizationInfo*
            {
                const auto pos = m_cache.find( getKey( authenticationToken ) );

                if( pos == m_cache.end() )
                {
                    return nullptr;
                }

                return &pos -> second;
            }

            auto createAuthorizationTaskImpl( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken )
                -> om::ObjPtr< tasks::Task >
            {
                /*
                 * The authentication token is assumed to be a string (if the authorization service is REST
                 * usually the same format usually passed as HTTP cookies)
                 *
                 * We first check if there is authorization info already loaded and if so we use the
                 * latest authentication token obtained from the security principal (otherwise we
                 * use the original authentication token provided by the caller)
                 */

                BL_MUTEX_GUARD( m_lock );

                const auto* info = tryGetAuthorizationInfo( authenticationToken );

                /*
                 * Note that the principal may carry no token at all (an authorization service
                 * which does not echo one), in which case the token presented by the caller
                 * is the one to use - it is dereferenced unconditionally further down
                 */

                const auto& latestAuthenticationToken =
                    ( info && info -> principal -> authenticationToken() ) ?
                        info -> principal -> authenticationToken() : authenticationToken;

                return m_authorizationService -> createAuthorizationTask( latestAuthenticationToken );
            }

            auto tryGetRefreshedPrincipal(
                SAA_in              const om::ObjPtr< data::DataBlock >&            authenticationToken,
                SAA_in_opt          const om::ObjPtr< tasks::Task >&                authorizationTask,
                SAA_in_opt          const bool                                      tryOnly
                )
                -> om::ObjPtr< SecurityPrincipal >
            {
                om::ObjPtr< tasks::Task > executedAuthorizationTask;

                if( authorizationTask )
                {
                    executedAuthorizationTask = om::copy( authorizationTask );
                }
                else
                {
                    executedAuthorizationTask = createAuthorizationTaskImpl( authenticationToken );

                    tasks::scheduleAndExecuteInParallel(
                        [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
                        {
                            /*
                             * Note that the authorization task can fail and the failure
                             * case will be handled below with re-mapping the exception
                             * to SecurityException (if necessary)
                             */

                            eq -> setOptions( tasks::ExecutionQueue::OptionKeepNone );

                            eq -> push_back( executedAuthorizationTask );
                        }
                        );
                }

                BL_CHK(
                    false,
                    tasks::Task::Created != executedAuthorizationTask -> getState() &&
                    tasks::Task::Running != executedAuthorizationTask -> getState(),
                    BL_MSG()
                        << "The provided authorization task is not ready as it has not been executed"
                    );

                if( executedAuthorizationTask -> isFailed() )
                {
                    if( tryOnly )
                    {
                        return nullptr;
                    }

                    try
                    {
                        cpp::safeRethrowException( executedAuthorizationTask -> exception() );
                    }
                    catch( std::exception& )
                    {
                        BL_THROW(
                            SecurityException()
                                << eh::errinfo_nested_exception_ptr( std::current_exception() ),
                            BL_MSG()
                                << "Authorization request to the authorization service has failed"
                            );
                    }
                }

                return m_authorizationService -> extractSecurityPrincipal( executedAuthorizationTask );
            }

            auto updateInternal(
                SAA_in              const om::ObjPtr< data::DataBlock >&            authenticationToken,
                SAA_in_opt          const om::ObjPtr< tasks::Task >&                authorizationTask,
                SAA_in_opt          const bool                                      tryOnly
                )
                -> om::ObjPtr< SecurityPrincipal >
            {
                auto principal = tryGetRefreshedPrincipal( authenticationToken, authorizationTask, tryOnly );

                BL_MUTEX_GUARD( m_lock );

                if( tryOnly && nullptr == principal )
                {
                    return nullptr;
                }

                BL_CHK(
                    false,
                    nullptr != principal,
                    BL_MSG()
                        << "Security principal cannot be nullptr"
                    );

                const auto key = getKey( authenticationToken );

                if( m_cache.size() >= m_maxEntries && m_cache.find( key ) == m_cache.end() )
                {
                    /*
                     * The cache is at its limit and this would be a new entry - sweep the
                     * stale ones once and, if that did not make room, simply don't cache the
                     * principal (the authorization itself succeeded, it will just cost
                     * another round trip next time)
                     */

                    sweepStaleEntriesNoLock();

                    if( m_cache.size() >= m_maxEntries )
                    {
                        if( ! m_cacheFullWasLogged )
                        {
                            m_cacheFullWasLogged = true;

                            BL_LOG(
                                Logging::warning(),
                                BL_MSG()
                                    << "The authorization cache has reached its maximum size of "
                                    << m_maxEntries
                                    << " entries; new authorizations will not be cached"
                                );
                        }

                        return principal;
                    }
                }

                auto& info = m_cache[ key ];

                info.principal = principal;
                info.timestamp = now();

                return principal;
            }

        public:

            static time::time_duration freshnessIntervalDefault()
            {
                return time::seconds( FRESHNESS_INTERVAL_DEFAULT_IN_SECONDS );
            }

            virtual auto tokenType() const NOEXCEPT -> const std::string& OVERRIDE
            {
                return m_authorizationService -> getTokenType();
            }

            virtual void configureFreshnessInterval(
                SAA_in_opt          const time::time_duration&                  freshnessInterval = time::neg_infin
                ) OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                m_freshnessInterval = freshnessInterval.is_special() ? freshnessIntervalDefault() : freshnessInterval;
            }

            virtual auto tryGetAuthorizedPrinciplal(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken
                )
                -> om::ObjPtr< SecurityPrincipal > OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                const auto pos = m_cache.find( getKey( authenticationToken ) );

                if( pos == m_cache.end() )
                {
                    /*
                     * This token has never been authorized and placed in the cache
                     */

                    return nullptr;
                }

                if( isStale( pos -> second ) )
                {
                    /*
                     * The entry is erased here, so the refresh re-validates the token which
                     * the caller presented instead of a stale one which is still cached
                     */

                    m_cache.erase( pos );

                    return nullptr;
                }

                return om::copy( pos -> second.principal );
            }

            virtual auto createAuthorizationTask(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken
                )
                -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                return createAuthorizationTaskImpl( authenticationToken );
            }

            virtual auto tryUpdate(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken,
                SAA_in_opt          const om::ObjPtr< tasks::Task >&        authorizationTask = nullptr
                )
                -> om::ObjPtr< SecurityPrincipal > OVERRIDE
            {
                return updateInternal( authenticationToken, authorizationTask, true /* tryOnly */ );
            }

            virtual auto update(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken,
                SAA_in_opt          const om::ObjPtr< tasks::Task >&        authorizationTask = nullptr
                )
                -> om::ObjPtr< SecurityPrincipal > OVERRIDE
            {
                return updateInternal( authenticationToken, authorizationTask, false /* tryOnly */ );
            }

            virtual void evict(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken
                ) OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                m_cache.erase( getKey( authenticationToken ) );
            }

            /**
             * @brief The number of entries which are currently cached
             */

            auto size() const -> std::size_t
            {
                BL_MUTEX_GUARD( m_lock );

                return m_cache.size();
            }

            /**
             * @brief Overwrites the timestamp of a cached entry
             *
             * It exists to let the tests inject a timestamp (in particular one in the future,
             * i.e. a wall clock which was stepped backwards) without waiting
             */

            void setTimestampForTesting(
                SAA_in              const om::ObjPtr< data::DataBlock >&    authenticationToken,
                SAA_in              const time::ptime&                      timestamp
                )
            {
                BL_MUTEX_GUARD( m_lock );

                const auto pos = m_cache.find( getKey( authenticationToken ) );

                BL_CHK(
                    false,
                    pos != m_cache.end(),
                    BL_MSG()
                        << "The authentication token is not in the cache"
                    );

                pos -> second.timestamp = timestamp;
            }
        };

    } // security

} // bl

#endif /* __BL_SECURITY_AUTHORIZATIONCACHEIMPL_H_ */
