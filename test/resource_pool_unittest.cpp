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
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fast_rand.h"

#define BAIDU_CLEAR_RESOURCE_POOL_AFTER_ALL_THREADS_QUIT
#include "butil/resource_pool.h"

namespace {
struct MyObject {};

int nfoo_dtor = 0;
struct Foo {
    Foo() {
        x = butil::fast_rand() % 2;
    }
    ~Foo() {
        ++nfoo_dtor;
    }
    int x;
};
}

namespace butil {
template <> struct ResourcePoolBlockMaxSize<MyObject> {
    static const size_t value = 128;
};

template <> struct ResourcePoolBlockMaxItem<MyObject> {
    static const size_t value = 3;
};

template <> struct ResourcePoolFreeChunkMaxItem<MyObject> {
    static size_t value() { return 5; }
};

template <> struct ResourcePoolValidator<Foo> {
    static bool validate(const Foo* foo) {
        return foo->x != 0;
    }
};

}

namespace {
using namespace butil;

class ResourcePoolTest : public ::testing::Test{
protected:
    ResourcePoolTest(){
    };
    virtual ~ResourcePoolTest(){};
    virtual void SetUp() {
        srand(time(0));
    };
    virtual void TearDown() {
    };
};

TEST_F(ResourcePoolTest, atomic_array_init) {
    for (int i = 0; i < 2; ++i) {
        if (i == 0) {
            butil::atomic<int> a[2];
            a[0] = 1;
            // The folowing will cause compile error with gcc3.4.5 and the
            // reason is unknown
            // a[1] = 2;
        } else if (i == 2) {
            butil::atomic<int> a[2];
            ASSERT_EQ(0, a[0]);
            ASSERT_EQ(0, a[1]);
        }
    }
}

int nc = 0;
int nd = 0;
std::set<void*> ptr_set;
struct YellObj {
    YellObj() {
        ++nc;
        ptr_set.insert(this);
        printf("Created %p\n", this);
    }
    ~YellObj() {
        ++nd;
        ptr_set.erase(this);
        printf("Destroyed %p\n", this);
    }
    char _dummy[96];
};

TEST_F(ResourcePoolTest, change_config) {
    int a[2];
    printf("%lu\n", ARRAY_SIZE(a));
    
    ResourcePoolInfo info = describe_resources<MyObject>();
    ResourcePoolInfo zero_info = { 0, 0, 0, 0, 3, 3, 0 };
    ASSERT_EQ(0, memcmp(&info, &zero_info, sizeof(info)));

    ResourceId<MyObject> id = { 0 };
    get_resource<MyObject>(&id);
    std::cout << describe_resources<MyObject>() << std::endl;
    return_resource(id);
    std::cout << describe_resources<MyObject>() << std::endl;
}

struct NonDefaultCtorObject {
    explicit NonDefaultCtorObject(int value) : _value(value) {}
    NonDefaultCtorObject(int value, int dummy) : _value(value + dummy) {}

