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

#include <sys/syscall.h>                   // SYS_gettid
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
#include <gflags/gflags.h>
#include "bthread/log.h"

DEFINE_int32(task_group_delete_delay, 1,
             "delay deletion of TaskGroup for so many seconds");
DEFINE_int32(task_group_runqueue_capacity, 4096,
             "capacity of runqueue in each TaskGroup");
DEFINE_int32(task_group_yield_before_idle, 0,
             "TaskGroup yields so many times before idle");
DEFINE_int32(task_group_ntags, 1, "TaskGroup will be grouped by number ntags");

namespace bthread {

DECLARE_int32(bthread_concurrency);
DECLARE_int32(bthread_min_concurrency);

extern pthread_mutex_t g_task_control_mutex;
extern BAIDU_THREAD_LOCAL TaskGroup* tls_task_group;
void (*g_worker_startfn)() = NULL;
void (*g_tagged_worker_startfn)(bthread_tag_t) = NULL;

// May be called in other modules to run startfn in non-worker pthreads.
void run_worker_startfn() {
    if (g_worker_startfn) {
        g_worker_startfn();
    }
}

void run_tagged_worker_startfn(bthread_tag_t tag) {
    if (g_tagged_worker_startfn) {
        g_tagged_worker_startfn(tag);
    }
}

struct WorkerThreadArgs {
    WorkerThreadArgs(TaskControl* _c, bthread_tag_t _t) : c(_c), tag(_t) {}
    TaskControl* c;
    bthread_tag_t tag;
};

void* TaskControl::worker_thread(void* arg) {
    run_worker_startfn();
#ifdef BAIDU_INTERNAL
    logging::ComlogInitializer comlog_initializer;
#endif

    auto dummy = static_cast<WorkerThreadArgs*>(arg);
    auto c = dummy->c;
    auto tag = dummy->tag;
    delete dummy;
    run_tagged_worker_startfn(tag);

    TaskGroup* g = c->create_group(tag);
    TaskStatistics stat;
    if (NULL == g) {
        LOG(ERROR) << "Fail to create TaskGroup in pthread=" << pthread_self();
        return NULL;
    }

    g->_tid = syscall(SYS_gettid);

    std::string worker_thread_name = butil::string_printf(
        "brpc_wkr:%d-%d", g->tag(),
        c->_next_worker_id.fetch_add(1, butil::memory_order_relaxed));
    butil::PlatformThread::SetName(worker_thread_name.c_str());
    BT_VLOG << "Created worker=" << pthread_self() << " tid=" << g->_tid
            << " bthread=" << g->main_tid() << " tag=" << g->tag();
    tls_task_group = g;
    c->_nworkers << 1;
    c->tag_nworkers(g->tag()) << 1;

    g->run_main_task();

    stat = g->main_stat();
    BT_VLOG << "Destroying worker=" << pthread_self() << " bthread="
            << g->main_tid() << " idle=" << stat.cputime_ns / 1000000.0
            << "ms uptime=" << g->current_uptime_ns() / 1000000.0 << "ms";
    tls_task_group = NULL;
    g->destroy_self();
    c->_nworkers << -1;
    c->tag_nworkers(g->tag()) << -1;
    return NULL;
}

TaskGroup* TaskControl::create_group(bthread_tag_t tag) {
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
    if (_add_group(g, tag) != 0) {
        delete g;
        return NULL;
    }
    return g;
}

static void print_rq_sizes_in_the_tc(std::ostream &os, void *arg) {
    TaskControl *tc = (TaskControl *)arg;
    tc->print_rq_sizes(os);
}

static double get_cumulated_worker_time_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_worker_time();
}

struct CumulatedWithTagArgs {
    CumulatedWithTagArgs(TaskControl* _c, bthread_tag_t _t) : c(_c), t(_t) {}
    TaskControl* c;
    bthread_tag_t t;
};

static double get_cumulated_worker_time_from_this_with_tag(void* arg) {
    auto a = static_cast<CumulatedWithTagArgs*>(arg);
    auto c = a->c;
    auto t = a->t;
    return c->get_cumulated_worker_time_with_tag(t);
}

static int64_t get_cumulated_switch_count_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_switch_count();
}

static int64_t get_cumulated_signal_count_from_this(void *arg) {
    return static_cast<TaskControl*>(arg)->get_cumulated_signal_count();
}

TaskControl::TaskControl()
    // NOTE: all fileds must be initialized before the vars.
    : _tagged_ngroup(FLAGS_task_group_ntags)
    , _tagged_groups(FLAGS_task_group_ntags)
    , _init(false)
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
    , _nbthreads("bthread_count")
    , _pl(FLAGS_task_group_ntags)
{}

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

    // task group group by tags
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        _tagged_ngroup[i].store(0, std::memory_order_relaxed);
        auto tag_str = std::to_string(i);
        _tagged_nworkers.push_back(new bvar::Adder<int64_t>("bthread_worker_count", tag_str));
        _tagged_cumulated_worker_time.push_back(new bvar::PassiveStatus<double>(
            get_cumulated_worker_time_from_this_with_tag, new CumulatedWithTagArgs{this, i}));
        _tagged_worker_usage_second.push_back(new bvar::PerSecond<bvar::PassiveStatus<double>>(
            "bthread_worker_usage", tag_str, _tagged_cumulated_worker_time[i], 1));
        _tagged_nbthreads.push_back(new bvar::Adder<int64_t>("bthread_count", tag_str));
    }

    // Make sure TimerThread is ready.
    if (get_or_create_global_timer_thread() == NULL) {
        LOG(ERROR) << "Fail to get global_timer_thread";
        return -1;
    }

