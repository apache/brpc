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

#ifndef BVAR_DETAIL_PROMETHEUS_NAME_REGISTRY_H
#define BVAR_DETAIL_PROMETHEUS_NAME_REGISTRY_H

#include <string>
#include <vector>

namespace bvar {
namespace detail {

// Reserves all names atomically for `owner'. Returns false without reserving
// anything if one of them belongs to another exposed variable.
bool reserve_prometheus_names(const void* owner, const std::vector<std::string>& names);

// Releases names that are still reserved by `owner'.
void release_prometheus_names(const void* owner, const std::vector<std::string>& names);

}  // namespace detail
}  // namespace bvar

#endif  // BVAR_DETAIL_PROMETHEUS_NAME_REGISTRY_H
