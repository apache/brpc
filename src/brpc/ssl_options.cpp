// Copyright (c) 2014 baidu-rpc authors.
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

// Authors: Rujie Jiang (jiangrujie@baidu.com)

#include "brpc/ssl_options.h"

namespace brpc {

VerifyOptions::VerifyOptions() : verify_depth(0) {}

ChannelSSLOptions::ChannelSSLOptions()
    : ciphers("DEFAULT")
    , protocols("TLSv1, TLSv1.1, TLSv1.2")
{}

ServerSSLOptions::ServerSSLOptions()
    : strict_sni(false)
    , disable_ssl3(true)
    , release_buffer(false)
    , session_lifetime_s(300)
    , session_cache_size(20480)
    , ecdhe_curve_name("prime256v1")
{}

} // namespace brpc
