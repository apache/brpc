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
#if BRPC_WITH_RDMA
#include <errno.h>
#include <functional>
#include <gflags/gflags.h>
#include <memory>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/memory/scope_guard.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/srq_receive_pool.h"
#include "brpc/rdma/rdma_async_event.h"

namespace brpc {
namespace rdma {
DECLARE_int32(rdma_srq_pool_growth_retry_ms);
extern ibv_mr* (*IbvRegMr)(ibv_pd*, void*, size_t, int);
extern int (*IbvDeregMr)(ibv_mr*);
extern void (*IbvAckAsyncEvent)(ibv_async_event*);
extern butil::atomic<int> SrqGrowthStartErrorForTest;
extern butil::atomic<int> SrqGrowthStartAttemptsForTest;
}  // namespace rdma
}  // namespace brpc

namespace {
using brpc::rdma::SrqReceivePool;

struct ClearReceiveBuffer {
    void operator()(SrqReceivePool::Buffer* buffer) const {
        buffer->clear();
    }
};
using ReceiveBufferPtr = std::unique_ptr<SrqReceivePool::Buffer, ClearReceiveBuffer>;

butil::atomic<int> registrations(0);
butil::atomic<int> deregistrations(0);
butil::atomic<int> registration_attempts{0};
butil::atomic<int> registration_failures{0};
butil::atomic<int> registration_error{0};
butil::atomic<int> relaxed_order_attempts{0};
butil::atomic<int> relaxed_order_error{0};
butil::atomic<bool> registration_paused{false};
butil::atomic<bool> registration_entered{false};
void (*previous_deallocate)(void*) = nullptr;

ibv_mr* Register(ibv_pd*, void* address, size_t bytes, int access) {
    ++registration_attempts;
    registration_entered.store(true, butil::memory_order_release);
    while (registration_paused.load(butil::memory_order_acquire)) {
        std::this_thread::yield();
    }
    int error = registration_error.load(butil::memory_order_acquire);
    if ((access & IBV_ACCESS_RELAXED_ORDERING) != 0) {
        ++relaxed_order_attempts;
        if (error == 0) {
            error = relaxed_order_error.load(butil::memory_order_acquire);
        }
    }
    if (error != 0) {
        registration_failures.fetch_add(1, butil::memory_order_release);
        errno = error;
        return nullptr;
    }
    auto* mr = new ibv_mr();
    mr->addr = address;
    mr->length = bytes;
    mr->lkey = 1233 + ++registrations;
    return mr;
}

int Deregister(ibv_mr* mr) {
    ++deregistrations;
    delete mr;
    return 0;
}

void Deallocate(void* address) {
    if (!SrqReceivePool::Recycle(address)) {
        previous_deallocate(address);
    }
}

bool WaitFor(const std::function<bool()>& ready) {
    for (int i = 0; i < 5000; ++i) {
        if (ready()) {
            return true;
        }
        usleep(1000);
    }
    return false;
}

SrqReceivePool::Buffer* AcquireReady(SrqReceivePool* pool) {
    for (int i = 0; i < 10; ++i) {
        auto* buffer = pool->Acquire();
        if (buffer != nullptr || errno != EAGAIN) {
            return buffer;
        }
        // Model an event-mode owner: growth always notifies, while block
        // returns require arm-and-recheck before sleeping.
        if (!pool->ArmRecyclingNotification()) {
            continue;
        }
        pollfd fd = {pool->recycle_fd(), POLLIN, 0};
        int ready = poll(&fd, 1, 1000);
        pool->DisarmRecyclingNotification();
        if (ready != 1) {
            return nullptr;
        }
        uint64_t events;
        while (read(fd.fd, &events, sizeof(events)) == sizeof(events)) {}
    }
    return nullptr;
}

class SrqReceivePoolTest : public testing::Test {
public:
    void SetUp() override {
        old_register = brpc::rdma::IbvRegMr;
        old_deregister = brpc::rdma::IbvDeregMr;
        brpc::rdma::IbvRegMr = Register;
        brpc::rdma::IbvDeregMr = Deregister;
        previous_deallocate = butil::iobuf::blockmem_deallocate;
        butil::iobuf::blockmem_deallocate = Deallocate;
        registrations = 0;
        deregistrations = 0;
        registration_attempts.store(0);
        registration_failures.store(0);
        registration_error.store(0);
        relaxed_order_attempts.store(0);
        relaxed_order_error.store(0);
        registration_paused.store(false);
        registration_entered.store(false);
    }

