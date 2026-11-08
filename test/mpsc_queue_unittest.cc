#include <gtest/gtest.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>
#include "butil/containers/mpsc_queue.h"
#include "butil/iobuf.h"

namespace {

const uint MAX_COUNT = 1000000;

void Consume(butil::MPSCQueue<uint>& q, bool allow_empty) {
    uint i = 0;
    uint empty_count = 0;
    while (true) {
        uint d;
        if (!q.Dequeue(d)) {
            ASSERT_TRUE(allow_empty);
            ASSERT_LT(empty_count++, (const uint)10000);
            ::usleep(10 * 1000);
            continue;
        }
        ASSERT_EQ(i++, d);
        if (i == MAX_COUNT) {
            break;
        }
    }
}

void* ProduceThread(void* arg) {
    auto q = (butil::MPSCQueue<uint>*)arg;
    for (uint i = 0; i < MAX_COUNT; ++i) {
        q->Enqueue(i);
    }
    return nullptr;
}

void* ConsumeThread1(void* arg) {
    auto q = (butil::MPSCQueue<uint>*)arg;
    Consume(*q, true);
    return nullptr;
}

TEST(MPSCQueueTest, spsc_single_thread) {
    butil::MPSCQueue<uint> q;
    for (uint i = 0; i < MAX_COUNT; ++i) {
        q.Enqueue(i);
    }
    Consume(q, false);
}

TEST(MPSCQueueTest, spsc_multi_thread) {
    butil::MPSCQueue<uint> q;
    pthread_t produce_tid;
    ASSERT_EQ(0, pthread_create(&produce_tid, nullptr, ProduceThread, &q));
    pthread_t consume_tid;
    ASSERT_EQ(0, pthread_create(&consume_tid, nullptr, ConsumeThread1, &q));

    pthread_join(produce_tid, nullptr);
    pthread_join(consume_tid, nullptr);

}

butil::atomic<uint> g_index(0);
void* MultiProduceThread(void* arg) {
    auto q = (butil::MPSCQueue<uint>*)arg;
    while (true) {
        uint i = g_index.fetch_add(1, butil::memory_order_relaxed);
        if (i >= MAX_COUNT) {
            break;
        }
        q->Enqueue(i);
    }
    return nullptr;
}

butil::Mutex g_mutex;
bool g_counts[MAX_COUNT];
void Consume2(butil::MPSCQueue<uint>& q) {
    uint empty_count = 0;
    uint count = 0;
    while (true) {
        uint d;
        if (!q.Dequeue(d)) {
            ASSERT_LT(empty_count++, (const uint)10000);
            ::usleep(1 * 1000);
            continue;
        }
        ASSERT_LT(d, MAX_COUNT);
        {
            BAIDU_SCOPED_LOCK(g_mutex);
            ASSERT_FALSE(g_counts[d]);
            g_counts[d] = true;
        }
        if (++count >= MAX_COUNT) {
            break;
        }
    }
}

void* ConsumeThread2(void* arg) {
    auto q = (butil::MPSCQueue<uint>*)arg;
    Consume2(*q);
    return nullptr;
}

TEST(MPSCQueueTest, mpsc_multi_thread) {
    butil::MPSCQueue<uint> q;

    int thread_num = 8;
    pthread_t threads[thread_num];
    for (int i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], nullptr, MultiProduceThread, &q));
    }

    pthread_t consume_tid;
    ASSERT_EQ(0, pthread_create(&consume_tid, nullptr, ConsumeThread2, &q));

    for (int i = 0; i < thread_num; ++i) {
        pthread_join(threads[i], nullptr);
    }
    pthread_join(consume_tid, nullptr);

}

