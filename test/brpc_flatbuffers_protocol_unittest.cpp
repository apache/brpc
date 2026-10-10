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
#include <gtest/gtest.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "butil/endpoint.h"
#include "butil/fd_guard.h"
#include "brpc/authenticator.h"
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/method_status.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/errno.pb.h"
#include "brpc/flatbuffers/message.h"
#include "brpc/flatbuffers/service.h"
#include "brpc/interceptor.h"
#include "brpc/retry_policy.h"
#include "brpc/policy/flatbuffers_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/server.h"
#include "brpc/stream.h"
#include "flatbuffers_message_generated.h"

namespace {

using brpc::flatbuffers::BrpcDescriptorTable;
using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;
using brpc::flatbuffers::MethodDescriptor;
using brpc::flatbuffers::ServiceDescriptor;
using brpc_fbtest::Payload;

const int kRpcTimeoutMs = 1500;
const int kWaitTimeoutMs = 5000;
const size_t kHeaderSize = 12;
const size_t kRequestMetaSize = 24;
const size_t kResponseMetaSize = 20;

// These independent byte-level helpers intentionally do not reuse the codec.
void Put32(std::string* bytes, size_t offset, uint32_t value,
           bool big_endian = false) {
    for (size_t i = 0; i < 4; ++i) {
        (*bytes)[offset + i] = static_cast<char>(
            value >> (8 * (big_endian ? 3 - i : i)));
    }
}

uint32_t Get32(const std::string& bytes, size_t offset,
               bool big_endian = false) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(
            static_cast<unsigned char>(bytes[offset + i]))
            << (8 * (big_endian ? 3 - i : i));
    }
    return value;
}

void Put64(std::string* bytes, size_t offset, uint64_t value) {
    Put32(bytes, offset, static_cast<uint32_t>(value));
    Put32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

uint64_t Get64(const std::string& bytes, size_t offset) {
    return Get32(bytes, offset) |
           (static_cast<uint64_t>(Get32(bytes, offset + 4)) << 32);
}

std::string Header(uint32_t body_size, uint32_t meta_size) {
    std::string bytes(kHeaderSize, '\0');
    bytes.replace(0, 4, "FRPC");
    Put32(&bytes, 4, body_size, true);
    Put32(&bytes, 8, meta_size, true);
    return bytes;
}

std::string Frame(const std::string& meta, const std::string& payload) {
    return Header(meta.size() + payload.size(), meta.size()) + meta + payload;
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

std::string RequestMeta(const MethodDescriptor* method, uint32_t message_size,
                        uint32_t attachment_size, uint64_t correlation_id) {
    std::string meta(kRequestMetaSize, '\0');
    Put32(&meta, 0, method->service()->index());
    Put32(&meta, 4, method->index());
    Put32(&meta, 8, message_size);
    Put32(&meta, 12, attachment_size);
    Put64(&meta, 16, correlation_id);
    return meta;
}

std::string ResponseMeta(uint32_t message_size, uint32_t attachment_size,
                         uint64_t correlation_id) {
    std::string meta(kResponseMetaSize, '\0');
    Put32(&meta, 4, message_size);
    Put32(&meta, 8, attachment_size);
    Put64(&meta, 12, correlation_id);
    return meta;
}

class Completion : public google::protobuf::Closure {
public:
    void Run() override {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_calls;
        _condition.notify_all();
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(_mutex);
        return _condition.wait_for(lock, std::chrono::milliseconds(kWaitTimeoutMs),
                                   [this] { return _calls != 0; });
    }
    int calls() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _calls;
    }
private:
    std::mutex _mutex;
    std::condition_variable _condition;
    int _calls = 0;
};

class PayloadService : public brpc::flatbuffers::Service {
public:
    explicit PayloadService(
        BrpcDescriptorTable table = {"brpc_fbtest", "ProtocolService",
                                      "Echo Alternate Removed", {7, 41, 99}},
        std::atomic<int>* destroyed = nullptr)
        : _destroyed(destroyed) {
        EXPECT_EQ(0, _descriptor.init(table));
    }
    ~PayloadService() override {
        ReleaseHeld();
        if (_destroyed) {
            ++*_destroyed;
        }
    }
    const ServiceDescriptor* GetDescriptor() override { return &_descriptor; }
    void FBCallMethod(const MethodDescriptor* method,
                      google::protobuf::RpcController* controller,
                      const Message* request, Message* response,
                      google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        ++entered;
        if (cntl->is_security_mode()) {
            ++security_mode_calls;
        }
        if (!method || _descriptor.FindMethodByIndex(method->index()) != method ||
            cntl->flatbuffers_method() != method) {
            cntl->SetFailed(brpc::ENOMETHOD, "Wrong FlatBuffers method context");
            return;
        }
        if (!request || !request->Verify<Payload>()) {
            ++rejected;
            cntl->SetFailed(brpc::EREQUEST, "Invalid Payload schema");
            return;
        }
        // No application field is read before Verify succeeds.
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
        if (response_compressed.load()) {
            cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);
        }
        if (response_checksummed.load()) {
            cntl->set_response_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
        }
        std::lock_guard<std::mutex> lock(_mutex);
        if (_hold_next) {
            _hold_next = false;
            _held = done_guard.release();
            _condition.notify_all();
        }
    }
    void HoldNext() {
        std::lock_guard<std::mutex> lock(_mutex);
        _hold_next = true;
    }
    bool WaitHeld() {
        std::unique_lock<std::mutex> lock(_mutex);
        return _condition.wait_for(lock, std::chrono::milliseconds(kWaitTimeoutMs),
                                   [this] { return _held != nullptr; });
    }
    void ReleaseHeld() {
        google::protobuf::Closure* done = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _hold_next = false;
            std::swap(done, _held);
        }
        if (done) {
            done->Run();
        }
    }
    std::atomic<int> entered{0};
    std::atomic<int> rejected{0};
    std::atomic<int> security_mode_calls{0};
    std::atomic<int> fail_next_code{0};
    std::atomic<bool> response_compressed{false};
    std::atomic<bool> response_checksummed{false};
private:
    ServiceDescriptor _descriptor;
    std::atomic<int>* _destroyed;
    std::mutex _mutex;
    std::condition_variable _condition;
    bool _hold_next = false;
    google::protobuf::Closure* _held = nullptr;
};

int InitChannel(brpc::Channel* channel, const butil::EndPoint& endpoint,
                const char* protocol = "fb_rpc",
                const char* connection_type = "single") {
    brpc::ChannelOptions options;
    options.protocol = protocol;
    options.connection_type = connection_type;
    options.timeout_ms = kRpcTimeoutMs;
    options.connect_timeout_ms = kRpcTimeoutMs;
    options.max_retry = 0;
    return channel->Init(endpoint, &options);
}

class FlatBuffersProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, server.AddFlatBuffersService(
            &service, brpc::SERVER_DOESNT_OWN_SERVICE));
        ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
        ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
    }
    void TearDown() override {
        service.ReleaseHeld();
        server.Stop(0);
        server.Join();
    }
    const MethodDescriptor* method(int id = 7) {
        return service.GetDescriptor()->FindMethodByIndex(id);
    }
    PayloadService service;
    brpc::Server server;
    brpc::Channel channel;
};

TEST(FlatBuffersFramingTest, PartialFramesAndInvalidHeadersDoNotConsumeInput) {
    const std::string wire = Frame(std::string(24, 'm'), "payload");
    for (size_t size = 0; size < wire.size(); ++size) {
        SCOPED_TRACE(size);
        butil::IOBuf input;
        input.append(wire.data(), size);
        const std::string before = input.to_string();
        brpc::ParseResult result = brpc::policy::ParseFlatBuffersMessage(
            &input, nullptr, false, nullptr);
        EXPECT_EQ(brpc::PARSE_ERROR_NOT_ENOUGH_DATA, result.error());
        EXPECT_EQ(before, input.to_string());
    }
    for (const std::string& bad : {std::string("X"), std::string("FX"),
                                  std::string("FRX"), std::string("FRPX")}) {
        butil::IOBuf input;
        input.append(bad);
        EXPECT_EQ(brpc::PARSE_ERROR_TRY_OTHERS,
                  brpc::policy::ParseFlatBuffersMessage(
                      &input, nullptr, false, nullptr).error());
        EXPECT_EQ(bad, input.to_string());
    }
    const std::vector<std::pair<std::string, brpc::ParseError> > malformed = {
        {Header(std::numeric_limits<uint32_t>::max(), 24),
         brpc::PARSE_ERROR_TOO_BIG_DATA},
        {Header(0, 1), brpc::PARSE_ERROR_ABSOLUTELY_WRONG},
        {Header(23, 24), brpc::PARSE_ERROR_ABSOLUTELY_WRONG}
    };
    for (const auto& entry : malformed) {
        butil::IOBuf input;
        input.append(entry.first);
        EXPECT_EQ(entry.second, brpc::policy::ParseFlatBuffersMessage(
            &input, nullptr, false, nullptr).error());
        EXPECT_EQ(entry.first, input.to_string());
    }
}

