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
// For example usage, check out butil/containers/small_map.h.

#ifndef BUTIL_MEMORY_MANUAL_CONSTRUCTOR_H_
#define BUTIL_MEMORY_MANUAL_CONSTRUCTOR_H_

#include <stddef.h>

#include "butil/memory/aligned_memory.h"

namespace butil {

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

    Type* get() {
        return _space.template data_as<Type>();
    }

    const Type* get() const  {
        return _space.template data_as<Type>();
    }

    Type* operator->() { return get(); }
    const Type* operator->() const { return get(); }

    Type& operator*() { return *get(); }
    const Type& operator*() const { return *get(); }

    template<typename... Args>
    void Init(Args&&... args) {
        new (_space.void_data()) Type(std::forward<Args>(args)...);
    }

     void Destroy() { get()->~Type(); }

 private:
#if defined(COMPILER_MSVC)
    AlignedMemory<sizeof(Type), __alignof(Type)> _space;
#else
    AlignedMemory<sizeof(Type), __alignof__(Type)> _space;
#endif
};

}  // namespace butil

#endif  // BUTIL_MEMORY_MANUAL_CONSTRUCTOR_H_
