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

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <sys/resource.h>
#include "brpc/ubring/timer/timer_mgr.h"

namespace brpc {
namespace ubring {

int32_t g_epollFd = -1;
std::atomic<uint32_t> g_totalTimerNum;
TimerFdCtx *g_timerFdCtxMap = NULL;
uint32_t maxSystemFd;
static pthread_t g_epollExecuteThread;
static int32_t g_timerModuleInitialized;

#if defined(OS_MACOSX)
static int timerfd_create_macosx(int clockid, int flags);
static int timerfd_settime_macosx(int fd, int flags,
                                   const ::itimerspec *new_value,
                                   ::itimerspec *old_value);
#endif

static RETURN_CODE DeleteTimerInner(uint32_t fd) {
    if (g_timerFdCtxMap == NULL) {
        return UBRING_OK;
    }

    if (pthread_spin_lock(&g_timerFdCtxMap[fd].spinLock) != 0) {
        return UBRING_ERR;
    }

    if (g_timerFdCtxMap[fd].status == TIMER_CONTEXT_NOT_USING) {
        pthread_spin_unlock(&g_timerFdCtxMap[fd].spinLock);
        return UBRING_OK;
    }

    g_timerFdCtxMap[fd].status = TIMER_CONTEXT_NOT_USING;
    g_timerFdCtxMap[fd].cb = NULL;
    g_timerFdCtxMap[fd].args = NULL;
    g_timerFdCtxMap[fd].periodical = 0;
    g_timerFdCtxMap[fd].fd = 0;

    pthread_spin_unlock(&g_timerFdCtxMap[fd].spinLock);

#if defined(OS_LINUX)
    epoll_ctl(g_epollFd, EPOLL_CTL_DEL, (int)fd, NULL);
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(g_epollFd, &evt, 1, NULL, 0, NULL);
#endif

    uint64_t exp = 0;
    read((int)fd, &exp, sizeof(exp));

    close((int)fd);
    atomic_fetch_sub(&g_totalTimerNum, 1);
    return UBRING_OK;
}

static RETURN_CODE StartTimeEpoll(void) {
#if defined(OS_LINUX)
    g_epollFd = epoll_create1(0);
#elif defined(OS_MACOSX)
    g_epollFd = kqueue();
#endif
    if (UNLIKELY(g_epollFd == -1)) {
        LOG(ERROR) << "Failed to create epoll/kqueue. errno=" << errno;
        return UBRING_ERR;
    }

    int ret = pthread_create(&g_epollExecuteThread, NULL, TimerEpoll, NULL);
    if (UNLIKELY(ret != 0)) {
        LOG(ERROR) << "Failed to create thread err=" << ret;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

static RETURN_CODE TimerSpinLocksInit(void) {
    if (g_timerFdCtxMap == NULL) {
        LOG(ERROR) << "Timer module is not fully initialized.";
        return UBRING_ERR;
    }

    for (uint32_t fd = 0; fd < maxSystemFd; fd++) {
        int ret = pthread_spin_init(&g_timerFdCtxMap[fd].spinLock,
                                    PTHREAD_PROCESS_PRIVATE);
        if (ret != EOK) {
            LOG(ERROR) << "Failed to initialize spin lock for fd=" << fd;
            for (uint32_t cleanupFd = 0; cleanupFd < fd; cleanupFd++) {
                pthread_spin_destroy(&g_timerFdCtxMap[cleanupFd].spinLock);
            }
            return UBRING_ERR;
        }
    }
    return UBRING_OK;
}

static RETURN_CODE ExecuteCallback(int32_t timerFd) {
    UnifiedCallback((void *)(&g_timerFdCtxMap[timerFd]));
    return UBRING_OK;
}

static RETURN_CODE TimerCtxMapCompletion(void) {
    memset(g_timerFdCtxMap, 0, sizeof(TimerFdCtx) * maxSystemFd);

    RETURN_CODE ret = TimerSpinLocksInit();
    if (ret != UBRING_OK) {
        LOG(ERROR) << "Failed to init spin locks for timer module.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE TimerInit(void) {
    if (g_timerModuleInitialized > 0) {
        return UBRING_OK;
    }

    g_totalTimerNum.store(0);

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != UBRING_OK) {
        LOG(ERROR) << "Failed to get fd";
        return UBRING_ERR;
    }
    maxSystemFd = (uint32_t)rlim.rlim_cur;

    if (g_timerFdCtxMap == NULL) {
        g_timerFdCtxMap = (TimerFdCtx *)malloc(sizeof(TimerFdCtx) * maxSystemFd);
        if (UNLIKELY(!g_timerFdCtxMap)) {
            LOG(ERROR) << "Fail to malloc space for timer modules. errno=%d", errno;
            return UBRING_ERR;
        }

        RETURN_CODE ret = TimerCtxMapCompletion();
        if (ret != UBRING_OK) {
            LOG(ERROR) << "Failed to init main data structure of Time Module. ret=" << ret;
            free(g_timerFdCtxMap);
            g_timerFdCtxMap = NULL;
            return UBRING_ERR;
        }
    }

    RETURN_CODE ret = StartTimeEpoll();
    if (ret != UBRING_OK) {
        LOG(ERROR) << "Failed to start Timer Epoll. ret=" << ret;
        if (LIKELY(g_timerFdCtxMap != NULL)) {
            FREE_PTR(g_timerFdCtxMap);
        }
        return UBRING_ERR;
    }
    g_timerModuleInitialized = 1;
    return UBRING_OK;
}

void *UnifiedCallback(void *args) {
    TimerFdCtx *ctx = (TimerFdCtx *)args;
    if (pthread_spin_lock(&ctx->spinLock) != 0) {
        return NULL;
    }

    if (ctx->status == TIMER_CONTEXT_NOT_USING) {
        pthread_spin_unlock(&ctx->spinLock);
        return NULL;
    }

    void *(*cb)(void *) = ctx->cb;
    void *cbArgs = ctx->args;
    uint32_t fd = ctx->fd;
    int isPeriodical = ctx->periodical;
    ctx->status = TIMER_CONTEXT_CALLBACK_ONGOING;

    pthread_spin_unlock(&ctx->spinLock);

    cb(cbArgs);

    if (!isPeriodical) {
        DeleteTimerInner(fd);
    }
    return NULL;
}

void *TimerEpoll(void *args) {
    UNREFERENCE_PARAM(args);
#if defined(OS_LINUX)
    struct epoll_event readyEvents[MAX_TIMER];
#elif defined(OS_MACOSX)
    struct kevent readyEvents[MAX_TIMER];
#endif

    while (1) {
        if (g_timerModuleInitialized <= 0) {
            LOG(ERROR) << "The Timer module is not initialized.";
            break;
        }

#if defined(OS_LINUX)
        int32_t readyNum = epoll_wait(g_epollFd, readyEvents, MAX_TIMER,
                                      TIMER_EPOLL_WAIT_TIMEOUT);
#elif defined(OS_MACOSX)
        struct timespec timeout = {0, TIMER_EPOLL_WAIT_TIMEOUT * 1000000};
        int32_t readyNum = kevent(g_epollFd, NULL, 0, readyEvents, MAX_TIMER, &timeout);
#endif

        if (UNLIKELY(readyNum == -1)) {
            errno_t err = errno;
            if (err == EINTR) {
                LOG_EVERY_SECOND(WARNING) << "Epoll/Kqueue wait was interrupted. errno=" << err;
                continue;
            } else if (err == EBADF) {
                LOG(WARNING) << "The Timer module is destroyed.";
                break;
            }
            LOG(ERROR) << "Epoll/Kqueue wait internal error. errno=" << err;
            break;
        }

        for (int32_t i = 0; i < readyNum; i++) {
#if defined(OS_LINUX)
            struct epoll_event *event = &readyEvents[i];
            int32_t timerFd = event->data.fd;
#elif defined(OS_MACOSX)
            struct kevent *event = &readyEvents[i];
            int32_t timerFd = event->ident;
#endif

            uint64_t exp = 0;
            if (read(timerFd, &exp, sizeof(exp)) < 0) {
                if (errno != EBADF) {
                    LOG(ERROR) << "Failed to read timerfd=" << timerFd << " errno=" << errno;
                }
                continue;
            }
            if (TimerFdCtxValidate((uint32_t)timerFd) != UBRING_OK) {
                continue;
            }

            RETURN_CODE ret = ExecuteCallback(timerFd);
            if (ret != UBRING_OK) {
                LOG(ERROR) << "Failed execute callback ret=" << ret;
                DeleteTimerInner((uint32_t)timerFd);
                continue;
            }
        }
    }
    return NULL;
}

void DeleteTimerSafe(uint32_t fd) {
    if (g_timerFdCtxMap == NULL) {
        return;
    }

    if (pthread_spin_lock(&g_timerFdCtxMap[fd].spinLock) != 0) {
        return;
    }

    if (g_timerFdCtxMap[fd].status == TIMER_CONTEXT_NOT_USING) {
        pthread_spin_unlock(&g_timerFdCtxMap[fd].spinLock);
        return;
    }

    g_timerFdCtxMap[fd].status = TIMER_CONTEXT_NOT_USING;
    g_timerFdCtxMap[fd].cb = NULL;
    g_timerFdCtxMap[fd].args = NULL;
    g_timerFdCtxMap[fd].periodical = 0;
    g_timerFdCtxMap[fd].fd = 0;

    pthread_spin_unlock(&g_timerFdCtxMap[fd].spinLock);

#if defined(OS_LINUX)
    epoll_ctl(g_epollFd, EPOLL_CTL_DEL, (int)fd, NULL);
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(g_epollFd, &evt, 1, NULL, 0, NULL);
#endif

    uint64_t exp = 0;
    read((int)fd, &exp, sizeof(exp));

    close((int)fd);
    atomic_fetch_sub(&g_totalTimerNum, 1);
}

void DeleteTimer(uint32_t fd) {
    if (g_timerFdCtxMap == NULL) {
        LOG(WARNING) << "The timer is not initialized.";
        return;
    }

    g_timerFdCtxMap[fd].periodical = 0;
}

int32_t TimerStart(const ::itimerspec *time, void *(*cb)(void *), void *args) {
    if (g_epollFd == -1) {
        LOG(ERROR) << "Timer epoll/kqueue encountered internal error.";
        return -1;
    }

#if defined(OS_LINUX)
    int timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
#elif defined(OS_MACOSX)
    int timerFd = timerfd_create_macosx(CLOCK_MONOTONIC, 0);
#endif

    if (UNLIKELY(timerFd >= (int)maxSystemFd || timerFd == -1)) {
        LOG(ERROR) << "Failed to create timerfd=" << timerFd << " errno=" << errno;
        return -1;
    }

    g_timerFdCtxMap[timerFd].status = TIMER_CONTEXT_EPOLL_WAITING;
    g_timerFdCtxMap[timerFd].cb = cb;
    g_timerFdCtxMap[timerFd].args = args;
    g_timerFdCtxMap[timerFd].fd = (uint32_t)timerFd;

    if (LIKELY(time->it_interval.tv_sec > 0 || time->it_interval.tv_nsec > 0)) {
        g_timerFdCtxMap[timerFd].periodical = 1;
    }

#if defined(OS_LINUX)
    struct epoll_event event = {
        .events = EPOLLIN,
        .data = {.fd = timerFd}
    };

    int32_t ret = epoll_ctl(g_epollFd, EPOLL_CTL_ADD, timerFd, &event);
#elif defined(OS_MACOSX)
    struct kevent event;
    uint64_t timeout_nsec = time->it_value.tv_sec * 1000000000ULL + time->it_value.tv_nsec;
    uint64_t interval_nsec = time->it_interval.tv_sec * 1000000000ULL + time->it_interval.tv_nsec;
    EV_SET(&event, timerFd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0,
           timeout_nsec / 1000000, NULL);
    int32_t ret = kevent(g_epollFd, &event, 1, NULL, 0, NULL);
#endif

    if (UNLIKELY(ret != 0)) {
        CloseTimerFd((uint32_t)timerFd);
        LOG(ERROR) << "Failed to add event to epoll/kqueue. errno=" << errno;
        return -1;
    }

    atomic_fetch_add(&g_totalTimerNum, 1);

#if defined(OS_LINUX)
    ret = timerfd_settime(timerFd, 0, time, NULL);
#elif defined(OS_MACOSX)
    ret = timerfd_settime_macosx(timerFd, 0, time, NULL);
#endif

    if (UNLIKELY(ret != 0)) {
#if defined(OS_LINUX)
        if (epoll_ctl(g_epollFd, EPOLL_CTL_DEL, timerFd, NULL) != 0) {
#elif defined(OS_MACOSX)
        struct kevent evt;
        EV_SET(&evt, timerFd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        if (kevent(g_epollFd, &evt, 1, NULL, 0, NULL) != 0) {
#endif
            LOG(ERROR) << "Failed to delete the timer fd=" << timerFd << " with errno=" << errno;
        }
        CloseTimerFd((uint32_t)timerFd);
        atomic_fetch_sub(&g_totalTimerNum, 1);
        LOG(ERROR) << "Failed to set timer";
        return -1;
    }

    return timerFd;
}

uint32_t GetActiveTimerNum(void) {
    return atomic_load(&g_totalTimerNum);
}

void CloseTimerFd(uint32_t fd) {
    g_timerFdCtxMap[fd].cb = NULL;
    g_timerFdCtxMap[fd].args = NULL;
    g_timerFdCtxMap[fd].status = TIMER_CONTEXT_NOT_USING;
    g_timerFdCtxMap[fd].fd = 0;
    g_timerFdCtxMap[fd].periodical = 0;
    if (close((int)fd) != 0) {
        LOG(ERROR) << "Failed to close timer fd=" << fd << " errno=" << errno;
        return;
    }
}

void TimerModuleDestroy(void) {
    uint32_t maxFd = maxSystemFd;
    if (g_timerFdCtxMap) {
        for (uint32_t fd = 0; fd < maxFd; fd++) {
            if (g_timerFdCtxMap[fd].status != TIMER_CONTEXT_NOT_USING) {
                DeleteTimerSafe(fd);
            }
        }
    }
    close(g_epollFd);
    g_epollFd = -1;
    g_totalTimerNum = 0;
    g_timerModuleInitialized = 0;
    int32_t ret = pthread_join(g_epollExecuteThread, NULL);
    if (ret != EOK) {
        LOG(ERROR) << "Failed to join pthread, during destroying timer module. ret=" << ret;
        return;
    }
}

RETURN_CODE TimerFdCtxValidate(uint32_t fd) {
    if (fd >= maxSystemFd) {
        LOG(ERROR) << "TimerFd=" << fd << " is out of range=" << maxSystemFd;
        return UBRING_ERR;
    }
    if (g_timerFdCtxMap[fd].status == TIMER_CONTEXT_NOT_USING) {
        LOG(ERROR) << "TimerFd=" << fd << " has wrong status=" << g_timerFdCtxMap[fd].status;
        return UBRING_ERR;
    }
    if (g_timerFdCtxMap[fd].cb == NULL) {
        LOG(ERROR) << "The callback is not set.";
        return UBRING_ERR;
    }

    return UBRING_OK;
}

#if defined(OS_MACOSX)
static int timerfd_create_macosx(int clockid, int flags) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    return pipefd[0];
}

static int timerfd_settime_macosx(int fd, int flags,
                                   const ::itimerspec *new_value,
                                   ::itimerspec *old_value) {
    if (old_value != NULL) {
        memset(old_value, 0, sizeof(::itimerspec));
    }
    return 0;
}
#endif

}  // namespace ubring
}  // namespace brpc