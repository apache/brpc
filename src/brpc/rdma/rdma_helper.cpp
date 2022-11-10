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

#if BRPC_WITH_RDMA

#include <dlfcn.h>                                // dlopen
#include <pthread.h>
#include <stdlib.h>
#include <vector>
#include <gflags/gflags.h>
#include "butil/containers/flat_map.h"            // butil::FlatMap
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"                     // butil::make_non_blocking
#include "butil/logging.h"
#include "brpc/socket.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_endpoint.h"
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

void* g_handle_ibverbs = NULL;
bool g_skip_rdma_init = false;

ibv_device** (*IbvGetDeviceList)(int*) = NULL;
void (*IbvFreeDeviceList)(ibv_device**) = NULL;
ibv_context* (*IbvOpenDevice)(ibv_device*) = NULL;
int (*IbvCloseDevice)(ibv_context*) = NULL;
const char* (*IbvGetDeviceName)(ibv_device*) = NULL;
int (*IbvForkInit)(void) = NULL;
int (*IbvQueryDevice)(ibv_context*, ibv_device_attr*) = NULL;
int (*IbvQueryPort)(ibv_context*, uint8_t, ibv_port_attr*) = NULL;
int (*IbvQueryGid)(ibv_context*, uint8_t, int, ibv_gid*) = NULL;
ibv_pd* (*IbvAllocPd)(ibv_context*) = NULL;
int (*IbvDeallocPd)(ibv_pd*) = NULL;
ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int) = NULL;
int (*IbvDestroyCq)(ibv_cq*) = NULL;
ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*) = NULL;
int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask) = NULL;
int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*) = NULL;
int (*IbvDestroyQp)(ibv_qp*) = NULL;
ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*) = NULL;
int (*IbvDestroyCompChannel)(ibv_comp_channel*) = NULL;
ibv_mr* (*IbvRegMr)(ibv_pd*, void*, size_t, ibv_access_flags) = NULL;
int (*IbvDeregMr)(ibv_mr*) = NULL;
int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**) = NULL;
void (*IbvAckCqEvents)(ibv_cq*, unsigned int) = NULL;
int (*IbvGetAsyncEvent)(ibv_context*, ibv_async_event*) = NULL;
void (*IbvAckAsyncEvent)(ibv_async_event*) = NULL;
const char* (*IbvEventTypeStr)(ibv_event_type) = NULL;

// NOTE:
// ibv_post_send, ibv_post_recv, ibv_poll_cq, ibv_req_notify_cq are all inline function
// defined in infiniband/verbs.h.

static int g_gid_tbl_len = 0;
static uint8_t g_gid_index = 0;
static ibv_gid g_gid;
static uint16_t g_lid;
static int g_max_sge = 0;
static uint8_t g_port_num = 1;

static int g_comp_vector_index = 0;

butil::atomic<bool> g_rdma_available(false);

DEFINE_int32(rdma_max_sge, 0, "Max SGE num in a WR");
DEFINE_string(rdma_device, "", "The name of the HCA device used "
                               "(Empty means using the first active device)");
DEFINE_int32(rdma_port, 1, "The port number to use. For RoCE, it is always 1.");
DEFINE_int32(rdma_gid_index, -1, "The GID index to use. -1 means using the last one.");

static const size_t SYSFS_SIZE = 4096;
static ibv_device** g_devices = NULL;
static ibv_context* g_context = NULL;
static SocketId g_async_socket;
static ibv_pd* g_pd = NULL;
static std::vector<ibv_mr*>* g_mrs = NULL;

// Store the original IOBuf memalloc and memdealloc functions
static void* (*g_mem_alloc)(size_t) = NULL;
static void (*g_mem_dealloc)(void*) = NULL;

butil::Mutex* g_addr_map_lock;
typedef butil::FlatMap<const void*, ibv_mr*> AddrMap;
static AddrMap* g_addr_map = NULL;  // for mr not in memory pool

static void GlobalRelease() {
    g_rdma_available.store(false, butil::memory_order_release);
    usleep(100000);  // to avoid unload library too early

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
    delete g_addr_map_lock;

    RdmaEndpoint::GlobalRelease();

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
        IbvCloseDevice(g_context);
        g_context = NULL;
    }

    if (g_devices) {
        IbvFreeDeviceList(g_devices);
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
    if (!ptr) {
        LOG(ERROR) << "Fail to get block from memory pool";
    }

    return ptr;
}

