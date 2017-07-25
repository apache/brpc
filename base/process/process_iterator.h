// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains methods to iterate over processes on the system.

#ifndef BASE_PROCESS_PROCESS_ITERATOR_H_
#define BASE_PROCESS_PROCESS_ITERATOR_H_

#include <list>
#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/basictypes.h"
#include "base/files/file_path.h"
#include "base/process/process.h"
#include "base/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#include <tlhelp32.h>
#elif defined(OS_MACOSX) || defined(OS_OPENBSD)
#include <sys/sysctl.h>
#elif defined(OS_FREEBSD)
#include <sys/user.h>
#elif defined(OS_POSIX)
#include <dirent.h>
#endif

namespace base {

#if defined(OS_WIN)
struct ProcessEntry : public PROCESSENTRY32 {
  ProcessId pid() const { return th32ProcessID; }
  ProcessId parent_pid() const { return th32ParentProcessID; }
  const wchar_t* exe_file() const { return szExeFile; }
};

// Process access masks. These constants provide platform-independent
// definitions for the standard Windows access masks.
// See http://msdn.microsoft.com/en-us/library/ms684880(VS.85).aspx for
// the specific semantics of each mask value.
const uint32_t kProcessAccessTerminate              = PROCESS_TERMINATE;
const uint32_t kProcessAccessCreateThread           = PROCESS_CREATE_THREAD;
const uint32_t kProcessAccessSetSessionId           = PROCESS_SET_SESSIONID;
const uint32_t kProcessAccessVMOperation            = PROCESS_VM_OPERATION;
const uint32_t kProcessAccessVMRead                 = PROCESS_VM_READ;
const uint32_t kProcessAccessVMWrite                = PROCESS_VM_WRITE;
const uint32_t kProcessAccessDuplicateHandle        = PROCESS_DUP_HANDLE;
const uint32_t kProcessAccessCreateProcess          = PROCESS_CREATE_PROCESS;
const uint32_t kProcessAccessSetQuota               = PROCESS_SET_QUOTA;
const uint32_t kProcessAccessSetInformation         = PROCESS_SET_INFORMATION;
const uint32_t kProcessAccessQueryInformation       = PROCESS_QUERY_INFORMATION;
const uint32_t kProcessAccessSuspendResume          = PROCESS_SUSPEND_RESUME;
const uint32_t kProcessAccessQueryLimitedInfomation =
    PROCESS_QUERY_LIMITED_INFORMATION;
const uint32_t kProcessAccessWaitForTermination     = SYNCHRONIZE;
#elif defined(OS_POSIX)
struct BASE_EXPORT ProcessEntry {
  ProcessEntry();
  ~ProcessEntry();

  ProcessId pid() const { return pid_; }
  ProcessId parent_pid() const { return ppid_; }
  ProcessId gid() const { return gid_; }
  const char* exe_file() const { return exe_file_.c_str(); }
  const std::vector<std::string>& cmd_line_args() const {
    return cmd_line_args_;
  }

  ProcessId pid_;
  ProcessId ppid_;
  ProcessId gid_;
  std::string exe_file_;
  std::vector<std::string> cmd_line_args_;
};

// Process access masks. They are not used on Posix because access checking
// does not happen during handle creation.
const uint32_t kProcessAccessTerminate              = 0;
const uint32_t kProcessAccessCreateThread           = 0;
const uint32_t kProcessAccessSetSessionId           = 0;
const uint32_t kProcessAccessVMOperation            = 0;
const uint32_t kProcessAccessVMRead                 = 0;
const uint32_t kProcessAccessVMWrite                = 0;
const uint32_t kProcessAccessDuplicateHandle        = 0;
const uint32_t kProcessAccessCreateProcess          = 0;
const uint32_t kProcessAccessSetQuota               = 0;
const uint32_t kProcessAccessSetInformation         = 0;
const uint32_t kProcessAccessQueryInformation       = 0;
const uint32_t kProcessAccessSuspendResume          = 0;
const uint32_t kProcessAccessQueryLimitedInfomation = 0;
const uint32_t kProcessAccessWaitForTermination     = 0;
#endif  // defined(OS_POSIX)

// Used to filter processes by process ID.
class ProcessFilter {
 public:
  // Returns true to indicate set-inclusion and false otherwise.  This method
  // should not have side-effects and should be idempotent.
  virtual bool Includes(const ProcessEntry& entry) const = 0;

 protected:
  virtual ~ProcessFilter() {}
};

// This class provides a way to iterate through a list of processes on the
// current machine with a specified filter.
// To use, create an instance and then call NextProcessEntry() until it returns
// false.
class BASE_EXPORT ProcessIterator {
 public:
  typedef std::list<ProcessEntry> ProcessEntries;

  explicit ProcessIterator(const ProcessFilter* filter);
  virtual ~ProcessIterator();

  // If there's another process that matches the given executable name,
  // returns a const pointer to the corresponding PROCESSENTRY32.
  // If there are no more matching processes, returns NULL.
  // The returned pointer will remain valid until NextProcessEntry()
  // is called again or this NamedProcessIterator goes out of scope.
  const ProcessEntry* NextProcessEntry();

  // Takes a snapshot of all the ProcessEntry found.
  ProcessEntries Snapshot();

 protected:
  virtual bool IncludeEntry();
  const ProcessEntry& entry() { return entry_; }

 private:
  // Determines whether there's another process (regardless of executable)
  // left in the list of all processes.  Returns true and sets entry_ to
  // that process's info if there is one, false otherwise.
  bool CheckForNextProcess();

  // Initializes a PROCESSENTRY32 data structure so that it's ready for
  // use with Process32First/Process32Next.
  void InitProcessEntry(ProcessEntry* entry);

#if defined(OS_WIN)
  HANDLE snapshot_;
  bool started_iteration_;
#elif defined(OS_MACOSX) || defined(OS_BSD)
  std::vector<kinfo_proc> kinfo_procs_;
  size_t index_of_kinfo_proc_;
#elif defined(OS_POSIX)
  DIR* procfs_dir_;
#endif
  ProcessEntry entry_;
  const ProcessFilter* filter_;

  DISALLOW_COPY_AND_ASSIGN(ProcessIterator);
};

// This class provides a way to iterate through the list of processes
// on the current machine that were started from the given executable
// name.  To use, create an instance and then call NextProcessEntry()
// until it returns false.
class BASE_EXPORT NamedProcessIterator : public ProcessIterator {
 public:
  NamedProcessIterator(const FilePath::StringType& executable_name,
                       const ProcessFilter* filter);
  virtual ~NamedProcessIterator();

 protected:
  virtual bool IncludeEntry() OVERRIDE;

 private:
  FilePath::StringType executable_name_;

  DISALLOW_COPY_AND_ASSIGN(NamedProcessIterator);
};

// Returns the number of processes on the machine that are running from the
// given executable name.  If filter is non-null, then only processes selected
// by the filter will be counted.
BASE_EXPORT int GetProcessCount(const FilePath::StringType& executable_name,
                                const ProcessFilter* filter);

}  // namespace base

#endif  // BASE_PROCESS_PROCESS_ITERATOR_H_
