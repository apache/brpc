// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <gtest/gtest.h>
#include "butil/gperftools_profiler.h"
#include "butil/time.h"
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/socket.h"
#include "brpc/policy/round_robin_load_balancer.h"
#include "brpc/policy/randomized_load_balancer.h"
#include "brpc/policy/locality_aware_load_balancer.h"
#include "brpc/policy/consistent_hashing_load_balancer.h"
#include "brpc/policy/hasher.h"

namespace brpc {
namespace policy {
DECLARE_bool(count_inflight);
extern uint32_t CRCHash32(const char *key, size_t len);
}}

namespace {
void initialize_random() {
    srand(time(0));
}
pthread_once_t initialize_random_control = PTHREAD_ONCE_INIT;

class LoadBalancerTest : public ::testing::Test{
protected:
    LoadBalancerTest(){
        pthread_once(&initialize_random_control, initialize_random);
    };
    virtual ~LoadBalancerTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

size_t TLS_ctor = 0;
size_t TLS_dtor = 0;
struct TLS {
    TLS() {
        ++TLS_ctor;
    }
    ~TLS() {
        ++TLS_dtor;
    }

};

struct Foo {
    Foo() : x(0) {}
    int x;
};

bool AddN(Foo& f, int n) {
    f.x += n;
    return true;
}

TEST_F(LoadBalancerTest, doubly_buffered_data) {
    const size_t old_TLS_ctor = TLS_ctor;
    const size_t old_TLS_dtor = TLS_dtor;
    {
        butil::DoublyBufferedData<Foo, TLS> d2;
        butil::DoublyBufferedData<Foo, TLS>::ScopedPtr ptr;
        d2.Read(&ptr);
        ASSERT_EQ(old_TLS_ctor + 1, TLS_ctor);
    }
    ASSERT_EQ(old_TLS_ctor + 1, TLS_ctor);
    ASSERT_EQ(old_TLS_dtor + 1, TLS_dtor);

    butil::DoublyBufferedData<Foo> d;
    {
        butil::DoublyBufferedData<Foo>::ScopedPtr ptr;
        ASSERT_EQ(0, d.Read(&ptr));
        ASSERT_EQ(0, ptr->x);
    }
    {
        butil::DoublyBufferedData<Foo>::ScopedPtr ptr;
        ASSERT_EQ(0, d.Read(&ptr));
        ASSERT_EQ(0, ptr->x);
    }

    d.Modify(AddN, 10);
    {
        butil::DoublyBufferedData<Foo>::ScopedPtr ptr;
        ASSERT_EQ(0, d.Read(&ptr));
        ASSERT_EQ(10, ptr->x);
    }
}

typedef brpc::policy::LocalityAwareLoadBalancer LALB;

static void ValidateWeightTree(
    std::vector<LALB::ServerInfo> & weight_tree) {
    std::vector<int64_t> weight_sum;
    weight_sum.resize(weight_tree.size());
    for (ssize_t i = weight_tree.size() - 1; i >= 0; --i) {
        const size_t left_child = i * 2 + 1;
        const size_t right_child = i * 2 + 2;
        weight_sum[i] = weight_tree[i].weight->volatile_value();
        if (left_child < weight_sum.size()) {
            weight_sum[i] += weight_sum[left_child];
        }
        if (right_child < weight_sum.size()) {
            weight_sum[i] += weight_sum[right_child];
        }
    }
    for (size_t i = 0; i < weight_tree.size(); ++i) {
        const int64_t left = weight_tree[i].left->load(butil::memory_order_relaxed);
        size_t left_child = i * 2 + 1;
        if (left_child < weight_tree.size()) {
            ASSERT_EQ(weight_sum[left_child], left) << "i=" << i;
        } else {
            ASSERT_EQ(0, left);
        }
    }
}

static void ValidateLALB(LALB& lalb, size_t N) {
    LALB::Servers* d = lalb._db_servers._data;
    for (size_t R = 0; R < 2; ++R) {
        ASSERT_EQ(d[R].weight_tree.size(), N);
        ASSERT_EQ(d[R].server_map.size(), N);
    }
    ASSERT_EQ(lalb._left_weights.size(), N);
    int64_t total = 0;
    for (size_t i = 0; i < N; ++i) {
        ASSERT_EQ(d[0].weight_tree[i].server_id, d[1].weight_tree[i].server_id);
        ASSERT_EQ(d[0].weight_tree[i].weight, d[1].weight_tree[i].weight);
        for (size_t R = 0; R < 2; ++R) {
            ASSERT_EQ((int64_t*)d[R].weight_tree[i].left, &lalb._left_weights[i]);
            size_t* pindex = d[R].server_map.seek(d[R].weight_tree[i].server_id);
            ASSERT_TRUE(pindex != NULL && *pindex == i);
        }
        total += d[0].weight_tree[i].weight->volatile_value();
    }
    ValidateWeightTree(d[0].weight_tree);
    ASSERT_EQ(total, lalb._total.load());
}

TEST_F(LoadBalancerTest, la_sanity) {
    LALB lalb;
    ASSERT_EQ(0, lalb._total.load());
    std::vector<brpc::ServerId> ids;
    const size_t N = 256;
    size_t cur_count = 0;

    for (int REP = 0; REP < 5; ++REP) {
        const size_t before_adding = cur_count;
        for (; cur_count < N; ++cur_count) {
            char addr[32];
            snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)cur_count);
            butil::EndPoint dummy;
            ASSERT_EQ(0, str2endpoint(addr, &dummy));
            brpc::ServerId id(8888);
            brpc::SocketOptions options;
            options.remote_side = dummy;
            ASSERT_EQ(0, brpc::Socket::Create(options, &id.id));
            ids.push_back(id);
            ASSERT_TRUE(lalb.AddServer(id));
        }
        std::cout << "Added " << cur_count - before_adding << std::endl;
        ValidateLALB(lalb, cur_count);

