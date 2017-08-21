// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_INTERRUPT_PTHREAD_H
#define BAIDU_BTHREAD_INTERRUPT_PTHREAD_H

#include <pthread.h>

namespace bthread {

// Make blocking ops in the pthread returns -1 and EINTR.
// Returns what pthread_kill returns.
int interrupt_pthread(pthread_t th);

}  // namespace bthread

#endif // BAIDU_BTHREAD_INTERRUPT_PTHREAD_H
