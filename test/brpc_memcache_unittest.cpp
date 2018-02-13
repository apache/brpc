// Copyright (c) 2014 Baidu, Inc.
// Date: Thu Jun 11 14:30:07 CST 2015

#ifdef BAIDU_INTERNAL

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

static void RemoveMemcached() {
    puts("Removing memcached...");
    system("rm -rf memcached_for_test");
}

static void DownloadMemcached() {
    puts("Downloading memcached...");
    system("pkill memcached; mkdir -p memcached_for_test && cd memcached_for_test && svn co https://svn.baidu.com/third-64/tags/memcached/memcached_1-4-15-100_PD_BL/bin");
    atexit(RemoveMemcached);
}

class MemcacheTest : public testing::Test {
protected:
    MemcacheTest() : _pid(-1) {}
    void SetUp() {
        pthread_once(&download_memcached_once, DownloadMemcached);

        _pid = fork();
        if (_pid < 0) {
            puts("Fail to fork");
            exit(1);
        } else if (_pid == 0) {
            puts("[Starting memcached]");
            char* const argv[] = { (char*)"memcached_for_test/bin/memcached", NULL };
            execv("memcached_for_test/bin/memcached", argv);
        }
        usleep(10000);
    }
    void TearDown() {
        puts("[Stopping memcached]");
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "kill %d", _pid);
        CHECK(0 == system(cmd));
    }
private:
    pid_t _pid;
};

TEST_F(MemcacheTest, sanity) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:11211", &options));
    brpc::MemcacheRequest request;
    brpc::MemcacheResponse response;
    brpc::Controller cntl;
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
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:11211", &options));
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
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:11211", &options));
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

#endif // BAIDU_INTERNAL
