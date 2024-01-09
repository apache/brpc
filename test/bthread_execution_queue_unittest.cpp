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

#include <bthread/execution_queue.h>
#include <bthread/sys_futex.h>
#include <bthread/countdown_event.h>
#include "butil/time.h"
#include "butil/fast_rand.h"
#include "butil/gperftools_profiler.h"

namespace {
bool stopped = false;

class ExecutionQueueTest : public testing::Test {
protected:
    void SetUp() { stopped = false; }
    void TearDown() {}
};

struct LongIntTask {
    long value;
    bthread::CountdownEvent* event;
    LongIntTask(long v)
        : value(v), event(NULL)
    {}
    LongIntTask(long v, bthread::CountdownEvent* e)
        : value(v), event(e)
    {}
    LongIntTask() : value(0), event(NULL) {}
};

int add(void* meta, bthread::TaskIterator<LongIntTask> &iter) {
    stopped = iter.is_queue_stopped();
    int64_t* result = (int64_t*)meta;
    for (; iter; ++iter) {
        *result += iter->value;
        if (iter->event) { iter->event->signal(); }
    }
    return 0;
}

void test_single_thread(bool use_pthread) {
    int64_t result = 0;
    int64_t expected_result = 0;
    stopped = false;
    bthread::ExecutionQueueId<LongIntTask> queue_id;
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add, &result));
    for (int i = 0; i < 100; ++i) {
        expected_result += i;
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, i));
    }
    LOG(INFO) << "stop";
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_NE(0, bthread::execution_queue_execute(queue_id, 0));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(expected_result, result);
    ASSERT_TRUE(stopped);
}

TEST_F(ExecutionQueueTest, single_thread) {
    for (int i = 0; i < 2; ++i) {
        test_single_thread(i);
    }
}

class RValue {
public:
    RValue() : _value(0) {}
    explicit RValue(int v) : _value(v) {}
    RValue(RValue&& rhs)  noexcept : _value(rhs._value) {}
    RValue& operator=(RValue&& rhs) noexcept {
        if (this != &rhs) {
            _value = rhs._value;
        }
        return *this;
    }

    DISALLOW_COPY_AND_ASSIGN(RValue);

    int value() const { return _value; }


private:
    int _value;
};

int add(void* meta, bthread::TaskIterator<RValue> &iter) {
    stopped = iter.is_queue_stopped();
    int* result = (int*)meta;
    for (; iter; ++iter) {
        *result += iter->value();
    }
    return 0;
}

void test_rvalue(bool use_pthread) {
    int64_t result = 0;
    int64_t expected_result = 0;
    stopped = false;
    bthread::ExecutionQueueId<RValue> queue_id;
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add, &result));
    for (int i = 0; i < 100; ++i) {
        expected_result += i;
        RValue v(i);
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, std::move(v)));
    }
    LOG(INFO) << "stop";
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_NE(0, bthread::execution_queue_execute(queue_id, RValue(0)));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(expected_result, result);
    ASSERT_TRUE(stopped);
}

TEST_F(ExecutionQueueTest, rvalue) {
    for (int i = 0; i < 2; ++i) {
        test_rvalue(i);
    }
}

struct PushArg {
    bthread::ExecutionQueueId<LongIntTask> id {0};
    butil::atomic<int64_t> total_num {0};
    butil::atomic<int64_t> total_time {0};
    butil::atomic<int64_t> expected_value {0};
    volatile bool stopped {false};
    bool wait_task_completed {false};
};

void* push_thread(void *arg) {
    PushArg* pa = (PushArg*)arg;
    int64_t sum = 0;
    butil::Timer timer;
    timer.start();
    int num = 0;
    bthread::CountdownEvent e;
    LongIntTask t(num, pa->wait_task_completed ? &e : NULL);
    if (pa->wait_task_completed) {
        e.reset(1);
    }
    while (bthread::execution_queue_execute(pa->id, t) == 0) {
        sum += num;
        t.value = ++num;
        if (pa->wait_task_completed) {
            e.wait();
            e.reset(1);
        }
    }
    timer.stop();
    pa->expected_value.fetch_add(sum, butil::memory_order_relaxed);
    pa->total_num.fetch_add(num);
    pa->total_time.fetch_add(timer.n_elapsed());
    return NULL;
}

