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

#ifndef BRPC_FLATBUFFERS_SERVICE_H
#define BRPC_FLATBUFFERS_SERVICE_H

#include "butil/config.h"

#if BRPC_WITH_FLATBUFFERS
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace google {
namespace protobuf {
class Closure;
class RpcController;
}  // namespace protobuf
}  // namespace google

namespace brpc {
namespace flatbuffers {
class Message;
class ServiceDescriptor;

struct BrpcDescriptorTable {
    // A namespace, optionally followed by one period. Empty means global scope.
    std::string prefix;
    std::string service_name;
    // Whitespace-separated method names in declaration order.
    std::string method_name_list;
    // Stable wire IDs, NOT array positions. Empty retains legacy ordinal IDs.
    // New schemas should specify every ID; IDs must be nonnegative and unique.
    std::vector<int> method_ids;
};

class MethodDescriptor {
public:
    MethodDescriptor(const std::string& name, const ServiceDescriptor* service,
                     int index);
    const std::string& name() const { return _name; }
    const std::string& full_name() const { return _full_name; }
    int index() const { return _index; }
    const ServiceDescriptor* service() const { return _service; }

private:
    std::string _name;
    std::string _full_name;
    const ServiceDescriptor* _service;
    int _index;
};

// Immutable after successful initialization. Owns all method descriptors.
class ServiceDescriptor {
public:
    ServiceDescriptor() : _index(0) {}
    ServiceDescriptor(const ServiceDescriptor&) = delete;
    ServiceDescriptor& operator=(const ServiceDescriptor&) = delete;
    int init(const BrpcDescriptorTable& table);
    const std::string& name() const { return _name; }
    const std::string& full_name() const { return _full_name; }
    // MurmurHash3 of the canonical fully-qualified service name, seed 1.
    uint32_t index() const { return _index; }
    int method_count() const { return static_cast<int>(_methods.size()); }
    // Dense declaration-order lookup, for enumeration and generated stubs.
    const MethodDescriptor* method(int position) const;
    // Sparse stable-ID lookup, for wire dispatch. Missing IDs return nullptr.
    const MethodDescriptor* FindMethodByIndex(int id) const;

private:
    std::string _name;
    std::string _full_name;
    uint32_t _index;
    std::vector<std::unique_ptr<MethodDescriptor> > _methods;
};

// On success the caller owns *out; on failure *out is unchanged.
int parse_service_descriptors(const BrpcDescriptorTable& table,
                              ServiceDescriptor** out);

class RpcChannel {
public:
    RpcChannel() = default;
    virtual ~RpcChannel() = default;
    RpcChannel(const RpcChannel&) = delete;
    RpcChannel& operator=(const RpcChannel&) = delete;
    virtual void FBCallMethod(const MethodDescriptor* method,
                              google::protobuf::RpcController* controller,
                              const Message* request, Message* response,
                              google::protobuf::Closure* done) = 0;
};

class Service {
public:
    Service() = default;
    virtual ~Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    enum ChannelOwnership { STUB_OWNS_CHANNEL, STUB_DOESNT_OWN_CHANNEL };
    virtual const ServiceDescriptor* GetDescriptor() = 0;
    // Implementations must validate the method and request, report errors on
    // controller and run a non-null done exactly once, including failure paths.
    virtual void FBCallMethod(const MethodDescriptor* method,
                              google::protobuf::RpcController* controller,
                              const Message* request, Message* response,
                              google::protobuf::Closure* done) = 0;
};

}  // namespace flatbuffers
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
#endif  // BRPC_FLATBUFFERS_SERVICE_H
