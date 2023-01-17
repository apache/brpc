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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/text_format.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/files/scoped_file.h"
#include "butil/fd_guard.h"
#include "butil/file_util.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "echo.pb.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/policy/http2_rpc_protocol.h"
#include "json2pb/pb_to_json.h"
#include "json2pb/json_to_pb.h"
#include "brpc/details/method_status.h"
#include "brpc/rpc_dump.h"
#include "bvar/collector.h"

namespace brpc {
DECLARE_bool(rpc_dump);
DECLARE_string(rpc_dump_dir);
DECLARE_int32(rpc_dump_max_requests_in_one_file);
extern bvar::CollectorSpeedLimit g_rpc_dump_sl;
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (GFLAGS_NS::SetCommandLineOption("socket_max_unwritten_bytes", "2000000").empty()) {
        std::cerr << "Fail to set -socket_max_unwritten_bytes" << std::endl;
        return -1;
    }
    if (GFLAGS_NS::SetCommandLineOption("crash_on_fatal_log", "true").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    return RUN_ALL_TESTS();
}

namespace {

static const std::string EXP_REQUEST = "hello";
static const std::string EXP_RESPONSE = "world";

static const std::string MOCK_CREDENTIAL = "mock credential";
static const std::string MOCK_USER = "mock user";

class MyAuthenticator : public brpc::Authenticator {
public:
    MyAuthenticator() {}

    int GenerateCredential(std::string* auth_str) const {
        *auth_str = MOCK_CREDENTIAL;
        return 0;
    }

    int VerifyCredential(const std::string& auth_str,
                         const butil::EndPoint&,
                         brpc::AuthContext* ctx) const {
        EXPECT_EQ(MOCK_CREDENTIAL, auth_str);
        ctx->set_user(MOCK_USER);
        return 0;
    }
};

class MyEchoService : public ::test::EchoService {
public:
    void Echo(::google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        const std::string* sleep_ms_str =
            cntl->http_request().uri().GetQuery("sleep_ms");
        if (sleep_ms_str) {
            bthread_usleep(strtol(sleep_ms_str->data(), NULL, 10) * 1000);
        }
        res->set_message(EXP_RESPONSE);
    }
};

class HttpTest : public ::testing::Test{
protected:
    HttpTest() {
        EXPECT_EQ(0, _server.AddBuiltinServices());
        EXPECT_EQ(0, _server.AddService(
            &_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        // Hack: Regard `_server' as running 
        _server._status = brpc::Server::RUNNING;
        _server._options.auth = &_auth;
        
        EXPECT_EQ(0, pipe(_pipe_fds));

        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));

        brpc::SocketOptions h2_client_options;
        h2_client_options.user = brpc::get_client_side_messenger();
        h2_client_options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(h2_client_options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_h2_client_sock));
    };

    virtual ~HttpTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    void VerifyMessage(brpc::InputMessageBase* msg, bool expect) {
        if (msg->_socket == NULL) {
            _socket->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        EXPECT_EQ(expect, brpc::policy::VerifyHttpRequest(msg));
    }

    void ProcessMessage(void (*process)(brpc::InputMessageBase*),
                        brpc::InputMessageBase* msg, bool set_eof) {
        if (msg->_socket == NULL) {
            _socket->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        _socket->PostponeEOF();
        if (set_eof) {
            _socket->SetEOF();
        }
        (*process)(msg);
    }

    brpc::policy::HttpContext* MakePostRequestMessage(const std::string& path) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext(false);
        msg->header().uri().set_path(path);
        msg->header().set_content_type("application/json");
        msg->header().set_method(brpc::HTTP_METHOD_POST);

        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        butil::IOBufAsZeroCopyOutputStream req_stream(&msg->body());
        EXPECT_TRUE(json2pb::ProtoMessageToJson(req, &req_stream, NULL));
        return msg;
    }

    brpc::policy::HttpContext* MakePostProtoTextRequestMessage(
        const std::string& path) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext(false);
        msg->header().uri().set_path(path);
        msg->header().set_content_type("application/proto-text");
        msg->header().set_method(brpc::HTTP_METHOD_POST);

        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        butil::IOBufAsZeroCopyOutputStream req_stream(&msg->body());
        EXPECT_TRUE(google::protobuf::TextFormat::Print(req, &req_stream));
        return msg;
    }

    brpc::policy::HttpContext* MakeGetRequestMessage(const std::string& path) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext(false);
        msg->header().uri().set_path(path);
        msg->header().set_method(brpc::HTTP_METHOD_GET);
        return msg;
    }


    brpc::policy::HttpContext* MakeResponseMessage(int code) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext(false);
        msg->header().set_status_code(code);
        msg->header().set_content_type("application/json");
        
        test::EchoResponse res;
        res.set_message(EXP_RESPONSE);
        butil::IOBufAsZeroCopyOutputStream res_stream(&msg->body());
        EXPECT_TRUE(json2pb::ProtoMessageToJson(res, &res_stream, NULL));
        return msg;
    }

    void CheckResponseCode(bool expect_empty, int expect_code) {
        int bytes_in_pipe = 0;
        ioctl(_pipe_fds[0], FIONREAD, &bytes_in_pipe);
        if (expect_empty) {
            EXPECT_EQ(0, bytes_in_pipe);
            return;
        }

        EXPECT_GT(bytes_in_pipe, 0);
        butil::IOPortal buf;
        EXPECT_EQ((ssize_t)bytes_in_pipe,
                  buf.append_from_file_descriptor(_pipe_fds[0], 1024));
        brpc::ParseResult pr =
                brpc::policy::ParseHttpMessage(&buf, _socket.get(), false, NULL);
        EXPECT_EQ(brpc::PARSE_OK, pr.error());
        brpc::policy::HttpContext* msg =
            static_cast<brpc::policy::HttpContext*>(pr.message());

        EXPECT_EQ(expect_code, msg->header().status_code());
        msg->Destroy();
    }

    void MakeH2EchoRequestBuf(butil::IOBuf* out, brpc::Controller* cntl, int* h2_stream_id) {
        butil::IOBuf request_buf;
        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        cntl->http_request().set_method(brpc::HTTP_METHOD_POST);
        brpc::policy::SerializeHttpRequest(&request_buf, cntl, &req);
        ASSERT_FALSE(cntl->Failed());
        brpc::policy::H2UnsentRequest* h2_req = brpc::policy::H2UnsentRequest::New(cntl);
        cntl->_current_call.stream_user_data = h2_req;
        brpc::SocketMessage* socket_message = NULL;
        brpc::policy::PackH2Request(NULL, &socket_message, cntl->call_id().value,
                                    NULL, cntl, request_buf, NULL);
        butil::Status st = socket_message->AppendAndDestroySelf(out, _h2_client_sock.get());
        ASSERT_TRUE(st.ok());
        *h2_stream_id = h2_req->_stream_id;
    }

    void MakeH2EchoResponseBuf(butil::IOBuf* out, int h2_stream_id) {
        brpc::Controller cntl;
        test::EchoResponse res;
        res.set_message(EXP_RESPONSE);
        cntl.http_request().set_content_type("application/proto");
        {
            butil::IOBufAsZeroCopyOutputStream wrapper(&cntl.response_attachment());
            EXPECT_TRUE(res.SerializeToZeroCopyStream(&wrapper));
        }
        brpc::policy::H2UnsentResponse* h2_res = brpc::policy::H2UnsentResponse::New(&cntl, h2_stream_id, false /*is grpc*/);
        butil::Status st = h2_res->AppendAndDestroySelf(out, _h2_client_sock.get());
        ASSERT_TRUE(st.ok());
    }

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
    brpc::SocketUniquePtr _h2_client_sock;
    brpc::Server _server;

    MyEchoService _svc;
    MyAuthenticator _auth;
};

