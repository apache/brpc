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
#include <gflags/gflags.h>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
#include "brpc/flatbuffers/message.h"
#include "brpc/flatbuffers/service.h"
#include "flatbuffers_message_generated.h"

#if !BRPC_WITH_GLOG
namespace logging {
DECLARE_bool(crash_on_fatal_log);
}
#endif

namespace {
using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;
using brpc::flatbuffers::SlabAllocator;
using brpc::flatbuffers::ServiceDescriptor;
using brpc::flatbuffers::BrpcDescriptorTable;
using brpc_fbtest::Payload;

void FinishPayload(::flatbuffers::FlatBufferBuilder& builder,
                   const std::string& text, int64_t value = 42) {
    auto str = builder.CreateString(text);
    std::vector<int32_t> numbers = {1, -2, 3, 4000};
    auto values = builder.CreateVector(numbers);
    builder.Finish(brpc_fbtest::CreatePayload(builder, value, str, values));
}

Message MakeMessage(size_t length = 16, int64_t value = 42) {
    MessageBuilder builder(8);
    FinishPayload(builder, std::string(length, 'x'), value);
    return builder.ReleaseMessage();
}

void ExpectPayload(const Message& msg, size_t length, int64_t value = 42) {
    ASSERT_TRUE(msg.Verify<Payload>());
    const Payload* root = msg.GetRoot<Payload>();
    ASSERT_NE(nullptr, root);
    EXPECT_EQ(value, root->value());
    ASSERT_NE(nullptr, root->message());
    EXPECT_EQ(std::string(length, 'x'), root->message()->str());
    ASSERT_NE(nullptr, root->values());
    ASSERT_EQ(4u, root->values()->size());
    EXPECT_EQ(-2, root->values()->Get(1));
}

TEST(FlatbuffersTest, EmptyAndClear) {
    Message msg;
    EXPECT_EQ(nullptr, msg.data());
    EXPECT_EQ(nullptr, msg.GetRoot<Payload>());
    EXPECT_EQ(nullptr, msg.GetMutableRoot<Payload>());
    EXPECT_EQ(nullptr, msg.reduce_meta_size_and_get_buf(0));
    EXPECT_FALSE(msg.Verify<Payload>());
    EXPECT_EQ(0u, msg.size());
    butil::IOBuf wire;
    EXPECT_FALSE(brpc::flatbuffers::SerializeFbToIOBUF(&msg, wire));
    EXPECT_FALSE(brpc::flatbuffers::SerializeFbToIOBUF(nullptr, wire));
    EXPECT_FALSE(brpc::flatbuffers::ParseFbFromIOBUF(nullptr, 0, wire));
    msg = MakeMessage();
    msg.Clear();
    msg.Clear();
    EXPECT_EQ(nullptr, msg.data());
    EXPECT_EQ(0u, msg.get_meta_size());
    EXPECT_EQ(0u, msg.size());
}

TEST(FlatbuffersTest, ReleaseLifetimeAndGrowth) {
    for (size_t length : {0u, 16u, 63u, 1024u, 8192u, 32768u}) {
        SCOPED_TRACE(length);
        Message msg;
        const uint8_t* payload = nullptr;
        {
            MessageBuilder builder(8);
            FinishPayload(builder, std::string(length, 'x'));
            payload = builder.GetBufferPointer();
            msg = builder.ReleaseMessage();
            EXPECT_EQ(payload, msg.data());
            EXPECT_EQ(0u, builder.GetSize());
            FinishPayload(builder, "replacement");
        }
        ExpectPayload(msg, length);
        EXPECT_EQ(std::string(brpc::flatbuffers::kDefaultMetaSize, '\0'),
                  std::string(static_cast<const char*>(msg.mutable_buf_begin()),
                              msg.get_meta_size()));
    }
}

TEST(FlatbuffersTest, MessageMoveAndSwap) {
    Message first = MakeMessage(16, 1);
    const uint8_t* ptr = first.data();
    Message second(std::move(first));
    EXPECT_EQ(ptr, second.data());
    EXPECT_EQ(nullptr, first.data());
    EXPECT_EQ(0u, first.size());
    EXPECT_EQ(0u, first.get_meta_size());
    EXPECT_FALSE(first.Verify<Payload>());
    first = MakeMessage(8192, 2);
    second = std::move(first);
    EXPECT_EQ(nullptr, first.data());
    ExpectPayload(second, 8192, 2);
    Message* alias = &second;
    second = std::move(*alias);
    ExpectPayload(second, 8192, 2);
    first = MakeMessage(16, 3);
    first.Swap(second);
    ExpectPayload(first, 8192, 2);
    ExpectPayload(second, 16, 3);
    Message empty;
    second = std::move(empty);
    EXPECT_EQ(nullptr, second.data());
    EXPECT_EQ(nullptr, empty.data());
}

TEST(FlatbuffersTest, BuilderMoveAndReuse) {
    MessageBuilder first(8);
    auto str = first.CreateString(std::string(8192, 'x'));
    MessageBuilder second(std::move(first));
    second.Finish(brpc_fbtest::CreatePayload(second, 8, str));
    Message msg = second.ReleaseMessage();
    ASSERT_TRUE(msg.Verify<Payload>());
    EXPECT_EQ(8192u, msg.GetRoot<Payload>()->message()->size());
    FinishPayload(first, std::string(16, 'x'));
    ExpectPayload(first.ReleaseMessage(), 16);
    FinishPayload(second, std::string(63, 'x'));
    first = std::move(second);
    MessageBuilder* alias = &first;
    first = std::move(*alias);
    ExpectPayload(first.ReleaseMessage(), 63);
    FinishPayload(second, std::string(1024, 'x'));
    ExpectPayload(second.ReleaseMessage(), 1024);
}

TEST(FlatbuffersTest, BuilderSwapFinishedAndUnfinished) {
    MessageBuilder first;
    FinishPayload(first, std::string(16, 'x'));
    MessageBuilder second;
    auto str = second.CreateString(std::string(63, 'x'));
    first.Swap(second);
    ExpectPayload(second.ReleaseMessage(), 16);
    first.Finish(brpc_fbtest::CreatePayload(first, 9, str));
    Message msg = first.ReleaseMessage();
    ASSERT_TRUE(msg.Verify<Payload>());
    EXPECT_EQ(9, msg.GetRoot<Payload>()->value());
    EXPECT_EQ(63u, msg.GetRoot<Payload>()->message()->size());
}

TEST(FlatbuffersTest, SharedStringsAfterMoveAndImport) {
    MessageBuilder first(8);
    auto old = first.CreateSharedString("shared");
    MessageBuilder second(std::move(first));
    auto same = second.CreateSharedString("shared");
    auto other = second.CreateSharedString("other");
    std::vector<::flatbuffers::Offset<::flatbuffers::String> > strings = {
        old, same, other};
    second.Finish(second.CreateVector(strings));
    Message msg = second.ReleaseMessage();
    auto root = ::flatbuffers::GetRoot<
        ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String> > >(
            msg.data());
    EXPECT_EQ("shared", root->Get(0)->str());
    EXPECT_EQ("shared", root->Get(1)->str());
    EXPECT_EQ("other", root->Get(2)->str());
    ::flatbuffers::FlatBufferBuilder foreign;
    auto text = foreign.CreateSharedString("shared");
    MessageBuilder imported(std::move(foreign));
    imported.CreateSharedString("after import");
    imported.Finish(brpc_fbtest::CreatePayload(imported, 1, text));
    ASSERT_TRUE(imported.ReleaseMessage().Verify<Payload>());
}

class CountingAllocator : public ::flatbuffers::Allocator {
public:
    CountingAllocator(int* allocations, int* frees, int* destructors)
        : _allocations(allocations), _frees(frees), _destructors(destructors) {}
    ~CountingAllocator() override { ++*_destructors; }
    uint8_t* allocate(size_t n) override {
        ++*_allocations;
        return new uint8_t[n];
    }
    void deallocate(uint8_t* p, size_t) override {
        ++*_frees;
        delete[] p;
    }
private:
    int* _allocations;
    int* _frees;
    int* _destructors;
};

TEST(FlatbuffersTest, ImportForeignAllocatorAndScratch) {
    int allocations = 0;
    int frees = 0;
    int destructors = 0;
    Message msg;
    {
        ::flatbuffers::FlatBufferBuilder foreign(8,
            new CountingAllocator(&allocations, &frees, &destructors), true);
        auto text = foreign.CreateString(std::string(8192, 'x'));
        const auto table = foreign.StartTable();
        foreign.AddOffset(Payload::VT_MESSAGE, text);
        foreign.AddElement<int64_t>(Payload::VT_VALUE, 123, 0);
        MessageBuilder imported(std::move(foreign));
        EXPECT_EQ(allocations, frees);
        EXPECT_EQ(1, destructors);
        imported.Finish(::flatbuffers::Offset<Payload>(imported.EndTable(table)));
        msg = imported.ReleaseMessage();
        FinishPayload(foreign, "reuse source");
    }
    ASSERT_TRUE(msg.Verify<Payload>());
    EXPECT_EQ(123, msg.GetRoot<Payload>()->value());
    EXPECT_EQ(8192u, msg.GetRoot<Payload>()->message()->size());
    EXPECT_EQ(allocations, frees);
    EXPECT_EQ(1, destructors);
}

TEST(FlatbuffersTest, ImportBorrowedAllocatorDoesNotOwnAllocator) {
    int allocations = 0;
    int frees = 0;
    int destructors = 0;
    Message msg;
    {
        CountingAllocator allocator(&allocations, &frees, &destructors);
        ::flatbuffers::FlatBufferBuilder foreign(8, &allocator, false);
        FinishPayload(foreign, std::string(1024, 'x'));
        MessageBuilder imported(std::move(foreign));
        EXPECT_EQ(allocations, frees);
        EXPECT_EQ(0, destructors);
        msg = imported.ReleaseMessage();
    }
    EXPECT_EQ(1, destructors);
    ExpectPayload(msg, 1024);
}

TEST(FlatbuffersTest, ImportFinishedDefaultBuilderAndEmpty) {
    ::flatbuffers::FlatBufferBuilder foreign(8);
    FinishPayload(foreign, std::string(8192, 'x'));
    MessageBuilder imported(std::move(foreign));
    ExpectPayload(imported.ReleaseMessage(), 8192);
    ::flatbuffers::FlatBufferBuilder empty;
    imported = std::move(empty);
    FinishPayload(imported, std::string(16, 'x'));
    ExpectPayload(imported.ReleaseMessage(), 16);
    FinishPayload(imported, std::string(63, 'x'));
    ::flatbuffers::FlatBufferBuilder& alias = imported;
    imported = std::move(alias);
    ExpectPayload(imported.ReleaseMessage(), 63);
}

TEST(FlatbuffersTest, AllocatorFrontBackAndMove) {
    SlabAllocator first;
    uint8_t* data = first.allocate(64);
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(data) %
                  brpc::flatbuffers::kBufferAlignment);
    memset(data, 'f', 16);
    memset(data + 48, 'b', 16);
    SlabAllocator second(std::move(first));
    size_t previous = 64;
    for (size_t next : {128u, 512u, 8192u, 16384u}) {
        data = second.reallocate_downward(data, previous, next, 16, 16);
        EXPECT_EQ(std::string(16, 'f'), std::string((char*)data, 16));
        EXPECT_EQ(std::string(16, 'b'), std::string((char*)data + next - 16, 16));
        previous = next;
    }
    first = std::move(second);
    SlabAllocator* alias = &first;
    first = std::move(*alias);
    first.deallocate(data, 16384);
    second.deallocate(nullptr, 0);
    data = second.allocate(32);
    second.deallocate(data, 32);
}

