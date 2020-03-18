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

#include <iostream>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "bthread/bthread.h"
#include "bthread/task_group.h"
#include "bthread/butex.h"

namespace bthread {
void id_status(bthread_id_t, std::ostream &);
uint32_t id_value(bthread_id_t id);
}

namespace {
inline uint32_t get_version(bthread_id_t id) {
    return (uint32_t)(id.value & 0xFFFFFFFFul);
}

struct SignalArg {
    bthread_id_t id;
    long sleep_us_before_fight;
    long sleep_us_before_signal;
};

void* signaller(void* void_arg) {
    SignalArg arg = *(SignalArg*)void_arg;
    bthread_usleep(arg.sleep_us_before_fight);
    void* data = NULL;
    int rc = bthread_id_trylock(arg.id, &data);
    if (rc == 0) {
        EXPECT_EQ(0xdead, *(int*)data);
        ++*(int*)data;
        //EXPECT_EQ(EBUSY, bthread_id_destroy(arg.id, ECANCELED));
        bthread_usleep(arg.sleep_us_before_signal);
        EXPECT_EQ(0, bthread_id_unlock_and_destroy(arg.id));
        return void_arg;
    } else {
        EXPECT_TRUE(EBUSY == rc || EINVAL == rc);
        return NULL;
    }
}

TEST(BthreadIdTest, join_after_destroy) {
    bthread_id_t id1;
    int x = 0xdead;
    ASSERT_EQ(0, bthread_id_create_ranged(&id1, &x, NULL, 2));
    bthread_id_t id2 = { id1.value + 1 };
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    ASSERT_EQ(get_version(id1), bthread::id_value(id2));
    pthread_t th[8];
    SignalArg args[ARRAY_SIZE(th)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].sleep_us_before_fight = 0;
        args[i].sleep_us_before_signal = 0;
        args[i].id = (i == 0 ? id1 : id2);
        ASSERT_EQ(0, pthread_create(&th[i], NULL, signaller, &args[i]));
    }
    void* ret[ARRAY_SIZE(th)];
    size_t non_null_ret = 0;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], &ret[i]));
        non_null_ret += (ret[i] != NULL);
    }
    ASSERT_EQ(1UL, non_null_ret);
    ASSERT_EQ(0, bthread_id_join(id1));
    ASSERT_EQ(0, bthread_id_join(id2));
    ASSERT_EQ(0xdead + 1, x);
    ASSERT_EQ(get_version(id1) + 5, bthread::id_value(id1));
    ASSERT_EQ(get_version(id1) + 5, bthread::id_value(id2));
}

TEST(BthreadIdTest, join_before_destroy) {
    bthread_id_t id1;
    int x = 0xdead;
    ASSERT_EQ(0, bthread_id_create(&id1, &x, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    pthread_t th[8];
    SignalArg args[ARRAY_SIZE(th)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].sleep_us_before_fight = 10000;
        args[i].sleep_us_before_signal = 0;
        args[i].id = id1;
        ASSERT_EQ(0, pthread_create(&th[i], NULL, signaller, &args[i]));
    }
    ASSERT_EQ(0, bthread_id_join(id1));
    ASSERT_EQ(0xdead + 1, x);
    ASSERT_EQ(get_version(id1) + 4, bthread::id_value(id1));

    void* ret[ARRAY_SIZE(th)];
    size_t non_null_ret = 0;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], &ret[i]));
        non_null_ret += (ret[i] != NULL);
    }
    ASSERT_EQ(1UL, non_null_ret);
}

struct OnResetArg {
    bthread_id_t id;
    int error_code;
};

int on_reset(bthread_id_t id, void* data, int error_code) {
    OnResetArg* arg = static_cast<OnResetArg*>(data);
    arg->id = id;
    arg->error_code = error_code;
    return bthread_id_unlock_and_destroy(id);
}

TEST(BthreadIdTest, error_is_destroy) {
    bthread_id_t id1;
    OnResetArg arg = { { 0 }, 0 };
    ASSERT_EQ(0, bthread_id_create(&id1, &arg, on_reset));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    ASSERT_EQ(0, bthread_id_error(id1, EBADF));
    ASSERT_EQ(EBADF, arg.error_code);
    ASSERT_EQ(id1.value, arg.id.value);
    ASSERT_EQ(get_version(id1) + 4, bthread::id_value(id1));
}

