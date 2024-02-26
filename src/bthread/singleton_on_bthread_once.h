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

#ifndef BRPC_SINGLETON_ON_BTHREAD_ONCE_H
#define BRPC_SINGLETON_ON_BTHREAD_ONCE_H

#include "bthread/bthread.h"

namespace bthread {

template <typename T>
class GetLeakySingleton {
public:
    static T* _instance;
    static bthread_once_t* g_create_leaky_singleton_once;
    static void create_leaky_singleton();
};

template <typename T>
T* GetLeakySingleton<T>::_instance = NULL;

template <typename T>
bthread_once_t* GetLeakySingleton<T>::g_create_leaky_singleton_once
    = new bthread_once_t;

template <typename T>
void GetLeakySingleton<T>::create_leaky_singleton() {
    _instance = new T;
}

// To get a never-deleted singleton of a type T, just call
// bthread::get_leaky_singleton<T>(). Most daemon (b)threads
// or objects that need to be always-on can be created by
// this function. This function can be called safely not only
// before main() w/o initialization issues of global variables,
// but also on bthread with hanging operation.
template <typename T>
inline T* get_leaky_singleton() {
    using LeakySingleton = GetLeakySingleton<T>;
    bthread_once(LeakySingleton::g_create_leaky_singleton_once,
                 LeakySingleton::create_leaky_singleton);
    return LeakySingleton::_instance;
}

} // namespace bthread

#endif // BRPC_SINGLETON_ON_BTHREAD_ONCE_H