        const size_t before_removal = cur_count;
        std::random_shuffle(ids.begin(), ids.end());
        for (size_t i = 0; i < N / 2; ++i) {
            const brpc::ServerId id = ids.back();
            ids.pop_back();
            --cur_count;
            ASSERT_TRUE(lalb.RemoveServer(id)) << "i=" << i;
            ASSERT_EQ(0, brpc::Socket::SetFailed(id.id));
        }
        std::cout << "Removed " << before_removal - cur_count << std::endl;
        ValidateLALB(lalb, cur_count);
    }
    
    for (size_t i = 0; i < ids.size(); ++i) {
        ASSERT_EQ(0, brpc::Socket::SetFailed(ids[i].id));
    }
}

typedef std::map<brpc::SocketId, int> CountMap;
volatile bool global_stop = false;

struct SelectArg {
    brpc::LoadBalancer *lb;
    uint32_t (*hash)(const void*, size_t);
};

void* select_server(void* arg) {
    SelectArg *sa = (SelectArg *)arg;
    brpc::LoadBalancer* c = sa->lb;
    brpc::SocketUniquePtr ptr;
    CountMap *selected_count = new CountMap;
    brpc::LoadBalancer::SelectIn in = { 0, false, 0u, NULL };
    brpc::LoadBalancer::SelectOut out(&ptr);
    uint32_t rand_seed = rand();
    if (sa->hash) {
        uint32_t rd = ++rand_seed;
        in.has_request_code = true;
        in.request_code = sa->hash((const char *)&rd, sizeof(uint32_t));
    }
    int ret = 0;
    while (!global_stop && (ret = c->SelectServer(in, &out)) == 0) {
        if (sa->hash) {
            uint32_t rd = ++rand_seed;
            in.has_request_code = true;
            in.request_code = sa->hash((const char *)&rd, sizeof(uint32_t));
        }
        ++(*selected_count)[ptr->id()];
    }
    LOG_IF(INFO, ret != 0) << "select_server[" << pthread_self()
                            << "] quits before of " << berror(ret);
    return selected_count;
}

