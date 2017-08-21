// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Sep  7 22:37:39 CST 2014

#ifndef BAIDU_BTHREAD_ALLOCATE_STACK_H
#define BAIDU_BTHREAD_ALLOCATE_STACK_H

#include <assert.h>
#include <gflags/gflags.h>          // DECLARE_int32
#include "bthread/types.h"
#include "bthread/context.h"        // bthread_fcontext_t
#include "base/object_pool.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h" // RunningOnValgrind
#include "base/third_party/valgrind/valgrind.h" // VALGRIND_STACK_REGISTER

namespace bthread {

struct StackContainer {
    bthread_fcontext_t context;
    int stacksize;
    int guardsize;
    void* stack;
    int stacktype;
    unsigned valgrind_stack_id;
};

enum StackType {
    STACK_TYPE_MAIN = 0,
    STACK_TYPE_PTHREAD = BTHREAD_STACKTYPE_PTHREAD,
    STACK_TYPE_SMALL = BTHREAD_STACKTYPE_SMALL,
    STACK_TYPE_NORMAL = BTHREAD_STACKTYPE_NORMAL,
    STACK_TYPE_LARGE = BTHREAD_STACKTYPE_LARGE
};

inline StackContainer* get_stack(StackType type, void (*entry)(intptr_t));
inline void return_stack(StackContainer* sc);

// Allocate/deallocate stacks with guard pages.
void* allocate_stack(int* stacksize, int* guardsize);
void deallocate_stack(void* mem, int stacksize, int guardsize);

}  // namespace bthread

#include "bthread/stack_inl.h"

#endif  // BAIDU_BTHREAD_ALLOCATE_STACK_H
