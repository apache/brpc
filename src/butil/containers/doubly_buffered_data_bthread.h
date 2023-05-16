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

// Date: Mon Sep 22 22:23:13 CST 2014

#ifndef BUTIL_DOUBLY_BUFFERED_DATA_BTHREAD_H
#define BUTIL_DOUBLY_BUFFERED_DATA_BTHREAD_H

#include <deque>
#include <vector>                                       // std::vector
#include <pthread.h>
#include "butil/containers/doubly_buffered_data_internal.h"
#include "butil/scoped_lock.h"
#include "butil/thread_local.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/type_traits.h"
#include "butil/errno.h"
#include "butil/atomicops.h"
#include "butil/unique_ptr.h"

namespace butil {

// Compared to DoublyBufferedData, DoublyBufferedDataBthread allows to
// suspend bthread when executing query logic.
// If bthread will not be suspended in the query logic, DoublyBufferedDataBthread
// also makes Read() almost lock-free by making Modify() *much* slower.
// If bthread will be suspended in the query logic, there is competition among
// bthreads using the same Wrapper.
//
// Read(): begin with thread-local reference count of foreground instance
// incremented by one which be protected by a thread-local mutex, then read
// the foreground instance which will not be changed before its all thread-local
// reference count become zero. At last, after the query completes, thread-local
// reference count of foreground instance will be decremented by one, and if
// it becomes zero, notifies Modify().
//
// Modify(): Modify background instance which is not used by any Read(), flip
// foreground and background, lock thread-local mutexes one by one and wait
// until thread-local reference counts which be protected by a thread-local
// mutex become 0 to make sure all existing Read() finish and later Read()
// see new foreground, then modify background(foreground before flip) again.

template <typename T>
class DoublyBufferedDataBthread {
    friend class internal::WrapperTLSGroup<DoublyBufferedDataBthread>;
    class Wrapper;
public:
    typedef T DataType;

    class ScopedPtr {
        friend class DoublyBufferedDataBthread;
    public:
        ScopedPtr() : _data(NULL), _index(0), _w(NULL) {}
        ~ScopedPtr() {
            if (_w) {
                _w->EndRead(_index);
            }
        }
        const T* get() const { return _data; }
        const T& operator*() const { return *_data; }
        const T* operator->() const { return _data; }

    private:
        DISALLOW_COPY_AND_ASSIGN(ScopedPtr);
        const T* _data;
        int _index;
        Wrapper* _w;
    };
    
    DoublyBufferedDataBthread();
    ~DoublyBufferedDataBthread();

    // Put foreground instance into ptr. The instance will not be changed until
    // ptr is destructed.
    // Returns 0 on success, -1 otherwise.
    int Read(ScopedPtr* ptr);

    // Modify background and foreground instances. fn(T&, ...) will be called
    // twice. Modify() from different threads are exclusive from each other.
    // NOTE: Call same series of fn to different equivalent instances should
    // result in equivalent instances, otherwise foreground and background
    // instance will be inconsistent.
    template <typename Fn> size_t Modify(Fn& fn);
    template <typename Fn, typename Arg1> size_t Modify(Fn& fn, const Arg1&);
    template <typename Fn, typename Arg1, typename Arg2>
    size_t Modify(Fn& fn, const Arg1&, const Arg2&);

    // fn(T& background, const T& foreground, ...) will be called to background
    // and foreground instances respectively.
    template <typename Fn> size_t ModifyWithForeground(Fn& fn);
    template <typename Fn, typename Arg1>
    size_t ModifyWithForeground(Fn& fn, const Arg1&);
    template <typename Fn, typename Arg1, typename Arg2>
    size_t ModifyWithForeground(Fn& fn, const Arg1&, const Arg2&);
    
private:

    const T* UnsafeRead(int& index) const {
        index = _index.load(butil::memory_order_acquire);
        return _data + index;
    }

    Wrapper* AddWrapper(Wrapper*);
    void RemoveWrapper(Wrapper*);

    // Foreground and background void.
    T _data[2];

    // Index of foreground instance.
    butil::atomic<int> _index;

    // Key to access thread-local wrappers.
    internal::WrapperTLSId _wrapper_key;

    // All thread-local instances.
    std::vector<Wrapper*> _wrappers;

    // Sequence access to _wrappers.
    pthread_mutex_t _wrappers_mutex{};

