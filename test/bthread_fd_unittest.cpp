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

#include "butil/compat.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>                           // uname
#include <fcntl.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "butil/logging.h"
#include "bthread/task_control.h"
#include "bthread/task_group.h"
#include "bthread/interrupt_pthread.h"
#include "bthread/bthread.h"
#include "bthread/unstable.h"
#if defined(OS_MACOSX)
#include <sys/types.h>                           // struct kevent
#include <sys/event.h>                           // kevent(), kqueue()
#endif

#ifndef NDEBUG
namespace bthread {
extern butil::atomic<int> break_nums;
extern TaskControl* global_task_control;
int stop_and_join_epoll_threads();
}
#endif

namespace {
TEST(FDTest, read_kernel_version) {
    utsname name;
    uname(&name);
    std::cout << "sysname=" << name.sysname << std::endl
         << "nodename=" << name.nodename << std::endl
         << "release=" << name.release << std::endl
         << "version=" << name.version << std::endl
         << "machine=" << name.machine << std::endl;
}

#define RUN_CLIENT_IN_BTHREAD 1
//#define USE_BLOCKING_EPOLL 1
//#define RUN_EPOLL_IN_BTHREAD 1
//#define CREATE_THREAD_TO_PROCESS 1

volatile bool stop = false;

struct SocketMeta {
    int fd;
    int epfd;
};

struct BAIDU_CACHELINE_ALIGNMENT ClientMeta {
    int fd;
    size_t count;
    size_t times;
};

struct EpollMeta {
    int epfd;
};

const size_t NCLIENT = 30;
void* process_thread(void* arg) {
    SocketMeta* m = (SocketMeta*)arg;
    size_t count;
    //printf("begin to process fd=%d\n", m->fd);
    ssize_t n = read(m->fd, &count, sizeof(count));
    if (n != sizeof(count)) {
        LOG(FATAL) << "Should not happen in this test";
        return NULL;
    }
    count += NCLIENT;
    //printf("write result=%lu to fd=%d\n", count, m->fd);
    if (write(m->fd, &count, sizeof(count)) != sizeof(count)) {
        LOG(FATAL) << "Should not happen in this test";
        return NULL;
    }
#ifdef CREATE_THREAD_TO_PROCESS
# if defined(OS_LINUX)
    epoll_event evt = { EPOLLIN | EPOLLONESHOT, { m } };
    if (epoll_ctl(m->epfd, EPOLL_CTL_MOD, m->fd, &evt) < 0) {
        epoll_ctl(m->epfd, EPOLL_CTL_ADD, m->fd, &evt);
    }
# elif defined(OS_MACOSX)
    struct kevent kqueue_event;
    EV_SET(&kqueue_event, m->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_ONESHOT,
            0, 0, m);
    kevent(m->epfd, &kqueue_event, 1, NULL, 0, NULL);
# endif
#endif
    return NULL;
}

void* epoll_thread(void* arg) {
    bthread_usleep(1);
    EpollMeta* m = (EpollMeta*)arg;
    const int epfd = m->epfd;
#if defined(OS_LINUX)
    epoll_event e[32];
#elif defined(OS_MACOSX)
    struct kevent e[32];
#endif

    while (!stop) {

#if defined(OS_LINUX)
# ifndef USE_BLOCKING_EPOLL
        const int n = epoll_wait(epfd, e, ARRAY_SIZE(e), 0);
        if (stop) {
            break;
        }
        if (n == 0) {
            bthread_fd_wait(epfd, EPOLLIN);
            continue;
        }
# else
        const int n = epoll_wait(epfd, e, ARRAY_SIZE(e), -1);
        if (stop) {
            break;
        }
        if (n == 0) {
            continue;
        }
# endif
#elif defined(OS_MACOSX)
        const int n = kevent(epfd, NULL, 0, e, ARRAY_SIZE(e), NULL);
        if (stop) {
            break;
        }
        if (n == 0) {
            continue;
        }
#endif
        if (n < 0) {
            if (EINTR == errno) {
                continue;
            }
#if defined(OS_LINUX)
            PLOG(FATAL) << "Fail to epoll_wait";
#elif defined(OS_MACOSX)
            PLOG(FATAL) << "Fail to kevent";
#endif
            break;
        }

#ifdef CREATE_THREAD_TO_PROCESS
        bthread_fvec vec[n];
        for (int i = 0; i < n; ++i) {
            vec[i].fn = process_thread;
# if defined(OS_LINUX)
            vec[i].arg = e[i].data.ptr;
# elif defined(OS_MACOSX)
            vec[i].arg = e[i].udata;
# endif
        }
        bthread_t tid[n];
        bthread_startv(tid, vec, n, &BTHREAD_ATTR_SMALL);
#else
        for (int i = 0; i < n; ++i) {
# if defined(OS_LINUX)
            process_thread(e[i].data.ptr);
# elif defined(OS_MACOSX)
            process_thread(e[i].udata);
# endif 
        }
#endif        
    }
    return NULL;
}

void* client_thread(void* arg) {
    ClientMeta* m = (ClientMeta*)arg;
    for (size_t i = 0; i < m->times; ++i) {
        if (write(m->fd, &m->count, sizeof(m->count)) != sizeof(m->count)) {
            LOG(FATAL) << "Should not happen in this test";
            return NULL;
        }
#ifdef RUN_CLIENT_IN_BTHREAD
        ssize_t rc;
        do {
# if defined(OS_LINUX)
            const int wait_rc = bthread_fd_wait(m->fd, EPOLLIN);
# elif defined(OS_MACOSX)
            const int wait_rc = bthread_fd_wait(m->fd, EVFILT_READ);
# endif
            EXPECT_EQ(0, wait_rc) << berror();
            rc = read(m->fd, &m->count, sizeof(m->count));
        } while (rc < 0 && errno == EAGAIN);
#else
        ssize_t rc = read(m->fd, &m->count, sizeof(m->count));
#endif
        if (rc != sizeof(m->count)) {
            PLOG(FATAL) << "Should not happen in this test, rc=" << rc;
            return NULL;
        }
    }
    return NULL;
}

inline uint32_t fmix32 ( uint32_t h ) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

// Disable temporarily due to epoll's bug. The bug is fixed by
// a kernel patch that lots of machines currently don't have
TEST(FDTest, ping_pong) {
#ifndef NDEBUG
    bthread::break_nums = 0;
#endif

    const size_t REP = 30000;
    const size_t NEPOLL = 2;

    int epfd[NEPOLL];
#ifdef RUN_EPOLL_IN_BTHREAD
    bthread_t eth[NEPOLL];
#else
    pthread_t eth[NEPOLL];
#endif
    int fds[2 * NCLIENT];
#ifdef RUN_CLIENT_IN_BTHREAD
    bthread_t cth[NCLIENT];
#else
    pthread_t cth[NCLIENT];
#endif
    ClientMeta* cm[NCLIENT];

    for (size_t i = 0; i < NEPOLL; ++i) {
#if defined(OS_LINUX)
        epfd[i] = epoll_create(1024);
#elif defined(OS_MACOSX)
        epfd[i] = kqueue();
#endif
        ASSERT_GT(epfd[i], 0);
    }
    
    for (size_t i = 0; i < NCLIENT; ++i) {
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds + 2 * i));
        //printf("Created fd=%d,%d i=%lu\n", fds[2*i], fds[2*i+1], i);
        SocketMeta* m = new SocketMeta;
        m->fd = fds[i * 2];
        m->epfd = epfd[fmix32(i) % NEPOLL];
        ASSERT_EQ(0, fcntl(m->fd, F_SETFL, fcntl(m->fd, F_GETFL, 0) | O_NONBLOCK));

#ifdef CREATE_THREAD_TO_PROCESS
# if defined(OS_LINUX)
        epoll_event evt = { EPOLLIN | EPOLLONESHOT, { m } };
# elif defined(OS_MACOSX)
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, m->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_ONESHOT,
                0, 0, m);
