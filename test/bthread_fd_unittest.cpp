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
#include "butil/compiler_specific.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>                           // uname
#include <fcntl.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <memory>
#include "gperftools_helper.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include <butil/endpoint.h>
#include <butil/fd_guard.h>
#include "butil/logging.h"
#include "bthread/task_control.h"
#include "bthread/task_group.h"
#include "bthread/interrupt_pthread.h"
#include "bthread/bthread.h"
#include "bthread/unstable.h"
#include <netinet/tcp.h>
#if defined(OS_MACOSX)
#include <sys/types.h>                           // struct kevent
#include <sys/event.h>                           // kevent(), kqueue()
#include <netinet/tcp_fsm.h>
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
        return nullptr;
    }
    count += NCLIENT;
    //printf("write result=%lu to fd=%d\n", count, m->fd);
    if (write(m->fd, &count, sizeof(count)) != sizeof(count)) {
        LOG(FATAL) << "Should not happen in this test";
        return nullptr;
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
    kevent(m->epfd, &kqueue_event, 1, nullptr, 0, nullptr);
# endif
#endif
    return nullptr;
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
        const int n = kevent(epfd, nullptr, 0, e, ARRAY_SIZE(e), nullptr);
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
    return nullptr;
}

void* client_thread(void* arg) {
    ClientMeta* m = (ClientMeta*)arg;
    for (size_t i = 0; i < m->times; ++i) {
        if (write(m->fd, &m->count, sizeof(m->count)) != sizeof(m->count)) {
            LOG(FATAL) << "Should not happen in this test";
            return nullptr;
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
            return nullptr;
        }
    }
    return nullptr;
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
    std::unique_ptr<ClientMeta> cm[NCLIENT];
    std::unique_ptr<SocketMeta> sm[NCLIENT];
    std::unique_ptr<EpollMeta> em_arr[NEPOLL];

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
        sm[i].reset(new SocketMeta);
        SocketMeta* m = sm[i].get();
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
        ASSERT_EQ(0, kevent(m->epfd, &kqueue_event, 1, nullptr, 0, nullptr));
#endif
        cm[i].reset(new ClientMeta);
        cm[i]->fd = fds[i * 2 + 1];
        cm[i]->count = i;
        cm[i]->times = REP;
#ifdef RUN_CLIENT_IN_BTHREAD
        butil::make_non_blocking(cm[i]->fd);
        ASSERT_EQ(0, bthread_start_urgent(&cth[i], nullptr, client_thread, cm[i].get()));
#else
        ASSERT_EQ(0, pthread_create(&cth[i], nullptr, client_thread, cm[i].get()));
#endif
    }

    ProfilerStart("ping_pong.prof");
    butil::Timer tm;
    tm.start();

    for (size_t i = 0; i < NEPOLL; ++i) {
        em_arr[i].reset(new EpollMeta);
        EpollMeta* em = em_arr[i].get();
        em->epfd = epfd[i];
#ifdef RUN_EPOLL_IN_BTHREAD
        ASSERT_EQ(0, bthread_start_urgent(&eth[i], epoll_thread, em, nullptr);
#else
        ASSERT_EQ(0, pthread_create(&eth[i], nullptr, epoll_thread, em));
#endif
    }

    for (size_t i = 0; i < NCLIENT; ++i) {
#ifdef RUN_CLIENT_IN_BTHREAD
        bthread_join(cth[i], nullptr);
#else
        pthread_join(cth[i], nullptr);
#endif
        ASSERT_EQ(i + REP * NCLIENT, cm[i]->count);
    }
    tm.stop();
    ProfilerStop();
    LOG(INFO) << "tid=" << REP*NCLIENT * 1000000L / tm.u_elapsed();
    stop = true;
    for (size_t i = 0; i < NEPOLL; ++i) {
#if defined(OS_LINUX)
        epoll_event evt = { EPOLLOUT,  { nullptr } };
        ASSERT_EQ(0, epoll_ctl(epfd[i], EPOLL_CTL_ADD, 0, &evt));
#elif defined(OS_MACOSX)
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, 0, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        ASSERT_EQ(0, kevent(epfd[i], &kqueue_event, 1, nullptr, 0, nullptr));
#endif
#ifdef RUN_EPOLL_IN_BTHREAD
        bthread_join(eth[i], nullptr);
#else
        pthread_join(eth[i], nullptr);
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
    epoll_event e = { EPOLLIN, { nullptr } };
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
    epoll_event e = { EPOLLIN, { nullptr } };
    ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &e));
    errno = 0;
    ASSERT_EQ(-1, epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &e));
    ASSERT_EQ(EEXIST, errno);
    ASSERT_EQ(0, close(epfd));
#endif
}

struct EpollWaitArg {
    int epfd;
    butil::atomic<bool> done{false};
    int result = 0;
    int error = 0;
};

