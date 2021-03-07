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

#ifndef BRPC_POLICY_MONGO_PROTOCOL_H
#define BRPC_POLICY_MONGO_PROTOCOL_H

#include "brpc/input_messenger.h"
#include "brpc/mongo.h"
#include "brpc/protocol.h"

namespace brpc {
namespace policy {

struct MongoReply {
  int32_t response_flags;
  int64_t cursorid;
  int32_t straring_from;
  int32_t number_returned;
  std::vector<BsonPtr> documents;
};

struct DocumentSequence {
  int32_t size;
  std::string identifier;
  std::vector<BsonPtr> documents;
};

typedef std::shared_ptr<DocumentSequence> DocumentSequencePtr;

struct Section {
  uint8_t type;
  BsonPtr body_document;
  DocumentSequencePtr document_sequence;
};

struct MongoMsg {
  uint32_t flagbits;
  std::vector<Section> sections;
  uint32_t checksum;

  void make_host_endian() {
    if (!ARCH_CPU_LITTLE_ENDIAN) {
        flagbits = butil::ByteSwap(flagbits);
        checksum = butil::ByteSwap(checksum);
    }
  }

  bool checksumPresent() {
    return flagbits & 0x00000001;
  }
};

struct MongoInputResponse : public InputMessageBase {
  int32_t opcode;
  MongoReply reply;
  MongoMsg msg;

  // @InputMessageBase
  void DestroyImpl() {
      delete this;
  }
};

// Parse binary format of mongo
ParseResult ParseMongoMessage(butil::IOBuf* source, Socket* socket,
                              bool read_eof, const void* arg);

// Actions to a (client) request in mongo format
void ProcessMongoRequest(InputMessageBase* msg);

// Actions to a server response in mongo format
void ProcessMongoResponse(InputMessageBase* msg_base);

// Serialize query request
void SerializeMongoQueryRequest(butil::IOBuf* request_buf, Controller* cntl,
                                const MongoQueryRequest* request);

// Serialize request into request_buf
void SerializeMongoRequest(butil::IOBuf* request_buf, Controller* cntl,
                           const google::protobuf::Message* request);

// Pack request_buf into msg, call after serialize
void PackMongoRequest(butil::IOBuf* msg, SocketMessage** user_message_out,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller, const butil::IOBuf& request_buf,
                      const Authenticator* auth);

// Parse Mongo Sections
bool ParseMongoSection(butil::IOBuf* source, Section *section);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MONGO_PROTOCOL_H
