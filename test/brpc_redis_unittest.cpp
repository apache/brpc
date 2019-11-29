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
#include <unordered_map>
#include <butil/time.h>
#include <butil/logging.h>
#include <brpc/redis.h>
#include <brpc/channel.h>
#include <brpc/policy/redis_authenticator.h>
#include <brpc/server.h>
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

static pid_t g_redis_pid = -1; 

static void RemoveRedisServer() {
    if (g_redis_pid > 0) {
        puts("[Stopping redis-server]");
        char cmd[256];
#if defined(BAIDU_INTERNAL)
        snprintf(cmd, sizeof(cmd), "kill %d; rm -rf redis_server_for_test", g_redis_pid);
#else
        snprintf(cmd, sizeof(cmd), "kill %d", g_redis_pid);
#endif
        CHECK(0 == system(cmd));
        // Wait for redis to stop
        usleep(50000);
    }
}

#define REDIS_SERVER_BIN "redis-server"
#define REDIS_SERVER_PORT "6479"

static void RunRedisServer() {
#if defined(BAIDU_INTERNAL)
    puts("Downloading redis-server...");
    if (system("mkdir -p redis_server_for_test && cd redis_server_for_test && svn co https://svn.baidu.com/third-64/tags/redis/redis_2-6-14-100_PD_BL/bin") != 0) {
        puts("Fail to get redis-server from svn");
        return;
    }
# undef REDIS_SERVER_BIN
# define REDIS_SERVER_BIN "redis_server_for_test/bin/redis-server";
#else
    if (system("which " REDIS_SERVER_BIN) != 0) {
        puts("Fail to find " REDIS_SERVER_BIN ", following tests will be skipped");
        return;
    }
#endif
    atexit(RemoveRedisServer);

    g_redis_pid = fork();
    if (g_redis_pid < 0) {
        puts("Fail to fork");
        exit(1);
    } else if (g_redis_pid == 0) {
        puts("[Starting redis-server]");
        char* const argv[] = { (char*)REDIS_SERVER_BIN,
                               (char*)"--port", (char*)REDIS_SERVER_PORT,
                               NULL };
        unlink("dump.rdb");
        if (execvp(REDIS_SERVER_BIN, argv) < 0) {
            puts("Fail to run " REDIS_SERVER_BIN);
            exit(1);
        }
    }
    // Wait for redis to start.
    usleep(50000);
}

class RedisTest : public testing::Test {
protected:
    RedisTest() {}
    void SetUp() {
        pthread_once(&download_redis_server_once, RunRedisServer);
    }
    void TearDown() {}
};

void AssertReplyEqual(const brpc::RedisReply& reply1,
                      const brpc::RedisReply& reply2) {
    if (&reply1 == &reply2) {
        return;
    }
    CHECK_EQ(reply1.type(), reply2.type());
    switch (reply1.type()) {
    case brpc::REDIS_REPLY_ARRAY:
        ASSERT_EQ(reply1.size(), reply2.size());
        for (size_t j = 0; j < reply1.size(); ++j) {
            ASSERT_NE(&reply1[j], &reply2[j]); // from different arena
            AssertReplyEqual(reply1[j], reply2[j]);
        }
        break;
    case brpc::REDIS_REPLY_INTEGER:
        ASSERT_EQ(reply1.integer(), reply2.integer());
        break;
    case brpc::REDIS_REPLY_NIL:
        break;
    case brpc::REDIS_REPLY_STRING:
        // fall through
    case brpc::REDIS_REPLY_STATUS:
        ASSERT_NE(reply1.c_str(), reply2.c_str()); // from different arena
        ASSERT_STREQ(reply1.c_str(), reply2.c_str());
        break;
    case brpc::REDIS_REPLY_ERROR:
        ASSERT_NE(reply1.error_message(), reply2.error_message()); // from different arena
        ASSERT_STREQ(reply1.error_message(), reply2.error_message());
        break;
    }
}

