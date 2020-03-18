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


#ifndef BRPC_PROTOCOL_H
#define BRPC_PROTOCOL_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include <vector>                                  // std::vector
#include <stdint.h>                                // uint64_t
#include <gflags/gflags_declare.h>                 // DECLARE_xxx
#include "butil/endpoint.h"                         // butil::EndPoint
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "brpc/options.pb.h"                  // ProtocolType
#include "brpc/socket_id.h"                   // SocketId
#include "brpc/parse_result.h"                // ParseResult
#include "brpc/adaptive_connection_type.h"
#include "brpc/adaptive_protocol_type.h"

namespace google {
namespace protobuf {
class Message;
class MethodDescriptor;
}  // namespace protobuf
}  // namespace google

namespace butil {
class IOBuf;
}


namespace brpc {
class Socket;
class SocketMessage;
class Controller;
class Authenticator;
class InputMessageBase;

DECLARE_uint64(max_body_size);
DECLARE_bool(log_error_text);

// 3 steps to add a new Protocol:
// Step1: Add a new ProtocolType in src/brpc/options.proto
//        as identifier of the Protocol.
// Step2: Implement callbacks of struct `Protocol' in policy/ directory.
// Step3: Register the protocol in global.cpp using `RegisterProtocol'

struct Protocol {
    // [Required by both client and server]
    // The callback to cut a message from `source'.
    // Returned message will be passed to process_request and process_response
    // later and Destroy()-ed by InputMessenger.
    // Returns:
    //   MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA):
    //     `source' does not form a complete message yet.
    //   MakeParseError(PARSE_ERROR_TRY_OTHERS).
    //     `source' does not fit the protocol, the data should be tried by
    //     other protocols. If the data is definitely corrupted (e.g. magic 
    //     header matches but other fields are wrong), pop corrupted part
    //     from `source' before returning.
    //  MakeMessage(InputMessageBase*):
    //     The message is parsed successfully and cut from `source'.
    typedef ParseResult (*Parse)(butil::IOBuf* source, Socket *socket,
                                 bool read_eof, const void *arg);
    Parse parse;

    // [Required by client]
    // The callback to serialize `request' into `request_buf' which will be
    // packed into message by pack_request later. Called once for each RPC.
    // `cntl' provides additional data needed by some protocol (say HTTP).
    // Call cntl->SetFailed() on error.
    typedef void (*SerializeRequest)(
        butil::IOBuf* request_buf,
        Controller* cntl,
        const google::protobuf::Message* request);
    SerializeRequest serialize_request;
    
    // [Required by client]
    // The callback to pack `request_buf' into `iobuf_out' or `user_message_out'
    // Called before sending each request (including retries).
    // Remember to pack authentication information when `auth' is not NULL.
    // Call cntl->SetFailed() on error.
    typedef void (*PackRequest)(
        butil::IOBuf* iobuf_out,
        SocketMessage** user_message_out,
        uint64_t correlation_id,
        const google::protobuf::MethodDescriptor* method,
        Controller* controller,
        const butil::IOBuf& request_buf,
        const Authenticator* auth);
    PackRequest pack_request;

    // [Required by server]
    // The callback to handle request `msg' created by a successful parse().
    // `msg' must be Destroy()-ed when the processing is done. To make sure
    // Destroy() is always called, consider using DestroyingPtr<> defined in
    // destroyable.h
    // May be called in a different thread from parse().
    typedef void (*ProcessRequest)(InputMessageBase* msg);
    ProcessRequest process_request;

    // [Required by client]
    // The callback to handle response `msg' created by a successful parse().
    // `msg' must be Destroy()-ed when the processing is done. To make sure
    // Destroy() is always called, consider using DestroyingPtr<> defined in
    // destroyable.h
    // May be called in a different thread from parse().
    typedef void (*ProcessResponse)(InputMessageBase* msg);
    ProcessResponse process_response;