brpc::SocketId recycled_sockets[1024];
butil::atomic<size_t> nrecycle(0);
class SaveRecycle : public brpc::SocketUser {
    void BeforeRecycle(brpc::Socket* s) {
        recycled_sockets[nrecycle.fetch_add(1, butil::memory_order_relaxed)] = s->id();
        delete this;
    }
};

TEST_F(LoadBalancerTest, update_while_selection) {
    const bool saved = brpc::policy::FLAGS_count_inflight;
    brpc::policy::FLAGS_count_inflight = false;
    
    for (size_t round = 0; round < 4; ++round) {
        brpc::LoadBalancer* lb = NULL;
        SelectArg sa = { NULL, NULL};
        bool is_lalb = false;
        if (round == 0) {
            lb = new brpc::policy::RoundRobinLoadBalancer;
        } else if (round == 1) {
            lb = new brpc::policy::RandomizedLoadBalancer;
        } else if (round == 2) {
            lb = new LALB;
            is_lalb = true;
        } else {
            lb = new brpc::policy::ConsistentHashingLoadBalancer(
                        ::brpc::policy::MurmurHash32);
            sa.hash = ::brpc::policy::MurmurHash32;
        }
        sa.lb = lb;

        // Accessing empty lb should result in error.
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectIn in = { 0, true, 0, NULL };
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(ENODATA, lb->SelectServer(in, &out));

        nrecycle = 0;
        global_stop = false;
        pthread_t th[8];
        std::vector<brpc::ServerId> ids;
        for (int i = 0; i < 256; ++i) {
            char addr[32];
            snprintf(addr, sizeof(addr), "192.%d.1.%d:8080", i, i);
            butil::EndPoint dummy;
            ASSERT_EQ(0, str2endpoint(addr, &dummy));
            brpc::ServerId id(8888);
            brpc::SocketOptions options;
            options.remote_side = dummy;
            options.user = new SaveRecycle;
            ASSERT_EQ(0, brpc::Socket::Create(options, &id.id));
            ids.push_back(id);
            ASSERT_TRUE(lb->AddServer(id));
        }
        std::cout << "Time " << butil::class_name_str(*lb) << " ..." << std::endl;
        butil::Timer tm;
        tm.start();
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            ASSERT_EQ(0, pthread_create(&th[i], NULL, select_server, &sa));
        }
        std::vector<brpc::ServerId> removed;
        const size_t REP = 200;
        for (size_t k = 0; k < REP; ++k) {
            removed = ids;
            std::random_shuffle(removed.begin(), removed.end());
            removed.pop_back();
            ASSERT_EQ(removed.size(), lb->RemoveServersInBatch(removed));
            ASSERT_EQ(removed.size(), lb->AddServersInBatch(removed));
            // // 1: Don't remove first server, otherwise select_server would quit.
            // for (size_t i = 1/*1*/; i < removed.size(); ++i) {
            //    ASSERT_TRUE(lb->RemoveServer(removed[i]));
            // }
            // for (size_t i = 1; i < removed.size(); ++i) {
            //    ASSERT_TRUE(lb->AddServer(removed[i]));
            // }
            if (is_lalb) {
                LALB* lalb = (LALB*)lb;
                ValidateLALB(*lalb, ids.size());
                ASSERT_GT(lalb->_total.load(), 0);
            }
        }
        global_stop = true;
        LOG(INFO) << "Stop all...";
        
        void* retval[ARRAY_SIZE(th)];
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            ASSERT_EQ(0, pthread_join(th[i], &retval[i]));
        }
        tm.stop();
        
        CountMap total_count;
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            CountMap* selected_count = (CountMap*)retval[i];
            size_t count = 0;
            for (CountMap::const_iterator it = selected_count->begin();
                 it != selected_count->end(); ++it) {
                total_count[it->first] += it->second;
                count += it->second;
            }
            delete selected_count;

            std::cout << "thread " << i << " selected "
                      << count * 1000000L / tm.u_elapsed() << " times/s"
                      << std::endl;
        }
        ASSERT_EQ(ids.size(), total_count.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_NE(0, total_count[ids[i].id]) << "i=" << i;
            std::cout << i << "=" << total_count[ids[i].id] << " ";
        }
        std::cout << std::endl;
        
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_EQ(0, brpc::Socket::SetFailed(ids[i].id));
        }
        ASSERT_EQ(ids.size(), nrecycle);
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_EQ(1UL, total_count.erase(recycled_sockets[i]));
        }
        delete lb;
    }
    brpc::policy::FLAGS_count_inflight = saved;
}