void AssertResponseEqual(const brpc::RedisResponse& r1,
                         const brpc::RedisResponse& r2,
                         int repeated_times = 1) {
    if (&r1 == &r2) {
        ASSERT_EQ(repeated_times, 1);
        return;
    }
    ASSERT_EQ(r2.reply_size()* repeated_times, r1.reply_size());
    for (int j = 0; j < repeated_times; ++j) {
        for (int i = 0; i < r2.reply_size(); ++i) {
            ASSERT_NE(&r2.reply(i), &r1.reply(j * r2.reply_size() + i));
            AssertReplyEqual(r2.reply(i), r1.reply(j * r2.reply_size() + i));
        }
    }
}

TEST_F(RedisTest, sanity) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
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
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
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

    brpc::RedisResponse response2 = response;
    AssertResponseEqual(response2, response);
    response2.MergeFrom(response);
    AssertResponseEqual(response2, response, 2);
}

TEST_F(RedisTest, incr_and_decr) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
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

    brpc::RedisResponse response2 = response;
    AssertResponseEqual(response2, response);
    response2.MergeFrom(response);
    AssertResponseEqual(response2, response, 2);
}

TEST_F(RedisTest, by_components) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;

    butil::StringPiece comp1[] = { "incr", "counter2" };
    butil::StringPiece comp2[] = { "decr", "counter2" };
    butil::StringPiece comp3[] = { "incrby", "counter2", "10" };
    butil::StringPiece comp4[] = { "decrby", "counter2", "20" };

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

    brpc::RedisResponse response2 = response;
    AssertResponseEqual(response2, response);
    response2.MergeFrom(response);
    AssertResponseEqual(response2, response, 2);
}

TEST_F(RedisTest, auth) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    // config auth
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_REDIS;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;

        butil::StringPiece comp1[] = { "set", "passwd", "my_redis" };
        butil::StringPiece comp2[] = { "config", "set", "requirepass", "my_redis" };
        butil::StringPiece comp3[] = { "auth", "my_redis" };
        butil::StringPiece comp4[] = { "get", "passwd" };

        request.AddCommandByComponents(comp1, arraysize(comp1));
        request.AddCommandByComponents(comp2, arraysize(comp2));
        request.AddCommandByComponents(comp3, arraysize(comp3));
        request.AddCommandByComponents(comp4, arraysize(comp4));

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(4, response.reply_size());
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(0).type());
        ASSERT_STREQ("OK", response.reply(0).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(1).type());
        ASSERT_STREQ("OK", response.reply(1).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(2).type());
        ASSERT_STREQ("OK", response.reply(2).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(3).type());
        ASSERT_STREQ("my_redis", response.reply(3).c_str());
    }

    // Auth failed
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_REDIS;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;

        butil::StringPiece comp1[] = { "get", "passwd" };

        request.AddCommandByComponents(comp1, arraysize(comp1));

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, response.reply_size());
        ASSERT_EQ(brpc::REDIS_REPLY_ERROR, response.reply(0).type());
    }

    // Auth with RedisAuthenticator && clear auth
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_REDIS;
        brpc::Channel channel;
        brpc::policy::RedisAuthenticator* auth =
          new brpc::policy::RedisAuthenticator("my_redis");
        options.auth = auth;
        ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;

        butil::StringPiece comp1[] = { "get", "passwd" };
        butil::StringPiece comp2[] = { "config", "set", "requirepass", "" };

        request.AddCommandByComponents(comp1, arraysize(comp1));
        request.AddCommandByComponents(comp2, arraysize(comp2));

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(2, response.reply_size());
        ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(0).type());
        ASSERT_STREQ("my_redis", response.reply(0).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(1).type());
        ASSERT_STREQ("OK", response.reply(1).c_str());
    }

    // check noauth.
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_REDIS;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init("0.0.0.0:" REDIS_SERVER_PORT, &options));
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;

        butil::StringPiece comp1[] = { "get", "passwd" };

        request.AddCommandByComponents(comp1, arraysize(comp1));

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1, response.reply_size());
        ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(0).type());
        ASSERT_STREQ("my_redis", response.reply(0).c_str());
    }
}