    void TearDown() override {
        registration_paused.store(false, butil::memory_order_release);
        EXPECT_TRUE(WaitFor([] {
            return SrqReceivePool::LivePools() == 0 &&
                   SrqReceivePool::RegisteredBytes() == 0;
        }));
        EXPECT_EQ(0u, SrqReceivePool::LivePools());
        EXPECT_EQ(0u, SrqReceivePool::RegisteredBytes());
        EXPECT_EQ(registrations.load(), deregistrations.load());
        butil::iobuf::blockmem_deallocate = previous_deallocate;
        brpc::rdma::IbvRegMr = old_register;
        brpc::rdma::IbvDeregMr = old_deregister;
    }
private:
    decltype(brpc::rdma::IbvRegMr) old_register;
    decltype(brpc::rdma::IbvDeregMr) old_deregister;
};

TEST_F(SrqReceivePoolTest, exhaustion_and_shared_slices_preserve_payload) {
    SrqReceivePool* pool = SrqReceivePool::Create(nullptr, 8192, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    EXPECT_EQ(16384u, SrqReceivePool::RegisteredBytes());
    auto* first = AcquireReady(pool);
    ASSERT_NE(nullptr, first);
    BUTIL_SCOPE_EXIT { first->clear(); };
    auto* second = AcquireReady(pool);
    ASSERT_NE(nullptr, second);
    BUTIL_SCOPE_EXIT { second->clear(); };
    EXPECT_EQ(nullptr, AcquireReady(pool));
    EXPECT_EQ(ENOMEM, errno);
    pool->DrainReturned();
    EXPECT_EQ(0u, pool->available());
    const void* first_address = first->data();
    memcpy(first->data(), "hello, shared receive", 21);
    auto input = first->TakeReceived(21);
    butil::IOBuf attachment;
    input.cutn(&attachment, 7);
    EXPECT_EQ(first_address, attachment.fetch1());
    EXPECT_EQ(1234u, brpc::rdma::GetRegionId(attachment.fetch1()));
    second->clear();
    pool->DrainReturned();
    EXPECT_EQ(1u, pool->available());
    input.clear();
    pool->DrainReturned();
    EXPECT_EQ(1u, pool->available());
    EXPECT_EQ("hello, ", attachment.to_string());
    attachment.clear();
    pool->DrainReturned();
    EXPECT_EQ(2u, pool->available());
    for (int i = 0; i < 100; ++i) {
        auto* buffer = AcquireReady(pool);
        ASSERT_NE(nullptr, buffer);
        EXPECT_EQ(8192u - sizeof(butil::IOBuf::Block), buffer->capacity());
        EXPECT_EQ(1234u, buffer->lkey());
        buffer->clear();
    }
}

TEST_F(SrqReceivePoolTest, prefix_transfer_preserves_address_and_boundary_sizes) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 1);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(pool) % BAIDU_CACHELINE_SIZE);
    for (size_t size : {size_t(0), size_t(1), size_t(4096),
                       size_t(8192 - sizeof(butil::IOBuf::Block))}) {
        auto* buffer = pool->Acquire();
        ASSERT_NE(nullptr, buffer);
        void* address = buffer->data();
        memset(address, 't', buffer->capacity());
        auto received = buffer->TakeReceived(size);
        EXPECT_EQ(size, received.size());
        if (size != 0) {
            EXPECT_EQ(address, received.fetch1());
            EXPECT_EQ(std::string(size, 't'), received.to_string());
            EXPECT_EQ(nullptr, pool->Acquire());
        }
        received.clear();
        // available intentionally excludes the unabsorbed return stack.
        EXPECT_EQ(0u, pool->available());
        pool->DrainReturned();
        EXPECT_EQ(1u, pool->available());
    }
}

TEST_F(SrqReceivePoolTest, stalled_registration_does_not_block_acquire_or_recycling) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 4, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT {
        registration_paused.store(false, butil::memory_order_release);
        pool->Release();
    };
    registration_entered.store(false, butil::memory_order_relaxed);
    registration_paused.store(true, butil::memory_order_release);
    auto* first = pool->Acquire();
    ASSERT_NE(nullptr, first);
    auto held = first->TakeReceived(16);
    auto* second = pool->Acquire();
    ASSERT_NE(nullptr, second);
    auto recycled = second->TakeReceived(16);
    ASSERT_TRUE(WaitFor([] { return registration_entered.load(); }));
    EXPECT_EQ(nullptr, pool->Acquire());
    EXPECT_EQ(EAGAIN, errno);
    void* address = second->data();
    recycled.clear();
    auto* reused = pool->Acquire();
    ASSERT_NE(nullptr, reused);
    EXPECT_EQ(address, reused->data());
    reused->clear();
    EXPECT_EQ(1, registrations.load());
    registration_paused.store(false, butil::memory_order_release);
    // A completed growth wakes even an owner that never armed block returns.
    pollfd fd = {pool->recycle_fd(), POLLIN, 0};
    EXPECT_EQ(1, poll(&fd, 1, 1000));
    pool->Prepare();
    EXPECT_EQ(4 * 8192u, SrqReceivePool::RegisteredBytes());
}

