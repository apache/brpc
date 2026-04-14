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
#include "brpc/ub/timer/timer_mgr.h"

namespace brpc {
namespace ub {
int32_t g_epoll_fd = -1;
std::atomic<uint32_t> g_total_timer_num;
TimerFdCtx *g_timer_fd_ctx_map = NULL;
uint32_t max_system_fd;
static pthread_t g_epoll_execute_thread;
static int32_t g_timer_module_initialized;

static RETURN_CODE DeleteTimerInner(uint32_t fd)
{
    if (g_timer_fd_ctx_map == NULL) {
        LOG(WARNING) << "The timer is not initialized.";
        return HLC_OK;
    }

    if (g_timer_fd_ctx_map[fd].status == TIMER_CONTEXT_NOT_USING) {
        LOG(WARNING) << "The timer is not using, timerFd=" << fd;
        return HLC_OK;
    }

    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, (int)fd, NULL) != 0) {
        LOG(ERROR) << "Failed to delete the timer fd=" << fd << " with errno=" << errno;
    }

    CloseTimerFd(fd);
    atomic_fetch_sub(&g_total_timer_num, 1);
    return HLC_OK;
}

static RETURN_CODE StartTimeEpoll(void)
{
    g_epoll_fd = epoll_create1(0);
    if (UNLIKELY(g_epoll_fd == -1)) {
        LOG(ERROR) << "Failed to create epoll. errno=" << errno;
        return HLC_ERR;
    }

    int ret = pthread_create(&g_epoll_execute_thread, NULL, TimerEpoll, NULL);
    if (UNLIKELY(ret != 0)) {
        LOG(ERROR) << "Failed to create thread err=" << ret;
        return HLC_ERR;
    }
    return HLC_OK;
}

static RETURN_CODE TimerSpinLocksInit(void)
{
    if (g_timer_fd_ctx_map == NULL) {
        LOG(ERROR) << "Timer module is not fully initialized.";
        return HLC_ERR;
    }

    for (uint32_t fd = 0; fd < max_system_fd; fd++) {
        int ret = pthread_spin_init(&g_timer_fd_ctx_map[fd].spin_lock, PTHREAD_PROCESS_PRIVATE);
        if (ret != EOK) {
            LOG(ERROR) << "Failed to initialize spin lock for fd=" << fd;
            for (uint32_t cleanup_fd = 0; cleanup_fd < fd; cleanup_fd++) {
                pthread_spin_destroy(&g_timer_fd_ctx_map[cleanup_fd].spin_lock);
            }
            return HLC_ERR;
        }
    }
    return HLC_OK;
}

static RETURN_CODE ExecuteCallback(int32_t timer_fd)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    error_t err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (err != 0) {
        LOG(ERROR) << "Failed to set thread detach status when executing callback";
    }

    pthread_t cb_thread;
    err = pthread_create(&cb_thread, &attr, UnifiedCallback, (void *)(&g_timer_fd_ctx_map[timer_fd]));
    if (err != 0) {
        pthread_attr_destroy(&attr);
        LOG(ERROR) << "Failed to create thread while executing callback due to errno=" << err;
        return HLC_ERR;
    }
    pthread_attr_destroy(&attr);
    return HLC_OK;
}

static RETURN_CODE TimerCtxMapCompletion(void)
{
    memset(g_timer_fd_ctx_map, 0,
        sizeof(TimerFdCtx) * max_system_fd);

    RETURN_CODE ret = TimerSpinLocksInit();
    if (ret != HLC_OK) {
        LOG(ERROR) << "Failed to init spin locks for timer module.";
        return HLC_ERR;
    }
    return HLC_OK;
}

