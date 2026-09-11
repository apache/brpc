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

#include <cstdint>
#include <errno.h>
#include <unistd.h>

#include "bthread/butex.h"
#include "butil/time.h"
#include "brpc/errno.pb.h"
#include "brpc/socket.h"

namespace brpc {
namespace handshake {

size_t IOBufHandshakeInput::Size() const {
    return _source != NULL ? _source->size() : 0;
}

bool IOBufHandshakeInput::CopyTo(void* data, size_t len) const {
    return _source != NULL && _source->copy_to(data, len) == len;
}

bool IOBufHandshakeInput::Consume(size_t len) {
    return _source != NULL && _source->pop_front(len) == len;
}

static const int WAIT_TIMEOUT_MS = 50;

SocketHandshakeIO::SocketHandshakeIO(Socket* socket)
    : _socket(socket)
    , _read_butex(bthread::butex_create_checked<butil::atomic<int> >()) {
}

SocketHandshakeIO::~SocketHandshakeIO() {
    bthread::butex_destroy(_read_butex);
}

void SocketHandshakeIO::Reset(Socket* socket) {
    _socket = socket;
}

void SocketHandshakeIO::NotifyReadable() {
    _read_butex->fetch_add(1, butil::memory_order_release);
    bthread::butex_wake(_read_butex);
}

template <typename ReadOnce>
static int ReadExactLoop(butil::atomic<int>* read_butex,
                         size_t len, ReadOnce read_once) {
    size_t received = 0;
    while (received < len) {
        const int expected = read_butex->load(butil::memory_order_acquire);
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        const ssize_t nr = read_once(received, len - received);
        if (nr < 0) {
            if (errno != EAGAIN) {
                return -1;
            }
            if (bthread::butex_wait(read_butex, expected, &duetime) < 0 &&
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

int SocketHandshakeIO::ReadExact(void* data, size_t len) {
    CHECK(data != NULL);
    CHECK(_socket != NULL);
    const int fd = _socket->fd();
    return ReadExactLoop(_read_butex, len,
        [data, fd](size_t offset, size_t remaining) {
            return read(fd, static_cast<uint8_t*>(data) + offset, remaining);
        });
}

template <typename WriteOnce, typename WaitWritable>
static int WriteAllLoop(size_t len, WriteOnce write_once,
                        WaitWritable wait_writable) {
    size_t written = 0;
    while (written < len) {
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        const ssize_t nw = write_once(written, len - written);
        if (nw > 0) {
            written += nw;
            continue;
        }
        if (nw == 0) {
            errno = EPIPE;
            return -1;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        if (wait_writable(&duetime) < 0 &&
            errno != ETIMEDOUT) {
            return -1;
        }
    }
    return 0;
}

int SocketHandshakeIO::WriteAll(const void* data, size_t len) {
    CHECK(data != NULL);
    CHECK(_socket != NULL);
    const int fd = _socket->fd();
    return WriteAllLoop(len,
        [data, fd](size_t offset, size_t remaining) {
            return write(fd, static_cast<const uint8_t*>(data) + offset,
                         remaining);
        },
        [this](const timespec* duetime) {
            return _socket->WaitEpollOut(
                _socket->fd(), true, duetime);
        });
}

int SocketHandshakeIO::PushBack(const void* data, size_t len) {
    CHECK(_socket != NULL);
    if (len != 0) {
        return _socket->fd_input_processor().read_buf().append(data, len) == 0 ? 0 : -1;
    }
    return 0;
}

}  // namespace handshake
}  // namespace brpc
