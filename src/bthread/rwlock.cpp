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

// Date: Tue August 10 23:50:50 CST 2024

#include <dlfcn.h>  // dlsym
#include <execinfo.h>
#include <fcntl.h>  // O_RDONLY
#include <pthread.h>

#include "bthread/bthread.h"
#include "bthread/butex.h"  // butex_*
#include "bthread/log.h"
#include "bthread/processor.h"  // cpu_relax, barrier
#include "bthread/sys_futex.h"
#include "butil/atomicops.h"
#include "butil/containers/flat_map.h"
#include "butil/fd_guard.h"
#include "butil/file_util.h"
#include "butil/files/file.h"
#include "butil/files/file_path.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/macros.h"  // BAIDU_CASSERT
#include "butil/object_pool.h"
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "butil/unique_ptr.h"
#include "bvar/bvar.h"
#include "bvar/collector.h"

namespace bthread {

inline int rwlock_unrlock(bthread_rwlock_t* rwlock) {
  butil::atomic<unsigned>* whole = (butil::atomic<unsigned>*)rwlock->lock_flag;

  while (true) {
    unsigned r = whole->load();
    if (r == 0 || (r >> 31) != 0) {
      LOG(ERROR) << "wrong unrlock!";
      return 0;
    }
    if (!(whole->compare_exchange_weak(r, r - 1))) {
      continue;
    }
    // wake up write waiter
    bthread::butex_wake(whole);
    return 0;
  }
}

inline int rwlock_unwlock(bthread_rwlock_t* rwlock) {
  butil::atomic<unsigned>* whole = (butil::atomic<unsigned>*)rwlock->lock_flag;

  while (true) {
    unsigned r = whole->load();
    if (r != (unsigned)(1 << 31)) {
      LOG(ERROR) << "wrong unwlock!";
      return 0;
    }
    if (!whole->compare_exchange_weak(r, 0)) {
      continue;
    }
    // wake up write waiter first
    bthread::butex_wake(whole);
    butil::atomic<unsigned>* w_wait_count = (butil::atomic<unsigned>*)rwlock->w_wait_count;
    // try reduce wait_count for read waiters,and wake up read waiters
    w_wait_count->fetch_sub(1);
    bthread::butex_wake_all(w_wait_count);
    return 0;
  }
}

inline int rwlock_unlock(bthread_rwlock_t* rwlock) {
  butil::atomic<unsigned>* whole = (butil::atomic<unsigned>*)rwlock->lock_flag;
  if ((whole->load(butil::memory_order_relaxed) >> 31) != 0) {
    return rwlock_unwlock(rwlock);
  } else {
    return rwlock_unrlock(rwlock);
  }
}

inline int rwlock_rlock(bthread_rwlock_t* rwlock) {
  butil::atomic<unsigned>* whole = (butil::atomic<unsigned>*)rwlock->lock_flag;

  butil::atomic<unsigned>* w_wait_count = (butil::atomic<unsigned>*)rwlock->w_wait_count;
  while (true) {
    unsigned w = w_wait_count->load();
    if (w > 0) {
      if (bthread::butex_wait(w_wait_count, w, NULL) < 0 && errno != EWOULDBLOCK && errno != EINTR) {
        return errno;
      }
      continue;
    }
    // FIXME!! we don't consider read_wait_count overflow yet,2^31 should be enough here
    unsigned r = whole->load();
    if ((r >> 31) == 0) {
      if (whole->compare_exchange_weak(r, r + 1)) {
        return 0;
      }
    }
  }
}

inline int rwlock_wlock(bthread_rwlock_t* rwlock) {
  butil::atomic<unsigned>* w_wait_count = (butil::atomic<unsigned>*)rwlock->w_wait_count;
  butil::atomic<unsigned>* whole = (butil::atomic<unsigned>*)rwlock->lock_flag;
  // we don't consider w_wait_count overflow yet,2^32 should be enough here
  w_wait_count->fetch_add(1);
  while (true) {
    unsigned r = whole->load();
    if (r != 0) {
      if (bthread::butex_wait(whole, r, NULL) < 0 && errno != EWOULDBLOCK && errno != EINTR) {
        whole->fetch_sub(1);
        return errno;
      }
      continue;
    }
    if (whole->compare_exchange_weak(r, (unsigned)(1 << 31))) {
      return 0;
    }
  }
}

}  // namespace bthread

extern "C" {

int bthread_rwlock_init(bthread_rwlock_t* __restrict rwlock, const bthread_rwlockattr_t* __restrict attr) {
  rwlock->w_wait_count = bthread::butex_create_checked<unsigned>();
  rwlock->lock_flag = bthread::butex_create_checked<unsigned>();
  if (!rwlock->w_wait_count || !rwlock->lock_flag) {
    LOG(ERROR) << "no memory";
    return ENOMEM;
  }
  *rwlock->w_wait_count = 0;
  *rwlock->lock_flag = 0;
  return 0;
}

int bthread_rwlock_destroy(bthread_rwlock_t* rwlock) {
  bthread::butex_destroy(rwlock->w_wait_count);
  bthread::butex_destroy(rwlock->lock_flag);
  return 0;
}

int bthread_rwlock_rdlock(bthread_rwlock_t* rwlock) { return bthread::rwlock_rlock(rwlock); }

int bthread_rwlock_wrlock(bthread_rwlock_t* rwlock) { return bthread::rwlock_wlock(rwlock); }

int bthread_rwlock_unrlock(bthread_rwlock_t* rwlock) { return bthread::rwlock_unrlock(rwlock); }

int bthread_rwlock_unwlock(bthread_rwlock_t* rwlock) { return bthread::rwlock_unwlock(rwlock); }

int bthread_rwlock_unlock(bthread_rwlock_t* rwlock) { return bthread::rwlock_unlock(rwlock); }

}