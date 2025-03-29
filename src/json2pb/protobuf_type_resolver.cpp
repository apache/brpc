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

#include "json2pb/protobuf_type_resolver.h"

namespace json2pb {

using google::protobuf::DescriptorPool;
using google::protobuf::util::TypeResolver;
using google::protobuf::util::NewTypeResolverForDescriptorPool;

TypeResolverUniqueptr GetTypeResolver(const google::protobuf::Message& message) {
    auto pool = message.GetDescriptor()->file()->pool();
    bool is_generated_pool = pool == DescriptorPool::generated_pool();
    TypeResolver* resolver = is_generated_pool
        ? butil::get_leaky_singleton<TypeResolver>()
        : NewTypeResolverForDescriptorPool(PROTOBUF_TYPE_URL_PREFIX, pool);
    return { resolver, TypeResolverDeleter(is_generated_pool) };
}

} // namespace json2pb

