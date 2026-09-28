// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to You under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <gflags/gflags.h>
#include <string>
#include <thread>
#include "butil/macros.h"
#include "butil/sys_byteorder.h"
#include "brpc/socket.h"

#if BRPC_WITH_UBRING
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/ub_endpoint.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/shm/shm_mgr.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ubr_msg.h"
#include "brpc/ubshm/ubr_msg_v2.h"

namespace brpc {
namespace ubring {
DECLARE_int32(ub_disconnect_timeout_s);
DECLARE_int32(ub_connect_timeout_s);
DECLARE_int32(ub_hb_timer_interval_s);
DECLARE_int32(ub_event_queue_timer_interval_us);
DECLARE_int32(ub_flying_io_timeout_s);

extern bool g_skip_ub_init;
}  // namespace ubring
}  // namespace brpc

namespace {
TEST(IpcV2LayoutTest, fixed_layout) {
    using brpc::ubring::IpcV2Slot;

    EXPECT_EQ(4096u, sizeof(IpcV2Slot));
    EXPECT_EQ(64u, alignof(IpcV2Slot));
    EXPECT_EQ(16u, offsetof(IpcV2Slot, payload));
    EXPECT_EQ(4080u, sizeof(((IpcV2Slot*)nullptr)->payload));
    EXPECT_EQ(0, brpc::ubring::UBR_DATA_FORMAT_NONE);
    EXPECT_EQ(1, brpc::ubring::UBR_DATA_FORMAT_LEGACY_64);
    EXPECT_EQ(2, brpc::ubring::UBR_DATA_FORMAT_IPC_V2);
}

TEST(IpcV2RingTest, fixed_writev_readv_boundaries_and_wrap) {
    using namespace brpc::ubring;
    IpcV2Slot slots[4] = {};
    uint32_t tail = 0;
    uint32_t write_pos = 0;
    uint32_t read_pos = 0;
    uint32_t read_offset = 0;
    IpcV2TxView tx(slots, &tail, 4, &write_pos);
    IpcV2RxView rx(slots, &tail, 4, &read_pos, &read_offset);
    EXPECT_EQ(IPC_V2_SLOT_UNINITIALIZED,
              __atomic_load_n(&slots[0].header.state, __ATOMIC_ACQUIRE));
    ASSERT_TRUE(IpcV2RxView::InitializeShared(slots, &tail, 4));
    EXPECT_EQ(3u, __atomic_load_n(&tail, __ATOMIC_ACQUIRE));

    std::string source(IpcV2Slot::PAYLOAD_SIZE + 19, '\0');
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<char>((i * 29 + 7) & 0xff);
    }
    struct iovec write_iov[4] = {
        {nullptr, 0},
        {&source[0], 7},
        {&source[7], IpcV2Slot::PAYLOAD_SIZE - 3},
        {&source[IpcV2Slot::PAYLOAD_SIZE + 4], 15},
    };
    size_t written = 99;
    ASSERT_EQ(IPC_V2_RING_OK, tx.TryWritev(write_iov, 4, written));
    EXPECT_EQ(source.size(), written);
    EXPECT_EQ(2u, write_pos);
    EXPECT_EQ(IpcV2Slot::PAYLOAD_SIZE, slots[0].header.payload_len);
    EXPECT_EQ(0u, slots[0].header.flags);
    EXPECT_EQ(19u, slots[1].header.payload_len);
    EXPECT_EQ(static_cast<uint32_t>(IPC_V2_SLOT_EOF), slots[1].header.flags);

    std::string output(source.size(), '?');
    struct iovec first_read = {&output[0], 31};
    size_t read = 99;
    bool batch_end = true;
    ASSERT_EQ(IPC_V2_RING_OK, rx.TryReadv(&first_read, 1, read, batch_end));
    EXPECT_EQ(31u, read);
    EXPECT_FALSE(batch_end);
    EXPECT_EQ(0u, read_pos);
    EXPECT_EQ(31u, read_offset);
    EXPECT_EQ(3u, __atomic_load_n(&tail, __ATOMIC_ACQUIRE));
    struct iovec remaining_iov[4] = {
        {nullptr, 0},
        {&output[31], 5},
        {&output[36], IpcV2Slot::PAYLOAD_SIZE - 20},
        {&output[IpcV2Slot::PAYLOAD_SIZE + 16], 3},
    };
    ASSERT_EQ(IPC_V2_RING_OK, rx.TryReadv(remaining_iov, 4, read, batch_end));
    EXPECT_EQ(source.size() - 31, read);
    EXPECT_TRUE(batch_end);
    EXPECT_EQ(source, output);
    EXPECT_EQ(2u, read_pos);
    EXPECT_EQ(0u, read_offset);
    EXPECT_EQ(1u, __atomic_load_n(&tail, __ATOMIC_ACQUIRE));

    std::string oversized(IpcV2Slot::PAYLOAD_SIZE * 4, 'x');
    struct iovec oversized_iov = {&oversized[0], oversized.size()};
    EXPECT_EQ(IPC_V2_RING_TOO_LARGE,
              tx.TryWritev(&oversized_iov, 1, written));
    EXPECT_EQ(0u, written);