TEST(FlatBuffersFramingTest, FragmentedUnalignedAndExtendedMetadata) {
    const Message payload = MakePayload(8193);
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "Framing", "Echo", {41}}));
    std::string meta = RequestMeta(descriptor.method(0), payload.size(), 3,
                                   0x1122334455667788ULL);
    meta.append("\x01\x02\x03\x04\x05", 5);
    const std::string wire = Frame(meta, Bytes(payload) + "att");
    for (size_t fragment_size : {1u, 7u, 13u, 4093u}) {
        SCOPED_TRACE(fragment_size);
        butil::IOBuf input;
        for (size_t pos = 0; pos < wire.size(); pos += fragment_size) {
            const size_t size = std::min(fragment_size, wire.size() - pos);
            char* allocation = static_cast<char*>(malloc(size + 1));
            ASSERT_NE(nullptr, allocation);
            memcpy(allocation + 1, wire.data() + pos, size);
            ASSERT_EQ(0, input.append_user_data(allocation + 1, size,
                [allocation](void*) { free(allocation); }));
        }
        ASSERT_GT(input.backing_block_num(), 1u);
        input.append("next");
        brpc::ParseResult result = brpc::policy::ParseFlatBuffersMessage(
            &input, nullptr, false, nullptr);
        ASSERT_EQ(brpc::PARSE_OK, result.error());
        brpc::DestroyingPtr<brpc::policy::MostCommonMessage> parsed(
            static_cast<brpc::policy::MostCommonMessage*>(result.message()));
        EXPECT_EQ(meta, parsed->meta.to_string());
        EXPECT_EQ(Bytes(payload) + "att", parsed->payload.to_string());
        EXPECT_EQ("next", input.to_string());
        Message decoded;
        butil::IOBuf body;
        parsed->payload.cutn(&body, payload.size());
        ASSERT_TRUE(decoded.parse_msg_from_iobuf(body, payload.size(), 0));
        ASSERT_TRUE(decoded.Verify<Payload>());
        EXPECT_EQ(Bytes(payload), Bytes(decoded));
    }
}

TEST(FlatBuffersFramingTest, EmptyMetadataIsSafelySeparatedForProcessorValidation) {
    for (size_t meta_size : {0u, 1u, 19u, 23u, 24u}) {
        butil::IOBuf input;
        input.append(Frame(std::string(meta_size, '\xff'), "x"));
        brpc::ParseResult result = brpc::policy::ParseFlatBuffersMessage(
            &input, nullptr, false, nullptr);
        ASSERT_EQ(brpc::PARSE_OK, result.error());
        brpc::DestroyingPtr<brpc::policy::MostCommonMessage> parsed(
            static_cast<brpc::policy::MostCommonMessage*>(result.message()));
        EXPECT_EQ(meta_size, parsed->meta.size());
        EXPECT_EQ("x", parsed->payload.to_string());
        EXPECT_TRUE(input.empty());
    }
}

TEST(FlatBuffersSerializationTest, RejectsNullProtobufEmptyCompressionAndChecksum) {
    google::protobuf::DescriptorProto protobuf;
    Message empty;
    Message valid = MakePayload();
    const google::protobuf::Message* invalid[] = {nullptr, &protobuf, &empty};
    for (const auto* request : invalid) {
        brpc::Controller cntl;
        butil::IOBuf serialized;
        brpc::policy::SerializeFlatBuffersRequest(&serialized, &cntl, request);
        EXPECT_TRUE(cntl.Failed());
        EXPECT_EQ(brpc::EREQUEST, cntl.ErrorCode());
        EXPECT_TRUE(serialized.empty());
    }
    for (int option = 0; option < 2; ++option) {
        brpc::Controller cntl;
        if (option == 0) {
            cntl.set_request_compress_type(brpc::COMPRESS_TYPE_GZIP);
        } else {
            cntl.set_request_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
        }
        butil::IOBuf serialized;
        brpc::policy::SerializeFlatBuffersRequest(&serialized, &cntl, &valid);
        EXPECT_EQ(brpc::EREQUEST, cntl.ErrorCode());
        EXPECT_TRUE(serialized.empty());
    }
}

TEST(FlatBuffersSerializationTest, PayloadOnlyAndIndependentRetryHeadersKeepConstInput) {
    Message mutable_request = MakePayload(8192);
    memset(mutable_request.mutable_buf_begin(), 0xa5,
           mutable_request.get_meta_size());
    const Message request = std::move(mutable_request);
    const std::string original = Bytes(request);
    const size_t prefix_size = request.get_meta_size();
    const std::string prefix(reinterpret_cast<const char*>(request.data()) -
                             prefix_size, prefix_size);
    const uint8_t* data = request.data();
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "Pack", "Echo", {41}}));
    brpc::Controller cntl;
    brpc::ControllerPrivateAccessor(&cntl).set_flatbuffers_method(
        descriptor.method(0));
    cntl.request_attachment().append("\0attachment", 11);
    butil::IOBuf serialized;
    brpc::policy::SerializeFlatBuffersRequest(&serialized, &cntl, &request);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(original, serialized.to_string());
    EXPECT_EQ(request.size(), serialized.size());
    butil::IOBuf first;
    brpc::policy::PackFlatBuffersRequest(&first, nullptr,
        0x0102030405060708ULL, nullptr, &cntl, serialized, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    const std::string first_snapshot = first.to_string();
    cntl.request_attachment().clear();
    cntl.request_attachment().append("second");
    butil::IOBuf second;
    brpc::policy::PackFlatBuffersRequest(&second, nullptr,
        0x8877665544332211ULL, nullptr, &cntl, serialized, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    const std::string second_snapshot = second.to_string();
    ASSERT_GE(first_snapshot.size(), kHeaderSize + kRequestMetaSize);
    ASSERT_GE(second_snapshot.size(), kHeaderSize + kRequestMetaSize);
    EXPECT_EQ("FRPC", first_snapshot.substr(0, 4));
    EXPECT_EQ(first_snapshot.size() - 12, Get32(first_snapshot, 4, true));
    EXPECT_EQ(24u, Get32(first_snapshot, 8, true));
    EXPECT_EQ(descriptor.index(), Get32(first_snapshot, 12));
    EXPECT_EQ(41u, Get32(first_snapshot, 16));
    EXPECT_EQ(request.size(), Get32(first_snapshot, 20));
    EXPECT_EQ(11u, Get32(first_snapshot, 24));
    EXPECT_EQ(0x0102030405060708ULL, Get64(first_snapshot, 28));
    EXPECT_EQ(0x8877665544332211ULL, Get64(second_snapshot, 28));
    EXPECT_EQ(6u, Get32(second_snapshot, 24));
    EXPECT_EQ(first_snapshot, first.to_string());
    EXPECT_EQ(original, serialized.to_string());
    EXPECT_EQ(data, request.data());
    EXPECT_EQ(prefix_size, request.get_meta_size());
    EXPECT_EQ(prefix, std::string(reinterpret_cast<const char*>(request.data()) -
                                  prefix_size, prefix_size));
    EXPECT_EQ(original, Bytes(request));
    EXPECT_EQ(original, first_snapshot.substr(36, request.size()));
    EXPECT_EQ(original, second_snapshot.substr(36, request.size()));
}

class RejectingAuthenticator : public brpc::Authenticator {
public:
    int GenerateCredential(std::string* credential) const override {
        ++generated;
        *credential = "test-credential";
        return 0;
    }
    int VerifyCredential(const std::string&, const butil::EndPoint&,
                         brpc::AuthContext*) const override {
        return -1;
    }
    mutable std::atomic<int> generated{0};
};

TEST(FlatBuffersSerializationTest, PackRejectsMissingMethodEmptyPayloadAndAuth) {
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "PackErrors", "Echo", {7}}));
    const Message request = MakePayload();
    RejectingAuthenticator auth;
    for (int variant = 0; variant < 3; ++variant) {
        brpc::Controller cntl;
        if (variant != 0) {
            brpc::ControllerPrivateAccessor(&cntl).set_flatbuffers_method(
                descriptor.method(0));
        }
        butil::IOBuf serialized;
        if (variant != 1) {
            serialized.append(Bytes(request));
        }
        butil::IOBuf packed;
        brpc::policy::PackFlatBuffersRequest(&packed, nullptr, 1, nullptr,
            &cntl, serialized, variant == 2 ? &auth : nullptr);
        EXPECT_TRUE(cntl.Failed());
        EXPECT_TRUE(packed.empty());
    }
}