TEST_F(SrqReceivePoolTest, owner_release_during_growth_cleans_unadopted_region) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 4, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT {
        registration_paused.store(false, butil::memory_order_release);
        if (pool != nullptr) {
            pool->Release();
        }
    };
    registration_entered.store(false, butil::memory_order_relaxed);
    registration_paused.store(true, butil::memory_order_release);
    auto* first = pool->Acquire();
    ASSERT_NE(nullptr, first);
    memcpy(first->data(), "survives growth", 15);
    auto held = first->TakeReceived(15);
    auto* second = pool->Acquire();
    ASSERT_NE(nullptr, second);
    second->clear();
    ASSERT_TRUE(WaitFor([] { return registration_entered.load(); }));
    pool->Release();
    pool = nullptr;
    EXPECT_EQ(1u, SrqReceivePool::LivePools());
    EXPECT_EQ("survives growth", held.to_string());
    held.clear();
    EXPECT_EQ(0, deregistrations.load());
    registration_paused.store(false, butil::memory_order_release);
    ASSERT_TRUE(WaitFor([] { return SrqReceivePool::LivePools() == 0; }));
    ASSERT_TRUE(WaitFor([] { return SrqReceivePool::RegisteredBytes() == 0; }));
    EXPECT_EQ(2, registrations.load());
    EXPECT_EQ(2, deregistrations.load());
}

TEST_F(SrqReceivePoolTest, unsupported_relaxed_order_is_remembered_per_pool) {
    relaxed_order_error.store(EOPNOTSUPP);
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 6, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    std::vector<butil::IOBuf> held;
    for (int i = 0; i < 6; ++i) {
        auto* buffer = AcquireReady(pool);
        ASSERT_NE(nullptr, buffer);
        held.push_back(buffer->TakeReceived(1));
    }
    EXPECT_EQ(3, registrations.load());
    EXPECT_EQ(1, relaxed_order_attempts.load());
    auto* another = SrqReceivePool::Create(nullptr, 8192, 1);
    ASSERT_NE(nullptr, another);
    another->Release();
    EXPECT_EQ(2, relaxed_order_attempts.load());
}

TEST_F(SrqReceivePoolTest, resource_failure_does_not_disable_relaxed_order) {
    relaxed_order_error.store(ENOMEM);
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 6, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    std::vector<butil::IOBuf> held;
    for (int i = 0; i < 6; ++i) {
        auto* buffer = AcquireReady(pool);
        ASSERT_NE(nullptr, buffer);
        held.push_back(buffer->TakeReceived(1));
    }
    EXPECT_EQ(3, registrations.load());
    EXPECT_EQ(3, relaxed_order_attempts.load());
}

TEST_F(SrqReceivePoolTest, grows_without_moving_leased_blocks_and_stops_at_limit) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 7, 3);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT {
        if (pool != nullptr) {
            pool->Release();
        }
    };
    EXPECT_EQ(3 * 8192u, SrqReceivePool::RegisteredBytes());
    std::vector<butil::IOBuf> received(7);
    std::vector<const void*> addresses;
    for (unsigned i = 0; i < 7; ++i) {
        auto* buffer = AcquireReady(pool);
        ASSERT_NE(nullptr, buffer);
        addresses.push_back(buffer->data());
        EXPECT_EQ(1234u + i / 3, buffer->lkey());
        *static_cast<unsigned*>(buffer->data()) = i;
        received[i] = buffer->TakeReceived(sizeof(i));
    }
    EXPECT_EQ(3, registrations.load());  // Region sizes: 3, 3, 1.
    EXPECT_EQ(7 * 8192u, SrqReceivePool::RegisteredBytes());
    EXPECT_EQ(nullptr, AcquireReady(pool));
    EXPECT_EQ(ENOMEM, errno);
    pool->Release();
    pool = nullptr;
    // After owner release, application references keep every region address,
    // lkey and payload valid.
    for (unsigned i = 0; i < received.size(); ++i) {
        unsigned value = 99;
        received[i].copy_to(&value, sizeof(value));
        EXPECT_EQ(i, value);
        EXPECT_EQ(addresses[i], received[i].fetch1());
        EXPECT_EQ(1234u + i / 3, SrqReceivePool::RegionId(received[i].fetch1()));
    }
    std::thread releaser([&] { received.clear(); });
    releaser.join();
    EXPECT_TRUE(WaitFor([] { return deregistrations.load() == 3; }));
}