#ifdef BRPC_BTHREAD_TRACER
    if (!_task_tracer.Init()) {
        LOG(ERROR) << "Fail to init TaskTracer";
        return -1;
    }
#endif // BRPC_BTHREAD_TRACER
    
    _workers.resize(_concurrency);   
    for (int i = 0; i < _concurrency; ++i) {
        auto arg = new WorkerThreadArgs(this, i % FLAGS_task_group_ntags);
        const int rc = pthread_create(&_workers[i], NULL, worker_thread, arg);
        if (rc) {
            delete arg;
            LOG(ERROR) << "Fail to create _workers[" << i << "], " << berror(rc);
            return -1;
        }
    }
    _worker_usage_second.expose("bthread_worker_usage");
    _switch_per_second.expose("bthread_switch_second");
    _signal_per_second.expose("bthread_signal_second");
    _status.expose("bthread_group_status");

    // Wait for at least one group is added so that choose_one_group()
    // never returns NULL.
    // TODO: Handle the case that worker quits before add_group
    for (int i = 0; i < FLAGS_task_group_ntags;) {
        if (_tagged_ngroup[i].load(std::memory_order_acquire) == 0) {
            usleep(100);  // TODO: Elaborate
            continue;
        }
        ++i;
    }

    _init.store(true, butil::memory_order_release);

    return 0;
}