TEST_F(RedisTest, cmd_format) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::RedisRequest request;
    // set empty string
    request.AddCommand("set a ''");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$0\r\n\r\n", 
		request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("mset b '' c ''");
    ASSERT_STREQ("*5\r\n$4\r\nmset\r\n$1\r\nb\r\n$0\r\n\r\n$1\r\nc\r\n$0\r\n\r\n",
		request._buf.to_string().c_str());
    request.Clear();
    // set non-empty string
    request.AddCommand("set a 123");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$3\r\n123\r\n", 
		request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("mset b '' c ccc");
    ASSERT_STREQ("*5\r\n$4\r\nmset\r\n$1\r\nb\r\n$0\r\n\r\n$1\r\nc\r\n$3\r\nccc\r\n",
		request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("get ''key value");  // == get <empty> key value
    ASSERT_STREQ("*4\r\n$3\r\nget\r\n$0\r\n\r\n$3\r\nkey\r\n$5\r\nvalue\r\n", request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("get key'' value");  // == get key <empty> value
    ASSERT_STREQ("*4\r\n$3\r\nget\r\n$3\r\nkey\r\n$0\r\n\r\n$5\r\nvalue\r\n", request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("get 'ext'key   value  ");  // == get ext key value
    ASSERT_STREQ("*4\r\n$3\r\nget\r\n$3\r\next\r\n$3\r\nkey\r\n$5\r\nvalue\r\n", request._buf.to_string().c_str());
    request.Clear();
    
    request.AddCommand("  get   key'ext'   value  ");  // == get key ext value
    ASSERT_STREQ("*4\r\n$3\r\nget\r\n$3\r\nkey\r\n$3\r\next\r\n$5\r\nvalue\r\n", request._buf.to_string().c_str());
    request.Clear();
}

TEST_F(RedisTest, quote_and_escape) {
    if (g_redis_pid < 0) {
        puts("Skipped due to absence of redis-server");
        return;
    }
    brpc::RedisRequest request;
    request.AddCommand("set a 'foo bar'");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$7\r\nfoo bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a 'foo \\'bar'");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$8\r\nfoo 'bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a 'foo \"bar'");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$8\r\nfoo \"bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a 'foo \\\"bar'");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$9\r\nfoo \\\"bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a \"foo 'bar\"");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$8\r\nfoo 'bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a \"foo \\'bar\"");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$9\r\nfoo \\'bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();

    request.AddCommand("set a \"foo \\\"bar\"");
    ASSERT_STREQ("*3\r\n$3\r\nset\r\n$1\r\na\r\n$8\r\nfoo \"bar\r\n",
                 request._buf.to_string().c_str());
    request.Clear();
}