    char values[4] = {'a', 'b', 'c', 'd'};
    for (int i = 0; i < 3; ++i) {
        struct iovec item = {&values[i], 1};
        ASSERT_EQ(IPC_V2_RING_OK, tx.TryWritev(&item, 1, written));
    }
    struct iovec fourth = {&values[3], 1};
    EXPECT_EQ(IPC_V2_RING_RETRY, tx.TryWritev(&fourth, 1, written));
    EXPECT_EQ(0u, written);
    char result[4] = {};
    struct iovec one = {&result[0], 1};
    ASSERT_EQ(IPC_V2_RING_OK, rx.TryReadv(&one, 1, read, batch_end));
    ASSERT_TRUE(batch_end);
    ASSERT_EQ(IPC_V2_RING_OK, tx.TryWritev(&fourth, 1, written));
    for (int i = 1; i < 4; ++i) {
        struct iovec destination = {&result[i], 1};
        ASSERT_EQ(IPC_V2_RING_OK,
                  rx.TryReadv(&destination, 1, read, batch_end));
        ASSERT_TRUE(batch_end);
    }
    EXPECT_EQ(0, memcmp(values, result, sizeof(values)));
}

TEST(IpcV2RingTest, rejects_invalid_iovec_without_publication) {
    using namespace brpc::ubring;
    IpcV2Slot slots[2] = {};
    uint32_t tail = 0;
    uint32_t write_pos = 0;
    uint32_t read_pos = 0;
    uint32_t read_offset = 0;
    IpcV2TxView tx(slots, &tail, 2, &write_pos);
    IpcV2RxView rx(slots, &tail, 2, &read_pos, &read_offset);
    size_t bytes = 99;
    bool batch_end = true;
    char value = 'x';
    struct iovec one_byte = {&value, 1};
    EXPECT_EQ(IPC_V2_RING_INVALID, tx.TryWritev(&one_byte, 1, bytes));
    EXPECT_EQ(IPC_V2_RING_INVALID,
              rx.TryReadv(&one_byte, 1, bytes, batch_end));
    ASSERT_TRUE(IpcV2RxView::InitializeShared(slots, &tail, 2));
    EXPECT_EQ(IPC_V2_RING_INVALID, tx.TryWritev(nullptr, 1, bytes));
    EXPECT_EQ(IPC_V2_RING_INVALID, tx.TryWritev(nullptr, -1, bytes));
    EXPECT_EQ(IPC_V2_RING_INVALID,
              rx.TryReadv(nullptr, 1, bytes, batch_end));
    struct iovec null_data = {nullptr, 1};
    EXPECT_EQ(IPC_V2_RING_INVALID, tx.TryWritev(&null_data, 1, bytes));
    EXPECT_EQ(IPC_V2_RING_OK, tx.TryWritev(nullptr, 0, bytes));
    EXPECT_EQ(0u, bytes);
    EXPECT_EQ(IPC_V2_RING_OK, rx.TryReadv(nullptr, 0, bytes, batch_end));
    EXPECT_EQ(0u, bytes);
    EXPECT_FALSE(batch_end);
    EXPECT_EQ(IPC_V2_SLOT_EMPTY,
              __atomic_load_n(&slots[0].header.state, __ATOMIC_ACQUIRE));
}

