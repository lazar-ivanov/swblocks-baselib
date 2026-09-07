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

#ifndef __BL_LOGGING_H_
#define __BL_LOGGING_H_

#include <baselib/core/Annotations.h>
#include <baselib/core/BaseDefs.h>
#include <baselib/core/detail/TimeBoostImports.h>
#include <baselib/core/TlsState.h>
#include <baselib/core/CPP.h>
#include <baselib/core/MessageBuffer.h>
#include <baselib/core/StringUtils.h>
#include <baselib/core/TimeUtils.h>

#include <iostream>

/*
 * The logging interface; e.g.
 *
 * BL_LOG(
 *         bl::Logging::notify(),
 *         BL_MSG()
 *             << "The answer to the Ultimate Question of Life is "
 *             << 42
 *         );
 */

#define BL_LOG( channel, msg ) \
    do \
    { \
        if( ( channel ).isEnabled() ) \
        { \
            ( channel ).out( msg ); \
        } \
    } \
    while( false ) \

#define BL_LOG_MULTILINE( channel, msg ) \
    do \
    { \
        if( ( channel ).isEnabled() ) \
        { \
            ( channel ).outMultiLine( msg ); \
        } \
    } \
    while( false ) \

#define BL_TRACE_FUNCTION() \
    do \
    { \
        if( ( bl::Logging::trace() ).isEnabled() ) \
        { \
            ( bl::Logging::trace() ).out( \
                BL_MSG() << __FILE__ << ':' << __LINE__ << " : " << BOOST_CURRENT_FUNCTION \
                ); \
        } \
    } \
    while( false ) \

namespace bl
{
    /**
     * @brief Logging framework
     */

    template
    <
        typename E = void
    >
    class LoggingT
    {
        BL_DECLARE_STATIC( LoggingT )

    public:

        typedef LoggingT< E > this_type;

        enum Level
        {
            LL_NONE    = 0,
            LL_NOTIFY,
            LL_ERROR,
            LL_WARNING,
            LL_INFO,
            LL_DEBUG,
            LL_TRACE,
            LL_LAST
        };

        enum
        {
            LL_DEFAULT = -1
        };

        class Channel;

        static Channel& notify()   { return notifyChannel(); }
        static Channel& error()    { return errorChannel(); }
        static Channel& warning()  { return warningChannel(); }
        static Channel& info()     { return infoChannel(); }
        static Channel& debug()    { return debugChannel(); }
        static Channel& trace()    { return traceChannel(); }

        typedef cpp::function
            <
                void (
                    SAA_in      const std::string&   prefix,
                    SAA_in      const std::string&   text,
                    SAA_in      const bool           enableTimestamp,
                    SAA_in      const Level          level
                    )
            >
            line_logger_t;

    protected:

        typedef std::array< Channel*, LL_LAST > channels_t;

        /*
         * Note that the channels, the line logger and the level to channel map are function
         * local statics and not static data members - the initialization order of static data
         * members of a class template across translation units is unspecified, so a BL_LOG
         * from another translation unit's static initializer (or during static destruction)
         * would otherwise use objects which are not constructed yet
         */

        static Channel& notifyChannel()
        {
            static Channel channel( LL_NOTIFY, "" );

            return channel;
        }

        static Channel& errorChannel()
        {
            static Channel channel( LL_ERROR, "ERROR: " );

            return channel;
        }

        static Channel& warningChannel()
        {
            static Channel channel( LL_WARNING, "WARNING: " );

            return channel;
        }

        static Channel& infoChannel()
        {
            static Channel channel( LL_INFO, "INFO: " );

            return channel;
        }

        static Channel& debugChannel()
        {
            static Channel channel( LL_DEBUG, "DEBUG: " );

            return channel;
        }

        static Channel& traceChannel()
        {
            static Channel channel( LL_TRACE, "TRACE: " );

            return channel;
        }

        static line_logger_t& lineLoggerRef()
        {
            static line_logger_t lineLogger( getDefaultLineLogger() );

            return lineLogger;
        }

        static channels_t& level2ChannelRef()
        {
            static channels_t channels( getChannels() );

            return channels;
        }

        /*
         * Note that the global level is written by setGlobalLevel / LevelPusher while it is
         * read by isEnabled() from any thread, so it must be atomic
         */

        static std::atomic< int > g_level;

        static os::mutex g_lock;

        static const std::string g_noneLabel;
        static const std::string g_notifyLabel;
        static const std::string g_errorLabel;
        static const std::string g_warningLabel;
        static const std::string g_infoLabel;
        static const std::string g_debugLabel;
        static const std::string g_traceLabel;

        static Level safeCastLevelNoThrow( SAA_in const int level ) NOEXCEPT
        {
            if( level >= LL_NONE && level < LL_LAST )
            {
                return ( Level ) level;
            }

            BL_RIP_MSG( "Level value is incorrect" );
            return LL_NONE;
        }

