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

#include "brpc/policy/couchbase_protocol.h"

#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>  // MethodDescriptor
#include <google/protobuf/message.h>     // Message

#include "brpc/compress.h"    // ParseFromCompressedData
#include "brpc/controller.h"  // Controller
#include "brpc/couchbase.h"   // CouchbaseRequest, CouchbaseResponse
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/server.h"  // Server
#include "brpc/socket.h"  // Socket
#include "brpc/span.h"
#include "butil/containers/flat_map.h"
#include "butil/iobuf.h"    // butil::IOBuf
#include "butil/logging.h"  // LOG()
#include "butil/sys_byteorder.h"
#include "butil/time.h"

namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

BAIDU_CASSERT(sizeof(CouchbaseRequestHeader) == 24, must_match);
BAIDU_CASSERT(sizeof(CouchbaseResponseHeader) == 24, must_match);

static uint64_t supported_cmd_map[8];
static pthread_once_t supported_cmd_map_once = PTHREAD_ONCE_INIT;

static void InitSupportedCommandMap() {
  butil::bit_array_clear(supported_cmd_map, 256);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_GET);
  butil::bit_array_set(supported_cmd_map, CB_HELLO_SELECT_FEATURES);
  butil::bit_array_set(supported_cmd_map, CB_SELECT_BUCKET);
  butil::bit_array_set(supported_cmd_map, CB_GET_SCOPE_ID);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_SET);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_ADD);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_REPLACE);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_DELETE);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_INCREMENT);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_DECREMENT);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_FLUSH);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_VERSION);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_NOOP);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_APPEND);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_PREPEND);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_STAT);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_TOUCH);
  butil::bit_array_set(supported_cmd_map, CB_BINARY_SASL_AUTH);
  // Collection management commands
  butil::bit_array_set(supported_cmd_map, CB_GET_COLLECTIONS_MANIFEST);
  butil::bit_array_set(supported_cmd_map, CB_COLLECTIONS_GET_CID);
  butil::bit_array_set(supported_cmd_map, CB_COLLECTIONS_GET_SCOPE_ID);
}

inline bool IsSupportedCommand(uint8_t command) {
  pthread_once(&supported_cmd_map_once, InitSupportedCommandMap);
  return butil::bit_array_get(supported_cmd_map, command);
}