TEST(BthreadIdTest, error_is_destroy_ranged) {
    bthread_id_t id1;
    OnResetArg arg = { { 0 }, 0 };
    ASSERT_EQ(0, bthread_id_create_ranged(&id1, &arg, on_reset, 2));
    bthread_id_t id2 = { id1.value + 1 };
    ASSERT_EQ(get_version(id1), bthread::id_value(id2));
    ASSERT_EQ(0, bthread_id_error(id2, EBADF));
    ASSERT_EQ(EBADF, arg.error_code);
    ASSERT_EQ(id2.value, arg.id.value);
    ASSERT_EQ(get_version(id1) + 5, bthread::id_value(id2));
}

TEST(BthreadIdTest, default_error_is_destroy) {
    bthread_id_t id1;
    ASSERT_EQ(0, bthread_id_create(&id1, NULL, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    ASSERT_EQ(0, bthread_id_error(id1, EBADF));
    ASSERT_EQ(get_version(id1) + 4, bthread::id_value(id1));
}

TEST(BthreadIdTest, doubly_destroy) {
    bthread_id_t id1;
    ASSERT_EQ(0, bthread_id_create_ranged(&id1, NULL, NULL, 2));
    bthread_id_t id2 = { id1.value + 1 };
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    ASSERT_EQ(get_version(id1), bthread::id_value(id2));
    ASSERT_EQ(0, bthread_id_error(id1, EBADF));
    ASSERT_EQ(get_version(id1) + 5, bthread::id_value(id1));
    ASSERT_EQ(get_version(id1) + 5, bthread::id_value(id2));
    ASSERT_EQ(EINVAL, bthread_id_error(id1, EBADF));
    ASSERT_EQ(EINVAL, bthread_id_error(id2, EBADF));
}

static int on_numeric_error(bthread_id_t id, void* data, int error_code) {
    std::vector<int>* result = static_cast<std::vector<int>*>(data);
    result->push_back(error_code);
    EXPECT_EQ(0, bthread_id_unlock(id));
    return 0;
}

TEST(BthreadIdTest, many_error) {
    bthread_id_t id1;
    std::vector<int> result;
    ASSERT_EQ(0, bthread_id_create(&id1, &result, on_numeric_error));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    int err = 0;
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, bthread_id_error(id1, err++));
    }
    ASSERT_EQ((size_t)N, result.size());
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(i, result[i]);
    }
    ASSERT_EQ(0, bthread_id_trylock(id1, NULL));
    ASSERT_EQ(get_version(id1) + 1, bthread::id_value(id1));
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, bthread_id_error(id1, err++));
    }
    ASSERT_EQ((size_t)N, result.size());
    ASSERT_EQ(0, bthread_id_unlock(id1));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    ASSERT_EQ((size_t)2*N, result.size());
    for (int i = 0; i < 2*N; ++i) {
        EXPECT_EQ(i, result[i]);
    }
    result.clear();
    
    ASSERT_EQ(0, bthread_id_trylock(id1, NULL));
    ASSERT_EQ(get_version(id1) + 1, bthread::id_value(id1));
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, bthread_id_error(id1, err++));
    }
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(id1));
    ASSERT_TRUE(result.empty());
}

static void* locker(void* arg) {
    bthread_id_t id = { (uintptr_t)arg };
    butil::Timer tm;
    tm.start();
    EXPECT_EQ(0, bthread_id_lock(id, NULL));
    bthread_usleep(2000);
    EXPECT_EQ(0, bthread_id_unlock(id));
    tm.stop();
    LOG(INFO) << "Unlocked, tm=" << tm.u_elapsed();
    return NULL;
}

TEST(BthreadIdTest, id_lock) {
    bthread_id_t id1;
    ASSERT_EQ(0, bthread_id_create(&id1, NULL, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    pthread_t th[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, locker,
                                    (void*)id1.value));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL));
    }
}

static void* failed_locker(void* arg) {
    bthread_id_t id = { (uintptr_t)arg };
    int rc = bthread_id_lock(id, NULL);
    if (rc == 0) {
        bthread_usleep(2000);
        EXPECT_EQ(0, bthread_id_unlock_and_destroy(id));
        return (void*)1;
    } else {
        EXPECT_EQ(EINVAL, rc);
        return NULL;
    }
}

TEST(BthreadIdTest, id_lock_and_destroy) {
    bthread_id_t id1;
    ASSERT_EQ(0, bthread_id_create(&id1, NULL, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    pthread_t th[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, failed_locker,
                                    (void*)id1.value));
    }
    int non_null = 0;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        void* ret = NULL;
        ASSERT_EQ(0, pthread_join(th[i], &ret));
        non_null += (ret != NULL);
    }
    ASSERT_EQ(1, non_null);
}