TEST_F(FlatBuffersProtocolTest, SynchronousAndAsynchronousPayloadAttachmentVariants) {
    EXPECT_EQ(30, static_cast<int>(brpc::PROTOCOL_FLATBUFFERS_RPC));
    for (size_t size : {0u, 1u, 63u, 1024u, 8193u, 65537u}) {
        for (bool asynchronous : {false, true}) {
            SCOPED_TRACE(size);
            SCOPED_TRACE(asynchronous);
            const Message request = MakePayload(size, size + 31, size % 19);
            Message response;
            brpc::Controller cntl;
            std::string attachment(size / 3, '\0');
            for (size_t i = 0; i < attachment.size(); ++i) {
                attachment[i] = static_cast<char>(i % 251);
            }
            cntl.request_attachment().append(attachment);
            Completion done;
            channel.FBCallMethod(method(41), &cntl, &request, &response,
                                 asynchronous ? &done : nullptr);
            if (asynchronous) {
                ASSERT_TRUE(done.Wait());
                EXPECT_EQ(1, done.calls());
            }
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(method(41), cntl.flatbuffers_method());
            EXPECT_EQ(brpc::PROTOCOL_FLATBUFFERS_RPC, cntl.request_protocol());
            ExpectReply(response, request, method(41));
            EXPECT_EQ(attachment, cntl.response_attachment().to_string());
        }
    }
}

TEST_F(FlatBuffersProtocolTest, ConcurrentCallsShareOneConstMessageWithoutMutation) {
    Message mutable_request = MakePayload(16385, 9981, 256);
    memset(mutable_request.mutable_buf_begin(), 0x3d,
           mutable_request.get_meta_size());
    const Message request = std::move(mutable_request);
    const std::string original = Bytes(request);
    const size_t prefix_size = request.get_meta_size();
    const std::string prefix(reinterpret_cast<const char*>(request.data()) -
                             prefix_size, prefix_size);
    const MethodDescriptor* echo = method();
    const MethodDescriptor* alternate = method(41);
    std::vector<std::thread> clients;
    for (int client = 0; client < 6; ++client) {
        clients.emplace_back([&, client] {
            for (int call = 0; call < 8; ++call) {
                brpc::Controller cntl;
                Message response;
                const std::string attachment = std::to_string(client) + ":" +
                                               std::to_string(call);
                cntl.request_attachment().append(attachment);
                const MethodDescriptor* selected = call % 2 ? echo : alternate;
                channel.FBCallMethod(selected, &cntl, &request, &response, nullptr);
                EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
                if (!cntl.Failed()) {
                    ExpectReply(response, request, selected);
                    EXPECT_EQ(attachment, cntl.response_attachment().to_string());
                }
            }
        });
    }
    for (auto& client : clients) {
        client.join();
    }
    EXPECT_EQ(48, service.entered.load());
    EXPECT_EQ(original, Bytes(request));
    EXPECT_EQ(prefix_size, request.get_meta_size());
    EXPECT_EQ(prefix, std::string(reinterpret_cast<const char*>(request.data()) -
                                  prefix_size, prefix_size));
}

