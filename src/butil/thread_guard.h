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

// Date: Mon. Mar 27 17:17:28 CST 2022

#ifndef BUTIL_THREAD_GUARD_H
#define BUTIL_THREAD_GUARD_H

#include <memory>
#include <pthread.h>
#include "butil/atomicops.h"

namespace butil {

class ThreadGuard {
public:
    ThreadGuard();
    ~ThreadGuard();

    pthread_t& thread_id() {
        return _thread_id;
    }

    pthread_mutex_t& mutex() {
        return _mutex;
    }

    pthread_cond_t& cond() {
        return _cond;
    }

    bool IsStopped() {
        return _stop.load();
    }

    void Signal();

    void Wait(const timespec& abstimespec);

public:
    pthread_t _thread_id;
    butil::atomic<bool> _stop;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
};

void auto_thread_stop_and_join(void*);

}  // namespace butil

#endif  // BUTIL_THREAD_GUARD_H
