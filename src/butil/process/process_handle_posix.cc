// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/process/process_handle.h"

#include <unistd.h>

namespace butil {

ProcessId GetCurrentProcId() {
  return getpid();
}

ProcessHandle GetCurrentProcessHandle() {
  return GetCurrentProcId();
}

bool OpenProcessHandle(ProcessId pid, ProcessHandle* handle) {
  // On Posix platforms, process handles are the same as PIDs, so we
  // don't need to do anything.
  *handle = pid;
  return true;
}

bool OpenPrivilegedProcessHandle(ProcessId pid, ProcessHandle* handle) {
  // On POSIX permissions are checked for each operation on process,
  // not when opening a "handle".
  return OpenProcessHandle(pid, handle);
}

bool OpenProcessHandleWithAccess(ProcessId pid,
                                 uint32_t access_flags,
                                 ProcessHandle* handle) {
  // On POSIX permissions are checked for each operation on process,
  // not when opening a "handle".
  return OpenProcessHandle(pid, handle);
}

void CloseProcessHandle(ProcessHandle process) {
  // See OpenProcessHandle, nothing to do.
  return;
}

ProcessId GetProcId(ProcessHandle process) {
  return process;
}

}  // namespace butil
