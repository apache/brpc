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

#ifndef BUTIL_DOUBLY_BUFFERED_DATA_H
#define BUTIL_DOUBLY_BUFFERED_DATA_H

#include <deque>
#include <vector>
#include <memory>
#include <pthread.h>
#include "butil/scoped_lock.h"
#include "butil/thread_local.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/type_traits.h"
#include "butil/errno.h"
#include "butil/atomicops.h"
#include "butil/unique_ptr.h"
#include "butil/type_traits.h"

namespace butil {

// This data structure makes Read() almost lock-free by making Modify()
// *much* slower. It's very suitable for implementing LoadBalancers which
// have a lot of concurrent read-only ops from many threads and occasional
// modifications of data. As a side effect, this data structure can store
// a thread-local data for user.
//
// --- `AllowBthreadSuspended=false' ---
// Read(): Begin with a thread-local mutex locked then read the foreground
// instance which will not be changed before the mutex is unlocked. Since the
// mutex is only locked by Modify() with an empty critical section, the
// function is almost lock-free.
//
// Modify(): Modify background instance which is not used by any Read(), flip
// foreground and background, lock thread-local mutexes one by one to make
// sure all existing Read() finish and later Read() see new foreground,
// then modify background(foreground before flip) again.
//
// But, when `AllowBthreadSuspended=false', it is not allowed to suspend bthread
// while reading. Otherwise, it may cause deadlock.
//
//
// --- `AllowBthreadSuspended=true' ---
// It is allowed to suspend bthread while reading.
// It is not allowed to use non-Void TLS.
// If bthread will not be suspended while reading, it also makes Read() almost
// lock-free by making Modify() *much* slower.
// If bthread will be suspended while reading, there is competition among
// bthreads using the same Wrapper.
//
// Read(): Begin with thread-local reference count of foreground instance
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

class Void { };

template <typename T> struct IsVoid : false_type { };
template <> struct IsVoid<Void> : true_type { };

template <typename T, typename TLS = Void, bool AllowBthreadSuspended = false>
class DoublyBufferedData {
    class Wrapper;
    class WrapperTLSGroup;
    typedef int WrapperTLSId;
    typedef std::shared_ptr<Wrapper> WrapperSharedPtr;
    typedef std::weak_ptr<Wrapper> WrapperWeakPtr;
public:
    class ScopedPtr {
    friend class DoublyBufferedData;
    public:
        ScopedPtr() : _data(NULL), _index(0), _w(NULL) {}
        ~ScopedPtr() {
            if (_w) {
                if (AllowBthreadSuspended) {
                    _w->EndRead(_index);
                } else {
                    _w->EndRead();
                }
            }
        }
        const T* get() const { return _data; }
        const T& operator*() const { return *_data; }
        const T* operator->() const { return _data; }
        TLS& tls() { return _w->user_tls(); }
        
    private:
        DISALLOW_COPY_AND_ASSIGN(ScopedPtr);
        const T* _data;
        // Index of foreground instance used by ScopedPtr.
        int _index;
        WrapperSharedPtr _w;
    };
    
    DoublyBufferedData();
    ~DoublyBufferedData();

    // Put foreground instance into ptr. The instance will not be changed until
    // ptr is destructed.
    // This function is not blocked by Read() and Modify() in other threads.
    // Returns 0 on success, -1 otherwise.
    int Read(ScopedPtr* ptr);

    // `fn(const T&)' will be called with foreground instance.
    // This function is not blocked by Read() and Modify() in other threads.
    // Returns 0 on success, otherwise on error.
    template<typename Fn>
    int Read(Fn&& fn);

    // Modify background and foreground instances. fn(T&, ...) will be called
    // twice. Modify() from different threads are exclusive from each other.
    // NOTE: Call same series of fn to different equivalent instances should
    // result in equivalent instances, otherwise foreground and background
    // instance will be inconsistent.
    template <typename Fn, typename... Args>
    size_t Modify(Fn&& fn, Args&&... args);

    // fn(T& background, const T& foreground, ...) will be called to background
    // and foreground instances respectively.
    template <typename Fn, typename... Args>
    size_t ModifyWithForeground(Fn&& fn, Args&&... args);
    
private:
    const T* UnsafeRead() const {
        return _data + _index.load(butil::memory_order_acquire);
    }

    const T* UnsafeRead(int& index) const {
        index = _index.load(butil::memory_order_acquire);
        return _data + index;
    }

    WrapperSharedPtr GetWrapper();

    // Foreground and background void.
    T _data[2];

    // Index of foreground instance.
    butil::atomic<int> _index;

