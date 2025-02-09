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

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#ifdef BRPC_WITH_GPERFTOOLS
#include "butil/gperftools_profiler.h"
#endif // BRPC_WITH_GPERFTOOLS
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "butil/memory/scope_guard.h"
#include "bthread/bthread.h"
#include "brpc/event_dispatcher.h"
#include "brpc/socket.h"
#include "brpc/details/has_epollrdhup.h"
#include "brpc/versioned_ref_with_id.h"

class EventDispatcherTest : public ::testing::Test{
protected:
    EventDispatcherTest() = default;
    ~EventDispatcherTest() override = default;
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(EventDispatcherTest, has_epollrdhup) {
    LOG(INFO) << brpc::has_epollrdhup;
}

TEST_F(EventDispatcherTest, versioned_ref) {
    butil::atomic<uint64_t> versioned_ref(2);
    versioned_ref.fetch_add(brpc::MakeVRef(0, -1),
                            butil::memory_order_release);
    ASSERT_EQ(brpc::MakeVRef(1, 1), versioned_ref);
}

struct UserData;

UserData* g_user_data = NULL;

struct UserData : public brpc::VersionedRefWithId<UserData> {
    explicit UserData(Forbidden f)
        : brpc::VersionedRefWithId<UserData>(f)
        , count(0)
        , _additional_ref_released(false) {}

    int OnCreated() {
        count.store(1, butil::memory_order_relaxed);
        _additional_ref_released = false;
        g_user_data = this;
        return 0;
    }

    void BeforeRecycled() {
        count.store(0, butil::memory_order_relaxed);
        g_user_data = NULL;
    }

    void BeforeAdditionalRefReleased() {
        _additional_ref_released = true;
    }

    void OnFailed() {
        count.fetch_sub(1, butil::memory_order_relaxed);
    }

    void AfterRevived() {
        count.fetch_add(1, butil::memory_order_relaxed);
        _additional_ref_released = false;
    }

    butil::atomic<int> count;
    bool _additional_ref_released;
};

// Unique identifier of a UserData.
// Users shall store UserDataId instead of UserData and call UserData::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed UserData will not be recycled.
typedef brpc::VRefId UserDataId;

const brpc::VRefId INVALID_EVENT_DATA_ID = brpc::INVALID_VREF_ID;

typedef brpc::VersionedRefWithIdUniquePtr<UserData> UserDataUniquePtr;

volatile bool vref_thread_stop = false;
butil::atomic<int> g_count(1);

void TestVRef(UserDataId id) {
    UserDataUniquePtr ptr;
    ASSERT_EQ(0, UserData::Address(id, &ptr));
    ptr->count.fetch_add(1, butil::memory_order_relaxed);
    g_count.fetch_add(1, butil::memory_order_relaxed);
}

void* VRefThread(void* arg) {
    auto id = (UserDataId)arg;
    while (!vref_thread_stop) {
        TestVRef(id);
    }
    return NULL;
}

TEST_F(EventDispatcherTest, versioned_ref_with_id) {
    UserDataId id = INVALID_EVENT_DATA_ID;
    ASSERT_EQ(0, UserData::Create(&id));
    ASSERT_NE(INVALID_EVENT_DATA_ID, id);
    UserDataUniquePtr ptr;
    ASSERT_EQ(0, UserData::Address(id, &ptr));
    ASSERT_EQ(2, ptr->nref());
    ASSERT_FALSE(ptr->Failed());
    ASSERT_EQ(1, ptr->count);
    ASSERT_EQ(g_user_data, ptr.get());
    {
        UserDataUniquePtr temp_ptr;
        ASSERT_EQ(0, UserData::Address(id, &temp_ptr));
        ASSERT_EQ(ptr, temp_ptr);
        ASSERT_EQ(3, ptr->nref());
    }

    const size_t thread_num = 8;
    pthread_t tid[thread_num];
    for (auto& i : tid) {
        ASSERT_EQ(0, pthread_create(&i, NULL, VRefThread, (void*)id));
    }

    sleep(2);

    vref_thread_stop = true;
    for (const auto i : tid) {
        pthread_join(i, NULL);
    }

    ASSERT_EQ(2, ptr->nref());
    ASSERT_EQ(g_count, ptr->count);
    ASSERT_EQ(0, ptr->SetFailed());
    ASSERT_TRUE(ptr->Failed());
    ASSERT_EQ(g_count - 1, ptr->count);
    // Additional reference has been released.
    ASSERT_TRUE(ptr->_additional_ref_released);
    ASSERT_EQ(1, ptr->nref());
    {
        UserDataUniquePtr temp_ptr;
        ASSERT_EQ(1, UserData::AddressFailedAsWell(id, &temp_ptr));
        ASSERT_EQ(ptr, temp_ptr);
        ASSERT_EQ(2, ptr->nref());
    }
    ptr->Revive(1);
    ASSERT_EQ(2, ptr->nref());
    ASSERT_EQ(g_count, ptr->count);
    ASSERT_FALSE(ptr->_additional_ref_released);
    ASSERT_EQ(0, UserData::SetFailedById(id));
    ASSERT_EQ(g_count - 1, ptr->count);
    ptr.reset();
    ASSERT_EQ(nullptr, ptr);
    ASSERT_EQ(nullptr, g_user_data);
}

std::vector<int> err_fd;
pthread_mutex_t err_fd_mutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<int> rel_fd;
pthread_mutex_t rel_fd_mutex = PTHREAD_MUTEX_INITIALIZER;

volatile bool client_stop = false;

struct BAIDU_CACHELINE_ALIGNMENT ClientMeta {
    int fd;
    size_t times;
    size_t bytes;
};

struct BAIDU_CACHELINE_ALIGNMENT SocketExtra : public brpc::SocketUser {
    char* buf;
    size_t buf_cap;
    size_t bytes;
    size_t times;

