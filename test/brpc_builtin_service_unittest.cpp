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
#include <fstream>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/span.h"
#include "brpc/reloadable_flags.h"
#include "brpc/builtin/version_service.h"
#include "brpc/builtin/health_service.h"
#include "brpc/builtin/list_service.h"
#include "brpc/builtin/status_service.h"
#include "brpc/builtin/threads_service.h"
#include "brpc/builtin/vlog_service.h"
#include "brpc/builtin/index_service.h"        // IndexService
#include "brpc/builtin/connections_service.h"  // ConnectionsService
#include "brpc/builtin/flags_service.h"        // FlagsService
#include "brpc/builtin/vars_service.h"         // VarsService
#include "brpc/builtin/rpcz_service.h"         // RpczService
#include "brpc/builtin/dir_service.h"          // DirService
#include "brpc/builtin/pprof_service.h"        // PProfService
#include "brpc/builtin/bthreads_service.h"     // BthreadsService
#include "brpc/builtin/ids_service.h"          // IdsService
#include "brpc/builtin/sockets_service.h"      // SocketsService
#include "brpc/builtin/common.h"
#include "brpc/builtin/bad_method_service.h"
#include "echo.pb.h"

DEFINE_bool(foo, false, "Flags for UT");
BRPC_VALIDATE_GFLAG(foo, brpc::PassValidate);

namespace brpc {
DECLARE_bool(enable_rpcz);
DECLARE_bool(rpcz_hex_log_id);
DECLARE_int32(idle_timeout_second);
} // namespace rpc

int main(int argc, char* argv[]) {
    brpc::FLAGS_idle_timeout_second = 0;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class EchoServiceImpl : public test::EchoService {
public:
    EchoServiceImpl() {}
    virtual ~EchoServiceImpl() {}
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const test::EchoRequest* req,
                      test::EchoResponse* res,
                      google::protobuf::Closure* done) {
        brpc::Controller* cntl =
                static_cast<brpc::Controller*>(cntl_base);
        brpc::ClosureGuard done_guard(done);
        TRACEPRINTF("MyAnnotation: %ld", cntl->log_id());
        if (req->sleep_us() > 0) {
            bthread_usleep(req->sleep_us());
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRIu64, cntl->trace_id());
        res->set_message(buf);
    }
};

class ClosureChecker : public google::protobuf::Closure {
public:
    ClosureChecker() : _count(1) {}
    ~ClosureChecker() { EXPECT_EQ(0, _count); }
    
    void Run() {
        --_count;
    }
    
private:
    int _count;
};

void MyVLogSite() {
    VLOG(3) << "This is a VLOG!";
}

void CheckContent(const brpc::Controller& cntl, const char* name) {
    const std::string& content = cntl.response_attachment().to_string();
    std::size_t pos = content.find(name);
    ASSERT_TRUE(pos != std::string::npos) << "name=" << name;
}

void CheckErrorText(const brpc::Controller& cntl, const char* error) {
    std::size_t pos = cntl.ErrorText().find(error);
    ASSERT_TRUE(pos != std::string::npos) << "error=" << error;
}

void CheckFieldInContent(const brpc::Controller& cntl,
                         const char* name, int32_t expect) {
    const std::string& content = cntl.response_attachment().to_string();
    std::size_t pos = content.find(name);
    ASSERT_TRUE(pos != std::string::npos);

    int32_t val = 0;
    ASSERT_EQ(1, sscanf(content.c_str() + pos + strlen(name), "%d", &val));
    ASSERT_EQ(expect, val) << "name=" << name;
}

void CheckAnnotation(const brpc::Controller& cntl, int64_t expect) {
    const std::string& content = cntl.response_attachment().to_string();
    std::string expect_str;
    butil::string_printf(&expect_str, "MyAnnotation: %" PRId64, expect);
    std::size_t pos = content.find(expect_str);
    ASSERT_TRUE(pos != std::string::npos) << expect;
}

