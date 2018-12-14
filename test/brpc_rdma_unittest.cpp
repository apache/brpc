// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2018 baidu-rpc authors

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netinet/in.h>
#include <google/protobuf/descriptor.h>
#include "butil/files/temp_file.h"
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"
#include "butil/iobuf.h"
#include "butil/sys_byteorder.h"
#include "butil/string_printf.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/global.h"
#include "brpc/parallel_channel.h"
#include "brpc/selective_channel.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/errno.pb.h"
#include "brpc/policy/giano_authenticator.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_fallback_channel.h"
#include "brpc/rdma/rdma_helper.h"
#include "echo.pb.h"

static const int PORT = 8103;

DEFINE_string(ip, "0.0.0.0", "ip address of the rdma device");

namespace brpc {
namespace rdma {
extern int (*RdmaCreateId)(rdma_event_channel *channel, rdma_cm_id **id,
                    void *context, rdma_port_space ps);
extern int (*RdmaDestroyId)(rdma_cm_id *id);
extern int (*RdmaResolveAddr)(rdma_cm_id *id, sockaddr *src_addr,
                       sockaddr *dst_addr, int timeout_ms);
extern int (*RdmaBindAddr)(rdma_cm_id *id, sockaddr *addr);
extern int (*RdmaResolveRoute)(rdma_cm_id *id, int timeout_ms);
extern int (*RdmaListen)(rdma_cm_id *id, int backlog);
extern int (*RdmaConnect)(rdma_cm_id *id, rdma_conn_param *conn_param);
extern int (*RdmaDisconnect)(rdma_cm_id *id);
DECLARE_int32(rdma_cq_num);
DECLARE_int32(rdma_cq_size);
DECLARE_bool(rdma_disable_local_connection);
extern bool DestinationInGivenCluster(std::string prefix, in_addr_t addr);
extern void InitRdmaConnParam(rdma_conn_param* p, const char* data, size_t len);
extern size_t g_rdma_recv_block_size;
}
}

static butil::EndPoint g_ep;

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
            usleep(1);
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
            butil::EndPoint ep(ip, PORT);
            g_ep = ep;
            EXPECT_EQ(0, _server_list.save(butil::endpoint2str(g_ep).c_str()));
            _naming_url = std::string("File://") + _server_list.fname();
        } else {
            std::cout << "ip is not correct!" << std::endl;
        }
        _server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
    }
    ~RdmaTest() { }

    virtual void SetUp() { }

    virtual void TearDown() { }

