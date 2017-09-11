// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"

namespace {

class CachelineTest : public testing::Test {
};

struct BAIDU_CACHELINE_ALIGNMENT Bar {
    int y;
};

struct Foo {
    char dummy1[0];
    int z;
    int BAIDU_CACHELINE_ALIGNMENT x[0];
    int y;
    int m;
    Bar bar;
};

TEST_F(CachelineTest, cacheline_alignment) {
    ASSERT_EQ(64u, offsetof(Foo, x));
    ASSERT_EQ(64u, offsetof(Foo, y));
    ASSERT_EQ(68u, offsetof(Foo, m));
    ASSERT_EQ(128u, offsetof(Foo, bar));
    ASSERT_EQ(64u, sizeof(Bar));
    ASSERT_EQ(192u, sizeof(Foo));
}

}
