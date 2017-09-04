// Copyright (c) 2014 baidu-rpc authors.
// Date: Thu Jun 11 14:30:07 CST 2015

#include <iostream>

#include "base/time.h"
#include "base/logging.h"
#include <brpc/redis.h>
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
static pthread_once_t download_redis_server_once = PTHREAD_ONCE_INIT;

static pid_t redis_pid = -1; 

static void RemoveRedisServer() {
    if (redis_pid > 0) {
        puts("[Stopping redis-server]");
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "kill %d; rm -rf redis_server_for_test", redis_pid);
        CHECK(0 == system(cmd));
    }
}

static void DownloadRedisServer() {
    puts("Downloading redis-server...");
    system("pkill redis-server; mkdir -p redis_server_for_test && cd redis_server_for_test && svn co https://svn.baidu.com/third-64/tags/redis/redis_2-6-14-100_PD_BL/bin");
    atexit(RemoveRedisServer);

    redis_pid = fork();
    if (redis_pid < 0) {
        puts("Fail to fork");
        exit(1);
    } else if (redis_pid == 0) {
        puts("[Starting redis-server]");
        char* const argv[] = { (char*)"redis_server_for_test/bin/redis-server",
                               (char*)"--port", (char*)"6479",
                               NULL };
        unlink("dump.rdb");
        execv("redis_server_for_test/bin/redis-server", argv);
    }
    usleep(10000);
}

class RedisTest : public testing::Test {
protected:
    RedisTest() {}
    void SetUp() {
        pthread_once(&download_redis_server_once, DownloadRedisServer);
    }
    void TearDown() {}
};

TEST_F(RedisTest, sanity) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:6479", &options));
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;

    ASSERT_TRUE(request.AddCommand("get hello"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_NIL, response.reply(0).type())
        << response;

    cntl.Reset();
    request.Clear();
    response.Clear();
    request.AddCommand("set hello world");
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(0).type());
    ASSERT_EQ("OK", response.reply(0).data());

    cntl.Reset();
    request.Clear();
    response.Clear();
    ASSERT_TRUE(request.AddCommand("get hello"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(0).type());
    ASSERT_EQ("world", response.reply(0).data());

    cntl.Reset();
    request.Clear();
    response.Clear();
    request.AddCommand("set hello world2");
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(0).type());
    ASSERT_EQ("OK", response.reply(0).data());
    
    cntl.Reset();
    request.Clear();
    response.Clear();
    ASSERT_TRUE(request.AddCommand("get hello"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(0).type());
    ASSERT_EQ("world2", response.reply(0).data());

    cntl.Reset();
    request.Clear();
    response.Clear();
    ASSERT_TRUE(request.AddCommand("del hello"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(0).type());
    ASSERT_EQ(1, response.reply(0).integer());

    cntl.Reset();
    request.Clear();
    response.Clear();
    ASSERT_TRUE(request.AddCommand("get %s", "hello"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(1, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_NIL, response.reply(0).type());
}

TEST_F(RedisTest, keys_with_spaces) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:6479", &options));
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;
    
    cntl.Reset();
    request.Clear();
    response.Clear();
    ASSERT_TRUE(request.AddCommand("set %s 'he1 he1 da1'", "hello world"));
    ASSERT_TRUE(request.AddCommand("set 'hello2 world2' 'he2 he2 da2'"));
    ASSERT_TRUE(request.AddCommand("set \"hello3 world3\" \"he3 he3 da3\""));
    ASSERT_TRUE(request.AddCommand("get \"hello world\""));
    ASSERT_TRUE(request.AddCommand("get 'hello world'"));
    ASSERT_TRUE(request.AddCommand("get 'hello2 world2'"));
    ASSERT_TRUE(request.AddCommand("get 'hello3 world3'"));

    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(7, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(0).type());
    ASSERT_EQ("OK", response.reply(0).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(1).type());
    ASSERT_EQ("OK", response.reply(1).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(2).type());
    ASSERT_EQ("OK", response.reply(2).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(3).type());
    ASSERT_EQ("he1 he1 da1", response.reply(3).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(4).type());
    ASSERT_EQ("he1 he1 da1", response.reply(4).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(5).type());
    ASSERT_EQ("he2 he2 da2", response.reply(5).data());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(6).type());
    ASSERT_EQ("he3 he3 da3", response.reply(6).data());
}

TEST_F(RedisTest, incr_and_decr) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:6479", &options));
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;

    request.AddCommand("incr counter1");
    request.AddCommand("decr counter1");
    request.AddCommand("incrby counter1 %d", 10);
    request.AddCommand("decrby counter1 %d", 20);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(4, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(0).type());
    ASSERT_EQ(1, response.reply(0).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(1).type());
    ASSERT_EQ(0, response.reply(1).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(2).type());
    ASSERT_EQ(10, response.reply(2).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(3).type());
    ASSERT_EQ(-10, response.reply(3).integer());
}

TEST_F(RedisTest, by_components) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:6479", &options));
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;

    base::StringPiece comp1[] = { "incr", "counter2" };
    base::StringPiece comp2[] = { "decr", "counter2" };
    base::StringPiece comp3[] = { "incrby", "counter2", "10" };
    base::StringPiece comp4[] = { "decrby", "counter2", "20" };

    request.AddCommandByComponents(comp1, arraysize(comp1));
    request.AddCommandByComponents(comp2, arraysize(comp2));
    request.AddCommandByComponents(comp3, arraysize(comp3));
    request.AddCommandByComponents(comp4, arraysize(comp4));

    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(4, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(0).type());
    ASSERT_EQ(1, response.reply(0).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(1).type());
    ASSERT_EQ(0, response.reply(1).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(2).type());
    ASSERT_EQ(10, response.reply(2).integer());
    ASSERT_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(3).type());
    ASSERT_EQ(-10, response.reply(3).integer());
}
} //namespace
