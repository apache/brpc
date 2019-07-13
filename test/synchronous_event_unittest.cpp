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
#include "butil/synchronous_event.h"

namespace {
class SynchronousEventTest : public ::testing::Test{
protected:
    SynchronousEventTest(){
    };
    virtual ~SynchronousEventTest(){};
    virtual void SetUp() {
        srand(time(0));
    };
    virtual void TearDown() {
    };
};

struct Foo {};

typedef butil::SynchronousEvent<int, int*> FooEvent;

FooEvent foo_event;
std::vector<std::pair<int, int> > result;

class FooObserver : public FooEvent::Observer {
public:
    FooObserver() : another_ob(NULL) {}
    
    void on_event(int x, int* p) {
        ++*p;
        result.push_back(std::make_pair(x, *p));
        if (another_ob) {
            foo_event.subscribe(another_ob);
        }
    }
    FooObserver* another_ob;
};


TEST_F(SynchronousEventTest, sanity) {
    const size_t N = 10;
    FooObserver foo_observer;
    FooObserver foo_observer2;
    foo_observer.another_ob = &foo_observer2;
    foo_event.subscribe(&foo_observer);
    int v = 0;
    result.clear();
    for (size_t i = 0; i < N; ++i) {
        foo_event.notify(i, &v);
    }
    ASSERT_EQ(2*N, result.size());
    for (size_t i = 0; i < 2*N; ++i) {
        ASSERT_EQ((int)i/2, result[i].first);
        ASSERT_EQ((int)i+1, result[i].second);
    }
}

}
