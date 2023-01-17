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

#include <stdio.h>
#include <gtest/gtest.h>
#include <vector>
#include "butil/string_printf.h"
#include "butil/files/temp_file.h"
#include "brpc/socket.h"
#include "brpc/channel.h"
#include "brpc/load_balancer.h"
#include "brpc/policy/file_naming_service.h"

class NamingServiceFilterTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
}; 

class MyNSFilter: public brpc::NamingServiceFilter {
public:
    bool Accept(const brpc::ServerNode& node) const {
        return node.tag == "enable";
    }
};

TEST_F(NamingServiceFilterTest, sanity) {
    std::vector<brpc::ServerNode> servers;
    const char *address_list[] =  {
        "10.127.0.1:1234",
        "10.128.0.1:1234 enable",
        "10.129.0.1:1234",
        "localhost:1234",
        "baidu.com:1234"
    };
    butil::TempFile tmp_file;
    {
        FILE* fp = fopen(tmp_file.fname(), "w");
        for (size_t i = 0; i < ARRAY_SIZE(address_list); ++i) {
            ASSERT_TRUE(fprintf(fp, "%s\n", address_list[i]));
        }
        fclose(fp);
    }
    MyNSFilter ns_filter;
    brpc::Channel channel;
    brpc::ChannelOptions opt;
    opt.ns_filter = &ns_filter;
    std::string ns = std::string("file://") + tmp_file.fname();
    ASSERT_EQ(0, channel.Init(ns.c_str(), "rr", &opt));

    butil::EndPoint ep;
    ASSERT_EQ(0, butil::hostname2endpoint("10.128.0.1:1234", &ep));
    for (int i = 0; i < 10; ++i) {
        brpc::SocketUniquePtr tmp_sock;
        brpc::LoadBalancer::SelectIn sel_in = { 0, false, false, 0, NULL };
        brpc::LoadBalancer::SelectOut sel_out(&tmp_sock);
        ASSERT_EQ(0, channel._lb->SelectServer(sel_in, &sel_out));
        ASSERT_EQ(ep, tmp_sock->remote_side());
    }
}