TEST_F(LoadBalancerTest, fairness) {
    const bool saved = brpc::policy::FLAGS_count_inflight;
    brpc::policy::FLAGS_count_inflight = false;
    for (size_t round = 0; round < 4; ++round) {
        brpc::LoadBalancer* lb = NULL;
        SelectArg sa = { NULL, NULL};
        if (round == 0) {
            lb = new brpc::policy::RoundRobinLoadBalancer;
        } else if (round == 1) {
            lb = new brpc::policy::RandomizedLoadBalancer;
        } else if (round == 2) {
            lb = new LALB;
        } else {
            lb = new brpc::policy::ConsistentHashingLoadBalancer(
                    brpc::policy::MurmurHash32);
            sa.hash = brpc::policy::MurmurHash32;
        }
        sa.lb = lb;
        
        std::string lb_name = butil::class_name_str(*lb);
        // Remove namespace
        size_t ns_pos = lb_name.find_last_of(':');
        if (ns_pos != std::string::npos) {
            lb_name = lb_name.substr(ns_pos + 1);
        }

        nrecycle = 0;
        global_stop = false;
        pthread_t th[8];
        std::vector<brpc::ServerId> ids;
        for (int i = 0; i < 256; ++i) {
            char addr[32];
            snprintf(addr, sizeof(addr), "192.168.1.%d:8080", i);
            butil::EndPoint dummy;
            ASSERT_EQ(0, str2endpoint(addr, &dummy));
            brpc::ServerId id(8888);
            brpc::SocketOptions options;
            options.remote_side = dummy;
            options.user = new SaveRecycle;
            ASSERT_EQ(0, brpc::Socket::Create(options, &id.id));
            ids.push_back(id);
            lb->AddServer(id);
        }

        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            ASSERT_EQ(0, pthread_create(&th[i], NULL, select_server, &sa));
        }
        bthread_usleep(10000);
        ProfilerStart((lb_name + ".prof").c_str());
        bthread_usleep(300000);
        ProfilerStop();

        global_stop = true;
        
        CountMap total_count;
        for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
            void* retval;
            ASSERT_EQ(0, pthread_join(th[i], &retval));
            CountMap* selected_count = (CountMap*)retval;
            ASSERT_TRUE(selected_count);
            int first_count = 0;
            for (CountMap::const_iterator it = selected_count->begin();
                 it != selected_count->end(); ++it) {
                if (round == 0) {
                    if (first_count == 0) {
                        first_count = it->second;
                    } else {
                        // Load is not ensured to be fair inside each thread
                        // ASSERT_LE(abs(first_count - it->second), 1);
                    }
                }
                total_count[it->first] += it->second;
            }
            delete selected_count;
        }
        ASSERT_EQ(ids.size(), total_count.size());
        size_t count_sum = 0;
        size_t count_squared_sum = 0;
        std::cout << lb_name << ':' << '\n';
        for (size_t i = 0; i < ids.size(); ++i) {
            size_t count = total_count[ids[i].id];
            ASSERT_NE(0ul, count) << "i=" << i;
            std::cout << i << '=' << count << ' ';
            count_sum += count;
            count_squared_sum += count * count;
        }

        std::cout << '\n'
                  << ": average=" << count_sum/ids.size()
                  << " deviation=" << sqrt(count_squared_sum * ids.size() - count_sum * count_sum) / ids.size() << std::endl;
        
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_EQ(0, brpc::Socket::SetFailed(ids[i].id));
        }
        ASSERT_EQ(ids.size(), nrecycle);
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_EQ(1UL, total_count.erase(recycled_sockets[i]));
        }
        delete lb;
    }
    brpc::policy::FLAGS_count_inflight = saved;
}