TEST_F(HttpTest, indenting_ostream) {
    std::ostringstream os1;
    brpc::IndentingOStream is1(os1, 2);
    brpc::IndentingOStream is2(is1, 2);
    os1 << "begin1\nhello" << std::endl << "world\nend1" << std::endl;
    is1 << "begin2\nhello" << std::endl << "world\nend2" << std::endl;
    is2 << "begin3\nhello" << std::endl << "world\nend3" << std::endl;
    ASSERT_EQ(
    "begin1\nhello\nworld\nend1\nbegin2\n  hello\n  world\n  end2\n"
    "  begin3\n    hello\n    world\n    end3\n",
    os1.str());
}

TEST_F(HttpTest, parse_http_address) {
    const std::string EXP_HOSTNAME = "www.baidu.com:9876";
    butil::EndPoint EXP_ENDPOINT;
    {
        std::string url = "https://" + EXP_HOSTNAME;
        EXPECT_TRUE(brpc::policy::ParseHttpServerAddress(&EXP_ENDPOINT, url.c_str()));
    }
    {
        butil::EndPoint ep;
        std::string url = "http://" +
                          std::string(endpoint2str(EXP_ENDPOINT).c_str());
        EXPECT_TRUE(brpc::policy::ParseHttpServerAddress(&ep, url.c_str()));
        EXPECT_EQ(EXP_ENDPOINT, ep);
    }
    {
        butil::EndPoint ep;
        std::string url = "https://" +
            std::string(butil::ip2str(EXP_ENDPOINT.ip).c_str());
        EXPECT_TRUE(brpc::policy::ParseHttpServerAddress(&ep, url.c_str()));
        EXPECT_EQ(EXP_ENDPOINT.ip, ep.ip);
        EXPECT_EQ(443, ep.port);
    }
    {
        butil::EndPoint ep;
        EXPECT_FALSE(brpc::policy::ParseHttpServerAddress(&ep, "invalid_url"));
    }
    {
        butil::EndPoint ep;
        EXPECT_FALSE(brpc::policy::ParseHttpServerAddress(
            &ep, "https://no.such.machine:9090"));
    }
}

TEST_F(HttpTest, verify_request) {
    {
        brpc::policy::HttpContext* msg =
                MakePostRequestMessage("/EchoService/Echo");
        VerifyMessage(msg, false);
        msg->Destroy();
    }
    {
        brpc::policy::HttpContext* msg = MakeGetRequestMessage("/status");
        VerifyMessage(msg, true);
        msg->Destroy();
    }
    {
        brpc::policy::HttpContext* msg =
                MakePostRequestMessage("/EchoService/Echo");
        _socket->SetFailed();
        VerifyMessage(msg, false);
        msg->Destroy();
    }
    {
        brpc::policy::HttpContext* msg =
                MakePostProtoTextRequestMessage("/EchoService/Echo");
        VerifyMessage(msg, false);
        msg->Destroy();
    }
}

TEST_F(HttpTest, process_request_failed_socket) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/EchoService/Echo");
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
    CheckResponseCode(true, 0);
}

TEST_F(HttpTest, reject_get_to_pb_services_with_required_fields) {
    brpc::policy::HttpContext* msg = MakeGetRequestMessage("/EchoService/Echo");
    _server._status = brpc::Server::RUNNING;
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
    const brpc::Server::MethodProperty* mp =
        _server.FindMethodPropertyByFullName("test.EchoService.Echo");
    ASSERT_TRUE(mp);
    ASSERT_TRUE(mp->status);
    ASSERT_EQ(1ll, mp->status->_nerror_bvar.get_value());
    CheckResponseCode(false, brpc::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(HttpTest, process_request_logoff) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/EchoService/Echo");
    _server._status = brpc::Server::READY;
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    CheckResponseCode(false, brpc::HTTP_STATUS_SERVICE_UNAVAILABLE);
}

TEST_F(HttpTest, process_request_wrong_method) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/NO_SUCH_METHOD");
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    CheckResponseCode(false, brpc::HTTP_STATUS_NOT_FOUND);
}

TEST_F(HttpTest, process_response_after_eof) {
    test::EchoResponse res;
    brpc::Controller cntl;
    cntl._response = &res;
    brpc::policy::HttpContext* msg =
            MakeResponseMessage(brpc::HTTP_STATUS_OK);
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(brpc::policy::ProcessHttpResponse, msg, true);
    ASSERT_EQ(EXP_RESPONSE, res.message());
    ASSERT_TRUE(_socket->Failed());
}

TEST_F(HttpTest, process_response_error_code) {
    {
        brpc::Controller cntl;
        _socket->set_correlation_id(cntl.call_id().value);
        brpc::policy::HttpContext* msg =
                MakeResponseMessage(brpc::HTTP_STATUS_CONTINUE);
        ProcessMessage(brpc::policy::ProcessHttpResponse, msg, false);
        ASSERT_EQ(brpc::EHTTP, cntl.ErrorCode());
        ASSERT_EQ(brpc::HTTP_STATUS_CONTINUE, cntl.http_response().status_code());
    }
    {
        brpc::Controller cntl;
        _socket->set_correlation_id(cntl.call_id().value);
        brpc::policy::HttpContext* msg =
                MakeResponseMessage(brpc::HTTP_STATUS_TEMPORARY_REDIRECT);
        ProcessMessage(brpc::policy::ProcessHttpResponse, msg, false);
        ASSERT_EQ(brpc::EHTTP, cntl.ErrorCode());
        ASSERT_EQ(brpc::HTTP_STATUS_TEMPORARY_REDIRECT,
                  cntl.http_response().status_code());
    }
    {
        brpc::Controller cntl;
        _socket->set_correlation_id(cntl.call_id().value);
        brpc::policy::HttpContext* msg =
                MakeResponseMessage(brpc::HTTP_STATUS_BAD_REQUEST);
        ProcessMessage(brpc::policy::ProcessHttpResponse, msg, false);
        ASSERT_EQ(brpc::EHTTP, cntl.ErrorCode());
        ASSERT_EQ(brpc::HTTP_STATUS_BAD_REQUEST,
                  cntl.http_response().status_code());
    }
    {
        brpc::Controller cntl;
        _socket->set_correlation_id(cntl.call_id().value);
        brpc::policy::HttpContext* msg = MakeResponseMessage(12345);
        ProcessMessage(brpc::policy::ProcessHttpResponse, msg, false);
        ASSERT_EQ(brpc::EHTTP, cntl.ErrorCode());
        ASSERT_EQ(12345, cntl.http_response().status_code());
    }
}

