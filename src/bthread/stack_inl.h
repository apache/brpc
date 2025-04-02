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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Sun Sep  7 22:37:39 CST 2014

#ifndef BTHREAD_ALLOCATE_STACK_INL_H
#define BTHREAD_ALLOCATE_STACK_INL_H

DECLARE_int32(guard_page_size);
DECLARE_int32(tc_stack_small);
DECLARE_int32(tc_stack_normal);

namespace bthread {

#ifdef BUTIL_USE_ASAN
namespace internal {

BUTIL_FORCE_INLINE void ASanPoisonMemoryRegion(const StackStorage& storage) {
    if (NULL == storage.bottom) {
        return;
    }

    CHECK_GT((void*)storage.bottom,
             reinterpret_cast<void*>(storage.stacksize + + storage.guardsize));
    BUTIL_ASAN_POISON_MEMORY_REGION(
        (char*)storage.bottom - storage.stacksize, storage.stacksize);
}

BUTIL_FORCE_INLINE void ASanUnpoisonMemoryRegion(const StackStorage& storage) {
    if (NULL == storage.bottom) {
        return;
    }
    CHECK_GT(storage.bottom,
             reinterpret_cast<void*>(storage.stacksize + storage.guardsize));
    BUTIL_ASAN_UNPOISON_MEMORY_REGION(
        (char*)storage.bottom - storage.stacksize, storage.stacksize);
}


BUTIL_FORCE_INLINE void StartSwitchFiber(void** fake_stack_save, StackStorage& storage) {
    if (NULL == storage.bottom) {
        return;
    }
    RELEASE_ASSERT(storage.bottom >
                   reinterpret_cast<void*>(storage.stacksize + storage.guardsize));
    // Lowest address of this stack.
    void* asan_stack_bottom = (char*)storage.bottom - storage.stacksize;
    BUTIL_ASAN_START_SWITCH_FIBER(fake_stack_save, asan_stack_bottom, storage.stacksize);
}

BUTIL_FORCE_INLINE void FinishSwitchFiber(void* fake_stack_save) {
    BUTIL_ASAN_FINISH_SWITCH_FIBER(fake_stack_save, NULL, NULL);
}

class ScopedASanFiberSwitcher {
public:
    ScopedASanFiberSwitcher(StackStorage& next_storage) {
        StartSwitchFiber(&_fake_stack, next_storage);
    }

    ~ScopedASanFiberSwitcher() {
        FinishSwitchFiber(_fake_stack);
    }

