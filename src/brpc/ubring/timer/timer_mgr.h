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

#ifndef BRPC_TIMER_MGR_H
#define BRPC_TIMER_MGR_H
#include <pthread.h>
#include <time.h>
#include "brpc/ubring/common/common.h"

#if defined(OS_LINUX)
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif defined(OS_MACOSX)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#define MAX_TIMER 1024
#define TIMER_EPOLL_WAIT_TIMEOUT 1000

namespace brpc {
namespace ubring {
typedef enum {
    TIMER_CONTEXT_NOT_USING,
    TIMER_CONTEXT_EPOLL_WAITING,
    TIMER_CONTEXT_CALLBACK_ONGOING
} TimerFdCtxStatus;

typedef struct {
    void *(*cb)(void*);
    void *args;
    uint32_t fd;
    TimerFdCtxStatus status;
    uint32_t periodical;
    pthread_spinlock_t spinLock;
} TimerFdCtx;

RETURN_CODE TimerInit(void);
void TimerModuleDestroy(void);
void *UnifiedCallback(void *args);
void *TimerEpoll(void *args);
int32_t TimerStart(const struct itimerspec *time, void *(*cb)(void *), void *args);
uint32_t GetActiveTimerNum(void);
void CloseTimerFd(uint32_t fd);

void DeleteTimerSafe(uint32_t fd);
void DeleteTimer(uint32_t fd);
RETURN_CODE TimerFdCtxValidate(uint32_t fd);
}
}
#endif //BRPC_TIMER_MGR_H