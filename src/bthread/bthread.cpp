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

#include <sys/syscall.h>
#include <gflags/gflags.h>
#include "butil/macros.h"                       // BAIDU_CASSERT
#include "butil/logging.h"
#include "butil/thread_local.h"
#include "butil/reloadable_flags.h"
#include "bthread/task_group.h"                // TaskGroup
#include "bthread/task_control.h"              // TaskControl
#include "bthread/timer_thread.h"
#include "bthread/list_of_abafree_id.h"
#include "bthread/bthread.h"

namespace bthread {

static bool validate_bthread_concurrency(const char*, int32_t val) {
    // bthread_setconcurrency sets the flag on success path which should
    // not be strictly in a validator. But it's OK for a int flag.
    return bthread_setconcurrency(val) == 0;
}
static bool validate_bthread_min_concurrency(const char*, int32_t val);
static bool validate_bthread_current_tag(const char*, int32_t val);
static bool validate_bthread_concurrency_by_tag(const char*, int32_t val);

DEFINE_int32(bthread_concurrency, 8 + BTHREAD_EPOLL_THREAD_NUM,
             "Number of pthread workers");
BUTIL_VALIDATE_GFLAG(bthread_concurrency, validate_bthread_concurrency);

DEFINE_int32(bthread_min_concurrency, 0,
            "Initial number of pthread workers which will be added on-demand."
            " The laziness is disabled when this value is non-positive,"
            " and workers will be created eagerly according to -bthread_concurrency and bthread_setconcurrency(). ");
BUTIL_VALIDATE_GFLAG(bthread_min_concurrency, validate_bthread_min_concurrency);

DEFINE_int32(bthread_current_tag, BTHREAD_TAG_INVALID, "Set bthread concurrency for this tag");
BUTIL_VALIDATE_GFLAG(bthread_current_tag, validate_bthread_current_tag);

DEFINE_int32(bthread_concurrency_by_tag, 8 + BTHREAD_EPOLL_THREAD_NUM,
             "Number of pthread workers of FLAGS_bthread_current_tag");
BUTIL_VALIDATE_GFLAG(bthread_concurrency_by_tag, validate_bthread_concurrency_by_tag);

static bool never_set_bthread_concurrency = true;

BAIDU_CASSERT(sizeof(TaskControl*) == sizeof(butil::atomic<TaskControl*>), atomic_size_match);

pthread_mutex_t g_task_control_mutex = PTHREAD_MUTEX_INITIALIZER;
// Referenced in rpc, needs to be extern.
// Notice that we can't declare the variable as atomic<TaskControl*> which
// are not constructed before main().
TaskControl* g_task_control = NULL;

extern BAIDU_THREAD_LOCAL TaskGroup* tls_task_group;
extern void (*g_worker_startfn)();
extern void (*g_tagged_worker_startfn)(bthread_tag_t);
extern void* (*g_create_span_func)();

inline TaskControl* get_task_control() {
    return g_task_control;
}

inline TaskControl* get_or_new_task_control() {
    butil::atomic<TaskControl*>* p = (butil::atomic<TaskControl*>*)&g_task_control;
    TaskControl* c = p->load(butil::memory_order_consume);
    if (c != NULL) {
        return c;
    }
    BAIDU_SCOPED_LOCK(g_task_control_mutex);
    c = p->load(butil::memory_order_consume);
    if (c != NULL) {
        return c;
    }
    c = new (std::nothrow) TaskControl;
    if (NULL == c) {
        return NULL;
    }
    int concurrency = FLAGS_bthread_min_concurrency > 0 ?
        FLAGS_bthread_min_concurrency :
        FLAGS_bthread_concurrency;
    if (c->init(concurrency) != 0) {
        LOG(ERROR) << "Fail to init g_task_control";
        delete c;
        return NULL;
    }
    p->store(c, butil::memory_order_release);
    return c;
}

#ifdef BRPC_BTHREAD_TRACER
BAIDU_THREAD_LOCAL TaskMeta* pthread_fake_meta = NULL;

bthread_t init_for_pthread_stack_trace() {
    if (NULL != pthread_fake_meta) {
        return pthread_fake_meta->tid;
    }

    TaskControl* c = get_task_control();
    if (NULL == c) {
        LOG(ERROR) << "TaskControl has not been created, "
                      "please use bthread_start_xxx before call this function";
        return INVALID_BTHREAD;
    }

    butil::ResourceId<TaskMeta> slot;
    pthread_fake_meta = butil::get_resource(&slot);
    if (BAIDU_UNLIKELY(NULL == pthread_fake_meta)) {
        LOG(ERROR) << "Fail to get TaskMeta";
        return INVALID_BTHREAD;
    }

    pthread_fake_meta->attr = BTHREAD_ATTR_PTHREAD;
    pthread_fake_meta->tid = make_tid(*pthread_fake_meta->version_butex, slot);
    // Make TaskTracer use signal trace mode for pthread.
    c->_task_tracer.set_running_status(syscall(SYS_gettid), pthread_fake_meta);

    // Release the TaskMeta at exit of pthread.
    butil::thread_atexit([]() {
        // Similar to TaskGroup::task_runner.
        bool tracing;
        {
            BAIDU_SCOPED_LOCK(pthread_fake_meta->version_lock);
            tracing = TaskTracer::set_end_status_unsafe(pthread_fake_meta);
            // If resulting version is 0,
            // change it to 1 to make bthread_t never be 0.
            if (0 == ++*pthread_fake_meta->version_butex) {
                ++*pthread_fake_meta->version_butex;
            }
        }

        if (tracing) {
            // Wait for tracing completion.
            get_task_control()->_task_tracer.WaitForTracing(pthread_fake_meta);
        }
        get_task_control()->_task_tracer.set_status(
            TASK_STATUS_UNKNOWN, pthread_fake_meta);

        butil::return_resource(get_slot(pthread_fake_meta->tid));
        pthread_fake_meta = NULL;
    });

    return pthread_fake_meta->tid;
}

void stack_trace(std::ostream& os, bthread_t tid) {
    TaskControl* c = get_task_control();
    if (NULL == c) {
        os << "TaskControl has not been created";
        return;
    }
    c->stack_trace(os, tid);
}

std::string stack_trace(bthread_t tid) {
    TaskControl* c = get_task_control();
    if (NULL == c) {
        return "TaskControl has not been created";
    }
    return c->stack_trace(tid);
}
#endif // BRPC_BTHREAD_TRACER

static int add_workers_for_each_tag(int num) {
    int added = 0;
    auto c = get_task_control();
    for (auto i = 0; i < num; ++i) {
        added += c->add_workers(1, i % FLAGS_task_group_ntags);
    }
    return added;
}

static bool validate_bthread_min_concurrency(const char*, int32_t val) {
    if (val <= 0) {
        return true;
    }
    if (val < BTHREAD_MIN_CONCURRENCY || val > FLAGS_bthread_concurrency) {
        return false;
    }
    TaskControl* c = get_task_control();
    if (!c) {
        return true;
    }
    BAIDU_SCOPED_LOCK(g_task_control_mutex);
    int concurrency = c->concurrency();
    if (val > concurrency) {
        int added = bthread::add_workers_for_each_tag(val - concurrency);
        return added == (val - concurrency);
    } else {
        return true;
    }
}

static bool validate_bthread_current_tag(const char*, int32_t val) {
    if (val == BTHREAD_TAG_INVALID) {
        return true;
    } else if (val < BTHREAD_TAG_DEFAULT || val >= FLAGS_task_group_ntags) {
        return false;
    }
    BAIDU_SCOPED_LOCK(bthread::g_task_control_mutex);
    auto c = bthread::get_task_control();
    if (c == NULL) {
        FLAGS_bthread_concurrency_by_tag = 8 + BTHREAD_EPOLL_THREAD_NUM;
        return true;
    }
    FLAGS_bthread_concurrency_by_tag = c->concurrency(val);
    return true;
}

static bool validate_bthread_concurrency_by_tag(const char*, int32_t val) {
    return bthread_setconcurrency_by_tag(val, FLAGS_bthread_current_tag) == 0;
}

__thread TaskGroup* tls_task_group_nosignal = NULL;

BUTIL_FORCE_INLINE int
start_from_non_worker(bthread_t* __restrict tid,
                      const bthread_attr_t* __restrict attr,
                      void* (*fn)(void*),
                      void* __restrict arg) {
    TaskControl* c = get_or_new_task_control();
    if (NULL == c) {
        return ENOMEM;
    }
    auto tag = BTHREAD_TAG_DEFAULT;
    if (attr != NULL && attr->tag != BTHREAD_TAG_INVALID) {
        tag = attr->tag;
    }
    if (attr != NULL && (attr->flags & BTHREAD_NOSIGNAL)) {
        // Remember the TaskGroup to insert NOSIGNAL tasks for 2 reasons:
        // 1. NOSIGNAL is often for creating many bthreads in batch,
        //    inserting into the same TaskGroup maximizes the batch.
        // 2. bthread_flush() needs to know which TaskGroup to flush.
        auto g = tls_task_group_nosignal;
        if (NULL == g) {
            g = c->choose_one_group(tag);
            tls_task_group_nosignal = g;
        }
        return g->start_background<true>(tid, attr, fn, arg);
    }
    return c->choose_one_group(tag)->start_background<true>(tid, attr, fn, arg);
}

// Meet one of the three conditions, can run in thread local
// attr is nullptr
// tag equal to thread local
// tag equal to BTHREAD_TAG_INVALID
BUTIL_FORCE_INLINE bool can_run_thread_local(const bthread_attr_t* __restrict attr) {
    return attr == nullptr || attr->tag == bthread::tls_task_group->tag() ||
           attr->tag == BTHREAD_TAG_INVALID;
}

struct TidTraits {
    static const size_t BLOCK_SIZE = 63;
    static const size_t MAX_ENTRIES = 65536;
    static const size_t INIT_GC_SIZE = 65536;
    static const bthread_t ID_INIT;
    static bool exists(bthread_t id) { return bthread::TaskGroup::exists(id); }
};
const bthread_t TidTraits::ID_INIT = INVALID_BTHREAD;

typedef ListOfABAFreeId<bthread_t, TidTraits> TidList;

struct TidStopper {
    void operator()(bthread_t id) const { bthread_stop(id); }
};
struct TidJoiner {
    void operator()(bthread_t & id) const {
        bthread_join(id, NULL);
        id = INVALID_BTHREAD;
    }
};

}  // namespace bthread