# endif
#else
# if defined(OS_LINUX)
        epoll_event evt = { EPOLLIN, { m } };
# elif defined(OS_MACOSX)
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, m->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, m);
# endif
#endif

#if defined(OS_LINUX)
        ASSERT_EQ(0, epoll_ctl(m->epfd, EPOLL_CTL_ADD, m->fd, &evt));
#elif defined(OS_MACOSX)
        ASSERT_EQ(0, kevent(m->epfd, &kqueue_event, 1, NULL, 0, NULL));
#endif
        cm[i] = new ClientMeta;
        cm[i]->fd = fds[i * 2 + 1];
        cm[i]->count = i;
        cm[i]->times = REP;
#ifdef RUN_CLIENT_IN_BTHREAD
        butil::make_non_blocking(cm[i]->fd);
        ASSERT_EQ(0, bthread_start_urgent(&cth[i], NULL, client_thread, cm[i]));
#else
        ASSERT_EQ(0, pthread_create(&cth[i], NULL, client_thread, cm[i]));
#endif
    }

    ProfilerStart("ping_pong.prof");
    butil::Timer tm;
    tm.start();

    for (size_t i = 0; i < NEPOLL; ++i) {
        EpollMeta *em = new EpollMeta;
        em->epfd = epfd[i];
#ifdef RUN_EPOLL_IN_BTHREAD
        ASSERT_EQ(0, bthread_start_urgent(&eth[i], epoll_thread, em, NULL);
#else
        ASSERT_EQ(0, pthread_create(&eth[i], NULL, epoll_thread, em));
#endif
    }

    for (size_t i = 0; i < NCLIENT; ++i) {
#ifdef RUN_CLIENT_IN_BTHREAD
        bthread_join(cth[i], NULL);
#else
        pthread_join(cth[i], NULL);
#endif
        ASSERT_EQ(i + REP * NCLIENT, cm[i]->count);
    }
    tm.stop();
    ProfilerStop();
    LOG(INFO) << "tid=" << REP*NCLIENT*1000000L/tm.u_elapsed();
    stop = true;
    for (size_t i = 0; i < NEPOLL; ++i) {
#if defined(OS_LINUX)
        epoll_event evt = { EPOLLOUT,  { NULL } };
        ASSERT_EQ(0, epoll_ctl(epfd[i], EPOLL_CTL_ADD, 0, &evt));
#elif defined(OS_MACOSX)
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, 0, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
        ASSERT_EQ(0, kevent(epfd[i], &kqueue_event, 1, NULL, 0, NULL));
#endif
#ifdef RUN_EPOLL_IN_BTHREAD
        bthread_join(eth[i], NULL);
#else
        pthread_join(eth[i], NULL);
#endif
    }
    //bthread::stop_and_join_epoll_threads();
    bthread_usleep(100000);

#ifndef NDEBUG
    std::cout << "break_nums=" << bthread::break_nums << std::endl;
#endif
}