TEST_F(SrqReceivePoolTest, growth_start_failure_reuses_blocks_and_backs_off) {
    GFLAGS_NAMESPACE::FlagSaver flags;
    brpc::rdma::FLAGS_rdma_srq_pool_growth_retry_ms = 60000;
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 4, 2);
    ASSERT_NE(nullptr, pool);
    int attempts = brpc::rdma::SrqGrowthStartAttemptsForTest.load();
    brpc::rdma::SrqGrowthStartErrorForTest.store(ENOMEM, butil::memory_order_relaxed);
    BUTIL_SCOPE_EXIT {
        brpc::rdma::SrqGrowthStartErrorForTest.store(0, butil::memory_order_relaxed);
        pool->Release();
    };
    auto* first = pool->Acquire();
    ASSERT_NE(nullptr, first);
    auto held = first->TakeReceived(16);
    EXPECT_EQ(1u, pool->available());
    // Both refill preparation and allocation must keep existing blocks usable.
    pool->Prepare();
    EXPECT_EQ(attempts + 1, brpc::rdma::SrqGrowthStartAttemptsForTest.load());
    auto* second = pool->Acquire();
    ASSERT_NE(nullptr, second);
    BUTIL_SCOPE_EXIT { second->clear(); };
    EXPECT_EQ(0u, pool->available());
    // Model a worker retaining the second lease for direct reposting.
    pool->Prepare(1);
    for (int i = 0; i < 100; ++i) {
        pool->Prepare();
        EXPECT_EQ(nullptr, pool->Acquire());
        EXPECT_EQ(EAGAIN, errno);
    }
    EXPECT_EQ(attempts + 1, brpc::rdma::SrqGrowthStartAttemptsForTest.load());
    EXPECT_EQ(1, registrations.load());

    // Backoff must preserve the return notification and reuse path even when
    // the pool has run out of ready blocks.
    EXPECT_TRUE(pool->ArmRecyclingNotification());
    held.clear();
    uint64_t events = 0;
    EXPECT_EQ(static_cast<ssize_t>(sizeof(events)),
              read(pool->recycle_fd(), &events, sizeof(events)));
    EXPECT_EQ(1u, events);
    auto* reused = pool->Acquire();
    ASSERT_NE(nullptr, reused);
    BUTIL_SCOPE_EXIT { reused->clear(); };
    EXPECT_EQ(first, reused);
    EXPECT_EQ(attempts + 1, brpc::rdma::SrqGrowthStartAttemptsForTest.load());

    // Expire the owner-only deadline explicitly rather than depending on how
    // quickly this test runs or sleeping through the configured backoff.
    brpc::rdma::SrqGrowthStartErrorForTest.store(0, butil::memory_order_relaxed);
    pool->_growth_retry_at_us = 0;
    pool->Prepare();
    EXPECT_EQ(attempts + 2, brpc::rdma::SrqGrowthStartAttemptsForTest.load());
    auto* grown = AcquireReady(pool);
    ASSERT_NE(nullptr, grown);
    EXPECT_NE(first->lkey(), grown->lkey());
    grown->clear();
    EXPECT_EQ(4 * 8192u, SrqReceivePool::RegisteredBytes());
}

