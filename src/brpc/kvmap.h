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

#ifndef BRPC_KVMAP_H
#define BRPC_KVMAP_H

#include "butil/containers/flat_map.h"

namespace brpc {
    
// Remember Key/Values in string
class KVMap {
public:
    typedef butil::FlatMap<std::string, std::string> Map;
    typedef Map::const_iterator Iterator;

    KVMap() {}

    // Exchange internal fields with another KVMap.
    void Swap(KVMap &rhs) { _entries.swap(rhs._entries); }

    // Reset internal fields as if they're just default-constructed.
    void Clear() { _entries.clear(); }

    // Get value of a key(case-sensitive)
    // Return pointer to the value, NULL on not found.
    const std::string* Get(const char* key) const { return _entries.seek(key); }
    const std::string* Get(const std::string& key) const {
        return _entries.seek(key);
    }

    // Set value of a key
    void Set(const std::string& key, const std::string& value) {
        _entries[key] = value;
    }
    void Set(const std::string& key, const char* value) { _entries[key] = value; }
    // Convert other types to string as well
    template <typename T>
    void Set(const std::string& key, const T& value) {
        _entries[key] = std::to_string(value);
    }

    // Remove a key
    void Remove(const char* key) { _entries.erase(key); }
    void Remove(const std::string& key) { _entries.erase(key); }

    // Get iterators to iterate key/value
    Iterator Begin() const { return _entries.begin(); }
    Iterator End() const { return _entries.end(); }
    
    // number of key/values
    size_t Count() const { return _entries.size(); }

private:

    Map _entries;
};

} // namespace brpc

#endif // BRPC_KVMAP_H