private:
    void StartServer() {
        brpc::ServerOptions options;
        options.use_rdma = true;
        options.idle_timeout_sec = 1;
        options.max_concurrency = 0;
        options.internal_port = -1;
        EXPECT_EQ(0, _server.Start(PORT, &options));
    }

    void StopServer() {
        _server.Stop(0);
        _server.Join();
    }

    void SetUpChannel(brpc::Channel* channel, 
            bool single_server, bool short_connection) {
        brpc::ChannelOptions opt;
        opt.use_rdma = true;
        if (short_connection) {
            opt.connection_type = brpc::CONNECTION_TYPE_SHORT;
        }
        opt.max_retry = 0;
        if (single_server) {
            EXPECT_EQ(0, channel->Init(g_ep, &opt));
        } else {                                                 
            EXPECT_EQ(0, channel->Init(_naming_url.c_str(), "rR", &opt));
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
        SetUpChannel(&channel, single_server, short_connection);
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

        StartServer();

        brpc::Channel channel;
        SetUpChannel(&channel, single_server, short_connection);
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        const brpc::CallId cid = cntl.call_id();
        ASSERT_TRUE(cid.value != 0);
        pthread_t th;
        CancelerArg carg = { 100000, cid };
        ASSERT_EQ(0, pthread_create(&th, NULL, Canceler, &carg));
        req.set_sleep_us(carg.sleep_before_cancel_us * 2);
        butil::Timer tm;
        tm.start();
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        tm.stop();
        EXPECT_EQ(ECANCELED, cntl.ErrorCode());
        EXPECT_LT(labs(tm.u_elapsed() - carg.sleep_before_cancel_us), 50000);
        ASSERT_EQ(0, pthread_join(th, NULL));
        EXPECT_TRUE(NULL == cntl.sub(1));
        EXPECT_TRUE(NULL == cntl.sub(0));

        StopServer();
    }

    void TestCancelAfterCall(bool single_server, bool async,
                             bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        StartServer();

        brpc::Channel channel;
        SetUpChannel(&channel, single_server, short_connection);
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        cntl.set_timeout_ms(500);
        const brpc::CallId cid = cntl.call_id();
        ASSERT_TRUE(cid.value != 0);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(0, cntl.ErrorCode());
        ASSERT_EQ(EINVAL, bthread_id_error(cid, ECANCELED));

        StopServer();
    }

    void TestRpcTimeout(bool single_server, bool async,
                        bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        StartServer();

        brpc::Channel channel;
        SetUpChannel(&channel, single_server, short_connection);
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_sleep_us(200000); // 70ms
        cntl.set_timeout_ms(100);
        butil::Timer tm;
        tm.start();
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        tm.stop();
        EXPECT_EQ(brpc::ERPCTIMEDOUT, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_LT(labs(tm.m_elapsed() - cntl.timeout_ms()), 50);

        StopServer();
    }

    void TestCloseFd(bool single_server, bool async,
                     bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        StartServer();

        brpc::Channel channel;
        SetUpChannel(&channel, single_server, short_connection);
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_close_fd(true);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(brpc::EEOF, cntl.ErrorCode()) << cntl.ErrorText();

        StopServer();
    }

    void TestServerFail(bool single_server, bool async,
                        bool short_connection, bool attachment) {
        std::cout << " *** single=" << single_server
                  << " async=" << async
                  << " short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        StartServer();

        brpc::Channel channel;
        SetUpChannel(&channel, single_server, short_connection);
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message(__FUNCTION__);
        req.set_server_fail(brpc::EINTERNAL);
        CallMethod(&channel, &cntl, &req, &res, async, attachment);
        EXPECT_EQ(brpc::EINTERNAL, cntl.ErrorCode()) << cntl.ErrorText();

        StopServer();
    }

    void TestDestroyChannel(bool single_server,
                            bool short_connection, bool attachment) {
        std::cout << "*** single=" << single_server
                  << ", short=" << short_connection
                  << " attachment=" << attachment << std::endl;

        StartServer();

        brpc::Channel* channel = new brpc::Channel();
        SetUpChannel(channel, single_server, short_connection);
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

        StopServer();
    }

    butil::TempFile _server_list;
    std::string _naming_url;

    brpc::Server _server;
    MyEchoService _svc;
};

struct StartClientOptions {
    std::string protocol;
    bool use_rdma;
    size_t att_size;
    bool user_data;
    bool large_block;
    bool single_connection;

    StartClientOptions()
        : protocol("baidu_std")
        , use_rdma(true)
        , att_size(0)
        , user_data(0)
        , large_block(false)
        , single_connection(true) { }
};

void DestroyData(void* buf) {
    brpc::rdma::DeregisterMemoryForRdma(buf);
    free(buf);
}

void* StartClient(void* arg) {
    StartClientOptions* opt = (StartClientOptions*)arg;

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    if (!opt->single_connection) {
        chan_options.connection_type = "pooled";
    }
    chan_options.use_rdma = opt->use_rdma;
    chan_options.protocol = opt->protocol;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    test::EchoService_Stub stub(&channel);
    brpc::Controller cntl;
    std::string message("hello world!");
    butil::IOBuf attachment;
    std::string att_str = "hello world!";
    att_str = att_str.substr(0, std::min(att_str.size(), opt->att_size));
    if (!opt->user_data) {
        if (!opt->large_block) {
            std::string att(att_str);
            att.resize(opt->att_size, 'a');
            attachment.append(att);
        } else {
            butil::IOBufAsZeroCopyOutputStream os(&attachment, 65536);
            void* data = NULL;
            int len = 0;
            size_t total_len = 0;
            do {
                EXPECT_TRUE(os.Next(&data, &len));
                total_len += len;
            } while (total_len < opt->att_size);
            if (total_len > opt->att_size) {
                os.BackUp(total_len - opt->att_size);
            }
            strcpy(const_cast<char*>(attachment.backing_block(0).data()), att_str.c_str());
        }
    } else {
        void* data = malloc(opt->att_size);
        memset(data, 'a', opt->att_size);
        memcpy(data, att_str.c_str(), att_str.size());
        brpc::rdma::RegisterMemoryForRdma(data, opt->att_size);
        attachment.append_user_data(data, opt->att_size, DestroyData);
    }

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
        EXPECT_EQ(opt->att_size, cntl.response_attachment().size());
        butil::IOBuf tmp;
        cntl.response_attachment().cutn(&tmp, att_str.size());
        EXPECT_EQ(att_str, tmp.to_string());
        cntl.Reset();
    }

    return NULL;
}

