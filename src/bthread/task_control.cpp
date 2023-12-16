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

#include "butil/scoped_lock.h"             // BAIDU_SCOPED_LOCK
#include "butil/errno.h"                   // berror
#include "butil/logging.h"
#include "butil/threading/platform_thread.h"
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "bthread/sys_futex.h"            // futex_wake_private
#include "bthread/interrupt_pthread.h"
#include "bthread/processor.h"            // cpu_relax
#include "bthread/task_group.h"           // TaskGroup
#include "bthread/task_control.h"
#include "bthread/timer_thread.h"         // global_timer_thread
#include <atomic>
#include <gflags/gflags.h>
#include "bthread/log.h"

DEFINE_int32(task_group_delete_delay, 1,
             "delay deletion of TaskGroup for so many seconds");
DEFINE_int32(task_group_runqueue_capacity, 4096,
             "capacity of runqueue in each TaskGroup");
DEFINE_int32(task_group_yield_before_idle, 0,
             "TaskGroup yields so many times before idle");

namespace bthread {

DECLARE_int32(bthread_concurrency);
DECLARE_int32(bthread_min_concurrency);

extern pthread_mutex_t g_task_control_mutex;
extern BAIDU_THREAD_LOCAL TaskGroup* tls_task_group;
void (*g_worker_startfn)() = NULL;

// May be called in other modules to run startfn in non-worker pthreads.
void run_worker_startfn() {
    if (g_worker_startfn) {
        g_worker_startfn();
    }
}

void* TaskControl::worker_thread(void* arg) {
    run_worker_startfn();    
#ifdef BAIDU_INTERNAL
    logging::ComlogInitializer comlog_initializer;
#endif
    
    TaskControl* c = static_cast<TaskControl*>(arg);
    TaskGroup* g = c->create_group();
    TaskStatistics stat;
    if (NULL == g) {
        LOG(ERROR) << "Fail to create TaskGroup in pthread=" << pthread_self();
        return NULL;
    }
    std::string worker_thread_name = butil::string_printf(
        "brpc_worker:%d",
        c->_next_worker_id.fetch_add(1, butil::memory_order_relaxed));
    butil::PlatformThread::SetName(worker_thread_name.c_str());
    BT_VLOG << "Created worker=" << pthread_self()
            << " bthread=" << g->main_tid();

    tls_task_group = g;
    c->_nworkers << 1;
    g->run_main_task();

    stat = g->main_stat();
    BT_VLOG << "Destroying worker=" << pthread_self() << " bthread="
            << g->main_tid() << " idle=" << stat.cputime_ns / 1000000.0
            << "ms uptime=" << g->current_uptime_ns() / 1000000.0 << "ms";
    tls_task_group = NULL;
    g->destroy_self();
    c->_nworkers << -1;
    return NULL;
}

TaskGroup* TaskControl::create_group() {
    TaskGroup* g = new (std::nothrow) TaskGroup(this);
    if (NULL == g) {
        LOG(FATAL) << "Fail to new TaskGroup";
        return NULL;
    }
    if (g->init(FLAGS_task_group_runqueue_capacity) != 0) {
        LOG(ERROR) << "Fail to init TaskGroup";
        delete g;
        return NULL;
    }
    if (_add_group(g) != 0) {
        delete g;
        return NULL;
    }
    return g;
}

static void print_rq_sizes_in_the_tc(std::ostream &os, void *arg) {
    TaskControl *tc = (TaskControl *)arg;
    tc->print_rq_sizes(os);
}

static void print_resume_q_sizes_in_the_tc(std::ostream &os, void *arg) {
    TaskControl *tc = (TaskControl *)arg;
    tc->print_resume_q_sizes(os);
}

static double get_cumulated_worker_time_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_worker_time();
}

static int64_t get_cumulated_switch_count_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_switch_count();
}

static int64_t get_cumulated_signal_count_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_signal_count();
}