TEST_F(FlatBuffersProtocolTest, UnknownServiceAndMethodDoNotDispatch) {
    ServiceDescriptor unknown_service;
    ServiceDescriptor unknown_method;
    ASSERT_EQ(0, unknown_service.init(
        {"brpc_fbtest", "MissingService", "Echo", {7}}));
    ASSERT_EQ(0, unknown_method.init(
        {"brpc_fbtest", "ProtocolService", "Missing", {12345}}));
    const Message request = MakePayload();
    for (const MethodDescriptor* missing : {unknown_service.method(0),
                                            unknown_method.method(0)}) {
        Message response;
        brpc::Controller cntl;
        channel.FBCallMethod(missing, &cntl, &request, &response, nullptr);
        EXPECT_EQ(brpc::ENOMETHOD, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_EQ(0u, response.size());
    }
    EXPECT_EQ(0, service.entered.load());
    brpc::Controller cntl;
    Message response;
    channel.FBCallMethod(method(), &cntl, &request, &response, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ExpectReply(response, request, method());
}

TEST_F(FlatBuffersProtocolTest, ServiceVerifiesInvalidPayloadAndRemainsUsable) {
    for (size_t size : {1u, 3u, 4u, 16u, 65u}) {
        butil::IOBuf bytes;
        bytes.append(std::string(size, '\xff'));
        Message invalid;
        ASSERT_TRUE(invalid.parse_msg_from_iobuf(bytes, size, 0));
        ASSERT_FALSE(invalid.Verify<Payload>());
        brpc::Controller cntl;
        Message response;
        channel.FBCallMethod(method(), &cntl, &invalid, &response, nullptr);
        EXPECT_EQ(brpc::EREQUEST, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_EQ(0u, response.size());
    }
    EXPECT_EQ(5, service.rejected.load());
    const Message request = MakePayload();
    Message response;
    brpc::Controller cntl;
    channel.FBCallMethod(method(), &cntl, &request, &response, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ExpectReply(response, request, method());
    EXPECT_EQ(6, service.entered.load());
}

TEST_F(FlatBuffersProtocolTest, AsynchronousValidationErrorsRunCallbackOnce) {
    const Message request = MakePayload();
    brpc::Channel protobuf_channel;
    ASSERT_EQ(0, InitChannel(&protobuf_channel, server.listen_address(),
                             "baidu_std"));
    for (int variant = 0; variant < 4; ++variant) {
        SCOPED_TRACE(variant);
        Message response;
        brpc::Controller cntl;
        Completion done;
        brpc::Channel* selected = variant == 0 ? &protobuf_channel : &channel;
        selected->FBCallMethod(variant == 1 ? nullptr : method(), &cntl,
            variant == 2 ? nullptr : &request,
            variant == 3 ? nullptr : &response, &done);
        ASSERT_TRUE(done.Wait());
        EXPECT_EQ(1, done.calls());
        EXPECT_TRUE(cntl.Failed());
    }
    EXPECT_EQ(0, service.entered.load());
}

TEST_F(FlatBuffersProtocolTest, RejectsProtobufOnLegacyCallbackWithoutDispatch) {
    google::protobuf::DescriptorProto request;
    google::protobuf::DescriptorProto response;
    request.set_name("not-a-flatbuffer");
    brpc::Controller cntl;
    Completion done;
    channel.CallMethod(nullptr, &cntl, &request, &response, &done);
    ASSERT_TRUE(done.Wait());
    EXPECT_EQ(1, done.calls());
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(0, service.entered.load());
}

TEST_F(FlatBuffersProtocolTest, UnsupportedCompressionChecksumAndStreamsFailRpc) {
    const Message request = MakePayload();
    for (int variant = 0; variant < 3; ++variant) {
        brpc::Controller cntl;
        Message response;
        brpc::StreamId stream = brpc::INVALID_STREAM_ID;
        if (variant == 0) {
            cntl.set_request_compress_type(brpc::COMPRESS_TYPE_GZIP);
        } else if (variant == 1) {
            cntl.set_request_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
        } else {
            ASSERT_EQ(0, brpc::StreamCreate(&stream, cntl, nullptr));
        }
        Completion done;
        channel.FBCallMethod(method(), &cntl, &request, &response, &done);
        ASSERT_TRUE(done.Wait());
        EXPECT_EQ(1, done.calls());
        EXPECT_TRUE(cntl.Failed());
        if (stream != brpc::INVALID_STREAM_ID) {
            brpc::StreamClose(stream);
        }
    }
    EXPECT_EQ(0, service.entered.load());
    for (int variant = 0; variant < 2; ++variant) {
        service.response_compressed = variant == 0;
        service.response_checksummed = variant == 1;
        brpc::Controller cntl;
        Message response;
        channel.FBCallMethod(method(), &cntl, &request, &response, nullptr);
        EXPECT_EQ(brpc::ERESPONSE, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_EQ(0u, response.size());
    }
}

TEST_F(FlatBuffersProtocolTest, ClientAuthenticationIsRejected) {
    RejectingAuthenticator auth;
    brpc::ChannelOptions options;
    options.protocol = "fb_rpc";
    options.auth = &auth;
    options.timeout_ms = kRpcTimeoutMs;
    options.max_retry = 0;
    brpc::Channel authenticated;
    ASSERT_EQ(0, authenticated.Init(server.listen_address(), &options));
    const Message request = MakePayload();
    brpc::Controller cntl;
    Message response;
    authenticated.FBCallMethod(method(), &cntl, &request, &response, nullptr);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(0, service.entered.load());
}

TEST(FlatBuffersLifecycleTest, AuthenticationOnServerCannotBeBypassed) {
    PayloadService service;
    RejectingAuthenticator auth;
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    options.auth = &auth;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
    const Message request = MakePayload();
    Message response;
    brpc::Controller cntl;
    channel.FBCallMethod(service.GetDescriptor()->method(0), &cntl, &request,
                         &response, nullptr);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(0, service.entered.load());
    EXPECT_EQ(0, server.Stop(0));
    EXPECT_EQ(0, server.Join());
}

TEST(FlatBuffersLifecycleTest, StableIdsSurviveReorderRemovalAndRestart) {
    PayloadService original;
    PayloadService reordered({"brpc_fbtest", "ProtocolService",
                               "Alternate Echo", {41, 7}});
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &original, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
    const ServiceDescriptor* client_descriptor = original.GetDescriptor();
    const Message request = MakePayload();
    for (int version = 0; version < 2; ++version) {
        brpc::Channel channel;
        ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
        for (int id : {7, 41, 99}) {
            SCOPED_TRACE(version);
            SCOPED_TRACE(id);
            const MethodDescriptor* method = client_descriptor->FindMethodByIndex(id);
            ASSERT_NE(nullptr, method);
            brpc::Controller cntl;
            Message response;
            channel.FBCallMethod(method, &cntl, &request, &response, nullptr);
            if (version == 1 && id == 99) {
                EXPECT_EQ(brpc::ENOMETHOD, cntl.ErrorCode());
            } else {
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                ExpectReply(response, request, method);
            }
        }
        ASSERT_EQ(0, server.Stop(0));
        ASSERT_EQ(0, server.Join());
        if (version == 0) {
            ASSERT_EQ(0, server.RemoveFlatBuffersService(&original));
            EXPECT_EQ(0, server.GetFlatBuffersServiceCount());
            ASSERT_EQ(0, server.AddFlatBuffersService(
                &reordered, brpc::SERVER_DOESNT_OWN_SERVICE));
            EXPECT_EQ(1, server.GetFlatBuffersServiceCount());
            ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
        }
    }
    EXPECT_EQ(3, original.entered.load());
    EXPECT_EQ(2, reordered.entered.load());
    server.ClearServices();
    EXPECT_EQ(0, server.GetFlatBuffersServiceCount());
}

class NullDescriptorService : public brpc::flatbuffers::Service {
public:
    explicit NullDescriptorService(std::atomic<int>* destroyed)
        : _destroyed(destroyed) {}
    ~NullDescriptorService() override { ++*_destroyed; }
    const ServiceDescriptor* GetDescriptor() override { return nullptr; }
    void FBCallMethod(const MethodDescriptor*,
                      google::protobuf::RpcController* controller,
                      const Message*, Message*,
                      google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        controller->SetFailed("Invalid service was dispatched");
        ADD_FAILURE() << "Invalid service was dispatched";
    }
private:
    std::atomic<int>* _destroyed;
};

TEST(FlatBuffersLifecycleTest, OwnershipFailureWrongInstanceRemoveAndClear) {
    std::atomic<int> owned_destroyed{0};
    std::atomic<int> duplicate_destroyed{0};
    std::atomic<int> invalid_destroyed{0};
    std::atomic<int> borrowed_destroyed{0};
    {
        PayloadService borrowed({"brpc_fbtest", "Borrowed", "Echo", {7}},
                                 &borrowed_destroyed);
        brpc::Server server;
        ASSERT_EQ(0, server.AddFlatBuffersService(
            &borrowed, brpc::SERVER_DOESNT_OWN_SERVICE));
        std::unique_ptr<PayloadService> owned(new PayloadService(
            {"brpc_fbtest", "Owned", "Echo", {41}}, &owned_destroyed));
        ASSERT_EQ(0, server.AddFlatBuffersService(
            owned.get(), brpc::SERVER_OWNS_SERVICE));
        owned.release();
        EXPECT_EQ(2, server.GetFlatBuffersServiceCount());
        for (brpc::ServiceOwnership ownership : {brpc::SERVER_OWNS_SERVICE,
                                                 brpc::SERVER_DOESNT_OWN_SERVICE}) {
            std::unique_ptr<PayloadService> duplicate(new PayloadService(
                {"brpc_fbtest", "Owned", "Echo", {41}}, &duplicate_destroyed));
            const int before = duplicate_destroyed.load();
            EXPECT_NE(0, server.AddFlatBuffersService(duplicate.get(), ownership));
            EXPECT_EQ(before, duplicate_destroyed.load());
            EXPECT_NE(0, server.RemoveFlatBuffersService(duplicate.get()));
            EXPECT_EQ(2, server.GetFlatBuffersServiceCount());
            std::unique_ptr<NullDescriptorService> invalid(
                new NullDescriptorService(&invalid_destroyed));
            const int invalid_before = invalid_destroyed.load();
            EXPECT_NE(0, server.AddFlatBuffersService(invalid.get(), ownership));
            EXPECT_EQ(invalid_before, invalid_destroyed.load());
        }
        EXPECT_EQ(2, duplicate_destroyed.load());
        EXPECT_EQ(2, invalid_destroyed.load());
        EXPECT_NE(0, server.AddFlatBuffersService(nullptr, brpc::SERVER_OWNS_SERVICE));
        EXPECT_NE(0, server.RemoveFlatBuffersService(nullptr));
        ASSERT_EQ(0, server.RemoveFlatBuffersService(&borrowed));
        EXPECT_EQ(0, borrowed_destroyed.load());
        EXPECT_EQ(1, server.GetFlatBuffersServiceCount());
        ASSERT_EQ(0, server.AddFlatBuffersService(
            &borrowed, brpc::SERVER_DOESNT_OWN_SERVICE));
        server.ClearServices();
        EXPECT_EQ(0, server.GetFlatBuffersServiceCount());
        EXPECT_EQ(1, owned_destroyed.load());
        EXPECT_EQ(0, borrowed_destroyed.load());
        server.ClearServices();
        EXPECT_EQ(1, owned_destroyed.load());
    }
    EXPECT_EQ(1, borrowed_destroyed.load());
}

bool WaitForServerConcurrency(const brpc::Server& server, int expected) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kWaitTimeoutMs);
    do {
        if (server.Concurrency() == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return server.Concurrency() == expected;
}

class RejectAllInterceptor : public brpc::Interceptor {
public:
    bool Accept(const brpc::Controller* cntl, int& error_code,
                std::string& error_text) const override {
        ++calls;
        EXPECT_EQ(brpc::PROTOCOL_FLATBUFFERS_RPC, cntl->request_protocol());
        EXPECT_NE(nullptr, cntl->flatbuffers_method());
        error_code = EACCES;
        error_text = "Rejected by test interceptor";
        return false;
    }
    mutable std::atomic<int> calls{0};
};

TEST(FlatBuffersLifecycleTest, InterceptorRejectsBeforeServiceAndReleasesConcurrency) {
    PayloadService service;
    RejectAllInterceptor interceptor;
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    options.interceptor = &interceptor;
    options.max_concurrency = 1;
    options.method_max_concurrency = 1;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
    const Message request = MakePayload();
    for (int call = 0; call < 3; ++call) {
        brpc::Controller cntl;
        Message response;
        channel.FBCallMethod(service.GetDescriptor()->method(0), &cntl,
                             &request, &response, nullptr);
        EXPECT_EQ(EACCES, cntl.ErrorCode()) << cntl.ErrorText();
        EXPECT_EQ(call + 1, interceptor.calls.load());
        EXPECT_EQ(0, service.entered.load());
        EXPECT_EQ(0u, response.size());
        EXPECT_TRUE(WaitForServerConcurrency(server, 0));
    }
}

TEST(FlatBuffersLifecycleTest, PublicControllerSecurityModeMatchesServerOptions) {
    for (bool security_mode : {false, true}) {
        SCOPED_TRACE(security_mode);
        PayloadService service;
        brpc::Server server;
        ASSERT_EQ(0, server.AddFlatBuffersService(
            &service, brpc::SERVER_DOESNT_OWN_SERVICE));
        brpc::ServerOptions options;
        options.has_builtin_services = !security_mode;
        ASSERT_EQ(security_mode, options.security_mode());
        ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));
        brpc::Channel channel;
        ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
        const Message request = MakePayload();
        brpc::Controller cntl;
        Message response;
        const MethodDescriptor* method = service.GetDescriptor()->method(0);
        channel.FBCallMethod(method, &cntl, &request, &response, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ExpectReply(response, request, method);
        EXPECT_EQ(1, service.entered.load());
        EXPECT_EQ(security_mode ? 1 : 0, service.security_mode_calls.load());
    }
}

int StartWithEphemeralInternalPort(brpc::Server* server,
                                   brpc::ServerOptions* options) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        butil::EndPoint endpoint;
        if (butil::str2endpoint("127.0.0.1:0", &endpoint) != 0) {
            return -1;
        }
        butil::fd_guard reserved(butil::tcp_listen(endpoint));
        if (reserved < 0 || butil::get_local_side(reserved, &endpoint) != 0) {
            continue;
        }
        options->internal_port = endpoint.port;
        // internal_port does not support 0. Retry if its ephemeral reservation
        // is taken between releasing it and starting the two server listeners.
        reserved.reset(-1);
        if (server->Start("127.0.0.1:0", options) == 0) {
            return 0;
        }
    }
    return -1;
}

TEST(FlatBuffersLifecycleTest, InternalPortRejectsOrdinaryFlatBuffersServices) {
    PayloadService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    ASSERT_EQ(0, StartWithEphemeralInternalPort(&server, &options));
    ASSERT_TRUE(server.options().security_mode());
    const butil::EndPoint internal_endpoint(server.listen_address().ip,
                                             options.internal_port);
    brpc::Channel internal_channel;
    brpc::Channel public_channel;
    ASSERT_EQ(0, InitChannel(&internal_channel, internal_endpoint));
    ASSERT_EQ(0, InitChannel(&public_channel, server.listen_address()));
    const Message request = MakePayload();
    const MethodDescriptor* method = service.GetDescriptor()->method(0);
    brpc::Controller rejected;
    Message rejected_response;
    internal_channel.FBCallMethod(method, &rejected, &request,
                                  &rejected_response, nullptr);
    EXPECT_EQ(EPERM, rejected.ErrorCode()) << rejected.ErrorText();
    EXPECT_EQ(0, service.entered.load());
    EXPECT_EQ(0u, rejected_response.size());

    brpc::Controller accepted;
    Message response;
    public_channel.FBCallMethod(method, &accepted, &request, &response, nullptr);
    ASSERT_FALSE(accepted.Failed()) << accepted.ErrorText();
    ExpectReply(response, request, method);
    EXPECT_EQ(1, service.entered.load());
    EXPECT_EQ(1, service.security_mode_calls.load());
}

void ExerciseMethodConcurrency(bool specific_limit) {
    PayloadService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &service, brpc::SERVER_DOESNT_OWN_SERVICE));
    const MethodDescriptor* echo = service.GetDescriptor()->FindMethodByIndex(7);
    if (specific_limit) {
        server.MaxConcurrencyOf(echo->full_name()) = 1;
    }
    brpc::ServerOptions options;
    options.max_concurrency = 8;
    options.method_max_concurrency = specific_limit ? 2 : 1;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, server.listen_address()));
    const Message request = MakePayload();
    for (int id : {7, 41}) {
        SCOPED_TRACE(id);
        const MethodDescriptor* method = service.GetDescriptor()->FindMethodByIndex(id);
        const int limit = specific_limit && id == 41 ? 2 : 1;
        const brpc::Server& const_server = server;
        EXPECT_EQ(specific_limit && id == 7 ? 1 : 0,
                  const_server.MaxConcurrencyOf(method->full_name()));
        const auto* property = brpc::ServerPrivateAccessor(&server).
            FindFlatBuffersMethodPropertyByIndex(method->service()->index(), id);
        ASSERT_NE(nullptr, property);
        ASSERT_NE(nullptr, property->status);
        EXPECT_EQ(limit, property->status->MaxConcurrency());
        const int entered_before = service.entered.load();
        service.HoldNext();
        brpc::Controller held_cntl;
        held_cntl.set_timeout_ms(kWaitTimeoutMs);
        Message held_response;
        Completion held_done;
        channel.FBCallMethod(method, &held_cntl, &request, &held_response, &held_done);
        const bool held = service.WaitHeld();
        EXPECT_TRUE(held);
        // No fatal assertions while a service completion is held: all exits
        // below must release it before the server's destructor calls Join().
        if (held) {
            EXPECT_TRUE(WaitForServerConcurrency(server, 1));
            for (int attempt = 0; attempt < 2; ++attempt) {
                brpc::Controller contender;
                Message contender_response;
                channel.FBCallMethod(method, &contender, &request,
                                     &contender_response, nullptr);
                if (limit == 1) {
                    EXPECT_EQ(brpc::ELIMIT, contender.ErrorCode())
                        << contender.ErrorText();
                    EXPECT_EQ(entered_before + 1, service.entered.load());
                    EXPECT_EQ(0u, contender_response.size());
                } else {
                    EXPECT_FALSE(contender.Failed()) << contender.ErrorText();
                    if (!contender.Failed()) {
                        ExpectReply(contender_response, request, method);
                    }
                    EXPECT_EQ(entered_before + attempt + 2, service.entered.load());
                }
                EXPECT_TRUE(WaitForServerConcurrency(server, 1));
            }
        }
        service.ReleaseHeld();
        const bool completed = held_done.Wait();
        EXPECT_TRUE(completed);
        EXPECT_EQ(1, held_done.calls());
        EXPECT_FALSE(held_cntl.Failed()) << held_cntl.ErrorText();
        if (completed && !held_cntl.Failed()) {
            ExpectReply(held_response, request, method);
        }
        EXPECT_TRUE(WaitForServerConcurrency(server, 0));
        brpc::Controller recovered;
        Message recovered_response;
        channel.FBCallMethod(method, &recovered, &request, &recovered_response, nullptr);
        EXPECT_FALSE(recovered.Failed()) << recovered.ErrorText();
        if (!recovered.Failed()) {
            ExpectReply(recovered_response, request, method);
        }
        EXPECT_TRUE(WaitForServerConcurrency(server, 0));
    }
}