int TaskControl::add_workers(int num, bthread_tag_t tag) {
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
        auto arg = new WorkerThreadArgs(this, tag);
        const int rc = pthread_create(
                &_workers[i + old_concurency], NULL, worker_thread, arg);
        if (rc) {
            delete arg;
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

TaskGroup* TaskControl::choose_one_group(bthread_tag_t tag) {
    CHECK(tag >= BTHREAD_TAG_DEFAULT && tag < FLAGS_task_group_ntags);
    auto& groups = tag_group(tag);
    const auto ngroup = tag_ngroup(tag).load(butil::memory_order_acquire);
    if (ngroup != 0) {
        return groups[butil::fast_rand_less_than(ngroup)];
    }
    CHECK(false) << "Impossible: ngroup is 0";
    return NULL;
}

#ifdef BRPC_BTHREAD_TRACER
void TaskControl::stack_trace(std::ostream& os, bthread_t tid) {
    _task_tracer.Trace(os, tid);
}

std::string TaskControl::stack_trace(bthread_t tid) {
    return _task_tracer.Trace(tid);
}
#endif // BRPC_BTHREAD_TRACER

extern int stop_and_join_epoll_threads();

void TaskControl::stop_and_join() {
    // Close epoll threads so that worker threads are not waiting on epoll(
    // which cannot be woken up by signal_task below)
    CHECK_EQ(0, stop_and_join_epoll_threads());

    // Stop workers
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        _stop = true;
        std::for_each(
            _tagged_ngroup.begin(), _tagged_ngroup.end(),
            [](butil::atomic<size_t>& index) { index.store(0, butil::memory_order_relaxed); });
    }
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        for (auto& pl : _pl[i]) {
            pl.stop();
        }
    }

    for (auto worker: _workers) {
        // Interrupt blocking operations.
#ifdef BRPC_BTHREAD_TRACER
        // TaskTracer has registered signal handler for SIGURG.
        pthread_kill(worker, SIGURG);
#else
        interrupt_pthread(worker);
#endif // BRPC_BTHREAD_TRACER
    }
    // Join workers
    for (auto worker : _workers) {
        pthread_join(worker, NULL);
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
    
    stop_and_join();
}

int TaskControl::_add_group(TaskGroup* g, bthread_tag_t tag) {
    if (__builtin_expect(NULL == g, 0)) {
        return -1;
    }
    std::unique_lock<butil::Mutex> mu(_modify_group_mutex);
    if (_stop) {
        return -1;
    }
    g->set_tag(tag);
    g->set_pl(&_pl[tag][butil::fmix64(pthread_numeric_id()) % PARKING_LOT_NUM]);
    size_t ngroup = _tagged_ngroup[tag].load(butil::memory_order_relaxed);
    if (ngroup < (size_t)BTHREAD_MAX_CONCURRENCY) {
        _tagged_groups[tag][ngroup] = g;
        _tagged_ngroup[tag].store(ngroup + 1, butil::memory_order_release);
    }
    mu.unlock();
    // See the comments in _destroy_group
    // TODO: Not needed anymore since non-worker pthread cannot have TaskGroup
    // signal_task(65536, tag);
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
        auto tag = g->tag();
        auto& groups = tag_group(tag);
        const size_t ngroup = tag_ngroup(tag).load(butil::memory_order_relaxed);
        for (size_t i = 0; i < ngroup; ++i) {
            if (groups[i] == g) {
                // No need for atomic_thread_fence because lock did it.
                groups[i] = groups[ngroup - 1];
                // Change _ngroup and keep _groups unchanged at last so that:
                //  - If steal_task sees the newest _ngroup, it would not touch
                //    _groups[ngroup -1]
                //  - If steal_task sees old _ngroup and is still iterating on
                //    _groups, it would not miss _groups[ngroup - 1] which was 
                //    swapped to _groups[i]. Although adding new group would
                //    overwrite it, since we do signal_task in _add_group(),
                //    we think the pending tasks of _groups[ngroup - 1] would
                //    not miss.
                tag_ngroup(tag).store(ngroup - 1, butil::memory_order_release);
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
    auto tag = tls_task_group->tag();
    // 1: Acquiring fence is paired with releasing fence in _add_group to
    // avoid accessing uninitialized slot of _groups.
    const size_t ngroup = tag_ngroup(tag).load(butil::memory_order_acquire/*1*/);
    if (0 == ngroup) {
        return false;
    }

    // NOTE: Don't return inside `for' iteration since we need to update |seed|
    bool stolen = false;
    size_t s = *seed;
    auto& groups = tag_group(tag);
    for (size_t i = 0; i < ngroup; ++i, s += offset) {
        TaskGroup* g = groups[s % ngroup];
        // g is possibly NULL because of concurrent _destroy_group
        if (g) {
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

void TaskControl::signal_task(int num_task, bthread_tag_t tag) {
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
    auto& pl = tag_pl(tag);
    int start_index = butil::fmix64(pthread_numeric_id()) % PARKING_LOT_NUM;
    num_task -= pl[start_index].signal(1);
    if (num_task > 0) {
        for (int i = 1; i < PARKING_LOT_NUM && num_task > 0; ++i) {
            if (++start_index >= PARKING_LOT_NUM) {
                start_index = 0;
            }
            num_task -= pl[start_index].signal(1);
        }
    }
    if (num_task > 0 &&
        FLAGS_bthread_min_concurrency > 0 &&    // test min_concurrency for performance
        _concurrency.load(butil::memory_order_relaxed) < FLAGS_bthread_concurrency) {
        // TODO: Reduce this lock
        BAIDU_SCOPED_LOCK(g_task_control_mutex);
        if (_concurrency.load(butil::memory_order_acquire) < FLAGS_bthread_concurrency) {
            add_workers(1, tag);
        }
    }
}

void TaskControl::print_rq_sizes(std::ostream& os) {
    size_t ngroup = 0;
    std::for_each(_tagged_ngroup.begin(), _tagged_ngroup.end(), [&](butil::atomic<size_t>& index) {
        ngroup += index.load(butil::memory_order_relaxed);
    });
    DEFINE_SMALL_ARRAY(int, nums, ngroup, 128);
    {
        BAIDU_SCOPED_LOCK(_modify_group_mutex);
        // ngroup > _ngroup: nums[_ngroup ... ngroup-1] = 0
        // ngroup < _ngroup: just ignore _groups[_ngroup ... ngroup-1]
        int i = 0;
        for_each_task_group([&](TaskGroup* g) {
            nums[i] = (g ? g->_rq.volatile_size() : 0);
            ++i;
        });
    }
    for (size_t i = 0; i < ngroup; ++i) {
        os << nums[i] << ' ';
    }
}

double TaskControl::get_cumulated_worker_time() {
    int64_t cputime_ns = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    for_each_task_group([&](TaskGroup* g) {
        if (g) {
            cputime_ns += g->_cumulated_cputime_ns;
        }
    });
    return cputime_ns / 1000000000.0;
}

double TaskControl::get_cumulated_worker_time_with_tag(bthread_tag_t tag) {
    int64_t cputime_ns = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    const size_t ngroup = tag_ngroup(tag).load(butil::memory_order_relaxed);
    auto& groups = tag_group(tag);
    for (size_t i = 0; i < ngroup; ++i) {
        if (groups[i]) {
            cputime_ns += groups[i]->_cumulated_cputime_ns;
        }
    }
    return cputime_ns / 1000000000.0;
}

int64_t TaskControl::get_cumulated_switch_count() {
    int64_t c = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    for_each_task_group([&](TaskGroup* g) {
        if (g) {
            c += g->_nswitch;
        }
    });
    return c;
}

int64_t TaskControl::get_cumulated_signal_count() {
    int64_t c = 0;
    BAIDU_SCOPED_LOCK(_modify_group_mutex);
    for_each_task_group([&](TaskGroup* g) {
        if (g) {
            c += g->_nsignaled + g->_remote_nsignaled;
        }
    });
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