TaskControl::TaskControl()
    // NOTE: all fileds must be initialized before the vars.
    : _ngroup(0)
    , _groups((TaskGroup**)calloc(BTHREAD_MAX_CONCURRENCY, sizeof(TaskGroup*)))
    , _stop(false)
    , _concurrency(0)
    , _next_worker_id(0)
    , _nworkers("bthread_worker_count")
    , _pending_time(NULL)
      // Delay exposure of following two vars because they rely on TC which
      // is not initialized yet.
    , _cumulated_worker_time(get_cumulated_worker_time_from_this, this)
    , _worker_usage_second(&_cumulated_worker_time, 1)
    , _cumulated_switch_count(get_cumulated_switch_count_from_this, this)
    , _switch_per_second(&_cumulated_switch_count)
    , _cumulated_signal_count(get_cumulated_signal_count_from_this, this)
    , _signal_per_second(&_cumulated_signal_count)
    , _status(print_rq_sizes_in_the_tc, this)
    , _resume_q_status(print_resume_q_sizes_in_the_tc, this)
    , _nbthreads("bthread_count")
{
    // calloc shall set memory to zero
    CHECK(_groups) << "Fail to create array of groups";
}

int TaskControl::init(int concurrency) {
    if (_concurrency != 0) {
        LOG(ERROR) << "Already initialized";
        return -1;
    }
    if (concurrency <= 0) {
        LOG(ERROR) << "Invalid concurrency=" << concurrency;
        return -1;
    }
    _concurrency = concurrency;

    // Make sure TimerThread is ready.
    if (get_or_create_global_timer_thread() == NULL) {
        LOG(ERROR) << "Fail to get global_timer_thread";
        return -1;
    }
    
    _workers.resize(_concurrency);   
    for (int i = 0; i < _concurrency; ++i) {
        const int rc = pthread_create(&_workers[i], NULL, worker_thread, this);
        if (rc) {
            LOG(ERROR) << "Fail to create _workers[" << i << "], " << berror(rc);
            return -1;
        }
    }
    _worker_usage_second.expose("bthread_worker_usage");
    _switch_per_second.expose("bthread_switch_second");
    _signal_per_second.expose("bthread_signal_second");
    _status.expose("bthread_group_status");
    _resume_q_status.expose("bthread_group_resume_q_status_");

    // Wait for at least one group is added so that choose_one_group()
    // never returns NULL.
    // TODO: Handle the case that worker quits before add_group
    while (_ngroup == 0) {
        usleep(100);  // TODO: Elaborate
    }
    return 0;
}

int TaskControl::add_workers(int num) {
    if (num <= 0) {
        return 0;
    }
    try {
        _workers.resize(_concurrency + num);
    } catch (...) {
        return 0;
    }
    const int old_concurency = _concurrency.load(butil::memory_order_relaxed);
    for (int i = 0; i < num; ++i) {
        // Worker will add itself to _idle_workers, so we have to add
        // _concurrency before create a worker.
        _concurrency.fetch_add(1);
        const int rc = pthread_create(
                &_workers[i + old_concurency], NULL, worker_thread, this);
        if (rc) {
            LOG(WARNING) << "Fail to create _workers[" << i + old_concurency
                         << "], " << berror(rc);
            _concurrency.fetch_sub(1, butil::memory_order_release);
            break;
        }
    }
    // Cannot fail
    _workers.resize(_concurrency.load(butil::memory_order_relaxed));
    return _concurrency.load(butil::memory_order_relaxed) - old_concurency;
}

TaskGroup* TaskControl::choose_one_group() {
    const size_t ngroup = _ngroup.load(butil::memory_order_acquire);
    if (ngroup != 0) {
        return _groups[butil::fast_rand_less_than(ngroup)];
    }
    CHECK(false) << "Impossible: ngroup is 0";
    return NULL;
}

extern int stop_and_join_epoll_threads();

void TaskControl::stop_and_join() {
    // Close epoll threads so that worker threads are not waiting on epoll(
    // which cannot be woken up by signal_task below)
    CHECK_EQ(0, stop_and_join_epoll_threads());

    // Stop workers
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        _stop = true;
        _ngroup.exchange(0, butil::memory_order_relaxed); 
    }
    for (int i = 0; i < PARKING_LOT_NUM; ++i) {
        _pl[i].stop();
    }
    // Interrupt blocking operations.
    for (size_t i = 0; i < _workers.size(); ++i) {
        interrupt_pthread(_workers[i]);
    }
    // Join workers
    for (size_t i = 0; i < _workers.size(); ++i) {
        pthread_join(_workers[i], NULL);
    }
}

