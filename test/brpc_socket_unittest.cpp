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

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>  // F_GETFD
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include <butil/fd_guard.h>
#include "bthread/unstable.h"
#include "bthread/task_control.h"
#include "brpc/socket.h"
#include "brpc/errno.pb.h"
#include "brpc/acceptor.h"
#include "brpc/policy/hulu_pbrpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/nshead.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "health_check.pb.h"
#if defined(OS_MACOSX)
#include <sys/event.h>
#endif
#include <netinet/tcp.h>

#define CONNECT_IN_KEEPWRITE 1;

namespace bthread {
extern TaskControl* g_task_control;
}

namespace brpc {
DECLARE_int32(health_check_interval);
DECLARE_bool(socket_keepalive);
DECLARE_int32(socket_keepalive_idle_s);
DECLARE_int32(socket_keepalive_interval_s);
DECLARE_int32(socket_keepalive_count);
DECLARE_int32(socket_tcp_user_timeout_ms);
}

void EchoProcessHuluRequest(brpc::InputMessageBase* msg_base);

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Protocol dummy_protocol = 
                             { brpc::policy::ParseHuluMessage,
                               brpc::SerializeRequestDefault, 
                               brpc::policy::PackHuluRequest,
                               EchoProcessHuluRequest, EchoProcessHuluRequest,
                               NULL, NULL, NULL,
                               brpc::CONNECTION_TYPE_ALL, "dummy_hulu" };
    EXPECT_EQ(0,  RegisterProtocol((brpc::ProtocolType)30, dummy_protocol));
    return RUN_ALL_TESTS();
}

struct WaitData {
    bthread_id_t id;
    int error_code;
    std::string error_text;

    WaitData() : id(INVALID_BTHREAD_ID), error_code(0) {}
};
int OnWaitIdReset(bthread_id_t id, void* data, int error_code,
                  const std::string& error_text) {
    static_cast<WaitData*>(data)->id = id;
    static_cast<WaitData*>(data)->error_code = error_code;
    static_cast<WaitData*>(data)->error_text = error_text;
    return bthread_id_unlock_and_destroy(id);
}

class SocketTest : public ::testing::Test{
protected:
    SocketTest(){
    };
    virtual ~SocketTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

brpc::Socket* global_sock = NULL;

class CheckRecycle : public brpc::SocketUser {
    void BeforeRecycle(brpc::Socket* s) {
        ASSERT_TRUE(global_sock);
        ASSERT_EQ(global_sock, s);
        global_sock = NULL;
        delete this;
    }
};

TEST_F(SocketTest, not_recycle_until_zero_nref) {
    std::cout << "sizeof(Socket)=" << sizeof(brpc::Socket) << std::endl;
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    brpc::SocketId id = 8888;
    butil::EndPoint dummy;
    ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
    brpc::SocketOptions options;
    options.fd = fds[1];
    options.remote_side = dummy;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    {
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(fds[1], s->fd());
        ASSERT_EQ(dummy, s->remote_side());
        ASSERT_EQ(id, s->id());
        ASSERT_EQ(0, s->SetFailed());
        ASSERT_EQ(s.get(), global_sock);
    }
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    close(fds[0]);

    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(-1, brpc::Socket::Address(id, &ptr));
}

butil::atomic<int> winner_count(0);
const int AUTH_ERR = -9;

void* auth_fighter(void* arg) {
    bthread_usleep(10000);
    int auth_error = 0;
    brpc::Socket* s = (brpc::Socket*)arg;
    if (s->FightAuthentication(&auth_error) == 0) {
        winner_count.fetch_add(1);
        s->SetAuthentication(AUTH_ERR);
    } else {
        EXPECT_EQ(AUTH_ERR, auth_error);        
    }
    return NULL;
}

TEST_F(SocketTest, authentication) {
    brpc::SocketId id;
    brpc::SocketOptions options;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr s;
    ASSERT_EQ(0, brpc::Socket::Address(id, &s));
    
    bthread_t th[64];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&th[i], NULL, auth_fighter, s.get()));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, bthread_join(th[i], NULL));
    }
    // Only one fighter wins
    ASSERT_EQ(1, winner_count.load());

    // Fight after signal is OK
    int auth_error = 0;
    ASSERT_NE(0, s->FightAuthentication(&auth_error));
    ASSERT_EQ(AUTH_ERR, auth_error);
    // Socket has been `SetFailed' when authentication failed
    ASSERT_TRUE(brpc::Socket::Address(s->id(), NULL));
}

static butil::atomic<int> g_called_seq(1);
class MyMessage : public brpc::SocketMessage {
public:
    MyMessage(const char* str, size_t len, int* called = NULL)
        : _str(str), _len(len), _called(called) {}
private:
    butil::Status AppendAndDestroySelf(butil::IOBuf* out_buf, brpc::Socket*) {
        out_buf->append(_str, _len);
        if (_called) {
            *_called = g_called_seq.fetch_add(1, butil::memory_order_relaxed);
        }
        delete this;
        return butil::Status::OK();
    };
    const char* _str;
    size_t _len;
    int* _called;
};

class MyErrorMessage : public brpc::SocketMessage {
public:
    explicit MyErrorMessage(const butil::Status& st) : _status(st) {}
private:
    butil::Status AppendAndDestroySelf(butil::IOBuf*, brpc::Socket*) {
        return _status;
    };
    butil::Status _status;
};

