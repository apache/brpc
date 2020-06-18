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


#ifndef BRPC_REDIS_H
#define BRPC_REDIS_H

#include <google/protobuf/message.h>
#include <unordered_map>
#include <memory>
#include <list>
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "butil/arena.h"
#include "brpc/proto_base.pb.h"
#include "brpc/redis_reply.h"
#include "brpc/parse_result.h"
#include "brpc/callback.h"
#include "brpc/socket.h"
#include "brpc/controller.h"

namespace brpc {

// Request to redis.
// Notice that you can pipeline multiple commands in one request and sent
// them to ONE redis-server together.
// Example:
//   RedisRequest request;
//   request.AddCommand("PING");
//   RedisResponse response;
//   channel.CallMethod(&controller, &request, &response, NULL/*done*/);
//   if (!cntl.Failed()) {
//       LOG(INFO) << response.reply(0);
//   }
class RedisRequest : public ::google::protobuf::Message {
public:
    RedisRequest();
    virtual ~RedisRequest();
    RedisRequest(const RedisRequest& from);
    inline RedisRequest& operator=(const RedisRequest& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(RedisRequest* other);

    // Add a command with a va_list to this request. The conversion
    // specifiers are compatible with the ones used by hiredis, namely except
    // that %b stands for binary data, other specifiers are similar with printf.
    bool AddCommandV(const char* fmt, va_list args);

    // Concatenate components into a redis command, similarly with
    // redisCommandArgv() in hiredis.
    // Example:
    //   butil::StringPiece components[] = { "set", "key", "value" };
    //   request.AddCommandByComponents(components, arraysize(components));
    bool AddCommandByComponents(const butil::StringPiece* components, size_t n);
    
    // Add a command with variadic args to this request.
    // The reason that adding so many overloads rather than using ... is that
    // it's the only way to dispatch the AddCommand w/o args differently.
    bool AddCommand(const butil::StringPiece& command);
    
    template <typename A1>
    bool AddCommand(const char* format, A1 a1)
    { return AddCommandWithArgs(format, a1); }
    
    template <typename A1, typename A2>
    bool AddCommand(const char* format, A1 a1, A2 a2)
    { return AddCommandWithArgs(format, a1, a2); }
    
    template <typename A1, typename A2, typename A3>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3)
    { return AddCommandWithArgs(format, a1, a2, a3); }
    
    template <typename A1, typename A2, typename A3, typename A4>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4)
    { return AddCommandWithArgs(format, a1, a2, a3, a4); }
    
    template <typename A1, typename A2, typename A3, typename A4, typename A5>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    { return AddCommandWithArgs(format, a1, a2, a3, a4, a5); }

    template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6)
    { return AddCommandWithArgs(format, a1, a2, a3, a4, a5, a6); }

    // Number of successfully added commands
    int command_size() const { return _ncommand; }

    // True if previous AddCommand[V] failed.
    bool has_error() const { return _has_error; }

    // Serialize the request into `buf'. Return true on success.
    bool SerializeTo(butil::IOBuf* buf) const;

    // Protobuf methods.
    RedisRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const RedisRequest& from);
    void MergeFrom(const RedisRequest& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return _cached_size_; }

    static const ::google::protobuf::Descriptor* descriptor();
    
    void Print(std::ostream&) const;

protected:
    ::google::protobuf::Metadata GetMetadata() const override;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;
    bool AddCommandWithArgs(const char* fmt, ...);

    int _ncommand;    // # of valid commands
    bool _has_error;  // previous AddCommand had error
    butil::IOBuf _buf;  // the serialized request.
    mutable int _cached_size_;  // ByteSize
};

// Response from Redis.
// Notice that a RedisResponse instance may contain multiple replies
// due to pipelining.
class RedisResponse : public ::google::protobuf::Message {
public:
    RedisResponse();
    virtual ~RedisResponse();
    RedisResponse(const RedisResponse& from);
    inline RedisResponse& operator=(const RedisResponse& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(RedisResponse* other);

    // Number of replies in this response.
    // (May have more than one reply due to pipeline)
    int reply_size() const { return _nreply; }

    // Get index-th reply. If index is out-of-bound, nil reply is returned.
    const RedisReply& reply(int index) const {
        if (index < reply_size()) {
            return (index == 0 ? _first_reply : _other_replies[index - 1]);
        }
        static RedisReply redis_nil(NULL);
        return redis_nil;
    }

    // Parse and consume intact replies from the buf.
    // Returns PARSE_OK on success.
    // Returns PARSE_ERROR_NOT_ENOUGH_DATA if data in `buf' is not enough to parse.
    // Returns PARSE_ERROR_ABSOLUTELY_WRONG if the parsing failed.
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf, int reply_count);
    
    // implements Message ----------------------------------------------
  
    RedisResponse* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const RedisResponse& from);
    void MergeFrom(const RedisResponse& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return _cached_size_; }

    static const ::google::protobuf::Descriptor* descriptor();

protected:
    ::google::protobuf::Metadata GetMetadata() const override;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    RedisReply _first_reply;
    RedisReply* _other_replies;
    butil::Arena _arena;
    int _nreply;
    mutable int _cached_size_;
};

std::ostream& operator<<(std::ostream& os, const RedisRequest&);
std::ostream& operator<<(std::ostream& os, const RedisResponse&);

class RedisCommandHandler;

// Container of CommandHandlers.
// Assign an instance to ServerOption.redis_service to enable redis support. 
class RedisService {
public:
    virtual ~RedisService() {}
    