void* epoll_waiter(void* arg) {
    EpollWaitArg* a = static_cast<EpollWaitArg*>(arg);
#if defined(OS_LINUX)
    epoll_event e;
    a->result = epoll_wait(a->epfd, &e, 1, 10000);
#elif defined(OS_MACOSX)
    struct kevent e;
    timespec timeout = {10, 0};
    a->result = kevent(a->epfd, nullptr, 0, &e, 1, &timeout);
#endif
    a->error = errno;
    a->done.store(true, butil::memory_order_release);
    return nullptr;
}

TEST(FDTest, interrupt_pthread) {
#if defined(OS_LINUX)
    butil::fd_guard epfd(epoll_create(1024));
#elif defined(OS_MACOSX)
    butil::fd_guard epfd(kqueue());
#endif
    ASSERT_GE(epfd, 0);
    EpollWaitArg args[2];
    pthread_t threads[2];
    size_t started = 0;
    for (; started < ARRAY_SIZE(threads); ++started) {
        args[started].epfd = epfd;
        int rc = pthread_create(&threads[started], nullptr,
                                      epoll_waiter, &args[started]);
        EXPECT_EQ(0, rc);
        if (rc != 0) {
            break;
        }
    }
    int64_t deadline = butil::cpuwide_time_us() + 15000000L;
    for (size_t i = 0; i < started; ++i) {
        // Signals are not persistent. Retry until the syscall observes one;
        // keep the pthread joinable until all signalling is finished.
        while (!args[i].done.load(butil::memory_order_acquire) &&
               butil::cpuwide_time_us() < deadline) {
            int rc = bthread::interrupt_pthread(threads[i]);
            if (rc != 0) {
                // The waiter may finish between the check above and the
                // signal, in which case interruption is no longer needed.
                // Why it stopped waiting is checked on args[i] below.
                EXPECT_EQ(ESRCH, rc) << berror(rc);
                break;
            }
            bthread_usleep(1000);
        }
        EXPECT_EQ(0, pthread_join(threads[i], nullptr));
    }
    for (size_t i = 0; i < started; ++i) {
        ASSERT_EQ(-1, args[i].result);
        ASSERT_EQ(EINTR, args[i].error);
    }
}

void* close_the_fd(void* arg) {
    bthread_usleep(10000/*10ms*/);
    EXPECT_EQ(0, bthread_close(*(int*)arg));
    return nullptr;
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
    ASSERT_EQ(-1, bthread_fd_timedwait(-1, EPOLLIN, nullptr));
#elif defined(OS_MACOSX)
    ASSERT_EQ(-1, bthread_fd_timedwait(-1, EVFILT_READ, nullptr));
#endif
    ASSERT_EQ(EINVAL, errno);

    int fds[2];
    ASSERT_EQ(0, pipe(fds));
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_wait(fds[0], EPOLLET));
    ASSERT_EQ(EINVAL, errno);
#endif
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, close_the_fd, &fds[1]));
    butil::Timer tm;
    tm.start();
#if defined(OS_LINUX)
    ASSERT_EQ(0, bthread_fd_wait(fds[0], EPOLLIN | EPOLLET));
#elif defined(OS_MACOSX)
    ASSERT_EQ(0, bthread_fd_wait(fds[0], EVFILT_READ));
#endif
    tm.stop();
    // Successful readiness, not scheduler latency, is the contract.
    ASSERT_EQ(0, bthread_join(th, nullptr));
    ASSERT_EQ(0, bthread_close(fds[0]));
}

struct FDWaitArg {
    int fd;
    int timeout_ms;
    int result = 0;
    int error = 0;
};

void* wait_for_the_fd(void* arg) {
    FDWaitArg* a = static_cast<FDWaitArg*>(arg);
    timespec ts = butil::milliseconds_from_now(a->timeout_ms);
#if defined(OS_LINUX)
    a->result = bthread_fd_timedwait(a->fd, EPOLLIN, &ts);
#elif defined(OS_MACOSX)
    a->result = bthread_fd_timedwait(a->fd, EVFILT_READ, &ts);
#endif
    a->error = errno;
    if (a->result == -1 && a->error == ETIMEDOUT) {
        EXPECT_GE(butil::gettimeofday_us(), butil::timespec_to_microseconds(ts));
    }
    return nullptr;
}

TEST(FDTest, timeout) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    FDWaitArg args[2];
    for (auto& arg : args) {
        arg.fd = fds[0];
        arg.timeout_ms = 50;
    }
    pthread_t th;
    ASSERT_EQ(0, pthread_create(&th, nullptr, wait_for_the_fd, &args[0]));
    bthread_t bth;
    ASSERT_EQ(0, bthread_start_urgent(&bth, nullptr, wait_for_the_fd, &args[1]));
    ASSERT_EQ(0, pthread_join(th, nullptr));
    ASSERT_EQ(0, bthread_join(bth, nullptr));
    ASSERT_EQ(0, bthread_close(fds[0]));
    ASSERT_EQ(0, bthread_close(fds[1]));
    for (auto& arg : args) {
        ASSERT_EQ(-1, arg.result);
        ASSERT_EQ(ETIMEDOUT, arg.error);
    }
}

