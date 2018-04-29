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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)
 
#ifdef BRPC_RDMA

#include <ifaddrs.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <butil/logging.h>
#include <butil/thread_local.h>
#include <gflags/gflags.h>
#include "brpc/rdma/tcmpool/memory_pool.h"
#include "brpc/rdma/rdma_global.h"

namespace butil {
namespace iobuf {
// declared in iobuf.cpp
extern void* (*blockmem_allocate)(size_t);
extern void  (*blockmem_deallocate)(void*);
}
}

namespace brpc {
namespace rdma {

// The size of the memory pool
DEFINE_int32(rdma_preregister_memory_size_mb, 1024,
             "Pre-Registered memory size for rdma use (in MB)");

static const size_t SYSFS_SIZE = 4096;
static ibv_context** s_context = NULL;
static ibv_pd* s_pd = NULL;
static ibv_mr* s_mr = NULL;
static pthread_once_t s_mr_once = PTHREAD_ONCE_INIT;
static size_t core_num = (size_t)sysconf(_SC_NPROCESSORS_ONLN);
static in_addr s_rdma_ip;

static inline bool RdmaSupported() {
    return s_mr != NULL;
}

static void ReleaseResouces() {
    if (s_mr) {
        ibv_dereg_mr(s_mr);
        s_mr = NULL;
    }
    if (s_pd) {
        ibv_dealloc_pd(s_pd);
        s_pd = NULL;
    }
    if (s_context) {
        rdma_free_devices(s_context);
        s_context = NULL;
    }
}

// read the sysfs file at the path
static char* ReadFile(char* path, int& len) {
    FILE* pfile = fopen(path, "rb");  // binary read
    if (pfile == NULL) {
        return NULL;
    }
    size_t length = SYSFS_SIZE;
    char* data = (char*)malloc(length * sizeof(char));
    size_t nr = fread(data, 1, length, pfile);
    if (nr > length) {
        fclose(pfile);
        free(data);
        return NULL;
    }
    fclose(pfile);
    len = nr;
    return data;
}  

void InitializeGlobalRdmaBuffer() {
    if (ibv_fork_init()) {
        PLOG(ERROR) << "Fail to ibv_fork_init";
        return;
    }

    int num = 0;
    s_context = rdma_get_devices(&num);
    if (num == 0) {
        PLOG(ERROR) << "Fail to find rdma device does not exist";
        return;
    }

    // find the first active device
    // currently we only use the first active device
    int device_index = -1;
    for (int i = 0; i < num; i++) {
        ibv_port_attr attr;
        if (ibv_query_port(s_context[i], 1, &attr)) {
            PLOG(ERROR) << "Fail to query device port";
            ReleaseResouces();
            return;
        }
        if (attr.state == IBV_PORT_ACTIVE) {
            device_index = i;
            break;
        }
    }
    if (device_index < 0) {
        LOG(ERROR) << "Fail to find active device port";
        ReleaseResouces();
        return;
    }

    // find the ip address corresponding to this device
    char* dev_path = s_context[device_index]->device->ibdev_path;
    if (strlen(dev_path) > 1000) {
        LOG(ERROR) << "Fail to find device sysfs path";
        ReleaseResouces();
        return;
    }
    char dev_resource_path[1024];
    strcpy(dev_resource_path, dev_path);
    strcat(dev_resource_path, "/device/resource");
    int dev_resource_len = 0;
    char* dev_resource = ReadFile(dev_resource_path, dev_resource_len);
    if (!dev_resource) {
        LOG(ERROR) << "Fail to find device sysfs path";
        ReleaseResouces();
        return;
    }

    // map ibdev (e.g. mlx5_0) to netdev (e.g. eth0)
    // must compare sysfs file
    ifaddrs* ifap = NULL;
    ifaddrs* ifaptr = NULL;
    if (getifaddrs(&ifap) == 0) {
        for (ifaptr = ifap; ifaptr != NULL; ifaptr = ifaptr->ifa_next) {
            if (ifaptr->ifa_addr->sa_family == AF_INET) {
                sockaddr_in* ptr = (sockaddr_in*)ifaptr->ifa_addr;
                char net_resource_path[1024];
                strcpy(net_resource_path, "/sys/class/net/");
                strcat(net_resource_path, ifaptr->ifa_name);
                strcat(net_resource_path, "/device/resource");
                int net_resource_len = 0;
                char* net_resource = ReadFile(net_resource_path,
                                              net_resource_len);
                if (!net_resource) {
                    continue;
                } 
                if (net_resource_len == dev_resource_len &&
                        memcmp(net_resource, dev_resource,
                               dev_resource_len) == 0) {
                    s_rdma_ip = ptr->sin_addr;
                    free(net_resource);
                    break;
                }
                free(net_resource);
            }
        }
        freeifaddrs(ifap);
    }
    if (dev_resource) {
        free(dev_resource);
    }

    // create protection domain
    s_pd = ibv_alloc_pd(s_context[device_index]);
    if (!s_pd) {
        PLOG(ERROR) << "Fail to allocate protection domain";
        ReleaseResouces();
        return;
    }

    // allocate the memory from tc-mpool
    size_t size = (uint64_t)FLAGS_rdma_preregister_memory_size_mb * 1048576;
    rdma::tcmpool::init_memory_pool(size);
    void* pool = rdma::tcmpool::get_memory_pool_addr();
    if (!pool) {
        PLOG(ERROR) << "Fail to allocate pre-registered memory";
        ReleaseResouces();
        return;
    }

    // register the memory
    s_mr = ibv_reg_mr(s_pd, pool, size,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                      IBV_ACCESS_REMOTE_READ);
    if (!s_mr) {
        PLOG(ERROR) << "Fail to register pre-registered memory";
        ReleaseResouces();
        return;
    }

    butil::thread_atexit(ReleaseResouces);

    butil::iobuf::blockmem_allocate = rdma::tcmpool::alloc;
    butil::iobuf::blockmem_deallocate = rdma::tcmpool::dealloc;
    srand((uint32_t)butil::gettimeofday_us());
}

ibv_mr* GetGlobalRdmaBuffer() {
    pthread_once(&s_mr_once, InitializeGlobalRdmaBuffer);
    return s_mr;
}

void* GlobalRdmaAllocate(size_t len) {
    pthread_once(&s_mr_once, InitializeGlobalRdmaBuffer);
    if (!RdmaSupported()) {
        return NULL;
    }
    return rdma::tcmpool::alloc(len);
}

void GlobalRdmaDeallocate(void* buf) {
    if (!RdmaSupported()) {
        return;
    }
    rdma::tcmpool::dealloc(buf);
}

ibv_pd* GetGlobalRdmaProtectionDomain() {
    pthread_once(&s_mr_once, InitializeGlobalRdmaBuffer);
    if (!RdmaSupported()) {
        return NULL;
    }
    return s_pd;
}

int GetGlobalCpuAffinity() {
    pthread_once(&s_mr_once, InitializeGlobalRdmaBuffer);
    if (!RdmaSupported()) {
        return 0;
    }
    return rand() % core_num;
}

in_addr GetGlobalIpAddress() {
    return s_rdma_ip;
}

}  // namespace rdma
}  // namespace brpc

#endif

