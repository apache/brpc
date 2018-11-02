// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#include <execinfo.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/logging.h"
#include "butil/logging.h"
#include "butil/gperftools_profiler.h"
#include "bthread/bthread.h"
#include "bthread/unstable.h"
#include "bthread/task_meta.h"

namespace {
class BthreadTest : public ::testing::Test{
protected:
    BthreadTest(){
        const int kNumCores = sysconf(_SC_NPROCESSORS_ONLN);
        if (kNumCores > 0) {
            bthread_setconcurrency(kNumCores);
        }
    };
    virtual ~BthreadTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

TEST_F(BthreadTest, sizeof_task_meta) {
    LOG(INFO) << "sizeof(TaskMeta)=" << sizeof(bthread::TaskMeta);
}

void* unrelated_pthread(void*) {
    LOG(INFO) << "I did not call any bthread function, "
        "I should begin and end without any problem";
    return (void*)(intptr_t)1;
}

TEST_F(BthreadTest, unrelated_pthread) {
    pthread_t th;
    ASSERT_EQ(0, pthread_create(&th, NULL, unrelated_pthread, NULL));
    void* ret = NULL;
    ASSERT_EQ(0, pthread_join(th, &ret));
    ASSERT_EQ(1, (intptr_t)ret);
}

TEST_F(BthreadTest, attr_init_and_destroy) {
    bthread_attr_t attr;
    ASSERT_EQ(0, bthread_attr_init(&attr));
    ASSERT_EQ(0, bthread_attr_destroy(&attr));
}

bthread_fcontext_t fcm;
bthread_fcontext_t fc;
typedef std::pair<int,int> pair_t;
static void f(intptr_t param) {
    pair_t* p = (pair_t*)param;
    p = (pair_t*)bthread_jump_fcontext(&fc, fcm, (intptr_t)(p->first+p->second));
    bthread_jump_fcontext(&fc, fcm, (intptr_t)(p->first+p->second));
}

TEST_F(BthreadTest, context_sanity) {
    fcm = NULL;
    std::size_t size(8192);
    void* sp = malloc(size);

    pair_t p(std::make_pair(2, 7));
    fc = bthread_make_fcontext((char*)sp + size, size, f);

    int res = (int)bthread_jump_fcontext(&fcm, fc, (intptr_t)&p);
    std::cout << p.first << " + " << p.second << " == " << res << std::endl;

    p = std::make_pair(5, 6);
    res = (int)bthread_jump_fcontext(&fcm, fc, (intptr_t)&p);
    std::cout << p.first << " + " << p.second << " == " << res << std::endl;
}

TEST_F(BthreadTest, call_bthread_functions_before_tls_created) {
    ASSERT_EQ(0, bthread_usleep(1000));
    ASSERT_EQ(EINVAL, bthread_join(0, NULL));
    ASSERT_EQ(0UL, bthread_self());
}

butil::atomic<bool> stop(false);

void* sleep_for_awhile(void* arg) {
    LOG(INFO) << "sleep_for_awhile(" << arg << ")";
    bthread_usleep(100000L);
    LOG(INFO) << "sleep_for_awhile(" << arg << ") wakes up";
    return NULL;
}

void* just_exit(void* arg) {
    LOG(INFO) << "just_exit(" << arg << ")";
    bthread_exit(NULL);
    EXPECT_TRUE(false) << "just_exit(" << arg << ") should never be here";
    return NULL;
}

void* repeated_sleep(void* arg) {
    for (size_t i = 0; !stop; ++i) {
        LOG(INFO) << "repeated_sleep(" << arg << ") i=" << i;
        bthread_usleep(1000000L);
    }
    return NULL;
}

void* spin_and_log(void* arg) {
    // This thread never yields CPU.
    butil::EveryManyUS every_1s(1000000L);
    size_t i = 0;
    while (!stop) {
        if (every_1s) {
            LOG(INFO) << "spin_and_log(" << arg << ")=" << i++;
        }
    }
    return NULL;
}

void* do_nothing(void* arg) {
    LOG(INFO) << "do_nothing(" << arg << ")";
    return NULL;
}

void* launcher(void* arg) {
    LOG(INFO) << "launcher(" << arg << ")";
    for (size_t i = 0; !stop; ++i) {
        bthread_t th;
        bthread_start_urgent(&th, NULL, do_nothing, (void*)i);
        bthread_usleep(1000000L);
    }
    return NULL;
}

void* stopper(void*) {
    // Need this thread to set `stop' to true. Reason: If spin_and_log (which
    // never yields CPU) is scheduled to main thread, main thread cannot get
    // to run again.
    bthread_usleep(5*1000000L);
    LOG(INFO) << "about to stop";
    stop = true;
    return NULL;
}

void* misc(void* arg) {
    LOG(INFO) << "misc(" << arg << ")";
    bthread_t th[8];
    EXPECT_EQ(0, bthread_start_urgent(&th[0], NULL, sleep_for_awhile, (void*)2));
    EXPECT_EQ(0, bthread_start_urgent(&th[1], NULL, just_exit, (void*)3));
    EXPECT_EQ(0, bthread_start_urgent(&th[2], NULL, repeated_sleep, (void*)4));
    EXPECT_EQ(0, bthread_start_urgent(&th[3], NULL, repeated_sleep, (void*)68));
    EXPECT_EQ(0, bthread_start_urgent(&th[4], NULL, spin_and_log, (void*)5));
    EXPECT_EQ(0, bthread_start_urgent(&th[5], NULL, spin_and_log, (void*)85));
    EXPECT_EQ(0, bthread_start_urgent(&th[6], NULL, launcher, (void*)6));
    EXPECT_EQ(0, bthread_start_urgent(&th[7], NULL, stopper, NULL));
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        EXPECT_EQ(0, bthread_join(th[i], NULL));
    }
    return NULL;
}

TEST_F(BthreadTest, sanity) {
    LOG(INFO) << "main thread " << pthread_self();
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, misc, (void*)1));
    LOG(INFO) << "back to main thread " << th1 << " " << pthread_self();
    ASSERT_EQ(0, bthread_join(th1, NULL));
}

