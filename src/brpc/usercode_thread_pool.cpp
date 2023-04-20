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

#include <deque>
#include <vector>
#include <gflags/gflags.h>
#include "butil/threading/platform_thread.h"
#include "brpc/usercode_thread_pool.h"
namespace bthread {
// Bthread local storage
extern __thread bthread::LocalStorage tls_bls;
// Defined in bthread/task_control.cpp
void run_worker_startfn();
}  // namespace bthread

namespace brpc {

DEFINE_int32(usercode_thread_pool_map_nbucket, 64 * 2,
             "usercode thread pool map bucket size");

static void* UserCodeRunner(void* args) {
    static_cast<UserCodeThreadWorker*>(args)->UserCodeLoop();
    return NULL;
}

UserCodeThreadWorker::UserCodeThreadWorker(UserCodeThreadPool* pool)
    : _pool(pool), _running(true) {}

// Entry of backup thread for running user code.
void UserCodeThreadWorker::UserCodeLoop() {
    auto pool_name = _pool->pool_name();
    auto worker_id = _pool->NextWorkerId();
    std::string thread_name =
        butil::string_printf("usercode_%s:%ld", pool_name.c_str(), worker_id);
    butil::PlatformThread::SetName(thread_name.c_str());
    auto startfn = _pool->thread_startfn();
    if (startfn) {
        startfn();
    } else {
        bthread::run_worker_startfn();
    }

    int64_t last_time = butil::cpuwide_time_us();
    while (true) {
        bool blocked = false;
        std::deque<UserCodeTask> usercodes;
        {
            std::unique_lock<bthread::Mutex> lk(_mutex);
            while (_running.load(std::memory_order_relaxed) && _queue.empty()) {
                _cond.wait(lk);
                blocked = true;
            }
            if (!_running.load(std::memory_order_relaxed)) {
                break;
            }
            usercodes = std::move(_queue);
            _queue = {};
        }
        const int64_t begin_time =
            (blocked ? butil::cpuwide_time_us() : last_time);
        for (auto& usercode : usercodes) {
            bthread::tls_bls.assigned_data = usercode.assigned_data;
            usercode.fn(usercode.arg);
        }
        const int64_t end_time = butil::cpuwide_time_us();
        _pool->inpool_count << usercodes.size();
        _pool->inpool_elapse_us << (end_time - begin_time);
        last_time = end_time;
    }
}

void UserCodeThreadWorker::UserCodeRun(UserCodeTask&& usercode) {
    std::unique_lock<bthread::Mutex> lk(_mutex);
    _queue.emplace_back(std::move(usercode));
    _cond.notify_one();
}

void UserCodeThreadWorker::Start() {
    _worker = std::thread(UserCodeRunner, this);
}

void UserCodeThreadWorker::Stop() {
    _running.store(false, std::memory_order_relaxed);
    std::unique_lock<bthread::Mutex> lk(_mutex);
    _cond.notify_one();
}

void UserCodeThreadWorker::Join() {
    if (_worker.joinable()) {
        _worker.join();
    }
}

double UserCodeThreadPool::GetInPoolElapseInSecond(void* arg) {
    return static_cast<bvar::Adder<int64_t>*>(arg)->get_value() / 1000000.0;
}

size_t UserCodeThreadPool::GetUserCodeThreadSize(void* arg) {
    auto pool = static_cast<UserCodeThreadPool*>(arg);
    return pool->_workers.size();
}

UserCodeThreadPool::UserCodeThreadPool(const std::string& pool_name,
                                       const std::function<void()>& startfn,
                                       UserCodeThreadAssignPolicy* policy)
    : inpool_per_second("rpc_usercode_thread_pool_second", pool_name,
                        &inpool_count),
      inpool_elapse_s(GetInPoolElapseInSecond, &inpool_elapse_us),
      pool_usage("rpc_usercode_thread_pool_usage", pool_name, &inpool_elapse_s,
                 1),
      thread_count("rpc_usercode_thread_num_threads", pool_name,
                   GetUserCodeThreadSize, this),
      _pool_name(pool_name),
      _thread_startfn(startfn),
      _assign_policy(policy ? policy : DefaultUserCodeThreadAssignPolicy()),
      _next_worker_id(0) {}

UserCodeThreadPool::~UserCodeThreadPool() { StopAndJoin(); }

bool UserCodeThreadPool::Init(size_t num_threads) {
    if (num_threads <= 0) {
        LOG(ERROR) << "Wrong parameter for usercode thread pool init";
        return false;
    }
    return SetNumThreads(num_threads);
}

void UserCodeThreadPool::RunUserCode(void (*fn)(void*), void* arg) {
    auto range = _workers.size();
    auto index = _assign_policy->Index(arg, range);
    auto& worker = _workers[index % range];
    UserCodeTask usercode{fn, arg, bthread::tls_bls.assigned_data};
    worker->UserCodeRun(std::move(usercode));
}

void UserCodeThreadPool::StopAndJoin() {
    std::unique_lock<bthread::Mutex> lk(_mutex);
    for (auto& worker : _workers) {
        worker->Stop();
    }
    for (auto& worker : _workers) {
        worker->Join();
    }
}

bool UserCodeThreadPool::SetNumThreads(size_t num_threads) {
    std::unique_lock<bthread::Mutex> lk(_mutex);
    if (num_threads <= _workers.size()) {
        LOG(ERROR) << "Fail to SetNumThreads, num_threads must larger than the "
                      "current";
        return false;
    }
    for (size_t i = _workers.size(); i < num_threads; ++i) {
        auto worker = new UserCodeThreadWorker(this);
        worker->Start();
        _workers.emplace_back(worker);
    }
    return true;
}

size_t UserCodeThreadPool::NextWorkerId() {
    return _next_worker_id.fetch_add(1, butil::memory_order_relaxed);
}

size_t UserCodeThreadRandomAssignPolicy::Index(void*, size_t range) {
    return butil::fast_rand_less_than(range);
}

static std::once_flag s_default_usercode_thread_assign_policy_once;
static UserCodeThreadAssignPolicy* s_default_usercode_thread_assign_policy =
    nullptr;
UserCodeThreadAssignPolicy* DefaultUserCodeThreadAssignPolicy() {
    std::call_once(s_default_usercode_thread_assign_policy_once, []() {
        s_default_usercode_thread_assign_policy =
            new UserCodeThreadRandomAssignPolicy();
    });
    return s_default_usercode_thread_assign_policy;
}

UserCodeThreadPoolMap UserCodeThreadPool::_thread_pool_map;
std::once_flag UserCodeThreadPool::_thread_pool_map_once;

UserCodeThreadPool* UserCodeThreadPool::GetOrCreatePool(
    const UserCodeThreadPoolConf& conf) {
    std::call_once(_thread_pool_map_once, [&]() {
        if (_thread_pool_map.init(FLAGS_usercode_thread_pool_map_nbucket) !=
            0) {
            LOG(ERROR) << "Fail to init usercode thread pool map";
            exit(1);
        }
    });

    auto p = _thread_pool_map.seek(conf.pool_name);
    if (p == nullptr) {
        std::unique_ptr<UserCodeThreadPool> pool(new UserCodeThreadPool(
            conf.pool_name, conf.thread_startfn, conf.assign_policy));
        if (pool->Init(conf.num_threads) == false) {
            return nullptr;
        }
        _thread_pool_map[conf.pool_name].swap(pool);
    }
    return _thread_pool_map.seek(conf.pool_name)->get();
}

bool UserCodeThreadPool::SetPoolThreads(const std::string& pool_name,
                                        size_t num_threads) {
    auto p = _thread_pool_map.seek(pool_name);
    if (p == nullptr) {
        LOG(ERROR) << "Fail to SetPoolThreads, pool name not exist";
        return false;
    }
    auto pool = p->get();
    return pool->SetNumThreads(num_threads);
}

}  // namespace brpc
