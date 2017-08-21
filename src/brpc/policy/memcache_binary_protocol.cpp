// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Jun  5 18:25:40 CST 2015

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>
#include "base/logging.h"                       // LOG()
#include "base/time.h"
#include "base/iobuf.h"                         // base::IOBuf
#include "base/sys_byteorder.h"
#include "brpc/controller.h"               // Controller
#include "brpc/details/controller_private_accessor.h"
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/compress.h"                 // ParseFromCompressedData
#include "brpc/policy/memcache_binary_protocol.h"
#include "brpc/policy/memcache_binary_header.h"
#include "brpc/memcache.h"
#include "brpc/policy/most_common_message.h"
#include "base/containers/flat_map.h"


namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

BAIDU_CASSERT(sizeof(MemcacheRequestHeader) == 24, must_match);
BAIDU_CASSERT(sizeof(MemcacheResponseHeader) == 24, must_match);

static uint64_t supported_cmd_map[8];
static pthread_once_t supported_cmd_map_once = PTHREAD_ONCE_INIT;

static void InitSupportedCommandMap() {
    base::bit_array_clear(supported_cmd_map, 256);
    base::bit_array_set(supported_cmd_map, MC_BINARY_GET);
    base::bit_array_set(supported_cmd_map, MC_BINARY_SET);
    base::bit_array_set(supported_cmd_map, MC_BINARY_ADD);
    base::bit_array_set(supported_cmd_map, MC_BINARY_REPLACE);
    base::bit_array_set(supported_cmd_map, MC_BINARY_DELETE);
    base::bit_array_set(supported_cmd_map, MC_BINARY_INCREMENT);
    base::bit_array_set(supported_cmd_map, MC_BINARY_DECREMENT);
    base::bit_array_set(supported_cmd_map, MC_BINARY_FLUSH);
    base::bit_array_set(supported_cmd_map, MC_BINARY_VERSION);
    base::bit_array_set(supported_cmd_map, MC_BINARY_NOOP);
    base::bit_array_set(supported_cmd_map, MC_BINARY_APPEND);
    base::bit_array_set(supported_cmd_map, MC_BINARY_PREPEND);
    base::bit_array_set(supported_cmd_map, MC_BINARY_STAT);
    base::bit_array_set(supported_cmd_map, MC_BINARY_TOUCH);
}

inline bool IsSupportedCommand(uint8_t command) {
    pthread_once(&supported_cmd_map_once, InitSupportedCommandMap);
    return base::bit_array_get(supported_cmd_map, command);
}

ParseResult ParseMemcacheMessage(base::IOBuf* source,
                                 Socket* socket, bool /*read_eof*/, const void */*arg*/) {
    while (1) {
        const uint8_t* p_mcmagic = (const uint8_t*)source->fetch1();
        if (NULL == p_mcmagic) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }            
        if (*p_mcmagic != (uint8_t)MC_MAGIC_RESPONSE) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        char buf[24];
        const uint8_t* p = (const uint8_t*)source->fetch(buf, sizeof(buf));
        if (NULL == p) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        const MemcacheResponseHeader* header = (const MemcacheResponseHeader*)p;
        uint32_t total_body_length = base::NetToHost32(header->total_body_length);
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
        const MemcacheResponseHeader local_header = {
            header->magic,
            header->command,
            base::NetToHost16(header->key_length),
            header->extras_length,
            header->data_type,
            base::NetToHost16(header->status),
            total_body_length,
            base::NetToHost32(header->opaque),
            base::NetToHost64(header->cas_value),
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

void ProcessMemcacheResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = base::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));

    const bthread_id_t cid = msg->pi.id_wait;
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(FATAL, rc != EINVAL) << "Fail to lock correlation_id="
                                    << cid.value << ": " << berror(rc);
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
    } else if (cntl->response()->GetDescriptor() != MemcacheResponse::descriptor()) {
        cntl->SetFailed(ERESPONSE, "Must be MemcacheResponse");
    } else {
        // We work around ParseFrom of pb which is just a placeholder.
        ((MemcacheResponse*)cntl->response())->raw_buffer() = msg->meta.movable();
        if (msg->pi.count != accessor.pipelined_count()) {
            cntl->SetFailed(ERESPONSE, "pipelined_count=%d of response does "
                                  "not equal request's=%d",
                                  msg->pi.count, accessor.pipelined_count());
        }
    }
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void SerializeMemcacheRequest(base::IOBuf* buf,
                              Controller* cntl,
                              const google::protobuf::Message* request) {
    if (request == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    if (request->GetDescriptor() != MemcacheRequest::descriptor()) {
        return cntl->SetFailed(EREQUEST, "Must be MemcacheRequest");
    }
    const MemcacheRequest* mr = (const MemcacheRequest*)request;
    // We work around SerializeTo of pb which is just a placeholder.
    *buf = mr->raw_buffer();
    ControllerPrivateAccessor(cntl).set_pipelined_count(mr->pipelined_count());
}

void PackMemcacheRequest(base::IOBuf* buf,
                         SocketMessage**,
                         uint64_t /*correlation_id*/,
                         const google::protobuf::MethodDescriptor*,
                         Controller*,
                         const base::IOBuf& request,
                         const Authenticator* /*auth*/) {
    buf->append(request);
}

const std::string& GetMemcacheMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*) {
    const static std::string MEMCACHED_STR = "memcached";
    return MEMCACHED_STR;
}

}  // namespace policy
} // namespace brpc

