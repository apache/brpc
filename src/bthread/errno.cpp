// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2010 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Wed Jul 30 11:47:19 CST 2014

#include "bthread/errno.h"

// Define errno in bthread/errno.h
extern const int ESTOP = -20;

BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")

extern "C" {

extern int *__errno_location() __THROW __attribute__((__const__));

int *bthread_errno_location() {
    return __errno_location();
}
}  // extern "C"
