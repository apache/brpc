// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>

#include "base/logging.h"
#include "base/process/kill.h"

namespace base {

// static
Process Process::Current() {
  return Process(GetCurrentProcessHandle());
}

ProcessId Process::pid() const {
  if (process_ == 0)
    return 0;

  return GetProcId(process_);
}

bool Process::is_current() const {
  return process_ == GetCurrentProcessHandle();
}

void Process::Close() {
  process_ = 0;
  // if the process wasn't terminated (so we waited) or the state
  // wasn't already collected w/ a wait from process_utils, we're gonna
  // end up w/ a zombie when it does finally exit.
}

void Process::Terminate(int result_code) {
  // result_code isn't supportable.
  if (!process_)
    return;
  // We don't wait here. It's the responsibility of other code to reap the
  // child.
  KillProcess(process_, result_code, false);
}

#if !defined(OS_LINUX)
bool Process::IsProcessBackgrounded() const {
  // See SetProcessBackgrounded().
  return false;
}

bool Process::SetProcessBackgrounded(bool value) {
  // POSIX only allows lowering the priority of a process, so if we
  // were to lower it we wouldn't be able to raise it back to its initial
  // priority.
  return false;
}

// static
bool Process::CanBackgroundProcesses() {
  return false;
}

#endif

int Process::GetPriority() const {
  DCHECK(process_);
  return getpriority(PRIO_PROCESS, process_);
}

}  // namspace base
