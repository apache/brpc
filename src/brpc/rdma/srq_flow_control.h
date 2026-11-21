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

#ifndef BRPC_RDMA_SRQ_FLOW_CONTROL_H
#define BRPC_RDMA_SRQ_FLOW_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include "butil/atomicops.h"
#include "butil/logging.h"

namespace brpc {
namespace rdma {

// UCX RC-style flow control. Control flags use host byte order here and are
// carried in immediate data on the wire. A grant restores the negotiated
// window, rather than adding private-RQ ACK credits. The receiver only
// publishes atomic events and never waits for the connection _post_mutex. The
// submitter holds _post_mutex and applies events at batch boundaries, never
// between preparation, post_send and accepted-prefix accounting. Grants do not
// wait for receive blocks to return. Pool exhaustion pauses refill and is
// handled by RC RNR.
class SrqFlowControl {
public:
    enum : uint32_t {
        SOFT_REQUEST = 1,
        HARD_REQUEST = 2,
        GRANT = 4,
        PURE_GRANT = SOFT_REQUEST | HARD_REQUEST | GRANT
    };

    // Copyable send state for planning per-WR requests and piggybacked grants
    // in a batch. The snapshot excludes the receive inbox. Plan on the copy,
    // commit only the accepted prefix to the real state, then apply new receive
    // events. Copying the snapshot back would incorrectly commit an unposted
    // suffix.
    class SendState {
    public:
        uint32_t window() const { return _window; }
        uint32_t capacity() const { return _capacity; }
        bool can_send() const { return _window != 0; }
        bool grant_pending() const { return _pending != 0; }
        bool hard_request_pending() const { return _pending == HARD_REQUEST; }

        uint32_t PrepareData() const {
            DCHECK(can_send());
            uint32_t flags = 0;
            if (_window == _capacity / 4) {
                flags = HARD_REQUEST;
            } else if (_window == _capacity / 2) {
                flags = SOFT_REQUEST;
            }
            if (_pending != 0) {
                flags |= GRANT;
            }
            return flags;
        }

        void DataPosted(uint32_t flags) {
            DCHECK(can_send());
            DCHECK(ValidMessage(flags, 1));
            --_window;
            if (flags & GRANT) {
                _pending = 0;
            }
        }

    private:
    friend class SrqFlowControl;
        // Negotiated capacity restored by each received grant.
        uint32_t _capacity{0};
        // Remaining data-send credits. Pure grants do not consume this window.
        uint32_t _window{0};
        // Peer request awaiting a reply: zero, SOFT_REQUEST or HARD_REQUEST.
        uint32_t _pending{0};
    };

    static bool ValidWindow(uint32_t window) {
        return window >= 4 && window <= UINT16_MAX;
    }

    bool Init(uint32_t window) {
        if (!ValidWindow(window)) {
            return false;
        }
        // Handshake initialization only. Must precede receive-side event
        // publication.
        _send._capacity = window;
        _send._window = window;
        _send._pending = 0;
        _received_events.store(0, butil::memory_order_relaxed);
        return true;
    }

    static bool ValidMessage(uint32_t flags, size_t bytes) {
        if (bytes == 0) {
            return flags == PURE_GRANT;
        }
        return (flags & ~PURE_GRANT) == 0 &&
               (flags & (SOFT_REQUEST | HARD_REQUEST)) != (SOFT_REQUEST | HARD_REQUEST);
    }

    // All send-state access below requires the connection _post_mutex. These
    // accessors do not implicitly consume receive events.
    SendState Snapshot() const { return _send; }
    uint32_t window() const { return _send.window(); }
    uint32_t capacity() const { return _send.capacity(); }
    bool can_send() const { return _send.can_send(); }
    bool grant_pending() const { return _send.grant_pending(); }
    bool hard_request_pending() const { return _send.hard_request_pending(); }

    // Prepare only computes flags. It neither consumes a credit nor clears a
    // pending request. After posting under the lock, call DataPosted only for
    // WRs actually accepted by post_send.
    uint32_t PrepareData() const {
        return _send.PrepareData();
    }

    void DataPosted(uint32_t flags) {
        _send.DataPosted(flags);
    }

    bool Receive(uint32_t flags, size_t bytes) {
        if (!ValidMessage(flags, bytes)) {
            return false;
        }
        // PURE_GRANT contains request bits in its wire encoding but means only
        // a grant. Decode before merging: the inbox stores semantic events, not
        // wire messages. Data without control flags needs no atomic
        // read-modify-write.
        if (flags != 0) {
            uint32_t events = flags == PURE_GRANT ? GRANT : flags;
            _received_events.fetch_or(events, butil::memory_order_release);
        }
        return true;
    }

    // Call only before preparing a batch or after accounting for its entire
    // accepted prefix. Events arriving after exchange stay in the inbox for the
    // next application. Clearing an older grant must not erase them.
    void ApplyReceived() {
        // Most small messages carry no control events. Avoid an atomic exchange
        // when the inbox is empty. Events published after this zero check
        // remain for a later batch, writability check or progress callback. The
        // publisher must still wake writers for grants and schedule progress
        // for hard requests.
        if (_received_events.load(butil::memory_order_acquire) == 0) {
            return;
        }
        uint32_t events = _received_events.exchange(0, butil::memory_order_acquire);
        if (events & GRANT) {
            _send._window = _send._capacity;
        }
        // Merged events may contain both soft and hard requests, even the value
        // 0x7. These are internal events: do not decode them again as
        // PURE_GRANT or validate them as one wire message. A soft request must
        // never downgrade a pending hard request.
        if (events & HARD_REQUEST) {
            _send._pending = HARD_REQUEST;
        } else if ((events & SOFT_REQUEST) && _send._pending == 0) {
            _send._pending = SOFT_REQUEST;
        }
    }

    bool NeedPureGrant() const {
        return hard_request_pending();
    }

    void PureGrantPosted() {
        DCHECK(hard_request_pending());
        _send._pending = 0;
    }

private:
    // Authoritative send state, serialized by _post_mutex. The receiver never
    // writes it.
    SendState _send;
    // Receiver-published events consumed in batches by the submitter. Kept
    // separate from copyable send snapshots. OR is sufficient because grants
    // restore capacity and pending requests coalesce with hard taking priority.
    // This is not an incremental-credit counter.
    butil::atomic<uint32_t> _received_events{0};
};

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_SRQ_FLOW_CONTROL_H