TEST_F(SrqReceivePoolTest, failed_growth_preserves_old_blocks_and_can_retry) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 4, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT {
        if (pool != nullptr) {
            pool->Release();
        }
    };
    auto* first = AcquireReady(pool);
    ASSERT_NE(nullptr, first);
    memcpy(first->data(), "old", 3);
    auto data = first->TakeReceived(3);
    // The last initial block triggers prefetch. Publish the fault before that
    // task can start, without replacing a function pointer it may be using.
    registration_error.store(ENOMEM, butil::memory_order_release);
    ReceiveBufferPtr second(AcquireReady(pool));
    ASSERT_NE(nullptr, second);
    EXPECT_EQ(nullptr, pool->Acquire());
    EXPECT_EQ(EAGAIN, errno);
    // Wait for both RO and fallback registration to fail. Their task may finish
    // backoff at any time, so do not assume a fixed number of later attempts.
    ASSERT_TRUE(WaitFor([] { return registration_failures.load() >= 2; }));
    EXPECT_EQ(2 * 8192u, SrqReceivePool::RegisteredBytes());
    EXPECT_EQ("old", data.to_string());
    // Failed growth must neither prevent returning/reusing existing blocks nor
    // destroy existing MRs.
    second.reset();
    ReceiveBufferPtr reused(AcquireReady(pool));
    ASSERT_NE(nullptr, reused);
    EXPECT_EQ(1234u, reused->lkey());
    registration_error.store(0, butil::memory_order_release);
    ReceiveBufferPtr grown(AcquireReady(pool));
    ASSERT_NE(nullptr, grown);
    EXPECT_EQ(1235u, grown->lkey());
    EXPECT_EQ(4 * 8192u, SrqReceivePool::RegisteredBytes());
    reused.reset();
    grown.reset();
    pool->Release();
    pool = nullptr;
    EXPECT_EQ("old", data.to_string());
    data.clear();
}

butil::atomic<bool> deregister_entered{false};
butil::atomic<bool> deregister_continue{false};

TEST_F(SrqReceivePoolTest, registered_memory_remains_until_deregistration_finishes) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 8, 2);
    ASSERT_NE(nullptr, pool);
    deregister_entered.store(false);
    deregister_continue.store(false);
    brpc::rdma::IbvDeregMr = [](ibv_mr* mr) {
        deregister_entered.store(true, butil::memory_order_release);
        while (!deregister_continue.load(butil::memory_order_acquire)) {
            std::this_thread::yield();
        }
        return Deregister(mr);
    };
    std::thread destroyer([&] { pool->Release(); });
    for (int i = 0; i < 1000 && !deregister_entered.load(); ++i) {
        usleep(1000);
    }
    EXPECT_TRUE(deregister_entered.load());
    // Registered memory must remain valid while MR deregistration is
    // incomplete.
    EXPECT_EQ(2 * 8192u, SrqReceivePool::RegisteredBytes());
    deregister_continue.store(true, butil::memory_order_release);
    destroyer.join();
    EXPECT_EQ(0u, SrqReceivePool::RegisteredBytes());
}

