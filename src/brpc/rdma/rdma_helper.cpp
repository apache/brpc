// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#endif
#include <arpa/inet.h>
#include <dlfcn.h>                                // dlopen
#include <fcntl.h>
#include <ifaddrs.h>
#include <pthread.h>
#include <stdlib.h>
#include <vector>
#include <gflags/gflags.h>
#include "butil/containers/flat_map.h"            // butil::FlatMap
#include "butil/endpoint.h"
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"                     // butil::make_non_blocking
#include "butil/logging.h"
#include "butil/string_printf.h"
#include "bthread/bthread.h"
#include "brpc/socket.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_completion_queue.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_fallback_channel.h"
#include "brpc/rdma/rdma_helper.h"

namespace butil {
namespace iobuf {
// declared in iobuf.cpp
extern void* (*blockmem_allocate)(size_t);
extern void  (*blockmem_deallocate)(void*);
}
}

namespace brpc {
namespace rdma {

void* g_handle_rdmacm = NULL;
void* g_handle_ibverbs = NULL;

// RDMA-related functions
#ifdef BRPC_RDMA
ibv_context** (*RdmaGetDevices)(int*) = NULL;
void (*RdmaFreeDevices)(ibv_context**) = NULL;
int (*RdmaCreateId)(rdma_event_channel*, rdma_cm_id**, void*, rdma_port_space) = NULL;
int (*RdmaDestroyId)(rdma_cm_id*) = NULL;
int (*RdmaResolveAddr)(rdma_cm_id*, sockaddr*, sockaddr*, int) = NULL;
int (*RdmaBindAddr)(rdma_cm_id*, sockaddr*) = NULL;
int (*RdmaResolveRoute)(rdma_cm_id*, int) = NULL;
int (*RdmaListen)(rdma_cm_id*, int) = NULL;
int (*RdmaConnect)(rdma_cm_id*, rdma_conn_param*) = NULL;
int (*RdmaGetRequest)(rdma_cm_id*, rdma_cm_id**) = NULL;
int (*RdmaAccept)(rdma_cm_id*, rdma_conn_param*) = NULL;
int (*RdmaDisconnect)(rdma_cm_id*) = NULL;
int (*RdmaGetCmEvent)(rdma_event_channel*, rdma_cm_event**) = NULL;
int (*RdmaAckCmEvent)(rdma_cm_event*) = NULL;
int (*RdmaCreateQp)(rdma_cm_id*, ibv_pd*, ibv_qp_init_attr*) = NULL;

int (*IbvForkInit)(void) = NULL;
int (*IbvQueryDevice)(ibv_context*, ibv_device_attr*) = NULL;
int (*IbvQueryPort)(ibv_context*, uint8_t, ibv_port_attr*) = NULL;
ibv_pd* (*IbvAllocPd)(ibv_context*) = NULL;
int (*IbvDeallocPd)(ibv_pd*) = NULL;
ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int) = NULL;
int (*IbvDestroyCq)(ibv_cq*) = NULL;
ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*) = NULL;
int (*IbvDestroyCompChannel)(ibv_comp_channel*) = NULL;
ibv_mr* (*IbvRegMr)(ibv_pd*, void*, size_t, ibv_access_flags) = NULL;
int (*IbvDeregMr)(ibv_mr*) = NULL;
int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**) = NULL;
void (*IbvAckCqEvents)(ibv_cq*, unsigned int) = NULL;
int (*IbvGetAsyncEvent)(ibv_context*, ibv_async_event*) = NULL;
void (*IbvAckAsyncEvent)(ibv_async_event*) = NULL;
int (*IbvDestroyQp)(ibv_qp*) = NULL;
#endif

// NOTE:
// ibv_post_send, ibv_post_recv, ibv_poll_cq, ibv_req_notify_cq are all inline function
// defined in infiniband/verbs.h.

// declared in rdma_completion_queue.cpp
extern int g_cq_num;

static in_addr g_rdma_ip = { 0 };
static int g_max_sge = 0;

static butil::atomic<bool> g_rdma_available(false);

#ifdef BRPC_RDMA
DECLARE_bool(rdma_disable_local_connection);
DEFINE_int32(rdma_max_sge, 0, "Max SGE num in a WR");
DEFINE_string(rdma_cluster, "0.0.0.0/0",
              "The ip address prefix of current cluster which supports RDMA");
DEFINE_string(rdma_device, "", "The name of the HCA device used "
                               "(Empty means using the first active device)");