extern "C" {

int bthread_start_urgent(bthread_t* __restrict tid,
                         const bthread_attr_t* __restrict attr,
                         void * (*fn)(void*),
                         void* __restrict arg) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (g) {
        // if attribute is null use thread local task group
        if (bthread::can_run_thread_local(attr)) {
            return bthread::TaskGroup::start_foreground(&g, tid, attr, fn, arg);
        }
    }
    return bthread::start_from_non_worker(tid, attr, fn, arg);
}

int bthread_start_background(bthread_t* __restrict tid,
                             const bthread_attr_t* __restrict attr,
                             void * (*fn)(void*),
                             void* __restrict arg) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (g) {
        // if attribute is null use thread local task group
        if (bthread::can_run_thread_local(attr)) {
            return g->start_background<false>(tid, attr, fn, arg);
        }
    }
    return bthread::start_from_non_worker(tid, attr, fn, arg);
}

void bthread_flush() {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (g) {
        return g->flush_nosignal_tasks();
    }
    g = bthread::tls_task_group_nosignal;
    if (g) {
        // NOSIGNAL tasks were created in this non-worker.
        bthread::tls_task_group_nosignal = NULL;
        return g->flush_nosignal_tasks_remote();
    }
}

int bthread_interrupt(bthread_t tid, bthread_tag_t tag) {
    return bthread::TaskGroup::interrupt(tid, bthread::get_task_control(), tag);
}

