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

#include <errno.h>                       // errno
#include <pthread.h>                     // pthread_key_t
#include <stdio.h>
#include <algorithm>                     // std::find
#include <vector>                        // std::vector
#include <stdlib.h>                      // abort, atexit

namespace butil {
namespace detail {

class ThreadExitHelper {
public:
    typedef void (*Fn)(void*);
    typedef std::pair<Fn, void*> Pair;
    
    ~ThreadExitHelper() {
        // Call function reversely.
        while (!_fns.empty()) {
            Pair back = _fns.back();
            _fns.pop_back();
            // Notice that _fns may be changed after calling Fn.
            back.first(back.second);
        }
    }

    int add(Fn fn, void* arg) {
        try {
            if (_fns.capacity() < 16) {
                _fns.reserve(16);
            }
            _fns.emplace_back(fn, arg);
        } catch (...) {
            errno = ENOMEM;
            return -1;
        }
        return 0;
    }

    void remove(Fn fn, void* arg) {
        std::vector<Pair>::iterator
            it = std::find(_fns.begin(), _fns.end(), std::make_pair(fn, arg));
        if (it != _fns.end()) {
            std::vector<Pair>::iterator ite = it + 1;
            for (; ite != _fns.end() && ite->first == fn && ite->second == arg;
                  ++ite) {}
            _fns.erase(it, ite);
        }
    }

private:
    std::vector<Pair> _fns;
};

static pthread_key_t thread_atexit_key;
static pthread_once_t thread_atexit_once = PTHREAD_ONCE_INIT;

static void delete_thread_exit_helper(void* arg) {
    delete static_cast<ThreadExitHelper*>(arg);
}

static void helper_exit_global() {
    detail::ThreadExitHelper* h = 
        (detail::ThreadExitHelper*)pthread_getspecific(detail::thread_atexit_key);
    if (h) {
        pthread_setspecific(detail::thread_atexit_key, NULL);
        delete h;
    }
}

static void make_thread_atexit_key() {
    if (pthread_key_create(&thread_atexit_key, delete_thread_exit_helper) != 0) {
        fprintf(stderr, "Fail to create thread_atexit_key, abort\n");
        abort();
    }
    // If caller is not pthread, delete_thread_exit_helper will not be called.
    // We have to rely on atexit().
    atexit(helper_exit_global);
}

detail::ThreadExitHelper* get_or_new_thread_exit_helper() {
    pthread_once(&detail::thread_atexit_once, detail::make_thread_atexit_key);

    detail::ThreadExitHelper* h =
        (detail::ThreadExitHelper*)pthread_getspecific(detail::thread_atexit_key);
    if (NULL == h) {
        h = new (std::nothrow) detail::ThreadExitHelper;
        if (NULL != h) {
            pthread_setspecific(detail::thread_atexit_key, h);
        }
    }
    return h;
}

detail::ThreadExitHelper* get_thread_exit_helper() {
    pthread_once(&detail::thread_atexit_once, detail::make_thread_atexit_key);
    return (detail::ThreadExitHelper*)pthread_getspecific(detail::thread_atexit_key);
}

static void call_single_arg_fn(void* fn) {
    ((void (*)())fn)();
}

}  // namespace detail

int thread_atexit(void (*fn)(void*), void* arg) {
    if (NULL == fn) {
        errno = EINVAL;
        return -1;
    }
    detail::ThreadExitHelper* h = detail::get_or_new_thread_exit_helper();
    if (h) {
        return h->add(fn, arg);
    }
    errno = ENOMEM;
    return -1;
}

int thread_atexit(void (*fn)()) {
    if (NULL == fn) {
        errno = EINVAL;
        return -1;
    }
    return thread_atexit(detail::call_single_arg_fn, (void*)fn);
}

void thread_atexit_cancel(void (*fn)(void*), void* arg) {
    if (fn != NULL) {
        detail::ThreadExitHelper* h = detail::get_thread_exit_helper();
        if (h) {
            h->remove(fn, arg);
        }
    }
}

void thread_atexit_cancel(void (*fn)()) {
    if (NULL != fn) {
        thread_atexit_cancel(detail::call_single_arg_fn, (void*)fn);
    }
}

}  // namespace butil
