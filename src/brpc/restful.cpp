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


#include <google/protobuf/descriptor.h>
#include "brpc/log.h"
#include "brpc/restful.h"
#include "brpc/details/method_status.h"


namespace brpc {

// Define in http_parser.cpp
extern bool is_url_char(char c);

inline butil::StringPiece remove_last_char(butil::StringPiece s) {
    if (!s.empty()) {
        s.remove_suffix(1);
    }
    return s;
}

std::ostream& operator<<(std::ostream& os, const RestfulMethodPath& p) {
    if (!p.service_name.empty()) {
        os << '/' << p.service_name;
    }
    if (p.has_wildcard) {
        os << p.prefix << '*' << remove_last_char(p.postfix);
    } else {
        os << remove_last_char(p.prefix);
    }
    return os;
}

std::string RestfulMethodPath::to_string() const {
    std::string s;
    s.reserve(service_name.size() + prefix.size() + 2 + postfix.size());
    if (!service_name.empty()) {
        s.push_back('/');
        s.append(service_name);
    }
    if (has_wildcard) {
        s.append(prefix);
        s.push_back('*');
        butil::StringPiece tmp = remove_last_char(postfix);
        s.append(tmp.data(), tmp.size());
    } else {
        butil::StringPiece tmp = remove_last_char(prefix);
        s.append(tmp.data(), tmp.size());        
    }
    return s;
}

struct DebugPrinter {
    explicit DebugPrinter(const RestfulMethodPath& p) : path(&p) {}
    const RestfulMethodPath* path;
};

std::ostream& operator<<(std::ostream& os, const DebugPrinter& p) {
    os << "{service=" << p.path->service_name
       << " prefix=" << p.path->prefix
       << " postfix=" << p.path->postfix
       << " wildcard=" << p.path->has_wildcard
       << '}';
    return os;
}

bool ParseRestfulPath(butil::StringPiece path,
                      RestfulMethodPath* path_out) {
    path.trim_spaces();
    if (path.empty()) {
        LOG(ERROR) << "Parameter[path] is empty";
        return false;
    }
    // Check validity of the path.
    // TODO(gejun): Probably too strict.
    int star_index = -1;
    for (const char* p = path.data(); p != path.data() + path.size(); ++p) {
        if (*p == '*') {
            if (star_index < 0) {
                star_index = (int)(p - path.data());
            } else {
                LOG(ERROR) << "More than one wildcard in restful_path=`"
                           << path << '\'';
                return false;
            }
        } else if (!is_url_char(*p)) {
            LOG(ERROR) << "Invalid character=`" << *p << "' (index="
                       << p - path.data() << ") in path=`" << path << '\'';
            return false;
        }
    }
    path_out->has_wildcard = (star_index >= 0);

    butil::StringPiece first_part;
    butil::StringPiece second_part;
    if (star_index < 0) {
        first_part = path;
    } else {
        first_part = path.substr(0, star_index);
        second_part = path.substr(star_index + 1);
    }

    // Extract service_name and prefix from first_part
    // The prefix is normalized as:
    //   /      -  "*B => M"
    //   /A     -  "/A*B => M" (disabled for performance)
    //   /A/    -  "/A/*B => M"
    path_out->service_name.clear();
    path_out->prefix.clear();
    {
        // remove heading slashes.
        size_t i = 0;
        for (; i < first_part.size() && first_part[i] == '/'; ++i) {}
        first_part.remove_prefix(i);
    }
    const size_t slash_pos = first_part.find('/');
    if (slash_pos != butil::StringPiece::npos) {
        path_out->service_name.assign(first_part.data(), slash_pos);
        butil::StringPiece prefix_raw = first_part.substr(slash_pos + 1);
        butil::StringSplitter sp(prefix_raw.data(),
                                prefix_raw.data() + prefix_raw.size(), '/');
        for (; sp; ++sp) {
            // Put first component into service_name and others into prefix.
            if (path_out->prefix.empty()) {
                path_out->prefix.reserve(prefix_raw.size() + 2);
            }
            path_out->prefix.push_back('/');
            path_out->prefix.append(sp.field(), sp.length());
        }
        if (!path_out->has_wildcard ||
            prefix_raw.empty() ||
            prefix_raw.back() == '/') {
            path_out->prefix.push_back('/');
        } else {
            LOG(ERROR) << "Pattern A* (A is not ended with /) in path=`"
                       << path << "' is disallowed for performance concerns";
            return false;
        }
    } else if (!path_out->has_wildcard) {
        // no slashes, no wildcard. Example: abc => Method
        path_out->service_name.assign(first_part.data(), first_part.size());
        path_out->prefix.push_back('/');
    } else { // no slashes, has wildcard. Example: abc* => Method
        if (!first_part.empty()) {
            LOG(ERROR) << "Pattern A* (A is not ended with /) in path=`"
                       << path << "' is disallowed for performance concerns";
            return false;
        }
        path_out->prefix.push_back('/');
        path_out->prefix.append(first_part.data(), first_part.size());
    }
    
    // Normalize second_part as postfix:
    //     /        -  "A* => M" or  "A => M"
    //    B/        -  "A*B => M"
    //   /B/        -  "A*/B => M"
    path_out->postfix.clear();
    if (path_out->has_wildcard) {
        if (second_part.empty() || second_part[0] == '/') {
            path_out->postfix.push_back('/');
        }
        butil::StringSplitter sp2(second_part.data(),
                                 second_part.data() + second_part.size(), '/');
        for (; sp2; ++sp2) {
            if (path_out->postfix.empty()) {
                path_out->postfix.reserve(second_part.size() + 2);
            }
            path_out->postfix.append(sp2.field(), sp2.length());
            path_out->postfix.push_back('/');
        }
    } else {
        path_out->postfix.push_back('/');
    }
    VLOG(RPC_VLOG_LEVEL + 1) << "orig_path=" << path
                             << " first_part=" << first_part
                             << " second_part=" << second_part
                             << " path=" << DebugPrinter(*path_out);
    return true;
}

bool ParseRestfulMappings(const butil::StringPiece& mappings,
                          std::vector<RestfulMapping>* list) {
    if (list == NULL) {
        LOG(ERROR) << "Param[list] is NULL";
        return false;
    }
    list->clear();
    list->reserve(8);
    butil::StringSplitter sp(
        mappings.data(), mappings.data() + mappings.size(), ',');
    int nmappings = 0;
    for (; sp; ++sp) {
        ++nmappings;
        size_t i = 0;
        const char* p = sp.field();
        const size_t n = sp.length();
        bool added_sth = false;
        for (; i < n; ++i) {
            // find =
            if (p[i] != '=') {
                continue;
            }
            const size_t equal_sign_pos = i;
            for (; i < n && p[i] == '='; ++i) {}   // skip repeated =
            // If the = ends with >, it's the arrow that we're finding.
            // otherwise just skip and keep searching.
            if (i < n && p[i] == '>') {
                RestfulMapping m;
                // Parse left part of the arrow as url path.
                butil::StringPiece path(sp.field(), equal_sign_pos);
                if (!ParseRestfulPath(path, &m.path)) {
                    LOG(ERROR) << "Fail to parse path=`" << path << '\'';
                    return false;
                }
                // Treat right part of the arrow as method_name.
                butil::StringPiece method_name_piece(p + i + 1, n - (i + 1));
                method_name_piece.trim_spaces();
                if (method_name_piece.empty()) {
                    LOG(ERROR) << "No method name in " << nmappings
                               << "-th mapping";
                    return false;
                }
                m.method_name.assign(method_name_piece.data(),
                                     method_name_piece.size());
                list->push_back(m);
                added_sth = true;
                break;
            }
        }
        // If we don't get a valid mapping from the string, issue error.
        if (!added_sth) {
            LOG(ERROR) << "Invalid mapping: "
                       << butil::StringPiece(sp.field(), sp.length());
            return false;
        }
    }
    return true;
}

RestfulMap::~RestfulMap() {
    ClearMethods();
}

// This function inserts a mapping into _dedup_map.
bool RestfulMap::AddMethod(const RestfulMethodPath& path,
                           google::protobuf::Service* service,
                           const Server::MethodProperty::OpaqueParams& params,
                           const std::string& method_name,
                           MethodStatus* status) {
    if (service == NULL) {
        LOG(ERROR) << "Param[service] is NULL";
        return false;
    }
    const google::protobuf::MethodDescriptor* md =
        service->GetDescriptor()->FindMethodByName(method_name);
    if (md == NULL) {
        LOG(ERROR) << service->GetDescriptor()->full_name()
                   << " has no method called `" << method_name << '\'';
        return false;
    }
    if (path.service_name != _service_name) {
        LOG(ERROR) << "Impossible: path.service_name does not match name"
            " of this RestfulMap";
        return false;
    }
    // Use the string-form of path as key is a MUST to implement
    // RemoveByPathString which is used in Server.RemoveMethodsOf
    std::string dedup_key = path.to_string();
    DedupMap::const_iterator it = _dedup_map.find(dedup_key);
    if (it != _dedup_map.end()) {
        LOG(ERROR) << "Already mapped `" << it->second.path
                   << "' to `" << it->second.method->full_name() << '\'';
        return false;
    }
    RestfulMethodProperty& info = _dedup_map[dedup_key];
    info.is_builtin_service = false;
    info.own_method_status = false;
    info.params = params;
    info.service = service;
    info.method = md;
    info.status = status;
    info.path = path;
    info.ownership = SERVER_DOESNT_OWN_SERVICE;
    RPC_VLOG << "Mapped `" << path << "' to `" << md->full_name() << '\'';
    return true;
}

void RestfulMap::ClearMethods() {
    _sorted_paths.clear();
    for (DedupMap::iterator it = _dedup_map.begin();
         it != _dedup_map.end(); ++it) {
        if (it->second.own_method_status) {
            delete it->second.status;
        }
    }
    _dedup_map.clear();
}

struct CompareItemInPathList {
    bool operator()(const RestfulMethodProperty* e1,
                    const RestfulMethodProperty* e2) const {
        const int rc1 = e1->path.prefix.compare(e2->path.prefix);
        if (rc1 != 0) {
            return rc1 < 0;
        }
        // /A/*/B is put before /A/B so that we try exact patterns first
        // (the matching is in reversed order)
        if (e1->path.has_wildcard != e2->path.has_wildcard) {
            return e1->path.has_wildcard > e2->path.has_wildcard;
        }
        // Compare postfix from back to front.
        // TODO: Optimize this.
        std::string::const_reverse_iterator it1 = e1->path.postfix.rbegin();
        std::string::const_reverse_iterator it2 = e2->path.postfix.rbegin();
        while (it1 != e1->path.postfix.rend() &&
               it2 != e2->path.postfix.rend()) {
            if (*it1 != *it2) {
                return (*it1 < *it2);
            }
            ++it1;
            ++it2;
        }
        return (it1 == e1->path.postfix.rend())
            > (it2 == e2->path.postfix.rend());
    }
};

void RestfulMap::PrepareForFinding() {
    _sorted_paths.clear();
    _sorted_paths.reserve(_dedup_map.size());
    for (DedupMap::iterator it = _dedup_map.begin(); it != _dedup_map.end();
         ++it) {
        _sorted_paths.push_back(&it->second);
    }
    std::sort(_sorted_paths.begin(), _sorted_paths.end(),
              CompareItemInPathList());
    if (VLOG_IS_ON(RPC_VLOG_LEVEL + 1)) {
        std::ostringstream os;
        os << "_sorted_paths(" << _service_name << "):";
        for (PathList::const_iterator it = _sorted_paths.begin();
             it != _sorted_paths.end(); ++it) {
            os << ' ' << (*it)->path;
        }
        VLOG(RPC_VLOG_LEVEL + 1) << os.str();
    }
}

// Remove last component from the (normalized) path:
// Say /A/B/C/ -> /A/B/
// Notice that /A/ is modified to / and returns true.
static bool RemoveLastComponent(butil::StringPiece* path) {
    if (path->empty()) {
        return false;
    }
    if (path->back() == '/') {
        path->remove_suffix(1);
    }
    size_t slash_pos = path->rfind('/');
    if (slash_pos == std::string::npos) {
        return false;
    }
    path->remove_suffix(path->size() - slash_pos - 1); // keep the slash
    return true;
}

// Normalized as /A/B/C/
static std::string NormalizeSlashes(const butil::StringPiece& path) {
    std::string out_path;
    out_path.reserve(path.size() + 2);
    butil::StringSplitter sp(path.data(), path.data() + path.size(), '/');
    for (; sp; ++sp) {
        out_path.push_back('/');
        out_path.append(sp.field(), sp.length());
    }
    out_path.push_back('/');
    return out_path;
}

size_t RestfulMap::RemoveByPathString(const std::string& path) {
    // removal only happens when server stops, clear _sorted_paths to make
    // sure wild pointers do not exist.
    if (!_sorted_paths.empty()) {
        _sorted_paths.clear();
    }
    return _dedup_map.erase(path);
}

struct PrefixLess {
    bool operator()(const butil::StringPiece& path,
                    const RestfulMethodProperty* p) const {
        return path < p->path.prefix;
    }
};

const Server::MethodProperty*
RestfulMap::FindMethodProperty(const butil::StringPiece& method_path,
                               std::string* unresolved_path) const {
    if (_sorted_paths.empty()) {
        LOG(ERROR) << "_sorted_paths is empty, method_path=" << method_path;
        return NULL;
    }
    const std::string full_path = NormalizeSlashes(method_path);
    butil::StringPiece sub_path = full_path;
    PathList::const_iterator last_find_pos = _sorted_paths.end();
    do {
        if (last_find_pos == _sorted_paths.begin()) {
            return NULL;
        }
        // Note: stop trying places that we already visited or skipped.
        PathList::const_iterator it =
            std::upper_bound(_sorted_paths.begin(), last_find_pos/*note*/,
                             sub_path, PrefixLess());
        if (it != _sorted_paths.begin()) {
            --it;
        }
        
        bool matched = false;
        bool remove_heading_slash_from_unresolved = false;
        butil::StringPiece left;
        do {
            const RestfulMethodPath& rpath = (*it)->path;
            if (!sub_path.starts_with(rpath.prefix)) {
                VLOG(RPC_VLOG_LEVEL + 1)
                    << "sub_path=" << sub_path << " does not match prefix="
                    << rpath.prefix << " full_path=" << full_path
                    << " candidate=" << DebugPrinter(rpath);
                // NOTE: We can stop trying patterns before *it because pattern
                // "/A*B => M" is disabled which makes prefixes of all restful
                // paths end with /. If `full_path' matches with a prefix, the
                // prefix must be a sub path of the full_path, which makes 
                // prefix matching runs at most #components-of-path times.
                // Otherwise we have to match all "/A*B" patterns before *it,
                // which is more complicated but rarely needed by users.
                break;
            }
            left = full_path;
            // Remove matched prefix from `left'.
            if (!rpath.prefix.empty()) {
                // make sure `left' is still starting with /
                size_t removal = rpath.prefix.size();
                if (rpath.prefix[removal - 1] == '/') {
                    --removal;
                    remove_heading_slash_from_unresolved = true;
                }
                left.remove_prefix(removal);
            }
            // Match postfix.
            if (left.ends_with(rpath.postfix)) {
                left.remove_suffix(rpath.postfix.size());
                if (!left.empty() && !rpath.has_wildcard) {
                    VLOG(RPC_VLOG_LEVEL + 1)
                        << "Unmatched extra=" << left
                        << " sub_path=" << sub_path
                        << " full_path=" << full_path
                        << " candidate=" << DebugPrinter(rpath);
                } else {
                    matched = true;
                    VLOG(RPC_VLOG_LEVEL + 1)
                        << "Matched sub_path=" << sub_path
                        << " full_path=" << full_path
                        << " with restful_path=" << DebugPrinter(rpath);
                    break;
                }
            }
            if (it == _sorted_paths.begin()) {
                VLOG(RPC_VLOG_LEVEL + 1)
                    << "Hit beginning, sub_path=" << sub_path
                    << " full_path=" << full_path
                    << " candidate=" << DebugPrinter(rpath);
                return NULL;
            }
            // Matched with prefix but postfix or wildcard, moving forward
            --it;
        } while (true);
        last_find_pos = it;
        
        if (!matched) {
            continue;
        }
        if (unresolved_path) {
            if (!left.empty()) {
                if (remove_heading_slash_from_unresolved && left[0] == '/') {
                    unresolved_path->assign(left.data() + 1, left.size() - 1);
                } else {
                    unresolved_path->assign(left.data(), left.size());
                }
            } else {
                unresolved_path->clear();
            }
        }
        return *it;
    } while (RemoveLastComponent(&sub_path));
    //                            ^^^^^^^^
    // sub_path can be / to match patterns like "*.flv => M" whose prefix is /
    return NULL;
}

} // namespace brpc
