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
#include "brpc/ubshm/timer/timer_mgr.h"
#include "butil/scoped_lock.h"

namespace brpc {
namespace ubring {

int32_t g_epoll_fd = -1;
std::atomic<uint32_t> g_total_timer_num(0);
TimerFdCtx *g_timer_fd_ctx_map = nullptr;
uint32_t g_max_system_fd = 0;
static pthread_t g_epoll_execute_thread = 0;
static std::atomic<int32_t> g_timer_module_initialized(0);
static pthread_mutex_t g_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_timer_idle = PTHREAD_COND_INITIALIZER;
static thread_local TimerFdCtx* g_running_timer = nullptr;

#if defined(OS_MACOSX)
static int timerfd_create_macosx(int clockid, int flags);
static int timerfd_settime_macosx(int fd, int flags,
                                   const itimerspec *new_value,
                                   itimerspec *old_value);
#endif

// Called with g_timer_mutex held, after the callback has finished.
static void CloseTimerLocked(uint32_t fd) {
    TimerFdCtx& ctx = g_timer_fd_ctx_map[fd];
#if defined(OS_LINUX)
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
    kevent(g_epoll_fd, &evt, 1, nullptr, 0, nullptr);
#endif
    close(fd);
    ctx.status = TIMER_CONTEXT_NOT_USING;
    ctx.cb = nullptr;
    ctx.args = nullptr;
    ctx.periodical = 0;
    g_total_timer_num.fetch_sub(1);
    pthread_cond_broadcast(&g_timer_idle);
}

static RETURN_CODE DeleteTimerInner(uint32_t fd) {
    pthread_mutex_lock(&g_timer_mutex);
    if (g_timer_fd_ctx_map == nullptr || fd >= g_max_system_fd) {
        pthread_mutex_unlock(&g_timer_mutex);
        return UBRING_OK;
    }
    TimerFdCtx& ctx = g_timer_fd_ctx_map[fd];
    const uint32_t generation = ctx.generation;
    if (ctx.status != TIMER_CONTEXT_NOT_USING) {
        ctx.canceled = true;
        if (ctx.status == TIMER_CONTEXT_CALLBACK_ONGOING) {
            // A callback can cancel itself. Its epilogue closes the descriptor.
            while (g_running_timer != &ctx &&
                   ctx.generation == generation &&
                   ctx.status == TIMER_CONTEXT_CALLBACK_ONGOING) {
                pthread_cond_wait(&g_timer_idle, &g_timer_mutex);
            }
        } else {
            CloseTimerLocked(fd);
        }
    }
    pthread_mutex_unlock(&g_timer_mutex);
    return UBRING_OK;
}

static RETURN_CODE StartTimeEpoll(void) {
#if defined(OS_LINUX)
    g_epoll_fd = epoll_create1(0);
#elif defined(OS_MACOSX)
    g_epoll_fd = kqueue();
#endif
    if (UNLIKELY(g_epoll_fd == -1)) {
        LOG(ERROR) << "Failed to create epoll/kqueue. errno=" << errno;
        return UBRING_ERR;
    }

    g_timer_module_initialized = 1;
    int ret = pthread_create(&g_epoll_execute_thread, nullptr, TimerEpoll, nullptr);
    if (UNLIKELY(ret != 0)) {
        g_timer_module_initialized = 0;
        LOG(ERROR) << "Failed to create thread err=" << ret;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

static RETURN_CODE TimerCtxMapCompletion(void) {
    memset(g_timer_fd_ctx_map, 0, sizeof(TimerFdCtx) * g_max_system_fd);
    return UBRING_OK;
}

RETURN_CODE TimerInit(void) {
    if (g_timer_module_initialized > 0) {
        return UBRING_OK;
    }

    g_total_timer_num.store(0);

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != UBRING_OK) {
        LOG(ERROR) << "Failed to get fd";
        return UBRING_ERR;
    }
    g_max_system_fd = (uint32_t)rlim.rlim_cur;

    if (g_timer_fd_ctx_map == nullptr) {
        g_timer_fd_ctx_map = (TimerFdCtx *)malloc(sizeof(TimerFdCtx) * g_max_system_fd);
        if (UNLIKELY(!g_timer_fd_ctx_map)) {
            LOG(ERROR) << "Fail to malloc space for timer modules. errno=%d", errno;
            return UBRING_ERR;
        }

        RETURN_CODE ret = TimerCtxMapCompletion();
        if (ret != UBRING_OK) {
            LOG(ERROR) << "Failed to init main data structure of Time Module. ret=" << ret;
            free(g_timer_fd_ctx_map);
            g_timer_fd_ctx_map = nullptr;
            return UBRING_ERR;
        }
    }

    RETURN_CODE ret = StartTimeEpoll();
    if (ret != UBRING_OK) {
        LOG(ERROR) << "Failed to start Timer Epoll. ret=" << ret;
        if (LIKELY(g_timer_fd_ctx_map != nullptr)) {
            FREE_PTR(g_timer_fd_ctx_map);
        }
        return UBRING_ERR;
    }
    g_timer_module_initialized = 1;
    return UBRING_OK;
}

static void DispatchTimer(uint32_t fd, uint32_t generation,
                          bool consume_event) {
    pthread_mutex_lock(&g_timer_mutex);
    if (fd >= g_max_system_fd) {
        pthread_mutex_unlock(&g_timer_mutex);
        return;
    }
    TimerFdCtx& ctx = g_timer_fd_ctx_map[fd];
    if (ctx.generation != generation ||
        ctx.status != TIMER_CONTEXT_EPOLL_WAITING || ctx.canceled) {
        pthread_mutex_unlock(&g_timer_mutex);
        return;
    }
#if defined(OS_LINUX)
    if (consume_event) {
        uint64_t expirations;
        if (read(fd, &expirations, sizeof(expirations)) != sizeof(expirations)) {
            pthread_mutex_unlock(&g_timer_mutex);
            return;
        }
    }
#endif
    auto cb = ctx.cb;
    void* cb_args = ctx.args;
    ctx.status = TIMER_CONTEXT_CALLBACK_ONGOING;
    pthread_mutex_unlock(&g_timer_mutex);

    g_running_timer = &ctx;
    cb(cb_args);
    g_running_timer = nullptr;

    pthread_mutex_lock(&g_timer_mutex);
    if (ctx.canceled || !ctx.periodical) {
        CloseTimerLocked(fd);
    } else {
        ctx.status = TIMER_CONTEXT_EPOLL_WAITING;
        pthread_cond_broadcast(&g_timer_idle);
    }
    pthread_mutex_unlock(&g_timer_mutex);
}

void *TimerEpoll(void *args) {
    UNREFERENCE_PARAM(args);
#if defined(OS_LINUX)
    struct epoll_event ready_events[MAX_TIMER];
#elif defined(OS_MACOSX)
    struct kevent ready_events[MAX_TIMER];
#endif

    while (1) {
        if (g_timer_module_initialized <= 0) {
            LOG(ERROR) << "The Timer module is not initialized.";
            break;
        }

#if defined(OS_LINUX)
        int32_t ready_num = epoll_wait(g_epoll_fd, ready_events, MAX_TIMER,
                                      TIMER_EPOLL_WAIT_TIMEOUT);
#elif defined(OS_MACOSX)
        struct timespec timeout = {0, TIMER_EPOLL_WAIT_TIMEOUT * 1000000};
        int32_t ready_num = kevent(g_epoll_fd, nullptr, 0, ready_events, MAX_TIMER, &timeout);
#endif

        if (UNLIKELY(ready_num == -1)) {
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

        for (int32_t i = 0; i < ready_num; i++) {
#if defined(OS_LINUX)
            struct epoll_event *event = &ready_events[i];
            int32_t timer_fd = static_cast<uint32_t>(event->data.u64);
#elif defined(OS_MACOSX)
            struct kevent *event = &ready_events[i];
            int32_t timer_fd = event->ident;
#endif

#if defined(OS_LINUX)
            const uint32_t generation = event->data.u64 >> 32;
#elif defined(OS_MACOSX)
            const uint32_t generation =
                reinterpret_cast<uintptr_t>(event->udata);
#endif
            DispatchTimer(timer_fd, generation, true);
        }
    }
    return nullptr;
}

void DeleteTimerSafe(uint32_t fd) {
    DeleteTimerInner(fd);
}

void DeleteTimer(uint32_t fd) {
    DeleteTimerInner(fd);
}

int32_t TimerStart(const itimerspec *time, void *(*cb)(void *), void *args) {
    BAIDU_SCOPED_LOCK(g_timer_mutex);
    if (g_epoll_fd == -1) {
        LOG(ERROR) << "Timer epoll/kqueue encountered internal error.";
        return -1;
    }

#if defined(OS_LINUX)
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
#elif defined(OS_MACOSX)
    int timer_fd = timerfd_create_macosx(CLOCK_MONOTONIC, 0);
#endif

    if (UNLIKELY(timer_fd >= (int)g_max_system_fd || timer_fd == -1)) {
        if (timer_fd >= 0) {
            close(timer_fd);
        }
        LOG(ERROR) << "Failed to create timerfd=" << timer_fd << " errno=" << errno;
        return -1;
    }

    ++g_timer_fd_ctx_map[timer_fd].generation;
    g_timer_fd_ctx_map[timer_fd].canceled = false;
    g_timer_fd_ctx_map[timer_fd].periodical = 0;
    g_timer_fd_ctx_map[timer_fd].status = TIMER_CONTEXT_EPOLL_WAITING;
    g_timer_fd_ctx_map[timer_fd].cb = cb;
    g_timer_fd_ctx_map[timer_fd].args = args;
    g_timer_fd_ctx_map[timer_fd].fd = (uint32_t)timer_fd;

    if (LIKELY(time->it_interval.tv_sec > 0 || time->it_interval.tv_nsec > 0)) {
        g_timer_fd_ctx_map[timer_fd].periodical = 1;
    }

#if defined(OS_LINUX)
    const uint64_t token =
        (static_cast<uint64_t>(g_timer_fd_ctx_map[timer_fd].generation) << 32) |
        static_cast<uint32_t>(timer_fd);
    struct epoll_event event = {
        .events = EPOLLIN,
        .data = {.u64 = token}
    };

    int32_t ret = epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, timer_fd, &event);
#elif defined(OS_MACOSX)
    struct kevent event;
    uint64_t timeout_nsec = time->it_value.tv_sec * 1000000000ULL + time->it_value.tv_nsec;
    uint64_t interval_nsec = time->it_interval.tv_sec * 1000000000ULL + time->it_interval.tv_nsec;
    EV_SET(&event, timer_fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0,
           timeout_nsec / 1000000,
           reinterpret_cast<void*>(static_cast<uintptr_t>(g_timer_fd_ctx_map[timer_fd].generation)));
    int32_t ret = kevent(g_epoll_fd, &event, 1, nullptr, 0, nullptr);
#endif

    if (UNLIKELY(ret != 0)) {
        CloseTimerFd(timer_fd);
        LOG(ERROR) << "Failed to add event to epoll/kqueue. errno=" << errno;
        return -1;
    }

    std::atomic_fetch_add(&g_total_timer_num, 1U);

#if defined(OS_LINUX)
    ret = timerfd_settime(timer_fd, 0, time, nullptr);
#elif defined(OS_MACOSX)
    ret = timerfd_settime_macosx(timer_fd, 0, time, nullptr);
#endif

    if (UNLIKELY(ret != 0)) {
#if defined(OS_LINUX)
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, timer_fd, nullptr) != 0) {
#elif defined(OS_MACOSX)
        struct kevent evt;
        EV_SET(&evt, timer_fd, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
        if (kevent(g_epoll_fd, &evt, 1, nullptr, 0, nullptr) != 0) {
#endif
            LOG(ERROR) << "Failed to delete the timer fd=" << timer_fd << " with errno=" << errno;
        }
        CloseTimerFd(timer_fd);
        std::atomic_fetch_sub(&g_total_timer_num, 1U);
        LOG(ERROR) << "Failed to set timer";
        return -1;
    }

    return timer_fd;
}

uint32_t GetActiveTimerNum(void) {
    return std::atomic_load(&g_total_timer_num);
}

void CloseTimerFd(int fd) {
    g_timer_fd_ctx_map[fd].cb = nullptr;
    g_timer_fd_ctx_map[fd].args = nullptr;
    g_timer_fd_ctx_map[fd].status = TIMER_CONTEXT_NOT_USING;
    g_timer_fd_ctx_map[fd].fd = 0;
    g_timer_fd_ctx_map[fd].periodical = 0;
    if (close((int)fd) != 0) {
        LOG(ERROR) << "Failed to close timer fd=" << fd << " errno=" << errno;
        return;
    }
}

void TimerModuleDestroy(void) {
    uint32_t max_fd = g_max_system_fd;
    if (g_timer_fd_ctx_map) {
        for (uint32_t fd = 0; fd < max_fd; fd++) {
            DeleteTimerSafe(fd);
        }
    }
    close(g_epoll_fd);
    g_epoll_fd = -1;
    g_total_timer_num = 0;
    g_timer_module_initialized = 0;
    int32_t ret = pthread_join(g_epoll_execute_thread, nullptr);
    if (ret != EOK) {
        LOG(ERROR) << "Failed to join pthread, during destroying timer module. ret=" << ret;
        return;
    }
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
                                   const itimerspec *new_value,
                                   itimerspec *old_value) {
    if (old_value != nullptr) {
        memset(old_value, 0, sizeof(itimerspec));
    }
    return 0;
}
#endif

}  // namespace ubring
}  // namespace brpc