TEST_F(HttpTest, complete_flow) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    cntl._response = &res;
    cntl._connection_type = brpc::CONNECTION_TYPE_SHORT;
    cntl._method = test::EchoService::descriptor()->method(0);
    ASSERT_EQ(0, brpc::Socket::Address(_socket->id(), &cntl._current_call.sending_sock));

    // Send request
    req.set_message(EXP_REQUEST);
    brpc::policy::SerializeHttpRequest(&request_buf, &cntl, &req);
    ASSERT_FALSE(cntl.Failed());
    brpc::policy::PackHttpRequest(
        &total_buf, NULL, cntl.call_id().value,
        cntl._method, &cntl, request_buf, &_auth);
    ASSERT_FALSE(cntl.Failed());

    // Verify and handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseHttpMessage(&total_buf, _socket.get(), false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    VerifyMessage(req_msg, true);
    ProcessMessage(brpc::policy::ProcessHttpRequest, req_msg, false);

    // Read response from pipe
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    brpc::ParseResult res_pr =
            brpc::policy::ParseHttpMessage(&response_buf, _socket.get(), false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, res_pr.error());
    brpc::InputMessageBase* res_msg = res_pr.message();
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_msg, false);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(EXP_RESPONSE, res.message());
}

TEST_F(HttpTest, chunked_uploading) {
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));

    // Send request via curl using chunked encoding
    const std::string req = "{\"message\":\"hello\"}";
    const std::string res_fname = "curl.out";
    std::string cmd;
    butil::string_printf(&cmd, "curl -X POST -d '%s' -H 'Transfer-Encoding:chunked' "
                        "-H 'Content-Type:application/json' -o %s "
                        "http://localhost:%d/EchoService/Echo",
                        req.c_str(), res_fname.c_str(), port);
    ASSERT_EQ(0, system(cmd.c_str()));

    // Check response
    const std::string exp_res = "{\"message\":\"world\"}";
    butil::ScopedFILE fp(res_fname.c_str(), "r");
    char buf[128];
    ASSERT_TRUE(fgets(buf, sizeof(buf), fp));
    EXPECT_EQ(exp_res, std::string(buf));
}

enum DonePlace {
    DONE_BEFORE_CREATE_PA = 0,
    DONE_AFTER_CREATE_PA_BEFORE_DESTROY_PA,
    DONE_AFTER_DESTROY_PA,
};
// For writing into PA.
const char PA_DATA[] = "abcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*()_=-+";
const size_t PA_DATA_LEN = sizeof(PA_DATA) - 1/*not count the ending zero*/;

static void CopyPAPrefixedWithSeqNo(char* buf, uint64_t seq_no) {
    memcpy(buf, PA_DATA, PA_DATA_LEN);
    *(uint64_t*)buf = seq_no;
}

class DownloadServiceImpl : public ::test::DownloadService {
public:
    DownloadServiceImpl(DonePlace done_place = DONE_BEFORE_CREATE_PA,
                        size_t num_repeat = 1)
        : _done_place(done_place)
        , _nrep(num_repeat)
        , _nwritten(0)
        , _ever_full(false)
        , _last_errno(0) {}
    
    void Download(::google::protobuf::RpcController* cntl_base,
                  const ::test::HttpRequest*,
                  ::test::HttpResponse*,
                  ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        cntl->http_response().set_content_type("text/plain");
        brpc::StopStyle stop_style = (_nrep == std::numeric_limits<size_t>::max() 
                ? brpc::FORCE_STOP : brpc::WAIT_FOR_STOP);
        butil::intrusive_ptr<brpc::ProgressiveAttachment> pa
            = cntl->CreateProgressiveAttachment(stop_style);
        if (pa == NULL) {
            cntl->SetFailed("The socket was just failed");
            return;
        }
        if (_done_place == DONE_BEFORE_CREATE_PA) {
            done_guard.reset(NULL);
        }
        ASSERT_GT(PA_DATA_LEN, 8u);  // long enough to hold a 64-bit decimal.
        char buf[PA_DATA_LEN];
        for (size_t c = 0; c < _nrep;) {
            CopyPAPrefixedWithSeqNo(buf, c);
            if (pa->Write(buf, sizeof(buf)) != 0) {
                if (errno == brpc::EOVERCROWDED) {
                    LOG_EVERY_SECOND(INFO) << "full pa=" << pa.get();
                    _ever_full = true;
                    bthread_usleep(10000);
                    continue;
                } else {
                    _last_errno = errno;
                    break;
                }
            } else {
                _nwritten += PA_DATA_LEN;
            }
            ++c;
        }
        if (_done_place == DONE_AFTER_CREATE_PA_BEFORE_DESTROY_PA) {
            done_guard.reset(NULL);
        }
        LOG(INFO) << "Destroy pa="  << pa.get();
        pa.reset(NULL);
        if (_done_place == DONE_AFTER_DESTROY_PA) {
            done_guard.reset(NULL);
        }
    }

    void DownloadFailed(::google::protobuf::RpcController* cntl_base,
                        const ::test::HttpRequest*,
                        ::test::HttpResponse*,
                        ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        cntl->http_response().set_content_type("text/plain");
        brpc::StopStyle stop_style = (_nrep == std::numeric_limits<size_t>::max() 
                ? brpc::FORCE_STOP : brpc::WAIT_FOR_STOP);
        butil::intrusive_ptr<brpc::ProgressiveAttachment> pa
            = cntl->CreateProgressiveAttachment(stop_style);
        if (pa == NULL) {
            cntl->SetFailed("The socket was just failed");
            return;
        }
        char buf[PA_DATA_LEN];
        while (true) {
            if (pa->Write(buf, sizeof(buf)) != 0) {
                if (errno == brpc::EOVERCROWDED) {
                    LOG_EVERY_SECOND(INFO) << "full pa=" << pa.get();
                    bthread_usleep(10000);
                    continue;
                } else {
                    _last_errno = errno;
                    break;
                }
            }
            break;
        }
        // The remote client will not receive the data written to the
        // progressive attachment when the controller failed.
        cntl->SetFailed("Intentionally set controller failed");
        done_guard.reset(NULL);
        
        // Return value of Write after controller has failed should
        // be less than zero.
        CHECK_LT(pa->Write(buf, sizeof(buf)), 0);
        CHECK_EQ(errno, ECANCELED);
    }
    
    void set_done_place(DonePlace done_place) { _done_place = done_place; }
    size_t written_bytes() const { return _nwritten; }
    bool ever_full() const { return _ever_full; }
    int last_errno() const { return _last_errno; }
    
private:
    DonePlace _done_place;
    size_t _nrep;
    size_t _nwritten;
    bool _ever_full;
    int _last_errno;
};
    
TEST_F(HttpTest, read_chunked_response_normally) {
    const int port = 8923;
    brpc::Server server;
    DownloadServiceImpl svc;
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));

    for (int i = 0; i < 3; ++i) {
        svc.set_done_place((DonePlace)i);
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HTTP;
        ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
        brpc::Controller cntl;
        cntl.http_request().uri() = "/DownloadService/Download";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();

        std::string expected(PA_DATA_LEN, 0);
        CopyPAPrefixedWithSeqNo(&expected[0], 0);
        ASSERT_EQ(expected, cntl.response_attachment());
    }
}

