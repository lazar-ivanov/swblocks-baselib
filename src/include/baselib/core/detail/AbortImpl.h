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

#ifndef __BL_ABORTIMPL_H_
#define __BL_ABORTIMPL_H_

#include <baselib/core/Annotations.h>

#if defined( _WIN32 )
#include <Windows.h>
#include <io.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <stdlib.h>

namespace bl
{
    namespace os
    {
        namespace detail
        {
            /**
             * @brief Implementation of an unconditional application abort
             *
             * @note This file is kept separate from the OSImpl header files to avoid circular
             * #include file dependencies
             */

            template
            <
                typename E = void
            >
            class AbortImplT
            {
            public:

                /**
                 * @brief Abort the running process immediately without executing any clean-up code
                 * nor any additional operations or user hooks
                 */

                SAA_noreturn
                static void fastAbort() NOEXCEPT
                {
#if defined( _WIN32 )

                    ::RaiseException(
                        EXCEPTION_NONCONTINUABLE_EXCEPTION /* dwExceptionCode */,
                        EXCEPTION_NONCONTINUABLE /* dwExceptionFlags */,
                        0 /* nNumberOfArguments */,
                        NULL /* lpArguments */
                        );
#else
                    /*
                     * Remove any user-specified ABORT signal handler and raise the signal
                     *
                     * The default ABORT signal action is to generate a core dump and terminate
                     * with exit code 134 (= 128 + 6(SIGABRT))
                     */

                    ::signal( SIGABRT, SIG_DFL );
                    ::raise( SIGABRT );

#endif // #if defined( _WIN32 )

                    /*
                     * If the above ever fails, exit anyway
                     */

                    ::_exit( EXIT_FAILURE );
                }
            };

            typedef AbortImplT<> AbortImpl;

        } // detail

        SAA_noreturn
        inline void fastAbort() NOEXCEPT
        {
            detail::AbortImpl::fastAbort();
        }

        /**
         * @brief Writes a message to the standard error stream without taking any lock
         *
         * It is the fallback of the abort path, where the stdio lock may be held by another
         * thread; the underlying call is async-signal-safe
         */

        inline void writeToStdErrNothrow( SAA_in const char* message ) NOEXCEPT
        {
            if( nullptr == message )
            {
                return;
            }

            std::size_t size = 0U;

            while( message[ size ] )
            {
                ++size;
            }

#if defined( _WIN32 )
            ( void ) ::_write( 2 /* stderr */, message, static_cast< unsigned int >( size ) );
#else
            ( void ) ::write( 2 /* stderr */, message, size );
#endif
        }

    } // os

} // bl

#endif /* __BL_ABORTIMPL_H_ */
