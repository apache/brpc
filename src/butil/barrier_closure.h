// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_BARRIER_CLOSURE_H_
#define BASE_BARRIER_CLOSURE_H_

#include "butil/base_export.h"
#include "butil/callback_forward.h"

namespace butil {

// BarrierClosure executes |done_closure| after it has been invoked
// |num_closures| times.
//
// If |num_closures| is 0, |done_closure| is executed immediately.
//
// BarrierClosure is thread-safe - the count of remaining closures is
// maintained as a butil::AtomicRefCount. |done_closure| will be run on
// the thread that calls the final Run() on the returned closures.
//
// |done_closure| is also Reset() on the final calling thread but due to the
// refcounted nature of callbacks, it is hard to know what thread resources
// will be released on.
BASE_EXPORT butil::Closure BarrierClosure(int num_closures,
                                         const butil::Closure& done_closure);

}  // namespace butil

#endif  // BASE_BARRIER_CLOSURE_H_