int bthread_stop(bthread_t tid) {
    bthread::TaskGroup::set_stopped(tid);
    return bthread_interrupt(tid);
}

int bthread_stopped(bthread_t tid) {
    return (int)bthread::TaskGroup::is_stopped(tid);
}

bthread_t bthread_self(void) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    // note: return 0 for main tasks now, which include main thread and
    // all work threads. So that we can identify main tasks from logs
    // more easily. This is probably questionable in future.
    if (g != NULL && !g->is_current_main_task()/*note*/) {
        return g->current_tid();
    }
    return INVALID_BTHREAD;
}

int bthread_equal(bthread_t t1, bthread_t t2) {
    return t1 == t2;
}

void bthread_exit(void* retval) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (g != NULL && !g->is_current_main_task()) {
        throw bthread::ExitException(retval);
    } else {
        pthread_exit(retval);
    }
}

int bthread_join(bthread_t tid, void** thread_return) {
    return bthread::TaskGroup::join(tid, thread_return);
}

int bthread_attr_init(bthread_attr_t* a) {
    *a = BTHREAD_ATTR_NORMAL;
    return 0;
}

int bthread_attr_destroy(bthread_attr_t*) {
    return 0;
}

int bthread_getattr(bthread_t tid, bthread_attr_t* attr) {
    return bthread::TaskGroup::get_attr(tid, attr);
}

