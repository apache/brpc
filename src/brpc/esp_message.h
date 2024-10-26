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

#ifndef BRPC_ESP_MESSAGE_H
#define BRPC_ESP_MESSAGE_H

#include "brpc/esp_head.h"
#include "brpc/nonreflectable_message.h"
#include "butil/iobuf.h"

namespace brpc {

class EspMessage : public NonreflectableMessage<EspMessage> {
public:
    EspHead head;
    butil::IOBuf body;

public:
    EspMessage();
    ~EspMessage() override;

    void Swap(EspMessage* other);

    // implements Message ----------------------------------------------

    void MergeFrom(const EspMessage& from) override;
    void Clear() override;

    size_t ByteSizeLong() const override;
    int GetCachedSize() const PB_425_OVERRIDE { return ByteSize(); }

    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

private:
    void SharedCtor();
    void SharedDtor();
};

} // namespace brpc

#endif  // BRPC_ESP_MESSAGE_H
