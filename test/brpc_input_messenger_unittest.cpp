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
#include <netdb.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "gperftools_helper.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "butil/fd_guard.h"
#include "butil/unix_socket.h"
#include "bthread/unstable.h"
#include "brpc/acceptor.h"
#include "brpc/input_messenger.h"
#include "brpc/policy/hulu_pbrpc_protocol.h"
#include "brpc/transport.h"

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_int32(input_message_batch_process_size);
}

namespace {

struct BatchRecorder {
    void Record(int value) {
        std::lock_guard<std::mutex> lock(mutex);
        values.push_back(value);
        condition.notify_all();
    }

    void RecordDestroy() {
        destroyed.fetch_add(1, std::memory_order_relaxed);
        condition.notify_all();
    }

    bool WaitForSize(size_t expected) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(
            lock, std::chrono::seconds(5),
            [this, expected] { return values.size() >= expected; });
    }

    bool WaitForDestroyed(int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(
            lock, std::chrono::seconds(5),
            [this, expected] {
                return destroyed.load(std::memory_order_relaxed) >= expected;
            });
    }

    std::vector<int> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return values;
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<int> values;
    std::atomic<int> destroyed{0};
};

class BatchTestMessage : public brpc::InputMessageBase {
public:
    BatchTestMessage(int value, BatchRecorder* recorder, bool throw_on_process)
        : value(value)
        , recorder(recorder)
        , throw_on_process(throw_on_process) {}

    int value;
    BatchRecorder* recorder;
    bool throw_on_process;

private:
    void DestroyImpl() override {
        recorder->RecordDestroy();
        delete this;
    }
};

void ProcessBatchTestMessage(brpc::InputMessageBase* msg_base) {
    brpc::DestroyingPtr<brpc::InputMessageBase> guard(msg_base);
    BatchTestMessage* msg = static_cast<BatchTestMessage*>(msg_base);
    msg->recorder->Record(msg->value);
    if (msg->throw_on_process) {
        throw std::runtime_error("injected handler failure");
    }
}

BatchTestMessage* NewBatchTestMessage(
        int value, BatchRecorder* recorder, bool throw_on_process = false) {
    BatchTestMessage* msg =
        new BatchTestMessage(value, recorder, throw_on_process);
    msg->_process = ProcessBatchTestMessage;
    return msg;
}