void CheckTraceId(const brpc::Controller& cntl,
                  const std::string& expect_id_str) {
    const std::string& content = cntl.response_attachment().to_string();
    std::string expect_str = std::string(brpc::TRACE_ID_STR) + "=" + expect_id_str;
    std::size_t pos = content.find(expect_str);
    ASSERT_TRUE(pos != std::string::npos) << expect_str;
}

class BuiltinServiceTest : public ::testing::Test{
protected:
    BuiltinServiceTest(){};
    virtual ~BuiltinServiceTest(){};
    virtual void SetUp() { EXPECT_EQ(0, _server.AddBuiltinServices()); }
    virtual void TearDown() { StopAndJoin(); }

    void StopAndJoin() {
        _server.Stop(0);
        _server.Join();
        _server.ClearServices();
    }

    void SetUpController(brpc::Controller* cntl, bool use_html) const {
        cntl->_server = &_server;
        if (use_html) {
            cntl->http_request().SetHeader(
                brpc::USER_AGENT_STR, "just keep user agent non-empty");
        }
    }

    void TestIndex(bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::IndexService service;
        brpc::IndexRequest req;
        brpc::IndexResponse res;
        brpc::Controller cntl;
        ClosureChecker done;
        SetUpController(&cntl, use_html);
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        EXPECT_EQ(expect_type, cntl.http_response().content_type());
    }