TEST(IpcV2RingTest, concurrent_writev_readv) {
    using namespace brpc::ubring;
    IpcV2Slot slots[8] = {};
    uint32_t tail = 0;
    uint32_t write_pos = 0;
    uint32_t read_pos = 0;
    uint32_t read_offset = 0;
    ASSERT_TRUE(IpcV2RxView::InitializeShared(slots, &tail, 8));
    IpcV2TxView tx(slots, &tail, 8, &write_pos);
    IpcV2RxView rx(slots, &tail, 8, &read_pos, &read_offset);
    const uint32_t rounds = 200;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    std::atomic<bool> stop(false);
    std::string producer_error;
    std::string consumer_error;
    const auto message_size = [](uint32_t sequence) {
        return 1 + (sequence * 53) % (IpcV2Slot::PAYLOAD_SIZE * 2);
    };
    const auto fill = [](uint32_t sequence, char* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            data[i] = static_cast<char>((sequence * 11 + i * 37) & 0xff);
        }
    };
    std::thread producer([&] {
        std::string input(IpcV2Slot::PAYLOAD_SIZE * 2, '\0');
        for (uint32_t sequence = 0; sequence < rounds; ++sequence) {
            const size_t length = message_size(sequence);
            fill(sequence, &input[0], length);
            const size_t first = length < 3 ? length : 3;
            struct iovec parts[3] = {
                {&input[0], first},
                {nullptr, 0},
                {&input[first], length - first},
            };
            for (;;) {
                size_t written = 0;
                const IpcV2RingResult result = tx.TryWritev(parts, 3, written);
                if (result == IPC_V2_RING_OK && written == length) {
                    break;
                }
                if (result != IPC_V2_RING_RETRY ||
                    stop.load(std::memory_order_relaxed) ||
                    std::chrono::steady_clock::now() >= deadline) {
                    producer_error = "valid writev failed or timed out";
                    stop.store(true, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    std::string output(IpcV2Slot::PAYLOAD_SIZE * 2, '\0');
    std::string expected(IpcV2Slot::PAYLOAD_SIZE * 2, '\0');
    for (uint32_t sequence = 0; sequence < rounds; ++sequence) {
        const size_t length = message_size(sequence);
        fill(sequence, &expected[0], length);
        size_t received = 0;
        bool ended = false;
        while (!ended && !stop.load(std::memory_order_relaxed)) {
            const size_t chunk = std::min(size_t(113), length - received);
            struct iovec destination = {&output[received], chunk};
            size_t read = 0;
            const IpcV2RingResult result =
                rx.TryReadv(&destination, 1, read, ended);
            if (result == IPC_V2_RING_RETRY) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    consumer_error = "consumer timed out";
                    break;
                }
                std::this_thread::yield();
                continue;
            }
            if (result != IPC_V2_RING_OK || read == 0 || read > chunk) {
                consumer_error = "valid readv failed";
                break;
            }
            received += read;
        }
        if (!consumer_error.empty() || stop.load(std::memory_order_relaxed)) {
            break;
        }
        if (received != length ||
            memcmp(output.data(), expected.data(), length) != 0) {
            consumer_error = "batch length, order or content mismatch";
            break;
        }
    }
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    EXPECT_TRUE(producer_error.empty()) << producer_error;
    EXPECT_TRUE(consumer_error.empty()) << consumer_error;
}

TEST(IpcV2QueueMappingTest, reuses_existing_tail_and_preserves_neighbors) {
    using namespace brpc::ubring;
    struct alignas(64) QueueStorage {
        IpcV2Slot slots[2];
        uint8_t trailing[31];
    } storage;
    memset(&storage, 0xa5, sizeof(storage));
    UbrDataStatusQMsg status = {};
    status.tail = 99;
    status.timeout = 12345;
    status.heart_beat = 7;

    IpcV2QueueView queue;
    ASSERT_TRUE(InitializeIpcV2ReceiveQueue(
        reinterpret_cast<uint8_t*>(storage.slots), sizeof(storage),
        &status.tail, queue));
    EXPECT_EQ(storage.slots, queue.slots);
    EXPECT_EQ(&status.tail, queue.tail);
    EXPECT_EQ(2u, queue.capacity);
    EXPECT_EQ(1u, __atomic_load_n(&status.tail, __ATOMIC_ACQUIRE));
    EXPECT_EQ(12345u, status.timeout);
    EXPECT_EQ(7u, status.heart_beat);
    EXPECT_EQ(IPC_V2_SLOT_EMPTY,
              __atomic_load_n(&storage.slots[0].header.state,
                              __ATOMIC_ACQUIRE));
    EXPECT_EQ(IPC_V2_SLOT_EMPTY,
              __atomic_load_n(&storage.slots[1].header.state,
                              __ATOMIC_ACQUIRE));
    const uint8_t* trailing = reinterpret_cast<const uint8_t*>(&storage) +
        sizeof(storage.slots);
    for (size_t i = 0; i < sizeof(storage) - sizeof(storage.slots); ++i) {
        EXPECT_EQ(0xa5, trailing[i]);
    }
}

TEST(IpcV2QueueMappingTest, rejects_invalid_layout_without_modification) {
    using namespace brpc::ubring;
    IpcV2Slot slots[2] = {};
    uint32_t tail = 77;
    IpcV2QueueView queue;
    EXPECT_FALSE(MapIpcV2Queue(nullptr, sizeof(slots), &tail, queue));
    EXPECT_FALSE(MapIpcV2Queue(
        reinterpret_cast<uint8_t*>(slots), sizeof(IpcV2Slot), &tail, queue));
    EXPECT_FALSE(MapIpcV2Queue(
        reinterpret_cast<uint8_t*>(slots) + 1, sizeof(slots) - 1,
        &tail, queue));
    alignas(uint32_t) uint8_t tail_storage[sizeof(uint32_t) + 1] = {};
    EXPECT_FALSE(MapIpcV2Queue(
        reinterpret_cast<uint8_t*>(slots), sizeof(slots),
        reinterpret_cast<uint32_t*>(tail_storage + 1), queue));
    EXPECT_EQ(nullptr, queue.slots);
    EXPECT_EQ(nullptr, queue.tail);
    EXPECT_EQ(0u, queue.capacity);
    EXPECT_EQ(77u, tail);
}

TEST(IpcV2FormatPreparationTest, prepares_layout_without_activation) {
    using namespace brpc::ubring;
    IpcV2Slot local_slots[2] = {};
    IpcV2Slot remote_slots[3] = {};
    UbrDataStatusQMsg local_status = {};
    UbrDataStatusQMsg remote_status = {};
    local_status.tail = 91;
    local_status.timeout = 111;
    local_status.heart_beat = 3;
    remote_status.tail = 92;
    remote_status.timeout = 222;
    remote_status.heart_beat = 4;
    UbrTrx trx = {};
    trx.ubr_tx.remote_data_q.addr =
        reinterpret_cast<uint8_t*>(remote_slots);
    trx.ubr_tx.remote_data_q.len = sizeof(remote_slots);
    trx.ubr_tx.local_data_status_q.addr =
        reinterpret_cast<uint8_t*>(&local_status);
    trx.ubr_rx.local_data_q.addr = reinterpret_cast<uint8_t*>(local_slots);
    trx.ubr_rx.local_data_q.len = sizeof(local_slots);
    trx.ubr_rx.remote_data_status_q.addr =
        reinterpret_cast<uint8_t*>(&remote_status);

    UBRing ring;
    ring.SetTrxForTest(&trx);
    ASSERT_EQ(0, ring.UbrPrepareIpcV2Format());
    EXPECT_EQ(UBR_DATA_FORMAT_IPC_V2, ring.data_format());
    EXPECT_EQ(3u, trx.ubr_tx.capacity);
    EXPECT_EQ(0u, trx.ubr_tx.write_pos);
    EXPECT_EQ(2u, trx.ubr_rx.capacity);
    EXPECT_EQ(0u, trx.ubr_rx.read_pos);
    EXPECT_EQ(0u, trx.ubr_rx.ipc_v2_read_offset);
    EXPECT_EQ(UBR_STATE_NONE, trx.ubr_tx.trx_state);
    EXPECT_EQ(UBR_STATE_NONE, trx.ubr_rx.trx_state);
    EXPECT_EQ(0, trx.timer_fd);
    EXPECT_EQ(0, trx.hb_timer_fd);
    EXPECT_EQ(91u, local_status.tail);
    EXPECT_EQ(1u, __atomic_load_n(&remote_status.tail, __ATOMIC_ACQUIRE));
    EXPECT_EQ(111u, local_status.timeout);
    EXPECT_EQ(3u, local_status.heart_beat);
    EXPECT_EQ(222u, remote_status.timeout);
    EXPECT_EQ(4u, remote_status.heart_beat);
    EXPECT_EQ(IPC_V2_SLOT_EMPTY,
              __atomic_load_n(&local_slots[0].header.state,
                              __ATOMIC_ACQUIRE));
    EXPECT_EQ(IPC_V2_SLOT_UNINITIALIZED,
              __atomic_load_n(&remote_slots[0].header.state,
                              __ATOMIC_ACQUIRE));
    EXPECT_EQ(-1, ring.UbrPrepareIpcV2Format());
}

TEST(IpcV2FormatPreparationTest, failure_keeps_connection_unselected) {
    using namespace brpc::ubring;
    alignas(64) uint8_t local_storage[sizeof(IpcV2Slot) * 2 + 1] = {};
    IpcV2Slot remote_slots[2] = {};
    UbrDataStatusQMsg local_status = {};
    UbrDataStatusQMsg remote_status = {};
    local_status.tail = 71;
    remote_status.tail = 72;
    UbrTrx trx = {};
    trx.ubr_tx.remote_data_q.addr =
        reinterpret_cast<uint8_t*>(remote_slots);
    trx.ubr_tx.remote_data_q.len = sizeof(remote_slots);
    trx.ubr_tx.local_data_status_q.addr =
        reinterpret_cast<uint8_t*>(&local_status);
    trx.ubr_rx.local_data_q.addr = local_storage + 1;
    trx.ubr_rx.local_data_q.len = sizeof(local_storage) - 1;
    trx.ubr_rx.remote_data_status_q.addr =
        reinterpret_cast<uint8_t*>(&remote_status);

    UBRing ring;
    ring.SetTrxForTest(&trx);
    EXPECT_EQ(-1, ring.UbrPrepareIpcV2Format());
    EXPECT_EQ(UBR_DATA_FORMAT_NONE, ring.data_format());
    EXPECT_EQ(0u, trx.ubr_tx.capacity);
    EXPECT_EQ(0u, trx.ubr_rx.capacity);
    EXPECT_EQ(71u, local_status.tail);
    EXPECT_EQ(72u, remote_status.tail);
}

struct IpcV2UbrTestContext {
    IpcV2UbrTestContext() {
        memset(local_slots, 0, sizeof(local_slots));
        memset(remote_slots, 0, sizeof(remote_slots));
        memset(&local_status, 0, sizeof(local_status));
        memset(&remote_status, 0, sizeof(remote_status));
        memset(&local_rx_event, 0, sizeof(local_rx_event));
        memset(&local_tx_event, 0, sizeof(local_tx_event));
        memset(&remote_rx_event, 0, sizeof(remote_rx_event));
        trx.local_shm.addr = reinterpret_cast<uint8_t*>(local_slots);
        trx.ubr_tx.remote_data_q.addr =
            reinterpret_cast<uint8_t*>(remote_slots);
        trx.ubr_tx.remote_data_q.len = sizeof(remote_slots);
        trx.ubr_tx.local_data_status_q.addr =
            reinterpret_cast<uint8_t*>(&local_status);
        trx.ubr_tx.local_tx_event_q.addr =
            reinterpret_cast<uint8_t*>(&local_tx_event);
        trx.ubr_tx.remote_rx_event_q.addr =
            reinterpret_cast<uint8_t*>(&remote_rx_event);
        trx.ubr_rx.local_data_q.addr =
            reinterpret_cast<uint8_t*>(local_slots);
        trx.ubr_rx.local_data_q.len = sizeof(local_slots);
        trx.ubr_rx.remote_data_status_q.addr =
            reinterpret_cast<uint8_t*>(&remote_status);
        trx.ubr_rx.local_rx_event_q.addr =
            reinterpret_cast<uint8_t*>(&local_rx_event);
        ring.SetTrxForTest(&trx);
    }

    bool PrepareAndConnect() {
        if (ring.UbrPrepareIpcV2Format() != 0 ||
            !brpc::ubring::IpcV2RxView::InitializeShared(
                remote_slots, &local_status.tail, CAPACITY)) {
            return false;
        }
        trx.ubr_tx.trx_state = brpc::ubring::UBR_STATE_CONNECTED;
        trx.ubr_rx.trx_state = brpc::ubring::UBR_STATE_CONNECTED;
        return true;
    }

    enum { CAPACITY = 4 };
    brpc::ubring::IpcV2Slot local_slots[CAPACITY];
    brpc::ubring::IpcV2Slot remote_slots[CAPACITY];
    brpc::ubring::UbrDataStatusQMsg local_status;
    brpc::ubring::UbrDataStatusQMsg remote_status;
    brpc::ubring::UbrEventQMsg local_rx_event;
    brpc::ubring::UbrEventQMsg local_tx_event;
    brpc::ubring::UbrEventQMsg remote_rx_event;
    brpc::ubring::UbrTrx trx{};
    brpc::ubring::UBRing ring;
};

TEST(IpcV2UbrDispatchTest, writev_readv_and_notifications) {
    using namespace brpc::ubring;
    IpcV2UbrTestContext context;
    ASSERT_TRUE(context.PrepareAndConnect());
    EXPECT_EQ(UBRING_OK, context.ring.IsUbrTrxWriteable(0));

    std::string source(IpcV2Slot::PAYLOAD_SIZE + 19, '\0');
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<char>((i * 13 + 5) & 0xff);
    }
    struct iovec source_iov[2] = {
        {&source[0], 17},
        {&source[17], source.size() - 17},
    };
    ASSERT_EQ(static_cast<ssize_t>(source.size()),
              context.ring.UbrTrxWritev(source_iov, 2));
    EXPECT_EQ(1u, context.trx.ubr_tx.out_io_id);
    EXPECT_EQ(1u, context.remote_rx_event.io_id);

    uint32_t peer_read_pos = 0;
    uint32_t peer_read_offset = 0;
    IpcV2RxView peer_rx(
        context.remote_slots, &context.local_status.tail,
        IpcV2UbrTestContext::CAPACITY, &peer_read_pos, &peer_read_offset);
    std::string peer_output(source.size(), '?');
    struct iovec peer_destination = {&peer_output[0], peer_output.size()};
    size_t peer_read = 0;
    bool peer_batch_end = false;
    ASSERT_EQ(IPC_V2_RING_OK,
              peer_rx.TryReadv(&peer_destination, 1, peer_read,
                               peer_batch_end));
    EXPECT_TRUE(peer_batch_end);
    EXPECT_EQ(source, peer_output);

    char empty_value = 0;
    struct iovec empty_destination = {&empty_value, 1};
    errno = 0;
    EXPECT_EQ(-1, context.ring.UbrTrxReadv(&empty_destination, 1));
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_EQ(MPA_MUXER_NOT_READY, context.ring.IsUbrTrxReadable(0));

    uint32_t peer_write_pos = 0;
    IpcV2TxView peer_tx(
        context.local_slots, &context.remote_status.tail,
        IpcV2UbrTestContext::CAPACITY, &peer_write_pos);
    size_t peer_written = 0;
    ASSERT_EQ(IPC_V2_RING_OK,
              peer_tx.TryWritev(source_iov, 2, peer_written));
    context.local_rx_event.io_id = 1;
    EXPECT_EQ(UBRING_OK, context.ring.IsUbrTrxReadable(0));

    std::string output(source.size(), '?');
    struct iovec first = {&output[0], 31};
    ASSERT_EQ(31, context.ring.UbrTrxReadv(&first, 1));
    EXPECT_EQ(0u, context.trx.ubr_rx.read_pos);
    EXPECT_EQ(31u, context.trx.ubr_rx.ipc_v2_read_offset);
    struct iovec remaining = {&output[31], output.size() - 31};
    ASSERT_EQ(static_cast<ssize_t>(output.size() - 31),
              context.ring.UbrTrxReadv(&remaining, 1));
    EXPECT_EQ(2u, context.trx.ubr_rx.read_pos);
    EXPECT_EQ(0u, context.trx.ubr_rx.ipc_v2_read_offset);
    EXPECT_EQ(source, output);
}

TEST(IpcV2UbrDispatchTest, backpressure_and_error_mapping) {
    using namespace brpc::ubring;
    IpcV2UbrTestContext context;
    ASSERT_TRUE(context.PrepareAndConnect());
    char values[4] = {'a', 'b', 'c', 'd'};
    for (int i = 0; i < 3; ++i) {
        struct iovec source = {&values[i], 1};
        ASSERT_EQ(1, context.ring.UbrTrxWritev(&source, 1));
    }
    const uint64_t notified_io_id = context.remote_rx_event.io_id;
    struct iovec fourth = {&values[3], 1};
    EXPECT_EQ(UBRING_RETRY, context.ring.UbrTrxWritev(&fourth, 1));
    EXPECT_EQ(notified_io_id, context.remote_rx_event.io_id);
    EXPECT_EQ(MPA_MUXER_NOT_READY,
              context.ring.IsUbrTrxWriteable(0));

    std::string oversized(
        IpcV2Slot::PAYLOAD_SIZE * IpcV2UbrTestContext::CAPACITY, 'x');
    struct iovec too_large = {&oversized[0], oversized.size()};
    errno = 0;
    EXPECT_EQ(-1, context.ring.UbrTrxWritev(&too_large, 1));
    EXPECT_EQ(EMSGSIZE, errno);
    EXPECT_EQ(notified_io_id, context.remote_rx_event.io_id);

    context.local_slots[0].header.payload_len =
        IpcV2Slot::PAYLOAD_SIZE + 1;
    context.local_slots[0].header.flags = IPC_V2_SLOT_EOF;
    context.local_slots[0].header.reserved = 0;
    __atomic_store_n(&context.local_slots[0].header.state,
                     IPC_V2_SLOT_READY, __ATOMIC_RELEASE);
    char destination = 0;
    struct iovec invalid = {&destination, 1};
    EXPECT_EQ(UBRING_OK, context.ring.IsUbrTrxReadable(0));
    errno = 0;
    EXPECT_EQ(-1, context.ring.UbrTrxReadv(&invalid, 1));
    EXPECT_EQ(EBADMSG, errno);

    context.trx.data_format = UBR_DATA_FORMAT_NONE;
    errno = 0;
    EXPECT_EQ(UBRING_ERR, context.ring.UbrTrxWritev(&fourth, 1));
    EXPECT_EQ(EPROTONOSUPPORT, errno);
}

TEST(IpcV2UbrDispatchTest, rejects_uninitialized_remote_ring) {
    using namespace brpc::ubring;
    IpcV2UbrTestContext context;
    ASSERT_EQ(0, context.ring.UbrPrepareIpcV2Format());
    context.trx.ubr_tx.trx_state = UBR_STATE_CONNECTED;
    context.trx.ubr_rx.trx_state = UBR_STATE_CONNECTED;

    EXPECT_EQ(UBRING_ERR, context.ring.IsUbrTrxWriteable(0));
    char value = 'x';
    struct iovec source = {&value, 1};
    errno = 0;
    EXPECT_EQ(-1, context.ring.UbrTrxWritev(&source, 1));
    EXPECT_EQ(EBADMSG, errno);
    EXPECT_EQ(0u, context.trx.ubr_tx.out_io_id);
    EXPECT_EQ(0u, context.remote_rx_event.io_id);
}

struct __attribute__((packed)) HelloMessageLayout {
    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint64_t len;
    char shm_name[SHM_MAX_NAME_BUFF_LEN];
};
}

class HelloMessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        memset(&msg, 0, sizeof(msg));
        buffer.resize(256, 0);
    }

    brpc::ubring::HelloMessage msg;
    std::string buffer;
};

