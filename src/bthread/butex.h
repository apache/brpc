// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu, Inc.
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

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 22 17:30:12 CST 2014

#ifndef BTHREAD_BUTEX_H
#define BTHREAD_BUTEX_H

#include <errno.h>                               // users need to check errno
#include <time.h>                                // timespec
#include "butil/macros.h"                         // BAIDU_CASSERT
#include "bthread/types.h"                       // bthread_t
#include "butil/atomicops.h"                // butil::atomic 

namespace bthread {

// Create a butex which is a futex-like 32-bit primitive for synchronizing
// bthreads/pthreads.
// Returns a pointer to 32-bit data, NULL on failure.
// NOTE: all butexes are private(not inter-process).
void* butex_create();

// Check width of user type before casting.
template <typename T> T* butex_create_checked() {
    BAIDU_CASSERT(sizeof(T) == sizeof(int), sizeof_T_must_equal_int);
    return static_cast<T*>(butex_create());
}

// Destroy the butex.
void butex_destroy(void* butex);

// Wake up at most 1 thread waiting on |butex|.
// Returns # of threads woken up.
int butex_wake(void* butex);

// Wake up all threads waiting on |butex|.
// Returns # of threads woken up.
int butex_wake_all(void* butex);

// Wake up all threads waiting on |butex| except a bthread whose identifier
// is |excluded_bthread|. This function does not yield.
// Returns # of threads woken up.
int butex_wake_except(void* butex, bthread_t excluded_bthread);

// Wake up at most 1 thread waiting on |butex1|, let all other threads wait
// on |butex2| instead.
// Returns # of threads woken up.
int butex_requeue(void* butex1, void* butex2);

// Atomically wait on |butex| if *butex equals |expected_value|, until the
// butex is woken up by butex_wake*, or CLOCK_REALTIME reached |abstime| if
// abstime is not NULL.
// About |abstime|:
//   Different from FUTEX_WAIT, butex_wait uses absolute time.
// Returns 0 on success, -1 otherwise and errno is set.
int butex_wait(void* butex, int expected_value, const timespec* abstime);

//add by haiert 2018.11.6
//Atomically wait on |butex| ,and take butex->value as QueuedMutexInternal
//if get QueuedMutex as owner before add wait_queue will return,else can only wake up by queued_butex_wake
//not support abstime yet,and disable interrupted by ignore_interrupted in task_meta 
//return 0 when be wake up by queued_butex_wake, when 1 as onwer with no queued_butex_wake 
int queued_butex_wait(void* butex, const timespec* abstime);

// Wake up at most 1 thread waiting on |butex|.
// Returns # of threads woken up.
// if return 1,set wake_bid
// when return 0,QueuedMutexInternal::locked will unlock 
int queued_butex_wake(void* arg, butil::static_atomic<uint64_t> &wake_bid);

}  // namespace bthread

#endif  // BTHREAD_BUTEX_H