DEFINE_string(network_name, "", "If bonding mode, must specify network name");

struct RdmaCluster {
    uint32_t ip;
    uint32_t mask;
};

static RdmaCluster g_cluster = { 0, 0 };

static const size_t SYSFS_SIZE = 4096;
static ibv_context** g_devices = NULL;
static ibv_context* g_context = NULL;
static SocketId g_async_socket;
static ibv_pd* g_pd = NULL;
static std::vector<ibv_mr*>* g_mrs = NULL;

// Store the original IOBuf memalloc and memdealloc functions
static void* (*g_mem_alloc)(size_t) = NULL;
static void (*g_mem_dealloc)(void*) = NULL;

butil::Mutex* g_addr_map_lock;
typedef butil::FlatMap<const void*, ibv_mr*> AddrMap;
static AddrMap* g_addr_map = NULL;

// Read sysfs file
static ssize_t ReadFile(std::string& path, void* data) {
    butil::fd_guard fd(open(path.c_str(), O_RDONLY));
    if (fd < 0) {
        return -1;
    }
    return read(fd, data, SYSFS_SIZE);
}

static void GlobalRelease() {
    g_rdma_available.store(false, butil::memory_order_release);
    sleep(1);  // to avoid unload library too early

    RdmaFallbackChannel::GlobalStop();

    // We do not set `g_async_socket' to failed explicitly to avoid
    // close async_fd twice.

    if (g_addr_map_lock) {
        BAIDU_SCOPED_LOCK(*g_addr_map_lock);
        if (g_addr_map) {
            for (AddrMap::iterator it = g_addr_map->begin();
                    it != g_addr_map->end(); ++it) {
                IbvDeregMr(it->second);
            }
            delete g_addr_map;
            g_addr_map = NULL;  // must set it to NULL
        }
    }

    GlobalCQRelease();

    if (g_mrs) {
        for (size_t i = 0; i < g_mrs->size(); ++i) {
            IbvDeregMr((*g_mrs)[i]);
        }
        delete g_mrs;
        g_mrs = NULL;
    }

    if (g_pd) {
        IbvDeallocPd(g_pd);
        g_pd = NULL;
    }

    if (g_context) {
        g_context = NULL;
    }

    if (g_devices) {
        RdmaFreeDevices(g_devices);
        g_devices = NULL;
    }
}

uint32_t RdmaRegisterMemory(void* buf, size_t size) {
    // Register the memory as callback in block_pool
    // The thread-safety should be guaranteed by the caller
    ibv_mr* mr = IbvRegMr(g_pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        PLOG(ERROR) << "Fail to register memory";
        return 0;
    }
    g_mrs->push_back(mr);
    return mr->lkey;
}

static void* BlockAllocate(size_t len) {
    if (len == 0) {
        errno = EINVAL;
        return NULL;
    }
    void* ptr = AllocBlock(len);

    // If the block_pool cannot allocate the memory, we try to allocate it
    // and register it on time. This may introduce high latency. So please
    // make sure your program does not rely on this frequently.
    if (!ptr) {
        ptr = g_mem_alloc(len);
        if (ptr) {
            ibv_mr* mr = IbvRegMr(g_pd, ptr, len, IBV_ACCESS_LOCAL_WRITE);
            if (!mr) {
                PLOG(ERROR) << "Fail to register memory";
                g_mem_dealloc(ptr);
                ptr = NULL;
            } else {
                BAIDU_SCOPED_LOCK(*g_addr_map_lock);
                if (!g_addr_map->insert(ptr, mr)) {
                    PLOG(ERROR) << "Fail to insert to g_addr_map";
                    IbvDeregMr(mr);
                    g_mem_dealloc(ptr);
                    ptr = NULL;
                }
            }
        }
    }

    return ptr;
}

void BlockDeallocate(void* buf) {
    if (!buf) {
        errno = EINVAL;
        return;
    }
    // This may happen after GlobalRelease
    if (DeallocBlock(buf) < 0 && errno == ERANGE) {
        {
            BAIDU_SCOPED_LOCK(*g_addr_map_lock);
            if (g_addr_map) {
                ibv_mr** mr = g_addr_map->seek(buf);
                if (mr && *mr) {
                    IbvDeregMr(*mr);
                    g_addr_map->erase(buf);
                }
            }
        }
        // Note that a block allocated before RDMA is initialized can
        // be deallocated here even if it is not in the g_addr_map.
        g_mem_dealloc(buf);
    }
}