void* push_thread_which_addresses_execq(void *arg) {
    PushArg* pa = (PushArg*)arg;
    int64_t sum = 0;
    butil::Timer timer;
    timer.start();
    int num = 0;
    bthread::ExecutionQueue<LongIntTask>::scoped_ptr_t ptr
            = bthread::execution_queue_address(pa->id);
    EXPECT_TRUE(ptr);
    while (ptr->execute(num) == 0) {
        sum += num;
        ++num;
    }
    EXPECT_TRUE(ptr->stopped());
    timer.stop();
    pa->expected_value.fetch_add(sum, butil::memory_order_relaxed);
    pa->total_num.fetch_add(num);
    pa->total_time.fetch_add(timer.n_elapsed());
    return NULL;
}

void test_performance(bool use_pthread) {
    pthread_t threads[8];
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    int64_t result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add, &result));
    PushArg pa;
    pa.id = queue_id;
    pa.total_num = 0;
    pa.total_time = 0;
    pa.expected_value = 0;
    pa.stopped = false;
    ProfilerStart("execq.prof");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &push_thread_which_addresses_execq, &pa);
    }
    usleep(500 * 1000);
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_join(threads[i], NULL);
    }
    ProfilerStop();
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(pa.expected_value.load(), result);
    LOG(INFO) << "With addressed execq, each execution_queue_execute takes "
              << pa.total_time.load() / pa.total_num.load()
              << " total_num=" << pa.total_num
              << " ns with " << ARRAY_SIZE(threads) << " threads";
#define BENCHMARK_BOTH
#ifdef BENCHMARK_BOTH
    result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add, &result));
    pa.id = queue_id;
    pa.total_num = 0;
    pa.total_time = 0;
    pa.expected_value = 0;
    pa.stopped = false;
    ProfilerStart("execq_id.prof");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &push_thread, &pa);
    }
    usleep(500 * 1000);
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_join(threads[i], NULL);
    }
    ProfilerStop();
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(pa.expected_value.load(), result);
    LOG(INFO) << "With id explicitly, execution_queue_execute takes "
              << pa.total_time.load() / pa.total_num.load()
              << " total_num=" << pa.total_num
              << " ns with " << ARRAY_SIZE(threads) << " threads";
#endif  // BENCHMARK_BOTH
}

TEST_F(ExecutionQueueTest, performance) {
    for (int i = 0; i < 2; ++i) {
        test_performance(i);
    }
}

volatile bool g_suspending = false;
volatile bool g_should_be_urgent = false;
int urgent_times = 0;

int add_with_suspend(void* meta, bthread::TaskIterator<LongIntTask>& iter) {
    int64_t* result = (int64_t*)meta;
    if (iter.is_queue_stopped()) {
        stopped = true;
        return 0;
    }
    if (g_should_be_urgent) {
        g_should_be_urgent = false;
        EXPECT_EQ(-1, iter->value) << urgent_times;
        if (iter->event) { iter->event->signal(); }
        ++iter;
        EXPECT_FALSE(iter) << urgent_times;
        ++urgent_times;
    } else {
        for (; iter; ++iter) {
            if (iter->value == -100) {
                g_suspending = true;
                while (g_suspending) {
                    bthread_usleep(100);
                }
                g_should_be_urgent = true;
                if (iter->event) { iter->event->signal(); }
                EXPECT_FALSE(++iter);
                return 0;
            } else {
                *result += iter->value;
                if (iter->event) { iter->event->signal(); }
            }
        }
    }
    return 0;
}