TEST(HelloFormatExtensionTest, serialize_deserialize_roundtrip) {
    brpc::ubring::HelloFormatExtension extension = {
        brpc::ubring::HelloFormatExtension::WIRE_SIZE,
        brpc::ubring::UBR_DATA_FORMAT_LEGACY_64};
    char buffer[brpc::ubring::HelloFormatExtension::WIRE_SIZE] = {};

    extension.Serialize(buffer);

    brpc::ubring::HelloFormatExtension decoded = {};
    decoded.Deserialize(buffer);
    EXPECT_EQ(extension.extension_len, decoded.extension_len);
    EXPECT_EQ(extension.format_id, decoded.format_id);

    extension.format_id = brpc::ubring::UBR_DATA_FORMAT_IPC_V2;
    extension.Serialize(buffer);
    decoded.Deserialize(buffer);
    EXPECT_EQ(extension.extension_len, decoded.extension_len);
    EXPECT_EQ(extension.format_id, decoded.format_id);
}

TEST(HelloFormatExtensionTest, serialize_uses_network_byte_order) {
    brpc::ubring::HelloFormatExtension extension = {0x0102, 0x0304};
    char buffer[brpc::ubring::HelloFormatExtension::WIRE_SIZE] = {};
    const unsigned char expected[] = {0x01, 0x02, 0x03, 0x04};

    extension.Serialize(buffer);

    EXPECT_EQ(0, memcmp(expected, buffer, sizeof(expected)));
}

