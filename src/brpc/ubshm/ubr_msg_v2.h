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
#include <limits>
#include <string.h>
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
    void Initialize() {
        header.payload_len = 0;
        header.flags = 0;
        header.reserved = 0;
        __atomic_store_n(&header.state, IPC_V2_SLOT_EMPTY, __ATOMIC_RELEASE);
    }

    IpcV2SlotResult TryConsume(void* destination, size_t size,
                               uint32_t& offset, size_t& copied,
                               bool& batch_end) {
        copied = 0;
        batch_end = false;
        if (size == 0) {
            return IPC_V2_SLOT_OK;
        }
        if (destination == nullptr) {
            return IPC_V2_SLOT_INVALID;
        }
        const uint32_t state =
            __atomic_load_n(&header.state, __ATOMIC_ACQUIRE);
        if (state == IPC_V2_SLOT_EMPTY) {
            return offset == 0 ? IPC_V2_SLOT_NOT_READY : IPC_V2_SLOT_INVALID;
        }
        if (state != IPC_V2_SLOT_READY) {
            return IPC_V2_SLOT_INVALID;
        }
        const uint32_t length = header.payload_len;
        const uint32_t flags = header.flags;
        if (length == 0 || length > PAYLOAD_SIZE || offset >= length ||
            (flags & ~static_cast<uint32_t>(IPC_V2_SLOT_EOF)) != 0 ||
            header.reserved != 0) {
            return IPC_V2_SLOT_INVALID;
        }
        const size_t remaining = length - offset;
        copied = size < remaining ? size : remaining;
        memcpy(destination, payload + offset, copied);
        offset += static_cast<uint32_t>(copied);
        if (offset == length) {
            batch_end = (flags & IPC_V2_SLOT_EOF) != 0;
            offset = 0;
            // Do not access this slot after returning ownership to Producer.
            __atomic_store_n(&header.state, IPC_V2_SLOT_EMPTY,
                             __ATOMIC_RELEASE);
        }
        return IPC_V2_SLOT_OK;
    }

    // `data_bytes` excludes the separately allocated control area. Trailing
    // bytes that do not fit one slot are intentionally unused.
    static bool CalculateCapacity(size_t data_bytes, uint32_t& capacity) {
        capacity = 0;
        const size_t slots = data_bytes / SLOT_SIZE;
        if (slots < 2 || slots > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        capacity = static_cast<uint32_t>(slots);
        return true;
    }
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
inline bool ValidateIpcV2Iov(const struct iovec* iov, int iovcnt,
                             size_t& total) {
    total = 0;
    if (iovcnt < 0 || (iovcnt > 0 && iov == nullptr)) {
        return false;
    }
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len != 0 && iov[i].iov_base == nullptr) {
            return false;
        }
        if (iov[i].iov_len > std::numeric_limits<size_t>::max() - total) {
            return false;
        }
        total += iov[i].iov_len;
    }
    return true;
}

// These views implement only one SPSC direction. UBRing owns notification,
// activation, teardown and shared-memory lifetime management.
class IpcV2TxView {
public:
    IpcV2TxView(IpcV2Slot* slots, uint32_t* tail, uint32_t capacity,
                uint32_t* write_pos)
        : _slots(slots), _tail(tail), _capacity(capacity),
          _write_pos(write_pos) {}