TEST_F(LoadBalancerTest, consistent_hashing) {
    ::brpc::policy::ConsistentHashingLoadBalancer::HashFunc hashs[] = {
            ::brpc::policy::MurmurHash32, 
            ::brpc::policy::MD5Hash32
            // ::brpc::policy::CRCHash32 crc is a bad hash function in test
    };
    const char* servers[] = { 
            "10.92.115.19:8833", 
            "10.42.108.25:8833", 
            "10.36.150.32:8833", 
            "10.92.149.48:8833", 
            "10.42.122.201:8833",
    };
    for (size_t round = 0; round < ARRAY_SIZE(hashs); ++round) {
        brpc::policy::ConsistentHashingLoadBalancer chlb(hashs[round]);
        std::vector<brpc::ServerId> ids;
        std::vector<butil::EndPoint> addrs;
        for (int j = 0;j < 5; ++j) 
        for (int i = 0; i < 5; ++i) {
            const char *addr = servers[i];
            //snprintf(addr, sizeof(addr), "192.168.1.%d:8080", i);
            butil::EndPoint dummy;
            ASSERT_EQ(0, str2endpoint(addr, &dummy));
            brpc::ServerId id(8888);
            brpc::SocketOptions options;
            options.remote_side = dummy;
            options.user = new SaveRecycle;
            ASSERT_EQ(0, brpc::Socket::Create(options, &id.id));
            ids.push_back(id);
            addrs.push_back(dummy);
            chlb.AddServer(id);
        }
        std::cout << chlb;
        for (int i = 0; i < 5; ++i) {
            std::vector<brpc::ServerId> empty;
            chlb.AddServersInBatch(empty);
            chlb.RemoveServersInBatch(empty);
            std::cout << chlb;
        }
        const size_t SELECT_TIMES = 1000000;
        std::map<butil::EndPoint, size_t> times;
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectIn in = { 0, false, 0u, NULL };
        ::brpc::LoadBalancer::SelectOut out(&ptr);
        for (size_t i = 0; i < SELECT_TIMES; ++i) {
            in.has_request_code = true;
            in.request_code = hashs[round]((const char *)&i, sizeof(i));
            chlb.SelectServer(in, &out);
            ++times[ptr->remote_side()];
        }
        std::map<butil::EndPoint, double> load_map;
        chlb.GetLoads(&load_map);
        ASSERT_EQ(times.size(), load_map.size());
        double load_sum = 0;;
        double load_sqr_sum = 0;
        for (size_t i = 0; i < addrs.size(); ++i) {
            double normalized_load = 
                    (double)times[addrs[i]] / SELECT_TIMES / load_map[addrs[i]];
            std::cout << i << '=' << normalized_load << ' ';
            load_sum += normalized_load;
            load_sqr_sum += normalized_load * normalized_load;
        }
        std::cout << '\n';
        std::cout << "average_normalized_load=" << load_sum / addrs.size()
                  << " deviation=" 
                  << sqrt(load_sqr_sum * addrs.size() - load_sum * load_sum) / addrs.size()
                  << '\n';
        for (size_t i = 0; i < ids.size(); ++i) {
            ASSERT_EQ(0, brpc::Socket::SetFailed(ids[i].id));
        }
    }
}
} //namespace