    SocketExtra() {
        buf_cap = 32768;
        buf = (char*)malloc(buf_cap);
        bytes = 0;
        times = 0;
    }

    void BeforeRecycle(brpc::Socket* m) override {
        pthread_mutex_lock(&rel_fd_mutex);
        rel_fd.push_back(m->fd());
        pthread_mutex_unlock(&rel_fd_mutex);
        delete this;
    }

    static int OnEdgeTriggeredEventOnce(brpc::Socket* m) {
        SocketExtra* e = static_cast<SocketExtra*>(m->user());
        // Read all data.
        do {
            ssize_t n = read(m->fd(), e->buf, e->buf_cap);
            if (n == 0
#ifdef BRPC_SOCKET_HAS_EOF
                || m->_eof
#endif
                ) {
                pthread_mutex_lock(&err_fd_mutex);
                err_fd.push_back(m->fd());
                pthread_mutex_unlock(&err_fd_mutex);
                LOG(WARNING) << "Another end closed fd=" << m->fd();
                return -1;
            } else if (n > 0) {
                e->bytes += n;
                ++e->times;
#ifdef BRPC_SOCKET_HAS_EOF
                if ((size_t)n < e->buf_cap && brpc::has_epollrdhup) {
                    break;
                }
#endif
            } else {
                if (errno == EAGAIN) {
                    break;
                } else if (errno == EINTR) {
                    continue;
                } else {
                    PLOG(WARNING) << "Fail to read fd=" << m->fd();
                    return -1;
                }
            }
        } while (1);
        return 0;
    }

