// Copyright (c) 2014 Baidu, Inc.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010-12-04 11:59

#include <gtest/gtest.h>
#include "butil/class_name.h"
#include "butil/logging.h"

namespace butil {
namespace foobar {
struct MyClass {};
}
}

namespace {
class ClassNameTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        srand(time(0));
    };
};

TEST_F(ClassNameTest, demangle) {
    ASSERT_EQ("add_something", butil::demangle("add_something"));
    ASSERT_EQ("guard variable for butil::my_ip()::ip",
              butil::demangle("_ZGVZN5butil5my_ipEvE2ip"));
    ASSERT_EQ("dp::FiberPBCommand<proto::PbRouteTable, proto::PbRouteAck>::marshal(dp::ParamWriter*)::__FUNCTION__",
              butil::demangle("_ZZN2dp14FiberPBCommandIN5proto12PbRouteTableENS1_10PbRouteAckEE7marshalEPNS_11ParamWriterEE12__FUNCTION__"));
    ASSERT_EQ("7&8", butil::demangle("7&8"));
}

TEST_F(ClassNameTest, class_name_sanity) {
    ASSERT_EQ("char", butil::class_name_str('\0'));
    ASSERT_STREQ("short", butil::class_name<short>());
    ASSERT_EQ("long", butil::class_name_str(1L));
    ASSERT_EQ("unsigned long", butil::class_name_str(1UL));
    ASSERT_EQ("float", butil::class_name_str(1.1f));
    ASSERT_EQ("double", butil::class_name_str(1.1));
    ASSERT_STREQ("char*", butil::class_name<char*>());
    ASSERT_STREQ("char const*", butil::class_name<const char*>());
    ASSERT_STREQ("butil::foobar::MyClass", butil::class_name<butil::foobar::MyClass>());

    int array[32];
    ASSERT_EQ("int [32]", butil::class_name_str(array));

    LOG(INFO) << butil::class_name_str(this);
    LOG(INFO) << butil::class_name_str(*this);
}
}