TEST_F(SocketTest, single_threaded_write) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    brpc::SocketId id = 8888;
    butil::EndPoint dummy;
    ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
    brpc::SocketOptions options;
    options.fd = fds[1];
    options.remote_side = dummy;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    {
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(fds[1], s->fd());
        ASSERT_EQ(dummy, s->remote_side());
        ASSERT_EQ(id, s->id());
        const int BATCH = 5;
        for (size_t i = 0; i < 20; ++i) {
            char buf[32 * BATCH];
            size_t len = snprintf(buf, sizeof(buf), "hello world! %lu", i);
            if (i % 4 == 0) {
                brpc::SocketMessagePtr<MyMessage> msg(new MyMessage(buf, len));
                ASSERT_EQ(0, s->Write(msg));
            } else if (i % 4 == 1) {
                brpc::SocketMessagePtr<MyErrorMessage> msg(
                    new MyErrorMessage(butil::Status(EINVAL, "Invalid input")));
                bthread_id_t wait_id;
                WaitData data;
                ASSERT_EQ(0, bthread_id_create2(&wait_id, &data, OnWaitIdReset));
                brpc::Socket::WriteOptions wopt;
                wopt.id_wait = wait_id;
                ASSERT_EQ(0, s->Write(msg, &wopt));
                ASSERT_EQ(0, bthread_id_join(wait_id));
                ASSERT_EQ(wait_id.value, data.id.value);
                ASSERT_EQ(EINVAL, data.error_code);
                ASSERT_EQ("Invalid input", data.error_text);
                continue;
            } else if (i % 4 == 2) {
                int seq[BATCH] = {};
                brpc::SocketMessagePtr<MyMessage> msgs[BATCH];
                // re-print the buffer.
                len = 0;
                for (int j = 0; j < BATCH; ++j) {
                    if (j % 2 == 0) {
                        // Empty message, should be skipped.
                        msgs[j].reset(new MyMessage(buf+len, 0, &seq[j]));
                    } else {
                        size_t sub_len = snprintf(
                            buf+len, sizeof(buf)-len, "hello world! %lu.%d", i, j);
                        msgs[j].reset(new MyMessage(buf+len, sub_len, &seq[j]));
                        len += sub_len;
                    }
                }
                for (size_t i = 0; i < BATCH; ++i) {
                    ASSERT_EQ(0, s->Write(msgs[i]));
                }
                for (int j = 1; j < BATCH; ++j) {
                    ASSERT_LT(seq[j-1], seq[j]) << "j=" << j;
                }
            } else {
                butil::IOBuf src;
                src.append(buf);
                ASSERT_EQ(len, src.length());
                ASSERT_EQ(0, s->Write(&src));
                ASSERT_TRUE(src.empty());
            }
            char dest[sizeof(buf)];
            ASSERT_EQ(len, (size_t)read(fds[0], dest, sizeof(dest)));
            ASSERT_EQ(0, memcmp(buf, dest, len));
        }
        ASSERT_EQ(0, s->SetFailed());
    }
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    close(fds[0]);
}

void EchoProcessHuluRequest(brpc::InputMessageBase* msg_base) {
    brpc::DestroyingPtr<brpc::policy::MostCommonMessage> msg(
        static_cast<brpc::policy::MostCommonMessage*>(msg_base));
    butil::IOBuf buf;
    buf.append(msg->meta);
    buf.append(msg->payload);
    ASSERT_EQ(0, msg->socket()->Write(&buf));
}

class MyConnect : public brpc::AppConnect {
public:
    MyConnect() : _done(NULL), _data(NULL), _called_start_connect(false) {}
    void StartConnect(const brpc::Socket*,
                      void (*done)(int err, void* data),
                      void* data) {
        LOG(INFO) << "Start application-level connect";
        _done = done;
        _data = data;
        _called_start_connect = true;
    }
    void StopConnect(brpc::Socket*) {
        LOG(INFO) << "Stop application-level connect";
    }
    void MakeConnectDone() {
        _done(0, _data);
    }
    bool is_start_connect_called() const { return _called_start_connect; }
private:
    void (*_done)(int err, void* data);
    void* _data;
    bool _called_start_connect; 
};

TEST_F(SocketTest, single_threaded_connect_and_write) {
    // FIXME(gejun): Messenger has to be new otherwise quitting may crash.
    brpc::Acceptor* messenger = new brpc::Acceptor;
    const brpc::InputMessageHandler pairs[] = {
        { brpc::policy::ParseHuluMessage, 
          EchoProcessHuluRequest, NULL, NULL, "dummy_hulu" }
    };

    butil::EndPoint point(butil::IP_ANY, 7878);
    int listening_fd = tcp_listen(point);
    ASSERT_TRUE(listening_fd > 0);
    butil::make_non_blocking(listening_fd);
    ASSERT_EQ(0, messenger->AddHandler(pairs[0]));
    ASSERT_EQ(0, messenger->StartAccept(listening_fd, -1, NULL, false));

    brpc::SocketId id = 8888;
    brpc::SocketOptions options;
    options.remote_side = point;
    std::shared_ptr<MyConnect> my_connect = std::make_shared<MyConnect>();
    options.app_connect = my_connect;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    {
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(-1, s->fd());
        ASSERT_EQ(butil::EndPoint(), s->local_side());
        ASSERT_EQ(point, s->remote_side());
        ASSERT_EQ(id, s->id());
        for (size_t i = 0; i < 20; ++i) {
            char buf[64];
            const size_t meta_len = 4;
            *(uint32_t*)(buf + 12) = *(uint32_t*)"Meta";
            const size_t len = snprintf(buf + 12 + meta_len,
                                        sizeof(buf) - 12 - meta_len,
                                        "hello world! %lu", i);
            memcpy(buf, "HULU", 4);
            // HULU uses host byte order directly...
            *(uint32_t*)(buf + 4) = len + meta_len;
            *(uint32_t*)(buf + 8) = meta_len;

            int called = 0;
            if (i % 2 == 0) {
                brpc::SocketMessagePtr<MyMessage> msg(
                    new MyMessage(buf, 12 + meta_len + len, &called));
                ASSERT_EQ(0, s->Write(msg));
            } else {
                butil::IOBuf src;
                src.append(buf, 12 + meta_len + len);
                ASSERT_EQ(12 + meta_len + len, src.length());
                ASSERT_EQ(0, s->Write(&src));
                ASSERT_TRUE(src.empty());
            }
            if (i == 0) {
                // connection needs to be established at first time.
                // Should be intentionally blocked in app_connect.
                bthread_usleep(10000);
                ASSERT_TRUE(my_connect->is_start_connect_called());
                ASSERT_LT(0, s->fd()); // already tcp connected
                ASSERT_EQ(0, called); // request is not serialized yet.
                my_connect->MakeConnectDone();
                ASSERT_LT(0, called); // serialized
            }
            int64_t start_time = butil::gettimeofday_us();
            while (s->fd() < 0) {
                bthread_usleep(1000);
                ASSERT_LT(butil::gettimeofday_us(), start_time + 1000000L) << "Too long!";
            }
#if defined(OS_LINUX)
            ASSERT_EQ(0, bthread_fd_wait(s->fd(), EPOLLIN));
#elif defined(OS_MACOSX)
            ASSERT_EQ(0, bthread_fd_wait(s->fd(), EVFILT_READ));
#endif
            char dest[sizeof(buf)];
            ASSERT_EQ(meta_len + len, (size_t)read(s->fd(), dest, sizeof(dest)));
            ASSERT_EQ(0, memcmp(buf + 12, dest, meta_len + len));
        }
        ASSERT_EQ(0, s->SetFailed());
    }
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    // The id is invalid.
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(-1, brpc::Socket::Address(id, &ptr));

    messenger->StopAccept(0);
    ASSERT_EQ(-1, messenger->listened_fd());
    ASSERT_EQ(-1, fcntl(listening_fd, F_GETFD));
    ASSERT_EQ(EBADF, errno);

    // The socket object is likely to be reused,
    // and the local side should be initialized.
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr s;
    ASSERT_EQ(0, brpc::Socket::Address(id, &s));
    ASSERT_TRUE(s.get());
    ASSERT_EQ(-1, s->fd());
    ASSERT_EQ(butil::EndPoint(), s->local_side());
    ASSERT_EQ(point, s->remote_side());
}