void BlockDeallocate(void* buf) {
    if (!buf) {
        errno = EINVAL;
        return;
    }
    DeallocBlock(buf);
}

static void FindRdmaLid() {
    ibv_port_attr attr;
    if (IbvQueryPort(g_context, g_port_num, &attr) < 0) {
        return;
    }
    g_lid = attr.lid;
    LOG(INFO) << "RDMA LID changes to: " << g_lid;
    return;
}

static bool FindRdmaGid(ibv_context* context) {
    bool found = false;
    for (int i = 0; i < g_gid_tbl_len; ++i) {
        ibv_gid gid;
        if (IbvQueryGid(context, g_port_num, i, &gid) < 0) {
            continue;
        }
        if (gid.global.interface_id == 0) {
            continue;
        }
        if (FLAGS_rdma_gid_index == i) {
            g_gid = gid;
            g_gid_index = i;
            return true;
        }
        // For infiniband, there is only one GID for each port.
        // For RoCE, there are 2 GIDs for each MAC and 2 GIDs for each IP.
        // Generally, the last GID is a RoCEv2-type GID generated by IP.
        g_gid = gid;
        g_gid_index = i;
        found = true;
    }
    if (FLAGS_rdma_gid_index >= 0) {
        if (g_gid_index != FLAGS_rdma_gid_index) {
            found = false;
        }
    }
    return found;
}

