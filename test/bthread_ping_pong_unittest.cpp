// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/compat.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/errno.h"
#include <bthread/sys_futex.h>
#include <bthread/butex.h>
#include "bthread/bthread.h"
#include "butil/atomicops.h"

namespace {
DEFINE_int32(thread_num, 1, "#pairs of threads doing ping pong");
DEFINE_bool(loop, false, "run until ctrl-C is pressed");
DEFINE_bool(use_futex, false, "use futex instead of pipe");
DEFINE_bool(use_butex, false, "use butex instead of pipe");

void ALLOW_UNUSED (*ignore_sigpipe)(int) = signal(SIGPIPE, SIG_IGN);

volatile bool stop = false;
void quit_handler(int) {
    stop = true;
}

struct BAIDU_CACHELINE_ALIGNMENT AlignedIntWrapper {
    int value;
};

struct BAIDU_CACHELINE_ALIGNMENT PlayerArg {
    int read_fd;
    int write_fd;
    int* wait_addr;
    int* wake_addr;
    long counter;
    long wakeup;
};

void* pipe_player(void* void_arg) {
    PlayerArg* arg = static_cast<PlayerArg*>(void_arg);
    char dummy = '\0';
    while (1) {
        ssize_t nr = read(arg->read_fd, &dummy, 1);
        if (nr <= 0) {
            if (nr == 0) {
                printf("[%" PRIu64 "] EOF\n", pthread_numeric_id());
                break;
            }
            if (errno != EINTR) {
                printf("[%" PRIu64 "] bad read, %m\n", pthread_numeric_id());
                break;
            }
            continue;
        }
        if (1L != write(arg->write_fd, &dummy, 1)) {
            printf("[%" PRIu64 "] bad write, %m\n", pthread_numeric_id());
            break;
        }
        ++arg->counter;
    }
    return NULL;
}

static const int INITIAL_FUTEX_VALUE = 0;

void* futex_player(void* void_arg) {
    PlayerArg* arg = static_cast<PlayerArg*>(void_arg);
    int counter = INITIAL_FUTEX_VALUE;
    while (!stop) {
        int rc = bthread::futex_wait_private(arg->wait_addr, counter, NULL);
        ++counter;
        ++*arg->wake_addr;
        bthread::futex_wake_private(arg->wake_addr, 1);
        ++arg->counter;
        arg->wakeup += (rc == 0);
    }
    return NULL;
}

void* butex_player(void* void_arg) {
    PlayerArg* arg = static_cast<PlayerArg*>(void_arg);
    int counter = INITIAL_FUTEX_VALUE;
    while (!stop) {
        int rc = bthread::butex_wait(arg->wait_addr, counter, NULL);
        ++counter;
        ++*arg->wake_addr;
        bthread::butex_wake(arg->wake_addr);
        ++arg->counter;
        arg->wakeup += (rc == 0);
    }
    return NULL;
}

TEST(PingPongTest, ping_pong) {
    signal(SIGINT, quit_handler);
    stop = false;
    PlayerArg* args[FLAGS_thread_num];

    for (int i = 0; i < FLAGS_thread_num; ++i) {
        int pipe1[2];
        int pipe2[2];
        if (!FLAGS_use_futex && !FLAGS_use_butex) {
            ASSERT_EQ(0, pipe(pipe1));
            ASSERT_EQ(0, pipe(pipe2));
        }

        PlayerArg* arg1 = new PlayerArg;
        if (!FLAGS_use_futex && !FLAGS_use_butex) {
            arg1->read_fd = pipe1[0];
            arg1->write_fd = pipe2[1];
        } else if (FLAGS_use_futex) {
            AlignedIntWrapper* w1 = new AlignedIntWrapper;
            w1->value = INITIAL_FUTEX_VALUE;
            AlignedIntWrapper* w2 = new AlignedIntWrapper;
            w2->value = INITIAL_FUTEX_VALUE;
            arg1->wait_addr = &w1->value;
            arg1->wake_addr = &w2->value;
        } else if (FLAGS_use_butex) {
            arg1->wait_addr = bthread::butex_create_checked<int>();
            *arg1->wait_addr = INITIAL_FUTEX_VALUE;
            arg1->wake_addr = bthread::butex_create_checked<int>();
            *arg1->wake_addr = INITIAL_FUTEX_VALUE;
        } else {
            ASSERT_TRUE(false);
        }
        arg1->counter = 0;
        arg1->wakeup = 0;
        args[i] = arg1;
    
        PlayerArg* arg2 = new PlayerArg;
        if (!FLAGS_use_futex && !FLAGS_use_butex) {
            arg2->read_fd = pipe2[0];
            arg2->write_fd = pipe1[1];
        } else {
            arg2->wait_addr = arg1->wake_addr;
            arg2->wake_addr = arg1->wait_addr;
        }
        arg2->counter = 0;
        arg2->wakeup = 0;

        pthread_t th1, th2;
        bthread_t bth1, bth2;
        if (!FLAGS_use_futex && !FLAGS_use_butex) {
            ASSERT_EQ(0, pthread_create(&th1, NULL, pipe_player, arg1));
            ASSERT_EQ(0, pthread_create(&th2, NULL, pipe_player, arg2));
        } else if (FLAGS_use_futex) {
            ASSERT_EQ(0, pthread_create(&th1, NULL, futex_player, arg1));
            ASSERT_EQ(0, pthread_create(&th2, NULL, futex_player, arg2));
        } else if (FLAGS_use_butex) {
            ASSERT_EQ(0, bthread_start_background(&bth1, NULL, butex_player, arg1));
            ASSERT_EQ(0, bthread_start_background(&bth2, NULL, butex_player, arg2));
        } else {
            ASSERT_TRUE(false);
        }

        if (!FLAGS_use_futex && !FLAGS_use_butex) {
            // send the seed data.
            unsigned char seed = 255;
            ASSERT_EQ(1L, write(pipe1[1], &seed, 1));
        } else if (FLAGS_use_futex) {
            ++*arg1->wait_addr;
            bthread::futex_wake_private(arg1->wait_addr, 1);
        } else if (FLAGS_use_butex) {
            ++*arg1->wait_addr;
            bthread::butex_wake(arg1->wait_addr);
        } else {
            ASSERT_TRUE(false);
        }
    }

    long last_counter = 0;
    long last_wakeup = 0;
    while (!stop) {
        butil::Timer tm;
        tm.start();
        sleep(1);
        tm.stop();
        long cur_counter = 0;
        long cur_wakeup = 0;
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            cur_counter += args[i]->counter;
            cur_wakeup += args[i]->wakeup;
        }
        if (FLAGS_use_futex || FLAGS_use_butex) {
            printf("pingpong-ed %" PRId64 "/s, wakeup=%" PRId64 "/s\n",
                   (cur_counter - last_counter) * 1000L / tm.m_elapsed(),
                   (cur_wakeup - last_wakeup) * 1000L / tm.m_elapsed());
        } else {
            printf("pingpong-ed %" PRId64 "/s\n",
                   (cur_counter - last_counter) * 1000L / tm.m_elapsed());
        }
        last_counter = cur_counter;
        last_wakeup = cur_wakeup;
        if (!FLAGS_loop) {
            break;
        }
    }
    stop = true;
    // Program quits, Let resource leak.
}
} // namespace