TEST(FlatbuffersTest, SerializeConstAndRetainWireStorage) {
    butil::IOBuf wire;
    {
        const Message msg = MakeMessage(8192);
        ASSERT_TRUE(brpc::flatbuffers::SerializeFbToIOBUF(&msg, wire));
        EXPECT_EQ(msg.size() + msg.get_meta_size(), wire.size());
    }
    Message parsed;
    const size_t meta = brpc::flatbuffers::kDefaultMetaSize;
    ASSERT_TRUE(brpc::flatbuffers::ParseFbFromIOBUF(
        &parsed, wire.size() - meta, wire, meta));
    wire.clear();
    ExpectPayload(parsed, 8192);
}

TEST(FlatbuffersTest, ParseAlignedStorageIsSharedAndRetained) {
    Message source = MakeMessage();
    void* raw = nullptr;
    ASSERT_EQ(0, posix_memalign(&raw, brpc::flatbuffers::kBufferAlignment,
                               source.size()));
    memcpy(raw, source.data(), source.size());
    int frees = 0;
    butil::IOBuf wire;
    ASSERT_EQ(0, wire.append_user_data(raw, source.size(), [&frees](void* p) {
        ++frees;
        free(p);
    }));
    Message parsed;
    ASSERT_TRUE(parsed.parse_msg_from_iobuf(wire, source.size(), 0));
    EXPECT_EQ(raw, parsed.data());
    wire.clear();
    EXPECT_EQ(0, frees);
    ExpectPayload(parsed, 16);
    parsed.Clear();
    EXPECT_EQ(1, frees);
}

