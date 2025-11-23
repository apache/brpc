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

#ifndef BRPC_RPC_PB_MESSAGE_FACTORY_H
#define BRPC_RPC_PB_MESSAGE_FACTORY_H

#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/arena.h>
#include "butil/object_pool.h"

namespace brpc {

// Inherit this class to customize rpc protobuf messages,
// include request and response.
class RpcPBMessages {
public:
    virtual ~RpcPBMessages() = default;
    // Get protobuf request message.
    virtual google::protobuf::Message* Request() = 0;
    // Get protobuf response message.
    virtual google::protobuf::Message* Response() = 0;
};

// Factory to manage `RpcPBMessages'.
class RpcPBMessageFactory {
public:
    virtual ~RpcPBMessageFactory() = default;
    // Get `RpcPBMessages' according to `service' and `method'.
    // Common practice to create protobuf message:
    // service.GetRequestPrototype(&method).New() -> request;
    // service.GetResponsePrototype(&method).New() -> response.
    virtual RpcPBMessages* Get(const ::google::protobuf::Service& service,
                               const ::google::protobuf::MethodDescriptor& method) = 0;
    // Return `RpcPBMessages' to factory.
    virtual void Return(RpcPBMessages* messages) = 0;
};

class DefaultRpcPBMessageFactory : public RpcPBMessageFactory {
public:
    RpcPBMessages* Get(const ::google::protobuf::Service& service,
                       const ::google::protobuf::MethodDescriptor& method) override;
    void Return(RpcPBMessages* messages) override;
};

namespace internal {

// Allocate protobuf message from arena.
// The arena is created with `StartBlockSize' and `MaxBlockSize' options.
// For more details, see `google::protobuf::ArenaOptions'.
template<size_t StartBlockSize, size_t MaxBlockSize>
struct ArenaRpcPBMessages : public RpcPBMessages {
    class ArenaOptionsWrapper {
    public:
        ArenaOptionsWrapper() {
            options.start_block_size = StartBlockSize;
            options.max_block_size = MaxBlockSize;
        }

    private:
    friend struct ArenaRpcPBMessages;
        ::google::protobuf::ArenaOptions options;
    };

    explicit ArenaRpcPBMessages(ArenaOptionsWrapper options_wrapper)
        : arena(options_wrapper.options)
        , request(NULL)
        , response(NULL) {}

    ::google::protobuf::Message* Request() override { return request; }
    ::google::protobuf::Message* Response() override { return response; }

    ::google::protobuf::Arena arena;
    ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
};

template<size_t StartBlockSize, size_t MaxBlockSize>
class ArenaRpcPBMessageFactory : public RpcPBMessageFactory {
    typedef ::brpc::internal::ArenaRpcPBMessages<StartBlockSize, MaxBlockSize>
        ArenaRpcPBMessages;
public:
    ArenaRpcPBMessageFactory() {
        _arena_options.start_block_size = StartBlockSize;
        _arena_options.max_block_size = MaxBlockSize;
    }

    RpcPBMessages* Get(const ::google::protobuf::Service& service,
                       const ::google::protobuf::MethodDescriptor& method) override {
        typename ArenaRpcPBMessages::ArenaOptionsWrapper options_wrapper;
        auto messages = butil::get_object<ArenaRpcPBMessages>(options_wrapper);
        messages->request = service.GetRequestPrototype(&method).New(&messages->arena);
        messages->response = service.GetResponsePrototype(&method).New(&messages->arena);
        return messages;
    }

    void Return(RpcPBMessages* messages) override {
        auto arena_messages = static_cast<ArenaRpcPBMessages*>(messages);
        arena_messages->request = NULL;
        arena_messages->response = NULL;
        arena_messages->arena.Reset();
        butil::return_object(arena_messages);
    }

private:
    ::google::protobuf::ArenaOptions _arena_options;
};

}

template<size_t StartBlockSize, size_t MaxBlockSize>
RpcPBMessageFactory* GetArenaRpcPBMessageFactory() {
    return new ::brpc::internal::ArenaRpcPBMessageFactory<StartBlockSize, MaxBlockSize>();
}

BUTIL_FORCE_INLINE RpcPBMessageFactory* GetArenaRpcPBMessageFactory() {
    // Default arena options, same as `google::protobuf::ArenaOptions'.
    return GetArenaRpcPBMessageFactory<256, 8192>();
}

} // namespace brpc

#endif // BRPC_RPC_PB_MESSAGE_FACTORY_H
