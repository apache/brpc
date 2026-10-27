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
#include <gflags/gflags.h>
#include "gperftools_helper.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "butil/memory/scope_guard.h"
#include "bthread/bthread.h"
#include "bthread/task_control.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/event_dispatcher.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/details/has_epollrdhup.h"
#include "brpc/versioned_ref_with_id.h"
#include "echo.pb.h"

DECLARE_int32(task_group_ntags);
DECLARE_int32(event_dispatcher_num);

namespace bthread {
extern TaskControl* g_task_control;
}

// Far more dispatchers than the workers the reserve tests at the end of this
// file configure, which is exactly the situation that used to deadlock.
static const int NUM_EVENT_DISPATCHERS = 16;

int main(int argc, char* argv[]) {
    FLAGS_event_dispatcher_num = NUM_EVENT_DISPATCHERS;
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

class EventDispatcherTest : public ::testing::Test{
protected:
    EventDispatcherTest() = default;
    ~EventDispatcherTest() override = default;
    void SetUp() override {}
    void TearDown() override {}
};

void set_threadsafe_death_test_style() {
    // Re-exec instead of forking a process with live bthread workers.
    // googletest >= 1.12 built with Abseil (`bazel --define absl=1`) exposes
    // the flag as an `absl::Flag`, which can only be set via `GTEST_FLAG_SET`.
#ifdef GTEST_FLAG_SET
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#else
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
#endif
}

TEST(EventDispatcherDeathTest, reservation_failure_uses_fatal_logging) {
    set_threadsafe_death_test_style();
#if !BRPC_WITH_GLOG
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
                     "crash_on_fatal_log", "true").empty());
#endif
    ASSERT_DEATH({
        // Exceed the independent reserved budget. Fatal logging must stop
        // initialization when the logging backend is configured to crash.
        FLAGS_event_dispatcher_num = BTHREAD_MAX_RESERVED_CONCURRENCY + 1;
        brpc::GetGlobalEventDispatcher(0, BTHREAD_TAG_DEFAULT);
    }, "Fail to reserve .* workers for event dispatchers");
}

void check_max_concurrency_with_dispatcher(bool dispatcher_first) {
    FLAGS_event_dispatcher_num = 1;
    if (dispatcher_first) {
        brpc::GetGlobalEventDispatcher(0, BTHREAD_TAG_DEFAULT);
    }
    ASSERT_EQ(0, bthread_setconcurrency_by_tag(
        BTHREAD_MAX_CONCURRENCY, BTHREAD_TAG_DEFAULT));
    if (!dispatcher_first) {
        brpc::GetGlobalEventDispatcher(0, BTHREAD_TAG_DEFAULT);
    }
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY,
              bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(1, bthread::g_task_control->reserved_concurrency(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY + 1,
              bthread::g_task_control->concurrency(BTHREAD_TAG_DEFAULT));

    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(
        &tid, nullptr, [](void*) -> void* { return nullptr; }, nullptr));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
}

TEST(EventDispatcherDeathTest, max_concurrency_supports_both_initialization_orders) {
    set_threadsafe_death_test_style();
    for (bool dispatcher_first : {false, true}) {
        SCOPED_TRACE(dispatcher_first);
        ASSERT_EXIT({
            check_max_concurrency_with_dispatcher(dispatcher_first);
            _exit(::testing::Test::HasFailure() ? 1 : 0);
        }, ::testing::ExitedWithCode(0), "");
    }
}

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

