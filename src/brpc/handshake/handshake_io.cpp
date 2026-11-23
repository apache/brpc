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

#include "brpc/handshake/handshake_io.h"

#include <algorithm>
#include <cstdint>
#include <errno.h>
#include <unistd.h>

#include "bthread/butex.h"
#include "butil/time.h"
#include "brpc/errno.pb.h"
#include "brpc/socket.h"

namespace brpc {
namespace handshake {

static const int WAIT_TIMEOUT_MS = 50;

SocketHandshakeIO::SocketHandshakeIO(Socket* socket)
    : _socket(socket) {}

SocketHandshakeIO::~SocketHandshakeIO() {
    if (auto* butex = _read_butex.load(butil::memory_order_relaxed)) {
        bthread::butex_destroy(butex);
    }
}

void SocketHandshakeIO::Reset(Socket* socket) {
    _socket = socket;
}

void SocketHandshakeIO::NotifyReadable() {
    if (auto* butex = _read_butex.load(butil::memory_order_acquire)) {
        butex->fetch_add(1, butil::memory_order_release);
        bthread::butex_wake(butex);
    }
}

int SocketHandshakeIO::GetWaitDeadline(timespec* deadline) const {
    if (_socket->Failed()) {
        errno = EFAILEDSOCKET;
        return -1;
    }
    const int64_t connect_deadline = _socket->_connect_deadline_us;
    const int64_t now = butil::gettimeofday_us();
    if (connect_deadline >= 0 && now >= connect_deadline) {
        errno = ETIMEDOUT;
        return -1;
    }
    const int64_t wait_until = now + WAIT_TIMEOUT_MS * 1000;
    *deadline = butil::microseconds_to_timespec(
        connect_deadline < 0 ? wait_until : std::min(wait_until, connect_deadline));
    return 0;
}

int SocketHandshakeIO::ReadExact(void* data, size_t len) {
    CHECK(data != nullptr);
    CHECK(_socket != nullptr);
    const int fd = _socket->fd();
    // Each handshake has a single reader. Notifiers may run concurrently.
    auto* butex = _read_butex.load(butil::memory_order_relaxed);
    if (butex == nullptr) {
        butex = bthread::butex_create_checked<butil::atomic<int>>();
        butex->store(0, butil::memory_order_relaxed);
        _read_butex.store(butex, butil::memory_order_release);
    }
    size_t received = 0;
    while (received < len) {
        const int expected = butex->load(butil::memory_order_acquire);
        timespec duetime;
        if (GetWaitDeadline(&duetime) != 0) {
            return -1;
        }
        const ssize_t nr = read(
            fd, static_cast<uint8_t*>(data) + received, len - received);
        if (nr < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                return -1;
            }
            if (bthread::butex_wait(butex, expected, &duetime) < 0 &&
                errno != EWOULDBLOCK && errno != ETIMEDOUT) {
                return -1;
            }
        } else if (nr == 0) {
            errno = EEOF;
            return -1;
        } else {
            received += nr;
        }
    }
    return 0;
}

int SocketHandshakeIO::WriteAll(const void* data, size_t len) {
    CHECK(data != nullptr);
    CHECK(_socket != nullptr);
    if (len != 0 && _socket->is_ssl()) {
        butil::IOBuf frame;
        if (frame.append(data, len) != 0) {
            return -1;
        }
        return _socket->Write(&frame);
    }
    const int fd = _socket->fd();
    size_t written = 0;
    while (written < len) {
        timespec duetime;
        if (GetWaitDeadline(&duetime) != 0) {
            return -1;
        }
        const ssize_t nw = write(
            fd, static_cast<const uint8_t*>(data) + written, len - written);
        if (nw > 0) {
            written += nw;
            continue;
        }
        if (nw == 0) {
            errno = EPIPE;
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        if (_socket->WaitEpollOut(fd, true, &duetime, /*check_failed=*/true) < 0 &&
            errno != ETIMEDOUT) {
            if (_socket->Failed()) {
                errno = EFAILEDSOCKET;
            }
            return -1;
        }
    }
    return 0;
}

int SocketHandshakeIO::PushBack(const void* data, size_t len) {
    CHECK(_socket != nullptr);
    if (len != 0) {
        butil::IOBuf restored;
        if (restored.append(data, len) != 0) {
            return -1;
        }
        auto& input = _socket->fd_input_processor().read_buf();
        restored.append(input);
        input.swap(restored);
    }
    return 0;
}

}  // namespace handshake
}  // namespace brpc