#define NUMBER_WIDTH 16

struct WriterArg {
    size_t times;
    size_t offset;
    brpc::SocketId socket_id;
};

void* FailedWriter(void* void_arg) {
    WriterArg* arg = static_cast<WriterArg*>(void_arg);
    brpc::SocketUniquePtr sock;
    if (brpc::Socket::Address(arg->socket_id, &sock) < 0) {
        printf("Fail to address SocketId=%" PRIu64 "\n", arg->socket_id);
        return NULL;
    }
    char buf[32];
    for (size_t i = 0; i < arg->times; ++i) {
        bthread_id_t id;
        EXPECT_EQ(0, bthread_id_create(&id, NULL, NULL));
        snprintf(buf, sizeof(buf), "%0" BAIDU_SYMBOLSTR(NUMBER_WIDTH) "lu",
                 i + arg->offset);
        butil::IOBuf src;
        src.append(buf);
        brpc::Socket::WriteOptions wopt;
        wopt.id_wait = id;
        sock->Write(&src, &wopt);
        EXPECT_EQ(0, bthread_id_join(id));
        // Only the first connect can see ECONNREFUSED and then
        // calls `SetFailed' making others' error_code=EINVAL
        //EXPECT_EQ(ECONNREFUSED, error_code);
    }
    return NULL;
}

TEST_F(SocketTest, fail_to_connect) {
    const size_t REP = 10;
    butil::EndPoint point(butil::IP_ANY, 7563/*not listened*/);
    brpc::SocketId id = 8888;
    brpc::SocketOptions options;
    options.remote_side = point;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    {
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(-1, s->fd());
        ASSERT_EQ(point, s->remote_side());
        ASSERT_EQ(id, s->id());
        pthread_t th[8];
        WriterArg args[ARRAY_SIZE(th)];
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            args[i].times = REP;
            args[i].offset = i * REP;
            args[i].socket_id = id;
            ASSERT_EQ(0, pthread_create(&th[i], NULL, FailedWriter, &args[i]));
        }
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            ASSERT_EQ(0, pthread_join(th[i], NULL));
        }
        ASSERT_EQ(-1, s->SetFailed());  // already SetFailed
        ASSERT_EQ(-1, s->fd());
    }
    // KeepWrite is possibly still running.
    int64_t start_time = butil::gettimeofday_us();
    while (global_sock != NULL) {
        bthread_usleep(1000);
        ASSERT_LT(butil::gettimeofday_us(), start_time + 1000000L) << "Too long!";
    }
    ASSERT_EQ(-1, brpc::Socket::Status(id));
    // The id is invalid.
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(-1, brpc::Socket::Address(id, &ptr));
}

TEST_F(SocketTest, not_health_check_when_nref_hits_0) {
    brpc::SocketId id = 8888;
    butil::EndPoint point(butil::IP_ANY, 7584/*not listened*/);
    brpc::SocketOptions options;
    options.remote_side = point;
    options.user = new CheckRecycle;
    options.health_check_interval_s = 1/*s*/;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    {
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(-1, s->fd());
        ASSERT_EQ(point, s->remote_side());
        ASSERT_EQ(id, s->id());

        char buf[64];
        const size_t meta_len = 4;
        *(uint32_t*)(buf + 12) = *(uint32_t*)"Meta";
        const size_t len = snprintf(buf + 12 + meta_len,
                                    sizeof(buf) - 12 - meta_len,
                                    "hello world!");
        memcpy(buf, "HULU", 4);
        // HULU uses host byte order directly...
        *(uint32_t*)(buf + 4) = len + meta_len;
        *(uint32_t*)(buf + 8) = meta_len;
        butil::IOBuf src;
        src.append(buf, 12 + meta_len + len);
        ASSERT_EQ(12 + meta_len + len, src.length());
#ifdef CONNECT_IN_KEEPWRITE
        bthread_id_t wait_id;
        WaitData data;
        ASSERT_EQ(0, bthread_id_create2(&wait_id, &data, OnWaitIdReset));
        brpc::Socket::WriteOptions wopt;
        wopt.id_wait = wait_id;
        ASSERT_EQ(0, s->Write(&src, &wopt));
        ASSERT_EQ(0, bthread_id_join(wait_id));
        ASSERT_EQ(wait_id.value, data.id.value);
        ASSERT_EQ(ECONNREFUSED, data.error_code);
        ASSERT_TRUE(butil::StringPiece(data.error_text).starts_with(
                        "Fail to connect "));
#else
        ASSERT_EQ(-1, s->Write(&src));
        ASSERT_EQ(ECONNREFUSED, errno);
#endif
        ASSERT_TRUE(src.empty());
        ASSERT_EQ(-1, s->fd());
        s->ReleaseHCRelatedReference();
    }
    // StartHealthCheck is possibly still running. Spin until global_sock
    // is NULL(set in CheckRecycle::BeforeRecycle). Notice that you should
    // not spin until Socket::Status(id) becomes -1 and assert global_sock
    // to be NULL because invalidating id happens before calling BeforeRecycle.
    const int64_t start_time = butil::gettimeofday_us();
    while (global_sock != NULL) {
        bthread_usleep(1000);
        ASSERT_LT(butil::gettimeofday_us(), start_time + 1000000L);
    }
    ASSERT_EQ(-1, brpc::Socket::Status(id));
}

