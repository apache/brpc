// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
 
// Date: Thu Dec 15 14:37:39 CST 2016

#ifndef BUTIL_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H
#define BUTIL_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H

#include <pthread.h>
#include "butil/atomicops.h"

namespace butil {

template <typename T> class GetLeakySingleton {
public:
    static butil::subtle::AtomicWord g_leaky_singleton_untyped;
    static pthread_once_t g_create_leaky_singleton_once;
    static void create_leaky_singleton();
};
template <typename T>
butil::subtle::AtomicWord GetLeakySingleton<T>::g_leaky_singleton_untyped = 0;

template <typename T>
pthread_once_t GetLeakySingleton<T>::g_create_leaky_singleton_once = PTHREAD_ONCE_INIT;

template <typename T>
void GetLeakySingleton<T>::create_leaky_singleton() {
    T* obj = new T;
    butil::subtle::Release_Store(
        &g_leaky_singleton_untyped,
        reinterpret_cast<butil::subtle::AtomicWord>(obj));
}

// To get a never-deleted singleton of a type T, just call get_leaky_singleton<T>().
// Most daemon threads or objects that need to be always-on can be created by
// this function.
// This function can be called safely before main() w/o initialization issues of
// global variables.
template <typename T>
inline T* get_leaky_singleton() {
    const butil::subtle::AtomicWord value = butil::subtle::Acquire_Load(
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
        butil::subtle::Acquire_Load(
            &GetLeakySingleton<T>::g_leaky_singleton_untyped));
}

} // namespace butil

#endif // BUTIL_MEMORY_SINGLETON_ON_PTHREAD_ONCE_H