    // Key to access thread-local wrappers.
    WrapperTLSId _wrapper_key;

    // All thread-local instances.
    std::vector<WrapperWeakPtr> _wrappers;

    // Sequence access to _wrappers.
    pthread_mutex_t _wrappers_mutex{};

    // Sequence modifications.
    pthread_mutex_t _modify_mutex{};
};

template <typename T, typename TLS>
class DoublyBufferedDataWrapperBase {
public:
    TLS& user_tls() { return _user_tls; }
protected:
    TLS _user_tls;
};

template <typename T>
class DoublyBufferedDataWrapperBase<T, Void> {
};

// Use pthread_key store data limits by _SC_THREAD_KEYS_MAX.
// WrapperTLSGroup can store Wrapper in thread local storage.
// WrapperTLSGroup will destruct Wrapper data when thread exits,
// other times only reset Wrapper inner structure.
template <typename T, typename TLS, bool AllowBthreadSuspended>
class DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup {
public:
    const static size_t RAW_BLOCK_SIZE = 4096;
    const static size_t ELEMENTS_PER_BLOCK =
        RAW_BLOCK_SIZE / sizeof(WrapperSharedPtr) > 0 ?
            RAW_BLOCK_SIZE / sizeof(WrapperSharedPtr) : 1;

    struct BAIDU_CACHELINE_ALIGNMENT ThreadBlock {
        WrapperSharedPtr at(size_t offset) {
            if (NULL == _data[offset]) {
                _data[offset] = std::make_shared<Wrapper>();
            }
            return _data[offset];
        };

    private:
        WrapperSharedPtr _data[ELEMENTS_PER_BLOCK];
    };

    static WrapperTLSId key_create() {
        BAIDU_SCOPED_LOCK(_s_mutex);
        WrapperTLSId id = 0;
        if (!_get_free_ids().empty()) {
            id = _get_free_ids().back();
            _get_free_ids().pop_back();
        } else {
            id = _s_id++;
        }
        return id;
    }

    static int key_delete(WrapperTLSId id) {
        BAIDU_SCOPED_LOCK(_s_mutex);
        if (id < 0 || id >= _s_id) {
            errno = EINVAL;
            return -1;
        }
        _get_free_ids().push_back(id);
        return 0;
    }

    static WrapperSharedPtr get_or_create_tls_data(WrapperTLSId id) {
        if (BAIDU_UNLIKELY(id < 0)) {
            CHECK(false) << "Invalid id=" << id;
            return NULL;
        }
        if (_s_tls_blocks == NULL) {
            _s_tls_blocks = new std::vector<ThreadBlock*>;
            butil::thread_atexit(_destroy_tls_blocks);
        }
        const size_t block_id = (size_t)id / ELEMENTS_PER_BLOCK;
        if (block_id >= _s_tls_blocks->size()) {
            // The 32ul avoid pointless small resizes.
            _s_tls_blocks->resize(std::max(block_id + 1, 32ul));
        }
        ThreadBlock* tb = (*_s_tls_blocks)[block_id];
        if (tb == NULL) {
            tb = new ThreadBlock;
            (*_s_tls_blocks)[block_id] = tb;
        }
        return tb->at(id - block_id * ELEMENTS_PER_BLOCK);
    }

private:
    static void _destroy_tls_blocks() {
        if (!_s_tls_blocks) {
            return;
        }
        for (size_t i = 0; i < _s_tls_blocks->size(); ++i) {
            delete (*_s_tls_blocks)[i];
        }
        delete _s_tls_blocks;
        _s_tls_blocks = NULL;
    }