    void TestStatus(bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::StatusService service;
        brpc::StatusRequest req;
        brpc::StatusResponse res;
        brpc::Controller cntl;
        ClosureChecker done;
        SetUpController(&cntl, use_html);
        EchoServiceImpl echo_svc;
        ASSERT_EQ(0, _server.AddService(
            &echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        EXPECT_EQ(expect_type, cntl.http_response().content_type());
        ASSERT_EQ(0, _server.RemoveService(&echo_svc));
    }
    
    void TestVLog(bool use_html) {
#if !BRPC_WITH_GLOG
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::VLogService service;
        brpc::VLogRequest req;
        brpc::VLogResponse res;
        brpc::Controller cntl;
        ClosureChecker done;
        SetUpController(&cntl, use_html);
        MyVLogSite();
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        EXPECT_EQ(expect_type, cntl.http_response().content_type());
        CheckContent(cntl, "brpc_builtin_service_unittest");
#endif
    }
    
    void TestConnections(bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::ConnectionsService service;
        brpc::ConnectionsRequest req;
        brpc::ConnectionsResponse res;
        brpc::Controller cntl;
        ClosureChecker done;
        SetUpController(&cntl, use_html);
        butil::EndPoint ep;
        ASSERT_EQ(0, str2endpoint("127.0.0.1:9798", &ep));
        ASSERT_EQ(0, _server.Start(ep, NULL));
        int self_port = -1;
        const int cfd = tcp_connect(ep, &self_port);
        ASSERT_GT(cfd, 0);
        char buf[64];
        snprintf(buf, sizeof(buf), "127.0.0.1:%d", self_port);
        usleep(100000);
        
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        EXPECT_EQ(expect_type, cntl.http_response().content_type());
        CheckContent(cntl, buf);
        CheckFieldInContent(cntl, "channel_connection_count: ", 0);

        close(cfd);
        StopAndJoin();
    }
    
    void TestBadMethod(bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::BadMethodService service;
        brpc::BadMethodResponse res;
        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            brpc::BadMethodRequest req;
            req.set_service_name(
                brpc::PProfService::descriptor()->full_name());
            service.no_method(&cntl, &req, &res, &done);
            EXPECT_EQ(brpc::ENOMETHOD, cntl.ErrorCode());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            CheckErrorText(cntl, "growth");
        }
    }

    void TestFlags(bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::FlagsService service;
        brpc::FlagsRequest req;
        brpc::FlagsResponse res;
        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            CheckContent(cntl, "bthread_concurrency");
        }
        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request()._unresolved_path = "foo";
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            CheckContent(cntl, "false");
        }
        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request()._unresolved_path = "foo";
            cntl.http_request().uri()
                    .SetQuery(brpc::SETVALUE_STR, "true");
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
        }
        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request()._unresolved_path = "foo";
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            CheckContent(cntl, "true");
        }
    }

    void TestRpcz(bool enable, bool hex, bool use_html) {
        std::string expect_type = (use_html ? "text/html" : "text/plain");
        brpc::RpczService service;
        brpc::RpczRequest req;
        brpc::RpczResponse res;
        if (!enable) {
            {
                ClosureChecker done;
                brpc::Controller cntl;
                SetUpController(&cntl, use_html);
                service.disable(&cntl, &req, &res, &done);
                EXPECT_FALSE(cntl.Failed());
                EXPECT_FALSE(brpc::FLAGS_enable_rpcz);
            }
            {
                ClosureChecker done;
                brpc::Controller cntl;
                SetUpController(&cntl, use_html);
                service.default_method(&cntl, &req, &res, &done);
                EXPECT_FALSE(cntl.Failed());
                EXPECT_EQ(expect_type,
                          cntl.http_response().content_type());
                if (!use_html) {
                    CheckContent(cntl, "rpcz is not enabled");
                }
            }
            {
                ClosureChecker done;
                brpc::Controller cntl;
                SetUpController(&cntl, use_html);
                service.stats(&cntl, &req, &res, &done);
                EXPECT_FALSE(cntl.Failed());
                if (!use_html) {
                    CheckContent(cntl, "rpcz is not enabled");
                }
            }
            return;
        }

        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            service.enable(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            EXPECT_TRUE(brpc::FLAGS_enable_rpcz);
        }

        if (hex) {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            service.hex_log_id(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_TRUE(brpc::FLAGS_rpcz_hex_log_id);
        } else {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            service.dec_log_id(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            EXPECT_FALSE(brpc::FLAGS_rpcz_hex_log_id);
        }
        
        ASSERT_EQ(0, _server.AddService(new EchoServiceImpl(),
                                        brpc::SERVER_OWNS_SERVICE));
        butil::EndPoint ep;
        ASSERT_EQ(0, str2endpoint("127.0.0.1:9748", &ep));
        ASSERT_EQ(0, _server.Start(ep, NULL));
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init(ep, NULL));
        test::EchoService_Stub stub(&channel);
        int64_t log_id = 1234567890;
        char querystr_buf[128];
        // Since LevelDB is unstable on jerkins, disable all the assertions here
        {
            // Find by trace_id
            test::EchoRequest echo_req;
            test::EchoResponse echo_res;
            brpc::Controller echo_cntl;
            echo_req.set_message("hello");
            echo_cntl.set_log_id(++log_id);
            stub.Echo(&echo_cntl, &echo_req, &echo_res, NULL);
            EXPECT_FALSE(echo_cntl.Failed());

            // Wait for level db to commit span information
            usleep(500000);
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request().uri()
                    .SetQuery(brpc::TRACE_ID_STR, echo_res.message());
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            // CheckAnnotation(cntl, log_id);
        }

        {
            // Find by latency
            test::EchoRequest echo_req;
            test::EchoResponse echo_res;
            brpc::Controller echo_cntl;
            echo_req.set_message("hello");
            echo_req.set_sleep_us(150000);
            echo_cntl.set_log_id(++log_id);
            stub.Echo(&echo_cntl, &echo_req, &echo_res, NULL);
            EXPECT_FALSE(echo_cntl.Failed());

            // Wait for level db to commit span information
            usleep(500000);
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request().uri()
                    .SetQuery(brpc::MIN_LATENCY_STR, "100000");
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            // CheckTraceId(cntl, echo_res.message());
        }

        {
            // Find by request size
            test::EchoRequest echo_req;
            test::EchoResponse echo_res;
            brpc::Controller echo_cntl;
            std::string request_str(1500, 'a');
            echo_req.set_message(request_str);
            echo_cntl.set_log_id(++log_id);
            stub.Echo(&echo_cntl, &echo_req, &echo_res, NULL);
            EXPECT_FALSE(echo_cntl.Failed());

            // Wait for level db to commit span information
            usleep(500000);
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            cntl.http_request().uri()
                    .SetQuery(brpc::MIN_REQUEST_SIZE_STR, "1024");
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            // CheckTraceId(cntl, echo_res.message());
        }

        {
            // Find by log id
            test::EchoRequest echo_req;
            test::EchoResponse echo_res;
            brpc::Controller echo_cntl;
            echo_req.set_message("hello");
            echo_cntl.set_log_id(++log_id);
            stub.Echo(&echo_cntl, &echo_req, &echo_res, NULL);
            EXPECT_FALSE(echo_cntl.Failed());

            // Wait for level db to commit span information
            usleep(500000);
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            snprintf(querystr_buf, sizeof(querystr_buf), "%" PRId64, log_id);
            cntl.http_request().uri()
                    .SetQuery(brpc::LOG_ID_STR, querystr_buf);
            service.default_method(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(expect_type, cntl.http_response().content_type());
            // CheckTraceId(cntl, echo_res.message());
        }

        {
            ClosureChecker done;
            brpc::Controller cntl;
            SetUpController(&cntl, use_html);
            service.stats(&cntl, &req, &res, &done);
            EXPECT_FALSE(cntl.Failed());
            // CheckContent(cntl, "rpcz.id_db");
            // CheckContent(cntl, "rpcz.time_db");
        }
        
        StopAndJoin();
    }
    
private:
    brpc::Server _server;
};

TEST_F(BuiltinServiceTest, index) {
    TestIndex(false);
    TestIndex(true);
}

TEST_F(BuiltinServiceTest, version) {
    const std::string VERSION = "test_version";
    brpc::VersionService service(&_server);
    brpc::VersionRequest req;
    brpc::VersionResponse res;
    brpc::Controller cntl;
    ClosureChecker done;
    _server.set_version(VERSION);
    service.default_method(&cntl, &req, &res, &done);
    EXPECT_FALSE(cntl.Failed());
    EXPECT_EQ(VERSION, cntl.response_attachment().to_string());
}

TEST_F(BuiltinServiceTest, health) {
    const std::string HEALTH_STR = "OK";
    brpc::HealthService service;
    brpc::HealthRequest req;
    brpc::HealthResponse res;
    brpc::Controller cntl;
    SetUpController(&cntl, false);
    ClosureChecker done;
    service.default_method(&cntl, &req, &res, &done);
    EXPECT_FALSE(cntl.Failed());
    EXPECT_EQ(HEALTH_STR, cntl.response_attachment().to_string());
}

class MyHealthReporter : public brpc::HealthReporter {
public:
    void GenerateReport(brpc::Controller* cntl,
                        google::protobuf::Closure* done) {
        cntl->response_attachment().append("i'm ok");
        done->Run();
    }
};

TEST_F(BuiltinServiceTest, customized_health) {
    brpc::ServerOptions opt;
    MyHealthReporter hr;
    opt.health_reporter = &hr;
    ASSERT_EQ(0, _server.Start(9798, &opt));
    brpc::HealthRequest req;
    brpc::HealthResponse res;
    brpc::ChannelOptions copt;
    copt.protocol = brpc::PROTOCOL_HTTP;
    brpc::Channel chan;
    ASSERT_EQ(0, chan.Init("127.0.0.1:9798", &copt));
    brpc::Controller cntl;
    cntl.http_request().uri() = "/health";
    chan.CallMethod(NULL, &cntl, &req, &res, NULL);
    EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ("i'm ok", cntl.response_attachment());
}

TEST_F(BuiltinServiceTest, status) {
    TestStatus(false);
    TestStatus(true);
}

TEST_F(BuiltinServiceTest, list) {
    brpc::ListService service(&_server);
    brpc::ListRequest req;
    brpc::ListResponse res;
    brpc::Controller cntl;
    ClosureChecker done;
    ASSERT_EQ(0, _server.AddService(new EchoServiceImpl(),
                                    brpc::SERVER_OWNS_SERVICE));
    service.default_method(&cntl, &req, &res, &done);
    EXPECT_FALSE(cntl.Failed());
    ASSERT_EQ(1, res.service_size());
    EXPECT_EQ(test::EchoService::descriptor()->name(), res.service(0).name());
}

void* sleep_thread(void*) {
    sleep(1);
    return NULL;
}

TEST_F(BuiltinServiceTest, threads) {
    brpc::ThreadsService service;
    brpc::ThreadsRequest req;
    brpc::ThreadsResponse res;
    brpc::Controller cntl;
    ClosureChecker done;
    pthread_t tid;
    ASSERT_EQ(0, pthread_create(&tid, NULL, sleep_thread, NULL));
    service.default_method(&cntl, &req, &res, &done);
    EXPECT_FALSE(cntl.Failed());
    // Doesn't work under gcc 4.8.2
    // CheckContent(cntl, "sleep_thread");
    pthread_join(tid, NULL);
}

TEST_F(BuiltinServiceTest, vlog) {
    TestVLog(false);
    TestVLog(true);
}

TEST_F(BuiltinServiceTest, connections) {
    TestConnections(false);
    TestConnections(true);
}

TEST_F(BuiltinServiceTest, flags) {
    TestFlags(false);
    TestFlags(true);
}

TEST_F(BuiltinServiceTest, bad_method) {
    TestBadMethod(false);
    TestBadMethod(true);
}

TEST_F(BuiltinServiceTest, vars) {
    // Start server to show bvars inside 
    ASSERT_EQ(0, _server.Start("127.0.0.1:9798", NULL));
    brpc::VarsService service;
    brpc::VarsRequest req;
    brpc::VarsResponse res;
    {
        ClosureChecker done;
        brpc::Controller cntl;
        bvar::Adder<int64_t> myvar;
        myvar.expose("myvar");
        myvar << 9;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckFieldInContent(cntl, "myvar : ", 9);
    }
    {
        ClosureChecker done;
        brpc::Controller cntl;
        cntl.http_request()._unresolved_path = "iobuf*";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "iobuf_block_count");
    }
}

