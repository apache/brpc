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
    // if (m != NULL && m->version == get_version(tid)) {
    //     return m;
    // }
    // return NULL;
    return address_resource(get_slot(tid));
}

inline void TaskGroup::exchange(TaskGroup** pg, bthread_t next_tid) {
    TaskGroup* g = *pg;
    if (g->is_current_pthread_task()) {
        return g->ready_to_run(next_tid);
    }
    ReadyToRunArgs args = { g->current_tid(), false };
    g->set_remained((g->current_task()->about_to_quit
                     ? ready_to_run_in_worker_ignoresignal
                     : ready_to_run_in_worker),
                    &args);
    TaskGroup::sched_to(pg, next_tid);
}

inline void TaskGroup::sched_to(TaskGroup** pg, bthread_t next_tid) {
    TaskMeta* next_meta = address_meta(next_tid);
    if (next_meta->stack == NULL) {
        ContextualStack* stk = get_stack(next_meta->stack_type(), task_runner);
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
        // TODO(gejun): May cause deadlock when all workers are spinning here.
        // A better solution is to pop and run existing bthreads, however which
        // make set_remained()-callbacks do context switches and need extensive
        // reviews on related code.
        ::usleep(1000);
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
