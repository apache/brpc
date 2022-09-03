
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "bthread/bthread.h"
#include "bthread/condition_variable.h"
#include "bthread/mutex.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "bvar/bvar.h"

DEFINE_int64(wait_us, 5, "wait us");
typedef std::unique_lock<bthread::Mutex> Lock;
typedef bthread::ConditionVariable Condition;
bthread::Mutex      g_mutex;
Condition           g_cond;
std::deque<int32_t> g_que;
const size_t        g_capacity = 5000;
const int PRODUCER_NUM = 5;
struct ProducerStat {
    std::atomic<int> loop_count;
    bvar::Adder<int> wait_count;
    bvar::Adder<int> wait_timeout_count;
    bvar::Adder<int> wait_success_count;
};
ProducerStat g_stat[PRODUCER_NUM];

void* print_func(void* arg) {
    int last_loop[PRODUCER_NUM] = {0};
    for (int j = 0; j < 10; j++) {
        usleep(1000000);
        for (int i = 0; i < PRODUCER_NUM; i++) {
            if (g_stat[i].loop_count.load() <= last_loop[i]) {
                LOG(ERROR) << "producer thread:" << i << " stopped";
                return nullptr;
            }
            LOG(NOTICE) << "producer stat idx:" << i
                        << " wait:" << g_stat[i].wait_count
                        << " wait_timeout:" << g_stat[i].wait_timeout_count
                        << " wait_success:" << g_stat[i].wait_success_count;
            g_stat[i].loop_count = g_stat[i].loop_count.load();
        }
    }
    return (void*)1;
}

void* produce_func(void* arg) {
    const int64_t wait_us = FLAGS_wait_us;
    LOG(NOTICE) << "wait us:" << wait_us;
    int64_t idx = (int64_t)(arg);
    int32_t i = 0;
    while (!bthread_stopped(bthread_self())) {
        LOG(DEBUG) << "come to a new round " << idx << "round[" << i << "]";
        {
            Lock lock(g_mutex); 
            while (g_que.size() >= g_capacity && !bthread_stopped(bthread_self())) {
                g_stat[idx].wait_count << 1;
                //LOG(DEBUG) << "wait begin " << idx;
                int ret = g_cond.wait_for(lock, wait_us);
                if (ret == ETIMEDOUT) {
                    g_stat[idx].wait_timeout_count << 1;
                    //LOG_EVERY_SECOND(NOTICE) << "wait timeout " << idx;
                } else {
                    g_stat[idx].wait_success_count << 1;
                    //LOG_EVERY_SECOND(NOTICE) << "wait early " << idx;
                }
            }
            g_que.push_back(++i);
            LOG(DEBUG) << "push back " << idx << " data[" << i << "]";
        }
        usleep(rand() % 20 + 5);
        g_stat[idx].loop_count.fetch_add(1);
    }
    LOG(NOTICE) << "producer func return, idx:" << idx;
    return nullptr;
}

void* consume_func(void* arg) {
    while (!bthread_stopped(bthread_self())) {
        bool need_notify = false;
        {
            Lock lock(g_mutex);
            need_notify = (g_que.size() == g_capacity);
            if (!g_que.empty()) {
                g_que.pop_front();
                LOG_EVERY_SECOND(NOTICE) << "pop a data";
            } else {
                LOG_EVERY_SECOND(NOTICE) << "que is empty";
            }
        }
        usleep(rand() % 300 + 500);
        if (need_notify) {
            //g_cond.notify_all();
            //LOG(WARNING) << "notify";
        }
    }
    LOG(NOTICE) << "consumer func return";
    return nullptr;
}

TEST(BthreadCondBugTest, test_bug) {
    bthread_t tids[PRODUCER_NUM];
    for (int i = 0; i < PRODUCER_NUM; i++) {
        bthread_start_background(&tids[i], NULL, produce_func, (void*)(int64_t)i);
    }
    bthread_t tid;
    bthread_start_background(&tid, NULL, consume_func, NULL);

    int64_t ret = (int64_t)print_func(nullptr);

    bthread_stop(tid);
    bthread_join(tid, nullptr);
    for (int i = 0; i < PRODUCER_NUM; i++) {
        bthread_stop(tids[i]);
        bthread_join(tids[i], nullptr);
    }

    ASSERT_EQ(ret, 1);
}