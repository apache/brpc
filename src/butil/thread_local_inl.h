// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Sep 16 12:39:12 CST 2014

#ifndef BUTIL_THREAD_LOCAL_INL_H
#define BUTIL_THREAD_LOCAL_INL_H

namespace butil {

namespace detail {

template <typename T>
class ThreadLocalHelper {
public:
    inline static T* get() {
        if (__builtin_expect(value != NULL, 1)) {
            return value;
        }
        value = new (std::nothrow) T;
        if (value != NULL) {
            butil::thread_atexit(delete_object<T>, value);
        }
        return value;
    }
    static BAIDU_THREAD_LOCAL T* value;
};

template <typename T> BAIDU_THREAD_LOCAL T* ThreadLocalHelper<T>::value = NULL;

}  // namespace detail

template <typename T> inline T* get_thread_local() {
    return detail::ThreadLocalHelper<T>::get();
}

}  // namespace butil

#endif  // BUTIL_THREAD_LOCAL_INL_H
