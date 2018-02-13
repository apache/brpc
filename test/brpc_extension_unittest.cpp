// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/extension.h"

class ExtensionTest : public ::testing::Test{
protected:
    ExtensionTest(){
    };
    virtual ~ExtensionTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

inline brpc::Extension<const int>* ConstIntExtension() {
    return brpc::Extension<const int>::instance();
}

inline brpc::Extension<int>* IntExtension() {
    return brpc::Extension<int>::instance();
}


const int g_foo = 10;
const int g_bar = 20;
    
TEST_F(ExtensionTest, basic) {
    ConstIntExtension()->Register("foo", NULL);
    ConstIntExtension()->Register("foo", &g_foo);
    ConstIntExtension()->Register("bar", &g_bar);

    int* val1 = new int(0xbeef);
    int* val2 = new int(0xdead);
    ASSERT_EQ(0, IntExtension()->Register("hello", val1));
    ASSERT_EQ(-1, IntExtension()->Register("hello", val1));
    IntExtension()->Register("there", val2);
    ASSERT_EQ(val1, IntExtension()->Find("hello"));
    ASSERT_EQ(val2, IntExtension()->Find("there"));
    std::ostringstream os;
    IntExtension()->List(os, ':');
    ASSERT_EQ("hello:there", os.str());

    os.str("");
    ConstIntExtension()->List(os, ':');
    ASSERT_EQ("bar:foo", os.str());
}
