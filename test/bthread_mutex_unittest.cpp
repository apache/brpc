// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#include <vector>
#include <gtest/gtest.h>
#include "butil/compat.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/string_printf.h"
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/mutex.h"
#include "butil/gperftools_profiler.h"

#ifdef BUTIL_CXX11_ENABLED
#include <atomic>
#include <chrono>
#include "bthread/bthread_cxx.h"
#endif

namespace {
inline unsigned* get_butex(bthread_mutex_t & m) {
    return m.butex;
}

long start_time = butil::cpuwide_time_ms();
int c = 0;
void* locker(void* arg) {
    bthread_mutex_t* m = (bthread_mutex_t*)arg;
    bthread_mutex_lock(m);
    printf("[%" PRIu64 "] I'm here, %d, %" PRId64 "ms\n",
           pthread_numeric_id(), ++c, butil::cpuwide_time_ms() - start_time);
    bthread_usleep(10000);
    bthread_mutex_unlock(m);
    return NULL;
}

TEST(MutexTest, sanity) {
    bthread_mutex_t m;
    ASSERT_EQ(0, bthread_mutex_init(&m, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_lock(&m));
    ASSERT_EQ(1u, *get_butex(m));
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, locker, &m));
    usleep(5000); // wait for locker to run.
    ASSERT_EQ(257u, *get_butex(m)); // contention
    ASSERT_EQ(0, bthread_mutex_unlock(&m));
    ASSERT_EQ(0, bthread_join(th1, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_destroy(&m));
}

TEST(MutexTest, used_in_pthread) {
    bthread_mutex_t m;
    ASSERT_EQ(0, bthread_mutex_init(&m, NULL));
    pthread_t th[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, locker, &m));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        pthread_join(th[i], NULL);
    }
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_destroy(&m));
}

void* do_locks(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_mutex_timedlock((bthread_mutex_t*)arg, &t));
    return NULL;
}

TEST(MutexTest, timedlock) {
    bthread_cond_t c;
    bthread_mutex_t m1;
    bthread_mutex_t m2;
    ASSERT_EQ(0, bthread_cond_init(&c, NULL));
    ASSERT_EQ(0, bthread_mutex_init(&m1, NULL));
    ASSERT_EQ(0, bthread_mutex_init(&m2, NULL));

    struct timespec t = { -2, 0 };

    bthread_mutex_lock (&m1);
    bthread_mutex_lock (&m2);
    bthread_t pth;
    ASSERT_EQ(0, bthread_start_urgent(&pth, NULL, do_locks, &m1));
    ASSERT_EQ(ETIMEDOUT, bthread_cond_timedwait(&c, &m2, &t));
    ASSERT_EQ(0, bthread_join(pth, NULL));
    bthread_mutex_unlock(&m1);
    bthread_mutex_unlock(&m2);
    bthread_mutex_destroy(&m1);
    bthread_mutex_destroy(&m2);
}

TEST(MutexTest, cpp_wrapper) {
    typedef bthread::Mutex mutex_type;
    mutex_type mutex;
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
    mutex.lock();
    mutex.unlock();
    {
        BAIDU_SCOPED_LOCK(mutex);
    }
    {
        std::unique_lock<mutex_type> lck1;
        std::unique_lock<mutex_type> lck2(mutex);
        lck1.swap(lck2);
        lck1.unlock();
        lck1.lock();
    }
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
    {
        BAIDU_SCOPED_LOCK(*mutex.native_handler());
    }
#ifdef BUTIL_CXX11_ENABLED
    {
        static_assert(std::is_same<mutex_type::native_handle_type, bthread_mutex_t*>::value,
                "Incorrect native_handle_type");
        BAIDU_SCOPED_LOCK(*mutex.native_handle());
    }
#endif
    {
        std::unique_lock<bthread_mutex_t> lck1;
        std::unique_lock<bthread_mutex_t> lck2(*mutex.native_handler());
        lck1.swap(lck2);
        lck1.unlock();
        lck1.lock();
    }
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
}

bool g_started = false;
bool g_stopped = false;

