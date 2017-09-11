// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BRPC_ESP_MESSAGE_H
#define BRPC_ESP_MESSAGE_H

#include <string>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>

#include "brpc/esp_head.h"
#include "butil/iobuf.h"       


namespace brpc {

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_esp_5fmessage_2eproto();
void protobuf_AssignDesc_esp_5fmessage_2eproto();
void protobuf_ShutdownFile_esp_5fmessage_2eproto();

class EspMessage : public ::google::protobuf::Message {
public:
    EspHead head;
    butil::IOBuf body;

public:
    EspMessage();
    virtual ~EspMessage();

    EspMessage(const EspMessage& from);

    inline EspMessage& operator=(const EspMessage& from) {
        CopyFrom(from);
        return *this;
    }

    static const ::google::protobuf::Descriptor* descriptor();
    static const EspMessage& default_instance();

    void Swap(EspMessage* other);

    // implements Message ----------------------------------------------

    EspMessage* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const EspMessage& from);
    void MergeFrom(const EspMessage& from);
    void Clear();
    bool IsInitialized() const;

    int ByteSize() const;
    bool MergePartialFromCodedStream(
            ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
            ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
            ::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return ByteSize(); }
    ::google::protobuf::Metadata GetMetadata() const;

private:
    void SharedCtor();
    void SharedDtor();
    friend void  protobuf_AddDesc_esp_5fmessage_2eproto();
    friend void protobuf_AssignDesc_esp_5fmessage_2eproto();
    friend void protobuf_ShutdownFile_esp_5fmessage_2eproto();

    void InitAsDefaultInstance();
    static EspMessage* default_instance_;
};

} // namespace brpc


#endif  // PROTOBUF_esp_5fmessage_2eproto__INCLUDED
