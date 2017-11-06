// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#ifndef BUTIL_OBJECT_POOL_H
#define BUTIL_OBJECT_POOL_H

#include <cstddef>                       // size_t

// ObjectPool is a derivative class of ResourcePool to allocate and
// reuse fixed-size objects without identifiers.

namespace butil {

// Specialize following classes to override default parameters for type T.
//   namespace butil {
//     template <> struct ObjectPoolBlockMaxSize<Foo> {
//       static const size_t value = 1024;
//     };
//   }

// Memory is allocated in blocks, memory size of a block will not exceed:
//   min(ObjectPoolBlockMaxSize<T>::value,
//       ObjectPoolBlockMaxItem<T>::value * sizeof(T))
template <typename T> struct ObjectPoolBlockMaxSize {
    static const size_t value = 64 * 1024; // bytes
};
template <typename T> struct ObjectPoolBlockMaxItem {
    static const size_t value = 256;
};

// Free objects of each thread are grouped into a chunk before they are merged
// to the global list. Memory size of objects in one free chunk will not exceed:
//   min(ObjectPoolFreeChunkMaxItem<T>::value(),
//       ObjectPoolBlockMaxSize<T>::value,
//       ObjectPoolBlockMaxItem<T>::value * sizeof(T))
template <typename T> struct ObjectPoolFreeChunkMaxItem {
    static size_t value() { return 256; }
};

// ObjectPool calls this function on newly constructed objects. If this
// function returns false, the object is destructed immediately and
// get_object() shall return NULL. This is useful when the constructor
// failed internally(namely ENOMEM).
template <typename T> struct ObjectPoolValidator {
    static bool validate(const T*) { return true; }
};

}  // namespace butil

#include "butil/object_pool_inl.h"

namespace butil {

// Get an object typed |T|. The object should be cleared before usage.
// NOTE: T must be default-constructible.
template <typename T> inline T* get_object() {
    return ObjectPool<T>::singleton()->get_object();
}

// Get an object whose constructor is T(arg1)
template <typename T, typename A1>
inline T* get_object(const A1& arg1) {
    return ObjectPool<T>::singleton()->get_object(arg1);
}

// Get an object whose constructor is T(arg1, arg2)
template <typename T, typename A1, typename A2>
inline T* get_object(const A1& arg1, const A2& arg2) {
    return ObjectPool<T>::singleton()->get_object(arg1, arg2);
}

// Return the object |ptr| back. The object is NOT destructed and will be
// returned by later get_object<T>. Similar with free/delete, validity of
// the object is not checked, user shall not return a not-yet-allocated or
// already-returned object otherwise behavior is undefined.
// Returns 0 when successful, -1 otherwise.
template <typename T> inline int return_object(T* ptr) {
    return ObjectPool<T>::singleton()->return_object(ptr);
}

// Reclaim all allocated objects typed T if caller is the last thread called
// this function, otherwise do nothing. You rarely need to call this function
// manually because it's called automatically when each thread quits.
template <typename T> inline void clear_objects() {
    ObjectPool<T>::singleton()->clear_objects();
}

// Get description of objects typed T.
// This function is possibly slow because it iterates internal structures.
// Don't use it frequently like a "getter" function.
template <typename T> ObjectPoolInfo describe_objects() {
    return ObjectPool<T>::singleton()->describe_objects();
}

}  // namespace butil

#endif  // BUTIL_OBJECT_POOL_H
