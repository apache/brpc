// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#include <signal.h>
#include "bthread/interrupt_pthread.h"

namespace bthread {

// TODO: Make sure SIGURG is not used by user.
// This empty handler is simply for triggering EINTR in blocking syscalls.
void do_nothing_handler(int) {}

static pthread_once_t register_sigurg_once = PTHREAD_ONCE_INIT;

static void register_sigurg() {
    signal(SIGURG, do_nothing_handler);
}

int interrupt_pthread(pthread_t th) {
    pthread_once(&register_sigurg_once, register_sigurg);
    return pthread_kill(th, SIGURG);
}

}  // namespace bthread
