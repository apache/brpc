// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/process/process_info.h"

#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "butil/basictypes.h"
#include "butil/memory/scoped_ptr.h"
#include "butil/time/time.h"

namespace butil {

//static
const Time CurrentProcessInfo::CreationTime() {
  int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
  size_t len = 0;
  if (sysctl(mib, arraysize(mib), NULL, &len, NULL, 0) < 0)
    return Time();

  scoped_ptr<struct kinfo_proc, butil::FreeDeleter>
      proc(static_cast<struct kinfo_proc*>(malloc(len)));
  if (sysctl(mib, arraysize(mib), proc.get(), &len, NULL, 0) < 0)
    return Time();
  return Time::FromTimeVal(proc->kp_proc.p_un.__p_starttime);
}

}  // namespace butil