template <typename Mutex>
struct BAIDU_CACHELINE_ALIGNMENT PerfArgs {
    Mutex* mutex;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    PerfArgs() : mutex(NULL), counter(0), elapse_ns(0), ready(false) {}
};

template <typename Mutex>
void* add_with_mutex(void* void_arg) {
    PerfArgs<Mutex>* args = (PerfArgs<Mutex>*)void_arg;
    args->ready = true;
    butil::Timer t;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        bthread_usleep(1000);
    }
    t.start();
    while (!g_stopped) {
        BAIDU_SCOPED_LOCK(*args->mutex);
        ++args->counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return NULL;
}

int g_prof_name_counter = 0;

template <typename Mutex, typename ThreadId,
          typename ThreadCreateFn, typename ThreadJoinFn>
void PerfTest(Mutex* mutex,
              ThreadId* /*dummy*/,
              int thread_num,
              const ThreadCreateFn& create_fn,
              const ThreadJoinFn& join_fn) {
    g_started = false;
    g_stopped = false;
    ThreadId threads[thread_num];
    std::vector<PerfArgs<Mutex> > args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        args[i].mutex = mutex;
        create_fn(&threads[i], NULL, add_with_mutex<Mutex>, &args[i]);
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
    char prof_name[32];
    snprintf(prof_name, sizeof(prof_name), "mutex_perf_%d.prof", ++g_prof_name_counter);
    ProfilerStart(prof_name);
    usleep(500 * 1000);
    ProfilerStop();
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        join_fn(threads[i], NULL);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    LOG(INFO) << butil::class_name<Mutex>() << " in "
              << ((void*)create_fn == (void*)pthread_create ? "pthread" : "bthread")
              << " thread_num=" << thread_num
              << " count=" << count
              << " average_time=" << wait_time / (double)count;
}

TEST(MutexTest, performance) {
    const int thread_num = 12;
    butil::Mutex base_mutex;
    PerfTest(&base_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&base_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    bthread::Mutex bth_mutex;
    PerfTest(&bth_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}

void* loop_until_stopped(void* arg) {
    bthread::Mutex *m = (bthread::Mutex*)arg;
    while (!g_stopped) {
        BAIDU_SCOPED_LOCK(*m);
        bthread_usleep(20);
    }
    return NULL;
}

TEST(MutexTest, mix_thread_types) {
    g_stopped = false;
    const int N = 16;
    const int M = N * 2;
    bthread::Mutex m;
    pthread_t pthreads[N];
    bthread_t bthreads[M];
    // reserve enough workers for test. This is a must since we have
    // BTHREAD_ATTR_PTHREAD bthreads which may cause deadlocks (the
    // bhtread_usleep below can't be scheduled and g_stopped is never
    // true, thus loop_until_stopped spins forever)
    bthread_setconcurrency(M);
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, pthread_create(&pthreads[i], NULL, loop_until_stopped, &m));
    }
    for (int i = 0; i < M; ++i) {
        const bthread_attr_t *attr = i % 2 ? NULL : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &m));
    }
    bthread_usleep(1000L * 1000);
    g_stopped = true;
    for (int i = 0; i < M; ++i) {
        bthread_join(bthreads[i], NULL);
    }
    for (int i = 0; i < N; ++i) {
        pthread_join(pthreads[i], NULL);
    }
}

#ifdef BUTIL_CXX11_ENABLED

