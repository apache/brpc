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

#ifndef BVAR_DETAIL_EXPOSED_REF_H_
#define BVAR_DETAIL_EXPOSED_REF_H_

#include <memory>
#include "butil/macros.h"
#include "butil/scoped_lock.h"
#include "butil/synchronization/condition_variable.h"

namespace bvar {
namespace detail {

// Indirection layer shared by Variable and MVariableBase that lets concurrent
// readers (describe_exposed() / dump_exposed()) access an exposed object outside
// the global map lock. That lock is a pthread mutex and must not wrap user
// callbacks which may yield the bthread, otherwise it deadlocks (see
// https://github.com/apache/brpc/issues/2888 for details).
//
// Protocol:
//   - A reader increments the reference via acquire() while still holding the
//     global map lock (so it is serialized with the owner's erase from the
//     map), then releases the map lock, uses the returned pointer outside the
//     lock, and finally calls release().
//   - The owner's hide() erases itself from the map and then calls hide_and_wait(),
//     which blocks until all references acquired before this point are released,
//     guaranteeing the owner stays alive throughout the reader's use. The handle
//     is single-use: once hidden it stays hidden, and the owner creates a fresh
//     one on re-expose.
template <typename T>
class ExposedRef {
public:
    explicit ExposedRef(T* obj)
        : _cond(&_mutex), _obj(obj), _nref(0), _hidden(false) {}

    DISALLOW_COPY_AND_ASSIGN(ExposedRef);

    // Must be called while holding the global map lock. Returns nullptr if the
    // owner is being hidden/destructed. On a non-nullptr return the caller must
    // call release() once it finishes using the pointer.
    T* acquire() {
        BAIDU_SCOPED_LOCK(_mutex);
        if (_hidden) {
            return nullptr;
        }
        ++_nref;
        return _obj;
    }

    void release() {
        BAIDU_SCOPED_LOCK(_mutex);
        if (--_nref == 0 && _hidden) {
            _cond.Broadcast();
        }
    }

    // Called by the owner's hide() after erasing itself from the map. Blocks
    // until all references acquired before this point are released.
    void hide_and_wait() {
        BAIDU_SCOPED_LOCK(_mutex);
        _hidden = true;
        while (_nref > 0) {
            _cond.Wait();
        }
    }

private:
    butil::Mutex _mutex;
    butil::ConditionVariable _cond;
    T* _obj;
    int _nref;
    bool _hidden;
};

template <typename T>
using SharedExposedRef = std::shared_ptr<ExposedRef<T>>;

template <typename T>
SharedExposedRef<T> make_exposed_ref(T* obj) {
    return std::make_shared<ExposedRef<T>>(obj);
}

}  // namespace detail
}  // namespace bvar

#endif  // BVAR_DETAIL_EXPOSED_REF_H_