class HealthCheckTestServiceImpl : public test::HealthCheckTestService {
public:
    HealthCheckTestServiceImpl()
        : _sleep_flag(true) {}
    virtual ~HealthCheckTestServiceImpl() {}

    virtual void default_method(google::protobuf::RpcController* cntl_base,
                                const test::HealthCheckRequest* request,
                                test::HealthCheckResponse* response,
                                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = (brpc::Controller*)cntl_base;
        if (_sleep_flag) {
            bthread_usleep(510000 /* 510ms, a little bit longer than the default
                                     timeout of health check rpc */);
        }
        cntl->response_attachment().append("OK");
    }

    bool _sleep_flag;
};

TEST_F(SocketTest, app_level_health_check) {
    int old_health_check_interval = brpc::FLAGS_health_check_interval;
    GFLAGS_NAMESPACE::SetCommandLineOption("health_check_path", "/HealthCheckTestService");
    GFLAGS_NAMESPACE::SetCommandLineOption("health_check_interval", "1");

    butil::EndPoint point(butil::IP_ANY, 7777);
    brpc::ChannelOptions options;
    options.protocol = "http";
    options.max_retry = 0;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init(point, &options));
    {
        brpc::Controller cntl;
        cntl.http_request().uri() = "/";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        EXPECT_TRUE(cntl.Failed());
        ASSERT_EQ(ECONNREFUSED, cntl.ErrorCode());
    }

    // 2s to make sure remote is connected by HealthCheckTask and enter the
    // sending-rpc state. Because the remote is not down, so hc rpc would keep
    // sending.
    int listening_fd = tcp_listen(point);
    bthread_usleep(2000000);

    // 2s to make sure HealthCheckTask find socket is failed and correct impl
    // should trigger next round of hc
    close(listening_fd);
    bthread_usleep(2000000);
   
    brpc::Server server;
    HealthCheckTestServiceImpl hc_service;
    ASSERT_EQ(0, server.AddService(&hc_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(point, NULL));

    for (int i = 0; i < 4; ++i) {
        // although ::connect would succeed, the stall in hc_service makes
        // the health check rpc fail.
        brpc::Controller cntl;
        cntl.http_request().uri() = "/";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_EQ(EHOSTDOWN, cntl.ErrorCode());
        bthread_usleep(1000000 /*1s*/);
    }
    hc_service._sleep_flag = false;
    bthread_usleep(2000000 /* a little bit longer than hc rpc timeout + hc interval */);
    // should recover now
    {
        brpc::Controller cntl;
        cntl.http_request().uri() = "/";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_GT(cntl.response_attachment().size(), (size_t)0);
    }

    GFLAGS_NAMESPACE::SetCommandLineOption("health_check_path", "");
    char hc_buf[8];
    snprintf(hc_buf, sizeof(hc_buf), "%d", old_health_check_interval);
    GFLAGS_NAMESPACE::SetCommandLineOption("health_check_interval", hc_buf);
}

TEST_F(SocketTest, health_check) {
    // FIXME(gejun): Messenger has to be new otherwise quitting may crash.
    brpc::Acceptor* messenger = new brpc::Acceptor;

    brpc::SocketId id = 8888;
    butil::EndPoint point(butil::IP_ANY, 7878);
    const int kCheckInteval = 1;
    brpc::SocketOptions options;
    options.remote_side = point;
    options.user = new CheckRecycle;
    options.health_check_interval_s = kCheckInteval/*s*/;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::Socket* s = NULL;
    {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        s = ptr.get();
    }
    global_sock = s;
    ASSERT_NE(nullptr, s);
    ASSERT_EQ(-1, s->fd());
    ASSERT_EQ(point, s->remote_side());
    ASSERT_EQ(id, s->id());
    int32_t nref = -1;
    ASSERT_EQ(0, brpc::Socket::Status(id, &nref));
    ASSERT_EQ(2, nref);

    char buf[64];
    const size_t meta_len = 4;
    *(uint32_t*)(buf + 12) = *(uint32_t*)"Meta";
    const size_t len = snprintf(buf + 12 + meta_len,
                                sizeof(buf) - 12 - meta_len,
                                "hello world!");
    memcpy(buf, "HULU", 4);
    // HULU uses host byte order directly...
    *(uint32_t*)(buf + 4) = len + meta_len;
    *(uint32_t*)(buf + 8) = meta_len;
    const bool use_my_message = (butil::fast_rand_less_than(2) == 0);
    brpc::SocketMessagePtr<MyMessage> msg;
    int appended_msg = 0;
    butil::IOBuf src;
    if (use_my_message) {
        LOG(INFO) << "Use MyMessage";
        msg.reset(new MyMessage(buf, 12 + meta_len + len, &appended_msg));
    } else {
        src.append(buf, 12 + meta_len + len);
        ASSERT_EQ(12 + meta_len + len, src.length());
    }
#ifdef CONNECT_IN_KEEPWRITE
    bthread_id_t wait_id;
    WaitData data;
    ASSERT_EQ(0, bthread_id_create2(&wait_id, &data, OnWaitIdReset));
    brpc::Socket::WriteOptions wopt;
    wopt.id_wait = wait_id;
    if (use_my_message) {
        ASSERT_EQ(0, s->Write(msg, &wopt));
    } else {
        ASSERT_EQ(0, s->Write(&src, &wopt));
    }
    ASSERT_EQ(0, bthread_id_join(wait_id));
    ASSERT_EQ(wait_id.value, data.id.value);
    ASSERT_EQ(ECONNREFUSED, data.error_code);
    ASSERT_TRUE(butil::StringPiece(data.error_text).starts_with(
                    "Fail to connect "));
    if (use_my_message) {
        ASSERT_TRUE(appended_msg);
    }
#else
    if (use_my_message) {
        ASSERT_EQ(-1, s->Write(msg));
    } else {
        ASSERT_EQ(-1, s->Write(&src));
    }
    ASSERT_EQ(ECONNREFUSED, errno);
#endif
    ASSERT_TRUE(src.empty());
    ASSERT_EQ(-1, s->fd());
    ASSERT_TRUE(global_sock);
    brpc::SocketUniquePtr invalid_ptr;
    ASSERT_EQ(-1, brpc::Socket::Address(id, &invalid_ptr));
    ASSERT_EQ(1, brpc::Socket::Status(id));

    const brpc::InputMessageHandler pairs[] = {
        { brpc::policy::ParseHuluMessage, 
          EchoProcessHuluRequest, NULL, NULL, "dummy_hulu" }
    };

    int listening_fd = tcp_listen(point);
    ASSERT_TRUE(listening_fd > 0);
    butil::make_non_blocking(listening_fd);
    ASSERT_EQ(0, messenger->AddHandler(pairs[0]));
    ASSERT_EQ(0, messenger->StartAccept(listening_fd, -1, NULL, false));

    int64_t start_time = butil::gettimeofday_us();
    nref = -1;
    while (brpc::Socket::Status(id, &nref) != 0) {
        bthread_usleep(1000);
        ASSERT_LT(butil::gettimeofday_us(),
                  start_time + kCheckInteval * 1000000L + 100000L/*100ms*/);
    }
    //ASSERT_EQ(2, nref);
    ASSERT_TRUE(global_sock);

    int fd = 0;
    {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        ASSERT_NE(0, ptr->fd());
        fd = ptr->fd();
    }

    // SetFailed again, should reconnect and succeed soon.
    ASSERT_EQ(0, s->SetFailed());
    ASSERT_EQ(fd, s->fd());
    start_time = butil::gettimeofday_us();
    while (brpc::Socket::Status(id) != 0) {
        bthread_usleep(1000);
        ASSERT_LT(butil::gettimeofday_us(), start_time + 1200000L);
    }
    ASSERT_TRUE(global_sock);

    {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        ASSERT_NE(0, ptr->fd());
    }

    s->ReleaseHCRelatedReference();

    // Must stop messenger before SetFailed the id otherwise StartHealthCheck
    // still has chance to get reconnected and revive the id.
    messenger->StopAccept(0);
    ASSERT_EQ(-1, messenger->listened_fd());
    ASSERT_EQ(-1, fcntl(listening_fd, F_GETFD));
    ASSERT_EQ(EBADF, errno);

    ASSERT_EQ(0, brpc::Socket::SetFailed(id));
    // StartHealthCheck is possibly still addressing the Socket.
    start_time = butil::gettimeofday_us();
    while (global_sock != NULL) {
        bthread_usleep(1000);
        ASSERT_LT(butil::gettimeofday_us(), start_time + 1000000L);
    }
    nref = 0;
    ASSERT_EQ(-1, brpc::Socket::Status(id, &nref)) << "nref=" << nref;
    // The id is invalid.
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(-1, brpc::Socket::Address(id, &ptr));
}

