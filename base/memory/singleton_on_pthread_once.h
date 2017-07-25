// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved
// 
// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Dec 15 14:37:39 CST 2016

#ifndef BASE_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H
#define BASE_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H

#include <pthread.h>
#include "base/atomicops.h"

namespace base {

template <typename T> struct GetLeakySingleton {
    static base::subtle::AtomicWord g_leaky_singleton_untyped;
    static pthread_once_t g_create_leaky_singleton_once;
    static void create_leaky_singleton();
};
template <typename T>
base::subtle::AtomicWord GetLeakySingleton<T>::g_leaky_singleton_untyped = 0;

template <typename T>
pthread_once_t GetLeakySingleton<T>::g_create_leaky_singleton_once = PTHREAD_ONCE_INIT;

template <typename T>
void GetLeakySingleton<T>::create_leaky_singleton() {
    T* obj = new T;
    base::subtle::Release_Store(
        &g_leaky_singleton_untyped,
        reinterpret_cast<base::subtle::AtomicWord>(obj));
}

// To get a never-deleted singleton of a type T, just call get_leaky_singleton<T>().
// Most daemon threads or objects that need to be always-on can be created by
// this function.
// This function can be called safely before main() w/o initialization issues of
// global variables.
template <typename T>
inline T* get_leaky_singleton() {
    const base::subtle::AtomicWord value = base::subtle::Acquire_Load(
        &GetLeakySingleton<T>::g_leaky_singleton_untyped);
    if (value) {
        return reinterpret_cast<T*>(value);
    }
    pthread_once(&GetLeakySingleton<T>::g_create_leaky_singleton_once,
                 GetLeakySingleton<T>::create_leaky_singleton);
    return reinterpret_cast<T*>(
        GetLeakySingleton<T>::g_leaky_singleton_untyped);
}

// True(non-NULL) if the singleton is created.
// The returned object (if not NULL) can be used directly.
template <typename T>
inline T* has_leaky_singleton() {
    return reinterpret_cast<T*>(
        base::subtle::Acquire_Load(
            &GetLeakySingleton<T>::g_leaky_singleton_untyped));
}

} // namespace base

#endif // BASE_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H
