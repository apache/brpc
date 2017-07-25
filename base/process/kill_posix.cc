// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/kill.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/file_util.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/process_iterator.h"
#include "base/synchronization/waitable_event.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/threading/platform_thread.h"

namespace base {

namespace {

bool WaitpidWithTimeout(ProcessHandle handle,
                        int* status,
                        base::TimeDelta wait) {
  // This POSIX version of this function only guarantees that we wait no less
  // than |wait| for the process to exit.  The child process may
  // exit sometime before the timeout has ended but we may still block for up
  // to 256 milliseconds after the fact.
  //
  // waitpid() has no direct support on POSIX for specifying a timeout, you can
  // either ask it to block indefinitely or return immediately (WNOHANG).
  // When a child process terminates a SIGCHLD signal is sent to the parent.
  // Catching this signal would involve installing a signal handler which may
  // affect other parts of the application and would be difficult to debug.
  //
  // Our strategy is to call waitpid() once up front to check if the process
  // has already exited, otherwise to loop for |wait|, sleeping for
  // at most 256 milliseconds each time using usleep() and then calling
  // waitpid().  The amount of time we sleep starts out at 1 milliseconds, and
  // we double it every 4 sleep cycles.
  //
  // usleep() is speced to exit if a signal is received for which a handler
  // has been installed.  This means that when a SIGCHLD is sent, it will exit
  // depending on behavior external to this function.
  //
  // This function is used primarily for unit tests, if we want to use it in
  // the application itself it would probably be best to examine other routes.

  if (wait.InMilliseconds() == base::kNoTimeout) {
    return HANDLE_EINTR(waitpid(handle, status, 0)) > 0;
  }

  pid_t ret_pid = HANDLE_EINTR(waitpid(handle, status, WNOHANG));
  static const int64_t kMaxSleepInMicroseconds = 1 << 18;  // ~256 milliseconds.
  int64_t max_sleep_time_usecs = 1 << 10;  // ~1 milliseconds.
  int64_t double_sleep_time = 0;

  // If the process hasn't exited yet, then sleep and try again.
  TimeTicks wakeup_time = TimeTicks::Now() + wait;
  while (ret_pid == 0) {
    TimeTicks now = TimeTicks::Now();
    if (now > wakeup_time)
      break;
    // Guaranteed to be non-negative!
    int64_t sleep_time_usecs = (wakeup_time - now).InMicroseconds();
    // Sleep for a bit while we wait for the process to finish.
    if (sleep_time_usecs > max_sleep_time_usecs)
      sleep_time_usecs = max_sleep_time_usecs;

    // usleep() will return 0 and set errno to EINTR on receipt of a signal
    // such as SIGCHLD.
    usleep(sleep_time_usecs);
    ret_pid = HANDLE_EINTR(waitpid(handle, status, WNOHANG));

    if ((max_sleep_time_usecs < kMaxSleepInMicroseconds) &&
        (double_sleep_time++ % 4 == 0)) {
      max_sleep_time_usecs *= 2;
    }
  }

  return ret_pid > 0;
}

TerminationStatus GetTerminationStatusImpl(ProcessHandle handle,
                                           bool can_block,
                                           int* exit_code) {
  int status = 0;
  const pid_t result = HANDLE_EINTR(waitpid(handle, &status,
                                            can_block ? 0 : WNOHANG));
  if (result == -1) {
    DPLOG(ERROR) << "waitpid(" << handle << ")";
    if (exit_code)
      *exit_code = 0;
    return TERMINATION_STATUS_NORMAL_TERMINATION;
  } else if (result == 0) {
    // the child hasn't exited yet.
    if (exit_code)
      *exit_code = 0;
    return TERMINATION_STATUS_STILL_RUNNING;
  }

  if (exit_code)
    *exit_code = status;

  if (WIFSIGNALED(status)) {
    switch (WTERMSIG(status)) {
      case SIGABRT:
      case SIGBUS:
      case SIGFPE:
      case SIGILL:
      case SIGSEGV:
        return TERMINATION_STATUS_PROCESS_CRASHED;
      case SIGINT:
      case SIGKILL:
      case SIGTERM:
        return TERMINATION_STATUS_PROCESS_WAS_KILLED;
      default:
        break;
    }
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    return TERMINATION_STATUS_ABNORMAL_TERMINATION;

  return TERMINATION_STATUS_NORMAL_TERMINATION;
}

}  // namespace

// Attempts to kill the process identified by the given process
// entry structure.  Ignores specified exit_code; posix can't force that.
// Returns true if this is successful, false otherwise.
bool KillProcess(ProcessHandle process_id, int exit_code, bool wait) {
  DCHECK_GT(process_id, 1) << " tried to kill invalid process_id";
  if (process_id <= 1)
    return false;
  bool result = kill(process_id, SIGTERM) == 0;
  if (result && wait) {
    int tries = 60;

    if (RunningOnValgrind()) {
      // Wait for some extra time when running under Valgrind since the child
      // processes may take some time doing leak checking.
      tries *= 2;
    }

    unsigned sleep_ms = 4;

    // The process may not end immediately due to pending I/O
    bool exited = false;
    while (tries-- > 0) {
      pid_t pid = HANDLE_EINTR(waitpid(process_id, NULL, WNOHANG));
      if (pid == process_id) {
        exited = true;
        break;
      }
      if (pid == -1) {
        if (errno == ECHILD) {
          // The wait may fail with ECHILD if another process also waited for
          // the same pid, causing the process state to get cleaned up.
          exited = true;
          break;
        }
        DPLOG(ERROR) << "Error waiting for process " << process_id;
      }

      usleep(sleep_ms * 1000);
      const unsigned kMaxSleepMs = 1000;
      if (sleep_ms < kMaxSleepMs)
        sleep_ms *= 2;
    }

    // If we're waiting and the child hasn't died by now, force it
    // with a SIGKILL.
    if (!exited)
      result = kill(process_id, SIGKILL) == 0;
  }

  if (!result)
    DPLOG(ERROR) << "Unable to terminate process " << process_id;

  return result;
}

bool KillProcessGroup(ProcessHandle process_group_id) {
  bool result = kill(-1 * process_group_id, SIGKILL) == 0;
  if (!result)
    DPLOG(ERROR) << "Unable to terminate process group " << process_group_id;
  return result;
}

TerminationStatus GetTerminationStatus(ProcessHandle handle, int* exit_code) {
  return GetTerminationStatusImpl(handle, false /* can_block */, exit_code);
}

TerminationStatus GetKnownDeadTerminationStatus(ProcessHandle handle,
                                                int* exit_code) {
  bool result = kill(handle, SIGKILL) == 0;

  if (!result)
    DPLOG(ERROR) << "Unable to terminate process " << handle;

  return GetTerminationStatusImpl(handle, true /* can_block */, exit_code);
}

bool WaitForExitCode(ProcessHandle handle, int* exit_code) {
  int status;
  if (HANDLE_EINTR(waitpid(handle, &status, 0)) == -1) {
    NOTREACHED();
    return false;
  }

  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return true;
  }