brpc::ParseResult ParseBatchTestMessage(
        butil::IOBuf* source, brpc::Socket*, bool, const void* arg) {
    if (source->empty()) {
        return brpc::MakeParseError(brpc::PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    char value = '\0';
    source->copy_to(&value, 1);
    source->pop_front(1);
    return brpc::MakeMessage(new BatchTestMessage(
        static_cast<unsigned char>(value),
        static_cast<BatchRecorder*>(const_cast<void*>(arg)), false));
}

brpc::InputMessageHandler MakeBatchTestHandler(BatchRecorder* recorder) {
    const brpc::InputMessageHandler handler = {
        ParseBatchTestMessage,
        ProcessBatchTestMessage,
        nullptr,
        recorder,
        "batch_test",
    };
    return handler;
}

brpc::SocketUniquePtr CreateBatchTestSocket(
        brpc::InputMessenger* messenger, brpc::SocketId* id) {
    brpc::SocketOptions options;
    options.socket_mode = brpc::SOCKET_MODE_TCP;
    EXPECT_EQ(0, messenger->Create(options, id));
    brpc::SocketUniquePtr socket;
    EXPECT_EQ(0, brpc::Socket::Address(*id, &socket));
    return socket;
}

class InputBatchFlagGuard {
public:
    InputBatchFlagGuard()
        : batch_size(brpc::FLAGS_input_message_batch_process_size)
        , usercode_in_coroutine(brpc::FLAGS_usercode_in_coroutine) {}

    ~InputBatchFlagGuard() {
        brpc::FLAGS_input_message_batch_process_size = batch_size;
        brpc::FLAGS_usercode_in_coroutine = usercode_in_coroutine;
    }

private:
    int batch_size;
    bool usercode_in_coroutine;
};

}  // namespace

void EmptyProcessHuluRequest(brpc::InputMessageBase* msg_base) {
    brpc::DestroyingPtr<brpc::InputMessageBase> a(msg_base);
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    brpc::Protocol dummy_protocol = 
                             { brpc::policy::ParseHuluMessage,
                               brpc::SerializeRequestDefault, 
                               brpc::policy::PackHuluRequest,
                               EmptyProcessHuluRequest, EmptyProcessHuluRequest,
                               nullptr, nullptr, nullptr,
                               brpc::CONNECTION_TYPE_ALL, "dummy_hulu" };
    EXPECT_EQ(0,  RegisterProtocol((brpc::ProtocolType)30, dummy_protocol));
    return RUN_ALL_TESTS();
}

class MessengerTest : public ::testing::Test{
protected:
    MessengerTest(){
    };
    virtual ~MessengerTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

TEST_F(MessengerTest, input_message_batch_runs_in_order_once) {
    BatchRecorder recorder;
    brpc::InputMessageBatch batch(8);
    batch.add(NewBatchTestMessage(1, &recorder));
    batch.add(nullptr);
    batch.add(NewBatchTestMessage(2, &recorder));
    batch.add(NewBatchTestMessage(3, &recorder));
    ASSERT_EQ(3u, batch.size());

    batch.Run();
    EXPECT_TRUE(batch.empty());
    EXPECT_EQ((std::vector<int>{1, 2, 3}), recorder.Snapshot());
    EXPECT_EQ(3, recorder.destroyed.load());

    batch.Run();
    EXPECT_EQ((std::vector<int>{1, 2, 3}), recorder.Snapshot());
    EXPECT_EQ(3, recorder.destroyed.load());
}

TEST_F(MessengerTest, input_message_batch_destructor_contains_exceptions) {
    static_assert(
        std::is_nothrow_destructible<brpc::InputMessageBatch>::value,
        "InputMessageBatch must be nothrow destructible");

    BatchRecorder recorder;
    EXPECT_NO_THROW({
        brpc::InputMessageBatch batch(2);
        batch.add(NewBatchTestMessage(1, &recorder, true));
        batch.add(NewBatchTestMessage(2, &recorder));
    });
    EXPECT_EQ((std::vector<int>{1}), recorder.Snapshot());
    EXPECT_EQ(2, recorder.destroyed.load());

    BatchRecorder worker_recorder;
    brpc::InputMessageBatch* batch = new brpc::InputMessageBatch(2);
    batch->add(NewBatchTestMessage(1, &worker_recorder, true));
    batch->add(NewBatchTestMessage(2, &worker_recorder));
    EXPECT_NO_THROW(brpc::ProcessInputMessageBatch(batch));
    EXPECT_EQ((std::vector<int>{1, 2}), worker_recorder.Snapshot());
    EXPECT_EQ(2, worker_recorder.destroyed.load());
}

TEST_F(MessengerTest, input_message_batch_flag_validation) {
    InputBatchFlagGuard flag_guard;
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "input_message_batch_process_size", "-1").empty());
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "input_message_batch_process_size", "0").empty());
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "input_message_batch_process_size", "1").empty());
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "input_message_batch_process_size", "8").empty());
    EXPECT_TRUE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "input_message_batch_process_size", "-2").empty());
}