ParseResult ParseCouchbaseMessage(butil::IOBuf* source, Socket* socket,
                                  bool /*read_eof*/, const void* /*arg*/) {
  while (1) {
    const uint8_t* p_cbmagic = (const uint8_t*)source->fetch1();
    if (NULL == p_cbmagic) {
      return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (*p_cbmagic != (uint8_t)CB_MAGIC_RESPONSE) {
      return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    char buf[24];
    const uint8_t* p = (const uint8_t*)source->fetch(buf, sizeof(buf));
    if (NULL == p) {
      return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const CouchbaseResponseHeader* header = (const CouchbaseResponseHeader*)p;
    uint32_t total_body_length = butil::NetToHost32(header->total_body_length);
    if (source->size() < sizeof(*header) + total_body_length) {
      return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    if (!IsSupportedCommand(header->command)) {
      LOG(WARNING) << "Not support command=" << header->command;
      source->pop_front(sizeof(*header) + total_body_length);
      return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    PipelinedInfo pi;
    if (!socket->PopPipelinedInfo(&pi)) {
      LOG(WARNING) << "No corresponding PipelinedInfo in socket, drop";
      source->pop_front(sizeof(*header) + total_body_length);
      return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    MostCommonMessage* msg =
        static_cast<MostCommonMessage*>(socket->parsing_context());
    if (msg == NULL) {
      msg = MostCommonMessage::Get();
      socket->reset_parsing_context(msg);
    }

    // endianness conversions.
    const CouchbaseResponseHeader local_header = {
        header->magic,
        header->command,
        butil::NetToHost16(header->key_length),
        header->extras_length,
        header->data_type,
        butil::NetToHost16(header->status),
        total_body_length,
        butil::NetToHost32(header->opaque),
        butil::NetToHost64(header->cas_value),
    };
    msg->meta.append(&local_header, sizeof(local_header));
    source->pop_front(sizeof(*header));
    source->cutn(&msg->meta, total_body_length);
    if (++msg->pi.count >= pi.count) {
      CHECK_EQ(msg->pi.count, pi.count);
      msg = static_cast<MostCommonMessage*>(socket->release_parsing_context());
      msg->pi = pi;
      return MakeMessage(msg);
    } else {
      socket->GivebackPipelinedInfo(pi);
    }
  }
}

void ProcessCouchbaseResponse(InputMessageBase* msg_base) {
  const int64_t start_parse_us = butil::cpuwide_time_us();
  DestroyingPtr<MostCommonMessage> msg(
      static_cast<MostCommonMessage*>(msg_base));

  const bthread_id_t cid = msg->pi.id_wait;
  Controller* cntl = NULL;
  const int rc = bthread_id_lock(cid, (void**)&cntl);
  if (rc != 0) {
    LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
        << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
    return;
  }

  ControllerPrivateAccessor accessor(cntl);
  Span* span = accessor.span();
  if (span) {
    span->set_base_real_us(msg->base_real_us());
    span->set_received_us(msg->received_us());
    span->set_response_size(msg->meta.length());
    span->set_start_parse_us(start_parse_us);
  }
  const int saved_error = cntl->ErrorCode();
  if (cntl->response() == NULL) {
    cntl->SetFailed(ERESPONSE, "response is NULL!");
  } else if (cntl->response()->GetDescriptor() !=
             CouchbaseOperations::CouchbaseResponse::descriptor()) {
    cntl->SetFailed(ERESPONSE, "Must be CouchbaseResponse");
  } else {
    // We work around ParseFrom of pb which is just a placeholder.
    ((CouchbaseOperations::CouchbaseResponse*)cntl->response())->rawBuffer() =
        msg->meta.movable();
    if (msg->pi.count != accessor.pipelined_count()) {
      cntl->SetFailed(ERESPONSE,
                      "pipelined_count=%d of response does "
                      "not equal request's=%d",
                      msg->pi.count, accessor.pipelined_count());
    }
  }
  // Unlocks correlation_id inside. Revert controller's
  // error code if it version check of `cid' fails
  msg.reset();  // optional, just release resource ASAP
  accessor.OnResponse(cid, saved_error);
}

void SerializeCouchbaseRequest(butil::IOBuf* buf, Controller* cntl,
                               const google::protobuf::Message* request) {
  if (request == NULL) {
    return cntl->SetFailed(EREQUEST, "request is NULL");
  }
  if (request->GetDescriptor() !=
      CouchbaseOperations::CouchbaseRequest::descriptor()) {
    return cntl->SetFailed(EREQUEST, "Must be CouchbaseRequest");
  }
  const CouchbaseOperations::CouchbaseRequest* mr =
      (const CouchbaseOperations::CouchbaseRequest*)request;
  // We work around SerializeTo of pb which is just a placeholder.
  *buf = mr->rawBuffer();
  ControllerPrivateAccessor(cntl).set_pipelined_count(mr->pipelinedCount());
}

void PackCouchbaseRequest(butil::IOBuf* buf, SocketMessage**,
                          uint64_t /*correlation_id*/,
                          const google::protobuf::MethodDescriptor*,
                          Controller* cntl, const butil::IOBuf& request,
                          const Authenticator* auth) {
  if (auth) {
    std::string auth_str;
    if (auth->GenerateCredential(&auth_str) != 0) {
      return cntl->SetFailed(EREQUEST, "Fail to generate credential");
    }
    if (auth_str.empty()) {
      return cntl->SetFailed(EREQUEST, "Empty auth_str");
    }
    buf->append(auth_str);
    // pipelined_count();
  } else {
    buf->append(request);
  }
}

const std::string& GetCouchbaseMethodName(
    const google::protobuf::MethodDescriptor*, const Controller*) {
  const static std::string CouchbaseD_STR = "Couchbase";
  return CouchbaseD_STR;
}

}  // namespace policy
}  // namespace brpc
