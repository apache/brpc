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


#ifndef BRPC_RESTFUL_H
#define BRPC_RESTFUL_H

#include <string>
#include "butil/strings/string_piece.h"
#include "brpc/server.h"


namespace brpc {

struct RestfulMethodPath {
    std::string service_name;
    std::string prefix;
    std::string postfix;
    bool has_wildcard;

    std::string to_string() const;
};
struct RestfulMapping {
    RestfulMethodPath path;
    std::string method_name;
};

// Split components of `path_in' into `path_out'.
// * path_out->service_name does not have /.
// * path_out->prefix is normalized as
//   prefix := "/COMPONENT" prefix | "" (no dot in COMPONENT)
// Returns true on success.
bool ParseRestfulPath(butil::StringPiece path_in, RestfulMethodPath* path_out);

// Parse "PATH1 => NAME1, PATH2 => NAME2 ..." where:
// * PATHs are acceptible by ParseRestfulPath.
// * NAMEs are valid as method names in protobuf.
// Returns true on success.
bool ParseRestfulMappings(const butil::StringPiece& mappings,
                          std::vector<RestfulMapping>* list);

struct RestfulMethodProperty : public Server::MethodProperty {
    RestfulMethodPath path;
    ServiceOwnership ownership;
};

// Store paths under a same toplevel name.
class RestfulMap {
public:
    typedef std::map<std::string, RestfulMethodProperty> DedupMap;
    typedef std::vector<RestfulMethodProperty*> PathList;

    explicit RestfulMap(const std::string& service_name)
        : _service_name(service_name) {}
    virtual ~RestfulMap();

    // Map `path' to the method denoted by `method_name' in `service'.
    // Returns MethodStatus of the method on success, NULL otherwise.
    bool AddMethod(const RestfulMethodPath& path,
                   google::protobuf::Service* service,
                   const Server::MethodProperty::OpaqueParams& params,
                   const std::string& method_name,
                   MethodStatus* status);

    // Remove by RestfulMethodPath::to_string() of the path to AddMethod()
    // Returns number of methods removed (should be 1 or 0 currently)
    size_t RemoveByPathString(const std::string& path);

    // Remove all methods.
    void ClearMethods();

    // Called after by Server at starting moment, to refresh _sorted_paths
    void PrepareForFinding();
    
    // Find the method by path.
    // Time complexity in worst-case is #slashes-in-input * log(#paths-stored)
    const Server::MethodProperty*
    FindMethodProperty(const butil::StringPiece& method_path,
                       std::string* unresolved_path) const;

    const std::string& service_name() const { return _service_name; }

    // Number of methods in this map. Only for UT right now.
    size_t size() const { return _dedup_map.size(); }
    
private:
    DISALLOW_COPY_AND_ASSIGN(RestfulMap);
    
    std::string _service_name;
    // refreshed each time 
    PathList _sorted_paths;
    DedupMap _dedup_map;
};

std::ostream& operator<<(std::ostream& os, const RestfulMethodPath&);

} // namespace brpc


#endif  // BRPC_RESTFUL_H
