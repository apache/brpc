// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// bthread - An M:N threading library to make applications more concurrent.

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_TASK_GROUP_INL_H
#define BTHREAD_TASK_GROUP_INL_H

namespace bthread {

// Utilities to manipulate bthread_t
inline bthread_t make_tid(uint32_t version, butil::ResourceId<TaskMeta> slot) {
    return (((bthread_t)version) << 32) | (bthread_t)slot.value;
}

inline butil::ResourceId<TaskMeta> get_slot(bthread_t tid) {
    butil::ResourceId<TaskMeta> id = { (tid & 0xFFFFFFFFul) };
    return id;
}
inline uint32_t get_version(bthread_t tid) {
    return (uint32_t)((tid >> 32) & 0xFFFFFFFFul);
}

inline TaskMeta* TaskGroup::address_meta(bthread_t tid) {
    // TaskMeta * m = address_resource<TaskMeta>(get_slot(tid));
    // if (m != nullptr && m->version == get_version(tid)) {
    //     return m;
    // }
    // return nullptr;
    return address_resource(get_slot(tid));
}

inline void TaskGroup::exchange(TaskGroup** pg, TaskMeta* next_meta) {
    TaskGroup* g = *pg;
    if (g->is_current_pthread_task()) {
        return g->ready_to_run(next_meta);
    }
    ReadyToRunArgs args = { g->tag(), g->_cur_meta, false };
    g->set_remained((g->current_task()->about_to_quit
                     ? ready_to_run_in_worker_ignoresignal
                     : ready_to_run_in_worker),
                    &args);
    TaskGroup::sched_to(pg, next_meta);
}

inline void TaskGroup::sched_to(TaskGroup** pg, bthread_t next_tid) {
    TaskMeta* next_meta = address_meta(next_tid);
    if (next_meta->stack == nullptr) {
#ifdef BUTIL_USE_ASAN
        ContextualStack* stk = get_stack(next_meta->stack_type(), asan_task_runner);
#else
        ContextualStack* stk = get_stack(next_meta->stack_type(), task_runner);
#endif // BUTIL_USE_ASAN
        if (stk) {
            next_meta->set_stack(stk);
        } else {
            // stack_type is BTHREAD_STACKTYPE_PTHREAD or out of memory,
            // In latter case, attr is forced to be BTHREAD_STACKTYPE_PTHREAD.
            // This basically means that if we can't allocate stack, run
            // the task in pthread directly.
            next_meta->attr.stack_type = BTHREAD_STACKTYPE_PTHREAD;
            next_meta->set_stack((*pg)->_main_stack);
        }
    }
    // Update now_ns only when wait_task did yield.
    sched_to(pg, next_meta);
}

inline void TaskGroup::push_rq(bthread_t tid) {
    while (!_rq.push(tid)) {
        // Created too many bthreads: a promising approach is to insert the
        // task into another TaskGroup, but we don't use it because:
        // * There're already many bthreads to run, inserting the bthread
        //   into other TaskGroup does not help.
        // * Insertions into other TaskGroups perform worse when all workers
        //   are busy at creating bthreads (proved by test_input_messenger in
        //   brpc)
        flush_nosignal_tasks();
        LOG_EVERY_SECOND(ERROR) << "_rq is full, capacity=" << _rq.capacity();
        if (is_current_pthread_task() && !is_current_main_task()) {
            // A pthread-mode bthread runs on _main_stack and cannot be
            // scheduled out, fall back to waiting.
            ::usleep(1000);
            continue;
        }
        const auto pop_task = [this](bthread_t* t) -> bool {
#ifndef BTHREAD_FAIR_WSQ
            return _rq.pop(t);
#else
            return _rq.steal(t);
#endif
        };
        bthread_t run_tid = 0;
        if (!pop_task(&run_tid)) {
            // The queue was drained by stealers between the failed push and
            // the pop, retrying the push will succeed.
            continue;
        }
        if (is_current_main_task() &&
            address_meta(run_tid)->attr.stack_type == BTHREAD_STACKTYPE_PTHREAD) {
            // pthread-mode tasks are run inline on _main_stack by the
            // dispatch loops (run_main_task/task_runner). Switching to one
            // from the main task itself causes no stack jump and would
            // silently drop it, so it cannot be run here. But pushing it
            // straight back makes the LIFO pop() return this same task
            // forever, hiding any runnable task sitting deeper in the
            // queue. Hold pthread-mode tasks aside and keep popping until
            // a runnable one shows up.
            bthread_t pending[16];
            int npending = 0;
            pending[npending++] = run_tid;
            bool found = false;
            while (npending < 16 && pop_task(&run_tid)) {
                if (address_meta(run_tid)->attr.stack_type !=
                        BTHREAD_STACKTYPE_PTHREAD) {
                    found = true;
                    break;
                }
                pending[npending++] = run_tid;
            }
            // Push the held tasks back. The pops above freed at least
            // |npending| slots, _rq has a single producer (this worker)
            // and stealers never push, so these pushes must succeed.
            // Reverse order restores their relative order in the queue.
            for (int i = npending - 1; i >= 0; --i) {
                const bool pushed = _rq.push(pending[i]);
                CHECK(pushed) << "push_rq: failed to put back held task";
            }
            if (!found) {
                // Everything within reach is pthread-mode: wait for
                // stealers to drain the queue.
                ::usleep(1000);
                continue;
            }
            // Fall through: run_tid is a runnable task, run it inline
            // below to free a slot for |tid|.
        }
        // Run the popped bthread in place instead of spinning, otherwise
        // all workers may spin here and no one drains the queues any more.
        // The pop also frees one slot, which guarantees that the deferred
        // push of the current task succeeds: _rq has a single producer (the
        // owning worker) and stealers never push.
        // |args| lives on the stack that sched_to() is about to freeze and is
        // read by the remained-callback after the switch, so it must stay in
        // this scope: if it were declared inside the branch below, its slot
        // could be reused before sched_to() reads it.
        ReadyToRunArgs args = { tag(), nullptr, false };
        if (!is_current_main_task()) {
            // Preserve the task being scheduled out. The push is deferred to
            // a remained-callback: publishing a still-running task into _rq
            // lets a stealer jump into it and run the same context
            // concurrently on another worker. The callback runs on the stack
            // of |run_tid| after sched_to() has frozen the current context,
            // same as exchange(). Capture the meta now, current_tid() is
            // |run_tid| by the time the callback runs. Note that push_rq()
            // can be re-entered from remained-callbacks, in which case the
            // task to preserve is still the one being frozen here.
            args.meta = _cur_meta;
            set_remained(ready_to_run_in_worker_ignoresignal, &args);
        }
        // The main task is never queued; ending_sched() resumes it via
        // _main_tid when the queue drains. Jumping to a pthread-mode task
        // from a normal bthread resumes the dispatch loop on _main_stack
        // which runs it inline, same as ending_sched() does.
        TaskGroup* g = this;
        sched_to(&g, run_tid);
        // Resume here after the popped bthread (and possibly others) ran,
        // then retry the push of |tid|.
    }
}

inline void TaskGroup::flush_nosignal_tasks_remote() {
    if (_remote_num_nosignal) {
        _remote_rq._mutex.lock();
        flush_nosignal_tasks_remote_locked(_remote_rq._mutex);
    }
}

}  // namespace bthread

#endif  // BTHREAD_TASK_GROUP_INL_H