TEST_F(RedisTest, codec) {
    butil::Arena arena;
    // status
    {
        brpc::RedisReply r(&arena);
        butil::IOBuf buf;
        ASSERT_TRUE(r.SetStatus("OK"));
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(), "+OK\r\n");
        ASSERT_STREQ(r.c_str(), "OK");
        r.Clear();
        brpc::ParseError err = r.ConsumePartialIOBuf(buf, &arena);
        ASSERT_EQ(err, brpc::PARSE_OK);
        ASSERT_TRUE(r.is_string());
        ASSERT_STREQ("OK", r.c_str());
    }
    // error
    {
        brpc::RedisReply r(&arena);
        butil::IOBuf buf;
        ASSERT_TRUE(r.SetError("not exist \'key\'"));
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(), "-not exist \'key\'\r\n");
        r.Clear();
        brpc::ParseError err = r.ConsumePartialIOBuf(buf, &arena);
        ASSERT_EQ(err, brpc::PARSE_OK);
        ASSERT_TRUE(r.is_error());
        ASSERT_STREQ("not exist \'key\'", r.error_message());
    }
    // string
    {
        brpc::RedisReply r(&arena);
        butil::IOBuf buf;
        ASSERT_TRUE(r.SetNilString());
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(), "$-1\r\n");
        r.Clear();
        brpc::ParseError err = r.ConsumePartialIOBuf(buf, &arena);
        ASSERT_EQ(err, brpc::PARSE_OK);
        ASSERT_TRUE(r.is_nil());

        r.Clear();
        ASSERT_TRUE(r.SetBulkString("abcde'hello world"));
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(), "$17\r\nabcde'hello world\r\n");
        ASSERT_STREQ(r.c_str(), "abcde'hello world");
        r.Clear();
        err = r.ConsumePartialIOBuf(buf, &arena);
        ASSERT_EQ(err, brpc::PARSE_OK);
        ASSERT_TRUE(r.is_string());
        ASSERT_STREQ(r.c_str(), "abcde'hello world");
    }
    // integer
    {
        brpc::RedisReply r(&arena);
        butil::IOBuf buf;
        int t = 2;
        int input[] = { -1, 1234567 };
        const char* output[] = { ":-1\r\n", ":1234567\r\n" };
        for (int i = 0; i < t; ++i) {
            r.Clear();
            ASSERT_TRUE(r.SetInteger(input[i]));
            ASSERT_TRUE(r.SerializeToIOBuf(&buf));
            ASSERT_STREQ(buf.to_string().c_str(), output[i]);
            r.Clear();
            brpc::ParseError err = r.ConsumePartialIOBuf(buf, &arena);
            ASSERT_EQ(err, brpc::PARSE_OK);
            ASSERT_TRUE(r.is_integer());
            ASSERT_EQ(r.integer(), input[i]);
        }
    }
    // array
    {
        brpc::RedisReply r(&arena);
        butil::IOBuf buf;
        ASSERT_TRUE(r.SetArray(3));
        brpc::RedisReply& sub_reply = r[0];
        sub_reply.SetArray(2);
        sub_reply[0].SetBulkString("hello, it's me");
        sub_reply[1].SetInteger(422);
        r[1].SetBulkString("To go over everything");
        r[2].SetInteger(1);
        ASSERT_TRUE(r[3].is_nil());
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(),
                "*3\r\n*2\r\n$14\r\nhello, it's me\r\n:422\r\n$21\r\n"
                "To go over everything\r\n:1\r\n");
        r.Clear();
        ASSERT_EQ(r.ConsumePartialIOBuf(buf, &arena), brpc::PARSE_OK);
        ASSERT_TRUE(r.is_array());
        ASSERT_EQ(3ul, r.size());
        ASSERT_TRUE(r[0].is_array());
        ASSERT_EQ(2ul, r[0].size());
        ASSERT_TRUE(r[0][0].is_string());
        ASSERT_STREQ(r[0][0].c_str(), "hello, it's me");
        ASSERT_TRUE(r[0][1].is_integer());
        ASSERT_EQ(r[0][1].integer(), 422);
        ASSERT_TRUE(r[1].is_string());
        ASSERT_STREQ(r[1].c_str(), "To go over everything");
        ASSERT_TRUE(r[2].is_integer());
        ASSERT_EQ(1, r[2].integer());

        r.Clear();
        // nil array
        ASSERT_TRUE(r.SetArray(-1));
        ASSERT_TRUE(r.SerializeToIOBuf(&buf));
        ASSERT_STREQ(buf.to_string().c_str(), "*-1\r\n");
        ASSERT_EQ(r.ConsumePartialIOBuf(buf, &arena), brpc::PARSE_OK);
        ASSERT_TRUE(r.is_nil());
    }
}