TEST(FDTest, mod_closed_fd) {
#if defined(OS_LINUX)
    // Conclusion:
    //   If fd is never added into epoll, MOD returns ENOENT
    //   If fd is inside epoll and valid, MOD returns 0
    //   If fd is closed and not-reused, MOD returns EBADF
    //   If fd is closed and reused, MOD returns ENOENT again
    
    const int epfd = epoll_create(1024);
    int new_fd[2];
    int fd[2];
    ASSERT_EQ(0, pipe(fd));
    epoll_event e = { EPOLLIN, { NULL } };
    errno = 0;
    ASSERT_EQ(-1, epoll_ctl(epfd, EPOLL_CTL_MOD, fd[0], &e));
    ASSERT_EQ(ENOENT, errno);
    ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_ADD, fd[0], &e));
    // mod after add
    ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_MOD, fd[0], &e));
    // mod after mod
    ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_MOD, fd[0], &e));
    ASSERT_EQ(0, close(fd[0]));
    ASSERT_EQ(0, close(fd[1]));

    errno = 0;
    ASSERT_EQ(-1, epoll_ctl(epfd, EPOLL_CTL_MOD, fd[0], &e));
    ASSERT_EQ(EBADF, errno) << berror();

    ASSERT_EQ(0, pipe(new_fd));
    ASSERT_EQ(fd[0], new_fd[0]);
    ASSERT_EQ(fd[1], new_fd[1]);
    
    errno = 0;
    ASSERT_EQ(-1, epoll_ctl(epfd, EPOLL_CTL_MOD, fd[0], &e));
    ASSERT_EQ(ENOENT, errno) << berror();
    
    ASSERT_EQ(0, close(epfd));
#endif
}

TEST(FDTest, add_existing_fd) {
#if defined(OS_LINUX)
    const int epfd = epoll_create(1024);
    epoll_event e = { EPOLLIN, { NULL } };
    ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &e));
    errno = 0;
    ASSERT_EQ(-1, epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &e));
    ASSERT_EQ(EEXIST, errno);
    ASSERT_EQ(0, close(epfd));
#endif
}

void* epoll_waiter(void* arg) {
#if defined(OS_LINUX)
    epoll_event e;
    if (1 == epoll_wait((int)(intptr_t)arg, &e, 1, -1)) {
        std::cout << e.events << std::endl;
    }
#elif defined(OS_MACOSX)
    struct kevent e;
    if (1 == kevent((int)(intptr_t)arg, NULL, 0, &e, 1, NULL)) {
        std::cout << e.flags << std::endl;
    }
#endif
    std::cout << pthread_self() << " quits" << std::endl;
    return NULL;
}

TEST(FDTest, interrupt_pthread) {
#if defined(OS_LINUX)
    const int epfd = epoll_create(1024);
#elif defined(OS_MACOSX)
    const int epfd = kqueue();
#endif
    pthread_t th, th2;
    ASSERT_EQ(0, pthread_create(&th, NULL, epoll_waiter, (void*)(intptr_t)epfd));
    ASSERT_EQ(0, pthread_create(&th2, NULL, epoll_waiter, (void*)(intptr_t)epfd));
    bthread_usleep(100000L);
    std::cout << "wake up " << th << std::endl;
    bthread::interrupt_pthread(th);
    bthread_usleep(100000L);
    std::cout << "wake up " << th2 << std::endl;
    bthread::interrupt_pthread(th2);
    pthread_join(th, NULL);
    pthread_join(th2, NULL);
}

