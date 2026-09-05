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
             * @brief The notification delivery policy requested when the observer is registered
             *
             * BEHAVIOR CHANGE: in earlier releases the execution queue held an internal mutex
             * across onEvent(), so callbacks for one queue were always mutually exclusive and an
             * observer could be written as if it were single threaded. That is no longer the
             * default. This parameter is mandatory precisely so that the change cannot be
             * inherited silently -- every call site must state which behavior it wants.
             *
             * DeliveryConcurrent is the current default behavior. Callbacks for one queue may run
             * at the same time on different threads, so the observer must be thread safe and must
             * synchronize its own state; see onEvent() below for the full contract. The number of
             * callbacks which can be in flight simultaneously is bounded only by the number of
             * tasks completing at once -- up to the thread pool width (32 by default), plus any
             * external threads which complete tasks through ExternalCompletionTask::markCompleted().
             *
             * DeliverySerialized restores mutual exclusion of onEvent() for one queue. It buys
             * exclusion and nothing else. Specifically it does NOT:
             *
             * -- order the callbacks (see below)
             * -- cover maxReadyOrExecuting(), which is sampled once at registration time
             * -- act as a notification drain barrier, so AllTasksCompleted still does not imply
             *    that earlier callbacks have returned
             * -- survive re-registration: the policy is captured per callback, under the queue
             *    lock, at the moment the callback is bound, so when setNotifyCallback() switches
             *    a queue from DeliverySerialized to DeliveryConcurrent a serialized callback
             *    bound before the switch may still be in flight while the callbacks bound after
             *    it are delivered concurrently with it
             *
             * ORDERING: DeliverySerialized guarantees exclusion, NOT order. Completing threads
             * contend for an internal mutex whose acquisition order is unspecified, so the delivery
             * order between different task completions remains arbitrary -- including between
             * repeated execution attempts of the same retained task. Releases prior to the
             * concurrent delivery change did not provide ordering either, so this is not a
             * regression against them. If ordering is required, sequence it inside the observer.
             *
             * WARNING (deadlock): a DeliverySerialized callback must never block waiting for
             * *another notification* from the same queue, because delivering that notification
             * requires the same internal mutex, which the blocked callback is holding. For the
             * same reason it must never *synchronously complete* another task of the same queue
             * from inside onEvent() - e.g. by calling ExternalCompletionTask::markCompleted() or
             * notifyReady() on a sibling task - because completion delivers that task's
             * notification on the calling thread, which would re-acquire the mutex it already
             * holds. The queue detects this case and aborts the process (BL_RT_ASSERT) rather
             * than deadlocking. Completing a task of a *different* queue is fine.
             *
             * This is still a narrower restriction than the one which applied before the
             * concurrent delivery change: a serialized callback *may* safely call back into the
             * queue (push_back, wait, flush, dispose) and *may* block waiting for another *task*
             * to complete, provided that completion happens on another thread, because the
             * queue's state transition and its condition variable signalling both happen before
             * the notification mutex is acquired. Only waiting on, or synchronously producing,
             * another *delivery* of the same queue deadlocks.
             *
             * WARNING (thread pool starvation): notification callbacks run on thread pool worker
             * threads. Under DeliverySerialized every concurrently completing task parks one
             * worker on the notification mutex for as long as the callback ahead of it runs. The
             * default thread pool is process wide and is shared with every other execution queue,
             * so a slow serialized observer on one queue degrades throughput on *unrelated*
             * queues and can exhaust the pool. Per queue notification throughput becomes
             * 1 / callbackDuration and does not improve with more cores.
             *
             * Choose DeliverySerialized only for short, non-blocking observers. If the callback
             * can block, or can take more than a few microseconds, choose DeliveryConcurrent and
             * synchronize inside the observer itself.
             */

            enum NotifyDelivery
            {
                DeliveryConcurrent,
                DeliverySerialized,
            };

            /**
             * @brief returns the maximum number of ready and executing tasks
             *
             * If zero is returned that means there is no limit
             *
             * We're not going to schedule more tasks until some of these are
             * processed and removed from the queue.
             *
             * BEHAVIOR CHANGE: this is sampled exactly once, when the observer is registered
             * through ExecutionQueue::setNotifyCallback(), and the value is then cached for the
             * lifetime of that registration. It is no longer re-queried from the task scheduling
             * path, so an implementation which returns a changing value will no longer see that
             * change take effect -- register again to install a new limit. Every implementation
             * is expected to return an effectively constant policy value.
             *
             * A consequence of caching is that the limit stays in effect even if the observer
             * proxy is subsequently disconnected. In earlier releases disconnecting the observer
             * silently removed the throttle.
             *
             * In exchange, no execution queue lock is held while this is invoked. The earlier
             * requirement that implementations must not call back into the execution queue,
             * on pain of deadlock, therefore no longer applies.
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
             * No execution queue state mutex is held while onEvent is invoked. Under the default
             * NotifyDelivery::DeliveryConcurrent policy callbacks from the same queue may execute
             * concurrently on different threads, so observer implementations must be thread-safe and
             * synchronize access to shared state. Registering with NotifyDelivery::DeliverySerialized
             * makes them mutually exclusive instead, subject to the hazards documented on that
             * enumeration. Under either policy, do not rely on callback thread identity or delivery
             * order between different task completion paths, including repeated execution attempts of
             * the same retained task.
             *
             * AllTasksCompleted does not wait for previously started notification callbacks to return and
             * is not a notification-drain barrier. TaskReady and TaskDiscarded use the observer selected
             * when the corresponding task state transition commits, while the AllTasksCompleted observer
             * is selected at final delivery validation.
             *
             * EVENT COLLAPSING: the count of AllTasksCompleted events delivered is NOT the count of
             * times the queue drained. Two or more drain cycles can complete and produce a single
             * event, or none at all for the earlier ones. The event is deduplicated against the
             * generation of the work which produced the completion candidate, so a candidate whose
             * generation has been overtaken by a later publish is suppressed rather than delivered
             * late. Concretely: a thread which completes the last task, forms a candidate, and then
             * enters a slow callback can find, when it finally re-acquires the lock, that another
             * thread has already admitted, executed and published a newer generation - and its own
             * candidate is then dropped, even though a distinct batch really did drain.
             *
             * This is correct under the point-in-time semantics above, and it is the price of not
             * holding a lock across user code. But it means the event MUST NOT be used as a
             * per-batch boundary marker or counted against the number of batches submitted; under
             * load - which is exactly when a callback is most likely to be slow - boundaries are
             * silently missed. An observer which needs to know that a specific set of work finished
             * must track that set itself, for example by counting TaskReady and TaskDiscarded, and
             * treat AllTasksCompleted only as a hint that the queue was momentarily idle.
             */

            virtual void onEvent(
                SAA_in                      const EventId                           eventId,
                SAA_in_opt                  const om::ObjPtrCopyable< Task >&       task
                ) NOEXCEPT = 0;
        };

    } // tasks

} // bl

#endif /* __BL_TASKS_EXECUTIONQUEUENOTIFY_H_ */
