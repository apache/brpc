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

#ifndef BUTIL_DOUBLY_BUFFERED_DATA_INTERNAL_H
#define BUTIL_DOUBLY_BUFFERED_DATA_INTERNAL_H

#include <deque>
#include <vector>                                       // std::vector
#include <pthread.h>
#include "butil/scoped_lock.h"
#include "butil/thread_local.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/type_traits.h"
#include "butil/errno.h"
#include "butil/atomicops.h"

namespace butil {
namespace internal {


// ----- Wrapper TLS -----
typedef int WrapperTLSId;

// Use pthread_key store data limits by _SC_THREAD_KEYS_MAX.
// WrapperTLSGroup can store Wrapper in thread local storage.
// WrapperTLSGroup will destruct Wrapper data when thread exits,
// other times only reset Wrapper inner structure.
template <typename DBD>
class WrapperTLSGroup {
public:
    const static size_t RAW_BLOCK_SIZE = 4096;
    const static size_t ELEMENTS_PER_BLOCK = (RAW_BLOCK_SIZE +
        sizeof(typename DBD::DataType) - 1) / sizeof(typename DBD::DataType);

    struct BAIDU_CACHELINE_ALIGNMENT ThreadBlock {
        inline typename DBD::Wrapper* at(size_t offset) {
            return _data + offset;
        };

    private:
        typename DBD::Wrapper _data[ELEMENTS_PER_BLOCK];
    };

    inline static WrapperTLSId key_create() {
        BAIDU_SCOPED_LOCK(_s_mutex);
        WrapperTLSId id;
        if (!_get_free_ids().empty()) {
            id = _get_free_ids().back();
            _get_free_ids().pop_back();
        } else {
            id = _s_id++;
        }
        return id;
    }

    inline static int key_delete(WrapperTLSId id) {
        BAIDU_SCOPED_LOCK(_s_mutex);
        if (id < 0 || id >= _s_id) {
            errno = EINVAL;
            return -1;
        }
        _get_free_ids().push_back(id);
        return 0;
    }

    inline static typename DBD::Wrapper* get_or_create_tls_data(WrapperTLSId id) {
        if (BAIDU_UNLIKELY(id < 0)) {
            CHECK(false) << "Invalid id=" << id;
            return NULL;
        }
        if (_s_tls_blocks == NULL) {
            _s_tls_blocks = new (std::nothrow) std::vector<ThreadBlock*>;
            if (BAIDU_UNLIKELY(_s_tls_blocks == NULL)) {
                LOG(FATAL) << "Fail to create vector, " << berror();
                return NULL;
            }
            butil::thread_atexit(_destroy_tls_blocks);
        }
        const size_t block_id = (size_t)id / ELEMENTS_PER_BLOCK;
        if (block_id >= _s_tls_blocks->size()) {
            // The 32ul avoid pointless small resizes.
            _s_tls_blocks->resize(std::max(block_id + 1, 32ul));
        }
        ThreadBlock* tb = (*_s_tls_blocks)[block_id];
        if (tb == NULL) {
            auto new_block = new (std::nothrow) ThreadBlock;
            if (BAIDU_UNLIKELY(new_block == NULL)) {
                return NULL;
            }
            tb = new_block;
            (*_s_tls_blocks)[block_id] = new_block;
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
            if (!_s_free_ids) {
                abort();
            }
        }
        return *_s_free_ids;
    }

private:
    static pthread_mutex_t _s_mutex;
    static WrapperTLSId _s_id;
    static std::deque<WrapperTLSId>* _s_free_ids;
    static __thread std::vector<ThreadBlock*>* _s_tls_blocks;
};

template <typename DBD>
pthread_mutex_t WrapperTLSGroup<DBD>::_s_mutex = PTHREAD_MUTEX_INITIALIZER;

template <typename DBD>
std::deque<WrapperTLSId>* WrapperTLSGroup<DBD>::_s_free_ids = NULL;

template <typename DBD>
WrapperTLSId WrapperTLSGroup<DBD>::_s_id = 0;

template <typename DBD>
__thread std::vector<typename WrapperTLSGroup<DBD>::ThreadBlock*>*
    WrapperTLSGroup<DBD>::_s_tls_blocks = NULL;
// ----- Wrapper TLS -----


template <typename T, typename Fn>
struct WithFG0 {
    WithFG0(Fn& fn, T* data) : _fn(fn), _data(data) { }
    size_t operator()(T& bg) {
        return _fn(bg, (const T&)_data[&bg == _data]);
    }
private:
    Fn& _fn;
    T* _data;
};

template <typename T, typename Fn, typename Arg1>
struct WithFG1 {
    WithFG1(Fn& fn, T* data, const Arg1& arg1)
        : _fn(fn), _data(data), _arg1(arg1) {}
    size_t operator()(T& bg) {
        return _fn(bg, (const T&)_data[&bg == _data], _arg1);
    }
private:
    Fn& _fn;
    T* _data;
    const Arg1& _arg1;
};

template <typename T, typename Fn, typename Arg1, typename Arg2>
struct WithFG2 {
    WithFG2(Fn& fn, T* data, const Arg1& arg1, const Arg2& arg2)
        : _fn(fn), _data(data), _arg1(arg1), _arg2(arg2) {}
    size_t operator()(T& bg) {
        return _fn(bg, (const T&)_data[&bg == _data], _arg1, _arg2);
    }
private:
    Fn& _fn;
    T* _data;
    const Arg1& _arg1;
    const Arg2& _arg2;
};

template <typename T, typename Fn, typename Arg1>
struct Closure1 {
    Closure1(Fn& fn, const Arg1& arg1) : _fn(fn), _arg1(arg1) {}
    size_t operator()(T& bg) { return _fn(bg, _arg1); }
private:
    Fn& _fn;
    const Arg1& _arg1;
};

template <typename T, typename Fn, typename Arg1, typename Arg2>
struct Closure2 {
    Closure2(Fn& fn, const Arg1& arg1, const Arg2& arg2)
        : _fn(fn), _arg1(arg1), _arg2(arg2) {}
    size_t operator()(T& bg) { return _fn(bg, _arg1, _arg2); }
private:
    Fn& _fn;
    const Arg1& _arg1;
    const Arg2& _arg2;
};

}

}  // namespace butil

#endif  // BUTIL_DOUBLY_BUFFERED_DATA_INTERNAL_H