TEST(HelloFormatExtensionTest, deserialize_none_format) {
    const unsigned char buffer[] = {0x00, 0x04, 0x00, 0x00};
    brpc::ubring::HelloFormatExtension extension = {};

    extension.Deserialize(buffer);

    EXPECT_EQ(4, extension.extension_len);
    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE, extension.format_id);
}

TEST(HelloFormatExtensionTest, deserialize_unknown_format) {
    const unsigned char buffer[] = {0x00, 0x04, 0x12, 0x34};
    brpc::ubring::HelloFormatExtension extension = {};

    extension.Deserialize(buffer);

    EXPECT_EQ(4, extension.extension_len);
    EXPECT_EQ(0x1234, extension.format_id);
}

TEST(HelloFormatExtensionTest, selects_format_for_local_backend) {
    using namespace brpc::ubring;
    EXPECT_EQ(UBR_DATA_FORMAT_IPC_V2,
              PreferredDataFormatForShmType(SHM_TYPE_IPC));
    EXPECT_EQ(UBR_DATA_FORMAT_LEGACY_64,
              PreferredDataFormatForShmType(SHM_TYPE_UBS));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              PreferredDataFormatForShmType(SHM_TYPE_UB));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              PreferredDataFormatForShmType(SHM_TYPE_UNSUPPORT));
}

