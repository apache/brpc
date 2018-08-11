// bthread - A M:N threading library to make applications more concurrent.
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
// Date: Sun Sep  7 22:37:39 CST 2014

#include <unistd.h>                               // getpagesize
#include <sys/mman.h>                             // mmap, munmap, mprotect
#include <algorithm>                              // std::max
#include <stdlib.h>                               // posix_memalign
#include "butil/macros.h"                          // BAIDU_CASSERT
#include "butil/memory/singleton_on_pthread_once.h"
#include "butil/third_party/dynamic_annotations/dynamic_annotations.h" // RunningOnValgrind
#include "butil/third_party/valgrind/valgrind.h"   // VALGRIND_STACK_REGISTER
#include "bvar/passive_status.h"
#include "bthread/types.h"                        // BTHREAD_STACKTYPE_*
#include "bthread/stack.h"

DEFINE_int32(stack_size_small, 32768, "size of small stacks");
DEFINE_int32(stack_size_normal, 1048576, "size of normal stacks");
DEFINE_int32(stack_size_large, 8388608, "size of large stacks");
DEFINE_int32(guard_page_size, 4096, "size of guard page, allocate stacks by malloc if it's 0(not recommended)");
DEFINE_int32(tc_stack_small, 32, "maximum small stacks cached by each thread");
DEFINE_int32(tc_stack_normal, 8, "maximum normal stacks cached by each thread");

namespace bthread {

BAIDU_CASSERT(BTHREAD_STACKTYPE_PTHREAD == STACK_TYPE_PTHREAD, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_SMALL == STACK_TYPE_SMALL, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_NORMAL == STACK_TYPE_NORMAL, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_LARGE == STACK_TYPE_LARGE, must_match);
BAIDU_CASSERT(STACK_TYPE_MAIN == 0, must_be_0);

static butil::static_atomic<int64_t> s_stack_count = BUTIL_STATIC_ATOMIC_INIT(0);
static int64_t get_stack_count(void*) {
    return s_stack_count.load(butil::memory_order_relaxed);
}
static bvar::PassiveStatus<int64_t> bvar_stack_count(
    "bthread_stack_count", get_stack_count, NULL);

int allocate_stack_storage(StackStorage* s, int stacksize_in, int guardsize_in) {
    const static int PAGESIZE = getpagesize();
    const int PAGESIZE_M1 = PAGESIZE - 1;
    const int MIN_STACKSIZE = PAGESIZE * 2;
    const int MIN_GUARDSIZE = PAGESIZE;

    // Align stacksize
    const int stacksize =
        (std::max(stacksize_in, MIN_STACKSIZE) + PAGESIZE_M1) &
        ~PAGESIZE_M1;

    if (guardsize_in <= 0) {
        void* mem = malloc(stacksize);
        if (NULL == mem) {
            PLOG_EVERY_SECOND(ERROR) << "Fail to malloc (size="
                                     << stacksize << ")";
            return -1;
        }
        s_stack_count.fetch_add(1, butil::memory_order_relaxed);
        s->bottom = (char*)mem + stacksize;
        s->stacksize = stacksize;
        s->guardsize = 0;
        if (RunningOnValgrind()) {
            s->valgrind_stack_id = VALGRIND_STACK_REGISTER(
                s->bottom, (char*)s->bottom - stacksize);
        } else {
            s->valgrind_stack_id = 0;
        }
        return 0;
    } else {
        // Align guardsize
        const int guardsize =
            (std::max(guardsize_in, MIN_GUARDSIZE) + PAGESIZE_M1) &
            ~PAGESIZE_M1;

        const int memsize = stacksize + guardsize;
        void* const mem = mmap(NULL, memsize, (PROT_READ | PROT_WRITE),
                               (MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);

        if (MAP_FAILED == mem) {
            PLOG_EVERY_SECOND(ERROR) 
                << "Fail to mmap size=" << memsize << " stack_count="
                << s_stack_count.load(butil::memory_order_relaxed)
                << ", possibly limited by /proc/sys/vm/max_map_count";
            // may fail due to limit of max_map_count (65536 in default)
            return -1;
        }

        void* aligned_mem = (void*)(((intptr_t)mem + PAGESIZE_M1) & ~PAGESIZE_M1);
        if (aligned_mem != mem) {
            LOG_ONCE(ERROR) << "addr=" << mem << " returned by mmap is not "
                "aligned by pagesize=" << PAGESIZE;
        }
        const int offset = (char*)aligned_mem - (char*)mem;
        if (guardsize <= offset ||
            mprotect(aligned_mem, guardsize - offset, PROT_NONE) != 0) {
            munmap(mem, memsize);
            PLOG_EVERY_SECOND(ERROR) 
                << "Fail to mprotect " << (void*)aligned_mem << " length="
                << guardsize - offset; 
            return -1;
        }

        s_stack_count.fetch_add(1, butil::memory_order_relaxed);
        s->bottom = (char*)mem + memsize;
        s->stacksize = stacksize;
        s->guardsize = guardsize;
        if (RunningOnValgrind()) {
            s->valgrind_stack_id = VALGRIND_STACK_REGISTER(
                s->bottom, (char*)s->bottom - stacksize);
        } else {
            s->valgrind_stack_id = 0;
        }
        return 0;
    }
}

void deallocate_stack_storage(StackStorage* s) {
    if (RunningOnValgrind()) {
        VALGRIND_STACK_DEREGISTER(s->valgrind_stack_id);
    }
    const int memsize = s->stacksize + s->guardsize;
    if ((uintptr_t)s->bottom <= (uintptr_t)memsize) {
        return;
    }
    s_stack_count.fetch_sub(1, butil::memory_order_relaxed);
    if (s->guardsize <= 0) {
        free((char*)s->bottom - memsize);
    } else {
        munmap((char*)s->bottom - memsize, memsize);
    }
}

int* SmallStackClass::stack_size_flag = &FLAGS_stack_size_small;
int* NormalStackClass::stack_size_flag = &FLAGS_stack_size_normal;
int* LargeStackClass::stack_size_flag = &FLAGS_stack_size_large;

}  // namespace bthread
