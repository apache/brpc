#ifndef BUTIL_COMPAT_H
#define BUTIL_COMPAT_H

// TODO: Some functions in this header are not implemented yet.

#include "butil/build_config.h"
#include <pthread.h>

#if defined(OS_MACOSX)

#include <sys/cdefs.h>
#include <stdint.h>
#include <dispatch/dispatch.h>    // dispatch_semaphore
#include <errno.h>                // EINVAL

__BEGIN_DECLS

// Implement pthread_spinlock_t for MAC.

typedef int pthread_spinlock_t;

inline int pthread_spin_init(pthread_spinlock_t *__lock, int __pshared) {
    __asm__ __volatile__ ("" ::: "memory");
    *__lock = 0;
    return 0;
}

inline int pthread_spin_destroy(pthread_spinlock_t *__lock) {
    (void)__lock;
    return 0;
}

inline int pthread_spin_lock(pthread_spinlock_t *__lock) {
    while (1) {
        int i;
        for (i=0; i < 10000; i++) {
            if (__sync_bool_compare_and_swap(__lock, 0, 1)) {
                return 0;
            }
        }
        sched_yield();
    }
    return 0;
}

inline int pthread_spin_trylock(pthread_spinlock_t *__lock) {
    if (__sync_bool_compare_and_swap(__lock, 0, 1)) {
        return 0;
    }
    return EBUSY;
}

inline int pthread_spin_unlock(pthread_spinlock_t *__lock) {
    __asm__ __volatile__ ("" ::: "memory");
    *__lock = 0;
    return 0;
}

__END_DECLS

#elif defined(OS_LINUX)

#include <sys/epoll.h>

#else

#error "The platform does not support epoll-like APIs"

#endif // defined(OS_MACOSX)

__BEGIN_DECLS

inline uint64_t pthread_numeric_id() {
#if defined(OS_MACOSX)
    uint64_t id;
    if (pthread_threadid_np(pthread_self(), &id) == 0) {
        return id;
    }
    return -1;
#else
    return pthread_self();
#endif
}

__END_DECLS

#endif // BUTIL_COMPAT_H