void* Writer(void* void_arg) {
    WriterArg* arg = static_cast<WriterArg*>(void_arg);
    brpc::SocketUniquePtr sock;
    if (brpc::Socket::Address(arg->socket_id, &sock) < 0) {
        printf("Fail to address SocketId=%" PRIu64 "\n", arg->socket_id);
        return NULL;
    }
    char buf[32];
    for (size_t i = 0; i < arg->times; ++i) {
        snprintf(buf, sizeof(buf), "%0" BAIDU_SYMBOLSTR(NUMBER_WIDTH) "lu",
                 i + arg->offset);
        butil::IOBuf src;
        src.append(buf);
        if (sock->Write(&src) != 0) {
            if (errno == brpc::EOVERCROWDED) {
                // The buf is full, sleep a while and retry.
                bthread_usleep(1000);
                --i;
                continue;
            }
            printf("Fail to write into SocketId=%" PRIu64 ", %s\n",
                   arg->socket_id, berror());
            break;
        }
    }
    return NULL;
}

TEST_F(SocketTest, multi_threaded_write) {
    const size_t REP = 20000;
    int fds[2];
    for (int k = 0; k < 2; ++k) {
        printf("Round %d\n", k + 1);
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        pthread_t th[8];
        WriterArg args[ARRAY_SIZE(th)];
        std::vector<size_t> result;
        result.reserve(ARRAY_SIZE(th) * REP);

        brpc::SocketId id = 8888;
        butil::EndPoint dummy;
        ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
        brpc::SocketOptions options;
        options.fd = fds[1];
        options.remote_side = dummy;
        options.user = new CheckRecycle;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(id, &s));    
        s->_ssl_state = brpc::SSL_OFF;
        global_sock = s.get();
        ASSERT_TRUE(s.get());
        ASSERT_EQ(fds[1], s->fd());
        ASSERT_EQ(dummy, s->remote_side());
        ASSERT_EQ(id, s->id());
        butil::make_non_blocking(fds[0]);

        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            args[i].times = REP;
            args[i].offset = i * REP;
            args[i].socket_id = id;
            ASSERT_EQ(0, pthread_create(&th[i], NULL, Writer, &args[i]));
        }

        if (k == 1) {
            printf("sleep 100ms to block writers\n");
            bthread_usleep(100000);
        }
        
        butil::IOPortal dest;
        const int64_t start_time = butil::gettimeofday_us();
        for (;;) {
            ssize_t nr = dest.append_from_file_descriptor(fds[0], 32768);
            if (nr < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (EAGAIN != errno) {
                    ASSERT_EQ(EAGAIN, errno) << berror();
                }
                bthread_usleep(1000);
                if (butil::gettimeofday_us() >= start_time + 2000000L) {
                    LOG(FATAL) << "Wait too long!";
                    break;
                }
                continue;
            }
            while (dest.length() >= NUMBER_WIDTH) {
                char buf[NUMBER_WIDTH + 1];
                dest.copy_to(buf, NUMBER_WIDTH);
                buf[sizeof(buf)-1] = 0;
                result.push_back(strtol(buf, NULL, 10));
                dest.pop_front(NUMBER_WIDTH);
            }
            if (result.size() >= REP * ARRAY_SIZE(th)) {
                break;
            }
        }
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            ASSERT_EQ(0, pthread_join(th[i], NULL));
        }
        ASSERT_TRUE(dest.empty());
        bthread::g_task_control->print_rq_sizes(std::cout);
        std::cout << std::endl;

        ASSERT_EQ(REP * ARRAY_SIZE(th), result.size()) 
            << "write_head=" << s->_write_head;
        std::sort(result.begin(), result.end());
        result.resize(std::unique(result.begin(),
                                  result.end()) - result.begin());
        ASSERT_EQ(REP * ARRAY_SIZE(th), result.size());
        ASSERT_EQ(0UL, *result.begin());
        ASSERT_EQ(REP * ARRAY_SIZE(th) - 1, *(result.end() - 1));

        ASSERT_EQ(0, s->SetFailed());
        s.release()->Dereference();
        ASSERT_EQ((brpc::Socket*)NULL, global_sock);
        close(fds[0]);
    }
}