TEST_F(HttpTest, read_failed_chunked_response) {
    const int port = 8923;
    brpc::Server server;
    DownloadServiceImpl svc;
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    brpc::Controller cntl;
    cntl.http_request().uri() = "/DownloadService/DownloadFailed";
    cntl.response_will_be_read_progressively();
    channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
    EXPECT_TRUE(cntl.response_attachment().empty());
    ASSERT_TRUE(cntl.Failed());
    ASSERT_NE(cntl.ErrorText().find("HTTP/1.1 500 Internal Server Error"),
              std::string::npos) << cntl.ErrorText();
    ASSERT_NE(cntl.ErrorText().find("Intentionally set controller failed"),
              std::string::npos) << cntl.ErrorText();
    ASSERT_EQ(0, svc.last_errno());
}

class ReadBody : public brpc::ProgressiveReader,
                 public brpc::SharedObject {
public:
    ReadBody()
        : _nread(0)
        , _ncount(0)
        , _destroyed(false) {
        butil::intrusive_ptr<ReadBody>(this).detach(); // ref
    }
                
    butil::Status OnReadOnePart(const void* data, size_t length) {
        _nread += length;
        while (length > 0) {
            size_t nappend = std::min(_buf.size() + length, PA_DATA_LEN) - _buf.size();
            _buf.append((const char*)data, nappend);
            data = (const char*)data + nappend;
            length -= nappend;
            if (_buf.size() >= PA_DATA_LEN) {
                EXPECT_EQ(PA_DATA_LEN, _buf.size());
                char expected[PA_DATA_LEN];
                CopyPAPrefixedWithSeqNo(expected, _ncount++);
                EXPECT_EQ(0, memcmp(expected, _buf.data(), PA_DATA_LEN))
                    << "ncount=" << _ncount;
                _buf.clear();
            }
        }
        return butil::Status::OK();
    }
    void OnEndOfMessage(const butil::Status& st) {
        butil::intrusive_ptr<ReadBody>(this, false); // deref
        ASSERT_LT(_buf.size(), PA_DATA_LEN);
        ASSERT_EQ(0, memcmp(_buf.data(), PA_DATA, _buf.size()));
        _destroyed = true;
        _destroying_st = st;
        LOG(INFO) << "Destroy ReadBody=" << this << ", " << st;
    }
    bool destroyed() const { return _destroyed; }
    const butil::Status& destroying_status() const { return _destroying_st; }
    size_t read_bytes() const { return _nread; }
private:
    std::string _buf;
    size_t _nread;
    size_t _ncount;
    bool _destroyed;
    butil::Status _destroying_st;
};

static const int GENERAL_DELAY_US = 300000; // 0.3s

TEST_F(HttpTest, read_long_body_progressively) {
    butil::intrusive_ptr<ReadBody> reader;
    {
        const int port = 8923;
        brpc::Server server;
        DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                                std::numeric_limits<size_t>::max());
        EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, server.Start(port, NULL));
        {
            brpc::Channel channel;
            brpc::ChannelOptions options;
            options.protocol = brpc::PROTOCOL_HTTP;
            ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
            {
                brpc::Controller cntl;
                cntl.response_will_be_read_progressively();
                cntl.http_request().uri() = "/DownloadService/Download";
                channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                ASSERT_TRUE(cntl.response_attachment().empty());
                reader.reset(new ReadBody);
                cntl.ReadProgressiveAttachmentBy(reader.get());
                size_t last_read = 0;
                for (size_t i = 0; i < 3; ++i) {
                    sleep(1);
                    size_t current_read = reader->read_bytes();
                    LOG(INFO) << "read=" << current_read - last_read
                              << " total=" << current_read;
                    last_read = current_read;
                }
                // Read something in past N seconds.
                ASSERT_GT(last_read, (size_t)100000);
            }
            // the socket still holds a ref.
            ASSERT_FALSE(reader->destroyed());
        }
        // Wait for recycling of the main socket.
        usleep(GENERAL_DELAY_US);
        // even if the main socket is recycled, the pooled socket for
        // receiving data is not affected.
        ASSERT_FALSE(reader->destroyed());
    }
    // Wait for close of the connection due to server's stopping.
    usleep(GENERAL_DELAY_US);
    ASSERT_TRUE(reader->destroyed());
    ASSERT_EQ(ECONNRESET, reader->destroying_status().error_code());
}

TEST_F(HttpTest, read_short_body_progressively) {
    butil::intrusive_ptr<ReadBody> reader;
    const int port = 8923;
    brpc::Server server;
    const int NREP = 10000;
    DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA, NREP);
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HTTP;
        ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
        {
            brpc::Controller cntl;
            cntl.response_will_be_read_progressively();
            cntl.http_request().uri() = "/DownloadService/Download";
            channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_TRUE(cntl.response_attachment().empty());
            reader.reset(new ReadBody);
            cntl.ReadProgressiveAttachmentBy(reader.get());
            size_t last_read = 0;
            for (size_t i = 0; i < 3; ++i) {
                sleep(1);
                size_t current_read = reader->read_bytes();
                LOG(INFO) << "read=" << current_read - last_read
                          << " total=" << current_read;
                last_read = current_read;
            }
            ASSERT_EQ(NREP * PA_DATA_LEN, svc.written_bytes());
            ASSERT_EQ(NREP * PA_DATA_LEN, last_read);
        }
        ASSERT_TRUE(reader->destroyed());
        ASSERT_EQ(0, reader->destroying_status().error_code());
    }
}

TEST_F(HttpTest, read_progressively_after_cntl_destroys) {
    butil::intrusive_ptr<ReadBody> reader;
    {
        const int port = 8923;
        brpc::Server server;
        DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                                std::numeric_limits<size_t>::max());
        EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, server.Start(port, NULL));
        {
            brpc::Channel channel;
            brpc::ChannelOptions options;
            options.protocol = brpc::PROTOCOL_HTTP;
            ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
            {
                brpc::Controller cntl;
                cntl.response_will_be_read_progressively();
                cntl.http_request().uri() = "/DownloadService/Download";
                channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                ASSERT_TRUE(cntl.response_attachment().empty());
                reader.reset(new ReadBody);
                cntl.ReadProgressiveAttachmentBy(reader.get());
            }
            size_t last_read = 0;
            for (size_t i = 0; i < 3; ++i) {
                sleep(1);
                size_t current_read = reader->read_bytes();
                LOG(INFO) << "read=" << current_read - last_read
                          << " total=" << current_read;
                last_read = current_read;
            }
            // Read something in past N seconds.
            ASSERT_GT(last_read, (size_t)100000);
            ASSERT_FALSE(reader->destroyed());
        }
        // Wait for recycling of the main socket.
        usleep(GENERAL_DELAY_US);
        ASSERT_FALSE(reader->destroyed());
    }
    // Wait for close of the connection due to server's stopping.
    usleep(GENERAL_DELAY_US);
    ASSERT_TRUE(reader->destroyed());
    ASSERT_EQ(ECONNRESET, reader->destroying_status().error_code());
}