// XXX: should we have a test utility header?
class MilliSTimeGuard {
public:
    explicit MilliSTimeGuard(int* out_millis) noexcept: _out_millis(out_millis),
                                                        _begin_tp(std::chrono::steady_clock::now()) {
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

TEST(MutexTest, cpp_timed_mutex) {
    using std::chrono::milliseconds;
    std::atomic<bool> ready{false};
    bthread::TimedMutex tmtx;
    bthread::Thread lock_holder([&tmtx, &ready](){
        std::lock_guard<bthread::TimedMutex> lock(tmtx);
        ready = true;
        bthread::this_thread::sleep_for(milliseconds(1000));
    });
    while(!ready); // wait for the lock_holder to take hold of the lock

    int timer = 0;
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock();
        ASSERT_FALSE(locked);
        locked = tmtx.try_lock_for(milliseconds(-5));
        ASSERT_FALSE(locked);
    }
    ASSERT_GE(2, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_for(milliseconds(100));
        ASSERT_FALSE(locked);
    }
    ASSERT_LE(100, timer);
    ASSERT_GE(120, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_until(std::chrono::steady_clock::now());
        ASSERT_FALSE(locked);
    }
    ASSERT_GE(2, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_until(std::chrono::steady_clock::now() + milliseconds(100));
        ASSERT_FALSE(locked);
    }
    ASSERT_LE(100, timer);
    ASSERT_GE(120, timer);

    lock_holder.join();
}

TEST(MutexTest, cpp_timed_mutex_performance) {
    const int thread_num = 12;
    bthread::TimedMutex bth_mutex;
    PerfTest(&bth_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}

TEST(MutexTest, cpp_recursive_mutex_sanity) {
    bthread::RecursiveMutex mtx;
    int counter = 0;
    int concurrency = 0;
    auto thread_func = [&mtx, &counter, &concurrency]() {
        for (int cnt = 0; cnt < 10000; ++cnt) {
            for (int i = 0; i < 3; ++i) {
                mtx.lock();
            }
            ++concurrency;
            bthread::this_thread::yield();
            ASSERT_EQ(1, concurrency);
            ++counter;
            --concurrency;
            for (int i = 0; i < 3; ++i) {
                mtx.unlock();
            }
            bthread::this_thread::yield();
        }
    };
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back(thread_func);
    }
    for (auto& th : threads){
        th.join();
    }
    ASSERT_EQ(30000, counter);

    counter = 0;
    std::vector<bthread::Thread> bthreads;
    for (int i = 0; i < 5; ++i) {
        bthreads.emplace_back(thread_func);
    }
    for (auto& th : bthreads){
        th.join();
    }
    ASSERT_EQ(50000, counter);

    counter = 0;
    std::thread th1(thread_func);
    std::thread th2(thread_func);
    bthread::Thread th3(thread_func);
    bthread::Thread th4(thread_func);
    th1.join();
    th2.join();
    th3.join();
    th4.join();
    ASSERT_EQ(40000, counter);
}

TEST(MutexTest, cpp_recursive_mutex_performance) {
    const int thread_num = 12;
    bthread::RecursiveMutex bth_mutex;
    PerfTest(&bth_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}

TEST(MutexTest, cpp_recursive_timed_mutex_timing) {
    using std::chrono::milliseconds;
    std::atomic<bool> ready{false};
    bthread::RecursiveTimedMutex tmtx;
    bthread::Thread lock_holder([&tmtx, &ready](){
        std::lock_guard<bthread::RecursiveTimedMutex> lock(tmtx);
        ready = true;
        bthread::this_thread::sleep_for(milliseconds(1000));
    });
    while(!ready); // wait for the lock_holder to take hold of the lock

    int timer = 0;
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock();
        ASSERT_FALSE(locked);
        locked = tmtx.try_lock_for(milliseconds(-5));
        ASSERT_FALSE(locked);
    }
    ASSERT_GE(2, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_for(milliseconds(100));
        ASSERT_FALSE(locked);
    }
    ASSERT_LE(100, timer);
    ASSERT_GE(120, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_until(std::chrono::steady_clock::now());
        ASSERT_FALSE(locked);
    }
    ASSERT_GE(2, timer);
    {
        MilliSTimeGuard tg(&timer);
        bool locked = tmtx.try_lock_until(std::chrono::steady_clock::now() + milliseconds(100));
        ASSERT_FALSE(locked);
    }
    ASSERT_LE(100, timer);
    ASSERT_GE(120, timer);

    lock_holder.join();
}

TEST(MutexTest, cpp_recursive_timed_mutex_performance) {
    const int thread_num = 12;
    bthread::RecursiveTimedMutex bth_mutex;
    PerfTest(&bth_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}

#endif // BUTIL_CXX11_ENABLED

} // namespace