void* FastWriter(void* void_arg) {
    WriterArg* arg = static_cast<WriterArg*>(void_arg);
    brpc::SocketUniquePtr sock;
    if (brpc::Socket::Address(arg->socket_id, &sock) < 0) {
        printf("Fail to address SocketId=%" PRIu64 "\n", arg->socket_id);
        return NULL;
    }
    char buf[] = "hello reader side!";
    int64_t begin_ts = butil::cpuwide_time_us();
    int64_t nretry = 0;
    size_t c = 0;
    for (; c < arg->times; ++c) {
        butil::IOBuf src;
        src.append(buf, 16);
        if (sock->Write(&src) != 0) {
            if (errno == brpc::EOVERCROWDED) {
                // The buf is full, sleep a while and retry.
                bthread_usleep(1000);
                --c;
                ++nretry;
                continue;
            }
            printf("Fail to write into SocketId=%" PRIu64 ", %s\n",
                   arg->socket_id, berror());
            break;
        }
    }
    int64_t end_ts = butil::cpuwide_time_us();
    int64_t total_time = end_ts - begin_ts;
    printf("total=%ld count=%ld nretry=%ld\n",
           (long)total_time * 1000/ c, (long)c, (long)nretry);
    return NULL;
}

struct ReaderArg {
    int fd;
    size_t nread;
};

void* reader(void* void_arg) {
    ReaderArg* arg = static_cast<ReaderArg*>(void_arg);
    const size_t LEN = 32768;
    char* buf = (char*)malloc(LEN);
    while (1) {
        ssize_t nr = read(arg->fd, buf, LEN);
        if (nr < 0) {
            printf("Fail to read, %m\n");
            return NULL;
        } else if (nr == 0) {
            printf("Far end closed\n");
            return NULL;
        }
        arg->nread += nr;
    }
    return NULL;
}

TEST_F(SocketTest, multi_threaded_write_perf) {
    const size_t REP = 1000000000;
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    bthread_t th[3];
    WriterArg args[ARRAY_SIZE(th)];

    brpc::SocketId id = 8888;
    butil::EndPoint dummy;
    ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
    brpc::SocketOptions options;
    options.fd = fds[1];
    options.remote_side = dummy;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr s;
    ASSERT_EQ(0, brpc::Socket::Address(id, &s));
    s->_ssl_state = brpc::SSL_OFF;
    ASSERT_EQ(2, brpc::NRefOfVRef(s->_versioned_ref));
    global_sock = s.get();
    ASSERT_TRUE(s.get());
    ASSERT_EQ(fds[1], s->fd());
    ASSERT_EQ(dummy, s->remote_side());
    ASSERT_EQ(id, s->id());

    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].times = REP;
        args[i].offset = i * REP;
        args[i].socket_id = id;
        bthread_start_background(&th[i], NULL, FastWriter, &args[i]);
    }

    pthread_t rth;
    ReaderArg reader_arg = { fds[0], 0 };
    pthread_create(&rth, NULL, reader, &reader_arg);

    butil::Timer tm;
    ProfilerStart("write.prof");
    const uint64_t old_nread = reader_arg.nread;
    tm.start();
    sleep(2);
    tm.stop();
    const uint64_t new_nread = reader_arg.nread;
    ProfilerStop();

    printf("tp=%" PRIu64 "M/s\n", (new_nread - old_nread) / tm.u_elapsed());
    
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].times = 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, bthread_join(th[i], NULL));
    }
    ASSERT_EQ(0, s->SetFailed());
    s.release()->Dereference();
    pthread_join(rth, NULL);
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    close(fds[0]);
}

void GetKeepaliveValue(int fd,
                       int& keepalive,
                       int& keepalive_idle,
                       int& keepalive_interval,
                       int& keepalive_count) {
    {
        socklen_t len = sizeof(keepalive);
        ASSERT_EQ(0, getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &len));

    }

    {
        socklen_t len = sizeof(keepalive_idle);
#if defined(OS_MACOSX)
        ASSERT_EQ(0, getsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &keepalive_idle, &len));
#elif defined(OS_LINUX)
        ASSERT_EQ(0, getsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &keepalive_idle, &len));
#endif
    }

    {
        socklen_t len = sizeof(keepalive_interval);
#if defined(OS_MACOSX)
        ASSERT_EQ(0, getsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepalive_interval, &len));
#elif defined(OS_LINUX)
        ASSERT_EQ(0, getsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &keepalive_interval, &len));
#endif
    }

    {
        socklen_t len = sizeof(keepalive_count);
#if defined(OS_MACOSX)
        ASSERT_EQ(0, getsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_count, &len));
#elif defined(OS_LINUX)
        ASSERT_EQ(0, getsockopt(fd, SOL_TCP, TCP_KEEPCNT, &keepalive_count, &len));
#endif
    }
}

void CheckNoKeepalive(int fd) {
    int keepalive = -1;
    socklen_t len = sizeof(keepalive);
    ASSERT_EQ(0, getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &len));
    ASSERT_EQ(0, keepalive);
}

void CheckKeepalive(int fd,
                    bool expected_keepalive,
                    int expected_keepalive_idle,
                    int expected_keepalive_interval,
                    int expected_keepalive_count) {

    int keepalive = -1;
    int keepalive_idle = -1;
    int keepalive_interval = -1;
    int keepalive_count = -1;
    GetKeepaliveValue(fd, keepalive, keepalive_idle,
                      keepalive_interval, keepalive_count);
    if (expected_keepalive) {
        ASSERT_LT(0, keepalive);
    } else {
        ASSERT_EQ(0, keepalive);
        return;
    }
    ASSERT_EQ(expected_keepalive_idle, keepalive_idle);
    ASSERT_EQ(expected_keepalive_interval, keepalive_interval);
    ASSERT_EQ(expected_keepalive_count, keepalive_count);
}