void* close_the_fd(void* arg) {
    bthread_usleep(10000/*10ms*/);
    EXPECT_EQ(0, bthread_close(*(int*)arg));
    return NULL;
}

TEST(FDTest, invalid_epoll_events) {
    errno = 0;
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_wait(-1, EPOLLIN));
#elif defined(OS_MACOSX)
    ASSERT_EQ(-1, bthread_fd_wait(-1, EVFILT_READ));
#endif
    ASSERT_EQ(EINVAL, errno);
    errno = 0;
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_timedwait(-1, EPOLLIN, NULL));
#elif defined(OS_MACOSX)
    ASSERT_EQ(-1, bthread_fd_timedwait(-1, EVFILT_READ, NULL));
#endif
    ASSERT_EQ(EINVAL, errno);

    int fds[2];
    ASSERT_EQ(0, pipe(fds));
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_wait(fds[0], EPOLLET));
    ASSERT_EQ(EINVAL, errno);
#endif
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, close_the_fd, &fds[1]));
    butil::Timer tm;
    tm.start();
#if defined(OS_LINUX)
    ASSERT_EQ(0, bthread_fd_wait(fds[0], EPOLLIN | EPOLLET));
#elif defined(OS_MACOSX)
    ASSERT_EQ(0, bthread_fd_wait(fds[0], EVFILT_READ));
#endif
    tm.stop();
    ASSERT_LT(tm.m_elapsed(), 20);
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_close(fds[0]));
}

void* wait_for_the_fd(void* arg) {
    timespec ts = butil::milliseconds_from_now(50);
#if defined(OS_LINUX)
    bthread_fd_timedwait(*(int*)arg, EPOLLIN, &ts);
#elif defined(OS_MACOSX)
    bthread_fd_timedwait(*(int*)arg, EVFILT_READ, &ts);
#endif
    return NULL;
}

TEST(FDTest, timeout) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    pthread_t th;
    ASSERT_EQ(0, pthread_create(&th, NULL, wait_for_the_fd, &fds[0]));
    bthread_t bth;
    ASSERT_EQ(0, bthread_start_urgent(&bth, NULL, wait_for_the_fd, &fds[0]));
    butil::Timer tm;
    tm.start();
    ASSERT_EQ(0, pthread_join(th, NULL));
    ASSERT_EQ(0, bthread_join(bth, NULL));
    tm.stop();
    ASSERT_LT(tm.m_elapsed(), 80);
    ASSERT_EQ(0, bthread_close(fds[0]));
    ASSERT_EQ(0, bthread_close(fds[1]));
}

TEST(FDTest, close_should_wakeup_waiter) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    bthread_t bth;
    ASSERT_EQ(0, bthread_start_urgent(&bth, NULL, wait_for_the_fd, &fds[0]));
    butil::Timer tm;
    tm.start();
    ASSERT_EQ(0, bthread_close(fds[0]));
    ASSERT_EQ(0, bthread_join(bth, NULL));
    tm.stop();
    ASSERT_LT(tm.m_elapsed(), 5);

    // Launch again, should quit soon due to EBADF
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_timedwait(fds[0], EPOLLIN, NULL));
#elif defined(OS_MACOSX)
    ASSERT_EQ(-1, bthread_fd_timedwait(fds[0], EVFILT_READ, NULL));
#endif
    ASSERT_EQ(EBADF, errno);

    ASSERT_EQ(0, bthread_close(fds[1]));
}

TEST(FDTest, close_definitely_invalid) {
    int ec = 0;
    ASSERT_EQ(-1, close(-1));
    ec = errno;
    ASSERT_EQ(-1, bthread_close(-1));
    ASSERT_EQ(ec, errno);
}

TEST(FDTest, bthread_close_fd_which_did_not_call_bthread_functions) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    ASSERT_EQ(0, bthread_close(fds[0]));
    ASSERT_EQ(0, bthread_close(fds[1]));
}

TEST(FDTest, double_close) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    ASSERT_EQ(0, close(fds[0]));
    int ec = 0;
    ASSERT_EQ(-1, close(fds[0]));
    ec = errno;
    ASSERT_EQ(0, bthread_close(fds[1]));
    ASSERT_EQ(-1, bthread_close(fds[1]));
    ASSERT_EQ(ec, errno);
}
} // namespace
