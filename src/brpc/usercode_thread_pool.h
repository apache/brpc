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

#ifndef BRPC_USERCODE_THREAD_POOL_H
#define BRPC_USERCODE_THREAD_POOL_H

#include <deque>
#include <mutex>
#include <functional>
#include <thread>
#include <condition_variable>
#include <gflags/gflags_declare.h>
#include "butil/atomicops.h"
#include "bvar/bvar.h"
#include "bthread/task_meta.h"
#include "bthread/mutex.h"
#include "bthread/condition_variable.h"
#include "butil/containers/case_ignored_flat_map.h"  // [CaseIgnored]FlatMap

namespace brpc {
// Store pending user code.
struct UserCodeTask {
    void (*fn)(void*);
    void* arg;
    void* assigned_data;
};

class UserCodeThreadAssignPolicy {
public:
    UserCodeThreadAssignPolicy() {}
    virtual ~UserCodeThreadAssignPolicy() {}
    virtual size_t Index(void* arg, size_t range) = 0;

private:
    DISALLOW_COPY_AND_ASSIGN(UserCodeThreadAssignPolicy);
};

class UserCodeThreadRandomAssignPolicy : public UserCodeThreadAssignPolicy {
public:
    UserCodeThreadRandomAssignPolicy() {}
    virtual ~UserCodeThreadRandomAssignPolicy() {}
    size_t Index(void* arg, size_t range) override;

private:
    DISALLOW_COPY_AND_ASSIGN(UserCodeThreadRandomAssignPolicy);
};

UserCodeThreadAssignPolicy* DefaultUserCodeThreadAssignPolicy();

class UserCodeThreadPool;
class UserCodeThreadWorker {
public:
    UserCodeThreadWorker(UserCodeThreadPool* pool);
    void UserCodeRun(UserCodeTask&& usercode);
    void UserCodeLoop();
    void Start();
    void Stop();
    void Join();

private:
    UserCodeThreadPool* _pool;
    std::deque<UserCodeTask> _queue;
    bthread::Mutex _mutex;
    bthread::ConditionVariable _cond;
    std::thread _worker;
    std::atomic<bool> _running;  // running flag
};

// "user code thread pool" configuration
struct UserCodeThreadPoolConf {
    UserCodeThreadPoolConf(const std::string& pool_name, size_t num_threads,
                           const std::function<void()>& startfn,
                           UserCodeThreadAssignPolicy* policy)
        : pool_name(pool_name),
          num_threads(num_threads),
          thread_startfn(startfn),
          assign_policy(policy) {}
    std::string pool_name;                      // pool name
    size_t num_threads;                         // thread number
    std::function<void()> thread_startfn;       // thread start function
    UserCodeThreadAssignPolicy* assign_policy;  // thread assign policy
};

using UserCodeThreadPoolMap =
    butil::FlatMap<std::string, std::unique_ptr<UserCodeThreadPool>>;

// "user code thread pool" is a set of pthreads to allow run user code in this
// pool for some methods
class UserCodeThreadPool {
public:
    bvar::Adder<size_t> inpool_count;
    bvar::PerSecond<bvar::Adder<size_t>> inpool_per_second;
    bvar::Adder<int64_t> inpool_elapse_us;
    bvar::PassiveStatus<double> inpool_elapse_s;
    bvar::PerSecond<bvar::PassiveStatus<double>> pool_usage;
    bvar::PassiveStatus<size_t> thread_count;

    UserCodeThreadPool(const std::string& pool_name,
                       const std::function<void()>& startfn,
                       UserCodeThreadAssignPolicy* policy);
    ~UserCodeThreadPool();
    bool Init(size_t num_threads);
    void RunUserCode(void (*fn)(void*), void* arg);
    bool SetNumThreads(size_t);
    size_t NextWorkerId();
    const std::string& pool_name() const { return _pool_name; }
    std::function<void()> thread_startfn() const { return _thread_startfn; }

    static UserCodeThreadPool* GetOrCreatePool(
        const UserCodeThreadPoolConf& conf);
    static bool SetPoolThreads(const std::string& pool_name,
                               size_t num_threads);

private:
    static double GetInPoolElapseInSecond(void*);
    static size_t GetUserCodeThreadSize(void*);
    void StopAndJoin();

    std::string _pool_name;                      // thread pool name
    std::function<void()> _thread_startfn;       // thread start function
    UserCodeThreadAssignPolicy* _assign_policy;  // thread assign policy
    std::vector<std::unique_ptr<UserCodeThreadWorker>> _workers;  // workers
    bthread::Mutex _mutex;                // worker vector mutex
    std::atomic<size_t> _next_worker_id;  // worker id

    static UserCodeThreadPoolMap _thread_pool_map;
    static std::once_flag _thread_pool_map_once;
};

}  // namespace brpc

#endif  // BRPC_USERCODE_THREAD_POOL_H