TEST(FlatBuffersLifecycleTest, DefaultMethodConcurrencyRecoversAfterRejection) {
    ExerciseMethodConcurrency(false);
}

TEST(FlatBuffersLifecycleTest, SpecificMethodConcurrencyOverridesDefaultAndRecovers) {
    ExerciseMethodConcurrency(true);
}

class RetryOnlyAccessDenied : public brpc::RetryPolicy {
public:
    bool DoRetry(const brpc::Controller* cntl) const override {
        if (cntl->ErrorCode() == EACCES) {
            ++retries;
            return true;
        }
        return false;
    }
    mutable std::atomic<int> retries{0};
};

TEST_F(FlatBuffersProtocolTest, RealNetworkRetryPreservesConstRequestAndAttachment) {
    RetryOnlyAccessDenied policy;
    brpc::ChannelOptions options;
    options.protocol = "fb_rpc";
    options.connection_type = "single";
    options.timeout_ms = kRpcTimeoutMs;
    options.connect_timeout_ms = kRpcTimeoutMs;
    options.max_retry = 2;
    options.retry_policy = &policy;
    brpc::Channel retry_channel;
    ASSERT_EQ(0, retry_channel.Init(server.listen_address(), &options));
    Message mutable_request = MakePayload(8193, 7654, 19);
    memset(mutable_request.mutable_buf_begin(), 0x69,
           mutable_request.get_meta_size());
    const Message request = std::move(mutable_request);
    const std::string original = Bytes(request);
    const size_t prefix_size = request.get_meta_size();
    const std::string prefix(reinterpret_cast<const char*>(request.data()) -
                             prefix_size, prefix_size);
    const std::string attachment("\0retry\xff", 7);
    service.fail_next_code = EACCES;
    brpc::Controller cntl;
    cntl.request_attachment().append(attachment);
    Message response;
    Completion done;
    retry_channel.FBCallMethod(method(41), &cntl, &request, &response, &done);
    ASSERT_TRUE(done.Wait());
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(1, done.calls());
    EXPECT_EQ(1, cntl.retried_count());
    EXPECT_EQ(1, policy.retries.load());
    EXPECT_EQ(2, service.entered.load());
    ExpectReply(response, request, method(41));
    EXPECT_EQ(attachment, cntl.response_attachment().to_string());
    EXPECT_EQ(original, Bytes(request));
    EXPECT_EQ(prefix_size, request.get_meta_size());
    EXPECT_EQ(prefix, std::string(reinterpret_cast<const char*>(request.data()) -
                                  prefix_size, prefix_size));

    // The same policy must not retry a different application error.
    service.fail_next_code = brpc::EREQUEST;
    brpc::Controller no_retry;
    Message no_retry_response;
    retry_channel.FBCallMethod(method(), &no_retry, &request,
                               &no_retry_response, nullptr);
    EXPECT_EQ(brpc::EREQUEST, no_retry.ErrorCode());
    EXPECT_EQ(0, no_retry.retried_count());
    EXPECT_EQ(1, policy.retries.load());
    EXPECT_EQ(3, service.entered.load());
}

