// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: You should *NOT* be using this class directly.  PlatformThread is
// the low-level platform-specific abstraction to the OS's threading interface.
// You should instead be using a message-loop driven Thread, see thread.h.

#ifndef BUTIL_THREADING_PLATFORM_THREAD_H_
#define BUTIL_THREADING_PLATFORM_THREAD_H_

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/time/time.h"
#include "butil/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#elif defined(OS_POSIX)
#include <pthread.h>
#include <unistd.h>
#endif

namespace butil {

// Used for logging. Always an integer value.
#if defined(OS_WIN)
typedef DWORD PlatformThreadId;
#elif defined(OS_POSIX)
typedef pid_t PlatformThreadId;
#endif

// Used for thread checking and debugging.
// Meant to be as fast as possible.
// These are produced by PlatformThread::CurrentRef(), and used to later
// check if we are on the same thread or not by using ==. These are safe
// to copy between threads, but can't be copied to another process as they
// have no meaning there. Also, the internal identifier can be re-used
// after a thread dies, so a PlatformThreadRef cannot be reliably used
// to distinguish a new thread from an old, dead thread.
class PlatformThreadRef {
 public:
#if defined(OS_WIN)
  typedef DWORD RefType;
#elif defined(OS_POSIX)
  typedef pthread_t RefType;
#endif
  PlatformThreadRef()
      : id_(0) {
  }

  explicit PlatformThreadRef(RefType id)
      : id_(id) {
  }

  bool operator==(PlatformThreadRef other) const {
    return id_ == other.id_;
  }

  bool is_null() const {
    return id_ == 0;
  }
 private:
  RefType id_;
};

// Used to operate on threads.
class PlatformThreadHandle {
 public:
#if defined(OS_WIN)
  typedef void* Handle;
#elif defined(OS_POSIX)
  typedef pthread_t Handle;
#endif

  PlatformThreadHandle()
      : handle_(0),
        id_(0) {
  }

  explicit PlatformThreadHandle(Handle handle)
      : handle_(handle),
        id_(0) {
  }

  PlatformThreadHandle(Handle handle,
                       PlatformThreadId id)
      : handle_(handle),
        id_(id) {
  }

  bool is_equal(const PlatformThreadHandle& other) const {
    return handle_ == other.handle_;
  }

  bool is_null() const {
    return !handle_;
  }

  Handle platform_handle() const {
    return handle_;
  }

 private:
  friend class PlatformThread;

  Handle handle_;
  PlatformThreadId id_;
};

const PlatformThreadId kInvalidThreadId(0);

// Valid values for SetThreadPriority()
enum ThreadPriority{
  kThreadPriority_Normal,
  // Suitable for low-latency, glitch-resistant audio.
  kThreadPriority_RealtimeAudio,
  // Suitable for threads which generate data for the display (at ~60Hz).
  kThreadPriority_Display,
  // Suitable for threads that shouldn't disrupt high priority work.
  kThreadPriority_Background
};

// A namespace for low-level thread functions.
class BUTIL_EXPORT PlatformThread {
 public:
  // Implement this interface to run code on a background thread.  Your
  // ThreadMain method will be called on the newly created thread.
  class BUTIL_EXPORT Delegate {
   public:
    virtual void ThreadMain() = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Gets the current thread id, which may be useful for logging purposes.
  static PlatformThreadId CurrentId();

  // Gets the current thread reference, which can be used to check if
  // we're on the right thread quickly.
  static PlatformThreadRef CurrentRef();

  // Get the current handle.
  static PlatformThreadHandle CurrentHandle();

  // Yield the current thread so another thread can be scheduled.
  static void YieldCurrentThread();

  // Sleeps for the specified duration.
  static void Sleep(butil::TimeDelta duration);

  // Sets the thread name visible to debuggers/tools. This has no effect
  // otherwise. This name pointer is not copied internally. Thus, it must stay
  // valid until the thread ends.
  static void SetName(const char* name);

  // Gets the thread name, if previously set by SetName.
  static const char* GetName();

  // Creates a new thread.  The |stack_size| parameter can be 0 to indicate
  // that the default stack size should be used.  Upon success,
  // |*thread_handle| will be assigned a handle to the newly created thread,
  // and |delegate|'s ThreadMain method will be executed on the newly created
  // thread.
  // NOTE: When you are done with the thread handle, you must call Join to
  // release system resources associated with the thread.  You must ensure that
  // the Delegate object outlives the thread.
  static bool Create(size_t stack_size, Delegate* delegate,
                     PlatformThreadHandle* thread_handle);

  // CreateWithPriority() does the same thing as Create() except the priority of
  // the thread is set based on |priority|.  Can be used in place of Create()
  // followed by SetThreadPriority().  SetThreadPriority() has not been
  // implemented on the Linux platform yet, this is the only way to get a high
  // priority thread on Linux.
  static bool CreateWithPriority(size_t stack_size, Delegate* delegate,
                                 PlatformThreadHandle* thread_handle,
                                 ThreadPriority priority);

  // CreateNonJoinable() does the same thing as Create() except the thread
  // cannot be Join()'d.  Therefore, it also does not output a
  // PlatformThreadHandle.
  static bool CreateNonJoinable(size_t stack_size, Delegate* delegate);

  // Joins with a thread created via the Create function.  This function blocks
  // the caller until the designated thread exits.  This will invalidate
  // |thread_handle|.
  static void Join(PlatformThreadHandle thread_handle);

  static void SetThreadPriority(PlatformThreadHandle handle,
                                ThreadPriority priority);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(PlatformThread);
};

}  // namespace butil

#endif  // BUTIL_THREADING_PLATFORM_THREAD_H_
