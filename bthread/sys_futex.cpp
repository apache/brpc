// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#include "bthread/sys_futex.h"

namespace bthread {

const int SYS_FUTEX_PRIVATE_FLAG = 128;

static int get_futex_private_flag() {
    static int dummy = 0;
    if (!syscall(SYS_futex, &dummy, (FUTEX_WAKE | SYS_FUTEX_PRIVATE_FLAG),
                 1, NULL, NULL, 0)) {
        return SYS_FUTEX_PRIVATE_FLAG;
    }
    return 0;
}

extern const int futex_private_flag = get_futex_private_flag();

}  // namespace bthread
