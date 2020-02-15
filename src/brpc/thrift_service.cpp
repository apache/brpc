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


#include "butil/class_name.h"
#include "brpc/thrift_service.h"
#include "brpc/details/method_status.h"

namespace brpc {

ThriftService::ThriftService() {
    _status = new (std::nothrow) MethodStatus;
    LOG_IF(FATAL, _status == NULL) << "Fail to new MethodStatus";
}

ThriftService::~ThriftService() {
    delete _status;
    _status = NULL;
}

void ThriftService::Describe(std::ostream &os, const DescribeOptions&) const {
    os << butil::class_name_str(*this);
}

void ThriftService::Expose(const butil::StringPiece& prefix) {
    if (_status == NULL) {
        return;
    }
    std::string s;
    const std::string& cached_name = butil::class_name_str(*this);
    s.reserve(prefix.size() + 1 + cached_name.size());
    s.append(prefix.data(), prefix.size());
    s.push_back('_');
    s.append(cached_name);
    _status->Expose(s);
}

} // namespace brpc

