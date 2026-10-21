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

// Date: Wed Mar 14 17:44:58 CST 2018

#include "bthread/sys_futex.h"
#include "butil/scoped_lock.h"
#include "butil/atomicops.h"
#include "butil/containers/linked_list.h"
#include <pthread.h>
#include <unordered_map>

#if defined(OS_MACOSX)

namespace bthread {

struct SimuFutexWaiter : public butil::LinkNode<SimuFutexWaiter> {
    SimuFutexWaiter() : signaled(false) {
        pthread_cond_init(&cond, nullptr);
    }
    ~SimuFutexWaiter() {
        // The futex mutex is still held when the stack waiter is destroyed.
        // A notified waiter is already detached; otherwise remove it on timeout
        // or error.
        RemoveFromList();
        pthread_cond_destroy(&cond);
    }

    pthread_cond_t cond;
    bool signaled;
};

struct SimuFutex {
    SimuFutex() : ref(0) {
        pthread_mutex_init(&lock, nullptr);
    }
    ~SimuFutex() {
        pthread_mutex_destroy(&lock);
    }

    pthread_mutex_t lock;
    // Only waiters that have not been notified are in this queue.
    butil::LinkedList<SimuFutexWaiter> waiters;
    int32_t ref;
};

static pthread_mutex_t s_futex_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_futex_map_once = PTHREAD_ONCE_INIT;
static std::unordered_map<void*, SimuFutex>* s_futex_map = nullptr;
static void InitFutexMap() {
    // Leave memory to process's clean up.
    s_futex_map = new std::unordered_map<void*, SimuFutex>();
}

int futex_wait_private(void* addr1, int expected, const timespec* timeout) {
    if (pthread_once(&init_futex_map_once, InitFutexMap) != 0) {
        LOG(FATAL) << "Fail to pthread_once";
        exit(1);
    }
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    SimuFutex& simu_futex = (*s_futex_map)[addr1];
    ++simu_futex.ref;
    mu.unlock();

    int rc = 0;
    {
        std::unique_lock<pthread_mutex_t> mu1(simu_futex.lock);
        if (static_cast<butil::atomic<int>*>(addr1)->load() == expected) {
            SimuFutexWaiter waiter;
            simu_futex.waiters.Append(&waiter);
            timespec timeout_abs = {};
            if (timeout) {
                timeout_abs = butil::timespec_from_now(*timeout);
            }
            do {
                if (timeout) {
                    rc = pthread_cond_timedwait(
                        &waiter.cond, &simu_futex.lock, &timeout_abs);
                } else {
                    rc = pthread_cond_wait(&waiter.cond, &simu_futex.lock);
                }
            } while (rc == 0 && !waiter.signaled);
            // A wake may win the mutex while a timed-out waiter is reacquiring
            // it. Honor that notification so it cannot be counted and then lost.
            if (waiter.signaled) {
                rc = 0;
            } else if (rc != 0) {
                errno = rc;
                rc = -1;
            }
        } else {
            errno = EAGAIN;
            rc = -1;
        }
    }

    std::unique_lock<pthread_mutex_t> mu1(s_futex_map_mutex);
    if (--simu_futex.ref == 0) {
        s_futex_map->erase(addr1);
    }
    mu1.unlock();
    return rc;
}

int futex_wake_private(void* addr1, int nwake) {
    if (pthread_once(&init_futex_map_once, InitFutexMap) != 0) {
        LOG(FATAL) << "Fail to pthread_once";
        exit(1);
    }
    std::unique_lock<pthread_mutex_t> mu(s_futex_map_mutex);
    auto it = s_futex_map->find(addr1);
    if (it == s_futex_map->end()) {
        mu.unlock();
        return 0;
    }
    SimuFutex& simu_futex = it->second;
    ++simu_futex.ref;
    mu.unlock();

    int nwakedup = 0;
    int rc = 0;
    {
        std::unique_lock<pthread_mutex_t> mu1(simu_futex.lock);
        while (nwakedup < nwake && !simu_futex.waiters.empty()) {
            SimuFutexWaiter* waiter = simu_futex.waiters.head()->value();
            if ((rc = pthread_cond_signal(&waiter->cond)) != 0) {
                errno = rc;
                break;
            }
            // Commit the notification to this waiter before releasing the
            // mutex. Later wakes cannot count it again while it is rescheduled,
            // and another waiter's timeout cannot consume its notification.
            waiter->signaled = true;
            waiter->RemoveFromList();
            ++nwakedup;
        }
    }

    std::unique_lock<pthread_mutex_t> mu2(s_futex_map_mutex);
    if (--simu_futex.ref == 0) {
        s_futex_map->erase(addr1);
    }
    mu2.unlock();
    return nwakedup;
}

} // namespace bthread

#endif