// Parse FLAGS_rdma_cluster_prefix
static struct RdmaCluster ParseRdmaCluster(const std::string& str) {
    bool has_error = false;
    struct RdmaCluster rdma_cluster;
    rdma_cluster.mask = 0xffffffff;
    rdma_cluster.ip = 0;

    butil::StringPiece ip_str(str);
    size_t pos = str.find('/');
    int len = 32;
    uint32_t ip_addr = 0;
    if (pos != std::string::npos) {
        // Check RDMA cluster mask
        butil::StringPiece mask_str(str.c_str() + pos + 1);
        if (mask_str.length() < 1 || mask_str.length() > 2) {
            has_error = true;
        } else {
            char* end = NULL;
            len = strtol(mask_str.data(), &end, 10);
            if (*end != '\0' || len > 32 || len < 0) {
                has_error = true;
            }
        }
        ip_str.remove_suffix(mask_str.length() + 1);
    } else {
        has_error = true;
    }

    if (inet_pton(AF_INET, ip_str.as_string().c_str(), &ip_addr) <= 0) {
        has_error = true;
    } else {
        ip_addr = ntohl(ip_addr);
    }

    if (has_error || len == 0) {
        rdma_cluster.mask = 0;
    } else {
        rdma_cluster.mask <<= 32 - len;
    }

    rdma_cluster.ip = ip_addr & rdma_cluster.mask;
    if (has_error) {
        LOG(WARNING) << "RDMA cluster error (" << str
                     << "), the correct configuration should be:"
                     << "ip/mask (0<=mask<=32)";
    }
    return rdma_cluster;
}

