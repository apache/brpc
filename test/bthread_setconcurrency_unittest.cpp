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

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/logging.h"
#include "butil/thread_local.h"
#include <bthread/butex.h>
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/task_control.h"

namespace bthread {
    extern TaskControl* g_task_control;
}

namespace {
void* dummy(void*) {
    return NULL;
}

TEST(BthreadTest, setconcurrency) {
    ASSERT_EQ(8 + BTHREAD_EPOLL_THREAD_NUM, (size_t)bthread_getconcurrency());
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY - 1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(0));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(-1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MAX_CONCURRENCY + 1));
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));  // smaller value
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, dummy, NULL));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 5));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
    ASSERT_EQ(EPERM, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
}

static butil::atomic<int> *odd;
static butil::atomic<int> *even;

static butil::atomic<int> nbthreads(0);
static butil::atomic<int> npthreads(0);
static BAIDU_THREAD_LOCAL bool counted = false;
static butil::atomic<bool> stop (false);

static void *odd_thread(void *) {
    nbthreads.fetch_add(1);
    while (!stop) {
        if (!counted) {
            counted = true;
            npthreads.fetch_add(1);
        }
        bthread::butex_wake_all(even);
        bthread::butex_wait(odd, 0, NULL);
    }
    return NULL;
}

static void *even_thread(void *) {
    nbthreads.fetch_add(1);
    while (!stop) {
        if (!counted) {
            counted = true;
            npthreads.fetch_add(1);
        }
        bthread::butex_wake_all(odd);
        bthread::butex_wait(even, 0, NULL);
    }
    return NULL;
}

TEST(BthreadTest, setconcurrency_with_running_bthread) {
    odd = bthread::butex_create_checked<butil::atomic<int> >();
    even = bthread::butex_create_checked<butil::atomic<int> >();
    ASSERT_TRUE(odd != NULL && even != NULL);
    *odd = 0;
    *even = 0;
    std::vector<bthread_t> tids;
    const int N = 500;
    for (int i = 0; i < N; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, odd_thread, NULL);
        tids.push_back(tid);
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, even_thread, NULL);
        tids.push_back(tid);
    }
    for (int i = 100; i <= N; ++i) {
        ASSERT_EQ(0, bthread_setconcurrency(i));
        ASSERT_EQ(i, bthread_getconcurrency());
    }
    usleep(1000 * N);
    *odd = 1;
    *even = 1;
    stop =  true;
    bthread::butex_wake_all(odd);
    bthread::butex_wake_all(even);
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], NULL);
    }
    LOG(INFO) << "All bthreads has quit";
    ASSERT_EQ(2*N, nbthreads);
    // This is not necessarily true, not all workers need to run sth.
    //ASSERT_EQ(N, npthreads);
    LOG(INFO) << "Touched pthreads=" << npthreads;
}

void* sleep_proc(void*) {
    usleep(100000);
    return NULL;
}

void* add_concurrency_proc(void*) {
    bthread_t tid;
    bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, sleep_proc, NULL);
    bthread_join(tid, NULL);
    return NULL;
}

bool set_min_concurrency(int num) {
    std::stringstream ss;
    ss << num;
    std::string ret = GFLAGS_NAMESPACE::SetCommandLineOption("bthread_min_concurrency", ss.str().c_str());
    return !ret.empty();
}

int get_min_concurrency() {
    std::string ret;
    GFLAGS_NAMESPACE::GetCommandLineOption("bthread_min_concurrency", &ret);
    return atoi(ret.c_str());
}

TEST(BthreadTest, min_concurrency) {
    ASSERT_EQ(1, set_min_concurrency(-1)); // set min success
    ASSERT_EQ(1, set_min_concurrency(0)); // set min success
    ASSERT_EQ(0, get_min_concurrency());
    int conn = bthread_getconcurrency();
    int add_conn = 100;

    ASSERT_EQ(0, set_min_concurrency(conn + 1)); // set min failed
    ASSERT_EQ(0, get_min_concurrency());

    ASSERT_EQ(1, set_min_concurrency(conn - 1)); // set min success
    ASSERT_EQ(conn - 1, get_min_concurrency());

    ASSERT_EQ(EINVAL, bthread_setconcurrency(conn - 2)); // set max failed
    ASSERT_EQ(0, bthread_setconcurrency(conn + add_conn + 1)); // set max success
    ASSERT_EQ(0, bthread_setconcurrency(conn + add_conn)); // set max success
    ASSERT_EQ(conn + add_conn, bthread_getconcurrency());
    ASSERT_EQ(conn, bthread::g_task_control->concurrency());

    ASSERT_EQ(1, set_min_concurrency(conn + 1)); // set min success
    ASSERT_EQ(conn + 1, get_min_concurrency());
    ASSERT_EQ(conn + 1, bthread::g_task_control->concurrency());

    std::vector<bthread_t> tids;
    for (int i = 0; i < conn; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, sleep_proc, NULL);
        tids.push_back(tid);
    }
    for (int i = 0; i < add_conn; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, add_concurrency_proc, NULL);
        tids.push_back(tid);
    }
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], NULL);
    }
    ASSERT_EQ(conn + add_conn, bthread_getconcurrency());
    ASSERT_EQ(conn + add_conn, bthread::g_task_control->concurrency());
}

int current_tag(int tag) {
    std::stringstream ss;
    ss << tag;
    std::string ret = GFLAGS_NAMESPACE::SetCommandLineOption("bthread_current_tag", ss.str().c_str());
    return !(ret.empty());
}

TEST(BthreadTest, current_tag) {
    ASSERT_EQ(false, current_tag(-2));
    ASSERT_EQ(true, current_tag(0));
    ASSERT_EQ(false, current_tag(1));
}

int concurrency_by_tag(int num) {
    std::stringstream ss;
    ss << num;
    std::string ret =
        GFLAGS_NAMESPACE::SetCommandLineOption("bthread_concurrency_by_tag", ss.str().c_str());
    return !(ret.empty());
}

TEST(BthreadTest, concurrency_by_tag) {
    ASSERT_EQ(concurrency_by_tag(1), false);
    auto tag_con = bthread_getconcurrency_by_tag(0);
    auto con = bthread_getconcurrency();
    ASSERT_EQ(concurrency_by_tag(con), true);
    ASSERT_EQ(concurrency_by_tag(con + 1), true);
    ASSERT_EQ(bthread_getconcurrency(), con+1);
    bthread_setconcurrency(con + 1);
    ASSERT_EQ(concurrency_by_tag(con + 1), true);
}

} // namespace