    // Sequence modifications.
    pthread_mutex_t _modify_mutex{};
};

template <typename T>
class DoublyBufferedDataBthread<T>::Wrapper {
friend class DoublyBufferedDataBthread;
public:
    explicit Wrapper()
            : _control(NULL)
            , _modify_wait(false) {
        pthread_mutex_init(&_mutex, NULL);
        pthread_cond_init(&_cond[0], NULL);
        pthread_cond_init(&_cond[1], NULL);
    }

    ~Wrapper() {
        if (_control != NULL) {
            _control->RemoveWrapper(this);
        }

        WaitReadDone(0);
        WaitReadDone(1);

        pthread_mutex_destroy(&_mutex);
        pthread_cond_destroy(&_cond[0]);
        pthread_cond_destroy(&_cond[1]);
    }

    // _mutex will be locked by the calling pthread and DoublyBufferedDataBthread.
    // Most of the time, no modifications are done, so the mutex is
    // uncontended and fast.
    inline void BeginRead() {
        pthread_mutex_lock(&_mutex);
    }

    // _mutex will be unlocked by the calling pthread and DoublyBufferedDataBthread.
    inline void BeginReadRelease() {
        pthread_mutex_unlock(&_mutex);
    }

    // Thread-local reference count which be protected by _mutex
    // will be decremented by one.
    inline void EndRead(int index) {
        BAIDU_SCOPED_LOCK(_mutex);
        SubRef(index);
        SignalReadCond(index);
    }

    inline void WaitReadDone(int index) {
        BAIDU_SCOPED_LOCK(_mutex);
        int& ref = index == 0 ? _ref[0] : _ref[1];
        while (ref != 0) {
            _modify_wait = true;
            pthread_cond_wait(&_cond[index], &_mutex);
        }
        _modify_wait = false;
    }

    inline void SignalReadCond(int index) {
        if (_ref[index] == 0) {
            pthread_cond_signal(&_cond[index]);
        }
    }

    void AddRef(int index) {
        ++_ref[index];
    }

