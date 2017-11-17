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

#include "cmd_flags.h"

// used by Kevin.XU
DEFINE_string(etcd_server, "http://127.0.0.1:2379", "The url which access Etcd node");
DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_string(node_ip, "127.0.0.1", "The IP address which the current node listen");
DEFINE_string(node_tags, "stage=beta;version=1.0", "Tags which owned by this node");
DEFINE_int32(check_interval, 5, "Wait so many seconds before next checking service");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
    "read/write operations during the last `idle_timeout_s'");

//for client
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "single", "Connection type. Available values: single, pooled, short");
DEFINE_string(load_balancer, "rr", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_string(http_content_type, "application/json", "Content type of http request");