int bthread_getconcurrency(void) {
    return bthread::FLAGS_bthread_concurrency;
}

int bthread_setconcurrency(int num) {
    if (num < BTHREAD_MIN_CONCURRENCY || num > BTHREAD_MAX_CONCURRENCY) {
        LOG(ERROR) << "Invalid concurrency=" << num;
        return EINVAL;
    }
    if (bthread::FLAGS_bthread_min_concurrency > 0) {
        if (num < bthread::FLAGS_bthread_min_concurrency) {
            return EINVAL;
        }
        if (bthread::never_set_bthread_concurrency) {
            bthread::never_set_bthread_concurrency = false;
        }
        bthread::FLAGS_bthread_concurrency = num;
        return 0;
    }
    bthread::TaskControl* c = bthread::get_task_control();
    if (c != NULL) {
        if (num < c->concurrency()) {
            return EPERM;
        } else if (num == c->concurrency()) {
            return 0;
        }
    }
    BAIDU_SCOPED_LOCK(bthread::g_task_control_mutex);
    c = bthread::get_task_control();
    if (c == NULL) {
        if (bthread::never_set_bthread_concurrency) {
            bthread::never_set_bthread_concurrency = false;
            bthread::FLAGS_bthread_concurrency = num;
        } else if (num > bthread::FLAGS_bthread_concurrency) {
            bthread::FLAGS_bthread_concurrency = num;
        }
        return 0;
    }
    if (bthread::FLAGS_bthread_concurrency != c->concurrency()) {
        LOG(ERROR) << "CHECK failed: bthread_concurrency="
                   << bthread::FLAGS_bthread_concurrency
                   << " != tc_concurrency=" << c->concurrency();
        bthread::FLAGS_bthread_concurrency = c->concurrency();
    }
    if (num > bthread::FLAGS_bthread_concurrency) {
        // Create more workers if needed.
        auto added = bthread::add_workers_for_each_tag(num - bthread::FLAGS_bthread_concurrency);
        bthread::FLAGS_bthread_concurrency += added;
    }
    return (num == bthread::FLAGS_bthread_concurrency ? 0 : EPERM);
}

int bthread_getconcurrency_by_tag(bthread_tag_t tag) {
    BAIDU_SCOPED_LOCK(bthread::g_task_control_mutex);
    auto c = bthread::get_task_control();
    if (c == NULL) {
        return EPERM;
    }
    return c->concurrency(tag);
}

int bthread_setconcurrency_by_tag(int num, bthread_tag_t tag) {
    if (tag == BTHREAD_TAG_INVALID) {
        return 0;
    } else if (tag < BTHREAD_TAG_DEFAULT || tag >= FLAGS_task_group_ntags) {
        return EINVAL;
    }
    if (num < BTHREAD_MIN_CONCURRENCY || num > BTHREAD_MAX_CONCURRENCY) {
        LOG(ERROR) << "Invalid concurrency_by_tag=" << num;
        return EINVAL;
    }
    auto c = bthread::get_or_new_task_control();
    BAIDU_SCOPED_LOCK(bthread::g_task_control_mutex);
    auto tag_ngroup = c->concurrency(tag);
    auto add = num - tag_ngroup;

    if (add >= 0) {
        auto added = c->add_workers(add, tag);
        bthread::FLAGS_bthread_concurrency += added;
        return (add == added ? 0 : EPERM);
    } else {
        LOG(WARNING) << "Fail to set concurrency by tag: " << tag
                     << ", tag concurrency should be larger than old oncurrency. old concurrency: "
                     << tag_ngroup << ", new concurrency: " << num;
        return EPERM;
    }
}

