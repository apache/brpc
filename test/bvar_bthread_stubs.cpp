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

#include <cerrno>
#include <mutex>

#include "bthread/mutex.h"

namespace {

// test_bvar verifies bvar behavior without linking the bthread runtime.
std::recursive_mutex* native_mutex(bthread_recursive_mutex_t* mutex) {
    return reinterpret_cast<std::recursive_mutex*>(mutex->mutex.butex);
}

}  // namespace

extern "C" {

int bthread_recursive_mutex_init(bthread_recursive_mutex_t* mutex) {
    mutex->mutex.butex =
        reinterpret_cast<unsigned*>(new std::recursive_mutex);
    return 0;
}

int bthread_recursive_mutex_destroy(bthread_recursive_mutex_t* mutex) {
    delete native_mutex(mutex);
    mutex->mutex.butex = nullptr;
    return 0;
}

int bthread_recursive_mutex_trylock(bthread_recursive_mutex_t* mutex) {
    return native_mutex(mutex)->try_lock() ? 0 : EBUSY;
}

int bthread_recursive_mutex_lock(bthread_recursive_mutex_t* mutex) {
    native_mutex(mutex)->lock();
    return 0;
}

int bthread_recursive_mutex_unlock(bthread_recursive_mutex_t* mutex) {
    native_mutex(mutex)->unlock();
    return 0;
}

}  // extern "C"