TEST_F(FlatBuffersProtocolTest, PooledAndShortConnectionsSupportSyncAndAsyncCalls) {
    for (const char* connection_type : {"pooled", "short"}) {
        SCOPED_TRACE(connection_type);
        brpc::Channel selected;
        ASSERT_EQ(0, InitChannel(&selected, server.listen_address(),
                                 "fb_rpc", connection_type));
        for (size_t size : {0u, 8193u}) {
            for (bool asynchronous : {false, true}) {
                const Message request = MakePayload(size, size + 23, 7);
                brpc::Controller cntl;
                cntl.request_attachment().append("connection-attachment");
                Message response;
                Completion done;
                selected.FBCallMethod(method(), &cntl, &request, &response,
                                      asynchronous ? &done : nullptr);
                if (asynchronous) {
                    ASSERT_TRUE(done.Wait());
                    EXPECT_EQ(1, done.calls());
                }
                ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
                const brpc::ConnectionType expected_type =
                    std::string(connection_type) == "pooled" ?
                        brpc::CONNECTION_TYPE_POOLED : brpc::CONNECTION_TYPE_SHORT;
                EXPECT_EQ(expected_type, cntl.connection_type());
                ExpectReply(response, request, method());
                EXPECT_EQ("connection-attachment",
                          cntl.response_attachment().to_string());
            }
        }
    }
    EXPECT_EQ(8, service.entered.load());
}

struct DeferredJoinState {
    ~DeferredJoinState() { service.ReleaseHeld(); }
    PayloadService service;
    Message request = MakePayload();
    Message response;
    brpc::Controller cntl;
    Completion done;
    // Destroy the server before the request/controller/service on early exits.
    brpc::Server server;
    std::mutex mutex;
    std::condition_variable condition;
    bool join_started = false;
    bool joined = false;
    int join_result = -1;
};

TEST(FlatBuffersLifecycleTest, StopRejectsRemovalUntilDeferredDoneAndJoin) {
    auto state = std::make_shared<DeferredJoinState>();
    ASSERT_EQ(0, state->server.AddFlatBuffersService(
        &state->service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, state->server.Start("127.0.0.1:0", nullptr));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, state->server.listen_address()));
    const MethodDescriptor* method = state->service.GetDescriptor()->method(0);
    state->service.HoldNext();
    state->cntl.set_timeout_ms(kWaitTimeoutMs);
    channel.FBCallMethod(method, &state->cntl, &state->request,
                         &state->response, &state->done);
    EXPECT_TRUE(state->service.WaitHeld());
    EXPECT_EQ(0, state->done.calls());
    EXPECT_NE(0, state->server.RemoveFlatBuffersService(&state->service));
    EXPECT_EQ(0, state->server.Stop(0));
    EXPECT_NE(0, state->server.RemoveFlatBuffersService(&state->service));
    EXPECT_EQ(1, state->server.GetFlatBuffersServiceCount());

    std::thread joiner([state] {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->join_started = true;
            state->condition.notify_all();
        }
        const int result = state->server.Join();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->join_result = result;
            state->joined = true;
            state->condition.notify_all();
        }
    });
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        EXPECT_TRUE(state->condition.wait_for(
            lock, std::chrono::milliseconds(kWaitTimeoutMs),
            [&state] { return state->join_started; }));
        EXPECT_FALSE(state->condition.wait_for(
            lock, std::chrono::milliseconds(100),
            [&state] { return state->joined; }));
    }
    EXPECT_EQ(0, state->done.calls());
    state->service.ReleaseHeld();
    const bool completed = state->done.Wait();
    EXPECT_TRUE(completed);
    EXPECT_EQ(1, state->done.calls());
    EXPECT_FALSE(state->cntl.Failed()) << state->cntl.ErrorText();
    if (completed && !state->cntl.Failed()) {
        ExpectReply(state->response, state->request, method);
    }
    bool joined;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        joined = state->condition.wait_for(
            lock, std::chrono::milliseconds(kWaitTimeoutMs),
            [&state] { return state->joined; });
    }
    if (!joined) {
        // A broken Join must fail rather than hang the suite. The thread owns
        // every object it may still access, so detaching cannot use dead locals.
        joiner.detach();
        FAIL() << "Server::Join did not return after deferred done completed";
    }
    joiner.join();
    EXPECT_EQ(0, state->join_result);
    ASSERT_EQ(0, state->server.RemoveFlatBuffersService(&state->service));
    EXPECT_EQ(0, state->server.GetFlatBuffersServiceCount());
}

// A normal protobuf service built from descriptors avoids another generated
// test proto while exercising the actual baidu_std serializer and dispatcher.
class DynamicEchoService : public google::protobuf::Service {
public:
    DynamicEchoService() {
        google::protobuf::FileDescriptorProto file;
        file.set_name("flatbuffers_protocol_pb_compat.proto");
        file.set_package("brpc_fbtest_pb");
        auto* message = file.add_message_type();
        message->set_name("Payload");
        auto* field = message->add_field();
        field->set_name("text");
        field->set_number(1);
        field->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
        field->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
        auto* service = file.add_service();
        service->set_name("EchoService");
        auto* method = service->add_method();
        method->set_name("Echo");
        method->set_input_type(".brpc_fbtest_pb.Payload");
        method->set_output_type(".brpc_fbtest_pb.Payload");
        const auto* built = _pool.BuildFile(file);
        if (built) {
            _descriptor = built->service(0);
            _prototype = _factory.GetPrototype(built->message_type(0));
        }
    }
    const google::protobuf::ServiceDescriptor* GetDescriptor() override {
        return _descriptor;
    }
    const google::protobuf::Message& GetRequestPrototype(
        const google::protobuf::MethodDescriptor*) const override {
        return *_prototype;
    }
    const google::protobuf::Message& GetResponsePrototype(
        const google::protobuf::MethodDescriptor*) const override {
        return *_prototype;
    }
    void CallMethod(const google::protobuf::MethodDescriptor*,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        auto* cntl = static_cast<brpc::Controller*>(controller);
        EXPECT_EQ(nullptr, cntl->flatbuffers_method());
        response->CopyFrom(*request);
    }
private:
    google::protobuf::DescriptorPool _pool;
    google::protobuf::DynamicMessageFactory _factory;
    const google::protobuf::ServiceDescriptor* _descriptor = nullptr;
    const google::protobuf::Message* _prototype = nullptr;
};

