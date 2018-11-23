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

// brpc - A framework to host and access services throughout Baidu.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "brpc/socket.h"
#include "bthread/bthread.h"
#include "brpc/socket_map.h"
#include "brpc/reloadable_flags.h"

namespace brpc {
DECLARE_int32(idle_timeout_second);
DECLARE_int32(defer_close_second);
DECLARE_int32(max_connection_pool_size);
DECLARE_int32(max_connection_multi_size);
DECLARE_int32(threshold_for_switch_multi_connection);
} // namespace brpc

namespace {
butil::EndPoint g_endpoint;
brpc::SocketMapKey g_key(g_endpoint);
volatile bool multiple_stop = false;

void* worker(void*) {
    const int ROUND = 2;
    const int COUNT = 1000;
    brpc::SocketId id;
    for (int i = 0; i < ROUND * 2; ++i) {
        for (int j = 0; j < COUNT; ++j) {
            if (i % 2 == 0) {
                EXPECT_EQ(0, brpc::SocketMapInsert(g_key, &id));
            } else {
                brpc::SocketMapRemove(g_key);
            }
        }
    }
    return NULL;
}

void* GetMultiConnection(void* arg) {
    brpc::SocketId main_id = *reinterpret_cast<brpc::SocketId*>(arg);
    brpc::SocketUniquePtr main_ptr;
    if(0 == brpc::Socket::Address(main_id, &main_ptr)) {
        brpc::SocketUniquePtr ptr;
        if(0 == main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_MULTI)) {
            return ptr.release();
        }
    }
    return nullptr;
}

void* SendMultiRequest(void* arg) {
    brpc::SocketId main_id = *reinterpret_cast<brpc::SocketId*>(arg);
    brpc::SocketUniquePtr main_ptr;
    std::map<brpc::SocketId, uint64_t>* multi_count = new std::map<brpc::SocketId, uint64_t>;
    if(0 == brpc::Socket::Address(main_id, &main_ptr)) {
        while (!multiple_stop) {
            brpc::SocketUniquePtr ptr;
            if(0 == main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_MULTI)) {
                auto insert = multi_count->emplace(ptr->id(), 1);
                if (!insert.second) {
                    ++(insert.first->second);
                }
                bthread_usleep(butil::fast_rand_less_than(2000) + 500);
                ptr->ReturnToGroup();
            } else {
                std::cout << "Failed to get a multi socket" << std::endl;
                EXPECT_TRUE(false);
                bthread_usleep(butil::fast_rand_less_than(2000) + 500);
            }
        }
    }

    return multi_count;
}

class SocketMapTest : public ::testing::Test{
protected:
    SocketMapTest(){};
    virtual ~SocketMapTest(){};
    virtual void SetUp(){};
    virtual void TearDown(){};
};

TEST_F(SocketMapTest, idle_timeout) {
    const int TIMEOUT = 1;
    const int NTHREAD = 10;
    brpc::FLAGS_defer_close_second = TIMEOUT;
    pthread_t tids[NTHREAD];
    for (int i = 0; i < NTHREAD; ++i) {
        ASSERT_EQ(0, pthread_create(&tids[i], NULL, worker, NULL));
    }
    for (int i = 0; i < NTHREAD; ++i) {
        ASSERT_EQ(0, pthread_join(tids[i], NULL));
    }
    brpc::SocketId id;
    // Socket still exists since it has not reached timeout yet
    ASSERT_EQ(0, brpc::SocketMapFind(g_key, &id));
    usleep(TIMEOUT * 1000000L + 1100000L);
    // Socket should be removed after timeout
    ASSERT_EQ(-1, brpc::SocketMapFind(g_key, &id));

    brpc::FLAGS_defer_close_second = TIMEOUT * 10;
    ASSERT_EQ(0, brpc::SocketMapInsert(g_key, &id));
    brpc::SocketMapRemove(g_key);
    ASSERT_EQ(0, brpc::SocketMapFind(g_key, &id));
    // Change `FLAGS_idle_timeout_second' to 0 to disable checking
    brpc::FLAGS_defer_close_second = 0;
    usleep(1100000L);
    // And then Socket should be removed
    ASSERT_EQ(-1, brpc::SocketMapFind(g_key, &id));

    brpc::SocketId main_id;
    ASSERT_EQ(0, brpc::SocketMapInsert(g_key, &main_id));
    brpc::FLAGS_idle_timeout_second = TIMEOUT;
    brpc::SocketUniquePtr main_ptr;
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
    ASSERT_EQ(0, main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_POOLED));
    ASSERT_TRUE(main_ptr.get());
    main_ptr.reset();
    id = ptr->id();
    ptr->ReturnToGroup();
    ptr.reset(NULL);
    usleep(TIMEOUT * 1000000L + 2000000L);
    // Pooled connection should be `ReleaseAdditionalReference',
    // which destroyed the Socket. As a result `GetSocketFromPool'
    // should return a new one
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
    ASSERT_EQ(0, main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_POOLED));
    ASSERT_TRUE(main_ptr.get());
    main_ptr.reset();
    ASSERT_NE(id, ptr->id());
    brpc::SocketMapRemove(g_key);
}