// This should be the first test case
TEST_F(RdmaTest, success_iobuf_created_before_rdma_initialization) {
    butil::IOBuf attachment;
    attachment.append("hello world");

    StartServer();

    // Wait for block pool ready
    void* buf = brpc::rdma::AllocBlock(brpc::rdma::g_rdma_recv_block_size);
    brpc::rdma::DeallocBlock(buf);

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    test::EchoService_Stub stub(&channel);
    brpc::Controller cntl;
    test::EchoRequest request;
    test::EchoResponse response;
    request.set_message("hello world");
    cntl.request_attachment().append(attachment);
    stub.Echo(&cntl, &request, &response, NULL);

    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

static int att_size[2] = { 1024, 1048576 };

TEST_F(RdmaTest, success) {
    StartServer();

    StartClientOptions opt;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                for (int l = 0; l < 2; ++l) {
                    LOG(INFO) << "attachment_size=" << att_size[i]
                              << ", single connection=" << j
                              << ", large block=" << k
                              << ", user data=" << l;
                    opt.att_size = att_size[i];
                    opt.single_connection = j;
                    opt.large_block = k;
                    opt.user_data = l;
                    StartClient(&opt);
                }
            }
        }
    }

    StopServer();
}

TEST_F(RdmaTest, success_with_other_rpc_protocols) {
    StartServer();

    std::vector<std::string> protocols;
    protocols.push_back("hulu_pbrpc");
    protocols.push_back("sofa_pbrpc");
    protocols.push_back("http");
    StartClientOptions opt;
    for (size_t i = 0; i < protocols.size(); ++i) {
        LOG(INFO) << "protocol=" << protocols[i];
        opt.protocol = protocols[i];
        StartClient(&opt);
    }

    StopServer();
}

TEST_F(RdmaTest, success_multi_clients) {
    StartServer();

    int client_num = 8;
    bthread_t tids[client_num];
    StartClientOptions opt;

    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                for (int l = 0; l < 2; ++l) {
                    LOG(INFO) << "attachment_size=" << att_size[i]
                              << ", single connection=" << j
                              << ", large block=" << k
                              << ", user data=" << l;
                    opt.att_size = att_size[i];
                    opt.single_connection = j;
                    opt.large_block = k;
                    opt.user_data = l;
                    for (int m = 0; m < client_num; ++m) {
                        ASSERT_EQ(0, bthread_start_background(
                            &tids[m], NULL, StartClient, &opt));
                    }
                    for (int m = 0; m < client_num; ++m) {
                        ASSERT_EQ(0, bthread_join(tids[m], NULL));
                    }
                }
            }
        }
    }

    StopServer();
}

TEST_F(RdmaTest, handshake_failure_incorrect_sid) {
    StartServer();

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    brpc::SocketUniquePtr s;
    EXPECT_EQ(0, brpc::Socket::AddressFailedAsWell(channel._server_id, &s));
    EXPECT_TRUE(s != NULL);

    // Normal Socket
    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(50000);
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, NULL);

    usleep(10000);

    // Abnormal rdmacm connection with the above Socket
    brpc::SocketId sid = s->_rdma_ep->_remote_sid;
    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = brpc::rdma::GetRdmaIP();
    addr.sin_port = htons(s->remote_side().port);
    rdma_cm_id* cm_id = NULL;
    EXPECT_EQ(0, brpc::rdma::RdmaCreateId(NULL, &cm_id, NULL, RDMA_PS_TCP));
    EXPECT_EQ(0, brpc::rdma::RdmaResolveAddr(cm_id, NULL, (sockaddr*)&addr, 1));
    EXPECT_EQ(0, brpc::rdma::RdmaResolveRoute(cm_id, 1));
    std::string tmp;
    butil::string_printf(&tmp, "%ld%s", butil::HostToNet64(sid), "0000");
    rdma_conn_param param;
    brpc::rdma::InitRdmaConnParam(&param, tmp.c_str(), tmp.size());
    EXPECT_EQ(-1, brpc::rdma::RdmaConnect(cm_id, &param));  // should fail

    bthread_id_join(cntl.call_id());

    // Should not affect the normal Socket
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, client_incomplete_hello_during_handshake) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);
    EXPECT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    EXPECT_EQ(brpc::rdma::MAGIC_LENGTH,
              write(sockfd, brpc::rdma::MAGIC_STR, brpc::rdma::MAGIC_LENGTH));
    sleep(3);  // sleep to let the server release the idle connection
    EXPECT_EQ(0, _server._am->ConnectionCount());

    StopServer();
}

