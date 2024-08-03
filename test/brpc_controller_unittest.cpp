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

// brpc - A framework to host and access services throughout Baidu.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <gtest/gtest.h>
#include <google/protobuf/stubs/common.h>
#include "butil/logging.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/controller.h"

class ControllerTest : public ::testing::Test{
protected:
    ControllerTest() {};
    virtual ~ControllerTest(){};
    virtual void SetUp() {};
    virtual void TearDown() {};
};

void MyCancelCallback(bool* cancel_flag) {
    *cancel_flag = true;
}

TEST_F(ControllerTest, notify_on_failed) {
    brpc::SocketId id = 0;
    ASSERT_EQ(0, brpc::Socket::Create(brpc::SocketOptions(), &id));

    brpc::Controller cntl;
    cntl._current_call.peer_id = id;
    ASSERT_FALSE(cntl.IsCanceled());

    bool cancel = false;
    cntl.NotifyOnCancel(brpc::NewCallback(&MyCancelCallback, &cancel));
    // Trigger callback
    brpc::Socket::SetFailed(id);
    usleep(20000); // sleep a while to wait for the canceling which will be
                   // happening in another thread.
    ASSERT_TRUE(cancel);
    ASSERT_TRUE(cntl.IsCanceled());
}

TEST_F(ControllerTest, notify_on_destruction) {
    brpc::SocketId id = 0;
    ASSERT_EQ(0, brpc::Socket::Create(brpc::SocketOptions(), &id));

    brpc::Controller* cntl = new brpc::Controller;
    cntl->_current_call.peer_id = id;
    ASSERT_FALSE(cntl->IsCanceled());

    bool cancel = false;
    cntl->NotifyOnCancel(brpc::NewCallback(&MyCancelCallback, &cancel));
    // Trigger callback
    delete cntl;
    ASSERT_TRUE(cancel);
}

#if ! BRPC_WITH_GLOG

static bool endsWith(const std::string& s1, const butil::StringPiece& s2)  {
    if (s1.size() < s2.size()) {
        return false;
    }
    return memcmp(s1.data() + s1.size() - s2.size(), s2.data(), s2.size()) == 0;
}
static bool startsWith(const std::string& s1, const butil::StringPiece& s2)  {
    if (s1.size() < s2.size()) {
        return false;
    }
    return memcmp(s1.data(), s2.data(), s2.size()) == 0;
}

DECLARE_bool(log_as_json);

TEST_F(ControllerTest, SessionKV) {
    FLAGS_log_as_json = false;
    logging::StringSink sink1;
    auto oldSink = logging::SetLogSink(&sink1);
    {
        brpc::Controller cntl;
        cntl.set_log_id(123); // not working now
        // set
        cntl.SessionKV().Set("Apple", 1234567);    
        cntl.SessionKV().Set("Baidu", "Building");
        // get
        auto v1 = cntl.SessionKV().Get("Apple");
        ASSERT_TRUE(v1);
        ASSERT_EQ("1234567", *v1);
        auto v2 = cntl.SessionKV().Get("Baidu");
        ASSERT_TRUE(v2);
        ASSERT_EQ("Building", *v2);

        // override
        cntl.SessionKV().Set("Baidu", "NewStuff");
        v2 = cntl.SessionKV().Get("Baidu");
        ASSERT_TRUE(v2);
        ASSERT_EQ("NewStuff", *v2);

        cntl.SessionKV().Set("Cisco", 33.33);

        CLOGW(&cntl) << "My WARNING Log";
        ASSERT_TRUE(endsWith(sink1, "] My WARNING Log")) << sink1;
        ASSERT_TRUE(startsWith(sink1, "W")) << sink1;
        sink1.clear();

        cntl.set_request_id("abcdEFG-456");
        CLOGE(&cntl) << "My ERROR Log";
        ASSERT_TRUE(endsWith(sink1, "] @rid=abcdEFG-456 My ERROR Log")) << sink1;
        ASSERT_TRUE(startsWith(sink1, "E")) << sink1;
        sink1.clear();

        FLAGS_log_as_json = true;
    }
    ASSERT_TRUE(endsWith(sink1, R"(,"@rid":"abcdEFG-456","M":"Session ends.","Cisco":"33.330000","Apple":"1234567","Baidu":"NewStuff"})")) << sink1;
    ASSERT_TRUE(startsWith(sink1, R"({"L":"I",)")) << sink1;

    logging::SetLogSink(oldSink);
}
#endif