TEST_F(HttpTest, read_progressively_after_long_delay) {
    butil::intrusive_ptr<ReadBody> reader;
    {
        const int port = 8923;
        brpc::Server server;
        DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                                std::numeric_limits<size_t>::max());
        EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, server.Start(port, NULL));
        {
            brpc::Channel channel;
            brpc::ChannelOptions options;
            options.protocol = brpc::PROTOCOL_HTTP;
            ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
            {
                brpc::Controller cntl;
                cntl.response_will_be_read_progressively();
                cntl.http_request().uri() = "/DownloadService/Download";
                channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                ASSERT_TRUE(cntl.response_attachment().empty());
                LOG(INFO) << "Sleep 3 seconds to make PA at server-side full";
                sleep(3);
                EXPECT_TRUE(svc.ever_full());
                ASSERT_EQ(0, svc.last_errno());
                reader.reset(new ReadBody);
                cntl.ReadProgressiveAttachmentBy(reader.get());
                size_t last_read = 0;
                for (size_t i = 0; i < 3; ++i) {
                    sleep(1);
                    size_t current_read = reader->read_bytes();
                    LOG(INFO) << "read=" << current_read - last_read
                              << " total=" << current_read;
                    last_read = current_read;
                }
                // Read something in past N seconds.
                ASSERT_GT(last_read, (size_t)100000);
            }
            ASSERT_FALSE(reader->destroyed());
        }
        // Wait for recycling of the main socket.
        usleep(GENERAL_DELAY_US);
        ASSERT_FALSE(reader->destroyed());
    }
    // Wait for close of the connection due to server's stopping.
    usleep(GENERAL_DELAY_US);
    ASSERT_TRUE(reader->destroyed());
    ASSERT_EQ(ECONNRESET, reader->destroying_status().error_code());
}

TEST_F(HttpTest, skip_progressive_reading) {
    const int port = 8923;
    brpc::Server server;
    DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                            std::numeric_limits<size_t>::max());
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
    {
        brpc::Controller cntl;
        cntl.response_will_be_read_progressively();
        cntl.http_request().uri() = "/DownloadService/Download";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(cntl.response_attachment().empty());
    }
    const size_t old_written_bytes = svc.written_bytes();
    LOG(INFO) << "Sleep 3 seconds after destroy of Controller";
    sleep(3);
    const size_t new_written_bytes = svc.written_bytes();
    ASSERT_EQ(0, svc.last_errno());
    LOG(INFO) << "Server still wrote " << new_written_bytes - old_written_bytes;
    // The server side still wrote things.
    ASSERT_GT(new_written_bytes - old_written_bytes, (size_t)100000);
}

class AlwaysFailRead : public brpc::ProgressiveReader {
public:
    // @ProgressiveReader
    butil::Status OnReadOnePart(const void* /*data*/, size_t /*length*/) {
        return butil::Status(-1, "intended fail at %s:%d", __FILE__, __LINE__);
    }
    void OnEndOfMessage(const butil::Status& st) {
        LOG(INFO) << "Destroy " << this << ": " << st;
        delete this;
    }
};

TEST_F(HttpTest, failed_on_read_one_part) {
    const int port = 8923;
    brpc::Server server;
    DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                            std::numeric_limits<size_t>::max());
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
    {
        brpc::Controller cntl;
        cntl.response_will_be_read_progressively();
        cntl.http_request().uri() = "/DownloadService/Download";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(cntl.response_attachment().empty());
        cntl.ReadProgressiveAttachmentBy(new AlwaysFailRead);
    }
    LOG(INFO) << "Sleep 1 second";
    sleep(1);
    ASSERT_NE(0, svc.last_errno());
}

TEST_F(HttpTest, broken_socket_stops_progressive_reading) {
    butil::intrusive_ptr<ReadBody> reader;
    const int port = 8923;
    brpc::Server server;
    DownloadServiceImpl svc(DONE_BEFORE_CREATE_PA,
                            std::numeric_limits<size_t>::max());
    EXPECT_EQ(0, server.AddService(&svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));
        
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));
    {
        brpc::Controller cntl;
        cntl.response_will_be_read_progressively();
        cntl.http_request().uri() = "/DownloadService/Download";
        channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(cntl.response_attachment().empty());
        reader.reset(new ReadBody);
        cntl.ReadProgressiveAttachmentBy(reader.get());
        size_t last_read = 0;
        for (size_t i = 0; i < 3; ++i) {
            sleep(1);
            size_t current_read = reader->read_bytes();
            LOG(INFO) << "read=" << current_read - last_read
                      << " total=" << current_read;
            last_read = current_read;
        }
        // Read something in past N seconds.
        ASSERT_GT(last_read, (size_t)100000);
    }
    // the socket still holds a ref.
    ASSERT_FALSE(reader->destroyed());
    LOG(INFO) << "Stopping the server";
    server.Stop(0);
    server.Join();
        
    // Wait for error reporting from the socket.
    usleep(GENERAL_DELAY_US);
    ASSERT_TRUE(reader->destroyed());
    ASSERT_EQ(ECONNRESET, reader->destroying_status().error_code());
}

TEST_F(HttpTest, http2_sanity) {
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "h2";
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    // Check that the first request with size larger than the default window can
    // be sent out, when remote settings are not received.
    brpc::Controller cntl;
    test::EchoRequest big_req;
    test::EchoResponse res;
    std::string message(2 * 1024 * 1024 /* 2M */, 'x');
    big_req.set_message(message);
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().uri() = "/EchoService/Echo";
    channel.CallMethod(NULL, &cntl, &big_req, &res, NULL);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(EXP_RESPONSE, res.message());

    // socket replacement when streamId runs out, the initial streamId is a special
    // value set in ctor of H2Context so that the number 15000 is enough to run out
    // of stream.
    test::EchoRequest req;
    req.set_message(EXP_REQUEST);
    for (int i = 0; i < 15000; ++i) {
        brpc::Controller cntl;
        cntl.http_request().set_content_type("application/json");
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.http_request().uri() = "/EchoService/Echo";
        channel.CallMethod(NULL, &cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message());
    }

    // check connection window size
    brpc::SocketUniquePtr main_ptr;
    brpc::SocketUniquePtr agent_ptr;
    EXPECT_EQ(brpc::Socket::Address(channel._server_id, &main_ptr), 0);
    EXPECT_EQ(main_ptr->GetAgentSocket(&agent_ptr, NULL), 0);
    brpc::policy::H2Context* ctx = static_cast<brpc::policy::H2Context*>(agent_ptr->parsing_context());
    ASSERT_GT(ctx->_remote_window_left.load(butil::memory_order_relaxed),
             brpc::H2Settings::DEFAULT_INITIAL_WINDOW_SIZE / 2);
}

TEST_F(HttpTest, http2_ping) {
    // This test injects PING frames before and after header and data.
    brpc::Controller cntl;

    // Prepare request
    butil::IOBuf req_out;
    int h2_stream_id = 0;
    MakeH2EchoRequestBuf(&req_out, &cntl, &h2_stream_id);
    // Prepare response
    butil::IOBuf res_out;
    char pingbuf[9 /*FRAME_HEAD_SIZE*/ + 8 /*Opaque Data*/];
    brpc::policy::SerializeFrameHead(pingbuf, 8, brpc::policy::H2_FRAME_PING, 0, 0);
    // insert ping before header and data
    res_out.append(pingbuf, sizeof(pingbuf));
    MakeH2EchoResponseBuf(&res_out, h2_stream_id);
    // insert ping after header and data
    res_out.append(pingbuf, sizeof(pingbuf));
    // parse response
    brpc::ParseResult res_pr =
            brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_TRUE(res_pr.is_ok());
    // process response
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_pr.message(), false);
    ASSERT_FALSE(cntl.Failed());
}

