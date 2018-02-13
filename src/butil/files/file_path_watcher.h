// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module provides a way to monitor a file or directory for changes.

#ifndef BUTIL_FILES_FILE_PATH_WATCHER_H_
#define BUTIL_FILES_FILE_PATH_WATCHER_H_

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/callback.h"
#include "butil/files/file_path.h"
#include "butil/memory/ref_counted.h"
#include "butil/message_loop/message_loop_proxy.h"

namespace butil {

// This class lets you register interest in changes on a FilePath.
// The callback will get called whenever the file or directory referenced by the
// FilePath is changed, including created or deleted. Due to limitations in the
// underlying OS APIs, FilePathWatcher has slightly different semantics on OS X
// than on Windows or Linux. FilePathWatcher on Linux and Windows will detect
// modifications to files in a watched directory. FilePathWatcher on Mac will
// detect the creation and deletion of files in a watched directory, but will
// not detect modifications to those files. See file_path_watcher_kqueue.cc for
// details.
class BUTIL_EXPORT FilePathWatcher {
 public:
  // Callback type for Watch(). |path| points to the file that was updated,
  // and |error| is true if the platform specific code detected an error. In
  // that case, the callback won't be invoked again.
  typedef butil::Callback<void(const FilePath& path, bool error)> Callback;

  // Used internally to encapsulate different members on different platforms.
  class PlatformDelegate : public butil::RefCountedThreadSafe<PlatformDelegate> {
   public:
    PlatformDelegate();

    // Start watching for the given |path| and notify |delegate| about changes.
    virtual bool Watch(const FilePath& path,
                       bool recursive,
                       const Callback& callback) WARN_UNUSED_RESULT = 0;

    // Stop watching. This is called from FilePathWatcher's dtor in order to
    // allow to shut down properly while the object is still alive.
    // It can be called from any thread.
    virtual void Cancel() = 0;

   protected:
    friend class butil::RefCountedThreadSafe<PlatformDelegate>;
    friend class FilePathWatcher;

    virtual ~PlatformDelegate();

    // Stop watching. This is only called on the thread of the appropriate
    // message loop. Since it can also be called more than once, it should
    // check |is_cancelled()| to avoid duplicate work.
    virtual void CancelOnMessageLoopThread() = 0;

    scoped_refptr<butil::MessageLoopProxy> message_loop() const {
      return message_loop_;
    }

    void set_message_loop(butil::MessageLoopProxy* loop) {
      message_loop_ = loop;
    }

    // Must be called before the PlatformDelegate is deleted.
    void set_cancelled() {
      cancelled_ = true;
    }

    bool is_cancelled() const {
      return cancelled_;
    }

   private:
    scoped_refptr<butil::MessageLoopProxy> message_loop_;
    bool cancelled_;
  };

  FilePathWatcher();
  virtual ~FilePathWatcher();

  // A callback that always cleans up the PlatformDelegate, either when executed
  // or when deleted without having been executed at all, as can happen during
  // shutdown.
  static void CancelWatch(const scoped_refptr<PlatformDelegate>& delegate);

  // Returns true if the platform and OS version support recursive watches.
  static bool RecursiveWatchAvailable();

  // Invokes |callback| whenever updates to |path| are detected. This should be
  // called at most once, and from a MessageLoop of TYPE_IO. Set |recursive| to
  // true, to watch |path| and its children. The callback will be invoked on
  // the same loop. Returns true on success.
  //
  // Recursive watch is not supported on all platforms and file systems.
  // Watch() will return false in the case of failure.
  bool Watch(const FilePath& path, bool recursive, const Callback& callback);

 private:
  scoped_refptr<PlatformDelegate> impl_;

  DISALLOW_COPY_AND_ASSIGN(FilePathWatcher);
};

}  // namespace butil

#endif  // BUTIL_FILES_FILE_PATH_WATCHER_H_