TEST(HelloFormatExtensionTest, selects_only_matching_supported_format) {
    using namespace brpc::ubring;
    EXPECT_EQ(UBR_DATA_FORMAT_IPC_V2,
              SelectDataFormat(UBR_DATA_FORMAT_IPC_V2,
                               UBR_DATA_FORMAT_IPC_V2));
    EXPECT_EQ(UBR_DATA_FORMAT_LEGACY_64,
              SelectDataFormat(UBR_DATA_FORMAT_LEGACY_64,
                               UBR_DATA_FORMAT_LEGACY_64));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              SelectDataFormat(UBR_DATA_FORMAT_IPC_V2,
                               UBR_DATA_FORMAT_LEGACY_64));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              SelectDataFormat(UBR_DATA_FORMAT_LEGACY_64,
                               UBR_DATA_FORMAT_IPC_V2));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              SelectDataFormat(UBR_DATA_FORMAT_IPC_V2, 0x1234));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              SelectDataFormat(UBR_DATA_FORMAT_NONE,
                               UBR_DATA_FORMAT_NONE));
    EXPECT_EQ(UBR_DATA_FORMAT_NONE,
              SelectDataFormat(static_cast<UbrDataFormat>(0x1234), 0x1234));
}

TEST_F(HelloMessageTest, serialize_deserialize_roundtrip) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 4 * 1024 * 1024;
    memcpy(msg.shm_name, "UBRING_test_C", 14);

    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(msg.msg_len, decoded.msg_len);
    EXPECT_EQ(msg.hello_ver, decoded.hello_ver);
    EXPECT_EQ(msg.impl_ver, decoded.impl_ver);
    EXPECT_EQ(msg.len, decoded.len);
    EXPECT_EQ(0, memcmp(msg.shm_name, decoded.shm_name, SHM_MAX_NAME_BUFF_LEN));
}