TEST_F(MessengerTest, adaptive_input_message_batch_rises_falls_and_caps) {
    uint32_t ema_q8 = 256;
    uint32_t batch_size = 1;
    std::vector<uint32_t> rising_levels(1, batch_size);
    for (int i = 0; i < 32 && batch_size < 16; ++i) {
        const uint32_t old_batch_size = batch_size;
        batch_size = brpc::InputMessenger::UpdateAdaptiveBatchSize(
            &ema_q8, batch_size, std::numeric_limits<size_t>::max());
        if (batch_size != old_batch_size) {
            rising_levels.push_back(batch_size);
        }
    }
    EXPECT_EQ((std::vector<uint32_t>{1, 2, 4, 8, 16}), rising_levels);
    EXPECT_LE(ema_q8, 32u * 256);

    std::vector<uint32_t> falling_levels(1, batch_size);
    for (int i = 0; i < 32 && batch_size > 1; ++i) {
        const uint32_t old_batch_size = batch_size;
        batch_size = brpc::InputMessenger::UpdateAdaptiveBatchSize(
            &ema_q8, batch_size, 1);
        if (batch_size != old_batch_size) {
            falling_levels.push_back(batch_size);
        }
    }
    EXPECT_EQ((std::vector<uint32_t>{16, 8, 4, 2, 1}), falling_levels);

    brpc::SocketId id;
    ASSERT_EQ(0, brpc::Socket::Create(brpc::SocketOptions(), &id));
    brpc::SocketUniquePtr socket;
    ASSERT_EQ(0, brpc::Socket::Address(id, &socket));
    socket->_input_messages_per_read_ema_q8 = ema_q8;
    socket->_adaptive_input_message_batch_size = batch_size;
    ASSERT_EQ(0, socket->ResetFileDescriptor(-1));
    EXPECT_EQ(0u, socket->_input_messages_per_read_ema_q8);
    EXPECT_EQ(0u, socket->_adaptive_input_message_batch_size);
}

TEST_F(MessengerTest, batching_consumes_the_last_message) {
    InputBatchFlagGuard flag_guard;
    brpc::FLAGS_input_message_batch_process_size = 8;
    brpc::FLAGS_usercode_in_coroutine = false;

    BatchRecorder recorder;
    brpc::InputMessenger messenger(4);
    ASSERT_EQ(0, messenger.AddNonProtocolHandler(
        MakeBatchTestHandler(&recorder)));
    brpc::SocketId id;
    brpc::SocketUniquePtr socket = CreateBatchTestSocket(&messenger, &id);
    ASSERT_TRUE(socket);
    socket->_read_buf.append("abcd", 4);

    brpc::InputMessageClosure last_msg;
    ASSERT_EQ(0, messenger.ProcessNewMessage(
        socket.get(), 4, false, 123, 456, last_msg));
    brpc::DestroyingPtr<brpc::InputMessageBase> leftover(last_msg.release());
    EXPECT_EQ(nullptr, leftover.get());
    ASSERT_TRUE(recorder.WaitForSize(4));
    EXPECT_EQ((std::vector<int>{'a', 'b', 'c', 'd'}), recorder.Snapshot());

    socket->SetFailed();
    ASSERT_TRUE(recorder.WaitForDestroyed(4));
}

TEST_F(MessengerTest, disabled_and_progressive_paths_remain_individual) {
    InputBatchFlagGuard flag_guard;
    brpc::FLAGS_usercode_in_coroutine = false;

    {
        brpc::FLAGS_input_message_batch_process_size = 0;
        BatchRecorder recorder;
        brpc::InputMessenger messenger(4);
        ASSERT_EQ(0, messenger.AddNonProtocolHandler(
            MakeBatchTestHandler(&recorder)));
        brpc::SocketId id;
        brpc::SocketUniquePtr socket = CreateBatchTestSocket(&messenger, &id);
        ASSERT_TRUE(socket);
        socket->_read_buf.append("ab", 2);
        brpc::InputMessageClosure last_msg;
        ASSERT_EQ(0, messenger.ProcessNewMessage(
            socket.get(), 2, false, 123, 456, last_msg));
        brpc::DestroyingPtr<brpc::InputMessageBase> leftover(last_msg.release());
        EXPECT_NE(nullptr, leftover.get());
        leftover.reset();
        ASSERT_TRUE(recorder.WaitForDestroyed(2));
        socket->SetFailed();
    }

    {
        brpc::FLAGS_input_message_batch_process_size = 8;
        BatchRecorder recorder;
        brpc::InputMessenger messenger(4);
        ASSERT_EQ(0, messenger.AddNonProtocolHandler(
            MakeBatchTestHandler(&recorder)));
        brpc::SocketId id;
        brpc::SocketUniquePtr socket = CreateBatchTestSocket(&messenger, &id);
        ASSERT_TRUE(socket);
        socket->read_will_be_progressive(brpc::CONNECTION_TYPE_SINGLE);
        socket->_read_buf.append("abc", 3);
        brpc::InputMessageClosure last_msg;
        ASSERT_EQ(0, messenger.ProcessNewMessage(
            socket.get(), 3, false, 123, 456, last_msg));
        EXPECT_EQ(nullptr, last_msg.release());
        ASSERT_TRUE(recorder.WaitForSize(3));
        std::vector<int> values = recorder.Snapshot();
        std::sort(values.begin(), values.end());
        EXPECT_EQ((std::vector<int>{'a', 'b', 'c'}), values);
        socket->SetFailed();
        ASSERT_TRUE(recorder.WaitForDestroyed(3));
    }
}