    int _value;
};

TEST_F(ResourcePoolTest, sanity) {
    ptr_set.clear();
    ResourceId<NonDefaultCtorObject> id0 = { 0 };
    get_resource<NonDefaultCtorObject>(&id0, 10);
    ASSERT_EQ(10, address_resource(id0)->_value);
    get_resource<NonDefaultCtorObject>(&id0, 100, 30);
    ASSERT_EQ(130, address_resource(id0)->_value);
    
    printf("BLOCK_NITEM=%lu\n", ResourcePool<YellObj>::BLOCK_NITEM);
    
    nc = 0;
    nd = 0;
     {
        ResourceId<YellObj> id1;
        YellObj* o1 = get_resource(&id1);
        ASSERT_TRUE(o1);
        ASSERT_EQ(o1, address_resource(id1));
        
        ASSERT_EQ(1, nc);
        ASSERT_EQ(0, nd);

        ResourceId<YellObj> id2;
        YellObj* o2 = get_resource(&id2);
        ASSERT_TRUE(o2);
        ASSERT_EQ(o2, address_resource(id2));

        ASSERT_EQ(2, nc);
        ASSERT_EQ(0, nd);
        
        return_resource(id1);
        ASSERT_EQ(2, nc);
        ASSERT_EQ(0, nd);

        return_resource(id2);
        ASSERT_EQ(2, nc);
        ASSERT_EQ(0, nd);
    }
    ASSERT_EQ(0, nd);

    clear_resources<YellObj>();
    ASSERT_EQ(2, nd);
    ASSERT_TRUE(ptr_set.empty()) << ptr_set.size();
}

TEST_F(ResourcePoolTest, validator) {
    nfoo_dtor = 0;
    int nfoo = 0;
    for (int i = 0; i < 100; ++i) {
        ResourceId<Foo> id = { 0 };
        Foo* foo = get_resource<Foo>(&id);
        if (foo) {
            ASSERT_EQ(1, foo->x);
            ++nfoo;
        }
    }
    ASSERT_EQ(nfoo + nfoo_dtor, 100);
    ASSERT_EQ((size_t)nfoo, describe_resources<Foo>().item_num);
}

TEST_F(ResourcePoolTest, get_int) {
    clear_resources<int>();
    
    // Perf of this test is affected by previous case.
    const size_t N = 100000;
    
    butil::Timer tm;
    ResourceId<int> id;

    // warm up
    if (get_resource(&id)) {
        return_resource(id);
    }
    ASSERT_EQ(0UL, id);
    delete (new int);
    
    tm.start();
    for (size_t i = 0; i < N; ++i) {
        *get_resource(&id) = i;
    }
    tm.stop();
    printf("get a int takes %.1fns\n", tm.n_elapsed()/(double)N);
    
    tm.start();
    for (size_t i = 0; i < N; ++i) {
        *(new int) = i;
    }    
    tm.stop();
    printf("new a int takes %luns\n", tm.n_elapsed()/N);

    tm.start();
    for (size_t i = 0; i < N; ++i) {
        id.value = i;
        *ResourcePool<int>::unsafe_address_resource(id) = i;
    }
    tm.stop();
    printf("unsafe_address a int takes %.1fns\n", tm.n_elapsed()/(double)N);
    
    tm.start();
    for (size_t i = 0; i < N; ++i) {
        id.value = i;
        *address_resource(id) = i;
    }
    tm.stop();
    printf("address a int takes %.1fns\n", tm.n_elapsed()/(double)N);

    std::cout << describe_resources<int>() << std::endl;
    clear_resources<int>();
    std::cout << describe_resources<int>() << std::endl;
}


struct SilentObj {
    char buf[sizeof(YellObj)];
};

TEST_F(ResourcePoolTest, get_perf) {
    const size_t N = 10000;
    std::vector<SilentObj*> new_list;
    new_list.reserve(N);
    ResourceId<SilentObj> id;
    
    butil::Timer tm1, tm2;

    // warm up
    if (get_resource(&id)) {
        return_resource(id);
    }
    delete (new SilentObj);

    // Run twice, the second time will be must faster.
    for (size_t j = 0; j < 2; ++j) {

        tm1.start();
        for (size_t i = 0; i < N; ++i) {
            get_resource(&id);
        }
        tm1.stop();
        printf("get a SilentObj takes %luns\n", tm1.n_elapsed()/N);
        //clear_resources<SilentObj>(); // free all blocks
        
        tm2.start();
        for (size_t i = 0; i < N; ++i) {
            new_list.push_back(new SilentObj);
        }    
        tm2.stop();
        printf("new a SilentObj takes %luns\n", tm2.n_elapsed()/N);
        for (size_t i = 0; i < new_list.size(); ++i) {
            delete new_list[i];
        }
        new_list.clear();
    }

    std::cout << describe_resources<SilentObj>() << std::endl;
}

struct D { int val[1]; };

void* get_and_return_int(void*) {
    // Perf of this test is affected by previous case.
    const size_t N = 100000;
    std::vector<ResourceId<D> > v;
    v.reserve(N);
    butil::Timer tm0, tm1, tm2;
    ResourceId<D> id = {0};
    D tmp = D();
    int sr = 0;

    // warm up
    tm0.start();
    if (get_resource(&id)) {
        return_resource(id);
    }
    tm0.stop();

    printf("[%lu] warmup=%lu\n", pthread_self(), tm0.n_elapsed());

    for (int j = 0; j < 5; ++j) {
        v.clear();
        sr = 0;
        
        tm1.start();
        for (size_t i = 0; i < N; ++i) {
            *get_resource(&id) = tmp;
            v.push_back(id);
        }
        tm1.stop();

        std::random_shuffle(v.begin(), v.end());

        tm2.start();
        for (size_t i = 0; i < v.size(); ++i) {
            sr += return_resource(v[i]);
        }
        tm2.stop();

        if (0 != sr) {
            printf("%d return_resource failed\n", sr);
        }
        
        printf("[%lu:%d] get<D>=%.1f return<D>=%.1f\n",
               pthread_self(), j, tm1.n_elapsed()/(double)N, tm2.n_elapsed()/(double)N);
    }
    return NULL;
}

void* new_and_delete_int(void*) {
    const size_t N = 100000;
    std::vector<D*> v2;
    v2.reserve(N);
    butil::Timer tm0, tm1, tm2;
    D tmp = D();

    for (int j = 0; j < 3; ++j) {
        v2.clear();
        
        // warm up
        delete (new D);

        tm1.start();
        for (size_t i = 0; i < N; ++i) {
            D *p = new D;
            *p = tmp;
            v2.push_back(p);
        }
        tm1.stop();

        std::random_shuffle(v2.begin(), v2.end());

        tm2.start();
        for (size_t i = 0; i < v2.size(); ++i) {
            delete v2[i];
        }
        tm2.stop();
        
        printf("[%lu:%d] new<D>=%.1f delete<D>=%.1f\n",
               pthread_self(), j, tm1.n_elapsed()/(double)N, tm2.n_elapsed()/(double)N);
    }
    
    return NULL;
}

TEST_F(ResourcePoolTest, get_and_return_int_single_thread) {
    get_and_return_int(NULL);
    new_and_delete_int(NULL);
}

TEST_F(ResourcePoolTest, get_and_return_int_multiple_threads) {
    pthread_t tid[16];
    for (size_t i = 0; i < ARRAY_SIZE(tid); ++i) {
        ASSERT_EQ(0, pthread_create(&tid[i], NULL, get_and_return_int, NULL));
    }
    for (size_t i = 0; i < ARRAY_SIZE(tid); ++i) {
        pthread_join(tid[i], NULL);
    }

    pthread_t tid2[16];
    for (size_t i = 0; i < ARRAY_SIZE(tid2); ++i) {
        ASSERT_EQ(0, pthread_create(&tid2[i], NULL, new_and_delete_int, NULL));
    }
    for (size_t i = 0; i < ARRAY_SIZE(tid2); ++i) {
        pthread_join(tid2[i], NULL);
    }

    std::cout << describe_resources<D>() << std::endl;
    clear_resources<D>();
    ResourcePoolInfo info = describe_resources<D>();
    ResourcePoolInfo zero_info = { 0, 0, 0, 0,
                                   ResourcePoolBlockMaxItem<D>::value,
                                   ResourcePoolBlockMaxItem<D>::value, 0};
    ASSERT_EQ(0, memcmp(&info, &zero_info, sizeof(info)));
}

TEST_F(ResourcePoolTest, verify_get) {
    clear_resources<int>();
    std::cout << describe_resources<int>() << std::endl;
                              
    std::vector<std::pair<int*, ResourceId<int> > > v;
    v.reserve(100000);
    ResourceId<int> id = { 0 };
    for (int i = 0; (size_t)i < v.capacity(); ++i)  {
        int* p = get_resource(&id);
        *p = i;
        v.push_back(std::make_pair(p, id));
    }
    int i;
    for (i = 0; (size_t)i < v.size() && *v[i].first == i; ++i);
    ASSERT_EQ(v.size(), (size_t)i);
    for (i = 0; (size_t)i < v.size() && v[i].second == (size_t)i; ++i);
    ASSERT_EQ(v.size(), (size_t)i) << "i=" << i << ", " << v[i].second;

    clear_resources<int>();
}
} // namespace