    IpcV2RingResult TryWritev(const struct iovec* iov, int iovcnt,
                              size_t& written) {
        written = 0;
        size_t total = 0;
        if (!ValidateIpcV2Iov(iov, iovcnt, total)) {
            return IPC_V2_RING_INVALID;
        }
        if (total == 0) {
            return IPC_V2_RING_OK;
        }
        if (_slots == nullptr || _tail == nullptr || _write_pos == nullptr ||
            _capacity < 2 || *_write_pos >= _capacity) {
            return IPC_V2_RING_INVALID;
        }
        const uint32_t current_state = __atomic_load_n(
            &_slots[*_write_pos].header.state, __ATOMIC_ACQUIRE);
        if (current_state != IPC_V2_SLOT_EMPTY &&
            current_state != IPC_V2_SLOT_READY) {
            return IPC_V2_RING_INVALID;
        }
        const size_t required = total / IpcV2Slot::PAYLOAD_SIZE +
            (total % IpcV2Slot::PAYLOAD_SIZE != 0);
        if (required >= _capacity) {
            return IPC_V2_RING_TOO_LARGE;
        }
        const uint32_t tail = __atomic_load_n(_tail, __ATOMIC_ACQUIRE);
        if (tail >= _capacity) {
            return IPC_V2_RING_INVALID;
        }
        const uint64_t available = *_write_pos > tail
            ? static_cast<uint64_t>(tail) + _capacity - *_write_pos
            : tail - *_write_pos;
        if (available < required) {
            return IPC_V2_RING_RETRY;
        }
        for (size_t i = 0; i < required; ++i) {
            const uint32_t pos = static_cast<uint32_t>(
                (static_cast<uint64_t>(*_write_pos) + i) % _capacity);
            if (__atomic_load_n(&_slots[pos].header.state, __ATOMIC_ACQUIRE) !=
                IPC_V2_SLOT_EMPTY) {
                return IPC_V2_RING_INVALID;
            }
        }

        int iov_index = 0;
        size_t iov_offset = 0;
        size_t remaining = total;
        while (remaining != 0) {
            IpcV2Slot& slot = _slots[*_write_pos];
            const size_t slot_length = remaining < IpcV2Slot::PAYLOAD_SIZE
                ? remaining : IpcV2Slot::PAYLOAD_SIZE;
            size_t slot_offset = 0;
            while (slot_offset < slot_length) {
                while (iov_index < iovcnt && iov[iov_index].iov_len == 0) {
                    ++iov_index;
                }
                const size_t available = iov[iov_index].iov_len - iov_offset;
                const size_t copy_length = available < slot_length - slot_offset
                    ? available : slot_length - slot_offset;
                memcpy(slot.payload + slot_offset,
                       static_cast<const uint8_t*>(iov[iov_index].iov_base) +
                           iov_offset,
                       copy_length);
                slot_offset += copy_length;
                iov_offset += copy_length;
                if (iov_offset == iov[iov_index].iov_len) {
                    ++iov_index;
                    iov_offset = 0;
                }
            }
            slot.header.payload_len = static_cast<uint32_t>(slot_length);
            slot.header.flags = remaining == slot_length
                ? static_cast<uint32_t>(IPC_V2_SLOT_EOF) : 0u;
            slot.header.reserved = 0;
            __atomic_store_n(&slot.header.state, IPC_V2_SLOT_READY,
                             __ATOMIC_RELEASE);
            *_write_pos = (*_write_pos + 1) % _capacity;
            remaining -= slot_length;
        }
        written = total;
        return IPC_V2_RING_OK;
    }

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
                                 uint32_t capacity) {
        if (slots == nullptr || tail == nullptr || capacity < 2) {
            return false;
        }
        for (uint32_t i = 0; i < capacity; ++i) {
            slots[i].Initialize();
        }
        __atomic_store_n(tail, capacity - 1, __ATOMIC_RELEASE);
        return true;
    }

    IpcV2RingResult TryReadv(const struct iovec* iov, int iovcnt,
                             size_t& read, bool& batch_end) {
        read = 0;
        batch_end = false;
        size_t destination_size = 0;
        if (!ValidateIpcV2Iov(iov, iovcnt, destination_size)) {
            return IPC_V2_RING_INVALID;
        }
        if (destination_size == 0) {
            return IPC_V2_RING_OK;
        }
        if (_slots == nullptr || _tail == nullptr || _read_pos == nullptr ||
            _read_offset == nullptr || _capacity < 2 ||
            *_read_pos >= _capacity) {
            return IPC_V2_RING_INVALID;
        }

        int iov_index = 0;
        size_t iov_offset = 0;
        while (read < destination_size) {
            while (iov_index < iovcnt && iov[iov_index].iov_len == 0) {
                ++iov_index;
            }
            size_t copied = 0;
            bool slot_batch_end = false;
            const IpcV2SlotResult result = _slots[*_read_pos].TryConsume(
                static_cast<uint8_t*>(iov[iov_index].iov_base) + iov_offset,
                iov[iov_index].iov_len - iov_offset, *_read_offset, copied,
                slot_batch_end);
            if (result == IPC_V2_SLOT_NOT_READY) {
                return read == 0 ? IPC_V2_RING_RETRY : IPC_V2_RING_OK;
            }
            if (result != IPC_V2_SLOT_OK || copied == 0) {
                return IPC_V2_RING_INVALID;
            }
            read += copied;
            iov_offset += copied;
            if (iov_offset == iov[iov_index].iov_len) {
                ++iov_index;
                iov_offset = 0;
            }
            if (*_read_offset == 0) {
                // TryConsume releases EMPTY first; tail follows that release.
                __atomic_store_n(_tail, *_read_pos, __ATOMIC_RELEASE);
                *_read_pos = (*_read_pos + 1) % _capacity;
                if (slot_batch_end) {
                    batch_end = true;
                    return IPC_V2_RING_OK;
                }
            }
        }
        return IPC_V2_RING_OK;
    }

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
inline bool MapIpcV2Queue(uint8_t* data_addr, size_t data_len,
                          uint32_t* tail, IpcV2QueueView& queue) {
    queue = IpcV2QueueView();
    if (data_addr == nullptr || tail == nullptr ||
        reinterpret_cast<uintptr_t>(data_addr) % alignof(IpcV2Slot) != 0 ||
        reinterpret_cast<uintptr_t>(tail) % alignof(uint32_t) != 0) {
        return false;
    }
    uint32_t capacity = 0;
    if (!IpcV2Slot::CalculateCapacity(data_len, capacity)) {
        return false;
    }
    queue.slots = reinterpret_cast<IpcV2Slot*>(data_addr);
    queue.tail = tail;
    queue.capacity = capacity;
    return true;
}

inline bool InitializeIpcV2ReceiveQueue(uint8_t* data_addr, size_t data_len,
                                        uint32_t* tail,
                                        IpcV2QueueView& queue) {
    if (!MapIpcV2Queue(data_addr, data_len, tail, queue)) {
        return false;
    }
    return IpcV2RxView::InitializeShared(queue.slots, queue.tail,
                                         queue.capacity);
}

}  // namespace ubring
}  // namespace brpc

#endif  // BRPC_UBSHM_UBR_MSG_V2_H