butil::Mutex s_mutex;
std::unordered_map<std::string, std::string> m;
std::unordered_map<std::string, int64_t> int_map;

struct SleepArgs {
    int sleep_ms;
    google::protobuf::Closure* done;
};

void* sleep(void *arg) {
    SleepArgs* args = static_cast<SleepArgs*>(arg);
    bthread_usleep(args->sleep_ms * 1000);
    args->done->Run();
    delete args;
    return NULL;
}

class SetCommandHandler : public brpc::RedisCommandHandler {
public:
    SetCommandHandler(bool sleep = false)
        : _sleep(sleep) {}

    brpc::RedisCommandHandler::Result Run(const char* args[],
                                          brpc::RedisReply* output,
                                          google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        std::string key = args[1];
        std::string value = args[2];
        m[key] = value;
        output->SetStatus("OK");
        if (_sleep) {
            SleepArgs *args = new SleepArgs;
            args->sleep_ms = _sleep_ms;
            args->done = done_guard.release();
            bthread_t bth;
            EXPECT_EQ(0, bthread_start_background(&bth, NULL, sleep, args));
            if (_sleep_ms > 20) _sleep_ms -= 20;
        }
        return brpc::RedisCommandHandler::OK;
    }
    RedisCommandHandler* New() { _new_count++; return new SetCommandHandler(_sleep); }
    int new_count() { return _new_count; }

private:
    int _sleep_ms = 100;
    int _new_count = 0;
    bool _sleep = false;
};

class GetCommandHandler : public brpc::RedisCommandHandler {
public:
    GetCommandHandler(bool sleep = false)
        : _sleep(sleep) {}

    brpc::RedisCommandHandler::Result Run(const char* args[],
                                          brpc::RedisReply* output,
                                          google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        std::string key = args[1];
        auto it = m.find(key);
        if (it != m.end()) {
            output->SetBulkString(it->second);
        } else {
            output->SetNilString();
        }
        if (_sleep) {
            SleepArgs *args = new SleepArgs;
            args->sleep_ms = _sleep_ms;
            args->done = done_guard.release();
            bthread_t bth;
            EXPECT_EQ(0, bthread_start_background(&bth, NULL, sleep, args));
            if (_sleep_ms > 20) _sleep_ms -= 20;
        }
        return brpc::RedisCommandHandler::OK;
    }
    RedisCommandHandler* New() { _new_count++; return new GetCommandHandler(_sleep); }
    int new_count() { return _new_count; }

private:
    int _sleep_ms = 100;
    int _new_count = 0;
    bool _sleep = false;
};

class IncrCommandHandler : public brpc::RedisCommandHandler {
public:
    IncrCommandHandler(bool sleep = false)
        : _sleep(sleep) {}

    brpc::RedisCommandHandler::Result Run(const char* args[],
                                          brpc::RedisReply* output,
                                          google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        int64_t value;
        s_mutex.lock();
        value = ++int_map[args[1]];
        s_mutex.unlock();
        output->SetInteger(value);
        if (_sleep) {
            SleepArgs *args = new SleepArgs;
            args->sleep_ms = _sleep_ms;
            args->done = done_guard.release();
            bthread_t bth;
            EXPECT_EQ(0, bthread_start_background(&bth, NULL, sleep, args));
            if (_sleep_ms > 20) _sleep_ms -= 20;
        }
        return brpc::RedisCommandHandler::OK;
    }
    RedisCommandHandler* New() { _new_count++; return new IncrCommandHandler(_sleep); }
    int new_count() { return _new_count; }

private:
    int _sleep_ms = 100;
    int _new_count = 0;
    bool _sleep = false;
};

class RedisServiceImpl : public brpc::RedisService { };

