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

#ifndef __BL_REACTIVE_INPUTCONNECTOR_H_
#define __BL_REACTIVE_INPUTCONNECTOR_H_

#include <baselib/reactive/ObserverBase.h>
#include <baselib/reactive/Observer.h>

#include <baselib/core/ErrorDispatcher.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace reactive
    {
        typedef cpp::function< bool ( SAA_in const cpp::any& value ) > input_connector_callback_t;

        /**
         * @brief class InputConnector - a simple connector implementation
         * which allows us to connect processing units
         */

        template
        <
            typename E = void
        >
        class InputConnectorT :
            public ObserverBase
        {
            BL_CTR_DEFAULT( InputConnectorT, protected )
            BL_DECLARE_OBJECT_IMPL( InputConnectorT )

        protected:

            const om::ObjPtr< ErrorDispatcher >                                     m_errorDispatcher;
            const input_connector_callback_t                                        m_inputCB;
            const cpp::void_callback_t                                              m_completedCB;

            InputConnectorT(
                SAA_in              input_connector_callback_t&&                    inputCB,
                SAA_in_opt          const om::ObjPtr< ErrorDispatcher >&            errorDispatcher,
                SAA_in              cpp::void_callback_t&&                          completedCB = cpp::void_callback_t()
                )
                :
                m_errorDispatcher( om::copy( errorDispatcher ) ),
                m_inputCB( inputCB ),
                m_completedCB( completedCB )
            {
                BL_ASSERT( m_inputCB );
            }

            static std::string getNameString( SAA_in const std::string& name )
            {
                if( name.empty() )
                {
                    return "";
                }

                return resolveMessage(
                    BL_MSG()
                        << "with name '"
                        << name
                        << "' "
                    );
            }

            void chkTargetActive() const
            {
                if( m_errorDispatcher )
                {
                    const auto task = om::tryQI< tasks::Task >( m_errorDispatcher );

                    if( task && task -> getState() != tasks::Task::Running )
                    {
                        /*
                         * If the target is a task object and it has completed
                         * then we have to force disconnect and unregister from
                         * the observable by throwing an exception
                         */

                        BL_THROW(
                            ObjectDisconnectedException(),
                            BL_MSG()
                                << "Observer object "
                                << getNameString( task -> name() )
                                << "is a task that is not running"
                            );
                    }
                }
            }

            enum : std::size_t
            {
                /*
                 * The nested exception chain is copied link by link - see copyForTarget(); a
                 * chain deeper than this is treated as one which cannot be copied
                 */

                MAX_NESTED_EXCEPTIONS_DEPTH = 32,
            };

            /**
             * @brief Returns an exception the target may fail with, or nullptr if there is none
             *
             * The target must not fail with the upstream exception object itself: every task
             * enhances the exception it fails with in place when it completes (see
             * TaskBase::enhanceException()) and dumps it, and the upstream task and the target
             * complete on different threads, so one shared object would be written by both
             *
             * A clone-enabled boost exception - every exception thrown with BL_THROW - is deep
             * copied. A non-boost exception is never enhanced in place (a task wraps it for
             * printing instead), so the target can share it. A boost exception which cannot be
             * cloned can be neither copied nor shared safely, so there is nothing to return
             *
             * The nested exception chain (eh::errinfo_nested_exception_ptr) is copied link by
             * link by the same rules. The container clone copies such an entry by copying the
             * exception_ptr, so the copy would otherwise share every nested link with the
             * upstream exception, and eh::diagnostic_information() walks the chain and has
             * Boost's formatter write a cached string inside each link it formats - the same
             * race as sharing the top level, one level down. A link which can be neither copied
             * nor shared makes the whole chain uncopyable. A null link is never followed or
             * stored: the formatter rethrows each link, and a null exception_ptr aborts the
             * process (cpp::safeRethrowException()) - so a null link already in the chain is left
             * in the copy as it is, since it shares no object
             *
             * boost::exception_detail::clone_base is used directly rather than the public
             * boost::current_exception() / boost::rethrow_exception() route: that route clones a
             * clone-enabled exception the same way, but it converts a boost exception which is
             * not clone-enabled into a wrapper type instead of refusing it, and refusing is the
             * safe behaviour here
             *
             * The copy is taken safely here: an observable does not complete until the events of
             * its subscribers - this onError() included - have been delivered (see
             * ObservableBase::chk2WaitAllEvents2Flush()), so the upstream task cannot be
             * enhancing its exception while it is being copied
             */

            static std::exception_ptr copyForTarget(
                SAA_in              const std::exception_ptr&                       eptr,
                SAA_in_opt          const std::size_t                               depth = 0U
                ) NOEXCEPT
            {
                if( depth > MAX_NESTED_EXCEPTIONS_DEPTH )
                {
                    return nullptr;
                }

                cpp::SafeUniquePtr< const boost::exception_detail::clone_base > clone;

                try
                {
                    cpp::safeRethrowException( eptr );
                }
                catch( const boost::exception_detail::clone_base& clonable )
                {
                    try
                    {
                        clone.reset( clonable.clone() );
                    }
                    catch( std::exception& )
                    {
                        return nullptr;
                    }
                }
                catch( eh::exception& )
                {
                    return nullptr;
                }
                catch( ... )
                {
                    return eptr;
                }

                /*
                 * The nested link, if any, is replaced with a copy of its own on the clone object
                 * itself and before the clone is rethrown: rethrowing copies the object on some
                 * platforms (MSVC), and the copies share the error info container by reference
                 * count, so a container changed here is what every copy carries
                 *
                 * A null link is left as it is: it shares no object, and following it would
                 * rethrow a null exception_ptr, which aborts the process
                 */

                const auto* copy = dynamic_cast< const eh::exception* >( clone.get() );

                if( copy )
                {
                    const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( *copy );

                    if( nested && *nested )
                    {
                        const auto nestedCopy = copyForTarget( *nested, depth + 1U );

                        if( ! nestedCopy )
                        {
                            return nullptr;
                        }

                        try
                        {
                            *copy << eh::errinfo_nested_exception_ptr( nestedCopy );
                        }
                        catch( std::exception& )
                        {
                            return nullptr;
                        }
                    }
                }

                try
                {
                    clone -> rethrow();
                }
                catch( ... )
                {
                    return std::current_exception();
                }

                /*
                 * Not reached - rethrow() always throws
                 */

                return nullptr;
            }

            /**
             * @brief An upstream failure is dispatched into the target
             *
             * A failing observable delivers onError() and then onCompleted() to its subscribers
             * (see ObservableBase::run()). Were the error only logged, the onCompleted() which
             * follows would look like a normal end of input, and the target would finish as if
             * its input were complete - or fail with a report of its own which hides the cause
             *
             * The target fails with a copy of the upstream exception - see copyForTarget() - and
             * the exception is not rethrown here: the error is not this connector's own, and the
             * observable delivering it is already failing. When no copy can be made the error
             * is only logged, and since the target then sees its input end as if it had
             * completed, that case is announced first
             */

            virtual void onError( SAA_in const std::exception_ptr& eptr ) OVERRIDE
            {
                if( m_errorDispatcher )
                {
                    const auto copy = copyForTarget( eptr );

                    if( copy )
                    {
                        m_errorDispatcher -> dispatchException( copy );

                        return;
                    }

                    /*
                     * Info rather than warning: it is not a failure of this connector, and the
                     * error itself is logged right after
                     */

                    BL_LOG(
                        Logging::info(),
                        BL_MSG()
                            << "InputConnector: an upstream error can be neither copied nor shared "
                            << "safely and will only be logged; the target will see its input as completed"
                        );
                }

                ObserverBase::onError( eptr );
            }

            virtual void onCompleted() OVERRIDE
            {
                std::exception_ptr eptr;

                try
                {
                    if( m_completedCB )
                    {
                        m_completedCB();
                        return;
                    }

                    ObserverBase::onCompleted();
                }
                catch( std::exception& )
                {
                    eptr = std::current_exception();
                }

                if( eptr )
                {
                    if( m_errorDispatcher )
                    {
                        m_errorDispatcher -> dispatchException( eptr );
                    }

                    cpp::safeRethrowException( eptr );
                }
            }

            virtual bool onNext( SAA_in const cpp::any& value ) OVERRIDE
            {
                bool handled = false;
                std::exception_ptr eptr;

                try
                {
                    handled = m_inputCB( value );

                    if( ! handled )
                    {
                        chkTargetActive();
                    }
                }
                catch( ObjectDisconnectedException& )
                {
                    /*
                     * The disconnected exceptions are not meant to be dispatched
                     * into the target
                     */

                    throw;
                }
                catch( std::exception& )
                {
                    eptr = std::current_exception();
                }

                if( eptr )
                {
                    if( m_errorDispatcher )
                    {
                        m_errorDispatcher -> dispatchException( eptr );
                    }

                    cpp::safeRethrowException( eptr );
                }

                return handled;
            }
        };

        typedef om::ObjectImpl< InputConnectorT<> > InputConnectorImpl;

        /*
         * A utility helper to create an input connector from a callback
         */

        inline om::ObjPtr< Observer > createInputConnector(
            SAA_in              input_connector_callback_t&&                    inputCB,
            SAA_in_opt          const om::ObjPtr< ErrorDispatcher >&            errorDispatcher,
            SAA_in              cpp::void_callback_t&&                          completedCB = cpp::void_callback_t()
            )
        {
            return InputConnectorImpl::createInstance< Observer >(
                std::forward< input_connector_callback_t >( inputCB ),
                errorDispatcher,
                std::forward< cpp::void_callback_t >( completedCB )
                );
        }

    } // reactive

} // bl

#endif /* __BL_REACTIVE_INPUTCONNECTOR_H_ */