int bthread_about_to_quit() {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (g != NULL) {
        bthread::TaskMeta* current_task = g->current_task();
        if(!(current_task->attr.flags & BTHREAD_NEVER_QUIT)) {
            current_task->about_to_quit = true;
        }
        return 0;
    }
    return EPERM;
}

int bthread_timer_add(bthread_timer_t* id, timespec abstime,
                      void (*on_timer)(void*), void* arg) {
    bthread::TaskControl* c = bthread::get_or_new_task_control();
    if (c == NULL) {
        return ENOMEM;
    }
    bthread::TimerThread* tt = bthread::get_or_create_global_timer_thread();
    if (tt == NULL) {
        return ENOMEM;
    }
    bthread_timer_t tmp = tt->schedule(on_timer, arg, abstime);
    if (tmp != 0) {
        *id = tmp;
        return 0;
    }
    return ESTOP;
}

int bthread_timer_del(bthread_timer_t id) {
    bthread::TaskControl* c = bthread::get_task_control();
    if (c != NULL) {
        bthread::TimerThread* tt = bthread::get_global_timer_thread();
        if (tt == NULL) {
            return EINVAL;
        }
        const int state = tt->unschedule(id);
        if (state >= 0) {
            return state;
        }
    }
    return EINVAL;
}

int bthread_usleep(uint64_t microseconds) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (NULL != g && !g->is_current_pthread_task()) {
        return bthread::TaskGroup::usleep(&g, microseconds);
    }
    return ::usleep(microseconds);
}

int bthread_yield(void) {
    bthread::TaskGroup* g = bthread::tls_task_group;
    if (NULL != g && !g->is_current_pthread_task()) {
        bthread::TaskGroup::yield(&g);
        return 0;
    }
    // pthread_yield is not available on MAC
    return sched_yield();
}

int bthread_set_worker_startfn(void (*start_fn)()) {
    if (start_fn == NULL) {
        return EINVAL;
    }
    bthread::g_worker_startfn = start_fn;
    return 0;
}

int bthread_set_tagged_worker_startfn(void (*start_fn)(bthread_tag_t)) {
    if (start_fn == NULL) {
        return EINVAL;
    }
    bthread::g_tagged_worker_startfn = start_fn;
    return 0;
}

int bthread_set_create_span_func(void* (*func)()) {
    if (func == NULL) {
        return EINVAL;
    }
    bthread::g_create_span_func = func;
    return 0;
}

void bthread_stop_world() {
    bthread::TaskControl* c = bthread::get_task_control();
    if (c != NULL) {
        c->stop_and_join();
    }
}

int bthread_list_init(bthread_list_t* list,
                      unsigned /*size*/,
                      unsigned /*conflict_size*/) {
    list->impl = new (std::nothrow) bthread::TidList;
    if (NULL == list->impl) {
        return ENOMEM;
    }
    // Set unused fields to zero as well.
    list->head = 0;
    list->size = 0;
    list->conflict_head = 0;
    list->conflict_size = 0;
    return 0;
}

void bthread_list_destroy(bthread_list_t* list) {
    delete static_cast<bthread::TidList*>(list->impl);
    list->impl = NULL;
}

int bthread_list_add(bthread_list_t* list, bthread_t id) {
    if (list->impl == NULL) {
        return EINVAL;
    }
    return static_cast<bthread::TidList*>(list->impl)->add(id);
}

int bthread_list_stop(bthread_list_t* list) {
    if (list->impl == NULL) {
        return EINVAL;
    }
    static_cast<bthread::TidList*>(list->impl)->apply(bthread::TidStopper());
    return 0;
}

int bthread_list_join(bthread_list_t* list) {
    if (list->impl == NULL) {
        return EINVAL;
    }
    static_cast<bthread::TidList*>(list->impl)->apply(bthread::TidJoiner());
    return 0;
}

bthread_tag_t bthread_self_tag(void) {
    return bthread::tls_task_group != nullptr ? bthread::tls_task_group->tag()
                                              : BTHREAD_TAG_DEFAULT;
}

uint64_t bthread_cpu_clock_ns(void) {
     bthread::TaskGroup* g = bthread::tls_task_group;
    if (g != NULL && !g->is_current_main_task()) {
        return g->current_task_cpu_clock_ns();
    }
    return 0;
}

}  // extern "C"
