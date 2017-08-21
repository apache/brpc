// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Fri Dec  5 13:40:57 CST 2014

#ifndef BAIDU_BTHREAD_PROCESSOR_H
#define BAIDU_BTHREAD_PROCESSOR_H

// Pause instruction to prevent excess processor bus usage, only works in GCC
# ifndef cpu_relax
# define cpu_relax() asm volatile("pause\n": : :"memory")
# endif

// Compile read-write barrier
# ifndef barrier
# define barrier() asm volatile("": : :"memory")
# endif


# define BT_LOOP_WHEN(expr, num_spins)                                  \
    do {                                                                \
        /*sched_yield may change errno*/                                \
        const int saved_errno = errno;                                  \
        for (int cnt = 0, saved_nspin = (num_spins); (expr); ++cnt) {   \
            if (cnt < saved_nspin) {                                    \
                cpu_relax();                                            \
            } else {                                                    \
                sched_yield();                                          \
            }                                                           \
        }                                                               \
        errno = saved_errno;                                            \
    } while (0)

#endif // BAIDU_BTHREAD_PROCESSOR_H