TEST_F(RedisTest, server_sanity) {
    brpc::Server server;
    brpc::ServerOptions server_options;
    RedisServiceImpl* rsimpl = new RedisServiceImpl;
    GetCommandHandler *gh = new GetCommandHandler(true);
    SetCommandHandler *sh = new SetCommandHandler(true);
    IncrCommandHandler *ih = new IncrCommandHandler(true);
    rsimpl->AddCommandHandler("get", gh);
    rsimpl->AddCommandHandler("set", sh);
    rsimpl->AddCommandHandler("incr", ih);
    server_options.redis_service = rsimpl;
    brpc::PortRange pr(8081, 8900);
    ASSERT_EQ(0, server.Start("127.0.0.1", pr, &server_options));

    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1", server.listen_address().port, &options));

    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;
    ASSERT_TRUE(request.AddCommand("get hello"));
    ASSERT_TRUE(request.AddCommand("get hello2"));
    ASSERT_TRUE(request.AddCommand("set key1 value1"));
    ASSERT_TRUE(request.AddCommand("get key1"));
    ASSERT_TRUE(request.AddCommand("set key2 value2"));
    ASSERT_TRUE(request.AddCommand("get key2"));
    ASSERT_TRUE(request.AddCommand("xxxcommand key2"));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(7, response.reply_size());
    ASSERT_EQ(brpc::REDIS_REPLY_NIL, response.reply(0).type());
    ASSERT_EQ(brpc::REDIS_REPLY_NIL, response.reply(1).type());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(2).type());
    ASSERT_STREQ("OK", response.reply(2).c_str());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(3).type());
    ASSERT_STREQ("value1", response.reply(3).c_str());
    ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(4).type());
    ASSERT_STREQ("OK", response.reply(4).c_str());
    ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(5).type());
    ASSERT_STREQ("value2", response.reply(5).c_str());
    ASSERT_EQ(brpc::REDIS_REPLY_ERROR, response.reply(6).type());
    ASSERT_TRUE(butil::StringPiece(response.reply(6).error_message()).starts_with("ERR unknown command"));

    ASSERT_EQ(gh->new_count(), 1);
    ASSERT_EQ(sh->new_count(), 1);
    ASSERT_EQ(ih->new_count(), 1);
}

void* incr_thread(void* arg) {
    brpc::Channel* c = static_cast<brpc::Channel*>(arg);

    for (int i = 0; i < 5000; ++i) {
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;
        EXPECT_TRUE(request.AddCommand("incr count"));
        c->CallMethod(NULL, &cntl, &request, &response, NULL);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_EQ(1, response.reply_size());
        EXPECT_TRUE(response.reply(0).is_integer());
    }
    return NULL;
}

TEST_F(RedisTest, server_concurrency) {
    int N = 10;
    brpc::Server server;
    brpc::ServerOptions server_options;
    RedisServiceImpl* rsimpl = new RedisServiceImpl;
    IncrCommandHandler *ih = new IncrCommandHandler;
    rsimpl->AddCommandHandler("incr", ih);
    server_options.redis_service = rsimpl;
    brpc::PortRange pr(8081, 8900);
    ASSERT_EQ(0, server.Start("0.0.0.0", pr, &server_options));

    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.connection_type = "pooled";
    std::vector<bthread_t> bths;
    std::vector<brpc::Channel*> channels;
    for (int i = 0; i < N; ++i) {
        channels.push_back(new brpc::Channel);
        ASSERT_EQ(0, channels.back()->Init("127.0.0.1", server.listen_address().port, &options));
        bthread_t bth;
        ASSERT_EQ(bthread_start_background(&bth, NULL, incr_thread, channels.back()), 0);
        bths.push_back(bth);
    }

    for (int i = 0; i < N; ++i) {
        bthread_join(bths[i], NULL);
        delete channels[i];
    }
    ASSERT_EQ(int_map["count"], 10 * 5000LL);
    ASSERT_EQ(ih->new_count(), N);
}

class MultiCommandHandler : public brpc::RedisCommandHandler {
public:
    MultiCommandHandler()
        : _started(false) {}

