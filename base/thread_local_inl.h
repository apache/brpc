// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// Inlined implementation of thread_local.h

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Sep 16 12:39:12 CST 2014

#ifndef BRPC_BASE_THREAD_LOCAL_INL_H
#define BRPC_BASE_THREAD_LOCAL_INL_H

namespace base {

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
            base::thread_atexit(delete_object<T>, value);
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

}  // namespace base

#endif  // BRPC_BASE_THREAD_LOCAL_INL_H
