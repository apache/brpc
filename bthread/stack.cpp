// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Sep  7 22:37:39 CST 2014

#include <unistd.h>                               // getpagesize
#include <sys/mman.h>                             // mmap, munmap, mprotect
#include <algorithm>                              // std::max
#include <stdlib.h>                               // posix_memalign
#include "base/macros.h"                          // BAIDU_CASSERT
#include "base/memory/singleton_on_pthread_once.h"
#include "bvar/reducer.h"                         // bvar::Adder
#include "bthread/types.h"                        // BTHREAD_STACKTYPE_*
#include "bthread/stack.h"

DEFINE_int32(stack_size_small, 32768, "size of small stacks");
DEFINE_int32(stack_size_normal, 1048576, "size of normal stacks");
DEFINE_int32(stack_size_large, 8388608, "size of large stacks");
DEFINE_int32(guard_page_size, 4096, "size of guard page, allocate stacks by malloc if it's 0(not recommended)");
DEFINE_int32(tc_stack_small, 32, "maximum small stacks cached by each thread");
DEFINE_int32(tc_stack_normal, 8, "maximum normal stacks cached by each thread");

DEFINE_bool(has_valgrind, false, "Set to true when running inside valgrind");

namespace bthread {

BAIDU_CASSERT(BTHREAD_STACKTYPE_PTHREAD == STACK_TYPE_PTHREAD, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_SMALL == STACK_TYPE_SMALL, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_NORMAL == STACK_TYPE_NORMAL, must_match);
BAIDU_CASSERT(BTHREAD_STACKTYPE_LARGE == STACK_TYPE_LARGE, must_match);
BAIDU_CASSERT(STACK_TYPE_MAIN == 0, must_be_0);

extern const int PAGESIZE = getpagesize();
extern const int PAGESIZE_M1 = PAGESIZE - 1;

const int MIN_STACKSIZE = PAGESIZE * 2;
const int MIN_GUARDSIZE = PAGESIZE;

struct StackCount : public bvar::Adder<int64_t> {
    StackCount() : bvar::Adder<int64_t>("bthread_stack_count") {}
};
inline bvar::Adder<int64_t>& stack_count() {
    return *base::get_leaky_singleton<StackCount>();
}

void* allocate_stack(int* inout_stacksize, int* inout_guardsize) {
    // Align stack and guard size.
    int stacksize =
        (std::max(*inout_stacksize, MIN_STACKSIZE) + PAGESIZE_M1) &
        ~PAGESIZE_M1;
    int guardsize =
        (std::max(*inout_guardsize, MIN_GUARDSIZE) + PAGESIZE_M1) &
        ~PAGESIZE_M1;

    if (FLAGS_guard_page_size <= 0) {
        void* mem = malloc(stacksize);
        if (NULL == mem) {
            return NULL;
        }
        stack_count() << 1;
        *inout_stacksize = stacksize;
        *inout_guardsize = 0;
        return (char*)mem + stacksize;
    } else {
        const int memsize = stacksize + guardsize;
        void* const mem = mmap(NULL, memsize, (PROT_READ | PROT_WRITE),
                               (MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);

        if (MAP_FAILED == mem) {
            PLOG_EVERY_SECOND(ERROR) 
                    << "Fail to mmap, which is likely to be limited by the value"
                       " in /proc/sys/vm/max_map_count";
            // may fail due to limit of max_map_count (65536 in default)
            return NULL;
        }

        stack_count() << 1;
        char* aligned_mem = (char*)(((intptr_t)mem + PAGESIZE_M1) & ~PAGESIZE_M1);
        const int offset = aligned_mem - (char*)mem;

        if (guardsize <= offset ||
            mprotect(aligned_mem, guardsize - offset, PROT_NONE) != 0) {
            munmap(mem, memsize);
            return NULL;
        }
        
        *inout_stacksize = stacksize;
        *inout_guardsize = guardsize;
        return (char*)mem + memsize;
    }
}

void deallocate_stack(void* mem, int stacksize, int guardsize) {
    const int memsize = stacksize + guardsize;
    if (FLAGS_guard_page_size <= 0) {
        if ((char*)mem > (char*)NULL + memsize) {
            stack_count() << -1;
            free((char*)mem - memsize);
        }
    } else {
        if ((char*)mem > (char*)NULL + memsize) {
            stack_count() << -1;
            munmap((char*)mem - memsize, memsize);
        }
    }
}

int* SmallStackClass::stack_size_flag = &FLAGS_stack_size_small;
int* NormalStackClass::stack_size_flag = &FLAGS_stack_size_normal;
int* LargeStackClass::stack_size_flag = &FLAGS_stack_size_large;

}  // namespace bthread
