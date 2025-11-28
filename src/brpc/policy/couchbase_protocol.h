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

#ifndef BRPC_POLICY_COUCHBASE_BINARY_PROTOCOL_H
#define BRPC_POLICY_COUCHBASE_BINARY_PROTOCOL_H

#include "brpc/protocol.h"

namespace brpc {
namespace policy {

enum CouchbaseMagic { CB_MAGIC_REQUEST = 0x80, CB_MAGIC_RESPONSE = 0x81 };

// Definition of the data types in the packet
// https://github.com/couchbase/kv_engine/blob/master/docs/BinaryProtocol.md
enum CouchbaseBinaryDataType { CB_BINARY_RAW_BYTES = 0x00 };

enum CouchbaseJsonDataType { CB_JSON = 0x01 };

// Definition of the different command opcodes.
// https://github.com/couchbase/kv_engine/blob/master/docs/BinaryProtocol.md
enum CouchbaseBinaryCommand {
  CB_HELLO_SELECT_FEATURES = 0x1f,
  CB_SELECT_BUCKET = 0x89,
  CB_GET_SCOPE_ID = 0xBC,
  CB_BINARY_GET = 0x00,
  CB_BINARY_SET = 0x01,
  CB_BINARY_ADD = 0x02,
  CB_BINARY_REPLACE = 0x03,
  CB_BINARY_DELETE = 0x04,
  CB_BINARY_INCREMENT = 0x05,
  CB_BINARY_DECREMENT = 0x06,
  CB_BINARY_QUIT = 0x07,
  CB_BINARY_FLUSH = 0x08,
  CB_BINARY_GETQ = 0x09,
  CB_BINARY_NOOP = 0x0a,
  CB_BINARY_VERSION = 0x0b,
  CB_BINARY_GETK = 0x0c,
  CB_BINARY_GETKQ = 0x0d,
  CB_BINARY_APPEND = 0x0e,
  CB_BINARY_PREPEND = 0x0f,
  CB_BINARY_STAT = 0x10,
  CB_BINARY_SETQ = 0x11,
  CB_BINARY_ADDQ = 0x12,
  CB_BINARY_REPLACEQ = 0x13,
  CB_BINARY_DELETEQ = 0x14,
  CB_BINARY_INCREMENTQ = 0x15,
  CB_BINARY_DECREMENTQ = 0x16,
  CB_BINARY_QUITQ = 0x17,
  CB_BINARY_FLUSHQ = 0x18,
  CB_BINARY_APPENDQ = 0x19,
  CB_BINARY_PREPENDQ = 0x1a,
  CB_BINARY_TOUCH = 0x1c,
  CB_BINARY_GAT = 0x1d,
  CB_BINARY_GATQ = 0x1e,
  CB_BINARY_GATK = 0x23,
  CB_BINARY_GATKQ = 0x24,

  CB_BINARY_SASL_LIST_MECHS = 0x20,
  CB_BINARY_SASL_AUTH = 0x21,
  CB_BINARY_SASL_STEP = 0x22,

  // Collection Management Commands (Couchbase 7.0+)
  CB_GET_CLUSTER_CONFIG = 0xb5,
  CB_GET_COLLECTIONS_MANIFEST = 0xba,
  CB_COLLECTIONS_GET_CID = 0xbb,
  CB_COLLECTIONS_GET_SCOPE_ID = 0xbc,

};

struct CouchbaseRequestHeader {
  // Magic number identifying the package (See Couchbase Binary
  // Protocol#Magic_Byte)
  uint8_t magic;

  // Command code (See Couchbase Binary Protocol#Command_opcodes)
  uint8_t command;

  // Length in bytes of the text key that follows the command extras
  uint16_t key_length;

  // Length in bytes of the command extras
  uint8_t extras_length;

  // Reserved for future use (See Couchbase Binary Protocol#Data_Type)
  uint8_t data_type;

  // The virtual bucket for this command
  uint16_t vbucket_id;

  // Length in bytes of extra + key + value
  uint32_t total_body_length;

  // Will be copied back to you in the response
  uint32_t opaque;

  // Data version check
  uint64_t cas_value;
};

struct CouchbaseResponseHeader {
  // Magic number identifying the package (See Couchbase Binary
  // Protocol#Magic_Byte)
  uint8_t magic;

  // Command code (See Couchbase Binary Protocol#Command_opcodes)
  uint8_t command;

  // Length in bytes of the text key that follows the command extras
  uint16_t key_length;

  // Length in bytes of the command extras
  uint8_t extras_length;

  // Reserved for future use (See Couchbase Binary Protocol#Data_Type)
  uint8_t data_type;

  // Status of the response (non-zero on error)
  uint16_t status;

  // Length in bytes of extra + key + value
  uint32_t total_body_length;

  // Will be copied back to you in the response
  uint32_t opaque;

  // Data version check
  uint64_t cas_value;
};

// Parse couchbase messages.
ParseResult ParseCouchbaseMessage(butil::IOBuf* source, Socket* socket,
                                  bool read_eof, const void* arg);

// Actions to a couchbase response.
void ProcessCouchbaseResponse(InputMessageBase* msg);

// Serialize a couchbase request.
void SerializeCouchbaseRequest(butil::IOBuf* buf, Controller* cntl,
                               const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackCouchbaseRequest(butil::IOBuf* buf, SocketMessage**,
                          uint64_t correlation_id,
                          const google::protobuf::MethodDescriptor* method,
                          Controller* controller, const butil::IOBuf& request,
                          const Authenticator* auth);

// process couchbase request.
// since, there is no server side instance running, this function is not
// implemented. void ProcessCouchbaseRequest(InputMessageBase* msg);

const std::string& GetCouchbaseMethodName(
    const google::protobuf::MethodDescriptor*, const Controller*);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_COUCHBASE_BINARY_PROTOCOL_H
