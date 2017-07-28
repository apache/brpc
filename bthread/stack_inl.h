// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Sep  7 22:37:39 CST 2014

#ifndef BAIDU_BTHREAD_ALLOCATE_STACK_INL_H
#define BAIDU_BTHREAD_ALLOCATE_STACK_INL_H

DECLARE_int32(guard_page_size);
DECLARE_int32(tc_stack_small);
DECLARE_int32(tc_stack_normal);

namespace bthread {

struct MainStackClass {};

struct SmallStackClass {
    static int* stack_size_flag;
    const static int stacktype = STACK_TYPE_SMALL;
};

struct NormalStackClass {
    static int* stack_size_flag;
    const static int stacktype = STACK_TYPE_NORMAL;
};

struct LargeStackClass {
    static int* stack_size_flag;
    const static int stacktype = STACK_TYPE_LARGE;
};

template <typename StackClass> struct StackContainerFactory {
    struct Wrapper : public StackContainer {
        explicit Wrapper(void (*entry)(intptr_t)) {
            stacksize = *StackClass::stack_size_flag;
            guardsize = FLAGS_guard_page_size;
            stack = allocate_stack(&stacksize, &guardsize);
            valgrind_stack_id = 0;
            if (BAIDU_UNLIKELY(NULL == stack)) {
                context = NULL;
                return;
            }
            // TODO: Growth direction of stack is arch-dependent(not handled by
            //       fcontext). We temporarily assume stack grows upwards.
            // http://www.boost.org/doc/libs/1_55_0/libs/context/doc/html/context/stack.html
            if (RunningOnValgrind()) {
                valgrind_stack_id = VALGRIND_STACK_REGISTER(
                    stack, (char*)stack - stacksize);
            }
            context = bthread_make_fcontext(stack, stacksize, entry);
            stacktype = StackClass::stacktype;
        }
        ~Wrapper() {
            if (stack) {
                if (RunningOnValgrind()) {
                    VALGRIND_STACK_DEREGISTER(valgrind_stack_id);
                    valgrind_stack_id = 0;
                }
                deallocate_stack(stack, stacksize, guardsize);
                stack = NULL;
            }
            context = NULL;
        }
    };
    
    static StackContainer* get_stack(void (*entry)(intptr_t)) {
        return base::get_object<Wrapper>(entry);
    }
    
    static void return_stack(StackContainer* sc) {
        base::return_object(static_cast<Wrapper*>(sc));
    }
};

template <> struct StackContainerFactory<MainStackClass> {
    static StackContainer* get_stack(void (*)(intptr_t)) {
        StackContainer* sc = new (std::nothrow) StackContainer;
        if (NULL == sc) {
            return NULL;
        }
        sc->stacksize = 0;
        sc->guardsize = 0;
        sc->stacktype = STACK_TYPE_MAIN;
        return sc;
    }
    
    static void return_stack(StackContainer* sc) {
        delete sc;
    }
};

StackContainer* get_stack(StackType type, void (*entry)(intptr_t)) {
    switch (type) {
    case STACK_TYPE_PTHREAD:
        return NULL;
    case STACK_TYPE_SMALL:
        return StackContainerFactory<SmallStackClass>::get_stack(entry);
    case STACK_TYPE_NORMAL:
        return StackContainerFactory<NormalStackClass>::get_stack(entry);
    case STACK_TYPE_LARGE:
        return StackContainerFactory<LargeStackClass>::get_stack(entry);
    case STACK_TYPE_MAIN:
        return StackContainerFactory<MainStackClass>::get_stack(entry);
    }
    return NULL;
}

void return_stack(StackContainer* sc) {
    if (NULL == sc) {
        return;
    }
    switch (sc->stacktype) {
    case STACK_TYPE_PTHREAD:
        assert(false);
        return;
    case STACK_TYPE_SMALL:
        return StackContainerFactory<SmallStackClass>::return_stack(sc);
    case STACK_TYPE_NORMAL:
        return StackContainerFactory<NormalStackClass>::return_stack(sc);
    case STACK_TYPE_LARGE:
        return StackContainerFactory<LargeStackClass>::return_stack(sc);
    case STACK_TYPE_MAIN:
        return StackContainerFactory<MainStackClass>::return_stack(sc);
    }
}

}  // namespace bthread

namespace base {

template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackContainerFactory<bthread::LargeStackClass>::Wrapper> {
    static const size_t value = 32;
};
template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackContainerFactory<bthread::NormalStackClass>::Wrapper> {
    static const size_t value = 32;
};

template <> struct ObjectPoolFreeChunkMaxItem<
    bthread::StackContainerFactory<bthread::SmallStackClass>::Wrapper> {
    static const size_t value = 32;
};

template <> struct ObjectPoolFreeChunkMaxItemDynamic<
    bthread::StackContainerFactory<bthread::SmallStackClass>::Wrapper> {
    inline static size_t value() {
        return (FLAGS_tc_stack_small <= 0 ? 0 : FLAGS_tc_stack_small);
    }
};

template <> struct ObjectPoolFreeChunkMaxItemDynamic<
    bthread::StackContainerFactory<bthread::NormalStackClass>::Wrapper> {
    inline static size_t value() {
        return (FLAGS_tc_stack_normal <= 0 ? 0 : FLAGS_tc_stack_normal);
    }
};

template <> struct ObjectPoolFreeChunkMaxItemDynamic<
    bthread::StackContainerFactory<bthread::LargeStackClass>::Wrapper> {
    inline static size_t value() { return 1UL; }
};

template <> struct ObjectPoolValidator<
    bthread::StackContainerFactory<bthread::LargeStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackContainerFactory<bthread::LargeStackClass>::Wrapper* w) {
        return w->stack != NULL;
    }
};

template <> struct ObjectPoolValidator<
    bthread::StackContainerFactory<bthread::NormalStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackContainerFactory<bthread::NormalStackClass>::Wrapper* w) {
        return w->stack != NULL;
    }
};

template <> struct ObjectPoolValidator<
    bthread::StackContainerFactory<bthread::SmallStackClass>::Wrapper> {
    inline static bool validate(
        const bthread::StackContainerFactory<bthread::SmallStackClass>::Wrapper* w) {
        return w->stack != NULL;
    }
};
    
}  // namespace base

#endif  // BAIDU_BTHREAD_ALLOCATE_STACK_INL_H
