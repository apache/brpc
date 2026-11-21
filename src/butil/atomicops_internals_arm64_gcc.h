// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an internal atomic implementation, use butil/atomicops.h instead.

#ifndef BUTIL_ATOMICOPS_INTERNALS_ARM64_GCC_H_
#define BUTIL_ATOMICOPS_INTERNALS_ARM64_GCC_H_

#if defined(OS_QNX)
#include <sys/cpuinline.h>
#endif

namespace butil {
namespace subtle {

inline void MemoryBarrier() {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32* ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value) {
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  return old_value;
}

inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32* ptr,
                                         Atomic32 new_value) {
  return __atomic_exchange_n(ptr, new_value, __ATOMIC_RELAXED);
}

inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32* ptr,
                                          Atomic32 increment) {
  return __atomic_add_fetch(ptr, increment, __ATOMIC_RELAXED);
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32* ptr,
                                        Atomic32 increment) {
  return __atomic_add_fetch(ptr, increment, __ATOMIC_ACQ_REL);
}

inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
  return old_value;
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  return old_value;
}

inline void NoBarrier_Store(volatile Atomic32* ptr, Atomic32 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

inline void Acquire_Store(volatile Atomic32* ptr, Atomic32 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline void Release_Store(volatile Atomic32* ptr, Atomic32 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

inline Atomic32 NoBarrier_Load(volatile const Atomic32* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline Atomic32 Acquire_Load(volatile const Atomic32* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

inline Atomic32 Release_Load(volatile const Atomic32* ptr) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

// 64-bit versions of the operations.
// See the 32-bit versions for comments.

inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64* ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value) {
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED);
  return old_value;
}

inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64* ptr,
                                         Atomic64 new_value) {
  return __atomic_exchange_n(ptr, new_value, __ATOMIC_RELAXED);
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64* ptr,
                                          Atomic64 increment) {
  return __atomic_add_fetch(ptr, increment, __ATOMIC_RELAXED);
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64* ptr,
                                        Atomic64 increment) {
  return __atomic_add_fetch(ptr, increment, __ATOMIC_ACQ_REL);
}

inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
  return old_value;
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                              __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  return old_value;
}

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

inline void Acquire_Store(volatile Atomic64* ptr, Atomic64 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline void Release_Store(volatile Atomic64* ptr, Atomic64 value) {
  __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline Atomic64 Acquire_Load(volatile const Atomic64* ptr) {
  return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

inline Atomic64 Release_Load(volatile const Atomic64* ptr) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

}  // namespace subtle
}  // namespace butil

#endif  // BUTIL_ATOMICOPS_INTERNALS_ARM64_GCC_H_
