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

#include <gflags/gflags.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/compiler/importer.h>
#include <brpc/server.h>
#include <butil/logging.h>
#include <butil/string_splitter.h>
#include <string.h>
#include "rpc_press_impl.h"

DEFINE_int32(dummy_port, 8888, "Port of dummy server"); 
DEFINE_string(proto, "", " user's proto files with path");
DEFINE_string(inc, "", "Include paths for proto, separated by semicolon(;)");
DEFINE_string(method, "example.EchoService.Echo", "The full method name");
DEFINE_string(server, "0.0.0.0:8002", "ip:port of the server when -load_balancer is empty, the naming service otherwise");
DEFINE_string(input, "", "The file containing requests in json format");
DEFINE_string(output, "", "The file containing responses in json format");
DEFINE_string(lb_policy, "", "The load balancer algorithm: rr, random, la, c_murmurhash, c_md5");
DEFINE_int32(thread_num, 0, "Number of threads to send requests. 0: automatically chosen according to -qps");
DEFINE_string(protocol, "baidu_std", "baidu_std hulu_pbrpc sofa_pbrpc http public_pbrpc nova_pbrpc ubrpc_compack...");
DEFINE_string(connection_type, "", "Type of connections: single, pooled, short");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in milliseconds");
DEFINE_int32(connection_timeout_ms, 500, " connection timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Maximum retry times by RPC framework");
DEFINE_int32(request_compress_type, 0, "Snappy:1 Gzip:2 Zlib:3 LZ4:4 None:0");
DEFINE_int32(response_compress_type, 0, "Snappy:1 Gzip:2 Zlib:3 LZ4:4 None:0");
DEFINE_int32(attachment_size, 0, "Carry so many byte attachment along with requests"); 
DEFINE_int32(duration, 0, "how many seconds the press keep");
DEFINE_int32(qps, 100 , "how many calls  per seconds");
DEFINE_bool(pretty, true, "output pretty jsons");

bool set_press_options(pbrpcframework::PressOptions* options){
    size_t dot_pos = FLAGS_method.find_last_of('.');
    if (dot_pos == std::string::npos) {
        LOG(ERROR) << "-method must be in form of: package.service.method";
        return false;
    }
    options->service = FLAGS_method.substr(0, dot_pos);
    options->method = FLAGS_method.substr(dot_pos + 1);
    options->lb_policy = FLAGS_lb_policy;
    options->test_req_rate = FLAGS_qps;
    if (FLAGS_thread_num > 0) {
        options->test_thread_num = FLAGS_thread_num;
    } else {
        if (FLAGS_qps <= 0) { // unlimited qps
            options->test_thread_num = 50;
        } else {
            options->test_thread_num = FLAGS_qps / 10000;
            if (options->test_thread_num < 1) {
                options->test_thread_num = 1;
            }
            if (options->test_thread_num > 50) {
                options->test_thread_num = 50;
            }
        }
    }

    const int rate_limit_per_thread = 1000000;
    double req_rate_per_thread = options->test_req_rate / options->test_thread_num;
    if (req_rate_per_thread > rate_limit_per_thread) {
        LOG(ERROR) << "req_rate: " << (int64_t) req_rate_per_thread << " is too large in one thread. The rate limit is " 
                <<  rate_limit_per_thread << " in one thread";
        return false;  
    }

    options->input = FLAGS_input;
    options->output = FLAGS_output;
    options->connection_type = FLAGS_connection_type;
    options->connect_timeout_ms = FLAGS_connection_timeout_ms;
    options->timeout_ms = FLAGS_timeout_ms;
    options->max_retry = FLAGS_max_retry;
    options->protocol = FLAGS_protocol;
    options->request_compress_type = FLAGS_request_compress_type;
    options->response_compress_type = FLAGS_response_compress_type;
    options->attachment_size = FLAGS_attachment_size;
    options->host = FLAGS_server;
    options->proto_file = FLAGS_proto;
    options->proto_includes = FLAGS_inc;
    return true;
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    // set global log option

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    pbrpcframework::PressOptions options;
    if (!set_press_options(&options)) {
        return -1;
    }
    pbrpcframework::RpcPress* rpc_press = new pbrpcframework::RpcPress;
    if (0 != rpc_press->init(&options)) {
        LOG(FATAL) << "Fail to init rpc_press";
        return -1;
    }

    rpc_press->start();
    if (FLAGS_duration <= 0) {
        while (!brpc::IsAskedToQuit()) {
            sleep(1);
        }
    } else {
        sleep(FLAGS_duration);
    }
    rpc_press->stop();
    // NOTE(gejun): Can't delete rpc_press on exit. It's probably
    // used by concurrently running done.
    return 0;
}
