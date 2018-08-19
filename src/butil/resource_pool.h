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

#ifndef BUTIL_RESOURCE_POOL_H
#define BUTIL_RESOURCE_POOL_H

#include <cstddef>                       // size_t

// Efficiently allocate fixed-size (small) objects addressable by identifiers
// in multi-threaded environment.
//
// Comparison with new/delete(glibc 2.3.4) under high contention:
//   -----------------------------
//   get<int>=26.1 return<int>=4.7
//   get<int>=46.1 return<int>=5.4
//   get<int>=27.5 return<int>=5.3
//   get<int>=24.8 return<int>=6.5
//   get<int>=26.6 return<int>=5.3
//   get<int>=24.7 return<int>=4.9
//   get<int>=67.8 return<int>=5.5
//   get<int>=36.7 return<int>=5.0
//   --------------------------------
//   new<int>=295.0 delete<int>=234.5
//   new<int>=299.2 delete<int>=359.7
//   new<int>=288.1 delete<int>=219.0
//   new<int>=298.6 delete<int>=428.0
//   new<int>=319.2 delete<int>=426.5
//   new<int>=293.9 delete<int>=651.8
//   new<int>=304.9 delete<int>=672.8
//   new<int>=170.6 delete<int>=292.8
//   --------------------------------

namespace butil {

// Specialize following classes to override default parameters for type T.
//   namespace butil {
//     template <> struct ResourcePoolBlockMaxSize<Foo> {
//       static const size_t value = 1024;
//     };
//   }

// Memory is allocated in blocks, memory size of a block will not exceed:
//   min(ResourcePoolBlockMaxSize<T>::value,
//       ResourcePoolBlockMaxItem<T>::value * sizeof(T))
template <typename T> struct ResourcePoolBlockMaxSize {
    static const size_t value = 64 * 1024; // bytes
};
template <typename T> struct ResourcePoolBlockMaxItem {
    static const size_t value = 256;
};

// Free objects of each thread are grouped into a chunk before they are merged
// to the global list. Memory size of objects in one free chunk will not exceed:
//   min(ResourcePoolFreeChunkMaxItem<T>::value(),
//       ResourcePoolBlockMaxSize<T>::value,
//       ResourcePoolBlockMaxItem<T>::value * sizeof(T))
template <typename T> struct ResourcePoolFreeChunkMaxItem {
    static size_t value() { return 256; }
};

// ResourcePool calls this function on newly constructed objects. If this
// function returns false, the object is destructed immediately and
// get_object() shall return NULL. This is useful when the constructor
// failed internally(namely ENOMEM).
template <typename T> struct ResourcePoolValidator {
    static bool validate(const T*) { return true; }
};

}  // namespace butil

#include "butil/resource_pool_inl.h"

namespace butil {

// Get an object typed |T| and write its identifier into |id|.
// The object should be cleared before usage.
// NOTE: T must be default-constructible.
template <typename T> inline T* get_resource(ResourceId<T>* id) {
    return ResourcePool<T>::singleton()->get_resource(id);
}

// Get an object whose constructor is T(arg1)
template <typename T, typename A1>
inline T* get_resource(ResourceId<T>* id, const A1& arg1) {
    return ResourcePool<T>::singleton()->get_resource(id, arg1);
}

// Get an object whose constructor is T(arg1, arg2)
template <typename T, typename A1, typename A2>
inline T* get_resource(ResourceId<T>* id, const A1& arg1, const A2& arg2) {
    return ResourcePool<T>::singleton()->get_resource(id, arg1, arg2);
}

// Return the object associated with identifier |id| back. The object is NOT
// destructed and will be returned by later get_resource<T>. Similar with
// free/delete, validity of the id is not checked, user shall not return a
// not-yet-allocated or already-returned id otherwise behavior is undefined.
// Returns 0 when successful, -1 otherwise.
template <typename T> inline int return_resource(ResourceId<T> id) {
    return ResourcePool<T>::singleton()->return_resource(id);
}

// Get the object associated with the identifier |id|.
// Returns NULL if |id| was not allocated by get_resource<T> or
// ResourcePool<T>::get_resource() of a variant before.
// Addressing a free(returned to pool) identifier does not return NULL.
// NOTE: Calling this function before any other get_resource<T>/
//       return_resource<T>/address<T>, even if the identifier is valid,
//       may race with another thread calling clear_resources<T>.
template <typename T> inline T* address_resource(ResourceId<T> id) {
    return ResourcePool<T>::address_resource(id);
}

// Reclaim all allocated resources typed T if caller is the last thread called
// this function, otherwise do nothing. You rarely need to call this function
// manually because it's called automatically when each thread quits.
template <typename T> inline void clear_resources() {
    ResourcePool<T>::singleton()->clear_resources();
}

// Get description of resources typed T.
// This function is possibly slow because it iterates internal structures.
// Don't use it frequently like a "getter" function.
template <typename T> ResourcePoolInfo describe_resources() {
    return ResourcePool<T>::singleton()->describe_resources();
}

}  // namespace butil

#endif  // BUTIL_RESOURCE_POOL_H