TEST_F(SrqReceivePoolTest, concurrent_region_updates_and_last_block_recycling) {
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([] {
            for (int round = 0; round < 32; ++round) {
                auto* pool = SrqReceivePool::Create(nullptr, 8192, 4, 1);
                ASSERT_NE(nullptr, pool);
                BUTIL_SCOPE_EXIT {
                    if (pool != nullptr) {
                        pool->Release();
                    }
                };
                butil::IOBuf received[4];
                uint32_t keys[4];
                for (int j = 0; j < 4; ++j) {
                    auto* buffer = AcquireReady(pool);
                    ASSERT_NE(nullptr, buffer);
                    keys[j] = buffer->lkey();
                    memset(buffer->data(), j, 64);
                    received[j] = buffer->TakeReceived(64);
                }
                // The owner releases first, so the last returned block
                // synchronously removes all pool index entries. Other threads
                // add/remove independent pools concurrently. Readers must
                // always find regions still retained by their block references.
                pool->Release();
                pool = nullptr;
                for (int j = 0; j < 4; ++j) {
                    EXPECT_EQ(keys[j], SrqReceivePool::RegionId(received[j].fetch1()));
                    EXPECT_EQ(std::string(64, j), received[j].to_string());
                    received[j].clear();
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(0u, SrqReceivePool::LivePools());
}

TEST_F(SrqReceivePoolTest, simultaneous_last_references_are_recycled_without_loss) {
    constexpr unsigned count = 128;
    constexpr unsigned threads = 8;
    SrqReceivePool* pool = SrqReceivePool::Create(nullptr, 8192, count);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    for (int round = 0; round < 30; ++round) {
        std::vector<butil::IOBuf> attachments(count);
        for (unsigned i = 0; i < count; ++i) {
            auto* buffer = AcquireReady(pool);
            ASSERT_NE(nullptr, buffer);
            *static_cast<uint32_t*>(buffer->data()) = i;
            attachments[i] = buffer->TakeReceived(sizeof(uint32_t));
        }
        pool->DrainReturned();
        ASSERT_EQ(0u, pool->available());
        std::vector<std::thread> consumers;
        for (unsigned thread = 0; thread < threads; ++thread) {
            consumers.emplace_back([thread, &attachments] {
                for (unsigned i = thread; i < count; i += threads) {
                    uint32_t value = 0;
                    attachments[i].copy_to(&value, sizeof(value));
                    EXPECT_EQ(i, value);
                    attachments[i].clear();
                }
            });
        }
        for (auto& consumer : consumers) {
            consumer.join();
        }
        pool->DrainReturned();
        EXPECT_EQ(count, pool->available());
    }
}

TEST_F(SrqReceivePoolTest, return_notification_is_armed_without_missing_ready_buffers) {
    SrqReceivePool* pool = SrqReceivePool::Create(nullptr, 8192, 1);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    auto* buffer = AcquireReady(pool);
    ASSERT_NE(nullptr, buffer);
    EXPECT_TRUE(pool->ArmRecyclingNotification());
    std::thread releaser([buffer] { buffer->clear(); });
    pollfd pfd = {pool->recycle_fd(), POLLIN, 0};
    const int polled = poll(&pfd, 1, 1000);
    releaser.join();
    ASSERT_EQ(1, polled);
    uint64_t events = 0;
    ASSERT_EQ(static_cast<ssize_t>(sizeof(events)),
              read(pool->recycle_fd(), &events, sizeof(events)));
    EXPECT_EQ(1u, events);
    EXPECT_FALSE(pool->ArmRecyclingNotification());
    buffer = AcquireReady(pool);
    ASSERT_NE(nullptr, buffer);
    pool->DisarmRecyclingNotification();
    buffer->clear();
    EXPECT_EQ(0, poll(&pfd, 1, 0));
    pool->DrainReturned();
    EXPECT_EQ(1u, pool->available());
    // Local free buffers must not prevent arming for the next return.
    EXPECT_TRUE(pool->ArmRecyclingNotification());
}

TEST_F(SrqReceivePoolTest, concurrent_returns_racing_with_arm_never_lose_wakeup) {
    auto* pool = SrqReceivePool::Create(nullptr, 8192, 2);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    SrqReceivePool::Buffer* buffers[2] = {};
    butil::atomic<int> start{0};
    butil::atomic<int> finished{0};
    constexpr int rounds = 2000;
    std::vector<std::thread> returners;
    for (int i = 0; i < 2; ++i) {
        returners.emplace_back([&, i] {
            for (int round = 1; round <= rounds; ++round) {
                while (start.load(butil::memory_order_acquire) < round) {
                    std::this_thread::yield();
                }
                buffers[i]->clear();
                finished.fetch_add(1, butil::memory_order_release);
            }
        });
    }
    for (int round = 1; round <= rounds; ++round) {
        buffers[0] = pool->Acquire();
        buffers[1] = pool->Acquire();
        // No fatal assertions while the threads depend on this producer.
        CHECK(buffers[0] != nullptr && buffers[1] != nullptr);
        pool->DisarmRecyclingNotification();
        uint64_t events;
        while (read(pool->recycle_fd(), &events, sizeof(events)) == sizeof(events)) {}
        start.store(round, butil::memory_order_release);
        bool armed = pool->ArmRecyclingNotification();
        while (finished.load(butil::memory_order_acquire) != round * 2) {
            std::this_thread::yield();
        }
        if (armed) {
            pollfd fd = {pool->recycle_fd(), POLLIN, 0};
            EXPECT_EQ(1, poll(&fd, 1, 0)) << "round=" << round;
        }
        pool->DrainReturned();
        EXPECT_EQ(2u, pool->available());
    }
    for (auto& returner : returners) {
        returner.join();
    }
}

TEST_F(SrqReceivePoolTest, descriptor_can_be_reacquired_while_release_returns) {
    SrqReceivePool* pool = SrqReceivePool::Create(nullptr, 8192, 1);
    ASSERT_NE(nullptr, pool);
    BUTIL_SCOPE_EXIT { pool->Release(); };
    butil::atomic<SrqReceivePool::Buffer*> handoff(nullptr);
    constexpr unsigned rounds = 10000;
    std::thread consumer([&handoff] {
        for (unsigned i = 0; i < rounds; ++i) {
            SrqReceivePool::Buffer* buffer;
            while ((buffer = handoff.exchange(nullptr, butil::memory_order_acquire)) == nullptr) {
                std::this_thread::yield();
            }
            EXPECT_EQ(i, *static_cast<uint32_t*>(buffer->data()));
            buffer->clear();
        }
    });
    for (unsigned i = 0; i < rounds; ++i) {
        SrqReceivePool::Buffer* buffer;
        while ((buffer = AcquireReady(pool)) == nullptr) {
            std::this_thread::yield();
        }
        *static_cast<uint32_t*>(buffer->data()) = i;
        handoff.store(buffer, butil::memory_order_release);
    }
    consumer.join();
    pool->DrainReturned();
    EXPECT_EQ(1u, pool->available());
}

TEST_F(SrqReceivePoolTest, creation_failure_leaves_no_registered_memory) {
    EXPECT_EQ(nullptr, SrqReceivePool::Create(nullptr, 8192, 0));
    EXPECT_EQ(nullptr, SrqReceivePool::Create(nullptr, sizeof(butil::IOBuf::Block), 2));
    EXPECT_EQ(nullptr, SrqReceivePool::Create(nullptr, 8193, 2));
    EXPECT_EQ(nullptr, SrqReceivePool::Create(nullptr, SIZE_MAX, 2));
    EXPECT_EQ(0, registrations.load());

    registration_error.store(ENOMEM, butil::memory_order_release);
    EXPECT_EQ(nullptr, SrqReceivePool::Create(nullptr, 8192, 2));
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(0, registrations.load());
}

TEST_F(SrqReceivePoolTest, failed_deregistration_keeps_pages_charged_until_retry) {
    SrqReceivePool* pool = SrqReceivePool::Create(nullptr, 8192, 2);
    ASSERT_NE(nullptr, pool);
    brpc::rdma::IbvDeregMr = [](ibv_mr*) -> int {
        errno = EBUSY;
        return EBUSY;
    };
    pool->Release();
    EXPECT_EQ(0u, SrqReceivePool::LivePools());
    EXPECT_EQ(16384u, SrqReceivePool::RegisteredBytes());
    EXPECT_EQ(1u, SrqReceivePool::RetryDeregistrations());
    EXPECT_EQ(16384u, SrqReceivePool::RegisteredBytes());
    brpc::rdma::IbvDeregMr = Deregister;
    EXPECT_EQ(0u, SrqReceivePool::RetryDeregistrations());
    EXPECT_EQ(0u, SrqReceivePool::RegisteredBytes());
}

std::string async_order;
class AsyncContext : public brpc::rdma::RdmaAsyncEventContext {
public:
    int generation = 7;
    int captures = 0;
    std::function<void()> PrepareAsyncEvent(ibv_event_type) override {
        ++captures;
        async_order += "capture ";
        int saved_generation = generation;
        return [saved_generation] {
            EXPECT_EQ(7, saved_generation);
            async_order += "callback";
        };
    }
};

TEST(RdmaAsyncEventTest, captures_before_ack_and_runs_after_ack) {
    struct Restore {
        decltype(brpc::rdma::IbvAckAsyncEvent) original = brpc::rdma::IbvAckAsyncEvent;
        ~Restore() { brpc::rdma::IbvAckAsyncEvent = original; }
    } restore;
    brpc::rdma::IbvAckAsyncEvent = [](ibv_async_event*) { async_order += "ack "; };
    AsyncContext context;
    ibv_qp qp = {};
    qp.qp_context = &context;
    ibv_cq cq = {};
    cq.cq_context = &context;
    ibv_srq srq = {};
    srq.srq_context = &context;
    brpc::rdma::RegisterRdmaAsyncEventContext(&qp, &context);
    brpc::rdma::RegisterRdmaAsyncEventContext(&cq, &context);
    brpc::rdma::RegisterRdmaAsyncEventContext(&srq, &context);
    auto unregister = butil::MakeScopeGuard([&] {
        brpc::rdma::UnregisterRdmaAsyncEventContext(&qp);
        brpc::rdma::UnregisterRdmaAsyncEventContext(&cq);
        brpc::rdma::UnregisterRdmaAsyncEventContext(&srq);
    });
    for (ibv_event_type type : {IBV_EVENT_QP_FATAL, IBV_EVENT_QP_LAST_WQE_REACHED,
                               IBV_EVENT_CQ_ERR, IBV_EVENT_SRQ_ERR,
                               IBV_EVENT_SRQ_LIMIT_REACHED}) {
        async_order.clear();
        ibv_async_event event = {};
        event.event_type = type;
        if (type == IBV_EVENT_CQ_ERR) {
            event.element.cq = &cq;
        }
        else if (type == IBV_EVENT_SRQ_ERR || type == IBV_EVENT_SRQ_LIMIT_REACHED) {
            event.element.srq = &srq;
        } else {
            event.element.qp = &qp;
        }
        EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
        EXPECT_EQ("capture ack callback", async_order);
    }
    async_order.clear();
    ibv_async_event event = {};
    event.event_type = IBV_EVENT_PORT_ACTIVE;
    EXPECT_FALSE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
    EXPECT_TRUE(async_order.empty());
}

TEST(RdmaAsyncEventTest, foreign_contexts_are_not_dereferenced) {
    auto old_ack = brpc::rdma::IbvAckAsyncEvent;
    auto restore = butil::MakeScopeGuard([&] { brpc::rdma::IbvAckAsyncEvent = old_ack; });
    brpc::rdma::IbvAckAsyncEvent = [](ibv_async_event*) { async_order += "ack "; };
    // External object contexts are application-defined and may even be
    // non-dereferenceable sentinel values.
    ibv_qp qp = {};
    qp.qp_context = reinterpret_cast<void*>(1);
    ibv_cq cq = {};
    cq.cq_context = reinterpret_cast<void*>(2);
    ibv_srq srq = {};
    srq.srq_context = reinterpret_cast<void*>(3);
    for (ibv_event_type type : {IBV_EVENT_QP_FATAL, IBV_EVENT_QP_REQ_ERR,
             IBV_EVENT_QP_ACCESS_ERR, IBV_EVENT_QP_LAST_WQE_REACHED,
             IBV_EVENT_COMM_EST, IBV_EVENT_SQ_DRAINED, IBV_EVENT_CQ_ERR,
             IBV_EVENT_SRQ_ERR, IBV_EVENT_SRQ_LIMIT_REACHED}) {
        ibv_async_event event = {};
        event.event_type = type;
        if (type == IBV_EVENT_CQ_ERR) {
            event.element.cq = &cq;
        } else if (type == IBV_EVENT_SRQ_ERR || type == IBV_EVENT_SRQ_LIMIT_REACHED) {
            event.element.srq = &srq;
        } else {
            event.element.qp = &qp;
        }
        async_order.clear();
        EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
        EXPECT_EQ("ack ", async_order);
    }
}

TEST(RdmaAsyncEventTest, unregistered_object_does_not_use_old_context) {
    auto old_ack = brpc::rdma::IbvAckAsyncEvent;
    auto restore = butil::MakeScopeGuard([&] { brpc::rdma::IbvAckAsyncEvent = old_ack; });
    brpc::rdma::IbvAckAsyncEvent = [](ibv_async_event*) { async_order += "ack "; };
    AsyncContext first, second;
    ibv_qp qp = {};
    qp.qp_context = &first;
    ibv_async_event event = {};
    event.event_type = IBV_EVENT_QP_FATAL;
    event.element.qp = &qp;
    auto unregister = butil::MakeScopeGuard([&] {
        brpc::rdma::UnregisterRdmaAsyncEventContext(&qp);
    });
    brpc::rdma::RegisterRdmaAsyncEventContext(&qp, &first);
    EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
    EXPECT_EQ(1, first.captures);
    brpc::rdma::UnregisterRdmaAsyncEventContext(&qp);
    EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
    EXPECT_EQ(1, first.captures);
    // Re-registering an address must use its new owner, ignoring stale object
    // context.
    brpc::rdma::RegisterRdmaAsyncEventContext(&qp, &second);
    EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
    EXPECT_EQ(1, first.captures);
    EXPECT_EQ(1, second.captures);
}

TEST(RdmaAsyncEventTest, context_can_be_deleted_at_ack_after_capture) {
    auto old_ack = brpc::rdma::IbvAckAsyncEvent;
    auto restore = butil::MakeScopeGuard([&] { brpc::rdma::IbvAckAsyncEvent = old_ack; });
    brpc::rdma::IbvAckAsyncEvent = [](ibv_async_event* event) {
        auto* qp = event->element.qp;
        // Unregistration must not wait on a registry lock held by the
        // dispatcher. Prepare has already finished.
        brpc::rdma::UnregisterRdmaAsyncEventContext(qp);
        delete static_cast<AsyncContext*>(qp->qp_context);
        qp->qp_context = nullptr;
        async_order += "ack ";
    };
    ibv_qp qp = {};
    auto* context = new AsyncContext;
    qp.qp_context = context;
    brpc::rdma::RegisterRdmaAsyncEventContext(&qp, context);
    ibv_async_event event = {};
    event.event_type = IBV_EVENT_QP_FATAL;
    event.element.qp = &qp;
    async_order.clear();
    EXPECT_TRUE(brpc::rdma::DispatchRdmaAsyncEvent(&event));
    EXPECT_EQ("capture ack callback", async_order);
    EXPECT_EQ(nullptr, qp.qp_context);
}
}  // namespace
#endif  // BRPC_WITH_RDMA