inline void SaveUint32(void* out, uint32_t v) {
    uint8_t* p = (uint8_t*)out;
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

TEST_F(HttpTest, http2_rst_before_header) {
    brpc::Controller cntl;
    // Prepare request
    butil::IOBuf req_out;
    int h2_stream_id = 0;
    MakeH2EchoRequestBuf(&req_out, &cntl, &h2_stream_id);
    // Prepare response
    butil::IOBuf res_out;
    char rstbuf[9 /*FRAME_HEAD_SIZE*/ + 4];
    brpc::policy::SerializeFrameHead(rstbuf, 4, brpc::policy::H2_FRAME_RST_STREAM, 0, h2_stream_id);
    SaveUint32(rstbuf + 9, brpc::H2_INTERNAL_ERROR);
    res_out.append(rstbuf, sizeof(rstbuf));
    MakeH2EchoResponseBuf(&res_out, h2_stream_id);
    // parse response
    brpc::ParseResult res_pr =
            brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_TRUE(res_pr.is_ok());
    // process response
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_pr.message(), false);
    ASSERT_TRUE(cntl.Failed());
    ASSERT_TRUE(cntl.ErrorCode() == brpc::EHTTP);
    ASSERT_TRUE(cntl.http_response().status_code() == brpc::HTTP_STATUS_INTERNAL_SERVER_ERROR);
}

TEST_F(HttpTest, http2_rst_after_header_and_data) {
    brpc::Controller cntl;
    // Prepare request
    butil::IOBuf req_out;
    int h2_stream_id = 0;
    MakeH2EchoRequestBuf(&req_out, &cntl, &h2_stream_id);
    // Prepare response
    butil::IOBuf res_out;
    MakeH2EchoResponseBuf(&res_out, h2_stream_id);
    char rstbuf[9 /*FRAME_HEAD_SIZE*/ + 4];
    brpc::policy::SerializeFrameHead(rstbuf, 4, brpc::policy::H2_FRAME_RST_STREAM, 0, h2_stream_id);
    SaveUint32(rstbuf + 9, brpc::H2_INTERNAL_ERROR);
    res_out.append(rstbuf, sizeof(rstbuf));
    // parse response
    brpc::ParseResult res_pr =
            brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_TRUE(res_pr.is_ok());
    // process response
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_pr.message(), false);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_TRUE(cntl.http_response().status_code() == brpc::HTTP_STATUS_OK);
}

TEST_F(HttpTest, http2_window_used_up) {
    brpc::Controller cntl;
    butil::IOBuf request_buf;
    test::EchoRequest req;
    // longer message to trigger using up window size sooner
    req.set_message("FLOW_CONTROL_FLOW_CONTROL");
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().set_content_type("application/proto");
    brpc::policy::SerializeHttpRequest(&request_buf, &cntl, &req);

    char settingsbuf[brpc::policy::FRAME_HEAD_SIZE + 36];
    brpc::H2Settings h2_settings;
    const size_t nb = brpc::policy::SerializeH2Settings(h2_settings, settingsbuf + brpc::policy::FRAME_HEAD_SIZE);
    brpc::policy::SerializeFrameHead(settingsbuf, nb, brpc::policy::H2_FRAME_SETTINGS, 0, 0);
    butil::IOBuf buf;
    buf.append(settingsbuf, brpc::policy::FRAME_HEAD_SIZE + nb);
    brpc::policy::ParseH2Message(&buf, _h2_client_sock.get(), false, NULL);

    int nsuc = brpc::H2Settings::DEFAULT_INITIAL_WINDOW_SIZE / cntl.request_attachment().size();
    for (int i = 0; i <= nsuc; i++) {
        brpc::policy::H2UnsentRequest* h2_req = brpc::policy::H2UnsentRequest::New(&cntl);
        cntl._current_call.stream_user_data = h2_req;
        brpc::SocketMessage* socket_message = NULL;
        brpc::policy::PackH2Request(NULL, &socket_message, cntl.call_id().value,
                                    NULL, &cntl, request_buf, NULL);
        butil::IOBuf dummy;
        butil::Status st = socket_message->AppendAndDestroySelf(&dummy, _h2_client_sock.get());
        if (i == nsuc) {
            // the last message should fail according to flow control policy.
            ASSERT_FALSE(st.ok());
            ASSERT_TRUE(st.error_code() == brpc::ELIMIT);
            ASSERT_TRUE(butil::StringPiece(st.error_str()).starts_with("remote_window_left is not enough"));
        } else {
            ASSERT_TRUE(st.ok());
        }
        h2_req->DestroyStreamUserData(_h2_client_sock, &cntl, 0, false);
    }
}

TEST_F(HttpTest, http2_settings) {
    char settingsbuf[brpc::policy::FRAME_HEAD_SIZE + 36];
    brpc::H2Settings h2_settings;
    h2_settings.header_table_size = 8192;
    h2_settings.max_concurrent_streams = 1024;
    h2_settings.stream_window_size= (1u << 29) - 1;
    const size_t nb = brpc::policy::SerializeH2Settings(h2_settings, settingsbuf + brpc::policy::FRAME_HEAD_SIZE);
    brpc::policy::SerializeFrameHead(settingsbuf, nb, brpc::policy::H2_FRAME_SETTINGS, 0, 0);
    butil::IOBuf buf;
    buf.append(settingsbuf, brpc::policy::FRAME_HEAD_SIZE + nb);

    brpc::policy::H2Context* ctx = new brpc::policy::H2Context(_socket.get(), NULL);
    CHECK_EQ(ctx->Init(), 0);
    _socket->initialize_parsing_context(&ctx);
    ctx->_conn_state = brpc::policy::H2_CONNECTION_READY;
    // parse settings
    brpc::policy::ParseH2Message(&buf, _socket.get(), false, NULL);

    butil::IOPortal response_buf;
    CHECK_EQ(response_buf.append_from_file_descriptor(_pipe_fds[0], 1024),
             (ssize_t)brpc::policy::FRAME_HEAD_SIZE);
    brpc::policy::H2FrameHead frame_head;
    butil::IOBufBytesIterator it(response_buf);
    ctx->ConsumeFrameHead(it, &frame_head);
    CHECK_EQ(frame_head.type, brpc::policy::H2_FRAME_SETTINGS);
    CHECK_EQ(frame_head.flags, 0x01 /* H2_FLAGS_ACK */);
    CHECK_EQ(frame_head.stream_id, 0);
    ASSERT_TRUE(ctx->_remote_settings.header_table_size == 8192);
    ASSERT_TRUE(ctx->_remote_settings.max_concurrent_streams == 1024);
    ASSERT_TRUE(ctx->_remote_settings.stream_window_size == (1u << 29) - 1);
}

TEST_F(HttpTest, http2_invalid_settings) {
    {
        brpc::Server server;
        brpc::ServerOptions options;
        options.h2_settings.stream_window_size = brpc::H2Settings::MAX_WINDOW_SIZE + 1;
        ASSERT_EQ(-1, server.Start("127.0.0.1:8924", &options));
    }
    {
        brpc::Server server;
        brpc::ServerOptions options;
        options.h2_settings.max_frame_size =
            brpc::H2Settings::DEFAULT_MAX_FRAME_SIZE - 1;
        ASSERT_EQ(-1, server.Start("127.0.0.1:8924", &options));
    }
    {
        brpc::Server server;
        brpc::ServerOptions options;
        options.h2_settings.max_frame_size =
            brpc::H2Settings::MAX_OF_MAX_FRAME_SIZE + 1;
        ASSERT_EQ(-1, server.Start("127.0.0.1:8924", &options));
    }
}