TEST_F(HelloMessageTest, serialize_uses_network_byte_order) {
    msg.msg_len = 0x0102;
    msg.hello_ver = 0x0304;
    msg.impl_ver = 0x0506;
    msg.len = 0x0102030405060708ULL;
    memset(msg.shm_name, 0, SHM_MAX_NAME_BUFF_LEN);

    msg.Serialize(&buffer[0]);

    HelloMessageLayout* raw = reinterpret_cast<HelloMessageLayout*>(&buffer[0]);
    EXPECT_EQ(butil::HostToNet16(0x0102), raw->msg_len);
    EXPECT_EQ(butil::HostToNet16(0x0304), raw->hello_ver);
    EXPECT_EQ(butil::HostToNet16(0x0506), raw->impl_ver);
    EXPECT_EQ(butil::HostToNet64(0x0102030405060708ULL), raw->len);
}

TEST_F(HelloMessageTest, large_len_value) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 0xFFFFFFFFFFFFFFFFULL;
    memset(msg.shm_name, 0, SHM_MAX_NAME_BUFF_LEN);

    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(0xFFFFFFFFFFFFFFFFULL, decoded.len);
}

TEST_F(HelloMessageTest, full_shm_name) {
    memset(msg.shm_name, 'A', SHM_MAX_NAME_BUFF_LEN);
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 0;
    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(0, memcmp(msg.shm_name, decoded.shm_name, SHM_MAX_NAME_BUFF_LEN));
}

TEST_F(HelloMessageTest, toString_contains_fields) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 4194304;
    memcpy(msg.shm_name, "UBRING_test", 12);

    std::string s = msg.toString();
    EXPECT_NE(std::string::npos, s.find("msg_len=64"));
    EXPECT_NE(std::string::npos, s.find("hello_ver=2"));
    EXPECT_NE(std::string::npos, s.find("impl_ver=1"));
    EXPECT_NE(std::string::npos, s.find("UBRING_test"));
}

TEST(UBRingConfigurationTest, time_flags_include_units_and_expected_defaults) {
    struct TimeFlagExpectation {
        const char* name;
        const char* suffix;
        const char* unit;
        const char* default_value;
    };
    const TimeFlagExpectation expected_flags[] = {
        {"ub_disconnect_timeout_s", "_s", "seconds", "5"},
        {"ub_connect_timeout_s", "_s", "seconds", "1"},
        {"ub_hb_timer_interval_s", "_s", "seconds", "5"},
        {"ub_event_queue_timer_interval_us", "_us", "microseconds", "100"},
        {"ub_flying_io_timeout_s", "_s", "seconds", "5"},
    };

    for (const auto& expected : expected_flags) {
        GFLAGS_NAMESPACE::CommandLineFlagInfo info;
        ASSERT_TRUE(GFLAGS_NAMESPACE::GetCommandLineFlagInfo(
            expected.name, &info)) << expected.name;
        const std::string flag_name(expected.name);
        const std::string suffix(expected.suffix);
        ASSERT_GE(flag_name.size(), suffix.size());
        EXPECT_EQ(flag_name.size() - suffix.size(), flag_name.rfind(suffix));
        EXPECT_NE(std::string::npos, info.description.find(expected.unit));
        EXPECT_EQ(std::string(expected.default_value), info.default_value);
    }

    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_disconnect_timeout_s);
    EXPECT_EQ(1, brpc::ubring::FLAGS_ub_connect_timeout_s);
    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_hb_timer_interval_s);
    EXPECT_EQ(100, brpc::ubring::FLAGS_ub_event_queue_timer_interval_us);
    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_flying_io_timeout_s);
    EXPECT_EQ(100U * USEC_TO_NSEC,
              static_cast<uint32_t>(
                  brpc::ubring::FLAGS_ub_event_queue_timer_interval_us) *
                  USEC_TO_NSEC);
}