template <typename Alloc>
void CheckConcurrentPublication() {
    const unsigned producer_count = 4;
    const unsigned items_per_producer = 10000;
    const unsigned total = producer_count * items_per_producer;
    using Item = std::pair<unsigned, unsigned>;
    butil::MPSCQueue<Item, Alloc> queue;
    butil::atomic<bool> start(false);
    std::vector<std::thread> producers;
    for (unsigned p = 0; p < producer_count; ++p) {
        producers.emplace_back([&, p] {
            while (!start.load(butil::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (unsigned i = 0; i < items_per_producer; ++i) {
                const unsigned value = p * items_per_producer + i;
                queue.Enqueue(Item(value, ~value));
            }
        });
    }
    std::vector<bool> seen(total, false);
    unsigned consumed = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(30);
    start.store(true, butil::memory_order_release);
    while (consumed < total && std::chrono::steady_clock::now() < deadline) {
        Item item;
        if (!queue.Dequeue(item)) {
            std::this_thread::yield();
            continue;
        }
        EXPECT_EQ(~item.first, item.second);
        EXPECT_LT(item.first, total);
        if (item.first < total) {
            EXPECT_FALSE(seen[item.first]);
            seen[item.first] = true;
        }
        ++consumed;
    }
    for (auto& producer : producers) {
        producer.join();
    }
    EXPECT_EQ(total, consumed);
    Item item;
    EXPECT_FALSE(queue.Dequeue(item));
}

TEST(MPSCQueueTest, concurrent_publication) {
    CheckConcurrentPublication<
        butil::DefaultAllocator<std::pair<unsigned, unsigned>>>();
}

TEST(MPSCQueueTest, concurrent_publication_with_object_pool) {
    CheckConcurrentPublication<
        butil::ObjectPoolAllocator<std::pair<unsigned, unsigned>>>();
}

struct MyObject {};

struct DestructionTracked {
    static int alive;

    DestructionTracked() { ++alive; }
    DestructionTracked(const DestructionTracked&) { ++alive; }
    DestructionTracked(DestructionTracked&&) { ++alive; }
    DestructionTracked& operator=(const DestructionTracked&) { return *this; }
    DestructionTracked& operator=(DestructionTracked&&) { return *this; }
    ~DestructionTracked() { --alive; }
};

int DestructionTracked::alive = 0;

TEST(MPSCQueueTest, destroys_pending_element) {
    DestructionTracked::alive = 0;
    {
        butil::MPSCQueue<DestructionTracked> q;
        DestructionTracked item;
        q.Enqueue(item);
        ASSERT_EQ(2, DestructionTracked::alive);
    }
    ASSERT_EQ(0, DestructionTracked::alive);
}

TEST(MPSCQueueTest, destroys_dequeued_element) {
    DestructionTracked::alive = 0;
    {
        butil::MPSCQueue<DestructionTracked> q;
        DestructionTracked item;
        DestructionTracked out;
        q.Enqueue(item);
        ASSERT_TRUE(q.Dequeue(out));
    }
    ASSERT_EQ(0, DestructionTracked::alive);
}

TEST(MPSCQueueTest, enqueue_move_only) {
  butil::MPSCQueue<std::unique_ptr<int>> q;
  q.Enqueue(std::make_unique<int>(1));
  std::unique_ptr<int> out;
  ASSERT_TRUE(q.Dequeue(out));
  ASSERT_NE(nullptr, out);
  ASSERT_EQ(1, *out);
}

TEST(MPSCQueueTest, emplace_transfers_iobuf_ownership) {
    char bytes[] = "user payload";
    int released = 0;
    butil::IOBuf input;
    ASSERT_EQ(0, input.append_user_data(bytes, sizeof(bytes), [&](void* data) {
        EXPECT_EQ(static_cast<void*>(bytes), data);
        ++released;
    }));
    butil::MPSCQueue<butil::IOBuf, butil::ObjectPoolAllocator<butil::IOBuf>> q;
    // 通过 Movable 直接构造节点。若先转换成 IOBuf 临时对象再拷贝入队，
    // 临时引用仍可能在入队返回时触发用户 deleter。
    q.Emplace(input.movable());
    EXPECT_TRUE(input.empty());
    EXPECT_EQ(0, released);

    butil::IOBuf output;
    ASSERT_TRUE(q.Dequeue(output));
    EXPECT_EQ(sizeof(bytes), output.size());
    EXPECT_EQ(static_cast<const void*>(bytes), output.fetch1());
    EXPECT_EQ(0, released);
    output.clear();
    EXPECT_EQ(1, released);
    EXPECT_FALSE(q.Dequeue(output));
}

TEST(MPSCQueueTest, mpsc_test_allocator) {
    butil::ObjectPoolAllocator<MyObject> alloc;

    auto  p = alloc.Alloc();
    butil::ObjectPoolInfo info = butil::describe_objects<butil::MPSCQueueNode<MyObject>>();
    ASSERT_EQ(1, info.item_num);

    alloc.Free(p);
    info = butil::describe_objects<butil::MPSCQueueNode<MyObject>>();
    ASSERT_EQ(1, info.item_num);

    p = alloc.Alloc();
    info = butil::describe_objects<butil::MPSCQueueNode<MyObject>>();
    ASSERT_EQ(1, info.item_num);
}

}