TEST_F(HttpTest, http2_not_closing_socket_when_rpc_timeout) {
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, NULL));
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "h2";
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(EXP_REQUEST);
    {
        // make a successful call to create the connection first
        brpc::Controller cntl;
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.http_request().uri() = "/EchoService/Echo";
        channel.CallMethod(NULL, &cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message());
    }

    brpc::SocketUniquePtr main_ptr;
    EXPECT_EQ(brpc::Socket::Address(channel._server_id, &main_ptr), 0);
    brpc::SocketId agent_id = main_ptr->_agent_socket_id.load(butil::memory_order_relaxed);

    for (int i = 0; i < 4; i++) {
        brpc::Controller cntl;
        cntl.set_timeout_ms(50);
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.http_request().uri() = "/EchoService/Echo?sleep_ms=300";
        channel.CallMethod(NULL, &cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());

        brpc::SocketUniquePtr ptr;
        brpc::SocketId id = main_ptr->_agent_socket_id.load(butil::memory_order_relaxed);
        EXPECT_EQ(id, agent_id);
    }

    {
        // make a successful call again to make sure agent_socket not changing
        brpc::Controller cntl;
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.http_request().uri() = "/EchoService/Echo";
        channel.CallMethod(NULL, &cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message());
        brpc::SocketUniquePtr ptr;
        brpc::SocketId id = main_ptr->_agent_socket_id.load(butil::memory_order_relaxed);
        EXPECT_EQ(id, agent_id);
    }
}

TEST_F(HttpTest, http2_header_after_data) {
    brpc::Controller cntl;

    // Prepare request
    butil::IOBuf req_out;
    int h2_stream_id = 0;
    MakeH2EchoRequestBuf(&req_out, &cntl, &h2_stream_id);

    // Prepare response to res_out
    butil::IOBuf res_out;
    {
        butil::IOBuf data_buf;
        test::EchoResponse res;
        res.set_message(EXP_RESPONSE);
        {
            butil::IOBufAsZeroCopyOutputStream wrapper(&data_buf);
            EXPECT_TRUE(res.SerializeToZeroCopyStream(&wrapper));
        }
        brpc::policy::H2Context* ctx =
            static_cast<brpc::policy::H2Context*>(_h2_client_sock->parsing_context());
        brpc::HPacker& hpacker = ctx->hpacker();
        butil::IOBufAppender header1_appender;
        brpc::HPackOptions options;
        options.encode_name = false;    /* disable huffman encoding */
        options.encode_value = false;
        {
            brpc::HPacker::Header header(":status", "200");
            hpacker.Encode(&header1_appender, header, options);
        }
        {
            brpc::HPacker::Header header("content-length",
                    butil::string_printf("%llu", (unsigned long long)data_buf.size()));
            hpacker.Encode(&header1_appender, header, options);
        }
        {
            brpc::HPacker::Header header(":status", "200");
            hpacker.Encode(&header1_appender, header, options);
        }
        {
            brpc::HPacker::Header header("content-type", "application/proto");
            hpacker.Encode(&header1_appender, header, options);
        }
        {
            brpc::HPacker::Header header("user-defined1", "a");
            hpacker.Encode(&header1_appender, header, options);
        }
        butil::IOBuf header1;
        header1_appender.move_to(header1);

        char headbuf[brpc::policy::FRAME_HEAD_SIZE];
        brpc::policy::SerializeFrameHead(headbuf, header1.size(),
                brpc::policy::H2_FRAME_HEADERS, 0, h2_stream_id);
        // append header1
        res_out.append(headbuf, sizeof(headbuf));
        res_out.append(butil::IOBuf::Movable(header1));

        brpc::policy::SerializeFrameHead(headbuf, data_buf.size(),
            brpc::policy::H2_FRAME_DATA, 0, h2_stream_id);
        // append data
        res_out.append(headbuf, sizeof(headbuf));
        res_out.append(butil::IOBuf::Movable(data_buf));

        butil::IOBufAppender header2_appender;
        {
            brpc::HPacker::Header header("user-defined1", "overwrite-a");
            hpacker.Encode(&header2_appender, header, options);
        }
        {
            brpc::HPacker::Header header("user-defined2", "b");
            hpacker.Encode(&header2_appender, header, options);
        }
        butil::IOBuf header2;
        header2_appender.move_to(header2);

        brpc::policy::SerializeFrameHead(headbuf, header2.size(),
                brpc::policy::H2_FRAME_HEADERS, 0x05/* end header and stream */,
                h2_stream_id);
        // append header2
        res_out.append(headbuf, sizeof(headbuf));
        res_out.append(butil::IOBuf::Movable(header2));
    }
    // parse response
    brpc::ParseResult res_pr =
            brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_TRUE(res_pr.is_ok());
    // process response
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_pr.message(), false);
    ASSERT_FALSE(cntl.Failed());

    brpc::HttpHeader& res_header = cntl.http_response();
    ASSERT_EQ(res_header.content_type(), "application/proto");
    // Check overlapped header is overwritten by the latter.
    const std::string* user_defined1 = res_header.GetHeader("user-defined1");
    ASSERT_EQ(*user_defined1, "overwrite-a");
    const std::string* user_defined2 = res_header.GetHeader("user-defined2");
    ASSERT_EQ(*user_defined2, "b");
}

TEST_F(HttpTest, http2_goaway_sanity) {
    brpc::Controller cntl;
    // Prepare request
    butil::IOBuf req_out;
    int h2_stream_id = 0;
    MakeH2EchoRequestBuf(&req_out, &cntl, &h2_stream_id);
    // Prepare response
    butil::IOBuf res_out;
    MakeH2EchoResponseBuf(&res_out, h2_stream_id);
    // append goaway
    char goawaybuf[9 /*FRAME_HEAD_SIZE*/ + 8];
    brpc::policy::SerializeFrameHead(goawaybuf, 8, brpc::policy::H2_FRAME_GOAWAY, 0, 0);
    SaveUint32(goawaybuf + 9, 0x7fffd8ef /*last stream id*/);
    SaveUint32(goawaybuf + 13, brpc::H2_NO_ERROR);
    res_out.append(goawaybuf, sizeof(goawaybuf));
    // parse response
    brpc::ParseResult res_pr =
            brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_TRUE(res_pr.is_ok());
    // process response
    ProcessMessage(brpc::policy::ProcessHttpResponse, res_pr.message(), false);
    ASSERT_TRUE(!cntl.Failed());

    // parse GOAWAY
    res_pr = brpc::policy::ParseH2Message(&res_out, _h2_client_sock.get(), false, NULL);
    ASSERT_EQ(res_pr.error(), brpc::PARSE_ERROR_NOT_ENOUGH_DATA);

    // Since GOAWAY has been received, the next request should fail
    brpc::policy::H2UnsentRequest* h2_req = brpc::policy::H2UnsentRequest::New(&cntl);
    cntl._current_call.stream_user_data = h2_req;
    brpc::SocketMessage* socket_message = NULL;
    brpc::policy::PackH2Request(NULL, &socket_message, cntl.call_id().value,
                                NULL, &cntl, butil::IOBuf(), NULL);
    butil::IOBuf dummy;
    butil::Status st = socket_message->AppendAndDestroySelf(&dummy, _h2_client_sock.get());
    ASSERT_EQ(st.error_code(), brpc::ELOGOFF);
    ASSERT_TRUE(st.error_data().ends_with("the connection just issued GOAWAY"));
}