TEST(BthreadIdTest, join_after_destroy_before_unlock) {
    bthread_id_t id1;
    int x = 0xdead;
    ASSERT_EQ(0, bthread_id_create(&id1, &x, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    pthread_t th[8];
    SignalArg args[ARRAY_SIZE(th)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].sleep_us_before_fight = 0;
        args[i].sleep_us_before_signal = 20000;
        args[i].id = id1;
        ASSERT_EQ(0, pthread_create(&th[i], NULL, signaller, &args[i]));
    }
    bthread_usleep(10000);
    // join() waits until destroy() is called.
    ASSERT_EQ(0, bthread_id_join(id1));
    ASSERT_EQ(0xdead + 1, x);
    ASSERT_EQ(get_version(id1) + 4, bthread::id_value(id1));

    void* ret[ARRAY_SIZE(th)];
    size_t non_null_ret = 0;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], &ret[i]));
        non_null_ret += (ret[i] != NULL);
    }
    ASSERT_EQ(1UL, non_null_ret);
}

struct StoppedWaiterArgs {
    bthread_id_t id;
    bool thread_started;
};

void* stopped_waiter(void* void_arg) {
    StoppedWaiterArgs* args = (StoppedWaiterArgs*)void_arg;
    args->thread_started = true;
    EXPECT_EQ(0, bthread_id_join(args->id));
    EXPECT_EQ(get_version(args->id) + 4, bthread::id_value(args->id));
    return NULL;
}

TEST(BthreadIdTest, stop_a_wait_after_fight_before_signal) {
    bthread_id_t id1;
    int x = 0xdead;
    ASSERT_EQ(0, bthread_id_create(&id1, &x, NULL));
    ASSERT_EQ(get_version(id1), bthread::id_value(id1));
    void* data;
    ASSERT_EQ(0, bthread_id_trylock(id1, &data));
    ASSERT_EQ(&x, data);
    bthread_t th[8];
    StoppedWaiterArgs args[ARRAY_SIZE(th)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].id = id1;
        args[i].thread_started = false;
        ASSERT_EQ(0, bthread_start_urgent(&th[i], NULL, stopped_waiter, &args[i]));
    }
    // stop does not wake up bthread_id_join
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        bthread_stop(th[i]);
    }
    bthread_usleep(10000);
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_TRUE(bthread::TaskGroup::exists(th[i]));
    }
    // destroy the id to end the joinings.
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(id1));
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, bthread_join(th[i], NULL));
    }
}

void* waiter(void* arg) {
    bthread_id_t id = { (uintptr_t)arg };
    EXPECT_EQ(0, bthread_id_join(id));
    EXPECT_EQ(get_version(id) + 4, bthread::id_value(id));
    return NULL;
}

int handle_data(bthread_id_t id, void* data, int error_code) {
    EXPECT_EQ(EBADF, error_code);
    ++*(int*)data;
    EXPECT_EQ(0, bthread_id_unlock_and_destroy(id));
    return 0;
}

TEST(BthreadIdTest, list_signal) {
    bthread_id_list_t list;
    ASSERT_EQ(0, bthread_id_list_init(&list, 32, 32));
    bthread_id_t id[16];
    int data[ARRAY_SIZE(id)];
    for (size_t i = 0; i < ARRAY_SIZE(id); ++i) {
        data[i] = i;
        ASSERT_EQ(0, bthread_id_create(&id[i], &data[i], handle_data));
        ASSERT_EQ(get_version(id[i]), bthread::id_value(id[i]));
        ASSERT_EQ(0, bthread_id_list_add(&list, id[i]));
    }
    pthread_t th[ARRAY_SIZE(id)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, waiter, (void*)(intptr_t)id[i].value));
    }
    bthread_usleep(10000);
    ASSERT_EQ(0, bthread_id_list_reset(&list, EBADF));

    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ((int)(i + 1), data[i]);
        ASSERT_EQ(0, pthread_join(th[i], NULL));
        // already reset.
        ASSERT_EQ((int)(i + 1), data[i]);
    }

    bthread_id_list_destroy(&list);
}

int error_without_unlock(bthread_id_t, void *, int) {
    return 0;
}