TEST(FlatBuffersLifecycleTest, ControllerResetClearsContextBeforeOrdinaryProtobufRpc) {
    PayloadService flatbuffers_service;
    DynamicEchoService protobuf_service;
    ASSERT_NE(nullptr, protobuf_service.GetDescriptor());
    brpc::Server server;
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &flatbuffers_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.AddService(&protobuf_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
    brpc::Channel fb_channel;
    brpc::Channel pb_channel;
    ASSERT_EQ(0, InitChannel(&fb_channel, server.listen_address()));
    ASSERT_EQ(0, InitChannel(&pb_channel, server.listen_address(), "baidu_std"));
    const MethodDescriptor* fb_method = flatbuffers_service.GetDescriptor()->method(0);
    const Message fb_request = MakePayload();
    Message fb_response;
    brpc::Controller cntl;
    fb_channel.FBCallMethod(fb_method, &cntl, &fb_request, &fb_response, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(fb_method, cntl.flatbuffers_method());
    cntl.Reset();
    EXPECT_EQ(nullptr, cntl.flatbuffers_method());
    EXPECT_EQ(nullptr, cntl.method());
    const auto* method = protobuf_service.GetDescriptor()->method(0);
    std::unique_ptr<google::protobuf::Message> request(
        protobuf_service.GetRequestPrototype(method).New());
    std::unique_ptr<google::protobuf::Message> response(
        protobuf_service.GetResponsePrototype(method).New());
    const auto* field = request->GetDescriptor()->FindFieldByName("text");
    ASSERT_NE(nullptr, field);
    request->GetReflection()->SetString(request.get(), field, "ordinary protobuf");
    pb_channel.CallMethod(method, &cntl, request.get(), response.get(), nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(nullptr, cntl.flatbuffers_method());
    EXPECT_EQ(method, cntl.method());
    EXPECT_EQ("ordinary protobuf",
              response->GetReflection()->GetString(*response, field));
    EXPECT_EQ(0, server.Stop(0));
    EXPECT_EQ(0, server.Join());
}

TEST(FlatBuffersLifecycleTest, AuthenticatedProtobufSocketCannotBypassFlatBuffersAuthCheck) {
    PayloadService flatbuffers_service;
    DynamicEchoService protobuf_service;
    RejectingAuthenticator auth;
    brpc::Server server;
    ASSERT_NE(nullptr, protobuf_service.GetDescriptor());
    ASSERT_EQ(0, server.AddFlatBuffersService(
        &flatbuffers_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.AddService(&protobuf_service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.auth = &auth;
    options.timeout_ms = kRpcTimeoutMs;
    options.connect_timeout_ms = kRpcTimeoutMs;
    options.max_retry = 0;
    brpc::Channel pb_channel;
    brpc::Channel fb_channel;
    ASSERT_EQ(0, pb_channel.Init(server.listen_address(), &options));
    // The socket pool key includes the auth pointer, but not the protocol.
    options.protocol = "fb_rpc";
    ASSERT_EQ(0, fb_channel.Init(server.listen_address(), &options));
    const auto* pb_method = protobuf_service.GetDescriptor()->method(0);
    std::unique_ptr<google::protobuf::Message> pb_request(
        protobuf_service.GetRequestPrototype(pb_method).New());
    std::unique_ptr<google::protobuf::Message> pb_response(
        protobuf_service.GetResponsePrototype(pb_method).New());
    const auto* field = pb_request->GetDescriptor()->FindFieldByName("text");
    ASSERT_NE(nullptr, field);
    pb_request->GetReflection()->SetString(pb_request.get(), field, "prime auth socket");
    brpc::Controller first_pb;
    pb_channel.CallMethod(pb_method, &first_pb, pb_request.get(),
                          pb_response.get(), nullptr);
    ASSERT_FALSE(first_pb.Failed()) << first_pb.ErrorText();
    ASSERT_EQ(1, auth.generated.load());
    ASSERT_GT(first_pb.local_side().port, 0);
    const butil::EndPoint first_connection = first_pb.local_side();
    brpc::ServerStatistics statistics;
    server.GetStat(&statistics);
    ASSERT_EQ(1u, statistics.connection_count);

    const Message request = MakePayload();
    Message response;
    brpc::Controller rejected;
    Completion done;
    fb_channel.FBCallMethod(flatbuffers_service.GetDescriptor()->method(0),
                            &rejected, &request, &response, &done);
    ASSERT_TRUE(done.Wait());
    EXPECT_EQ(1, done.calls());
    EXPECT_TRUE(rejected.Failed())
        << "Previously authenticated socket bypassed FRPC auth check";
    EXPECT_EQ(0u, response.size());
    EXPECT_EQ(0, flatbuffers_service.entered.load());
    EXPECT_EQ(1, auth.generated.load());

    // Verify the primed connection stayed usable and did not authenticate again.
    brpc::Controller second_pb;
    pb_response->Clear();
    pb_channel.CallMethod(pb_method, &second_pb, pb_request.get(),
                          pb_response.get(), nullptr);
    ASSERT_FALSE(second_pb.Failed()) << second_pb.ErrorText();
    EXPECT_EQ(first_connection, second_pb.local_side());
    EXPECT_EQ("prime auth socket",
              pb_response->GetReflection()->GetString(*pb_response, field));
    EXPECT_EQ(1, auth.generated.load());
    server.GetStat(&statistics);
    EXPECT_EQ(1u, statistics.connection_count);
}

// Raw peers use both an absolute deadline and socket timeouts; a regression
// must fail a test instead of leaving accept/read/write blocked indefinitely.
bool ConfigureSocket(int fd) {
    struct timeval timeout = {kRpcTimeoutMs / 1000,
                              (kRpcTimeoutMs % 1000) * 1000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return false;
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        return false;
    }
#endif
    return true;
}

bool WaitFd(int fd, short events,
            const std::chrono::steady_clock::time_point& deadline) {
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            return false;
        }
        struct pollfd descriptor = {fd, events, 0};
        const int rc = poll(&descriptor, 1, static_cast<int>(remaining));
        if (rc > 0) {
            return (descriptor.revents & (events | POLLHUP | POLLERR)) != 0;
        }
        if (rc == 0 || errno != EINTR) {
            return false;
        }
    }
}

bool ReadExactly(int fd, char* bytes, size_t size) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kWaitTimeoutMs);
    size_t offset = 0;
    while (offset < size && WaitFd(fd, POLLIN, deadline)) {
        const ssize_t count = recv(fd, bytes + offset, size - offset, 0);
        if (count > 0) {
            offset += count;
        } else if (count == 0 || errno != EINTR) {
            return false;
        }
    }
    return offset == size;
}

bool WriteExactly(int fd, const std::string& bytes) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kWaitTimeoutMs);
    size_t offset = 0;
    while (offset < bytes.size() && WaitFd(fd, POLLOUT, deadline)) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const ssize_t count = send(fd, bytes.data() + offset,
                                   bytes.size() - offset, flags);
        if (count > 0) {
            offset += count;
        } else if (count == 0 || errno != EINTR) {
            return false;
        }
    }
    return offset == bytes.size();
}

bool ReadFrame(int fd, std::string* meta, std::string* payload) {
    std::string header(kHeaderSize, '\0');
    if (!ReadExactly(fd, &header[0], header.size()) ||
        header.compare(0, 4, "FRPC") != 0) {
        return false;
    }
    const uint32_t body_size = Get32(header, 4, true);
    const uint32_t meta_size = Get32(header, 8, true);
    if (meta_size > body_size || body_size > 1024 * 1024) {
        return false;
    }
    std::string body(body_size, '\0');
    if (body_size && !ReadExactly(fd, &body[0], body.size())) {
        return false;
    }
    *meta = body.substr(0, meta_size);
    *payload = body.substr(meta_size);
    return true;
}

TEST_F(FlatBuffersProtocolTest, ExtendedRequestMetadataTraversesRealSocket) {
    const Message request = MakePayload(8193, 7123, 17);
    const std::string attachment("\0binary\xff", 8);
    std::string meta = RequestMeta(method(41), request.size(), attachment.size(),
                                   0x1122334455667788ULL);
    meta.append("unknown future fields", 21);
    butil::fd_guard fd(butil::tcp_connect(server.listen_address(), nullptr,
                                         kRpcTimeoutMs));
    ASSERT_GE(static_cast<int>(fd), 0);
    ASSERT_TRUE(ConfigureSocket(fd));
    const std::string wire = Frame(meta, Bytes(request) + attachment);
    ASSERT_TRUE(WriteExactly(fd, wire.substr(0, 5)));
    ASSERT_TRUE(WriteExactly(fd, wire.substr(5, 13)));
    ASSERT_TRUE(WriteExactly(fd, wire.substr(18)));
    std::string reply_meta;
    std::string reply_payload;
    ASSERT_TRUE(ReadFrame(fd, &reply_meta, &reply_payload));
    ASSERT_GE(reply_meta.size(), kResponseMetaSize);
    EXPECT_EQ(0u, Get32(reply_meta, 0));
    EXPECT_EQ(0x1122334455667788ULL, Get64(reply_meta, 12));
    const uint32_t message_size = Get32(reply_meta, 4);
    ASSERT_EQ(reply_payload.size(), message_size + attachment.size());
    EXPECT_EQ(attachment.size(), Get32(reply_meta, 8));
    EXPECT_EQ(attachment, reply_payload.substr(message_size));
    butil::IOBuf body;
    body.append(reply_payload.data(), message_size);
    Message response;
    ASSERT_TRUE(response.parse_msg_from_iobuf(body, message_size, 0));
    ExpectReply(response, request, method(41));
}

