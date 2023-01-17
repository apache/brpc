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
