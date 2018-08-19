// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <gtest/gtest.h>
#include <google/protobuf/stubs/common.h>
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