TEST_F(RdmaTest, client_miss_during_handshake) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    char rand[brpc::rdma::RANDOM_LENGTH] = { 0 };

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);
    EXPECT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    EXPECT_EQ(brpc::rdma::MAGIC_LENGTH,
        write(sockfd, brpc::rdma::MAGIC_STR, brpc::rdma::MAGIC_LENGTH));
    EXPECT_EQ(brpc::rdma::RANDOM_LENGTH,
        write(sockfd, &rand, brpc::rdma::RANDOM_LENGTH));
    sleep(3);  // sleep to let the server release the idle connection
    EXPECT_EQ(0, _server._am->ConnectionCount());

    StopServer();
}

TEST_F(RdmaTest, client_abort_during_handshake) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = brpc::rdma::GetRdmaIP();
    addr.sin_port = htons(PORT);
    char rand[brpc::rdma::MAGIC_LENGTH] = { '0' };
    char sid[sizeof(brpc::SocketId) + brpc::rdma::RANDOM_LENGTH] = { '0' };

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);
    EXPECT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    EXPECT_EQ(brpc::rdma::MAGIC_LENGTH,
        write(sockfd, brpc::rdma::MAGIC_STR, brpc::rdma::MAGIC_LENGTH));
    EXPECT_EQ(brpc::rdma::RANDOM_LENGTH,
        write(sockfd, &rand, brpc::rdma::RANDOM_LENGTH));
    EXPECT_EQ(sizeof(brpc::SocketId), read(sockfd, &sid, sizeof(brpc::SocketId)));
    rdma_cm_id* cm_id = NULL;
    EXPECT_EQ(0, brpc::rdma::RdmaCreateId(NULL, &cm_id, NULL, RDMA_PS_TCP));
    EXPECT_EQ(0, brpc::rdma::RdmaResolveAddr(cm_id, NULL, (sockaddr*)&addr, 1));
    EXPECT_EQ(0, brpc::rdma::RdmaResolveRoute(cm_id, 1));
    rdma_conn_param param;
    brpc::rdma::InitRdmaConnParam(&param, sid, 12);
    EXPECT_EQ(0, butil::make_non_blocking(cm_id->channel->fd));
    if (brpc::rdma::RdmaConnect(cm_id, &param) < 0) {
        EXPECT_EQ(EAGAIN, errno);
        close(sockfd);
        usleep(100000);
        EXPECT_EQ(0, _server._am->ConnectionCount());
    }

    StopServer();
}

