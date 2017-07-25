// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_REF_COUNTED_DELETE_ON_MESSAGE_LOOP_H_
#define BASE_MEMORY_REF_COUNTED_DELETE_ON_MESSAGE_LOOP_H_

#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop_proxy.h"

namespace base {

// RefCountedDeleteOnMessageLoop is similar to RefCountedThreadSafe, and ensures
// that the object will be deleted on a specified message loop.
//
// Sample usage:
// class Foo : public RefCountedDeleteOnMessageLoop<Foo> {
//
//   Foo(const scoped_refptr<MessageLoopProxy>& loop)
//       : RefCountedDeleteOnMessageLoop<Foo>(loop) {
//     ...
//   }
//   ...
//  private:
//   friend class RefCountedDeleteOnMessageLoop<Foo>;
//   friend class DeleteHelper<Foo>;
//
//   ~Foo();
// };

template <class T>
class RefCountedDeleteOnMessageLoop : public subtle::RefCountedThreadSafeBase {
 public:
  RefCountedDeleteOnMessageLoop(
      const scoped_refptr<MessageLoopProxy>& proxy) : proxy_(proxy) {
    DCHECK(proxy_.get());
  }

  void AddRef() const {
    subtle::RefCountedThreadSafeBase::AddRef();
  }

  void Release() const {
    if (subtle::RefCountedThreadSafeBase::Release())
      DestructOnMessageLoop();
  }

 protected:
  friend class DeleteHelper<RefCountedDeleteOnMessageLoop>;
  ~RefCountedDeleteOnMessageLoop() {}

  void DestructOnMessageLoop() const {
    const T* t = static_cast<const T*>(this);
    if (proxy_->BelongsToCurrentThread())
      delete t;
    else
      proxy_->DeleteSoon(FROM_HERE, t);
  }

  scoped_refptr<MessageLoopProxy> proxy_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RefCountedDeleteOnMessageLoop);
};

}  // namespace base

#endif  // BASE_MEMORY_REF_COUNTED_DELETE_ON_MESSAGE_LOOP_H_
