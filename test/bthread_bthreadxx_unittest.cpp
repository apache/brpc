//
// Created by psi on 2019/4/18.
//

#include <atomic>
#include <functional>
#include <iostream>
#include <sstream>
#include <thread>
#include <bthread/bthreadxx.h>
#include <gtest/gtest.h>

namespace {

void add_func(int x, int y, int& out) {
    out = x + y;
}

TEST(BthreadXXTest, sanity) {
    int result = 0;
    bthread::bthread th(add_func, 1, 2, std::ref(result));
    ASSERT_TRUE(th.joinable());
    th.join();
    ASSERT_FALSE(th.joinable());
    ASSERT_EQ(3, result);
    ASSERT_THROW(th.join(), std::system_error);

    th = bthread::bthread(add_func, 100, 200, std::ref(result));
    ASSERT_TRUE(th.joinable());
    th.detach();
    ASSERT_FALSE(th.joinable());
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // sleep to wait for detached
    ASSERT_EQ(300, result);
    ASSERT_THROW(th.detach(), std::system_error);

    std::atomic<int> state{0};
    auto inner = [](std::atomic<int>& state) {
        if (state.load() == 0) {
            state.store(1);
        }
    };
    // With urgent_launch_tag the inner bthread should run before the outer thread
    auto spawner = [&state, &inner]() {
        bthread::bthread sub_thread(bthread::urgent_launch_tag(), inner, std::ref(state));
        if (state.load() == 1) {
            state.store(2);
        }
        sub_thread.join();
    };
    bthread::bthread outer_thread(spawner);
    outer_thread.join();
    ASSERT_EQ(2, state.load());
}

TEST(BthreadXXTest, id_sanity) {
    bthread::bthread not_a_thread;
    std::ostringstream oss;
    oss << not_a_thread.get_id();
    ASSERT_STREQ("0", oss.str().c_str());

    oss = std::ostringstream();
    auto dummy_func = [](){
        return;
    };
    bthread::bthread dummy_thread(dummy_func);
    ASSERT_NE(not_a_thread.get_id(), dummy_thread.get_id());
    oss << dummy_thread.get_id();
    ASSERT_STRNE("0", oss.str().c_str());
    dummy_thread.join(); // dummy_thread no longer associates with a bthread
    ASSERT_EQ(not_a_thread.get_id(), dummy_thread.get_id());
}

}