    // [Required by authenticating server]
    // The callback to verify authentication of this socket. Only called
    // on the first message that a socket receives. Can be NULL when 
    // authentication is not needed or this is the client side.
    // Returns true on successful authentication.
    typedef bool (*Verify)(const InputMessageBase* msg);
    Verify verify;

    // [Optional]
    // Convert `server_addr_and_port'(a parameter to Channel) to butil::EndPoint.
    typedef bool (*ParseServerAddress)(butil::EndPoint* out,
                                       const char* server_addr_and_port);
    ParseServerAddress parse_server_address;

    // [Optional] Customize method name.
    typedef const std::string& (*GetMethodName)(
        const google::protobuf::MethodDescriptor* method,
        const Controller*);
    GetMethodName get_method_name;

    // Bitwise-or of supported ConnectionType
    ConnectionType supported_connection_type;

    // Name of this protocol, must be string constant.
    const char* name;

    // True if this protocol is supported at client-side.
    bool support_client() const {
        return serialize_request && pack_request && process_response;
    }
    // True if this protocol is supported at server-side.
    bool support_server() const { return process_request; }
};

const ConnectionType CONNECTION_TYPE_POOLED_AND_SHORT =
    (ConnectionType)((int)CONNECTION_TYPE_POOLED |
                     (int)CONNECTION_TYPE_SHORT);

const ConnectionType CONNECTION_TYPE_ALL =
    (ConnectionType)((int)CONNECTION_TYPE_SINGLE |
                     (int)CONNECTION_TYPE_POOLED |
                     (int)CONNECTION_TYPE_SHORT);

// [thread-safe] 
// Register `protocol' using key=`type'. 
// Returns 0 on success, -1 otherwise
int RegisterProtocol(ProtocolType type, const Protocol& protocol);

// [thread-safe]
// Find the protocol registered with key=`type'.
// Returns NULL on not found.
const Protocol* FindProtocol(ProtocolType type);

// [thread-safe]
// List all registered protocols into `vec'.
void ListProtocols(std::vector<Protocol>* vec);
void ListProtocols(std::vector<std::pair<ProtocolType, Protocol> >* vec);

// The common serialize_request implementation used by many protocols.
void SerializeRequestDefault(butil::IOBuf* buf,
                             Controller* cntl,
                             const google::protobuf::Message* request);

// Replacements for msg->ParseFromXXX() to make the bytes limit in pb
// consistent with -max_body_size
bool ParsePbFromZeroCopyStream(google::protobuf::Message* msg,
                               google::protobuf::io::ZeroCopyInputStream* input);
bool ParsePbFromIOBuf(google::protobuf::Message* msg, const butil::IOBuf& buf);
bool ParsePbFromArray(google::protobuf::Message* msg, const void* data, size_t size);
bool ParsePbFromString(google::protobuf::Message* msg, const std::string& str);

// Deleter for unique_ptr to print error_text of the controller when
// -log_error_text is on, then delete the controller if `delete_cntl' is true
class LogErrorTextAndDelete {
public:
    explicit LogErrorTextAndDelete(bool delete_cntl = true)
        : _delete_cntl(delete_cntl) {}
    void operator()(Controller* c) const;
private:
    bool _delete_cntl;
};

// Utility to build a temporary array.
// Example:
//   TemporaryArrayBuilder<Foo, 5> b;
//   b.push() = Foo1;
//   b.push() = Foo2;
//   UseArray(b.raw_array(), b.size());
template <typename T, size_t N>
class TemporaryArrayBuilder {
public:
    TemporaryArrayBuilder() : _size(0) {}
    T& push() {
        if (_size < N) {
            return _arr[_size++];
        } else {
            CHECK(false) << "push to a full array, cap=" << N;
            static T dummy;
            return dummy;
        }
    }
    T& operator[](size_t i) { return _arr[i]; }
    size_t size() const { return _size; }
    T* raw_array() { return _arr; }
private:
    size_t _size;
    T _arr[N];
};

} // namespace brpc


#endif // BRPC_PROTOCOL_H
