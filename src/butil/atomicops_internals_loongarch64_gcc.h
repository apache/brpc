// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an internal atomic implementation, use butil/atomicops.h instead.

#ifndef BUTIL_ATOMICOPS_INTERNALS_LOONGARCH64_GCC_H_
#define BUTIL_ATOMICOPS_INTERNALS_LOONGARCH64_GCC_H_

#include "butil/atomicops.h"
#include "butil/atomicops_internals_loongarch64_gcc.h"

namespace butil {
namespace subtle {

// 32bit
inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32* ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value) {
  Atomic32 ret;
  __asm__ __volatile__("1:\n"
                       "ll.w %0, %1\n"
                       "or   $t0, %3, $zero\n"            
                       "bne  %0, %2, 2f\n"
                       "sc.w $t0, %1\n"
                       "beqz $t0, 1b\n"
                       "2:\n"
                       "dbar 0\n"
                       : "=&r" (ret), "+ZB"(*ptr)
                       : "r" (old_value), "r" (new_value)
                       : "t0", "memory");
  return ret;
}

// Atomically store new_value into *ptr, returning the previous value held in
// *ptr.  This routine implies no memory barriers.
inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32* ptr,
                                         Atomic32 new_value) {
  Atomic32 ret;
  __asm__ __volatile__("amswap_db.w %0, %2, %1\n"
                       : "=&r"(ret), "+ZB"(*ptr)
                       : "r"(new_value)
                       : "memory");
  return ret;
}

// Atomically increment *ptr by "increment".  Returns the new value of
// *ptr with the increment applied.  This routine implies no memory barriers.
inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32* ptr,
                                          Atomic32 increment) {
  Atomic32 tmp;
  __asm__ __volatile__("amadd_db.w  %1, %2, %0\n"
                       : "+ZB"(*ptr), "=&r"(tmp)
                       : "r"(increment)
                       : "memory");
  return tmp+increment;
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32* ptr,
                                        Atomic32 increment) {
  MemoryBarrier();
  Atomic32 res = NoBarrier_AtomicIncrement(ptr, increment);
  MemoryBarrier();
  return res;
}

// "Acquire" operations
// ensure that no later memory access can be reordered ahead of the operation.
// "Release" operations ensure that no previous memory access can be reordered
// after the operation.  "Barrier" operations have both "Acquire" and "Release"
// semantics.   A MemoryBarrier() has "Barrier" semantics, but does no memory
// access.
inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  Atomic32 res = NoBarrier_CompareAndSwap(ptr, old_value, new_value);
  MemoryBarrier();
  return res;
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value) {
  MemoryBarrier();
  return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline void NoBarrier_Store(volatile Atomic32* ptr, Atomic32 value) {
  *ptr = value;
}

inline void Acquire_Store(volatile Atomic32* ptr, Atomic32 value) {
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic32* ptr, Atomic32 value) {
  MemoryBarrier();
  *ptr = value;
}

inline Atomic32 NoBarrier_Load(volatile const Atomic32* ptr) {
  return *ptr;
}

inline Atomic32 Acquire_Load(volatile const Atomic32* ptr) {
  Atomic32 value = *ptr;
  MemoryBarrier();
  return value;
}

inline Atomic32 Release_Load(volatile const Atomic32* ptr) {
  MemoryBarrier();
  return *ptr;
}

// 64bit
inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64* ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value) {
  Atomic64 ret;
  __asm__ __volatile__("1:\n"
                       "ll.d %0, %1\n"
                       "or   $t0, %3, $zero\n"            
                       "bne  %0, %2, 2f\n"
                       "sc.d $t0, %1\n"
                       "beqz $t0, 1b\n"
                       "2:\n"
                       "dbar 0\n"
                       : "=&r" (ret), "+ZB"(*ptr)
                       : "r" (old_value), "r" (new_value)
                       : "t0", "memory");
  return ret;
}

// Atomically store new_value into *ptr, returning the previous value held in
// *ptr.  This routine implies no memory barriers.
inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64* ptr,
                                         Atomic64 new_value) {
  Atomic64 ret;
  __asm__ __volatile__("amswap_db.d %0, %2, %1\n"
                       : "=&r"(ret), "+ZB"(*ptr)
                       : "r"(new_value)
                       : "memory");
  return ret;
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64* ptr,
                                          Atomic64 increment) {
  Atomic64 tmp;
  __asm__ __volatile__("amadd_db.d  %1, %2, %0\n"
                       : "+ZB"(*ptr), "=&r"(tmp)
                       : "r"(increment)
                       : "memory");
  return tmp+increment;
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64* ptr,
                                        Atomic64 increment) {
  MemoryBarrier();
  Atomic64 res = NoBarrier_AtomicIncrement(ptr, increment);
  MemoryBarrier();
  return res;
}

// "Acquire" operations
// ensure that no later memory access can be reordered ahead of the operation.
// "Release" operations ensure that no previous memory access can be reordered
// after the operation.  "Barrier" operations have both "Acquire" and "Release"
// semantics.   A MemoryBarrier() has "Barrier" semantics, but does no memory
// access.
inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  Atomic64 res = NoBarrier_CompareAndSwap(ptr, old_value, new_value);
  MemoryBarrier();
  return res;
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value) {
  MemoryBarrier();
  return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
  *ptr = value;
}

inline void MemoryBarrier() {
  __asm__ __volatile__("dbar 0x0" : : : "memory");
}

inline void Acquire_Store(volatile Atomic64* ptr, Atomic64 value) {
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic64* ptr, Atomic64 value) {
  MemoryBarrier();
  *ptr = value;
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
  return *ptr;
}

inline Atomic64 Acquire_Load(volatile const Atomic64* ptr) {
  Atomic64 value = *ptr;
  MemoryBarrier();
  return value;
}

inline Atomic64 Release_Load(volatile const Atomic64* ptr) {
  MemoryBarrier();
  return *ptr;
}

} // namespace butil::subtle
} // namespace butil

#endif  // BUTIL_ATOMICOPS_INTERNALS_LOONGARCH64_GCC_H_
