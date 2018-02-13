// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a low level implementation of atomic semantics for reference
// counting.  Please use butil/memory/ref_counted.h directly instead.
//
// The implementation includes annotations to avoid some false positives
// when using data race detection tools.

#ifndef BUTIL_ATOMIC_REF_COUNT_H_
#define BUTIL_ATOMIC_REF_COUNT_H_

#include "butil/atomicops.h"
#include "butil/third_party/dynamic_annotations/dynamic_annotations.h"

namespace butil {

typedef subtle::Atomic32 AtomicRefCount;

// Increment a reference count by "increment", which must exceed 0.
inline void AtomicRefCountIncN(volatile AtomicRefCount *ptr,
                               AtomicRefCount increment) {
  subtle::NoBarrier_AtomicIncrement(ptr, increment);
}

// Decrement a reference count by "decrement", which must exceed 0,
// and return whether the result is non-zero.
// Insert barriers to ensure that state written before the reference count
// became zero will be visible to a thread that has just made the count zero.
inline bool AtomicRefCountDecN(volatile AtomicRefCount *ptr,
                               AtomicRefCount decrement) {
  ANNOTATE_HAPPENS_BEFORE(ptr);
  bool res = (subtle::Barrier_AtomicIncrement(ptr, -decrement) != 0);
  if (!res) {
    ANNOTATE_HAPPENS_AFTER(ptr);
  }
  return res;
}

// Increment a reference count by 1.
inline void AtomicRefCountInc(volatile AtomicRefCount *ptr) {
  butil::AtomicRefCountIncN(ptr, 1);
}

// Decrement a reference count by 1 and return whether the result is non-zero.
// Insert barriers to ensure that state written before the reference count
// became zero will be visible to a thread that has just made the count zero.
inline bool AtomicRefCountDec(volatile AtomicRefCount *ptr) {
  return butil::AtomicRefCountDecN(ptr, 1);
}

// Return whether the reference count is one.  If the reference count is used
// in the conventional way, a refrerence count of 1 implies that the current
// thread owns the reference and no other thread shares it.  This call performs
// the test for a reference count of one, and performs the memory barrier
// needed for the owning thread to act on the object, knowing that it has
// exclusive access to the object.
inline bool AtomicRefCountIsOne(volatile AtomicRefCount *ptr) {
  bool res = (subtle::Acquire_Load(ptr) == 1);
  if (res) {
    ANNOTATE_HAPPENS_AFTER(ptr);
  }
  return res;
}

// Return whether the reference count is zero.  With conventional object
// referencing counting, the object will be destroyed, so the reference count
// should never be zero.  Hence this is generally used for a debug check.
inline bool AtomicRefCountIsZero(volatile AtomicRefCount *ptr) {
  bool res = (subtle::Acquire_Load(ptr) == 0);
  if (res) {
    ANNOTATE_HAPPENS_AFTER(ptr);
  }
  return res;
}

}  // namespace butil

#endif  // BUTIL_ATOMIC_REF_COUNT_H_
