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

#ifndef  BUTIL_THREAD_KEY_H
#define  BUTIL_THREAD_KEY_H

#include <limits>
#include <pthread.h>
#include <stdlib.h>
#include <vector>
#include "butil/scoped_lock.h"
#include "butil/type_traits.h"

namespace butil {

typedef void (*DtorFunction)(void *);

class ThreadKey {
public:
    friend int thread_key_create(ThreadKey& thread_key, DtorFunction dtor);
    friend int thread_key_delete(ThreadKey& thread_key);
    friend int thread_setspecific(ThreadKey& thread_key, void* data);
    friend void* thread_getspecific(ThreadKey& thread_key);

    static constexpr size_t InvalidID = std::numeric_limits<size_t>::max();
    static constexpr size_t InitSeq = 0;

    constexpr ThreadKey() : _id(InvalidID), _seq(InitSeq) {}

    ~ThreadKey() {
        Reset();
    }

    ThreadKey(ThreadKey&& other) noexcept
        : _id(other._id)
        , _seq(other._seq) {
        other.Reset();
    }

    ThreadKey& operator=(ThreadKey&& other) noexcept;

    ThreadKey(const ThreadKey& other) = delete;
    ThreadKey& operator=(const ThreadKey& other) = delete;

    bool Valid() const;

    void Reset() {
        _id = InvalidID;
        _seq = InitSeq;
    }

private:
    size_t _id; // Key id.
    // Sequence number form g_thread_keys set in thread_key_create.
    size_t _seq;
};

struct ThreadKeyInfo {
    ThreadKeyInfo() : seq(0), dtor(NULL) {}

    size_t seq; // Already allocated?
    DtorFunction dtor; // Destruction routine.
};

struct ThreadKeyTLS {
    ThreadKeyTLS() : seq(0), data(NULL) {}

    // Sequence number form ThreadKey,
    // set in `thread_setspecific',
    // used to check if the key is valid in `thread_getspecific'.
    size_t seq;
    void* data; // User data.
};

// pthread_key_xxx implication without num limit of key.
int thread_key_create(ThreadKey& thread_key, DtorFunction dtor);
int thread_key_delete(ThreadKey& thread_key);
int thread_setspecific(ThreadKey& thread_key, void* data);
void* thread_getspecific(ThreadKey& thread_key);


template <typename T>
class ThreadLocal {
public:
    ThreadLocal() : ThreadLocal(false) {}

    explicit ThreadLocal(bool delete_on_thread_exit);

    ~ThreadLocal();

    // non-copyable
    ThreadLocal(const ThreadLocal&) = delete;
    ThreadLocal& operator=(const ThreadLocal&) = delete;

    T* get();

    T* operator->() { return get(); }

    T& operator*() { return *get(); }

    // Iterate through all thread local objects.
    // Callback, which must accept Args params and return void,
    // will be called under a thread lock.
    template <typename Callback>
    void for_each(Callback&& callback) {
        BAIDU_CASSERT(
            (is_result_void<Callback, T*>::value),
            "Callback must accept Args params and return void");
        BAIDU_SCOPED_LOCK(_mutex);
        for (auto ptr : ptrs) {
            callback(ptr);
        }
    }

    void reset(T* ptr);

    void reset() {
        reset(NULL);
    }

private:
    static void DefaultDtor(void* ptr) {
        if (ptr) {
            delete static_cast<T*>(ptr);
        }
    }

    ThreadKey _key;
    pthread_mutex_t _mutex;
    // All pointers of data allocated by the ThreadLocal.
    std::vector<T*> ptrs;
    // Delete data on thread exit or destructor of ThreadLocal.
    bool _delete_on_thread_exit;
};

template <typename T>
ThreadLocal<T>::ThreadLocal(bool delete_on_thread_exit)
        : _mutex(PTHREAD_MUTEX_INITIALIZER)
        , _delete_on_thread_exit(delete_on_thread_exit) {
    DtorFunction dtor = _delete_on_thread_exit ? DefaultDtor : NULL;
    thread_key_create(_key, dtor);
}


template <typename T>
ThreadLocal<T>::~ThreadLocal() {
    thread_key_delete(_key);
    if (!_delete_on_thread_exit) {
        BAIDU_SCOPED_LOCK(_mutex);
        for (auto ptr : ptrs) {
            DefaultDtor(ptr);
        }
    }
    pthread_mutex_destroy(&_mutex);
}

template <typename T>
T* ThreadLocal<T>::get() {
    T* ptr = static_cast<T*>(thread_getspecific(_key));
    if (!ptr) {
        ptr = new (std::nothrow) T;
        if (!ptr) {
            return NULL;
        }
        int rc = thread_setspecific(_key, ptr);
        if (rc != 0) {
            DefaultDtor(ptr);
            return NULL;
        }
        {
            BAIDU_SCOPED_LOCK(_mutex);
            ptrs.push_back(ptr);
        }
    }
    return ptr;
}

template <typename T>
void ThreadLocal<T>::reset(T* ptr) {
    T* old_ptr = get();
    if (ptr == old_ptr) {
        return;
    }
    if (thread_setspecific(_key, ptr) != 0) {
        return;
    }
    {
        BAIDU_SCOPED_LOCK(_mutex);
        if (ptr) {
            ptrs.push_back(ptr);
        }
        // Remove and delete old_ptr.
        if (old_ptr) {
            auto iter = std::remove(ptrs.begin(), ptrs.end(), old_ptr);
            if (iter != ptrs.end()) {
                ptrs.erase(iter, ptrs.end());
            }
            DefaultDtor(old_ptr);
        }
    }
}

}


#endif // BUTIL_THREAD_KEY_H
