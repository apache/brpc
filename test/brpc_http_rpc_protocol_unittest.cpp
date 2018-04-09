// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/files/scoped_file.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "echo.pb.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "json2pb/pb_to_json.h"
#include "json2pb/json_to_pb.h"
#include "brpc/details/method_status.h"

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
    void Echo(::google::protobuf::RpcController*,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);

        EXPECT_EQ(EXP_REQUEST, req->message());
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
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext();
        msg->header().uri().set_path(path);
        msg->header().set_content_type("application/json");
        msg->header().set_method(brpc::HTTP_METHOD_POST);

        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        butil::IOBufAsZeroCopyOutputStream req_stream(&msg->body());
        EXPECT_TRUE(json2pb::ProtoMessageToJson(req, &req_stream, NULL));
        return msg;
    }

    brpc::policy::HttpContext* MakeGetRequestMessage(const std::string& path) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext();
        msg->header().uri().set_path(path);
        msg->header().set_method(brpc::HTTP_METHOD_GET);
        return msg;
    }


    brpc::policy::HttpContext* MakeResponseMessage(int code) {
        brpc::policy::HttpContext* msg = new brpc::policy::HttpContext();
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

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
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
}

TEST_F(HttpTest, process_request_failed_socket) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/EchoService/Echo");
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror.get_value());
    CheckResponseCode(true, 0);
}

TEST_F(HttpTest, reject_get_to_pb_services_with_required_fields) {
    brpc::policy::HttpContext* msg = MakeGetRequestMessage("/EchoService/Echo");
    _server._status = brpc::Server::RUNNING;
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror.get_value());
    const brpc::Server::MethodProperty* mp =
        _server.FindMethodPropertyByFullName("test.EchoService.Echo");
    ASSERT_TRUE(mp);
    ASSERT_TRUE(mp->status);
    ASSERT_EQ(1ll, mp->status->_nerror.get_value());
    CheckResponseCode(false, brpc::HTTP_STATUS_BAD_REQUEST);
}

TEST_F(HttpTest, process_request_logoff) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/EchoService/Echo");
    _server._status = brpc::Server::READY;
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror.get_value());
    CheckResponseCode(false, brpc::HTTP_STATUS_SERVICE_UNAVAILABLE);
}

TEST_F(HttpTest, process_request_wrong_method) {
    brpc::policy::HttpContext* msg = MakePostRequestMessage("/NO_SUCH_METHOD");
    ProcessMessage(brpc::policy::ProcessHttpRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror.get_value());
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
        butil::intrusive_ptr<brpc::ProgressiveAttachment> pa(
                cntl->CreateProgressiveAttachment(stop_style));
        if (pa == NULL) {
            cntl->SetFailed("The socket was just failed");
            return;
        }
        if (_done_place == DONE_BEFORE_CREATE_PA) {
            done_guard.reset(NULL);
        }
        ASSERT_GT(PA_DATA_LEN, 8);  // long enough to hold a 64-bit decimal.
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
        butil::intrusive_ptr<brpc::ProgressiveAttachment> pa(
                cntl->CreateProgressiveAttachment(stop_style));
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
                ASSERT_GT(last_read, 100000);
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
            ASSERT_GT(last_read, 100000);
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
                ASSERT_GT(last_read, 100000);
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
    ASSERT_GT(new_written_bytes - old_written_bytes, 100000);
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
        ASSERT_GT(last_read, 100000);
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
} //namespace