TEST_F(MessengerTest, transport_batch_helper_handles_inline_and_empty_batches) {
    InputBatchFlagGuard flag_guard;
    brpc::FLAGS_usercode_in_coroutine = true;

    brpc::InputMessenger messenger;
    brpc::SocketId id;
    brpc::SocketUniquePtr socket = CreateBatchTestSocket(&messenger, &id);
    ASSERT_TRUE(socket);

    BatchRecorder recorder;
    int num_bthread_created = 0;
    brpc::InputMessageBatch* batch = new brpc::InputMessageBatch(2);
    batch->add(NewBatchTestMessage(1, &recorder));
    batch->add(NewBatchTestMessage(2, &recorder));
    socket->_transport->QueueMessages(batch, &num_bthread_created);
    EXPECT_EQ((std::vector<int>{1, 2}), recorder.Snapshot());
    EXPECT_EQ(0, num_bthread_created);

    socket->_transport->QueueMessages(
        new brpc::InputMessageBatch, &num_bthread_created);
    EXPECT_EQ(0, num_bthread_created);

    brpc::FLAGS_usercode_in_coroutine = false;
    batch = new brpc::InputMessageBatch(2);
    batch->add(NewBatchTestMessage(3, &recorder));
    batch->add(NewBatchTestMessage(4, &recorder));
    socket->_transport->QueueMessages(batch, &num_bthread_created);
    EXPECT_EQ(1, num_bthread_created);
    bthread_flush();
    ASSERT_TRUE(recorder.WaitForSize(4));
    EXPECT_EQ((std::vector<int>{1, 2, 3, 4}), recorder.Snapshot());

    socket->SetFailed();
}

#define USE_UNIX_DOMAIN_SOCKET 1

const size_t NEPOLL = 1;
const size_t NCLIENT = 6;
const size_t NMESSAGE = 1024;
const size_t MESSAGE_SIZE = 32;

inline uint32_t fmix32 ( uint32_t h ) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

volatile bool client_stop = false;

struct BAIDU_CACHELINE_ALIGNMENT ClientMeta {
    size_t times;
    size_t bytes;
};

butil::atomic<size_t> client_index(0);

