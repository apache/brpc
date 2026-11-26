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

#ifndef BRPC_URMA_QOS_H
#define BRPC_URMA_QOS_H

namespace brpc {

// Outbound RM QoS, resolved through the selected device's priority/SL map.
struct UrmaQosOptions {
    // -1 selects from SL or the device default; explicit range [0, 15].
    int priority = -1;
    // -1 leaves SL unspecified; explicit range [0, 255].
    int service_level = -1;

    bool IsValid() const {
        return priority >= -1 && priority <= 15 && service_level >= -1 &&
               service_level <= 255;
    }
};

}  // namespace brpc

#endif  // BRPC_URMA_QOS_H