TEST_F(SocketMapTest, max_pool_size) {
    const int MAXSIZE = 5;
    const int TOTALSIZE = MAXSIZE + 5;
    brpc::FLAGS_max_connection_pool_size = MAXSIZE;

    brpc::SocketId main_id;
    ASSERT_EQ(0, brpc::SocketMapInsert(g_key, &main_id));

    brpc::SocketUniquePtr ptrs[TOTALSIZE];
    for (int i = 0; i < TOTALSIZE; ++i) {
        brpc::SocketUniquePtr main_ptr;
        ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
        ASSERT_EQ(0, main_ptr->GetSocketFromGroup(&ptrs[i], brpc::CONNECTION_TYPE_POOLED));
        ASSERT_TRUE(main_ptr.get());
        main_ptr.reset();
    }
    for (int i = 0; i < TOTALSIZE; ++i) {
        ASSERT_EQ(0, ptrs[i]->ReturnToGroup());
    }
    std::vector<brpc::SocketId> ids;
    brpc::SocketUniquePtr main_ptr;
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
    main_ptr->ListSocketsOfGroup(&ids);
    EXPECT_EQ(MAXSIZE, (int)ids.size());
    // The last few Sockets should be `SetFailed' by `ReturnSocketToPool'
    for (int i = MAXSIZE; i < TOTALSIZE; ++i) {
        EXPECT_TRUE(ptrs[i]->Failed());
    }

    // When no pending rpc is on connection group. The shardpart reference number should be 1
    // due to only main_socket is refer to the sharedpart.
    int ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    ASSERT_EQ(ref_num, 1);

    brpc::SocketMapRemove(g_key);
}

TEST_F(SocketMapTest, max_multi_connection_size) {
    const int MAXSIZE = 5;
    const int THRESHOLD = 3;
    brpc::FLAGS_max_connection_multi_size = MAXSIZE;
    brpc::FLAGS_threshold_for_switch_multi_connection = THRESHOLD;

    brpc::SocketId main_id;
    ASSERT_EQ(0, brpc::SocketMapInsert(g_key, &main_id));

    size_t times = MAXSIZE * (THRESHOLD + 1);
    std::vector<pthread_t> tids(times);
    for (size_t i = 0; i != times; ++i) {
        ASSERT_EQ(0, pthread_create(&tids[i], NULL, GetMultiConnection, &main_id));
    }

    std::vector<void*> retval(times);
    for (size_t i = 0; i != times; ++i) {
        ASSERT_EQ(0, pthread_join(tids[i], &retval[i]));
    }

    // Created sockets reach the max number.
    brpc::SocketUniquePtr main_ptr;
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
    std::vector<brpc::SocketId> ids;
    main_ptr->ListSocketsOfGroup(&ids);
    ASSERT_EQ(MAXSIZE, (int)ids.size());

    for (size_t i = 0; i != times; ++i) {
        brpc::Socket* sock = reinterpret_cast<brpc::Socket*>(retval[i]);
        ASSERT_TRUE(sock != nullptr); 
        brpc::SocketUniquePtr ptr(sock);
        ptr->ReturnToGroup();
    }

    std::vector<brpc::SocketId> out;
    main_ptr->ListSocketsOfGroup(&out);
    int ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    size_t active_connections = 0;
    for (const brpc::SocketId sid : out) {
        brpc::SocketUniquePtr ptr;
        if (brpc::Socket::Address(sid, &ptr) == 0) {
            ++active_connections;
            ptr->ReleaseAdditionalReference();
        }
    }
    std::cout << "mulit_size=" << out.size() << " ref_num=" << ref_num << " actives=" 
        << active_connections << std::endl; 

    // When no pending rpc is on connection group. The shardpart reference number should be 1
    // due to only main_socket is refer to the sharedpart.
    ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    ASSERT_EQ(ref_num, 1);
    brpc::SocketMapRemove(g_key);
}

