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
#include <cerrno>
#include <limits>
#include <set>
#include <sstream>
#include "butil/third_party/murmurhash3/murmurhash3.h"

namespace brpc {
namespace flatbuffers {
namespace {

bool IsIdentifier(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') &&
            !(c >= 'A' && c <= 'Z') &&
            !(i != 0 && c >= '0' && c <= '9')) {
            return false;
        }
    }
    return true;
}

bool IsNamespace(const std::string& name) {
    if (name.empty()) {
        return true;
    }
    size_t begin = 0;
    do {
        const size_t end = name.find('.', begin);
        if (!IsIdentifier(name.substr(begin, end - begin))) {
            return false;
        }
        if (end == std::string::npos) {
            return true;
        }
        begin = end + 1;
    } while (begin < name.size());
    return false;
}

}  // namespace

int ServiceDescriptor::init(const BrpcDescriptorTable& table) {
    if (!_methods.empty()) {
        errno = EALREADY;
        return -1;
    }
    std::string prefix = table.prefix;
    if (!prefix.empty() && prefix.back() == '.') {
        prefix.pop_back();
        if (prefix.empty()) {
            errno = EINVAL;
            return -1;
        }
    }
    if (!IsIdentifier(table.service_name) || !IsNamespace(prefix)) {
        errno = EINVAL;
        return -1;
    }
    std::istringstream input(table.method_name_list);
    std::vector<std::string> names;
    std::set<std::string> unique_names;
    std::string name;
    while (input >> name) {
        if (!IsIdentifier(name) || !unique_names.insert(name).second) {
            errno = EINVAL;
            return -1;
        }
        names.push_back(name);
    }
    if (names.empty() || names.size() > static_cast<size_t>(
            std::numeric_limits<int>::max()) ||
        (!table.method_ids.empty() && table.method_ids.size() != names.size())) {
        errno = EINVAL;
        return -1;
    }
    std::set<int> unique_ids;
    for (int id : table.method_ids) {
        if (id < 0 || !unique_ids.insert(id).second) {
            errno = EINVAL;
            return -1;
        }
    }
    _name = table.service_name;
    _full_name = prefix.empty() ? _name : prefix + "." + _name;
    butil::MurmurHash3_x86_32(_full_name.data(), _full_name.size(), 1, &_index);
    _methods.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        const int id = table.method_ids.empty() ? static_cast<int>(i)
                                                : table.method_ids[i];
        _methods.emplace_back(new MethodDescriptor(names[i], this, id));
    }
    return 0;
}

const MethodDescriptor* ServiceDescriptor::method(int position) const {
    if (position < 0 || static_cast<size_t>(position) >= _methods.size()) {
        errno = EINVAL;
        return nullptr;
    }
    return _methods[position].get();
}

const MethodDescriptor* ServiceDescriptor::FindMethodByIndex(int id) const {
    for (const auto& method : _methods) {
        if (method->index() == id) {
            return method.get();
        }
    }
    errno = EINVAL;
    return nullptr;
}

MethodDescriptor::MethodDescriptor(const std::string& name,
                                   const ServiceDescriptor* service, int index)
    : _name(name), _full_name(service->full_name() + "." + name),
      _service(service), _index(index) {}

int parse_service_descriptors(const BrpcDescriptorTable& table,
                              ServiceDescriptor** out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    std::unique_ptr<ServiceDescriptor> descriptor(new ServiceDescriptor);
    if (descriptor->init(table) != 0) {
        return -1;
    }
    *out = descriptor.release();
    return 0;
}

}  // namespace flatbuffers
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
