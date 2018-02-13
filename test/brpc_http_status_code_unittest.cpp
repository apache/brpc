// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// File: test_http_status_code.cpp
// Date: 2014/11/04 18:33:39

#include <gtest/gtest.h>
#include "brpc/http_status_code.h"

class HttpStatusTest : public testing::Test {
    void SetUp() {}
    void TearDown() {}
};

TEST_F(HttpStatusTest, sanity) {
    ASSERT_STREQ("OK", brpc::HttpReasonPhrase(
                     brpc::HTTP_STATUS_OK));
    ASSERT_STREQ("Continue", brpc::HttpReasonPhrase(
                     brpc::HTTP_STATUS_CONTINUE));
    ASSERT_STREQ("HTTP Version Not Supported", brpc::HttpReasonPhrase(
                     brpc::HTTP_STATUS_VERSION_NOT_SUPPORTED));
    ASSERT_STREQ("Unknown status code (-2)", brpc::HttpReasonPhrase(-2));
}
