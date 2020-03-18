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

#include <iostream>
#include "butil/time.h"
#include "butil/logging.h"
#include <brpc/memcache.h>
#include <brpc/channel.h>
#include <gtest/gtest.h>

namespace brpc {
DECLARE_int32(idle_timeout_second);
} 

int main(int argc, char* argv[]) {
    brpc::FLAGS_idle_timeout_second = 0;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {
static pthread_once_t download_memcached_once = PTHREAD_ONCE_INIT;
static pid_t g_mc_pid = -1;

static void RemoveMemcached() {
    puts("[Stopping memcached]");
    char cmd[256];
#if defined(BAIDU_INTERNAL)
    snprintf(cmd, sizeof(cmd), "kill %d; rm -rf memcached_for_test", g_mc_pid);
#else
    snprintf(cmd, sizeof(cmd), "kill %d", g_mc_pid);
#endif
    CHECK(0 == system(cmd));
    // Wait for mc to stop
    usleep(50000);
}

#define MEMCACHED_BIN "memcached"
#define MEMCACHED_PORT "11211"

static void RunMemcached() {
#if defined(BAIDU_INTERNAL)
    puts("Downloading memcached...");
    if (system("mkdir -p memcached_for_test && cd memcached_for_test && svn co https://svn.baidu.com/third-64/tags/memcached/memcached_1-4-15-100_PD_BL/bin") != 0) {
        puts("Fail to get memcached from svn");
        return;
    }
# undef MEMCACHED_BIN
# define MEMCACHED_BIN "memcached_for_test/bin/memcached";
#else
    if (system("which " MEMCACHED_BIN) != 0) {
        puts("Fail to find " MEMCACHED_BIN ", following tests will be skipped");
        return;
    }
#endif
    atexit(RemoveMemcached);

    g_mc_pid = fork();
    if (g_mc_pid < 0) {
        puts("Fail to fork");
        exit(1);
    } else if (g_mc_pid == 0) {
        puts("[Starting memcached]");
        char* const argv[] = { (char*)MEMCACHED_BIN,
                               (char*)"-p", (char*)MEMCACHED_PORT,
                               NULL };
        if (execvp(MEMCACHED_BIN, argv) < 0) {
            puts("Fail to run " MEMCACHED_BIN);
            exit(1);
        }
    }
    // Wait for memcached to start.
    usleep(50000);
}

class MemcacheTest : public testing::Test {
protected:
    MemcacheTest() {}
    void SetUp() {
        pthread_once(&download_memcached_once, RunMemcached);
    }
    void TearDown() {
    }
};

TEST_F(MemcacheTest, sanity) {
    if (g_mc_pid < 0) {
        puts("Skipped due to absence of memcached");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" MEMCACHED_PORT, &options));
    brpc::MemcacheRequest request;
    brpc::MemcacheResponse response;
    brpc::Controller cntl;

    // Clear all contents in MC which is still holding older data after
    // restarting in Ubuntu 18.04 (mc=1.5.6)
    request.Flush(0);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(response.PopFlush());

    cntl.Reset();
    request.Clear();
    request.Get("hello");
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    std::string value;
    uint32_t flags = 0;
    uint64_t cas_value = 0;
    ASSERT_FALSE(response.PopGet(&value, &flags, &cas_value));
    ASSERT_EQ("Not found", response.LastError());

    cntl.Reset();
    request.Clear();
    request.Set("hello", "world", 0xdeadbeef, 10, 0);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(response.PopSet(&cas_value)) << response.LastError();
    ASSERT_EQ("", response.LastError());

    cntl.Reset();
    request.Clear();
    request.Get("hello");
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_TRUE(response.PopGet(&value, &flags, &cas_value));
    ASSERT_EQ("", response.LastError());
    ASSERT_EQ("world", value);
    ASSERT_EQ(0xdeadbeef, flags);
    std::cout << "cas_value=" << cas_value << std::endl;

    cntl.Reset();
    request.Clear();
    request.Set("hello", "world2", 0xdeadbeef, 10,
                cas_value/*intended match*/);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    uint64_t cas_value2 = 0;
    ASSERT_TRUE(response.PopSet(&cas_value2)) << response.LastError();

    cntl.Reset();
    request.Clear();
    request.Set("hello", "world3", 0xdeadbeef, 10,
                cas_value2 + 1/*intended unmatch*/);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    uint64_t cas_value3 = ~0;
    ASSERT_FALSE(response.PopSet(&cas_value3));
    std::cout << response.LastError() << std::endl;
    ASSERT_EQ(~0ul, cas_value3);
}

TEST_F(MemcacheTest, incr_and_decr) {
    if (g_mc_pid < 0) {
        puts("Skipped due to absence of memcached");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" MEMCACHED_PORT, &options));
    brpc::MemcacheRequest request;
    brpc::MemcacheResponse response;
    brpc::Controller cntl;
    request.Increment("counter1", 2, 10, 10);
    request.Decrement("counter1", 1, 10, 10);
    request.Increment("counter1", 3, 10, 10);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    uint64_t new_value1 = 0;
    uint64_t cas_value1 = 0;
    ASSERT_TRUE(response.PopIncrement(&new_value1, &cas_value1));
    ASSERT_EQ(10ul, new_value1);
    uint64_t new_value2 = 0;
    uint64_t cas_value2 = 0;
    ASSERT_TRUE(response.PopDecrement(&new_value2, &cas_value2));
    ASSERT_EQ(9ul, new_value2);
    uint64_t new_value3 = 0;
    uint64_t cas_value3 = 0;
    ASSERT_TRUE(response.PopIncrement(&new_value3, &cas_value3));
    ASSERT_EQ(12ul, new_value3);
    std::cout << "cas1=" << cas_value1
              << " cas2=" << cas_value2
              << " cas3=" << cas_value3
              << std::endl;
}

TEST_F(MemcacheTest, version) {
    if (g_mc_pid < 0) {
        puts("Skipped due to absence of memcached");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" MEMCACHED_PORT, &options));
    brpc::MemcacheRequest request;
    brpc::MemcacheResponse response;
    brpc::Controller cntl;
    request.Version();
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    std::string version;
    ASSERT_TRUE(response.PopVersion(&version)) << response.LastError();
    std::cout << "version=" << version << std::endl;
}
} //namespace
