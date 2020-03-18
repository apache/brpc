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

// bthread - A M:N threading library to make applications more concurrent.

// Date: Fri Dec  5 13:40:57 CST 2014

#ifndef BTHREAD_PROCESSOR_H
#define BTHREAD_PROCESSOR_H

#include "butil/build_config.h"

// Pause instruction to prevent excess processor bus usage, only works in GCC
# ifndef cpu_relax
#if defined(ARCH_CPU_ARM_FAMILY)
# define cpu_relax() asm volatile("yield\n": : :"memory")
#else
# define cpu_relax() asm volatile("pause\n": : :"memory")
#endif
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

#endif // BTHREAD_PROCESSOR_H
