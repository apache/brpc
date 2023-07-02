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

#include "butil/thread_key.h"
#include "butil/fast_rand.h"
#include "bthread/bthread.h"

namespace butil {
namespace {

//pthread_key_xxx implication without num limit...
//user promise no setspecific/getspecific called in calling thread_key_delete().
// Check whether an entry is unused.
#define KEY_UNUSED(p) (((p) & 1) == 0)

// Check whether a key is usable.  We cannot reuse an allocated key if
// the sequence counter would overflow after the next destroy call.
// This would mean that we potentially free memory for a key with the
// same sequence.  This is *very* unlikely to happen, A program would
// have to create and destroy a key 2^31 times. If it should happen we
// simply don't use this specific key anymore.
#define KEY_USABLE(p) (((size_t) (p)) < ((size_t) ((p) + 2)))

bool g_started = false;
bool g_stopped = false;

struct ThreadKeyInfo {
    uint32_t id;
    uint32_t seq;
};

TEST(ThreadLocalTest, sanity) {
    {
        ThreadKey key;
        for (int i = 0; i < 5; ++i) {
            std::unique_ptr<int> data(new int(1));
            int *raw_data = data.get();
            ASSERT_EQ(0, butil::thread_key_create(key, NULL));

            ASSERT_EQ(NULL, butil::thread_getspecific(key));
            ASSERT_EQ(0, butil::thread_setspecific(key, (void *)raw_data));
            ASSERT_EQ(raw_data, butil::thread_getspecific(key));

            ASSERT_EQ(0, butil::thread_key_delete(key));
            ASSERT_EQ(NULL, butil::thread_getspecific(key));
            ASSERT_NE(0, butil::thread_setspecific(key, (void *)raw_data));
        }
    }

    for (int i = 0; i < 5; ++i) {
        ThreadLocal<int> tl;
        ASSERT_TRUE(tl.get()!=NULL);
        int* data = new int;
        tl.reset(data); // tl owns data
        ASSERT_EQ(data, tl.get());
        tl.reset(); // data has been deleted
        ASSERT_TRUE(tl.get()!=NULL);
    }
}

TEST(ThreadLocalTest, thread_key_seq) {
    std::vector<uint32_t> seqs;
    std::vector<ThreadKey> keys;
    for (int i = 0; i < 10000; ++i) {
        bool create = fast_rand_less_than(2);
        uint64_t num = fast_rand_less_than(5);
        if (keys.empty() || create) {
            for (uint64_t j = 0; j < num; ++j) {
                keys.emplace_back();
                ASSERT_EQ(0, butil::thread_key_create(keys.back(), NULL));
                ASSERT_TRUE(!KEY_UNUSED(keys.back()._seq));
                if (keys.back()._id >= seqs.size()) {
                    seqs.resize(keys.back()._id + 1);
                } else {
                    ASSERT_EQ(seqs[keys.back()._id] + 2, keys.back()._seq);
                }
                seqs[keys.back()._id] = keys.back()._seq;
            }
        } else {
            for (uint64_t j = 0; j < num && !keys.empty(); ++j) {
                uint64_t index = fast_rand_less_than(keys.size());
                ASSERT_TRUE(!KEY_UNUSED(seqs[keys[index]._id]));
                ASSERT_EQ(0, butil::thread_key_delete(keys[index]));
                keys.erase(keys.begin() + index);
            }
        }
    }
}

void* THreadKeyCreateAndDeleteFunc(void* arg) {
    while (!g_stopped) {
        ThreadKey key;
        EXPECT_EQ(0, butil::thread_key_create(key, NULL));
        EXPECT_TRUE(!KEY_UNUSED(key._seq));
        EXPECT_EQ(0, butil::thread_key_delete(key));
    }
    return NULL;
}

TEST(ThreadLocalTest, thread_key_create_and_delete) {
    LOG(INFO) << "numeric_limits<uint32_t>::max()=" << std::numeric_limits<uint32_t>::max();
    g_stopped = false;
    const int thread_num = 8;
    pthread_t threads[thread_num];
    for (int i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, THreadKeyCreateAndDeleteFunc, NULL));
    }
    sleep(2);
    g_stopped = true;
    for (const auto& thread : threads) {
        pthread_join(thread, NULL);
    }
}