const size_t BT_SIZE = 64;
void *bt_array[BT_SIZE];
int bt_cnt;

int do_bt (void) {
    bt_cnt = backtrace (bt_array, BT_SIZE);
    return 56;
}

int call_do_bt (void) {
    return do_bt () + 1;
}

void * tf (void*) {
    if (call_do_bt () != 57) {
        return (void *) 1L;
    }
    return NULL;
}

TEST_F(BthreadTest, backtrace) {
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, tf, NULL));
    ASSERT_EQ(0, bthread_join (th, NULL));

    char **text = backtrace_symbols (bt_array, bt_cnt);
    ASSERT_TRUE(text);
    for (int i = 0; i < bt_cnt; ++i) {
        puts(text[i]);
    }
}

void* show_self(void*) {
    EXPECT_NE(0ul, bthread_self());
    LOG(INFO) << "bthread_self=" << bthread_self();
    return NULL;
}

TEST_F(BthreadTest, bthread_self) {
    ASSERT_EQ(0ul, bthread_self());
    bthread_t bth;
    ASSERT_EQ(0, bthread_start_urgent(&bth, NULL, show_self, NULL));
    ASSERT_EQ(0, bthread_join(bth, NULL));
}

void* join_self(void*) {
    EXPECT_EQ(EINVAL, bthread_join(bthread_self(), NULL));
    return NULL;
}

TEST_F(BthreadTest, bthread_join) {
    // Invalid tid
    ASSERT_EQ(EINVAL, bthread_join(0, NULL));
    
    // Unexisting tid
    ASSERT_EQ(EINVAL, bthread_join((bthread_t)-1, NULL));

    // Joining self
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, join_self, NULL));
}

void* change_errno(void* arg) {
    errno = (intptr_t)arg;
    return NULL;
}

TEST_F(BthreadTest, errno_not_changed) {
    bthread_t th;
    errno = 1;
    bthread_start_urgent(&th, NULL, change_errno, (void*)(intptr_t)2);
    ASSERT_EQ(1, errno);
}

static long sleep_in_adding_func = 0;

void* adding_func(void* arg) {
    butil::atomic<size_t>* s = (butil::atomic<size_t>*)arg;
    if (sleep_in_adding_func > 0) {
        long t1 = 0;
        if (10000 == s->fetch_add(1)) {
            t1 = butil::cpuwide_time_us();
        }
        bthread_usleep(sleep_in_adding_func);
        if (t1) {
            LOG(INFO) << "elapse is " << butil::cpuwide_time_us() - t1 << "ns";
        }
    } else {
        s->fetch_add(1);
    }
    return NULL;
}