TEST(FlatbuffersTest, ParseFragmentedUnalignedAndReplace) {
    for (size_t length : {16u, 8192u, 32768u}) {
        Message source = MakeMessage(length);
        butil::IOBuf wire;
        const size_t split = source.size() / 2;
        void* first = malloc(split);
        void* second = malloc(source.size() - split);
        memcpy(first, source.data(), split);
        memcpy(second, source.data() + split, source.size() - split);
        ASSERT_EQ(0, wire.append_user_data(first, split, free));
        ASSERT_EQ(0, wire.append_user_data(second, source.size() - split, free));
        ASSERT_EQ(2u, wire.backing_block_num());
        Message parsed = MakeMessage(1);
        ASSERT_TRUE(parsed.parse_msg_from_iobuf(wire, source.size(), 0));
        ASSERT_TRUE(parsed.parse_msg_from_iobuf(wire, source.size(), 0));
        wire.clear();
        ExpectPayload(parsed, length);
        EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(parsed.data()) %
                      brpc::flatbuffers::kBufferAlignment);

        char* bytes = static_cast<char*>(malloc(source.size() + 1));
        bytes[0] = 'h';
        memcpy(bytes + 1, source.data(), source.size());
        ASSERT_EQ(0, wire.append_user_data(bytes, source.size() + 1, free));
        ASSERT_TRUE(parsed.parse_msg_from_iobuf(wire, source.size(), 1));
        EXPECT_NE(bytes + 1, reinterpret_cast<const char*>(parsed.data()));
        wire.clear();
        ExpectPayload(parsed, length);
        EXPECT_EQ(1u, parsed.get_meta_size());
    }
}

