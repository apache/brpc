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


#ifndef BRPC_NSHEAD_MESSAGE_H
#define BRPC_NSHEAD_MESSAGE_H

#include "brpc/nonreflectable_message.h"
#include "brpc/nshead.h" // nshead_t
#include "brpc/pb_compat.h"
#include "butil/iobuf.h" // IOBuf

namespace brpc {

// Representing a nshead request or response.
class NsheadMessage : public NonreflectableMessage<NsheadMessage> {
public:
    nshead_t head;
    butil::IOBuf body;
    
public:
    NsheadMessage();
    ~NsheadMessage() override;

    NsheadMessage(const NsheadMessage& from);

    inline NsheadMessage& operator=(const NsheadMessage& from) {
        CopyFrom(from);
        return *this;
    }

    void Swap(NsheadMessage* other);

    // implements Message ----------------------------------------------
    void MergeFrom(const ::google::protobuf::Message& from) PB_526_OVERRIDE;
    void MergeFrom(const NsheadMessage& from) override;
    void Clear() override;

    size_t ByteSizeLong() const override;
    int GetCachedSize() const PB_425_OVERRIDE { return ByteSize(); }

    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

private:
    void SharedCtor();
    void SharedDtor();
};

} // namespace brpc


#endif  // BRPC_NSHEAD_MESSAGE_H