void test_execute_urgent(bool use_pthread) {
    g_should_be_urgent = false;
    pthread_t threads[10];
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    int64_t result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add_with_suspend, &result));
    PushArg pa;
    pa.id = queue_id;
    pa.total_num = 0;
    pa.total_time = 0;
    pa.expected_value = 0;
    pa.stopped = false;
    pa.wait_task_completed = true;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &push_thread, &pa);
    }
    g_suspending = false;
    usleep(1000);

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, -100));
        while (!g_suspending) {
            usleep(100);
        }
        ASSERT_EQ(0, bthread::execution_queue_execute(
            queue_id, -1, &bthread::TASK_OPTIONS_URGENT));
        g_suspending = false;
        usleep(100);
    }
    usleep(500* 1000);
    pa.stopped = true;
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_join(threads[i], NULL);
    }
    LOG(INFO) << "result=" << result;
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(pa.expected_value.load(), result);
}

TEST_F(ExecutionQueueTest, execute_urgent) {
    for (int i = 0; i < 2; ++i) {
        test_execute_urgent(i);
    }
}

void test_urgent_task_is_the_last_task(bool use_pthread) {
    g_should_be_urgent = false;
    g_suspending = false;
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    int64_t result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add_with_suspend, &result));
    g_suspending = false;
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, -100));
    while (!g_suspending) {
        usleep(10);
    }
    LOG(INFO) << "Going to push";
    int64_t expected = 0;
    for (int j = 1; j < 100; ++j) {
        expected += j;
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, j));
    }
    ASSERT_EQ(0, bthread::execution_queue_execute(
        queue_id, -1, &bthread::TASK_OPTIONS_URGENT));
    usleep(100);
    g_suspending = false;
    butil::atomic_thread_fence(butil::memory_order_acq_rel);
    usleep(10 * 1000);
    LOG(INFO) << "going to quit";
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(expected, result);
}
TEST_F(ExecutionQueueTest, urgent_task_is_the_last_task) {
    for (int i = 0; i < 2; ++i) {
        test_urgent_task_is_the_last_task(i);
    }
}

long next_task[1024];
butil::atomic<int> num_threads(0);

void* push_thread_with_id(void* arg) {
    bthread::ExecutionQueueId<LongIntTask> id = { (uint64_t)arg };
    int thread_id = num_threads.fetch_add(1, butil::memory_order_relaxed);
    LOG(INFO) << "Start thread" << thread_id;
    for (int i = 0; i < 100000; ++i) {
        bthread::execution_queue_execute(id, ((long)thread_id << 32) | i);
    }
    return NULL;
}

int check_order(void* meta, bthread::TaskIterator<LongIntTask>& iter) {
    for (; iter; ++iter) {
        long value = iter->value;
        int thread_id = value >> 32;
        long task = value & 0xFFFFFFFFul;
        if (task != next_task[thread_id]++) {
            EXPECT_TRUE(false) << "task=" << task << " thread_id=" << thread_id;
            ++*(long*)meta;
        }
        if (iter->event) { iter->event->signal(); }
    }
    return 0;
}

void test_multi_threaded_order(bool use_pthread) {
    memset(next_task, 0, sizeof(next_task));
    long disorder_times = 0;
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                check_order, &disorder_times));
    pthread_t threads[12];
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &push_thread_with_id, (void *)queue_id.value);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_join(threads[i], NULL);
    }
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(0, disorder_times);
}

TEST_F(ExecutionQueueTest, multi_threaded_order) {
    for (int i = 0; i < 2; ++i) {
        test_multi_threaded_order(i);
    }
}

int check_running_thread(void* arg, bthread::TaskIterator<LongIntTask>& iter) {
    if (iter.is_queue_stopped()) {
        return 0;
    }
    for (; iter; ++iter) {}
    EXPECT_EQ(pthread_self(), (pthread_t)arg);
    return 0;
}

void test_in_place_task(bool use_pthread) {
    pthread_t thread_id = pthread_self();
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                check_running_thread,
                                                (void*)thread_id));
    ASSERT_EQ(0, bthread::execution_queue_execute(
        queue_id, 0, &bthread::TASK_OPTIONS_INPLACE));
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
}

