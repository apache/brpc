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
#include <netdb.h>                   //
#include <gtest/gtest.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "butil/fd_guard.h"
#include "butil/unix_socket.h"
#include "brpc/acceptor.h"
#include "brpc/policy/hulu_pbrpc_protocol.h"

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
                               NULL, NULL, NULL,
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
        return NULL;
    }
#else
    butil::EndPoint point(butil::IP_ANY, 7878);
    butil::fd_guard fd(butil::tcp_connect(point, NULL));
    if (fd < 0) {
        PLOG(FATAL) << "Fail to connect to " << point;
        return NULL;
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
                return NULL;
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
    return NULL;
}

TEST_F(MessengerTest, dispatch_tasks) {
    client_stop = false;
    
    brpc::Acceptor messenger[NEPOLL];
    pthread_t cth[NCLIENT];
    ClientMeta* cm[NCLIENT];

    const brpc::InputMessageHandler pairs[] = {
        { brpc::policy::ParseHuluMessage, 
          EmptyProcessHuluRequest, NULL, NULL, "dummy_hulu" }
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
        ASSERT_EQ(0, messenger[i].StartAccept(listening_fd, -1, NULL));
    }
    
    for (size_t i = 0; i < NCLIENT; ++i) {
        cm[i] = new ClientMeta;
        cm[i]->times = 0;
        cm[i]->bytes = 0;
        ASSERT_EQ(0, pthread_create(&cth[i], NULL, client_thread, cm[i]));
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
        pthread_join(cth[i], NULL);
        printf("joined client %lu\n", i);
    }
    for (size_t i = 0; i < NEPOLL; ++i) {
        messenger[i].StopAccept(0);
    }
    sleep(1);
    LOG(WARNING) << "begin to exit!!!!";
}