static void OnRdmaAsyncEvent(Socket* m) {
    int progress = Socket::PROGRESS_INIT;
    do {
        ibv_async_event event;
        if (IbvGetAsyncEvent(g_context, &event) < 0) {
            break;
        }
        LOG(WARNING) << "rdma async event: " << IbvEventTypeStr(event.event_type);
        switch (event.event_type) {
        case IBV_EVENT_QP_REQ_ERR:
        case IBV_EVENT_QP_ACCESS_ERR:
        case IBV_EVENT_QP_FATAL: {
            SocketId sid = (SocketId)event.element.qp->qp_context;
            SocketUniquePtr s;
            if (Socket::Address(sid, &s) == 0) {
                s->SetFailed(ERDMA, "QP fatal error");
                LOG(WARNING) << "Receive a QP fatal error on " << s->description();
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
            LOG(WARNING) << "CQ overruns, the connection will be stopped.";
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_COMM_EST:
        case IBV_EVENT_SQ_DRAINED:
        case IBV_EVENT_QP_LAST_WQE_REACHED: {
            // just ignore the event
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_SRQ_ERR:
        case IBV_EVENT_SRQ_LIMIT_REACHED: {
            // SRQ not used, should not happen
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_LID_CHANGE: {
            FindRdmaLid();
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_PATH_MIG:
        case IBV_EVENT_PATH_MIG_ERR:
        case IBV_EVENT_PKEY_CHANGE:
        case IBV_EVENT_SM_CHANGE:
        case IBV_EVENT_CLIENT_REREGISTER: {
            // for IB only, we haven't test these events carefully
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_PORT_ACTIVE:
        case IBV_EVENT_PORT_ERR: {
            // Port up/down will lead these two events.
            // The port error is recoverable.
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_GID_CHANGE: {
            FindRdmaGid(g_context);
            IbvAckAsyncEvent(&event);
            break;
        }
        case IBV_EVENT_DEVICE_FATAL: {
            // because the memory resources are related to rdma device
            // we view this error unrecoverable
            GlobalDisableRdma();
            IbvAckAsyncEvent(&event);
            break;
        }
        default:
            // should not hannen
            IbvAckAsyncEvent(&event);
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
    g_handle_ibverbs = dlopen("libibverbs.so", RTLD_LAZY);
    if (!g_handle_ibverbs) {
        LOG(WARNING) << "Failed to load libibverbs.so " << dlerror() << " try libibverbs.so.1";
        // Clear existing error
        dlerror();
        g_handle_ibverbs = dlopen("libibverbs.so.1", RTLD_LAZY);
        if (!g_handle_ibverbs) {
            LOG(ERROR) << "Fail to load libibverbs.so.1 due to " << dlerror();
            return -1;
        }
    }

    LoadSymbol(g_handle_ibverbs, IbvGetDeviceList, "ibv_get_device_list");
    LoadSymbol(g_handle_ibverbs, IbvFreeDeviceList, "ibv_free_device_list");
    LoadSymbol(g_handle_ibverbs, IbvOpenDevice, "ibv_open_device");
    LoadSymbol(g_handle_ibverbs, IbvCloseDevice, "ibv_close_device");
    LoadSymbol(g_handle_ibverbs, IbvGetDeviceName, "ibv_get_device_name");
    LoadSymbol(g_handle_ibverbs, IbvForkInit, "ibv_fork_init");
    LoadSymbol(g_handle_ibverbs, IbvQueryDevice, "ibv_query_device");
    LoadSymbol(g_handle_ibverbs, IbvQueryPort, "ibv_query_port");
    LoadSymbol(g_handle_ibverbs, IbvQueryGid, "ibv_query_gid");
    LoadSymbol(g_handle_ibverbs, IbvAllocPd, "ibv_alloc_pd");
    LoadSymbol(g_handle_ibverbs, IbvDeallocPd, "ibv_dealloc_pd");
    LoadSymbol(g_handle_ibverbs, IbvCreateCq, "ibv_create_cq");
    LoadSymbol(g_handle_ibverbs, IbvDestroyCq, "ibv_destroy_cq");
    LoadSymbol(g_handle_ibverbs, IbvCreateQp, "ibv_create_qp");
    LoadSymbol(g_handle_ibverbs, IbvModifyQp, "ibv_modify_qp");
    LoadSymbol(g_handle_ibverbs, IbvQueryQp, "ibv_query_qp");
    LoadSymbol(g_handle_ibverbs, IbvDestroyQp, "ibv_destroy_qp");
    LoadSymbol(g_handle_ibverbs, IbvCreateCompChannel, "ibv_create_comp_channel");
    LoadSymbol(g_handle_ibverbs, IbvDestroyCompChannel, "ibv_destroy_comp_channel");
    LoadSymbol(g_handle_ibverbs, IbvRegMr, "ibv_reg_mr");
    LoadSymbol(g_handle_ibverbs, IbvDeregMr, "ibv_dereg_mr");
    LoadSymbol(g_handle_ibverbs, IbvGetCqEvent, "ibv_get_cq_event");
    LoadSymbol(g_handle_ibverbs, IbvAckCqEvents, "ibv_ack_cq_events");
    LoadSymbol(g_handle_ibverbs, IbvGetAsyncEvent, "ibv_get_async_event");
    LoadSymbol(g_handle_ibverbs, IbvAckAsyncEvent, "ibv_ack_async_event");
    LoadSymbol(g_handle_ibverbs, IbvEventTypeStr, "ibv_event_type_str");

    return 0;
}

static inline void ExitWithError() {
    GlobalRelease(); 
    exit(1);
}

static void GlobalRdmaInitializeOrDieImpl() {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        return;
    }

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
    g_devices = IbvGetDeviceList(&num);
    if (num == 0) {
        LOG(ERROR) << "Fail to find rdma device";
        ExitWithError();
    }

    // Find the first active port
    int available_devices = 0;
    g_port_num = FLAGS_rdma_port;
    for (int i = 0; i < num; ++i) {
        ibv_context* context = IbvOpenDevice(g_devices[i]);
        if (!context) {
            PLOG(ERROR) << "Fail to open rdma device " << IbvGetDeviceName(g_devices[i]);
            ExitWithError();
        }
        ibv_port_attr attr;
        if (IbvQueryPort(context, g_port_num, &attr) < 0) {
            PLOG(WARNING) << "Fail to query port " << g_port_num 
                          << " on " << IbvGetDeviceName(g_devices[i]);
            if (FLAGS_rdma_device.size() > 0) {
                ExitWithError();
            }
            IbvCloseDevice(context);
            continue;
        }
        if (attr.state != IBV_PORT_ACTIVE) {
            IbvCloseDevice(context);
            continue;
        }
        if (FLAGS_rdma_device.size() > 0) {
            if (strcmp(context->device->name, FLAGS_rdma_device.c_str()) == 0) {
                ++available_devices;
                g_context = context;
                g_gid_tbl_len = attr.gid_tbl_len;
                g_lid = attr.lid;
                break;
            }
        } else {
            g_context = context;
            g_gid_tbl_len = attr.gid_tbl_len;
            g_lid = attr.lid;
            ++available_devices;
        }
    }
    if (!g_context) {
        LOG(ERROR) << "Fail to find available RDMA device " << FLAGS_rdma_device;
        ExitWithError();
    }
    if (available_devices > 1 && FLAGS_rdma_device.size() == 0) {
        LOG(INFO) << "This server has more than one available RDMA device. Only "
                  << "the first one (" << g_context->device->name
                  << ") will be used. If you want to use other device, please "
                  << "specify it with --rdma_device.";
    } else {
        LOG(INFO) << "RDMA device: " << g_context->device->name;
    }
    LOG(INFO) << "RDMA LID: " << g_lid;
    if (!FindRdmaGid(g_context)) {
        LOG(ERROR) << "Fail to find available RDMA GID";
        ExitWithError();
    } else {
        LOG(INFO) << "RDMA GID Index: " << (int)g_gid_index;
    }
    IbvCreateCompChannel(g_context);

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
    // Too large sge consumes too much memory for QP
    if (FLAGS_rdma_max_sge > 0) {
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
    g_rdma_available.store(true, butil::memory_order_relaxed);
}

static pthread_once_t initialize_rdma_once = PTHREAD_ONCE_INIT;

void GlobalRdmaInitializeOrDie() {
    if (pthread_once(&initialize_rdma_once,
                     GlobalRdmaInitializeOrDieImpl) != 0) {
        LOG(FATAL) << "Fail to pthread_once GlobalRdmaInitializeOrDie";
        exit(1);
    }
}

int RegisterMemoryForRdma(void* buf, size_t len) {
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
}

void DeregisterMemoryForRdma(void* buf) {
    BAIDU_SCOPED_LOCK(*g_addr_map_lock);
    ibv_mr** mr = g_addr_map->seek(buf);
    if (mr && *mr) {
        IbvDeregMr(*mr);
        g_addr_map->erase(buf);
    }
}

int GetRdmaMaxSge() {
    return g_max_sge;
}

int GetRdmaCompVector() {
    if (!g_context) {
        return 0;
    }
    // g_comp_vector_index is not an atomic variable. If more than
    // one CQ is created at the same time, some CQs will share the
    // same index. However, this vector is only used to assign a
    // event queue for the CQ. Sharing the same event queue is not
    // a problem.
    return (g_comp_vector_index++) % g_context->num_comp_vectors;
}

ibv_context* GetRdmaContext() {
    return g_context;
}

ibv_pd* GetRdmaPd() {
    return g_pd;
}

uint32_t GetLKey(const void* buf) {
    uint32_t lkey = GetRegionId(buf);
    if (lkey == 0) {
        BAIDU_SCOPED_LOCK(*g_addr_map_lock);
        ibv_mr** mr = g_addr_map->seek(buf);
        if (mr && *mr) {
            lkey = (*mr)->lkey;
        }
    }
    return lkey;
}

ibv_gid GetRdmaGid() {
    return g_gid;
}

uint16_t GetRdmaLid() {
    return g_lid;
}

uint8_t GetRdmaGidIndex() {
    return g_gid_index;
}

uint8_t GetRdmaPortNum() {
    return g_port_num;
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
    if (protocol.compare("baidu_std") == 0) {
        // Since rdma is used for high performance scenario,
        // we consider baidu_std for the only protocol to support.
        return true;
    }
    return false;
}

}  // namespace rdma
}  // namespace brpc

#else

#include <stdlib.h>
#include "butil/logging.h"

namespace brpc {
namespace rdma {
void GlobalRdmaInitializeOrDie() {
    LOG(ERROR) << "brpc is not compiled with rdma. To enable it, please refer to "
               << "https://github.com/apache/incubator-brpc/blob/master/docs/en/rdma.md";
    exit(1);
}
}
}

#endif  // if BRPC_WITH_RDMA