void* client_thread(void* arg) {
    ClientMeta* m = (ClientMeta*)arg;
    size_t offset = 0;
    m->times = 0;
    m->bytes = 0;
    const size_t buf_cap = NMESSAGE * MESSAGE_SIZE;
    char* buf = (char*)malloc(buf_cap);
    for (size_t i = 0; i < NMESSAGE; ++i) {
        memcpy(buf + i * MESSAGE_SIZE, "HULU", 4);
        // HULU use host byte order directly...
        *(uint32_t*)(buf + i * MESSAGE_SIZE + 4) = MESSAGE_SIZE - 12;
        *(uint32_t*)(buf + i * MESSAGE_SIZE + 8) = 4;
    }
#ifdef USE_UNIX_DOMAIN_SOCKET
    const size_t id = client_index.fetch_add(1);
    char socket_name[64];
    snprintf(socket_name, sizeof(socket_name), "input_messenger.socket%lu",
             (id % NEPOLL));
    butil::fd_guard fd(butil::unix_socket_connect(socket_name));
    if (fd < 0) {
        PLOG(FATAL) << "Fail to connect to " << socket_name;
        return nullptr;
    }
#else
    butil::EndPoint point(butil::IP_ANY, 7878);
    butil::fd_guard fd(butil::tcp_connect(point, nullptr));
    if (fd < 0) {
        PLOG(FATAL) << "Fail to connect to " << point;
        return nullptr;
    }
#endif

    while (!client_stop) {
        ssize_t n;
        if (offset == 0) {
            n = write(fd, buf, buf_cap);
        } else {
            iovec v[2];
            v[0].iov_base = buf + offset;
            v[0].iov_len = buf_cap - offset;
            v[1].iov_base = buf;
            v[1].iov_len = offset;
            n = writev(fd, v, 2);
        }
        if (n < 0) {
            if (errno != EINTR) {
                PLOG(FATAL) << "Fail to write fd=" << fd;
                return nullptr;
            }
        } else {
            ++m->times;
            m->bytes += n;
            offset += n;
            if (offset >= buf_cap) {
                offset -= buf_cap;
            }
        }
    }
    free(buf);
    return nullptr;
}

TEST_F(MessengerTest, dispatch_tasks) {
    client_stop = false;
    
    brpc::Acceptor messenger[NEPOLL];
    pthread_t cth[NCLIENT];
    ClientMeta* cm[NCLIENT];

    const brpc::InputMessageHandler pairs[] = {
        { brpc::policy::ParseHuluMessage, 
          EmptyProcessHuluRequest, nullptr, nullptr, "dummy_hulu" }
    };

    for (size_t i = 0; i < NEPOLL; ++i) {        
#ifdef USE_UNIX_DOMAIN_SOCKET
        char buf[64];
        snprintf(buf, sizeof(buf), "input_messenger.socket%lu", i);
        int listening_fd = butil::unix_socket_listen(buf);
#else
        int listening_fd = tcp_listen(butil::EndPoint(butil::IP_ANY, 7878));
#endif
        ASSERT_TRUE(listening_fd > 0);
        butil::make_non_blocking(listening_fd);
        ASSERT_EQ(0, messenger[i].AddHandler(pairs[0]));
        ASSERT_EQ(0, messenger[i].StartAccept(listening_fd, -1, nullptr, false));
    }
    
    for (size_t i = 0; i < NCLIENT; ++i) {
        cm[i] = new ClientMeta;
        cm[i]->times = 0;
        cm[i]->bytes = 0;
        ASSERT_EQ(0, pthread_create(&cth[i], nullptr, client_thread, cm[i]));
    }

    sleep(1);


    LOG(INFO) << "Begin to profile... (5 seconds)";
    ProfilerStart("input_messenger.prof");

    size_t start_client_bytes = 0;
    for (size_t i = 0; i < NCLIENT; ++i) {
        start_client_bytes += cm[i]->bytes;
    }
    butil::Timer tm;
    tm.start();
    
    sleep(5);
    
    tm.stop();
    ProfilerStop();
    LOG(INFO) << "End profiling";

    client_stop = true;

    size_t client_bytes = 0;
    for (size_t i = 0; i < NCLIENT; ++i) {
        client_bytes += cm[i]->bytes;
    }
    LOG(INFO) << "client_tp=" << (client_bytes - start_client_bytes) / (double)tm.u_elapsed()
              << "MB/s client_msg="
              << (client_bytes - start_client_bytes) * 1000000L / (MESSAGE_SIZE * tm.u_elapsed())
              << "/s";

    for (size_t i = 0; i < NCLIENT; ++i) {
        pthread_join(cth[i], nullptr);
        printf("joined client %lu\n", i);
    }
    for (size_t i = 0; i < NEPOLL; ++i) {
        messenger[i].StopAccept(0);
    }
    sleep(1);
    for (size_t i = 0; i < NCLIENT; ++i) {
        delete cm[i];
    }
    LOG(WARNING) << "begin to exit!!!!";
}