TaskControl::~TaskControl() {
    // NOTE: g_task_control is not destructed now because the situation
    //       is extremely racy.
    delete _pending_time.exchange(NULL, butil::memory_order_relaxed);
    _worker_usage_second.hide();
    _switch_per_second.hide();
    _signal_per_second.hide();
    _status.hide();
    _resume_q_status.hide();
    
    stop_and_join();

    free(_groups);
    _groups = NULL;
}

int TaskControl::_add_group(TaskGroup* g) {
    if (__builtin_expect(NULL == g, 0)) {
        return -1;
    }
    std::unique_lock<butil::Mutex> mu(_modify_group_mutex);
    if (_stop) {
        return -1;
    }
    size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    if (ngroup < (size_t)BTHREAD_MAX_CONCURRENCY) {
        _groups[ngroup] = g;
        _ngroup.store(ngroup + 1, butil::memory_order_release);
    }
    mu.unlock();
    // See the comments in _destroy_group
    // TODO: Not needed anymore since non-worker pthread cannot have TaskGroup
    signal_task(65536);
    return 0;
}

void TaskControl::delete_task_group(void* arg) {
    delete(TaskGroup*)arg;
}

int TaskControl::_destroy_group(TaskGroup* g) {
    if (NULL == g) {
        LOG(ERROR) << "Param[g] is NULL";
        return -1;
    }
    if (g->_control != this) {
        LOG(ERROR) << "TaskGroup=" << g
                   << " does not belong to this TaskControl=" << this;
        return -1;
    }
    bool erased = false;
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
        for (size_t i = 0; i < ngroup; ++i) {
            if (_groups[i] == g) {
                // No need for atomic_thread_fence because lock did it.
                _groups[i] = _groups[ngroup - 1];
                // Change _ngroup and keep _groups unchanged at last so that:
                //  - If steal_task sees the newest _ngroup, it would not touch
                //    _groups[ngroup -1]
                //  - If steal_task sees old _ngroup and is still iterating on
                //    _groups, it would not miss _groups[ngroup - 1] which was 
                //    swapped to _groups[i]. Although adding new group would
                //    overwrite it, since we do signal_task in _add_group(),
                //    we think the pending tasks of _groups[ngroup - 1] would
                //    not miss.
                _ngroup.store(ngroup - 1, butil::memory_order_release);
                //_groups[ngroup - 1] = NULL;
                erased = true;
                break;
            }
        }
    }

    // Can't delete g immediately because for performance consideration,
    // we don't lock _modify_group_mutex in steal_task which may
    // access the removed group concurrently. We use simple strategy here:
    // Schedule a function which deletes the TaskGroup after
    // FLAGS_task_group_delete_delay seconds
    if (erased) {
        get_global_timer_thread()->schedule(
            delete_task_group, g,
            butil::microseconds_from_now(FLAGS_task_group_delete_delay * 1000000L));
    }
    return 0;
}

bool TaskControl::steal_task(bthread_t* tid, size_t* seed, size_t offset) {
    // 1: Acquiring fence is paired with releasing fence in _add_group to
    // avoid accessing uninitialized slot of _groups.
    const size_t ngroup = _ngroup.load(butil::memory_order_acquire/*1*/);
    if (0 == ngroup) {
        return false;
    }

    // NOTE: Don't return inside `for' iteration since we need to update |seed|
    bool stolen = false;
    size_t s = *seed;
    for (size_t i = 0; i < ngroup; ++i, s += offset) {
        TaskGroup* g = _groups[s % ngroup];
        // g is possibly NULL because of concurrent _destroy_group
        if (g) {
            if (g->pop_resume_task(tid)) {
                stolen = true;
                break;
            }
            if (g->_rq.steal(tid)) {
                stolen = true;
                break;
            }
            if (g->_remote_rq.pop(tid)) {
                stolen = true;
                break;
            }
        }
    }
    *seed = s;
    return stolen;
}