    static void OnEdgeTriggeredEvents(brpc::Socket* m) {
        int progress = brpc::Socket::PROGRESS_INIT;
        do {
            if (OnEdgeTriggeredEventOnce(m) != 0) {
                m->SetFailed();
                return;
            }
        } while (m->MoreReadEvents(&progress));
    }
};

void* client_thread(void* arg) {
    ClientMeta* m = (ClientMeta*)arg;
    size_t offset = 0;
    m->times = 0;
    m->bytes = 0;
    const size_t buf_cap = 32768;
    char* buf = (char*)malloc(buf_cap);
    for (size_t i = 0; i < buf_cap/8; ++i) {
        ((uint64_t*)buf)[i] = i;
    }
    while (!client_stop) {
        ssize_t n;
        if (offset == 0) {
            n = write(m->fd, buf, buf_cap);
        } else {
            iovec v[2];
            v[0].iov_base = buf + offset;
            v[0].iov_len = buf_cap - offset;
            v[1].iov_base = buf;
            v[1].iov_len = offset;
            n = writev(m->fd, v, 2);
        }
        if (n < 0) {
            if (errno != EINTR) {
                PLOG(WARNING) << "Fail to write fd=" << m->fd;
                break;
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
    EXPECT_EQ(0, close(m->fd));
    return NULL;
}

inline uint32_t fmix32 ( uint32_t h ) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

TEST_F(EventDispatcherTest, dispatch_tasks) {
#ifdef BUTIL_RESOURCE_POOL_NEED_FREE_ITEM_NUM
    const butil::ResourcePoolInfo old_info =
        butil::describe_resources<brpc::Socket>();
#endif

    client_stop = false;

    const size_t NCLIENT = 16;

    int fds[2 * NCLIENT];
    pthread_t cth[NCLIENT];
    ClientMeta* cm[NCLIENT];
    SocketExtra* sm[NCLIENT];

    for (size_t i = 0; i < NCLIENT; ++i) {
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds + 2 * i));
        sm[i] = new SocketExtra;

        const int fd = fds[i * 2];
        butil::make_non_blocking(fd);
        brpc::SocketId socket_id;
        brpc::SocketOptions options;
        options.fd = fd;
        options.user = sm[i];
        options.on_edge_triggered_events = SocketExtra::OnEdgeTriggeredEvents;

        ASSERT_EQ(0, brpc::Socket::Create(options, &socket_id));
        cm[i] = new ClientMeta;
        cm[i]->fd = fds[i * 2 + 1];
        cm[i]->times = 0;
        cm[i]->bytes = 0;
        ASSERT_EQ(0, pthread_create(&cth[i], NULL, client_thread, cm[i]));
    }
    
    LOG(INFO) << "Begin to profile... (5 seconds)";
#ifdef BRPC_WITH_GPERFTOOLS
    ProfilerStart("event_dispatcher.prof");
#endif // BRPC_WITH_GPERFTOOLS
    butil::Timer tm;
    tm.start();
    sleep(5);
    tm.stop();
#ifdef BRPC_WITH_GPERFTOOLS
    ProfilerStop();
    LOG(INFO) << "End profiling";
#endif // BRPC_WITH_GPERFTOOLS

    size_t client_bytes = 0;
    size_t server_bytes = 0;
    for (size_t i = 0; i < NCLIENT; ++i) {
        client_bytes += cm[i]->bytes;
        server_bytes += sm[i]->bytes;
    }
    LOG(INFO) << "client_tp=" << client_bytes / (double)tm.u_elapsed()
              << "MB/s server_tp=" << server_bytes / (double)tm.u_elapsed() 
              << "MB/s";

    client_stop = true;
    for (size_t i = 0; i < NCLIENT; ++i) {
        pthread_join(cth[i], NULL);
    }
    sleep(1);

    std::vector<int> copy1, copy2;
    pthread_mutex_lock(&err_fd_mutex);
    copy1.swap(err_fd);
    pthread_mutex_unlock(&err_fd_mutex);
    pthread_mutex_lock(&rel_fd_mutex);
    copy2.swap(rel_fd);
    pthread_mutex_unlock(&rel_fd_mutex);

    std::sort(copy1.begin(), copy1.end());
    std::sort(copy2.begin(), copy2.end());
    ASSERT_EQ(copy1.size(), copy2.size());
    for (size_t i = 0; i < copy1.size(); ++i) {
        ASSERT_EQ(copy1[i], copy2[i]) << i;
    }
    ASSERT_EQ(NCLIENT, copy1.size());
    const butil::ResourcePoolInfo info
        = butil::describe_resources<brpc::Socket>();
    LOG(INFO) << info;
#ifdef BUTIL_RESOURCE_POOL_NEED_FREE_ITEM_NUM
    ASSERT_EQ(NCLIENT, info.free_item_num - old_info.free_item_num);
#endif
}

// Unique identifier of a EventPipe.
// Users shall store EventFDId instead of EventPipe and call EventPipe::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed EventPipe will not be recycled.
typedef brpc::VRefId EventPipeId;

const brpc::VRefId INVALID_EVENT_PIPE_ID = brpc::INVALID_VREF_ID;

class EventPipe;
typedef brpc::VersionedRefWithIdUniquePtr<EventPipe> EventPipeUniquePtr;


class EventPipe : public brpc::VersionedRefWithId<EventPipe> {
public:
    explicit EventPipe(Forbidden f)
        : brpc::VersionedRefWithId<EventPipe>(f)
        , _pipe_fds{-1, -1}
        , _input_event_count(0)
        {}

    int Notify() {
        char c = 0;
        if (write(_pipe_fds[1], &c, 1) != 1) {
            PLOG(ERROR) << "Fail to write to _pipe_fds[1]";
            return -1;
        }
        return 0;
    }

private:
friend class VersionedRefWithId<EventPipe>;
friend class brpc::IOEvent<EventPipe>;

    int OnCreated() {
        if (pipe(_pipe_fds)) {
            PLOG(FATAL) << "Fail to create _pipe_fds";
            return -1;
        }
        if (_io_event.Init((void*)id()) != 0) {
            LOG(ERROR) << "Fail to init IOEvent";
            return -1;
        }
        _io_event.set_bthread_tag(bthread_self_tag());
        if (_io_event.AddConsumer(_pipe_fds[0]) != 0) {
            PLOG(ERROR) << "Fail to add SocketId=" << id()
                        << " into EventDispatcher";
            return -1;
        }


        _input_event_count = 0;
        return 0;
    }

    void BeforeRecycled() {
        brpc::GetGlobalEventDispatcher(_pipe_fds[0], bthread_self_tag())
            .RemoveConsumer(_pipe_fds[0]);
        _io_event.Reset();
        if (_pipe_fds[0] >= 0) {
            close(_pipe_fds[0]);
        }
        if (_pipe_fds[1] >= 0) {
            close(_pipe_fds[1]);
        }
    }

    static int OnInputEvent(void* user_data, uint32_t,
                            const bthread_attr_t&) {
        auto id = reinterpret_cast<EventPipeId>(user_data);
        EventPipeUniquePtr ptr;
        if (EventPipe::Address(id, &ptr) != 0) {
            LOG(WARNING) << "Fail to address EventPipe";
            return -1;
        }

        char buf[1024];
        ssize_t nr = read(ptr->_pipe_fds[0], &buf, arraysize(buf));
        if (nr <= 0) {
            if (errno == EAGAIN) {
                return 0;
            } else {
                PLOG(WARNING) << "Fail to read from _pipe_fds[0]";
                ptr->SetFailed();
                return -1;
            }
        }

        ptr->_input_event_count += nr;
        return 0;
    }

    static int OnOutputEvent(void*, uint32_t,
                             const bthread_attr_t&) {
        EXPECT_TRUE(false) << "Should not be called";
        return 0;
    }

    brpc::IOEvent<EventPipe> _io_event;
    int _pipe_fds[2];

    size_t _input_event_count;
};

TEST_F(EventDispatcherTest, customize_dispatch_task) {
    EventPipeId id = INVALID_EVENT_PIPE_ID;
    ASSERT_EQ(0, EventPipe::Create(&id));
    ASSERT_NE(INVALID_EVENT_PIPE_ID, id);
    EventPipeUniquePtr ptr;
    ASSERT_EQ(0, EventPipe::Address(id, &ptr));
    ASSERT_EQ(2, ptr->nref());
    ASSERT_FALSE(ptr->Failed());

    ASSERT_EQ((size_t)0, ptr->_input_event_count);
    const size_t N = 10000;
    for (size_t i = 0; i < N; ++i) {
        ASSERT_EQ(0, ptr->Notify());
    }
    usleep(1000 * 50);
    ASSERT_EQ(N, ptr->_input_event_count);

    ASSERT_EQ(0, ptr->SetFailed());
    ASSERT_TRUE(ptr->Failed());
    ptr.reset();
    ASSERT_EQ(nullptr, ptr);
    ASSERT_NE(0, EventPipe::Address(id, &ptr));
}
