// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/fd_utility.h"
#include "brpc/event_dispatcher.h"
#include "brpc/details/has_epollrdhup.h"

class EventDispatcherTest : public ::testing::Test{
protected:
    EventDispatcherTest(){
    };
    virtual ~EventDispatcherTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
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

    virtual void BeforeRecycle(brpc::Socket* m) {
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