    // Call this function to register `handler' that can handle command `name'.
    // `name' is case insensitive.
    bool AddCommandHandler(const std::string& name, RedisCommandHandler* handler);

    // This function should not be touched by user and used by brpc deverloper only.
    RedisCommandHandler* FindCommandHandler(const butil::StringPiece& name) const;

private:
    typedef std::unordered_map<std::string, RedisCommandHandler*> CommandMap;
    CommandMap _command_map;
};

// The base class of waitable object returned by RedisCommandHandler if user want to
// implement asynchronous processing.
class Waitable {
public:
    virtual ~Waitable() {}

    // Wait() would be blocked until Notify() is called.
    virtual void Notify() = 0;
    virtual void Wait() = 0;
};

// A Waitable implementation by condition variable.
class ConditionWaitable : public Waitable {
public:
    ConditionWaitable();
    ~ConditionWaitable();

    void Notify() override;
    void Wait() override;

private:
    bthread_cond_t cond;
    bthread_mutex_t mutex;
    bool notified;
};

// A Waitable implementation by joining call id of rpc.
class RPCWaitable: public Waitable {
public:
    RPCWaitable(CallId call_id);
    ~RPCWaitable();

    // Note that Notify() could not be called after rpc is done since call_id is
    // automatically joinable.
    void Notify() override;
    void Wait() override;

private:
    CallId _call_id;
};

enum RedisCommandHandlerState {
    REDIS_CMD_HANDLED = 0,
    REDIS_CMD_CONTINUE = 1,
    REDIS_CMD_WAIT = 2,
};

// The result return by RedisCommandHandler.
// if the return state is REDIS_CMD_HANDLED or REDIS_CMD_CONTINUE, waitobj should be null.
// if the return state is REDIS_CMD_WAIT, waitobj should be set correspondingly and brpc
// would would own the waitobj.
struct RedisCommandHandlerResult {
    RedisCommandHandlerResult()
        : state(REDIS_CMD_HANDLED)
        , waitobj(NULL) {}

    RedisCommandHandlerState state;
    Waitable* waitobj;
};

// The Command handler for a redis request. User should impletement Run().
class RedisCommandHandler {
public:
    virtual ~RedisCommandHandler() {}

    // Once Server receives commands, it will first find the corresponding handlers and
    // call them sequentially(one by one) according to the order that requests arrive,
    // just like what redis-server does.
    // `args' is the array of request command. For example, "set somekey somevalue"
    // corresponds to args[0]=="set", args[1]=="somekey" and args[2]=="somevalue".
    // `output', which should be filled by user, is the content that sent to client side.
    // Read brpc/src/redis_reply.h for more usage.
    //
    // `last_of_batch' indicates whether it is the last message of current batch. If
    // user want to do some asynchronous processing during the batch, user should start
    // the execution and return REDIS_CMD_WAIT along with a brpc::Waitable object.
    // The framework provides two options: ConditionWaitable and RPCWaitable(read more
    // at the definition of these classes).
    // Usage example:
    //
    // RedisCommandHandlerResult Run(args, output, last_of_batch) {
    //      if (!last_of_batch) {
    //          ...
    //          RedisCommandHandlerResult result;
    //          result.state = REDIS_CMD_WAIT;
    //          brpc::ConditionWaitable* waitable = new brpc::ConditionWaitable;
    //          // start execution...
    //          // Remember to call waitable->Notify() when the execution is done.
    //          result.waitobj = waitable;
    //          return result;
    //      }
    //      ...
    // }
    // or 
    // RedisCommandHandlerResult Run(args, output, last_of_batch) {
    //      if (!last_of_batch) {
    //          ...
    //          RedisCommandHandlerResult result;
    //          result.state = REDIS_CMD_WAIT;
    //          // Set output in done->Run().
    //          Service_Stub.Method(&cntl, &request, &response, done);
    //          // You don't need to call waitobj->Notify() in this situation since
    //          // call_id is automatically joinable.
    //          result.waitobj = new brpc::RPCWaitable(cntl.call_id());
    //          return result;
    //      }
    //      ...
    // }
    //
    // The return value should be { REDIS_CMD_HANDLED, NULL } for normal cases. If you
    // want to implement transaction, return { REDIS_CMD_CONTINUE, NULL } once server
    // receives an start marker and brpc will call MultiTransactionHandler() to new a
    // transaction handler that all the following commands are sent to this tranction
    // handler until it returns { REDIS_CMD_HANDLED, NULL}. Read the comment below.
    virtual RedisCommandHandlerResult Run(const std::vector<butil::StringPiece>& args,
                                          brpc::RedisReply* output,
                                          bool last_of_batch) = 0;

    // The Run() returns CONTINUE for "multi", which makes brpc call this method to
    // create a transaction_handler to process following commands until transaction_handler
    // returns OK. For example, for command "multi; set k1 v1; set k2 v2; set k3 v3;
    // exec":
    // 1) First command is "multi" and Run() should return REDIS_CMD_CONTINUE,
    // then brpc calls NewTransactionHandler() to new a transaction_handler.
    // 2) brpc calls transaction_handler.Run() with command "set k1 v1",
    // which should return CONTINUE.
    // 3) brpc calls transaction_handler.Run() with command "set k2 v2",
    // which should return CONTINUE.
    // 4) brpc calls transaction_handler.Run() with command "set k3 v3",
    // which should return CONTINUE.
    // 5) An ending marker(exec) is found in transaction_handler.Run(), user exeuctes all
    // the commands and return OK. This Transation is done.
    virtual RedisCommandHandler* NewTransactionHandler();
};

} // namespace brpc

#endif  // BRPC_REDIS_H
