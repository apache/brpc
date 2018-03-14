// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Zhu,Jiashun (zhujiahun@baidu.com)
// Date: Wed Mar 14 17:44:58 CST 2018

#include "bthread/sys_futex.h"
#include "butil/scoped_lock.h"
#include <map>
#include <pthread.h>

#if defined(OS_MACOSX)

namespace bthread {

struct SimuFutex {
    pthread_mutex_t lock;
    pthread_cond_t cond;

    SimuFutex() {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&cond, NULL);
    }
    ~SimuFutex() {
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }
};

// TODO: use a more efficient way. Current impl doesn't delete SimuFutex at all.
static std::map<void*, SimuFutex> s_futex_map;
static pthread_mutex_t s_futex_map_mutex = PTHREAD_MUTEX_INITIALIZER;

int futex_wait_private(void* addr1, int expected, const timespec* timeout) {
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    SimuFutex& simu_futex = s_futex_map[addr1];
    mu.unlock();
    int rc = pthread_mutex_lock(&simu_futex.lock);
    if (rc < 0) {
        return rc;
    }
    if (static_cast<butil::atomic<int>*>(addr1)->load() == expected) {
        pthread_cond_wait(&simu_futex.cond, &simu_futex.lock);
    }
    rc = pthread_mutex_unlock(&simu_futex.lock);
    if (rc < 0) {
        return rc;
    }
    return 0;
}

int futex_wake_private(void* addr1, int nwake) {
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    SimuFutex& simu_futex = s_futex_map[addr1];
    mu.unlock();
    int rc = pthread_mutex_lock(&simu_futex.lock);
    if (rc < 0) {
        return rc;
    }
    for (int i = 0; i < nwake; ++i) {
        rc = pthread_cond_signal(&simu_futex.cond);
        if (rc < 0) {
            return rc;
        }
    }
    rc = pthread_mutex_unlock(&simu_futex.lock);
    if (rc < 0) {
        return rc;
    }
    return 0;
}

int futex_requeue_private(void* addr1, int nwake, void* addr2) {
    // TODO
    return -1;
}

} // namespace bthread

#endif