namespace brpc {
namespace ubring {
class UBShmEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        _saved_skip = g_skip_ub_init;
        g_skip_ub_init = false;
        ShmMgrInit();
        UBRingManager::UbrMgrInit();
        UBShmEndpoint::GlobalInitialize();

        brpc::SocketOptions options;
        ASSERT_EQ(0, brpc::Socket::Create(options, &_socket_id));
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(_socket_id, &s));
        _socket = s.get();
        _ep = new UBShmEndpoint(_socket);
    }

    void TearDown() override {
        delete _ep;
        brpc::SocketUniquePtr s;
        if (brpc::Socket::Address(_socket_id, &s) == 0) {
            s->SetFailed();
        }
        UBShmEndpoint::GlobalRelease();
        UBRingManager::UbrMgrFini();
        ShmMgrFini();
        g_skip_ub_init = _saved_skip;
    }

    brpc::SocketId _socket_id = brpc::INVALID_SOCKET_ID;
    brpc::Socket* _socket = nullptr;
    UBShmEndpoint* _ep = nullptr;
    bool _saved_skip = false;
};
}  // namespace ubring
}  // namespace brpc

using brpc::ubring::UBShmEndpointTest;

TEST_F(UBShmEndpointTest, construct_initial_state) {
    ASSERT_NE(nullptr, _ep);
    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE,
              _ep->_negotiated_data_format);
}

TEST_F(UBShmEndpointTest, reset_clears_negotiated_data_format) {
    _ep->_negotiated_data_format = brpc::ubring::UBR_DATA_FORMAT_LEGACY_64;

    _ep->Reset();

    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE,
              _ep->_negotiated_data_format);
}

TEST_F(UBShmEndpointTest, allocate_client_resources_real_shm) {
    brpc::ubring::SHM local_trx_shm =
        {nullptr, 4 * 1024 * 1024, 0, {0}, (uint32_t)_socket->fd()};
    int ret = _ep->AllocateClientResources(&local_trx_shm, "UBRING_ut_client");
    EXPECT_EQ(0, ret);
}

TEST_F(UBShmEndpointTest, reset_cleans_up_resources) {
    brpc::ubring::SHM local_trx_shm =
        {nullptr, 4 * 1024 * 1024, 0, {0}, (uint32_t)_socket->fd()};
    _ep->AllocateClientResources(&local_trx_shm, "UBRING_ut_reset");
    _ep->Reset();
}

TEST_F(UBShmEndpointTest, reset_is_idempotent) {
    _ep->Reset();
    _ep->Reset();
}

// The receive paths (UbrTrxRecvBlockMode / StartReadv) read `msg_len' and
// `cur_index' out of a chunk header the remote peer writes into the ring, then
// copy `msg_len - cur_index' bytes from the 60-byte `payload.inner'. A peer
// that writes msg_len > 60, or cur_index > msg_len (which underflows the
// uint8_t subtraction), makes that copy over-read the payload into adjacent
// shared memory. IsRecvChunkHeaderValid is the guard both paths now apply.
TEST(UBRingRecvChunkHeaderTest, reject_out_of_range_len_and_index) {
    using brpc::ubring::UBRing;
    // Legitimate values a well-formed peer produces: full payload, partial
    // consume, and the fully-consumed boundary.
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN, 0));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(10, 5));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(0, 0));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN,
                                               UBR_MSG_PAYLOAD_LEN));
    // msg_len past the payload capacity -> over-read source.
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN + 1, 0));
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(255, 0));
    // cur_index past msg_len -> `msg_len - cur_index' underflows to a large
    // uint8_t.
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(0, 1));
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(10, 20));
}

// A crafted chunk laid out exactly like one in the ring: the guard rejects it
// so the recv loop never reaches the over-reading memcpy.
TEST(UBRingRecvChunkHeaderTest, crafted_chunk_is_rejected) {
    brpc::ubring::UbrMsgFormat chunk;
    memset(&chunk, 0xAB, sizeof(chunk));
    chunk.header[UBR_MSG_LEN_INDEX] = 255;  // peer claims 255 bytes in a 60-byte payload
    chunk.header[UBR_MSG_CUR_INDEX] = 0;
    EXPECT_FALSE(brpc::ubring::UBRing::IsRecvChunkHeaderValid(
        chunk.header[UBR_MSG_LEN_INDEX], chunk.header[UBR_MSG_CUR_INDEX]));

    chunk.header[UBR_MSG_LEN_INDEX] = UBR_MSG_PAYLOAD_LEN;
    chunk.header[UBR_MSG_CUR_INDEX] = 0;
    EXPECT_TRUE(brpc::ubring::UBRing::IsRecvChunkHeaderValid(
        chunk.header[UBR_MSG_LEN_INDEX], chunk.header[UBR_MSG_CUR_INDEX]));
}

#else

TEST(UbringDisabledTest, skip) {
    SUCCEED() << "BRPC_WITH_UBRING is not enabled, skip.";
}

#endif  // BRPC_WITH_UBRING