TEST_F(RdmaTest, server_miss_during_handshake) {
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = brpc::rdma::GetRdmaIP();

    EXPECT_EQ(0, bind(sockfd, (sockaddr*)&addr, sizeof(addr)));
    EXPECT_EQ(0, listen(sockfd, 1024));

    rdma_cm_id* cm_id;
    EXPECT_EQ(0, brpc::rdma::RdmaCreateId(NULL, &cm_id, NULL, RDMA_PS_TCP));
    EXPECT_EQ(0, brpc::rdma::RdmaBindAddr(cm_id, (sockaddr*)&addr));
    EXPECT_EQ(0, brpc::rdma::RdmaListen(cm_id, 1024));

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    butil::EndPoint ep;
    ep.ip = brpc::rdma::GetRdmaIP();
    ep.port = PORT;
    EXPECT_EQ(0, channel.Init(ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    EXPECT_TRUE(acc_fd >= 0);
    bthread_id_join(cntl.call_id());

    EXPECT_EQ(brpc::ERPCTIMEDOUT, cntl.ErrorCode());
    EXPECT_EQ(0, brpc::rdma::RdmaDestroyId(cm_id));
    close(sockfd);
}

TEST_F(RdmaTest, server_abort_during_handshake) {
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = brpc::rdma::GetRdmaIP();

    EXPECT_EQ(0, bind(sockfd, (sockaddr*)&addr, sizeof(addr)));
    EXPECT_EQ(0, listen(sockfd, 1024));

    rdma_cm_id* cm_id;
    EXPECT_EQ(0, brpc::rdma::RdmaCreateId(NULL, &cm_id, NULL, RDMA_PS_TCP));
    EXPECT_EQ(0, brpc::rdma::RdmaBindAddr(cm_id, (sockaddr*)&addr));
    EXPECT_EQ(0, brpc::rdma::RdmaListen(cm_id, 1024));

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    butil::EndPoint ep;
    ep.ip = brpc::rdma::GetRdmaIP();
    ep.port = PORT;
    EXPECT_EQ(0, channel.Init(ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    EXPECT_TRUE(acc_fd >= 0);
    usleep(10000);
    char sid[sizeof(brpc::SocketId)];
    EXPECT_EQ(sizeof(brpc::SocketId), write(acc_fd, sid, sizeof(brpc::SocketId)));
    usleep(10000);
    close(acc_fd);
    bthread_id_join(cntl.call_id());

    EXPECT_EQ(EHOSTDOWN, cntl.ErrorCode());
    EXPECT_EQ(0, brpc::rdma::RdmaDestroyId(cm_id));
    close(sockfd);
}

TEST_F(RdmaTest, handshake_incorrect_protocol_client) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    char rand[brpc::rdma::RANDOM_LENGTH] = { '0' };

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);
    EXPECT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    EXPECT_EQ(brpc::rdma::MAGIC_LENGTH, 
        write(sockfd, brpc::rdma::MAGIC_STR, brpc::rdma::MAGIC_LENGTH));
    EXPECT_EQ(brpc::rdma::RANDOM_LENGTH,
        write(sockfd, &rand, brpc::rdma::RANDOM_LENGTH));
    EXPECT_EQ(brpc::rdma::RANDOM_LENGTH,
        write(sockfd, &rand, brpc::rdma::RANDOM_LENGTH));

    StopServer();
}

TEST_F(RdmaTest, handshake_incorrect_protocol_server) {
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    EXPECT_TRUE(sockfd >= 0);

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = brpc::rdma::GetRdmaIP();

    EXPECT_EQ(0, bind(sockfd, (sockaddr*)&addr, sizeof(addr)));
    EXPECT_EQ(0, listen(sockfd, 1024));

    rdma_cm_id* cm_id;
    EXPECT_EQ(0, brpc::rdma::RdmaCreateId(NULL, &cm_id, NULL, RDMA_PS_TCP));
    EXPECT_EQ(0, brpc::rdma::RdmaBindAddr(cm_id, (sockaddr*)&addr));
    EXPECT_EQ(0, brpc::rdma::RdmaListen(cm_id, 1024));

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    butil::EndPoint ep;
    ep.ip = brpc::rdma::GetRdmaIP();
    ep.port = PORT;
    EXPECT_EQ(0, channel.Init(ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    EXPECT_TRUE(acc_fd >= 0);
    usleep(10000);
    char tmp[sizeof(brpc::SocketId)] = { 1 };
    EXPECT_EQ(sizeof(brpc::SocketId), write(acc_fd, tmp, sizeof(brpc::SocketId)));
    EXPECT_EQ(sizeof(brpc::SocketId), write(acc_fd, tmp, sizeof(brpc::SocketId)));
    bthread_id_join(cntl.call_id());

    EXPECT_EQ(EPROTO, cntl.ErrorCode());
    EXPECT_EQ(0, brpc::rdma::RdmaDestroyId(cm_id));
    close(sockfd);
}

TEST_F(RdmaTest, socket_revive) {
    StartServer();

    brpc::Channel channel;
    SetUpChannel(&channel, true, false);

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    cntl.Reset();
    sleep(2);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, rdmacm_disconnect) {
    StartServer();

    brpc::Channel channel;
    SetUpChannel(&channel, true, false);

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);  // wait for rdmacm connection completed
    brpc::Socket* socket;
    {
        brpc::SocketUniquePtr m;
        brpc::SocketId sid = _server._am->_socket_map.begin()->first;
        EXPECT_EQ(0, brpc::Socket::Address(sid, &m));
        EXPECT_TRUE(m != NULL && m->_rdma_ep->_rcm != NULL);
        socket = m.get();
    }

    brpc::rdma::RdmaDisconnect((rdma_cm_id*)socket->_rdma_ep->_rcm->_cm_id);
    bthread_id_join(cntl.call_id());
    EXPECT_EQ(brpc::EEOF, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, verbs_error) {
    StartServer();

    brpc::Channel channel;
    SetUpChannel(&channel, true, false);
    brpc::SocketUniquePtr s;
    EXPECT_EQ(0, brpc::Socket::AddressFailedAsWell(channel._server_id, &s));
    EXPECT_TRUE(s != NULL);

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);  // wait for rdmacm connection completed

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    void* buf = brpc::rdma::AllocBlock(8192);
    ibv_sge sge;
    sge.addr = (uint64_t)buf;
    sge.length = 8192;
    sge.lkey = 1;
    wr.wr_id = s->id();
    wr.sg_list = &sge;
    wr.num_sge = 1;
    ibv_send_wr* bad = NULL;
    rdma_cm_id* cm_id = (rdma_cm_id*)s->_rdma_ep->_rcm->_cm_id;
    ibv_post_send(cm_id->qp, &wr, &bad);
    bthread_id_join(cntl.call_id());
    EXPECT_EQ(brpc::ERDMA, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, rdma_fallback_channel) {
    StartServer();

    brpc::SocketUniquePtr s;
    brpc::Controller cntl;
    test::EchoResponse res;
    {
        brpc::rdma::RdmaFallbackChannel channel;
        brpc::ChannelOptions opt;
        opt.use_rdma = true;
        EXPECT_EQ(0, channel.Init(g_ep, &opt));

        EXPECT_EQ(0, brpc::Socket::AddressFailedAsWell(
                    channel._rdma_chan._server_id, &s));
        EXPECT_TRUE(s != NULL);

        test::EchoRequest req;
        req.set_message(__FUNCTION__);
        req.set_sleep_us(200000);
        google::protobuf::Closure* done = brpc::DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
        bthread_id_join(cntl.call_id());
        EXPECT_EQ(0, cntl.ErrorCode());

        cntl.Reset();
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    }

    usleep(100000);  // wait for rdmacm connection completed

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    void* buf = brpc::rdma::AllocBlock(8192);
    ibv_sge sge;
    sge.addr = (uint64_t)buf;
    sge.length = 8192;
    sge.lkey = 1;
    wr.wr_id = s->id();
    wr.sg_list = &sge;
    wr.num_sge = 1;
    ibv_send_wr* bad = NULL;
    rdma_cm_id* cm_id = (rdma_cm_id*)s->_rdma_ep->_rcm->_cm_id;
    s.reset(NULL);
    ibv_post_send(cm_id->qp, &wr, &bad);
    bthread_id_join(cntl.call_id());
    EXPECT_EQ(0, cntl.ErrorCode());

    sleep(3);  // must sleep to let this socket recover

    StopServer();
}

TEST_F(RdmaTest, tcp_channel_extra_data) {
    StartServer();

    brpc::Channel channel;
    SetUpChannel(&channel, true, false);

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    google::protobuf::Closure* done = brpc::DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);  // wait for rdmacm connection completed

    brpc::Socket* socket;
    {
        brpc::SocketUniquePtr m;
        brpc::SocketId sid = _server._am->_socket_map.begin()->first;
        EXPECT_EQ(0, brpc::Socket::Address(sid, &m));
        EXPECT_TRUE(m != NULL && m->_rdma_ep->_rcm != NULL);
        socket = m.get();
    }
    char c;
    EXPECT_EQ(1, write(socket->fd(), &c, 1));
    bthread_id_join(cntl.call_id());
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, tcp_client_to_rdma_server) {
    StartServer();

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, rdma_client_to_tcp_server) {
    brpc::ServerOptions options;
    options.idle_timeout_sec = 1;
    options.max_concurrency = 0;
    options.internal_port = -1;
    brpc::Server server;
    server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
    EXPECT_EQ(0, server.Start(PORT, &options));

    brpc::Channel channel;
    SetUpChannel(&channel, true, false);

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    server.Stop(0);
    server.Join();
}

TEST_F(RdmaTest, tcp_client_to_tcp_server) {
    brpc::ServerOptions options;
    options.idle_timeout_sec = 1;
    options.max_concurrency = 0;
    options.internal_port = -1;
    brpc::Server server;
    server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
    EXPECT_EQ(0, server.Start(PORT, &options));

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    server.Stop(0);
    server.Join();
}

TEST_F(RdmaTest, both_clients_to_tcp_server) {
    StartServer();

    brpc::ChannelOptions options;
    options.connect_timeout_ms = 500;
    options.timeout_ms = 500;
    brpc::Channel channel1;
    EXPECT_EQ(0, channel1.Init(g_ep, &options));
    options.use_rdma = true;
    brpc::Channel channel2;
    EXPECT_EQ(0, channel2.Init(g_ep, &options));

    test::EchoRequest req;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = brpc::DoNothing();
    test::EchoResponse res1;
    brpc::Controller cntl1;
    ::test::EchoService::Stub(&channel1).Echo(&cntl1, &req, &res1, done);
    brpc::Controller cntl2;
    test::EchoResponse res2;
    ::test::EchoService::Stub(&channel2).Echo(&cntl2, &req, &res2, done);

    bthread_id_join(cntl1.call_id());
    bthread_id_join(cntl2.call_id());
    EXPECT_EQ(0, cntl1.ErrorCode());
    EXPECT_EQ(0, cntl2.ErrorCode());
    EXPECT_EQ(2, _server._am->ConnectionCount());

    StopServer();
}

TEST_F(RdmaTest, server_option_invalid) {
    brpc::Server server;
    brpc::ServerOptions options;
    options.use_rdma = true;

    options.rtmp_service = (brpc::RtmpService*)1;
    EXPECT_EQ(-1, server.Start(PORT, &options));

    options.rtmp_service = NULL;
    options.nshead_service = (brpc::NsheadService*)1;
    EXPECT_EQ(-1, server.Start(PORT, &options));

    options.nshead_service = NULL;
    options.mongo_service_adaptor = (brpc::MongoServiceAdaptor*)1;
    EXPECT_EQ(-1, server.Start(PORT, &options));

    options.mongo_service_adaptor = NULL;
    options.mutable_ssl_options()->default_cert.certificate = "test";
    EXPECT_EQ(-1, server.Start(PORT, &options));
}

TEST_F(RdmaTest, channel_option_invalid) {
    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;

    chan_options.protocol = "rtmp";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "streaming_rpc";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "nshead";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "nshead_mcpack";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "nova_pbrpc";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "public_pbrpc";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "redis";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "memcache";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "ubrpc_compack";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "itp";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "esp";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "baidu_std";
    chan_options.mutable_ssl_options()->sni_name = "test";
    EXPECT_EQ(-1, channel.Init(g_ep, &chan_options));
}

TEST_F(RdmaTest, use_compress) {
    StartServer();

    brpc::Channel channel;
    brpc::ChannelOptions chan_options;
    chan_options.use_rdma = true;
    EXPECT_EQ(0, channel.Init(g_ep, &chan_options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    cntl.set_request_compress_type(brpc::COMPRESS_TYPE_ZLIB);
    CallMethod(&channel, &cntl, &req, &res, false);
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

class SetCode : public brpc::CallMapper {
public:
    brpc::SubCall Map(
        int channel_index,
        const google::protobuf::MethodDescriptor* method,
        const google::protobuf::Message* req_base,
        google::protobuf::Message* response) {
        test::EchoRequest* req = brpc::Clone<test::EchoRequest>(req_base);
        req->set_code(channel_index + 1/*non-zero*/);
        return brpc::SubCall(method, req, response->New(),
                            brpc::DELETE_REQUEST | brpc::DELETE_RESPONSE);
    }
};

TEST_F(RdmaTest, use_parallel_channel) {
    StartServer();

    const size_t NCHANS = 8;
    brpc::Channel subchans[NCHANS];
    brpc::ParallelChannel channel;
    for (size_t i = 0; i < NCHANS; ++i) {
        SetUpChannel(&subchans[i], false, false);
        EXPECT_EQ(0, channel.AddChannel(
                    &subchans[i], brpc::DOESNT_OWN_CHANNEL,
                    new SetCode, NULL));
    }

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_code(23);
    CallMethod(&channel, &cntl, &req, &res, true);
    EXPECT_EQ(0, cntl.ErrorCode());
    EXPECT_EQ(NCHANS, (size_t)cntl.sub_count());

    StopServer();
}

TEST_F(RdmaTest, use_selective_channel) {
    StartServer();

    const size_t NCHANS = 8;
    brpc::SelectiveChannel channel;
    brpc::ChannelOptions options;
    options.max_retry = 0;
    ASSERT_EQ(0, channel.Init("rr", &options));
    for (size_t i = 0; i < NCHANS; ++i) {
        brpc::Channel* subchan = new brpc::Channel;
        SetUpChannel(subchan, false, false);
        EXPECT_EQ(0, channel.AddChannel(subchan, NULL));
    }

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_code(23);
    CallMethod(&channel, &cntl, &req, &res, true);
    EXPECT_EQ(0, cntl.ErrorCode()) << cntl.ErrorText();
    EXPECT_EQ(1, cntl.sub_count());

    StopServer();
}

TEST_F(RdmaTest, rdma_cluster_filter) {
    butil::ip_t addr;
    EXPECT_EQ(0, butil::str2ip("192.168.1.1", &addr));
    in_addr_t in = ntohl(butil::ip2int(addr));

    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("0.0.0.0/0", in));     // illegal
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("0.0.0/0", in));       // illegal
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("0.0.0.0/a", in));     // illegal
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("0.0.0.0", in));       // illegal
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("192.168.1.1", in));   // illegal
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("192.168.1.1/32", in));
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("192.168.1.0/24", in));
    EXPECT_TRUE(brpc::rdma::DestinationInGivenCluster("192.168.0.0/16", in));
    EXPECT_FALSE(brpc::rdma::DestinationInGivenCluster("11.22.33.0/24", in));
    EXPECT_FALSE(brpc::rdma::DestinationInGivenCluster("192.168.1.128/25", in));
}

