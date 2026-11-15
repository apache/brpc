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

#include "butil/config.h"
#if BRPC_WITH_FLATBUFFERS
#if BRPC_WITH_RDMA
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <string>
#include <vector>
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/errno.pb.h"
#include "brpc/flatbuffers/message.h"
#include "brpc/flatbuffers/service.h"
#include "brpc/global.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma_transport.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "flatbuffers_message_generated.h"

namespace {

using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;
using brpc::flatbuffers::MethodDescriptor;
using brpc::flatbuffers::ServiceDescriptor;
using brpc_fbtest::Payload;

bool IsRdmaOn(const brpc::Socket* socket) {
    if (socket == nullptr || socket->socket_mode() != brpc::SOCKET_MODE_RDMA) {
        return false;
    }
    const auto* transport =
        static_cast<const brpc::RdmaTransport*>(socket->_transport.get());
    return transport->_rdma_ep->_state.load(butil::memory_order_acquire) ==
               brpc::rdma::RdmaEndpoint::ESTABLISHED &&
           transport->_rdma_state == brpc::RdmaTransport::RDMA_ON;
}

std::string Bytes(const Message& message) {
    return std::string(reinterpret_cast<const char*>(message.data()),
                       message.size());
}

Message MakePayload(size_t text_size = 16, int64_t value = 123,
                    size_t vector_size = 4) {
    MessageBuilder builder(8);
    auto text = builder.CreateString(std::string(text_size, 'x'));
    std::vector<int32_t> numbers(vector_size);
    for (size_t i = 0; i < numbers.size(); ++i) {
        numbers[i] = static_cast<int32_t>(i) * -3;
    }
    auto values = builder.CreateVector(numbers);
    builder.Finish(brpc_fbtest::CreatePayload(builder, value, text, values));
    return builder.ReleaseMessage();
}

void ExpectReply(const Message& response, const Message& request,
                 const MethodDescriptor* method) {
    ASSERT_NE(nullptr, method);
    ASSERT_TRUE(request.Verify<Payload>());
    // Framing validation is not schema validation; the caller must do this.
    ASSERT_TRUE(response.Verify<Payload>());
    const Payload* in = request.GetRoot<Payload>();
    const Payload* out = response.GetRoot<Payload>();
    EXPECT_EQ(in->value() + method->index(), out->value());
    ASSERT_NE(nullptr, out->message());
    EXPECT_EQ(method->name() + ":" + in->message()->str(),
              out->message()->str());
    ASSERT_NE(nullptr, out->values());
    ASSERT_EQ(in->values()->size(), out->values()->size());
    for (size_t i = 0; i < in->values()->size(); ++i) {
        EXPECT_EQ(in->values()->Get(i), out->values()->Get(i));
    }
}

class Completion : public google::protobuf::Closure {
public:
    void Run() override { ++_calls; }
    int calls() const { return _calls.load(); }
private:
    std::atomic<int> _calls{0};
};

class PayloadService : public brpc::flatbuffers::Service {
public:
    PayloadService() {
        EXPECT_EQ(0, _descriptor.init(
            {"brpc_fbtest", "RdmaService", "Echo", {41}}));
    }
    const ServiceDescriptor* GetDescriptor() override { return &_descriptor; }
    void FBCallMethod(const MethodDescriptor* method,
                      google::protobuf::RpcController* controller,
                      const Message* request, Message* response,
                      google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        if (IsRdmaOn(brpc::ControllerPrivateAccessor(cntl).get_sending_socket())) {
            ++rdma_calls;
        }
        if (!method || _descriptor.FindMethodByIndex(method->index()) != method ||
            cntl->flatbuffers_method() != method) {
            cntl->SetFailed(brpc::ENOMETHOD, "Wrong FlatBuffers method context");
            return;
        }
        if (!request || !request->Verify<Payload>()) {
            cntl->SetFailed(brpc::EREQUEST, "Invalid Payload schema");
            return;
        }
        const Payload* in = request->GetRoot<Payload>();
        if (!in->message() || !in->values()) {
            cntl->SetFailed(brpc::EREQUEST, "Missing application fields");
            return;
        }
        const int failure = fail_next_code.exchange(0);
        if (failure != 0) {
            cntl->SetFailed(failure, "Requested one-shot application failure");
            return;
        }
        MessageBuilder builder(8);
        auto text = builder.CreateString(method->name() + ":" +
                                         in->message()->str());
        const std::vector<int32_t> numbers(in->values()->begin(),
                                           in->values()->end());
        auto values = builder.CreateVector(numbers);
        builder.Finish(brpc_fbtest::CreatePayload(
            builder, in->value() + method->index(), text, values));
        *response = builder.ReleaseMessage();
        cntl->response_attachment().append(cntl->request_attachment());
    }
    std::atomic<int> rdma_calls{0};
    std::atomic<int> fail_next_code{0};
private:
    ServiceDescriptor _descriptor;
};

class FlatBuffersRdmaTest : public ::testing::Test {
protected:
    void StartServer(brpc::SocketMode mode) {
        ASSERT_EQ(0, server.AddFlatBuffersService(
            &service, brpc::SERVER_DOESNT_OWN_SERVICE));
        brpc::ServerOptions options;
        options.socket_mode = mode;
        ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));
    }

    void StartChannel(brpc::SocketMode mode) {
        brpc::ChannelOptions options;
        options.protocol = "fb_rpc";
        options.connection_type = "single";
        options.socket_mode = mode;
        options.timeout_ms = 10000;
        options.connect_timeout_ms = 5000;
        options.max_retry = 0;
        ASSERT_EQ(0, channel.Init(server.listen_address(), &options));
    }

    void TearDown() override {
        server.Stop(0);
        server.Join();
    }

    void CheckTransport(bool expect_rdma) {
        brpc::SocketUniquePtr socket;
        ASSERT_EQ(0, brpc::Socket::Address(channel._server_id, &socket));
        ASSERT_EQ(expect_rdma, IsRdmaOn(socket.get()));
        if (!expect_rdma && socket->socket_mode() == brpc::SOCKET_MODE_RDMA) {
            const auto* transport =
                static_cast<const brpc::RdmaTransport*>(socket->_transport.get());
            EXPECT_EQ(brpc::rdma::RdmaEndpoint::FALLBACK_TCP,
                      transport->_rdma_ep->_state.load(butil::memory_order_acquire));
            EXPECT_EQ(brpc::RdmaTransport::RDMA_OFF, transport->_rdma_state);
        }
    }

    void RoundTrips(bool expect_rdma) {
        const MethodDescriptor* method =
            service.GetDescriptor()->FindMethodByIndex(41);
        // The last payload exceeds the RDMA pool's largest block. The tiny
        // initial builder size also exercises growth, and both peers must
        // merge multiple receive blocks into an aligned contiguous message.
        for (size_t size : {0u, 63u, 8193u, 3u * 1024 * 1024 + 17}) {
            for (bool asynchronous : {false, true}) {
                SCOPED_TRACE(size);
                SCOPED_TRACE(asynchronous);
                const Message request = MakePayload(size, 31, 19);
                const std::string original = Bytes(request);
                Message response;
                brpc::Controller cntl;
                std::string attachment(std::min<size_t>(size / 3, 131073), '\0');
                for (size_t i = 0; i < attachment.size(); ++i) {
                    attachment[i] = static_cast<char>(i % 251);
                }
                cntl.request_attachment().append(attachment);
                Completion done;
                const int calls_before = service.rdma_calls.load();
                channel.FBCallMethod(method, &cntl, &request, &response,
                                     asynchronous ? &done : nullptr);
                if (asynchronous) {
                    brpc::Join(cntl.call_id());
                    EXPECT_EQ(1, done.calls());
                }
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                ASSERT_NO_FATAL_FAILURE(CheckTransport(expect_rdma));
                EXPECT_EQ(calls_before + (expect_rdma ? 1 : 0),
                          service.rdma_calls.load());
                ExpectReply(response, request, method);
                EXPECT_EQ(attachment, cntl.response_attachment().to_string());
                EXPECT_EQ(original, Bytes(request));
            }
        }

        // Error responses have no application payload. They must complete
        // sync and async calls over the negotiated transport as well.
        for (bool asynchronous : {false, true}) {
            SCOPED_TRACE(asynchronous);
            const Message request = MakePayload();
            Message response;
            brpc::Controller cntl;
            Completion done;
            service.fail_next_code = EACCES;
            const int calls_before = service.rdma_calls.load();
            channel.FBCallMethod(method, &cntl, &request, &response,
                                 asynchronous ? &done : nullptr);
            if (asynchronous) {
                brpc::Join(cntl.call_id());
                EXPECT_EQ(1, done.calls());
            }
            ASSERT_EQ(EACCES, cntl.ErrorCode()) << cntl.ErrorText();
            EXPECT_EQ(0u, response.size());
            ASSERT_NO_FATAL_FAILURE(CheckTransport(expect_rdma));
            EXPECT_EQ(calls_before + (expect_rdma ? 1 : 0),
                      service.rdma_calls.load());
        }
    }

    PayloadService service;
    brpc::Server server;
    brpc::Channel channel;
};

