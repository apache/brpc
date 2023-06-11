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

#include "brpc/retry_backoff_policy.h"
#include "butil/logging.h"

namespace brpc {

int32_t FixedRetryBackoffPolicy::get_backoff_time_ms(int nretry,
                                                     int64_t reaming_rpc_time_ms) const {
    if (nretry <= 0 || reaming_rpc_time_ms < _no_backoff_remaining_rpc_time_ms) {
        return 0;
    }
    return _backoff_time_ms < reaming_rpc_time_ms ? _backoff_time_ms : 0;
}

int32_t JitteredRetryBackoffPolicy::get_backoff_time_ms(int nretry ,
                                                        int64_t reaming_rpc_time_ms) const {
    if (nretry <= 0 || reaming_rpc_time_ms < _no_backoff_remaining_rpc_time_ms) {
      return 0;
    }
    int32_t backoff_time_ms = butil::fast_rand_in(_min_backoff_time_ms,
                                                  _max_backoff_time_ms);
    return backoff_time_ms < reaming_rpc_time_ms ? backoff_time_ms : 0;
}


} // namespace brpc

