// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/threading/platform_thread.h"

#include <errno.h>
#include <sched.h>

#include "butil/lazy_instance.h"
#include "butil/logging.h"
#include "butil/memory/scoped_ptr.h"
#include "butil/safe_strerror_posix.h"
#include "butil/threading/thread_id_name_manager.h"
#include "butil/threading/thread_restrictions.h"

#if !defined(OS_NACL)
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace butil {

namespace {

int ThreadNiceValue(ThreadPriority priority) {
  switch (priority) {
    case kThreadPriority_RealtimeAudio:
      return -10;
    case kThreadPriority_Background:
      return 10;
    case kThreadPriority_Normal:
      return 0;
    case kThreadPriority_Display:
      return -6;
    default:
      NOTREACHED() << "Unknown priority.";
      return 0;
  }
}

}  // namespace

// NOTE(gejun): PR_SET_NAME was added in 2.6.9, should be working on most of
// our machines, but missing from our linux headers.
#if !defined(PR_SET_NAME)
#define PR_SET_NAME 15
#endif

// static
void PlatformThread::SetName(const char* name) {
  ThreadIdNameManager::GetInstance()->SetName(CurrentId(), name);

#if !defined(OS_NACL)
  // On linux we can get the thread names to show up in the debugger by setting
  // the process name for the LWP.  We don't want to do this for the main
  // thread because that would rename the process, causing tools like killall
  // to stop working.
  if (PlatformThread::CurrentId() == getpid())
    return;

  // http://0pointer.de/blog/projects/name-your-threads.html
  // Set the name for the LWP (which gets truncated to 15 characters).
  // Note that glibc also has a 'pthread_setname_np' api, but it may not be
  // available everywhere and it's only benefit over using prctl directly is
  // that it can set the name of threads other than the current thread.
  int err = prctl(PR_SET_NAME, name);
  // We expect EPERM failures in sandboxed processes, just ignore those.
  if (err < 0 && errno != EPERM)
    DPLOG(ERROR) << "prctl(PR_SET_NAME)";
#endif  //  !defined(OS_NACL)
}

// static
void PlatformThread::SetThreadPriority(PlatformThreadHandle handle,
                                       ThreadPriority priority) {
#if !defined(OS_NACL)
  if (priority == kThreadPriority_RealtimeAudio) {
    const struct sched_param kRealTimePrio = { 8 };
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &kRealTimePrio) == 0) {
      // Got real time priority, no need to set nice level.
      return;
    }
  }

  // setpriority(2) will set a thread's priority if it is passed a tid as
  // the 'process identifier', not affecting the rest of the threads in the
  // process. Setting this priority will only succeed if the user has been
  // granted permission to adjust nice values on the system.
  DCHECK_NE(handle.id_, kInvalidThreadId);
  const int kNiceSetting = ThreadNiceValue(priority);
  if (setpriority(PRIO_PROCESS, handle.id_, kNiceSetting)) {
    DVPLOG(1) << "Failed to set nice value of thread ("
              << handle.id_ << ") to " << kNiceSetting;
  }
#endif  //  !defined(OS_NACL)
}

void InitThreading() {}

void InitOnThread() {}

void TerminateOnThread() {}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
#if !defined(THREAD_SANITIZER) && !defined(MEMORY_SANITIZER)
  return 0;
#else
  // ThreadSanitizer bloats the stack heavily. Evidence has been that the
  // default stack size isn't enough for some browser tests.
  // MemorySanitizer needs this as a temporary fix for http://crbug.com/353687
  return 2 * (1 << 23);  // 2 times 8192K (the default stack size on Linux).
#endif
}

}  // namespace butil