void* ThreadLocalFunc(void* arg) {
    auto thread_locals = (std::vector<ThreadLocal<int>*>*)arg;
    std::vector<int> expects(thread_locals->size(), 0);
    for (auto tl : *thread_locals) {
        EXPECT_TRUE(tl->get() != NULL);
        *(tl->get()) = 0;
    }
    while (!g_stopped) {
        uint64_t index =
            fast_rand_less_than(thread_locals->size());
        EXPECT_TRUE((*thread_locals)[index]->get() != NULL);
        EXPECT_EQ(*((*thread_locals)[index]->get()), expects[index]);
        ++(*((*thread_locals)[index]->get()));
        ++expects[index];
        bthread_usleep(10);
    }
    return NULL;
}

TEST(ThreadLocalTest, thread_local_multi_thread) {
    g_stopped = false;
    int thread_local_num = 20480;
    std::vector<ThreadLocal<int>*> args(thread_local_num, NULL);
    for (int i = 0; i < thread_local_num; ++i) {
        args[i] = new ThreadLocal<int>();
        ASSERT_TRUE(args[i]->get() != NULL);
    }
    const int thread_num = 8;
    pthread_t threads[thread_num];
    for (int i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, ThreadLocalFunc, &args));
    }

    sleep(5);
    g_stopped = true;
    for (const auto& thread : threads) {
        pthread_join(thread, NULL);
    }
    for (auto tl : args) {
        delete tl;
    }
}

struct BAIDU_CACHELINE_ALIGNMENT ThreadKeyArg {
    std::vector<ThreadKey*> thread_keys;
    bool ready_delete = false;
};

bool g_deleted = false;
void* ThreadKeyFunc(void* arg) {
    auto thread_key_arg = (ThreadKeyArg*)arg;
    auto thread_keys = thread_key_arg->thread_keys;
    std::vector<int> expects(thread_keys.size(), 0);
    for (auto key : thread_keys) {
        EXPECT_TRUE(butil::thread_getspecific(*key) == NULL);
        EXPECT_EQ(0, butil::thread_setspecific(*key, new int(0)));
        EXPECT_EQ(*(static_cast<int*>(butil::thread_getspecific(*key))), 0);
    }
    while (!g_stopped) {
        uint64_t index =
            fast_rand_less_than(thread_keys.size());
        auto data = static_cast<int*>(butil::thread_getspecific(*thread_keys[index]));
        EXPECT_TRUE(data != NULL);
        EXPECT_EQ(*data, expects[index]);
        ++(*data);
        ++expects[index];
        bthread_usleep(10);
    }

    thread_key_arg->ready_delete = true;
    while (!g_deleted) {
        bthread_usleep(10);
    }

    for (auto key : thread_keys) {
        EXPECT_TRUE(butil::thread_getspecific(*key) == NULL)
        << butil::thread_getspecific(*key);
    }
    return NULL;
}

TEST(ThreadLocalTest, thread_key_multi_thread) {
    g_stopped = false;
    g_deleted = false;
    std::vector<ThreadKey*> thread_keys;
    int key_num = 20480;
    for (int i = 0; i < key_num; ++i) {
        thread_keys.push_back(new ThreadKey());
        ASSERT_EQ(0, butil::thread_key_create(*thread_keys.back(), [](void* data) {
            delete static_cast<int*>(data);
        }));
        ASSERT_TRUE(butil::thread_getspecific(*thread_keys.back()) == NULL);
        ASSERT_EQ(0, butil::thread_setspecific(*thread_keys.back(), new int(0)));
        ASSERT_EQ(*(static_cast<int*>(butil::thread_getspecific(*thread_keys.back()))), 0);
    }
    const int thread_num = 8;
    std::vector<ThreadKeyArg> args(thread_num);
    pthread_t threads[thread_num];
    for (int i = 0; i < thread_num; ++i) {
        args[i].thread_keys = thread_keys;
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, ThreadKeyFunc, &args[i]));
    }

    sleep(5);
    g_stopped = true;
    while (true) {
        bool all_ready = true;
        for (int i = 0; i < thread_num; ++i) {
            if (!args[i].ready_delete) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            break;
        }
        usleep(1000);
    }
    for (auto key : thread_keys) {
        ASSERT_EQ(0, butil::thread_key_delete(*key));
        ASSERT_TRUE(butil::thread_getspecific(*key) == NULL);
    }
    g_deleted = true;

    for (const auto& thread : threads) {
        ASSERT_EQ(0, pthread_join(thread, NULL));
    }
    for (auto key : thread_keys) {
        delete key;
    }
}

DEFINE_bool(test_pthread_key, true, "test pthread_key");

struct BAIDU_CACHELINE_ALIGNMENT ThreadKeyPerfArgs {
    pthread_key_t pthread_key;
    ThreadKey* thread_key;
    bool is_pthread_key;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    ThreadKeyPerfArgs()
        : thread_key(NULL)
        , is_pthread_key(true)
        , counter(0)
        , elapse_ns(0)
        , ready(false) {}
};