TEST_F(ExecutionQueueTest, in_place_task) {
    for (int i = 0; i < 2; ++i) {
        test_in_place_task(i);
    }
}

struct InPlaceTask {
    bool first_task;
    pthread_t thread_id;
};

void *run_first_tasks(void* arg) {
    bthread::ExecutionQueueId<InPlaceTask> queue_id = { (uint64_t)arg };
    InPlaceTask task;
    task.first_task = true;
    task.thread_id = pthread_self();
    EXPECT_EQ(0, bthread::execution_queue_execute(
        queue_id, task, &bthread::TASK_OPTIONS_INPLACE));
    return NULL;
}

int stuck_and_check_running_thread(void* arg, bthread::TaskIterator<InPlaceTask>& iter) {
    if (iter.is_queue_stopped()) {
        return 0;
    }
    butil::atomic<int>* futex = (butil::atomic<int>*)arg;
    if (iter->first_task) {
        EXPECT_EQ(pthread_self(), iter->thread_id);
        futex->store(1);
        bthread::futex_wake_private(futex, 1);
        while (futex->load() != 2) {
            bthread::futex_wait_private(futex, 1, NULL);
        }
        ++iter;
        EXPECT_FALSE(iter);
    } else {
        for (; iter; ++iter) {
            EXPECT_FALSE(iter->first_task);
            EXPECT_NE(pthread_self(), iter->thread_id);
        }
    }
    return 0;
}

void test_should_start_new_thread_on_more_tasks(bool use_pthread) {
    bthread::ExecutionQueueId<InPlaceTask> queue_id = { 0 };
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    butil::atomic<int> futex(0);
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                stuck_and_check_running_thread,
                                                (void*)&futex));
    pthread_t thread;
    ASSERT_EQ(0, pthread_create(&thread, NULL, run_first_tasks, (void*)queue_id.value));
    while (futex.load() != 1) {
        bthread::futex_wait_private(&futex, 0, NULL);
    }
    for (size_t i = 0; i < 100; ++i) {
        InPlaceTask task;
        task.first_task = false;
        task.thread_id = pthread_self();
        ASSERT_EQ(0, bthread::execution_queue_execute(
            queue_id, task, &bthread::TASK_OPTIONS_INPLACE));
    }
    futex.store(2);
    bthread::futex_wake_private(&futex, 1);
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
}

TEST_F(ExecutionQueueTest, should_start_new_thread_on_more_tasks) {
    for (int i = 0; i < 2; ++i) {
        test_should_start_new_thread_on_more_tasks(i);
    }
}

void* inplace_push_thread(void* arg) {
    bthread::ExecutionQueueId<LongIntTask> id = { (uint64_t)arg };
    int thread_id = num_threads.fetch_add(1, butil::memory_order_relaxed);
    LOG(INFO) << "Start thread" << thread_id;
    for (int i = 0; i < 100000; ++i) {
        bthread::execution_queue_execute(id, ((long)thread_id << 32) | i,
                                         &bthread::TASK_OPTIONS_INPLACE);
    }
    return NULL;
}

void test_inplace_and_order(bool use_pthread) {
    memset(next_task, 0, sizeof(next_task));
    long disorder_times = 0;
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                check_order, &disorder_times));
    pthread_t threads[12];
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &inplace_push_thread, (void *)queue_id.value);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_join(threads[i], NULL);
    }
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(0, disorder_times);
}

TEST_F(ExecutionQueueTest, inplace_and_order) {
    for (int i = 0; i < 2; ++i) {
        test_inplace_and_order(i);
    }
}

TEST_F(ExecutionQueueTest, size_of_task_node) {
    LOG(INFO) << "sizeof(TaskNode)=" << sizeof(bthread::TaskNode);
}

int add_with_suspend2(void* meta, bthread::TaskIterator<LongIntTask>& iter) {
    int64_t* result = (int64_t*)meta;
    if (iter.is_queue_stopped()) {
        stopped = true;
        return 0;
    }
    for (; iter; ++iter) {
        if (iter->value == -100) {
            g_suspending = true;
            while (g_suspending) {
                usleep(10);
            }
            if (iter->event) { iter->event->signal(); }
        } else {
            *result += iter->value;
            if (iter->event) { iter->event->signal(); }
        }
    }
    return 0;
}