        static int setLevelInternal(
            SAA_inout   TlsState&             tlsData,
            SAA_in_opt  const int             level = LL_DEFAULT
            )
        {
            const int prev = tlsData.logging.level;

            tlsData.logging.level =
                ( LL_DEFAULT == level ? LL_DEFAULT : ( int )( safeCastLevelNoThrow( level ) ) );

            return prev;
        }

        static channels_t getChannels()
        {
            channels_t channels;

            channels[ LL_NONE ]         = nullptr;
            channels[ LL_NOTIFY ]       = &notify();
            channels[ LL_ERROR ]        = &error();
            channels[ LL_WARNING ]      = &warning();
            channels[ LL_INFO ]         = &info();
            channels[ LL_DEBUG ]        = &debug();
            channels[ LL_TRACE ]        = &trace();

            return channels;
        }

    public:

        static void defaultLineLoggerNoLock(
            SAA_in      const std::string&   prefix,
            SAA_in      const std::string&   text,
            SAA_in      const bool           enableTimestamp,
            SAA_in      const Level          level,
            SAA_in      const bool           addNewLine = true,
            SAA_inout   std::ostream&        os = std::cout
            )
        {
            BL_UNUSED( level );

            os << prefix;

            if( enableTimestamp )
            {
                os << "[";
                os << time::getCurrentLocalTimeISO();
                os << "] ";
            }

            os << text;

            if( addNewLine )
            {
                os << std::endl;
            }
        }

        static void defaultLineLoggerWithLock(
            SAA_in      const std::string&   prefix,
            SAA_in      const std::string&   text,
            SAA_in      const bool           enableTimestamp,
            SAA_in      const Level          level,
            SAA_in      const bool           addNewLine = true,
            SAA_inout   std::ostream&        os = std::cout
            )
        {
            BL_STDIO_TEXT(
                {
                    defaultLineLoggerNoLock( prefix, text, enableTimestamp, level, addNewLine, os );
                }
                );
        }

        static Level getLevel()
        {
            const int level = tlsData().logging.level;

            return
                LL_DEFAULT == level ?
                    safeCastLevelNoThrow( g_level.load() ) : safeCastLevelNoThrow( level );
        }

        static int setGlobalLevel( SAA_in_opt const int level = LL_DEFAULT ) NOEXCEPT
        {
            const int prev = g_level;
            g_level = ( LL_DEFAULT == level ? LL_INFO : safeCastLevelNoThrow( level ) );
            return prev;
        }

        static int setLevel(
            SAA_in_opt  const int            level = LL_DEFAULT,
            SAA_in_opt  const bool           global = false
            )
        {
            if( global )
            {
                return setGlobalLevel( level );
            }

            return setLevelInternal( tlsData(), level );
        }

        static line_logger_t setLineLogger( SAA_in const line_logger_t& lineLogger )
        {
            BL_MUTEX_GUARD( g_lock );

            const line_logger_t prev = lineLoggerRef();
            lineLoggerRef() = lineLogger;
            return prev;
        }

        static line_logger_t getDefaultLineLogger( SAA_in_opt std::ostream& os = std::cout )
        {
            /*
             * The default line logger is just to print on std::cout
             */

            return cpp::bind(
                &this_type::defaultLineLoggerWithLock, _1, _2, _3, _4, true /* addNewLine */, cpp::ref( os )
                );
        }

        static bool isVerboseModeEnabled()
        {
            return ( getLevel() >= LL_DEBUG );
        }

        static Level stringToLogLevel( SAA_in const std::string& levelName )
        {
            Level level;

            BL_CHK(
                false,
                tryStringToLogLevel( levelName, level ),
                BL_MSG()
                    << "Invalid logging level '"
                    << levelName
                    << "' was specified"
                );

            return level;
        }

        static bool tryStringToLogLevel(
            SAA_in          const std::string&              levelName,
            SAA_out         Level&                          level
            )
        {
            if( str::iequals( g_noneLabel, levelName ) )
            {
                level = LL_NONE;
            }
            else if( str::iequals( g_notifyLabel, levelName ) )
            {
                level = LL_NOTIFY;
            }
            else if( str::iequals( g_errorLabel, levelName ) )
            {
                level = LL_ERROR;
            }
            else if( str::iequals( g_warningLabel, levelName ) )
            {
                level = LL_WARNING;
            }
            else if( str::iequals( g_infoLabel, levelName ) )
            {
                level = LL_INFO;
            }
            else if( str::iequals( g_debugLabel, levelName ) )
            {
                level = LL_DEBUG;
            }
            else if( str::iequals( g_traceLabel, levelName ) )
            {
                level = LL_TRACE;
            }
            else
            {
                level = LL_LAST;

                return false;
            }

            return true;
        }

