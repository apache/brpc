// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ManualConstructor statically-allocates space in which to store some
// object, but does not initialize it.  You can then call the constructor
// and destructor for the object yourself as you see fit.  This is useful
// for memory management optimizations, where you want to initialize and
// destroy an object multiple times but only allocate it once.
//
// (When I say ManualConstructor statically allocates space, I mean that
// the ManualConstructor object itself is forced to be the right size.)
//
// For example usage, check out base/containers/small_map.h.

#ifndef BASE_MEMORY_MANUAL_CONSTRUCTOR_H_
#define BASE_MEMORY_MANUAL_CONSTRUCTOR_H_

#include <stddef.h>

#include "base/memory/aligned_memory.h"

namespace base {

template <typename Type>
class ManualConstructor {
 public:
  // No constructor or destructor because one of the most useful uses of
  // this class is as part of a union, and members of a union cannot have
  // constructors or destructors.  And, anyway, the whole point of this
  // class is to bypass these.

  // Support users creating arrays of ManualConstructor<>s.  This ensures that
  // the array itself has the correct alignment.
  static void* operator new[](size_t size) {
#if defined(COMPILER_MSVC)
    return AlignedAlloc(size, __alignof(Type));
#else
    return AlignedAlloc(size, __alignof__(Type));
#endif
  }
  static void operator delete[](void* mem) {
    AlignedFree(mem);
  }

  inline Type* get() {
    return space_.template data_as<Type>();
  }
  inline const Type* get() const  {
    return space_.template data_as<Type>();
  }

  inline Type* operator->() { return get(); }
  inline const Type* operator->() const { return get(); }

  inline Type& operator*() { return *get(); }
  inline const Type& operator*() const { return *get(); }

  // You can pass up to eight constructor arguments as arguments of Init().
  inline void Init() {
    new(space_.void_data()) Type;
  }

  template <typename T1>
  inline void Init(const T1& p1) {
    new(space_.void_data()) Type(p1);
  }

  template <typename T1, typename T2>
  inline void Init(const T1& p1, const T2& p2) {
    new(space_.void_data()) Type(p1, p2);
  }

  template <typename T1, typename T2, typename T3>
  inline void Init(const T1& p1, const T2& p2, const T3& p3) {
    new(space_.void_data()) Type(p1, p2, p3);
  }

  template <typename T1, typename T2, typename T3, typename T4>
  inline void Init(const T1& p1, const T2& p2, const T3& p3, const T4& p4) {
    new(space_.void_data()) Type(p1, p2, p3, p4);
  }

  template <typename T1, typename T2, typename T3, typename T4, typename T5>
  inline void Init(const T1& p1, const T2& p2, const T3& p3, const T4& p4,
                   const T5& p5) {
    new(space_.void_data()) Type(p1, p2, p3, p4, p5);
  }

  template <typename T1, typename T2, typename T3, typename T4, typename T5,
            typename T6>
  inline void Init(const T1& p1, const T2& p2, const T3& p3, const T4& p4,
                   const T5& p5, const T6& p6) {
    new(space_.void_data()) Type(p1, p2, p3, p4, p5, p6);
  }

  template <typename T1, typename T2, typename T3, typename T4, typename T5,
            typename T6, typename T7>
  inline void Init(const T1& p1, const T2& p2, const T3& p3, const T4& p4,
                   const T5& p5, const T6& p6, const T7& p7) {
    new(space_.void_data()) Type(p1, p2, p3, p4, p5, p6, p7);
  }

  template <typename T1, typename T2, typename T3, typename T4, typename T5,
            typename T6, typename T7, typename T8>
  inline void Init(const T1& p1, const T2& p2, const T3& p3, const T4& p4,
                   const T5& p5, const T6& p6, const T7& p7, const T8& p8) {
    new(space_.void_data()) Type(p1, p2, p3, p4, p5, p6, p7, p8);
  }

  inline void Destroy() {
    get()->~Type();
  }

 private:
#if defined(COMPILER_MSVC)
  AlignedMemory<sizeof(Type), __alignof(Type)> space_;
#else
  AlignedMemory<sizeof(Type), __alignof__(Type)> space_;
#endif
};

}  // namespace base

#endif  // BASE_MEMORY_MANUAL_CONSTRUCTOR_H_