    DISALLOW_COPY_AND_ASSIGN(ScopedASanFiberSwitcher);

private:
    void* _fake_stack{NULL};
};

#define BTHREAD_ASAN_POISON_MEMORY_REGION(storage) \
    ::bthread::internal::ASanPoisonMemoryRegion(storage)

#define BTHREAD_ASAN_UNPOISON_MEMORY_REGION(storage) \
    ::bthread::internal::ASanUnpoisonMemoryRegion(storage)

#define BTHREAD_SCOPED_ASAN_FIBER_SWITCHER(storage) \
    ::bthread::internal::ScopedASanFiberSwitcher switcher(storage)

} // namespace internal
#else

// If ASan are used, the annotations should be no-ops.
#define BTHREAD_ASAN_POISON_MEMORY_REGION(storage) ((void)(storage))
#define BTHREAD_ASAN_UNPOISON_MEMORY_REGION(storage) ((void)(storage))
#define BTHREAD_SCOPED_ASAN_FIBER_SWITCHER(storage) ((void)(storage))

#endif // BUTIL_USE_ASAN

struct MainStackClass {};

struct SmallStackClass {
    static int* stack_size_flag;
    // Older gcc does not allow static const enum, use int instead.
    static const int stacktype = (int)STACK_TYPE_SMALL;
};

struct NormalStackClass {
    static int* stack_size_flag;
    static const int stacktype = (int)STACK_TYPE_NORMAL;
};

struct LargeStackClass {
    static int* stack_size_flag;
    static const int stacktype = (int)STACK_TYPE_LARGE;
};

template <typename StackClass> struct StackFactory {
    struct Wrapper : public ContextualStack {
        explicit Wrapper(void (*entry)(intptr_t)) {
            if (allocate_stack_storage(&storage, *StackClass::stack_size_flag,
                                       FLAGS_guard_page_size) != 0) {
                storage.zeroize();
                context = NULL;
                return;
            }
            context = bthread_make_fcontext(storage.bottom, storage.stacksize, entry);
            stacktype = (StackType)StackClass::stacktype;
            // It's poisoned prior to use.
            BTHREAD_ASAN_POISON_MEMORY_REGION(storage);
        }
        ~Wrapper() {
            if (context) {
                context = NULL;
                // Unpoison to avoid affecting other allocator.
                BTHREAD_ASAN_UNPOISON_MEMORY_REGION(storage);
                deallocate_stack_storage(&storage);
                storage.zeroize();
            }
        }
    };
    
    static ContextualStack* get_stack(void (*entry)(intptr_t)) {
        ContextualStack* cs = butil::get_object<Wrapper>(entry);
        // Marks stack as addressable.
        BTHREAD_ASAN_UNPOISON_MEMORY_REGION(cs->storage);
        return cs;
    }
    
    static void return_stack(ContextualStack* cs) {
        // Marks stack as unaddressable.
        BTHREAD_ASAN_POISON_MEMORY_REGION(cs->storage);
        butil::return_object(static_cast<Wrapper*>(cs));
    }
};

template <> struct StackFactory<MainStackClass> {
    static ContextualStack* get_stack(void (*)(intptr_t)) {
        ContextualStack* s = new (std::nothrow) ContextualStack;
        if (NULL == s) {
            return NULL;
        }
        s->context = NULL;
        s->stacktype = STACK_TYPE_MAIN;
        s->storage.zeroize();
        return s;
    }
    
    static void return_stack(ContextualStack* s) {
        delete s;
    }
};

inline ContextualStack* get_stack(StackType type, void (*entry)(intptr_t)) {
    switch (type) {
    case STACK_TYPE_PTHREAD:
        return NULL;
    case STACK_TYPE_SMALL:
        return StackFactory<SmallStackClass>::get_stack(entry);
    case STACK_TYPE_NORMAL:
        return StackFactory<NormalStackClass>::get_stack(entry);
    case STACK_TYPE_LARGE:
        return StackFactory<LargeStackClass>::get_stack(entry);
    case STACK_TYPE_MAIN:
        return StackFactory<MainStackClass>::get_stack(entry);
    }
    return NULL;
}

inline void return_stack(ContextualStack* s) {
    if (NULL == s) {
        return;
    }
    switch (s->stacktype) {
    case STACK_TYPE_PTHREAD:
        assert(false);
        return;
    case STACK_TYPE_SMALL:
        return StackFactory<SmallStackClass>::return_stack(s);
    case STACK_TYPE_NORMAL:
        return StackFactory<NormalStackClass>::return_stack(s);
    case STACK_TYPE_LARGE:
        return StackFactory<LargeStackClass>::return_stack(s);
    case STACK_TYPE_MAIN:
        return StackFactory<MainStackClass>::return_stack(s);
    }
}

inline void jump_stack(ContextualStack* from, ContextualStack* to) {
    bthread_jump_fcontext(&from->context, to->context, 0/*not skip remained*/);
}

}  // namespace bthread

namespace butil {

template <> struct ObjectPoolBlockMaxItem<
    bthread::StackFactory<bthread::LargeStackClass>::Wrapper> {
    static const size_t value = 64;
};
template <> struct ObjectPoolBlockMaxItem<
    bthread::StackFactory<bthread::NormalStackClass>::Wrapper> {
    static const size_t value = 64;
};

template <> struct ObjectPoolBlockMaxItem<
    bthread::StackFactory<bthread::SmallStackClass>::Wrapper> {
    static const size_t value = 64;
};

template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackFactory<bthread::SmallStackClass>::Wrapper> {
    inline static size_t value() {
        return (FLAGS_tc_stack_small <= 0 ? 0 : FLAGS_tc_stack_small);
    }
};

template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackFactory<bthread::NormalStackClass>::Wrapper> {
    inline static size_t value() {
        return (FLAGS_tc_stack_normal <= 0 ? 0 : FLAGS_tc_stack_normal);
    }
};

template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackFactory<bthread::LargeStackClass>::Wrapper> {
    inline static size_t value() { return 1UL; }
};

template <> struct ObjectPoolValidator<
    bthread::StackFactory<bthread::LargeStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackFactory<bthread::LargeStackClass>::Wrapper* w) {
        return w->context != NULL;
    }
};

template <> struct ObjectPoolValidator<
    bthread::StackFactory<bthread::NormalStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackFactory<bthread::NormalStackClass>::Wrapper* w) {
        return w->context != NULL;
    }
};

template <> struct ObjectPoolValidator<
    bthread::StackFactory<bthread::SmallStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackFactory<bthread::SmallStackClass>::Wrapper* w) {
        return w->context != NULL;
    }
};
    
}  // namespace butil

#endif  // BTHREAD_ALLOCATE_STACK_INL_H