TEST_F(SocketMapTest, fairness_multi_connections) {
    const int MAXSIZE = 5;
    const int THRESHOLD = 3;
    brpc::FLAGS_max_connection_multi_size = MAXSIZE;
    brpc::FLAGS_threshold_for_switch_multi_connection = THRESHOLD;
    brpc::SocketId main_id;
    ASSERT_EQ(0, brpc::SocketMapInsert(g_key, &main_id));
    brpc::SocketUniquePtr main_ptr;
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));

    size_t times = MAXSIZE * (THRESHOLD + 1);
    std::vector<pthread_t> tids(times);
    std::cout << "Time start ..." << std::endl;
    butil::Timer tm;
    tm.start();

    for (size_t i = 0; i != tids.size(); ++i) {
        ASSERT_EQ(0, pthread_create(&tids[i], NULL, SendMultiRequest, &main_id));
    }
    bthread_usleep(5*1000*1000);
    
    std::cout << "Time end ..." << std::endl;
    multiple_stop = true;
    std::vector<void*> retval(tids.size());
    for (size_t i = 0; i != tids.size(); ++i) {
        ASSERT_EQ(0, pthread_join(tids[i], &retval[i]));
    }
    tm.stop();

    std::map<brpc::SocketId, uint64_t> total_count;
    for (auto count : retval) {
        auto* p = reinterpret_cast<std::map<brpc::SocketId, uint64_t>*>(count);
        for (const auto& id_count : *p) {
            auto insert = total_count.insert(id_count);
            if (!insert.second) {
                insert.first->second += id_count.second;
            }
        }
        delete p;
    }
    uint64_t count_sum = 0;
    uint64_t count_squared_sum = 0;
    uint64_t min_count = (uint64_t)-1;
    uint64_t max_count = 0;
    for (auto it = total_count.begin(); it != total_count.end(); ++it) {
        uint64_t n = it->second;
        if (n > max_count) { max_count = n; }
        if (n < min_count) { min_count = n; }
        count_sum += n;
        count_squared_sum += n * n; 
    }
    ASSERT_TRUE(min_count > 0);
    const size_t num = total_count.size();
    std::cout << '\n'
              << ": elapse_milliseconds=" << tm.u_elapsed() * 1000
              << " total_count=" << count_sum
              << " min_count=" << min_count
              << " max_count=" << max_count
              << " average=" << count_sum / num
              << " deviation=" << sqrt(count_squared_sum * num 
                  - count_sum * count_sum) / num << std::endl;

    std::vector<brpc::SocketId> out;
    main_ptr->ListSocketsOfGroup(&out);
    int ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    size_t active_connections = 0;
    for (const brpc::SocketId sid : out) {
        brpc::SocketUniquePtr ptr;
        if (brpc::Socket::Address(sid, &ptr) == 0) {
            ++active_connections;
            ptr->ReleaseAdditionalReference();
        }
    }
    std::cout << "mulit_size=" << out.size() << " ref_num=" << ref_num << " actives=" 
        << active_connections << std::endl;

    // When no pending rpc is on connection group. The shardpart reference number should be 1
    // due to only main_socket is refer to the sharedpart.
    ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    ASSERT_EQ(ref_num, 1);

    brpc::SocketMapRemove(g_key);
}

} //namespace

int main(int argc, char* argv[]) {
    butil::str2endpoint("127.0.0.1:12345", &g_key.peer.addr);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
