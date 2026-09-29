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

#ifndef BRPC_UBSHM_UBR_MSG_V2_H
#define BRPC_UBSHM_UBR_MSG_V2_H

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>
#include <type_traits>

namespace brpc {
namespace ubring {

enum IpcV2SlotState : uint32_t {
    IPC_V2_SLOT_UNINITIALIZED = 0,
    IPC_V2_SLOT_EMPTY = 1,
    IPC_V2_SLOT_READY = 2,
};

enum IpcV2SlotFlags : uint32_t {
    // End of one lower-level write batch, not an RPC or connection boundary.
    IPC_V2_SLOT_EOF = 1u << 0,
};

enum IpcV2SlotResult {
    IPC_V2_SLOT_OK,
    IPC_V2_SLOT_NOT_READY,
    IPC_V2_SLOT_INVALID,
};

enum IpcV2RingResult {
    IPC_V2_RING_OK,
    IPC_V2_RING_RETRY,
    IPC_V2_RING_INVALID,
    IPC_V2_RING_TOO_LARGE,
};

struct IpcV2SlotHeader {
    // Access state atomically. The producer publishes READY with release and
    // the consumer observes it with acquire; recycling EMPTY is symmetric.
    uint32_t state;
    uint32_t payload_len;
    uint32_t flags;
    uint32_t reserved;
};

struct alignas(64) IpcV2Slot {
    enum : size_t {
        SLOT_SIZE = 4096,
        PAYLOAD_OFFSET = 16,
        PAYLOAD_SIZE = SLOT_SIZE - PAYLOAD_OFFSET,
    };

    IpcV2SlotHeader header;
    uint8_t payload[PAYLOAD_SIZE];

    // Initialization happens before the peer is allowed to access this ring.
    void Initialize();

    IpcV2SlotResult TryConsume(void* destination, size_t size,
                               uint32_t& offset, size_t& copied,
                               bool& batch_end);

    // `data_bytes` excludes the separately allocated control area. Trailing
    // bytes that do not fit one slot are intentionally unused.
    static bool CalculateCapacity(size_t data_bytes, uint32_t& capacity);
};

static_assert(std::is_standard_layout<IpcV2SlotHeader>::value,
              "IPC_V2 header must have a predictable layout");
static_assert(sizeof(IpcV2SlotHeader) == IpcV2Slot::PAYLOAD_OFFSET,
              "IPC_V2 header must occupy 16 bytes");
static_assert(offsetof(IpcV2SlotHeader, state) == 0,
              "Unexpected IPC_V2 state offset");
static_assert(offsetof(IpcV2SlotHeader, payload_len) == 4,
              "Unexpected IPC_V2 payload length offset");
static_assert(offsetof(IpcV2SlotHeader, flags) == 8,
              "Unexpected IPC_V2 flags offset");
static_assert(offsetof(IpcV2SlotHeader, reserved) == 12,
              "Unexpected IPC_V2 reserved offset");
static_assert(std::is_standard_layout<IpcV2Slot>::value,
              "IPC_V2 slot must have a predictable layout");
static_assert(offsetof(IpcV2Slot, header) == 0,
              "IPC_V2 header must be first");
static_assert(offsetof(IpcV2Slot, payload) == IpcV2Slot::PAYLOAD_OFFSET,
              "Unexpected IPC_V2 payload offset");
static_assert(sizeof(IpcV2Slot) == IpcV2Slot::SLOT_SIZE,
              "IPC_V2 slot must occupy 4096 bytes");
static_assert(alignof(IpcV2Slot) == 64,
              "IPC_V2 slot must be 64-byte aligned");
static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
              "IPC_V2 slot state requires lock-free 32-bit atomics");

bool ValidateIpcV2Iov(const struct iovec* iov, int iovcnt, size_t& total);

// These views implement only one SPSC direction. UBRing owns notification,
// activation, teardown and shared-memory lifetime management.
class IpcV2TxView {
public:
    IpcV2TxView(IpcV2Slot* slots, uint32_t* tail, uint32_t capacity,
                uint32_t* write_pos)
        : _slots(slots), _tail(tail), _capacity(capacity),
          _write_pos(write_pos) {}

    IpcV2RingResult TryWritev(const struct iovec* iov, int iovcnt,
                              size_t& written);

private:
    IpcV2TxView(const IpcV2TxView&) = delete;
    IpcV2TxView& operator=(const IpcV2TxView&) = delete;

    IpcV2Slot* _slots;
    uint32_t* _tail;
    uint32_t _capacity;
    uint32_t* _write_pos;
};

class IpcV2RxView {
public:
    IpcV2RxView(IpcV2Slot* slots, uint32_t* tail, uint32_t capacity,
                uint32_t* read_pos, uint32_t* read_offset)
        : _slots(slots), _tail(tail), _capacity(capacity),
          _read_pos(read_pos), _read_offset(read_offset) {}

    static bool InitializeShared(IpcV2Slot* slots, uint32_t* tail,
                                 uint32_t capacity);

    IpcV2RingResult TryReadv(const struct iovec* iov, int iovcnt,
                             size_t& read, bool& batch_end);

private:
    IpcV2RxView(const IpcV2RxView&) = delete;
    IpcV2RxView& operator=(const IpcV2RxView&) = delete;

    IpcV2Slot* _slots;
    uint32_t* _tail;
    uint32_t _capacity;
    uint32_t* _read_pos;
    uint32_t* _read_offset;
};

struct IpcV2QueueView {
    IpcV2QueueView() : slots(nullptr), tail(nullptr), capacity(0) {}

    IpcV2Slot* slots;
    uint32_t* tail;
    uint32_t capacity;
};

// Adapt an existing UBRing DataQ and its existing status tail to IPC_V2.
// No shared memory is modified unless initialization succeeds.
bool MapIpcV2Queue(uint8_t* data_addr, size_t data_len,
                   uint32_t* tail, IpcV2QueueView& queue);

bool InitializeIpcV2ReceiveQueue(uint8_t* data_addr, size_t data_len,
                                 uint32_t* tail, IpcV2QueueView& queue);

}  // namespace ubring
}  // namespace brpc

#endif  // BRPC_UBSHM_UBR_MSG_V2_H
