//
// Created by psi on 2019/4/18.
//

#include <butil/macros.h>

#ifdef BUTIL_CXX11_ENABLED

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
    // Tests joined thread
    int result = 0;
    bthread::BThread th(add_func, 1, 2, std::ref(result));
    ASSERT_TRUE(th.joinable());
    th.join();
    ASSERT_FALSE(th.joinable());
    ASSERT_EQ(3, result);
    ASSERT_THROW(th.join(), std::system_error);

    // Tests detached thread
    th = bthread::BThread(add_func, 100, 200, std::ref(result));
    ASSERT_TRUE(th.joinable());
    th.detach();
    ASSERT_FALSE(th.joinable());
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // sleep to wait for detached
    ASSERT_EQ(300, result);
    ASSERT_THROW(th.detach(), std::system_error);

    // Tests urgent launch
    std::atomic<int> state{0};
    auto inner = [](std::atomic<int>& state) {
        if (state.load() == 0) {
            state.store(1);
        }
    };
    // With urgent_launch_tag the inner bthread should run before the outer thread
    auto spawner = [&state, &inner]() {
        bthread::BThread sub_thread(bthread::urgent_launch_tag, inner, std::ref(state));
        if (state.load() == 1) {
            state.store(2);
        }
        sub_thread.join();
    };
    bthread::BThread outer_thread(spawner);
    outer_thread.join();
    ASSERT_EQ(2, state.load());

    // Tests not-a-thread
    const bthread::BThread not_a_thread;
    ASSERT_FALSE(not_a_thread.joinable());
    bthread::BThread th1([]() {});
    ASSERT_TRUE(th1.joinable());
    bthread::BThread th2(std::move(th1));
    ASSERT_TRUE(th2.joinable());
    ASSERT_FALSE(th1.joinable());
    th2.join();
    th1 = bthread::BThread([]() {});
    ASSERT_TRUE(th1.joinable());
    ASSERT_FALSE(th2.joinable());
    th2 = std::move(th1);
    ASSERT_TRUE(th2.joinable());
    ASSERT_FALSE(th1.joinable());
    th2.join();
}

TEST(BthreadXXTest, id_sanity) {
    const bthread::BThread not_a_thread;
    const bthread::BThread::id inv_id;
    std::ostringstream oss;
    oss << not_a_thread.get_id();
    ASSERT_STREQ("0", oss.str().c_str());
    ASSERT_EQ(inv_id, not_a_thread.get_id());

    oss = std::ostringstream();
    auto dummy_func = []() {
        return;
    };
    bthread::BThread dummy_thread(dummy_func);
    ASSERT_NE(not_a_thread.get_id(), dummy_thread.get_id());
    oss << dummy_thread.get_id();
    ASSERT_STRNE("0", oss.str().c_str());
    dummy_thread.join(); // dummy_thread no longer associates with a bthread
    ASSERT_EQ(not_a_thread.get_id(), dummy_thread.get_id());

    dummy_thread = bthread::BThread(dummy_func);
    ASSERT_NE(not_a_thread.get_id(), dummy_thread.get_id());
    bthread::BThread dummy_thread2 = std::move(dummy_thread);
    ASSERT_EQ(not_a_thread.get_id(), dummy_thread.get_id());
    ASSERT_NE(not_a_thread.get_id(), dummy_thread2.get_id());
    dummy_thread2.join();
}

class MilliSTimeGuard {
public:
    explicit MilliSTimeGuard(int* out_millis) noexcept: _out_millis(out_millis),
                                                        _begin_tp(
                                                                std::chrono::steady_clock::now()) {
    }

    ~MilliSTimeGuard() {
        auto end_time = std::chrono::steady_clock::now();
        *_out_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - _begin_tp).count();
    }

private:
    int* _out_millis;
    std::chrono::steady_clock::time_point _begin_tp;
};

TEST(BthreadXXTest, this_thread_sleep) {
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    int millis_elapsed = 0;
    {
        MilliSTimeGuard tg(&millis_elapsed);
        bthread::BThread th([]() {
            bthread::this_bthread::sleep_for(milliseconds(100));
        });
        th.join();
    }
    ASSERT_LE(100, millis_elapsed);
    ASSERT_GE(110, millis_elapsed);

    {
        MilliSTimeGuard tg(&millis_elapsed);
        bthread::BThread th([]() {
            bthread::this_bthread::sleep_for(milliseconds(0));
        });
        th.join();
    }
    ASSERT_GE(5, millis_elapsed);

    {
        MilliSTimeGuard tg(&millis_elapsed);
        bthread::BThread th([]() {
            bthread::this_bthread::sleep_until(steady_clock::now() + milliseconds(150));
        });
        th.join();
    }
    ASSERT_LE(150, millis_elapsed);
    ASSERT_GE(160, millis_elapsed);

    {
        MilliSTimeGuard tg(&millis_elapsed);
        bthread::BThread th([]() {
            bthread::this_bthread::sleep_until(steady_clock::now() + milliseconds(-5));
        });
        th.join();
    }
    ASSERT_GE(5, millis_elapsed);
}

TEST(BthreadXXTest, this_thread_get_id) {
    const bthread::BThread::id inv_id;
    bthread::BThread::id id;
    id = bthread::this_bthread::get_id(); // call from non-bthread returns an invalid id
    ASSERT_EQ(inv_id, id);

    bthread::BThread get_id_thread([&id]() { id = bthread::this_bthread::get_id(); });
    auto id2 = get_id_thread.get_id();
    get_id_thread.join();
    ASSERT_NE(inv_id, id);
    ASSERT_EQ(id, id2);
}

TEST(BthreadXXTest, this_thread_yield) {
    int out = 0;
    bthread::BThread yield_thread([&out](){
        bthread::this_bthread::yield();
        out = 1;
    });
    yield_thread.join();
    ASSERT_EQ(1, out);
}

} // namespace

#endif // BUTIL_CXX11_ENABLED
