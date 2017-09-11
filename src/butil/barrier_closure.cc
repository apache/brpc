// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/barrier_closure.h"

#include "butil/atomic_ref_count.h"
#include "butil/bind.h"

namespace {

// Maintains state for a BarrierClosure.
class BarrierInfo {
 public:
  BarrierInfo(int num_callbacks_left, const butil::Closure& done_closure);
  void Run();

 private:
  butil::AtomicRefCount num_callbacks_left_;
  butil::Closure done_closure_;
};

BarrierInfo::BarrierInfo(int num_callbacks, const butil::Closure& done_closure)
    : num_callbacks_left_(num_callbacks),
      done_closure_(done_closure) {
}

void BarrierInfo::Run() {
  DCHECK(!butil::AtomicRefCountIsZero(&num_callbacks_left_));
  if (!butil::AtomicRefCountDec(&num_callbacks_left_)) {
    done_closure_.Run();
    done_closure_.Reset();
  }
}

}  // namespace

namespace butil {

butil::Closure BarrierClosure(int num_callbacks_left,
                             const butil::Closure& done_closure) {
  DCHECK(num_callbacks_left >= 0);

  if (num_callbacks_left == 0)
    done_closure.Run();

  return butil::Bind(&BarrierInfo::Run,
                    butil::Owned(
                        new BarrierInfo(num_callbacks_left, done_closure)));
}

}  // namespace butil
