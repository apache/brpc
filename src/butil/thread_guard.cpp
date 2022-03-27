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

#include "butil/scoped_lock.h"
#include "butil/thread_guard.h"

namespace butil {

ThreadGuard::ThreadGuard() {
    stop.store(false);
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
}

ThreadGuard::~ThreadGuard() {
    if (thread_id != 0) {
        stop.store(true);
        Signal();
        pthread_join(thread_id, NULL);
    }
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}

void ThreadGuard::Signal() {
    BAIDU_SCOPED_LOCK(mutex);
    pthread_cond_signal(&cond);
}

void ThreadGuard::Wait(const timespec& abstimespec) {
    BAIDU_SCOPED_LOCK(mutex);
    pthread_cond_timedwait(&cond, &mutex, &abstimespec);
}

void auto_thread_stop_and_join(void* arg) {
    if (!arg) {
        return;
    }

    ThreadGuard* thread = static_cast<ThreadGuard*>(arg);
    delete thread;
    thread = NULL;
}

}  // namespace butil