    void SubRef(int index) {
        --_ref[index];
    }

private:
    DoublyBufferedDataBthread* _control;
    pthread_mutex_t _mutex{};
    // _cond[0] for _ref[0], _cond[1] for _ref[1]
    pthread_cond_t _cond[2];
    // _ref[0] is reference count for _data[0],
    // _ref[1] is reference count for _data[1].
    int _ref[2]{0, 0};
    bool _modify_wait;       // Whether there is a Modify() waiting for _ref0/_ref1.
};

// Called when thread initializes thread-local wrapper.
template <typename T>
typename DoublyBufferedDataBthread<T>::Wrapper* DoublyBufferedDataBthread<T>::AddWrapper(
        typename DoublyBufferedDataBthread<T>::Wrapper* w) {
    if (NULL == w) {
        return NULL;
    }
    if (w->_control == this) {
        return w;
    }
    if (w->_control != NULL) {
        LOG(FATAL) << "Get wrapper from tls but control != this";
        return NULL;
    }
    try {
        w->_control = this;
        BAIDU_SCOPED_LOCK(_wrappers_mutex);
        _wrappers.push_back(w);
    } catch (std::exception& e) {
        return NULL;
    }
    return w;
}

// Called when thread quits.
template <typename T>
void DoublyBufferedDataBthread<T>::RemoveWrapper(
    typename DoublyBufferedDataBthread<T>::Wrapper* w) {
    if (NULL == w) {
        return;
    }
    BAIDU_SCOPED_LOCK(_wrappers_mutex);
    for (size_t i = 0; i < _wrappers.size(); ++i) {
        if (_wrappers[i] == w) {
            _wrappers[i] = _wrappers.back();
            _wrappers.pop_back();
            return;
        }
    }
}

template <typename T>
DoublyBufferedDataBthread<T>::DoublyBufferedDataBthread()
    : _index(0)
    , _wrapper_key(0) {
    _wrappers.reserve(64);
    pthread_mutex_init(&_modify_mutex, NULL);
    pthread_mutex_init(&_wrappers_mutex, NULL);
    _wrapper_key = internal::WrapperTLSGroup<DoublyBufferedDataBthread<T>>::key_create();
    // Initialize _data for some POD types. This is essential for pointer
    // types because they should be Read() as NULL before any Modify().
    if (is_integral<T>::value || is_floating_point<T>::value ||
        is_pointer<T>::value || is_member_function_pointer<T>::value) {
        _data[0] = T();
        _data[1] = T();
    }
}

template <typename T>
DoublyBufferedDataBthread<T>::~DoublyBufferedDataBthread() {
    // User is responsible for synchronizations between Read()/Modify() and
    // this function.
    
    {
        BAIDU_SCOPED_LOCK(_wrappers_mutex);
        for (size_t i = 0; i < _wrappers.size(); ++i) {
            _wrappers[i]->_control = NULL;  // hack: disable removal.
        }
        _wrappers.clear();
    }
    internal::WrapperTLSGroup<DoublyBufferedDataBthread<T>>::key_delete(_wrapper_key);
    _wrapper_key = -1;
    pthread_mutex_destroy(&_modify_mutex);
    pthread_mutex_destroy(&_wrappers_mutex);
}

template <typename T>
int DoublyBufferedDataBthread<T>::Read(
    typename DoublyBufferedDataBthread<T>::ScopedPtr* ptr) {
    Wrapper* p = internal::WrapperTLSGroup<DoublyBufferedDataBthread>::
        get_or_create_tls_data(_wrapper_key);
    Wrapper* w = AddWrapper(p);
    if (BAIDU_LIKELY(w != NULL)) {
        w->BeginRead();
        int index = -1;
        ptr->_data = UnsafeRead(index);
        ptr->_index = index;
        w->AddRef(index);
        ptr->_w = w;
        w->BeginReadRelease();
        return 0;
    }
    return -1;
}

template <typename T>
template <typename Fn>
size_t DoublyBufferedDataBthread<T>::Modify(Fn& fn) {
    // _modify_mutex sequences modifications. Using a separate mutex rather
    // than _wrappers_mutex is to avoid blocking threads calling
    // AddWrapper() or RemoveWrapper() too long. Most of the time, modifications
    // are done by one thread, contention should be negligible.
    BAIDU_SCOPED_LOCK(_modify_mutex);
    int bg_index = !_index.load(butil::memory_order_relaxed);
    // background instance is not accessed by other threads, being safe to
    // modify.
    const size_t ret = fn(_data[bg_index]);
    if (!ret) {
        return 0;
    }

    // Publish, flip background and foreground.
    // The release fence matches with the acquire fence in UnsafeRead() to
    // make readers which just begin to read the new foreground instance see
    // all changes made in fn.
    _index.store(bg_index, butil::memory_order_release);
    bg_index = !bg_index;
    
    // Wait until all threads finishes current reading. When they begin next
    // read, they should see updated _index.
    {
        BAIDU_SCOPED_LOCK(_wrappers_mutex);
        for (size_t i = 0; i < _wrappers.size(); ++i) {
            _wrappers[i]->WaitReadDone(bg_index);
        }
    }

    const size_t ret2 = fn(_data[bg_index]);
    CHECK_EQ(ret2, ret) << "index=" << _index.load(butil::memory_order_relaxed);
    return ret2;
}

template <typename T>
template <typename Fn, typename Arg1>
size_t DoublyBufferedDataBthread<T>::Modify(Fn& fn, const Arg1& arg1) {
    internal::Closure1<T, Fn, Arg1> c(fn, arg1);
    return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1, typename Arg2>
size_t DoublyBufferedDataBthread<T>::Modify(
    Fn& fn, const Arg1& arg1, const Arg2& arg2) {
    internal::Closure2<T, Fn, Arg1, Arg2> c(fn, arg1, arg2);
    return Modify(c);
}

template <typename T>
template <typename Fn>
size_t DoublyBufferedDataBthread<T>::ModifyWithForeground(Fn& fn) {
    internal::WithFG0<T, Fn> c(fn, _data);
    return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1>
size_t DoublyBufferedDataBthread<T>::ModifyWithForeground(Fn& fn, const Arg1& arg1) {
    internal::WithFG1<T, Fn, Arg1> c(fn, _data, arg1);
    return Modify(c);
}

template <typename T>
template <typename Fn, typename Arg1, typename Arg2>
size_t DoublyBufferedDataBthread<T>::ModifyWithForeground(
    Fn& fn, const Arg1& arg1, const Arg2& arg2) {
    internal::WithFG2<T, Fn, Arg1, Arg2> c(fn, _data, arg1, arg2);
    return Modify(c);
}

}  // namespace butil

#endif  // BUTIL_DOUBLY_BUFFERED_DATA_BTHREAD_H
