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

#ifndef BRPC_HANDSHAKE_HANDSHAKE_IO_H
#define BRPC_HANDSHAKE_HANDSHAKE_IO_H

#include <cstddef>

#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"

namespace brpc {

class Socket;

namespace handshake {

// Blocking byte-stream interface used by client handshakes and by protocols
// whose server handshake still runs in a dedicated bthread.
class HandshakeIO {
public:
    virtual ~HandshakeIO() = default;

    virtual int ReadExact(void* data, size_t len) = 0;
    virtual int WriteAll(const void* data, size_t len) = 0;
    virtual int PushBack(const void* data, size_t len) = 0;
};

// Non-blocking input used by the standard InputMessenger parser path.
// Consume is called only after a complete frame has been validated.
class HandshakeInput {
public:
    virtual ~HandshakeInput() = default;

    virtual size_t Size() const = 0;
    virtual bool CopyTo(void* data, size_t len) const = 0;
    virtual bool Consume(size_t len) = 0;
};

class IOBufHandshakeInput : public HandshakeInput {
public:
    explicit IOBufHandshakeInput(butil::IOBuf* source) : _source(source) {}

    size_t Size() const override;
    bool CopyTo(void* data, size_t len) const override;
    bool Consume(size_t len) override;

private:
    butil::IOBuf* _source;
};

class SocketHandshakeIO : public HandshakeIO {
public:
    explicit SocketHandshakeIO(Socket* socket = nullptr);
    ~SocketHandshakeIO() override;

    void Reset(Socket* socket);
    void NotifyReadable();

    int ReadExact(void* data, size_t len) override;
    int WriteAll(const void* data, size_t len) override;
    int PushBack(const void* data, size_t len) override;

private:
    Socket* _socket;
    butil::atomic<int>* _read_butex;

    DISALLOW_COPY_AND_ASSIGN(SocketHandshakeIO);
};

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_HANDSHAKE_HANDSHAKE_IO_H