TEST_F(BthreadTest, small_threads) {
    for (size_t z = 0; z < 2; ++z) {
        sleep_in_adding_func = (z ? 1 : 0);
        char prof_name[32];
        if (sleep_in_adding_func) {
            snprintf(prof_name, sizeof(prof_name), "smallthread.prof");
        } else {
            snprintf(prof_name, sizeof(prof_name), "smallthread_nosleep.prof");
        }

        butil::atomic<size_t> s(0);
        size_t N = (sleep_in_adding_func ? 40000 : 100000);
        std::vector<bthread_t> th;
        th.reserve(N);
        butil::Timer tm;
        for (size_t j = 0; j < 3; ++j) {
            th.clear();
            if (j == 1) {
                ProfilerStart(prof_name);
            }
            tm.start();
            for (size_t i = 0; i < N; ++i) {
                bthread_t t1;
                ASSERT_EQ(0, bthread_start_urgent(
                              &t1, &BTHREAD_ATTR_SMALL, adding_func, &s));
                th.push_back(t1);
            }
            tm.stop();
            if (j == 1) {
                ProfilerStop();
            }
            for (size_t i = 0; i < N; ++i) {
                bthread_join(th[i], NULL);
            }
            LOG(INFO) << "[Round " << j + 1 << "] bthread_start_urgent takes "
                      << tm.n_elapsed()/N << "ns, sum=" << s;
            ASSERT_EQ(N * (j + 1), (size_t)s);
        
            // Check uniqueness of th
            std::sort(th.begin(), th.end());
            ASSERT_EQ(th.end(), std::unique(th.begin(), th.end()));
        }
    }
}

void* bthread_starter(void* void_counter) {
    while (!stop.load(butil::memory_order_relaxed)) {
        bthread_t th;
        EXPECT_EQ(0, bthread_start_urgent(&th, NULL, adding_func, void_counter));
    }
    return NULL;
}

struct BAIDU_CACHELINE_ALIGNMENT AlignedCounter {
    AlignedCounter() : value(0) {}
    butil::atomic<size_t> value;
};

TEST_F(BthreadTest, start_bthreads_frequently) {
    sleep_in_adding_func = 0;
    char prof_name[32];
    snprintf(prof_name, sizeof(prof_name), "start_bthreads_frequently.prof");
    const int con = bthread_getconcurrency();
    ASSERT_GT(con, 0);
    AlignedCounter* counters = new AlignedCounter[con];
    bthread_t th[con];

    std::cout << "Perf with different parameters..." << std::endl;
    //ProfilerStart(prof_name);
    for (int cur_con = 1; cur_con <= con; ++cur_con) {
        stop = false;
        for (int i = 0; i < cur_con; ++i) {
            counters[i].value = 0;
            ASSERT_EQ(0, bthread_start_urgent(
                          &th[i], NULL, bthread_starter, &counters[i].value));
        }
        butil::Timer tm;
        tm.start();
        bthread_usleep(200000L);
        stop = true;
        for (int i = 0; i < cur_con; ++i) {
            bthread_join(th[i], NULL);
        }
        tm.stop();
        size_t sum = 0;
        for (int i = 0; i < cur_con; ++i) {
            sum += counters[i].value * 1000 / tm.m_elapsed();
        }
        std::cout << sum << ",";
    }
    std::cout << std::endl;
    //ProfilerStop();
    delete [] counters;
}

void* log_start_latency(void* void_arg) {
    butil::Timer* tm = static_cast<butil::Timer*>(void_arg);
    tm->stop();
    return NULL;
}

TEST_F(BthreadTest, start_latency_when_high_idle) {
    bool warmup = true;
    long elp1 = 0;
    long elp2 = 0;
    int REP = 0;
    for (int i = 0; i < 10000; ++i) {
        butil::Timer tm;
        tm.start();
        bthread_t th;
        bthread_start_urgent(&th, NULL, log_start_latency, &tm);
        bthread_join(th, NULL);
        bthread_t th2;
        butil::Timer tm2;
        tm2.start();
        bthread_start_background(&th2, NULL, log_start_latency, &tm2);
        bthread_join(th2, NULL);
        if (!warmup) {
            ++REP;
            elp1 += tm.n_elapsed();
            elp2 += tm2.n_elapsed();
        } else if (i == 100) {
            warmup = false;
        }
    }
    LOG(INFO) << "start_urgent=" << elp1 / REP << "ns start_background="
              << elp2 / REP << "ns";
}

void* sleep_for_awhile_with_sleep(void* arg) {
    bthread_usleep((intptr_t)arg);
    return NULL;
}

