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

#ifndef __BL_TASKS_EXECUTIONQUEUENOTIFY_H_
#define __BL_TASKS_EXECUTIONQUEUENOTIFY_H_

#include <baselib/tasks/Task.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

BL_IID_DECLARE( ExecutionQueueNotify, "1eb49d31-8278-425f-a8ae-11755aac9a71" )

namespace bl
{
    namespace tasks
    {
        /**
         * @brief ExecutionQueueNotify interface
         */

        class ExecutionQueueNotify : public om::Object
        {
            BL_DECLARE_INTERFACE( ExecutionQueueNotify )

        public:

            enum EventId
            {
                TaskReady           =   0x0001,
                TaskDiscarded       =   0x0002,
                AllTasksCompleted   =   0x0004,
            };

            enum
            {
                AllEvents = TaskReady | TaskDiscarded | AllTasksCompleted,
            };

            /**
             * @brief returns the maximum number of ready and executing tasks
             *
             * If zero is returned that means there is no limit
             *
             * We're not going to schedule more tasks until some of these are
             * processed and removed from the queue.
             */

            virtual std::size_t maxReadyOrExecuting() const NOEXCEPT = 0;

            /**
             * @brief Notify the callback that an event have occurred in the execution queue
             *
             * Note: The task parameter can be nullptr depending on the eventId (e.g. if eventId=AllTasksCompleted)
             *
             * AllTasksCompleted is a point-in-time notification. Immediately before it is dispatched,
             * the pending and executing queues were empty and no scheduled work had been admitted or
             * rescheduled since the completion candidate was created. Work can be enqueued concurrently
             * after that validation point, including before or during the callback. Ready tasks, including
             * retained tasks and tasks added with dontSchedule=true, do not prevent AllTasksCompleted.
             * The event is generated from task completion processing; removing the last pending task through
             * cancellation or flush does not by itself generate AllTasksCompleted.
             *
             * No internal execution queue mutex is held while onEvent is invoked. Callbacks from the same
             * queue may execute concurrently on different threads, so observer implementations must be
             * thread-safe and synchronize access to shared state. Do not rely on callback thread identity
             * or delivery order between different task completion paths, including repeated execution
             * attempts of the same retained task.
             *
             * AllTasksCompleted does not wait for previously started notification callbacks to return and
             * is not a notification-drain barrier. TaskReady and TaskDiscarded use the observer selected
             * when the corresponding task state transition commits, while the AllTasksCompleted observer
             * is selected at final delivery validation.
             */

            virtual void onEvent(
                SAA_in                      const EventId                           eventId,
                SAA_in_opt                  const om::ObjPtrCopyable< Task >&       task
                ) NOEXCEPT = 0;
        };

    } // tasks

} // bl

#endif /* __BL_TASKS_EXECUTIONQUEUENOTIFY_H_ */
