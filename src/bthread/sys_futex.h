// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_SYS_FUTEX_H
#define BAIDU_BTHREAD_SYS_FUTEX_H

#include <syscall.h>                    // SYS_futex
#include <unistd.h>                     // syscall
#include <time.h>                       // timespec
#include <linux/futex.h>                // FUTEX_WAIT, FUTEX_WAKE

namespace bthread {

extern const int futex_private_flag;

inline int futex_wait_private(
    void* addr1, int expected, const timespec* timeout) {
    return syscall(SYS_futex, addr1, (FUTEX_WAIT | futex_private_flag),
                   expected, timeout, NULL, 0);
}

inline int futex_wake_private(void* addr1, int nwake) {
    return syscall(SYS_futex, addr1, (FUTEX_WAKE | futex_private_flag),
                   nwake, NULL, NULL, 0);
}

inline int futex_requeue_private(void* addr1, int nwake, void* addr2) {
    return syscall(SYS_futex, addr1, (FUTEX_REQUEUE | futex_private_flag),
                   nwake, NULL, addr2, 0);
}

}  // namespace bthread

#endif // BAIDU_BTHREAD_SYS_FUTEX_H
