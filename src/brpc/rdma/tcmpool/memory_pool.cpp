// Copyright (c) 2018 Baidu Inc.
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

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#include "memory_pool.h"

#ifdef TCMPOOL_HUGEPAGE
extern "C" {
#include <hugetlbfs.h>
}
#endif
#include <pthread.h>
#include <stdlib.h>
#include <cstdlib>
#include <new>

#include "butil/thread_local.h"
#include "brpc/rdma/tcmpool/central_cache.h"
#include "brpc/rdma/tcmpool/const.h"
#include "brpc/rdma/tcmpool/page_heap.h"
#include "brpc/rdma/tcmpool/thread_cache.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

static bool s_user_specified = false;
static void* s_addr = NULL;
static size_t s_size = 0;
static PageHeap* s_ph = NULL;
static CentralCache* s_cc = NULL;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread ThreadCache* s_tc = NULL;

static void on_exit() {
    delete s_cc;
    delete s_ph;
    if (s_addr && !s_user_specified) {
#ifdef TCMPOOL_HUGEPAGE
        free_hugepage_region(s_addr);
#else
        free(s_addr);
#endif
    }
}

static void release_tc() {
    if (s_tc) {
        s_tc->release();
        delete s_tc;
        s_tc = NULL;
    }
}

static ThreadCache* get_tc(CentralCache* cc) {
    if (!s_tc) {
        if (!cc) {
            return NULL;
        }
        s_tc = new (std::nothrow) ThreadCache(cc);
        if (s_tc) {
            butil::thread_atexit(release_tc);
        }
    }
    return s_tc;
}

void* get_memory_pool_addr() {
    return s_addr;
}

size_t get_memory_pool_size() {
    return s_size;
}

bool init_memory_pool(size_t size, void* buf) {
    if (size <= MAX_PAGE_NUM * PAGE_SIZE) {
        return false;
    }
    pthread_mutex_lock(&s_lock);
    if (s_ph) {
        // cannot init twice
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    if (!buf) {  // user does not specify the memory address
#ifdef TCMPOOL_HUGEPAGE
        s_addr = get_hugepage_region(size, GHR_FALLBACK);;
#else
        s_addr = malloc(size);;
#endif
        if (!s_addr) {
            pthread_mutex_unlock(&s_lock);
            return false;
        }
        s_user_specified = false;
    } else {  // user specifies the memory address
        s_addr = buf;
        s_user_specified = true;
    }
    s_ph = new (std::nothrow) PageHeap(s_addr, size);
    if (!s_ph || !s_ph->_span_index) {
        if (s_ph) {
            delete s_ph;
            s_ph = NULL;
        }
        if (s_addr && !s_user_specified) {
#ifdef TCMPOOL_HUGEPAGE
            free_hugepage_region(s_addr);
#else
            free(s_addr);
#endif
            s_addr = NULL;
        }
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    s_cc = new (std::nothrow) CentralCache(s_ph);
    if (!s_cc) {
        delete s_ph;
        s_ph = NULL;
        if (s_addr && !s_user_specified) {
#ifdef TCMPOOL_HUGEPAGE
            free_hugepage_region(s_addr);
#else
            free(s_addr);
#endif
            s_addr = NULL;
        }
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    s_size = size;
    pthread_mutex_unlock(&s_lock);
    butil::thread_atexit(on_exit);
    return true;
}

void* alloc(size_t len) {
    if (!s_ph) {
        return NULL;
    }
    len += META_LEN;
    void* buf = NULL;
    if (len > THRESHOLD_SMALL_OBJECT) {
        buf = s_ph->alloc(len);
    } else {
        buf = get_tc(s_cc)->alloc(len);
    }
    if (buf) {
        buf = (size_t*)buf + 1;
    }
    return buf;
}

void dealloc(void* buf) {
    // NOTE:
    // The thread calling dealloc may be different from the one calling alloc.
    if (!s_ph) {
        return;
    }
    if (!buf) {
        return;
    }
    size_t* buf_with_meta = (size_t*)buf - 1;
    size_t len = *buf_with_meta;
    if (len > THRESHOLD_SMALL_OBJECT) {
        s_ph->dealloc(buf_with_meta, len);
    } else {
        get_tc(s_cc)->dealloc(buf_with_meta, len);
    }
}

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

