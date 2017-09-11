// Copyright (c) 2016 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_TRACKME_H
#define BRPC_TRACKME_H

// [Internal] RPC users are not supposed to call functions below. 

#include "butil/endpoint.h"


namespace brpc {

// Set the server address for reporting.
// Currently only the first address will be saved.
void SetTrackMeAddress(butil::EndPoint pt);

// Call this function every second (or every several seconds) to send
// TrackMeRequest to -trackme_server every TRACKME_INTERVAL seconds.
void TrackMe();

} // namespace brpc


#endif // BRPC_TRACKME_H