        static Channel& levelToChannel( SAA_in const Level level )
        {
            BL_CHK(
                true,
                level < LL_NOTIFY || level > LL_TRACE,
                BL_MSG()
                    << "Invalid logging level '"
                    << level
                    << "' was specified"
                );

            return *level2ChannelRef()[ level ];
        }

        static std::string logLevelToString( SAA_in const Level logLevel )
        {
            std::string level;

            switch( logLevel )
            {
                case LL_NONE:
                    level = g_noneLabel;
                    break;

                case LL_NOTIFY:
                    level = g_notifyLabel;
                    break;

                case LL_ERROR:
                    level = g_errorLabel;
                    break;

                case LL_WARNING:
                    level = g_warningLabel;
                    break;

                case LL_INFO:
                    level = g_infoLabel;
                    break;

                case LL_DEBUG:
                    level = g_debugLabel;
                    break;

                case LL_TRACE:
                    level = g_traceLabel;
                    break;

                case LL_LAST:
                    break;
            }

            return level;
        }

        /**
         * @brief The logging channel implementation
         */

        class Channel
        {
            BL_NO_COPY( Channel )

        private:

            const Level           m_level;
            const std::string     m_prefix;

        public:

            Channel( SAA_in const Level level, SAA_in const std::string& prefix )
                :
                m_level( level ),
                m_prefix( prefix )
            {
            }

            const std::string& prefix() const NOEXCEPT
            {
                return m_prefix;
            }

            bool isEnabled() const NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                return ( lineLoggerRef() && m_level <= getLevel() );

                BL_NOEXCEPT_END()

                return false;
            }

            template
            <
                typename T
            >
            void out( SAA_in T&& msg ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * Filter on the level this channel is associated with relative to the
                 * current logging level
                 */

                if( isEnabled() )
                {
                    BL_MUTEX_GUARD( g_lock );

                    lineLoggerRef()(
                        m_prefix,
                        resolveMessage( std::forward< T >( msg ) ),
                        isVerboseModeEnabled() /* enableTimestamp */,
                        m_level
                        );
                }

                BL_NOEXCEPT_END()
            }

            template
            <
                typename T
            >
            void outMultiLine( SAA_in T&& msg ) NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                /*
                 * Filter on the level this channel is associated with relative to the
                 * current logging level
                 */

                if( isEnabled() )
                {
                    BL_MUTEX_GUARD( g_lock );

                    cpp::SafeInputStringStream is( bl::resolveMessage( std::forward< T >( msg ) ) );

                    std::string line;
                    while( ! is.eof() )
                    {
                        std::getline( is, line );

                        lineLoggerRef()(
                            m_prefix,
                            line,
                            isVerboseModeEnabled() /* enableTimestamp */,
                            m_level
                            );
                    }
                }

                BL_NOEXCEPT_END()
            }
        };

        /**
         * @brief Level push/restore wrapper
         */

        class LevelPusher
        {
            BL_NO_COPY( LevelPusher )

        private:

            const bool m_isGlobal;
            const int m_prevLevel;

        public:

            LevelPusher( SAA_in const int level = LL_DEFAULT, SAA_in const bool global = false )
                :
                m_isGlobal( global ),
                m_prevLevel( global ? setGlobalLevel( level ) : setLevelInternal( tlsData(), level ) )
            {
            }

            ~LevelPusher() NOEXCEPT
            {
                if( m_isGlobal )
                {
                    setGlobalLevel( m_prevLevel );
                }
                else
                {
                    setLevelInternal( tlsDataNoThrow(), m_prevLevel );
                }
            }
        };

        /**
         * @brief Line logger push/restore wrapper
         */

        class LineLoggerPusher
        {
            BL_NO_COPY( LineLoggerPusher )

        private:

            line_logger_t m_prev;

        public:

            LineLoggerPusher( SAA_in const line_logger_t& lineLogger )
                :
                m_prev( setLineLogger( lineLogger ) )
            {
            }

            ~LineLoggerPusher() NOEXCEPT
            {
                BL_MUTEX_GUARD( g_lock );

                lineLoggerRef().swap( m_prev );
            }
        };
    };


    BL_DEFINE_STATIC_MEMBER( LoggingT, std::atomic< int >, g_level )                                ( LoggingT< TCLASS >::LL_INFO );
    BL_DEFINE_STATIC_MEMBER( LoggingT, os::mutex, g_lock );

    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_noneLabel )                                         = "none";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_notifyLabel )                                       = "notify";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_errorLabel )                                        = "error";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_warningLabel )                                      = "warning";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_infoLabel )                                         = "info";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_debugLabel )                                        = "debug";
    BL_DEFINE_STATIC_CONST_STRING ( LoggingT, g_traceLabel )                                        = "trace";

    typedef LoggingT<> Logging;

} // bl

#endif /* __BL_LOGGING_H_ */
