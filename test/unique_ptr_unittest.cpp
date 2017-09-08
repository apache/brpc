// Copyright (c) 2014 Baidu, Inc.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010-12-04 11:59

#include <gtest/gtest.h>
#include "butil/unique_ptr.h"

namespace {

class UniquePtrTest : public testing::Test {
protected:
    virtual void SetUp() {
    };
};

struct Foo {
    Foo() : destroyed(false), called_func(false) {}
    void destroy() { destroyed = true; }
    void func() { called_func = true; }
    bool destroyed;
    bool called_func;
};

struct FooDeleter {
    void operator()(Foo* f) const {
        f->destroy();
    }
};

TEST_F(UniquePtrTest, basic) {
    Foo foo;
    ASSERT_FALSE(foo.destroyed);
    ASSERT_FALSE(foo.called_func);
    {
        std::unique_ptr<Foo, FooDeleter> foo_ptr(&foo);
        foo_ptr->func();
        ASSERT_TRUE(foo.called_func);
    }
    ASSERT_TRUE(foo.destroyed);
}

static std::unique_ptr<Foo> generate_foo(Foo* foo) {
    std::unique_ptr<Foo> foo_ptr(foo);
    return foo_ptr;
}

TEST_F(UniquePtrTest, return_unique_ptr) {
    Foo* foo = new Foo;
    std::unique_ptr<Foo> foo_ptr = generate_foo(foo);
    ASSERT_EQ(foo, foo_ptr.get());
}

}
