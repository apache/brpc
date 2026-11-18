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

#include "brpc/flatbuffers/service.h"

#if BRPC_WITH_FLATBUFFERS
#include "brpc/channel.h"

namespace brpc {
namespace flatbuffers {
namespace {

// Forwards generated-stub calls to brpc::Channel::FBCallMethod.
class ChannelAdapter : public RpcChannel {
public:
    ChannelAdapter(::brpc::Channel* channel, bool owns_channel)
        : _channel(channel), _owns_channel(owns_channel) {}
    ~ChannelAdapter() override {
        if (_owns_channel) {
            delete _channel;
        }
    }

    void FBCallMethod(const MethodDescriptor* method,
                      google::protobuf::RpcController* controller,
                      const Message* request, Message* response,
                      google::protobuf::Closure* done) override {
        _channel->FBCallMethod(method, controller, request, response, done);
    }

private:
    ::brpc::Channel* _channel;
    bool _owns_channel;
};

}  // namespace

RpcChannel* NewChannelAdapter(::brpc::Channel* channel, bool owns_channel) {
    if (channel == nullptr) {
        return nullptr;
    }
    return new ChannelAdapter(channel, owns_channel);
}

}  // namespace flatbuffers
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