void* ThreadKeyPerfFunc(void* void_arg) {
    auto args = (ThreadKeyPerfArgs*)void_arg;
    args->ready = true;
    std::unique_ptr<int> data(new int(1));
    if (args->is_pthread_key) {
        pthread_setspecific(args->pthread_key, (void*)data.get());
    } else {
        butil::thread_setspecific(*args->thread_key, (void*)data.get());
    }
    butil::Timer t;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        bthread_usleep(10);
    }
    t.start();
    while (!g_stopped) {
        if (args->is_pthread_key) {
            pthread_getspecific(args->pthread_key);
        } else {
            butil::thread_getspecific(*args->thread_key);
        }
        ++args->counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return NULL;
}


void ThreadKeyPerfTest(int thread_num, bool test_pthread_key) {
    g_started = false;
    g_stopped = false;
    pthread_key_t pthread_key;
    butil::ThreadKey thread_key;
    if (test_pthread_key) {
        ASSERT_EQ(0, pthread_key_create(&pthread_key, NULL));
    } else {
        ASSERT_EQ(0, butil::thread_key_create(thread_key, NULL));
    }
    pthread_t threads[thread_num];
    std::vector<ThreadKeyPerfArgs> args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        if (test_pthread_key) {
            args[i].pthread_key = pthread_key;
            args[i].is_pthread_key = true;
        } else {
            args[i].thread_key = &thread_key;
            args[i].is_pthread_key = false;
        }
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, ThreadKeyPerfFunc, &args[i]));
    }
    while (true) {
        bool all_ready = true;
        for (int i = 0; i < thread_num; ++i) {
            if (!args[i].ready) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            break;
        }
        usleep(1000);
    }
    g_started = true;
    int64_t run_ms = 5 * 1000;
    usleep(run_ms * 1000);
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        pthread_join(threads[i], NULL);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    if (test_pthread_key) {
        ASSERT_EQ(0, pthread_key_delete(pthread_key));
    } else {
        ASSERT_EQ(0, butil::thread_key_delete(thread_key));
    }
    LOG(INFO) << (test_pthread_key ? "pthread_key" : "thread_key")
              << " thread_num=" << thread_num
              << " count=" << count
              << " average_time=" << wait_time / (double)count;
}

struct BAIDU_CACHELINE_ALIGNMENT ThreadLocalPerfArgs {
    ThreadLocal<int>* tl;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    ThreadLocalPerfArgs()
        : tl(NULL) , counter(0)
        , elapse_ns(0) , ready(false) {}
};

void* ThreadLocalPerfFunc(void* void_arg) {
    auto args = (ThreadLocalPerfArgs*)void_arg;
    args->ready = true;
    EXPECT_TRUE(args->tl->get() != NULL);
    butil::Timer t;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        bthread_usleep(10);
    }
    t.start();
    while (!g_stopped) {
        args->tl->get();
        ++args->counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return NULL;
}

void ThreadLocalPerfTest(int thread_num) {
    g_started = false;
    g_stopped = false;
    ThreadLocal<int> tl;
    pthread_t threads[thread_num];
    std::vector<ThreadLocalPerfArgs> args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        args[i].tl = &tl;
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, ThreadLocalPerfFunc, &args[i]));
    }
    while (true) {
        bool all_ready = true;
        for (int i = 0; i < thread_num; ++i) {
            if (!args[i].ready) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            break;
        }
        usleep(1000);
    }
    g_started = true;
    int64_t run_ms = 5 * 1000;
    usleep(run_ms * 1000);
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        pthread_join(threads[i], NULL);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    LOG(INFO) << "ThreadLocal thread_num=" << thread_num
              << " count=" << count
              << " average_time=" << wait_time / (double)count;
}

TEST(ThreadLocalTest, thread_key_performance) {
    int thread_num = 1;
    ThreadKeyPerfTest(thread_num, true);
    ThreadKeyPerfTest(thread_num, false);
    ThreadLocalPerfTest(thread_num);

    thread_num = 4;
    ThreadKeyPerfTest(thread_num, true);
    ThreadKeyPerfTest(thread_num, false);
    ThreadLocalPerfTest(thread_num);

    thread_num = 8;
    ThreadKeyPerfTest(thread_num, true);
    ThreadKeyPerfTest(thread_num, false);
    ThreadLocalPerfTest(thread_num);

}

}
} // namespace butil