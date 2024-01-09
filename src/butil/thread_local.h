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

// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BUTIL_THREAD_LOCAL_H
#define BUTIL_THREAD_LOCAL_H

#include <new>                      // std::nothrow
#include <cstddef>                  // NULL
#include "butil/macros.h"            

#ifdef _MSC_VER
#define BAIDU_THREAD_LOCAL __declspec(thread)
#else
#define BAIDU_THREAD_LOCAL __thread
#endif  // _MSC_VER

#define BAIDU_VOLATILE_THREAD_LOCAL(type, var_name, default_value)             \
  BAIDU_THREAD_LOCAL type var_name = default_value;                            \
  static __attribute__((noinline, unused)) type get_##var_name(void) {         \
    asm volatile("");                                                          \
    return var_name;                                                           \
  }                                                                            \
  static __attribute__((noinline, unused)) type *get_ptr_##var_name(void) {    \
    type *ptr = &var_name;                                                     \
    asm volatile("" : "+rm"(ptr));                                             \
    return ptr;                                                                \
  }                                                                            \
  static __attribute__((noinline, unused)) void set_##var_name(type v) {       \
    asm volatile("");                                                          \
    var_name = v;                                                              \
  }

#if (defined (__aarch64__) && defined (__GNUC__)) || defined(__clang__)
// GNU compiler under aarch and Clang compiler is incorrectly caching the 
// address of thread_local variables across a suspend-point. The following
// macros used to disable the volatile thread local access optimization.
#define BAIDU_GET_VOLATILE_THREAD_LOCAL(var_name) get_##var_name()
#define BAIDU_GET_PTR_VOLATILE_THREAD_LOCAL(var_name) get_ptr_##var_name()
#define BAIDU_SET_VOLATILE_THREAD_LOCAL(var_name, value) set_##var_name(value)
#else
#define BAIDU_GET_VOLATILE_THREAD_LOCAL(var_name) var_name
#define BAIDU_GET_PTR_VOLATILE_THREAD_LOCAL(var_name) &##var_name
#define BAIDU_SET_VOLATILE_THREAD_LOCAL(var_name, value) var_name = value
#endif

namespace butil {

// Get a thread-local object typed T. The object will be default-constructed
// at the first call to this function, and will be deleted when thread
// exits.
template <typename T> inline T* get_thread_local();

// |fn| or |fn(arg)| will be called at caller's exit. If caller is not a 
// thread, fn will be called at program termination. Calling sequence is LIFO:
// last registered function will be called first. Duplication of functions 
// are not checked. This function is often used for releasing thread-local
// resources declared with __thread which is much faster than
// pthread_getspecific or boost::thread_specific_ptr.
// Returns 0 on success, -1 otherwise and errno is set.
int thread_atexit(void (*fn)());
int thread_atexit(void (*fn)(void*), void* arg);

// Remove registered function, matched functions will not be called.
void thread_atexit_cancel(void (*fn)());
void thread_atexit_cancel(void (*fn)(void*), void* arg);

// Delete the typed-T object whose address is `arg'. This is a common function
// to thread_atexit.
template <typename T> void delete_object(void* arg) {
    delete static_cast<T*>(arg);
}

}  // namespace butil

#include "thread_local_inl.h"

#endif  // BUTIL_THREAD_LOCAL_H