RETURN_CODE TimerInit(void)
{
    if (g_timer_module_initialized > 0) {
        return HLC_OK;
    }

    g_total_timer_num.store(0);

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != HLC_OK) {
        LOG(ERROR) << "Failed to get fd";
        return HLC_ERR;
    }
    max_system_fd = (uint32_t)rlim.rlim_cur;

    if (g_timer_fd_ctx_map == NULL) {
        g_timer_fd_ctx_map = (TimerFdCtx *)malloc(sizeof(TimerFdCtx) * max_system_fd);
        if (UNLIKELY(!g_timer_fd_ctx_map)) {
            LOG(ERROR) << "Fail to malloc space for timer modules. errno=%d", errno;
            return HLC_ERR;
        }

        RETURN_CODE ret = TimerCtxMapCompletion();
        if (ret != HLC_OK) {
            LOG(ERROR) << "Failed to init main data structure of Time Module. ret=" << ret;
            free(g_timer_fd_ctx_map);
            g_timer_fd_ctx_map = NULL;
            return HLC_ERR;
        }
    }

    RETURN_CODE ret = StartTimeEpoll();
    if (ret != HLC_OK) {
        LOG(ERROR) << "Failed to start Timer Epoll. ret=" << ret;
        if (LIKELY(g_timer_fd_ctx_map != NULL)) {
            FREE_PTR(g_timer_fd_ctx_map);
        }
        return HLC_ERR;
    }
    g_timer_module_initialized = 1;
    return HLC_OK;
}

void *UnifiedCallback(void *args)
{
    TimerFdCtx *ctx = (TimerFdCtx *)args;
    if (pthread_spin_trylock(&ctx->spin_lock) == 0) {
        if (ctx->status == TIMER_CONTEXT_NOT_USING) {
            pthread_spin_unlock(&ctx->spin_lock);
            return NULL;
        }
        ctx->status = TIMER_CONTEXT_CALLBACK_ONGOING;
        ctx->cb(ctx->args);
        if (ctx->periodical != 1) {
            DeleteTimerInner((uint32_t)ctx->fd);
        }
        pthread_spin_unlock(&ctx->spin_lock);
    } else {
        LOG_EVERY_SECOND(WARNING) << "The context status is " << ctx->status;
        return NULL;
    }
    return NULL;
}

void *TimerEpoll(void *args)
{
    UNREFERENCE_PARAM(args);
    struct epoll_event ready_events[MAX_TIMER];
    while (1) {
        if (g_timer_module_initialized <= 0) {
            LOG(ERROR) << "The Timer module is not initialized.";
            break;
        }
        
        int32_t ready_num = epoll_wait(g_epoll_fd, ready_events, MAX_TIMER, TIMER_EPOLL_WAIT_TIMEOUT);
        if (UNLIKELY(ready_num == -1)) {
            error_t err = errno;
            if (err == EINTR) {
                LOG_EVERY_SECOND(WARNING) << "Epoll wait was interrupted. errno=" << err;
                continue;
            } else if (err == EBADF) {
                LOG(WARNING) << "The Timer module is destroyed.";
                break;
            }
            LOG(ERROR) << "Epoll wait internal error. errno=" << err;
            break;
        }

        for (int32_t i = 0; i < ready_num; i++) {
            struct epoll_event *event = &ready_events[i];
            int32_t timer_fd = event->data.fd;
            uint64_t exp = 0;
            if (read(timer_fd, &exp, sizeof(exp)) < 0) {
                LOG(ERROR) << "Failed to read timerfd=" << timer_fd << " errno=" << errno;
                continue;
            }
            if (TimerFdCtxValidate((uint32_t)timer_fd) != HLC_OK) {
                LOG(ERROR) << "Timer ctx is not valid=" << timer_fd;
                continue;
            }

            RETURN_CODE ret = ExecuteCallback(timer_fd);
            if (ret != HLC_OK) {
                LOG(ERROR) << "Failed execute callback ret=" << ret;
                DeleteTimerInner((uint32_t)timer_fd);
                continue;
            }
        }
    }
    return NULL;
}

void DeleteTimerSafe(uint32_t fd)
{
    if (g_timer_fd_ctx_map == NULL) {
        LOG(WARNING) << "The timer is not initialized.";
        return;
    }

    if (pthread_spin_lock(&g_timer_fd_ctx_map[fd].spin_lock) != 0) {
        LOG(ERROR) << "Failed to lock while deleting timer=" << fd << " errno=" << errno;
        return;
    }

    if (g_timer_fd_ctx_map[fd].status == TIMER_CONTEXT_NOT_USING) {
        LOG(WARNING) << "The timer is not using, timerFd=" << fd;
        pthread_spin_unlock(&g_timer_fd_ctx_map[fd].spin_lock);
        return;
    }

    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, (int)fd, NULL) != 0) {
        LOG(ERROR) << "Failed to delete the timer fd=" << fd << " with errno=" << errno;
    }

    CloseTimerFd(fd);
    atomic_fetch_sub(&g_total_timer_num, 1);

    pthread_spin_unlock(&g_timer_fd_ctx_map[fd].spin_lock);
}
void DeleteTimer(uint32_t fd)
{
    if (g_timer_fd_ctx_map == NULL) {
        LOG(WARNING) << "The timer is not initialized.";
        return;
    }

    g_timer_fd_ctx_map[fd].periodical = 0;
}