class AfterRecevingGoAway : public ::google::protobuf::Closure {
public:
    void Run() {
        ASSERT_EQ(brpc::EHTTP, cntl.ErrorCode());
        delete this;
    }
    brpc::Controller cntl;
};

TEST_F(HttpTest, http2_handle_goaway_streams) {
    const butil::EndPoint ep(butil::IP_ANY, 5961);
    butil::fd_guard listenfd(butil::tcp_listen(ep));
    ASSERT_GT(listenfd, 0);

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_H2;
    ASSERT_EQ(0, channel.Init(ep, &options));

    int req_size = 10;
    std::vector<brpc::CallId> ids(req_size);
    for (int i = 0; i < req_size; i++) {
        AfterRecevingGoAway* done = new AfterRecevingGoAway;
        brpc::Controller& cntl = done->cntl;
        ids.push_back(cntl.call_id());
        cntl.set_timeout_ms(-1);
        cntl.http_request().uri() = "/it-doesnt-matter";
        channel.CallMethod(NULL, &cntl, NULL, NULL, done);
    }

    int servfd = accept(listenfd, NULL, NULL);
    ASSERT_GT(servfd, 0);
    // Sleep for a while to make sure that server has received all data.
    bthread_usleep(2000);
    char goawaybuf[brpc::policy::FRAME_HEAD_SIZE + 8];
    SerializeFrameHead(goawaybuf, 8, brpc::policy::H2_FRAME_GOAWAY, 0, 0);
    SaveUint32(goawaybuf + brpc::policy::FRAME_HEAD_SIZE, 0);
    SaveUint32(goawaybuf + brpc::policy::FRAME_HEAD_SIZE + 4, 0);
    ASSERT_EQ((ssize_t)brpc::policy::FRAME_HEAD_SIZE + 8, ::write(servfd, goawaybuf, brpc::policy::FRAME_HEAD_SIZE + 8));

    // After receving GOAWAY, the callbacks in client should be run correctly.
    for (int i = 0; i < req_size; i++) {
        brpc::Join(ids[i]);
    }
}

TEST_F(HttpTest, spring_protobuf_content_type) {
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, nullptr));

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "http";
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(EXP_REQUEST);
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().uri() = "/EchoService/Echo";
    cntl.http_request().set_content_type("application/x-protobuf");
    cntl.request_attachment().append(req.SerializeAsString());
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ("application/x-protobuf", cntl.http_response().content_type());
    ASSERT_TRUE(res.ParseFromString(cntl.response_attachment().to_string()));
    ASSERT_EQ(EXP_RESPONSE, res.message());

    brpc::Controller cntl2;
    test::EchoService_Stub stub(&channel);
    req.set_message(EXP_REQUEST);
    res.Clear();
    cntl2.http_request().set_content_type("application/x-protobuf");
    stub.Echo(&cntl2, &req, &res, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(EXP_RESPONSE, res.message());
    ASSERT_EQ("application/x-protobuf", cntl.http_response().content_type());
}

TEST_F(HttpTest, dump_http_request) {
    // save origin value of gflag
    auto rpc_dump_dir = brpc::FLAGS_rpc_dump_dir;
    auto rpc_dump_max_requests_in_one_file = brpc::FLAGS_rpc_dump_max_requests_in_one_file;

    // set gflag and global variable in order to be sure to dump request
    brpc::FLAGS_rpc_dump = true;
    brpc::FLAGS_rpc_dump_dir = "dump_http_request";
    brpc::FLAGS_rpc_dump_max_requests_in_one_file = 1;
    brpc::g_rpc_dump_sl.ever_grabbed = true;
    brpc::g_rpc_dump_sl.sampling_range = bvar::COLLECTOR_SAMPLING_BASE;

    // init channel
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, nullptr));

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "http";
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    // send request and dump it to file
    {
        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        std::string req_json;
        ASSERT_TRUE(json2pb::ProtoMessageToJson(req, &req_json));

        brpc::Controller cntl;
        cntl.http_request().uri() = "/EchoService/Echo";
        cntl.http_request().set_content_type("application/json");
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.request_attachment() = req_json;
        channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        ASSERT_FALSE(cntl.Failed());

        // sleep 1s, because rpc_dump doesn't run immediately
        sleep(1);
    }

    // replay request from dump file
    {
        brpc::SampleIterator it(brpc::FLAGS_rpc_dump_dir);
        brpc::SampledRequest* sample = it.Next();
        ASSERT_NE(nullptr, sample);

        std::unique_ptr<brpc::SampledRequest> sample_guard(sample);

        // the logic of next code is same as that in rpc_replay.cpp
        ASSERT_EQ(sample->meta.protocol_type(), brpc::PROTOCOL_HTTP);
        brpc::Controller cntl;
        cntl.reset_sampled_request(sample_guard.release());
        brpc::HttpMessage http_message;
        http_message.ParseFromIOBuf(sample->request);
        cntl.http_request().Swap(http_message.header());
        // clear origin Host in header
        cntl.http_request().RemoveHeader("Host");
        cntl.http_request().uri().set_host("");
        cntl.request_attachment() = http_message.body().movable();

        channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ("application/json", cntl.http_response().content_type());

        std::string res_json = cntl.response_attachment().to_string();
        test::EchoResponse res;
        json2pb::Json2PbOptions options;
        ASSERT_TRUE(json2pb::JsonToProtoMessage(res_json, &res, options));
        ASSERT_EQ(EXP_RESPONSE, res.message());
    }

    // delete dump directory
    butil::DeleteFile(butil::FilePath(brpc::FLAGS_rpc_dump_dir), true);

    // restore gflag and global variable
    brpc::FLAGS_rpc_dump = false;
    brpc::FLAGS_rpc_dump_dir = rpc_dump_dir;
    brpc::FLAGS_rpc_dump_max_requests_in_one_file = rpc_dump_max_requests_in_one_file;
    brpc::g_rpc_dump_sl.ever_grabbed = false;
    brpc::g_rpc_dump_sl.sampling_range = 0;
}

TEST_F(HttpTest, spring_protobuf_text_content_type) {
    const int port = 8923;
    brpc::Server server;
    EXPECT_EQ(0, server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    EXPECT_EQ(0, server.Start(port, nullptr));

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "http";
    ASSERT_EQ(0, channel.Init(butil::EndPoint(butil::my_ip(), port), &options));

    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(EXP_REQUEST);
    cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
    cntl.http_request().uri() = "/EchoService/Echo";
    cntl.http_request().set_content_type("application/proto-text");
    cntl.request_attachment().append(req.Utf8DebugString());
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ("application/proto-text", cntl.http_response().content_type());
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
            cntl.response_attachment().to_string(), &res));
    ASSERT_EQ(EXP_RESPONSE, res.message());
}

} //namespace