UserData* g_user_data = nullptr;

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
        g_user_data = nullptr;
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
    return nullptr;
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
        ASSERT_EQ(0, pthread_create(&i, nullptr, VRefThread, (void*)id));
    }

    sleep(2);

    vref_thread_stop = true;
    for (const auto i : tid) {
        pthread_join(i, nullptr);
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

    ~SocketExtra() {
        free(buf);
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
    free(buf);
    EXPECT_EQ(0, close(m->fd));
    return nullptr;
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
    brpc::SocketId socket_ids[NCLIENT];

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
        socket_ids[i] = socket_id;
        cm[i] = new ClientMeta;
        cm[i]->fd = fds[i * 2 + 1];
        cm[i]->times = 0;
        cm[i]->bytes = 0;
        ASSERT_EQ(0, pthread_create(&cth[i], nullptr, client_thread, cm[i]));
    }
    
    LOG(INFO) << "Begin to profile... (5 seconds)";
    ProfilerStart("event_dispatcher.prof");
    butil::Timer tm;
    tm.start();
    sleep(5);
    tm.stop();
    ProfilerStop();
    LOG(INFO) << "End profiling";

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
        pthread_join(cth[i], nullptr);
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

    // Release sockets (SocketExtra::BeforeRecycle deletes the user) and the
    // per-client metadata to avoid leaking them.
    for (size_t i = 0; i < NCLIENT; ++i) {
        brpc::SocketUniquePtr s;
        if (brpc::Socket::Address(socket_ids[i], &s) == 0) {
            s->SetFailed();
        }
        delete cm[i];
    }
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

// If those workers were taken out of the concurrency configured by the user, a
// process with `event_dispatcher_num >= num_threads` would have no worker to
// run anything else, and would never even be able to quit.

// Must stay below `NUM_EVENT_DISPATCHERS` and above the default
// `-bthread_concurrency`, as concurrency can only ever grow.
static const int NUM_THREADS = 10;
static const int RESERVE_PORT = 8798;

class ReserveEchoServiceImpl : public test::EchoService {
public:
    void Echo(google::protobuf::RpcController*,
              const test::EchoRequest* req,
              test::EchoResponse* res,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        res->set_message(req->message());
    }
};

struct StopArgs {
    brpc::Server* server;
    butil::atomic<bool> done;
    explicit StopArgs(brpc::Server* s) : server(s), done(false) {}
};

void* stop_and_join(void* arg) {
    StopArgs* args = static_cast<StopArgs*>(arg);
    args->server->Stop(0);
    args->server->Join();
    args->done.store(true, butil::memory_order_release);
    return nullptr;
}

// Dispatchers need extra workers beyond the configured normal concurrency.
TEST_F(EventDispatcherTest, dispatchers_do_not_consume_configured_concurrency) {
    ASSERT_EQ(NUM_EVENT_DISPATCHERS, FLAGS_event_dispatcher_num);
    // Force initialization of the global dispatchers.
    brpc::GetGlobalEventDispatcher(0, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(bthread::g_task_control != nullptr);

    int expected = FLAGS_task_group_ntags * FLAGS_event_dispatcher_num;
    // The internal `bthread::EpollThread` also reserves a worker. It may not
    // have started yet, so compare with `>=`.
    ASSERT_GE(bthread::g_task_control->reserved_concurrency(), expected);
    // Reserved workers are on top of the configured concurrency, not inside it.
    int normal = bthread_getconcurrency();
    ASSERT_EQ(normal, bthread::g_task_control->normal_concurrency());
    ASSERT_GE(bthread::g_task_control->concurrency(), normal + expected);
}

// End-to-end: a server whose `num_threads` is smaller than the number of event
// dispatchers must still serve requests and, crucially, be able to quit.
TEST_F(EventDispatcherTest, server_with_fewer_threads_than_dispatchers_can_quit) {
    brpc::Server server;
    ReserveEchoServiceImpl service;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    options.num_threads = NUM_THREADS;
    ASSERT_EQ(0, server.Start(RESERVE_PORT, &options));
    // `num_threads` workers are available to normal bthreads, on top of the
    // ones reserved for the dispatchers.
    ASSERT_EQ(NUM_THREADS, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));

    brpc::Channel channel;
    brpc::ChannelOptions copt;
    copt.timeout_ms = 3000;
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1", RESERVE_PORT, &ep));
    ASSERT_EQ(0, channel.Init(ep, &copt));

    test::EchoService_Stub stub(&channel);
    for (int i = 0; i < 20; ++i) {
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message("hello");
        stub.Echo(&cntl, &req, &res, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ("hello", res.message());
    }

    // Previously, `Stop()`/`Join()` could not schedule their bthreads because
    // every worker was stuck in `epoll_wait`.
    StopArgs args(&server);
    pthread_t th;
    ASSERT_EQ(0, pthread_create(&th, nullptr, stop_and_join, &args));
    int64_t deadline_us = butil::gettimeofday_us() + 10 * 1000000L;
    while (!args.done.load(butil::memory_order_acquire) &&
           butil::gettimeofday_us() < deadline_us) {
        usleep(10000);
    }
    ASSERT_TRUE(args.done.load(butil::memory_order_acquire))
        << "Server::Stop()/Join() did not finish in 10s, no worker available?";
    ASSERT_EQ(0, pthread_join(th, nullptr));
}