TEST(BthreadIdTest, status) {
    bthread_id_t id;
    bthread_id_create(&id, NULL, NULL);
    bthread::id_status(id, std::cout);
    bthread_id_lock(id, NULL);
    bthread_id_error(id, 123);
    bthread_id_error(id, 256);
    bthread_id_error(id, 1256);
    bthread::id_status(id, std::cout);
    bthread_id_unlock_and_destroy(id);
    bthread_id_create(&id, NULL, error_without_unlock);
    bthread_id_lock(id, NULL);
    bthread::id_status(id, std::cout);
    bthread_id_error(id, 12);
    bthread::id_status(id, std::cout);
    bthread_id_unlock(id);
    bthread::id_status(id, std::cout);
    bthread_id_unlock_and_destroy(id);
}

TEST(BthreadIdTest, reset_range) {
    bthread_id_t id;
    ASSERT_EQ(0, bthread_id_create(&id, NULL, NULL));
    ASSERT_EQ(0, bthread_id_lock_and_reset_range(id, NULL, 1000));
    bthread::id_status(id, std::cout);
    bthread_id_unlock(id);
    ASSERT_EQ(0, bthread_id_lock_and_reset_range(id, NULL, 300));
    bthread::id_status(id, std::cout);
    bthread_id_unlock_and_destroy(id);
}

static bool any_thread_quit = false;

struct FailToLockIdArgs {
    bthread_id_t id;
    int expected_return;
};

static void* fail_to_lock_id(void* args_in) {
    FailToLockIdArgs* args = (FailToLockIdArgs*)args_in;
    butil::Timer tm;
    EXPECT_EQ(args->expected_return, bthread_id_lock(args->id, NULL));
    any_thread_quit = true;
    return NULL;
}

TEST(BthreadIdTest, about_to_destroy_before_locking) {
    bthread_id_t id;
    ASSERT_EQ(0, bthread_id_create(&id, NULL, NULL));
    ASSERT_EQ(0, bthread_id_lock(id, NULL));
    ASSERT_EQ(0, bthread_id_about_to_destroy(id));
    pthread_t pth;
    bthread_t bth;
    FailToLockIdArgs args = { id, EPERM };
    ASSERT_EQ(0, pthread_create(&pth, NULL, fail_to_lock_id, &args));
    ASSERT_EQ(0, bthread_start_background(&bth, NULL, fail_to_lock_id, &args));
    // The threads should quit soon.
    pthread_join(pth, NULL);
    bthread_join(bth, NULL);
    bthread::id_status(id, std::cout);
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(id));
}

static void* succeed_to_lock_id(void* arg) {
    bthread_id_t id = *(bthread_id_t*)arg;
    EXPECT_EQ(0, bthread_id_lock(id, NULL));
    EXPECT_EQ(0, bthread_id_unlock(id));
    return NULL;
}

TEST(BthreadIdTest, about_to_destroy_cancelled) {
    bthread_id_t id;
    ASSERT_EQ(0, bthread_id_create(&id, NULL, NULL));
    ASSERT_EQ(0, bthread_id_lock(id, NULL));
    ASSERT_EQ(0, bthread_id_about_to_destroy(id));
    ASSERT_EQ(0, bthread_id_unlock(id));
    pthread_t pth;
    bthread_t bth;
    ASSERT_EQ(0, pthread_create(&pth, NULL, succeed_to_lock_id, &id));
    ASSERT_EQ(0, bthread_start_background(&bth, NULL, succeed_to_lock_id, &id));
    // The threads should quit soon.
    pthread_join(pth, NULL);
    bthread_join(bth, NULL);
    bthread::id_status(id, std::cout);
    ASSERT_EQ(0, bthread_id_lock(id, NULL));
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(id));
}

TEST(BthreadIdTest, about_to_destroy_during_locking) {
    bthread_id_t id;
    ASSERT_EQ(0, bthread_id_create(&id, NULL, NULL));
    ASSERT_EQ(0, bthread_id_lock(id, NULL));
    any_thread_quit = false;
    pthread_t pth;
    bthread_t bth;
    FailToLockIdArgs args = { id, EPERM };
    ASSERT_EQ(0, pthread_create(&pth, NULL, fail_to_lock_id, &args));
    ASSERT_EQ(0, bthread_start_background(&bth, NULL, fail_to_lock_id, &args));

    usleep(100000);
    ASSERT_FALSE(any_thread_quit);
    ASSERT_EQ(0, bthread_id_about_to_destroy(id));

    // The threads should quit soon.
    pthread_join(pth, NULL);
    bthread_join(bth, NULL);
    bthread::id_status(id, std::cout);
    ASSERT_EQ(0, bthread_id_unlock_and_destroy(id));
}

