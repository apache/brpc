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

// A client sending requests to server every 1 second.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#include "echo.pb.h"
using namespace std;
DEFINE_string(attachment, "", "Carry this along with requests");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_int32(interval_ms, 1000, "Milliseconds between consecutive requests");


void *fun(void *args){
    std::cout<<"fun:\n";
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    // brpc::Channel channel;
    LOG(INFO)<<endl;
    LOG(INFO) <<"测试\n"<<std::endl<<"!";
    LOG(INFO) <<"测试2\n"<<std::endl<<"!";
    LOG(INFO) <<"测试7\n"<<std::endl<<"!";
    bthread_t tid1=2;
    bthread_t tid2;
    std::cout<<"嘿嘿嘿我是tid1:"<<tid1<<std::endl;
    std::cout<<"我是tid2:"<<tid2<<std::endl;
    bthread_start_background(&tid1, NULL, fun, NULL);
    std::cout<<"我是tid1第一次:"<<tid1<<std::endl;
    bthread_start_background(&tid1, NULL, fun, NULL);
    std::cout<<"我是tid1第二次:"<<tid1<<std::endl;
    std::cout<<"我是tid2:"<<tid2<<std::endl;
    return 0;
}


