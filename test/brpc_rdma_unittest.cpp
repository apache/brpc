// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2018 brpc-rdma authors

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/files/temp_file.h"

#ifdef BRPC_RDMA
#include "brpc/acceptor.h"
#include "brpc/controller.h"
#include "brpc/channel.h"
#include "brpc/global.h"
#include "brpc/server.h"
#include "test/echo.pb.h"

DEFINE_string(ip, "0.0.0.0", "ip address of the rdma device");

static butil::EndPoint s_ep;

class MyEchoService : public ::test::EchoService {
    void Echo(google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              google::protobuf::Closure* done) {
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        brpc::ClosureGuard done_guard(done);
        if (req->server_fail()) {
            cntl->SetFailed(req->server_fail(), "Server fail1");
            cntl->SetFailed(req->server_fail(), "Server fail2");
            return;
        }
        if (req->close_fd()) {
            LOG(INFO) << "close fd...";
            cntl->CloseConnection("Close connection according to request");
            return;
        }
        if (req->sleep_us() > 0) {
            LOG(INFO) << "sleep " << req->sleep_us() << "us...";
            bthread_usleep(req->sleep_us());
        }
        res->set_message(req->message());
        if (req->code() != 0) {
            res->add_code_list(req->code());
        }
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

class RdmaTest : public ::testing::Test {
protected:
    RdmaTest() {
        butil::ip_t ip;
        if (butil::str2ip(FLAGS_ip.c_str(), &ip) == 0) {
            butil::EndPoint ep(ip, 8003);
            s_ep = ep;
            EXPECT_EQ(0, _server_list.save(butil::endpoint2str(s_ep).c_str()));
            _naming_url = std::string("File://") + _server_list.fname();
        } else {
            std::cout << "ip is not correct!" << std::endl;
        }
    }
    ~RdmaTest() { }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:
    void StartServer() {
        _server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
        brpc::ServerOptions options;
        options.enabled_protocols = "rdma";
        options.idle_timeout_sec = -1;
        options.max_concurrency = 0;
        options.internal_port = -1;
        _server.Start(8003, &options);
    }

    void StopServer() {
        _server.Stop(0);
        _server.Join();
    }

    int SetUpChannel(brpc::Channel* channel, 
            bool single_server, bool short_connection) {
        brpc::ChannelOptions opt;
        opt.protocol = "rdma";
        if (short_connection) {
            opt.connection_type = brpc::CONNECTION_TYPE_SHORT;
        }
        opt.max_retry = 0;
        if (single_server) {
            return channel->Init(s_ep, &opt);
        } else {                                                 
            return channel->Init(_naming_url.c_str(), "rR", &opt);
        }                                         
    }

    void CallMethod(brpc::ChannelBase* channel, 
            brpc::Controller* cntl,
            test::EchoRequest* req, test::EchoResponse* res,
            bool async, bool attachment = false,
            bool destroy = false) {
        google::protobuf::Closure* done = NULL;                     
        brpc::CallId sync_id = { 0 };
        if (async) {
            sync_id = cntl->call_id();
            done = brpc::DoNothing();
        }
        if (attachment) {
            std::string message;
            message.resize(1048576, 'a');
            butil::IOBuf attachment;
            attachment.append(message);
            cntl->request_attachment().append(attachment);
        }
        ::test::EchoService::Stub(channel).Echo(cntl, req, res, done);
        if (async) {
            if (destroy) {
                delete channel;
            }
            // Callback MUST be called for once and only once
            bthread_id_join(sync_id);
        }
    }

    void TestCancelBeforeCall(bool single_server, bool async,
                              bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        brpc::Controller cntl;
        const brpc::CallId cid = cntl.call_id();
        ASSERT_TRUE(cid.value != 0);
        brpc::StartCancel(cid);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(ECANCELED, cntl.ErrorCode()) << cntl.ErrorText();
    }

    struct CancelerArg {
        int64_t sleep_before_cancel_us;
        brpc::CallId cid;
    };

    static void* Canceler(void* void_arg) {
        CancelerArg* arg = static_cast<CancelerArg*>(void_arg);
        if (arg->sleep_before_cancel_us > 0) {
            bthread_usleep(arg->sleep_before_cancel_us);
        }
        LOG(INFO) << "Start to cancel cid=" << arg->cid.value;
        brpc::StartCancel(arg->cid);
        return NULL;
    }

    void TestCancelDuringCall(bool single_server, bool async,
                              bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        const brpc::CallId cid = cntl.call_id();
        ASSERT_TRUE(cid.value != 0);
        pthread_t th;
        CancelerArg carg = { 10000, cid };
        ASSERT_EQ(0, pthread_create(&th, NULL, Canceler, &carg));
        req.set_sleep_us(carg.sleep_before_cancel_us * 2);
        butil::Timer tm;
        tm.start();
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        tm.stop();
        EXPECT_LT(labs(tm.u_elapsed() - carg.sleep_before_cancel_us), 10000);
        ASSERT_EQ(0, pthread_join(th, NULL));
        EXPECT_EQ(ECANCELED, cntl.ErrorCode());
        EXPECT_EQ(0, cntl.sub_count());
        EXPECT_TRUE(NULL == cntl.sub(1));
        EXPECT_TRUE(NULL == cntl.sub(0));
    }

    void TestCancelAfterCall(bool single_server, bool async,
                             bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        const brpc::CallId cid = cntl.call_id();
        ASSERT_TRUE(cid.value != 0);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(0, cntl.ErrorCode());
        EXPECT_EQ(0, cntl.sub_count());
        ASSERT_EQ(EINVAL, bthread_id_error(cid, ECANCELED));
    }

    void TestRpcTimeout(bool single_server, bool async,
                        bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_sleep_us(70000); // 70ms
        cntl.set_timeout_ms(17);
        butil::Timer tm;
        tm.start();
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        tm.stop();
        EXPECT_EQ(brpc::ERPCTIMEDOUT, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_LT(labs(tm.m_elapsed() - cntl.timeout_ms()), 10);
    }

    void TestCloseFd(bool single_server, bool async,
                     bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_close_fd(true);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(brpc::EEOF, cntl.ErrorCode()) << cntl.ErrorText();
    }

    void TestServerFail(bool single_server, bool async,
                        bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel channel;
        if (SetUpChannel(&channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_server_fail(brpc::EINTERNAL);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(brpc::EINTERNAL, cntl.ErrorCode()) << cntl.ErrorText();
    }

    void TestDestroyChannel(bool single_server,
                            bool short_connection, bool attachment) {
        std::cout << "*** single=" << single_server
                  << ", short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        brpc::Channel* channel = new brpc::Channel();
        if (SetUpChannel(channel, single_server, short_connection) != 0) {
            LOG(WARNING) << "channel init failed";
            return;
        }
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_sleep_us(10000);
        CallMethod(channel, &cntl, &req, &res, true, attachment, true/*destroy*/);
        EXPECT_EQ(0, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_EQ(std::string(__FUNCTION__), res.message());
        // Sleep to let `_server' detect `Socket' being `SetFailed'
        const int64_t start_time = butil::gettimeofday_us();
        while (_server._am->ConnectionCount() != 0) {
            EXPECT_LT(butil::gettimeofday_us(), start_time + 100000L/*100ms*/);
            bthread_usleep(1000);
        }
    }

    butil::TempFile _server_list;                                        
    std::string _naming_url;

    brpc::Server _server;
    MyEchoService _svc;
};

void* StartClient(void* arg) {
    size_t att_size = (size_t)arg;
    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.protocol = "rdma";
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 3;
    if (channel.Init(s_ep, &chan_options) == 0) {
        test::EchoService_Stub stub(&channel);

        brpc::Controller cntl;
        std::string message("hello world! 111111");

        std::string att;
        att.resize(att_size, 'a');
        butil::IOBuf attachment;
        attachment.append(att);

        int cnt = 100;
        while (cnt--) {
            test::EchoRequest request;
            test::EchoResponse response;
            request.set_message(message);
            cntl.request_attachment().append(attachment);
            cntl.set_log_id(cnt);
            stub.Echo(&cntl, &request, &response, NULL);
            EXPECT_EQ(0, cntl.ErrorCode());
            EXPECT_EQ(0, request.message().compare(response.message()));
            EXPECT_EQ(att_size, cntl.response_attachment().size());
            cntl.Reset();
        }
    } else {
        LOG(WARNING) << "channel init failed";
    }
    return NULL;
}

TEST_F(RdmaTest, success_multi_clients) {
    StartServer();
    int client_num = 8;
    bthread_t tids[client_num];
    for (int i = 0; i < client_num; ++i) {
        bthread_start_background(&tids[i], NULL, StartClient, (void*)1024);
    }
    for (int i = 0; i < client_num; ++i) {
        ASSERT_EQ(0, bthread_join(tids[i], NULL));
    }
    for (int i = 0; i < client_num; ++i) {
        bthread_start_background(&tids[i], NULL, StartClient, (void*)1048576);
    }
    for (int i = 0; i < client_num; ++i) {
        ASSERT_EQ(0, bthread_join(tids[i], NULL));
    }
    StopServer();
}

TEST_F(RdmaTest, cancel_before_call) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelBeforeCall(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, cancel_during_call) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelDuringCall(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, cancel_after_call) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelAfterCall(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, rpc_timeout) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestRpcTimeout(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, close_fd) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCloseFd(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, server_fail) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestServerFail(i, j, k, l);
                }
            }
        }
    }
    StopServer();
}

TEST_F(RdmaTest, destroy_channel) {
    StartServer();
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
            for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                TestDestroyChannel(i, k, l);
            }
        }
    }
    StopServer();
}

#else

class RdmaTest : public ::testing::Test {
protected:
    RdmaTest() { }
    ~RdmaTest() { }
};

TEST_F(RdmaTest, dummy) { }

#endif

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);
#ifdef BRPC_RDMA
    brpc::GlobalInitializeOrDie();
#endif
    return RUN_ALL_TESTS();
}

