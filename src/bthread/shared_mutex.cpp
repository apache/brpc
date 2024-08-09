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

#include "shared_mutex.h"
#include "butex.h"
#include "butil/logging.h"

namespace bthread {

SharedMutex::SharedMutex(): _reader_count(0), _reader_wait(0) {
    _writer_butex = butex_create_checked<uint32_t>();
    *_writer_butex = 0;
    _reader_butex = butex_create_checked<uint32_t>();
    *_reader_butex = 0;
}
SharedMutex::~SharedMutex() {
    butex_destroy(_writer_butex);
    butex_destroy(_reader_butex);
}

void SharedMutex::lock_shared() {
    if (_reader_count.fetch_add(1) < 0) {
        butex_wait(_reader_butex, 1, nullptr);
        *_reader_butex -= 1;
    }
}

void SharedMutex::unlock_shared() {
    int32_t r = _reader_count.fetch_add(-1);
    if (r < 0) {
        unlock_shared_slow(r);
    }
}

void SharedMutex::unlock_shared_slow(int32_t r) {
    CHECK(r != 0 && r != -max_readers) << "unlock of unlocked SharedMutex";
    if (_reader_wait.fetch_add(-1) == 1) {
        *_writer_butex = 1;
        butex_wake(_writer_butex);
    }
}

void SharedMutex::lock() {
    _w.lock();
    int32_t r = _reader_count.fetch_add(-max_readers);
    if (r != 0 && _reader_wait.fetch_add(r) + r != 0) {
        butex_wait(_writer_butex, 1, nullptr);
        *_writer_butex = 0;
    }
}

void SharedMutex::unlock() {
    int32_t r = _reader_count.fetch_add(max_readers) + max_readers;
    for(int32_t i = 0; i < r; i++) {
        *_reader_butex += 1;
        butex_wake(_reader_butex);
    } 
    _w.unlock();
}
}