  // If it didn't exit cleanly, it must have been signaled.
  DCHECK(WIFSIGNALED(status));
  return false;
}

bool WaitForExitCodeWithTimeout(ProcessHandle handle,
                                int* exit_code,
                                base::TimeDelta timeout) {
  int status;
  if (!WaitpidWithTimeout(handle, &status, timeout))
    return false;
  if (WIFSIGNALED(status)) {
    *exit_code = -1;
    return true;
  }
  if (WIFEXITED(status)) {
    *exit_code = WEXITSTATUS(status);
    return true;
  }
  return false;
}

bool WaitForProcessesToExit(const FilePath::StringType& executable_name,
                            base::TimeDelta wait,
                            const ProcessFilter* filter) {
  bool result = false;

  // TODO(port): This is inefficient, but works if there are multiple procs.
  // TODO(port): use waitpid to avoid leaving zombies around

  base::TimeTicks end_time = base::TimeTicks::Now() + wait;
  do {
    NamedProcessIterator iter(executable_name, filter);
    if (!iter.NextProcessEntry()) {
      result = true;
      break;
    }
    base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  } while ((end_time - base::TimeTicks::Now()) > base::TimeDelta());

  return result;
}

#if defined(OS_MACOSX)
// Using kqueue on Mac so that we can wait on non-child processes.
// We can't use kqueues on child processes because we need to reap
// our own children using wait.
static bool WaitForSingleNonChildProcess(ProcessHandle handle,
                                         base::TimeDelta wait) {
  DCHECK_GT(handle, 0);
  DCHECK(wait.InMilliseconds() == base::kNoTimeout || wait > base::TimeDelta());

  ScopedFD kq(kqueue());
  if (!kq.is_valid()) {
    DPLOG(ERROR) << "kqueue";
    return false;
  }

  struct kevent change = {0};
  EV_SET(&change, handle, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
  int result = HANDLE_EINTR(kevent(kq.get(), &change, 1, NULL, 0, NULL));
  if (result == -1) {
    if (errno == ESRCH) {
      // If the process wasn't found, it must be dead.
      return true;
    }

    DPLOG(ERROR) << "kevent (setup " << handle << ")";
    return false;
  }

  // Keep track of the elapsed time to be able to restart kevent if it's
  // interrupted.
  bool wait_forever = wait.InMilliseconds() == base::kNoTimeout;
  base::TimeDelta remaining_delta;
  base::TimeTicks deadline;
  if (!wait_forever) {
    remaining_delta = wait;
    deadline = base::TimeTicks::Now() + remaining_delta;
  }

  result = -1;
  struct kevent event = {0};

  while (wait_forever || remaining_delta > base::TimeDelta()) {
    struct timespec remaining_timespec;
    struct timespec* remaining_timespec_ptr;
    if (wait_forever) {
      remaining_timespec_ptr = NULL;
    } else {
      remaining_timespec = remaining_delta.ToTimeSpec();
      remaining_timespec_ptr = &remaining_timespec;
    }

    result = kevent(kq.get(), NULL, 0, &event, 1, remaining_timespec_ptr);

    if (result == -1 && errno == EINTR) {
      if (!wait_forever) {
        remaining_delta = deadline - base::TimeTicks::Now();
      }
      result = 0;
    } else {
      break;
    }
  }

  if (result < 0) {
    DPLOG(ERROR) << "kevent (wait " << handle << ")";
    return false;
  } else if (result > 1) {
    DLOG(ERROR) << "kevent (wait " << handle << "): unexpected result "
                << result;
    return false;
  } else if (result == 0) {
    // Timed out.
    return false;
  }

  DCHECK_EQ(result, 1);

  if (event.filter != EVFILT_PROC ||
      (event.fflags & NOTE_EXIT) == 0 ||
      event.ident != static_cast<uintptr_t>(handle)) {
    DLOG(ERROR) << "kevent (wait " << handle
                << "): unexpected event: filter=" << event.filter
                << ", fflags=" << event.fflags
                << ", ident=" << event.ident;
    return false;
  }

  return true;
}
#endif  // OS_MACOSX

bool WaitForSingleProcess(ProcessHandle handle, base::TimeDelta wait) {
  ProcessHandle parent_pid = GetParentProcessId(handle);
  ProcessHandle our_pid = Process::Current().handle();
  if (parent_pid != our_pid) {
#if defined(OS_MACOSX)
    // On Mac we can wait on non child processes.
    return WaitForSingleNonChildProcess(handle, wait);
#else
    // Currently on Linux we can't handle non child processes.
    NOTIMPLEMENTED();
#endif  // OS_MACOSX
  }

  int status;
  if (!WaitpidWithTimeout(handle, &status, wait))
    return false;
  return WIFEXITED(status);
}

bool CleanupProcesses(const FilePath::StringType& executable_name,
                      base::TimeDelta wait,
                      int exit_code,
                      const ProcessFilter* filter) {
  bool exited_cleanly = WaitForProcessesToExit(executable_name, wait, filter);
  if (!exited_cleanly)
    KillProcesses(executable_name, exit_code, filter);
  return exited_cleanly;
}

#if !defined(OS_MACOSX)

namespace {

// Return true if the given child is dead. This will also reap the process.
// Doesn't block.
static bool IsChildDead(pid_t child) {
  const pid_t result = HANDLE_EINTR(waitpid(child, NULL, WNOHANG));
  if (result == -1) {
    DPLOG(ERROR) << "waitpid(" << child << ")";
    NOTREACHED();
  } else if (result > 0) {
    // The child has died.
    return true;
  }

  return false;
}

// A thread class which waits for the given child to exit and reaps it.
// If the child doesn't exit within a couple of seconds, kill it.
class BackgroundReaper : public PlatformThread::Delegate {
 public:
  BackgroundReaper(pid_t child, unsigned timeout)
      : child_(child),
        timeout_(timeout) {
  }

  // Overridden from PlatformThread::Delegate:
  virtual void ThreadMain() OVERRIDE {
    WaitForChildToDie();
    delete this;
  }

  void WaitForChildToDie() {
    // Wait forever case.
    if (timeout_ == 0) {
      pid_t r = HANDLE_EINTR(waitpid(child_, NULL, 0));
      if (r != child_) {
        DPLOG(ERROR) << "While waiting for " << child_
                     << " to terminate, we got the following result: " << r;
      }
      return;
    }

    // There's no good way to wait for a specific child to exit in a timed
    // fashion. (No kqueue on Linux), so we just loop and sleep.

    // Wait for 2 * timeout_ 500 milliseconds intervals.
    for (unsigned i = 0; i < 2 * timeout_; ++i) {
      PlatformThread::Sleep(TimeDelta::FromMilliseconds(500));
      if (IsChildDead(child_))
        return;
    }

    if (kill(child_, SIGKILL) == 0) {
      // SIGKILL is uncatchable. Since the signal was delivered, we can
      // just wait for the process to die now in a blocking manner.
      if (HANDLE_EINTR(waitpid(child_, NULL, 0)) < 0)
        DPLOG(WARNING) << "waitpid";
    } else {
      DLOG(ERROR) << "While waiting for " << child_ << " to terminate we"
                  << " failed to deliver a SIGKILL signal (" << errno << ").";
    }
  }

 private:
  const pid_t child_;
  // Number of seconds to wait, if 0 then wait forever and do not attempt to
  // kill |child_|.
  const unsigned timeout_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundReaper);
};

}  // namespace

void EnsureProcessTerminated(ProcessHandle process) {
  // If the child is already dead, then there's nothing to do.
  if (IsChildDead(process))
    return;

  const unsigned timeout = 2;  // seconds
  BackgroundReaper* reaper = new BackgroundReaper(process, timeout);
  PlatformThread::CreateNonJoinable(0, reaper);
}

void EnsureProcessGetsReaped(ProcessHandle process) {
  // If the child is already dead, then there's nothing to do.
  if (IsChildDead(process))
    return;

  BackgroundReaper* reaper = new BackgroundReaper(process, 0);
  PlatformThread::CreateNonJoinable(0, reaper);
}

#endif  // !defined(OS_MACOSX)

}  // namespace base