int32_t TimerStart(const struct itimerspec *time, void *(*cb)(void *), void *args)
{
    if (g_epoll_fd == -1) {
        LOG(ERROR) << "Timer epoll encountered internal error.";
        return -1;
    }

    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (UNLIKELY(timer_fd >= (int)max_system_fd || timer_fd == -1)) {
        LOG(ERROR) << "Failed to create timerfd=" << timer_fd << " errno=" << errno;
        return -1;
    }

    g_timer_fd_ctx_map[timer_fd].status = TIMER_CONTEXT_EPOLL_WAITING;
    g_timer_fd_ctx_map[timer_fd].cb = cb;
    g_timer_fd_ctx_map[timer_fd].args = args;
    g_timer_fd_ctx_map[timer_fd].fd = (uint32_t)timer_fd;
    
    if (LIKELY(time->it_interval.tv_sec > 0 || time->it_interval.tv_nsec > 0)) {
        g_timer_fd_ctx_map[timer_fd].periodical = 1;
    }

    struct epoll_event event = {
        .events = EPOLLIN,
        .data = {.fd = timer_fd}
    };

    int32_t ret = epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, timer_fd, &event);
    if (UNLIKELY(ret != 0)) {
        CloseTimerFd((uint32_t)timer_fd);
        LOG(ERROR) << "Failed to add event to epoll. errno=" << errno;
        return -1;
    }

    atomic_fetch_add(&g_total_timer_num, 1);

    ret = timerfd_settime(timer_fd, 0, time, NULL);
    if (UNLIKELY(ret != 0)) {
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, timer_fd, NULL) != 0) {
            LOG(ERROR) << "Failed to delete the timer fd=" << timer_fd << " with errno=" << errno;
        }
        CloseTimerFd((uint32_t)timer_fd);
        atomic_fetch_sub(&g_total_timer_num, 1);
        LOG(ERROR) << "Failed to set timer";
        return -1;
    }

    return timer_fd;
}

uint32_t GetActiveTimerNum(void)
{
    return atomic_load(&g_total_timer_num);
}

void CloseTimerFd(uint32_t fd)
{
    g_timer_fd_ctx_map[fd].cb = NULL;
    g_timer_fd_ctx_map[fd].args = NULL;
    g_timer_fd_ctx_map[fd].status = TIMER_CONTEXT_NOT_USING;
    g_timer_fd_ctx_map[fd].fd = 0;
    g_timer_fd_ctx_map[fd].periodical = 0;
    if (close((int)fd) != 0) {
        LOG(ERROR) << "Failed to close timer fd=" << fd << " errno=" << errno;
        return;
    }
}

void TimerModuleDestroy(void)
{
    uint32_t max_fd = max_system_fd;
    if (g_timer_fd_ctx_map) {
        for (uint32_t fd = 0; fd < max_fd; fd++) {
            if (g_timer_fd_ctx_map[fd].status != TIMER_CONTEXT_NOT_USING) {
                DeleteTimerSafe(fd);
            }
        }
    }
    close(g_epoll_fd);
    g_epoll_fd = -1;
    g_total_timer_num = 0;
    g_timer_module_initialized = 0;
    int32_t ret = pthread_join(g_epoll_execute_thread, NULL);
    if (ret != EOK) {
        LOG(ERROR) << "Failed to join pthread, during destroying timer module. ret=" << ret;
        return;
    }
}

RETURN_CODE TimerFdCtxValidate(uint32_t fd)
{
    if (fd >= max_system_fd) {
        LOG(ERROR) << "TimerFd=" << fd << " is out of range=" << max_system_fd;
        return HLC_ERR;
    }
    if (g_timer_fd_ctx_map[fd].status == TIMER_CONTEXT_NOT_USING) {
        LOG(ERROR) << "TimerFd=" << fd << " has wrong status=" << g_timer_fd_ctx_map[fd].status;
        return HLC_ERR;
    }
    if (g_timer_fd_ctx_map[fd].cb == NULL) {
        LOG(ERROR) << "The callback is not set.";
        return HLC_ERR;
    }

    return HLC_OK;
}
}
}