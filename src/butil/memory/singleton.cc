// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
#include "butil/memory/singleton.h"
#include "butil/threading/platform_thread.h"
__BEGIN_DECLS
// Defined in bthread/bthread.cpp
int BAIDU_WEAK bthread_usleep(uint64_t microseconds);
__END_DECLS

namespace butil {

DEFINE_uint32(max_sched_yield_count_on_bthread, 1000, "Max count of sched_yield on bthread. "
                                                     "If count of sched_yield is greater than max count, "
                                                     "use bthread_yield instead.");

namespace internal {

subtle::AtomicWord WaitForInstance(subtle::AtomicWord* instance) {
  // Handle the race. Another thread beat us and either:
  // - Has the object in BeingCreated state
  // - Already has the object created...
  // We know value != NULL.  It could be kBeingCreatedMarker, or a valid ptr.
  // Unless your constructor can be very time consuming, it is very unlikely
  // to hit this race.  When it does, we just spin and yield the thread until
  // the object has been created.
  subtle::AtomicWord value;
  uint32_t count = 0;
  while (true) {
    // The load has acquire memory ordering as the thread which reads the
    // instance pointer must acquire visibility over the associated data.
    // The pairing Release_Store operation is in Singleton::get().
    value = subtle::Acquire_Load(instance);
    if (value != kBeingCreatedMarker)
      break;

    if (bthread_usleep && count >= FLAGS_max_sched_yield_count_on_bthread) {
        bthread_usleep(1000);
    } else {
      PlatformThread::YieldCurrentThread();
    }
    ++count;
  }
  return value;
}

}  // namespace internal
}  // namespace butil
