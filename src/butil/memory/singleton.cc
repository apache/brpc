// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/memory/singleton.h"
#include "butil/threading/platform_thread.h"

__BEGIN_DECLS
// Defined in bthread/bthread.cpp
void BAIDU_WEAK bthread_yield();
__END_DECLS

namespace butil {
namespace internal {

void yield() {
    if (bthread_yield) {
        // Use `bthread_yield' to avoid blocking worker threads of bthread.
        bthread_yield();
    } else {
        PlatformThread::YieldCurrentThread();
    }
}

subtle::AtomicWord WaitForInstance(subtle::AtomicWord* instance) {
  // Handle the race. Another thread beat us and either:
  // - Has the object in BeingCreated state
  // - Already has the object created...
  // We know value != NULL.  It could be kBeingCreatedMarker, or a valid ptr.
  // Unless your constructor can be very time consuming, it is very unlikely
  // to hit this race.  When it does, we just spin and yield the thread until
  // the object has been created.
  subtle::AtomicWord value;
  while (true) {
    // The load has acquire memory ordering as the thread which reads the
    // instance pointer must acquire visibility over the associated data.
    // The pairing Release_Store operation is in Singleton::get().
    value = subtle::Acquire_Load(instance);
    if (value != kBeingCreatedMarker)
      break;
    yield();
  }
  return value;
}

}  // namespace internal
}  // namespace butil
