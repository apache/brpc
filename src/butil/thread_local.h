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

#include <memory>                   // std::addressof
#include <new>                      // std::nothrow
#include "butil/macros.h"

#ifdef _MSC_VER
#define BAIDU_THREAD_LOCAL __declspec(thread)
#else
#define BAIDU_THREAD_LOCAL __thread
#endif  // _MSC_VER

// Define a pthread-local variable accessed through name():
//   BUTIL_THREAD_LOCAL(int, counter, 0);
//   ++counter();
//   counter() = 10;
// Prefix with static for internal linkage or a static class member, or inline
// for a definition in a header. A declaration in another translation unit is
// simply `type& name();`. Use a type alias for arrays or types containing commas.
//
// A bthread can resume on a different pthread. Keep TLS address resolution in
// a non-inlined function so the compiler cannot hoist it across a suspension.
// The opaque asm output also prevents interprocedural optimization (including
// LTO) from treating the returned address as invariant. A memory barrier alone,
// or an inline function taking &tls as its argument, does not achieve this.
//
// The returned reference/pointer is only valid for the current pthread: never
// retain it across a call that may suspend. In particular, evaluate a possibly
// suspending RHS before assigning it to name(). Initializers must not suspend.
// Constant-initialized trivial types have no dynamic initialization/exit hook.
#define BUTIL_THREAD_LOCAL(type, name, ...)                                  \
    __attribute__((noinline, unused)) type& name() {                          \
        static thread_local type tls_value = __VA_ARGS__;                    \
        type* tls_ptr = std::addressof(tls_value);                            \
        asm volatile("" : "+r"(tls_ptr) : : "memory");                       \
        return *tls_ptr;                                                     \
    }

// Legacy interfaces retained for source compatibility. Prefer BUTIL_THREAD_LOCAL
// for new code: it hides the storage and needs no separate get/set/ptr macros.
#define BAIDU_VOLATILE_THREAD_LOCAL(type, var_name, default_value)             \
  BAIDU_THREAD_LOCAL type var_name = default_value;                            \
  __attribute__((noinline, unused)) type get_##var_name(void) {                \
    asm volatile("");                                                          \
    return var_name;                                                           \
  }                                                                            \
  __attribute__((noinline, unused)) type *get_ptr_##var_name(void) {           \
    type *ptr = &var_name;                                                     \
    asm volatile("" : "+rm"(ptr));                                             \
    return ptr;                                                                \
  }                                                                            \
  __attribute__((noinline, unused)) void set_##var_name(type v) {              \
    asm volatile("");                                                          \
    var_name = v;                                                              \
  }

 #define STATIC_MEMBER_BAIDU_VOLATILE_THREAD_LOCAL(type, var_name)              \
   static BAIDU_THREAD_LOCAL type var_name;                                     \
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

// Always use the accessors to prevent TLS address caching across suspension,
// regardless of compiler or architecture.
#define BAIDU_GET_VOLATILE_THREAD_LOCAL(var_name) get_##var_name()
#define BAIDU_GET_PTR_VOLATILE_THREAD_LOCAL(var_name) get_ptr_##var_name()
#define BAIDU_SET_VOLATILE_THREAD_LOCAL(var_name, value) set_##var_name(value)

#define EXTERN_BAIDU_VOLATILE_THREAD_LOCAL(type, var_name)                     \
    extern type get_##var_name(void);                                          \
    extern type *get_ptr_##var_name(void);                                     \
    extern void set_##var_name(type v)

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