TEST_F(FlatBuffersRdmaTest, ProtocolAdmissionAndChannelOptions) {
    EXPECT_TRUE(brpc::rdma::SupportedByRdma("fb_rpc"));
    EXPECT_TRUE(brpc::rdma::SupportedByRdma("baidu_std"));
    EXPECT_FALSE(brpc::rdma::SupportedByRdma("http"));
    ASSERT_NO_FATAL_FAILURE(StartServer(brpc::SOCKET_MODE_TCP));
    for (const char* connection_type : {"single", "pooled", "short"}) {
        SCOPED_TRACE(connection_type);
        brpc::Channel candidate;
        brpc::ChannelOptions options;
        options.protocol = "fb_rpc";
        options.connection_type = connection_type;
        options.socket_mode = brpc::SOCKET_MODE_RDMA;
        ASSERT_EQ(0, candidate.Init(server.listen_address(), &options));
    }
}

TEST_F(FlatBuffersRdmaTest, RdmaClientFallsBackToTcpServer) {
    ASSERT_NO_FATAL_FAILURE(StartServer(brpc::SOCKET_MODE_TCP));
    ASSERT_NO_FATAL_FAILURE(StartChannel(brpc::SOCKET_MODE_RDMA));
    ASSERT_NO_FATAL_FAILURE(RoundTrips(false));
}

TEST_F(FlatBuffersRdmaTest, TcpClientCallsRdmaServer) {
    ASSERT_NO_FATAL_FAILURE(StartServer(brpc::SOCKET_MODE_RDMA));
    ASSERT_NO_FATAL_FAILURE(StartChannel(brpc::SOCKET_MODE_TCP));
    ASSERT_NO_FATAL_FAILURE(RoundTrips(false));
}

TEST_F(FlatBuffersRdmaTest, HardwareRdmaRoundTrips) {
    ASSERT_TRUE(brpc::rdma::IsRdmaAvailable());
    ASSERT_NO_FATAL_FAILURE(StartServer(brpc::SOCKET_MODE_RDMA));
    ASSERT_NO_FATAL_FAILURE(StartChannel(brpc::SOCKET_MODE_RDMA));
    ASSERT_NO_FATAL_FAILURE(RoundTrips(true));
}

}  // namespace

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    // RDMA must initialize before any test allocates IOBuf storage.
    brpc::GlobalInitializeOrDie();
    brpc::rdma::GlobalRdmaInitializeOrDie();
    return RUN_ALL_TESTS();
}
#endif  // BRPC_WITH_RDMA
#endif  // BRPC_WITH_FLATBUFFERS
