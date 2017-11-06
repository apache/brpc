// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_MEMORY_SCOPED_OPEN_PROCESS_H_
#define BUTIL_MEMORY_SCOPED_OPEN_PROCESS_H_

#include "butil/process/process_handle.h"

namespace butil {

// A class that opens a process from its process id and closes it when the
// instance goes out of scope.
class ScopedOpenProcess {
 public:
  ScopedOpenProcess() : handle_(kNullProcessHandle) {
  }

  // Automatically close the process.
  ~ScopedOpenProcess() {
    Close();
  }

  // Open a new process by pid. Closes any previously opened process (even if
  // opening the new one fails).
  bool Open(ProcessId pid) {
    Close();
    return OpenProcessHandle(pid, &handle_);
  }

  // Close the previously opened process.
  void Close() {
    if (handle_ == kNullProcessHandle)
      return;

    CloseProcessHandle(handle_);
    handle_ = kNullProcessHandle;
  }

  ProcessHandle handle() const { return handle_; }

 private:
  ProcessHandle handle_;
  DISALLOW_COPY_AND_ASSIGN(ScopedOpenProcess);
};
}  // namespace butil

#endif  // BUTIL_MEMORY_SCOPED_OPEN_PROCESS_H_
