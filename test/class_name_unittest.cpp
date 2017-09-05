// Copyright (c) 2014 Baidu, Inc.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010-12-04 11:59

#include <gtest/gtest.h>
#include "base/class_name.h"
#include "base/logging.h"

namespace base {
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
    ASSERT_EQ("add_something", base::demangle("add_something"));
    ASSERT_EQ("guard variable for base::my_ip()::ip",
              base::demangle("_ZGVZN4base5my_ipEvE2ip"));
    ASSERT_EQ("dp::FiberPBCommand<proto::PbRouteTable, proto::PbRouteAck>::marshal(dp::ParamWriter*)::__FUNCTION__",
              base::demangle("_ZZN2dp14FiberPBCommandIN5proto12PbRouteTableENS1_10PbRouteAckEE7marshalEPNS_11ParamWriterEE12__FUNCTION__"));
    ASSERT_EQ("7&8", base::demangle("7&8"));
}

TEST_F(ClassNameTest, class_name_sanity) {
    ASSERT_EQ("char", base::class_name_str('\0'));
    ASSERT_STREQ("short", base::class_name<short>());
    ASSERT_EQ("long", base::class_name_str(1L));
    ASSERT_EQ("unsigned long", base::class_name_str(1UL));
    ASSERT_EQ("float", base::class_name_str(1.1f));
    ASSERT_EQ("double", base::class_name_str(1.1));
    ASSERT_STREQ("char*", base::class_name<char*>());
    ASSERT_STREQ("char const*", base::class_name<const char*>());
    ASSERT_STREQ("base::foobar::MyClass", base::class_name<base::foobar::MyClass>());

    int array[32];
    ASSERT_EQ("int [32]", base::class_name_str(array));

    LOG(INFO) << base::class_name_str(this);
    LOG(INFO) << base::class_name_str(*this);
}
}
