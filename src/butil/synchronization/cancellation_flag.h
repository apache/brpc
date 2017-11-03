// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_SYNCHRONIZATION_CANCELLATION_FLAG_H_
#define BUTIL_SYNCHRONIZATION_CANCELLATION_FLAG_H_

#include "butil/base_export.h"
#include "butil/atomicops.h"
#include "butil/threading/platform_thread.h"

namespace butil {

// CancellationFlag allows one thread to cancel jobs executed on some worker
// thread. Calling Set() from one thread and IsSet() from a number of threads
// is thread-safe.
//
// This class IS NOT intended for synchronization between threads.
class BUTIL_EXPORT CancellationFlag {
 public:
  CancellationFlag() : flag_(false) {
#if !defined(NDEBUG)
    set_on_ = PlatformThread::CurrentId();
#endif
  }
  ~CancellationFlag() {}

  // Set the flag. May only be called on the thread which owns the object.
  void Set();
  bool IsSet() const;  // Returns true iff the flag was set.

 private:
  butil::subtle::Atomic32 flag_;
#if !defined(NDEBUG)
  PlatformThreadId set_on_;
#endif

  DISALLOW_COPY_AND_ASSIGN(CancellationFlag);
};

}  // namespace butil

#endif  // BUTIL_SYNCHRONIZATION_CANCELLATION_FLAG_H_