TEST_F(BuiltinServiceTest, rpcz) {
    for (int i = 0; i <= 1; ++i) {  // enable rpcz
        for (int j = 0; j <= 1; ++j) {  // hex log id
            for (int k = 0; k <= 1; ++k) {  // use html
                TestRpcz(i, j, k);
            }
        }
    }
}

TEST_F(BuiltinServiceTest, pprof) {
    brpc::PProfService service;
    {
        ClosureChecker done;
        brpc::Controller cntl;
        cntl.http_request().uri().SetQuery("seconds", "1");
        service.profile(&cntl, NULL, NULL, &done);
        // Just for loading symbols in gperftools/profiler.h
        ProfilerFlush();
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_GT(cntl.response_attachment().length(), 0ul);
    }
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.heap(&cntl, NULL, NULL, &done);
        const int rc = getenv("TCMALLOC_SAMPLE_PARAMETER") ? 0 : brpc::ENOMETHOD;
        EXPECT_EQ(rc, cntl.ErrorCode());
    }
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.growth(&cntl, NULL, NULL, &done);
        // linked tcmalloc in UT
        EXPECT_EQ(0, cntl.ErrorCode());
    }
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.symbol(&cntl, NULL, NULL, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "num_symbols");
    }
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.cmdline(&cntl, NULL, NULL, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "brpc_builtin_service_unittest");
    }
}