void test_cancel(bool use_pthread) {
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    int64_t result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add_with_suspend2, &result));
    g_suspending = false;
    bthread::TaskHandle handle0;
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, -100, NULL, &handle0));
    while (!g_suspending) {
        usleep(10);
    }
    ASSERT_EQ(1, bthread::execution_queue_cancel(handle0));
    ASSERT_EQ(1, bthread::execution_queue_cancel(handle0));
    bthread::TaskHandle handle1;
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, 100, NULL, &handle1));
    ASSERT_EQ(0, bthread::execution_queue_cancel(handle1));
    g_suspending = false;
    ASSERT_EQ(-1, bthread::execution_queue_cancel(handle1));
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(0, result);
}

TEST_F(ExecutionQueueTest, cancel) {
    for (int i = 0; i < 2; ++i) {
        test_cancel(i);
    }
}

struct CancelSelf {
    butil::atomic<bthread::TaskHandle*> handle;
};

int cancel_self(void* /*meta*/, bthread::TaskIterator<CancelSelf*>& iter) {

    for (; iter; ++iter) {
        while ((*iter)->handle == NULL) {
            usleep(10);
        }
        EXPECT_EQ(1, bthread::execution_queue_cancel(*(*iter)->handle.load()));
        EXPECT_EQ(1, bthread::execution_queue_cancel(*(*iter)->handle.load()));
        EXPECT_EQ(1, bthread::execution_queue_cancel(*(*iter)->handle.load()));
    }
    return 0;
}

void test_cancel_self(bool use_pthread) {
    bthread::ExecutionQueueId<CancelSelf*> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                cancel_self, NULL));
    CancelSelf task;
    task.handle = NULL;
    bthread::TaskHandle handle;
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, &task, NULL, &handle));
    task.handle.store(&handle);
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
}

TEST_F(ExecutionQueueTest, cancel_self) {
    for (int i = 0; i < 2; ++i) {
        test_cancel_self(i);
    }
}

struct AddTask {
    int value;
    bool cancel_task;
    int cancel_value;
    bthread::TaskHandle handle;
};

struct AddMeta {
    int64_t sum;
    butil::atomic<int64_t> expected;
    butil::atomic<int64_t> succ_times;
    butil::atomic<int64_t> race_times;
    butil::atomic<int64_t> fail_times;
};

int add_with_cancel(void* meta, bthread::TaskIterator<AddTask>& iter) {
    if (iter.is_queue_stopped()) {
        return 0;
    }
    AddMeta* m = (AddMeta*)meta;
    for (; iter; ++iter) {
        if (iter->cancel_task) {
            const int rc = bthread::execution_queue_cancel(iter->handle);
            if (rc == 0) {
                m->expected.fetch_sub(iter->cancel_value);
                m->succ_times.fetch_add(1);
            } else if (rc < 0) {
                m->fail_times.fetch_add(1);
            } else {
                m->race_times.fetch_add(1);
            }
        } else {
            m->sum += iter->value;
        }
    }
    return 0;
}

void test_random_cancel(bool use_pthread) {
    bthread::ExecutionQueueId<AddTask> queue_id = { 0 };
    AddMeta m;
    m.sum = 0;
    m.expected.store(0);
    m.succ_times.store(0);
    m.fail_times.store(0);
    m.race_times.store(0);
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, NULL,
                                                add_with_cancel, &m));
    int64_t expected = 0;
    for (int i = 0; i < 100000; ++i) {
        bthread::TaskHandle h;
        AddTask t;
        t.value = i;
        t.cancel_task = false;
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, t, NULL, &h));
        const int r = butil::fast_rand_less_than(4);
        expected += i;
        if (r == 0) {
            if (bthread::execution_queue_cancel(h) == 0) {
                expected -= i;
            }
        } else if (r == 1) {
            t.cancel_task = true;
            t.cancel_value = i;
            t.handle = h;
            ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, t, NULL));
        } else if (r == 2) {
            t.cancel_task = true;
            t.cancel_value = i;
            t.handle = h;
            ASSERT_EQ(0, bthread::execution_queue_execute(
                queue_id, t, &bthread::TASK_OPTIONS_URGENT));
        } else {
            // do nothing;
        }
    }
    m.expected.fetch_add(expected);
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(m.sum, m.expected.load());
    LOG(INFO) << "sum=" << m.sum << " race_times=" << m.race_times
              << " succ_times=" << m.succ_times
              << " fail_times=" << m.fail_times;
}

