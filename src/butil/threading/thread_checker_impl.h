// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_THREADING_THREAD_CHECKER_IMPL_H_
#define BUTIL_THREADING_THREAD_CHECKER_IMPL_H_

#include "butil/base_export.h"
#include "butil/synchronization/lock.h"
#include "butil/threading/platform_thread.h"

namespace butil {

// Real implementation of ThreadChecker, for use in debug mode, or
// for temporary use in release mode (e.g. to CHECK on a threading issue
// seen only in the wild).
//
// Note: You should almost always use the ThreadChecker class to get the
// right version for your build configuration.
class BUTIL_EXPORT ThreadCheckerImpl {
 public:
  ThreadCheckerImpl();
  ~ThreadCheckerImpl();

  bool CalledOnValidThread() const;

  // Changes the thread that is checked for in CalledOnValidThread.  This may
  // be useful when an object may be created on one thread and then used
  // exclusively on another thread.
  void DetachFromThread();

 private:
  void EnsureThreadIdAssigned() const;

  mutable butil::Lock lock_;
  // This is mutable so that CalledOnValidThread can set it.
  // It's guarded by |lock_|.
  mutable PlatformThreadRef valid_thread_id_;
};

}  // namespace butil

#endif  // BUTIL_THREADING_THREAD_CHECKER_IMPL_H_
