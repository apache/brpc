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
#include "butil/atomicops.h"
#include <map>
#include <pthread.h>

#if defined(OS_MACOSX)

namespace bthread {

class SimuFutex {
public:
    SimuFutex() :
        counts(0) {
        pthread_mutex_init(&lock, NULL);
        pthread_cond_init(&cond, NULL);
    }
    ~SimuFutex() {
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }

public:
    pthread_mutex_t lock;
    pthread_cond_t cond;
    butil::atomic<int32_t> counts;
};

// TODO: use a more efficient way. Current impl doesn't delete SimuFutex at all.
static std::map<void*, SimuFutex> s_futex_map;
static pthread_mutex_t s_futex_map_mutex = PTHREAD_MUTEX_INITIALIZER;

int futex_wait_private(void* addr1, int expected, const timespec* timeout) {
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    SimuFutex& simu_futex = s_futex_map[addr1];
    mu.unlock();

    std::unique_lock<pthread_mutex_t> mu1(simu_futex.lock);
    if (static_cast<butil::atomic<int>*>(addr1)->load() == expected) {
        int rc = 0;
        ++simu_futex.counts;
        if (timeout) {
            timespec timeout_abs = butil::timespec_from_now(*timeout);
            if ((rc = pthread_cond_timedwait(&simu_futex.cond, &simu_futex.lock, &timeout_abs)) != 0) {
                errno = rc;
                return -1;
            }
        } else {
            if ((rc = pthread_cond_wait(&simu_futex.cond, &simu_futex.lock)) != 0) {
                errno = rc;
                return -1;
            }
        }
        --simu_futex.counts;
    }
    return 0;
}

int futex_wake_private(void* addr1, int nwake) {
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    SimuFutex& simu_futex = s_futex_map[addr1];
    mu.unlock();

    std::unique_lock<pthread_mutex_t> mu1(simu_futex.lock);
    nwake = (nwake < simu_futex.counts)? nwake: simu_futex.counts.load();
    int nwakedup = 0;
    int rc = 0;
    for (int i = 0; i < nwake; ++i) {
        if ((rc = pthread_cond_signal(&simu_futex.cond)) != 0) {
            errno = rc;
            return -1;
        }
        ++nwakedup;
    }
    return nwakedup;
}

int futex_requeue_private(void* addr1, int nwake, void* addr2) {
    // TODO
    return -1;
}

} // namespace bthread

#endif