    inline static std::deque<WrapperTLSId>& _get_free_ids() {
        if (BAIDU_UNLIKELY(!_s_free_ids)) {
            _s_free_ids = new (std::nothrow) std::deque<WrapperTLSId>();
            RELEASE_ASSERT(_s_free_ids);
        }
        return *_s_free_ids;
    }

private:
    static pthread_mutex_t _s_mutex;
    static WrapperTLSId _s_id;
    static std::deque<WrapperTLSId>* _s_free_ids;
    static __thread std::vector<ThreadBlock*>* _s_tls_blocks;
};

template <typename T, typename TLS, bool AllowBthreadSuspended>
pthread_mutex_t DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup::_s_mutex = PTHREAD_MUTEX_INITIALIZER;

template <typename T, typename TLS, bool AllowBthreadSuspended>
std::deque<typename DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSId>*
        DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup::_s_free_ids = NULL;

template <typename T, typename TLS, bool AllowBthreadSuspended>
typename DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSId
        DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup::_s_id = 0;

template <typename T, typename TLS, bool AllowBthreadSuspended>
__thread std::vector<typename DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup::ThreadBlock*>*
        DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperTLSGroup::_s_tls_blocks = NULL;

template <typename T, typename TLS, bool AllowBthreadSuspended>
class BAIDU_CACHELINE_ALIGNMENT DoublyBufferedData<T, TLS, AllowBthreadSuspended>::Wrapper
    : public DoublyBufferedDataWrapperBase<T, TLS> {
friend class DoublyBufferedData;
public:
    explicit Wrapper()
        : _control(NULL)
        , _modify_wait(false) {
        pthread_mutex_init(&_mutex, NULL);
        if (AllowBthreadSuspended) {
            pthread_cond_init(&_cond[0], NULL);
            pthread_cond_init(&_cond[1], NULL);
        }
    }
    
    ~Wrapper() {
        if (AllowBthreadSuspended) {
            WaitReadDone(0);
            WaitReadDone(1);

            pthread_cond_destroy(&_cond[0]);
            pthread_cond_destroy(&_cond[1]);
        }

        pthread_mutex_destroy(&_mutex);
    }

    // _mutex will be locked by the calling pthread and DoublyBufferedData.
    // Most of the time, no modifications are done, so the mutex is
    // uncontended and fast.
    inline void BeginRead() {
        pthread_mutex_lock(&_mutex);
    }

    // For `AllowBthreadSuspended=true'.
    inline void BeginReadRelease() {
        pthread_mutex_unlock(&_mutex);
    }

    inline void EndRead() {
        pthread_mutex_unlock(&_mutex);
    }

    // For `AllowBthreadSuspended=true'.
    // Thread-local reference count which be protected by _mutex
    // will be decremented by one.
    inline void EndRead(int index) {
        BAIDU_SCOPED_LOCK(_mutex);
        SubRef(index);
        SignalReadCond(index);
    }

    inline void WaitReadDone() {
        BAIDU_SCOPED_LOCK(_mutex);
    }

    // For `AllowBthreadSuspended=true'.
    // Wait until all read of foreground instance done.
    inline void WaitReadDone(int index) {
        BAIDU_SCOPED_LOCK(_mutex);
        int& ref = index == 0 ? _ref[0] : _ref[1];
        while (ref != 0) {
            _modify_wait = true;
            pthread_cond_wait(&_cond[index], &_mutex);
        }
        _modify_wait = false;
    }

    // For `AllowBthreadSuspended=true'.
    inline void SignalReadCond(int index) {
        if (_ref[index] == 0 && _modify_wait) {
            pthread_cond_signal(&_cond[index]);
        }
    }

    // For `AllowBthreadSuspended=true'.
    void AddRef(int index) {
        ++_ref[index];
    }

    // For `AllowBthreadSuspended=true'.
    void SubRef(int index) {
        --_ref[index];
    }
    
private:
    DoublyBufferedData* _control;
    pthread_mutex_t _mutex{};
    // For `AllowBthreadSuspended=true'.
    // _cond[0] for _ref[0], _cond[1] for _ref[1]
    pthread_cond_t _cond[2]{};
    // For `AllowBthreadSuspended=true'.
    // _ref[0] is reference count for _data[0],
    // _ref[1] is reference count for _data[1].
    int _ref[2]{0, 0};
    // For `AllowBthreadSuspended=true'.
    // Whether there is a Modify() waiting for _ref0/_ref1.
    bool _modify_wait;
};

// Called when thread initializes thread-local wrapper.
template <typename T, typename TLS, bool AllowBthreadSuspended>
typename DoublyBufferedData<T, TLS, AllowBthreadSuspended>::WrapperSharedPtr
DoublyBufferedData<T, TLS, AllowBthreadSuspended>::GetWrapper() {
    WrapperSharedPtr w = WrapperTLSGroup::get_or_create_tls_data(_wrapper_key);
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
        // The chance to remove expired weak_ptr.
        _wrappers.erase(
            std::remove_if(_wrappers.begin(), _wrappers.end(),
                           [](const WrapperWeakPtr& w) {
                    return w.expired();
                }),
            _wrappers.end());
    } catch (std::exception& e) {
        return NULL;
    }
    return w;
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
DoublyBufferedData<T, TLS, AllowBthreadSuspended>::DoublyBufferedData()
    : _index(0)
    , _wrapper_key(0) {
    BAIDU_CASSERT(!(AllowBthreadSuspended && !IsVoid<TLS>::value),
                  "Forbidden to allow bthread suspended with non-Void TLS");

    _wrappers.reserve(64);
    pthread_mutex_init(&_modify_mutex, NULL);
    pthread_mutex_init(&_wrappers_mutex, NULL);
    _wrapper_key = WrapperTLSGroup::key_create();
    // Initialize _data for some POD types. This is essential for pointer
    // types because they should be Read() as NULL before any Modify().
    if (is_integral<T>::value || is_floating_point<T>::value ||
        is_pointer<T>::value || is_member_function_pointer<T>::value) {
        _data[0] = T();
        _data[1] = T();
    }
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
DoublyBufferedData<T, TLS, AllowBthreadSuspended>::~DoublyBufferedData() {
    // User is responsible for synchronizations between Read()/Modify() and
    // this function.
    
    {
        BAIDU_SCOPED_LOCK(_wrappers_mutex);
        for (size_t i = 0; i < _wrappers.size(); ++i) {
            WrapperSharedPtr w = _wrappers[i].lock();
            if (NULL != w) {
                w->_control = NULL;  // hack: disable removal.
            }
        }
        _wrappers.clear();
    }
    WrapperTLSGroup::key_delete(_wrapper_key);
    _wrapper_key = -1;
    pthread_mutex_destroy(&_modify_mutex);
    pthread_mutex_destroy(&_wrappers_mutex);
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
int DoublyBufferedData<T, TLS, AllowBthreadSuspended>::Read(
    typename DoublyBufferedData<T, TLS, AllowBthreadSuspended>::ScopedPtr* ptr) {
    WrapperSharedPtr w = GetWrapper();
    if (BAIDU_UNLIKELY(w == NULL)) {
        return -1;
    }

    if (AllowBthreadSuspended) {
        // Use reference count instead of mutex to indicate read of
        // foreground instance, so during the read process, there is
        // no need to lock mutex and bthread is allowed to be suspended.
        w->BeginRead();
        // UnsafeRead will update ptr->_index
        ptr->_data = UnsafeRead(ptr->_index);
        w->AddRef(ptr->_index);
        w->BeginReadRelease();
    } else {
        w->BeginRead();
        ptr->_data = UnsafeRead();
    }
    ptr->_w.swap(w);
    return 0;
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
template <typename Fn>
int DoublyBufferedData<T, TLS, AllowBthreadSuspended>::Read(Fn&& fn) {
    BAIDU_CASSERT((is_result_void<Fn, const T&>::value),
                  "Fn must accept `const T&' and return void");
    ScopedPtr ptr;
    if (Read(&ptr) != 0) {
        return -1;
    }
    fn(*ptr);
    return 0;
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
template <typename Fn, typename... Args>
size_t DoublyBufferedData<T, TLS, AllowBthreadSuspended>::Modify(Fn&& fn, Args&&... args) {
    // _modify_mutex sequences modifications. Using a separate mutex rather
    // than _wrappers_mutex is to avoid blocking threads calling
    // GetWrapper() too long. Most of the time, modifications
    // are done by one thread, contention should be negligible.
    BAIDU_SCOPED_LOCK(_modify_mutex);
    int bg_index = !_index.load(butil::memory_order_relaxed);
    // background instance is not accessed by other threads, being safe to
    // modify.
    const size_t ret = fn(_data[bg_index], std::forward<Args>(args)...);
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
        // The chance to remove expired weak_ptr.
        _wrappers.erase(
            std::remove_if(_wrappers.begin(), _wrappers.end(),
                           [bg_index](const WrapperWeakPtr& weak) {
                WrapperSharedPtr w = weak.lock();
                bool expired = NULL == w;
                if (!expired) {
                    // Notify all threads waiting for read done.
                    if (AllowBthreadSuspended) {
                        w->WaitReadDone(bg_index);
                    } else {
                        w->WaitReadDone();
                    }
               }
                // Remove expired weak_ptr.
                return expired;
            }),
            _wrappers.end());
    }

    const size_t ret2 = fn(_data[bg_index], std::forward<Args>(args)...);
    CHECK_EQ(ret2, ret) << "index=" << _index.load(butil::memory_order_relaxed);
    return ret2;
}

template <typename T, typename TLS, bool AllowBthreadSuspended>
template <typename Fn, typename... Args>
size_t DoublyBufferedData<T, TLS, AllowBthreadSuspended>::ModifyWithForeground(Fn&& fn, Args&&... args) {
    return Modify([this, &fn](T& bg, Args&&... args) {
        return fn(bg, (const T&)_data[&bg == _data], std::forward<Args>(args)...);
    }, std::forward<Args>(args)...);
}

}  // namespace butil

#endif  // BUTIL_DOUBLY_BUFFERED_DATA_H
