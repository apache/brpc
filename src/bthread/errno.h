// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Wed Jul 30 11:47:19 CST 2014

#ifndef BAIDU_BTHREAD_ERRNO_H
#define BAIDU_BTHREAD_ERRNO_H

#include <errno.h>                    // errno
#include "base/errno.h"               // berror(), DEFINE_BTHREAD_ERRNO

__BEGIN_DECLS

extern int *bthread_errno_location();

#ifdef errno
#undef errno
#define errno *bthread_errno_location()
#endif

// List errno used throughout bthread
extern const int ESTOP;

__END_DECLS

#endif  //BAIDU_BTHREAD_ERRNO_H