TEST_F(SocketTest, keepalive) {
    int default_keepalive = 0;
    int default_keepalive_idle = 0;
    int default_keepalive_interval = 0;
    int default_keepalive_count = 0;
    {
        butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
        ASSERT_GT(sockfd, 0);
        GetKeepaliveValue(sockfd, default_keepalive, default_keepalive_idle,
                          default_keepalive_interval, default_keepalive_count);
    }

    // Disable keepalive.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckNoKeepalive(ptr->fd());
    }

    int keepalive_idle = 1;
    int keepalive_interval = 2;
    int keepalive_count = 2;
    // Enable keepalive.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, default_keepalive_idle,
                       default_keepalive_interval, default_keepalive_count);
    }

    // Enable keepalive and set keepalive idle.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_idle_s
            = keepalive_idle;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, keepalive_idle,
                       default_keepalive_interval,
                       default_keepalive_count);
    }

    // Enable keepalive and set keepalive interval.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_interval_s
            = keepalive_interval;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, default_keepalive_idle,
                       keepalive_interval, default_keepalive_count);
    }

    // Enable keepalive and set keepalive count.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_count = keepalive_count;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, default_keepalive_idle,
                       default_keepalive_interval, keepalive_count);
    }

    // Enable keepalive and set keepalive idle, interval, count.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_idle_s = keepalive_idle;
        options.keepalive_options->keepalive_interval_s = keepalive_interval;
        options.keepalive_options->keepalive_count = keepalive_count;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, keepalive_idle,
                       keepalive_interval, keepalive_count);
    }
}

TEST_F(SocketTest, keepalive_input_message) {
    int default_keepalive = 0;
    int default_keepalive_idle = 0;
    int default_keepalive_interval = 0;
    int default_keepalive_count = 0;
    {
        butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
        ASSERT_GT(sockfd, 0);
        GetKeepaliveValue(sockfd, default_keepalive, default_keepalive_idle,
                          default_keepalive_interval, default_keepalive_count);
    }

    // Disable keepalive.
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckNoKeepalive(ptr->fd());
    }

    // Enable keepalive.
    brpc::FLAGS_socket_keepalive = true;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, default_keepalive_idle,
                       default_keepalive_interval, default_keepalive_count);
    }

    // Enable keepalive and set keepalive idle.
    brpc::FLAGS_socket_keepalive_idle_s = 10;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, brpc::FLAGS_socket_keepalive_idle_s,
                       default_keepalive_interval, default_keepalive_count);
    }

    // Enable keepalive and set keepalive idle, interval.
    brpc::FLAGS_socket_keepalive_interval_s = 10;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, brpc::FLAGS_socket_keepalive_idle_s,
                       brpc::FLAGS_socket_keepalive_interval_s, default_keepalive_count);
    }

    // Enable keepalive and set keepalive idle, interval, count.
    brpc::FLAGS_socket_keepalive_count = 10;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, brpc::FLAGS_socket_keepalive_idle_s,
                       brpc::FLAGS_socket_keepalive_interval_s,
                       brpc::FLAGS_socket_keepalive_count);
    }

    // Options of keepalive set by user have priority over Gflags.
    int keepalive_idle = 2;
    int keepalive_interval = 2;
    int keepalive_count = 2;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_idle_s = keepalive_idle;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, keepalive_idle,
                       brpc::FLAGS_socket_keepalive_interval_s,
                       brpc::FLAGS_socket_keepalive_count);
    }

    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_interval_s = keepalive_interval;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, brpc::FLAGS_socket_keepalive_idle_s,
                       keepalive_interval, brpc::FLAGS_socket_keepalive_count);
    }

    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_count = keepalive_count;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, brpc::FLAGS_socket_keepalive_idle_s,
                       brpc::FLAGS_socket_keepalive_interval_s, keepalive_count);
    }

    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.keepalive_options = std::make_shared<brpc::SocketKeepaliveOptions>();
        options.keepalive_options->keepalive_idle_s = keepalive_idle;
        options.keepalive_options->keepalive_interval_s = keepalive_interval;
        options.keepalive_options->keepalive_count = keepalive_count;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckKeepalive(ptr->fd(), true, keepalive_idle,
                       keepalive_interval, keepalive_count);
    }
}

#if defined(OS_LINUX)
void CheckTCPUserTimeout(int fd, int expect_tcp_user_timeout) {
    int tcp_user_timeout = 0;
    socklen_t len = sizeof(tcp_user_timeout);
    ASSERT_EQ(0, getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &tcp_user_timeout, &len) );
    ASSERT_EQ(tcp_user_timeout, expect_tcp_user_timeout);
}

TEST_F(SocketTest, tcp_user_timeout) {
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckTCPUserTimeout(ptr->fd(), 0);
    }

    {
        int tcp_user_timeout_ms = 1000;
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.tcp_user_timeout_ms = tcp_user_timeout_ms;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::Socket::Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckTCPUserTimeout(ptr->fd(), tcp_user_timeout_ms);
    }

    brpc::FLAGS_socket_tcp_user_timeout_ms = 2000;
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckTCPUserTimeout(ptr->fd(), brpc::FLAGS_socket_tcp_user_timeout_ms);
    }
    {
        int tcp_user_timeout_ms = 3000;
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GT(sockfd, 0);
        brpc::SocketOptions options;
        options.fd = sockfd;
        options.tcp_user_timeout_ms = tcp_user_timeout_ms;
        brpc::SocketId id;
        ASSERT_EQ(0, brpc::get_or_new_client_side_messenger()->Create(options, &id));
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(id, &ptr));
        CheckTCPUserTimeout(ptr->fd(), tcp_user_timeout_ms);
    }
}
#endif

int HandleSocketSuccessWrite(bthread_id_t id, void* data, int error_code,
    const std::string& error_text) {
    auto success_count = static_cast<size_t*>(data);
    EXPECT_NE(nullptr, success_count);
    EXPECT_EQ(0, error_code);
    ++(*success_count);
    CHECK_EQ(0, bthread_id_unlock_and_destroy(id));
    return 0;
}