TEST_F(FlatBuffersProtocolTest, MalformedRequestLengthsFailBeforeServiceDispatch) {
    const Message request = MakePayload();
    const std::string payload = Bytes(request) + "att";
    for (int variant = 0; variant < 6; ++variant) {
        SCOPED_TRACE(variant);
        std::string meta = RequestMeta(method(), request.size(), 3, 991 + variant);
        switch (variant) {
        case 0: Put32(&meta, 8, 0); break;
        case 1: Put32(&meta, 8, 0xffffffffu); break;
        case 2: Put32(&meta, 12, 0xffffffffu); break;
        case 3: Put32(&meta, 8, request.size() + 1); break;
        case 4: Put32(&meta, 12, 2); break;
        case 5: Put32(&meta, 4, 0xffffffffu); break;
        }
        butil::fd_guard fd(butil::tcp_connect(server.listen_address(), nullptr,
                                             kRpcTimeoutMs));
        ASSERT_GE(static_cast<int>(fd), 0);
        ASSERT_TRUE(ConfigureSocket(fd));
        ASSERT_TRUE(WriteExactly(fd, Frame(meta, payload)));
        std::string reply_meta;
        std::string reply_payload;
        ASSERT_TRUE(ReadFrame(fd, &reply_meta, &reply_payload));
        ASSERT_GE(reply_meta.size(), kResponseMetaSize);
        EXPECT_EQ(static_cast<uint32_t>(variant == 5 ? brpc::ENOMETHOD :
                                                       brpc::EREQUEST),
                  Get32(reply_meta, 0));
        EXPECT_EQ(static_cast<uint64_t>(991 + variant), Get64(reply_meta, 12));
        EXPECT_EQ(0u, Get32(reply_meta, 4));
        EXPECT_EQ(0u, Get32(reply_meta, 8));
        EXPECT_TRUE(reply_payload.empty());
    }
    EXPECT_EQ(0, service.entered.load());
    brpc::Controller cntl;
    Message response;
    channel.FBCallMethod(method(), &cntl, &request, &response, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ExpectReply(response, request, method());
}

TEST_F(FlatBuffersProtocolTest, TruncatedMetadataAndImpossibleHeaderCloseConnection) {
    std::vector<std::string> frames;
    for (size_t size : {0u, 1u, 8u, 19u, 23u}) {
        frames.push_back(Frame(std::string(size, '\xff'), "invalid"));
    }
    frames.push_back(Header(0, 24));
    const Message warmup_request = MakePayload();
    int warmups = 0;
    for (const auto& wire : frames) {
        SCOPED_TRACE(wire.size());
        butil::fd_guard fd(butil::tcp_connect(server.listen_address(), nullptr,
                                             kRpcTimeoutMs));
        ASSERT_GE(static_cast<int>(fd), 0);
        ASSERT_TRUE(ConfigureSocket(fd));
        // NSHEAD precedes FRPC during initial multi-protocol discovery and
        // needs 28 bytes. First establish FRPC on this exact connection.
        const uint64_t correlation_id = 9001 + warmups;
        ASSERT_TRUE(WriteExactly(fd, Frame(
            RequestMeta(method(), warmup_request.size(), 0, correlation_id),
            Bytes(warmup_request))));
        std::string reply_meta;
        std::string reply_payload;
        ASSERT_TRUE(ReadFrame(fd, &reply_meta, &reply_payload));
        ASSERT_GE(reply_meta.size(), kResponseMetaSize);
        ASSERT_EQ(0u, Get32(reply_meta, 0));
        EXPECT_EQ(correlation_id, Get64(reply_meta, 12));
        ASSERT_EQ(reply_payload.size(), Get32(reply_meta, 4));
        EXPECT_EQ(0u, Get32(reply_meta, 8));
        butil::IOBuf reply_bytes;
        reply_bytes.append(reply_payload);
        Message warmup_response;
        ASSERT_TRUE(warmup_response.parse_msg_from_iobuf(
            reply_bytes, reply_payload.size(), 0));
        ExpectReply(warmup_response, warmup_request, method());
        EXPECT_EQ(++warmups, service.entered.load());

        ASSERT_TRUE(WriteExactly(fd, wire));
        char byte;
        ssize_t count;
        do {
            count = recv(fd, &byte, 1, 0);
        } while (count < 0 && errno == EINTR);
        // A receive timeout is not evidence that the server rejected the frame.
        EXPECT_TRUE(count == 0 || (count < 0 && errno == ECONNRESET))
            << "recv=" << count << " errno=" << errno;
        EXPECT_EQ(warmups, service.entered.load());
    }
    EXPECT_EQ(static_cast<int>(frames.size()), service.entered.load());
}

class ScriptedPeer {
public:
    using Reply = std::function<std::string(const std::string&, const std::string&)>;
    bool Start(Reply reply) {
        butil::EndPoint endpoint;
        if (butil::str2endpoint("127.0.0.1:0", &endpoint) != 0) {
            return false;
        }
        _listener.reset(butil::tcp_listen(endpoint));
        if (_listener < 0 || butil::get_local_side(_listener, &_endpoint) != 0) {
            return false;
        }
        _thread = std::thread([this, reply] {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(kWaitTimeoutMs);
            if (!WaitFd(_listener, POLLIN, deadline)) {
                return;
            }
            butil::fd_guard connection(accept(_listener, nullptr, nullptr));
            if (connection < 0 || !ConfigureSocket(connection)) {
                return;
            }
            std::string request_meta;
            std::string request_payload;
            if (!ReadFrame(connection, &request_meta, &request_payload) ||
                request_meta.size() < kRequestMetaSize) {
                return;
            }
            _served = WriteExactly(connection, reply(request_meta, request_payload));
            // Do not let EOF accidentally turn an accepted malformed response
            // into an RPC failure. Keep the connection open until the assertion.
            std::unique_lock<std::mutex> lock(_mutex);
            _condition.wait_for(lock, std::chrono::milliseconds(kWaitTimeoutMs),
                                [this] { return _release; });
        });
        return true;
    }
    ~ScriptedPeer() { Join(); }
    void Join() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _release = true;
            _condition.notify_all();
        }
        if (_thread.joinable()) {
            _thread.join();
        }
    }
    butil::EndPoint endpoint() const { return _endpoint; }
    bool served() const { return _served.load(); }
private:
    butil::fd_guard _listener;
    butil::EndPoint _endpoint;
    std::thread _thread;
    std::atomic<bool> _served{false};
    std::mutex _mutex;
    std::condition_variable _condition;
    bool _release = false;
};

TEST(FlatBuffersResponseTest, ExtendedResponseMetadataUsesDeclaredPayloadOffset) {
    ScriptedPeer peer;
    ASSERT_TRUE(peer.Start([](const std::string& request_meta,
                              const std::string& request_payload) {
        const uint32_t message_size = Get32(request_meta, 8);
        const uint32_t attachment_size = Get32(request_meta, 12);
        std::string meta = ResponseMeta(message_size, attachment_size,
                                        Get64(request_meta, 16));
        meta.append("\xff\x01\0\x7f\x03", 5);
        return Frame(meta, request_payload);
    }));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, peer.endpoint()));
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "Peer", "Echo", {7}}));
    const Message request = MakePayload(8193);
    Message response;
    brpc::Controller cntl;
    cntl.request_attachment().append("\0tail", 5);
    channel.FBCallMethod(descriptor.method(0), &cntl, &request, &response, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_TRUE(response.Verify<Payload>());
    EXPECT_EQ(Bytes(request), Bytes(response));
    EXPECT_EQ(std::string("\0tail", 5), cntl.response_attachment().to_string());
    peer.Join();
    EXPECT_TRUE(peer.served());
}

TEST(FlatBuffersResponseTest, MalformedResponseFramesFailRpcNotMerelySchemaVerification) {
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "Peer", "Echo", {41}}));
    const Message request = MakePayload();
    for (int variant = 0; variant < 12; ++variant) {
        SCOPED_TRACE(variant);
        ScriptedPeer peer;
        ASSERT_TRUE(peer.Start([variant](const std::string& request_meta,
                                        const std::string& request_payload) {
            std::string meta = ResponseMeta(Get32(request_meta, 8), 0,
                                            Get64(request_meta, 16));
            std::string payload = request_payload;
            switch (variant) {
            case 0: meta.clear(); break;
            case 1: meta.resize(1); break;
            case 2: meta.resize(19); break;
            case 3: Put32(&meta, 4, 0xffffffffu); break;
            case 4: Put32(&meta, 8, 0xffffffffu); break;
            case 5: Put32(&meta, 4, payload.size() + 1); break;
            case 6: Put32(&meta, 8, 1); break;
            case 7: payload.clear(); Put32(&meta, 4, 0); break;
            case 8: return Header(0, 20);
            case 9: return Header(0xffffffffu, 20);
            case 10: Put64(&meta, 12, Get64(request_meta, 16) + (1ULL << 32)); break;
            case 11: {
                std::string wire = Frame(meta, payload);
                wire.resize(wire.size() - 1);
                return wire;
            }
            }
            return Frame(meta, payload);
        }));
        brpc::Channel channel;
        ASSERT_EQ(0, InitChannel(&channel, peer.endpoint()));
        Message response;
        brpc::Controller cntl;
        Completion done;
        channel.FBCallMethod(descriptor.method(0), &cntl, &request, &response, &done);
        ASSERT_TRUE(done.Wait());
        EXPECT_EQ(1, done.calls());
        EXPECT_TRUE(cntl.Failed()) << "Malformed frame reported RPC success";
        if (variant >= 3 && variant <= 7) {
            EXPECT_EQ(brpc::ERESPONSE, cntl.ErrorCode()) << cntl.ErrorText();
        }
        EXPECT_EQ(0u, response.size());
        peer.Join();
        EXPECT_TRUE(peer.served());
    }
}

TEST(FlatBuffersResponseTest, SchemaVerificationIsExplicitlyTheCallersResponsibility) {
    ScriptedPeer peer;
    ASSERT_TRUE(peer.Start([](const std::string& request_meta, const std::string&) {
        const std::string invalid(16, '\xff');
        return Frame(ResponseMeta(invalid.size(), 0, Get64(request_meta, 16)),
                     invalid);
    }));
    brpc::Channel channel;
    ASSERT_EQ(0, InitChannel(&channel, peer.endpoint()));
    ServiceDescriptor descriptor;
    ASSERT_EQ(0, descriptor.init({"brpc_fbtest", "Peer", "Echo", {7}}));
    const Message request = MakePayload();
    Message response;
    brpc::Controller cntl;
    channel.FBCallMethod(descriptor.method(0), &cntl, &request, &response, nullptr);
    // The wire sizes are valid; no typed schema is known by the FRPC transport.
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_FALSE(response.Verify<Payload>());
    peer.Join();
    EXPECT_TRUE(peer.served());
}

}  // namespace
#endif  // BRPC_WITH_FLATBUFFERS