void* const DUMMY_DATA1 = (void*)1;
void* const DUMMY_DATA2 = (void*)2;
int branch_counter = 0;
int branch_tags[4] = {};
int expected_code = 0;
const char* expected_desc = "";
int handler_without_desc(bthread_id_t id, void* data, int error_code) {
    EXPECT_EQ(DUMMY_DATA1, data);
    EXPECT_EQ(expected_code, error_code);
    if (error_code == ESTOP) {
        branch_tags[0] = branch_counter;
        return bthread_id_unlock_and_destroy(id);
    } else {
        branch_tags[1] = branch_counter;
        return bthread_id_unlock(id);
    }
}
int handler_with_desc(bthread_id_t id, void* data, int error_code,
                      const std::string& error_text) {
    EXPECT_EQ(DUMMY_DATA2, data);
    EXPECT_EQ(expected_code, error_code);
    EXPECT_STREQ(expected_desc, error_text.c_str());
    if (error_code == ESTOP) {
        branch_tags[2] = branch_counter;
        return bthread_id_unlock_and_destroy(id);
    } else {
        branch_tags[3] = branch_counter;
        return bthread_id_unlock(id);
    }
}

TEST(BthreadIdTest, error_with_descriptions) {
    bthread_id_t id1;
    ASSERT_EQ(0, bthread_id_create(&id1, DUMMY_DATA1, handler_without_desc));
    bthread_id_t id2;
    ASSERT_EQ(0, bthread_id_create2(&id2, DUMMY_DATA2, handler_with_desc));

    // [ Matched in-place ]
    // Call bthread_id_error on an id created by bthread_id_create
    ++branch_counter;
    expected_code = EINVAL;
    ASSERT_EQ(0, bthread_id_error(id1, expected_code));
    ASSERT_EQ(branch_counter, branch_tags[1]);

    // Call bthread_id_error2 on an id created by bthread_id_create2
    ++branch_counter;
    expected_code = EPERM;
    expected_desc = "description1";
    ASSERT_EQ(0, bthread_id_error2(id2, expected_code, expected_desc));
    ASSERT_EQ(branch_counter, branch_tags[3]);

    // [ Mixed in-place ]
    // Call bthread_id_error on an id created by bthread_id_create2
    ++branch_counter;
    expected_code = ECONNREFUSED;
    expected_desc = "";
    ASSERT_EQ(0, bthread_id_error(id2, expected_code));
    ASSERT_EQ(branch_counter, branch_tags[3]);
    // Call bthread_id_error2 on an id created by bthread_id_create
    ++branch_counter;
    expected_code = EINTR;
    ASSERT_EQ(0, bthread_id_error2(id1, expected_code, ""));
    ASSERT_EQ(branch_counter, branch_tags[1]);

    // [ Matched pending ]
    // Call bthread_id_error on an id created by bthread_id_create
    ++branch_counter;
    expected_code = ECONNRESET;
    ASSERT_EQ(0, bthread_id_lock(id1, NULL));
    ASSERT_EQ(0, bthread_id_error(id1, expected_code));
    ASSERT_EQ(0, bthread_id_unlock(id1));
    ASSERT_EQ(branch_counter, branch_tags[1]);

    // Call bthread_id_error2 on an id created by bthread_id_create2
    ++branch_counter;
    expected_code = ENOSPC;
    expected_desc = "description3";
    ASSERT_EQ(0, bthread_id_lock(id2, NULL));
    ASSERT_EQ(0, bthread_id_error2(id2, expected_code, expected_desc));
    ASSERT_EQ(0, bthread_id_unlock(id2));
    ASSERT_EQ(branch_counter, branch_tags[3]);

    // [ Mixed pending ]
    // Call bthread_id_error on an id created by bthread_id_create2
    ++branch_counter;
    expected_code = ESTOP;
    expected_desc = "";
    ASSERT_EQ(0, bthread_id_lock(id2, NULL));
    ASSERT_EQ(0, bthread_id_error(id2, expected_code));
    ASSERT_EQ(0, bthread_id_unlock(id2));
    ASSERT_EQ(branch_counter, branch_tags[2]);
    // Call bthread_id_error2 on an id created by bthread_id_create
    ++branch_counter;
    ASSERT_EQ(0, bthread_id_lock(id1, NULL));
    ASSERT_EQ(0, bthread_id_error2(id1, expected_code, ""));
    ASSERT_EQ(0, bthread_id_unlock(id1));
    ASSERT_EQ(branch_counter, branch_tags[0]);
}
} // namespace