TEST_F(BuiltinServiceTest, dir) {
    brpc::DirService service;
    brpc::DirRequest req;
    brpc::DirResponse res;
    {
        // Open root path
        ClosureChecker done;
        brpc::Controller cntl;
        SetUpController(&cntl, true);
        cntl.http_request()._unresolved_path = "";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "tmp");
    }
    {
        // Open a specific file
        ClosureChecker done;
        brpc::Controller cntl;
        SetUpController(&cntl, false);
        cntl.http_request()._unresolved_path = "/usr/include/errno.h";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
#if defined(OS_LINUX)
        CheckContent(cntl, "ERRNO_H");
#elif defined(OS_MACOSX)
        CheckContent(cntl, "sys/errno.h");
#endif
    }
    {
        // Open a file that doesn't exist
        ClosureChecker done;
        brpc::Controller cntl;
        SetUpController(&cntl, false);
        cntl.http_request()._unresolved_path = "file_not_exist";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_TRUE(cntl.Failed());
        CheckErrorText(cntl, "Cannot open");
    }
}
    
TEST_F(BuiltinServiceTest, ids) {
    brpc::IdsService service;
    brpc::IdsRequest req;
    brpc::IdsResponse res;
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "Use /ids/<call_id>");
    }    
    {
        ClosureChecker done;
        brpc::Controller cntl;
        cntl.http_request()._unresolved_path = "not_valid";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_TRUE(cntl.Failed());
        CheckErrorText(cntl, "is not a bthread_id");
    }    
    {
        bthread_id_t id;
        EXPECT_EQ(0, bthread_id_create(&id, NULL, NULL));
        ClosureChecker done;
        brpc::Controller cntl;
        std::string id_string;
        butil::string_printf(&id_string, "%llu", (unsigned long long)id.value);
        cntl.http_request()._unresolved_path = id_string;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "Status: UNLOCKED");
    }    
}