static void OnRdmaAsyncEvent(Socket* m) {
    int progress = Socket::PROGRESS_INIT;
    do {
        ibv_async_event event;
        if (IbvGetAsyncEvent(g_context, &event) < 0) {
            break;
        }
        switch (event.event_type) {
        case IBV_EVENT_QP_FATAL: {
            SocketId sid = (SocketId)event.element.qp->qp_context;
            SocketUniquePtr s;
            if (Socket::Address(sid, &s) == 0) {
                s->SetFailed(ERDMA, "Received QP fatal error");
                LOG(WARNING) << "Receive a QP fatal error on SocketId " << sid;
            }
            // NOTE:
            // We must ack the async event here, before `s' is recycled.
            // Otherwise there will be an deadlock.
            // Please check the use of ibv_ack_async_event at:
            // http://www.rdmamojo.com/2012/08/16/ibv_ack_async_event/
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_CQ_ERR: {
            LOG(WARNING) << "CQ overruns, the connections will be stopped. "
                         << "Try to set rdma_cq_size larger.";
            IbvAckAsyncEvent(&event);
            if (g_cq_num > 0) {
                LOG(FATAL) << "We get a CQ error when we use shared CQ mode.";
                GlobalDisableRdma();
            }
        }
        default:
            break;
        }
        if (!m->MoreReadEvents(&progress)) {
            break;
        }
    } while (true);
}

#define LoadSymbol(handle, func, symbol) \
    *(void**)(&func) = dlsym(handle, symbol); \
    if (!func) { \
        LOG(ERROR) << "Fail to find symbol: " << symbol; \
        return -1; \
    }

static int ReadRdmaDynamicLib() {
    // Must load libibverbs.so before librdmacm.so
    g_handle_ibverbs = dlopen("libibverbs.so", RTLD_LAZY);
    if (!g_handle_ibverbs) {
        LOG(ERROR) << "Fail to load libibverbs.so due to " << dlerror();
        return -1;
    }

    g_handle_rdmacm = dlopen("librdmacm.so", RTLD_LAZY);
    if (!g_handle_rdmacm) {
        LOG(ERROR) << "Fail to load librdmacm.so due to " << dlerror();
        return -1;
    }

    LoadSymbol(g_handle_rdmacm, RdmaGetDevices, "rdma_get_devices");
    LoadSymbol(g_handle_rdmacm, RdmaFreeDevices, "rdma_free_devices");
    LoadSymbol(g_handle_rdmacm, RdmaCreateId, "rdma_create_id");
    LoadSymbol(g_handle_rdmacm, RdmaDestroyId, "rdma_destroy_id");
    LoadSymbol(g_handle_rdmacm, RdmaResolveAddr, "rdma_resolve_addr");
    LoadSymbol(g_handle_rdmacm, RdmaBindAddr, "rdma_bind_addr");
    LoadSymbol(g_handle_rdmacm, RdmaResolveRoute, "rdma_resolve_route");
    LoadSymbol(g_handle_rdmacm, RdmaListen, "rdma_listen");
    LoadSymbol(g_handle_rdmacm, RdmaConnect, "rdma_connect");
    LoadSymbol(g_handle_rdmacm, RdmaGetRequest, "rdma_get_request");
    LoadSymbol(g_handle_rdmacm, RdmaAccept, "rdma_accept");
    LoadSymbol(g_handle_rdmacm, RdmaDisconnect, "rdma_disconnect");
    LoadSymbol(g_handle_rdmacm, RdmaGetCmEvent, "rdma_get_cm_event");
    LoadSymbol(g_handle_rdmacm, RdmaAckCmEvent, "rdma_ack_cm_event");
    LoadSymbol(g_handle_rdmacm, RdmaCreateQp, "rdma_create_qp");

    LoadSymbol(g_handle_ibverbs, IbvForkInit, "ibv_fork_init");
    LoadSymbol(g_handle_ibverbs, IbvQueryDevice, "ibv_query_device");
    LoadSymbol(g_handle_ibverbs, IbvQueryPort, "ibv_query_port");
    LoadSymbol(g_handle_ibverbs, IbvAllocPd, "ibv_alloc_pd");
    LoadSymbol(g_handle_ibverbs, IbvDeallocPd, "ibv_dealloc_pd");
    LoadSymbol(g_handle_ibverbs, IbvCreateCq, "ibv_create_cq");
    LoadSymbol(g_handle_ibverbs, IbvDestroyCq, "ibv_destroy_cq");
    LoadSymbol(g_handle_ibverbs, IbvCreateCompChannel, "ibv_create_comp_channel");
    LoadSymbol(g_handle_ibverbs, IbvDestroyCompChannel, "ibv_destroy_comp_channel");
    LoadSymbol(g_handle_ibverbs, IbvRegMr, "ibv_reg_mr");
    LoadSymbol(g_handle_ibverbs, IbvDeregMr, "ibv_dereg_mr");
    LoadSymbol(g_handle_ibverbs, IbvGetCqEvent, "ibv_get_cq_event");
    LoadSymbol(g_handle_ibverbs, IbvAckCqEvents, "ibv_ack_cq_events");
    LoadSymbol(g_handle_ibverbs, IbvGetAsyncEvent, "ibv_get_async_event");
    LoadSymbol(g_handle_ibverbs, IbvAckAsyncEvent, "ibv_ack_async_event");
    LoadSymbol(g_handle_ibverbs, IbvDestroyQp, "ibv_destroy_qp");

    return 0;
}

static inline void ExitWithError() {
    GlobalRelease(); 
    exit(1);
}

#endif

static void GlobalRdmaInitializeOrDieImpl() {
#ifndef BRPC_RDMA
    CHECK(false) << "This libbdrpc.a does not support RDMA";
    exit(1);
#else
    if (ReadRdmaDynamicLib() < 0) { 
        LOG(ERROR) << "Fail to load rdma dynamic lib";
        ExitWithError();
    }

    // ibv_fork_init is very important. If we don't call this API,
    // we may get some very, very strange problems if the program
    // calls fork().
    if (IbvForkInit()) {
        PLOG(ERROR) << "Fail to ibv_fork_init";
        ExitWithError();
    }

    int num = 0;
    g_devices = RdmaGetDevices(&num);
    if (num == 0) {
        PLOG(ERROR) << "Fail to find rdma device";
        ExitWithError();
    }

    // Find the first active device
    // Currently we only use the first active device
    int device_index = -1;
    for (int i = 0; i < num; ++i) {
        ibv_port_attr attr;
        if (IbvQueryPort(g_devices[i], 1, &attr) < 0) {
            continue;
        }
        if (attr.state != IBV_PORT_ACTIVE) {
            continue;
        } else {
            if (device_index != -1) {
                LOG(ERROR) << "This server has more than one active RDMA port. "
                              "Since currently we do not support multiple active "
                              "ports, try to 1) disable extra ports; 2) make them "
                              "bonding together; 3) specify a port with --rdma_device.";
                ExitWithError();
            }
            device_index = i;
        }
        if (FLAGS_rdma_device.size() > 0) {
            if (strcmp(g_devices[i]->device->name, FLAGS_rdma_device.c_str()) == 0) {
                break;
            } else {
                device_index = -1;
            }
        }
    }
    if (device_index < 0) {
        LOG(ERROR) << "Fail to find active RDMA device " << FLAGS_rdma_device;
        ExitWithError();
    }
    g_context = g_devices[device_index];
    IbvCreateCompChannel(g_context);

    // Find the IP address corresponding to this device
    char* dev_path = g_context->device->ibdev_path;
    std::string dev_resource_path = butil::string_printf(
            "%s/device/resource", dev_path);
    char dev_resource[SYSFS_SIZE];
    if (ReadFile(dev_resource_path, dev_resource) < 0) {
        LOG(ERROR) << "Fail to find device sysfs at " << dev_resource_path;
        ExitWithError();
    }

    // Map ibdev (e.g. mlx5_0) to netdev (e.g. eth0), must compare sysfs file
    ifaddrs* ifap = NULL;
    ifaddrs* ifaptr = NULL;
    bool found = false;
    if (getifaddrs(&ifap) == 0) {
        for (ifaptr = ifap; ifaptr != NULL; ifaptr = ifaptr->ifa_next) {
            if (ifaptr->ifa_addr->sa_family == AF_INET) {
                sockaddr_in* ptr = (sockaddr_in*)ifaptr->ifa_addr;
                std::string net_resource_path = butil::string_printf(
                        "/sys/class/net/%s/device/resource", ifaptr->ifa_name);
                char net_resource[SYSFS_SIZE];
                ssize_t len = ReadFile(net_resource_path, net_resource);
                std::string ifa_name = butil::string_printf("%s", ifaptr->ifa_name);
                if ((memcmp(net_resource, dev_resource, len) == 0) ||
                    (ifa_name == FLAGS_network_name)) {
                    g_rdma_ip = ptr->sin_addr;
                    found = true;
                    break;
                }
            }
        }
        freeifaddrs(ifap);
    }
    if (!found) {
        LOG(WARNING) << "Fail to find address of rdma device. "
                        "Do not use 0.0.0.0/127.0.0.1 to do local RDMA connection. "
                     << "if bonding mode, must specify network name, "
                        "for example: --network_name=bond0.";
    }
    if (FLAGS_rdma_disable_local_connection) {
        LOG(INFO) << "Now local connection only uses TCP. "
                  << "Try to set rdma_disable_local_connection to false "
                  << "to allow RDMA local connection";
    }

    // Create protection domain
    g_pd = IbvAllocPd(g_context);
    if (!g_pd) {
        PLOG(ERROR) << "Fail to allocate protection domain";
        ExitWithError();
    }

    g_mrs = new (std::nothrow) std::vector<ibv_mr*>;
    if (!g_mrs) {
        PLOG(ERROR) << "Fail to allocate a RDMA MR list";
        ExitWithError();
    }

    ibv_device_attr attr;
    if (IbvQueryDevice(g_context, &attr) < 0) {
        PLOG(ERROR) << "Fail to get the device information";
        ExitWithError();
    }

    if (FLAGS_rdma_max_sge > 0) {
        // Too large sge consumes too much memory for QP
        g_max_sge = attr.max_sge < FLAGS_rdma_max_sge ?
                    attr.max_sge : FLAGS_rdma_max_sge;
    } else {
        g_max_sge = attr.max_sge;
    }

    // Initialize RDMA memory pool (block_pool)
    if (!InitBlockPool(RdmaRegisterMemory)) {
        PLOG(ERROR) << "Fail to initialize RDMA memory pool";
        ExitWithError();
    }

    if (GlobalCQInit() < 0) {
        ExitWithError();
    }

    if (RdmaEndpoint::GlobalInitialize() < 0) {
        LOG(ERROR) << "rdma_recv_block_type incorrect "
                   << "(valid value: default/large/huge)";
        ExitWithError();
    }

    g_addr_map_lock = new (std::nothrow) butil::Mutex;
    if (!g_addr_map_lock) {
        PLOG(WARNING) << "Fail to construct g_addr_map_lock";
        ExitWithError();
    }

    g_addr_map = new (std::nothrow) AddrMap;
    if (!g_addr_map) {
        PLOG(WARNING) << "Fail to construct g_addr_map";
        ExitWithError();
    }

    if (g_addr_map->init(65536) < 0) {
        PLOG(WARNING) << "Fail to initialize g_addr_map";
        ExitWithError();
    }

    SocketOptions opt;
    opt.fd = g_context->async_fd;
    butil::make_close_on_exec(opt.fd);
    if (butil::make_non_blocking(opt.fd) < 0) {
        PLOG(WARNING) << "Fail to set async_fd to nonblocking";
        ExitWithError();
    }
    opt.on_edge_triggered_events = OnRdmaAsyncEvent;
    if (Socket::Create(opt, &g_async_socket) < 0) {
        LOG(WARNING) << "Fail to create socket to get async event of RDMA";
        ExitWithError();
    }

    atexit(GlobalRelease);

    g_mem_alloc = butil::iobuf::blockmem_allocate;
    g_mem_dealloc = butil::iobuf::blockmem_deallocate;
    butil::iobuf::blockmem_allocate = BlockAllocate;
    butil::iobuf::blockmem_deallocate = BlockDeallocate;
    g_cluster = ParseRdmaCluster(FLAGS_rdma_cluster);
    g_rdma_available.store(true, butil::memory_order_relaxed);
    RdmaFallbackChannel::GlobalStart();
#endif
}

static pthread_once_t initialize_rdma_once = PTHREAD_ONCE_INIT;

void GlobalRdmaInitializeOrDie() {
    if (pthread_once(&initialize_rdma_once,
                     GlobalRdmaInitializeOrDieImpl) != 0) {
        LOG(FATAL) << "Fail to pthread_once GlobalRdmaInitializeOrDie";
        exit(1);
    }
}

bool DestinationInRdmaCluster(in_addr_t addr) {
#ifdef BRPC_RDMA
    if ((addr & g_cluster.mask) == g_cluster.ip) {
        return true;
    }
#endif
    return false;
}

// Just for UT
bool DestinationInGivenCluster(std::string prefix, in_addr_t addr) {
#ifdef BRPC_RDMA
    RdmaCluster cluster = ParseRdmaCluster(prefix);
    if ((addr & cluster.mask) == cluster.ip) {
        return true;
    }
#endif
    return false;
}

int RegisterMemoryForRdma(void* buf, size_t len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This libbdrpc.a does not support RDMA";
    return -1;
#else
    ibv_mr* mr = IbvRegMr(g_pd, buf, len, IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        return -1;
    }
    BAIDU_SCOPED_LOCK(*g_addr_map_lock);
    if (!g_addr_map->insert(buf, mr)) {
        IbvDeregMr(mr);
        return -1;
    }
    return 0;
#endif
}

void DeregisterMemoryForRdma(void* buf) {
#ifndef BRPC_RDMA
    CHECK(false) << "This libbdrpc.a does not support RDMA";
    return;
#else
    BAIDU_SCOPED_LOCK(*g_addr_map_lock);
    ibv_mr** mr = g_addr_map->seek(buf);
    if (mr && *mr) {
        IbvDeregMr(*mr);
        g_addr_map->erase(buf);
    }
#endif
}

in_addr GetRdmaIP() {
    return g_rdma_ip;
}

bool IsLocalIP(in_addr addr) {
    butil::ip_t local_ip;
    butil::str2ip("127.0.0.1", &local_ip);
    if (addr.s_addr == butil::ip2int(local_ip) ||
        addr.s_addr == 0 || addr == g_rdma_ip) {
        return true;;
    }
    return false;
}

int GetRdmaMaxSge() {
    return g_max_sge;
}

void* GetRdmaContext() {
#ifdef BRPC_RDMA
    return g_context;
#else
    return NULL;
#endif
}

void* GetRdmaProtectionDomain() {
#ifdef BRPC_RDMA
    return g_pd;
#else
    return NULL;
#endif
}

uint32_t GetLKey(const void* buf) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return 0;
#else
    uint32_t lkey = GetRegionId(buf);
    if (lkey == 0) {
        BAIDU_SCOPED_LOCK(*g_addr_map_lock);
        ibv_mr** mr = g_addr_map->seek(buf);
        if (mr && *mr) {
            lkey = (*mr)->lkey;
        }
    }
    return lkey;
#endif
}

bool IsRdmaAvailable() {
    return g_rdma_available.load(butil::memory_order_acquire);
}

void GlobalDisableRdma() {
    if (g_rdma_available.exchange(false, butil::memory_order_acquire)) {
        LOG(FATAL) << "RDMA is disabled due to some unrecoverable problem";
    }
}

bool SupportedByRdma(std::string protocol) {
    if (protocol.compare("baidu_std") == 0 ||
        protocol.compare("hulu_pbrpc") == 0 ||
        protocol.compare("sofa_pbrpc") == 0 ||
        protocol.compare("http") == 0) {
        return true;
    }
    return false;
}

}  // namespace rdma
}  // namespace brpc