TEST(FDTest, close_should_wakeup_waiter) {
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    FDWaitArg arg;
    arg.fd = fds[0];
    arg.timeout_ms = 10000;
    bthread_t bth;
    ASSERT_EQ(0, bthread_start_urgent(&bth, nullptr, wait_for_the_fd, &arg));
    auto* meta = bthread::TaskGroup::address_meta(bth);
    int64_t deadline = butil::cpuwide_time_us() + 5000000L;
    while (meta->current_waiter.load(butil::memory_order_acquire) == nullptr &&
           butil::cpuwide_time_us() < deadline) {
        bthread_usleep(1000);
    }
    // Keep cleanup reachable even if the waiter did not register in time.
    EXPECT_NE(nullptr, meta->current_waiter.load(butil::memory_order_acquire));
    ASSERT_EQ(0, bthread_close(fds[0]));
    ASSERT_EQ(0, bthread_join(bth, nullptr));
    ASSERT_EQ(0, arg.result) << "errno=" << arg.error;

    // Launch again, should quit soon due to EBADF
#if defined(OS_LINUX)
    ASSERT_EQ(-1, bthread_fd_timedwait(fds[0], EPOLLIN, nullptr));
#elif defined(OS_MACOSX)
    ASSERT_EQ(-1, bthread_fd_timedwait(fds[0], EVFILT_READ, nullptr));
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

// Local listeners keep connect tests independent of DNS, Internet latency,
// and the assumption that a connection cannot complete within one millisecond.
void TestLocalConnect(bool timed) {
    butil::EndPoint endpoint;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:0", &endpoint));
    butil::fd_guard listener(butil::tcp_listen(endpoint));
    ASSERT_GE(listener, 0);
    ASSERT_EQ(0, butil::get_local_side(listener, &endpoint));
    struct sockaddr_storage address{};
    socklen_t length = 0;
    ASSERT_EQ(0, endpoint2sockaddr(endpoint, &address, &length));
    butil::fd_guard client(socket(address.ss_family, SOCK_STREAM, 0));
    ASSERT_GE(client, 0);
    bool was_blocking = butil::is_blocking(client);
    timespec deadline = butil::seconds_from_now(10);
    int rc = bthread_timed_connect(
        client, reinterpret_cast<sockaddr*>(&address), length,
        timed ? &deadline : nullptr);
    ASSERT_EQ(0, rc) << "errno=" << errno;
    ASSERT_EQ(was_blocking, butil::is_blocking(client));
    ASSERT_EQ(0, butil::is_connected(client));
    // The handshake does not require a concurrent accept thread.
    butil::fd_guard accepted(accept(listener, nullptr, nullptr));
    ASSERT_GE(accepted, 0);
}

TEST(FDTest, bthread_connect) {
    TestLocalConnect(false);
    TestLocalConnect(true);
}

#if defined(OS_LINUX)
TEST(FDTest, connect_timeout_with_full_accept_queue) {
    butil::EndPoint endpoint;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:0", &endpoint));
    butil::fd_guard listener(butil::tcp_listen(endpoint));
    ASSERT_GE(listener, 0);
    // Linux permits one queued connection for backlog=0. Leave it unaccepted
    // so the next handshake cannot complete; no external network is needed.
    ASSERT_EQ(0, listen(listener, 0));
    ASSERT_EQ(0, butil::get_local_side(listener, &endpoint));
    sockaddr_storage address{};
    socklen_t length = 0;
    ASSERT_EQ(0, endpoint2sockaddr(endpoint, &address, &length));
    butil::fd_guard queued(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_GE(queued, 0);
    timespec setup_deadline = butil::seconds_from_now(5);
    ASSERT_EQ(0, bthread_timed_connect(queued,
        reinterpret_cast<sockaddr*>(&address), length, &setup_deadline));
    butil::fd_guard client(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_GE(client, 0);
    timespec deadline = butil::milliseconds_from_now(50);
    int rc = bthread_timed_connect(client,
        reinterpret_cast<sockaddr*>(&address), length, &deadline);
    int error = errno;
    ASSERT_EQ(-1, rc);
    // tcp_abort_on_overflow may turn an accept-queue overflow into an RST.
    ASSERT_TRUE(error == ETIMEDOUT || error == ECONNREFUSED)
        << "errno=" << error;
    EXPECT_TRUE(butil::is_blocking(client));
}
#endif

void TestConnectInterruptImpl(bool timed) {
    // Stop must precede connect even if the task starts on another worker
    // immediately. Yield does not consume the pending interruption.
    while (!bthread_stopped(bthread_self())) {
        bthread_yield();
    }
    TestLocalConnect(timed);
}

void* ConnectThread(void* arg) {
    bool timed = *(bool*)arg;
    TestConnectInterruptImpl(timed);
    return nullptr;
}

void TestConnectInterrupt(bool timed) {
    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(&tid, nullptr, ConnectThread, &timed));
    ASSERT_EQ(0, bthread_stop(tid));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
}

TEST(FDTest, interrupt) {
    TestConnectInterrupt(false);
    TestConnectInterrupt(true);
}

} // namespace