TEST_F(BthreadTest, stop_sleep) {
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(
                  &th, NULL, sleep_for_awhile_with_sleep, (void*)1000000L));
    butil::Timer tm;
    tm.start();
    bthread_usleep(10000);
    ASSERT_EQ(0, bthread_stop(th));
    ASSERT_EQ(0, bthread_join(th, NULL));
    tm.stop();
    ASSERT_LE(labs(tm.m_elapsed() - 10), 10);
}

TEST_F(BthreadTest, bthread_exit) {
    bthread_t th1;
    bthread_t th2;
    pthread_t th3;
    bthread_t th4;
    bthread_t th5;
    const bthread_attr_t attr = BTHREAD_ATTR_PTHREAD;

    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, just_exit, NULL));
    ASSERT_EQ(0, bthread_start_background(&th2, NULL, just_exit, NULL));
    ASSERT_EQ(0, pthread_create(&th3, NULL, just_exit, NULL));
    EXPECT_EQ(0, bthread_start_urgent(&th4, &attr, just_exit, NULL));
    EXPECT_EQ(0, bthread_start_background(&th5, &attr, just_exit, NULL));

    ASSERT_EQ(0, bthread_join(th1, NULL));
    ASSERT_EQ(0, bthread_join(th2, NULL));
    ASSERT_EQ(0, pthread_join(th3, NULL));
    ASSERT_EQ(0, bthread_join(th4, NULL));
    ASSERT_EQ(0, bthread_join(th5, NULL));
}

TEST_F(BthreadTest, bthread_equal) {
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, do_nothing, NULL));
    bthread_t th2;
    ASSERT_EQ(0, bthread_start_urgent(&th2, NULL, do_nothing, NULL));
    ASSERT_EQ(0, bthread_equal(th1, th2));
    bthread_t th3 = th2;
    ASSERT_EQ(1, bthread_equal(th3, th2));
    ASSERT_EQ(0, bthread_join(th1, NULL));
    ASSERT_EQ(0, bthread_join(th2, NULL));
}

void* mark_run(void* run) {
    *static_cast<pthread_t*>(run) = pthread_self();
    return NULL;
}

void* check_sleep(void* pthread_task) {
    EXPECT_TRUE(bthread_self() != 0);
    // Create a no-signal task that other worker will not steal. The task will be
    // run if current bthread does context switch.
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL | BTHREAD_NOSIGNAL;
    bthread_t th1;
    pthread_t run = 0;
    const pthread_t pid = pthread_self();
    EXPECT_EQ(0, bthread_start_urgent(&th1, &attr, mark_run, &run));
    if (pthread_task) {
        bthread_usleep(100000L);
        // due to NOSIGNAL, mark_run did not run.
        // FIXME: actually runs. someone is still stealing.
        // EXPECT_EQ((pthread_t)0, run);
        // bthread_usleep = usleep for BTHREAD_ATTR_PTHREAD
        EXPECT_EQ(pid, pthread_self());
        // schedule mark_run
        bthread_flush();
    } else {
        // start_urgent should jump to the new thread first, then back to
        // current thread.
        EXPECT_EQ(pid, run);             // should run in the same pthread
    }
    EXPECT_EQ(0, bthread_join(th1, NULL));
    if (pthread_task) {
        EXPECT_EQ(pid, pthread_self());
        EXPECT_NE((pthread_t)0, run); // the mark_run should run.
    }
    return NULL;
}

TEST_F(BthreadTest, bthread_usleep) {
    // NOTE: May fail because worker threads may still be stealing tasks
    // after previous cases.
    usleep(10000);
    
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, &BTHREAD_ATTR_PTHREAD,
                                      check_sleep, (void*)1));
    ASSERT_EQ(0, bthread_join(th1, NULL));
    
    bthread_t th2;
    ASSERT_EQ(0, bthread_start_urgent(&th2, NULL,
                                      check_sleep, (void*)0));
    ASSERT_EQ(0, bthread_join(th2, NULL));
}

void* dummy_thread(void*) {
    return NULL;
}

TEST_F(BthreadTest, too_many_nosignal_threads) {
    for (size_t i = 0; i < 100000; ++i) {
        bthread_attr_t attr = BTHREAD_ATTR_NORMAL | BTHREAD_NOSIGNAL;
        bthread_t tid;
        ASSERT_EQ(0, bthread_start_urgent(&tid, &attr, dummy_thread, NULL));
    }
}

static void* yield_thread(void*) {
    bthread_yield();
    return NULL;
}

TEST_F(BthreadTest, yield_single_thread) {
    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(&tid, NULL, yield_thread, NULL));
    ASSERT_EQ(0, bthread_join(tid, NULL));
}

} // namespace