TEST_F(SocketTest, notify_on_success) {
    const size_t REP = 10000;
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    brpc::SocketId id = 8888;
    butil::EndPoint dummy;
    ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
    brpc::SocketOptions options;
    options.fd = fds[1];
    options.remote_side = dummy;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr s;
    ASSERT_EQ(0, brpc::Socket::Address(id, &s));
    s->_ssl_state = brpc::SSL_OFF;
    ASSERT_EQ(2, brpc::NRefOfVRef(s->_versioned_ref));
    global_sock = s.get();
    ASSERT_TRUE(s.get());
    ASSERT_EQ(fds[1], s->fd());
    ASSERT_EQ(dummy, s->remote_side());
    ASSERT_EQ(id, s->id());

    pthread_t rth;
    ReaderArg reader_arg = { fds[0], 0 };
    pthread_create(&rth, NULL, reader, &reader_arg);

    size_t success_count = 0;
    char buf[] = "hello reader side!";
    for (size_t c = 0; c < REP; ++c) {
        bthread_id_t write_id;
        ASSERT_EQ(0, bthread_id_create2(&write_id, &success_count,
            HandleSocketSuccessWrite));
        brpc::Socket::WriteOptions wopt;
        wopt.id_wait = write_id;
        wopt.notify_on_success = true;
        butil::IOBuf src;
        src.append(buf, 16);
        if (s->Write(&src, &wopt) != 0) {
            if (errno == brpc::EOVERCROWDED) {
                // The buf is full, sleep a while and retry.
                bthread_usleep(1000);
                --c;
                continue;
            }
            PLOG(ERROR) << "Fail to write into SocketId=" << id;
            break;
        }
    }
    bthread_usleep(1000 * 1000);

    ASSERT_EQ(0, s->SetFailed());
    s.release()->Dereference();
    pthread_join(rth, NULL);
    ASSERT_EQ(REP, success_count);
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    close(fds[0]);
}

struct ShutdownWriterArg {
    size_t times;
    brpc::SocketId socket_id;
    butil::atomic<int> total_count;
    butil::atomic<int> success_count;
};

int HandleSocketShutdownWrite(bthread_id_t id, void* data, int error_code,
    const std::string& error_text) {
    auto arg = static_cast<ShutdownWriterArg*>(data);
    EXPECT_NE(nullptr, arg);
    EXPECT_TRUE(0 == error_code || brpc::ESHUTDOWNWRITE == error_code) << error_code;
    ++arg->total_count;
    if (0 == error_code) {
        ++arg->success_count;
    }
    CHECK_EQ(0, bthread_id_unlock_and_destroy(id));
    return 0;
}

void* ShutdownWriter(void* void_arg) {
    auto arg = static_cast<ShutdownWriterArg*>(void_arg);
    brpc::SocketUniquePtr sock;
    if (brpc::Socket::Address(arg->socket_id, &sock) < 0) {
        LOG(INFO) << "Fail to address SocketId=" << arg->socket_id;
        return NULL;
    }
    for (size_t c = 0; c < arg->times; ++c) {
        bthread_id_t write_id;
        EXPECT_EQ(0, bthread_id_create2(&write_id, arg,
            HandleSocketShutdownWrite));
        brpc::Socket::WriteOptions wopt;
        wopt.id_wait = write_id;
        wopt.notify_on_success = true;
        wopt.shutdown_write = true;
        butil::IOBuf src;
        src.push_back('a');
        if (sock->Write(&src, &wopt) != 0) {
            if (errno == brpc::EOVERCROWDED) {
                // The buf is full, sleep a while and retry.
                bthread_usleep(1000);
                --c;
                continue;
            }
        }
    }
    return NULL;
}

void TestShutdownWrite() {
    const size_t REP = 100;
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    brpc::SocketId id = 8888;
    butil::EndPoint dummy;
    ASSERT_EQ(0, str2endpoint("192.168.1.26:8080", &dummy));
    brpc::SocketOptions options;
    options.fd = fds[1];
    options.remote_side = dummy;
    options.user = new CheckRecycle;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr s;
    ASSERT_EQ(0, brpc::Socket::Address(id, &s));
    s->_ssl_state = brpc::SSL_OFF;
    ASSERT_EQ(2, brpc::NRefOfVRef(s->_versioned_ref));
    global_sock = s.get();
    ASSERT_TRUE(s.get());
    ASSERT_EQ(fds[1], s->fd());
    ASSERT_EQ(dummy, s->remote_side());
    ASSERT_EQ(id, s->id());
    ASSERT_FALSE(s->IsWriteShutdown());

    pthread_t rth;
    ReaderArg reader_arg = { fds[0], 0 };
    pthread_create(&rth, NULL, reader, &reader_arg);

    bthread_t th[3];
    ShutdownWriterArg args[ARRAY_SIZE(th)];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        args[i].times = REP;
        args[i].socket_id = id;
        args[i].total_count = 0;
        args[i].success_count = 0;
        bthread_start_background(&th[i], NULL, ShutdownWriter, &args[i]);
    }

    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, bthread_join(th[i], NULL));
    }
    bthread_usleep(50 * 1000);

    ASSERT_TRUE(s->IsWriteShutdown());
    ASSERT_FALSE(s->Failed());
    ASSERT_EQ(0, s->SetFailed());
    s.release()->Dereference();
    pthread_join(rth, NULL);
    ASSERT_EQ((brpc::Socket*)NULL, global_sock);
    close(fds[0]);

    size_t total_count = 0;
    size_t success_count = 0;
    for (auto & arg : args) {
        total_count += arg.total_count;
        success_count += arg.success_count;
    }
    ASSERT_EQ(REP * ARRAY_SIZE(th), total_count);
    EXPECT_EQ((size_t)1, reader_arg.nread);
    EXPECT_EQ((size_t)1, success_count);
}

TEST_F(SocketTest, shutdown_write) {
    for (int i = 0; i < 100; ++i) {
        TestShutdownWrite();
    }
}

TEST_F(SocketTest, packed_ptr) {
    brpc::PackedPtr<int> ptr;
    ASSERT_EQ(nullptr, ptr.get());
    ASSERT_EQ(0, ptr.extra());

    int a = 1;
    uint16_t b = 2;
    ptr.set(&a);
    ASSERT_EQ(&a, ptr.get());
    *ptr.get() = b;
    ASSERT_EQ(a, b);
    ptr.set_extra(b);
    ASSERT_EQ(b, ptr.extra());
    ptr.reset();
    ptr.reset_extra();
    ASSERT_EQ(nullptr, ptr.get());
    ASSERT_EQ(0, ptr.extra());

    int c = 3;
    uint16_t d = 4;
    ptr.set_ptr_and_extra(&c, d);
    ASSERT_EQ(&c, ptr.get());
    ASSERT_EQ(d, ptr.extra());
    ptr.reset_ptr_and_extra();
    ASSERT_EQ(nullptr, ptr.get());
    ASSERT_EQ(0, ptr.extra());
}
