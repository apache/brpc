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


#ifndef BRPC_SIMPLE_DATA_POOL_H
#define BRPC_SIMPLE_DATA_POOL_H

#include "butil/scoped_lock.h"
#include "brpc/data_factory.h"


namespace brpc {

// As the name says, this is a simple unbounded dynamic-size pool for
// reusing void* data. We're assuming that data consumes considerable
// memory and should be reused as much as possible, thus unlike the
// multi-threaded allocator caching objects thread-locally, we just
// put everything in a global list to maximize sharing. It's currently
// used by Server to reuse session-local data. 
class SimpleDataPool {
public:
    struct Stat {
        unsigned nfree;
        unsigned ncreated;
    };

    explicit SimpleDataPool(const DataFactory* factory);
    ~SimpleDataPool();
    void Reset(const DataFactory* factory);
    void Reserve(unsigned n);
    void* Borrow();
    void Return(void*);
    Stat stat() const;
    
private:
    butil::Mutex _mutex;
    unsigned _capacity;
    unsigned _size;
    butil::atomic<unsigned> _ncreated;
    void** _pool;
    const DataFactory* _factory;
};

inline SimpleDataPool::SimpleDataPool(const DataFactory* factory)
    : _capacity(0)
    , _size(0)
    , _ncreated(0)
    , _pool(NULL)
    , _factory(factory) {
}

inline SimpleDataPool::~SimpleDataPool() {
    Reset(NULL);
}

inline void SimpleDataPool::Reset(const DataFactory* factory) {
    unsigned saved_size = 0;
    void** saved_pool = NULL;
    const DataFactory* saved_factory = NULL;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        saved_size = _size;
        saved_pool = _pool;
        saved_factory = _factory;
        _capacity = 0;
        _size = 0;
        _ncreated.store(0, butil::memory_order_relaxed);
        _pool = NULL;
        _factory = factory;
    }
    if (saved_pool) {
        if (saved_factory) {
            for (unsigned i = 0; i < saved_size; ++i) {
                saved_factory->DestroyData(saved_pool[i]);
            }
        }
        free(saved_pool);
    }
}

inline void SimpleDataPool::Reserve(unsigned n) {
    if (_capacity >= n) {
        return;
    }
    BAIDU_SCOPED_LOCK(_mutex);
    if (_capacity >= n) {
        return;
    }
    // Resize.
    const unsigned new_cap = std::max(_capacity * 3 / 2, n);
    void** new_pool = (void**)malloc(new_cap * sizeof(void*));
    if (NULL == new_pool) {
        return;
    }
    if (_pool) {
        memcpy(new_pool, _pool, _capacity * sizeof(void*));
        free(_pool);
    }
    unsigned i = _capacity;
    _capacity = new_cap;
    _pool = new_pool;

    for (; i < n; ++i) {
        void* data = _factory->CreateData();
        if (data == NULL) {
            break;
        }
        _ncreated.fetch_add(1,  butil::memory_order_relaxed);
        _pool[_size++] = data;
    }
}

inline void* SimpleDataPool::Borrow() {
    if (_size) {
        BAIDU_SCOPED_LOCK(_mutex);
        if (_size) {
            return _pool[--_size];
        }
    }
    void* data = _factory->CreateData();
    if (data) {
        _ncreated.fetch_add(1,  butil::memory_order_relaxed);
    }
    return data;
}

inline void SimpleDataPool::Return(void* data) {
    if (data == NULL) {
        return;
    }
    std::unique_lock<butil::Mutex> mu(_mutex);
    if (_capacity == _size) {
        const unsigned new_cap = (_capacity <= 1 ? 128 : (_capacity * 3 / 2));
        void** new_pool = (void**)malloc(new_cap * sizeof(void*));
        if (NULL == new_pool) {
            mu.unlock();
            return _factory->DestroyData(data);
        }
        if (_pool) {
            memcpy(new_pool, _pool, _capacity * sizeof(void*));
            free(_pool);
        }
        _capacity = new_cap;
        _pool = new_pool;
    }
    _pool[_size++] = data;
}

inline SimpleDataPool::Stat SimpleDataPool::stat() const {
    Stat s = { _size, _ncreated.load(butil::memory_order_relaxed) };
    return s;
}

} // namespace brpc


#endif  // BRPC_SIMPLE_DATA_POOL_H
