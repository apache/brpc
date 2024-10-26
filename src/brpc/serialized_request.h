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


#ifndef BRPC_SERIALIZED_REQUEST_H
#define BRPC_SERIALIZED_REQUEST_H

#include "brpc/nonreflectable_message.h"
#include "brpc/pb_compat.h"
#include "butil/iobuf.h"

namespace brpc {

class SerializedRequest : public NonreflectableMessage<SerializedRequest> {
public:
    SerializedRequest();
    ~SerializedRequest() override;

    SerializedRequest(const SerializedRequest& from);

    inline SerializedRequest& operator=(const SerializedRequest& from) {
        CopyFrom(from);
        return *this;
    }

    void Swap(SerializedRequest* other);

    void MergeFrom(const SerializedRequest& from) override;

    // implements Message ----------------------------------------------
    void Clear() override;
    size_t ByteSizeLong() const override;
    int GetCachedSize() const PB_425_OVERRIDE { return ByteSize(); }
    butil::IOBuf& serialized_data() { return _serialized; }
    const butil::IOBuf& serialized_data() const { return _serialized; }

    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

private:
    void SharedCtor();
    void SharedDtor();

private:
    butil::IOBuf _serialized;
};

} // namespace brpc

#endif  // BRPC_SERIALIZED_REQUEST_H