TEST(FlatbuffersTest, ParseRejectsBadFramingWithoutChangingMessage) {
    Message msg = MakeMessage(16);
    const uint8_t* data = msg.data();
    butil::IOBuf buf;
    buf.append("abcd");
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, 0, 4));
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, 4, 1));
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, 5, 0));
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, 1, 5));
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, std::numeric_limits<size_t>::max(), 5));
    EXPECT_FALSE(msg.parse_msg_from_iobuf(buf, 5, std::numeric_limits<size_t>::max()));
    EXPECT_EQ(data, msg.data());
    ExpectPayload(msg, 16);
}

TEST(FlatbuffersTest, InvalidPayloadAndOptionalString) {
    Message msg;
    for (size_t size : {1u, 3u, 4u, 16u, 64u}) {
        butil::IOBuf buf;
        buf.append(std::string(size, '\xff'));
        ASSERT_TRUE(msg.parse_msg_from_iobuf(buf, size, 0));
        EXPECT_FALSE(msg.Verify<Payload>());
    }
    MessageBuilder builder;
    builder.Finish(brpc_fbtest::CreatePayload(builder));
    msg = builder.ReleaseMessage();
    ASSERT_TRUE(msg.Verify<Payload>());
    EXPECT_EQ(nullptr, msg.GetRoot<Payload>()->message());
    // The schema allows an absent string; callers must not dereference it.
    msg = MakeMessage();
    memset(msg.mutable_data(), 0xff, sizeof(::flatbuffers::uoffset_t));
    EXPECT_FALSE(msg.Verify<Payload>());
}