void* dummy_bthread(void*) {
    bthread_usleep(1000000);
    return NULL;
}

TEST_F(BuiltinServiceTest, bthreads) {
    brpc::BthreadsService service;
    brpc::BthreadsRequest req;
    brpc::BthreadsResponse res;
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "Use /bthreads/<bthread_id>");
    }    
    {
        ClosureChecker done;
        brpc::Controller cntl;
        cntl.http_request()._unresolved_path = "not_valid";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_TRUE(cntl.Failed());
        CheckErrorText(cntl, "is not a bthread id");
    }    
    {
        bthread_t th;
        EXPECT_EQ(0, bthread_start_background(&th, NULL, dummy_bthread, NULL));
        ClosureChecker done;
        brpc::Controller cntl;
        std::string id_string;
        butil::string_printf(&id_string, "%llu", (unsigned long long)th);
        cntl.http_request()._unresolved_path = id_string;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "stop=0");
    }    
}

TEST_F(BuiltinServiceTest, sockets) {
    brpc::SocketsService service;
    brpc::SocketsRequest req;
    brpc::SocketsResponse res;
    {
        ClosureChecker done;
        brpc::Controller cntl;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "Use /sockets/<SocketId>");
    }    
    {
        ClosureChecker done;
        brpc::Controller cntl;
        cntl.http_request()._unresolved_path = "not_valid";
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_TRUE(cntl.Failed());
        CheckErrorText(cntl, "is not a SocketId");
    }    
    {
        brpc::SocketId id;
        brpc::SocketOptions options;
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        ClosureChecker done;
        brpc::Controller cntl;
        std::string id_string;
        butil::string_printf(&id_string, "%llu", (unsigned long long)id);
        cntl.http_request()._unresolved_path = id_string;
        service.default_method(&cntl, &req, &res, &done);
        EXPECT_FALSE(cntl.Failed());
        CheckContent(cntl, "fd=-1");
    }    
}
