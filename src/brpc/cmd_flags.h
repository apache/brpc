// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)


#ifndef BRPC_CMD_FLAGS_H
#define BRPC_CMD_FLAGS_H

#include <gflags/gflags.h>

DECLARE_bool(debug_mode);
DECLARE_string(etcd_server);
DECLARE_int32(port);
DECLARE_string(node_ip);
DECLARE_string(node_tags);
DECLARE_int32(check_interval);
DECLARE_int32(idle_timeout_s);

DECLARE_string(protocol);
DECLARE_string(connection_type);
DECLARE_string(load_balancer);
DECLARE_int32(timeout_ms);
DECLARE_int32(max_retry); 
DECLARE_string(http_content_type);

#endif // BRPC_COMPRESS_H
