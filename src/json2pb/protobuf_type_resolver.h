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

#ifndef BRPC_PROTOBUF_TYPE_RESOLVER_H
#define BRPC_PROTOBUF_TYPE_RESOLVER_H

#include <string>
#include <memory>
#include <google/protobuf/message.h>
#include <google/protobuf/util/type_resolver.h>
#include <google/protobuf/util/type_resolver_util.h>
#include "butil/string_printf.h"
#include "butil/memory/singleton_on_pthread_once.h"

namespace json2pb {

#define PROTOBUF_TYPE_URL_PREFIX "type.googleapis.com"

inline std::string GetTypeUrl(const google::protobuf::Message& message) {
    return butil::string_printf(PROTOBUF_TYPE_URL_PREFIX"/%s",
                                message.GetDescriptor()->full_name().c_str());
}

// unique_ptr deleter for TypeResolver only deletes the object
// when it's not from the generated pool.
class TypeResolverDeleter {
public:
    explicit TypeResolverDeleter(bool is_generated_pool)
        : _is_generated_pool(is_generated_pool) {}

    void operator()(google::protobuf::util::TypeResolver* resolver) const {
        if (!_is_generated_pool) {
            delete resolver;
        }
    }
private:
    bool _is_generated_pool;
};

using TypeResolverUniqueptr = std::unique_ptr<
    google::protobuf::util::TypeResolver, TypeResolverDeleter>;

TypeResolverUniqueptr GetTypeResolver(const google::protobuf::Message& message);

} // namespace json2pb

namespace butil {

// Customized singleton object creation for google::protobuf::util::TypeResolver.
template<>
inline google::protobuf::util::TypeResolver*
create_leaky_singleton_obj<google::protobuf::util::TypeResolver>() {
    return google::protobuf::util::NewTypeResolverForDescriptorPool(
        PROTOBUF_TYPE_URL_PREFIX, google::protobuf::DescriptorPool::generated_pool());
}

} // namespace butil

#endif // BRPC_PROTOBUF_TYPE_RESOLVER_H
