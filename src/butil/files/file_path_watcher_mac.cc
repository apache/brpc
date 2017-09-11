// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/files/file_path_watcher.h"
#include "butil/files/file_path_watcher_kqueue.h"

#if !defined(OS_IOS)
#include "butil/files/file_path_watcher_fsevents.h"
#endif

namespace butil {

namespace {

class FilePathWatcherImpl : public FilePathWatcher::PlatformDelegate {
 public:
  virtual bool Watch(const FilePath& path,
                     bool recursive,
                     const FilePathWatcher::Callback& callback) OVERRIDE {
    // Use kqueue for non-recursive watches and FSEvents for recursive ones.
    DCHECK(!impl_.get());
    if (recursive) {
      if (!FilePathWatcher::RecursiveWatchAvailable())
        return false;
#if !defined(OS_IOS)
      impl_ = new FilePathWatcherFSEvents();
#endif  // OS_IOS
    } else {
      impl_ = new FilePathWatcherKQueue();
    }
    DCHECK(impl_.get());
    return impl_->Watch(path, recursive, callback);
  }

  virtual void Cancel() OVERRIDE {
    if (impl_)
      impl_->Cancel();
    set_cancelled();
  }

  virtual void CancelOnMessageLoopThread() OVERRIDE {
    if (impl_)
      impl_->Cancel();
    set_cancelled();
  }

 protected:
  virtual ~FilePathWatcherImpl() {}

  scoped_refptr<PlatformDelegate> impl_;
};

}  // namespace

FilePathWatcher::FilePathWatcher() {
  impl_ = new FilePathWatcherImpl();
}

}  // namespace butil