TEST(FlatbuffersTest, ShrinkMetadataDoesNotMovePayload) {
    Message msg = MakeMessage(16);
    const uint8_t* data = msg.data();
    EXPECT_EQ(nullptr, msg.reduce_meta_size_and_get_buf(65));
    ASSERT_NE(nullptr, msg.reduce_meta_size_and_get_buf(36));
    memset(msg.mutable_buf_begin(), 'm', 36);
    EXPECT_EQ(data, msg.data());
    ExpectPayload(msg, 16);
    EXPECT_EQ(data, msg.reduce_meta_size_and_get_buf(0));
    EXPECT_EQ(data, msg.reduce_meta_size_and_get_buf(0));
    EXPECT_EQ(nullptr, msg.reduce_meta_size_and_get_buf(1));
    EXPECT_EQ(0u, msg.get_meta_size());
    butil::IOBuf wire;
    ASSERT_TRUE(msg.append_msg_to_iobuf(wire));
    EXPECT_EQ(msg.size(), wire.size());
}

void* FailBlockAllocation(size_t) { return nullptr; }

class FlatbuffersDeathTest : public ::testing::Test {
protected:
    void SetUp() override {
        _old_style = GTEST_FLAG_GET(death_test_style);
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#if !BRPC_WITH_GLOG
        _old = logging::FLAGS_crash_on_fatal_log;
        logging::FLAGS_crash_on_fatal_log = false;
#endif
    }
    void TearDown() override {
        GTEST_FLAG_SET(death_test_style, _old_style);
#if !BRPC_WITH_GLOG
        logging::FLAGS_crash_on_fatal_log = _old;
#endif
    }
private:
    bool _old = false;
    std::string _old_style;
};

TEST_F(FlatbuffersDeathTest, AllocationFailureIsChecked) {
    EXPECT_DEATH({
        butil::iobuf::blockmem_allocate = FailBlockAllocation;
        SlabAllocator allocator;
        allocator.allocate(1024 * 1024);
    }, "Fail to allocate");
    EXPECT_DEATH({
        SlabAllocator allocator;
        uint8_t* data = allocator.allocate(1024 * 1024);
        butil::iobuf::blockmem_allocate = FailBlockAllocation;
        allocator.reallocate_downward(data, 1024 * 1024, 2 * 1024 * 1024, 8, 8);
    }, "Fail to allocate");
}

TEST_F(FlatbuffersDeathTest, RejectsUnsafeAllocatorInputsAndUnfinishedRelease) {
    EXPECT_DEATH({ SlabAllocator a; a.allocate(0); }, "");
    EXPECT_DEATH({ SlabAllocator a; a.allocate(std::numeric_limits<size_t>::max()); }, "");
    EXPECT_DEATH({
        SlabAllocator a;
        uint8_t* p = a.allocate(8);
        a.reallocate_downward(p, 8, 16, 8, 1);
    }, "");
    EXPECT_DEATH({ MessageBuilder builder; builder.ReleaseMessage(); }, "Finish");
    EXPECT_DEATH({ Message msg; Message other; msg.MergeFrom(other); }, "move-only");
}

