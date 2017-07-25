// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#ifndef BTHREAD_BASE_OBJECT_POOL_H
#define BTHREAD_BASE_OBJECT_POOL_H

#include <cstddef>                       // size_t

// ObjectPool is a derivative class of ResourcePool without identifiers.

namespace base {

// Specialize following classes to override default parameters for type T.
//
// For example, following code specializes max size of a block for type Foo.
//   template <> struct ObjectPoolBlockMaxSize<Foo> {
//       static const size_t value = 1024;
//   };

// Memory is allocated in blocks, override this class to change maximum size
// (in bytes) of a block for type T.
template <typename T> struct ObjectPoolBlockMaxSize {
    static const size_t value = 128 * 1024;
};

// Number of objects in a block will not exceed the value in this class.
template <typename T> struct ObjectPoolBlockMaxItem {
    static const size_t value = 512;
};

// Free objects are grouped into a local chunk (of each thread) before they
// are merged into global list. Override this class to change number of free
// objects in a chunk.
template <typename T> struct ObjectPoolFreeChunkMaxItem {
    static const size_t value = 0;  // use default value
};
template <typename T> struct ObjectPoolFreeChunkMaxItemDynamic {
    static size_t value() { return 0; }  // use default value
};

// ObjectPool calls this function on newly constructed objects. If this
// function returns false, the object is destructed immediately and
// get_object() shall return NULL. This is useful when the constructor
// failed internally(namely ENOMEM).
template <typename T> struct ObjectPoolValidator {
    static bool validate(const T*) { return true; }
};

}  // namespace base

#include "base/object_pool_inl.h"

namespace base {

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

}  // namespace base

#endif  // BTHREAD_BASE_OBJECT_POOL_H
