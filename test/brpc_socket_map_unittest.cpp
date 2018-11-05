// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

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
DECLARE_int32(max_connection_multiple_size);
DECLARE_int32(threshold_for_switch_multiple_connection);
} // namespace brpc

namespace {
butil::EndPoint g_endpoint;
brpc::SocketMapKey g_key(g_endpoint);
std::map<brpc::SocketId, butil::atomic<uint64_t>> g_multi_count;
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
        if(0 == main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_MULTIPLE)) {
            return ptr.release(
        }
    }
    return nullptr;
}

void* SendMultiRequest(void* arg) {
    brpc::SocketId main_id = *reinterpret_cast<brpc::SocketId*>(arg);
    brpc::SocketUniquePtr main_ptr;
    if(0 == brpc::Socket::Address(main_id, &main_ptr)) {
        while (!multiple_stop) {
            brpc::SocketUniquePtr ptr;
            if(0 == main_ptr->GetSocketFromGroup(&ptr, brpc::CONNECTION_TYPE_MULTI)) {
                g_multi_count.at(ptr->id()).fetch_add(1, butil::memory_order_relaxed);
                bthread_usleep(butil::fast_rand_less_than(2000) + 500);
                ptr->ReturnToGroup();
            } else {
                bthread_usleep(butil::fast_rand_less_than(2000) + 500);
            }
        }
    }

    return nullptr;
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

TEST_F(SocketMapTest, max_multiple_size) {
    const int MAXSIZE = 5;
    const int THRESHOLD = 3;
    brpc::FLAGS_max_connection_multiple_size = MAXSIZE;
    brpc::FLAGS_threshold_for_switch_multiple_connection = THRESHOLD;

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

    // When no pending rpc is on connection group. The shardpart reference number should be 1
    // due to only main_socket is refer to the sharedpart.
    int ref_num = 0;
    ASSERT_TRUE(main_ptr->GetSharedPartRefNum(&ref_num));
    ASSERT_EQ(ref_num, 1);
    brpc::SocketMapRemove(g_key);
}

TEST_F(SocketMapTest, fairness_multiple_connections) {
    const int MAXSIZE = 5;
    const int THRESHOLD = 3;
    brpc::FLAGS_max_connection_multiple_size = MAXSIZE;
    brpc::FLAGS_threshold_for_switch_multiple_connection = THRESHOLD;
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
    for (size_t i = 0; i != times; ++i) {
        brpc::Socket* sock = reinterpret_cast<brpc::Socket*>(retval[i]);
        ASSERT_TRUE(sock != nullptr); 
        brpc::SocketUniquePtr ptr(sock);
        ptr->ReturnToGroup();
    }

    brpc::SocketUniquePtr main_ptr;
    ASSERT_EQ(0, brpc::Socket::Address(main_id, &main_ptr));
    std::vector<brpc::SocketId> ids;
    main_ptr->ListSocketsOfGroup(&ids);
    ASSERT_EQ(MAXSIZE, (int)ids.size());
    for (const auto id : ids) {
        g_multi_count[id] = 0;
    }     

    std::cout << "Time start ..." << std::endl;
    butil::Timer tm;
    tm.start();

    tids.resize(20, 0);
    for (size_t i = 0; i != tids.size(); ++i) {
        ASSERT_EQ(0, pthread_create(&tids[i], NULL, SendMultiRequest, &main_id));
    }
    bthread_usleep(5*1000*1000);
    
    std::cout << "Time end ..." << std::endl;
    multiple_stop = true;
    for (const auto tid : tids) {
        ASSERT_EQ(0, pthread_join(tid, nullptr));
    }
    tm.stop();
    
    uint64_t count_sum = 0;
    uint64_t count_squared_sum = 0;
    uint64_t min_count = (uint64_t)-1;
    uint64_t max_count = 0;
    for (auto it = g_multi_count.begin(); it != g_multi_count.end(); ++it) {
        uint64_t n = it->second;
        if (n > max_count) { max_count = n; }
        if (n < min_count) { min_count = n; }
        count_sum += n;
        count_squared_sum += n * n; 
    }
    ASSERT_TRUE(min_count > 0);
    ASSERT_EQ(g_multi_count.size(), ids.size());
    const size_t num = g_multi_count.size();
    std::cout << '\n'
              << ": elapse_milliseconds=" << tm.u_elapsed() * 1000
              << " total_count=" << count_sum
              << " min_count=" << min_count
              << " max_count=" << max_count
              << " average=" << count_sum / num
              << " deviation=" << sqrt(count_squared_sum * num 
                  - count_sum * count_sum) / num << std::endl;
    ASSERT_TRUE((max_count - min_count) < min_count * 0.1);

    // When no pending rpc is on connection group. The shardpart reference number should be 1
    // due to only main_socket is refer to the sharedpart.
    int ref_num = 0;
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