    RedisCommandHandler::Result Run(const char* args[],
                                     brpc::RedisReply* output,
                                     google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        if (strcasecmp(args[0], "multi") == 0) {
            if (!_started) {
                output->SetStatus("OK");
                _started = true;
            } else {
                output->SetError("ERR duplicate multi");
            }
            return brpc::RedisCommandHandler::CONTINUE;
        } 
        if (strcasecmp(args[0], "exec") != 0) {
            std::vector<std::string> sargs;
            for (const char** c = args; *c; ++c) {
                sargs.push_back(*c);
            }
            _commands.push_back(sargs);
            output->SetStatus("QUEUED");
            return brpc::RedisCommandHandler::CONTINUE;
        }
        output->SetArray(_commands.size());
        s_mutex.lock();
        for (size_t i = 0; i < _commands.size(); ++i) {
            if (_commands[i][0] == "incr") {
                int64_t value;
                value = ++int_map[_commands[i][1]];
                (*output)[i].SetInteger(value);
            }
        }
        s_mutex.unlock();
        _started = false;
        return brpc::RedisCommandHandler::OK;
    }
    RedisCommandHandler* New() { return new MultiCommandHandler; }

private:
    std::vector<std::vector<std::string>> _commands;
    bool _started;
};

TEST_F(RedisTest, server_command_continue) {
    brpc::Server server;
    brpc::ServerOptions server_options;
    RedisServiceImpl* rsimpl = new RedisServiceImpl;
    rsimpl->AddCommandHandler("get", new GetCommandHandler);
    rsimpl->AddCommandHandler("set", new SetCommandHandler);
    rsimpl->AddCommandHandler("incr", new IncrCommandHandler);
    rsimpl->AddCommandHandler("multi", new MultiCommandHandler);
    server_options.redis_service = rsimpl;
    brpc::PortRange pr(8081, 8900);
    ASSERT_EQ(0, server.Start("127.0.0.1", pr, &server_options));

    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1", server.listen_address().port, &options));

    {
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;
        ASSERT_TRUE(request.AddCommand("set hello world"));
        ASSERT_TRUE(request.AddCommand("get hello"));
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(2, response.reply_size());
        ASSERT_STREQ("world", response.reply(1).c_str());
    }
    {
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;
        ASSERT_TRUE(request.AddCommand("multi"));
        int count = 10;
        for (int i = 0; i < count; ++i) {
            ASSERT_TRUE(request.AddCommand("incr hello 1"));
        }
        ASSERT_TRUE(request.AddCommand("exec"));
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_EQ(12, response.reply_size());
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(0).type());
        ASSERT_STREQ("OK", response.reply(0).c_str());
        for (int i = 1; i < count + 1; ++i) {
            ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(i).type());
            ASSERT_STREQ("QUEUED", response.reply(i).c_str());
        }
        const brpc::RedisReply& m = response.reply(count+1);
        ASSERT_EQ(count, (int)m.size());
        for (int i = 0; i < count; ++i) {
            ASSERT_EQ(i+1, m[i].integer());
        }
    }
    // After 'multi', normal requests should be successful
    {
        brpc::RedisRequest request;
        brpc::RedisResponse response;
        brpc::Controller cntl;
        ASSERT_TRUE(request.AddCommand("get hello"));
        ASSERT_TRUE(request.AddCommand("get hello2"));
        ASSERT_TRUE(request.AddCommand("set key1 value1"));
        ASSERT_TRUE(request.AddCommand("get key1"));
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_STREQ("world", response.reply(0).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_NIL, response.reply(1).type());
        ASSERT_EQ(brpc::REDIS_REPLY_STATUS, response.reply(2).type());
        ASSERT_STREQ("OK", response.reply(2).c_str());
        ASSERT_EQ(brpc::REDIS_REPLY_STRING, response.reply(3).type());
        ASSERT_STREQ("value1", response.reply(3).c_str());
    }
}

} //namespace
