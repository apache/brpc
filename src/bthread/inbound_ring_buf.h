/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#include <cstdint>

namespace brpc {
class Socket;
}

struct InboundRingBuf {
  InboundRingBuf() = default;

  InboundRingBuf(brpc::Socket *sock, int32_t bytes, uint16_t bid,
                 bool rearm = false)
      : sock_(sock), bytes_(bytes), buf_id_(bid), need_rearm_(rearm) {}

  InboundRingBuf(InboundRingBuf &&rhs)
      : sock_(rhs.sock_), bytes_(rhs.bytes_), buf_id_(rhs.buf_id_),
        need_rearm_(rhs.need_rearm_) {}

  InboundRingBuf &operator=(InboundRingBuf &&rhs) {
    if (this == &rhs) {
      return *this;
    }
    sock_ = rhs.sock_;
    bytes_ = rhs.bytes_;
    buf_id_ = rhs.buf_id_;
    need_rearm_ = rhs.need_rearm_;
    return *this;
  }

  brpc::Socket *sock_{nullptr};
  int32_t bytes_{0};
  uint16_t buf_id_{UINT16_MAX};
  bool need_rearm_{false};
};