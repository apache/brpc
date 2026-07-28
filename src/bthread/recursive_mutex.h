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

// bthread - An M:N threading library to make applications more concurrent.

// Split from mutex.h so bvar can use RecursiveMutex without depending on the
// full bthread mutex header (which includes bvar utilities and would form a
// bazel dependency cycle: bvar -> bthread -> bvar).

#ifndef BTHREAD_RECURSIVE_MUTEX_H
#define BTHREAD_RECURSIVE_MUTEX_H

#include <system_error>

#include "bthread/types.h"
#include "butil/macros.h"

__BEGIN_DECLS
extern int bthread_recursive_mutex_init(bthread_recursive_mutex_t* mutex);
extern int bthread_recursive_mutex_destroy(bthread_recursive_mutex_t* mutex);
extern int bthread_recursive_mutex_trylock(bthread_recursive_mutex_t* mutex);
extern int bthread_recursive_mutex_lock(bthread_recursive_mutex_t* mutex);
extern int bthread_recursive_mutex_unlock(bthread_recursive_mutex_t* mutex);
__END_DECLS

namespace bthread {

class RecursiveMutex {
public:
    typedef bthread_recursive_mutex_t* native_handler_type;

    RecursiveMutex() {
        const int ec = bthread_recursive_mutex_init(&_mutex);
        if (ec != 0) {
            throw std::system_error(
                std::error_code(ec, std::system_category()),
                "RecursiveMutex constructor failed");
        }
    }
    ~RecursiveMutex() {
        CHECK_EQ(0, bthread_recursive_mutex_destroy(&_mutex));
    }
    native_handler_type native_handler() { return &_mutex; }
    void lock() {
        const int ec = bthread_recursive_mutex_lock(&_mutex);
        if (ec != 0) {
            throw std::system_error(
                std::error_code(ec, std::system_category()),
                "RecursiveMutex lock failed");
        }
    }
    void unlock() { CHECK_EQ(0, bthread_recursive_mutex_unlock(&_mutex)); }
    bool try_lock() {
        return bthread_recursive_mutex_trylock(&_mutex) == 0;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(RecursiveMutex);
    bthread_recursive_mutex_t _mutex;
};

}  // namespace bthread

#endif  // BTHREAD_RECURSIVE_MUTEX_H
