// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 22 17:30:12 CST 2014

#ifndef BAIDU_BTHREAD_BUTEX_H
#define BAIDU_BTHREAD_BUTEX_H

#include <errno.h>                               // users need to check errno
#include <time.h>                                // timespec
#include "base/macros.h"                         // BAIDU_CASSERT
#include "bthread/types.h"                       // bthread_t

namespace bthread {

// Create a butex which is a futex-like 32-bit primitive for synchronizing
// bthreads/pthreads.
// Returns a pointer to 32-bit data, NULL on failure.
// NOTE: all butexes are private(not inter-process).
void* butex_create();

// Check width of user type before casting.
template <typename T> T* butex_create_checked() {
    BAIDU_CASSERT(sizeof(T) == sizeof(int), sizeof_T_must_equal_int);
    return static_cast<T*>(butex_create());
}

// Destroy the butex.
void butex_destroy(void* butex);

// Wake up at most 1 thread waiting on |butex|.
// Returns # of threads woken up.
int butex_wake(void* butex);

// Wake up all threads waiting on |butex|.
// Returns # of threads woken up.
int butex_wake_all(void* butex);

// Wake up all threads waiting on |butex| except a bthread whose identifier
// is |excluded_bthread|. This function does not yield.
// Returns # of threads woken up.
int butex_wake_except(void* butex, bthread_t excluded_bthread);

// Wake up at most 1 thread waiting on |butex1|, let all other threads wait
// on |butex2| instead.
// Returns # of threads woken up.
int butex_requeue(void* butex1, void* butex2);

// Atomically wait on |butex| if *butex equals |expected_value|, until the
// butex is woken up by butex_wake*, or CLOCK_REALTIME reached |abstime| if
// abstime is not NULL.
// About |abstime|:
//   Different from FUTEX_WAIT, butex_wait uses absolute time.
int butex_wait(void* butex, int expected_value, const timespec* abstime);

// Same with butex_wait except that this function can not be woken up by
// bthread_stop(), although this function still returns -1(ESTOP) after
// wake-up.
int butex_wait_uninterruptible(void* butex, int expected_value,
                               const timespec* abstime);

}  // namespace bthread

#endif  // BAIDU_BTHREAD_BUTEX_H