TEST(FlatbuffersDescriptorTest, SparseStableIDsAndCanonicalNames) {
    ServiceDescriptor first;
    ASSERT_EQ(0, first.init({"test.", "Benchmark", "One Two Three", {2, 5, 1}}));
    EXPECT_EQ("test.Benchmark", first.full_name());
    EXPECT_EQ(3, first.method_count());
    EXPECT_EQ(2, first.method(0)->index());
    EXPECT_EQ(5, first.method(1)->index());
    EXPECT_EQ("test.Benchmark.Two", first.method(1)->full_name());
    EXPECT_EQ(&first, first.method(0)->service());
    EXPECT_EQ(first.method(1), first.FindMethodByIndex(5));
    EXPECT_EQ(nullptr, first.FindMethodByIndex(3));
    EXPECT_EQ(nullptr, first.method(-1));
    EXPECT_EQ(nullptr, first.method(3));
    ServiceDescriptor reduced;
    ASSERT_EQ(0, reduced.init({"test", "Benchmark", "Three Two", {1, 5}}));
    EXPECT_EQ(first.index(), reduced.index());
    EXPECT_EQ(first.FindMethodByIndex(5)->index(), reduced.method(1)->index());
    EXPECT_EQ(first.FindMethodByIndex(5)->full_name(), reduced.method(1)->full_name());
    EXPECT_NE(0, first.init({"test", "Other", "Call", {3}}));
    EXPECT_EQ("test.Benchmark", first.full_name());
}

TEST(FlatbuffersDescriptorTest, LegacyGlobalAndWhitespace) {
    ServiceDescriptor desc;
    ASSERT_EQ(0, desc.init({"", "Global", " First\tSecond\n Third ", {}}));
    EXPECT_EQ("Global", desc.full_name());
    EXPECT_EQ(3, desc.method_count());
    EXPECT_EQ(0, desc.method(0)->index());
    EXPECT_EQ(2, desc.method(2)->index());
    EXPECT_EQ("Global.First", desc.method(0)->full_name());
}

TEST(FlatbuffersDescriptorTest, InvalidTablesAndFailureOwnership) {
    const std::vector<BrpcDescriptorTable> invalid = {
        {"test", "", "A", {}}, {"test", "Service", "", {}},
        {"test", "Service", "A A", {1, 2}},
        {"test", "Service", "A B", {1, 1}},
        {"test", "Service", "A B", {1}},
        {"test", "Service", "A B", {-1, 2}},
        {"test..", "Service", "A", {}}, {".", "Service", "A", {}},
        {"test", "Bad.Name", "A", {}}, {"test", "Service", "1Bad", {}}
    };
    ServiceDescriptor desc;
    for (const auto& table : invalid) {
        EXPECT_NE(0, desc.init(table));
        EXPECT_EQ(0, desc.method_count());
        ServiceDescriptor* output = &desc;
        EXPECT_NE(0, brpc::flatbuffers::parse_service_descriptors(table, &output));
        EXPECT_EQ(&desc, output);
    }
    ASSERT_EQ(0, desc.init({"test", "Service", "A", {0}}));
    EXPECT_NE(0, brpc::flatbuffers::parse_service_descriptors(
        {"test", "Service", "A", {0}}, nullptr));
    ServiceDescriptor* output = nullptr;
    ASSERT_EQ(0, brpc::flatbuffers::parse_service_descriptors(
        {"test", "Service", "A", {0}}, &output));
    std::unique_ptr<ServiceDescriptor> owner(output);
    EXPECT_EQ("test.Service", owner->full_name());
}

}  // namespace
#endif  // BRPC_WITH_FLATBUFFERS
