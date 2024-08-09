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

#ifndef  BTHREAD_SHARED_MUTEX_H
#define  BTHREAD_SHARED_MUTEX_H

#include "mutex.h"

namespace bthread {

// compatible with c++17 std::shared_mutex, migration from golang
// see https://github.com/golang/go/blob/master/src/sync/rwmutex.go
class SharedMutex {
public:
    SharedMutex();
    ~SharedMutex();
    void lock_shared();
    void unlock_shared();
    void lock();
    void unlock();

private:
    DISALLOW_COPY_AND_ASSIGN(SharedMutex);
    void unlock_shared_slow(int32_t r);

    static constexpr int32_t max_readers = 1 << 30;
    Mutex _w;
    uint32_t* _writer_butex;
    uint32_t* _reader_butex;
    butil::atomic<int32_t> _reader_count;
    butil::atomic<int32_t> _reader_wait;
};
}

#endif  //BTHREAD_SHARED_MUTEX_H