TEST_F(RdmaTest, cancel_before_call) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelBeforeCall(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, cancel_during_call) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelDuringCall(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, cancel_after_call) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCancelAfterCall(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, rpc_timeout) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestRpcTimeout(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, close_fd) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestCloseFd(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, server_fail) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int j = 0; j <= 1; ++j) { // Flag Asynchronous
            for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
                for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                    TestServerFail(i, j, k, l);
                }
            }
        }
    }
}

TEST_F(RdmaTest, destroy_channel) {
    for (int i = 0; i <= 1; ++i) { // Flag SingleServer 
        for (int k = 0; k <= 1; ++k) { // Flag ShortConnection
            for (int l = 0; l <= 1; ++l) { // Flag LargeAttachment
                TestDestroyChannel(i, k, l);
            }
        }
    }
}

// Must be the last test
TEST_F(RdmaTest, cq_overrun) {
    StartServer();

    int32_t saved_cq_size = brpc::rdma::FLAGS_rdma_cq_size;
    brpc::rdma::FLAGS_rdma_cq_size = 2;

    {
        int call_num = 128;
        brpc::Channel channel[call_num];
        brpc::Controller cntl[call_num];
        test::EchoRequest req[call_num];
        test::EchoResponse res[call_num];
        google::protobuf::Closure* done[call_num];
        for (int i = 0; i < call_num; ++i) {
            SetUpChannel(&channel[i], true, false);
            done[i] = brpc::DoNothing();
            req[i].set_message(__FUNCTION__);
        }
        for (int i = 0; i < call_num; ++i) {
            ::test::EchoService::Stub(&channel[i]).Echo(
                    &cntl[i], &req[i], &res[i], done[i]);
        }

        // If a CQ overruns, all the QPs using it will be set to error state,
        // which means that the connections will be stopped when there is a
        // timeout or other send/recv operations.

        int sum = 0;
        for (int i = 0; i < call_num; ++i) {
            bthread_id_join(cntl[i].call_id());
            if (cntl[i].ErrorCode() != 0) {
                ++sum;
            }
        }
        if (brpc::rdma::FLAGS_rdma_cq_num == 0 || saved_cq_size < 10) {
            EXPECT_LT(0, sum);
        }
    }

    brpc::Controller cntl;
    test::EchoRequest req;
    req.set_message(__FUNCTION__);
    test::EchoResponse res;
    if (brpc::rdma::FLAGS_rdma_cq_num > 0) {
        brpc::rdma::RdmaFallbackChannel channel;
        brpc::ChannelOptions opt;
        EXPECT_EQ(0, channel.Init(g_ep, &opt));
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, NULL);
    } else {
        brpc::Channel channel;
        SetUpChannel(&channel, true, false);
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, NULL);
    }
    EXPECT_EQ(0, cntl.ErrorCode());

    StopServer();
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);
    brpc::rdma::FLAGS_rdma_disable_local_connection = false;
    return RUN_ALL_TESTS();
}

