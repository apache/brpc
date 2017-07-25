// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_TASK_GROUP_INL_H
#define BAIDU_BTHREAD_TASK_GROUP_INL_H

namespace bthread {

// Utilities to manipulate bthread_t
inline bthread_t make_tid(uint32_t version, base::ResourceId<TaskMeta> slot) {
    return (((bthread_t)version) << 32) | (bthread_t)slot.value;
}

inline base::ResourceId<TaskMeta> get_slot(bthread_t tid) {
    base::ResourceId<TaskMeta> id = { (tid & 0xFFFFFFFFul) };
    return id;
}
inline uint32_t get_version(bthread_t tid) {
    return (uint32_t)((tid >> 32) & 0xFFFFFFFFul);
}

TaskMeta* TaskGroup::address_meta(bthread_t tid) {
    // TaskMeta * m = address_resource<TaskMeta>(get_slot(tid));
    // if (m != NULL && m->version == get_version(tid)) {
    //     return m;
    // }
    // return NULL;
    return address_resource(get_slot(tid));
}

void TaskGroup::exchange(TaskGroup** pg, bthread_t next_tid) {
    TaskGroup* g = *pg;
    if (g->is_current_pthread_task()) {
        return g->ready_to_run(next_tid);
    }
    if (!g->current_task()->about_to_quit) {
        g->set_remained(ready_to_run_in_worker,
                        (void*)g->current_tid());
    } else {
        g->set_remained(ready_to_run_in_worker_nosignal,
                        (void*)g->current_tid());
    }
    TaskGroup::sched_to(pg, next_tid);
}

void TaskGroup::sched_to(TaskGroup** pg, bthread_t next_tid) {
    TaskMeta* next_meta = address_meta(next_tid);
    if (next_meta->stack_container == NULL) {
        StackContainer* sc = get_stack(next_meta->stack_type(), task_runner);
        if (sc != NULL) {
            next_meta->set_stack(sc);
        } else {
            // stack_type is BTHREAD_STACKTYPE_PTHREAD or out of memory,
            // In latter case, attr is forced to be BTHREAD_STACKTYPE_PTHREAD.
            // This basically means that if we can't allocate stack, run
            // the task in pthread directly.
            next_meta->attr.stack_type = BTHREAD_STACKTYPE_PTHREAD;
            next_meta->set_stack((*pg)->_main_stack_container);
        }
    }
    // Update now_ns only when wait_task did yield.
    sched_to(pg, next_meta);
}

void TaskGroup::set_remained(void (*last_context_remained)(void*), void* arg) {
    _last_context_remained = last_context_remained;
    _last_context_remained_arg = arg;
}

bool TaskGroup::is_current_main_task() const {
    return current_tid() == _main_tid;
}

bool TaskGroup::is_current_pthread_task() const {
    return _cur_meta->stack_container == _main_stack_container;
}


int64_t TaskGroup::current_uptime_ns() const {
    return base::cpuwide_time_ns() - _cur_meta->cpuwide_start_ns;
}

}  // namespace bthread

#endif  // BAIDU_BTHREAD_TASK_GROUP_INL_H