TEST_F(ExecutionQueueTest, random_cancel) {
    for (int i = 0; i < 2; ++i) {
        test_random_cancel(i);
    }

}

int add2(void* meta, bthread::TaskIterator<LongIntTask> &iter) {
    if (iter) {
        int64_t* result = (int64_t*)meta;
        *result += iter->value;
        if (iter->event) { iter->event->signal(); }
    }
    return 0;
}

void test_not_do_iterate_at_all(bool use_pthread) {
    int64_t result = 0;
    int64_t expected_result = 0;
    bthread::ExecutionQueueId<LongIntTask> queue_id;
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add2, &result));
    for (int i = 0; i < 100; ++i) {
        expected_result += i;
        ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, i));
    }
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_NE(0, bthread::execution_queue_execute(queue_id, 0));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));
    ASSERT_EQ(expected_result, result);
}

TEST_F(ExecutionQueueTest, not_do_iterate_at_all) {
    for (int i = 0; i < 2; ++i) {
        test_not_do_iterate_at_all(i);
    }
}

int add_with_suspend3(void* meta, bthread::TaskIterator<LongIntTask>& iter) {
    int64_t* result = (int64_t*)meta;
    if (iter.is_queue_stopped()) {
        stopped = true;
        return 0;
    }
    for (; iter; ++iter) {
        if (iter->value == -100) {
            g_suspending = true;
            while (g_suspending) {
                usleep(10);
            }
            if (iter->event) { iter->event->signal(); }
        } else {
            *result += iter->value;
            if (iter->event) { iter->event->signal(); }
        }
    }
    return 0;
}

void test_cancel_unexecuted_high_priority_task(bool use_pthread) {
    g_should_be_urgent = false;
    bthread::ExecutionQueueId<LongIntTask> queue_id = { 0 }; // to suppress warnings
    bthread::ExecutionQueueOptions options;
    options.use_pthread = use_pthread;
    if (options.use_pthread) {
        LOG(INFO) << "================ pthread ================";
    } else {
        LOG(INFO) << "================ bthread ================";
    }
    int64_t result = 0;
    ASSERT_EQ(0, bthread::execution_queue_start(&queue_id, &options,
                                                add_with_suspend3, &result));
    // Push a normal task to make the executor suspend
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, -100));
    while (!g_suspending) {
        usleep(10);
    }
    // At this point, executor is suspended by the first task. Then we put
    // a high_priority task which is going to be cancelled immediately,
    // expecting that both operations are successful.
    bthread::TaskHandle h;
    ASSERT_EQ(0, bthread::execution_queue_execute(
        queue_id, -100, &bthread::TASK_OPTIONS_URGENT, &h));
    ASSERT_EQ(0, bthread::execution_queue_cancel(h));

    // Resume executor
    g_suspending = false;

    // Push a normal task
    ASSERT_EQ(0, bthread::execution_queue_execute(queue_id, 12345));

    // The execq should stop normally
    ASSERT_EQ(0, bthread::execution_queue_stop(queue_id));
    ASSERT_EQ(0, bthread::execution_queue_join(queue_id));

    ASSERT_EQ(12345, result);
}

TEST_F(ExecutionQueueTest, cancel_unexecuted_high_priority_task) {
    for (int i = 0; i < 2; ++i) {
        test_cancel_unexecuted_high_priority_task(i);
    }
}
} // namespace