void TaskControl::signal_task(int num_task) {
    if (num_task <= 0) {
        return;
    }
    // TODO(gejun): Current algorithm does not guarantee enough threads will
    // be created to match caller's requests. But in another side, there's also
    // many useless signalings according to current impl. Capping the concurrency
    // is a good balance between performance and timeliness of scheduling.
    if (num_task > 2) {
        num_task = 2;
    }
    int start_index = butil::fmix64(pthread_numeric_id()) % PARKING_LOT_NUM;
    num_task -= _pl[start_index].signal(1);
    if (num_task > 0) {
        for (int i = 1; i < PARKING_LOT_NUM && num_task > 0; ++i) {
            if (++start_index >= PARKING_LOT_NUM) {
                start_index = 0;
            }
            num_task -= _pl[start_index].signal(1);
        }
    }
    if (num_task > 0 &&
        FLAGS_bthread_min_concurrency > 0 &&    // test min_concurrency for performance
        _concurrency.load(butil::memory_order_relaxed) < FLAGS_bthread_concurrency) {
        // TODO: Reduce this lock
        BAIDU_SCOPED_LOCK(g_task_control_mutex);
        if (_concurrency.load(butil::memory_order_acquire) < FLAGS_bthread_concurrency) {
            add_workers(1);
        }
    }
}

void TaskControl::print_rq_sizes(std::ostream& os) {
    const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    DEFINE_SMALL_ARRAY(int, nums, ngroup, 128);
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        // ngroup > _ngroup: nums[_ngroup ... ngroup-1] = 0
        // ngroup < _ngroup: just ignore _groups[_ngroup ... ngroup-1]
        for (size_t i = 0; i < ngroup; ++i) {
            nums[i] = (_groups[i] ? _groups[i]->_rq.volatile_size() : 0);
        }
    }
    for (size_t i = 0; i < ngroup; ++i) {
        os << nums[i] << ' ';
    }
}

void TaskControl::print_resume_q_sizes(std::ostream &os) {
    const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    DEFINE_SMALL_ARRAY(int, nums, ngroup, 128);
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        // ngroup > _ngroup: nums[_ngroup ... ngroup-1] = 0
        // ngroup < _ngroup: just ignore _groups[_ngroup ... ngroup-1]
        for (size_t i = 0; i < ngroup; ++i) {
            nums[i] = (_groups[i] ? _groups[i]->_resume_rq_cnt.load(std::memory_order_relaxed) : 0);
        }
    }
    for (size_t i = 0; i < ngroup; ++i) {
        os << nums[i] << ' ';
    }
}

double TaskControl::get_cumulated_worker_time() {
    int64_t cputime_ns = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    for (size_t i = 0; i < ngroup; ++i) {
        if (_groups[i]) {
            cputime_ns += _groups[i]->_cumulated_cputime_ns;
        }
    }
    return cputime_ns / 1000000000.0;
}

int64_t TaskControl::get_cumulated_switch_count() {
    int64_t c = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    for (size_t i = 0; i < ngroup; ++i) {
        if (_groups[i]) {
            c += _groups[i]->_nswitch;
        }
    }
    return c;
}

int64_t TaskControl::get_cumulated_signal_count() {
    int64_t c = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    const size_t ngroup = _ngroup.load(butil::memory_order_relaxed);
    for (size_t i = 0; i < ngroup; ++i) {
        TaskGroup* g = _groups[i];
        if (g) {
            c += g->_nsignaled + g->_remote_nsignaled;
        }
    }
    return c;
}

bvar::LatencyRecorder* TaskControl::create_exposed_pending_time() {
    bool is_creator = false;
    _pending_time_mutex.lock();
    bvar::LatencyRecorder* pt = _pending_time.load(butil::memory_order_consume);
    if (!pt) {
        pt = new bvar::LatencyRecorder;
        _pending_time.store(pt, butil::memory_order_release);
        is_creator = true;
    }
    _pending_time_mutex.unlock();
    if (is_creator) {
        pt->expose("bthread_creation");
    }
    return pt;
}

}  // namespace bthread
